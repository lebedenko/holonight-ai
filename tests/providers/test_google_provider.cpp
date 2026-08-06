#include "fake_http_client.h"
#include "holonight_providers/google_provider.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>

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
using holonight_domain::ToolCallEntry;
using holonight_domain::ToolCallKind;
using holonight_domain::ToolRequestEvent;
using holonight_domain::Usage;

std::shared_ptr<FakeHttpClient> makeFakeWithEmptyModels() {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedSuccess(QByteArray(R"({"models":[]})"));
  return fake;
}

ModelId testModel() {
  return ModelId{.provider_id = QStringLiteral("google"), .model_name = QStringLiteral("gemini-2.0-flash")};
}

// Builds a Part-level JSON object -- the object with sibling keys "functionCall" and, optionally,
// "thoughtSignature" -- matching Gemini's atomic (non-fragmented) functionCall delivery shape.
QJsonObject functionCallPart(const QString& name, const QJsonObject& args, const QString& id = QString(),
                             const QString& thoughtSignature = QString()) {
  QJsonObject functionCall{{QStringLiteral("name"), name}, {QStringLiteral("args"), args}};
  if (!id.isEmpty()) {
    functionCall[QStringLiteral("id")] = id;
  }
  QJsonObject part{{QStringLiteral("functionCall"), functionCall}};
  if (!thoughtSignature.isEmpty()) {
    part[QStringLiteral("thoughtSignature")] = thoughtSignature;
  }
  return part;
}

// One complete candidates[0].content.parts[] SSE block -- Gemini has no
// content_block_start/delta/stop lifecycle, so a single block is how a functionCall actually
// arrives on the wire.
QByteArray candidateChunk(const QJsonArray& parts, const QString& finishReason = QString()) {
  QJsonObject candidate{{QStringLiteral("content"), QJsonObject{{QStringLiteral("role"), QStringLiteral("model")},
                                                                {QStringLiteral("parts"), parts}}}};
  if (!finishReason.isEmpty()) {
    candidate[QStringLiteral("finishReason")] = finishReason;
  }
  return QByteArray("data: ") +
         QJsonDocument(QJsonObject{{QStringLiteral("candidates"), QJsonArray{candidate}}})
             .toJson(QJsonDocument::Compact) +
         "\n\n";
}

// Model denylist + supportedGenerationMethods filtering (REQ-F-009/010/011).
TEST(GoogleProvider, RefreshFiltersDenylistedAndNonChatModelsAndStripsPrefix) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedSuccess(QByteArray(R"({"models":[
      {"name":"models/gemini-2.0-flash","supportedGenerationMethods":["generateContent"]},
      {"name":"models/text-embedding-004","supportedGenerationMethods":["embedContent"]},
      {"name":"models/moderation-latest","supportedGenerationMethods":["generateContent"]},
      {"name":"models/gemini-vision","supportedGenerationMethods":["generateContent"]},
      {"name":"models/gemini-ocr-preview","supportedGenerationMethods":["generateContent"]},
      {"name":"models/gemini-only-embed","supportedGenerationMethods":["embedContent"]},
      {"name":"models/gemini-no-methods"},
      {"name":"models/gemini-4-ultra-future","supportedGenerationMethods":["generateContent"]}
  ]})"));

  GoogleProvider provider(fake);
  provider.refresh();

  ASSERT_EQ(provider.availableModels().size(), 2U);
  EXPECT_EQ(provider.availableModels()[0].provider_id, QStringLiteral("google"));
  EXPECT_EQ(provider.availableModels()[0].model_name, QStringLiteral("gemini-2.0-flash"));
  EXPECT_EQ(provider.availableModels()[1].model_name, QStringLiteral("gemini-4-ultra-future"));
}

TEST(GoogleProvider, RefreshUsesConfiguredInstanceId) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedSuccess(QByteArray(
      R"({"models":[{"name":"models/gemini-2.0-flash","supportedGenerationMethods":["generateContent"]}]})"));
  GoogleProvider provider(fake, QStringLiteral("https://generativelanguage.googleapis.com"),
                          QStringLiteral("google-work"));

  provider.refresh();

  ASSERT_EQ(provider.availableModels().size(), 1U);
  EXPECT_EQ(provider.availableModels()[0].provider_id, QStringLiteral("google-work"));
}

TEST(GoogleProvider, RefreshFailureLeavesModelListEmpty) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedError(QStringLiteral("connection refused"));

  GoogleProvider provider(fake);
  provider.refresh();

  EXPECT_TRUE(provider.availableModels().empty());
}

TEST(GoogleProvider, RefreshTwoCallbackOverloadCallsOnSuccessOnHttpSuccess) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedSuccess(QByteArray(
      R"({"models":[{"name":"models/gemini-2.0-flash","supportedGenerationMethods":["generateContent"]}]})"));
  GoogleProvider provider(fake);

  bool successCalled = false;
  bool errorCalled = false;
  provider.refresh([&successCalled] { successCalled = true; }, [&errorCalled](const QString&) { errorCalled = true; });

  EXPECT_TRUE(successCalled);
  EXPECT_FALSE(errorCalled);
}

TEST(GoogleProvider, RefreshTwoCallbackOverloadCallsOnErrorWithReasonOnHttpFailure) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedError(QStringLiteral("connection refused"));
  GoogleProvider provider(fake);

  bool successCalled = false;
  QString errorReason;
  provider.refresh([&successCalled] { successCalled = true; },
                   [&errorReason](const QString& reason) { errorReason = reason; });

  EXPECT_FALSE(successCalled);
  EXPECT_EQ(errorReason, QStringLiteral("connection refused"));
}

TEST(GoogleProvider, RefreshRejectsMalformedSuccessAndPreservesCachedModels) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedSuccess(QByteArray(
      R"({"models":[{"name":"models/gemini-2.0-flash","supportedGenerationMethods":["generateContent"]}]})"));
  GoogleProvider provider(fake);
  provider.refresh();

  bool successCalled = false;
  QString errorReason;
  fake->enqueueBufferedSuccess(QByteArray(R"({"unexpected":[]})"));
  provider.refresh([&successCalled] { successCalled = true; },
                   [&errorReason](const QString& reason) { errorReason = reason; });

  EXPECT_FALSE(successCalled);
  EXPECT_EQ(errorReason, QStringLiteral("Malformed model list response from Google"));
  ASSERT_EQ(provider.availableModels().size(), 1U);
  EXPECT_EQ(provider.availableModels().front().model_name, QStringLiteral("gemini-2.0-flash"));
}

