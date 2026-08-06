#pragma once

#include "holonight_application/tools/tool_contracts.h"
#include "holonight_application/tools/tool_orchestrator.h"
#include "holonight_providers/anthropic_provider.h"
#include "holonight_providers/clock.h"
#include "holonight_providers/google_provider.h"
#include "holonight_providers/ollama_provider.h"
#include "holonight_providers/openai_provider.h"

#include <QHash>
#include <QString>

#include <cstdint>
#include <expected>
#include <functional>
#include <holonight_domain/holonight_domain.h>
#include <memory>
#include <optional>
#include <vector>

namespace holonight_application {

class ProviderAdapterRouter;
class ToolRegistry;

enum class SendRejectReason : std::uint8_t {
  NoModelSelected,
  AlreadyStreaming,
  NoAssistantMessageToRegenerate,
};

struct SendRejected {
  SendRejectReason reason;

  friend bool operator==(const SendRejected&, const SendRejected&) = default;
};

class ChatController {
 public:
  // clock defaults to SteadyClock (production); tests inject a FakeClock for exact duration_ms
  // assertions (REQ-NF-001).
  // tool_registry defaults to a fresh, empty ToolRegistry rather than nullptr so handleStreamEvent()
  // never needs a null check before calling tool_registry_->invoke() -- an empty registry simply
  // means every ToolCall event resolves to ToolRegistry::invoke()'s own "unknown tool" error, which
  // cannot happen in practice since a provider only ever offers tools this registry advertised.
  explicit ChatController(
      std::shared_ptr<holonight_providers::OllamaProvider> ollama_provider,
      std::shared_ptr<holonight_providers::OpenAIProvider> openai_provider,
      std::shared_ptr<holonight_providers::AnthropicProvider> anthropic_provider,
      std::shared_ptr<holonight_providers::GoogleProvider> google_provider,
      std::shared_ptr<holonight_providers::Clock> clock = std::make_shared<holonight_providers::SteadyClock>(),
      std::shared_ptr<ToolRegistry> tool_registry = nullptr);
  explicit ChatController(
      ProviderAdapterRouter* router,
      std::shared_ptr<holonight_providers::Clock> clock = std::make_shared<holonight_providers::SteadyClock>(),
      std::shared_ptr<ToolRegistry> tool_registry = nullptr);

  std::expected<void, SendRejected> send(holonight_domain::Conversation& conversation,
                                         const holonight_domain::ModelId& model, QString user_text,
                                         std::function<void(const holonight_domain::StreamEvent&)> on_event = {});

  std::expected<void, SendRejected> regenerate(holonight_domain::Conversation& conversation,
                                               const holonight_domain::ModelId& model,
                                               std::function<void(const holonight_domain::StreamEvent&)> on_event = {});

  // Idempotent: stopping a conversation with nothing in flight is a no-op, not an error.
  void stop(const holonight_domain::ConversationId& conversation_id);

  [[nodiscard]] bool isStreaming(const holonight_domain::ConversationId& conversation_id) const;

 private:
  struct PendingToolCall {
    QString invocation_id;
    holonight_domain::Message invocation_message;
    std::optional<holonight_domain::Message> result_message;
    ToolExecutionHandlePtr execution_handle;
  };

  struct InFlightStream {
    holonight_providers::HttpRequestHandlePtr handle = nullptr;
    holonight_domain::Message working_message;
    QString accumulated_text;
    holonight_domain::Conversation* conversation = nullptr;
    std::function<void(const holonight_domain::StreamEvent&)> on_event;
    // Wall-clock dispatch timestamp (REQ-F-011); duration_ms = terminal timestamp - this, computed
    // uniformly across all four providers regardless of which one actually streamed the response.
    std::chrono::milliseconds dispatch_time{};
    // Needed to re-dispatch via continueToolLoop() after a tool-call round-trip (REQ-F-001/NF-002);
    // startStream() no longer has any other place to remember which model this turn targets.
    holonight_domain::ModelId model;
    std::vector<PendingToolCall> pending_tool_calls;
    // Cumulative across every re-dispatch within one user turn; reset only in send()/regenerate()
    // (REQ-F-006).
    int tool_calls_this_turn = 0;
    // Reset at the start of each dispatchSendChat() round; tells the Completed branch whether to
    // finalize normally or re-enter the loop via continueToolLoop().
    bool tool_call_seen_this_stream = false;
    // A provider can emit its round-completed event before asynchronous local tools finish.
    // Keep the round open until both conditions are satisfied.
    bool provider_round_completed = false;
    std::optional<holonight_domain::StreamEvent> tool_round_sync_event;
  };

  std::expected<void, SendRejected> startStream(holonight_domain::Conversation& conversation,
                                                const holonight_domain::ModelId& model,
                                                holonight_domain::Message placeholder_message,
                                                std::function<void(const holonight_domain::StreamEvent&)> on_event);

  void handleStreamEvent(const QString& conversation_key, const holonight_domain::StreamEvent& event);
  // Enforces the REQ-F-006 cap and executes the tool through ToolOrchestrator, recording
  // invocation/result/next-placeholder messages. Split out of handleStreamEvent()'s std::visit to keep
  // its cognitive complexity in check.
  void handleToolCall(const QString& conversation_key, InFlightStream& stream,
                      const holonight_domain::StreamEvent& event, const holonight_domain::ToolRequestEvent& tool_call);
  void finishToolRound(const QString& conversation_key, InFlightStream& stream);
  // Re-dispatches dispatchSendChat() with the conversation's current message list (now including
  // every invocation/result/placeholder pair appended so far this turn) after a tool-call
  // round-trip, reusing the same in_flight_ entry so tool_calls_this_turn and stop()/cancellation
  // keep working across re-dispatches (REQ-F-006).
  void continueToolLoop(const QString& conversation_key, InFlightStream& stream);
  // Stamps the wall-clock duration since dispatch into usage (or a fresh Usage if none was
  // reported) -- the canonical, uniform-across-providers "time to generate" metric (REQ-F-011).
  [[nodiscard]] holonight_domain::Usage stampDuration(const InFlightStream& stream,
                                                      std::optional<holonight_domain::Usage> usage) const;

  // Four named concrete providers with if/else routing on ModelId::provider_id — deliberately not
  // a virtual provider interface. google-provider-adapter/DESIGN.md §5.1: Google's own SPEC.md §F.9
  // explicitly re-states the no-dynamic-dispatch constraint for a fourth consecutive provider,
  // independent of how many providers now exist, so the "rule of three" (now "rule of four") is
  // deliberately not acted on here.
  [[nodiscard]] bool hasModelsFor(const QString& provider_id) const;
  holonight_providers::HttpRequestHandlePtr dispatchSendChat(
      const holonight_domain::ModelId& model, const std::vector<holonight_domain::Message>& history,
      const std::function<void(const holonight_domain::StreamEvent&)>& on_event);

  std::shared_ptr<holonight_providers::OllamaProvider> ollama_provider_;
  std::shared_ptr<holonight_providers::OpenAIProvider> openai_provider_;
  std::shared_ptr<holonight_providers::AnthropicProvider> anthropic_provider_;
  std::shared_ptr<holonight_providers::GoogleProvider> google_provider_;
  ProviderAdapterRouter* router_ = nullptr;
  std::shared_ptr<holonight_providers::Clock> clock_;
  std::shared_ptr<ToolRegistry> tool_registry_;
  ToolOrchestrator tool_orchestrator_;
  QHash<QString, InFlightStream> in_flight_;
};

}  // namespace holonight_application
