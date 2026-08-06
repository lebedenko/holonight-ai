#include "holonight_application/message_content_model.h"
#include "holonight_application/message_list_model.h"

#include <QDate>
#include <QJsonObject>
#include <QMap>
#include <QSignalSpy>
#include <QTime>
#include <QTimeZone>
#include <QVariantMap>

#include <gtest/gtest.h>
#include <holonight_domain/message.h>
#include <holonight_domain/tool_call.h>
#include <holonight_persistence/usage_record.h>
#include <holonight_rendering/content_block.h>

namespace holonight_application {
namespace {

using holonight_domain::Message;
using holonight_domain::MessageId;
using holonight_domain::MessageRole;
using holonight_domain::MessageStatus;
using holonight_domain::ModelId;
using holonight_domain::ToolCallEntry;
using holonight_domain::ToolCallKind;

QString textAt(const MessageListModel& model, int row) {
  return model.data(model.index(row), MessageListModel::TextRole).toString();
}

QList<int> changedRoles(const QSignalSpy& spy) { return qvariant_cast<QList<int>>(spy.constLast().at(2)); }

TEST(MessageListModel, RoleNamesExposeProviderId) {
  const MessageListModel model;

  EXPECT_EQ(model.roleNames().value(MessageListModel::ProviderIdRole), QByteArrayLiteral("providerId"));
}

TEST(MessageListModel, InsertsNewestMessageAtRowZero) {
  MessageListModel model;

  model.insertNewestMessage(
      Message(MessageId::generate(), MessageRole::User, QStringLiteral("older"), MessageStatus::Complete));
  model.insertNewestMessage(
      Message(MessageId::generate(), MessageRole::Assistant, QStringLiteral("newer"), MessageStatus::Complete));

  ASSERT_EQ(model.rowCount(), 2);
  EXPECT_EQ(textAt(model, 0), QStringLiteral("newer"));
  EXPECT_EQ(textAt(model, 1), QStringLiteral("older"));
}

TEST(MessageListModel, ResetReversesChronologicalInput) {
  MessageListModel model;
  const std::vector<Message> chronological{
      Message(MessageId::generate(), MessageRole::User, QStringLiteral("first"), MessageStatus::Complete),
      Message(MessageId::generate(), MessageRole::Assistant, QStringLiteral("second"), MessageStatus::Complete),
      Message(MessageId::generate(), MessageRole::User, QStringLiteral("third"), MessageStatus::Complete),
  };

  model.resetFromChronological(chronological);

  ASSERT_EQ(model.rowCount(), 3);
  EXPECT_EQ(textAt(model, 0), QStringLiteral("third"));
  EXPECT_EQ(textAt(model, 1), QStringLiteral("second"));
  EXPECT_EQ(textAt(model, 2), QStringLiteral("first"));
}

TEST(MessageListModel, StreamingUpdateChangesOnlyNewestTextAndStatusRoles) {
  MessageListModel model;
  const MessageId messageId = MessageId::generate();
  model.insertNewestMessage(
      Message(MessageId::generate(), MessageRole::User, QStringLiteral("older"), MessageStatus::Complete));
  model.insertNewestMessage(
      Message(messageId, MessageRole::Assistant, QString(), MessageStatus::Pending, {},
              ModelId{.provider_id = QStringLiteral("openai"), .model_name = QStringLiteral("gpt")}));
  QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);

  model.updateNewestMessage(
      Message(messageId, MessageRole::Assistant, QStringLiteral("partial"), MessageStatus::Streaming, {},
              ModelId{.provider_id = QStringLiteral("openai"), .model_name = QStringLiteral("gpt")}));

  ASSERT_EQ(changedSpy.count(), 1);
  EXPECT_EQ(changedSpy.constLast().at(0).value<QModelIndex>().row(), 0);
  EXPECT_EQ(changedSpy.constLast().at(1).value<QModelIndex>().row(), 0);
  EXPECT_EQ(changedRoles(changedSpy), (QList<int>{MessageListModel::TextRole, MessageListModel::StatusRole}));
  EXPECT_EQ(textAt(model, 1), QStringLiteral("older"));
}

TEST(MessageListModel, IdenticalUpdateEmitsNothing) {
  MessageListModel model;
  const Message message(MessageId::generate(), MessageRole::Assistant, QStringLiteral("partial"),
                        MessageStatus::Streaming);
  model.insertNewestMessage(message);
  QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);

  model.updateNewestMessage(message);

  EXPECT_EQ(changedSpy.count(), 0);
}

