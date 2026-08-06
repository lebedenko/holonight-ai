#include "holonight_application/chat_controller.h"

#include "holonight_application/provider_adapter_router.h"
#include "holonight_application/tools/tool_registry.h"

#include <QJsonArray>

#include <algorithm>

namespace {

[[nodiscard]] QJsonObject resultObjectFrom(const std::optional<QJsonValue>& value) {
  if (!value.has_value()) {
    return {};
  }

  const QJsonValue& result = *value;
  if (result.isObject()) {
    return result.toObject();
  }
  if (result.isArray()) {
    QJsonObject object;
    object.insert(QStringLiteral("result"), result.toArray());
    return object;
  }
  if (!result.isUndefined() && !result.isNull()) {
    QJsonObject object;
    object.insert(QStringLiteral("result"), result);
    return object;
  }

  return {};
}

holonight_domain::ToolCallEntry makeInvocationCallEntry(const holonight_domain::ToolInvocation& invocation,
                                                        const holonight_domain::ToolRequestEvent& tool_request) {
  return holonight_domain::ToolCallEntry{
      .kind = holonight_domain::ToolCallKind::Invocation,
      .tool_use_id = tool_request.provider_call_id,
      .tool_name = tool_request.function_name,
      .tool_id = invocation.tool_id,
      .function_name = invocation.function_name.isEmpty() ? tool_request.function_name : invocation.function_name,
      .input = tool_request.arguments,
      .result = {},
      .is_error = invocation.status == holonight_domain::ToolInvocationStatus::Failed ||
                  invocation.status == holonight_domain::ToolInvocationStatus::Denied ||
                  invocation.status == holonight_domain::ToolInvocationStatus::Cancelled ||
                  invocation.error.has_value(),
      .status = invocation.status,
      .requested_at = invocation.requested_at,
      .started_at = invocation.started_at,
      .finished_at = invocation.finished_at,
      .execution_location = invocation.location,
      .can_cancel = invocation.can_cancel,
      .thought_signature = tool_request.thought_signature,
      .provider_call_id_synthesized = tool_request.provider_call_id_synthesized,
      .provider_item_id = tool_request.provider_item_id,
      .provider_context = tool_request.provider_context,
  };
}

holonight_domain::ToolCallEntry makeResultCallEntry(const holonight_domain::ToolInvocation& invocation,
                                                    const holonight_domain::ToolRequestEvent& tool_request) {
  return holonight_domain::ToolCallEntry{
      .kind = holonight_domain::ToolCallKind::Result,
      .tool_use_id = tool_request.provider_call_id,
      .tool_name = tool_request.function_name,
      .tool_id = invocation.tool_id,
      .function_name = invocation.function_name.isEmpty() ? tool_request.function_name : invocation.function_name,
      .result = resultObjectFrom(invocation.result),
      .is_error = (invocation.error.has_value() || invocation.status == holonight_domain::ToolInvocationStatus::Failed),
      .status = invocation.status,
      .requested_at = invocation.requested_at,
      .started_at = invocation.started_at,
      .finished_at = invocation.finished_at,
      .execution_location = invocation.location,
      .provider_call_id_synthesized = tool_request.provider_call_id_synthesized};
}

}  // namespace

