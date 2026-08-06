#include "credentials/fake_credential_store.h"
#include "holonight_application/anthropic_provider_settings_controller.h"
#include "holonight_application/provider_draft_session.h"
#include "providers/fake_http_client.h"

#include <QByteArray>
#include <QString>

#include <gtest/gtest.h>
#include <holonight_config/provider_config.h>
#include <memory>
#include <optional>
#include <utility>

namespace holonight_application {
namespace {

using holonight_config::AnthropicProviderConfig;
using holonight_credentials::FakeCredentialStore;
using holonight_providers::AnthropicProvider;
using holonight_providers::FakeHttpClient;

struct Backends {
  std::shared_ptr<FakeHttpClient> probe_http = std::make_shared<FakeHttpClient>();
  std::shared_ptr<AnthropicProvider> probe_provider = std::make_shared<AnthropicProvider>(probe_http);
  FakeCredentialStore credential_store;
};

ProviderDraftSession makeDraft(AnthropicProviderConfig settings = {}) {
  return ProviderDraftSession(std::nullopt, {.id = QStringLiteral("anthropic-work"),
                                             .type = holonight_config::ProviderType::Anthropic,
                                             .display_name = QStringLiteral("Work"),
                                             .settings = std::move(settings)});
}

TEST(AnthropicProviderSettingsController, DraftSessionRetargetsAndStagesAnthropicFields) {
  Backends backends;
  AnthropicProviderSettingsController controller(backends.probe_provider, &backends.credential_store);
  auto draft = makeDraft({.base_url = QStringLiteral("https://work"),
                          .default_model = QStringLiteral("claude-3-5-sonnet-20241022"),
                          .temperature = 0.4,
                          .max_output_tokens = 2048,
                          .tool_calling_enabled = true});

  controller.setDraftSession(&draft);

  EXPECT_EQ(controller.baseUrl(), QStringLiteral("https://work"));
  EXPECT_EQ(controller.defaultModel(), QStringLiteral("claude-3-5-sonnet-20241022"));
  EXPECT_DOUBLE_EQ(controller.temperature(), 0.4);
  EXPECT_EQ(controller.maxOutputTokens(), 2048);
  EXPECT_TRUE(controller.toolCallingEnabled());
  controller.setMaxOutputTokens(4096);
  EXPECT_EQ(std::get<AnthropicProviderConfig>(draft.editable().settings).max_output_tokens, 4096);
  controller.setToolCallingEnabled(false);
  EXPECT_FALSE(std::get<AnthropicProviderConfig>(draft.editable().settings).tool_calling_enabled);
}

TEST(AnthropicProviderSettingsController, DraftCredentialEditUsesInstanceId) {
  Backends backends;
  AnthropicProviderSettingsController controller(backends.probe_provider, &backends.credential_store);
  auto draft = makeDraft();
  controller.setDraftSession(&draft);

  controller.setAuthToken(QStringLiteral("sk-ant-token"));

  EXPECT_EQ(draft.credentialEdit(), ProviderDraftSession::CredentialEdit::Store);
  EXPECT_EQ(draft.credentialValue(), QStringLiteral("sk-ant-token"));
}

TEST(AnthropicProviderSettingsController, RefreshModelsUsesDraftUrlAndProbeProvider) {
  Backends backends;
  AnthropicProviderSettingsController controller(backends.probe_provider, &backends.credential_store);
  auto draft = makeDraft({.base_url = QStringLiteral("https://edited.test")});
  controller.setDraftSession(&draft);
  backends.probe_http->enqueueBufferedSuccess(QByteArray(
      R"({"data":[{"id":"claude-3-5-sonnet-20241022"},{"id":"claude-3-vision"},{"id":"claude-3-haiku-20250219"}]})"));

  controller.refreshModels();

  EXPECT_EQ(backends.probe_http->lastBufferedRequest().url, QStringLiteral("https://edited.test/v1/models"));
  QStringList names = controller.availableModelNames();
  names.sort();
  EXPECT_EQ(names,
            (QStringList{QStringLiteral("claude-3-5-sonnet-20241022"), QStringLiteral("claude-3-haiku-20250219")}));
}

TEST(AnthropicProviderSettingsController, RefreshModelsFailureDoesNotOverwriteConnectionTestMessage) {
  Backends backends;
  AnthropicProviderSettingsController controller(backends.probe_provider, &backends.credential_store);
  auto draft = makeDraft();
  controller.setDraftSession(&draft);
  backends.probe_http->enqueueBufferedError(QStringLiteral("invalid_api_key"));

  controller.refreshModels();

  EXPECT_FALSE(controller.modelRefreshError().isEmpty());
  EXPECT_TRUE(controller.testConnectionMessage().isEmpty());
}

TEST(AnthropicProviderSettingsController, TestConnectionUpdatesAnthropicConnectionStatus) {
  Backends backends;
  AnthropicProviderSettingsController controller(backends.probe_provider, &backends.credential_store);
  auto draft = makeDraft();
  controller.setDraftSession(&draft);
  backends.probe_http->enqueueBufferedSuccess(QByteArray(R"({"data":[{"id":"claude-3-5-sonnet-20241022"}]})"));

  controller.testConnection();

  EXPECT_EQ(controller.testConnectionStatus(), QStringLiteral("success"));
  EXPECT_EQ(controller.anthropicConnectionStatus(), QStringLiteral("connected"));
}

TEST(AnthropicProviderSettingsController, CancelDiscardsDraftEdits) {
  Backends backends;
  AnthropicProviderSettingsController controller(backends.probe_provider, &backends.credential_store);
  auto draft = makeDraft({.base_url = QStringLiteral("https://saved"),
                          .temperature = 0.5,
                          .max_output_tokens = 2048,
                          .tool_calling_enabled = false});
  controller.setDraftSession(&draft);
  controller.setBaseUrl(QStringLiteral("https://edited"));
  controller.setTemperature(0.9);
  controller.setMaxOutputTokens(4096);
  controller.setToolCallingEnabled(true);

  controller.cancel();

  EXPECT_EQ(controller.baseUrl(), QStringLiteral("https://saved"));
  EXPECT_DOUBLE_EQ(controller.temperature(), 0.5);
  EXPECT_EQ(controller.maxOutputTokens(), 2048);
  EXPECT_FALSE(controller.toolCallingEnabled());
  EXPECT_FALSE(draft.dirty());
}

TEST(AnthropicProviderSettingsController, ResetToDefaultsUpdatesDraftOnly) {
  Backends backends;
  AnthropicProviderSettingsController controller(backends.probe_provider, &backends.credential_store);
  auto draft = makeDraft({.base_url = QStringLiteral("https://custom"),
                          .temperature = 0.9,
                          .max_output_tokens = 16384,
                          .tool_calling_enabled = true});
  controller.setDraftSession(&draft);
  controller.setAuthToken(QStringLiteral("sk-ant-token"));

  controller.resetToDefaults();

  const AnthropicProviderConfig defaults;
  EXPECT_EQ(controller.baseUrl(), defaults.base_url);
  EXPECT_DOUBLE_EQ(controller.temperature(), defaults.temperature);
  EXPECT_EQ(controller.maxOutputTokens(), defaults.max_output_tokens);
  EXPECT_EQ(controller.toolCallingEnabled(), defaults.tool_calling_enabled);
  EXPECT_TRUE(controller.authToken().isEmpty());
  EXPECT_TRUE(draft.dirty());
}

}  // namespace
}  // namespace holonight_application