TEST(MessageListModel, CompletionKeepsPersistentContentModel) {
  MessageListModel model;
  const MessageId messageId = MessageId::generate();
  const QDateTime createdAt(QDate(2026, 8, 5), QTime(12, 0), QTimeZone::UTC);
  model.insertNewestMessage(Message(messageId, MessageRole::Assistant, QStringLiteral("```cpp\ncomplete\n```"),
                                    MessageStatus::Streaming, createdAt));
  auto* blocks = qobject_cast<MessageContentModel*>(
      model.data(model.index(0), MessageListModel::ContentBlocksRole).value<QObject*>());
  ASSERT_NE(blocks, nullptr);
  QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);

  model.updateNewestMessage(Message(messageId, MessageRole::Assistant, QStringLiteral("```cpp\ncomplete\n```"),
                                    MessageStatus::Complete, createdAt));

  ASSERT_EQ(changedSpy.count(), 1);
  EXPECT_EQ(changedRoles(changedSpy), (QList<int>{MessageListModel::StatusRole}));
  EXPECT_EQ(qobject_cast<MessageContentModel*>(
                model.data(model.index(0), MessageListModel::ContentBlocksRole).value<QObject*>()),
            blocks);
  ASSERT_EQ(blocks->rowCount(), 1);
  EXPECT_EQ(blocks->data(blocks->index(0), MessageContentModel::TextRole).toString(), QStringLiteral("complete\n"));
  EXPECT_TRUE(blocks->data(blocks->index(0), MessageContentModel::CompleteRole).toBool());
}

TEST(MessageListModel, TerminalTextChangeInvalidatesTextAndContentBlocksOnly) {
  MessageListModel model;
  const MessageId messageId = MessageId::generate();
  const QDateTime createdAt(QDate(2026, 8, 5), QTime(12, 0), QTimeZone::UTC);
  model.insertNewestMessage(
      Message(messageId, MessageRole::Assistant, QStringLiteral("before"), MessageStatus::Complete, createdAt));
  (void)model.data(model.index(0), MessageListModel::ContentBlocksRole);
  QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);

  model.updateNewestMessage(
      Message(messageId, MessageRole::Assistant, QStringLiteral("after"), MessageStatus::Complete, createdAt));

  ASSERT_EQ(changedSpy.count(), 1);
  EXPECT_EQ(changedRoles(changedSpy), (QList<int>{MessageListModel::TextRole}));
}

TEST(MessageListModel, StreamingMessageExposesPersistentContentModel) {
  MessageListModel model;
  model.insertNewestMessage(Message(MessageId::generate(), MessageRole::Assistant, QStringLiteral("```cpp\npartial"),
                                    MessageStatus::Streaming));

  auto* blocks = qobject_cast<MessageContentModel*>(
      model.data(model.index(0), MessageListModel::ContentBlocksRole).value<QObject*>());
  ASSERT_NE(blocks, nullptr);
  EXPECT_EQ(blocks->rawMarkdown(), QStringLiteral("```cpp\npartial"));
}

// T-011 (REQ-F-007/REQ-F-011): ordinary text messages carry no tool-call data.
TEST(MessageListModel, ToolCallRoleIsInvalidForOrdinaryTextMessage) {
  MessageListModel model;
  model.insertNewestMessage(
      Message(MessageId::generate(), MessageRole::User, QStringLiteral("hello"), MessageStatus::Complete));

  EXPECT_FALSE(model.data(model.index(0), MessageListModel::ToolCallRole).isValid());
}

TEST(MessageListModel, ToolCallRoleExposesInvocationNameAndInput) {
  MessageListModel model;
  Message invocation(MessageId::generate(), MessageRole::Assistant, QString(), MessageStatus::Complete);
  invocation.setToolCalls(
      {ToolCallEntry{.kind = ToolCallKind::Invocation,
                     .tool_use_id = QStringLiteral("toolu_01"),
                     .tool_name = QStringLiteral("ListFiles"),
                     .input = QJsonObject{{QStringLiteral("path"), QStringLiteral("~/Documents")}}}});
  model.insertNewestMessage(invocation);

  const QVariantMap toolCall = model.data(model.index(0), MessageListModel::ToolCallRole).toMap();
  EXPECT_EQ(toolCall.value(QStringLiteral("kind")).toString(), QStringLiteral("invocation"));
  EXPECT_EQ(toolCall.value(QStringLiteral("toolName")).toString(), QStringLiteral("ListFiles"));
  EXPECT_EQ(toolCall.value(QStringLiteral("input")).toMap().value(QStringLiteral("path")).toString(),
            QStringLiteral("~/Documents"));
}

