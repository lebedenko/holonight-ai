#include "fake_http_client.h"
#include "holonight_providers/anthropic_provider.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <gtest/gtest.h>
#include <holonight_domain/holonight_domain.h>
#include <memory>
#include <vector>

namespace holonight_providers {
namespace {

using holonight_domain::Cancelled;
using holonight_domain::Completed;
using holonight_domain::ContentDelta;
using holonight_domain::Error;
using holonight_domain::Message;
using holonight_domain::MessageId;
using holonight_domain::MessageRole;
using holonight_domain::MessageStatus;
using holonight_domain::ModelId;
using holonight_domain::StreamEvent;
using holonight_domain::ToolCallEntry;
using holonight_domain::ToolCallKind;
using holonight_domain::ToolRequestEvent;
using holonight_domain::Usage;

std::shared_ptr<FakeHttpClient> makeFakeWithEmptyModels() {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedSuccess(QByteArray(R"({"data":[]})"));
  return fake;
}

ModelId testModel() {
  return ModelId{.provider_id = QStringLiteral("anthropic"),
                 .model_name = QStringLiteral("claude-3-5-sonnet-20241022")};
}

// Serializes a JSON object as one SSE "data:" block, letting tests build tool-call payloads via
// QJsonObject instead of hand-escaping nested-quote raw string literals.
QByteArray sseBlock(const QJsonObject& object) {
  return QByteArray("data: ") + QJsonDocument(object).toJson(QJsonDocument::Compact) + "\n\n";
}

QJsonObject contentBlockStart(int index, const QString& blockType, const QJsonObject& contentBlock) {
  QJsonObject object;
  object[QStringLiteral("type")] = QStringLiteral("content_block_start");
  object[QStringLiteral("index")] = index;
  QJsonObject block = contentBlock;
  block[QStringLiteral("type")] = blockType;
  object[QStringLiteral("content_block")] = block;
  return object;
}

QJsonObject toolUseStart(int index, const QString& toolUseId, const QString& name) {
  return contentBlockStart(index, QStringLiteral("tool_use"),
                           QJsonObject{{QStringLiteral("id"), toolUseId}, {QStringLiteral("name"), name}});
}

QJsonObject inputJsonDelta(int index, const QString& partialJson) {
  QJsonObject object;
  object[QStringLiteral("type")] = QStringLiteral("content_block_delta");
  object[QStringLiteral("index")] = index;
  object[QStringLiteral("delta")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("input_json_delta")},
                                                {QStringLiteral("partial_json"), partialJson}};
  return object;
}

QJsonObject contentBlockStop(int index) {
  QJsonObject object;
  object[QStringLiteral("type")] = QStringLiteral("content_block_stop");
  object[QStringLiteral("index")] = index;
  return object;
}

QJsonObject messageStopEvent() { return QJsonObject{{QStringLiteral("type"), QStringLiteral("message_stop")}}; }

// Model denylist filtering (REQ-F-010/011).
TEST(AnthropicProvider, RefreshFiltersDenylistedModelsAndKeepsAllowedOnes) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedSuccess(QByteArray(R"({"data":[
      {"id":"claude-3-5-sonnet-20241022"},
      {"id":"claude-3-embedding"},
      {"id":"claude-moderation-latest"},
      {"id":"claude-3-vision"},
      {"id":"claude-ocr-preview"},
      {"id":"claude-4-ultra-future"}
  ]})"));

  AnthropicProvider provider(fake);
  provider.refresh();

  ASSERT_EQ(provider.availableModels().size(), 2U);
  EXPECT_EQ(provider.availableModels()[0].provider_id, QStringLiteral("anthropic"));
  EXPECT_EQ(provider.availableModels()[0].model_name, QStringLiteral("claude-3-5-sonnet-20241022"));
  EXPECT_EQ(provider.availableModels()[1].model_name, QStringLiteral("claude-4-ultra-future"));
}

TEST(AnthropicProvider, RefreshUsesConfiguredInstanceId) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedSuccess(QByteArray(R"({"data":[{"id":"claude-3-5-sonnet-20241022"}]})"));
  AnthropicProvider provider(fake, QStringLiteral("https://api.anthropic.com"), QStringLiteral("anthropic-work"));

  provider.refresh();

  ASSERT_EQ(provider.availableModels().size(), 1U);
  EXPECT_EQ(provider.availableModels()[0].provider_id, QStringLiteral("anthropic-work"));
}

TEST(AnthropicProvider, RefreshFailureLeavesModelListEmpty) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedError(QStringLiteral("connection refused"));

  AnthropicProvider provider(fake);
  provider.refresh();

  EXPECT_TRUE(provider.availableModels().empty());
}

TEST(AnthropicProvider, RefreshTwoCallbackOverloadCallsOnSuccessOnHttpSuccess) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedSuccess(QByteArray(R"({"data":[{"id":"claude-3-5-sonnet-20241022"}]})"));
  AnthropicProvider provider(fake);

  bool successCalled = false;
  bool errorCalled = false;
  provider.refresh([&successCalled] { successCalled = true; }, [&errorCalled](const QString&) { errorCalled = true; });

  EXPECT_TRUE(successCalled);
  EXPECT_FALSE(errorCalled);
}

