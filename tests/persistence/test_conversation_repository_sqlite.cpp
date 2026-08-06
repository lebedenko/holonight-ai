#include "holonight_persistence/migration_runner.h"
#include "holonight_persistence/sqlite_conversation_repository.h"
#include "test_support.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QTemporaryDir>
#include <QTimeZone>

#include <gtest/gtest.h>
#include <holonight_domain/holonight_domain.h>

namespace holonight_persistence {
namespace {

using holonight_domain::Message;
using holonight_domain::MessageId;
using holonight_domain::MessageRole;
using holonight_domain::MessageStatus;
using holonight_domain::ModelId;
using holonight_domain::TitleSource;
using holonight_domain::ToolExecutionLocation;
using holonight_domain::ToolInvocationStatus;

struct InitializedFixture {
  SqliteConversationRepository repository{QStringLiteral(":memory:")};

  InitializedFixture() {
    QSignalSpy initializedSpy(&repository, &ConversationRepository::initialized);
    repository.initialize();
    initializedSpy.wait(1000);
  }
};

TEST(SqliteConversationRepository, InitializeEmitsInitializedForInMemoryDatabase) {
  SqliteConversationRepository repository{QStringLiteral(":memory:")};
  QSignalSpy initialized(&repository, &ConversationRepository::initialized);

  repository.initialize();

  ASSERT_TRUE(initialized.wait(1000));
}

TEST(SqliteConversationRepository, CreateThenListReturnsNewConversationFirst) {
  InitializedFixture fixture;

  QSignalSpy created(&fixture.repository, &ConversationRepository::conversationCreated);
  fixture.repository.createConversation();
  ASSERT_TRUE(created.wait(1000));

  QSignalSpy listed(&fixture.repository, &ConversationRepository::conversationListLoaded);
  fixture.repository.listConversations();
  ASSERT_TRUE(listed.wait(1000));

  const auto conversations = listed.at(0).at(0).value<QList<ConversationSummary>>();
  ASSERT_EQ(conversations.size(), 1);
  EXPECT_EQ(conversations.front().title, kDefaultConversationTitle);
}

TEST(SqliteConversationRepository, MaterializeConversationAtomicallyStoresSummaryMessagesAndModel) {
  InitializedFixture fixture;
  const QString conversationId = holonight_domain::ConversationId::generate().toString();
  const QDateTime createdAt(QDate(2026, 7, 30), QTime(10, 0), QTimeZone::UTC);
  const ModelId model{.provider_id = QStringLiteral("ollama"), .model_name = QStringLiteral("llama3")};
  const Message user(MessageId::generate(), MessageRole::User, QStringLiteral("hello"), MessageStatus::Complete,
                     createdAt);
  const Message assistant(MessageId::generate(), MessageRole::Assistant, QString(), MessageStatus::Streaming,
                          createdAt.addMSecs(1), model);
  const ConversationSummary summary{
      .id = conversationId,
      .title = QStringLiteral("hello"),
      .created_at = createdAt,
      .updated_at = createdAt.addMSecs(1),
      .last_model_id = model,
      .title_source = TitleSource::Fallback,
  };

  QSignalSpy created(&fixture.repository, &ConversationRepository::conversationCreated);
  fixture.repository.materializeConversation(summary, {user, assistant});
  ASSERT_TRUE(created.wait(1000));
  EXPECT_EQ(created.at(0).at(0).value<ConversationSummary>(), summary);

  QSignalSpy loaded(&fixture.repository, &ConversationRepository::conversationLoaded);
  fixture.repository.loadConversation(conversationId);
  ASSERT_TRUE(loaded.wait(1000));
  const auto stored = loaded.at(0).at(0).value<LoadedConversation>();
  EXPECT_EQ(stored.summary, summary);
  ASSERT_EQ(stored.messages.size(), 2U);
  EXPECT_EQ(stored.messages[0], user);
  EXPECT_EQ(stored.messages[1].id(), assistant.id());
  EXPECT_EQ(stored.messages[1].role(), MessageRole::Assistant);
  EXPECT_EQ(stored.messages[1].status(), MessageStatus::Cancelled);
  EXPECT_EQ(stored.messages[1].modelId(), assistant.modelId());
}

TEST(SqliteConversationRepository, CreateConversationSetsFallbackTitleSource) {
  InitializedFixture fixture;

  QSignalSpy created(&fixture.repository, &ConversationRepository::conversationCreated);
  fixture.repository.createConversation();
  ASSERT_TRUE(created.wait(1000));

  EXPECT_EQ(created.at(0).at(0).value<ConversationSummary>().title_source, TitleSource::Fallback);
}

// T-028 (REQ-C-003): the migration's column DEFAULT for title_source is 'Manual' (see
// 0003_add_title_source.sql), not 'Fallback'. This test proves createConversation()'s INSERT
// explicitly binds title_source = 'Fallback' rather than implicitly relying on that DEFAULT: a
// raw INSERT that omits the column (in a separate, directly-owned connection) is shown to fall
// back to 'Manual', while createConversation() — exercised through the repository under test —
// still produces 'Fallback'. If a future regression dropped the explicit bind from the worker's
// INSERT, this test would fail because the row would silently pick up 'Manual' instead.
TEST(SqliteConversationRepository, CreateConversationBindsFallbackExplicitlyNotViaMigrationDefault) {
  const QString connectionName = uniqueTestConnectionName();
  {
    QSqlDatabase rawDatabase = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    rawDatabase.setDatabaseName(QStringLiteral(":memory:"));
    ASSERT_TRUE(rawDatabase.open());
    ASSERT_TRUE(MigrationRunner::apply(rawDatabase, MigrationRunner::builtInMigrations()).has_value());

    QSqlQuery insert(rawDatabase);
    ASSERT_TRUE(
        insert.exec(QStringLiteral("INSERT INTO conversations (id, title, created_at, updated_at) VALUES "
                                   "('no-title-source', 'Untitled', '2026-01-01T00:00:00Z', '2026-01-01T00:00:00Z')")));

    QSqlQuery select(rawDatabase);
    ASSERT_TRUE(select.exec(QStringLiteral("SELECT title_source FROM conversations WHERE id = 'no-title-source'")));
    ASSERT_TRUE(select.next());
    EXPECT_EQ(select.value(0).toString(), QStringLiteral("Manual"));
  }
  QSqlDatabase::removeDatabase(connectionName);

  InitializedFixture fixture;
  QSignalSpy created(&fixture.repository, &ConversationRepository::conversationCreated);
  fixture.repository.createConversation();
  ASSERT_TRUE(created.wait(1000));

  EXPECT_EQ(created.at(0).at(0).value<ConversationSummary>().title_source, TitleSource::Fallback);
}

TEST(SqliteConversationRepository, LoadConversationReturnsPersistedTitleSource) {
  InitializedFixture fixture;

  QSignalSpy created(&fixture.repository, &ConversationRepository::conversationCreated);
  fixture.repository.createConversation();
  ASSERT_TRUE(created.wait(1000));
  const QString conversationId = created.at(0).at(0).value<ConversationSummary>().id;

  QSignalSpy renamed(&fixture.repository, &ConversationRepository::conversationRenamed);
  fixture.repository.renameConversation(conversationId, QStringLiteral("Generated Title"), TitleSource::Generated);
  ASSERT_TRUE(renamed.wait(1000));

  QSignalSpy loaded(&fixture.repository, &ConversationRepository::conversationLoaded);
  fixture.repository.loadConversation(conversationId);
  ASSERT_TRUE(loaded.wait(1000));

  const auto conversation = loaded.at(0).at(0).value<LoadedConversation>();
  EXPECT_EQ(conversation.summary.title_source, TitleSource::Generated);
}

TEST(SqliteConversationRepository, RenameMovesConversationToFrontAndUpdatesTitle) {
  InitializedFixture fixture;

  QSignalSpy createdOld(&fixture.repository, &ConversationRepository::conversationCreated);
  fixture.repository.createConversation(QStringLiteral("Old"));
  ASSERT_TRUE(createdOld.wait(1000));
  const QString oldId = createdOld.at(0).at(0).value<ConversationSummary>().id;

  QSignalSpy createdNew(&fixture.repository, &ConversationRepository::conversationCreated);
  fixture.repository.createConversation(QStringLiteral("New"));
  ASSERT_TRUE(createdNew.wait(1000));

  QSignalSpy renamed(&fixture.repository, &ConversationRepository::conversationRenamed);
  fixture.repository.renameConversation(oldId, QStringLiteral("Renamed"));
  ASSERT_TRUE(renamed.wait(1000));
  EXPECT_EQ(renamed.at(0).at(0).value<ConversationSummary>().title, QStringLiteral("Renamed"));

  QSignalSpy listed(&fixture.repository, &ConversationRepository::conversationListLoaded);
  fixture.repository.listConversations();
  ASSERT_TRUE(listed.wait(1000));
  const auto conversations = listed.at(0).at(0).value<QList<ConversationSummary>>();
  ASSERT_EQ(conversations.size(), 2);
  EXPECT_EQ(conversations.front().id, oldId);
  EXPECT_EQ(conversations.front().title, QStringLiteral("Renamed"));
}

TEST(SqliteConversationRepository, LoadConversationReturnsMessagesInChronologicalOrder) {
  InitializedFixture fixture;

  QSignalSpy created(&fixture.repository, &ConversationRepository::conversationCreated);
  fixture.repository.createConversation();
  ASSERT_TRUE(created.wait(1000));
  const QString conversationId = created.at(0).at(0).value<ConversationSummary>().id;

  const QDateTime userCreatedAt(QDate(2026, 7, 26), QTime(12, 34, 56), QTimeZone::UTC);
  const QDateTime assistantCreatedAt = userCreatedAt.addSecs(10);
  const ModelId assistantModel{.provider_id = QStringLiteral("ollama"), .model_name = QStringLiteral("llama3")};
  const Message userMessage(MessageId::generate(), MessageRole::User, QStringLiteral("hello"), MessageStatus::Complete,
                            userCreatedAt);
  const Message assistantMessage(MessageId::generate(), MessageRole::Assistant, QStringLiteral("hi there"),
                                 MessageStatus::Complete, assistantCreatedAt, assistantModel);
  fixture.repository.persistNewMessage(conversationId, assistantMessage);
  fixture.repository.persistNewMessage(conversationId, userMessage);

  QSignalSpy loaded(&fixture.repository, &ConversationRepository::conversationLoaded);
  fixture.repository.loadConversation(conversationId);
  ASSERT_TRUE(loaded.wait(1000));

  const auto conversation = loaded.at(0).at(0).value<LoadedConversation>();
  ASSERT_EQ(conversation.messages.size(), 2U);
  EXPECT_EQ(conversation.messages[0].text(), QStringLiteral("hello"));
  EXPECT_EQ(conversation.messages[0].role(), MessageRole::User);
  EXPECT_EQ(conversation.messages[0].createdAt(), userCreatedAt);
  EXPECT_EQ(conversation.messages[1].text(), QStringLiteral("hi there"));
  EXPECT_EQ(conversation.messages[1].role(), MessageRole::Assistant);
  EXPECT_EQ(conversation.messages[1].status(), MessageStatus::Complete);
  EXPECT_EQ(conversation.messages[1].createdAt(), assistantCreatedAt);
  ASSERT_TRUE(conversation.messages[1].modelId().has_value());
  EXPECT_EQ(*conversation.messages[1].modelId(), assistantModel);
}

TEST(SqliteConversationRepository, PersistNewMessageRoundTripsToolCallsThroughLoadConversation) {
  InitializedFixture fixture;

  QSignalSpy created(&fixture.repository, &ConversationRepository::conversationCreated);
  fixture.repository.createConversation();
  ASSERT_TRUE(created.wait(1000));
  const QString conversationId = created.at(0).at(0).value<ConversationSummary>().id;

  const QDateTime invocationCreatedAt(QDate(2026, 8, 4), QTime(9, 0, 0), QTimeZone::UTC);
  const QDateTime resultCreatedAt = invocationCreatedAt.addSecs(1);
  const QDateTime requestedAt = invocationCreatedAt.addMSecs(-7);
  const QDateTime& startedAt = invocationCreatedAt;
  const QDateTime& finishedAt = resultCreatedAt;

  holonight_domain::ToolCallEntry invocationEntry{.kind = holonight_domain::ToolCallKind::Invocation,
                                                  .tool_use_id = QStringLiteral("toolu_123"),
                                                  .tool_name = QStringLiteral("ListFiles"),
                                                  .tool_id = QStringLiteral("filesystem.list"),
                                                  .function_name = QStringLiteral("list_files"),
                                                  .status = ToolInvocationStatus::Running,
                                                  .requested_at = requestedAt,
                                                  .execution_location = ToolExecutionLocation::LocalClient};
  invocationEntry.input = QJsonObject{{QStringLiteral("path"), QStringLiteral("~/Documents")}};
  invocationEntry.started_at = startedAt;
  Message invocationMessage(MessageId::generate(), MessageRole::Assistant, QString(), MessageStatus::Complete,
                            invocationCreatedAt);
  invocationMessage.setToolCalls({invocationEntry});

  holonight_domain::ToolCallEntry resultEntry{.kind = holonight_domain::ToolCallKind::Result,
                                              .tool_use_id = QStringLiteral("toolu_123"),
                                              .tool_id = QStringLiteral("filesystem.list"),
                                              .function_name = QStringLiteral("list_files"),
                                              .status = ToolInvocationStatus::Completed,
                                              .requested_at = requestedAt,
                                              .execution_location = ToolExecutionLocation::LocalClient};
  resultEntry.started_at = startedAt;
  resultEntry.finished_at = finishedAt;
  resultEntry.result = QJsonObject{
      {QStringLiteral("entries"), QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("notes.txt")},
                                                         {QStringLiteral("type"), QStringLiteral("file")}}}}};
  resultEntry.is_error = false;
  Message resultMessage(MessageId::generate(), MessageRole::User, QString(), MessageStatus::Complete, resultCreatedAt);
  resultMessage.setToolCalls({resultEntry});

  fixture.repository.persistNewMessage(conversationId, invocationMessage);
  fixture.repository.persistNewMessage(conversationId, resultMessage);

  QSignalSpy loaded(&fixture.repository, &ConversationRepository::conversationLoaded);
  fixture.repository.loadConversation(conversationId);
  ASSERT_TRUE(loaded.wait(1000));

  const auto conversation = loaded.at(0).at(0).value<LoadedConversation>();
  ASSERT_EQ(conversation.messages.size(), 2U);

  ASSERT_EQ(conversation.messages[0].toolCalls().size(), 1U);
  EXPECT_EQ(conversation.messages[0].toolCalls()[0], invocationEntry);

  ASSERT_EQ(conversation.messages[1].toolCalls().size(), 1U);
  EXPECT_EQ(conversation.messages[1].toolCalls()[0], resultEntry);
}

