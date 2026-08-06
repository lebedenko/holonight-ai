#include "fake_http_client.h"
#include "holonight_providers/ollama_provider.h"

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
using holonight_domain::Usage;

std::shared_ptr<FakeHttpClient> makeFakeWithEmptyTags() {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedSuccess(QByteArray(R"({"models":[]})"));
  return fake;
}

TEST(OllamaProvider, RefreshFetchesModelList) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedSuccess(QByteArray(R"({"models":[{"name":"llama3:latest"}]})"));

  OllamaProvider provider(fake);
  provider.refresh();

  ASSERT_EQ(provider.availableModels().size(), 1U);
  EXPECT_EQ(provider.availableModels()[0].provider_id, QStringLiteral("ollama"));
  EXPECT_EQ(provider.availableModels()[0].model_name, QStringLiteral("llama3:latest"));
}

TEST(OllamaProvider, RefreshUsesConfiguredInstanceId) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedSuccess(QByteArray(R"({"models":[{"name":"llama3:latest"}]})"));
  OllamaProvider provider(fake, QStringLiteral("http://localhost:11434"), QStringLiteral("local-work"));

  provider.refresh();

  ASSERT_EQ(provider.availableModels().size(), 1U);
  EXPECT_EQ(provider.availableModels()[0].provider_id, QStringLiteral("local-work"));
}

TEST(OllamaProvider, AvailableModelsIsCachedAcrossCalls) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedSuccess(QByteArray(R"({"models":[{"name":"llama3:latest"}]})"));
  OllamaProvider provider(fake);
  provider.refresh();

  const auto& first = provider.availableModels();
  const auto& second = provider.availableModels();
  EXPECT_EQ(first, second);
}

TEST(OllamaProvider, RefreshFailureLeavesModelListEmpty) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedError(QStringLiteral("connection refused"));

  OllamaProvider provider(fake);
  provider.refresh();

  EXPECT_TRUE(provider.availableModels().empty());
}

TEST(OllamaProvider, RefreshRejectsMalformedSuccessAndPreservesCachedModels) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedSuccess(QByteArray(R"({"models":[{"name":"llama3:latest"}]})"));
  fake->enqueueBufferedSuccess(QByteArray(R"({"unexpected":[]})"));
  OllamaProvider provider(fake);
  provider.refresh();

  bool successCalled = false;
  QString errorReason;
  provider.refresh([&successCalled] { successCalled = true; },
                   [&errorReason](const QString& reason) { errorReason = reason; });

  EXPECT_FALSE(successCalled);
  EXPECT_EQ(errorReason, QStringLiteral("Malformed model list response from Ollama"));
  ASSERT_EQ(provider.availableModels().size(), 1U);
  EXPECT_EQ(provider.availableModels().front().model_name, QStringLiteral("llama3:latest"));
}

TEST(OllamaProvider, RefreshReplacesModelList) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedSuccess(QByteArray(R"({"models":[{"name":"llama3:latest"}]})"));
  OllamaProvider provider(fake);
  provider.refresh();

  fake->enqueueBufferedSuccess(QByteArray(R"({"models":[{"name":"mistral:latest"}]})"));
  bool completed = false;
  provider.refresh([&completed]() { completed = true; });

  EXPECT_TRUE(completed);
  ASSERT_EQ(provider.availableModels().size(), 1U);
  EXPECT_EQ(provider.availableModels()[0].model_name, QStringLiteral("mistral:latest"));
}

TEST(OllamaProvider, RefreshSucceedsEvenIfInitialFetchFailed) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedError(QStringLiteral("connection refused"));
  OllamaProvider provider(fake);
  provider.refresh();
  ASSERT_TRUE(provider.availableModels().empty());

  fake->enqueueBufferedSuccess(QByteArray(R"({"models":[{"name":"llama3:latest"}]})"));
  provider.refresh();

  ASSERT_EQ(provider.availableModels().size(), 1U);
}

