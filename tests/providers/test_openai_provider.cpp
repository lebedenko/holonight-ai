#include "fake_http_client.h"
#include "holonight_providers/openai_provider.h"

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

using holonight_domain::Completed;
using holonight_domain::ContentDelta;
using holonight_domain::Error;
using holonight_domain::Message;
using holonight_domain::MessageId;
using holonight_domain::MessageRole;
using holonight_domain::ModelId;
using holonight_domain::StreamEvent;
using holonight_domain::ToolRequestEvent;
using holonight_domain::Usage;

std::shared_ptr<FakeHttpClient> makeFakeWithEmptyModels() {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedSuccess(QByteArray(R"({"data":[]})"));
  return fake;
}

// T-023: model denylist filtering.
TEST(OpenAIProvider, RefreshFiltersDenylistedModelsAndKeepsAllowedOnes) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedSuccess(QByteArray(R"({"data":[
      {"id":"gpt-4o"},
      {"id":"text-embedding-3-small"},
      {"id":"whisper-1"},
      {"id":"dall-e-3"},
      {"id":"gpt-image-1"},
      {"id":"omni-moderation-latest"},
      {"id":"text-moderation-latest"},
      {"id":"davinci-002"},
      {"id":"babbage-002"},
      {"id":"sora-1"},
      {"id":"tts-1"},
      {"id":"gpt-6-hypothetical-future-model"}
  ]})"));

  OpenAIProvider provider(fake);
  provider.refresh();

  ASSERT_EQ(provider.availableModels().size(), 2U);
  EXPECT_EQ(provider.availableModels()[0].provider_id, QStringLiteral("openai"));
  EXPECT_EQ(provider.availableModels()[0].model_name, QStringLiteral("gpt-4o"));
  EXPECT_EQ(provider.availableModels()[1].model_name, QStringLiteral("gpt-6-hypothetical-future-model"));
}

TEST(OpenAIProvider, RefreshUsesConfiguredInstanceId) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedSuccess(QByteArray(R"({"data":[{"id":"gpt-4o"}]})"));
  OpenAIProvider provider(fake, QStringLiteral("https://api.openai.com/v1"), QStringLiteral("openai-work"));

  provider.refresh();

  ASSERT_EQ(provider.availableModels().size(), 1U);
  EXPECT_EQ(provider.availableModels()[0].provider_id, QStringLiteral("openai-work"));
}

TEST(OpenAIProvider, RefreshFailureLeavesModelListEmpty) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedError(QStringLiteral("connection refused"));

  OpenAIProvider provider(fake);
  provider.refresh();

  EXPECT_TRUE(provider.availableModels().empty());
}

TEST(OpenAIProvider, RefreshTwoCallbackOverloadCallsOnSuccessOnHttpSuccess) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedSuccess(QByteArray(R"({"data":[{"id":"gpt-4o"}]})"));
  OpenAIProvider provider(fake);

  bool successCalled = false;
  bool errorCalled = false;
  provider.refresh([&successCalled] { successCalled = true; }, [&errorCalled](const QString&) { errorCalled = true; });

  EXPECT_TRUE(successCalled);
  EXPECT_FALSE(errorCalled);
}

TEST(OpenAIProvider, RefreshTwoCallbackOverloadCallsOnErrorWithReasonOnHttpFailure) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedError(QStringLiteral("connection refused"));
  OpenAIProvider provider(fake);

  bool successCalled = false;
  QString errorReason;
  provider.refresh([&successCalled] { successCalled = true; },
                   [&errorReason](const QString& reason) { errorReason = reason; });

  EXPECT_FALSE(successCalled);
  EXPECT_EQ(errorReason, QStringLiteral("connection refused"));
}

