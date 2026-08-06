#include "holonight_application/chat_controller.h"
#include "holonight_application/tools/i_tool.h"
#include "holonight_application/tools/tool_presenters.h"
#include "holonight_application/tools/tool_registry.h"
#include "holonight_providers/anthropic_provider.h"
#include "holonight_providers/google_provider.h"
#include "holonight_providers/ollama_provider.h"
#include "holonight_providers/openai_provider.h"
#include "providers/fake_clock.h"
#include "providers/fake_http_client.h"

#include <QByteArray>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <gtest/gtest.h>
#include <holonight_domain/holonight_domain.h>
#include <memory>
#include <variant>
#include <vector>

namespace holonight_application {
namespace {

using holonight_domain::Cancelled;
using holonight_domain::Completed;
using holonight_domain::ContentDelta;
using holonight_domain::Conversation;
using holonight_domain::ConversationId;
using holonight_domain::Error;
using holonight_domain::Message;
using holonight_domain::MessageId;
using holonight_domain::MessageRole;
using holonight_domain::MessageStatus;
using holonight_domain::ModelId;
using holonight_domain::StreamEvent;
using holonight_domain::ToolCallKind;
using holonight_providers::AnthropicProvider;
using holonight_providers::FakeHttpClient;
using holonight_providers::GoogleProvider;
using holonight_providers::OllamaProvider;
using holonight_providers::OpenAIProvider;

// Test-only ITool: always succeeds, so tests can drive the tool-calling loop without depending on
// any concrete tool's real behavior (REQ-F-001(2): ChatController must dispatch generically).
class RecordingTool : public ITool {
 public:
  [[nodiscard]] QString name() const override { return QStringLiteral("RecordingTool"); }
  [[nodiscard]] QString description() const override { return QStringLiteral("Test-only no-op tool."); }
  [[nodiscard]] QJsonObject schema() const override { return QJsonObject{}; }
  [[nodiscard]] QJsonObject execute(const QJsonObject& /*parameters*/) override {
    ++call_count;
    return QJsonObject{{QStringLiteral("ok"), true}};
  }

  int call_count = 0;
};

class DeferredHandle final : public ToolExecutionHandle {
 public:
  [[nodiscard]] bool canCancel() const override { return true; }
  void cancel() override { cancelled = true; }

  bool cancelled = false;
};

class DeferredExecutor final : public IToolExecutor {
 public:
  [[nodiscard]] ToolExecutionHandlePtr start(const ToolExecutionRequest& request,
                                             ToolOutcomeCallback on_finished) override {
    requests.push_back(request);
    callbacks.push_back(std::move(on_finished));
    auto handle = std::make_shared<DeferredHandle>();
    handles.push_back(handle);
    return handle;
  }

  void finish(std::size_t index) {
    callbacks.at(index)(
        ToolOutcome{.result = QJsonObject{{QStringLiteral("call"), requests.at(index).provider_call_id}}});
  }

  std::vector<ToolExecutionRequest> requests;
  std::vector<ToolOutcomeCallback> callbacks;
  std::vector<std::shared_ptr<DeferredHandle>> handles;
};

ToolRegistration deferredToolRegistration(const std::shared_ptr<DeferredExecutor>& executor) {
  return ToolRegistration{
      .definition = ToolDefinition{.id = QStringLiteral("recording.tool"),
                                   .function_name = QStringLiteral("RecordingTool"),
                                   .display_name = QStringLiteral("Recording tool"),
                                   .renderer_key = QStringLiteral("generic"),
                                   .description = QStringLiteral("Test-only deferred tool."),
                                   .input_schema = QJsonObject{},
                                   .risk = ToolDefinition::ToolRisk::Safe},
      .executor = executor,
      .presenter = std::make_shared<GenericToolPresenter>(),
  };
}

// Builds the minimal Anthropic SSE sequence that yields exactly one StreamEvent::ToolCall followed
// by a StreamEvent::Completed (content_block_start/stop bracket a tool_use block with no
// input_json_delta fragments, so input defaults to "{}" -- see AnthropicProvider's own tests).
QByteArray toolCallRoundSse(const QString& toolUseId) {
  return QByteArray(R"(data: {"type":"content_block_start","index":0,"content_block":{"type":"tool_use","id":")") +
         toolUseId.toUtf8() + QByteArray(R"(","name":"RecordingTool"}})") + "\n\n" +
         QByteArray(R"(data: {"type":"content_block_stop","index":0})") + "\n\n" +
         QByteArray(R"(data: {"type":"message_stop"})") + "\n\n";
}