TEST(GoogleProvider, RefreshExtractsErrorMessageFromJsonBodyInsteadOfSurfacingRawBlob) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedError(
      QStringLiteral(R"({"error":{"code":401,"message":"API key not valid","status":"UNAUTHENTICATED"}})"));
  GoogleProvider provider(fake);

  QString errorReason;
  provider.refresh([] {}, [&errorReason](const QString& reason) { errorReason = reason; });

  EXPECT_EQ(errorReason, QStringLiteral("API key not valid"));
}

TEST(GoogleProvider, RefreshRequestsExpectedUrlAndHeaders) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedSuccess(QByteArray(R"({"models":[]})"));
  GoogleProvider provider(fake);

  provider.refresh();

  EXPECT_EQ(fake->lastBufferedRequest().url,
            QStringLiteral("https://generativelanguage.googleapis.com/v1beta/models?pageSize=1000"));
  EXPECT_FALSE(fake->lastBufferedRequest().headers.contains(QStringLiteral("x-goog-api-key")));
  EXPECT_EQ(fake->lastBufferedTimeout(), std::chrono::seconds{10});
}

TEST(GoogleProvider, RefreshFollowsNextPageTokenAndCombinesAllModels) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedSuccess(QByteArray(R"({
    "models":[{"name":"models/gemini-first","supportedGenerationMethods":["generateContent"]}],
    "nextPageToken":"next page/+"
  })"));
  fake->enqueueBufferedSuccess(
      QByteArray(R"({"models":[{"name":"models/gemini-second","supportedGenerationMethods":["generateContent"]}]})"));
  GoogleProvider provider(fake);

  provider.refresh();

  ASSERT_EQ(fake->bufferedCallCount(), 2U);
  const QUrl secondPageUrl(fake->lastBufferedRequest().url);
  const QUrlQuery secondPageQuery(secondPageUrl);
  EXPECT_EQ(secondPageUrl.path(), QStringLiteral("/v1beta/models"));
  EXPECT_EQ(secondPageQuery.queryItemValue(QStringLiteral("pageSize")), QStringLiteral("1000"));
  EXPECT_EQ(secondPageQuery.queryItemValue(QStringLiteral("pageToken"), QUrl::FullyDecoded),
            QStringLiteral("next page/+"));
  ASSERT_EQ(provider.availableModels().size(), 2U);
  EXPECT_EQ(provider.availableModels()[0].model_name, QStringLiteral("gemini-first"));
  EXPECT_EQ(provider.availableModels()[1].model_name, QStringLiteral("gemini-second"));
}

TEST(GoogleProvider, RefreshLaterPageFailurePreservesCachedModels) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedSuccess(
      QByteArray(R"({"models":[{"name":"models/gemini-cached","supportedGenerationMethods":["generateContent"]}]})"));
  GoogleProvider provider(fake);
  provider.refresh();

  fake->enqueueBufferedSuccess(QByteArray(R"({
    "models":[{"name":"models/gemini-partial","supportedGenerationMethods":["generateContent"]}],
    "nextPageToken":"next"
  })"));
  fake->enqueueBufferedError(QStringLiteral("second page failed"));
  QString errorReason;
  provider.refresh([] {}, [&errorReason](const QString& reason) { errorReason = reason; });

  EXPECT_EQ(errorReason, QStringLiteral("second page failed"));
  ASSERT_EQ(provider.availableModels().size(), 1U);
  EXPECT_EQ(provider.availableModels().front().model_name, QStringLiteral("gemini-cached"));
}

// Request body construction (REQ-F-001-007).
TEST(GoogleProvider, SendChatConstructsGenerateContentRequestBody) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);
  provider.setTemperature(0.8);
  provider.setMaxOutputTokens(2048);

  const std::vector<Message> history{Message(MessageId::generate(), MessageRole::User, QString("hi"))};
  provider.sendChat(testModel(), history, [](const StreamEvent&) {});

  ASSERT_EQ(fake->streamingCallCount(), 1U);
  EXPECT_EQ(
      fake->streamingCall(0).request.url,
      QStringLiteral(
          "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:streamGenerateContent?alt=sse"));

  const QJsonObject body = QJsonDocument::fromJson(fake->streamingCall(0).request.body).object();
  EXPECT_FALSE(body.contains(QStringLiteral("model")));  // DESIGN.md §5.9: model lives in URL only
  EXPECT_FALSE(body.contains(QStringLiteral("stream")));
  const QJsonObject generationConfig = body.value(QStringLiteral("generationConfig")).toObject();
  EXPECT_DOUBLE_EQ(generationConfig.value(QStringLiteral("temperature")).toDouble(), 0.8);
  EXPECT_EQ(generationConfig.value(QStringLiteral("maxOutputTokens")).toInt(), 2048);
  ASSERT_TRUE(body.value(QStringLiteral("contents")).isArray());
  const QJsonObject firstEntry = body.value(QStringLiteral("contents")).toArray().at(0).toObject();
  EXPECT_EQ(firstEntry.value(QStringLiteral("role")).toString(), QStringLiteral("user"));
  EXPECT_EQ(
      firstEntry.value(QStringLiteral("parts")).toArray().at(0).toObject().value(QStringLiteral("text")).toString(),
      QStringLiteral("hi"));

  EXPECT_FALSE(body.contains(QStringLiteral("safetySettings")));
  EXPECT_FALSE(body.contains(QStringLiteral("tools")));
  EXPECT_FALSE(body.contains(QStringLiteral("toolConfig")));
}