namespace holonight_application {

using holonight_domain::Conversation;
using holonight_domain::ConversationId;
using holonight_domain::Message;
using holonight_domain::MessageId;
using holonight_domain::MessageRole;
using holonight_domain::MessageStatus;
using holonight_domain::ModelId;
using holonight_domain::StreamEvent;
using holonight_domain::ToolCallEntry;
using holonight_domain::ToolCallKind;
using holonight_domain::ToolInvocation;
using holonight_domain::ToolInvocationStatus;

ChatController::ChatController(std::shared_ptr<holonight_providers::OllamaProvider> ollama_provider,
                               std::shared_ptr<holonight_providers::OpenAIProvider> openai_provider,
                               std::shared_ptr<holonight_providers::AnthropicProvider> anthropic_provider,
                               std::shared_ptr<holonight_providers::GoogleProvider> google_provider,
                               std::shared_ptr<holonight_providers::Clock> clock,
                               std::shared_ptr<ToolRegistry> tool_registry)
    : ollama_provider_(std::move(ollama_provider)),
      openai_provider_(std::move(openai_provider)),
      anthropic_provider_(std::move(anthropic_provider)),
      google_provider_(std::move(google_provider)),
      clock_(std::move(clock)),
      tool_registry_(tool_registry ? std::move(tool_registry) : std::make_shared<ToolRegistry>()) {}

ChatController::ChatController(ProviderAdapterRouter* router, std::shared_ptr<holonight_providers::Clock> clock,
                               std::shared_ptr<ToolRegistry> tool_registry)
    : router_(router),
      clock_(std::move(clock)),
      tool_registry_(tool_registry ? std::move(tool_registry) : std::make_shared<ToolRegistry>()) {}

bool ChatController::hasModelsFor(const QString& provider_id) const {
  if (router_ != nullptr) {
    const auto* models = router_->availableModels(provider_id);
    return router_->isEnabled(provider_id) && models != nullptr && !models->empty();
  }
  if (provider_id == QStringLiteral("openai")) {
    return !openai_provider_->availableModels().empty();
  }
  if (provider_id == QStringLiteral("anthropic")) {
    return !anthropic_provider_->availableModels().empty();
  }
  if (provider_id == QStringLiteral("google")) {
    return !google_provider_->availableModels().empty();
  }
  return !ollama_provider_->availableModels().empty();
}

holonight_providers::HttpRequestHandlePtr ChatController::dispatchSendChat(
    const ModelId& model, const std::vector<Message>& history,
    const std::function<void(const StreamEvent&)>& on_event) {
  if (router_ != nullptr) {
    return router_->sendChat(model, history, on_event);
  }
  if (model.provider_id == QStringLiteral("openai")) {
    return openai_provider_->sendChat(model, history, on_event);
  }
  if (model.provider_id == QStringLiteral("anthropic")) {
    return anthropic_provider_->sendChat(model, history, on_event);
  }
  if (model.provider_id == QStringLiteral("google")) {
    return google_provider_->sendChat(model, history, on_event);
  }
  return ollama_provider_->sendChat(model, history, on_event);
}

std::expected<void, SendRejected> ChatController::send(Conversation& conversation, const ModelId& model,
                                                       QString user_text,
                                                       std::function<void(const StreamEvent&)> on_event) {
  if (model.model_name.isEmpty() || model.provider_id.isEmpty() || !hasModelsFor(model.provider_id)) {
    return std::unexpected(SendRejected{.reason = SendRejectReason::NoModelSelected});
  }

  const QString conversationKey = conversation.id().toString();
  if (in_flight_.contains(conversationKey)) {
    return std::unexpected(SendRejected{.reason = SendRejectReason::AlreadyStreaming});
  }

  conversation.appendMessage(
      Message(MessageId::generate(), MessageRole::User, std::move(user_text), MessageStatus::Complete));

  Message assistantMessage(MessageId::generate(), MessageRole::Assistant, QString(), MessageStatus::Pending,
                           QDateTime{}, model);
  conversation.appendMessage(assistantMessage);

  return startStream(conversation, model, std::move(assistantMessage), std::move(on_event));
}

std::expected<void, SendRejected> ChatController::regenerate(Conversation& conversation, const ModelId& model,
                                                             std::function<void(const StreamEvent&)> on_event) {
  if (model.model_name.isEmpty() || model.provider_id.isEmpty() || !hasModelsFor(model.provider_id)) {
    return std::unexpected(SendRejected{.reason = SendRejectReason::NoModelSelected});
  }

  const QString conversationKey = conversation.id().toString();
  if (in_flight_.contains(conversationKey)) {
    return std::unexpected(SendRejected{.reason = SendRejectReason::AlreadyStreaming});
  }

  const std::vector<Message>& messages = conversation.messages();
  if (messages.empty() || messages.back().role() != MessageRole::Assistant) {
    return std::unexpected(SendRejected{.reason = SendRejectReason::NoAssistantMessageToRegenerate});
  }

  Message regeneratedMessage(messages.back().id(), MessageRole::Assistant, QString(), MessageStatus::Pending,
                             messages.back().createdAt(), model);
  conversation.replaceLastMessage(regeneratedMessage);

  return startStream(conversation, model, std::move(regeneratedMessage), std::move(on_event));
}

std::expected<void, SendRejected> ChatController::startStream(Conversation& conversation, const ModelId& model,
                                                              Message placeholder_message,
                                                              std::function<void(const StreamEvent&)> on_event) {
  const QString conversationKey = conversation.id().toString();

  Message workingMessage = std::move(placeholder_message);
  static_cast<void>(workingMessage.transitionTo(MessageStatus::Streaming));
  conversation.replaceLastMessage(workingMessage);

  std::vector<Message> history = conversation.messages();
  history.pop_back();  // exclude the placeholder/regenerated message itself

  InFlightStream stream;
  stream.conversation = &conversation;
  stream.working_message = workingMessage;
  stream.on_event = std::move(on_event);
  stream.dispatch_time = clock_->now();
  stream.model = model;
  in_flight_.insert(conversationKey, stream);

  holonight_providers::HttpRequestHandlePtr handle = dispatchSendChat(
      model, history, [this, conversationKey](const StreamEvent& event) { handleStreamEvent(conversationKey, event); });

  auto entryIt = in_flight_.find(conversationKey);
  if (entryIt != in_flight_.end()) {
    entryIt->handle = handle;
  }

  return {};
}

holonight_domain::Usage ChatController::stampDuration(const InFlightStream& stream,
                                                      std::optional<holonight_domain::Usage> usage) const {
  holonight_domain::Usage stamped = usage.value_or(holonight_domain::Usage{});
  stamped.duration_ms = std::max<qint64>(1, (clock_->now() - stream.dispatch_time).count());
  return stamped;
}

void ChatController::handleStreamEvent(const QString& conversation_key, const StreamEvent& event) {
  auto entryIt = in_flight_.find(conversation_key);
  if (entryIt == in_flight_.end()) {
    return;
  }
  InFlightStream& stream = entryIt.value();

  std::visit(
      [&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, holonight_domain::ContentDelta>) {
          stream.accumulated_text += value.text;
          stream.working_message.setText(stream.accumulated_text);
          stream.conversation->replaceLastMessage(stream.working_message);
          if (stream.on_event) {
            stream.on_event(event);
          }
        } else if constexpr (std::is_same_v<T, holonight_domain::Completed>) {
          if (stream.tool_call_seen_this_stream) {
            stream.provider_round_completed = true;
            if (std::ranges::all_of(stream.pending_tool_calls,
                                    [](const PendingToolCall& call) { return call.result_message.has_value(); })) {
              finishToolRound(conversation_key, stream);
            }
            return;
          }
          static_cast<void>(stream.working_message.transitionTo(MessageStatus::Complete));
          stream.conversation->replaceLastMessage(stream.working_message);
          if (stream.on_event) {
            stream.on_event(StreamEvent{holonight_domain::Completed{.usage = stampDuration(stream, value.usage),
                                                                    .model_identifier = value.model_identifier}});
          }
          in_flight_.remove(conversation_key);
        } else if constexpr (std::is_same_v<T, holonight_domain::ToolRequestEvent>) {
          handleToolCall(conversation_key, stream, event, value);
        } else if constexpr (std::is_same_v<T, holonight_domain::Error>) {
          static_cast<void>(stream.working_message.transitionTo(MessageStatus::Error));
          const QString messageText = stream.accumulated_text.isEmpty()
                                          ? value.message
                                          : stream.accumulated_text + QStringLiteral("\n\n") + value.message;
          stream.working_message.setText(messageText);
          stream.conversation->replaceLastMessage(stream.working_message);
          if (stream.on_event) {
            stream.on_event(StreamEvent{holonight_domain::Error{.message = value.message,
                                                                .usage = stampDuration(stream, value.usage),
                                                                .model_identifier = value.model_identifier}});
          }
          in_flight_.remove(conversation_key);
        } else if constexpr (std::is_same_v<T, holonight_domain::Cancelled>) {
          static_cast<void>(stream.working_message.transitionTo(MessageStatus::Cancelled));
          stream.conversation->replaceLastMessage(stream.working_message);
          if (stream.on_event) {
            stream.on_event(StreamEvent{holonight_domain::Cancelled{.usage = stampDuration(stream, value.usage),
                                                                    .model_identifier = value.model_identifier}});
          }
          in_flight_.remove(conversation_key);
        }
      },
      event);
}