// Google-only fields (thought_signature, provider_call_id_synthesized) must survive the same
// persist-then-reload round trip as every other ToolCallEntry field (REQ-F-005/006/008/009).
TEST(SqliteConversationRepository, PersistNewMessageRoundTripsThoughtSignatureAndSynthesizedProviderCallId) {
  InitializedFixture fixture;

  QSignalSpy created(&fixture.repository, &ConversationRepository::conversationCreated);
  fixture.repository.createConversation();
  ASSERT_TRUE(created.wait(1000));
  const QString conversationId = created.at(0).at(0).value<ConversationSummary>().id;

  const QDateTime invocationCreatedAt(QDate(2026, 8, 5), QTime(9, 0, 0), QTimeZone::UTC);

  holonight_domain::ToolCallEntry invocationEntry{.kind = holonight_domain::ToolCallKind::Invocation,
                                                  .tool_use_id = QStringLiteral("f47ac10b-58cc-4372-a567-0e02b2c3d479"),
                                                  .tool_name = QStringLiteral("ListFiles"),
                                                  .tool_id = QStringLiteral("filesystem.list"),
                                                  .function_name = QStringLiteral("list_files"),
                                                  .status = ToolInvocationStatus::Completed,
                                                  .requested_at = invocationCreatedAt,
                                                  .execution_location = ToolExecutionLocation::LocalClient,
                                                  .thought_signature = QStringLiteral("sig-xyz"),
                                                  .provider_call_id_synthesized = true};
  invocationEntry.input = QJsonObject{{QStringLiteral("path"), QStringLiteral("~/Documents")}};
  Message invocationMessage(MessageId::generate(), MessageRole::Assistant, QString(), MessageStatus::Complete,
                            invocationCreatedAt);
  invocationMessage.setToolCalls({invocationEntry});

  fixture.repository.persistNewMessage(conversationId, invocationMessage);

  QSignalSpy loaded(&fixture.repository, &ConversationRepository::conversationLoaded);
  fixture.repository.loadConversation(conversationId);
  ASSERT_TRUE(loaded.wait(1000));

  const auto conversation = loaded.at(0).at(0).value<LoadedConversation>();
  ASSERT_EQ(conversation.messages.size(), 1U);
  ASSERT_EQ(conversation.messages[0].toolCalls().size(), 1U);
  EXPECT_EQ(conversation.messages[0].toolCalls()[0], invocationEntry);

  const auto& roundTripped = conversation.messages[0].toolCalls()[0];
  ASSERT_TRUE(roundTripped.thought_signature.has_value());
  EXPECT_EQ(*roundTripped.thought_signature, QStringLiteral("sig-xyz"));
  EXPECT_TRUE(roundTripped.provider_call_id_synthesized);
}