TEST(GoogleProvider, SendChatMapsAssistantRoleToModelNotAssistant) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);

  const std::vector<Message> history{
      Message(MessageId::generate(), MessageRole::User, QString("hi")),
      Message(MessageId::generate(), MessageRole::Assistant, QString("hello")),
      Message(MessageId::generate(), MessageRole::User, QString("how are you")),
  };
  provider.sendChat(testModel(), history, [](const StreamEvent&) {});

  const QJsonObject body = QJsonDocument::fromJson(fake->streamingCall(0).request.body).object();
  const QJsonArray contents = body.value(QStringLiteral("contents")).toArray();
  ASSERT_EQ(contents.size(), 3);
  EXPECT_EQ(contents.at(0).toObject().value(QStringLiteral("role")).toString(), QStringLiteral("user"));
  EXPECT_EQ(contents.at(1).toObject().value(QStringLiteral("role")).toString(), QStringLiteral("model"));
  EXPECT_EQ(contents.at(2).toObject().value(QStringLiteral("role")).toString(), QStringLiteral("user"));
}

TEST(GoogleProvider, SendChatDefaultTemperatureAndMaxTokensMatchConfigDefaults) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);

  provider.sendChat(testModel(), {}, [](const StreamEvent&) {});

  const QJsonObject body = QJsonDocument::fromJson(fake->streamingCall(0).request.body).object();
  const QJsonObject generationConfig = body.value(QStringLiteral("generationConfig")).toObject();
  EXPECT_DOUBLE_EQ(generationConfig.value(QStringLiteral("temperature")).toDouble(), 1.0);
  EXPECT_EQ(generationConfig.value(QStringLiteral("maxOutputTokens")).toInt(), 1024);
}

TEST(GoogleProvider, SendChatWithNoSystemMessagesOmitsSystemInstructionFieldEntirely) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);

  const std::vector<Message> history{Message(MessageId::generate(), MessageRole::User, QString("hi"))};
  provider.sendChat(testModel(), history, [](const StreamEvent&) {});

  const QJsonObject body = QJsonDocument::fromJson(fake->streamingCall(0).request.body).object();
  EXPECT_FALSE(body.contains(QStringLiteral("systemInstruction")));
}

TEST(GoogleProvider, SendChatHoistsSingleSystemMessageToTopLevelFieldAndExcludesItFromContents) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);

  const std::vector<Message> history{
      Message(MessageId::generate(), MessageRole::System, QString("Be terse.")),
      Message(MessageId::generate(), MessageRole::User, QString("hi")),
  };
  provider.sendChat(testModel(), history, [](const StreamEvent&) {});

  const QJsonObject body = QJsonDocument::fromJson(fake->streamingCall(0).request.body).object();
  const QJsonObject systemInstruction = body.value(QStringLiteral("systemInstruction")).toObject();
  EXPECT_EQ(systemInstruction.value(QStringLiteral("parts"))
                .toArray()
                .at(0)
                .toObject()
                .value(QStringLiteral("text"))
                .toString(),
            QStringLiteral("Be terse."));
  ASSERT_TRUE(body.value(QStringLiteral("contents")).isArray());
  EXPECT_EQ(body.value(QStringLiteral("contents")).toArray().size(), 1);
}

TEST(GoogleProvider, SendChatConcatenatesMultipleSystemMessagesWithBlankLine) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);

  const std::vector<Message> history{
      Message(MessageId::generate(), MessageRole::System, QString("Context A")),
      Message(MessageId::generate(), MessageRole::System, QString("Context B")),
      Message(MessageId::generate(), MessageRole::User, QString("hi")),
  };
  provider.sendChat(testModel(), history, [](const StreamEvent&) {});

  const QJsonObject body = QJsonDocument::fromJson(fake->streamingCall(0).request.body).object();
  const QJsonObject systemInstruction = body.value(QStringLiteral("systemInstruction")).toObject();
  EXPECT_EQ(systemInstruction.value(QStringLiteral("parts"))
                .toArray()
                .at(0)
                .toObject()
                .value(QStringLiteral("text"))
                .toString(),
            QStringLiteral("Context A\n\nContext B"));
  ASSERT_TRUE(body.value(QStringLiteral("contents")).isArray());
  EXPECT_EQ(body.value(QStringLiteral("contents")).toArray().size(), 1);
}

TEST(GoogleProvider, EmptyAuthKeyOmitsApiKeyHeaderEntirely) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedSuccess(QByteArray(R"({"models":[]})"));
  GoogleProvider provider(fake);

  provider.refresh();

  EXPECT_FALSE(fake->lastBufferedRequest().headers.contains(QStringLiteral("x-goog-api-key")));
}

TEST(GoogleProvider, SetAuthKeyAddsApiKeyHeaderToSendChat) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);
  provider.setAuthKey(QStringLiteral("secret-key"));

  provider.sendChat(testModel(), {}, [](const StreamEvent&) {});

  ASSERT_EQ(fake->streamingCallCount(), 1U);
  EXPECT_EQ(fake->streamingCall(0).request.headers.value(QStringLiteral("x-goog-api-key")),
            QStringLiteral("secret-key"));
}

TEST(GoogleProvider, SetBaseUrlAffectsOnlySubsequentCalls) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedSuccess(QByteArray(R"({"models":[]})"));
  fake->setBufferedResponsesDeferred(true);
  GoogleProvider provider(fake);

  provider.refresh();
  ASSERT_EQ(fake->lastBufferedRequest().url,
            QStringLiteral("https://generativelanguage.googleapis.com/v1beta/models?pageSize=1000"));

  provider.setBaseUrl(QStringLiteral("https://proxy.example.test"));
  fake->enqueueBufferedSuccess(QByteArray(R"({"models":[]})"));
  provider.refresh();

  EXPECT_EQ(fake->lastBufferedRequest().url, QStringLiteral("https://proxy.example.test/v1beta/models?pageSize=1000"));
}

// SSE parsing with fragmented byte chunks (REQ-F-012/013).
TEST(GoogleProvider, SendChatReassemblesEventsSplitAcrossChunks) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  const QByteArray block =
      QByteArray(R"(data: {"candidates":[{"content":{"role":"model","parts":[{"text":"Hello"}]}}]})") + "\n\n";
  fake->emitData(0, block.left(15));
  fake->emitData(0, block.mid(15));

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<ContentDelta>(events[0]));
  EXPECT_EQ(std::get<ContentDelta>(events[0]).text, QStringLiteral("Hello"));
}

TEST(GoogleProvider, SendChatReassemblesBlankLineBoundarySplitAcrossChunks) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  const QByteArray block =
      QByteArray(R"(data: {"candidates":[{"content":{"role":"model","parts":[{"text":"Hi"}]}}]})") + "\n\n";
  fake->emitData(0, block.left(block.size() - 1));
  fake->emitData(0, block.right(1));

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<ContentDelta>(events[0]));
  EXPECT_EQ(std::get<ContentDelta>(events[0]).text, QStringLiteral("Hi"));
}