TEST(AnthropicProvider, RefreshTwoCallbackOverloadCallsOnErrorWithReasonOnHttpFailure) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedError(QStringLiteral("connection refused"));
  AnthropicProvider provider(fake);

  bool successCalled = false;
  QString errorReason;
  provider.refresh([&successCalled] { successCalled = true; },
                   [&errorReason](const QString& reason) { errorReason = reason; });

  EXPECT_FALSE(successCalled);
  EXPECT_EQ(errorReason, QStringLiteral("connection refused"));
}

TEST(AnthropicProvider, RefreshRejectsMalformedSuccessAndPreservesCachedModels) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedSuccess(QByteArray(R"({"data":[{"id":"claude-3-5-sonnet-20241022"}]})"));
  fake->enqueueBufferedSuccess(QByteArray(R"({"unexpected":[]})"));
  AnthropicProvider provider(fake);
  provider.refresh();

  bool successCalled = false;
  QString errorReason;
  provider.refresh([&successCalled] { successCalled = true; },
                   [&errorReason](const QString& reason) { errorReason = reason; });

  EXPECT_FALSE(successCalled);
  EXPECT_EQ(errorReason, QStringLiteral("Malformed model list response from Anthropic"));
  ASSERT_EQ(provider.availableModels().size(), 1U);
  EXPECT_EQ(provider.availableModels().front().model_name, QStringLiteral("claude-3-5-sonnet-20241022"));
}

TEST(AnthropicProvider, RefreshExtractsErrorMessageFromJsonBodyInsteadOfSurfacingRawBlob) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedError(
      QStringLiteral(R"({"type":"error","error":{"type":"authentication_error","message":"Invalid x-api-key"}})"));
  AnthropicProvider provider(fake);

  QString errorReason;
  provider.refresh([] {}, [&errorReason](const QString& reason) { errorReason = reason; });

  EXPECT_EQ(errorReason, QStringLiteral("Invalid x-api-key"));
}

TEST(AnthropicProvider, RefreshRequestsExpectedUrlAndHeaders) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedSuccess(QByteArray(R"({"data":[]})"));
  AnthropicProvider provider(fake);

  provider.refresh();

  EXPECT_EQ(fake->lastBufferedRequest().url, QStringLiteral("https://api.anthropic.com/v1/models"));
  EXPECT_EQ(fake->lastBufferedRequest().headers.value(QStringLiteral("anthropic-version")),
            QStringLiteral("2023-06-01"));
  EXPECT_FALSE(fake->lastBufferedRequest().headers.contains(QStringLiteral("x-api-key")));
  EXPECT_EQ(fake->lastBufferedTimeout(), std::chrono::seconds{10});
}

// Request body construction (REQ-F-001-008).
TEST(AnthropicProvider, SendChatConstructsMessagesApiRequestBody) {
  auto fake = makeFakeWithEmptyModels();
  AnthropicProvider provider(fake);
  provider.setTemperature(0.4);
  provider.setMaxOutputTokens(2048);

  const std::vector<Message> history{Message(MessageId::generate(), MessageRole::User, QString("hi"))};
  provider.sendChat(testModel(), history, [](const StreamEvent&) {});

  ASSERT_EQ(fake->streamingCallCount(), 1U);
  EXPECT_EQ(fake->streamingCall(0).request.url, QStringLiteral("https://api.anthropic.com/v1/messages"));

  const QJsonObject body = QJsonDocument::fromJson(fake->streamingCall(0).request.body).object();
  EXPECT_EQ(body.value(QStringLiteral("model")).toString(), QStringLiteral("claude-3-5-sonnet-20241022"));
  EXPECT_TRUE(body.value(QStringLiteral("stream")).toBool());
  EXPECT_DOUBLE_EQ(body.value(QStringLiteral("temperature")).toDouble(), 0.4);
  EXPECT_EQ(body.value(QStringLiteral("max_tokens")).toInt(), 2048);  // REQ-F-007: always present
  ASSERT_TRUE(body.value(QStringLiteral("messages")).isArray());
  const QJsonObject firstEntry = body.value(QStringLiteral("messages")).toArray().at(0).toObject();
  EXPECT_EQ(firstEntry.value(QStringLiteral("role")).toString(), QStringLiteral("user"));
  EXPECT_EQ(firstEntry.value(QStringLiteral("content")).toString(), QStringLiteral("hi"));

  EXPECT_FALSE(body.contains(QStringLiteral("tools")));
  EXPECT_FALSE(body.contains(QStringLiteral("thinking")));
  EXPECT_FALSE(body.contains(QStringLiteral("top_k")));
}

TEST(AnthropicProvider, SendChatDefaultTemperatureAndMaxTokensMatchConfigDefaults) {
  auto fake = makeFakeWithEmptyModels();
  AnthropicProvider provider(fake);

  provider.sendChat(testModel(), {}, [](const StreamEvent&) {});

  const QJsonObject body = QJsonDocument::fromJson(fake->streamingCall(0).request.body).object();
  EXPECT_DOUBLE_EQ(body.value(QStringLiteral("temperature")).toDouble(), 1.0);
  EXPECT_EQ(body.value(QStringLiteral("max_tokens")).toInt(), 1024);
}