void ChatController::handleToolCall(const QString& conversation_key, InFlightStream& stream, const StreamEvent& event,
                                    const holonight_domain::ToolRequestEvent& tool_call) {
  if (tool_call.execution_location != holonight_domain::ToolExecutionLocation::LocalClient) {
    static_cast<void>(stream.working_message.transitionTo(MessageStatus::Complete));
    stream.conversation->replaceLastMessage(stream.working_message);

    Message observation(MessageId::generate(), MessageRole::Assistant, QString(), MessageStatus::Complete);
    observation.setToolCalls({ToolCallEntry{.kind = ToolCallKind::Invocation,
                                            .tool_use_id = tool_call.provider_call_id,
                                            .tool_name = tool_call.function_name,
                                            .function_name = tool_call.function_name,
                                            .input = tool_call.arguments,
                                            .status = ToolInvocationStatus::Completed,
                                            .requested_at = QDateTime::currentDateTimeUtc(),
                                            .finished_at = QDateTime::currentDateTimeUtc(),
                                            .execution_location = tool_call.execution_location,
                                            .thought_signature = tool_call.thought_signature,
                                            .provider_call_id_synthesized = tool_call.provider_call_id_synthesized}});
    auto entries = observation.toolCalls();
    entries.front().provider_item_id = tool_call.provider_item_id;
    entries.front().provider_context = tool_call.provider_context;
    observation.setToolCalls(std::move(entries));
    stream.conversation->appendMessage(observation);
    stream.working_message = observation;
    if (stream.on_event) {
      stream.on_event(event);
    }
    return;
  }

  if (stream.tool_calls_this_turn >= kMaxToolCallsPerTurn) {
    // REQ-F-006(2)/(4): the 11th call is neither executed nor counted further.
    const QString limitMessage =
        QStringLiteral("Tool-calling limit exceeded (max %1 calls per turn); stopping.").arg(kMaxToolCallsPerTurn);
    static_cast<void>(stream.working_message.transitionTo(MessageStatus::Error));
    stream.working_message.setText(limitMessage);
    stream.conversation->replaceLastMessage(stream.working_message);
    if (stream.on_event) {
      stream.on_event(StreamEvent{holonight_domain::Error{.message = limitMessage}});
    }
    // Capture the handle and remove the in_flight_ entry BEFORE cancelling it: some providers'
    // handles (e.g. Anthropic's CancellationAwareHandle) synchronously re-enter
    // handleStreamEvent() with a Cancelled event on cancel(). Removing the entry first makes that
    // reentrant call a no-op via handleStreamEvent()'s own `entryIt == in_flight_.end()` guard, so
    // it cannot clobber the Error status/message just set here (or use-after-free `stream`, which
    // is a reference into the very map entry being removed).
    const holonight_providers::HttpRequestHandlePtr handle = stream.handle;
    in_flight_.remove(conversation_key);
    if (handle) {
      handle->cancel();
    }
    return;
  }

  stream.tool_calls_this_turn += 1;
  const bool firstToolCallThisRound = !stream.tool_call_seen_this_stream;
  stream.tool_call_seen_this_stream = true;

  // 1. Finalize any in-progress prose bubble so its partial text is preserved as its own message
  //    and any later ContentDelta text in this same stream starts a fresh bubble.
  if (firstToolCallThisRound) {
    static_cast<void>(stream.working_message.transitionTo(MessageStatus::Complete));
    stream.conversation->replaceMessage(stream.working_message);
  }

  // 2. Record the invocation as its own, visually distinct Message (REQ-F-007(1)).
  Message invocation(MessageId::generate(), MessageRole::Assistant, QString(), MessageStatus::Complete);
  const QString invocationId = invocation.id().toString();
  const StreamEvent& toolCallEvent = event;
  invocation.setToolCalls({ToolCallEntry{.kind = ToolCallKind::Invocation,
                                         .tool_use_id = tool_call.provider_call_id,
                                         .tool_name = tool_call.function_name,
                                         .function_name = tool_call.function_name,
                                         .input = tool_call.arguments,
                                         .status = ToolInvocationStatus::Requested,
                                         .requested_at = QDateTime::currentDateTimeUtc(),
                                         .execution_location = tool_call.execution_location,
                                         .thought_signature = tool_call.thought_signature,
                                         .provider_call_id_synthesized = tool_call.provider_call_id_synthesized,
                                         .provider_item_id = tool_call.provider_item_id,
                                         .provider_context = tool_call.provider_context}});
  stream.conversation->appendMessage(invocation);
  stream.pending_tool_calls.push_back(PendingToolCall{
      .invocation_id = invocationId,
      .invocation_message = invocation,
  });

  const ToolOrchestrator::ExecutionRequest request{
      .invocation_id = invocationId,
      .provider_call_id = tool_call.provider_call_id,
      .function_name = tool_call.function_name,
      .arguments = tool_call.arguments,
      .requested_at = QDateTime::currentDateTimeUtc(),
  };

  const auto on_running = [this, conversation_key, tool_call, toolCallEvent](const ToolInvocation& invocation) {
    auto entryIt = in_flight_.find(conversation_key);
    if (entryIt == in_flight_.end()) {
      return;
    }

    InFlightStream& stream = entryIt.value();
    const auto callIt = std::ranges::find(stream.pending_tool_calls, invocation.id, &PendingToolCall::invocation_id);
    if (callIt == stream.pending_tool_calls.end()) {
      return;
    }
    callIt->invocation_message.setToolCalls({makeInvocationCallEntry(invocation, tool_call)});
    stream.conversation->replaceMessage(callIt->invocation_message);
    if (stream.on_event) {
      stream.on_event(toolCallEvent);
    }
  };

  const auto on_terminal = [this, conversation_key, tool_call, toolCallEvent](const ToolInvocation& invocation) {
    auto entryIt = in_flight_.find(conversation_key);
    if (entryIt == in_flight_.end()) {
      return;
    }

    InFlightStream& stream = entryIt.value();
    const auto callIt = std::ranges::find(stream.pending_tool_calls, invocation.id, &PendingToolCall::invocation_id);
    if (callIt == stream.pending_tool_calls.end()) {
      return;
    }
    callIt->invocation_message.setToolCalls({makeInvocationCallEntry(invocation, tool_call)});
    stream.conversation->replaceMessage(callIt->invocation_message);

    Message result(MessageId::generate(), MessageRole::User, QString(), MessageStatus::Complete);
    result.setToolCalls({makeResultCallEntry(invocation, tool_call)});
    callIt->result_message = std::move(result);
    callIt->execution_handle.reset();
    stream.tool_round_sync_event = toolCallEvent;
    const bool roundFinished = stream.provider_round_completed &&
                               std::ranges::all_of(stream.pending_tool_calls, [](const PendingToolCall& call) {
                                 return call.result_message.has_value();
                               });
    if (roundFinished) {
      finishToolRound(conversation_key, stream);
    } else if (stream.on_event) {
      stream.on_event(toolCallEvent);
    }
  };

  const auto on_capabilities_changed = [this, conversation_key, tool_call,
                                        toolCallEvent](const ToolInvocation& invocation) {
    auto entryIt = in_flight_.find(conversation_key);
    if (entryIt == in_flight_.end()) {
      return;
    }
    InFlightStream& stream = entryIt.value();
    const auto callIt = std::ranges::find(stream.pending_tool_calls, invocation.id, &PendingToolCall::invocation_id);
    if (callIt == stream.pending_tool_calls.end()) {
      return;
    }
    callIt->invocation_message.setToolCalls({makeInvocationCallEntry(invocation, tool_call)});
    stream.conversation->replaceMessage(callIt->invocation_message);
    if (stream.on_event) {
      stream.on_event(toolCallEvent);
    }
  };

  ToolExecutionHandlePtr executionHandle = ToolOrchestrator::execute(
      *tool_registry_, request,
      ToolOrchestrator::Callbacks{
          .on_running = on_running, .on_capabilities_changed = on_capabilities_changed, .on_terminal = on_terminal});
  const auto callIt = std::ranges::find(stream.pending_tool_calls, invocationId, &PendingToolCall::invocation_id);
  if (callIt != stream.pending_tool_calls.end() && !callIt->result_message.has_value()) {
    callIt->execution_handle = std::move(executionHandle);
  }

  // Publish initial synchronization point at invocation/begin transition.
  if (stream.on_event) {
    stream.on_event(event);
  }
}