TEST(GoogleProvider, SendChatParsesFiveBlocksAcrossFragmentedChunksInOrder) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  const QByteArray payload =
      QByteArray(R"(data: {"candidates":[{"content":{"role":"model","parts":[{"text":"Hi"}]}}]})") + "\n\n" +
      QByteArray(R"(data: {"candidates":[{"content":{"role":"model","parts":[{"text":" there"}]}}]})") + "\n\n" +
      QByteArray(R"(data: {"candidates":[{"content":{"role":"model","parts":[{"text":"!"}]}}]})") + "\n\n" +
      QByteArray(R"(data: {"candidates":[]})") + "\n\n" +
      QByteArray(R"(data: {"candidates":[{"finishReason":"STOP"}]})") + "\n\n";
  // Split mid-block to exercise the fragmentation path across all five blocks.
  fake->emitData(0, payload.left(40));
  fake->emitData(0, payload.mid(40));

  ASSERT_EQ(events.size(), 4U);
  EXPECT_TRUE(std::holds_alternative<ContentDelta>(events[0]));
  EXPECT_EQ(std::get<ContentDelta>(events[0]).text, QStringLiteral("Hi"));
  EXPECT_TRUE(std::holds_alternative<ContentDelta>(events[1]));
  EXPECT_EQ(std::get<ContentDelta>(events[1]).text, QStringLiteral(" there"));
  EXPECT_TRUE(std::holds_alternative<ContentDelta>(events[2]));
  EXPECT_EQ(std::get<ContentDelta>(events[2]).text, QStringLiteral("!"));
  EXPECT_TRUE(std::holds_alternative<Completed>(events[3]));
}

TEST(GoogleProvider, SendChatParsesCrLfDelimitedBlocks) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  const QByteArray payload =
      QByteArray(R"(data: {"candidates":[{"content":{"role":"model","parts":[{"text":"Hi"}]}}]})") + "\r\n\r\n" +
      QByteArray(R"(data: {"candidates":[{"finishReason":"STOP"}]})") + "\r\n\r\n";
  fake->emitData(0, payload);

  ASSERT_EQ(events.size(), 2U);
  ASSERT_TRUE(std::holds_alternative<ContentDelta>(events[0]));
  EXPECT_EQ(std::get<ContentDelta>(events[0]).text, QStringLiteral("Hi"));
  EXPECT_TRUE(std::holds_alternative<Completed>(events[1]));
}

// Terminal finishReason handling — a block carrying both trailing text and a terminal reason
// together emits both events, in order (REQ-F-014/015).
TEST(GoogleProvider, SendChatEmitsTrailingDeltaThenCompletedFromSameBlock) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(
      0,
      QByteArray(
          R"(data: {"candidates":[{"content":{"role":"model","parts":[{"text":"done."}]},"finishReason":"STOP"}]})") +
          "\n\n");

  ASSERT_EQ(events.size(), 2U);
  EXPECT_TRUE(std::holds_alternative<ContentDelta>(events[0]));
  EXPECT_EQ(std::get<ContentDelta>(events[0]).text, QStringLiteral("done."));
  EXPECT_TRUE(std::holds_alternative<Completed>(events[1]));
}

// Regression: real Gemini responses include "finishReason" on intermediate chunks too, set to the
// proto3 zero-value "FINISH_REASON_UNSPECIFIED" rather than omitting the key. Key *presence* alone
// must not be treated as terminal, or streams cut off after the first chunk (reported bug).
TEST(GoogleProvider, SendChatTreatsUnspecifiedFinishReasonAsNonTerminal) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  const QByteArray payload =
      QByteArray(
          R"(data: {"candidates":[{"content":{"role":"model","parts":[{"text":"Hi"}]},"finishReason":"FINISH_REASON_UNSPECIFIED"}]})") +
      "\n\n" +
      QByteArray(
          R"(data: {"candidates":[{"content":{"role":"model","parts":[{"text":" there"}]},"finishReason":"FINISH_REASON_UNSPECIFIED"}]})") +
      "\n\n" +
      QByteArray(
          R"(data: {"candidates":[{"content":{"role":"model","parts":[{"text":"!"}]},"finishReason":"STOP"}]})") +
      "\n\n";
  fake->emitData(0, payload);

  ASSERT_EQ(events.size(), 4U);
  EXPECT_TRUE(std::holds_alternative<ContentDelta>(events[0]));
  EXPECT_EQ(std::get<ContentDelta>(events[0]).text, QStringLiteral("Hi"));
  EXPECT_TRUE(std::holds_alternative<ContentDelta>(events[1]));
  EXPECT_EQ(std::get<ContentDelta>(events[1]).text, QStringLiteral(" there"));
  ASSERT_TRUE(std::holds_alternative<ContentDelta>(events[2]));
  EXPECT_EQ(std::get<ContentDelta>(events[2]).text, QStringLiteral("!"));
  EXPECT_TRUE(std::holds_alternative<Completed>(events[3]));
}

// Non-text, non-functionCall parts remain a deliberate no-op (functionCall parts are covered by the
// tool-calling suite below, T-019).
TEST(GoogleProvider, SendChatIgnoresInlineDataAndOtherNonTextNonFunctionCallParts) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0, QByteArray(R"(data: {"candidates":[{"content":{"role":"model","parts":[)"
                               R"({"inlineData":{"mimeType":"image/png","data":"AA=="}}]}}]})") +
                        "\n\n");

  EXPECT_TRUE(events.empty());
}

// Tool catalog wiring in the request body (REQ-F-001/002).
TEST(GoogleProvider, SendChatIncludesToolsArrayInRequestBodyWhenProvided) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);

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
  const QJsonArray declarations = bodyTools.at(0).toObject().value(QStringLiteral("functionDeclarations")).toArray();
  ASSERT_EQ(declarations.size(), 1);
  const QJsonObject declaration = declarations.at(0).toObject();
  EXPECT_EQ(declaration.value(QStringLiteral("name")).toString(), QStringLiteral("ListFiles"));
  EXPECT_EQ(declaration.value(QStringLiteral("description")).toString(), QStringLiteral("Lists directory entries."));
  EXPECT_EQ(declaration.value(QStringLiteral("parameters")).toObject().value(QStringLiteral("type")).toString(),
            QStringLiteral("object"));
}

