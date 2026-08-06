#include "holonight_application/message_list_model.h"

#include "holonight_application/message_content_model.h"
#include "holonight_application/provider_instance_registry.h"
#include "holonight_application/tools/tool_presenters.h"
#include "holonight_application/tools/tool_registry.h"

#include <QJsonDocument>
#include <QModelIndex>
#include <QVariant>

namespace holonight_application {

using holonight_domain::Message;
using holonight_domain::MessageRole;
using holonight_domain::MessageStatus;

namespace {

QString roleToString(MessageRole role) {
  switch (role) {
    case MessageRole::System:
      return QStringLiteral("system");
    case MessageRole::User:
      return QStringLiteral("user");
    case MessageRole::Assistant:
      return QStringLiteral("assistant");
  }
  return QStringLiteral("unknown");
}

QString statusToString(MessageStatus status) {
  switch (status) {
    case MessageStatus::Pending:
      return QStringLiteral("pending");
    case MessageStatus::Streaming:
      return QStringLiteral("streaming");
    case MessageStatus::Complete:
      return QStringLiteral("complete");
    case MessageStatus::Error:
      return QStringLiteral("error");
    case MessageStatus::Cancelled:
      return QStringLiteral("cancelled");
  }
  return QStringLiteral("unknown");
}

template <typename T>
QVariant toVariant(const std::optional<T>& value) {
  return value.has_value() ? QVariant::fromValue(*value) : QVariant();
}

QString toolCallStatusToString(holonight_domain::ToolInvocationStatus status) {
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

QString toolTitleFromEntry(const holonight_domain::ToolCallEntry& entry) {
  return entry.tool_name.isEmpty() ? (entry.tool_id.isEmpty() ? QStringLiteral("Tool activity") : entry.tool_id)
                                   : QStringLiteral("Tool: %1").arg(entry.tool_name);
}

QString toolSummaryFromEntry(const QString& title, holonight_domain::ToolInvocationStatus status, bool has_result,
                             bool has_error) {
  if (has_error) {
    return QStringLiteral("%1 failed").arg(title);
  }

  switch (status) {
    case holonight_domain::ToolInvocationStatus::Requested:
      return QStringLiteral("%1 requested").arg(title);
    case holonight_domain::ToolInvocationStatus::AwaitingApproval:
      return QStringLiteral("%1 awaiting approval").arg(title);
    case holonight_domain::ToolInvocationStatus::Running:
      return QStringLiteral("%1 running").arg(title);
    case holonight_domain::ToolInvocationStatus::Completed:
      return QStringLiteral("%1 complete").arg(title);
    case holonight_domain::ToolInvocationStatus::Failed:
      return QStringLiteral("%1 failed").arg(title);
    case holonight_domain::ToolInvocationStatus::Denied:
      return QStringLiteral("%1 denied").arg(title);
    case holonight_domain::ToolInvocationStatus::Cancelled:
      return QStringLiteral("%1 cancelled").arg(title);
  }
  return QStringLiteral("%1 requested").arg(title);
}

QVariantMap toolDetailData(const holonight_domain::ToolCallEntry& entry) {
  QVariantMap detailData;
  if (!entry.tool_id.isEmpty()) {
    detailData.insert(QStringLiteral("toolId"), entry.tool_id);
  }
  if (!entry.function_name.isEmpty()) {
    detailData.insert(QStringLiteral("functionName"), entry.function_name);
  }
  if (!entry.input.isEmpty()) {
    detailData.insert(QStringLiteral("input"), entry.input.toVariantMap());
  }
  if (!entry.result.isEmpty()) {
    detailData.insert(QStringLiteral("result"), entry.result.toVariantMap());
  }
  if (entry.is_error) {
    detailData.insert(QStringLiteral("isError"), entry.is_error);
  }
  return detailData;
}

qint64 toolDurationMs(const holonight_domain::ToolCallEntry& entry) {
  const auto started = entry.started_at.has_value() ? *entry.started_at : entry.requested_at;
  if (!started.isValid()) {
    return -1;
  }
  const auto finished = entry.finished_at.has_value() ? *entry.finished_at : QDateTime{};
  if (!finished.isValid()) {
    return -1;
  }
  return started.msecsTo(finished);
}

// REQ-F-007/REQ-F-011. A tool-calling Message carries exactly one ToolCallEntry in current history.
// A newer provider result can be paired with an older invocation using tool_use_id.
std::optional<ToolPresentation> presentationFor(const holonight_domain::ToolCallEntry& entry,
                                                const ToolRegistry* registry) {
  if (registry == nullptr) {
    return std::nullopt;
  }

  holonight_domain::ToolInvocation invocation;
  invocation.id = entry.tool_use_id;
  invocation.provider_call_id = entry.tool_use_id;
  invocation.tool_id = entry.tool_id;
  invocation.function_name = entry.function_name.isEmpty() ? entry.tool_name : entry.function_name;
  invocation.location = entry.execution_location;
  invocation.arguments = entry.input;
  invocation.status = entry.status;
  invocation.requested_at = entry.requested_at;
  invocation.started_at = entry.started_at;
  invocation.finished_at = entry.finished_at;
  invocation.can_cancel = entry.can_cancel;
  if (!entry.result.isEmpty()) {
    invocation.result = entry.result;
  }
  if (entry.is_error) {
    invocation.error = holonight_domain::ToolError{
        .code = QStringLiteral("TOOL_ERROR"), .message = QStringLiteral("Tool execution failed."), .details = {}};
  }

  const ToolRegistration* registration = registry->registrationByFunctionName(invocation.function_name);
  if (registration != nullptr && registration->presenter != nullptr) {
    if (invocation.tool_id.isEmpty()) {
      invocation.tool_id = registration->definition.id;
    }
    return registration->presenter->present(invocation);
  }

  return GenericToolPresenter{}.present(invocation);
}

QVariantMap toolCallVariant(const holonight_domain::ToolCallEntry& entry, const ToolRegistry* registry) {
  const QString title = toolTitleFromEntry(entry);
  const qint64 durationMs = toolDurationMs(entry);
  const bool hasInput = !entry.input.isEmpty();
  const bool hasResult = !entry.result.isEmpty();
  QVariantMap map;
  if (!entry.tool_use_id.isEmpty()) {
    map.insert(QStringLiteral("toolUseId"), entry.tool_use_id);
  }
  map.insert(QStringLiteral("kind"), entry.kind == holonight_domain::ToolCallKind::Invocation
                                         ? QStringLiteral("invocation")
                                         : QStringLiteral("result"));
  map.insert(QStringLiteral("toolId"), entry.tool_id);
  map.insert(QStringLiteral("functionName"), entry.function_name);
  if (!entry.tool_name.isEmpty()) {
    map.insert(QStringLiteral("toolName"), entry.tool_name);
  }
  if (!entry.input.isEmpty()) {
    map.insert(QStringLiteral("input"), entry.input.toVariantMap());
  }
  if (!entry.result.isEmpty()) {
    map.insert(QStringLiteral("result"), entry.result.toVariantMap());
  }
  map.insert(QStringLiteral("status"), [&] {
    switch (entry.status) {
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
  }());
  if (entry.requested_at.isValid()) {
    map.insert(QStringLiteral("requestedAt"), entry.requested_at);
  }
  if (entry.started_at.has_value() && entry.started_at->isValid()) {
    map.insert(QStringLiteral("startedAt"), *entry.started_at);
  }
  if (entry.finished_at.has_value() && entry.finished_at->isValid()) {
    map.insert(QStringLiteral("finishedAt"), *entry.finished_at);
  }
  if (durationMs >= 0) {
    map.insert(QStringLiteral("durationMs"), durationMs);
  }
  map.insert(QStringLiteral("rendererKey"),
             entry.tool_id.isEmpty() && entry.tool_name == QStringLiteral("ListFiles")
                 ? QStringLiteral("filesystem.list")
                 : (entry.tool_id.isEmpty() ? QStringLiteral("generic") : entry.tool_id));
  map.insert(QStringLiteral("canonicalId"), entry.tool_id.isEmpty() ? (entry.tool_name == QStringLiteral("ListFiles")
                                                                           ? QStringLiteral("filesystem.list")
                                                                           : entry.tool_name)
                                                                    : entry.tool_id);
  map.insert(QStringLiteral("canCancel"), entry.can_cancel);
  map.insert(QStringLiteral("rawArgumentsAvailable"), hasInput);
  map.insert(QStringLiteral("rawResultAvailable"), hasResult);
  map.insert(QStringLiteral("detailData"), toolDetailData(entry));
  map.insert(QStringLiteral("rawArgumentsJson"),
             QString::fromUtf8(QJsonDocument(entry.input).toJson(QJsonDocument::Compact)));
  map.insert(QStringLiteral("rawResultJson"),
             QString::fromUtf8(QJsonDocument(entry.result).toJson(QJsonDocument::Compact)));
  map.insert(QStringLiteral("isError"), entry.is_error);
  map.insert(QStringLiteral("toolTitle"), title);
  map.insert(QStringLiteral("summary"), toolSummaryFromEntry(title, entry.status, hasResult, entry.is_error));

  if (const auto presentation = presentationFor(entry, registry); presentation.has_value()) {
    map.insert(QStringLiteral("rendererKey"), presentation->renderer_key);
    map.insert(QStringLiteral("toolTitle"), presentation->title);
    map.insert(QStringLiteral("summary"), presentation->summary);
    map.insert(QStringLiteral("detailData"), presentation->detail_data);
    map.insert(QStringLiteral("rawArgumentsJson"), presentation->raw_arguments_json);
    map.insert(QStringLiteral("rawResultJson"), presentation->raw_result_json);
    map.insert(QStringLiteral("isError"), presentation->is_error);
    map.insert(QStringLiteral("canCancel"), presentation->can_cancel);
  }
  return map;
}

QList<int> toolActivityRoles() {
  return {
      MessageListModel::ToolCallRole,
      MessageListModel::ToolInvocationIdRole,
      MessageListModel::ToolCanonicalIdRole,
      MessageListModel::ToolStatusRole,
      MessageListModel::ToolTitleRole,
      MessageListModel::ToolSummaryRole,
      MessageListModel::ToolRendererKeyRole,
      MessageListModel::ToolDurationMsRole,
      MessageListModel::ToolDetailDataRole,
      MessageListModel::ToolCanCancelRole,
      MessageListModel::ToolHasRawArgumentsRole,
      MessageListModel::ToolHasRawResultRole,
      MessageListModel::ToolIsErrorRole,
      MessageListModel::ToolRawArgumentsJsonRole,
      MessageListModel::ToolRawResultJsonRole,
  };
}

std::optional<holonight_domain::ToolCallEntry> singleToolCallFromMessage(const Message& message) {
  const auto& entries = message.toolCalls();
  if (entries.empty()) {
    return std::nullopt;
  }
  return entries.front();
}

// REQ-F-003/REQ-NF-003. Isolated from MessageListModel::data()'s switch to keep its cognitive
// complexity under clang-tidy's threshold -- these 7 roles share one "absent usage -> invalid
// QVariant" fallback, so folding them inline blows past readability-function-cognitive-complexity.
QVariant usageRoleData(int role, const std::optional<holonight_persistence::UsageRecord>& usage) {
  if (!usage.has_value()) {
    return {};
  }
  switch (role) {
    case MessageListModel::InputTokenCountRole:
      return toVariant(usage->input_tokens);
    case MessageListModel::OutputTokenCountRole:
      return toVariant(usage->output_tokens);
    case MessageListModel::ReasoningTokenCountRole:
      return toVariant(usage->reasoning_tokens);
    case MessageListModel::CacheCreationTokenCountRole:
      return toVariant(usage->cache_creation_tokens);
    case MessageListModel::CacheReadTokenCountRole:
      return toVariant(usage->cache_read_tokens);
    case MessageListModel::TotalTokenCountRole:
      return toVariant(usage->total_tokens);
    case MessageListModel::DurationMsRole:
      return toVariant(usage->duration_ms);
    default:
      return {};
  }
}

}  // namespace

std::optional<holonight_domain::ToolCallEntry> MessageListModel::singleToolCall(const Message& message) {
  return singleToolCallFromMessage(message);
}

std::optional<std::size_t> MessageListModel::findUnresolvedInvocationRow(const QString& tool_use_id) const {
  if (tool_use_id.isEmpty()) {
    return std::nullopt;
  }

  for (std::size_t rowIndex = 0; rowIndex < rows_.size(); ++rowIndex) {
    const Row& row = rows_[rowIndex];
    if (row.tool_call_id.has_value() && *row.tool_call_id == tool_use_id && row.tool_call_is_invocation &&
        !row.tool_call_has_result) {
      return rowIndex;
    }
  }
  return std::nullopt;
}

void MessageListModel::insertProjectedMessage(const Message& message, bool emit_data_changes) {
  const auto tool_call = singleToolCall(message);
  if (!tool_call.has_value()) {
    Row row = toRow(message);
    if (emit_data_changes) {
      beginInsertRows(QModelIndex(), 0, 0);
      rows_.insert(rows_.begin(), std::move(row));
      endInsertRows();
    } else {
      rows_.insert(rows_.begin(), std::move(row));
    }
    return;
  }

  if (tool_call->kind == holonight_domain::ToolCallKind::Invocation) {
    Row row = toRow(message);
    row.tool_call_id = tool_call->tool_use_id;
    row.tool_call_is_invocation = true;
    row.tool_call_has_result = false;
    if (emit_data_changes) {
      beginInsertRows(QModelIndex(), 0, 0);
      rows_.insert(rows_.begin(), std::move(row));
      endInsertRows();
    } else {
      rows_.insert(rows_.begin(), std::move(row));
    }
    return;
  }

  // tool_call->kind == ToolCallKind::Result
  if (tool_call->tool_use_id.isEmpty()) {
    Row row = toRow(message);
    row.tool_call_is_invocation = false;
    if (emit_data_changes) {
      beginInsertRows(QModelIndex(), 0, 0);
      rows_.insert(rows_.begin(), std::move(row));
      endInsertRows();
    } else {
      rows_.insert(rows_.begin(), std::move(row));
    }
    return;
  }

  const auto matching = findUnresolvedInvocationRow(tool_call->tool_use_id);
  if (matching.has_value()) {
    Row& invocation_row = rows_[*matching];
    if (!invocation_row.tool_call_has_result && invocation_row.tool_call.has_value() &&
        invocation_row.projected_tool_call.has_value()) {
      holonight_domain::ToolCallEntry merged = *invocation_row.projected_tool_call;
      merged.result = tool_call->result;
      merged.is_error = tool_call->is_error;
      merged.status = tool_call->status == holonight_domain::ToolInvocationStatus::Requested
                          ? (tool_call->is_error ? holonight_domain::ToolInvocationStatus::Failed
                                                 : holonight_domain::ToolInvocationStatus::Completed)
                          : tool_call->status;
      if (!tool_call->tool_id.isEmpty()) {
        merged.tool_id = tool_call->tool_id;
      }
      if (!tool_call->function_name.isEmpty()) {
        merged.function_name = tool_call->function_name;
      }
      if (tool_call->started_at.has_value()) {
        merged.started_at = tool_call->started_at;
      }
      if (tool_call->finished_at.has_value()) {
        merged.finished_at = tool_call->finished_at;
      }
      invocation_row.projected_tool_call = merged;
      invocation_row.tool_call = toolCallVariant(merged, tool_registry_.get());
      invocation_row.tool_call_has_result = true;
      if (emit_data_changes) {
        const QModelIndex changed = index(static_cast<int>(*matching));
        const QList<int> roles = toolActivityRoles();
        emit dataChanged(changed, changed, roles);
      }
      return;
    }
  }

  Row row = toRow(message);
  row.tool_call_id = tool_call->tool_use_id;
  row.tool_call_is_invocation = false;
  row.tool_call_has_result = false;
  if (emit_data_changes) {
    beginInsertRows(QModelIndex(), 0, 0);
    rows_.insert(rows_.begin(), std::move(row));
    endInsertRows();
  } else {
    rows_.insert(rows_.begin(), std::move(row));
  }
}

MessageListModel::MessageListModel(holonight_config::ProviderState provider_state, QObject* parent)
    : MessageListModel(std::move(provider_state), nullptr, parent) {}

MessageListModel::MessageListModel(holonight_config::ProviderState provider_state,
                                   std::shared_ptr<const ToolRegistry> tool_registry, QObject* parent)
    : QAbstractListModel(parent),
      provider_state_(std::move(provider_state)),
      tool_registry_(std::move(tool_registry)) {}

int MessageListModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return static_cast<int>(rows_.size());
}

QVariant MessageListModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || static_cast<std::size_t>(index.row()) >= rows_.size()) {
    return {};
  }