void ChatController::finishToolRound(const QString& conversation_key, InFlightStream& stream) {
  for (const PendingToolCall& call : stream.pending_tool_calls) {
    if (call.result_message.has_value()) {
      stream.conversation->appendMessage(*call.result_message);
    }
  }
  stream.pending_tool_calls.clear();

  Message nextPlaceholder(MessageId::generate(), MessageRole::Assistant, QString(), MessageStatus::Streaming,
                          QDateTime{}, stream.model);
  stream.conversation->appendMessage(nextPlaceholder);
  stream.working_message = nextPlaceholder;
  stream.accumulated_text.clear();
  const auto onEvent = stream.on_event;
  const auto syncEvent = stream.tool_round_sync_event;
  stream.tool_round_sync_event.reset();
  continueToolLoop(conversation_key, stream);
  if (onEvent && syncEvent.has_value()) {
    onEvent(*syncEvent);
  }
}

void ChatController::continueToolLoop(const QString& conversation_key, InFlightStream& stream) {
  stream.tool_call_seen_this_stream = false;
  stream.provider_round_completed = false;

  std::vector<Message> history = stream.conversation->messages();
  history.pop_back();  // exclude the fresh placeholder appended by handleToolCall()

  stream.dispatch_time = clock_->now();
  holonight_providers::HttpRequestHandlePtr handle = dispatchSendChat(
      stream.model, history,
      [this, conversation_key](const StreamEvent& event) { handleStreamEvent(conversation_key, event); });

  auto entryIt = in_flight_.find(conversation_key);
  if (entryIt != in_flight_.end()) {
    entryIt->handle = handle;
  }
}