TEST(MessageListModel, HistoricalListFilesAliasKeepsSpecializedRenderer) {
  MessageListModel model;
  Message invocation(MessageId::generate(), MessageRole::Assistant, QString(), MessageStatus::Complete);
  invocation.setToolCalls({ToolCallEntry{.kind = ToolCallKind::Invocation,
                                         .tool_use_id = QStringLiteral("toolu_legacy_list"),
                                         .tool_name = QStringLiteral("ListFiles"),
                                         .input = QJsonObject{{QStringLiteral("path"), QStringLiteral("~")}},
                                         .status = holonight_domain::ToolInvocationStatus::Cancelled}});

  model.resetFromChronological({invocation});

  ASSERT_EQ(model.rowCount(), 1);
  const QModelIndex index = model.index(0);
  EXPECT_EQ(model.data(index, MessageListModel::ToolCanonicalIdRole).toString(), QStringLiteral("filesystem.list"));
  EXPECT_EQ(model.data(index, MessageListModel::ToolRendererKeyRole).toString(), QStringLiteral("filesystem.list"));
  EXPECT_EQ(model.data(index, MessageListModel::ToolStatusRole).toString(), QStringLiteral("cancelled"));
}

TEST(MessageListModel, UnknownAndMalformedHistoricalToolsRemainVisibleThroughGenericFallback) {
  MessageListModel model;
  Message unknown(MessageId::generate(), MessageRole::Assistant, QString(), MessageStatus::Complete);
  unknown.setToolCalls({ToolCallEntry{.kind = ToolCallKind::Invocation,
                                      .tool_use_id = QStringLiteral("toolu_removed"),
                                      .tool_name = QStringLiteral("RemovedTool"),
                                      .input = QJsonObject{{QStringLiteral("legacy"), true}},
                                      .status = holonight_domain::ToolInvocationStatus::Completed}});
  Message malformed(MessageId::generate(), MessageRole::Assistant, QString(), MessageStatus::Complete);
  malformed.setToolCalls({ToolCallEntry{.kind = ToolCallKind::Invocation}});

  model.resetFromChronological({unknown, malformed});

  ASSERT_EQ(model.rowCount(), 2);
  const QVariantMap malformedCall = model.data(model.index(0), MessageListModel::ToolCallRole).toMap();
  EXPECT_FALSE(malformedCall.isEmpty());
  EXPECT_EQ(malformedCall.value(QStringLiteral("rendererKey")).toString(), QStringLiteral("generic"));
  EXPECT_EQ(malformedCall.value(QStringLiteral("toolTitle")).toString(), QStringLiteral("Tool activity"));

  const QVariantMap unknownCall = model.data(model.index(1), MessageListModel::ToolCallRole).toMap();
  EXPECT_EQ(unknownCall.value(QStringLiteral("rendererKey")).toString(), QStringLiteral("generic"));
  EXPECT_EQ(unknownCall.value(QStringLiteral("toolName")).toString(), QStringLiteral("RemovedTool"));
  EXPECT_TRUE(unknownCall.value(QStringLiteral("rawArgumentsAvailable")).toBool());
}

TEST(MessageListModel, ToolCallRoleExposesErrorResult) {
  MessageListModel model;
  Message result(MessageId::generate(), MessageRole::User, QString(), MessageStatus::Complete);
  result.setToolCalls({ToolCallEntry{
      .kind = ToolCallKind::Result,
      .tool_use_id = QStringLiteral("toolu_01"),
      .result =
          QJsonObject{{QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), QStringLiteral("NOT_FOUND")}}}},
      .is_error = true}});
  model.insertNewestMessage(result);

  const QVariantMap toolCall = model.data(model.index(0), MessageListModel::ToolCallRole).toMap();
  EXPECT_EQ(toolCall.value(QStringLiteral("kind")).toString(), QStringLiteral("result"));
  EXPECT_TRUE(toolCall.value(QStringLiteral("isError")).toBool());
  EXPECT_EQ(toolCall.value(QStringLiteral("result"))
                .toMap()
                .value(QStringLiteral("error"))
                .toMap()
                .value(QStringLiteral("code"))
                .toString(),
            QStringLiteral("NOT_FOUND"));
}