TEST(OpenAIProvider, RefreshRejectsMalformedSuccessAndPreservesCachedModels) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedSuccess(QByteArray(R"({"data":[{"id":"gpt-4o"}]})"));
  fake->enqueueBufferedSuccess(QByteArray(R"({"unexpected":[]})"));
  OpenAIProvider provider(fake);
  provider.refresh();

  bool successCalled = false;
  QString errorReason;
  provider.refresh([&successCalled] { successCalled = true; },
                   [&errorReason](const QString& reason) { errorReason = reason; });

  EXPECT_FALSE(successCalled);
  EXPECT_EQ(errorReason, QStringLiteral("Malformed model list response from OpenAI"));
  ASSERT_EQ(provider.availableModels().size(), 1U);
  EXPECT_EQ(provider.availableModels().front().model_name, QStringLiteral("gpt-4o"));
}

// Regression test: the HTTP client's error path (qt_network_http_client.cpp) surfaces the raw
// response body as the error string, and OpenAI's error responses are always JSON-shaped
// ({"error":{"message":...}}). Before this fix, fetchModelList()'s on_error forwarded that raw
// JSON blob verbatim, so the Settings UI rendered the entire escaped JSON structure instead of a
// readable message. It must extract just error.message, matching sendChat()'s existing behavior.
TEST(OpenAIProvider, RefreshExtractsErrorMessageFromJsonBodyInsteadOfSurfacingRawBlob) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedError(
      QStringLiteral(R"({"error":{"message":"Incorrect API key provided: sk-***.","type":"invalid_request_error"}})"));
  OpenAIProvider provider(fake);

  QString errorReason;
  provider.refresh([] {}, [&errorReason](const QString& reason) { errorReason = reason; });

  EXPECT_EQ(errorReason, QStringLiteral("Incorrect API key provided: sk-***."));
}

TEST(OpenAIProvider, RefreshUsesBoundedTimeout) {
  auto fake = makeFakeWithEmptyModels();
  OpenAIProvider provider(fake);

  provider.refresh();

  EXPECT_EQ(fake->lastBufferedTimeout(), std::chrono::seconds{10});
}

// Request body construction (REQ-F-001-007).
TEST(OpenAIProvider, SendChatConstructsResponsesApiRequestBody) {
  auto fake = makeFakeWithEmptyModels();
  OpenAIProvider provider(fake);
  provider.setTemperature(0.4);

  const ModelId model{.provider_id = QStringLiteral("openai"), .model_name = QStringLiteral("gpt-4o")};
  const std::vector<Message> history{Message(MessageId::generate(), MessageRole::User, QString("hi"))};
  provider.sendChat(model, history, [](const StreamEvent&) {});

  ASSERT_EQ(fake->streamingCallCount(), 1U);
  EXPECT_EQ(fake->streamingCall(0).request.url, QStringLiteral("https://api.openai.com/v1/responses"));

  const QJsonObject body = QJsonDocument::fromJson(fake->streamingCall(0).request.body).object();
  EXPECT_EQ(body.value(QStringLiteral("model")).toString(), QStringLiteral("gpt-4o"));
  EXPECT_TRUE(body.value(QStringLiteral("stream")).toBool());
  EXPECT_FALSE(body.value(QStringLiteral("store")).toBool(true));
  EXPECT_DOUBLE_EQ(body.value(QStringLiteral("temperature")).toDouble(), 0.4);
  ASSERT_TRUE(body.value(QStringLiteral("input")).isArray());
  const QJsonObject firstEntry = body.value(QStringLiteral("input")).toArray().at(0).toObject();
  EXPECT_EQ(firstEntry.value(QStringLiteral("role")).toString(), QStringLiteral("user"));
  EXPECT_EQ(firstEntry.value(QStringLiteral("content")).toString(), QStringLiteral("hi"));

  EXPECT_FALSE(body.contains(QStringLiteral("previous_response_id")));
  EXPECT_FALSE(body.contains(QStringLiteral("max_output_tokens")));
  EXPECT_FALSE(body.contains(QStringLiteral("reasoning_effort")));
}

TEST(OpenAIProvider, SendChatDefaultTemperatureMatchesConfigDefault) {
  auto fake = makeFakeWithEmptyModels();
  OpenAIProvider provider(fake);

  const ModelId model{.provider_id = QStringLiteral("openai"), .model_name = QStringLiteral("gpt-4o")};
  provider.sendChat(model, {}, [](const StreamEvent&) {});

  const QJsonObject body = QJsonDocument::fromJson(fake->streamingCall(0).request.body).object();
  EXPECT_DOUBLE_EQ(body.value(QStringLiteral("temperature")).toDouble(), 1.0);
}