void ChatController::stop(const ConversationId& conversation_id) {
  const QString key = conversation_id.toString();
  auto entryIt = in_flight_.find(key);
  if (entryIt == in_flight_.end()) {
    return;
  }

  InFlightStream stream = entryIt.value();
  std::vector<ToolExecutionHandlePtr> toolExecutionHandles;
  for (const PendingToolCall& call : stream.pending_tool_calls) {
    if (call.execution_handle != nullptr) {
      toolExecutionHandles.push_back(call.execution_handle);
    }
  }
  const holonight_providers::HttpRequestHandlePtr handle = stream.handle;

  in_flight_.remove(key);

  if (handle) {
    handle->cancel();
  }
  for (const ToolExecutionHandlePtr& toolExecutionHandle : toolExecutionHandles) {
    toolExecutionHandle->cancel();
  }

  static_cast<void>(stream.working_message.transitionTo(MessageStatus::Cancelled));
  if (stream.conversation != nullptr) {
    stream.conversation->replaceMessage(stream.working_message);
  }
  if (stream.on_event) {
    stream.on_event(StreamEvent{holonight_domain::Cancelled{.usage = stampDuration(stream, std::nullopt),
                                                            .model_identifier = stream.model.model_name}});
  }
}

bool ChatController::isStreaming(const ConversationId& conversation_id) const {
  return in_flight_.contains(conversation_id.toString());
}

}  // namespace holonight_application