  const Row& row = rows_[static_cast<std::size_t>(index.row())];
  switch (role) {
    case IdRole:
      return row.id;
    case RoleRole:
      return row.role;
    case TextRole:
      return row.text;
    case StatusRole:
      return row.status;
    case CreatedAtRole:
      return row.created_at;
    case ModelNameRole:
      return row.model_name;
    case ProviderIdRole:
      return row.provider_id;
    case ProviderTypeRole:
      return row.provider_type;
    case ProviderNameRole:
      return row.provider_name;
    case ContentBlocksRole: {
      return row.content_model ? QVariant::fromValue(static_cast<QObject*>(row.content_model.get())) : QVariant();
    }
    case InputTokenCountRole:
    case OutputTokenCountRole:
    case ReasoningTokenCountRole:
    case CacheCreationTokenCountRole:
    case CacheReadTokenCountRole:
    case TotalTokenCountRole:
    case DurationMsRole:
      return usageRoleData(role, row.usage);
    case ToolCallRole:
      return row.tool_call.has_value() ? QVariant(*row.tool_call) : QVariant();
    case ToolInvocationIdRole:
      return row.tool_call.has_value() ? row.tool_call->value(QStringLiteral("toolUseId")).toString() : QVariant();
    case ToolCanonicalIdRole:
      return row.tool_call.has_value() ? row.tool_call->value(QStringLiteral("canonicalId")).toString() : QVariant();
    case ToolStatusRole:
      return row.tool_call.has_value() ? row.tool_call->value(QStringLiteral("status")).toString() : QVariant();
    case ToolDurationMsRole:
      return row.tool_call.has_value() && row.tool_call->contains(QStringLiteral("durationMs"))
                 ? row.tool_call->value(QStringLiteral("durationMs"))
                 : QVariant();
    case ToolDetailDataRole:
      return row.tool_call.has_value() ? row.tool_call->value(QStringLiteral("detailData")).toMap() : QVariant();
    case ToolCanCancelRole:
      return row.tool_call.has_value() ? row.tool_call->value(QStringLiteral("canCancel")).toBool() : QVariant();
    case ToolHasRawArgumentsRole:
      return row.tool_call.has_value() ? row.tool_call->value(QStringLiteral("rawArgumentsAvailable"), false).toBool()
                                       : QVariant();
    case ToolHasRawResultRole:
      return row.tool_call.has_value() ? row.tool_call->value(QStringLiteral("rawResultAvailable"), false).toBool()
                                       : QVariant();
    case ToolIsErrorRole:
      return row.tool_call.has_value() ? row.tool_call->value(QStringLiteral("isError")).toBool() : QVariant();
    case ToolTitleRole:
      return row.tool_call.has_value()
                 ? row.tool_call->value(QStringLiteral("toolName"), row.tool_call->value(QStringLiteral("toolId")))
                       .toString()
                 : QVariant();
    case ToolSummaryRole:
      return row.tool_call.has_value() ? row.tool_call->value(QStringLiteral("summary")).toString() : QVariant();
    case ToolRendererKeyRole:
      return row.tool_call.has_value() ? row.tool_call->value(QStringLiteral("rendererKey")).toString() : QVariant();
    case ToolRawArgumentsJsonRole:
      return row.tool_call.has_value() ? row.tool_call->value(QStringLiteral("rawArgumentsJson")).toString()
                                       : QVariant();
    case ToolRawResultJsonRole:
      return row.tool_call.has_value() ? row.tool_call->value(QStringLiteral("rawResultJson")).toString() : QVariant();
    default:
      return {};
  }
}

