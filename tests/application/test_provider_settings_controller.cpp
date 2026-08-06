#include "credentials/fake_credential_store.h"
#include "holonight_application/provider_draft_session.h"
#include "holonight_application/provider_settings_controller.h"
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

using holonight_config::OllamaProviderConfig;
using holonight_credentials::FakeCredentialStore;
using holonight_providers::FakeHttpClient;
using holonight_providers::OllamaProvider;

struct Backends {
  std::shared_ptr<FakeHttpClient> probe_http = std::make_shared<FakeHttpClient>();
  std::shared_ptr<OllamaProvider> probe_provider = std::make_shared<OllamaProvider>(probe_http);
  FakeCredentialStore credential_store;
};

ProviderDraftSession makeDraft(OllamaProviderConfig settings = {}) {
  return ProviderDraftSession(std::nullopt, {.id = QStringLiteral("ollama-work"),
                                             .type = holonight_config::ProviderType::Ollama,
                                             .display_name = QStringLiteral("Work"),
                                             .settings = std::move(settings)});
}

TEST(ProviderSettingsController, DraftSessionRetargetsAndStagesOllamaFields) {
  Backends backends;
  ProviderSettingsController controller(backends.probe_provider, &backends.credential_store);
  auto draft = makeDraft({.base_url = QStringLiteral("http://work"),
                          .default_model = QStringLiteral("llama3.2"),
                          .context_window = 8192,
                          .temperature = 0.4,
                          .tool_calling_enabled = true});

  controller.setDraftSession(&draft);

  EXPECT_EQ(controller.baseUrl(), QStringLiteral("http://work"));
  EXPECT_EQ(controller.defaultModel(), QStringLiteral("llama3.2"));
  EXPECT_EQ(controller.contextWindow(), 8192);
  EXPECT_DOUBLE_EQ(controller.temperature(), 0.4);
  EXPECT_TRUE(controller.toolCallingEnabled());

  controller.setContextWindow(16384);
  controller.setTemperature(1.1);
  controller.setToolCallingEnabled(false);
  const auto& settings = std::get<OllamaProviderConfig>(draft.editable().settings);
  EXPECT_EQ(settings.context_window, 16384);
  EXPECT_DOUBLE_EQ(settings.temperature, 1.1);
  EXPECT_FALSE(settings.tool_calling_enabled);
}

TEST(ProviderSettingsController, DraftCredentialEditUsesInstanceId) {
  Backends backends;
  ProviderSettingsController controller(backends.probe_provider, &backends.credential_store);
  auto draft = makeDraft();
  controller.setDraftSession(&draft);

  controller.setAuthToken(QStringLiteral("token"));

  EXPECT_EQ(draft.credentialEdit(), ProviderDraftSession::CredentialEdit::Store);
  EXPECT_EQ(draft.credentialValue(), QStringLiteral("token"));
}

TEST(ProviderSettingsController, RefreshModelsUsesDraftUrlAndProbeProvider) {
  Backends backends;
  ProviderSettingsController controller(backends.probe_provider, &backends.credential_store);
  auto draft = makeDraft({.base_url = QStringLiteral("http://edited:4242")});
  controller.setDraftSession(&draft);
  backends.probe_http->enqueueBufferedSuccess(QByteArray(R"({"models":[{"name":"llama3"}]})"));

  controller.refreshModels();

  EXPECT_EQ(backends.probe_http->lastBufferedRequest().url, QStringLiteral("http://edited:4242/api/tags"));
  EXPECT_EQ(controller.availableModelNames(), QStringList{QStringLiteral("llama3")});
}

TEST(ProviderSettingsController, RefreshModelsFailureDoesNotOverwriteConnectionTestMessage) {
  Backends backends;
  ProviderSettingsController controller(backends.probe_provider, &backends.credential_store);
  auto draft = makeDraft();
  controller.setDraftSession(&draft);
  backends.probe_http->enqueueBufferedError(QStringLiteral("connection refused"));

  controller.refreshModels();

  EXPECT_FALSE(controller.modelRefreshError().isEmpty());
  EXPECT_TRUE(controller.testConnectionMessage().isEmpty());
}

TEST(ProviderSettingsController, TestConnectionUpdatesOllamaConnectionStatus) {
  Backends backends;
  ProviderSettingsController controller(backends.probe_provider, &backends.credential_store);
  auto draft = makeDraft();
  controller.setDraftSession(&draft);
  backends.probe_http->enqueueBufferedSuccess(QByteArray(R"({"models":[]})"));

  controller.testConnection();

  EXPECT_EQ(controller.testConnectionStatus(), QStringLiteral("success"));
  EXPECT_EQ(controller.ollamaConnectionStatus(), QStringLiteral("connected"));
}

TEST(ProviderSettingsController, CancelDiscardsDraftEdits) {
  Backends backends;
  ProviderSettingsController controller(backends.probe_provider, &backends.credential_store);
  auto draft = makeDraft({.base_url = QStringLiteral("http://saved"), .context_window = 8192});
  controller.setDraftSession(&draft);
  controller.setBaseUrl(QStringLiteral("http://edited"));
  controller.setContextWindow(16384);

  controller.cancel();

  EXPECT_EQ(controller.baseUrl(), QStringLiteral("http://saved"));
  EXPECT_EQ(controller.contextWindow(), 8192);
  EXPECT_FALSE(draft.dirty());
}

TEST(ProviderSettingsController, ResetToDefaultsUpdatesDraftOnly) {
  Backends backends;
  ProviderSettingsController controller(backends.probe_provider, &backends.credential_store);
  auto draft = makeDraft({.base_url = QStringLiteral("http://custom"),
                          .context_window = 8192,
                          .temperature = 1.2,
                          .tool_calling_enabled = true});
  controller.setDraftSession(&draft);
  controller.setAuthToken(QStringLiteral("token"));

  controller.resetToDefaults();

  const OllamaProviderConfig defaults;
  EXPECT_EQ(controller.baseUrl(), defaults.base_url);
  EXPECT_EQ(controller.contextWindow(), defaults.context_window);
  EXPECT_DOUBLE_EQ(controller.temperature(), defaults.temperature);
  EXPECT_EQ(controller.toolCallingEnabled(), defaults.tool_calling_enabled);
  EXPECT_TRUE(controller.authToken().isEmpty());
  EXPECT_TRUE(draft.dirty());
}

}  // namespace
}  // namespace holonight_application