QByteArray googleToolCallRoundSse(const QStringList& callIds) {
  QJsonArray parts;
  for (const QString& callId : callIds) {
    QJsonObject functionCall{{QStringLiteral("name"), QStringLiteral("RecordingTool")},
                             {QStringLiteral("args"), QJsonObject{}}};
    if (!callId.isNull()) {
      functionCall[QStringLiteral("id")] = callId;
    }
    parts.append(QJsonObject{{QStringLiteral("functionCall"), functionCall}});
  }
  const QJsonObject payload{
      {QStringLiteral("candidates"),
       QJsonArray{QJsonObject{{QStringLiteral("content"), QJsonObject{{QStringLiteral("role"), QStringLiteral("model")},
                                                                      {QStringLiteral("parts"), parts}}},
                              {QStringLiteral("finishReason"), QStringLiteral("STOP")}}}}};
  return QByteArrayLiteral("data: ") + QJsonDocument(payload).toJson(QJsonDocument::Compact) +
         QByteArrayLiteral("\n\n");
}

std::shared_ptr<OllamaProvider> makeOllamaProvider(const std::shared_ptr<FakeHttpClient>& client) {
  client->enqueueBufferedSuccess(QByteArray(R"({"models":[{"name":"llama3"}]})"));
  auto provider = std::make_shared<OllamaProvider>(client);
  provider->refresh();
  return provider;
}

std::shared_ptr<OpenAIProvider> makeOpenAiProvider(const std::shared_ptr<FakeHttpClient>& client) {
  client->enqueueBufferedSuccess(QByteArray(R"({"data":[{"id":"gpt-4o"}]})"));
  auto provider = std::make_shared<OpenAIProvider>(client);
  provider->refresh();
  return provider;
}

std::shared_ptr<AnthropicProvider> makeAnthropicProvider(const std::shared_ptr<FakeHttpClient>& client) {
  client->enqueueBufferedSuccess(QByteArray(R"({"data":[{"id":"claude-3-5-sonnet-20241022"}]})"));
  auto provider = std::make_shared<AnthropicProvider>(client);
  provider->refresh();
  return provider;
}

std::shared_ptr<GoogleProvider> makeGoogleProvider(const std::shared_ptr<FakeHttpClient>& client) {
  client->enqueueBufferedSuccess(QByteArray(
      R"({"models":[{"name":"models/gemini-2.0-flash","supportedGenerationMethods":["generateContent"]}]})"));
  auto provider = std::make_shared<GoogleProvider>(client);
  provider->refresh();
  return provider;
}

struct Fixture {
  std::shared_ptr<FakeHttpClient> http_client = std::make_shared<FakeHttpClient>();
  std::shared_ptr<OllamaProvider> provider = makeOllamaProvider(http_client);
  std::shared_ptr<FakeHttpClient> openai_http_client = std::make_shared<FakeHttpClient>();
  std::shared_ptr<OpenAIProvider> openai_provider = makeOpenAiProvider(openai_http_client);
  std::shared_ptr<FakeHttpClient> anthropic_http_client = std::make_shared<FakeHttpClient>();
  std::shared_ptr<AnthropicProvider> anthropic_provider = makeAnthropicProvider(anthropic_http_client);
  std::shared_ptr<FakeHttpClient> google_http_client = std::make_shared<FakeHttpClient>();
  std::shared_ptr<GoogleProvider> google_provider = makeGoogleProvider(google_http_client);
  ChatController controller{provider, openai_provider, anthropic_provider, google_provider};
};

Conversation makeConversation() {
  return {ConversationId::generate(), QString("Title"), QDateTime::currentDateTimeUtc()};
}

ModelId testModel() { return ModelId{.provider_id = QStringLiteral("ollama"), .model_name = QStringLiteral("llama3")}; }

ModelId testOpenAiModel() {
  return ModelId{.provider_id = QStringLiteral("openai"), .model_name = QStringLiteral("gpt-4o")};
}

ModelId testAnthropicModel() {
  return ModelId{.provider_id = QStringLiteral("anthropic"),
                 .model_name = QStringLiteral("claude-3-5-sonnet-20241022")};
}

ModelId testGoogleModel() {
  return ModelId{.provider_id = QStringLiteral("google"), .model_name = QStringLiteral("gemini-2.0-flash")};
}

TEST(ChatController, SendStreamsContentDeltasThenCompletes) {
  Fixture fixture;
  Conversation conversation = makeConversation();

  std::vector<StreamEvent> events;
  const auto result = fixture.controller.send(conversation, testModel(), QString("hello"),
                                              [&events](const StreamEvent& event) { events.push_back(event); });
  ASSERT_TRUE(result.has_value());

  ASSERT_EQ(fixture.http_client->streamingCallCount(), 1U);
  fixture.http_client->emitData(
      0, QByteArray(R"({"model":"llama3","message":{"role":"assistant","content":"Hi"},"done":false})") + "\n");
  fixture.http_client->emitData(
      0, QByteArray(R"({"model":"llama3","message":{"role":"assistant","content":""},"done":true})") + "\n");
  fixture.http_client->emitFinished(0);

  // Every NDJSON line yields a ContentDelta (REQ-F-002), including the empty-content "done: true"
  // line Ollama sends to signal completion — so this is ContentDelta("Hi"), ContentDelta(""),
  // then Completed.
  ASSERT_EQ(events.size(), 3U);
  EXPECT_TRUE(std::holds_alternative<ContentDelta>(events[0]));
  EXPECT_TRUE(std::holds_alternative<ContentDelta>(events[1]));
  EXPECT_TRUE(std::holds_alternative<Completed>(events[2]));

  ASSERT_EQ(conversation.messages().size(), 2U);
  const Message& assistantMessage = conversation.messages()[1];
  EXPECT_EQ(assistantMessage.role(), MessageRole::Assistant);
  EXPECT_EQ(assistantMessage.text(), QStringLiteral("Hi"));
  EXPECT_EQ(assistantMessage.status(), MessageStatus::Complete);
  ASSERT_TRUE(assistantMessage.modelId().has_value());
  EXPECT_EQ(*assistantMessage.modelId(), testModel());
  EXPECT_FALSE(fixture.controller.isStreaming(conversation.id()));
}