QHash<int, QByteArray> MessageListModel::roleNames() const {
  return {
      {IdRole, QByteArrayLiteral("messageId")},
      {RoleRole, QByteArrayLiteral("role")},
      {TextRole, QByteArrayLiteral("text")},
      {StatusRole, QByteArrayLiteral("status")},
      {CreatedAtRole, QByteArrayLiteral("createdAt")},
      {ModelNameRole, QByteArrayLiteral("modelName")},
      {ContentBlocksRole, QByteArrayLiteral("contentBlocks")},
      {ProviderIdRole, QByteArrayLiteral("providerId")},
      {ProviderTypeRole, QByteArrayLiteral("providerType")},
      {ProviderNameRole, QByteArrayLiteral("providerName")},
      {InputTokenCountRole, QByteArrayLiteral("inputTokenCount")},
      {OutputTokenCountRole, QByteArrayLiteral("outputTokenCount")},
      {ReasoningTokenCountRole, QByteArrayLiteral("reasoningTokenCount")},
      {CacheCreationTokenCountRole, QByteArrayLiteral("cacheCreationTokenCount")},
      {CacheReadTokenCountRole, QByteArrayLiteral("cacheReadTokenCount")},
      {TotalTokenCountRole, QByteArrayLiteral("totalTokenCount")},
      {DurationMsRole, QByteArrayLiteral("durationMs")},
      {ToolCallRole, QByteArrayLiteral("toolCall")},
      {ToolInvocationIdRole, QByteArrayLiteral("toolInvocationId")},
      {ToolCanonicalIdRole, QByteArrayLiteral("toolCanonicalId")},
      {ToolStatusRole, QByteArrayLiteral("toolStatus")},
      {ToolDurationMsRole, QByteArrayLiteral("toolDurationMs")},
      {ToolTitleRole, QByteArrayLiteral("toolTitle")},
      {ToolSummaryRole, QByteArrayLiteral("toolSummary")},
      {ToolRendererKeyRole, QByteArrayLiteral("toolRendererKey")},
      {ToolDetailDataRole, QByteArrayLiteral("toolDetailData")},
      {ToolCanCancelRole, QByteArrayLiteral("toolCanCancel")},
      {ToolHasRawArgumentsRole, QByteArrayLiteral("toolHasRawArguments")},
      {ToolHasRawResultRole, QByteArrayLiteral("toolHasRawResult")},
      {ToolIsErrorRole, QByteArrayLiteral("toolIsError")},
      {ToolRawArgumentsJsonRole, QByteArrayLiteral("toolRawArgumentsJson")},
      {ToolRawResultJsonRole, QByteArrayLiteral("toolRawResultJson")},
  };
}