TEST(MessageListModel, MatchingInvocationAndResultMergeIntoSingleProjectionRow) {
  MessageListModel model;
  Message invocation(MessageId::generate(), MessageRole::Assistant, QString(), MessageStatus::Complete);
  invocation.setToolCalls(
      {ToolCallEntry{.kind = ToolCallKind::Invocation,
                     .tool_use_id = QStringLiteral("toolu_merge"),
                     .tool_name = QStringLiteral("ListFiles"),
                     .input = QJsonObject{{QStringLiteral("path"), QStringLiteral("~/Downloads")}}}});
  model.insertNewestMessage(invocation);

  QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);
  Message result(MessageId::generate(), MessageRole::User, QString(), MessageStatus::Complete);
  result.setToolCalls({ToolCallEntry{.kind = ToolCallKind::Result,
                                     .tool_use_id = QStringLiteral("toolu_merge"),
                                     .result = QJsonObject{{QStringLiteral("error"), QStringLiteral("no_permission")}},
                                     .is_error = true}});
  model.insertNewestMessage(result);

  EXPECT_EQ(model.rowCount(), 1);
  ASSERT_EQ(changedSpy.count(), 1);
  EXPECT_EQ(changedSpy.constLast().at(0).value<QModelIndex>().row(), 0);
  EXPECT_EQ(changedSpy.constLast().at(1).value<QModelIndex>().row(), 0);
  const QList<int> changed = changedRoles(changedSpy);
  EXPECT_TRUE(changed.contains(MessageListModel::ToolCallRole));
  EXPECT_TRUE(changed.contains(MessageListModel::ToolSummaryRole));
  EXPECT_TRUE(changed.contains(MessageListModel::ToolStatusRole));

  const QVariantMap toolCall = model.data(model.index(0), MessageListModel::ToolCallRole).toMap();
  EXPECT_EQ(toolCall.value(QStringLiteral("kind")).toString(), QStringLiteral("invocation"));
  EXPECT_EQ(toolCall.value(QStringLiteral("toolName")).toString(), QStringLiteral("ListFiles"));
  EXPECT_TRUE(toolCall.value(QStringLiteral("isError")).toBool());
  EXPECT_EQ(toolCall.value(QStringLiteral("result")).toMap().value(QStringLiteral("error")).toString(),
            QStringLiteral("no_permission"));
}

TEST(MessageListModel, ToolActivityRolesExposeCanonicalAndDurationMetadata) {
  MessageListModel model;
  Message invocation(MessageId::generate(), MessageRole::Assistant, QString(), MessageStatus::Complete);
  invocation.setToolCalls(
      {ToolCallEntry{.kind = ToolCallKind::Invocation,
                     .tool_use_id = QStringLiteral("toolu_details"),
                     .tool_id = QStringLiteral("filesystem.list"),
                     .function_name = QStringLiteral("list_files"),
                     .input =
                         QJsonObject{
                             {QStringLiteral("path"), QStringLiteral("~/Pictures")},
                         },
                     .status = holonight_domain::ToolInvocationStatus::Completed,
                     .requested_at = QDateTime(QDate(2026, 8, 5), QTime(12, 0), QTimeZone::UTC),
                     .started_at = QDateTime(QDate(2026, 8, 5), QTime(12, 0, 2), QTimeZone::UTC),
                     .finished_at = QDateTime(QDate(2026, 8, 5), QTime(12, 0, 5), QTimeZone::UTC)}});
  model.insertNewestMessage(invocation);

  const QModelIndex index = model.index(0);
  EXPECT_EQ(model.data(index, MessageListModel::ToolInvocationIdRole).toString(), QStringLiteral("toolu_details"));
  EXPECT_EQ(model.data(index, MessageListModel::ToolCanonicalIdRole).toString(), QStringLiteral("filesystem.list"));
  EXPECT_EQ(model.data(index, MessageListModel::ToolStatusRole).toString(), QStringLiteral("completed"));
  EXPECT_EQ(model.data(index, MessageListModel::ToolTitleRole).toString(), QStringLiteral("filesystem.list"));
  EXPECT_EQ(model.data(index, MessageListModel::ToolRendererKeyRole).toString(), QStringLiteral("filesystem.list"));
  EXPECT_EQ(model.data(index, MessageListModel::ToolDurationMsRole).toLongLong(), 3000);
  EXPECT_EQ(model.data(index, MessageListModel::ToolSummaryRole).toString(),
            QStringLiteral("filesystem.list complete"));
  EXPECT_TRUE(model.data(index, MessageListModel::ToolHasRawArgumentsRole).toBool());
  EXPECT_FALSE(model.data(index, MessageListModel::ToolHasRawResultRole).toBool());
  EXPECT_FALSE(model.data(index, MessageListModel::ToolCanCancelRole).toBool());
  EXPECT_FALSE(model.data(index, MessageListModel::ToolIsErrorRole).toBool());

  const QVariantMap detail = model.data(index, MessageListModel::ToolDetailDataRole).toMap();
  EXPECT_EQ(detail.value(QStringLiteral("toolId")).toString(), QStringLiteral("filesystem.list"));
  EXPECT_EQ(detail.value(QStringLiteral("functionName")).toString(), QStringLiteral("list_files"));
  EXPECT_EQ(detail.value(QStringLiteral("input")).toMap().value(QStringLiteral("path")).toString(),
            QStringLiteral("~/Pictures"));
}

