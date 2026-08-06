#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <expected>
#include <optional>
#include <vector>

namespace holonight_domain {

enum class ToolInvocationStatus : std::uint8_t {
  Requested,
  AwaitingApproval,
  Running,
  Completed,
  Failed,
  Denied,
  Cancelled,
};

enum class ToolExecutionLocation : std::uint8_t {
  LocalClient,
  ProviderHosted,
  RemoteExternal,
};

enum class ToolSource : std::uint8_t {
  BuiltIn,
  Provider,
  Mcp,
};

struct ToolInvocationTransitionError {
  ToolInvocationStatus from;
  ToolInvocationStatus attempted;

  friend bool operator==(const ToolInvocationTransitionError&, const ToolInvocationTransitionError&) = default;
};

struct ToolError {
  QString code;
  QString message;
  QJsonValue details;

  friend bool operator==(const ToolError&, const ToolError&) = default;
};

struct ToolInvocation {
  QString id;
  QString provider_call_id;  // Exact provider correlation ID for this invocation.
  QString provider_instance_id;
  QString tool_id;        // Canonical application identifier, e.g. "filesystem.list".
  QString function_name;  // Provider function name in wire-safe form, e.g. "list_files".
  ToolExecutionLocation location = ToolExecutionLocation::LocalClient;
  ToolSource source = ToolSource::BuiltIn;
  QJsonValue arguments;
  ToolInvocationStatus status = ToolInvocationStatus::Requested;
  QDateTime requested_at;
  std::optional<QDateTime> started_at;
  std::optional<QDateTime> finished_at;
  std::optional<QJsonValue> result;
  std::optional<ToolError> error;
  bool can_cancel = false;

  [[nodiscard]] std::expected<void, ToolInvocationTransitionError> transitionTo(ToolInvocationStatus next_status) {
    if (next_status == status) {
      return {};
    }

    const bool is_requested = status == ToolInvocationStatus::Requested;
    const bool is_awaiting_approval = status == ToolInvocationStatus::AwaitingApproval;
    const bool is_running = status == ToolInvocationStatus::Running;
    const bool is_terminal = status == ToolInvocationStatus::Completed || status == ToolInvocationStatus::Failed ||
                             status == ToolInvocationStatus::Denied || status == ToolInvocationStatus::Cancelled;

    if (is_terminal) {
      return std::unexpected(ToolInvocationTransitionError{.from = status, .attempted = next_status});
    }

    const bool valid_transition =
        (is_requested &&
         (next_status == ToolInvocationStatus::AwaitingApproval || next_status == ToolInvocationStatus::Running ||
          next_status == ToolInvocationStatus::Completed || next_status == ToolInvocationStatus::Failed ||
          next_status == ToolInvocationStatus::Denied || next_status == ToolInvocationStatus::Cancelled)) ||
        (is_awaiting_approval &&
         (next_status == ToolInvocationStatus::Running || next_status == ToolInvocationStatus::Failed ||
          next_status == ToolInvocationStatus::Denied || next_status == ToolInvocationStatus::Cancelled)) ||
        (is_running && (next_status == ToolInvocationStatus::Completed || next_status == ToolInvocationStatus::Failed ||
                        next_status == ToolInvocationStatus::Cancelled));

    if (!valid_transition) {
      return std::unexpected(ToolInvocationTransitionError{.from = status, .attempted = next_status});
    }

    status = next_status;
    return {};
  }

  friend bool operator==(const ToolInvocation&, const ToolInvocation&) = default;
};

struct ToolDefinition {
  QString id;             // Stable application identifier, e.g. "filesystem.list".
  QString function_name;  // Provider-safe invocation name, e.g. "list_files".
  QString display_name;   // Translatable/fallback display label: "List files".
  QString renderer_key;   // Stable UI contract key, e.g. "filesystem.list".
  QString description;
  QJsonObject input_schema;
  QStringList legacy_aliases;

  enum class ToolRisk : std::uint8_t {
    Safe,
    PotentiallySensitive,
    Unsafe,
  };

  ToolRisk risk = ToolRisk::Safe;
};

struct ToolCatalogSnapshot {
  std::vector<ToolDefinition> client_tools;
};

}  // namespace holonight_domain
