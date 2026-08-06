#include "holonight_application/provider_adapter_router.h"
#include "holonight_application/tools/tool_registry.h"
#include "providers/fake_http_client.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>

#include <gtest/gtest.h>
#include <memory>
#include <variant>

namespace {

using holonight_application::ITool;
using holonight_application::ProviderAdapterRouter;
using holonight_application::ToolRegistry;
using holonight_config::AnthropicProviderConfig;
using holonight_config::GoogleProviderConfig;
using holonight_config::OllamaProviderConfig;
using holonight_config::OpenAIProviderConfig;
using holonight_config::ProviderInstanceConfig;
using holonight_config::ProviderType;
using holonight_domain::ModelId;
using holonight_providers::FakeHttpClient;

ProviderInstanceConfig openAiConfig(QString id, QString baseUrl = QStringLiteral("https://example.test/v1"),
                                    bool toolCallingEnabled = false) {
  return ProviderInstanceConfig{
      .id = std::move(id),
      .type = ProviderType::OpenAi,
      .display_name = QStringLiteral("OpenAI"),
      .enabled = true,
      .settings = OpenAIProviderConfig{.base_url = std::move(baseUrl), .tool_calling_enabled = toolCallingEnabled}};
}

ProviderInstanceConfig anthropicConfig(QString id, bool toolCallingEnabled) {
  return ProviderInstanceConfig{
      .id = std::move(id),
      .type = ProviderType::Anthropic,
      .display_name = QStringLiteral("Anthropic"),
      .enabled = true,
      .settings = AnthropicProviderConfig{.base_url = QStringLiteral("https://example.test/anthropic"),
                                          .tool_calling_enabled = toolCallingEnabled}};
}

ProviderInstanceConfig googleConfig(QString id, bool toolCallingEnabled) {
  return ProviderInstanceConfig{
      .id = std::move(id),
      .type = ProviderType::Google,
      .display_name = QStringLiteral("Google"),
      .enabled = true,
      .settings = GoogleProviderConfig{.base_url = QStringLiteral("https://example.test/google"),
                                       .tool_calling_enabled = toolCallingEnabled}};
}

ProviderInstanceConfig ollamaConfig(QString id, bool toolCallingEnabled) {
  return ProviderInstanceConfig{.id = std::move(id),
                                .type = ProviderType::Ollama,
                                .display_name = QStringLiteral("Ollama"),
                                .enabled = true,
                                .settings = OllamaProviderConfig{.base_url = QStringLiteral("http://localhost:11434"),
                                                                 .tool_calling_enabled = toolCallingEnabled}};
}

class FakeTool : public ITool {
 public:
  [[nodiscard]] QString name() const override { return QStringLiteral("Fake"); }
  [[nodiscard]] QString description() const override { return QStringLiteral("A fake tool for tests."); }
  [[nodiscard]] QJsonObject schema() const override {
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}};
  }
  [[nodiscard]] QJsonObject execute(const QJsonObject& parameters) override {
    return QJsonObject{{QStringLiteral("echo"), parameters}};
  }
};

TEST(ProviderAdapterRouter, SameTypeInstancesDiscoverIndependentInstanceScopedModels) {
  auto firstHttp = std::make_shared<FakeHttpClient>();
  auto secondHttp = std::make_shared<FakeHttpClient>();
  firstHttp->enqueueBufferedSuccess(R"({"data":[{"id":"shared-model"}]})");
  secondHttp->enqueueBufferedSuccess(R"({"data":[{"id":"shared-model"}]})");
  ProviderAdapterRouter router;

  ASSERT_TRUE(router.add(openAiConfig(QStringLiteral("work-openai")), firstHttp));
  ASSERT_TRUE(router.add(openAiConfig(QStringLiteral("personal-openai")), secondHttp));
  ASSERT_TRUE(router.refresh(QStringLiteral("work-openai")));
  ASSERT_TRUE(router.refresh(QStringLiteral("personal-openai")));

  const auto* workModels = router.availableModels(QStringLiteral("work-openai"));
  const auto* personalModels = router.availableModels(QStringLiteral("personal-openai"));
  ASSERT_NE(workModels, nullptr);
  ASSERT_NE(personalModels, nullptr);
  ASSERT_EQ(workModels->size(), 1);
  ASSERT_EQ(personalModels->size(), 1);
  EXPECT_EQ(workModels->front(),
            (ModelId{.provider_id = QStringLiteral("work-openai"), .model_name = QStringLiteral("shared-model")}));
  EXPECT_EQ(personalModels->front(),
            (ModelId{.provider_id = QStringLiteral("personal-openai"), .model_name = QStringLiteral("shared-model")}));
}