TEST(MessageListModel, RestoredInterruptedInvocationDisplaysAsCancelled) {
  MessageListModel model;
  Message invocation(MessageId::generate(), MessageRole::Assistant, QString(), MessageStatus::Complete);
  invocation.setToolCalls({ToolCallEntry{.kind = ToolCallKind::Invocation,
                                         .tool_use_id = QStringLiteral("toolu_interrupted"),
                                         .tool_id = QStringLiteral("filesystem.list"),
                                         .function_name = QStringLiteral("list_files"),
                                         .status = holonight_domain::ToolInvocationStatus::Cancelled}});

  model.resetFromChronological({invocation});

  ASSERT_EQ(model.rowCount(), 1);
  const QModelIndex index = model.index(0);
  EXPECT_EQ(model.data(index, MessageListModel::ToolStatusRole).toString(), QStringLiteral("cancelled"));
  EXPECT_EQ(model.data(index, MessageListModel::ToolSummaryRole).toString(),
            QStringLiteral("filesystem.list cancelled"));
}

TEST(MessageListModel, OrphanResultDoesNotMergeAndRemainsVisible) {
  MessageListModel model;
  Message orphanResult(MessageId::generate(), MessageRole::Assistant, QString(), MessageStatus::Complete);
  orphanResult.setToolCalls(
      {ToolCallEntry{.kind = ToolCallKind::Result, .tool_use_id = QStringLiteral("toolu_unknown"), .is_error = false}});

  model.insertNewestMessage(orphanResult);

  EXPECT_EQ(model.rowCount(), 1);
  const QVariantMap toolCall = model.data(model.index(0), MessageListModel::ToolCallRole).toMap();
  EXPECT_EQ(toolCall.value(QStringLiteral("kind")).toString(), QStringLiteral("result"));
  EXPECT_EQ(toolCall.value(QStringLiteral("toolUseId")).toString(), QStringLiteral("toolu_unknown"));
}

TEST(MessageListModel, DuplicateResultIdsCreateFallbackRowAfterFirstMerge) {
  MessageListModel model;
  Message invocation(MessageId::generate(), MessageRole::Assistant, QString(), MessageStatus::Complete);
  invocation.setToolCalls(
      {ToolCallEntry{.kind = ToolCallKind::Invocation, .tool_use_id = QStringLiteral("toolu_dup")}});
  model.insertNewestMessage(invocation);
  Message firstResult(MessageId::generate(), MessageRole::User, QString(), MessageStatus::Complete);
  firstResult.setToolCalls({ToolCallEntry{.kind = ToolCallKind::Result, .tool_use_id = QStringLiteral("toolu_dup")}});
  model.insertNewestMessage(firstResult);

  Message secondResult(MessageId::generate(), MessageRole::User, QString(), MessageStatus::Complete);
  secondResult.setToolCalls({ToolCallEntry{.kind = ToolCallKind::Result, .tool_use_id = QStringLiteral("toolu_dup")}});
  model.insertNewestMessage(secondResult);

  EXPECT_EQ(model.rowCount(), 2);
  EXPECT_EQ(model.data(model.index(0), MessageListModel::TextRole).toString(), QStringLiteral(""));
  const QVariantMap firstRow = model.data(model.index(1), MessageListModel::ToolCallRole).toMap();
  EXPECT_EQ(firstRow.value(QStringLiteral("kind")).toString(), QStringLiteral("invocation"));
  const QVariantMap secondRow = model.data(model.index(0), MessageListModel::ToolCallRole).toMap();
  EXPECT_EQ(secondRow.value(QStringLiteral("kind")).toString(), QStringLiteral("result"));
}