TEST(ChatController, StopMidStreamRetainsPartialTextAndEmitsCancelled) {
  Fixture fixture;
  Conversation conversation = makeConversation();

  std::vector<StreamEvent> events;
  const auto result = fixture.controller.send(conversation, testModel(), QString("hello"),
                                              [&events](const StreamEvent& event) { events.push_back(event); });
  ASSERT_TRUE(result.has_value());

  fixture.http_client->emitData(
      0, QByteArray(R"({"model":"llama3","message":{"role":"assistant","content":"Partial"},"done":false})") + "\n");

  fixture.controller.stop(conversation.id());

  ASSERT_EQ(events.size(), 2U);
  EXPECT_TRUE(std::holds_alternative<ContentDelta>(events[0]));
  EXPECT_TRUE(std::holds_alternative<Cancelled>(events[1]));
  const auto& cancelled = std::get<Cancelled>(events[1]);
  ASSERT_TRUE(cancelled.usage.has_value());
  ASSERT_TRUE(cancelled.usage->duration_ms.has_value());
  EXPECT_GT(*cancelled.usage->duration_ms, 0);
  EXPECT_EQ(cancelled.model_identifier, QStringLiteral("llama3"));

  const Message& assistantMessage = conversation.messages().back();
  EXPECT_EQ(assistantMessage.status(), MessageStatus::Cancelled);
  EXPECT_EQ(assistantMessage.text(), QStringLiteral("Partial"));

  EXPECT_FALSE(fixture.controller.isStreaming(conversation.id()));
  EXPECT_TRUE(fixture.http_client->streamingCall(0).cancelled);

  // Data arriving after stop must not be forwarded — the stream is no longer current.
  fixture.http_client->emitData(
      0, QByteArray(R"({"model":"llama3","message":{"role":"assistant","content":"more"},"done":false})") + "\n");
  EXPECT_EQ(events.size(), 2U);
}

TEST(ChatController, RegeneratePreservesMessageIdAndConversationLength) {
  Fixture fixture;
  Conversation conversation = makeConversation();

  std::vector<StreamEvent> firstEvents;
  ASSERT_TRUE(fixture.controller
                  .send(conversation, testModel(), QString("hello"),
                        [&firstEvents](const StreamEvent& event) { firstEvents.push_back(event); })
                  .has_value());
  fixture.http_client->emitData(
      0, QByteArray(R"({"model":"llama3","message":{"role":"assistant","content":"first"},"done":true})") + "\n");
  fixture.http_client->emitFinished(0);

  ASSERT_EQ(conversation.messages().size(), 2U);
  const MessageId originalAssistantId = conversation.messages().back().id();
  const QDateTime originalAssistantCreatedAt = conversation.messages().back().createdAt();

  std::vector<StreamEvent> regenerateEvents;
  const auto regenerateResult = fixture.controller.regenerate(
      conversation, testModel(), [&regenerateEvents](const StreamEvent& event) { regenerateEvents.push_back(event); });
  ASSERT_TRUE(regenerateResult.has_value());

  ASSERT_EQ(fixture.http_client->streamingCallCount(), 2U);
  fixture.http_client->emitData(
      1, QByteArray(R"({"model":"llama3","message":{"role":"assistant","content":"second"},"done":true})") + "\n");
  fixture.http_client->emitFinished(1);

  ASSERT_EQ(conversation.messages().size(), 2U);
  const Message& regenerated = conversation.messages().back();
  EXPECT_EQ(regenerated.id(), originalAssistantId);
  EXPECT_EQ(regenerated.createdAt(), originalAssistantCreatedAt);
  EXPECT_EQ(regenerated.text(), QStringLiteral("second"));
  EXPECT_EQ(regenerated.status(), MessageStatus::Complete);
  ASSERT_TRUE(regenerated.modelId().has_value());
  EXPECT_EQ(*regenerated.modelId(), testModel());
}