TEST(AnthropicProvider, SendChatWithNoSystemMessagesOmitsSystemFieldEntirely) {
  auto fake = makeFakeWithEmptyModels();
  AnthropicProvider provider(fake);

  const std::vector<Message> history{Message(MessageId::generate(), MessageRole::User, QString("hi"))};
  provider.sendChat(testModel(), history, [](const StreamEvent&) {});

  const QJsonObject body = QJsonDocument::fromJson(fake->streamingCall(0).request.body).object();
  EXPECT_FALSE(body.contains(QStringLiteral("system")));
}

TEST(AnthropicProvider, SendChatHoistsSingleSystemMessageToTopLevelFieldAndExcludesItFromMessages) {
  auto fake = makeFakeWithEmptyModels();
  AnthropicProvider provider(fake);

  const std::vector<Message> history{
      Message(MessageId::generate(), MessageRole::System, QString("Be terse.")),
      Message(MessageId::generate(), MessageRole::User, QString("hi")),
  };
  provider.sendChat(testModel(), history, [](const StreamEvent&) {});

  const QJsonObject body = QJsonDocument::fromJson(fake->streamingCall(0).request.body).object();
  EXPECT_EQ(body.value(QStringLiteral("system")).toString(), QStringLiteral("Be terse."));
  ASSERT_TRUE(body.value(QStringLiteral("messages")).isArray());
  EXPECT_EQ(body.value(QStringLiteral("messages")).toArray().size(), 1);
}

TEST(AnthropicProvider, SendChatConcatenatesMultipleSystemMessagesWithBlankLine) {
  auto fake = makeFakeWithEmptyModels();
  AnthropicProvider provider(fake);

  const std::vector<Message> history{
      Message(MessageId::generate(), MessageRole::System, QString("Context A")),
      Message(MessageId::generate(), MessageRole::System, QString("Context B")),
      Message(MessageId::generate(), MessageRole::User, QString("hi")),
  };
  provider.sendChat(testModel(), history, [](const StreamEvent&) {});

  const QJsonObject body = QJsonDocument::fromJson(fake->streamingCall(0).request.body).object();
  EXPECT_EQ(body.value(QStringLiteral("system")).toString(), QStringLiteral("Context A\n\nContext B"));
  ASSERT_TRUE(body.value(QStringLiteral("messages")).isArray());
  EXPECT_EQ(body.value(QStringLiteral("messages")).toArray().size(), 1);
}

TEST(AnthropicProvider, EmptyAuthKeyOmitsApiKeyHeaderButKeepsAnthropicVersion) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedSuccess(QByteArray(R"({"data":[]})"));
  AnthropicProvider provider(fake);

  provider.refresh();

  EXPECT_FALSE(fake->lastBufferedRequest().headers.contains(QStringLiteral("x-api-key")));
  EXPECT_EQ(fake->lastBufferedRequest().headers.value(QStringLiteral("anthropic-version")),
            QStringLiteral("2023-06-01"));
}

TEST(AnthropicProvider, SetAuthKeyAddsApiKeyHeaderToSendChat) {
  auto fake = makeFakeWithEmptyModels();
  AnthropicProvider provider(fake);
  provider.setAuthKey(QStringLiteral("sk-ant-secret"));

  provider.sendChat(testModel(), {}, [](const StreamEvent&) {});

  ASSERT_EQ(fake->streamingCallCount(), 1U);
  EXPECT_EQ(fake->streamingCall(0).request.headers.value(QStringLiteral("x-api-key")), QStringLiteral("sk-ant-secret"));
  EXPECT_EQ(fake->streamingCall(0).request.headers.value(QStringLiteral("anthropic-version")),
            QStringLiteral("2023-06-01"));
}

TEST(AnthropicProvider, SetBaseUrlAffectsOnlySubsequentCalls) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedSuccess(QByteArray(R"({"data":[]})"));
  fake->setBufferedResponsesDeferred(true);
  AnthropicProvider provider(fake);

  provider.refresh();
  ASSERT_EQ(fake->lastBufferedRequest().url, QStringLiteral("https://api.anthropic.com/v1/models"));

  provider.setBaseUrl(QStringLiteral("https://proxy.example.test"));
  fake->enqueueBufferedSuccess(QByteArray(R"({"data":[]})"));
  provider.refresh();

  EXPECT_EQ(fake->lastBufferedRequest().url, QStringLiteral("https://proxy.example.test/v1/models"));
}

// SSE parsing with fragmented byte chunks (REQ-F-013).
TEST(AnthropicProvider, SendChatReassemblesEventsSplitAcrossChunks) {
  auto fake = makeFakeWithEmptyModels();
  AnthropicProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  const QByteArray block =
      QByteArray(R"(data: {"type":"content_block_delta","delta":{"type":"text_delta","text":"Hello"}})") + "\n\n";
  fake->emitData(0, block.left(15));
  fake->emitData(0, block.mid(15));

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<ContentDelta>(events[0]));
  EXPECT_EQ(std::get<ContentDelta>(events[0]).text, QStringLiteral("Hello"));
}