TEST(SqliteConversationRepository, LegacyToolCallMetadataRowsWithoutNormalizationDecodeSafely) {
  QTemporaryDir temporaryDir;
  ASSERT_TRUE(temporaryDir.isValid());
  const QString dbPath = temporaryDir.filePath(QStringLiteral("legacy_tool_calls.sqlite"));

  SqliteConversationRepository repository(dbPath);
  QSignalSpy initializedSpy(&repository, &ConversationRepository::initialized);
  repository.initialize();
  ASSERT_TRUE(initializedSpy.wait(1000));

  QSignalSpy created(&repository, &ConversationRepository::conversationCreated);
  repository.createConversation();
  ASSERT_TRUE(created.wait(1000));
  const QString conversationId = created.at(0).at(0).value<ConversationSummary>().id;

  const QString messageId = MessageId::generate().toString();
  const QDateTime messageCreatedAt(QDate(2026, 7, 30), QTime(9, 0), QTimeZone::UTC);
  const QJsonObject legacyEntry{
      {QStringLiteral("kind"), QStringLiteral("invocation")},
      {QStringLiteral("tool_use_id"), QStringLiteral("toolu_legacy")},
      {QStringLiteral("tool_name"), QStringLiteral("ListFiles")},
      {QStringLiteral("input"), QJsonObject{{QStringLiteral("path"), QStringLiteral("~")}}},
  };
  const QString legacyToolCalls =
      QString::fromUtf8(QJsonDocument(QJsonArray{legacyEntry}).toJson(QJsonDocument::Compact));
  const QString rawConnectionName = uniqueTestConnectionName();
  {
    QSqlDatabase legacyDatabase = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), rawConnectionName);
    legacyDatabase.setDatabaseName(dbPath);
    ASSERT_TRUE(legacyDatabase.open());
    QSqlQuery insert(legacyDatabase);
    const QString insertSql = QStringLiteral(
        "INSERT INTO messages (id, conversation_id, role, content, status, created_at, model_id, "
        "tool_calls) VALUES (:id, :conversation_id, :role, :content, :status, :created_at, "
        ":model_id, :tool_calls)");
    ASSERT_TRUE(insert.prepare(insertSql));
    insert.bindValue(QStringLiteral(":id"), messageId);
    insert.bindValue(QStringLiteral(":conversation_id"), conversationId);
    insert.bindValue(QStringLiteral(":role"), QStringLiteral("assistant"));
    insert.bindValue(QStringLiteral(":content"), QStringLiteral(""));
    insert.bindValue(QStringLiteral(":status"), QStringLiteral("complete"));
    insert.bindValue(QStringLiteral(":created_at"), messageCreatedAt);
    insert.bindValue(QStringLiteral(":model_id"), QVariant());
    insert.bindValue(QStringLiteral(":tool_calls"), legacyToolCalls);
    ASSERT_TRUE(insert.exec());
    legacyDatabase.close();
  }
  QSqlDatabase::removeDatabase(rawConnectionName);

  QSignalSpy loaded(&repository, &ConversationRepository::conversationLoaded);
  repository.loadConversation(conversationId);
  ASSERT_TRUE(loaded.wait(1000));

  const auto conversation = loaded.at(0).at(0).value<LoadedConversation>();
  ASSERT_EQ(conversation.messages.size(), 1U);
  ASSERT_EQ(conversation.messages[0].toolCalls().size(), 1U);
  const auto& entry = conversation.messages[0].toolCalls()[0];
  EXPECT_EQ(entry.tool_id, QString());
  EXPECT_EQ(entry.tool_name, QStringLiteral("ListFiles"));
  EXPECT_EQ(entry.function_name, QStringLiteral("ListFiles"));
  EXPECT_EQ(entry.status, ToolInvocationStatus::Cancelled);
  EXPECT_EQ(entry.execution_location, ToolExecutionLocation::LocalClient);
  EXPECT_FALSE(entry.started_at.has_value());
  EXPECT_FALSE(entry.finished_at.has_value());
  EXPECT_FALSE(entry.requested_at.isValid());
}