TEST(ProviderAdapterRouter, RoutesSendAndCancellationByInstanceId) {
  auto firstHttp = std::make_shared<FakeHttpClient>();
  auto secondHttp = std::make_shared<FakeHttpClient>();
  ProviderAdapterRouter router;
  ASSERT_TRUE(router.add(openAiConfig(QStringLiteral("work-openai")), firstHttp));
  ASSERT_TRUE(router.add(openAiConfig(QStringLiteral("personal-openai")), secondHttp));

  auto request =
      router.sendChat(ModelId{.provider_id = QStringLiteral("personal-openai"), .model_name = QStringLiteral("gpt-4o")},
                      {}, [](const auto&) {});

  ASSERT_NE(request, nullptr);
  EXPECT_EQ(firstHttp->streamingCallCount(), 0);
  ASSERT_EQ(secondHttp->streamingCallCount(), 1);
  ProviderAdapterRouter::cancel(request);
  EXPECT_TRUE(secondHttp->streamingCall(0).cancelled);
  EXPECT_EQ(router.sendChat(ModelId{.provider_id = QStringLiteral("missing"), .model_name = QStringLiteral("gpt")}, {},
                            [](const auto&) {}),
            nullptr);
}

TEST(ProviderAdapterRouter, RejectsDuplicateIdsAndMismatchedTypedSettings) {
  auto http = std::make_shared<FakeHttpClient>();
  ProviderAdapterRouter router;
  const auto valid = openAiConfig(QStringLiteral("openai-instance"));
  ASSERT_TRUE(router.add(valid, http));
  EXPECT_FALSE(router.add(valid, http));

  auto mismatched = valid;
  mismatched.id = QStringLiteral("bad-instance");
  mismatched.settings = holonight_config::OllamaProviderConfig{};
  EXPECT_FALSE(router.add(mismatched, http));
  EXPECT_FALSE(router.contains(QStringLiteral("bad-instance")));

  auto changedType = valid;
  changedType.type = ProviderType::Ollama;
  changedType.settings = holonight_config::OllamaProviderConfig{};
  EXPECT_FALSE(router.reconfigure(changedType));
}

TEST(ProviderAdapterRouter, ReconfiguresExistingAdapterWithoutChangingItsIdentity) {
  auto http = std::make_shared<FakeHttpClient>();
  ProviderAdapterRouter router;
  auto config = openAiConfig(QStringLiteral("openai-instance"));
  ASSERT_TRUE(router.add(config, http));

  config.settings = OpenAIProviderConfig{
      .base_url = QStringLiteral("https://replacement.test/v1"), .default_model = {}, .temperature = 0.25};
  ASSERT_TRUE(router.reconfigure(config));
  http->enqueueBufferedSuccess(R"({"data":[{"id":"gpt-new"}]})");
  ASSERT_TRUE(router.refresh(config.id));

  EXPECT_EQ(http->lastBufferedRequest().url, QStringLiteral("https://replacement.test/v1/models"));
  ASSERT_NE(router.availableModels(config.id), nullptr);
  EXPECT_EQ(router.availableModels(config.id)->front().provider_id, config.id);
  EXPECT_FALSE(router.reconfigure(openAiConfig(QStringLiteral("missing"))));
}

TEST(ProviderAdapterRouter, DisabledInstanceRejectsNewWorkButKeepsModelsIsolated) {
  auto http = std::make_shared<FakeHttpClient>();
  ProviderAdapterRouter router;
  auto config = openAiConfig(QStringLiteral("disabled-openai"));
  config.enabled = false;
  ASSERT_TRUE(router.add(config, http));

  EXPECT_FALSE(router.isEnabled(config.id));
  EXPECT_FALSE(router.refresh(config.id));
  EXPECT_EQ(http->bufferedCallCount(), 0);
  EXPECT_EQ(
      router.sendChat(ModelId{.provider_id = config.id, .model_name = QStringLiteral("gpt")}, {}, [](const auto&) {}),
      nullptr);
  EXPECT_EQ(http->streamingCallCount(), 0);
  EXPECT_NE(router.availableModels(config.id), nullptr);
}