TEST(AnthropicProvider, SendChatReassemblesBlankLineBoundarySplitAcrossChunks) {
  auto fake = makeFakeWithEmptyModels();
  AnthropicProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  const QByteArray block =
      QByteArray(R"(data: {"type":"content_block_delta","delta":{"type":"text_delta","text":"Hi"}})") + "\n\n";
  fake->emitData(0, block.left(block.size() - 1));
  fake->emitData(0, block.right(1));

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<ContentDelta>(events[0]));
  EXPECT_EQ(std::get<ContentDelta>(events[0]).text, QStringLiteral("Hi"));
}

TEST(AnthropicProvider, SendChatParsesFiveBlocksAcrossFragmentedChunksInOrder) {
  auto fake = makeFakeWithEmptyModels();
  AnthropicProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  const QByteArray payload =
      QByteArray(R"(data: {"type":"message_start","message":{}})") + "\n\n" + QByteArray(R"(data: {"type":"ping"})") +
      "\n\n" + QByteArray(R"(data: {"type":"content_block_delta","delta":{"type":"text_delta","text":"Hi"}})") +
      "\n\n" + QByteArray(R"(data: {"type":"content_block_delta","delta":{"type":"text_delta","text":" there"}})") +
      "\n\n" + QByteArray(R"(data: {"type":"message_stop"})") + "\n\n";
  // Split mid-block to exercise the fragmentation path across all five blocks.
  fake->emitData(0, payload.left(40));
  fake->emitData(0, payload.mid(40));

  ASSERT_EQ(events.size(), 3U);
  EXPECT_TRUE(std::holds_alternative<ContentDelta>(events[0]));
  EXPECT_EQ(std::get<ContentDelta>(events[0]).text, QStringLiteral("Hi"));
  EXPECT_TRUE(std::holds_alternative<ContentDelta>(events[1]));
  EXPECT_EQ(std::get<ContentDelta>(events[1]).text, QStringLiteral(" there"));
  EXPECT_TRUE(std::holds_alternative<Completed>(events[2]));
}

TEST(AnthropicProvider, SendChatParsesCrLfDelimitedBlocks) {
  auto fake = makeFakeWithEmptyModels();
  AnthropicProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  const QByteArray payload =
      QByteArray(R"(data: {"type":"content_block_delta","delta":{"type":"text_delta","text":"Hi"}})") + "\r\n\r\n" +
      QByteArray(R"(data: {"type":"message_stop"})") + "\r\n\r\n";
  fake->emitData(0, payload);

  ASSERT_EQ(events.size(), 2U);
  ASSERT_TRUE(std::holds_alternative<ContentDelta>(events[0]));
  EXPECT_EQ(std::get<ContentDelta>(events[0]).text, QStringLiteral("Hi"));
  EXPECT_TRUE(std::holds_alternative<Completed>(events[1]));
}

// SSE event routing and type mapping (REQ-F-014-019).
TEST(AnthropicProvider, SendChatIgnoresPingMessageStartMessageDeltaAndContentBlockLifecycleEvents) {
  auto fake = makeFakeWithEmptyModels();
  AnthropicProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  const QByteArray payload =
      QByteArray(R"(data: {"type":"message_start","message":{}})") + "\n\n" +
      QByteArray(R"(data: {"type":"content_block_start","index":0,"content_block":{"type":"text","text":""}})") +
      "\n\n" + QByteArray(R"(data: {"type":"ping"})") + "\n\n" +
      QByteArray(R"(data: {"type":"content_block_delta","delta":{"type":"text_delta","text":"Hi"}})") + "\n\n" +
      QByteArray(R"(data: {"type":"content_block_stop","index":0})") + "\n\n" +
      QByteArray(R"(data: {"type":"message_delta","delta":{"stop_reason":"end_turn"},"usage":{"output_tokens":5}})") +
      "\n\n";
  fake->emitData(0, payload);

  ASSERT_EQ(events.size(), 1U);
  EXPECT_TRUE(std::holds_alternative<ContentDelta>(events[0]));
}

TEST(AnthropicProvider, SendChatReportsMaxTokensStopReasonAsTruncation) {
  auto fake = makeFakeWithEmptyModels();
  AnthropicProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  const QByteArray payload =
      QByteArray(R"(data: {"type":"content_block_delta","delta":{"type":"text_delta","text":"partial"}})") + "\n\n" +
      QByteArray(
          R"(data: {"type":"message_delta","delta":{"stop_reason":"max_tokens"},"usage":{"output_tokens":1024}})") +
      "\n\n" + QByteArray(R"(data: {"type":"message_stop"})") + "\n\n";
  fake->emitData(0, payload);

  ASSERT_EQ(events.size(), 2U);
  EXPECT_TRUE(std::holds_alternative<ContentDelta>(events[0]));
  ASSERT_TRUE(std::holds_alternative<Error>(events[1]));
  EXPECT_EQ(std::get<Error>(events[1]).message,
            QStringLiteral("Anthropic response was truncated after reaching the maximum output token limit"));
}

TEST(AnthropicProvider, SendChatIgnoresNonTextDeltaTypesWithinContentBlockDelta) {
  auto fake = makeFakeWithEmptyModels();
  AnthropicProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(
      0, QByteArray(R"(data: {"type":"content_block_delta","delta":{"type":"input_json_delta","partial_json":"{}"}})") +
             "\n\n");

  EXPECT_TRUE(events.empty());
}