TEST(SqliteConversationRepository, LoadCancelsUnmatchedRunningToolInvocationAsInterrupted) {
  InitializedFixture fixture;

  QSignalSpy created(&fixture.repository, &ConversationRepository::conversationCreated);
  fixture.repository.createConversation();
  ASSERT_TRUE(created.wait(1000));
  const QString conversationId = created.at(0).at(0).value<ConversationSummary>().id;

  const QDateTime requestedAt(QDate(2026, 8, 5), QTime(10, 0), QTimeZone::UTC);
  holonight_domain::ToolCallEntry invocationEntry{
      .kind = holonight_domain::ToolCallKind::Invocation,
      .tool_use_id = QStringLiteral("toolu_interrupted"),
      .tool_name = QStringLiteral("list_files"),
      .tool_id = QStringLiteral("filesystem.list"),
      .function_name = QStringLiteral("list_files"),
      .status = ToolInvocationStatus::Running,
      .requested_at = requestedAt,
      .execution_location = ToolExecutionLocation::LocalClient,
  };
  invocationEntry.input = QJsonObject{{QStringLiteral("path"), QStringLiteral("~/Pictures")}};
  invocationEntry.started_at = requestedAt.addMSecs(5);
  Message invocationMessage(MessageId::generate(), MessageRole::Assistant, QString(), MessageStatus::Complete,
                            requestedAt);
  invocationMessage.setToolCalls({invocationEntry});
  fixture.repository.persistNewMessage(conversationId, invocationMessage);

  QSignalSpy loaded(&fixture.repository, &ConversationRepository::conversationLoaded);
  fixture.repository.loadConversation(conversationId);
  ASSERT_TRUE(loaded.wait(1000));

  const auto conversation = loaded.at(0).at(0).value<LoadedConversation>();
  ASSERT_EQ(conversation.messages.size(), 1U);
  ASSERT_EQ(conversation.messages[0].toolCalls().size(), 1U);
  const auto& restored = conversation.messages[0].toolCalls()[0];
  EXPECT_EQ(restored.status, ToolInvocationStatus::Cancelled);
  EXPECT_EQ(restored.tool_use_id, invocationEntry.tool_use_id);
  EXPECT_EQ(restored.tool_id, invocationEntry.tool_id);
  EXPECT_EQ(restored.function_name, invocationEntry.function_name);
  EXPECT_EQ(restored.input, invocationEntry.input);
  EXPECT_EQ(restored.requested_at, invocationEntry.requested_at);
  EXPECT_EQ(restored.started_at, invocationEntry.started_at);
  EXPECT_FALSE(restored.finished_at.has_value());
}

