#include "credentials/fake_credential_store.h"
#include "holonight_application/utility_task_runner.h"
#include "holonight_config/config_repository.h"
#include "providers/fake_http_client.h"
#include "testing/scoped_message_capture.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

#include <gtest/gtest.h>

namespace holonight_application {
namespace {

using holonight_config::ConfigRepository;
using holonight_config::OllamaProviderConfig;
using holonight_config::OpenAIProviderConfig;
using holonight_config::ProviderInstanceConfig;
using holonight_config::ProviderState;
using holonight_config::ProviderType;
using holonight_config::UtilityConfig;
using holonight_credentials::FakeCredentialStore;
using holonight_domain::ConversationId;
using holonight_domain::ModelId;
using holonight_providers::FakeHttpClient;
using holonight_testing::ScopedMessageCapture;

class DelayedCredentialStore final : public FakeCredentialStore {
 public:
  void retrieve(QString providerId) override { pending_provider_ids_.append(std::move(providerId)); }

  void resolve(const QString& providerId, const QString& secret) {
    if (!pending_provider_ids_.removeOne(providerId)) {
      return;
    }
    emit retrieveCompleted(providerId, true, secret);
  }

 private:
  QStringList pending_provider_ids_;
};

// Model resolution failures are observable through qWarning() diagnostics: a
// FakeCredentialStore with no stored secrets leaves every Required-policy provider blocked at
// MissingCredential *synchronously* during the constructor's prepare() loop (see
// ProviderRuntimeCoordinator::resolveCredential()), so onProviderReady() logs which provider_id it
// checked without ever reaching real network I/O.

TEST(UtilityTaskRunner, UnsetDefaultModelFallsThroughToChatFallbackModel) {
  FakeCredentialStore store;
  ScopedMessageCapture capture;
  UtilityConfig config;  // default_utility_model left unset

  UtilityTaskRunner runner(&store, config, UtilityProviderEndpoints{});
  runner.requestTitleGeneration(
      ConversationId::generate(), QStringLiteral("hi"), QStringLiteral("hello"),
      ModelId{.provider_id = QStringLiteral("anthropic"), .model_name = QStringLiteral("claude")});

  EXPECT_TRUE(capture.anyMessageContains(QStringLiteral("anthropic")));
  EXPECT_TRUE(capture.anyMessageContains(QStringLiteral("not ready for title generation")));
}

TEST(UtilityTaskRunner, SetValidDefaultModelIsUsedOverChatFallbackModel) {
  FakeCredentialStore store;
  ScopedMessageCapture capture;
  UtilityConfig config;
  config.default_utility_model =
      ModelId{.provider_id = QStringLiteral("google"), .model_name = QStringLiteral("gemini")};

  UtilityTaskRunner runner(&store, config, UtilityProviderEndpoints{});
  runner.requestTitleGeneration(
      ConversationId::generate(), QStringLiteral("hi"), QStringLiteral("hello"),
      ModelId{.provider_id = QStringLiteral("anthropic"), .model_name = QStringLiteral("claude")});

  EXPECT_TRUE(capture.anyMessageContains(QStringLiteral("google")));
  EXPECT_TRUE(capture.anyMessageContains(QStringLiteral("not ready for title generation")));
  EXPECT_FALSE(capture.anyMessageContains(QStringLiteral("anthropic")));
}

TEST(UtilityTaskRunner, UnknownProviderIdInDefaultModelSkipsGenerationEntirely) {
  FakeCredentialStore store;
  ScopedMessageCapture capture;
  UtilityConfig config;
  config.default_utility_model =
      ModelId{.provider_id = QStringLiteral("not-a-real-provider"), .model_name = QStringLiteral("whatever")};

  UtilityTaskRunner runner(&store, config, UtilityProviderEndpoints{});
  runner.requestTitleGeneration(
      ConversationId::generate(), QStringLiteral("hi"), QStringLiteral("hello"),
      ModelId{.provider_id = QStringLiteral("anthropic"), .model_name = QStringLiteral("claude")});

  EXPECT_TRUE(capture.anyMessageContains(QStringLiteral("unknown")));
  EXPECT_TRUE(capture.anyMessageContains(QStringLiteral("not-a-real-provider")));
  EXPECT_FALSE(capture.anyMessageContains(QStringLiteral("anthropic")));
  EXPECT_FALSE(capture.anyMessageContains(QStringLiteral("not ready for title generation")));
}

TEST(UtilityTaskRunner, SavedInstanceDefaultCreatesAnIsolatedProviderFromThatInstancesSettings) {
  FakeCredentialStore store;
  auto httpClient = std::make_shared<FakeHttpClient>();
  httpClient->enqueueBufferedSuccess(QByteArray(R"({"models":[{"name":"first-model"}]})"));
  httpClient->enqueueBufferedSuccess(QByteArray(R"({"models":[{"name":"second-model"}]})"));
  UtilityProviderEndpoints endpoints;
  endpoints.ollama_http_client = httpClient;
  ProviderState state{.instances = {
                          ProviderInstanceConfig{
                              .id = QStringLiteral("local-fast"),
                              .type = ProviderType::Ollama,
                              .display_name = QStringLiteral("Local Fast"),
                              .enabled = true,
                              .settings = OllamaProviderConfig{.base_url = QStringLiteral("http://fast.example.test")}},
                          ProviderInstanceConfig{
                              .id = QStringLiteral("local-deep"),
                              .type = ProviderType::Ollama,
                              .display_name = QStringLiteral("Local Deep"),
                              .enabled = true,
                              .settings = OllamaProviderConfig{.base_url = QStringLiteral("http://deep.example.test")}},
                      }};
  UtilityConfig config{.default_utility_model = ModelId{.provider_id = QStringLiteral("local-deep"),
                                                        .model_name = QStringLiteral("second-model")}};
  QHash<QString, std::vector<ModelId>> models{
      {QStringLiteral("local-fast"),
       {ModelId{.provider_id = QStringLiteral("local-fast"), .model_name = QStringLiteral("first-model")}}},
      {QStringLiteral("local-deep"),
       {ModelId{.provider_id = QStringLiteral("local-deep"), .model_name = QStringLiteral("second-model")}}}};

  UtilityTaskRunner runner(&store, config, endpoints, nullptr, state, models);
  runner.requestTitleGeneration(
      ConversationId::generate(), QStringLiteral("hi"), QStringLiteral("hello"),
      ModelId{.provider_id = QStringLiteral("local-fast"), .model_name = QStringLiteral("first-model")});

  ASSERT_EQ(httpClient->streamingCallCount(), 1);
  EXPECT_EQ(httpClient->streamingCall(0).request.url, QStringLiteral("http://deep.example.test/api/chat"));
}

TEST(UtilityTaskRunner, DisabledDefaultFallsBackToUsableChatInstance) {
  FakeCredentialStore store;
  auto httpClient = std::make_shared<FakeHttpClient>();
  httpClient->enqueueBufferedSuccess(QByteArray(R"({"models":[{"name":"ready-model"}]})"));
  UtilityProviderEndpoints endpoints;
  endpoints.ollama_http_client = httpClient;
  ProviderState state{
      .instances = {
          ProviderInstanceConfig{
              .id = QStringLiteral("disabled"),
              .type = ProviderType::Ollama,
              .display_name = QStringLiteral("Disabled"),
              .enabled = false,
              .settings = OllamaProviderConfig{.base_url = QStringLiteral("http://disabled.example.test")}},
          ProviderInstanceConfig{
              .id = QStringLiteral("ready"),
              .type = ProviderType::Ollama,
              .display_name = QStringLiteral("Ready"),
              .enabled = true,
              .settings = OllamaProviderConfig{.base_url = QStringLiteral("http://ready.example.test")}},
      }};
  UtilityConfig config{.default_utility_model = ModelId{.provider_id = QStringLiteral("disabled"),
                                                        .model_name = QStringLiteral("disabled-model")}};
  QHash<QString, std::vector<ModelId>> models{
      {QStringLiteral("disabled"),
       {ModelId{.provider_id = QStringLiteral("disabled"), .model_name = QStringLiteral("disabled-model")}}},
      {QStringLiteral("ready"),
       {ModelId{.provider_id = QStringLiteral("ready"), .model_name = QStringLiteral("ready-model")}}}};

  UtilityTaskRunner runner(&store, config, endpoints, nullptr, state, models);
  runner.requestTitleGeneration(
      ConversationId::generate(), QStringLiteral("hi"), QStringLiteral("hello"),
      ModelId{.provider_id = QStringLiteral("ready"), .model_name = QStringLiteral("ready-model")});

  ASSERT_EQ(httpClient->streamingCallCount(), 1);
  EXPECT_EQ(httpClient->streamingCall(0).request.url, QStringLiteral("http://ready.example.test/api/chat"));
}

TEST(UtilityTaskRunner, UnavailableDefaultFallsBackToUsableSavedInstance) {
  FakeCredentialStore store;
  auto ollamaClient = std::make_shared<FakeHttpClient>();
  ollamaClient->enqueueBufferedSuccess(QByteArray(R"({"models":[{"name":"ready-model"}]})"));
  UtilityProviderEndpoints endpoints;
  endpoints.ollama_http_client = ollamaClient;
  endpoints.openai_http_client = std::make_shared<FakeHttpClient>();
  ProviderState state{
      .instances = {
          ProviderInstanceConfig{.id = QStringLiteral("cloud"),
                                 .type = ProviderType::OpenAi,
                                 .display_name = QStringLiteral("Cloud"),
                                 .enabled = true,
                                 .settings = OpenAIProviderConfig{.base_url = QStringLiteral("https://cloud.example")}},
          ProviderInstanceConfig{
              .id = QStringLiteral("ready"),
              .type = ProviderType::Ollama,
              .display_name = QStringLiteral("Ready"),
              .enabled = true,
              .settings = OllamaProviderConfig{.base_url = QStringLiteral("http://ready.example.test")}},
      }};
  UtilityConfig config{.default_utility_model = ModelId{.provider_id = QStringLiteral("cloud"),
                                                        .model_name = QStringLiteral("cloud-model")}};
  QHash<QString, std::vector<ModelId>> models{
      {QStringLiteral("cloud"),
       {ModelId{.provider_id = QStringLiteral("cloud"), .model_name = QStringLiteral("cloud-model")}}},
      {QStringLiteral("ready"),
       {ModelId{.provider_id = QStringLiteral("ready"), .model_name = QStringLiteral("ready-model")}}}};

  UtilityTaskRunner runner(&store, config, endpoints, nullptr, state, models);
  runner.requestTitleGeneration(
      ConversationId::generate(), QStringLiteral("hi"), QStringLiteral("hello"),
      ModelId{.provider_id = QStringLiteral("ready"), .model_name = QStringLiteral("ready-model")});

  ASSERT_EQ(ollamaClient->streamingCallCount(), 1);
  EXPECT_EQ(ollamaClient->streamingCall(0).request.url, QStringLiteral("http://ready.example.test/api/chat"));
}

TEST(UtilityTaskRunner, ModelLessInstancesClearResolutionWithoutDispatching) {
  FakeCredentialStore store;
  auto httpClient = std::make_shared<FakeHttpClient>();
  httpClient->enqueueBufferedSuccess(QByteArray(R"({"models":[]})"));
  UtilityProviderEndpoints endpoints;
  endpoints.ollama_http_client = httpClient;
  ProviderState state{.instances = {ProviderInstanceConfig{
                          .id = QStringLiteral("empty"),
                          .type = ProviderType::Ollama,
                          .display_name = QStringLiteral("Empty"),
                          .enabled = true,
                          .settings = OllamaProviderConfig{.base_url = QStringLiteral("http://empty.example.test")}}}};
  UtilityConfig config{.default_utility_model =
                           ModelId{.provider_id = QStringLiteral("deleted"), .model_name = QStringLiteral("gone")}};

  UtilityTaskRunner runner(&store, config, endpoints, nullptr, state, {});
  runner.requestTitleGeneration(
      ConversationId::generate(), QStringLiteral("hi"), QStringLiteral("hello"),
      ModelId{.provider_id = QStringLiteral("empty"), .model_name = QStringLiteral("missing")});

  EXPECT_EQ(httpClient->streamingCallCount(), 0);
}

// REQ-NF-004/REQ-NF-005: two rapid requestTitleGeneration() calls for the same conversation must
// dispatch exactly one generation. "ollama" is CredentialPolicy::Optional, so
// ProviderRuntimeCoordinator::prepare() marks it Ready synchronously (see provider_runtime_
// coordinator.cpp) — both calls below run to completion, synchronously, up to firing a real (but
// backgrounded) HTTP request via the production QtNetworkHttpClient; there is no seam to swap in a
// FakeHttpClient here. The endpoint below points at a closed loopback port so the request fails
// almost immediately with "connection refused" — no external network dependency, no real server
// needed — and dispatchSendChat()'s error callback logs exactly once per *dispatched* request, so
// counting occurrences after the request settles proves whether the second call was actually
// blocked by the generations_ in-flight guard (requestTitleGeneration()'s very first check) or slipped
// through and fired a second, redundant request.
TEST(UtilityTaskRunner, RapidDuplicateRequestForSameConversationDispatchesOnlyOnce) {
  FakeCredentialStore store;
  ScopedMessageCapture capture;
  UtilityConfig config;  // unset default_utility_model — resolveModel() falls through to chatFallbackModel
  UtilityProviderEndpoints endpoints;
  endpoints.ollama_base_url = QStringLiteral("http://127.0.0.1:1");  // guaranteed connection-refused

  UtilityTaskRunner runner(&store, config, endpoints);
  QSignalSpy startedSpy(&runner, &UtilityTaskRunner::titleGenerationStarted);
  QSignalSpy finishedSpy(&runner, &UtilityTaskRunner::titleGenerationFinished);
  const ConversationId conversationId = ConversationId::generate();
  const ModelId ollamaModel{.provider_id = QStringLiteral("ollama"), .model_name = QStringLiteral("llama3")};

  runner.requestTitleGeneration(conversationId, QStringLiteral("hi"), QStringLiteral("hello"), ollamaModel);
  runner.requestTitleGeneration(conversationId, QStringLiteral("hi"), QStringLiteral("hello"), ollamaModel);

  const QString failureNeedle = QStringLiteral("generation failed for");
  int occurrences = 0;
  for (int elapsedMs = 0; elapsedMs < 2000 && occurrences == 0; elapsedMs += 20) {
    QTest::qWait(20);
    occurrences = capture.countMessagesContaining(failureNeedle);
  }

  EXPECT_EQ(occurrences, 1);
  EXPECT_EQ(startedSpy.count(), 1);
  EXPECT_EQ(finishedSpy.count(), 1);
}

TEST(UtilityTaskRunner, FailedGenerationCannotBeRetried) {
  FakeCredentialStore store;
  ScopedMessageCapture capture;
  UtilityProviderEndpoints endpoints;
  endpoints.ollama_base_url = QStringLiteral("http://127.0.0.1:1");

  UtilityTaskRunner runner(&store, UtilityConfig{}, endpoints);
  const ConversationId conversationId = ConversationId::generate();
  const ModelId model{.provider_id = QStringLiteral("ollama"), .model_name = QStringLiteral("llama3")};

  runner.requestTitleGeneration(conversationId, QStringLiteral("hi"), QStringLiteral("hello"), model);
  for (int elapsedMs = 0;
       elapsedMs < 2000 && capture.countMessagesContaining(QStringLiteral("generation failed for")) == 0;
       elapsedMs += 20) {
    QTest::qWait(20);
  }
  ASSERT_EQ(capture.countMessagesContaining(QStringLiteral("generation failed for")), 1);

  runner.requestTitleGeneration(conversationId, QStringLiteral("hi"), QStringLiteral("hello"), model);
  QTest::qWait(100);

  EXPECT_EQ(capture.countMessagesContaining(QStringLiteral("generation failed for")), 1);
}

TEST(UtilityTaskRunner, PendingGenerationWaitsForCredentialResolution) {
  DelayedCredentialStore store;
  ScopedMessageCapture capture;
  UtilityConfig config;
  config.default_utility_model =
      ModelId{.provider_id = QStringLiteral("anthropic"), .model_name = QStringLiteral("claude")};
  UtilityProviderEndpoints endpoints;
  endpoints.anthropic_base_url = QStringLiteral("http://127.0.0.1:1");

  UtilityTaskRunner runner(&store, config, endpoints);
  runner.requestTitleGeneration(
      ConversationId::generate(), QStringLiteral("hi"), QStringLiteral("hello"),
      ModelId{.provider_id = QStringLiteral("ollama"), .model_name = QStringLiteral("llama3")});

  EXPECT_EQ(capture.countMessagesContaining(QStringLiteral("not ready for title generation")), 0);
  EXPECT_EQ(capture.countMessagesContaining(QStringLiteral("generation failed for")), 0);

  store.resolve(QStringLiteral("anthropic"), QStringLiteral("secret"));
  for (int elapsedMs = 0;
       elapsedMs < 2000 && capture.countMessagesContaining(QStringLiteral("generation failed for")) == 0;
       elapsedMs += 20) {
    QTest::qWait(20);
  }

  EXPECT_EQ(capture.countMessagesContaining(QStringLiteral("generation failed for")), 1);
}

// T-027 (REQ-F-007, REQ-U-001, REQ-U-002): a Required-policy provider ("anthropic") with no stored
// secret in FakeCredentialStore resolves to MissingCredential synchronously, before any network
// I/O — onProviderReady() must bail out without ever dispatching, so titleGenerated() is never
// emitted (the fallback title is left untouched because nothing ever calls renameConversation()).
TEST(UtilityTaskRunner, MissingCredentialNeverEmitsTitleGenerated) {
  FakeCredentialStore store;  // no secret stored for "anthropic"
  ScopedMessageCapture capture;
  UtilityConfig config;  // unset — resolveModel() falls through to chatFallbackModel

  UtilityTaskRunner runner(&store, config, UtilityProviderEndpoints{});
  QSignalSpy titleSpy(&runner, &UtilityTaskRunner::titleGenerated);
  QSignalSpy startedSpy(&runner, &UtilityTaskRunner::titleGenerationStarted);
  QSignalSpy finishedSpy(&runner, &UtilityTaskRunner::titleGenerationFinished);
  runner.requestTitleGeneration(
      ConversationId::generate(), QStringLiteral("hi"), QStringLiteral("hello"),
      ModelId{.provider_id = QStringLiteral("anthropic"), .model_name = QStringLiteral("claude")});

  EXPECT_EQ(titleSpy.count(), 0);
  EXPECT_EQ(startedSpy.count(), 1);
  EXPECT_EQ(finishedSpy.count(), 1);
  EXPECT_TRUE(capture.anyMessageContains(QStringLiteral("not ready for title generation")));
}

// T-027 (REQ-F-007, REQ-U-001, REQ-U-002): an unknown provider_id in default_utility_model makes
// resolveModel() return std::nullopt, so requestTitleGeneration() returns before onProviderReady()
// is ever reached — titleGenerated() must never fire.
TEST(UtilityTaskRunner, UnknownProviderIdNeverEmitsTitleGenerated) {
  FakeCredentialStore store;
  ScopedMessageCapture capture;
  UtilityConfig config;
  config.default_utility_model =
      ModelId{.provider_id = QStringLiteral("not-a-real-provider"), .model_name = QStringLiteral("whatever")};

  UtilityTaskRunner runner(&store, config, UtilityProviderEndpoints{});
  QSignalSpy titleSpy(&runner, &UtilityTaskRunner::titleGenerated);
  QSignalSpy startedSpy(&runner, &UtilityTaskRunner::titleGenerationStarted);
  QSignalSpy finishedSpy(&runner, &UtilityTaskRunner::titleGenerationFinished);
  runner.requestTitleGeneration(
      ConversationId::generate(), QStringLiteral("hi"), QStringLiteral("hello"),
      ModelId{.provider_id = QStringLiteral("anthropic"), .model_name = QStringLiteral("claude")});

  EXPECT_EQ(titleSpy.count(), 0);
  EXPECT_EQ(startedSpy.count(), 0);
  EXPECT_EQ(finishedSpy.count(), 0);
  EXPECT_TRUE(capture.anyMessageContains(QStringLiteral("unknown")));
}

// T-027 (REQ-F-007, REQ-U-001, REQ-U-002): a genuine provider error (real, backgrounded HTTP
// request against a guaranteed connection-refused loopback port, same technique as
// RapidDuplicateRequestForSameConversationDispatchesOnlyOnce above) must not emit titleGenerated()
// — the Error branch in dispatchGeneration()'s callback only logs and clears the in-flight guard.
TEST(UtilityTaskRunner, ProviderErrorNeverEmitsTitleGenerated) {
  FakeCredentialStore store;
  ScopedMessageCapture capture;
  UtilityConfig config;
  UtilityProviderEndpoints endpoints;
  endpoints.ollama_base_url = QStringLiteral("http://127.0.0.1:1");  // guaranteed connection-refused

  UtilityTaskRunner runner(&store, config, endpoints);
  QSignalSpy startedSpy(&runner, &UtilityTaskRunner::titleGenerationStarted);
  QSignalSpy finishedSpy(&runner, &UtilityTaskRunner::titleGenerationFinished);
  QSignalSpy titleSpy(&runner, &UtilityTaskRunner::titleGenerated);
  runner.requestTitleGeneration(
      ConversationId::generate(), QStringLiteral("hi"), QStringLiteral("hello"),
      ModelId{.provider_id = QStringLiteral("ollama"), .model_name = QStringLiteral("llama3")});

  const QString failureNeedle = QStringLiteral("generation failed for");
  int occurrences = 0;
  for (int elapsedMs = 0; elapsedMs < 2000 && occurrences == 0; elapsedMs += 20) {
    QTest::qWait(20);
    occurrences = capture.countMessagesContaining(failureNeedle);
  }

  EXPECT_EQ(occurrences, 1);
  EXPECT_EQ(titleSpy.count(), 0);
  EXPECT_EQ(startedSpy.count(), 1);
  EXPECT_EQ(finishedSpy.count(), 1);
}

// T-027 (REQ-F-008, REQ-U-001, REQ-U-002): a *successful* response whose accumulated content is
// empty must not emit titleGenerated() either — dispatchGeneration()'s Completed branch checks
// accumulated_text.trimmed().isEmpty() and logs "empty title for ... keeping fallback" instead.
// A FakeHttpClient drives the successful streaming response deterministically.
TEST(UtilityTaskRunner, EmptyAccumulatedResponseNeverEmitsTitleGenerated) {
  FakeCredentialStore store;
  auto httpClient = std::make_shared<FakeHttpClient>();
  httpClient->enqueueBufferedSuccess(QByteArray(R"({"models":[{"name":"llama3"}]})"));
  ScopedMessageCapture capture;
  UtilityConfig config;
  UtilityProviderEndpoints endpoints;
  endpoints.ollama_http_client = httpClient;

  UtilityTaskRunner runner(&store, config, endpoints);
  QSignalSpy titleSpy(&runner, &UtilityTaskRunner::titleGenerated);
  QSignalSpy startedSpy(&runner, &UtilityTaskRunner::titleGenerationStarted);
  QSignalSpy finishedSpy(&runner, &UtilityTaskRunner::titleGenerationFinished);
  runner.requestTitleGeneration(
      ConversationId::generate(), QStringLiteral("hi"), QStringLiteral("hello"),
      ModelId{.provider_id = QStringLiteral("ollama"), .model_name = QStringLiteral("llama3")});

  ASSERT_EQ(httpClient->streamingCallCount(), 1);
  httpClient->emitData(
      0, QByteArray(R"({"model":"llama3","message":{"role":"assistant","content":""},"done":true})") + "\n");

  EXPECT_EQ(capture.countMessagesContaining(QStringLiteral("empty title for")), 1);
  EXPECT_EQ(titleSpy.count(), 0);
  EXPECT_EQ(startedSpy.count(), 1);
  EXPECT_EQ(finishedSpy.count(), 1);
}

TEST(UtilityTaskRunner, SuccessfulGenerationEmitsPayloadBeforeFinished) {
  FakeCredentialStore store;
  auto httpClient = std::make_shared<FakeHttpClient>();
  httpClient->enqueueBufferedSuccess(QByteArray(R"({"models":[{"name":"llama3"}]})"));
  UtilityProviderEndpoints endpoints;
  endpoints.ollama_http_client = httpClient;
  UtilityTaskRunner runner(&store, UtilityConfig{}, endpoints);
  QStringList events;
  QObject::connect(&runner, &UtilityTaskRunner::titleGenerationStarted, &runner,
                   [&events] { events.append(QStringLiteral("started")); });
  QObject::connect(&runner, &UtilityTaskRunner::titleGenerated, &runner,
                   [&events] { events.append(QStringLiteral("generated")); });
  QObject::connect(&runner, &UtilityTaskRunner::titleGenerationFinished, &runner,
                   [&events] { events.append(QStringLiteral("finished")); });

  runner.requestTitleGeneration(
      ConversationId::generate(), QStringLiteral("hi"), QStringLiteral("hello"),
      ModelId{.provider_id = QStringLiteral("ollama"), .model_name = QStringLiteral("llama3")});
  httpClient->emitData(
      0,
      QByteArray(R"({"model":"llama3","message":{"role":"assistant","content":"A Useful Title"},"done":true})") + "\n");

  EXPECT_EQ(events, QStringList({QStringLiteral("started"), QStringLiteral("generated"), QStringLiteral("finished")}));
}

TEST(UtilityTaskRunner, GoogleTitleGenerationUsesExpandedOutputBudget) {
  FakeCredentialStore store;
  store.store(QStringLiteral("google"), QStringLiteral("secret"));
  auto httpClient = std::make_shared<FakeHttpClient>();
  httpClient->enqueueBufferedSuccess(QByteArray(R"({"models":[{"name":"models/gemini"}]})"));
  UtilityProviderEndpoints endpoints;
  endpoints.google_http_client = httpClient;

  UtilityTaskRunner runner(&store, UtilityConfig{}, endpoints);
  runner.requestTitleGeneration(
      ConversationId::generate(), QStringLiteral("hi"), QStringLiteral("hello"),
      ModelId{.provider_id = QStringLiteral("google"), .model_name = QStringLiteral("gemini")});

  ASSERT_EQ(httpClient->streamingCallCount(), 1);
  const QJsonObject body = QJsonDocument::fromJson(httpClient->streamingCall(0).request.body).object();
  EXPECT_EQ(body.value(QStringLiteral("generationConfig")).toObject().value(QStringLiteral("maxOutputTokens")).toInt(),
            1024);
}

// T-027 (REQ-F-014/015): applyProviderState() must resync the router — an instance added after
// construction (including a UUID-style instance ID, not just the four hardcoded literals) must
// become dispatchable, proving the router is no longer built once and left stale.
TEST(UtilityTaskRunner, ApplyProviderStateAddsNewlyCreatedInstanceToRouter) {
  FakeCredentialStore store;
  auto httpClient = std::make_shared<FakeHttpClient>();
  httpClient->enqueueBufferedSuccess(QByteArray(R"({"models":[{"name":"seed-model"}]})"));
  UtilityProviderEndpoints endpoints;
  endpoints.ollama_http_client = httpClient;
  ProviderState initialState{
      .instances = {ProviderInstanceConfig{
          .id = QStringLiteral("seed"),
          .type = ProviderType::Ollama,
          .display_name = QStringLiteral("Seed"),
          .enabled = true,
          .settings = OllamaProviderConfig{.base_url = QStringLiteral("http://seed.example.test")}}}};

  UtilityTaskRunner runner(&store, UtilityConfig{}, endpoints, nullptr, initialState, {});

  const QString uuidId = QStringLiteral("550e8400-e29b-41d4-a716-446655440000");
  httpClient->enqueueBufferedSuccess(QByteArray(R"({"models":[{"name":"new-model"}]})"));
  ProviderState updatedState{
      .instances = {initialState.instances.front(),
                    ProviderInstanceConfig{
                        .id = uuidId,
                        .type = ProviderType::Ollama,
                        .display_name = QStringLiteral("New"),
                        .enabled = true,
                        .settings = OllamaProviderConfig{.base_url = QStringLiteral("http://new.example.test")}}}};

  runner.applyProviderState(updatedState);
  runner.requestTitleGeneration(ConversationId::generate(), QStringLiteral("hi"), QStringLiteral("hello"),
                                ModelId{.provider_id = uuidId, .model_name = QStringLiteral("new-model")});

  ASSERT_EQ(httpClient->streamingCallCount(), 1);
  EXPECT_EQ(httpClient->streamingCall(0).request.url, QStringLiteral("http://new.example.test/api/chat"));
}

// T-027 (REQ-F-017/REQ-NF-003): applyProviderState() must remove an instance no longer present in
// the new state, so resolveModel()'s isUsable() check for it starts returning false immediately
// (not just after a restart).
TEST(UtilityTaskRunner, ApplyProviderStateRemovesDroppedInstanceFromRouter) {
  FakeCredentialStore store;
  auto httpClient = std::make_shared<FakeHttpClient>();
  httpClient->enqueueBufferedSuccess(QByteArray(R"({"models":[{"name":"a-model"}]})"));
  httpClient->enqueueBufferedSuccess(QByteArray(R"({"models":[{"name":"b-model"}]})"));
  UtilityProviderEndpoints endpoints;
  endpoints.ollama_http_client = httpClient;
  ProviderState initialState{
      .instances = {
          ProviderInstanceConfig{.id = QStringLiteral("a"),
                                 .type = ProviderType::Ollama,
                                 .display_name = QStringLiteral("A"),
                                 .enabled = true,
                                 .settings = OllamaProviderConfig{.base_url = QStringLiteral("http://a.example.test")}},
          ProviderInstanceConfig{
              .id = QStringLiteral("b"),
              .type = ProviderType::Ollama,
              .display_name = QStringLiteral("B"),
              .enabled = true,
              .settings = OllamaProviderConfig{.base_url = QStringLiteral("http://b.example.test")}}}};
  UtilityConfig config{.default_utility_model =
                           ModelId{.provider_id = QStringLiteral("a"), .model_name = QStringLiteral("a-model")}};

  UtilityTaskRunner runner(&store, config, endpoints, nullptr, initialState, {});

  ProviderState updatedState{.instances = {initialState.instances.back()}};  // "a" dropped
  runner.applyProviderState(updatedState);

  runner.requestTitleGeneration(ConversationId::generate(), QStringLiteral("hi"), QStringLiteral("hello"),
                                ModelId{.provider_id = QStringLiteral("b"), .model_name = QStringLiteral("b-model")});

  ASSERT_EQ(httpClient->streamingCallCount(), 1);
  EXPECT_EQ(httpClient->streamingCall(0).request.url, QStringLiteral("http://b.example.test/api/chat"));
}

// T-027 (REQ-F-016/REQ-NF-001): an instance added via applyProviderState() must reach the router
// with utility generation params injected — the chat-configured temperature/max-tokens must not
// leak through onto a utility call.
TEST(UtilityTaskRunner, ApplyProviderStateInjectsUtilityGenerationParamsOnAddedInstance) {
  FakeCredentialStore store;
  store.store(QStringLiteral("anthropic-instance"), QStringLiteral("secret"));
  auto httpClient = std::make_shared<FakeHttpClient>();
  httpClient->enqueueBufferedSuccess(QByteArray(R"({"models":[{"name":"seed-model"}]})"));
  httpClient->enqueueBufferedSuccess(QByteArray(R"({"data":[{"id":"claude"}]})"));
  UtilityProviderEndpoints endpoints;
  endpoints.ollama_http_client = httpClient;
  endpoints.anthropic_http_client = httpClient;
  // A non-empty initial ProviderState is required so the constructor creates adapter_router_ —
  // applyProviderState() is a no-op resync when adapter_router_ is null (legacy fallback path).
  ProviderState seedState{
      .instances = {ProviderInstanceConfig{
          .id = QStringLiteral("seed"),
          .type = ProviderType::Ollama,
          .display_name = QStringLiteral("Seed"),
          .enabled = true,
          .settings = OllamaProviderConfig{.base_url = QStringLiteral("http://seed.example.test")}}}};

  UtilityTaskRunner runner(&store, UtilityConfig{}, endpoints, nullptr, seedState, {});

  ProviderState state{.instances = {seedState.instances.front(),
                                    ProviderInstanceConfig{.id = QStringLiteral("anthropic-instance"),
                                                           .type = ProviderType::Anthropic,
                                                           .display_name = QStringLiteral("Anthropic"),
                                                           .enabled = true,
                                                           .settings = holonight_config::AnthropicProviderConfig{
                                                               .temperature = 1.0, .max_output_tokens = 4096}}}};
  runner.applyProviderState(state);
  runner.requestTitleGeneration(
      ConversationId::generate(), QStringLiteral("hi"), QStringLiteral("hello"),
      ModelId{.provider_id = QStringLiteral("anthropic-instance"), .model_name = QStringLiteral("claude")});

  ASSERT_EQ(httpClient->streamingCallCount(), 1);
  const QJsonObject body = QJsonDocument::fromJson(httpClient->streamingCall(0).request.body).object();
  EXPECT_DOUBLE_EQ(body.value(QStringLiteral("temperature")).toDouble(), 0.3);
  EXPECT_EQ(body.value(QStringLiteral("max_tokens")).toInt(), 64);
}

// T-028 (REQ-F-012): the chat-title model override tier must win over the default utility model
// tier when both are set and usable.
TEST(UtilityTaskRunner, ChatTitleModelOverrideTierWinsOverDefaultUtilityModelTier) {
  FakeCredentialStore store;
  auto httpClient = std::make_shared<FakeHttpClient>();
  httpClient->enqueueBufferedSuccess(QByteArray(R"({"models":[{"name":"override-model"}]})"));
  httpClient->enqueueBufferedSuccess(QByteArray(R"({"models":[{"name":"default-model"}]})"));
  UtilityProviderEndpoints endpoints;
  endpoints.ollama_http_client = httpClient;
  ProviderState state{
      .instances = {ProviderInstanceConfig{
                        .id = QStringLiteral("override"),
                        .type = ProviderType::Ollama,
                        .display_name = QStringLiteral("Override"),
                        .enabled = true,
                        .settings = OllamaProviderConfig{.base_url = QStringLiteral("http://override.example.test")}},
                    ProviderInstanceConfig{
                        .id = QStringLiteral("default"),
                        .type = ProviderType::Ollama,
                        .display_name = QStringLiteral("Default"),
                        .enabled = true,
                        .settings = OllamaProviderConfig{.base_url = QStringLiteral("http://default.example.test")}}}};
  UtilityConfig config{.default_utility_model = ModelId{.provider_id = QStringLiteral("default"),
                                                        .model_name = QStringLiteral("default-model")},
                       .chat_title_model_override = ModelId{.provider_id = QStringLiteral("override"),
                                                            .model_name = QStringLiteral("override-model")}};

  UtilityTaskRunner runner(&store, config, endpoints, nullptr, state, {});
  runner.requestTitleGeneration(
      ConversationId::generate(), QStringLiteral("hi"), QStringLiteral("hello"),
      ModelId{.provider_id = QStringLiteral("default"), .model_name = QStringLiteral("default-model")});

  ASSERT_EQ(httpClient->streamingCallCount(), 1);
  EXPECT_EQ(httpClient->streamingCall(0).request.url, QStringLiteral("http://override.example.test/api/chat"));
}

// T-028 (REQ-F-013): if the chat-title model override references an instance that isn't usable
// (e.g. no models loaded), resolution must fall through to the default utility model tier rather
// than skipping generation entirely.
TEST(UtilityTaskRunner, UnusableChatTitleModelOverrideFallsThroughToDefaultUtilityModelTier) {
  FakeCredentialStore store;
  auto httpClient = std::make_shared<FakeHttpClient>();
  httpClient->enqueueBufferedSuccess(QByteArray(R"({"models":[]})"));
  httpClient->enqueueBufferedSuccess(QByteArray(R"({"models":[{"name":"default-model"}]})"));
  UtilityProviderEndpoints endpoints;
  endpoints.ollama_http_client = httpClient;
  ProviderState state{
      .instances = {ProviderInstanceConfig{
                        .id = QStringLiteral("empty"),
                        .type = ProviderType::Ollama,
                        .display_name = QStringLiteral("Empty"),
                        .enabled = true,
                        .settings = OllamaProviderConfig{.base_url = QStringLiteral("http://empty.example.test")}},
                    ProviderInstanceConfig{
                        .id = QStringLiteral("default"),
                        .type = ProviderType::Ollama,
                        .display_name = QStringLiteral("Default"),
                        .enabled = true,
                        .settings = OllamaProviderConfig{.base_url = QStringLiteral("http://default.example.test")}}}};
  UtilityConfig config{.default_utility_model = ModelId{.provider_id = QStringLiteral("default"),
                                                        .model_name = QStringLiteral("default-model")},
                       .chat_title_model_override = ModelId{.provider_id = QStringLiteral("empty"),
                                                            .model_name = QStringLiteral("nonexistent-model")}};

  UtilityTaskRunner runner(&store, config, endpoints, nullptr, state, {});
  runner.requestTitleGeneration(
      ConversationId::generate(), QStringLiteral("hi"), QStringLiteral("hello"),
      ModelId{.provider_id = QStringLiteral("default"), .model_name = QStringLiteral("default-model")});

  ASSERT_EQ(httpClient->streamingCallCount(), 1);
  EXPECT_EQ(httpClient->streamingCall(0).request.url, QStringLiteral("http://default.example.test/api/chat"));
}

// T-029 (REQ-F-009): chat_title_generation_enabled == false must prevent dispatch entirely, with
// no generations_ entry left behind (a subsequent enabled call for the same conversation must
// still be able to dispatch).
TEST(UtilityTaskRunner, DisabledTitleGenerationSkipsDispatchEntirely) {
  FakeCredentialStore store;
  auto httpClient = std::make_shared<FakeHttpClient>();
  httpClient->enqueueBufferedSuccess(QByteArray(R"({"models":[{"name":"llama3"}]})"));  // constructor's own refresh
  UtilityProviderEndpoints endpoints;
  endpoints.ollama_http_client = httpClient;
  UtilityConfig config{.chat_title_generation_enabled = false};

  UtilityTaskRunner runner(&store, config, endpoints);
  QSignalSpy startedSpy(&runner, &UtilityTaskRunner::titleGenerationStarted);
  QSignalSpy finishedSpy(&runner, &UtilityTaskRunner::titleGenerationFinished);
  const std::size_t bufferedCallsAfterConstruction = httpClient->bufferedCallCount();
  runner.requestTitleGeneration(
      ConversationId::generate(), QStringLiteral("hi"), QStringLiteral("hello"),
      ModelId{.provider_id = QStringLiteral("ollama"), .model_name = QStringLiteral("llama3")});

  EXPECT_EQ(httpClient->streamingCallCount(), 0);
  EXPECT_EQ(httpClient->bufferedCallCount(), bufferedCallsAfterConstruction);
  EXPECT_EQ(startedSpy.count(), 0);
  EXPECT_EQ(finishedSpy.count(), 0);
}

// T-029 (REQ-F-009): explicitly enabling the toggle must still dispatch normally.
TEST(UtilityTaskRunner, ExplicitlyEnabledTitleGenerationStillDispatches) {
  FakeCredentialStore store;
  auto httpClient = std::make_shared<FakeHttpClient>();
  httpClient->enqueueBufferedSuccess(QByteArray(R"({"models":[{"name":"llama3"}]})"));
  UtilityProviderEndpoints endpoints;
  endpoints.ollama_http_client = httpClient;
  UtilityConfig config{.chat_title_generation_enabled = true};

  UtilityTaskRunner runner(&store, config, endpoints);
  runner.requestTitleGeneration(
      ConversationId::generate(), QStringLiteral("hi"), QStringLiteral("hello"),
      ModelId{.provider_id = QStringLiteral("ollama"), .model_name = QStringLiteral("llama3")});

  EXPECT_EQ(httpClient->streamingCallCount(), 1);
}

// T-031 (REQ-F-017/REQ-NF-003): after applyProviderState() removes the instance referenced by
// default_utility_model, a subsequent title-generation dispatch must not crash and must fall back
// to the chat model via isUsable()'s tier-skip logic.
TEST(UtilityTaskRunner, DeletedDefaultProviderFallsBackToChatModelWithoutCrashing) {
  FakeCredentialStore store;
  auto httpClient = std::make_shared<FakeHttpClient>();
  httpClient->enqueueBufferedSuccess(QByteArray(R"({"models":[{"name":"gone-model"}]})"));
  httpClient->enqueueBufferedSuccess(QByteArray(R"({"models":[{"name":"ready-model"}]})"));
  UtilityProviderEndpoints endpoints;
  endpoints.ollama_http_client = httpClient;
  ProviderState state{
      .instances = {ProviderInstanceConfig{
                        .id = QStringLiteral("gone"),
                        .type = ProviderType::Ollama,
                        .display_name = QStringLiteral("Gone"),
                        .enabled = true,
                        .settings = OllamaProviderConfig{.base_url = QStringLiteral("http://gone.example.test")}},
                    ProviderInstanceConfig{
                        .id = QStringLiteral("ready"),
                        .type = ProviderType::Ollama,
                        .display_name = QStringLiteral("Ready"),
                        .enabled = true,
                        .settings = OllamaProviderConfig{.base_url = QStringLiteral("http://ready.example.test")}}}};
  UtilityConfig config{.default_utility_model =
                           ModelId{.provider_id = QStringLiteral("gone"), .model_name = QStringLiteral("gone-model")}};

  UtilityTaskRunner runner(&store, config, endpoints, nullptr, state, {});
  runner.applyProviderState(ProviderState{.instances = {state.instances.back()}});  // "gone" removed

  ASSERT_NO_FATAL_FAILURE(runner.requestTitleGeneration(
      ConversationId::generate(), QStringLiteral("hi"), QStringLiteral("hello"),
      ModelId{.provider_id = QStringLiteral("ready"), .model_name = QStringLiteral("ready-model")}));

  ASSERT_EQ(httpClient->streamingCallCount(), 1);
  EXPECT_EQ(httpClient->streamingCall(0).request.url, QStringLiteral("http://ready.example.test/api/chat"));
}

// T-035 (REQ-F-009/012/020): end-to-end flow — settings persisted via ConfigRepository (as the
// Background AI panel's Save button would leave them), then loaded back into a fresh
// UtilityTaskRunner (as app startup does), must dispatch using the persisted override model and
// must respect the persisted enable/disable toggle.
TEST(UtilityTaskRunner, PersistedSettingsAreHonoredOnNextDispatch) {
  QTemporaryDir dir;
  ConfigRepository repository(dir.filePath(QStringLiteral("config.json")));
  UtilityConfig persisted;
  persisted.chat_title_generation_enabled = true;
  persisted.chat_title_model_override =
      ModelId{.provider_id = QStringLiteral("override"), .model_name = QStringLiteral("override-model")};
  ASSERT_TRUE(repository.saveUtilityConfig(persisted).has_value());

  FakeCredentialStore store;
  auto httpClient = std::make_shared<FakeHttpClient>();
  httpClient->enqueueBufferedSuccess(QByteArray(R"({"models":[{"name":"override-model"}]})"));
  httpClient->enqueueBufferedSuccess(QByteArray(R"({"models":[{"name":"chat-model"}]})"));
  UtilityProviderEndpoints endpoints;
  endpoints.ollama_http_client = httpClient;
  ProviderState state{
      .instances = {ProviderInstanceConfig{
                        .id = QStringLiteral("override"),
                        .type = ProviderType::Ollama,
                        .display_name = QStringLiteral("Override"),
                        .enabled = true,
                        .settings = OllamaProviderConfig{.base_url = QStringLiteral("http://override.example.test")}},
                    ProviderInstanceConfig{
                        .id = QStringLiteral("chat"),
                        .type = ProviderType::Ollama,
                        .display_name = QStringLiteral("Chat"),
                        .enabled = true,
                        .settings = OllamaProviderConfig{.base_url = QStringLiteral("http://chat.example.test")}}}};

  UtilityTaskRunner runner(&store, repository.loadUtilityConfig(), endpoints, nullptr, state, {});
  runner.requestTitleGeneration(
      ConversationId::generate(), QStringLiteral("hi"), QStringLiteral("hello"),
      ModelId{.provider_id = QStringLiteral("chat"), .model_name = QStringLiteral("chat-model")});

  ASSERT_EQ(httpClient->streamingCallCount(), 1);
  EXPECT_EQ(httpClient->streamingCall(0).request.url, QStringLiteral("http://override.example.test/api/chat"));
}

// T-035: the same persisted-then-loaded round trip, but with the toggle disabled — no dispatch at
// all, regardless of an otherwise-usable override.
TEST(UtilityTaskRunner, PersistedDisabledToggleSuppressesDispatchAfterReload) {
  QTemporaryDir dir;
  ConfigRepository repository(dir.filePath(QStringLiteral("config.json")));
  UtilityConfig persisted;
  persisted.chat_title_generation_enabled = false;
  persisted.chat_title_model_override =
      ModelId{.provider_id = QStringLiteral("override"), .model_name = QStringLiteral("override-model")};
  ASSERT_TRUE(repository.saveUtilityConfig(persisted).has_value());

  FakeCredentialStore store;
  auto httpClient = std::make_shared<FakeHttpClient>();
  UtilityProviderEndpoints endpoints;
  endpoints.ollama_http_client = httpClient;

  UtilityTaskRunner runner(&store, repository.loadUtilityConfig(), endpoints);
  runner.requestTitleGeneration(
      ConversationId::generate(), QStringLiteral("hi"), QStringLiteral("hello"),
      ModelId{.provider_id = QStringLiteral("ollama"), .model_name = QStringLiteral("llama3")});

  EXPECT_EQ(httpClient->streamingCallCount(), 0);
}

}  // namespace
}  // namespace holonight_application