TEST(MessageListModel, ResetFromChronologicalPreservesOrderWhileProjectingToolRows) {
  MessageListModel model;
  std::vector<Message> chronological{
      Message(MessageId::generate(), MessageRole::User, QStringLiteral("before"), MessageStatus::Complete),
      Message(MessageId::generate(), MessageRole::Assistant, QStringLiteral("ask"), MessageStatus::Complete),
      Message(MessageId::generate(), MessageRole::Assistant, QStringLiteral(""), MessageStatus::Complete),
      Message(MessageId::generate(), MessageRole::User, QStringLiteral("final"), MessageStatus::Complete),
  };
  chronological[2].setToolCalls(
      {ToolCallEntry{.kind = ToolCallKind::Invocation, .tool_use_id = QStringLiteral("toolu_1")}});
  chronological[3].setToolCalls(
      {ToolCallEntry{.kind = ToolCallKind::Result, .tool_use_id = QStringLiteral("toolu_1"), .result = QJsonObject{}}});

  model.resetFromChronological(chronological);

  ASSERT_EQ(model.rowCount(), 3);
  EXPECT_EQ(model.data(model.index(0), MessageListModel::TextRole).toString(), QStringLiteral(""));
  EXPECT_EQ(model.data(model.index(1), MessageListModel::TextRole).toString(), QStringLiteral("ask"));
  EXPECT_EQ(model.data(model.index(2), MessageListModel::TextRole).toString(), QStringLiteral("before"));
  EXPECT_EQ(model.data(model.index(0), MessageListModel::ToolCallRole).toMap().value(QStringLiteral("kind")).toString(),
            QStringLiteral("invocation"));
  EXPECT_TRUE(model.data(model.index(1), MessageListModel::ToolCallRole).isNull());
  EXPECT_TRUE(model.data(model.index(2), MessageListModel::ToolCallRole).isNull());
}

TEST(MessageListModel, DeletedProviderUsesFrozenTombstoneAttributionWithoutChangingProviderId) {
  MessageListModel model(
      holonight_config::ProviderState{.tombstones = {{.instance_id = QStringLiteral("openai"),
                                                      .type = holonight_config::ProviderType::OpenAi,
                                                      .last_display_name = QStringLiteral("Former work account")}}});
  model.insertNewestMessage(
      Message(MessageId::generate(), MessageRole::Assistant, QStringLiteral("response"), MessageStatus::Complete, {},
              ModelId{.provider_id = QStringLiteral("openai"), .model_name = QStringLiteral("gpt")}));

  EXPECT_EQ(model.data(model.index(0), MessageListModel::ProviderIdRole).toString(), QStringLiteral("openai"));
  EXPECT_EQ(model.data(model.index(0), MessageListModel::ProviderTypeRole).toString(), QStringLiteral("openai"));
  EXPECT_EQ(model.data(model.index(0), MessageListModel::ProviderNameRole).toString(),
            QStringLiteral("Former work account"));
}

TEST(MessageListModel, RenamedLiveProviderUsesCurrentAttributionWithoutChangingProviderId) {
  MessageListModel model(holonight_config::ProviderState{.instances = {
                                                             {.id = QStringLiteral("work-openai"),
                                                              .type = holonight_config::ProviderType::OpenAi,
                                                              .display_name = QStringLiteral("Renamed work account"),
                                                              .enabled = true,
                                                              .settings = holonight_config::OpenAIProviderConfig{}},
                                                         }});
  model.insertNewestMessage(
      Message(MessageId::generate(), MessageRole::Assistant, QStringLiteral("response"), MessageStatus::Complete, {},
              ModelId{.provider_id = QStringLiteral("work-openai"), .model_name = QStringLiteral("gpt")}));

  EXPECT_EQ(model.data(model.index(0), MessageListModel::ProviderIdRole).toString(), QStringLiteral("work-openai"));
  EXPECT_EQ(model.data(model.index(0), MessageListModel::ProviderTypeRole).toString(), QStringLiteral("openai"));
  EXPECT_EQ(model.data(model.index(0), MessageListModel::ProviderNameRole).toString(),
            QStringLiteral("Renamed work account"));
}