TEST(AnthropicProvider, SendChatRejectsCompletionWithoutTextContent) {
  auto fake = makeFakeWithEmptyModels();
  AnthropicProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  const QByteArray payload =
      QByteArray(R"(data: {"type":"content_block_delta","delta":{"type":"input_json_delta","partial_json":"{}"}})") +
      "\n\n" + QByteArray(R"(data: {"type":"message_stop"})") + "\n\n";
  fake->emitData(0, payload);

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<Error>(events[0]));
  EXPECT_EQ(std::get<Error>(events[0]).message, QStringLiteral("Anthropic completed without returning text content"));
}

// Tool-use SSE parsing and request-body wiring (REQ-F-008, REQ-NF-001).
TEST(AnthropicProvider, SendChatIncludesToolsArrayInRequestBodyWhenProvided) {
  auto fake = makeFakeWithEmptyModels();
  AnthropicProvider provider(fake);

  const holonight_domain::ToolCatalogSnapshot tools{
      .client_tools = {holonight_domain::ToolDefinition{
          .id = QStringLiteral("filesystem.list"),
          .function_name = QStringLiteral("ListFiles"),
          .description = QStringLiteral("Lists directory entries."),
          .input_schema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}},
      }}};

  provider.sendChat(testModel(), {}, [](const StreamEvent&) {}, std::chrono::seconds{30}, tools);

  const QJsonObject body = QJsonDocument::fromJson(fake->streamingCall(0).request.body).object();
  ASSERT_TRUE(body.contains(QStringLiteral("tools")));
  const QJsonArray bodyTools = body.value(QStringLiteral("tools")).toArray();
  ASSERT_EQ(bodyTools.size(), 1);
  EXPECT_EQ(bodyTools.at(0).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("ListFiles"));
}

TEST(AnthropicProvider, SendChatEmitsToolCallAfterAccumulatingInputJsonDeltaFragments) {
  auto fake = makeFakeWithEmptyModels();
  AnthropicProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0, sseBlock(toolUseStart(0, QStringLiteral("toolu_01"), QStringLiteral("ListFiles"))) +
                        sseBlock(inputJsonDelta(0, QStringLiteral(R"({"path":")"))) +
                        sseBlock(inputJsonDelta(0, QStringLiteral(R"(~/Documents"})"))) +
                        sseBlock(contentBlockStop(0)) + sseBlock(messageStopEvent()));

  ASSERT_EQ(events.size(), 2U);
  ASSERT_TRUE(std::holds_alternative<ToolRequestEvent>(events[0]));
  const ToolRequestEvent& toolCall = std::get<ToolRequestEvent>(events[0]);
  EXPECT_EQ(toolCall.provider_call_id, QStringLiteral("toolu_01"));
  EXPECT_EQ(toolCall.provider_instance_id, QStringLiteral("anthropic"));
  EXPECT_EQ(toolCall.function_name, QStringLiteral("ListFiles"));
  EXPECT_EQ(toolCall.arguments.value(QStringLiteral("path")).toString(), QStringLiteral("~/Documents"));
  EXPECT_EQ(toolCall.execution_location, holonight_domain::ToolExecutionLocation::LocalClient);
  EXPECT_TRUE(std::holds_alternative<Completed>(events[1]));  // tool-only turn: no text, still completes.
}

TEST(AnthropicProvider, SendChatDefaultsToolInputToEmptyObjectWhenNoInputJsonDeltaArrives) {
  auto fake = makeFakeWithEmptyModels();
  AnthropicProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0, sseBlock(toolUseStart(0, QStringLiteral("toolu_02"), QStringLiteral("ListFiles"))) +
                        sseBlock(contentBlockStop(0)) + sseBlock(messageStopEvent()));

  ASSERT_EQ(events.size(), 2U);
  ASSERT_TRUE(std::holds_alternative<ToolRequestEvent>(events[0]));
  EXPECT_TRUE(std::get<ToolRequestEvent>(events[0]).arguments.isEmpty());
}

TEST(AnthropicProvider, ServerToolUseIsNormalizedAsProviderHostedWithExactCorrelationId) {
  auto fake = makeFakeWithEmptyModels();
  AnthropicProvider provider(fake, QStringLiteral("https://api.anthropic.com"), QStringLiteral("work-anthropic"));

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0, sseBlock(contentBlockStart(0, QStringLiteral("server_tool_use"),
                                               QJsonObject{{QStringLiteral("id"), QStringLiteral("srvtoolu_exact_01")},
                                                           {QStringLiteral("name"), QStringLiteral("web_search")}})) +
                        sseBlock(inputJsonDelta(0, QStringLiteral(R"({"query":"Qt"})"))) +
                        sseBlock(contentBlockStop(0)) + sseBlock(messageStopEvent()));

  ASSERT_EQ(events.size(), 2U);
  ASSERT_TRUE(std::holds_alternative<ToolRequestEvent>(events[0]));
  const auto& request = std::get<ToolRequestEvent>(events[0]);
  EXPECT_EQ(request.provider_call_id, QStringLiteral("srvtoolu_exact_01"));
  EXPECT_EQ(request.provider_instance_id, QStringLiteral("work-anthropic"));
  EXPECT_EQ(request.execution_location, holonight_domain::ToolExecutionLocation::ProviderHosted);
  EXPECT_EQ(request.source, holonight_domain::ToolSource::Provider);
}