TEST(OpenAIProvider, EmptyAuthTokenOmitsAuthorizationHeader) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedSuccess(QByteArray(R"({"data":[]})"));
  OpenAIProvider provider(fake);

  provider.refresh();

  EXPECT_FALSE(fake->lastBufferedRequest().headers.contains(QStringLiteral("Authorization")));
}

TEST(OpenAIProvider, SetAuthTokenAddsBearerHeaderToSendChat) {
  auto fake = makeFakeWithEmptyModels();
  OpenAIProvider provider(fake);
  provider.setAuthToken(QStringLiteral("sk-secret"));

  const ModelId model{.provider_id = QStringLiteral("openai"), .model_name = QStringLiteral("gpt-4o")};
  provider.sendChat(model, {}, [](const StreamEvent&) {});

  ASSERT_EQ(fake->streamingCallCount(), 1U);
  EXPECT_EQ(fake->streamingCall(0).request.headers.value(QStringLiteral("Authorization")),
            QStringLiteral("Bearer sk-secret"));
}

TEST(OpenAIProvider, SetBaseUrlAffectsOnlySubsequentCalls) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedSuccess(QByteArray(R"({"data":[]})"));
  fake->setBufferedResponsesDeferred(true);
  OpenAIProvider provider(fake);

  provider.refresh();
  ASSERT_EQ(fake->lastBufferedRequest().url, QStringLiteral("https://api.openai.com/v1/models"));

  provider.setBaseUrl(QStringLiteral("https://proxy.example.test/v1"));
  fake->enqueueBufferedSuccess(QByteArray(R"({"data":[]})"));
  provider.refresh();

  EXPECT_EQ(fake->lastBufferedRequest().url, QStringLiteral("https://proxy.example.test/v1/models"));
}

// T-020: SSE parsing with fragmented byte chunks.
TEST(OpenAIProvider, SendChatReassemblesEventsSplitAcrossChunks) {
  auto fake = makeFakeWithEmptyModels();
  OpenAIProvider provider(fake);

  std::vector<StreamEvent> events;
  const ModelId model{.provider_id = QStringLiteral("openai"), .model_name = QStringLiteral("gpt-4o")};
  provider.sendChat(model, {}, [&events](const StreamEvent& event) { events.push_back(event); });

  const QByteArray block = QByteArray(R"(data: {"type":"response.output_text.delta","delta":"Hello"})") + "\n\n";
  fake->emitData(0, block.left(15));
  fake->emitData(0, block.mid(15));

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<ContentDelta>(events[0]));
  EXPECT_EQ(std::get<ContentDelta>(events[0]).text, QStringLiteral("Hello"));
}

TEST(OpenAIProvider, SendChatReassemblesBlankLineBoundarySplitAcrossChunks) {
  auto fake = makeFakeWithEmptyModels();
  OpenAIProvider provider(fake);

  std::vector<StreamEvent> events;
  const ModelId model{.provider_id = QStringLiteral("openai"), .model_name = QStringLiteral("gpt-4o")};
  provider.sendChat(model, {}, [&events](const StreamEvent& event) { events.push_back(event); });

  const QByteArray block = QByteArray(R"(data: {"type":"response.output_text.delta","delta":"Hi"})") + "\n\n";
  fake->emitData(0, block.left(block.size() - 1));
  fake->emitData(0, block.right(1));

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<ContentDelta>(events[0]));
  EXPECT_EQ(std::get<ContentDelta>(events[0]).text, QStringLiteral("Hi"));
}

