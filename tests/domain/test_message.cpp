#include "holonight_domain/message.h"

#include <QDateTime>
#include <QString>
#include <QTimeZone>

#include <gtest/gtest.h>
#include <set>

namespace holonight_domain {
namespace {

TEST(MessageId, GenerateProducesUniqueValues) {
  std::set<QString> seen;
  for (int i = 0; i < 1000; ++i) {
    seen.insert(MessageId::generate().toString());
  }
  EXPECT_EQ(seen.size(), 1000U);
}

TEST(MessageId, FromStringRoundTripsToString) {
  const MessageId original = MessageId::generate();
  const MessageId reconstructed = MessageId::fromString(original.toString());

  EXPECT_EQ(reconstructed, original);
  EXPECT_EQ(reconstructed.toString(), original.toString());
}

TEST(Message, ConstructionAndAccessors) {
  const MessageId messageId = MessageId::generate();
  const QDateTime createdAt(QDate(2026, 7, 26), QTime(12, 34, 56), QTimeZone::UTC);
  const Message message(messageId, MessageRole::User, QString("hello"), MessageStatus::Streaming, createdAt);

  EXPECT_EQ(message.id(), messageId);
  EXPECT_EQ(message.role(), MessageRole::User);
  EXPECT_EQ(message.text(), QString("hello"));
  EXPECT_EQ(message.status(), MessageStatus::Streaming);
  EXPECT_EQ(message.createdAt(), createdAt);
}

TEST(Message, OmittedTimestampCapturesCurrentUtcTime) {
  const QDateTime before = QDateTime::currentDateTimeUtc();
  const Message message(MessageId::generate(), MessageRole::User, QString("hello"));
  const QDateTime after = QDateTime::currentDateTimeUtc();

  EXPECT_EQ(message.createdAt().timeSpec(), Qt::UTC);
  EXPECT_GE(message.createdAt(), before);
  EXPECT_LE(message.createdAt(), after);
}

TEST(Message, DefaultStatusIsPending) {
  const Message message(MessageId::generate(), MessageRole::Assistant, QString("hi"));
  EXPECT_EQ(message.status(), MessageStatus::Pending);
}

TEST(Message, AssistantRetainsModelId) {
  const ModelId model{.provider_id = QStringLiteral("ollama"), .model_name = QStringLiteral("llama3")};
  const Message message(MessageId::generate(), MessageRole::Assistant, QString("hi"), MessageStatus::Complete,
                        QDateTime{}, model);

  ASSERT_TRUE(message.modelId().has_value());
  EXPECT_EQ(*message.modelId(), model);
}

TEST(Message, NonAssistantRolesDiscardModelId) {
  const ModelId model{.provider_id = QStringLiteral("ollama"), .model_name = QStringLiteral("llama3")};
  const Message user(MessageId::generate(), MessageRole::User, QString("hi"), MessageStatus::Complete, QDateTime{},
                     model);
  Message system(MessageId::generate(), MessageRole::System, QString("instructions"));

  system.setModelId(model);

  EXPECT_FALSE(user.modelId().has_value());
  EXPECT_FALSE(system.modelId().has_value());
}

TEST(Message, EqualityComparesAllFields) {
  const MessageId messageId = MessageId::generate();
  const QDateTime createdAt(QDate(2026, 7, 26), QTime(12, 34, 56), QTimeZone::UTC);
  const Message lhs(messageId, MessageRole::User, QString("hello"), MessageStatus::Pending, createdAt);
  const Message rhs(messageId, MessageRole::User, QString("hello"), MessageStatus::Pending, createdAt);
  EXPECT_EQ(lhs, rhs);

  const Message differentText(messageId, MessageRole::User, QString("other"), MessageStatus::Pending, createdAt);
  EXPECT_NE(lhs, differentText);

  const Message differentTimestamp(messageId, MessageRole::User, QString("hello"), MessageStatus::Pending,
                                   createdAt.addSecs(1));
  EXPECT_NE(lhs, differentTimestamp);
}

TEST(Message, ValidTransitionPendingToStreaming) {
  Message message(MessageId::generate(), MessageRole::User, QString(""));
  const auto result = message.transitionTo(MessageStatus::Streaming);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(message.status(), MessageStatus::Streaming);
}

TEST(Message, ValidTransitionStreamingToComplete) {
  Message message(MessageId::generate(), MessageRole::User, QString(""));
  ASSERT_TRUE(message.transitionTo(MessageStatus::Streaming).has_value());
  const auto result = message.transitionTo(MessageStatus::Complete);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(message.status(), MessageStatus::Complete);
}

TEST(Message, ValidTransitionStreamingToError) {
  Message message(MessageId::generate(), MessageRole::User, QString(""));
  ASSERT_TRUE(message.transitionTo(MessageStatus::Streaming).has_value());
  const auto result = message.transitionTo(MessageStatus::Error);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(message.status(), MessageStatus::Error);
}

TEST(Message, ValidTransitionStreamingToCancelled) {
  Message message(MessageId::generate(), MessageRole::User, QString(""));
  ASSERT_TRUE(message.transitionTo(MessageStatus::Streaming).has_value());
  const auto result = message.transitionTo(MessageStatus::Cancelled);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(message.status(), MessageStatus::Cancelled);
}

TEST(Message, InvalidTransitionPendingToComplete) {
  Message message(MessageId::generate(), MessageRole::User, QString(""));
  const auto result = message.transitionTo(MessageStatus::Complete);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().from, MessageStatus::Pending);
  EXPECT_EQ(result.error().attempted, MessageStatus::Complete);
  EXPECT_EQ(message.status(), MessageStatus::Pending);
}

TEST(Message, InvalidTransitionPendingToError) {
  Message message(MessageId::generate(), MessageRole::User, QString(""));
  const auto result = message.transitionTo(MessageStatus::Error);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(message.status(), MessageStatus::Pending);
}

TEST(Message, InvalidTransitionPendingToCancelled) {
  Message message(MessageId::generate(), MessageRole::User, QString(""));
  const auto result = message.transitionTo(MessageStatus::Cancelled);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(message.status(), MessageStatus::Pending);
}

TEST(Message, InvalidTransitionCompleteToStreaming) {
  Message message(MessageId::generate(), MessageRole::User, QString(""));
  ASSERT_TRUE(message.transitionTo(MessageStatus::Streaming).has_value());
  ASSERT_TRUE(message.transitionTo(MessageStatus::Complete).has_value());
  const auto result = message.transitionTo(MessageStatus::Streaming);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(message.status(), MessageStatus::Complete);
}

TEST(Message, InvalidTransitionErrorToStreaming) {
  Message message(MessageId::generate(), MessageRole::User, QString(""));
  ASSERT_TRUE(message.transitionTo(MessageStatus::Streaming).has_value());
  ASSERT_TRUE(message.transitionTo(MessageStatus::Error).has_value());
  const auto result = message.transitionTo(MessageStatus::Streaming);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(message.status(), MessageStatus::Error);
}

TEST(Message, SetTextReplacesTextWithoutAffectingStatus) {
  const QDateTime createdAt(QDate(2026, 7, 26), QTime(12, 34, 56), QTimeZone::UTC);
  Message message(MessageId::generate(), MessageRole::Assistant, QString("partial"), MessageStatus::Streaming,
                  createdAt);
  message.setText(QString("partial more"));
  EXPECT_EQ(message.text(), QString("partial more"));
  EXPECT_EQ(message.status(), MessageStatus::Streaming);
  EXPECT_EQ(message.createdAt(), createdAt);
}

TEST(Message, StatusTransitionPreservesTimestamp) {
  const QDateTime createdAt(QDate(2026, 7, 26), QTime(12, 34, 56), QTimeZone::UTC);
  Message message(MessageId::generate(), MessageRole::Assistant, QString(), MessageStatus::Pending, createdAt);

  ASSERT_TRUE(message.transitionTo(MessageStatus::Streaming).has_value());
  EXPECT_EQ(message.createdAt(), createdAt);
}

TEST(Message, InvalidTransitionCancelledToStreaming) {
  Message message(MessageId::generate(), MessageRole::User, QString(""));
  ASSERT_TRUE(message.transitionTo(MessageStatus::Streaming).has_value());
  ASSERT_TRUE(message.transitionTo(MessageStatus::Cancelled).has_value());
  const auto result = message.transitionTo(MessageStatus::Streaming);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(message.status(), MessageStatus::Cancelled);
}

}  // namespace
}  // namespace holonight_domain