TEST(AnthropicProvider, SendChatFailsStreamOnMalformedAccumulatedToolInput) {
  auto fake = makeFakeWithEmptyModels();
  AnthropicProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0, sseBlock(toolUseStart(0, QStringLiteral("toolu_03"), QStringLiteral("ListFiles"))) +
                        sseBlock(inputJsonDelta(0, QStringLiteral("{not valid json"))) + sseBlock(contentBlockStop(0)));

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<Error>(events[0]));
  EXPECT_TRUE(std::get<Error>(events[0]).message.contains(QStringLiteral("Malformed tool input")));
}

TEST(AnthropicProvider, SendChatReconstructsToolUseAndToolResultAsSeparateMessagesAcrossTurns) {
  auto fake = makeFakeWithEmptyModels();
  AnthropicProvider provider(fake);

  QJsonObject input;
  input[QStringLiteral("path")] = QStringLiteral("~/Documents");
  Message invocation(MessageId::generate(), MessageRole::Assistant, QString());
  invocation.setToolCalls({ToolCallEntry{.kind = ToolCallKind::Invocation,
                                         .tool_use_id = QStringLiteral("toolu_01"),
                                         .tool_name = QStringLiteral("ListFiles"),
                                         .input = input}});

  QJsonObject result;
  result[QStringLiteral("entries")] = QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("notes.txt")},
                                                             {QStringLiteral("type"), QStringLiteral("file")}}};
  Message toolResult(MessageId::generate(), MessageRole::User, QString());
  toolResult.setToolCalls(
      {ToolCallEntry{.kind = ToolCallKind::Result, .tool_use_id = QStringLiteral("toolu_01"), .result = result}});

  const std::vector<Message> history{
      Message(MessageId::generate(), MessageRole::User, QString("List my documents")),
      invocation,
      toolResult,
  };
  provider.sendChat(testModel(), history, [](const StreamEvent&) {});

  const QJsonObject body = QJsonDocument::fromJson(fake->streamingCall(0).request.body).object();
  const QJsonArray messages = body.value(QStringLiteral("messages")).toArray();
  ASSERT_EQ(messages.size(), 3);

  EXPECT_EQ(messages.at(0).toObject().value(QStringLiteral("content")).toString(), QStringLiteral("List my documents"));

  const QJsonObject assistantEntry = messages.at(1).toObject();
  EXPECT_EQ(assistantEntry.value(QStringLiteral("role")).toString(), QStringLiteral("assistant"));
  ASSERT_TRUE(assistantEntry.value(QStringLiteral("content")).isArray());
  const QJsonObject toolUseBlock = assistantEntry.value(QStringLiteral("content")).toArray().at(0).toObject();
  EXPECT_EQ(toolUseBlock.value(QStringLiteral("type")).toString(), QStringLiteral("tool_use"));
  EXPECT_EQ(toolUseBlock.value(QStringLiteral("id")).toString(), QStringLiteral("toolu_01"));
  EXPECT_EQ(toolUseBlock.value(QStringLiteral("name")).toString(), QStringLiteral("ListFiles"));
  EXPECT_EQ(toolUseBlock.value(QStringLiteral("input")).toObject().value(QStringLiteral("path")).toString(),
            QStringLiteral("~/Documents"));

  const QJsonObject resultEntry = messages.at(2).toObject();
  EXPECT_EQ(resultEntry.value(QStringLiteral("role")).toString(), QStringLiteral("user"));
  ASSERT_TRUE(resultEntry.value(QStringLiteral("content")).isArray());
  const QJsonObject toolResultBlock = resultEntry.value(QStringLiteral("content")).toArray().at(0).toObject();
  EXPECT_EQ(toolResultBlock.value(QStringLiteral("type")).toString(), QStringLiteral("tool_result"));
  EXPECT_EQ(toolResultBlock.value(QStringLiteral("tool_use_id")).toString(), QStringLiteral("toolu_01"));
  EXPECT_FALSE(toolResultBlock.value(QStringLiteral("is_error")).toBool());  // required field, present even when false
  const QJsonDocument resultContent =
      QJsonDocument::fromJson(toolResultBlock.value(QStringLiteral("content")).toString().toUtf8());
  EXPECT_TRUE(resultContent.object().contains(QStringLiteral("entries")));
}