TEST(OllamaProvider, RefreshUsesBoundedTimeout) {
  auto fake = makeFakeWithEmptyTags();
  OllamaProvider provider(fake);

  provider.refresh();

  EXPECT_EQ(fake->lastBufferedTimeout(), std::chrono::seconds{10});
}

TEST(OllamaProvider, SendChatParsesMultiLineNdjson) {
  auto fake = makeFakeWithEmptyTags();
  OllamaProvider provider(fake);

  std::vector<StreamEvent> events;
  const ModelId model{.provider_id = QStringLiteral("ollama"), .model_name = QStringLiteral("llama3")};
  const std::vector<Message> history{Message(MessageId::generate(), MessageRole::User, QString("hi"))};

  provider.sendChat(model, history, [&events](const StreamEvent& event) { events.push_back(event); });

  ASSERT_EQ(fake->streamingCallCount(), 1U);
  const QByteArray payload = R"({"model":"llama3","message":{"role":"assistant","content":"Hi"},"done":false})"
                             "\n"
                             R"({"model":"llama3","message":{"role":"assistant","content":" there"},"done":false})"
                             "\n"
                             R"({"model":"llama3","message":{"role":"assistant","content":""},"done":true})"
                             "\n";
  fake->emitData(0, payload);
  fake->emitFinished(0);

  ASSERT_EQ(events.size(), 4U);
  ASSERT_TRUE(std::holds_alternative<ContentDelta>(events[0]));
  EXPECT_EQ(std::get<ContentDelta>(events[0]).text, QStringLiteral("Hi"));
  ASSERT_TRUE(std::holds_alternative<ContentDelta>(events[1]));
  EXPECT_EQ(std::get<ContentDelta>(events[1]).text, QStringLiteral(" there"));
  ASSERT_TRUE(std::holds_alternative<ContentDelta>(events[2]));
  EXPECT_TRUE(std::holds_alternative<Completed>(events[3]));
}

TEST(OllamaProvider, SendChatHandlesChunkBoundarySplitAcrossCalls) {
  auto fake = makeFakeWithEmptyTags();
  OllamaProvider provider(fake);

  std::vector<StreamEvent> events;
  const ModelId model{.provider_id = QStringLiteral("ollama"), .model_name = QStringLiteral("llama3")};
  const std::vector<Message> history{Message(MessageId::generate(), MessageRole::User, QString("hi"))};
  provider.sendChat(model, history, [&events](const StreamEvent& event) { events.push_back(event); });

  const QByteArray fullLine =
      QByteArray(R"({"model":"llama3","message":{"role":"assistant","content":"Hi"},"done":false})") + "\n";
  fake->emitData(0, fullLine.left(20));
  fake->emitData(0, fullLine.mid(20));

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<ContentDelta>(events[0]));
  EXPECT_EQ(std::get<ContentDelta>(events[0]).text, QStringLiteral("Hi"));
}

TEST(OllamaProvider, SendChatTreatsMalformedJsonAsFatalError) {
  auto fake = makeFakeWithEmptyTags();
  OllamaProvider provider(fake);

  std::vector<StreamEvent> events;
  const ModelId model{.provider_id = QStringLiteral("ollama"), .model_name = QStringLiteral("llama3")};
  const std::vector<Message> history{Message(MessageId::generate(), MessageRole::User, QString("hi"))};
  provider.sendChat(model, history, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0, QByteArray("not-json\n"));

  ASSERT_EQ(events.size(), 1U);
  EXPECT_TRUE(std::holds_alternative<Error>(events[0]));
}

TEST(OllamaProvider, SendChatMapsErrorFieldToStreamError) {
  auto fake = makeFakeWithEmptyTags();
  OllamaProvider provider(fake);

  std::vector<StreamEvent> events;
  const ModelId model{.provider_id = QStringLiteral("ollama"), .model_name = QStringLiteral("llama3")};
  const std::vector<Message> history{Message(MessageId::generate(), MessageRole::User, QString("hi"))};
  provider.sendChat(model, history, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0, QByteArray(R"({"error":"model runner exited"})") + "\n");

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<Error>(events[0]));
  EXPECT_EQ(std::get<Error>(events[0]).message, QStringLiteral("model runner exited"));
}