TEST(ChatController, ConcurrentPerConversationStreamsDoNotInterfere) {
  Fixture fixture;
  Conversation conversationA = makeConversation();
  Conversation conversationB = makeConversation();

  std::vector<StreamEvent> eventsA;
  std::vector<StreamEvent> eventsB;
  ASSERT_TRUE(fixture.controller
                  .send(conversationA, testModel(), QString("a"),
                        [&eventsA](const StreamEvent& event) { eventsA.push_back(event); })
                  .has_value());
  ASSERT_TRUE(fixture.controller
                  .send(conversationB, testModel(), QString("b"),
                        [&eventsB](const StreamEvent& event) { eventsB.push_back(event); })
                  .has_value());

  ASSERT_EQ(fixture.http_client->streamingCallCount(), 2U);

  fixture.http_client->emitData(
      0, QByteArray(R"({"model":"llama3","message":{"role":"assistant","content":"from-a"},"done":true})") + "\n");
  fixture.http_client->emitFinished(0);

  EXPECT_TRUE(fixture.controller.isStreaming(conversationB.id()));
  EXPECT_FALSE(fixture.controller.isStreaming(conversationA.id()));

  fixture.http_client->emitData(
      1, QByteArray(R"({"model":"llama3","message":{"role":"assistant","content":"from-b"},"done":true})") + "\n");
  fixture.http_client->emitFinished(1);

  EXPECT_EQ(conversationA.messages().back().text(), QStringLiteral("from-a"));
  EXPECT_EQ(conversationB.messages().back().text(), QStringLiteral("from-b"));
}

TEST(ChatController, RejectsSendWithNoModelSelected) {
  Fixture fixture;
  Conversation conversation = makeConversation();

  const auto result = fixture.controller.send(conversation, ModelId{}, QString("hello"));

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().reason, SendRejectReason::NoModelSelected);
  EXPECT_TRUE(conversation.messages().empty());
  EXPECT_EQ(fixture.http_client->streamingCallCount(), 0U);
}

TEST(ChatController, RejectsSendWhenAlreadyStreaming) {
  Fixture fixture;
  Conversation conversation = makeConversation();

  ASSERT_TRUE(fixture.controller.send(conversation, testModel(), QString("first")).has_value());

  const auto secondResult = fixture.controller.send(conversation, testModel(), QString("second"));

  ASSERT_FALSE(secondResult.has_value());
  EXPECT_EQ(secondResult.error().reason, SendRejectReason::AlreadyStreaming);
  EXPECT_EQ(fixture.http_client->streamingCallCount(), 1U);
}

TEST(ChatController, ConnectionErrorSurfacesAsInlineErrorMessage) {
  Fixture fixture;
  Conversation conversation = makeConversation();

  std::vector<StreamEvent> events;
  ASSERT_TRUE(fixture.controller
                  .send(conversation, testModel(), QString("hello"),
                        [&events](const StreamEvent& event) { events.push_back(event); })
                  .has_value());

  fixture.http_client->emitError(0, QStringLiteral("Failed to connect to Ollama server"));

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<Error>(events[0]));

  const Message& assistantMessage = conversation.messages().back();
  EXPECT_EQ(assistantMessage.status(), MessageStatus::Error);
  EXPECT_EQ(assistantMessage.text(), QStringLiteral("Failed to connect to Ollama server"));
  EXPECT_FALSE(fixture.controller.isStreaming(conversation.id()));
}

TEST(ChatController, IdleTimeoutSurfacesAsInlineErrorMessage) {
  Fixture fixture;
  Conversation conversation = makeConversation();

  std::vector<StreamEvent> events;
  ASSERT_TRUE(fixture.controller
                  .send(conversation, testModel(), QString("hello"),
                        [&events](const StreamEvent& event) { events.push_back(event); })
                  .has_value());

  fixture.http_client->simulateIdleTimeout(0);

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<Error>(events[0]));
  EXPECT_EQ(conversation.messages().back().status(), MessageStatus::Error);
  EXPECT_FALSE(fixture.controller.isStreaming(conversation.id()));
}

TEST(ChatController, IncompleteTransportFinishMarksMessageErrorAndClearsInFlightState) {
  Fixture fixture;
  Conversation conversation = makeConversation();

  ASSERT_TRUE(fixture.controller.send(conversation, testModel(), QString("hello")).has_value());
  EXPECT_TRUE(fixture.controller.isStreaming(conversation.id()));

  fixture.http_client->emitFinished(0);

  EXPECT_EQ(conversation.messages().back().status(), MessageStatus::Error);
  EXPECT_FALSE(fixture.controller.isStreaming(conversation.id()));
}

TEST(ChatController, SendWithOpenAiModelRoutesToOpenAiProviderOnly) {
  Fixture fixture;
  Conversation conversation = makeConversation();

  const auto result = fixture.controller.send(conversation, testOpenAiModel(), QString("hello"));

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(fixture.openai_http_client->streamingCallCount(), 1U);
  EXPECT_EQ(fixture.http_client->streamingCallCount(), 0U);
}

TEST(ChatController, SendWithOllamaModelRoutesToOllamaProviderOnly) {
  Fixture fixture;
  Conversation conversation = makeConversation();

  const auto result = fixture.controller.send(conversation, testModel(), QString("hello"));

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(fixture.http_client->streamingCallCount(), 1U);
  EXPECT_EQ(fixture.openai_http_client->streamingCallCount(), 0U);
}