TEST(AnthropicProvider, SendChatGroupsConsecutiveSameRoleMessagesIntoOneApiMessageWithArrayContent) {
  auto fake = makeFakeWithEmptyModels();
  AnthropicProvider provider(fake);

  Message invocation(MessageId::generate(), MessageRole::Assistant, QString());
  invocation.setToolCalls({ToolCallEntry{.kind = ToolCallKind::Invocation,
                                         .tool_use_id = QStringLiteral("toolu_09"),
                                         .tool_name = QStringLiteral("ListFiles"),
                                         .input = QJsonObject{{QStringLiteral("path"), QStringLiteral("~")}}}});

  const std::vector<Message> history{
      Message(MessageId::generate(), MessageRole::Assistant, QString("Let me check that.")),
      invocation,
  };
  provider.sendChat(testModel(), history, [](const StreamEvent&) {});

  const QJsonObject body = QJsonDocument::fromJson(fake->streamingCall(0).request.body).object();
  const QJsonArray messages = body.value(QStringLiteral("messages")).toArray();
  ASSERT_EQ(messages.size(), 1);

  const QJsonObject entry = messages.at(0).toObject();
  EXPECT_EQ(entry.value(QStringLiteral("role")).toString(), QStringLiteral("assistant"));
  ASSERT_TRUE(entry.value(QStringLiteral("content")).isArray());
  const QJsonArray blocks = entry.value(QStringLiteral("content")).toArray();
  ASSERT_EQ(blocks.size(), 2);
  EXPECT_EQ(blocks.at(0).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("text"));
  EXPECT_EQ(blocks.at(0).toObject().value(QStringLiteral("text")).toString(), QStringLiteral("Let me check that."));
  EXPECT_EQ(blocks.at(1).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("tool_use"));
  EXPECT_EQ(blocks.at(1).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("toolu_09"));
}

// Regression: when the model calls a tool with no leading prose, ChatController finalizes the
// working Message to Complete with empty text before appending the Invocation Message right after
// it (chat_controller.cpp's handleToolCall()) -- both are Assistant-role and get merged into one
// API message. Anthropic rejects requests containing an empty text content block ("messages: text
// content blocks must be non-empty"), so the empty-text Message must be dropped entirely rather
// than merged in as `{"type":"text","text":""}`.
TEST(AnthropicProvider, SendChatOmitsEmptyTextMessageAdjacentToToolUse) {
  auto fake = makeFakeWithEmptyModels();
  AnthropicProvider provider(fake);

  Message invocation(MessageId::generate(), MessageRole::Assistant, QString());
  invocation.setToolCalls({ToolCallEntry{.kind = ToolCallKind::Invocation,
                                         .tool_use_id = QStringLiteral("toolu_10"),
                                         .tool_name = QStringLiteral("ListFiles"),
                                         .input = QJsonObject{{QStringLiteral("path"), QStringLiteral("~")}}}});

  const std::vector<Message> history{
      Message(MessageId::generate(), MessageRole::User, QString("List my Pictures")),
      Message(MessageId::generate(), MessageRole::Assistant, QString(), MessageStatus::Complete),
      invocation,
  };
  provider.sendChat(testModel(), history, [](const StreamEvent&) {});

  const QJsonObject body = QJsonDocument::fromJson(fake->streamingCall(0).request.body).object();
  const QJsonArray messages = body.value(QStringLiteral("messages")).toArray();
  ASSERT_EQ(messages.size(), 2);

  const QJsonObject assistantEntry = messages.at(1).toObject();
  EXPECT_EQ(assistantEntry.value(QStringLiteral("role")).toString(), QStringLiteral("assistant"));
  ASSERT_TRUE(assistantEntry.value(QStringLiteral("content")).isArray());
  const QJsonArray blocks = assistantEntry.value(QStringLiteral("content")).toArray();
  ASSERT_EQ(blocks.size(), 1);
  EXPECT_EQ(blocks.at(0).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("tool_use"));
}

// Usage accumulation across message_start/message_delta (REQ-F-005, REQ-F-002).
TEST(AnthropicProvider, MessageDeltaOverwritesNotSums) {
  auto fake = makeFakeWithEmptyModels();
  AnthropicProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  const QByteArray payload =
      QByteArray(R"(data: {"type":"message_start","message":{"usage":{"input_tokens":40,"output_tokens":1}}})") +
      "\n\n" + QByteArray(R"(data: {"type":"content_block_delta","delta":{"type":"text_delta","text":"Hi"}})") +
      "\n\n" +
      QByteArray(
          R"(data: {"type":"message_delta","delta":{"stop_reason":"end_turn"},"usage":{"output_tokens":20,"cache_read_input_tokens":5}})") +
      "\n\n" + QByteArray(R"(data: {"type":"message_stop"})") + "\n\n";
  fake->emitData(0, payload);

  ASSERT_EQ(events.size(), 2U);
  ASSERT_TRUE(std::holds_alternative<ContentDelta>(events[0]));
  ASSERT_TRUE(std::holds_alternative<Completed>(events[1]));
  const Completed& completed = std::get<Completed>(events[1]);
  ASSERT_TRUE(completed.usage.has_value());
  const Usage& usage = *completed.usage;
  EXPECT_EQ(usage.input_tokens, 40);
  EXPECT_EQ(usage.output_tokens, 20);  // message_delta's 20 replaces message_start's 1, not 1+20.
  EXPECT_EQ(usage.cache_read_tokens, 5);
  EXPECT_EQ(usage.total_tokens, 60);
}

