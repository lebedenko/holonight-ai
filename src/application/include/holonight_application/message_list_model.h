#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QMap>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <holonight_config/provider_config.h>
#include <holonight_domain/holonight_domain.h>
#include <holonight_persistence/usage_record.h>
#include <memory>
#include <optional>
#include <vector>

namespace holonight_application {

class ToolRegistry;
class MessageContentModel;

// Bridges Conversation::messages() (a plain std::vector<Message>, with no change notification of
// its own) to a newest-first QML transcript projection. Tool activity rows are collapsed in this
// projection while conversation persistence remains canonical and unchanged.
// model synchronously after ChatController mutates the authoritative chronological Conversation,
// so these rows remain a faithful presentation mirror, never a second source of truth.
class MessageListModel : public QAbstractListModel {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Instantiated only by ChatViewModel; do not construct from QML")

 public:
  enum Roles : std::uint16_t {  // NOLINT(cppcoreguidelines-use-enum-class): Qt model roles are int-compatible.
    IdRole = Qt::UserRole + 1,
    RoleRole,  // "user" | "assistant" | "system"
    TextRole,
    StatusRole,  // "pending" | "streaming" | "complete" | "error" | "cancelled"
    CreatedAtRole,
    ModelNameRole,      // model name for assistant messages, "" for user/system
    ContentBlocksRole,  // persistent MessageContentModel for assistant text rows
    ProviderIdRole,     // provider identifier for assistant messages, "" for user/system
    ProviderTypeRole,   // provider type resolved from live configuration or a tombstone
    ProviderNameRole,   // current live name or the frozen tombstone name
    // REQ-F-003/REQ-NF-003. Undefined in QML (invalid QVariant) until applyUsageRecords() supplies
    // a matching holonight_persistence::UsageRecord for this row's message id.
    InputTokenCountRole,
    OutputTokenCountRole,
    ReasoningTokenCountRole,
    CacheCreationTokenCountRole,
    CacheReadTokenCountRole,
    TotalTokenCountRole,
    DurationMsRole,
    // REQ-F-007/REQ-F-011. Undefined in QML (invalid QVariant) for ordinary text messages. For a
    // tool activity row it is usually a QVariantMap:
    // {kind: "invocation", toolName, input, result?, isError?}, keyed by tool_use_id.
    // A legacy/orphan result uses {kind: "result", isError, result, tool_use_id?}.
    ToolCallRole,
    // REQ-F-015. Tool-activity presentation details for dedicated shell rendering. All fields are
    // invalid QVariant for non-tool rows.
    ToolInvocationIdRole,
    ToolCanonicalIdRole,
    ToolStatusRole,
    ToolTitleRole,
    ToolSummaryRole,
    ToolRendererKeyRole,
    ToolDurationMsRole,
    ToolDetailDataRole,
    ToolCanCancelRole,
    ToolHasRawArgumentsRole,
    ToolHasRawResultRole,
    ToolIsErrorRole,
    ToolRawArgumentsJsonRole,
    ToolRawResultJsonRole,
  };
  Q_ENUM(Roles)

  explicit MessageListModel(holonight_config::ProviderState provider_state = {}, QObject* parent = nullptr);
  MessageListModel(holonight_config::ProviderState provider_state, std::shared_ptr<const ToolRegistry> tool_registry,
                   QObject* parent = nullptr);

  [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  // Refreshes provider attribution for existing rows while preserving their stored model identity.
  void setProviderState(holonight_config::ProviderState provider_state);

  // Inserts one row at index 0 while applying tool-call projection. Sending user then assistant
  // produces [assistant, user, previous...].
  void insertNewestMessage(const holonight_domain::Message& message);

  // Updates row 0 in place and emits only roles whose values actually changed.
  void updateNewestMessage(const holonight_domain::Message& message);

  // Rebuilds the newest-first projection from authoritative chronological input.
  void resetFromChronological(const std::vector<holonight_domain::Message>& messages);

  // REQ-F-003/REQ-NF-003. Sets row.usage for each row whose id matches a key in `records`, then
  // emits dataChanged for the 7 usage-related roles on each affected row. Rows with no matching
  // entry (e.g. user/system messages, or assistant messages predating this feature) are untouched.
  void applyUsageRecords(const QMap<QString, holonight_persistence::UsageRecord>& records);

 private:
  [[nodiscard]] static std::optional<holonight_domain::ToolCallEntry> singleToolCall(
      const holonight_domain::Message& message);
  [[nodiscard]] std::optional<std::size_t> findUnresolvedInvocationRow(const QString& tool_use_id) const;
  void insertProjectedMessage(const holonight_domain::Message& message, bool emit_data_changes);

  struct Row {
    QString id;
    QString role;
    QString text;
    QString status;
    QDateTime created_at;
    QString model_name;
    QString provider_id;
    QString provider_type;
    QString provider_name;
    std::unique_ptr<MessageContentModel> content_model;
    // REQ-F-003. Absent until applyUsageRecords() supplies a match; absence means the footer's 13
    // usage roles report an invalid QVariant (undefined in QML), not zero-filled values.
    std::optional<holonight_persistence::UsageRecord> usage;
    // REQ-F-007/REQ-F-011. Built from Message::toolCalls() at row-construction time; std::nullopt
    // for ordinary text messages, which is exactly the majority case this stays cheap for.
    std::optional<QVariantMap> tool_call;
    // When non-empty, this tracks the invocation/result correlation key used for tool activity
    // projector merges. For ordinary text rows, tool_call_id stays empty.
    std::optional<QString> tool_call_id;
    // true when tool_call represents an invocation role (standalone or paired).
    bool tool_call_is_invocation = false;
    // true once the row has both invocation and result metadata in one projection.
    bool tool_call_has_result = false;
    std::optional<holonight_domain::ToolCallEntry> projected_tool_call;
  };

  [[nodiscard]] Row toRow(const holonight_domain::Message& message);

  holonight_config::ProviderState provider_state_;
  std::shared_ptr<const ToolRegistry> tool_registry_;
  std::vector<Row> rows_;
};

}  // namespace holonight_application
