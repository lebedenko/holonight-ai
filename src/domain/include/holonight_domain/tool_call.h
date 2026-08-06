#pragma once

#include "holonight_domain/tool_activity.h"

#include <QJsonObject>
#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

namespace holonight_domain {

enum class ToolCallKind : std::uint8_t { Invocation, Result };

// One entry per tool_use block. An Invocation entry records what the model asked to run; a Result
// entry (in a separate, later Message) records what running it produced. tool_use_id is the join
// key between the two, matching Anthropic's tool_use/tool_result id pairing.
struct ToolCallEntry {
  ToolCallKind kind = ToolCallKind::Invocation;
  QString tool_use_id;
  QString tool_name;      // Invocation only; empty for Result.
  QString tool_id;        // Canonical tool identifier (e.g. "filesystem.list"), empty when legacy.
  QString function_name;  // Provider wire function name.
  QJsonObject input;      // Invocation only: parameters exactly as sent to ITool::execute().
  QJsonObject result;     // Result only: the tool's JSON result (success shape or {"error": {...}}).
  bool is_error = false;  // Result only.
  ToolInvocationStatus status = ToolInvocationStatus::Requested;
  QDateTime requested_at;
  std::optional<QDateTime> started_at;
  std::optional<QDateTime> finished_at;
  ToolExecutionLocation execution_location = ToolExecutionLocation::LocalClient;
  bool can_cancel = false;

  // Mirrors ToolRequestEvent's fields of the same name/purpose. Populated only on Invocation-kind
  // entries sourced from a Google functionCall; every other provider's entries leave these at their
  // defaults (nullopt / false), and Result-kind entries never set them.
  std::optional<QString> thought_signature;
  bool provider_call_id_synthesized = false;
  std::optional<QString> provider_item_id;
  std::vector<QJsonObject> provider_context;

  friend bool operator==(const ToolCallEntry&, const ToolCallEntry&) = default;
};

}  // namespace holonight_domain
