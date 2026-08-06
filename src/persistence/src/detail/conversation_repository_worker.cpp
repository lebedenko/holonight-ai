#include "holonight_persistence/detail/conversation_repository_worker.h"

#include "holonight_persistence/migration_runner.h"
#include "holonight_persistence/usage_record.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

#include <algorithm>
#include <optional>
#include <utility>

namespace holonight_persistence::detail {

namespace {

// last_model_id is stored as "<provider_id>\x1F<model_name>" — 0x1F (unit separator) is not a
// character either identifier is expected to contain.
QString encodeModelId(const holonight_domain::ModelId& modelId) {
  return modelId.provider_id + QChar(0x1F) + modelId.model_name;
}

std::optional<holonight_domain::ModelId> decodeModelId(const QVariant& value) {
  if (value.isNull()) {
    return std::nullopt;
  }
  const QString text = value.toString();
  const qsizetype separatorIndex = text.indexOf(QChar(0x1F));
  if (separatorIndex < 0) {
    return std::nullopt;
  }
  return holonight_domain::ModelId{
      .provider_id = text.left(separatorIndex),
      .model_name = text.mid(separatorIndex + 1),
  };
}

QString titleSourceToText(holonight_domain::TitleSource source) {
  switch (source) {
    case holonight_domain::TitleSource::Fallback:
      return QStringLiteral("Fallback");
    case holonight_domain::TitleSource::Generated:
      return QStringLiteral("Generated");
    case holonight_domain::TitleSource::Manual:
      return QStringLiteral("Manual");
  }
  return QStringLiteral("Manual");
}

holonight_domain::TitleSource titleSourceFromText(const QString& text) {
  if (text == QStringLiteral("Fallback")) {
    return holonight_domain::TitleSource::Fallback;
  }
  if (text == QStringLiteral("Generated")) {
    return holonight_domain::TitleSource::Generated;
  }
  return holonight_domain::TitleSource::Manual;
}

std::optional<QDateTime> decodeOptionalDateTime(const QVariant& value) {
  if (value.isNull()) {
    return std::nullopt;
  }
  return value.toDateTime();
}

ConversationSummary summaryFromRecord(const QSqlQuery& query) {
  return ConversationSummary{
      .id = query.value(0).toString(),
      .title = query.value(1).toString(),
      .created_at = query.value(2).toDateTime(),
      .updated_at = query.value(3).toDateTime(),
      .last_model_id = decodeModelId(query.value(4)),
      .title_source = titleSourceFromText(query.value(5).toString()),
      .pinned_at = decodeOptionalDateTime(query.value(6)),
  };
}

QString messageRoleToText(holonight_domain::MessageRole role) {
  switch (role) {
    case holonight_domain::MessageRole::System:
      return QStringLiteral("system");
    case holonight_domain::MessageRole::User:
      return QStringLiteral("user");
    case holonight_domain::MessageRole::Assistant:
      return QStringLiteral("assistant");
  }
  return QStringLiteral("user");
}

holonight_domain::MessageRole messageRoleFromText(const QString& text) {
  if (text == QStringLiteral("assistant")) {
    return holonight_domain::MessageRole::Assistant;
  }
  if (text == QStringLiteral("system")) {
    return holonight_domain::MessageRole::System;
  }
  return holonight_domain::MessageRole::User;
}

QString messageStatusToText(holonight_domain::MessageStatus status) {
  switch (status) {
    case holonight_domain::MessageStatus::Pending:
      return QStringLiteral("pending");
    case holonight_domain::MessageStatus::Streaming:
      return QStringLiteral("streaming");
    case holonight_domain::MessageStatus::Complete:
      return QStringLiteral("complete");
    case holonight_domain::MessageStatus::Error:
      return QStringLiteral("error");
    case holonight_domain::MessageStatus::Cancelled:
      return QStringLiteral("cancelled");
  }
  return QStringLiteral("pending");
}

holonight_domain::MessageStatus messageStatusFromText(const QString& text) {
  if (text == QStringLiteral("streaming")) {
    return holonight_domain::MessageStatus::Streaming;
  }
  if (text == QStringLiteral("complete")) {
    return holonight_domain::MessageStatus::Complete;
  }
  if (text == QStringLiteral("error")) {
    return holonight_domain::MessageStatus::Error;
  }
  if (text == QStringLiteral("cancelled")) {
    return holonight_domain::MessageStatus::Cancelled;
  }
  return holonight_domain::MessageStatus::Pending;
}

// A default-constructed QString() (e.g. an Assistant placeholder's empty text) has isNull() ==
// true; Qt's SQLite driver binds a null QVariant as SQL NULL, not an empty string, which would
// violate messages.content's NOT NULL constraint. Normalize before every content bind.
QString normalizeNullToEmpty(const QString& text) { return text.isNull() ? QStringLiteral("") : text; }

QString toolCallKindToText(holonight_domain::ToolCallKind kind) {
  switch (kind) {
    case holonight_domain::ToolCallKind::Invocation:
      return QStringLiteral("invocation");
    case holonight_domain::ToolCallKind::Result:
      return QStringLiteral("result");
  }
  return QStringLiteral("invocation");
}

QString toolCallStatusToText(holonight_domain::ToolInvocationStatus status) {
  switch (status) {
    case holonight_domain::ToolInvocationStatus::Requested:
      return QStringLiteral("requested");
    case holonight_domain::ToolInvocationStatus::AwaitingApproval:
      return QStringLiteral("awaiting_approval");
    case holonight_domain::ToolInvocationStatus::Running:
      return QStringLiteral("running");
    case holonight_domain::ToolInvocationStatus::Completed:
      return QStringLiteral("completed");
    case holonight_domain::ToolInvocationStatus::Failed:
      return QStringLiteral("failed");
    case holonight_domain::ToolInvocationStatus::Denied:
      return QStringLiteral("denied");
    case holonight_domain::ToolInvocationStatus::Cancelled:
      return QStringLiteral("cancelled");
  }
  return QStringLiteral("requested");
}

holonight_domain::ToolInvocationStatus toolCallStatusFromText(const QString& text) {
  if (text == QStringLiteral("awaiting_approval")) {
    return holonight_domain::ToolInvocationStatus::AwaitingApproval;
  }
  if (text == QStringLiteral("running")) {
    return holonight_domain::ToolInvocationStatus::Running;
  }
  if (text == QStringLiteral("completed")) {
    return holonight_domain::ToolInvocationStatus::Completed;
  }
  if (text == QStringLiteral("failed")) {
    return holonight_domain::ToolInvocationStatus::Failed;
  }
  if (text == QStringLiteral("denied")) {
    return holonight_domain::ToolInvocationStatus::Denied;
  }
  if (text == QStringLiteral("cancelled")) {
    return holonight_domain::ToolInvocationStatus::Cancelled;
  }
  return holonight_domain::ToolInvocationStatus::Requested;
}

QString toolExecutionLocationToText(holonight_domain::ToolExecutionLocation location) {
  switch (location) {
    case holonight_domain::ToolExecutionLocation::LocalClient:
      return QStringLiteral("local_client");
    case holonight_domain::ToolExecutionLocation::ProviderHosted:
      return QStringLiteral("provider_hosted");
    case holonight_domain::ToolExecutionLocation::RemoteExternal:
      return QStringLiteral("remote_external");
  }
  return QStringLiteral("local_client");
}

holonight_domain::ToolExecutionLocation toolExecutionLocationFromText(const QString& text) {
  if (text == QStringLiteral("provider_hosted")) {
    return holonight_domain::ToolExecutionLocation::ProviderHosted;
  }
  if (text == QStringLiteral("remote_external")) {
    return holonight_domain::ToolExecutionLocation::RemoteExternal;
  }
  return holonight_domain::ToolExecutionLocation::LocalClient;
}

std::optional<QDateTime> decodeOptionalDateTimeFromValue(const QJsonValue& value) {
  if (!value.isString()) {
    return std::nullopt;
  }
  const QString text = value.toString();
  if (text.isEmpty()) {
    return std::nullopt;
  }
  QDateTime parsed = QDateTime::fromString(text, Qt::ISODateWithMs);
  if (!parsed.isValid()) {
    parsed = QDateTime::fromString(text, Qt::ISODate);
  }
  if (!parsed.isValid()) {
    return std::nullopt;
  }
  return parsed;
}

holonight_domain::ToolCallKind toolCallKindFromText(const QString& text) {
  if (text == QStringLiteral("result")) {
    return holonight_domain::ToolCallKind::Result;
  }
  return holonight_domain::ToolCallKind::Invocation;
}

// tool_calls is stored as a JSON-array-in-a-TEXT-column; NULL (empty QVariant) means "no tool
// calls on this message" -- the common case for every ordinary text message.
QVariant encodeToolCalls(const std::vector<holonight_domain::ToolCallEntry>& entries) {
  if (entries.empty()) {
    return {};
  }
  QJsonArray array;
  for (const auto& entry : entries) {
    QJsonObject object{{QStringLiteral("kind"), toolCallKindToText(entry.kind)},
                       {QStringLiteral("tool_use_id"), entry.tool_use_id}};
    if (!entry.tool_id.isEmpty()) {
      object[QStringLiteral("tool_id")] = entry.tool_id;
    }
    if (!entry.function_name.isEmpty()) {
      object[QStringLiteral("function_name")] = entry.function_name;
    }
    object[QStringLiteral("status")] = toolCallStatusToText(entry.status);
    object[QStringLiteral("execution_location")] = toolExecutionLocationToText(entry.execution_location);
    if (entry.can_cancel) {
      object[QStringLiteral("can_cancel")] = true;
    }
    if (entry.thought_signature.has_value()) {
      object[QStringLiteral("thought_signature")] = *entry.thought_signature;
    }
    if (entry.provider_call_id_synthesized) {
      object[QStringLiteral("provider_call_id_synthesized")] = true;
    }
    if (entry.provider_item_id.has_value()) {
      object[QStringLiteral("provider_item_id")] = *entry.provider_item_id;
    }
    if (!entry.provider_context.empty()) {
      QJsonArray context;
      for (const auto& item : entry.provider_context) {
        context.append(item);
      }
      object[QStringLiteral("provider_context")] = context;
    }
    if (entry.requested_at.isValid()) {
      object[QStringLiteral("requested_at")] = entry.requested_at.toString(Qt::ISODateWithMs);
    }
    if (entry.started_at.has_value() && entry.started_at->isValid()) {
      object[QStringLiteral("started_at")] = entry.started_at->toString(Qt::ISODateWithMs);
    }
    if (entry.finished_at.has_value() && entry.finished_at->isValid()) {
      object[QStringLiteral("finished_at")] = entry.finished_at->toString(Qt::ISODateWithMs);
    }
    if (entry.kind == holonight_domain::ToolCallKind::Invocation) {
      object[QStringLiteral("tool_name")] = entry.tool_name;
      object[QStringLiteral("input")] = entry.input;
    } else {
      object[QStringLiteral("result")] = entry.result;
      object[QStringLiteral("is_error")] = entry.is_error;
    }
    array.append(object);
  }
  return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

std::vector<holonight_domain::ToolCallEntry> decodeToolCalls(const QVariant& value) {
  std::vector<holonight_domain::ToolCallEntry> entries;
  if (value.isNull()) {
    return entries;
  }
  const QJsonDocument document = QJsonDocument::fromJson(value.toString().toUtf8());
  if (!document.isArray()) {
    return entries;
  }
  for (const auto& item : document.array()) {
    const QJsonObject object = item.toObject();
    holonight_domain::ToolCallEntry entry{
        .kind = toolCallKindFromText(object.value(QStringLiteral("kind")).toString()),
        .tool_use_id = object.value(QStringLiteral("tool_use_id")).toString(),
        .tool_name = object.value(QStringLiteral("tool_name")).toString(),
        .input = object.value(QStringLiteral("input")).toObject(),
        .result = object.value(QStringLiteral("result")).toObject(),
        .is_error = object.value(QStringLiteral("is_error")).toBool(),
    };
    entry.tool_id = object.value(QStringLiteral("tool_id")).toString();
    const QString functionName = object.value(QStringLiteral("function_name")).toString();
    entry.function_name = functionName.isEmpty() ? entry.tool_name : functionName;
    entry.status = toolCallStatusFromText(object.value(QStringLiteral("status")).toString());
    entry.execution_location =
        toolExecutionLocationFromText(object.value(QStringLiteral("execution_location")).toString());
    entry.can_cancel = object.value(QStringLiteral("can_cancel")).toBool();
    if (object.contains(QStringLiteral("thought_signature"))) {
      entry.thought_signature = object.value(QStringLiteral("thought_signature")).toString();
    }
    entry.provider_call_id_synthesized = object.value(QStringLiteral("provider_call_id_synthesized")).toBool();
    if (object.contains(QStringLiteral("provider_item_id"))) {
      entry.provider_item_id = object.value(QStringLiteral("provider_item_id")).toString();
    }
    for (const auto& contextItem : object.value(QStringLiteral("provider_context")).toArray()) {
      if (contextItem.isObject()) {
        entry.provider_context.push_back(contextItem.toObject());
      }
    }
    if (const std::optional<QDateTime> requestedAt =
            decodeOptionalDateTimeFromValue(object.value(QStringLiteral("requested_at")));
        requestedAt.has_value()) {
      entry.requested_at = *requestedAt;
    }
    entry.started_at = decodeOptionalDateTimeFromValue(object.value(QStringLiteral("started_at")));
    entry.finished_at = decodeOptionalDateTimeFromValue(object.value(QStringLiteral("finished_at")));
    entries.push_back(entry);
  }
  return entries;
}

bool isNonTerminalToolStatus(holonight_domain::ToolInvocationStatus status) {
  return status == holonight_domain::ToolInvocationStatus::Requested ||
         status == holonight_domain::ToolInvocationStatus::AwaitingApproval ||
         status == holonight_domain::ToolInvocationStatus::Running;
}

void settleInterruptedToolInvocations(std::vector<holonight_domain::Message>& messages) {
  QSet<QString> completedCallIds;
  for (const auto& message : messages) {
    for (const auto& entry : message.toolCalls()) {
      if (entry.kind == holonight_domain::ToolCallKind::Result && !entry.tool_use_id.isEmpty()) {
        completedCallIds.insert(entry.tool_use_id);
      }
    }
  }

  for (auto& message : messages) {
    std::vector<holonight_domain::ToolCallEntry> entries = message.toolCalls();
    bool changed = false;
    for (auto& entry : entries) {
      if (entry.kind != holonight_domain::ToolCallKind::Invocation || !isNonTerminalToolStatus(entry.status)) {
        continue;
      }
      if (!entry.tool_use_id.isEmpty() && completedCallIds.contains(entry.tool_use_id)) {
        continue;
      }
      entry.status = holonight_domain::ToolInvocationStatus::Cancelled;
      changed = true;
    }
    if (changed) {
      message.setToolCalls(std::move(entries));
    }
  }
}

holonight_domain::Message messageFromRecord(const QSqlQuery& query) {
  return {holonight_domain::MessageId::fromString(query.value(0).toString()),
          messageRoleFromText(query.value(1).toString()),
          query.value(2).toString(),
          messageStatusFromText(query.value(3).toString()),
          query.value(4).toDateTime(),
          decodeModelId(query.value(5)),
          decodeToolCalls(query.value(6))};
}

template <typename T>
std::optional<T> decodeOptionalNumeric(const QVariant& value) {
  if (value.isNull()) {
    return std::nullopt;
  }
  return value.value<T>();
}

UsageRecord usageRecordFromRecord(const QSqlQuery& query) {
  return UsageRecord{
      .conversation_id = query.value(QStringLiteral("conversation_id")).toString(),
      .message_id = query.value(QStringLiteral("message_id")).toString(),
      .model_identifier = query.value(QStringLiteral("model_identifier")).toString(),
      .input_tokens = decodeOptionalNumeric<int>(query.value(QStringLiteral("input_tokens"))),
      .output_tokens = decodeOptionalNumeric<int>(query.value(QStringLiteral("output_tokens"))),
      .reasoning_tokens = decodeOptionalNumeric<int>(query.value(QStringLiteral("reasoning_tokens"))),
      .cache_creation_tokens = decodeOptionalNumeric<int>(query.value(QStringLiteral("cache_creation_tokens"))),
      .cache_read_tokens = decodeOptionalNumeric<int>(query.value(QStringLiteral("cache_read_tokens"))),
      .total_tokens = decodeOptionalNumeric<int>(query.value(QStringLiteral("total_tokens"))),
      .duration_ms = decodeOptionalNumeric<qint64>(query.value(QStringLiteral("duration_ms"))),
      .ollama_total_duration_ns =
          decodeOptionalNumeric<qint64>(query.value(QStringLiteral("ollama_total_duration_ns"))),
      .ollama_load_duration_ns = decodeOptionalNumeric<qint64>(query.value(QStringLiteral("ollama_load_duration_ns"))),
      .ollama_prompt_eval_duration_ns =
          decodeOptionalNumeric<qint64>(query.value(QStringLiteral("ollama_prompt_eval_duration_ns"))),
      .ollama_eval_duration_ns = decodeOptionalNumeric<qint64>(query.value(QStringLiteral("ollama_eval_duration_ns"))),
  };
}

}  // namespace

ConversationRepositoryWorker::ConversationRepositoryWorker(QString databasePath, QString connectionName,
                                                           QObject* parent)
    : QObject(parent), database_path_(std::move(databasePath)), connection_name_(std::move(connectionName)) {}

ConversationRepositoryWorker::~ConversationRepositoryWorker() {
  // QSqlDatabase::removeDatabase() requires every QSqlDatabase handle to this connection —
  // including this function's own local variable — to be out of scope first, or Qt logs
  // "connection '...' is still in use" even though nothing is actually still querying it.
  {
    QSqlDatabase database = QSqlDatabase::database(connection_name_, false);
    if (database.isValid() && database.isOpen()) {
      database.close();
    }
  }
  QSqlDatabase::removeDatabase(connection_name_);
}

QSqlDatabase ConversationRepositoryWorker::connection() { return QSqlDatabase::database(connection_name_); }

QDateTime ConversationRepositoryWorker::nextOperationTimestamp() {
  QDateTime timestamp = QDateTime::currentDateTimeUtc();
  if (last_operation_timestamp_.isValid() && timestamp <= last_operation_timestamp_) {
    timestamp = last_operation_timestamp_.addMSecs(1);
  }
  last_operation_timestamp_ = timestamp;
  return timestamp;
}

void ConversationRepositoryWorker::markUnavailable(const QString& reason) {
  available_ = false;
  QSqlDatabase database = QSqlDatabase::database(connection_name_, false);
  if (database.isValid() && database.isOpen()) {
    database.close();
  }
  emit unavailable(reason);
}

bool ConversationRepositoryWorker::isFatal(const QSqlError& sqlError) {
  return sqlError.type() == QSqlError::ConnectionError;
}

void ConversationRepositoryWorker::reportOperationError(const QString& conversationId, const QSqlError& sqlError) {
  if (isFatal(sqlError) || !connection().isOpen()) {
    markUnavailable(sqlError.text());
    return;
  }
  emit error(conversationId, sqlError.text());
}

void ConversationRepositoryWorker::openAndMigrate() {
  QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection_name_);
  database.setDatabaseName(database_path_);