TEST(OpenAIProvider, SendChatParsesMultipleBlocksInOneChunk) {
  auto fake = makeFakeWithEmptyModels();
  OpenAIProvider provider(fake);

  std::vector<StreamEvent> events;
  const ModelId model{.provider_id = QStringLiteral("openai"), .model_name = QStringLiteral("gpt-4o")};
  provider.sendChat(model, {}, [&events](const StreamEvent& event) { events.push_back(event); });

  const QByteArray payload = QByteArray(R"(data: {"type":"response.output_text.delta","delta":"Hi"})") + "\n\n" +
                             QByteArray(R"(data: {"type":"response.output_text.delta","delta":" there"})") + "\n\n" +
                             QByteArray(R"(data: {"type":"response.completed"})") + "\n\n";
  fake->emitData(0, payload);

  ASSERT_EQ(events.size(), 3U);
  EXPECT_TRUE(std::holds_alternative<ContentDelta>(events[0]));
  EXPECT_TRUE(std::holds_alternative<ContentDelta>(events[1]));
  EXPECT_TRUE(std::holds_alternative<Completed>(events[2]));
}

TEST(OpenAIProvider, SendChatParsesCrLfDelimitedBlocks) {
  auto fake = makeFakeWithEmptyModels();
  OpenAIProvider provider(fake);

  std::vector<StreamEvent> events;
  const ModelId model{.provider_id = QStringLiteral("openai"), .model_name = QStringLiteral("gpt-4o")};
  provider.sendChat(model, {}, [&events](const StreamEvent& event) { events.push_back(event); });

  const QByteArray payload = QByteArray(R"(data: {"type":"response.output_text.delta","delta":"Hi"})") + "\r\n\r\n" +
                             QByteArray(R"(data: {"type":"response.completed"})") + "\r\n\r\n";
  fake->emitData(0, payload);

  ASSERT_EQ(events.size(), 2U);
  ASSERT_TRUE(std::holds_alternative<ContentDelta>(events[0]));
  EXPECT_EQ(std::get<ContentDelta>(events[0]).text, QStringLiteral("Hi"));
  EXPECT_TRUE(std::holds_alternative<Completed>(events[1]));
}