TEST(GoogleProvider, SendChatOmitsToolsKeyWhenToolsAbsentOrEmpty) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);

  provider.sendChat(testModel(), {}, [](const StreamEvent&) {});

  const QJsonObject body = QJsonDocument::fromJson(fake->streamingCall(0).request.body).object();
  EXPECT_FALSE(body.contains(QStringLiteral("tools")));
}

// Atomic and parallel functionCall parsing (REQ-F-003/004/NF-004): Gemini delivers name+args in one
// chunk, unlike Anthropic's input_json_delta accumulation, so no per-index accumulator is exercised.
TEST(GoogleProvider, SendChatEmitsToolCallOnAtomicFunctionCallPart) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(
      0, candidateChunk(QJsonArray{functionCallPart(
             QStringLiteral("ListFiles"), QJsonObject{{QStringLiteral("path"), QStringLiteral("~/Documents")}})}));

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<ToolRequestEvent>(events[0]));
  const ToolRequestEvent& toolCall = std::get<ToolRequestEvent>(events[0]);
  EXPECT_EQ(toolCall.function_name, QStringLiteral("ListFiles"));
  EXPECT_EQ(toolCall.arguments.value(QStringLiteral("path")).toString(), QStringLiteral("~/Documents"));
  EXPECT_EQ(toolCall.provider_instance_id, QStringLiteral("google"));
  EXPECT_EQ(toolCall.execution_location, holonight_domain::ToolExecutionLocation::LocalClient);
  EXPECT_EQ(toolCall.source, holonight_domain::ToolSource::BuiltIn);
}

TEST(GoogleProvider, SendChatEmitsMultipleToolCallsForMultipleFunctionCallPartsInOrder) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0, candidateChunk(QJsonArray{
                        functionCallPart(QStringLiteral("check_weather"),
                                         QJsonObject{{QStringLiteral("city"), QStringLiteral("Paris")}}),
                        functionCallPart(QStringLiteral("check_weather"),
                                         QJsonObject{{QStringLiteral("city"), QStringLiteral("London")}}),
                    }));

  ASSERT_EQ(events.size(), 2U);
  ASSERT_TRUE(std::holds_alternative<ToolRequestEvent>(events[0]));
  ASSERT_TRUE(std::holds_alternative<ToolRequestEvent>(events[1]));
  EXPECT_EQ(std::get<ToolRequestEvent>(events[0]).arguments.value(QStringLiteral("city")).toString(),
            QStringLiteral("Paris"));
  EXPECT_EQ(std::get<ToolRequestEvent>(events[1]).arguments.value(QStringLiteral("city")).toString(),
            QStringLiteral("London"));
}

// Thought signature and provider-call-id metadata (REQ-F-005/006).
TEST(GoogleProvider, SendChatCapturesThoughtSignatureWhenPresent) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0, candidateChunk(QJsonArray{functionCallPart(QStringLiteral("ListFiles"), QJsonObject{}, QString(),
                                                               QStringLiteral("sig-abc"))}));

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<ToolRequestEvent>(events[0]));
  const ToolRequestEvent& toolCall = std::get<ToolRequestEvent>(events[0]);
  ASSERT_TRUE(toolCall.thought_signature.has_value());
  EXPECT_EQ(*toolCall.thought_signature, QStringLiteral("sig-abc"));
}

TEST(GoogleProvider, SendChatOmitsThoughtSignatureWhenAbsent) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0, candidateChunk(QJsonArray{functionCallPart(QStringLiteral("ListFiles"), QJsonObject{})}));

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<ToolRequestEvent>(events[0]));
  EXPECT_FALSE(std::get<ToolRequestEvent>(events[0]).thought_signature.has_value());
}

TEST(GoogleProvider, SendChatUsesProviderSuppliedIdVerbatim) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0, candidateChunk(QJsonArray{
                        functionCallPart(QStringLiteral("ListFiles"), QJsonObject{}, QStringLiteral("call_123"))}));

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<ToolRequestEvent>(events[0]));
  const ToolRequestEvent& toolCall = std::get<ToolRequestEvent>(events[0]);
  EXPECT_EQ(toolCall.provider_call_id, QStringLiteral("call_123"));
  EXPECT_FALSE(toolCall.provider_call_id_synthesized);
}

TEST(GoogleProvider, SendChatSynthesizesUuidWhenIdAbsent) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0, candidateChunk(QJsonArray{functionCallPart(QStringLiteral("ListFiles"), QJsonObject{})}));

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<ToolRequestEvent>(events[0]));
  const ToolRequestEvent& toolCall = std::get<ToolRequestEvent>(events[0]);
  EXPECT_TRUE(toolCall.provider_call_id_synthesized);
  EXPECT_FALSE(QUuid::fromString(toolCall.provider_call_id).isNull());
}

// Malformed functionCall parts fail the stream instead of emitting a garbage event (REQ-F-003).
TEST(GoogleProvider, SendChatFailsStreamOnFunctionCallMissingName) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  const QJsonObject part{{QStringLiteral("functionCall"), QJsonObject{{QStringLiteral("args"), QJsonObject{}}}}};
  fake->emitData(0, candidateChunk(QJsonArray{part}));

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<Error>(events[0]));
  EXPECT_EQ(std::get<Error>(events[0]).message, QStringLiteral("Malformed function call from Google"));
}

TEST(GoogleProvider, SendChatFailsStreamOnNonObjectArgs) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  const QJsonObject part{
      {QStringLiteral("functionCall"), QJsonObject{{QStringLiteral("name"), QStringLiteral("ListFiles")},
                                                   {QStringLiteral("args"), QStringLiteral("not-an-object")}}}};
  fake->emitData(0, candidateChunk(QJsonArray{part}));

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<Error>(events[0]));
  EXPECT_EQ(std::get<Error>(events[0]).message, QStringLiteral("Malformed function call from Google"));
}