TEST(SqliteConversationRepository, PersistMessageSettledUpdatesContentAndStatusInPlace) {
  InitializedFixture fixture;

  QSignalSpy created(&fixture.repository, &ConversationRepository::conversationCreated);
  fixture.repository.createConversation();
  ASSERT_TRUE(created.wait(1000));
  const QString conversationId = created.at(0).at(0).value<ConversationSummary>().id;

  const MessageId assistantId = MessageId::generate();
  const Message placeholder(assistantId, MessageRole::Assistant, QString(), MessageStatus::Streaming);
  fixture.repository.persistNewMessage(conversationId, placeholder);

  const Message settled(assistantId, MessageRole::Assistant, QStringLiteral("final answer"), MessageStatus::Complete);
  fixture.repository.persistMessageSettled(conversationId, settled);

  QSignalSpy loaded(&fixture.repository, &ConversationRepository::conversationLoaded);
  fixture.repository.loadConversation(conversationId);
  ASSERT_TRUE(loaded.wait(1000));

  const auto conversation = loaded.at(0).at(0).value<LoadedConversation>();
  ASSERT_EQ(conversation.messages.size(), 1U);
  EXPECT_EQ(conversation.messages[0].text(), QStringLiteral("final answer"));
  EXPECT_EQ(conversation.messages[0].status(), MessageStatus::Complete);
}