void MessageListModel::setProviderState(holonight_config::ProviderState provider_state) {
  provider_state_ = std::move(provider_state);

  for (std::size_t row_index = 0; row_index < rows_.size(); ++row_index) {
    Row& row = rows_[row_index];
    const auto identity = row.provider_id.isEmpty()
                              ? HistoricalProviderIdentity{}
                              : resolveHistoricalProviderIdentity(provider_state_, row.provider_id);
    const QString provider_type = identity.type ? holonight_config::providerTypeToString(*identity.type) : QString();

    QList<int> changed_roles;
    if (row.provider_type != provider_type) {
      row.provider_type = provider_type;
      changed_roles.append(ProviderTypeRole);
    }
    if (row.provider_name != identity.display_name) {
      row.provider_name = identity.display_name;
      changed_roles.append(ProviderNameRole);
    }
    if (!changed_roles.isEmpty()) {
      const QModelIndex changed_index = index(static_cast<int>(row_index));
      emit dataChanged(changed_index, changed_index, changed_roles);
    }
  }
}

void MessageListModel::insertNewestMessage(const Message& message) { insertProjectedMessage(message, true); }

void MessageListModel::updateNewestMessage(const Message& message) {
  if (rows_.empty()) {
    return;
  }

  Row& current = rows_.front();
  Row updated = toRow(message);
  std::unique_ptr<MessageContentModel> content_model = std::move(current.content_model);
  QList<int> changedRoles;
  const bool textChanged = current.text != updated.text;

  if (current.id != updated.id) {
    changedRoles.append(IdRole);
  }
  if (current.role != updated.role) {
    changedRoles.append(RoleRole);
  }
  if (textChanged) {
    changedRoles.append(TextRole);
  }
  if (current.status != updated.status) {
    changedRoles.append(StatusRole);
  }
  if (current.created_at != updated.created_at) {
    changedRoles.append(CreatedAtRole);
  }
  if (current.model_name != updated.model_name) {
    changedRoles.append(ModelNameRole);
  }
  if (current.provider_id != updated.provider_id) {
    changedRoles.append(ProviderIdRole);
  }
  if (current.provider_type != updated.provider_type) {
    changedRoles.append(ProviderTypeRole);
  }
  if (current.provider_name != updated.provider_name) {
    changedRoles.append(ProviderNameRole);
  }
  const bool toolCallChanged = current.tool_call != updated.tool_call || current.tool_call_id != updated.tool_call_id ||
                               current.tool_call_is_invocation != updated.tool_call_is_invocation ||
                               current.tool_call_has_result != updated.tool_call_has_result;
  if (toolCallChanged) {
    changedRoles.append(toolActivityRoles());
  }
  if (changedRoles.isEmpty()) {
    current.content_model = std::move(content_model);
    return;
  }
  if (content_model && updated.role == QStringLiteral("assistant") && !updated.tool_call.has_value()) {
    const bool terminal = updated.status == QStringLiteral("complete") || updated.status == QStringLiteral("error") ||
                          updated.status == QStringLiteral("cancelled");
    content_model->setSource(updated.text, terminal);
    updated.content_model = std::move(content_model);
  }

  current = std::move(updated);
  emit dataChanged(index(0), index(0), changedRoles);
}