TEST(MessageListModel, ProviderStateRefreshesUnknownAttributionAndOnlyChangedRoles) {
  MessageListModel model;
  model.insertNewestMessage(
      Message(MessageId::generate(), MessageRole::Assistant, QStringLiteral("response"), MessageStatus::Complete, {},
              ModelId{.provider_id = QStringLiteral("ollama-mac"), .model_name = QStringLiteral("gemma4:12b-mlx")}));
  QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);

  model.setProviderState(holonight_config::ProviderState{.instances = {
                                                             {.id = QStringLiteral("ollama-mac"),
                                                              .type = holonight_config::ProviderType::Ollama,
                                                              .display_name = QStringLiteral("Ollama Mac"),
                                                              .enabled = true,
                                                              .settings = holonight_config::OllamaProviderConfig{}},
                                                         }});

  ASSERT_EQ(changedSpy.count(), 1);
  EXPECT_EQ(changedRoles(changedSpy),
            (QList<int>{MessageListModel::ProviderTypeRole, MessageListModel::ProviderNameRole}));
  EXPECT_EQ(model.data(model.index(0), MessageListModel::ProviderIdRole).toString(), QStringLiteral("ollama-mac"));
  EXPECT_EQ(model.data(model.index(0), MessageListModel::ModelNameRole).toString(), QStringLiteral("gemma4:12b-mlx"));
  EXPECT_EQ(model.data(model.index(0), MessageListModel::ProviderTypeRole).toString(), QStringLiteral("ollama"));
  EXPECT_EQ(model.data(model.index(0), MessageListModel::ProviderNameRole).toString(), QStringLiteral("Ollama Mac"));
}

TEST(MessageListModel, ProviderStateRefreshesRenameAndDeletionAttribution) {
  const QString providerId = QStringLiteral("ollama-mac");
  holonight_config::ProviderState state{.instances = {{.id = providerId,
                                                       .type = holonight_config::ProviderType::Ollama,
                                                       .display_name = QStringLiteral("Ollama Mac"),
                                                       .enabled = true,
                                                       .settings = holonight_config::OllamaProviderConfig{}}}};
  MessageListModel model(state);
  model.insertNewestMessage(
      Message(MessageId::generate(), MessageRole::Assistant, QStringLiteral("response"), MessageStatus::Complete, {},
              ModelId{.provider_id = providerId, .model_name = QStringLiteral("gemma4:12b-mlx")}));

  state.instances.front().display_name = QStringLiteral("Studio Ollama");
  model.setProviderState(state);
  EXPECT_EQ(model.data(model.index(0), MessageListModel::ProviderNameRole).toString(), QStringLiteral("Studio Ollama"));
  EXPECT_EQ(model.data(model.index(0), MessageListModel::ProviderIdRole).toString(), providerId);

  model.setProviderState(holonight_config::ProviderState{
      .tombstones = {{.instance_id = providerId,
                      .type = holonight_config::ProviderType::Ollama,
                      .last_display_name = QStringLiteral("Studio Ollama (deleted)")}}});
  EXPECT_EQ(model.data(model.index(0), MessageListModel::ProviderNameRole).toString(),
            QStringLiteral("Studio Ollama (deleted)"));
  EXPECT_EQ(model.data(model.index(0), MessageListModel::ProviderTypeRole).toString(), QStringLiteral("ollama"));
  EXPECT_EQ(model.data(model.index(0), MessageListModel::ProviderIdRole).toString(), providerId);
  EXPECT_EQ(model.data(model.index(0), MessageListModel::ModelNameRole).toString(), QStringLiteral("gemma4:12b-mlx"));
}

// T-022: applyUsageRecords() -- sets row usage data for matching message IDs.
TEST(MessageListModel, ApplyUsageRecordsSetsUsageDataForMatchingRow) {
  MessageListModel model;
  const MessageId messageId = MessageId::generate();
  model.insertNewestMessage(
      Message(messageId, MessageRole::Assistant, QStringLiteral("response"), MessageStatus::Complete));

  QMap<QString, holonight_persistence::UsageRecord> records;
  records.insert(messageId.toString(), holonight_persistence::UsageRecord{
                                           .conversation_id = QStringLiteral("conv-1"),
                                           .message_id = messageId.toString(),
                                           .model_identifier = QStringLiteral("gpt-4"),
                                           .input_tokens = 120,
                                           .output_tokens = 45,
                                       });

  model.applyUsageRecords(records);

  EXPECT_EQ(model.data(model.index(0), MessageListModel::InputTokenCountRole).toInt(), 120);
  EXPECT_EQ(model.data(model.index(0), MessageListModel::OutputTokenCountRole).toInt(), 45);
  EXPECT_FALSE(model.data(model.index(0), MessageListModel::ReasoningTokenCountRole).isValid());
}