TEST(SqliteConversationRepository, PersistMessageSettledNormalizesNullContentToEmpty) {
  InitializedFixture fixture;

  QSignalSpy created(&fixture.repository, &ConversationRepository::conversationCreated);
  fixture.repository.createConversation();
  ASSERT_TRUE(created.wait(1000));
  const QString conversationId = created.at(0).at(0).value<ConversationSummary>().id;

  const MessageId assistantId = MessageId::generate();
  fixture.repository.persistNewMessage(
      conversationId, Message(assistantId, MessageRole::Assistant, QString(), MessageStatus::Streaming));
  fixture.repository.persistMessageSettled(
      conversationId, Message(assistantId, MessageRole::Assistant, QString(), MessageStatus::Complete));

  QSignalSpy loaded(&fixture.repository, &ConversationRepository::conversationLoaded);
  fixture.repository.loadConversation(conversationId);
  ASSERT_TRUE(loaded.wait(1000));

  const auto conversation = loaded.at(0).at(0).value<LoadedConversation>();
  ASSERT_EQ(conversation.messages.size(), 1U);
  EXPECT_FALSE(conversation.messages[0].text().isNull());
  EXPECT_TRUE(conversation.messages[0].text().isEmpty());
  EXPECT_EQ(conversation.messages[0].status(), MessageStatus::Complete);
}

TEST(SqliteConversationRepository, LoadCancelsAbandonedAssistantStream) {
  InitializedFixture fixture;

  QSignalSpy created(&fixture.repository, &ConversationRepository::conversationCreated);
  fixture.repository.createConversation();
  ASSERT_TRUE(created.wait(1000));
  const QString conversationId = created.at(0).at(0).value<ConversationSummary>().id;

  fixture.repository.persistNewMessage(conversationId, Message(MessageId::generate(), MessageRole::User,
                                                               QStringLiteral("hello"), MessageStatus::Pending));
  fixture.repository.persistNewMessage(
      conversationId, Message(MessageId::generate(), MessageRole::Assistant, QString(), MessageStatus::Streaming));

  QSignalSpy loaded(&fixture.repository, &ConversationRepository::conversationLoaded);
  fixture.repository.loadConversation(conversationId);
  ASSERT_TRUE(loaded.wait(1000));

  const auto conversation = loaded.at(0).at(0).value<LoadedConversation>();
  ASSERT_EQ(conversation.messages.size(), 2U);
  EXPECT_EQ(conversation.messages[0].status(), MessageStatus::Pending);
  EXPECT_EQ(conversation.messages[1].status(), MessageStatus::Cancelled);
}

TEST(SqliteConversationRepository, DeleteConversationCascadesMessages) {
  InitializedFixture fixture;

  QSignalSpy created(&fixture.repository, &ConversationRepository::conversationCreated);
  fixture.repository.createConversation();
  ASSERT_TRUE(created.wait(1000));
  const QString conversationId = created.at(0).at(0).value<ConversationSummary>().id;

  fixture.repository.persistNewMessage(conversationId,
                                       Message(MessageId::generate(), MessageRole::User, QStringLiteral("hi")));

  QSignalSpy deleted(&fixture.repository, &ConversationRepository::conversationDeleted);
  fixture.repository.deleteConversation(conversationId);
  ASSERT_TRUE(deleted.wait(1000));

  QSignalSpy listed(&fixture.repository, &ConversationRepository::conversationListLoaded);
  fixture.repository.listConversations();
  ASSERT_TRUE(listed.wait(1000));
  EXPECT_TRUE(listed.at(0).at(0).value<QList<ConversationSummary>>().isEmpty());
}

TEST(SqliteConversationRepository, PinConversationSetsPinnedAtToNonNull) {
  InitializedFixture fixture;

  QSignalSpy created(&fixture.repository, &ConversationRepository::conversationCreated);
  fixture.repository.createConversation();
  ASSERT_TRUE(created.wait(1000));
  const QString conversationId = created.at(0).at(0).value<ConversationSummary>().id;

  QSignalSpy pinned(&fixture.repository, &ConversationRepository::conversationPinned);
  fixture.repository.pinConversation(conversationId);
  ASSERT_TRUE(pinned.wait(1000));
  EXPECT_EQ(pinned.at(0).at(0).toString(), conversationId);
  EXPECT_TRUE(pinned.at(0).at(1).toDateTime().isValid());
  EXPECT_EQ(pinned.at(0).at(1).toDateTime().time().msec(), 0);

  QSignalSpy loaded(&fixture.repository, &ConversationRepository::conversationLoaded);
  fixture.repository.loadConversation(conversationId);
  ASSERT_TRUE(loaded.wait(1000));
  const auto conversation = loaded.at(0).at(0).value<LoadedConversation>();
  ASSERT_TRUE(conversation.summary.pinned_at.has_value());
  EXPECT_EQ(*conversation.summary.pinned_at, pinned.at(0).at(1).toDateTime());
}