void MessageListModel::applyUsageRecords(const QMap<QString, holonight_persistence::UsageRecord>& records) {
  static const QList<int> kUsageRoles = {
      InputTokenCountRole,     OutputTokenCountRole, ReasoningTokenCountRole, CacheCreationTokenCountRole,
      CacheReadTokenCountRole, TotalTokenCountRole,  DurationMsRole,
  };

  int row_index = 0;
  for (Row& row : rows_) {
    const auto found = records.constFind(row.id);
    if (found != records.constEnd()) {
      row.usage = found.value();
      const QModelIndex changed_index = index(row_index);
      emit dataChanged(changed_index, changed_index, kUsageRoles);
    }
    ++row_index;
  }
}

void MessageListModel::resetFromChronological(const std::vector<Message>& messages) {
  beginResetModel();
  rows_.clear();
  rows_.reserve(messages.size());
  for (const Message& message : messages) {
    insertProjectedMessage(message, false);
  }
  endResetModel();
}

MessageListModel::Row MessageListModel::toRow(const Message& message) {
  const QString provider_id = message.modelId() ? message.modelId()->provider_id : QString();
  const auto identity = provider_id.isEmpty() ? HistoricalProviderIdentity{}
                                              : resolveHistoricalProviderIdentity(provider_state_, provider_id);
  Row row{
      .id = message.id().toString(),
      .role = roleToString(message.role()),
      .text = message.text(),
      .status = statusToString(message.status()),
      .created_at = message.createdAt(),
      .model_name = message.modelId() ? message.modelId()->model_name : QString(),
      .provider_id = provider_id,
      .provider_type = identity.type ? holonight_config::providerTypeToString(*identity.type) : QString(),
      .provider_name = identity.display_name,
  };

  const auto toolCallOpt = singleToolCall(message);
  if (!toolCallOpt.has_value()) {
    if (row.role == QStringLiteral("assistant")) {
      row.content_model = std::make_unique<MessageContentModel>(this);
      const bool terminal = row.status == QStringLiteral("complete") || row.status == QStringLiteral("error") ||
                            row.status == QStringLiteral("cancelled");
      row.content_model->setSource(row.text, terminal);
    }
    return row;
  }
  row.tool_call = toolCallVariant(*toolCallOpt, tool_registry_.get());
  row.projected_tool_call = *toolCallOpt;
  if (!toolCallOpt->tool_use_id.isEmpty()) {
    row.tool_call_id = toolCallOpt->tool_use_id;
  }
  row.tool_call_is_invocation = toolCallOpt->kind == holonight_domain::ToolCallKind::Invocation;
  row.tool_call_has_result = false;

  return row;
}

}  // namespace holonight_application