TEST(ProviderAdapterRouter, DisablePreservesActiveStreamAndBlocksDeletionUntilTerminalEvent) {
  auto http = std::make_shared<FakeHttpClient>();
  ProviderAdapterRouter router;
  QSignalSpy streamCountChanged(&router, &ProviderAdapterRouter::activeStreamCountChanged);
  const auto config = openAiConfig(QStringLiteral("streaming-openai"));
  ASSERT_TRUE(router.add(config, http));
  auto request =
      router.sendChat(ModelId{.provider_id = config.id, .model_name = QStringLiteral("gpt")}, {}, [](const auto&) {});
  ASSERT_NE(request, nullptr);
  EXPECT_EQ(router.activeStreamCount(config.id), 1);
  ASSERT_EQ(streamCountChanged.count(), 1);
  EXPECT_EQ(streamCountChanged.takeFirst().at(0).toString(), config.id);

  ASSERT_TRUE(router.setEnabled(config.id, false));
  EXPECT_FALSE(router.remove(config.id));
  EXPECT_EQ(router.activeStreamCount(config.id), 1);
  http->emitFinished(0);

  EXPECT_EQ(router.activeStreamCount(config.id), 0);
  ASSERT_EQ(streamCountChanged.count(), 1);
  EXPECT_EQ(streamCountChanged.takeFirst().at(0).toString(), config.id);
  EXPECT_TRUE(router.canDelete(config.id));
  EXPECT_TRUE(router.remove(config.id));
}