TEST(SqliteConversationRepository, PinMissingConversationEmitsErrorInsteadOfSuccess) {
  InitializedFixture fixture;
  QSignalSpy pinned(&fixture.repository, &ConversationRepository::conversationPinned);
  QSignalSpy error(&fixture.repository, &ConversationRepository::error);

  fixture.repository.pinConversation(QStringLiteral("missing"));

  ASSERT_TRUE(error.wait(1000));
  EXPECT_EQ(error.at(0).at(0).toString(), QStringLiteral("missing"));
  EXPECT_EQ(error.at(0).at(1).toString(), QStringLiteral("conversation not found"));
  EXPECT_EQ(pinned.count(), 0);
}

TEST(SqliteConversationRepository, UnpinConversationClearsPinnedAtToNull) {
  InitializedFixture fixture;

  QSignalSpy created(&fixture.repository, &ConversationRepository::conversationCreated);
  fixture.repository.createConversation();
  ASSERT_TRUE(created.wait(1000));
  const QString conversationId = created.at(0).at(0).value<ConversationSummary>().id;

  QSignalSpy pinned(&fixture.repository, &ConversationRepository::conversationPinned);
  fixture.repository.pinConversation(conversationId);
  ASSERT_TRUE(pinned.wait(1000));

  QSignalSpy unpinned(&fixture.repository, &ConversationRepository::conversationUnpinned);
  fixture.repository.unpinConversation(conversationId);
  ASSERT_TRUE(unpinned.wait(1000));
  EXPECT_EQ(unpinned.at(0).at(0).toString(), conversationId);

  QSignalSpy loaded(&fixture.repository, &ConversationRepository::conversationLoaded);
  fixture.repository.loadConversation(conversationId);
  ASSERT_TRUE(loaded.wait(1000));
  const auto conversation = loaded.at(0).at(0).value<LoadedConversation>();
  EXPECT_FALSE(conversation.summary.pinned_at.has_value());
}

TEST(SqliteConversationRepository, UnpinMissingConversationEmitsErrorInsteadOfSuccess) {
  InitializedFixture fixture;
  QSignalSpy unpinned(&fixture.repository, &ConversationRepository::conversationUnpinned);
  QSignalSpy error(&fixture.repository, &ConversationRepository::error);

  fixture.repository.unpinConversation(QStringLiteral("missing"));

  ASSERT_TRUE(error.wait(1000));
  EXPECT_EQ(error.at(0).at(0).toString(), QStringLiteral("missing"));
  EXPECT_EQ(error.at(0).at(1).toString(), QStringLiteral("conversation not found"));
  EXPECT_EQ(unpinned.count(), 0);
}

TEST(SqliteConversationRepository, RenameConversationLeavesPinnedAtUnchanged) {
  InitializedFixture fixture;

  QSignalSpy created(&fixture.repository, &ConversationRepository::conversationCreated);
  fixture.repository.createConversation();
  ASSERT_TRUE(created.wait(1000));
  const QString conversationId = created.at(0).at(0).value<ConversationSummary>().id;

  QSignalSpy pinned(&fixture.repository, &ConversationRepository::conversationPinned);
  fixture.repository.pinConversation(conversationId);
  ASSERT_TRUE(pinned.wait(1000));
  const QDateTime pinnedAt = pinned.at(0).at(1).toDateTime();

  QSignalSpy renamed(&fixture.repository, &ConversationRepository::conversationRenamed);
  fixture.repository.renameConversation(conversationId, QStringLiteral("Renamed"));
  ASSERT_TRUE(renamed.wait(1000));

  QSignalSpy loaded(&fixture.repository, &ConversationRepository::conversationLoaded);
  fixture.repository.loadConversation(conversationId);
  ASSERT_TRUE(loaded.wait(1000));
  const auto conversation = loaded.at(0).at(0).value<LoadedConversation>();
  ASSERT_TRUE(conversation.summary.pinned_at.has_value());
  EXPECT_EQ(*conversation.summary.pinned_at, pinnedAt);
}

TEST(SqliteConversationRepository, DeleteConversationLeavesOtherConversationsPinnedAtUnchanged) {
  InitializedFixture fixture;

  QSignalSpy createdPinned(&fixture.repository, &ConversationRepository::conversationCreated);
  fixture.repository.createConversation(QStringLiteral("Keep"));
  ASSERT_TRUE(createdPinned.wait(1000));
  const QString keptId = createdPinned.at(0).at(0).value<ConversationSummary>().id;

  QSignalSpy createdOther(&fixture.repository, &ConversationRepository::conversationCreated);
  fixture.repository.createConversation(QStringLiteral("Remove"));
  ASSERT_TRUE(createdOther.wait(1000));
  const QString removedId = createdOther.at(0).at(0).value<ConversationSummary>().id;

  QSignalSpy pinned(&fixture.repository, &ConversationRepository::conversationPinned);
  fixture.repository.pinConversation(keptId);
  ASSERT_TRUE(pinned.wait(1000));
  const QDateTime pinnedAt = pinned.at(0).at(1).toDateTime();

  QSignalSpy deleted(&fixture.repository, &ConversationRepository::conversationDeleted);
  fixture.repository.deleteConversation(removedId);
  ASSERT_TRUE(deleted.wait(1000));

  QSignalSpy loaded(&fixture.repository, &ConversationRepository::conversationLoaded);
  fixture.repository.loadConversation(keptId);
  ASSERT_TRUE(loaded.wait(1000));
  const auto conversation = loaded.at(0).at(0).value<LoadedConversation>();
  ASSERT_TRUE(conversation.summary.pinned_at.has_value());
  EXPECT_EQ(*conversation.summary.pinned_at, pinnedAt);
}