TEST(OllamaProvider, SendChatMapsTransportErrorToStreamError) {
  auto fake = makeFakeWithEmptyTags();
  OllamaProvider provider(fake);

  std::vector<StreamEvent> events;
  const ModelId model{.provider_id = QStringLiteral("ollama"), .model_name = QStringLiteral("llama3")};
  const std::vector<Message> history{Message(MessageId::generate(), MessageRole::User, QString("hi"))};
  provider.sendChat(model, history, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitError(0, QStringLiteral("Connection refused"));

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<Error>(events[0]));
  EXPECT_EQ(std::get<Error>(events[0]).message, QStringLiteral("Connection refused"));
}

TEST(OllamaProvider, SendChatTreatsTransportFinishBeforeDoneAsError) {
  auto fake = makeFakeWithEmptyTags();
  OllamaProvider provider(fake);

  std::vector<StreamEvent> events;
  const ModelId model{.provider_id = QStringLiteral("ollama"), .model_name = QStringLiteral("llama3")};
  provider.sendChat(model, {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitFinished(0);

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<Error>(events[0]));
  EXPECT_EQ(std::get<Error>(events[0]).message, QStringLiteral("Ollama response ended before completion"));
}

TEST(OllamaProvider, SendChatCancelsRequestAfterMalformedJson) {
  auto fake = makeFakeWithEmptyTags();
  OllamaProvider provider(fake);

  std::vector<StreamEvent> events;
  const ModelId model{.provider_id = QStringLiteral("ollama"), .model_name = QStringLiteral("llama3")};
  provider.sendChat(model, {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0, QByteArray("not-json\n"));
  fake->emitFinished(0);

  EXPECT_TRUE(fake->streamingCall(0).cancelled);
  ASSERT_EQ(events.size(), 1U);
  EXPECT_TRUE(std::holds_alternative<Error>(events[0]));
}

TEST(OllamaProvider, SendChatAcceptsFinalNdjsonLineWithoutTrailingNewline) {
  auto fake = makeFakeWithEmptyTags();
  OllamaProvider provider(fake);

  std::vector<StreamEvent> events;
  const ModelId model{.provider_id = QStringLiteral("ollama"), .model_name = QStringLiteral("llama3")};
  provider.sendChat(model, {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0, QByteArray(R"({"message":{"content":"Hi"},"done":true})"));
  fake->emitFinished(0);

  ASSERT_EQ(events.size(), 2U);
  EXPECT_TRUE(std::holds_alternative<ContentDelta>(events[0]));
  EXPECT_TRUE(std::holds_alternative<Completed>(events[1]));
}

// T-007: usage accumulation from the done:true chunk (REQ-F-003, REQ-F-002, REQ-F-012).
TEST(OllamaProvider, SendChatPopulatesUsageFromDone) {
  auto fake = makeFakeWithEmptyTags();
  OllamaProvider provider(fake);

  std::vector<StreamEvent> events;
  const ModelId model{.provider_id = QStringLiteral("ollama"), .model_name = QStringLiteral("llama3")};
  provider.sendChat(model, {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0, QByteArray(R"({"message":{"content":"Hi"},"done":false})") + "\n" +
                        R"({"message":{"content":""},"done":true,)"
                        R"("prompt_eval_count":42,"eval_count":18,)"
                        R"("total_duration":5000000000,"load_duration":100000000,)"
                        R"("prompt_eval_duration":200000000,"eval_duration":300000000})" +
                        "\n");

  ASSERT_EQ(events.size(), 3U);
  ASSERT_TRUE(std::holds_alternative<Completed>(events[2]));
  const Completed& completed = std::get<Completed>(events[2]);
  ASSERT_TRUE(completed.usage.has_value());
  const Usage& usage = *completed.usage;
  EXPECT_EQ(usage.input_tokens, 42);
  EXPECT_EQ(usage.output_tokens, 18);
  EXPECT_EQ(usage.total_tokens, 60);
  EXPECT_EQ(usage.ollama_total_duration_ns, 5000000000);
  EXPECT_EQ(usage.ollama_load_duration_ns, 100000000);
  EXPECT_EQ(usage.ollama_prompt_eval_duration_ns, 200000000);
  EXPECT_EQ(usage.ollama_eval_duration_ns, 300000000);
}

TEST(OllamaProvider, MissingTokenCountLeavesComputedTotalUnset) {
  auto fake = makeFakeWithEmptyTags();
  OllamaProvider provider(fake);
  const ModelId model{.provider_id = QStringLiteral("ollama"), .model_name = QStringLiteral("llama3")};

  std::vector<StreamEvent> events;
  provider.sendChat(model, {}, [&events](const StreamEvent& event) { events.push_back(event); });
  fake->emitData(
      0, QByteArray(R"({"model":"llama3","message":{"content":""},"done":true,"prompt_eval_count":42})") + "\n");

  ASSERT_EQ(events.size(), 2U);
  ASSERT_TRUE(std::holds_alternative<Completed>(events.back()));
  const auto& usage = std::get<Completed>(events.back()).usage;
  ASSERT_TRUE(usage.has_value());
  EXPECT_EQ(usage->input_tokens, 42);
  EXPECT_FALSE(usage->output_tokens.has_value());
  EXPECT_FALSE(usage->total_tokens.has_value());
}

TEST(OllamaProvider, SendChatLeavesReasoningTokensNullWhenNotReportedByOllama) {
  auto fake = makeFakeWithEmptyTags();
  OllamaProvider provider(fake);

  std::vector<StreamEvent> events;
  const ModelId model{.provider_id = QStringLiteral("ollama"), .model_name = QStringLiteral("llama3")};
  provider.sendChat(model, {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0, QByteArray(R"({"message":{"content":"Hi"},"done":true,)"
                               R"("prompt_eval_count":10,"eval_count":5})") +
                        "\n");

  ASSERT_EQ(events.size(), 2U);
  ASSERT_TRUE(std::holds_alternative<Completed>(events[1]));
  const Completed& completed = std::get<Completed>(events[1]);
  ASSERT_TRUE(completed.usage.has_value());
  EXPECT_FALSE(completed.usage->reasoning_tokens.has_value());
  EXPECT_FALSE(completed.usage->cache_creation_tokens.has_value());
  EXPECT_FALSE(completed.usage->cache_read_tokens.has_value());
  EXPECT_FALSE(completed.usage->ollama_total_duration_ns.has_value());
}

// T-009: setters, auth headers, and the two-callback refresh() overload (REQ-F-007, REQ-F-010,
// REQ-F-011, REQ-NF-005).
TEST(OllamaProvider, SetBaseUrlAffectsOnlySubsequentRefreshCalls) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedSuccess(QByteArray(R"({"models":[]})"));
  fake->setBufferedResponsesDeferred(true);
  OllamaProvider provider(fake);

  provider.refresh();
  ASSERT_EQ(fake->lastBufferedRequest().url, QStringLiteral("http://localhost:11434/api/tags"));

  provider.setBaseUrl(QStringLiteral("http://otherhost:9999"));

  fake->enqueueBufferedSuccess(QByteArray(R"({"models":[]})"));
  provider.refresh();

  EXPECT_EQ(fake->lastBufferedRequest().url, QStringLiteral("http://otherhost:9999/api/tags"));
}

TEST(OllamaProvider, SetAuthTokenAddsBearerHeaderToFetchModelList) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedSuccess(QByteArray(R"({"models":[]})"));
  OllamaProvider provider(fake);
  provider.setAuthToken(QStringLiteral("secret-token"));

  provider.refresh();

  EXPECT_EQ(fake->lastBufferedRequest().headers.value(QStringLiteral("Authorization")),
            QStringLiteral("Bearer secret-token"));
}

TEST(OllamaProvider, EmptyAuthTokenOmitsAuthorizationHeader) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedSuccess(QByteArray(R"({"models":[]})"));
  OllamaProvider provider(fake);

  provider.refresh();

  EXPECT_FALSE(fake->lastBufferedRequest().headers.contains(QStringLiteral("Authorization")));
}

TEST(OllamaProvider, SetAuthTokenAddsBearerHeaderToSendChat) {
  auto fake = makeFakeWithEmptyTags();
  OllamaProvider provider(fake);
  provider.setAuthToken(QStringLiteral("secret-token"));

  const ModelId model{.provider_id = QStringLiteral("ollama"), .model_name = QStringLiteral("llama3")};
  provider.sendChat(model, {}, [](const StreamEvent&) {});

  ASSERT_EQ(fake->streamingCallCount(), 1U);
  EXPECT_EQ(fake->streamingCall(0).request.headers.value(QStringLiteral("Authorization")),
            QStringLiteral("Bearer secret-token"));
}

TEST(OllamaProvider, SendChatIncludesTemperatureAndContextWindowInOptions) {
  auto fake = makeFakeWithEmptyTags();
  OllamaProvider provider(fake);
  provider.setTemperature(1.5);
  provider.setContextWindow(8192);

  const ModelId model{.provider_id = QStringLiteral("ollama"), .model_name = QStringLiteral("llama3")};
  provider.sendChat(model, {}, [](const StreamEvent&) {});

  ASSERT_EQ(fake->streamingCallCount(), 1U);
  const QJsonObject body = QJsonDocument::fromJson(fake->streamingCall(0).request.body).object();
  const QJsonObject options = body.value(QStringLiteral("options")).toObject();
  EXPECT_DOUBLE_EQ(options.value(QStringLiteral("temperature")).toDouble(), 1.5);
  EXPECT_EQ(options.value(QStringLiteral("num_ctx")).toInt(), 8192);
}

TEST(OllamaProvider, SendChatDefaultsMatchOllamaProviderConfigDefaults) {
  auto fake = makeFakeWithEmptyTags();
  OllamaProvider provider(fake);

  const ModelId model{.provider_id = QStringLiteral("ollama"), .model_name = QStringLiteral("llama3")};
  provider.sendChat(model, {}, [](const StreamEvent&) {});

  const QJsonObject body = QJsonDocument::fromJson(fake->streamingCall(0).request.body).object();
  const QJsonObject options = body.value(QStringLiteral("options")).toObject();
  EXPECT_DOUBLE_EQ(options.value(QStringLiteral("temperature")).toDouble(), 0.7);
  EXPECT_EQ(options.value(QStringLiteral("num_ctx")).toInt(), 4096);
}

TEST(OllamaProvider, RefreshTwoCallbackOverloadCallsOnSuccessOnHttpSuccess) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedSuccess(QByteArray(R"({"models":[{"name":"llama3"}]})"));
  OllamaProvider provider(fake);

  bool successCalled = false;
  bool errorCalled = false;
  provider.refresh([&successCalled] { successCalled = true; }, [&errorCalled](const QString&) { errorCalled = true; });

  EXPECT_TRUE(successCalled);
  EXPECT_FALSE(errorCalled);
}

TEST(OllamaProvider, RefreshTwoCallbackOverloadCallsOnErrorWithReasonOnHttpFailure) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedError(QStringLiteral("connection refused"));
  OllamaProvider provider(fake);

  bool successCalled = false;
  QString errorReason;
  provider.refresh([&successCalled] { successCalled = true; },
                   [&errorReason](const QString& reason) { errorReason = reason; });

  EXPECT_FALSE(successCalled);
  EXPECT_EQ(errorReason, QStringLiteral("connection refused"));
}