TEST(ProviderAdapterRouter, SendsToolsArrayToAnthropicWhenToolCallingEnabled) {
  auto registry = std::make_shared<ToolRegistry>();
  registry->registerTool(std::make_shared<FakeTool>());
  auto http = std::make_shared<FakeHttpClient>();
  ProviderAdapterRouter router(registry);
  ASSERT_TRUE(router.add(anthropicConfig(QStringLiteral("anthropic-instance"), /*toolCallingEnabled=*/true), http));

  auto request = router.sendChat(
      ModelId{.provider_id = QStringLiteral("anthropic-instance"), .model_name = QStringLiteral("claude-x")}, {},
      [](const auto&) {});

  ASSERT_NE(request, nullptr);
  ASSERT_EQ(http->streamingCallCount(), 1U);
  const QJsonDocument body = QJsonDocument::fromJson(http->streamingCall(0).request.body);
  const QJsonArray tools = body.object().value(QStringLiteral("tools")).toArray();
  ASSERT_EQ(tools.size(), 1);
  EXPECT_EQ(tools[0].toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Fake"));
}

TEST(ProviderAdapterRouter, ToolCallKeepsAnthropicStreamActiveUntilTerminalEvent) {
  auto registry = std::make_shared<ToolRegistry>();
  registry->registerTool(std::make_shared<FakeTool>());
  auto http = std::make_shared<FakeHttpClient>();
  ProviderAdapterRouter router(registry);
  const QString instanceId = QStringLiteral("anthropic-instance");
  ASSERT_TRUE(router.add(anthropicConfig(instanceId, /*toolCallingEnabled=*/true), http));

  auto request = router.sendChat(ModelId{.provider_id = instanceId, .model_name = QStringLiteral("claude-x")}, {},
                                 [](const auto&) {});
  ASSERT_NE(request, nullptr);
  ASSERT_EQ(router.activeStreamCount(instanceId), 1);

  http->emitData(
      0,
      QByteArray(
          R"(data: {"type":"content_block_start","index":0,"content_block":{"type":"tool_use","id":"toolu_01","name":"Fake"}})") +
          "\n\n" + QByteArray(R"(data: {"type":"content_block_stop","index":0})") + "\n\n");

  EXPECT_EQ(router.activeStreamCount(instanceId), 1);
  EXPECT_FALSE(router.canDelete(instanceId));

  http->emitData(0, QByteArray(R"(data: {"type":"message_stop"})") + "\n\n");

  EXPECT_EQ(router.activeStreamCount(instanceId), 0);
  EXPECT_TRUE(router.canDelete(instanceId));
}

TEST(ProviderAdapterRouter, OmitsToolsArrayWhenAnthropicToolCallingDisabled) {
  auto registry = std::make_shared<ToolRegistry>();
  registry->registerTool(std::make_shared<FakeTool>());
  auto http = std::make_shared<FakeHttpClient>();
  ProviderAdapterRouter router(registry);
  ASSERT_TRUE(router.add(anthropicConfig(QStringLiteral("anthropic-instance"), /*toolCallingEnabled=*/false), http));

  auto request = router.sendChat(
      ModelId{.provider_id = QStringLiteral("anthropic-instance"), .model_name = QStringLiteral("claude-x")}, {},
      [](const auto&) {});

  ASSERT_NE(request, nullptr);
  ASSERT_EQ(http->streamingCallCount(), 1U);
  const QJsonDocument body = QJsonDocument::fromJson(http->streamingCall(0).request.body);
  EXPECT_FALSE(body.object().contains(QStringLiteral("tools")));
}

TEST(ProviderAdapterRouter, OmitsToolsArrayWithoutRegistryEvenWhenEnabled) {
  auto http = std::make_shared<FakeHttpClient>();
  ProviderAdapterRouter router;  // no ToolRegistry injected
  ASSERT_TRUE(router.add(anthropicConfig(QStringLiteral("anthropic-instance"), /*toolCallingEnabled=*/true), http));

  auto request = router.sendChat(
      ModelId{.provider_id = QStringLiteral("anthropic-instance"), .model_name = QStringLiteral("claude-x")}, {},
      [](const auto&) {});

  ASSERT_NE(request, nullptr);
  ASSERT_EQ(http->streamingCallCount(), 1U);
  const QJsonDocument body = QJsonDocument::fromJson(http->streamingCall(0).request.body);
  EXPECT_FALSE(body.object().contains(QStringLiteral("tools")));
}

TEST(ProviderAdapterRouter, SendsToolsArrayToGoogleWhenToolCallingEnabled) {
  auto registry = std::make_shared<ToolRegistry>();
  registry->registerTool(std::make_shared<FakeTool>());
  auto http = std::make_shared<FakeHttpClient>();
  ProviderAdapterRouter router(registry);
  ASSERT_TRUE(router.add(googleConfig(QStringLiteral("google-instance"), /*toolCallingEnabled=*/true), http));

  auto request = router.sendChat(
      ModelId{.provider_id = QStringLiteral("google-instance"), .model_name = QStringLiteral("gemini-2.0-flash")}, {},
      [](const auto&) {});

  ASSERT_NE(request, nullptr);
  ASSERT_EQ(http->streamingCallCount(), 1U);
  const QJsonDocument body = QJsonDocument::fromJson(http->streamingCall(0).request.body);
  const QJsonArray tools = body.object().value(QStringLiteral("tools")).toArray();
  ASSERT_EQ(tools.size(), 1);
  const QJsonArray declarations = tools[0].toObject().value(QStringLiteral("functionDeclarations")).toArray();
  ASSERT_EQ(declarations.size(), 1);
  EXPECT_EQ(declarations[0].toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Fake"));
}

TEST(ProviderAdapterRouter, OmitsToolsArrayWhenGoogleToolCallingDisabled) {
  auto registry = std::make_shared<ToolRegistry>();
  registry->registerTool(std::make_shared<FakeTool>());
  auto http = std::make_shared<FakeHttpClient>();
  ProviderAdapterRouter router(registry);
  ASSERT_TRUE(router.add(googleConfig(QStringLiteral("google-instance"), /*toolCallingEnabled=*/false), http));

  auto request = router.sendChat(
      ModelId{.provider_id = QStringLiteral("google-instance"), .model_name = QStringLiteral("gemini-2.0-flash")}, {},
      [](const auto&) {});

  ASSERT_NE(request, nullptr);
  ASSERT_EQ(http->streamingCallCount(), 1U);
  const QJsonDocument body = QJsonDocument::fromJson(http->streamingCall(0).request.body);
  EXPECT_FALSE(body.object().contains(QStringLiteral("tools")));
}

TEST(ProviderAdapterRouter, OmitsToolsArrayWithoutRegistryEvenWhenGoogleToolCallingEnabled) {
  auto http = std::make_shared<FakeHttpClient>();
  ProviderAdapterRouter router;  // no ToolRegistry injected
  ASSERT_TRUE(router.add(googleConfig(QStringLiteral("google-instance"), /*toolCallingEnabled=*/true), http));

  auto request = router.sendChat(
      ModelId{.provider_id = QStringLiteral("google-instance"), .model_name = QStringLiteral("gemini-2.0-flash")}, {},
      [](const auto&) {});

  ASSERT_NE(request, nullptr);
  ASSERT_EQ(http->streamingCallCount(), 1U);
  const QJsonDocument body = QJsonDocument::fromJson(http->streamingCall(0).request.body);
  EXPECT_FALSE(body.object().contains(QStringLiteral("tools")));
}

TEST(ProviderAdapterRouter, SendsToolsArrayToOllamaWhenToolCallingEnabled) {
  auto registry = std::make_shared<ToolRegistry>();
  registry->registerTool(std::make_shared<FakeTool>());
  auto http = std::make_shared<FakeHttpClient>();
  ProviderAdapterRouter router(registry);
  ASSERT_TRUE(router.add(ollamaConfig(QStringLiteral("ollama-instance"), /*toolCallingEnabled=*/true), http));

  auto request =
      router.sendChat(ModelId{.provider_id = QStringLiteral("ollama-instance"), .model_name = QStringLiteral("llama3")},
                      {}, [](const auto&) {});

  ASSERT_NE(request, nullptr);
  ASSERT_EQ(http->streamingCallCount(), 1U);
  const QJsonDocument body = QJsonDocument::fromJson(http->streamingCall(0).request.body);
  const QJsonArray tools = body.object().value(QStringLiteral("tools")).toArray();
  ASSERT_EQ(tools.size(), 1);
  EXPECT_EQ(tools[0].toObject().value(QStringLiteral("function")).toObject().value(QStringLiteral("name")).toString(),
            QStringLiteral("Fake"));
}

TEST(ProviderAdapterRouter, OmitsToolsArrayWhenOllamaToolCallingDisabled) {
  auto registry = std::make_shared<ToolRegistry>();
  registry->registerTool(std::make_shared<FakeTool>());
  auto http = std::make_shared<FakeHttpClient>();
  ProviderAdapterRouter router(registry);
  ASSERT_TRUE(router.add(ollamaConfig(QStringLiteral("ollama-instance"), /*toolCallingEnabled=*/false), http));

  auto request =
      router.sendChat(ModelId{.provider_id = QStringLiteral("ollama-instance"), .model_name = QStringLiteral("llama3")},
                      {}, [](const auto&) {});

  ASSERT_NE(request, nullptr);
  ASSERT_EQ(http->streamingCallCount(), 1U);
  const QJsonDocument body = QJsonDocument::fromJson(http->streamingCall(0).request.body);
  EXPECT_FALSE(body.object().contains(QStringLiteral("tools")));
}

TEST(ProviderAdapterRouter, OmitsToolsArrayWithoutRegistryEvenWhenOllamaToolCallingEnabled) {
  auto http = std::make_shared<FakeHttpClient>();
  ProviderAdapterRouter router;  // no ToolRegistry injected
  ASSERT_TRUE(router.add(ollamaConfig(QStringLiteral("ollama-instance"), /*toolCallingEnabled=*/true), http));

  auto request =
      router.sendChat(ModelId{.provider_id = QStringLiteral("ollama-instance"), .model_name = QStringLiteral("llama3")},
                      {}, [](const auto&) {});

  ASSERT_NE(request, nullptr);
  ASSERT_EQ(http->streamingCallCount(), 1U);
  const QJsonDocument body = QJsonDocument::fromJson(http->streamingCall(0).request.body);
  EXPECT_FALSE(body.object().contains(QStringLiteral("tools")));
}

TEST(ProviderAdapterRouter, SendsToolsToOpenAiOnlyWhenEnabledAndRegistryExists) {
  auto registry = std::make_shared<ToolRegistry>();
  registry->registerTool(std::make_shared<FakeTool>());
  auto http = std::make_shared<FakeHttpClient>();
  ProviderAdapterRouter router(registry);
  ASSERT_TRUE(
      router.add(openAiConfig(QStringLiteral("openai-tools"), QStringLiteral("https://example.test/v1"), true), http));

  const auto request =
      router.sendChat(ModelId{.provider_id = QStringLiteral("openai-tools"), .model_name = QStringLiteral("gpt")}, {},
                      [](const auto&) {});

  ASSERT_NE(request, nullptr);
  const QJsonObject body = QJsonDocument::fromJson(http->streamingCall(0).request.body).object();
  ASSERT_EQ(body.value(QStringLiteral("tools")).toArray().size(), 1);
  EXPECT_EQ(body.value(QStringLiteral("tools")).toArray().at(0).toObject().value(QStringLiteral("name")),
            QStringLiteral("Fake"));
}

}  // namespace