// History reconstruction across turns (REQ-F-007/008/009).
TEST(GoogleProvider, SendChatReconstructsFunctionCallAndFunctionResponseAcrossTurns) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);

  QJsonObject input;
  input[QStringLiteral("path")] = QStringLiteral("~/Documents");
  Message invocation(MessageId::generate(), MessageRole::Assistant, QString());
  invocation.setToolCalls({ToolCallEntry{.kind = ToolCallKind::Invocation,
                                         .tool_use_id = QStringLiteral("call_123"),
                                         .tool_name = QStringLiteral("ListFiles"),
                                         .function_name = QStringLiteral("ListFiles"),
                                         .input = input}});

  QJsonObject result;
  result[QStringLiteral("entries")] = QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("notes.txt")}}};
  Message toolResult(MessageId::generate(), MessageRole::User, QString());
  toolResult.setToolCalls({ToolCallEntry{.kind = ToolCallKind::Result,
                                         .tool_use_id = QStringLiteral("call_123"),
                                         .function_name = QStringLiteral("ListFiles"),
                                         .result = result}});

  const std::vector<Message> history{
      Message(MessageId::generate(), MessageRole::User, QString("List my documents")),
      invocation,
      toolResult,
  };
  provider.sendChat(testModel(), history, [](const StreamEvent&) {});

  const QJsonObject body = QJsonDocument::fromJson(fake->streamingCall(0).request.body).object();
  const QJsonArray contents = body.value(QStringLiteral("contents")).toArray();
  ASSERT_EQ(contents.size(), 3);

  EXPECT_EQ(contents.at(0).toObject().value(QStringLiteral("role")).toString(), QStringLiteral("user"));

  const QJsonObject modelEntry = contents.at(1).toObject();
  EXPECT_EQ(modelEntry.value(QStringLiteral("role")).toString(), QStringLiteral("model"));
  const QJsonObject functionCallObj = modelEntry.value(QStringLiteral("parts"))
                                          .toArray()
                                          .at(0)
                                          .toObject()
                                          .value(QStringLiteral("functionCall"))
                                          .toObject();
  EXPECT_EQ(functionCallObj.value(QStringLiteral("name")).toString(), QStringLiteral("ListFiles"));
  EXPECT_EQ(functionCallObj.value(QStringLiteral("id")).toString(), QStringLiteral("call_123"));
  EXPECT_EQ(functionCallObj.value(QStringLiteral("args")).toObject().value(QStringLiteral("path")).toString(),
            QStringLiteral("~/Documents"));

  const QJsonObject userEntry = contents.at(2).toObject();
  EXPECT_EQ(userEntry.value(QStringLiteral("role")).toString(), QStringLiteral("user"));
  const QJsonObject functionResponseObj = userEntry.value(QStringLiteral("parts"))
                                              .toArray()
                                              .at(0)
                                              .toObject()
                                              .value(QStringLiteral("functionResponse"))
                                              .toObject();
  EXPECT_EQ(functionResponseObj.value(QStringLiteral("name")).toString(), QStringLiteral("ListFiles"));
  EXPECT_EQ(functionResponseObj.value(QStringLiteral("id")).toString(), QStringLiteral("call_123"));
  EXPECT_TRUE(functionResponseObj.value(QStringLiteral("response")).toObject().contains(QStringLiteral("entries")));
}

TEST(GoogleProvider, SendChatEchoesThoughtSignatureOnFollowUp) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);

  Message invocation(MessageId::generate(), MessageRole::Assistant, QString());
  invocation.setToolCalls({ToolCallEntry{.kind = ToolCallKind::Invocation,
                                         .tool_use_id = QStringLiteral("call_123"),
                                         .tool_name = QStringLiteral("ListFiles"),
                                         .thought_signature = QStringLiteral("sig-xyz")}});
  provider.sendChat(testModel(), {invocation}, [](const StreamEvent&) {});

  const QJsonObject body = QJsonDocument::fromJson(fake->streamingCall(0).request.body).object();
  const QJsonObject part = body.value(QStringLiteral("contents"))
                               .toArray()
                               .at(0)
                               .toObject()
                               .value(QStringLiteral("parts"))
                               .toArray()
                               .at(0)
                               .toObject();
  ASSERT_TRUE(part.contains(QStringLiteral("thoughtSignature")));
  EXPECT_EQ(part.value(QStringLiteral("thoughtSignature")).toString(), QStringLiteral("sig-xyz"));

  auto fakeNoSignature = makeFakeWithEmptyModels();
  GoogleProvider providerNoSignature(fakeNoSignature);
  Message invocationNoSignature(MessageId::generate(), MessageRole::Assistant, QString());
  invocationNoSignature.setToolCalls({ToolCallEntry{.kind = ToolCallKind::Invocation,
                                                    .tool_use_id = QStringLiteral("call_456"),
                                                    .tool_name = QStringLiteral("ListFiles")}});
  providerNoSignature.sendChat(testModel(), {invocationNoSignature}, [](const StreamEvent&) {});

  const QJsonObject bodyNoSignature = QJsonDocument::fromJson(fakeNoSignature->streamingCall(0).request.body).object();
  const QJsonObject partNoSignature = bodyNoSignature.value(QStringLiteral("contents"))
                                          .toArray()
                                          .at(0)
                                          .toObject()
                                          .value(QStringLiteral("parts"))
                                          .toArray()
                                          .at(0)
                                          .toObject();
  EXPECT_FALSE(partNoSignature.contains(QStringLiteral("thoughtSignature")));
}