TEST(AnthropicProvider, SendChatLeavesReasoningAndCacheCreationTokensNullWhenNotReportedByAnthropic) {
  auto fake = makeFakeWithEmptyModels();
  AnthropicProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  const QByteArray payload =
      QByteArray(R"(data: {"type":"message_start","message":{"usage":{"input_tokens":40,"output_tokens":1}}})") +
      "\n\n" + QByteArray(R"(data: {"type":"content_block_delta","delta":{"type":"text_delta","text":"Hi"}})") +
      "\n\n" +
      QByteArray(R"(data: {"type":"message_delta","delta":{"stop_reason":"end_turn"},"usage":{"output_tokens":2}})") +
      "\n\n" + QByteArray(R"(data: {"type":"message_stop"})") + "\n\n";
  fake->emitData(0, payload);

  ASSERT_EQ(events.size(), 2U);
  ASSERT_TRUE(std::holds_alternative<Completed>(events[1]));
  const Completed& completed = std::get<Completed>(events[1]);
  ASSERT_TRUE(completed.usage.has_value());
  EXPECT_FALSE(completed.usage->reasoning_tokens.has_value());
  EXPECT_FALSE(completed.usage->cache_creation_tokens.has_value());
  EXPECT_FALSE(completed.usage->cache_read_tokens.has_value());
}

// Error handling (REQ-F-018, REQ-F-020-023).
TEST(AnthropicProvider, SendChatTreatsMalformedJsonAsFatalError) {
  auto fake = makeFakeWithEmptyModels();
  AnthropicProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0, QByteArray("data: not-json") + "\n\n");

  ASSERT_EQ(events.size(), 1U);
  EXPECT_TRUE(std::holds_alternative<Error>(events[0]));
}

TEST(AnthropicProvider, SendChatRoutesInStreamErrorEventToError) {
  auto fake = makeFakeWithEmptyModels();
  AnthropicProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(
      0, QByteArray(R"(data: {"type":"error","error":{"type":"overloaded_error","message":"Overloaded"}})") + "\n\n");

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<Error>(events[0]));
  EXPECT_EQ(std::get<Error>(events[0]).message, QStringLiteral("Overloaded"));
}

TEST(AnthropicProvider, SendChatTreatsStreamEndBeforeMessageStopAsError) {
  auto fake = makeFakeWithEmptyModels();
  AnthropicProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitFinished(0);

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<Error>(events[0]));
  EXPECT_EQ(std::get<Error>(events[0]).message, QStringLiteral("Stream ended without completion"));
}

TEST(AnthropicProvider, SendChatRecoversErrorMessageFromBufferedJsonBodyOnHttpError) {
  auto fake = makeFakeWithEmptyModels();
  AnthropicProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(
      0, QByteArray(R"({"type":"error","error":{"type":"authentication_error","message":"Invalid x-api-key"}})"));
  fake->emitError(0, QStringLiteral("Request failed with HTTP status 401"));

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<Error>(events[0]));
  EXPECT_EQ(std::get<Error>(events[0]).message, QStringLiteral("Invalid x-api-key"));
}

TEST(AnthropicProvider, SendChatFallsBackToTransportMessageWhenBufferIsNotJson) {
  auto fake = makeFakeWithEmptyModels();
  AnthropicProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitError(0, QStringLiteral("Connection refused"));

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<Error>(events[0]));
  EXPECT_EQ(std::get<Error>(events[0]).message, QStringLiteral("Connection refused"));
}

// Cancellation via HttpRequestHandle (REQ-F-024/025).
TEST(AnthropicProvider, CancellingHandleStopsFurtherDeltaProcessing) {
  auto fake = makeFakeWithEmptyModels();
  AnthropicProvider provider(fake);

  std::vector<StreamEvent> events;
  const HttpRequestHandlePtr handle =
      provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(
      0, QByteArray(R"(data: {"type":"content_block_delta","delta":{"type":"text_delta","text":"Hi"}})") + "\n\n");
  ASSERT_EQ(events.size(), 1U);

  handle->cancel();
  EXPECT_TRUE(fake->streamingCall(0).cancelled);
}

TEST(AnthropicProvider, CancellingHandleCarriesPartialUsageAndModelIdentifier) {
  auto fake = makeFakeWithEmptyModels();
  AnthropicProvider provider(fake);

  std::vector<StreamEvent> events;
  const HttpRequestHandlePtr handle =
      provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });
  fake->emitData(
      0,
      QByteArray(
          R"(data: {"type":"message_start","message":{"model":"claude-exact-version","usage":{"input_tokens":40,"output_tokens":1}}})") +
          "\n\n");
  fake->emitData(
      0,
      QByteArray(
          R"(data: {"type":"message_delta","delta":{"stop_reason":"end_turn"},"usage":{"output_tokens":20,"cache_read_input_tokens":5}})") +
          "\n\n");

  handle->cancel();

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<Cancelled>(events[0]));
  const auto& cancelled = std::get<Cancelled>(events[0]);
  EXPECT_EQ(cancelled.model_identifier, QStringLiteral("claude-exact-version"));
  ASSERT_TRUE(cancelled.usage.has_value());
  EXPECT_EQ(cancelled.usage->input_tokens, 40);
  EXPECT_EQ(cancelled.usage->output_tokens, 20);
  EXPECT_EQ(cancelled.usage->cache_read_tokens, 5);
}

TEST(AnthropicProvider, SendChatCancelsRequestAfterMalformedJson) {
  auto fake = makeFakeWithEmptyModels();
  AnthropicProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0, QByteArray("data: not-json") + "\n\n");

  EXPECT_TRUE(fake->streamingCall(0).cancelled);
  ASSERT_EQ(events.size(), 1U);
  EXPECT_TRUE(std::holds_alternative<Error>(events[0]));
}

}  // namespace
}  // namespace holonight_providers