TEST(OllamaProvider, RefreshTwoCallbackOverloadTreatsEmptyModelsArrayAsSuccess) {
  auto fake = std::make_shared<FakeHttpClient>();
  fake->enqueueBufferedSuccess(QByteArray(R"({"models":[]})"));
  OllamaProvider provider(fake);

  bool successCalled = false;
  provider.refresh([&successCalled] { successCalled = true; }, [](const QString&) {});

  EXPECT_TRUE(successCalled);
  EXPECT_TRUE(provider.availableModels().empty());
}

// T-009: tool-calling wiring (REQ-F-002, REQ-F-003, REQ-F-004, REQ-NF-002).
TEST(OllamaProvider, SendChatIncludesToolsArrayInRequestBodyWhenProvided) {
  auto fake = makeFakeWithEmptyTags();
  OllamaProvider provider(fake);

  const ModelId model{.provider_id = QStringLiteral("ollama"), .model_name = QStringLiteral("llama3")};
  const holonight_domain::ToolCatalogSnapshot catalog{
      .client_tools = {{
          .function_name = QStringLiteral("list_files"),
          .description = QStringLiteral("List files"),
          .input_schema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}},
      }}};
  provider.sendChat(model, {}, [](const StreamEvent&) {}, std::chrono::seconds{30}, catalog);

  ASSERT_EQ(fake->streamingCallCount(), 1U);
  const QJsonObject body = QJsonDocument::fromJson(fake->streamingCall(0).request.body).object();
  ASSERT_TRUE(body.value(QStringLiteral("tools")).isArray());
  const QJsonArray tools = body.value(QStringLiteral("tools")).toArray();
  ASSERT_EQ(tools.size(), 1);
  EXPECT_EQ(tools.at(0).toObject().value(QStringLiteral("type")), QStringLiteral("function"));
  EXPECT_EQ(tools.at(0).toObject().value(QStringLiteral("function")).toObject().value(QStringLiteral("name")),
            QStringLiteral("list_files"));
}