TEST(GoogleProvider, SendChatOmitsIdInFunctionResponseWhenIdWasSynthesized) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);

  Message invocation(MessageId::generate(), MessageRole::Assistant, QString());
  invocation.setToolCalls({ToolCallEntry{.kind = ToolCallKind::Invocation,
                                         .tool_use_id = QStringLiteral("f47ac10b-58cc-4372-a567-0e02b2c3d479"),
                                         .tool_name = QStringLiteral("ListFiles"),
                                         .provider_call_id_synthesized = true}});
  Message toolResult(MessageId::generate(), MessageRole::User, QString());
  toolResult.setToolCalls({ToolCallEntry{.kind = ToolCallKind::Result,
                                         .tool_use_id = QStringLiteral("f47ac10b-58cc-4372-a567-0e02b2c3d479"),
                                         .tool_name = QStringLiteral("ListFiles"),
                                         .provider_call_id_synthesized = true}});

  provider.sendChat(testModel(), {invocation, toolResult}, [](const StreamEvent&) {});

  const QJsonObject body = QJsonDocument::fromJson(fake->streamingCall(0).request.body).object();
  const QJsonArray contents = body.value(QStringLiteral("contents")).toArray();
  ASSERT_EQ(contents.size(), 2);
  const QJsonObject functionCallObj = contents.at(0)
                                          .toObject()
                                          .value(QStringLiteral("parts"))
                                          .toArray()
                                          .at(0)
                                          .toObject()
                                          .value(QStringLiteral("functionCall"))
                                          .toObject();
  EXPECT_FALSE(functionCallObj.contains(QStringLiteral("id")));
  const QJsonObject functionResponseObj = contents.at(1)
                                              .toObject()
                                              .value(QStringLiteral("parts"))
                                              .toArray()
                                              .at(0)
                                              .toObject()
                                              .value(QStringLiteral("functionResponse"))
                                              .toObject();
  EXPECT_FALSE(functionResponseObj.contains(QStringLiteral("id")));
}

TEST(GoogleProvider, SendChatIncludesIdInFunctionResponseWhenIdWasFromModel) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);

  Message invocation(MessageId::generate(), MessageRole::Assistant, QString());
  invocation.setToolCalls({ToolCallEntry{.kind = ToolCallKind::Invocation,
                                         .tool_use_id = QStringLiteral("call_123"),
                                         .tool_name = QStringLiteral("ListFiles"),
                                         .provider_call_id_synthesized = false}});
  Message toolResult(MessageId::generate(), MessageRole::User, QString());
  toolResult.setToolCalls({ToolCallEntry{.kind = ToolCallKind::Result,
                                         .tool_use_id = QStringLiteral("call_123"),
                                         .tool_name = QStringLiteral("ListFiles"),
                                         .provider_call_id_synthesized = false}});

  provider.sendChat(testModel(), {invocation, toolResult}, [](const StreamEvent&) {});

  const QJsonObject body = QJsonDocument::fromJson(fake->streamingCall(0).request.body).object();
  const QJsonArray contents = body.value(QStringLiteral("contents")).toArray();
  ASSERT_EQ(contents.size(), 2);
  const QJsonObject functionCallObj = contents.at(0)
                                          .toObject()
                                          .value(QStringLiteral("parts"))
                                          .toArray()
                                          .at(0)
                                          .toObject()
                                          .value(QStringLiteral("functionCall"))
                                          .toObject();
  EXPECT_EQ(functionCallObj.value(QStringLiteral("id")).toString(), QStringLiteral("call_123"));
  const QJsonObject functionResponseObj = contents.at(1)
                                              .toObject()
                                              .value(QStringLiteral("parts"))
                                              .toArray()
                                              .at(0)
                                              .toObject()
                                              .value(QStringLiteral("functionResponse"))
                                              .toObject();
  EXPECT_EQ(functionResponseObj.value(QStringLiteral("id")).toString(), QStringLiteral("call_123"));
}

// Parallel calls from the same turn land in one Content entry's parts array, not one Content entry
// per Message (§3.4/§9.3 of DESIGN.md) -- mirrors how ChatController actually appends one Message
// per ToolCallEntry (consecutive same-role Messages), not one Message per turn.
TEST(GoogleProvider, SendChatGroupsParallelFunctionCallsIntoOneContentEntry) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);

  Message invocationA(MessageId::generate(), MessageRole::Assistant, QString());
  invocationA.setToolCalls({ToolCallEntry{.kind = ToolCallKind::Invocation,
                                          .tool_use_id = QStringLiteral("call_a"),
                                          .tool_name = QStringLiteral("check_weather")}});
  Message invocationB(MessageId::generate(), MessageRole::Assistant, QString());
  invocationB.setToolCalls({ToolCallEntry{.kind = ToolCallKind::Invocation,
                                          .tool_use_id = QStringLiteral("call_b"),
                                          .tool_name = QStringLiteral("check_weather")}});
  Message resultA(MessageId::generate(), MessageRole::User, QString());
  resultA.setToolCalls({ToolCallEntry{.kind = ToolCallKind::Result,
                                      .tool_use_id = QStringLiteral("call_a"),
                                      .tool_name = QStringLiteral("check_weather")}});
  Message resultB(MessageId::generate(), MessageRole::User, QString());
  resultB.setToolCalls({ToolCallEntry{.kind = ToolCallKind::Result,
                                      .tool_use_id = QStringLiteral("call_b"),
                                      .tool_name = QStringLiteral("check_weather")}});

  provider.sendChat(testModel(), {invocationA, invocationB, resultA, resultB}, [](const StreamEvent&) {});

  const QJsonObject body = QJsonDocument::fromJson(fake->streamingCall(0).request.body).object();
  const QJsonArray contents = body.value(QStringLiteral("contents")).toArray();
  ASSERT_EQ(contents.size(), 2);  // grouped, not four separate Content entries
  EXPECT_EQ(contents.at(0).toObject().value(QStringLiteral("parts")).toArray().size(), 2);
  EXPECT_EQ(contents.at(1).toObject().value(QStringLiteral("parts")).toArray().size(), 2);
}

// Safety/recitation error handling (REQ-F-016/024).
TEST(GoogleProvider, SendChatEmitsDistinctErrorForSafetyFinishReason) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0, QByteArray(R"(data: {"candidates":[{"finishReason":"SAFETY"}]})") + "\n\n");

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<Error>(events[0]));
  EXPECT_EQ(std::get<Error>(events[0]).message, QStringLiteral("Response blocked by Google's safety filters."));
}

TEST(GoogleProvider, SendChatEmitsDistinctErrorForRecitationFinishReason) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0, QByteArray(R"(data: {"candidates":[{"finishReason":"RECITATION"}]})") + "\n\n");

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<Error>(events[0]));
  EXPECT_EQ(std::get<Error>(events[0]).message, QStringLiteral("Response blocked due to recitation concerns."));
}

TEST(GoogleProvider, SendChatReportsPromptFeedbackWhenPromptIsBlocked) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0, QByteArray(R"(data: {"promptFeedback":{"blockReason":"PROHIBITED_CONTENT"}})") + "\n\n");

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<Error>(events[0]));
  EXPECT_EQ(std::get<Error>(events[0]).message,
            QStringLiteral("Prompt blocked because it contains prohibited content."));
}