TEST(ChatController, SendWithOpenAiModelStreamsAndCompletesViaOpenAiSseFraming) {
  Fixture fixture;
  Conversation conversation = makeConversation();

  std::vector<StreamEvent> events;
  ASSERT_TRUE(fixture.controller
                  .send(conversation, testOpenAiModel(), QString("hello"),
                        [&events](const StreamEvent& event) { events.push_back(event); })
                  .has_value());

  fixture.openai_http_client->emitData(
      0, QByteArray(R"(data: {"type":"response.output_text.delta","delta":"Hi"})") + "\n\n");
  fixture.openai_http_client->emitData(0, QByteArray(R"(data: {"type":"response.completed"})") + "\n\n");

  ASSERT_EQ(events.size(), 2U);
  EXPECT_TRUE(std::holds_alternative<ContentDelta>(events[0]));
  EXPECT_TRUE(std::holds_alternative<Completed>(events[1]));
  EXPECT_EQ(conversation.messages().back().text(), QStringLiteral("Hi"));
}

TEST(ChatController, SendWithAnthropicModelRoutesToAnthropicProviderOnly) {
  Fixture fixture;
  Conversation conversation = makeConversation();

  const auto result = fixture.controller.send(conversation, testAnthropicModel(), QString("hello"));

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(fixture.anthropic_http_client->streamingCallCount(), 1U);
  EXPECT_EQ(fixture.http_client->streamingCallCount(), 0U);
  EXPECT_EQ(fixture.openai_http_client->streamingCallCount(), 0U);
}

TEST(ChatController, SendWithAnthropicModelStreamsAndCompletesViaAnthropicSseFraming) {
  Fixture fixture;
  Conversation conversation = makeConversation();

  std::vector<StreamEvent> events;
  ASSERT_TRUE(fixture.controller
                  .send(conversation, testAnthropicModel(), QString("hello"),
                        [&events](const StreamEvent& event) { events.push_back(event); })
                  .has_value());

  fixture.anthropic_http_client->emitData(
      0, QByteArray(R"(data: {"type":"content_block_delta","delta":{"type":"text_delta","text":"Hi"}})") + "\n\n");
  fixture.anthropic_http_client->emitData(0, QByteArray(R"(data: {"type":"message_stop"})") + "\n\n");

  ASSERT_EQ(events.size(), 2U);
  EXPECT_TRUE(std::holds_alternative<ContentDelta>(events[0]));
  EXPECT_TRUE(std::holds_alternative<Completed>(events[1]));
  EXPECT_EQ(conversation.messages().back().text(), QStringLiteral("Hi"));
}

TEST(ChatController, AnthropicTokenLimitMarksPartialResponseAsErrorWithoutDiscardingIt) {
  Fixture fixture;
  Conversation conversation = makeConversation();

  std::vector<StreamEvent> events;
  ASSERT_TRUE(fixture.controller
                  .send(conversation, testAnthropicModel(), QString("hello"),
                        [&events](const StreamEvent& event) { events.push_back(event); })
                  .has_value());

  const QByteArray payload =
      QByteArray(R"(data: {"type":"content_block_delta","delta":{"type":"text_delta","text":"partial"}})") + "\n\n" +
      QByteArray(R"(data: {"type":"message_delta","delta":{"stop_reason":"max_tokens"}})") + "\n\n" +
      QByteArray(R"(data: {"type":"message_stop"})") + "\n\n";
  fixture.anthropic_http_client->emitData(0, payload);

  ASSERT_EQ(events.size(), 2U);
  ASSERT_TRUE(std::holds_alternative<Error>(events[1]));
  const Message& assistantMessage = conversation.messages().back();
  EXPECT_EQ(assistantMessage.status(), MessageStatus::Error);
  EXPECT_EQ(
      assistantMessage.text(),
      QStringLiteral("partial\n\nAnthropic response was truncated after reaching the maximum output token limit"));
  EXPECT_FALSE(fixture.controller.isStreaming(conversation.id()));
}

TEST(ChatController, SendWithGoogleModelRoutesToGoogleProviderOnly) {
  Fixture fixture;
  Conversation conversation = makeConversation();

  const auto result = fixture.controller.send(conversation, testGoogleModel(), QString("hello"));

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(fixture.google_http_client->streamingCallCount(), 1U);
  EXPECT_EQ(fixture.http_client->streamingCallCount(), 0U);
  EXPECT_EQ(fixture.openai_http_client->streamingCallCount(), 0U);
  EXPECT_EQ(fixture.anthropic_http_client->streamingCallCount(), 0U);
}

TEST(ChatController, SendWithGoogleModelStreamsAndCompletesViaGoogleSseFraming) {
  Fixture fixture;
  Conversation conversation = makeConversation();

  std::vector<StreamEvent> events;
  ASSERT_TRUE(fixture.controller
                  .send(conversation, testGoogleModel(), QString("hello"),
                        [&events](const StreamEvent& event) { events.push_back(event); })
                  .has_value());

  fixture.google_http_client->emitData(
      0, QByteArray(
             R"(data: {"candidates":[{"content":{"role":"model","parts":[{"text":"Hi"}]},"finishReason":"STOP"}]})") +
             "\n\n");

  ASSERT_EQ(events.size(), 2U);
  EXPECT_TRUE(std::holds_alternative<ContentDelta>(events[0]));
  EXPECT_TRUE(std::holds_alternative<Completed>(events[1]));
  EXPECT_EQ(conversation.messages().back().text(), QStringLiteral("Hi"));
}

