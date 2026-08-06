#pragma once

#include "holonight_domain/tool_activity.h"

#include <QJsonObject>
#include <QString>
#include <QtGlobal>

#include <optional>
#include <variant>
#include <vector>

namespace holonight_domain {

struct ContentDelta {
  QString text{};

  friend bool operator==(const ContentDelta&, const ContentDelta&) = default;
};

struct Usage {
  std::optional<int> input_tokens;
  std::optional<int> output_tokens;
  std::optional<int> reasoning_tokens;
  std::optional<int> cache_creation_tokens;
  std::optional<int> cache_read_tokens;
  std::optional<int> total_tokens;

  // Canonical cross-provider metric. Populated by ChatController, not the adapters — no adapter
  // has meaningful access to "when did ChatController::sendChat() get invoked."
  std::optional<qint64> duration_ms;

  // Ollama-only supplementary fields. Left unset by the other three adapters.
  std::optional<qint64> ollama_total_duration_ns;
  std::optional<qint64> ollama_load_duration_ns;
  std::optional<qint64> ollama_prompt_eval_duration_ns;
  std::optional<qint64> ollama_eval_duration_ns;

  friend bool operator==(const Usage&, const Usage&) = default;
};

struct Completed {
  std::optional<Usage> usage;
  // Exact provider-returned model string (e.g. "gpt-4-turbo-2024-04-09"), not the user-facing
  // alias -- null if the provider never reported one before this event fired (REQ-F-010).
  std::optional<QString> model_identifier;

  friend bool operator==(const Completed&, const Completed&) = default;
};

struct Error {
  QString message{};
  std::optional<Usage> usage;
  std::optional<QString> model_identifier;

  friend bool operator==(const Error&, const Error&) = default;
};

struct Cancelled {
  std::optional<Usage> usage;
  std::optional<QString> model_identifier;

  friend bool operator==(const Cancelled&, const Cancelled&) = default;
};

// Mid-stream signal that the model requested a tool invocation. Carries no Usage/model_identifier
// (unlike Completed/Error/Cancelled) -- those are stamped once, at the terminal Completed event of
// each HTTP round-trip.
struct ToolRequestEvent {
  QString provider_call_id;
  QString provider_instance_id;
  QString function_name;
  QJsonObject arguments;
  ToolExecutionLocation execution_location = ToolExecutionLocation::LocalClient;
  ToolSource source = ToolSource::BuiltIn;

  // Gemini 3.x thinking-model functionCall parts carry an opaque signature that must be echoed
  // verbatim in the follow-up turn's reconstructed history. std::nullopt when absent (older/
  // non-thinking models, and unconditionally for every non-Google provider).
  std::optional<QString> thought_signature;

  // True when `provider_call_id` above was client-synthesized (Gemini omitted "id") rather than
  // sourced from the model. Threaded through so the follow-up-turn codec knows whether to omit
  // "id" from the reconstructed functionResponse.
  bool provider_call_id_synthesized = false;

  // Provider-neutral storage for stateless APIs that require opaque output items to be replayed.
  std::optional<QString> provider_item_id;
  std::vector<QJsonObject> provider_context;

  friend bool operator==(const ToolRequestEvent&, const ToolRequestEvent&) = default;
};

using StreamEvent = std::variant<ContentDelta, Completed, Error, Cancelled, ToolRequestEvent>;

}  // namespace holonight_domain