TEST(GoogleProvider, SendChatReportsMaxTokensAsTruncationError) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0, QByteArray(R"(data: {"candidates":[{"finishReason":"MAX_TOKENS"}]})") + "\n\n");

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<Error>(events[0]));
  EXPECT_EQ(std::get<Error>(events[0]).message,
            QStringLiteral("Response stopped after reaching the maximum output-token limit."));
}

TEST(GoogleProvider, SendChatReportsOtherFailureFinishReasonsAsErrors) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0, QByteArray(R"(data: {"candidates":[{"finishReason":"MALFORMED_FUNCTION_CALL"}]})") + "\n\n");

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<Error>(events[0]));
  EXPECT_EQ(std::get<Error>(events[0]).message,
            QStringLiteral("Google stopped the response: MALFORMED_FUNCTION_CALL."));
}

// Usage accumulation across streamed chunks (REQ-F-006, REQ-F-002). Gemini reports usageMetadata
// cumulative-to-date on every chunk (not a delta), so the last chunk received before the terminal
// finishReason must win — unlike Anthropic where two distinct fields combine.
TEST(GoogleProvider, SendChatFinalChunkWinsForUsageMetadata) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  const QByteArray payload =
      QByteArray(R"(data: {"candidates":[{"content":{"role":"model","parts":[{"text":"Hi"}]}}],)"
                 R"("usageMetadata":{"promptTokenCount":10,"candidatesTokenCount":1,"totalTokenCount":11}})") +
      "\n\n" +
      QByteArray(
          R"(data: {"candidates":[{"finishReason":"STOP"}],"usageMetadata":{"promptTokenCount":10,)"
          R"("candidatesTokenCount":25,"thoughtsTokenCount":8,"cachedContentTokenCount":3,"totalTokenCount":38}})") +
      "\n\n";
  fake->emitData(0, payload);

  ASSERT_EQ(events.size(), 2U);
  ASSERT_TRUE(std::holds_alternative<Completed>(events[1]));
  const std::optional<Usage> usage = std::get<Completed>(events[1]).usage;
  ASSERT_TRUE(usage.has_value());
  EXPECT_EQ(usage->input_tokens, 10);
  EXPECT_EQ(usage->output_tokens, 25);
  EXPECT_EQ(usage->reasoning_tokens, 8);
  EXPECT_EQ(usage->cache_read_tokens, 3);
  EXPECT_EQ(usage->total_tokens, 38);
}

TEST(GoogleProvider, SendChatLeavesReasoningAndCacheTokensNullWhenNotReportedByGoogle) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0, QByteArray(R"(data: {"candidates":[{"finishReason":"STOP"}],)"
                               R"("usageMetadata":{"promptTokenCount":10,"candidatesTokenCount":4,)"
                               R"("totalTokenCount":14}})") +
                        "\n\n");

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<Completed>(events[0]));
  const std::optional<Usage> usage = std::get<Completed>(events[0]).usage;
  ASSERT_TRUE(usage.has_value());
  EXPECT_EQ(usage->input_tokens, 10);
  EXPECT_EQ(usage->output_tokens, 4);
  EXPECT_FALSE(usage->reasoning_tokens.has_value());
  EXPECT_FALSE(usage->cache_read_tokens.has_value());
}

// Error handling (REQ-F-018, REQ-F-020-023).
TEST(GoogleProvider, SendChatTreatsMalformedJsonAsFatalError) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0, QByteArray("data: not-json") + "\n\n");

  ASSERT_EQ(events.size(), 1U);
  EXPECT_TRUE(std::holds_alternative<Error>(events[0]));
}

TEST(GoogleProvider, SendChatRoutesInStreamErrorObjectToError) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(
      0, QByteArray(R"(data: {"error":{"code":429,"message":"Resource exhausted","status":"RESOURCE_EXHAUSTED"}})") +
             "\n\n");

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<Error>(events[0]));
  EXPECT_EQ(std::get<Error>(events[0]).message, QStringLiteral("Resource exhausted"));
}

TEST(GoogleProvider, SendChatTreatsStreamEndBeforeFinishReasonAsError) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitFinished(0);

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<Error>(events[0]));
  EXPECT_EQ(std::get<Error>(events[0]).message, QStringLiteral("Stream ended without completion"));
}

TEST(GoogleProvider, SendChatRecoversErrorMessageFromBufferedJsonBodyOnHttpError) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0, QByteArray(R"({"error":{"code":401,"message":"API key not valid","status":"UNAUTHENTICATED"}})"));
  fake->emitError(0, QStringLiteral("Request failed with HTTP status 401"));

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<Error>(events[0]));
  EXPECT_EQ(std::get<Error>(events[0]).message, QStringLiteral("API key not valid"));
}

TEST(GoogleProvider, SendChatFallsBackToTransportMessageWhenBufferIsNotJson) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitError(0, QStringLiteral("Connection refused"));

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<Error>(events[0]));
  EXPECT_EQ(std::get<Error>(events[0]).message, QStringLiteral("Connection refused"));
}

// Cancellation via HttpRequestHandle (REQ-F-025/026).
TEST(GoogleProvider, CancellingHandleStopsFurtherDeltaProcessing) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);

  std::vector<StreamEvent> events;
  const HttpRequestHandlePtr handle =
      provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0,
                 QByteArray(R"(data: {"candidates":[{"content":{"role":"model","parts":[{"text":"Hi"}]}}]})") + "\n\n");
  ASSERT_EQ(events.size(), 1U);

  handle->cancel();
  EXPECT_TRUE(fake->streamingCall(0).cancelled);
}

TEST(GoogleProvider, SendChatCancelsRequestAfterMalformedJson) {
  auto fake = makeFakeWithEmptyModels();
  GoogleProvider provider(fake);

  std::vector<StreamEvent> events;
  provider.sendChat(testModel(), {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0, QByteArray("data: not-json") + "\n\n");

  EXPECT_TRUE(fake->streamingCall(0).cancelled);
  ASSERT_EQ(events.size(), 1U);
  EXPECT_TRUE(std::holds_alternative<Error>(events[0]));
}

}  // namespace
}  // namespace holonight_providers