TEST(ChatController, DurationMsComputedExactly) {
  auto httpClient = std::make_shared<FakeHttpClient>();
  auto provider = makeOllamaProvider(httpClient);
  auto clock = std::make_shared<holonight_providers::FakeClock>();
  clock->push(1000).push(2500);
  ChatController controller(provider, nullptr, nullptr, nullptr, clock);
  Conversation conversation = makeConversation();

  std::vector<StreamEvent> events;
  ASSERT_TRUE(controller
                  .send(conversation, testModel(), QString("hello"),
                        [&events](const StreamEvent& event) { events.push_back(event); })
                  .has_value());

  httpClient->emitData(
      0, QByteArray(R"({"model":"llama3","message":{"role":"assistant","content":"Hi"},"done":true})") + "\n");

  ASSERT_EQ(events.size(), 2U);
  ASSERT_TRUE(std::holds_alternative<Completed>(events[1]));
  const auto& completed = std::get<Completed>(events[1]);
  ASSERT_TRUE(completed.usage.has_value());
  ASSERT_TRUE(completed.usage->duration_ms.has_value());
  EXPECT_EQ(*completed.usage->duration_ms, 1500);
}

TEST(ChatController, DefaultConstructedClockProducesPositiveDuration) {
  Fixture fixture;
  Conversation conversation = makeConversation();

  std::vector<StreamEvent> events;
  ASSERT_TRUE(fixture.controller
                  .send(conversation, testModel(), QString("hello"),
                        [&events](const StreamEvent& event) { events.push_back(event); })
                  .has_value());

  fixture.http_client->emitData(
      0, QByteArray(R"({"model":"llama3","message":{"role":"assistant","content":"Hi"},"done":true})") + "\n");

  ASSERT_EQ(events.size(), 2U);
  ASSERT_TRUE(std::holds_alternative<Completed>(events[1]));
  const auto& completed = std::get<Completed>(events[1]);
  ASSERT_TRUE(completed.usage.has_value());
  ASSERT_TRUE(completed.usage->duration_ms.has_value());
  EXPECT_GT(*completed.usage->duration_ms, 0);
}

TEST(ChatController, ToolCallLoopExecutesToolsGenericallyAndRecordsMessages) {
  Fixture fixture;
  auto toolRegistry = std::make_shared<ToolRegistry>();
  auto recordingTool = std::make_shared<RecordingTool>();
  toolRegistry->registerTool(recordingTool);
  ChatController controller(nullptr, nullptr, fixture.anthropic_provider, nullptr,
                            std::make_shared<holonight_providers::SteadyClock>(), toolRegistry);
  Conversation conversation = makeConversation();

  std::vector<StreamEvent> events;
  ASSERT_TRUE(controller
                  .send(conversation, testAnthropicModel(), QString("hello"),
                        [&events](const StreamEvent& event) { events.push_back(event); })
                  .has_value());

  fixture.anthropic_http_client->emitData(0, toolCallRoundSse(QStringLiteral("toolu_01")));

  // A second HTTP round trip should have been dispatched automatically (continueToolLoop).
  ASSERT_EQ(fixture.anthropic_http_client->streamingCallCount(), 2U);
  EXPECT_EQ(recordingTool->call_count, 1);

  // Invocation + Result messages were appended, followed by a fresh streaming placeholder.
  const std::vector<Message>& messages = conversation.messages();
  ASSERT_EQ(messages.size(), 5U);  // user, invocation, result, placeholder (original assistant slot consumed)
  const Message& invocation = messages[2];
  EXPECT_EQ(invocation.role(), MessageRole::Assistant);
  ASSERT_EQ(invocation.toolCalls().size(), 1U);
  EXPECT_EQ(invocation.toolCalls()[0].kind, ToolCallKind::Invocation);
  EXPECT_EQ(invocation.toolCalls()[0].tool_name, QStringLiteral("RecordingTool"));
  EXPECT_EQ(invocation.toolCalls()[0].tool_use_id, QStringLiteral("toolu_01"));

  const Message& result = messages[3];
  EXPECT_EQ(result.role(), MessageRole::User);
  ASSERT_EQ(result.toolCalls().size(), 1U);
  EXPECT_EQ(result.toolCalls()[0].kind, ToolCallKind::Result);
  EXPECT_EQ(result.toolCalls()[0].tool_use_id, QStringLiteral("toolu_01"));
  EXPECT_FALSE(result.toolCalls()[0].is_error);

  EXPECT_EQ(messages.back().status(), MessageStatus::Streaming);
  EXPECT_TRUE(controller.isStreaming(conversation.id()));

  fixture.anthropic_http_client->emitData(
      1, QByteArray(R"(data: {"type":"content_block_delta","delta":{"type":"text_delta","text":"done"}})") + "\n\n" +
             QByteArray(R"(data: {"type":"message_stop"})") + "\n\n");

  EXPECT_EQ(conversation.messages().back().text(), QStringLiteral("done"));
  EXPECT_EQ(conversation.messages().back().status(), MessageStatus::Complete);
  EXPECT_FALSE(controller.isStreaming(conversation.id()));
}

