#include "holonight_persistence/detail/conversation_repository_worker.h"
#include "test_support.h"

#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>

#include <gtest/gtest.h>
#include <holonight_domain/holonight_domain.h>

namespace holonight_persistence::detail {
namespace {

using holonight_domain::Message;
using holonight_domain::MessageId;
using holonight_domain::MessageRole;
using holonight_domain::MessageStatus;
using holonight_domain::Usage;

class UsagePersistenceTest : public ::testing::Test {
 protected:
  void TearDown() override {
    for (const QString& connectionName : connection_names_) {
      QSqlDatabase::database(connectionName, false).close();
      QSqlDatabase::removeDatabase(connectionName);
    }
  }

  [[nodiscard]] QString newConnectionName() {
    connection_names_.append(uniqueTestConnectionName());
    return connection_names_.back();
  }

  [[nodiscard]] static QString createConversation(ConversationRepositoryWorker& worker) {
    QSignalSpy created(&worker, &ConversationRepositoryWorker::conversationCreated);
    worker.createConversation(QStringLiteral("Test"));
    return created.at(0).at(0).value<ConversationSummary>().id;
  }

 private:
  QList<QString> connection_names_;
};

TEST_F(UsagePersistenceTest, PersistUsageRoundTripsAllFieldsIncludingNulls) {
  const QString connectionName = newConnectionName();
  ConversationRepositoryWorker worker(QStringLiteral(":memory:"), connectionName);
  worker.openAndMigrate();
  const QString conversationId = createConversation(worker);

  const MessageId assistantId = MessageId::generate();
  worker.persistNewMessage(conversationId,
                           Message(assistantId, MessageRole::Assistant, QStringLiteral("hi"), MessageStatus::Complete));

  Usage usage;
  usage.input_tokens = 50;
  usage.output_tokens = 25;
  usage.duration_ms = 1500;
  // reasoning_tokens/cache_creation_tokens/cache_read_tokens/total_tokens intentionally left unset.
  worker.persistUsage(conversationId, assistantId.toString(), QStringLiteral("gpt-4-turbo-2024-04-09"), usage);

  QSqlQuery query(QSqlDatabase::database(connectionName));
  ASSERT_TRUE(query.exec(QStringLiteral(
      "SELECT message_id, conversation_id, model_identifier, input_tokens, output_tokens, reasoning_tokens, "
      "cache_creation_tokens, cache_read_tokens, total_tokens, duration_ms FROM usage")));
  ASSERT_TRUE(query.next());
  EXPECT_EQ(query.value(0).toString(), assistantId.toString());
  EXPECT_EQ(query.value(1).toString(), conversationId);
  EXPECT_EQ(query.value(2).toString(), QStringLiteral("gpt-4-turbo-2024-04-09"));
  EXPECT_EQ(query.value(3).toInt(), 50);
  EXPECT_EQ(query.value(4).toInt(), 25);
  EXPECT_TRUE(query.value(5).isNull());
  EXPECT_TRUE(query.value(6).isNull());
  EXPECT_TRUE(query.value(7).isNull());
  EXPECT_TRUE(query.value(8).isNull());
  EXPECT_EQ(query.value(9).toLongLong(), 1500);
  EXPECT_FALSE(query.next());
}

TEST_F(UsagePersistenceTest, OneUsageRowPerAssistantMessage) {
  const QString connectionName = newConnectionName();
  ConversationRepositoryWorker worker(QStringLiteral(":memory:"), connectionName);
  worker.openAndMigrate();
  const QString conversationId = createConversation(worker);

  constexpr int kTurns = 3;
  for (int i = 0; i < kTurns; ++i) {
    worker.persistNewMessage(conversationId, Message(MessageId::generate(), MessageRole::User, QStringLiteral("hi"),
                                                     MessageStatus::Complete));

    const MessageId assistantId = MessageId::generate();
    worker.persistNewMessage(
        conversationId, Message(assistantId, MessageRole::Assistant, QStringLiteral("hello"), MessageStatus::Complete));
    Usage usage;
    usage.input_tokens = 10;
    usage.output_tokens = 5;
    worker.persistUsage(conversationId, assistantId.toString(), QStringLiteral("gpt-4-turbo-2024-04-09"), usage);
  }

  QSqlQuery countQuery(QSqlDatabase::database(connectionName));
  countQuery.prepare(QStringLiteral("SELECT COUNT(*) FROM usage WHERE conversation_id = :id"));
  countQuery.bindValue(QStringLiteral(":id"), conversationId);
  ASSERT_TRUE(countQuery.exec());
  ASSERT_TRUE(countQuery.next());
  EXPECT_EQ(countQuery.value(0).toInt(), kTurns);
}

TEST_F(UsagePersistenceTest, RepeatedUsageForRegeneratedMessageReplacesExistingRow) {
  const QString connectionName = newConnectionName();
  ConversationRepositoryWorker worker(QStringLiteral(":memory:"), connectionName);
  worker.openAndMigrate();
  const QString conversationId = createConversation(worker);

  const MessageId assistantId = MessageId::generate();
  worker.persistNewMessage(conversationId,
                           Message(assistantId, MessageRole::Assistant, QStringLiteral("hi"), MessageStatus::Complete));
  Usage timedOutUsage;
  timedOutUsage.duration_ms = 30'000;
  worker.persistUsage(conversationId, assistantId.toString(), QStringLiteral("o4-mini"), timedOutUsage);

  Usage successfulUsage;
  successfulUsage.input_tokens = 100;
  successfulUsage.output_tokens = 40;
  successfulUsage.duration_ms = 2'000;
  worker.persistUsage(conversationId, assistantId.toString(), QStringLiteral("o4-mini-2025-04-16"), successfulUsage);

  QSqlQuery query(QSqlDatabase::database(connectionName));
  query.prepare(QStringLiteral(
      "SELECT COUNT(*), model_identifier, input_tokens, output_tokens, duration_ms FROM usage WHERE message_id = :id"));
  query.bindValue(QStringLiteral(":id"), assistantId.toString());
  ASSERT_TRUE(query.exec());
  ASSERT_TRUE(query.next());
  EXPECT_EQ(query.value(0).toInt(), 1);
  EXPECT_EQ(query.value(1).toString(), QStringLiteral("o4-mini-2025-04-16"));
  EXPECT_EQ(query.value(2).toInt(), 100);
  EXPECT_EQ(query.value(3).toInt(), 40);
  EXPECT_EQ(query.value(4).toLongLong(), 2'000);
}

// T-020: usageForConversation() batch query -- returned QMap must contain all and only the usage
// records for the requested conversation, keyed by message ID (asserted via key lookup, not
// iteration order, since QMap iterates in key-sort order which is incidental, not contractual).
TEST_F(UsagePersistenceTest, UsageForConversationReturnsAllAndOnlyMatchingRecords) {
  const QString connectionName = newConnectionName();
  ConversationRepositoryWorker worker(QStringLiteral(":memory:"), connectionName);
  worker.openAndMigrate();
  const QString targetConversationId = createConversation(worker);
  const QString otherConversationId = createConversation(worker);

  const MessageId firstAssistantId = MessageId::generate();
  worker.persistNewMessage(targetConversationId, Message(firstAssistantId, MessageRole::Assistant, QStringLiteral("a"),
                                                         MessageStatus::Complete));
  Usage firstUsage;
  firstUsage.input_tokens = 100;
  firstUsage.output_tokens = 20;
  worker.persistUsage(targetConversationId, firstAssistantId.toString(), QStringLiteral("gpt-4-turbo-2024-04-09"),
                      firstUsage);

  const MessageId secondAssistantId = MessageId::generate();
  worker.persistNewMessage(targetConversationId, Message(secondAssistantId, MessageRole::Assistant, QStringLiteral("b"),
                                                         MessageStatus::Complete));
  Usage secondUsage;
  secondUsage.input_tokens = 130;
  secondUsage.output_tokens = 25;
  worker.persistUsage(targetConversationId, secondAssistantId.toString(), QStringLiteral("gpt-4-turbo-2024-04-09"),
                      secondUsage);

  // A usage row in a different conversation must never leak into the target conversation's result.
  const MessageId otherAssistantId = MessageId::generate();
  worker.persistNewMessage(otherConversationId, Message(otherAssistantId, MessageRole::Assistant, QStringLiteral("c"),
                                                        MessageStatus::Complete));
  Usage otherUsage;
  otherUsage.input_tokens = 999;
  worker.persistUsage(otherConversationId, otherAssistantId.toString(), QStringLiteral("gpt-4-turbo-2024-04-09"),
                      otherUsage);

  QSignalSpy loadedSpy(&worker, &ConversationRepositoryWorker::usageForConversationLoaded);
  worker.usageForConversation(targetConversationId);
  ASSERT_EQ(loadedSpy.count(), 1);
  EXPECT_EQ(loadedSpy.at(0).at(0).toString(), targetConversationId);
  const auto usageByMessageId = loadedSpy.at(0).at(1).value<QMap<QString, holonight_persistence::UsageRecord>>();

  EXPECT_EQ(usageByMessageId.size(), 2);
  ASSERT_TRUE(usageByMessageId.contains(firstAssistantId.toString()));
  ASSERT_TRUE(usageByMessageId.contains(secondAssistantId.toString()));
  EXPECT_FALSE(usageByMessageId.contains(otherAssistantId.toString()));

  const auto& firstRecord = usageByMessageId.value(firstAssistantId.toString());
  EXPECT_EQ(firstRecord.conversation_id, targetConversationId);
  EXPECT_EQ(firstRecord.message_id, firstAssistantId.toString());
  ASSERT_TRUE(firstRecord.input_tokens.has_value());
  EXPECT_EQ(firstRecord.input_tokens.value(), 100);
  ASSERT_TRUE(firstRecord.output_tokens.has_value());
  EXPECT_EQ(firstRecord.output_tokens.value(), 20);

  const auto& secondRecord = usageByMessageId.value(secondAssistantId.toString());
  ASSERT_TRUE(secondRecord.input_tokens.has_value());
  EXPECT_EQ(secondRecord.input_tokens.value(), 130);
}

}  // namespace
}  // namespace holonight_persistence::detail