// T-021: SSE event routing and type mapping.
TEST(OpenAIProvider, SendChatRoutesRefusalDeltaAsContentDelta) {
  auto fake = makeFakeWithEmptyModels();
  OpenAIProvider provider(fake);

  std::vector<StreamEvent> events;
  const ModelId model{.provider_id = QStringLiteral("openai"), .model_name = QStringLiteral("gpt-4o")};
  provider.sendChat(model, {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0, QByteArray(R"(data: {"type":"response.refusal.delta","delta":"I can't help with that"})") + "\n\n");

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<ContentDelta>(events[0]));
  EXPECT_EQ(std::get<ContentDelta>(events[0]).text, QStringLiteral("I can't help with that"));
}

TEST(OpenAIProvider, SendChatIgnoresUnhandledEventTypes) {
  auto fake = makeFakeWithEmptyModels();
  OpenAIProvider provider(fake);

  std::vector<StreamEvent> events;
  const ModelId model{.provider_id = QStringLiteral("openai"), .model_name = QStringLiteral("gpt-4o")};
  provider.sendChat(model, {}, [&events](const StreamEvent& event) { events.push_back(event); });

  const QByteArray payload = QByteArray(R"(data: {"type":"response.created"})") + "\n\n" +
                             QByteArray(R"(data: {"type":"response.in_progress"})") + "\n\n" +
                             QByteArray(R"(data: {"type":"response.output_item.added"})") + "\n\n" +
                             QByteArray(R"(data: {"type":"response.content_part.added"})") + "\n\n" +
                             QByteArray(R"(data: {"type":"response.output_text.delta","delta":"Hi"})") + "\n\n";
  fake->emitData(0, payload);

  ASSERT_EQ(events.size(), 1U);
  EXPECT_TRUE(std::holds_alternative<ContentDelta>(events[0]));
}

// T-022: error handling.
TEST(OpenAIProvider, SendChatTreatsMalformedJsonAsFatalError) {
  auto fake = makeFakeWithEmptyModels();
  OpenAIProvider provider(fake);

  std::vector<StreamEvent> events;
  const ModelId model{.provider_id = QStringLiteral("openai"), .model_name = QStringLiteral("gpt-4o")};
  provider.sendChat(model, {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0, QByteArray("data: not-json") + "\n\n");

  ASSERT_EQ(events.size(), 1U);
  EXPECT_TRUE(std::holds_alternative<Error>(events[0]));
}

TEST(OpenAIProvider, SendChatRoutesResponseFailedToError) {
  auto fake = makeFakeWithEmptyModels();
  OpenAIProvider provider(fake);

  std::vector<StreamEvent> events;
  const ModelId model{.provider_id = QStringLiteral("openai"), .model_name = QStringLiteral("gpt-4o")};
  provider.sendChat(model, {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(
      0, QByteArray(R"(data: {"type":"response.failed","response":{"error":{"message":"server_error"}}})") + "\n\n");

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<Error>(events[0]));
  EXPECT_EQ(std::get<Error>(events[0]).message, QStringLiteral("server_error"));
}

TEST(OpenAIProvider, SendChatRoutesResponseFailedWithTopLevelErrorToError) {
  auto fake = makeFakeWithEmptyModels();
  OpenAIProvider provider(fake);

  std::vector<StreamEvent> events;
  const ModelId model{.provider_id = QStringLiteral("openai"), .model_name = QStringLiteral("gpt-4o")};
  provider.sendChat(model, {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0, QByteArray(R"(data: {"type":"response.failed","error":{"message":"Model overloaded"}})") + "\n\n");

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<Error>(events[0]));
  EXPECT_EQ(std::get<Error>(events[0]).message, QStringLiteral("Model overloaded"));
}

TEST(OpenAIProvider, SendChatRoutesResponseIncompleteToError) {
  auto fake = makeFakeWithEmptyModels();
  OpenAIProvider provider(fake);

  std::vector<StreamEvent> events;
  const ModelId model{.provider_id = QStringLiteral("openai"), .model_name = QStringLiteral("gpt-4o")};
  provider.sendChat(model, {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(
      0, QByteArray(R"(data: {"type":"response.incomplete","response":{"error":{"message":"max_output_tokens"}}})") +
             "\n\n");

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<Error>(events[0]));
  EXPECT_EQ(std::get<Error>(events[0]).message, QStringLiteral("max_output_tokens"));
}

TEST(OpenAIProvider, SendChatRoutesTopLevelErrorTypeToError) {
  auto fake = makeFakeWithEmptyModels();
  OpenAIProvider provider(fake);

  std::vector<StreamEvent> events;
  const ModelId model{.provider_id = QStringLiteral("openai"), .model_name = QStringLiteral("gpt-4o")};
  provider.sendChat(model, {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0, QByteArray(R"(data: {"type":"error","error":{"message":"invalid_api_key"}})") + "\n\n");

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<Error>(events[0]));
  EXPECT_EQ(std::get<Error>(events[0]).message, QStringLiteral("invalid_api_key"));
}

TEST(OpenAIProvider, SendChatTreatsStreamEndBeforeCompletedAsError) {
  auto fake = makeFakeWithEmptyModels();
  OpenAIProvider provider(fake);

  std::vector<StreamEvent> events;
  const ModelId model{.provider_id = QStringLiteral("openai"), .model_name = QStringLiteral("gpt-4o")};
  provider.sendChat(model, {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitFinished(0);

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<Error>(events[0]));
  EXPECT_EQ(std::get<Error>(events[0]).message, QStringLiteral("Stream ended without completion"));
}

TEST(OpenAIProvider, SendChatRecoversErrorMessageFromBufferedJsonBodyOnHttpError) {
  auto fake = makeFakeWithEmptyModels();
  OpenAIProvider provider(fake);

  std::vector<StreamEvent> events;
  const ModelId model{.provider_id = QStringLiteral("openai"), .model_name = QStringLiteral("gpt-4o")};
  provider.sendChat(model, {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0, QByteArray(R"({"error":{"message":"Rate limit exceeded","type":"rate_limit_error"}})"));
  fake->emitError(0, QStringLiteral("Request failed with HTTP status 429"));

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<Error>(events[0]));
  EXPECT_EQ(std::get<Error>(events[0]).message, QStringLiteral("Rate limit exceeded"));
}

TEST(OpenAIProvider, SendChatFallsBackToTransportMessageWhenBufferIsNotJson) {
  auto fake = makeFakeWithEmptyModels();
  OpenAIProvider provider(fake);

  std::vector<StreamEvent> events;
  const ModelId model{.provider_id = QStringLiteral("openai"), .model_name = QStringLiteral("gpt-4o")};
  provider.sendChat(model, {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitError(0, QStringLiteral("Connection refused"));

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<Error>(events[0]));
  EXPECT_EQ(std::get<Error>(events[0]).message, QStringLiteral("Connection refused"));
}

// T-009: usage accumulation from the response.completed event (REQ-F-004, REQ-F-002).
TEST(OpenAIProvider, SendChatPopulatesUsageFromResponseCompleted) {
  auto fake = makeFakeWithEmptyModels();
  OpenAIProvider provider(fake);

  std::vector<StreamEvent> events;
  const ModelId model{.provider_id = QStringLiteral("openai"), .model_name = QStringLiteral("gpt-4o")};
  provider.sendChat(model, {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0, QByteArray(R"(data: {"type":"response.completed","response":{"usage":{)"
                               R"("input_tokens":50,"output_tokens":25,"total_tokens":75,)"
                               R"("input_tokens_details":{"cached_tokens":10},)"
                               R"("output_tokens_details":{"reasoning_tokens":5}}}})") +
                        "\n\n");

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<Completed>(events[0]));
  const Completed& completed = std::get<Completed>(events[0]);
  ASSERT_TRUE(completed.usage.has_value());
  const Usage& usage = *completed.usage;
  EXPECT_EQ(usage.input_tokens, 50);
  EXPECT_EQ(usage.output_tokens, 25);
  EXPECT_EQ(usage.total_tokens, 75);
  EXPECT_EQ(usage.cache_read_tokens, 10);
  EXPECT_EQ(usage.reasoning_tokens, 5);
}

TEST(OpenAIProvider, SendChatLeavesCacheReadTokensNullWhenNotReportedByOpenAI) {
  auto fake = makeFakeWithEmptyModels();
  OpenAIProvider provider(fake);

  std::vector<StreamEvent> events;
  const ModelId model{.provider_id = QStringLiteral("openai"), .model_name = QStringLiteral("gpt-4o")};
  provider.sendChat(model, {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0, QByteArray(R"(data: {"type":"response.completed","response":{"usage":{)"
                               R"("input_tokens":50,"output_tokens":25,"total_tokens":75}}})") +
                        "\n\n");

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<Completed>(events[0]));
  const Completed& completed = std::get<Completed>(events[0]);
  ASSERT_TRUE(completed.usage.has_value());
  EXPECT_FALSE(completed.usage->cache_read_tokens.has_value());
  EXPECT_FALSE(completed.usage->cache_creation_tokens.has_value());
  EXPECT_FALSE(completed.usage->reasoning_tokens.has_value());
}

// T-024: cancellation via HttpRequestHandle.
TEST(OpenAIProvider, CancellingHandleStopsFurtherDeltaProcessing) {
  auto fake = makeFakeWithEmptyModels();
  OpenAIProvider provider(fake);

  std::vector<StreamEvent> events;
  const ModelId model{.provider_id = QStringLiteral("openai"), .model_name = QStringLiteral("gpt-4o")};
  const HttpRequestHandlePtr handle =
      provider.sendChat(model, {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0, QByteArray(R"(data: {"type":"response.output_text.delta","delta":"Hi"})") + "\n\n");
  ASSERT_EQ(events.size(), 1U);

  handle->cancel();
  EXPECT_TRUE(fake->streamingCall(0).cancelled);
}

TEST(OpenAIProvider, SendChatCancelsRequestAfterMalformedJson) {
  auto fake = makeFakeWithEmptyModels();
  OpenAIProvider provider(fake);

  std::vector<StreamEvent> events;
  const ModelId model{.provider_id = QStringLiteral("openai"), .model_name = QStringLiteral("gpt-4o")};
  provider.sendChat(model, {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0, QByteArray("data: not-json") + "\n\n");

  EXPECT_TRUE(fake->streamingCall(0).cancelled);
  ASSERT_EQ(events.size(), 1U);
  EXPECT_TRUE(std::holds_alternative<Error>(events[0]));
}

TEST(OpenAIProvider, SendChatIncludesToolsAndEncryptedReasoningOnlyWhenCatalogProvided) {
  auto fake = makeFakeWithEmptyModels();
  OpenAIProvider provider(fake);
  const holonight_domain::ToolCatalogSnapshot catalog{
      .client_tools = {{
          .function_name = QStringLiteral("list_files"),
          .description = QStringLiteral("List files"),
          .input_schema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}},
      }}};

  provider.sendChat(
      ModelId{.provider_id = QStringLiteral("openai"), .model_name = QStringLiteral("gpt-4o")}, {},
      [](const StreamEvent&) {}, std::chrono::seconds{30}, catalog);

  const QJsonObject body = QJsonDocument::fromJson(fake->streamingCall(0).request.body).object();
  EXPECT_EQ(body.value(QStringLiteral("tools")).toArray().size(), 1);
  EXPECT_EQ(body.value(QStringLiteral("include")).toArray().at(0), QStringLiteral("reasoning.encrypted_content"));
  EXPECT_EQ(body.value(QStringLiteral("store")), false);
  EXPECT_FALSE(body.contains(QStringLiteral("previous_response_id")));
}

TEST(OpenAIProvider, SendChatEmitsAllCompletedFunctionCallsBeforeCompleted) {
  auto fake = makeFakeWithEmptyModels();
  OpenAIProvider provider(fake, QStringLiteral("https://api.openai.com/v1"), QStringLiteral("work"));
  std::vector<StreamEvent> events;
  provider.sendChat(ModelId{.provider_id = QStringLiteral("work"), .model_name = QStringLiteral("gpt-4o")}, {},
                    [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(
      0,
      QByteArray(
          R"(data: {"type":"response.completed","response":{"output":[{"type":"reasoning","id":"rs_1","encrypted_content":"opaque"},{"type":"function_call","id":"fc_1","call_id":"call_1","name":"list_files","arguments":"{}"},{"type":"function_call","call_id":"call_2","name":"list_files","arguments":"{\"path\":\".\"}"}]}})") +
          "\n\n");

  ASSERT_EQ(events.size(), 3U);
  ASSERT_TRUE(std::holds_alternative<ToolRequestEvent>(events[0]));
  ASSERT_TRUE(std::holds_alternative<ToolRequestEvent>(events[1]));
  EXPECT_EQ(std::get<ToolRequestEvent>(events[0]).provider_instance_id, QStringLiteral("work"));
  EXPECT_EQ(std::get<ToolRequestEvent>(events[1]).provider_call_id, QStringLiteral("call_2"));
  EXPECT_TRUE(std::holds_alternative<Completed>(events[2]));
}

TEST(OpenAIProvider, SendChatRejectsMalformedCompletedCallWithoutExecutingEarlierCall) {
  auto fake = makeFakeWithEmptyModels();
  OpenAIProvider provider(fake);
  std::vector<StreamEvent> events;
  provider.sendChat(ModelId{.provider_id = QStringLiteral("openai"), .model_name = QStringLiteral("gpt-4o")}, {},
                    [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(
      0,
      QByteArray(
          R"(data: {"type":"response.completed","response":{"output":[{"type":"function_call","call_id":"call_1","name":"list_files","arguments":"{}"},{"type":"function_call","call_id":"call_2","name":"list_files","arguments":"[]"}]}})") +
          "\n\n");

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<Error>(events[0]));
  EXPECT_TRUE(std::get<Error>(events[0]).message.contains(QStringLiteral("arguments must be a JSON object")));
}

}  // namespace
}  // namespace holonight_providers