TEST(OllamaProvider, SendChatOmitsToolsKeyWhenCatalogEmpty) {
  auto fake = makeFakeWithEmptyTags();
  OllamaProvider provider(fake);

  const ModelId model{.provider_id = QStringLiteral("ollama"), .model_name = QStringLiteral("llama3")};
  provider.sendChat(model, {}, [](const StreamEvent&) {});

  ASSERT_EQ(fake->streamingCallCount(), 1U);
  const QJsonObject body = QJsonDocument::fromJson(fake->streamingCall(0).request.body).object();
  EXPECT_FALSE(body.contains(QStringLiteral("tools")));
}

TEST(OllamaProvider, SendChatEmitsToolRequestEventOnSingleToolCallLine) {
  auto fake = makeFakeWithEmptyTags();
  OllamaProvider provider(fake);

  std::vector<StreamEvent> events;
  const ModelId model{.provider_id = QStringLiteral("ollama"), .model_name = QStringLiteral("llama3")};
  provider.sendChat(model, {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0, QByteArray(R"({"message":{"content":"","tool_calls":[)"
                               R"({"function":{"name":"list_files","arguments":{"path":"~"}}}]},"done":false})") +
                        "\n");

  ASSERT_EQ(events.size(), 2U);
  EXPECT_TRUE(std::holds_alternative<ContentDelta>(events[0]));
  ASSERT_TRUE(std::holds_alternative<holonight_domain::ToolRequestEvent>(events[1]));
  const auto& toolRequest = std::get<holonight_domain::ToolRequestEvent>(events[1]);
  EXPECT_EQ(toolRequest.function_name, QStringLiteral("list_files"));
  EXPECT_EQ(toolRequest.arguments.value(QStringLiteral("path")), QStringLiteral("~"));
  EXPECT_EQ(toolRequest.execution_location, holonight_domain::ToolExecutionLocation::LocalClient);
  EXPECT_EQ(toolRequest.source, holonight_domain::ToolSource::BuiltIn);
}

TEST(OllamaProvider, SendChatEmitsMultipleToolRequestEventsForMultipleToolCallsInOrder) {
  auto fake = makeFakeWithEmptyTags();
  OllamaProvider provider(fake);

  std::vector<StreamEvent> events;
  const ModelId model{.provider_id = QStringLiteral("ollama"), .model_name = QStringLiteral("llama3")};
  provider.sendChat(model, {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0, QByteArray(R"({"message":{"content":"","tool_calls":[)"
                               R"({"function":{"name":"list_files","arguments":{"path":"a"}}},)"
                               R"({"function":{"name":"list_files","arguments":{"path":"b"}}}]},"done":false})") +
                        "\n");

  ASSERT_EQ(events.size(), 3U);
  ASSERT_TRUE(std::holds_alternative<holonight_domain::ToolRequestEvent>(events[1]));
  ASSERT_TRUE(std::holds_alternative<holonight_domain::ToolRequestEvent>(events[2]));
  EXPECT_EQ(std::get<holonight_domain::ToolRequestEvent>(events[1]).arguments.value(QStringLiteral("path")),
            QStringLiteral("a"));
  EXPECT_EQ(std::get<holonight_domain::ToolRequestEvent>(events[2]).arguments.value(QStringLiteral("path")),
            QStringLiteral("b"));
}

TEST(OllamaProvider, SendChatFailsStreamOnMalformedToolCallArguments) {
  auto fake = makeFakeWithEmptyTags();
  OllamaProvider provider(fake);

  std::vector<StreamEvent> events;
  const ModelId model{.provider_id = QStringLiteral("ollama"), .model_name = QStringLiteral("llama3")};
  provider.sendChat(model, {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0, QByteArray(R"({"message":{"content":"","tool_calls":[)"
                               R"({"function":{"name":"list_files","arguments":"not an object"}}]},"done":false})") +
                        "\n");

  ASSERT_EQ(events.size(), 2U);
  EXPECT_TRUE(std::holds_alternative<ContentDelta>(events[0]));
  EXPECT_TRUE(std::holds_alternative<Error>(events[1]));
}

TEST(OllamaProvider, SendChatFailsStreamWhenToolCallsIsNotAnArray) {
  auto fake = makeFakeWithEmptyTags();
  OllamaProvider provider(fake);

  std::vector<StreamEvent> events;
  const ModelId model{.provider_id = QStringLiteral("ollama"), .model_name = QStringLiteral("llama3")};
  provider.sendChat(model, {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0, QByteArray(R"({"message":{"content":"","tool_calls":{}} ,"done":false})") + "\n");

  ASSERT_EQ(events.size(), 2U);
  EXPECT_TRUE(std::holds_alternative<ContentDelta>(events[0]));
  EXPECT_TRUE(std::holds_alternative<Error>(events[1]));
}

TEST(OllamaProvider, SendChatContinuesToEmitDoneLineAfterToolCallLine) {
  auto fake = makeFakeWithEmptyTags();
  OllamaProvider provider(fake);

  std::vector<StreamEvent> events;
  const ModelId model{.provider_id = QStringLiteral("ollama"), .model_name = QStringLiteral("llama3")};
  provider.sendChat(model, {}, [&events](const StreamEvent& event) { events.push_back(event); });

  fake->emitData(0, QByteArray(R"({"message":{"content":"","tool_calls":[)"
                               R"({"function":{"name":"list_files","arguments":{"path":"~"}}}]},"done":false})") +
                        "\n" +
                        R"({"model":"llama3","message":{"content":""},"done":true,)"
                        R"("prompt_eval_count":10,"eval_count":5})" +
                        "\n");

  ASSERT_EQ(events.size(), 4U);
  EXPECT_TRUE(std::holds_alternative<ContentDelta>(events[0]));
  ASSERT_TRUE(std::holds_alternative<holonight_domain::ToolRequestEvent>(events[1]));
  EXPECT_TRUE(std::holds_alternative<ContentDelta>(events[2]));
  ASSERT_TRUE(std::holds_alternative<Completed>(events[3]));
  const Completed& completed = std::get<Completed>(events[3]);
  EXPECT_EQ(completed.model_identifier, QStringLiteral("llama3"));
  ASSERT_TRUE(completed.usage.has_value());
  EXPECT_EQ(completed.usage->input_tokens, 10);
  EXPECT_EQ(completed.usage->output_tokens, 5);
}

}  // namespace
}  // namespace holonight_providers