TEST(ChatController, GoogleSynthesizedCallIdStaysInternalOnFollowUp) {
  Fixture fixture;
  auto toolRegistry = std::make_shared<ToolRegistry>();
  toolRegistry->registerTool(std::make_shared<RecordingTool>());
  ChatController controller(nullptr, nullptr, nullptr, fixture.google_provider,
                            std::make_shared<holonight_providers::SteadyClock>(), toolRegistry);
  Conversation conversation = makeConversation();

  ASSERT_TRUE(controller.send(conversation, testGoogleModel(), QStringLiteral("hello")).has_value());
  fixture.google_http_client->emitData(0, googleToolCallRoundSse({QString{}}));

  ASSERT_EQ(fixture.google_http_client->streamingCallCount(), 2U);
  const QJsonObject followUp =
      QJsonDocument::fromJson(fixture.google_http_client->streamingCall(1).request.body).object();
  const QJsonArray contents = followUp.value(QStringLiteral("contents")).toArray();
  ASSERT_GE(contents.size(), 2);
  const QJsonObject functionResponse = contents.last()
                                           .toObject()
                                           .value(QStringLiteral("parts"))
                                           .toArray()
                                           .first()
                                           .toObject()
                                           .value(QStringLiteral("functionResponse"))
                                           .toObject();
  EXPECT_FALSE(functionResponse.contains(QStringLiteral("id")));
}

TEST(ChatController, GoogleParallelCallsPreserveEveryInvocationAndResultInRequestOrder) {
  Fixture fixture;
  auto toolRegistry = std::make_shared<ToolRegistry>();
  auto recordingTool = std::make_shared<RecordingTool>();
  toolRegistry->registerTool(recordingTool);
  ChatController controller(nullptr, nullptr, nullptr, fixture.google_provider,
                            std::make_shared<holonight_providers::SteadyClock>(), toolRegistry);
  Conversation conversation = makeConversation();

  ASSERT_TRUE(controller.send(conversation, testGoogleModel(), QStringLiteral("hello")).has_value());
  fixture.google_http_client->emitData(0, googleToolCallRoundSse({QStringLiteral("call_a"), QStringLiteral("call_b")}));

  EXPECT_EQ(recordingTool->call_count, 2);
  ASSERT_EQ(fixture.google_http_client->streamingCallCount(), 2U);
  const std::vector<Message>& messages = conversation.messages();
  ASSERT_EQ(messages.size(), 7U);
  ASSERT_EQ(messages[2].toolCalls().size(), 1U);
  ASSERT_EQ(messages[3].toolCalls().size(), 1U);
  ASSERT_EQ(messages[4].toolCalls().size(), 1U);
  ASSERT_EQ(messages[5].toolCalls().size(), 1U);
  EXPECT_EQ(messages[2].toolCalls().front().tool_use_id, QStringLiteral("call_a"));
  EXPECT_EQ(messages[3].toolCalls().front().tool_use_id, QStringLiteral("call_b"));
  EXPECT_EQ(messages[4].toolCalls().front().tool_use_id, QStringLiteral("call_a"));
  EXPECT_EQ(messages[5].toolCalls().front().tool_use_id, QStringLiteral("call_b"));
  EXPECT_EQ(messages[6].status(), MessageStatus::Streaming);

  const QJsonObject followUp =
      QJsonDocument::fromJson(fixture.google_http_client->streamingCall(1).request.body).object();
  const QJsonArray contents = followUp.value(QStringLiteral("contents")).toArray();
  ASSERT_GE(contents.size(), 3);
  const QJsonArray invocationParts =
      contents.at(contents.size() - 2).toObject().value(QStringLiteral("parts")).toArray();
  const QJsonArray resultParts = contents.last().toObject().value(QStringLiteral("parts")).toArray();
  ASSERT_EQ(invocationParts.size(), 2);
  ASSERT_EQ(resultParts.size(), 2);
  EXPECT_EQ(
      invocationParts.at(0).toObject().value(QStringLiteral("functionCall")).toObject().value(QStringLiteral("id")),
      QStringLiteral("call_a"));
  EXPECT_EQ(
      invocationParts.at(1).toObject().value(QStringLiteral("functionCall")).toObject().value(QStringLiteral("id")),
      QStringLiteral("call_b"));
  EXPECT_EQ(
      resultParts.at(0).toObject().value(QStringLiteral("functionResponse")).toObject().value(QStringLiteral("id")),
      QStringLiteral("call_a"));
  EXPECT_EQ(
      resultParts.at(1).toObject().value(QStringLiteral("functionResponse")).toObject().value(QStringLiteral("id")),
      QStringLiteral("call_b"));
}