  if (!database.open()) {
    markUnavailable(database.lastError().text());
    return;
  }

  QSqlQuery pragma(database);
  pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"));

  const auto result = MigrationRunner::apply(database, MigrationRunner::builtInMigrations());
  if (!result.has_value()) {
    markUnavailable(result.error());
    return;
  }

  QSqlQuery latestTimestamp(database);
  if (latestTimestamp.exec(QStringLiteral("SELECT MAX(updated_at) FROM conversations")) && latestTimestamp.next()) {
    last_operation_timestamp_ = latestTimestamp.value(0).toDateTime();
  }

  available_ = true;
  emit initialized();
}

void ConversationRepositoryWorker::createConversation(const QString& title) {
  if (!available_) {
    return;
  }

  const QString conversationId = holonight_domain::ConversationId::generate().toString();
  const QDateTime now = nextOperationTimestamp();
  const QString resolvedTitle = title.isEmpty() ? kDefaultConversationTitle : title;

  QSqlQuery query(connection());
  query.prepare(
      QStringLiteral("INSERT INTO conversations (id, title, created_at, updated_at, last_model_id, title_source) "
                     "VALUES (:id, :title, :created_at, :updated_at, NULL, :title_source)"));
  query.bindValue(QStringLiteral(":id"), conversationId);
  query.bindValue(QStringLiteral(":title"), resolvedTitle);
  query.bindValue(QStringLiteral(":created_at"), now);
  query.bindValue(QStringLiteral(":updated_at"), now);
  query.bindValue(QStringLiteral(":title_source"), titleSourceToText(holonight_domain::TitleSource::Fallback));
  if (!query.exec()) {
    reportOperationError(conversationId, query.lastError());
    return;
  }

  emit conversationCreated(ConversationSummary{
      .id = conversationId,
      .title = resolvedTitle,
      .created_at = now,
      .updated_at = now,
      .last_model_id = std::nullopt,
      .title_source = holonight_domain::TitleSource::Fallback,
  });
}

void ConversationRepositoryWorker::materializeConversation(
    const ConversationSummary& conversation, const std::vector<holonight_domain::Message>& initialMessages) {
  if (!available_) {
    return;
  }

  QSqlDatabase database = connection();
  if (!database.transaction()) {
    reportOperationError(conversation.id, database.lastError());
    return;
  }

  QSqlQuery conversationQuery(database);
  conversationQuery.prepare(
      QStringLiteral("INSERT INTO conversations (id, title, created_at, updated_at, last_model_id, title_source) "
                     "VALUES (:id, :title, :created_at, :updated_at, :last_model_id, :title_source)"));
  conversationQuery.bindValue(QStringLiteral(":id"), conversation.id);
  conversationQuery.bindValue(QStringLiteral(":title"), conversation.title);
  conversationQuery.bindValue(QStringLiteral(":created_at"), conversation.created_at);
  conversationQuery.bindValue(QStringLiteral(":updated_at"), conversation.updated_at);
  conversationQuery.bindValue(
      QStringLiteral(":last_model_id"),
      conversation.last_model_id ? QVariant(encodeModelId(*conversation.last_model_id)) : QVariant());
  conversationQuery.bindValue(QStringLiteral(":title_source"), titleSourceToText(conversation.title_source));
  if (!conversationQuery.exec()) {
    database.rollback();
    reportOperationError(conversation.id, conversationQuery.lastError());
    return;
  }

  for (const holonight_domain::Message& message : initialMessages) {
    QSqlQuery messageQuery(database);
    messageQuery.prepare(QStringLiteral(
        "INSERT INTO messages (id, conversation_id, role, content, status, created_at, model_id, tool_calls) "
        "VALUES (:id, :conversation_id, :role, :content, :status, :created_at, :model_id, :tool_calls)"));
    messageQuery.bindValue(QStringLiteral(":id"), message.id().toString());
    messageQuery.bindValue(QStringLiteral(":conversation_id"), conversation.id);
    messageQuery.bindValue(QStringLiteral(":role"), messageRoleToText(message.role()));
    messageQuery.bindValue(QStringLiteral(":content"), normalizeNullToEmpty(message.text()));
    messageQuery.bindValue(QStringLiteral(":status"), messageStatusToText(message.status()));
    messageQuery.bindValue(QStringLiteral(":created_at"), message.createdAt());
    messageQuery.bindValue(QStringLiteral(":model_id"),
                           message.modelId() ? QVariant(encodeModelId(*message.modelId())) : QVariant());
    messageQuery.bindValue(QStringLiteral(":tool_calls"), encodeToolCalls(message.toolCalls()));
    if (!messageQuery.exec()) {
      database.rollback();
      reportOperationError(conversation.id, messageQuery.lastError());
      return;
    }
  }

  if (!database.commit()) {
    database.rollback();
    reportOperationError(conversation.id, database.lastError());
    return;
  }

  last_operation_timestamp_ = std::max(last_operation_timestamp_, conversation.updated_at);
  emit conversationCreated(conversation);
}

void ConversationRepositoryWorker::listConversations() {
  if (!available_) {
    return;
  }

  QSqlQuery query(connection());
  if (!query.exec(QStringLiteral(
          "SELECT id, title, created_at, updated_at, last_model_id, title_source, pinned_at FROM conversations "
          "ORDER BY (pinned_at IS NULL) ASC, pinned_at DESC, updated_at DESC, rowid DESC"))) {
    reportOperationError(QString(), query.lastError());
    return;
  }

  QList<ConversationSummary> conversations;
  while (query.next()) {
    conversations.append(summaryFromRecord(query));
  }
  emit conversationListLoaded(conversations);
}

void ConversationRepositoryWorker::loadConversation(const QString& conversationId) {
  if (!available_) {
    return;
  }

  QSqlQuery conversationQuery(connection());
  conversationQuery.prepare(QStringLiteral(
      "SELECT id, title, created_at, updated_at, last_model_id, title_source, pinned_at FROM conversations "
      "WHERE id = :id"));
  conversationQuery.bindValue(QStringLiteral(":id"), conversationId);
  if (!conversationQuery.exec()) {
    reportOperationError(conversationId, conversationQuery.lastError());
    return;
  }
  if (!conversationQuery.next()) {
    emit error(conversationId, QStringLiteral("conversation not found"));
    return;
  }
  const ConversationSummary summary = summaryFromRecord(conversationQuery);

  // A process cannot resume the network request represented by an assistant placeholder. Convert
  // abandoned work to a terminal state before exposing it to the application; otherwise the
  // restored conversation remains permanently non-sendable with no in-flight request to stop.
  QSqlQuery settleAbandoned(connection());
  settleAbandoned.prepare(
      QStringLiteral("UPDATE messages SET status = 'cancelled' WHERE conversation_id = :id "
                     "AND role = 'assistant' AND status IN ('pending', 'streaming')"));
  settleAbandoned.bindValue(QStringLiteral(":id"), conversationId);
  if (!settleAbandoned.exec()) {
    reportOperationError(conversationId, settleAbandoned.lastError());
    return;
  }

  QSqlQuery messagesQuery(connection());
  messagesQuery.prepare(QStringLiteral(
      "SELECT id, role, content, status, created_at, model_id, tool_calls FROM messages WHERE conversation_id = :id "
      "ORDER BY created_at ASC, rowid ASC"));
  messagesQuery.bindValue(QStringLiteral(":id"), conversationId);
  if (!messagesQuery.exec()) {
    reportOperationError(conversationId, messagesQuery.lastError());
    return;
  }

  std::vector<holonight_domain::Message> messages;
  while (messagesQuery.next()) {
    messages.push_back(messageFromRecord(messagesQuery));
  }
  settleInterruptedToolInvocations(messages);

  emit conversationLoaded(LoadedConversation{.summary = summary, .messages = std::move(messages)});
}

void ConversationRepositoryWorker::renameConversation(const QString& conversationId, const QString& newTitle,
                                                      holonight_domain::TitleSource titleSource) {
  if (!available_) {
    return;
  }

  const QDateTime now = nextOperationTimestamp();
  QSqlQuery query(connection());
  // Generated writes are guarded: a manual rename racing in during title generation must win, so
  // the write only lands while the row is still at title_source = 'Fallback' (DESIGN.md §4.6).
  // Manual/Fallback writes are unconditional, matching pre-existing rename semantics.
  if (titleSource == holonight_domain::TitleSource::Generated) {
    query.prepare(
        QStringLiteral("UPDATE conversations SET title = :title, title_source = :title_source, "
                       "updated_at = :updated_at WHERE id = :id AND title_source = 'Fallback'"));
  } else {
    query.prepare(
        QStringLiteral("UPDATE conversations SET title = :title, title_source = :title_source, "
                       "updated_at = :updated_at WHERE id = :id"));
  }
  query.bindValue(QStringLiteral(":title"), newTitle);
  query.bindValue(QStringLiteral(":title_source"), titleSourceToText(titleSource));
  query.bindValue(QStringLiteral(":updated_at"), now);
  query.bindValue(QStringLiteral(":id"), conversationId);
  if (!query.exec()) {
    reportOperationError(conversationId, query.lastError());
    return;
  }
  if (titleSource == holonight_domain::TitleSource::Generated && query.numRowsAffected() == 0) {
    // Lost the race to a manual rename that already flipped title_source away from Fallback;
    // that writer already emitted conversationRenamed, so this is a silent no-op.
    return;
  }

  QSqlQuery selectLastModelId(connection());
  selectLastModelId.prepare(QStringLiteral("SELECT created_at, last_model_id FROM conversations WHERE id = :id"));
  selectLastModelId.bindValue(QStringLiteral(":id"), conversationId);
  if (!selectLastModelId.exec() || !selectLastModelId.next()) {
    reportOperationError(conversationId, selectLastModelId.lastError());
    return;
  }

  emit conversationRenamed(ConversationSummary{
      .id = conversationId,
      .title = newTitle,
      .created_at = selectLastModelId.value(0).toDateTime(),
      .updated_at = now,
      .last_model_id = decodeModelId(selectLastModelId.value(1)),
      .title_source = titleSource,
  });
}

void ConversationRepositoryWorker::deleteConversation(const QString& conversationId) {
  if (!available_) {
    return;
  }

  QSqlDatabase database = connection();
  if (!database.transaction()) {
    reportOperationError(conversationId, database.lastError());
    return;
  }

  QSqlQuery deleteMessages(database);
  deleteMessages.prepare(QStringLiteral("DELETE FROM messages WHERE conversation_id = :id"));
  deleteMessages.bindValue(QStringLiteral(":id"), conversationId);
  if (!deleteMessages.exec()) {
    database.rollback();
    reportOperationError(conversationId, deleteMessages.lastError());
    return;
  }

  QSqlQuery deleteConversationRow(database);
  deleteConversationRow.prepare(QStringLiteral("DELETE FROM conversations WHERE id = :id"));
  deleteConversationRow.bindValue(QStringLiteral(":id"), conversationId);
  if (!deleteConversationRow.exec()) {
    database.rollback();
    reportOperationError(conversationId, deleteConversationRow.lastError());
    return;
  }

  if (!database.commit()) {
    database.rollback();
    reportOperationError(conversationId, database.lastError());
    return;
  }

  emit conversationDeleted(conversationId);
}

void ConversationRepositoryWorker::pinConversation(const QString& conversationId) {
  if (!available_) {
    return;
  }

  QSqlQuery query(connection());
  query.prepare(QStringLiteral("UPDATE conversations SET pinned_at = CURRENT_TIMESTAMP WHERE id = :id"));
  query.bindValue(QStringLiteral(":id"), conversationId);
  if (!query.exec()) {
    reportOperationError(conversationId, query.lastError());
    return;
  }
  if (query.numRowsAffected() == 0) {
    emit error(conversationId, QStringLiteral("conversation not found"));
    return;
  }

  QSqlQuery selectPinnedAt(connection());
  selectPinnedAt.prepare(QStringLiteral("SELECT pinned_at FROM conversations WHERE id = :id"));
  selectPinnedAt.bindValue(QStringLiteral(":id"), conversationId);
  if (!selectPinnedAt.exec()) {
    reportOperationError(conversationId, selectPinnedAt.lastError());
    return;
  }
  if (!selectPinnedAt.next()) {
    emit error(conversationId, QStringLiteral("conversation not found"));
    return;
  }

  emit conversationPinned(conversationId, selectPinnedAt.value(0).toDateTime());
}

void ConversationRepositoryWorker::unpinConversation(const QString& conversationId) {
  if (!available_) {
    return;
  }

  QSqlQuery query(connection());
  query.prepare(QStringLiteral("UPDATE conversations SET pinned_at = NULL WHERE id = :id"));
  query.bindValue(QStringLiteral(":id"), conversationId);
  if (!query.exec()) {
    reportOperationError(conversationId, query.lastError());
    return;
  }
  if (query.numRowsAffected() == 0) {
    emit error(conversationId, QStringLiteral("conversation not found"));
    return;
  }

  emit conversationUnpinned(conversationId);
}

void ConversationRepositoryWorker::updateLastModelId(const QString& conversationId,
                                                     const holonight_domain::ModelId& modelId) {
  if (!available_) {
    return;
  }

  const QDateTime now = nextOperationTimestamp();
  QSqlQuery query(connection());
  query.prepare(QStringLiteral(
      "UPDATE conversations SET last_model_id = :last_model_id, updated_at = :updated_at WHERE id = :id"));
  query.bindValue(QStringLiteral(":last_model_id"), encodeModelId(modelId));
  query.bindValue(QStringLiteral(":updated_at"), now);
  query.bindValue(QStringLiteral(":id"), conversationId);
  if (!query.exec()) {
    reportOperationError(conversationId, query.lastError());
    return;
  }

  emit lastModelIdUpdated(conversationId, modelId, now);
}

void ConversationRepositoryWorker::persistNewMessage(const QString& conversationId,
                                                     const holonight_domain::Message& message) {
  if (!available_) {
    return;
  }

  QSqlQuery query(connection());
  query.prepare(QStringLiteral(
      "INSERT INTO messages (id, conversation_id, role, content, status, created_at, model_id, tool_calls) "
      "VALUES (:id, :conversation_id, :role, :content, :status, :created_at, :model_id, :tool_calls)"));
  query.bindValue(QStringLiteral(":id"), message.id().toString());
  query.bindValue(QStringLiteral(":conversation_id"), conversationId);
  query.bindValue(QStringLiteral(":role"), messageRoleToText(message.role()));
  query.bindValue(QStringLiteral(":content"), normalizeNullToEmpty(message.text()));
  query.bindValue(QStringLiteral(":status"), messageStatusToText(message.status()));
  query.bindValue(QStringLiteral(":created_at"), message.createdAt());
  query.bindValue(QStringLiteral(":model_id"),
                  message.modelId() ? QVariant(encodeModelId(*message.modelId())) : QVariant());
  query.bindValue(QStringLiteral(":tool_calls"), encodeToolCalls(message.toolCalls()));
  if (!query.exec()) {
    reportOperationError(conversationId, query.lastError());
  }
}

void ConversationRepositoryWorker::persistUsage(const QString& conversationId, const QString& messageId,
                                                const QString& modelIdentifier, const holonight_domain::Usage& usage) {
  if (!available_) {
    return;
  }

  const UsageRecord record = toUsageRecord(conversationId, messageId, modelIdentifier, usage);

  QSqlQuery query(connection());
  query.prepare(QStringLiteral(
      "INSERT INTO usage (id, message_id, conversation_id, model_identifier, input_tokens, output_tokens, "
      "reasoning_tokens, cache_creation_tokens, cache_read_tokens, total_tokens, duration_ms, "
      "ollama_total_duration_ns, ollama_load_duration_ns, ollama_prompt_eval_duration_ns, ollama_eval_duration_ns, "
      "created_at) "
      "VALUES (:id, :message_id, :conversation_id, :model_identifier, :input_tokens, :output_tokens, "
      ":reasoning_tokens, :cache_creation_tokens, :cache_read_tokens, :total_tokens, :duration_ms, "
      ":ollama_total_duration_ns, :ollama_load_duration_ns, :ollama_prompt_eval_duration_ns, "
      ":ollama_eval_duration_ns, :created_at) "
      "ON CONFLICT(message_id) DO UPDATE SET "
      "conversation_id = excluded.conversation_id, model_identifier = excluded.model_identifier, "
      "input_tokens = excluded.input_tokens, output_tokens = excluded.output_tokens, "
      "reasoning_tokens = excluded.reasoning_tokens, cache_creation_tokens = excluded.cache_creation_tokens, "
      "cache_read_tokens = excluded.cache_read_tokens, total_tokens = excluded.total_tokens, "
      "duration_ms = excluded.duration_ms, ollama_total_duration_ns = excluded.ollama_total_duration_ns, "
      "ollama_load_duration_ns = excluded.ollama_load_duration_ns, "
      "ollama_prompt_eval_duration_ns = excluded.ollama_prompt_eval_duration_ns, "
      "ollama_eval_duration_ns = excluded.ollama_eval_duration_ns, created_at = excluded.created_at"));
  query.bindValue(QStringLiteral(":id"), QUuid::createUuid().toString(QUuid::WithoutBraces));
  query.bindValue(QStringLiteral(":message_id"), record.message_id);
  query.bindValue(QStringLiteral(":conversation_id"), record.conversation_id);
  query.bindValue(QStringLiteral(":model_identifier"), record.model_identifier);
  query.bindValue(QStringLiteral(":input_tokens"), record.input_tokens ? QVariant(*record.input_tokens) : QVariant());
  query.bindValue(QStringLiteral(":output_tokens"),
                  record.output_tokens ? QVariant(*record.output_tokens) : QVariant());
  query.bindValue(QStringLiteral(":reasoning_tokens"),
                  record.reasoning_tokens ? QVariant(*record.reasoning_tokens) : QVariant());
  query.bindValue(QStringLiteral(":cache_creation_tokens"),
                  record.cache_creation_tokens ? QVariant(*record.cache_creation_tokens) : QVariant());
  query.bindValue(QStringLiteral(":cache_read_tokens"),
                  record.cache_read_tokens ? QVariant(*record.cache_read_tokens) : QVariant());
  query.bindValue(QStringLiteral(":total_tokens"), record.total_tokens ? QVariant(*record.total_tokens) : QVariant());
  query.bindValue(QStringLiteral(":duration_ms"), record.duration_ms ? QVariant(*record.duration_ms) : QVariant());
  query.bindValue(QStringLiteral(":ollama_total_duration_ns"),
                  record.ollama_total_duration_ns ? QVariant(*record.ollama_total_duration_ns) : QVariant());
  query.bindValue(QStringLiteral(":ollama_load_duration_ns"),
                  record.ollama_load_duration_ns ? QVariant(*record.ollama_load_duration_ns) : QVariant());
  query.bindValue(
      QStringLiteral(":ollama_prompt_eval_duration_ns"),
      record.ollama_prompt_eval_duration_ns ? QVariant(*record.ollama_prompt_eval_duration_ns) : QVariant());
  query.bindValue(QStringLiteral(":ollama_eval_duration_ns"),
                  record.ollama_eval_duration_ns ? QVariant(*record.ollama_eval_duration_ns) : QVariant());
  query.bindValue(QStringLiteral(":created_at"), nextOperationTimestamp());
  if (!query.exec()) {
    reportOperationError(conversationId, query.lastError());
    return;
  }

  emit usagePersisted(conversationId, messageId, record);
}

void ConversationRepositoryWorker::usageForConversation(const QString& conversationId) {
  if (!available_) {
    return;
  }

  QSqlQuery query(connection());
  query.prepare(QStringLiteral("SELECT * FROM usage WHERE conversation_id = :conversation_id ORDER BY message_id ASC"));
  query.bindValue(QStringLiteral(":conversation_id"), conversationId);
  if (!query.exec()) {
    reportOperationError(conversationId, query.lastError());
    return;
  }

  QMap<QString, UsageRecord> usageByMessageId;
  while (query.next()) {
    UsageRecord record = usageRecordFromRecord(query);
    usageByMessageId.insert(record.message_id, record);
  }

  emit usageForConversationLoaded(conversationId, usageByMessageId);
}

void ConversationRepositoryWorker::persistMessageSettled(const QString& conversationId,
                                                         const holonight_domain::Message& message) {
  if (!available_) {
    return;
  }

  QSqlQuery query(connection());
  query.prepare(
      QStringLiteral("UPDATE messages SET content = :content, status = :status, model_id = :model_id WHERE id = :id"));
  query.bindValue(QStringLiteral(":content"), normalizeNullToEmpty(message.text()));
  query.bindValue(QStringLiteral(":status"), messageStatusToText(message.status()));
  query.bindValue(QStringLiteral(":model_id"),
                  message.modelId() ? QVariant(encodeModelId(*message.modelId())) : QVariant());
  query.bindValue(QStringLiteral(":id"), message.id().toString());
  if (!query.exec()) {
    reportOperationError(conversationId, query.lastError());
  }
}

}  // namespace holonight_persistence::detail