TEST(SqliteConversationRepository, ListConversationsReturnsPinnedBeforeUnpinnedEachGroupSortedByOwnField) {
  InitializedFixture fixture;

  auto createAndGetId = [&](const QString& title) {
    QSignalSpy created(&fixture.repository, &ConversationRepository::conversationCreated);
    fixture.repository.createConversation(title);
    created.wait(1000);
    return created.at(0).at(0).value<ConversationSummary>().id;
  };

  const QString conversationA = createAndGetId(QStringLiteral("A"));
  const QString conversationB = createAndGetId(QStringLiteral("B"));
  const QString conversationC = createAndGetId(QStringLiteral("C"));
  const QString conversationD = createAndGetId(QStringLiteral("D"));

  auto pinAndWait = [&](const QString& conversationId) {
    QSignalSpy pinned(&fixture.repository, &ConversationRepository::conversationPinned);
    fixture.repository.pinConversation(conversationId);
    pinned.wait(1000);
  };
  pinAndWait(conversationC);
  pinAndWait(conversationA);

  QSignalSpy listed(&fixture.repository, &ConversationRepository::conversationListLoaded);
  fixture.repository.listConversations();
  ASSERT_TRUE(listed.wait(1000));
  const auto conversations = listed.at(0).at(0).value<QList<ConversationSummary>>();

  ASSERT_EQ(conversations.size(), 4);
  // Pinned block first and ordered by pinned_at DESC. SQLite CURRENT_TIMESTAMP has one-second
  // resolution, so two pins in the same second may tie and fall back to rowid ordering.
  EXPECT_TRUE((conversations.at(0).id == conversationA && conversations.at(1).id == conversationC) ||
              (conversations.at(0).id == conversationC && conversations.at(1).id == conversationA));
  ASSERT_TRUE(conversations.at(0).pinned_at.has_value());
  ASSERT_TRUE(conversations.at(1).pinned_at.has_value());
  EXPECT_GE(*conversations.at(0).pinned_at, *conversations.at(1).pinned_at);
  // Unpinned block remains ordered by updated_at DESC: D was created after B.
  EXPECT_EQ(conversations.at(2).id, conversationD);
  EXPECT_EQ(conversations.at(3).id, conversationB);
}

TEST(SqliteConversationRepository, UpdateLastModelIdPersistsAndReportedByLoad) {
  InitializedFixture fixture;

  QSignalSpy created(&fixture.repository, &ConversationRepository::conversationCreated);
  fixture.repository.createConversation();
  ASSERT_TRUE(created.wait(1000));
  const QString conversationId = created.at(0).at(0).value<ConversationSummary>().id;

  const ModelId model{.provider_id = QStringLiteral("ollama"), .model_name = QStringLiteral("llama3")};
  QSignalSpy updated(&fixture.repository, &ConversationRepository::lastModelIdUpdated);
  fixture.repository.updateLastModelId(conversationId, model);
  ASSERT_TRUE(updated.wait(1000));

  QSignalSpy loaded(&fixture.repository, &ConversationRepository::conversationLoaded);
  fixture.repository.loadConversation(conversationId);
  ASSERT_TRUE(loaded.wait(1000));
  const auto conversation = loaded.at(0).at(0).value<LoadedConversation>();
  ASSERT_TRUE(conversation.summary.last_model_id.has_value());
  EXPECT_EQ(*conversation.summary.last_model_id, model);
}

TEST(SqliteConversationRepository, FatalInitializationFailureEmitsUnavailableNotInitialized) {
  SqliteConversationRepository repository{QStringLiteral("/nonexistent/path/conversations.db")};
  QSignalSpy unavailable(&repository, &ConversationRepository::unavailable);
  QSignalSpy initialized(&repository, &ConversationRepository::initialized);

  repository.initialize();

  ASSERT_TRUE(unavailable.wait(1000));
  EXPECT_EQ(initialized.size(), 0);
}

TEST(SqliteConversationRepository, RequestsAfterFatalFailureAreNoOps) {
  SqliteConversationRepository repository{QStringLiteral("/nonexistent/path/conversations.db")};
  QSignalSpy unavailable(&repository, &ConversationRepository::unavailable);
  repository.initialize();
  ASSERT_TRUE(unavailable.wait(1000));

  QSignalSpy created(&repository, &ConversationRepository::conversationCreated);
  repository.createConversation();

  EXPECT_FALSE(created.wait(200));
}

}  // namespace
}  // namespace holonight_persistence