TEST(ChatController, GoogleParallelCallsWaitForAllResultsAndIgnoreCompletionOrder) {
  Fixture fixture;
  auto toolRegistry = std::make_shared<ToolRegistry>();
  auto executor = std::make_shared<DeferredExecutor>();
  toolRegistry->registerTool(deferredToolRegistration(executor));
  ChatController controller(nullptr, nullptr, nullptr, fixture.google_provider,
                            std::make_shared<holonight_providers::SteadyClock>(), toolRegistry);
  Conversation conversation = makeConversation();

  ASSERT_TRUE(controller.send(conversation, testGoogleModel(), QStringLiteral("hello")).has_value());
  fixture.google_http_client->emitData(0, googleToolCallRoundSse({QStringLiteral("call_a"), QStringLiteral("call_b")}));

  ASSERT_EQ(executor->callbacks.size(), 2U);
  EXPECT_EQ(fixture.google_http_client->streamingCallCount(), 1U);
  executor->finish(1);
  EXPECT_EQ(fixture.google_http_client->streamingCallCount(), 1U);
  executor->finish(0);

  ASSERT_EQ(fixture.google_http_client->streamingCallCount(), 2U);
  const std::vector<Message>& messages = conversation.messages();
  ASSERT_EQ(messages.size(), 7U);
  EXPECT_EQ(messages[4].toolCalls().front().tool_use_id, QStringLiteral("call_a"));
  EXPECT_EQ(messages[5].toolCalls().front().tool_use_id, QStringLiteral("call_b"));
}

TEST(ChatController, StopCancelsEveryParallelToolExecution) {
  Fixture fixture;
  auto toolRegistry = std::make_shared<ToolRegistry>();
  auto executor = std::make_shared<DeferredExecutor>();
  toolRegistry->registerTool(deferredToolRegistration(executor));
  ChatController controller(nullptr, nullptr, nullptr, fixture.google_provider,
                            std::make_shared<holonight_providers::SteadyClock>(), toolRegistry);
  Conversation conversation = makeConversation();

  ASSERT_TRUE(controller.send(conversation, testGoogleModel(), QStringLiteral("hello")).has_value());
  fixture.google_http_client->emitData(0, googleToolCallRoundSse({QStringLiteral("call_a"), QStringLiteral("call_b")}));
  ASSERT_EQ(executor->handles.size(), 2U);

  controller.stop(conversation.id());

  EXPECT_TRUE(executor->handles[0]->cancelled);
  EXPECT_TRUE(executor->handles[1]->cancelled);
  EXPECT_FALSE(controller.isStreaming(conversation.id()));
}

TEST(ChatController, ToolCallLoopRejectsCallsBeyondCap) {
  Fixture fixture;
  auto toolRegistry = std::make_shared<ToolRegistry>();
  toolRegistry->registerTool(std::make_shared<RecordingTool>());
  ChatController controller(nullptr, nullptr, fixture.anthropic_provider, nullptr,
                            std::make_shared<holonight_providers::SteadyClock>(), toolRegistry);
  Conversation conversation = makeConversation();

  std::vector<StreamEvent> events;
  ASSERT_TRUE(controller
                  .send(conversation, testAnthropicModel(), QString("hello"),
                        [&events](const StreamEvent& event) { events.push_back(event); })
                  .has_value());

  for (std::size_t round = 0; round < 10; ++round) {
    ASSERT_EQ(fixture.anthropic_http_client->streamingCallCount(), round + 1);
    fixture.anthropic_http_client->emitData(round, toolCallRoundSse(QStringLiteral("toolu_%1").arg(round)));
  }

  // 10 tool calls executed; continueToolLoop dispatched an 11th HTTP round for the model's next move.
  ASSERT_EQ(fixture.anthropic_http_client->streamingCallCount(), 11U);
  EXPECT_TRUE(controller.isStreaming(conversation.id()));

  // The 11th ToolCall event must be rejected without executing the tool or recording a message.
  fixture.anthropic_http_client->emitData(10, toolCallRoundSse(QStringLiteral("toolu_10")));

  ASSERT_FALSE(events.empty());
  ASSERT_TRUE(std::holds_alternative<Error>(events.back()));
  EXPECT_EQ(std::get<Error>(events.back()).message,
            QStringLiteral("Tool-calling limit exceeded (max 10 calls per turn); stopping."));

  const Message& lastMessage = conversation.messages().back();
  EXPECT_EQ(lastMessage.status(), MessageStatus::Error);
  EXPECT_EQ(lastMessage.text(), QStringLiteral("Tool-calling limit exceeded (max 10 calls per turn); stopping."));
  EXPECT_TRUE(lastMessage.toolCalls().empty());  // the 11th call was never recorded (REQ-F-006(4)).

  EXPECT_FALSE(controller.isStreaming(conversation.id()));
  EXPECT_TRUE(fixture.anthropic_http_client->streamingCall(10).cancelled);
}

}  // namespace
}  // namespace holonight_application