// T-022: applyUsageRecords() must emit dataChanged for exactly the 7 usage roles, in the
// production order (InputTokenCountRole..DurationMsRole), matching message_list_model.cpp's
// kUsageRoles list so QML bindings on any of the 7 roles are notified.
TEST(MessageListModel, ApplyUsageRecordsEmitsDataChangedForAllSevenUsageRoles) {
  MessageListModel model;
  const MessageId messageId = MessageId::generate();
  model.insertNewestMessage(
      Message(messageId, MessageRole::Assistant, QStringLiteral("response"), MessageStatus::Complete));
  QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);

  QMap<QString, holonight_persistence::UsageRecord> records;
  records.insert(messageId.toString(), holonight_persistence::UsageRecord{
                                           .conversation_id = QStringLiteral("conv-1"),
                                           .message_id = messageId.toString(),
                                           .model_identifier = QStringLiteral("gpt-4"),
                                           .input_tokens = 10,
                                       });

  model.applyUsageRecords(records);

  ASSERT_EQ(changedSpy.count(), 1);
  EXPECT_EQ(changedSpy.constLast().at(0).value<QModelIndex>().row(), 0);
  EXPECT_EQ(changedSpy.constLast().at(1).value<QModelIndex>().row(), 0);
  EXPECT_EQ(changedRoles(changedSpy),
            (QList<int>{MessageListModel::InputTokenCountRole, MessageListModel::OutputTokenCountRole,
                        MessageListModel::ReasoningTokenCountRole, MessageListModel::CacheCreationTokenCountRole,
                        MessageListModel::CacheReadTokenCountRole, MessageListModel::TotalTokenCountRole,
                        MessageListModel::DurationMsRole}));
}

// T-022: a user row with no matching record must stay untouched -- no dataChanged emission and no
// usage data -- while an assistant row in the same records map is still populated correctly.
TEST(MessageListModel, ApplyUsageRecordsDoesNotAffectNonMatchingRows) {
  MessageListModel model;
  model.insertNewestMessage(
      Message(MessageId::generate(), MessageRole::User, QStringLiteral("question"), MessageStatus::Complete));
  const MessageId assistantId = MessageId::generate();
  model.insertNewestMessage(
      Message(assistantId, MessageRole::Assistant, QStringLiteral("answer"), MessageStatus::Complete));
  QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);

  QMap<QString, holonight_persistence::UsageRecord> records;
  records.insert(assistantId.toString(), holonight_persistence::UsageRecord{
                                             .conversation_id = QStringLiteral("conv-1"),
                                             .message_id = assistantId.toString(),
                                             .model_identifier = QStringLiteral("gpt-4"),
                                             .input_tokens = 30,
                                         });

  model.applyUsageRecords(records);

  ASSERT_EQ(changedSpy.count(), 1);
  EXPECT_EQ(changedSpy.constLast().at(0).value<QModelIndex>().row(), 0);
  EXPECT_EQ(model.data(model.index(0), MessageListModel::InputTokenCountRole).toInt(), 30);
  EXPECT_FALSE(model.data(model.index(1), MessageListModel::InputTokenCountRole).isValid());
}

// T-022: a later applyUsageRecords() call for one row must not clear data already applied to a
// different row by an earlier call.
TEST(MessageListModel, ApplyUsageRecordsRepeatedCallsPreserveEarlierMatches) {
  MessageListModel model;
  const MessageId firstId = MessageId::generate();
  model.insertNewestMessage(Message(firstId, MessageRole::Assistant, QStringLiteral("first"), MessageStatus::Complete));
  const MessageId secondId = MessageId::generate();
  model.insertNewestMessage(
      Message(secondId, MessageRole::Assistant, QStringLiteral("second"), MessageStatus::Complete));

  QMap<QString, holonight_persistence::UsageRecord> firstRecords;
  firstRecords.insert(firstId.toString(), holonight_persistence::UsageRecord{
                                              .conversation_id = QStringLiteral("conv-1"),
                                              .message_id = firstId.toString(),
                                              .model_identifier = QStringLiteral("gpt-4"),
                                              .input_tokens = 10,
                                          });
  model.applyUsageRecords(firstRecords);

  QMap<QString, holonight_persistence::UsageRecord> secondRecords;
  secondRecords.insert(secondId.toString(), holonight_persistence::UsageRecord{
                                                .conversation_id = QStringLiteral("conv-1"),
                                                .message_id = secondId.toString(),
                                                .model_identifier = QStringLiteral("gpt-4"),
                                                .input_tokens = 20,
                                            });
  model.applyUsageRecords(secondRecords);

  // secondId was inserted last, so it occupies row 0; firstId occupies row 1.
  EXPECT_EQ(model.data(model.index(1), MessageListModel::InputTokenCountRole).toInt(), 10);
  EXPECT_EQ(model.data(model.index(0), MessageListModel::InputTokenCountRole).toInt(), 20);
}

}  // namespace
}  // namespace holonight_application
