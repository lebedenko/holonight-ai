#include "credentials/fake_credential_store.h"
#include "holonight_application/google_provider_settings_controller.h"
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

using holonight_config::GoogleProviderConfig;
using holonight_credentials::FakeCredentialStore;
using holonight_providers::FakeHttpClient;
using holonight_providers::GoogleProvider;

struct Backends {
  std::shared_ptr<FakeHttpClient> probe_http = std::make_shared<FakeHttpClient>();
  std::shared_ptr<GoogleProvider> probe_provider = std::make_shared<GoogleProvider>(probe_http);
  FakeCredentialStore credential_store;
};

ProviderDraftSession makeDraft(GoogleProviderConfig settings = {}) {
  return ProviderDraftSession(std::nullopt, {.id = QStringLiteral("google-work"),
                                             .type = holonight_config::ProviderType::Google,
                                             .display_name = QStringLiteral("Work"),
                                             .settings = std::move(settings)});
}

TEST(GoogleProviderSettingsController, DraftSessionRetargetsAndStagesGoogleFields) {
  Backends backends;
  GoogleProviderSettingsController controller(backends.probe_provider, &backends.credential_store);
  auto draft = makeDraft({.base_url = QStringLiteral("https://work"),
                          .default_model = QStringLiteral("gemini-2.0-flash"),
                          .temperature = 0.6,
                          .max_output_tokens = 16384,
                          .tool_calling_enabled = true});

  controller.setDraftSession(&draft);

  EXPECT_EQ(controller.baseUrl(), QStringLiteral("https://work"));
  EXPECT_EQ(controller.defaultModel(), QStringLiteral("gemini-2.0-flash"));
  EXPECT_DOUBLE_EQ(controller.temperature(), 0.6);
  EXPECT_EQ(controller.maxOutputTokens(), 16384);
  EXPECT_TRUE(controller.toolCallingEnabled());
  controller.setTemperature(0.2);
  EXPECT_DOUBLE_EQ(std::get<GoogleProviderConfig>(draft.editable().settings).temperature, 0.2);
  controller.setToolCallingEnabled(false);
  EXPECT_FALSE(std::get<GoogleProviderConfig>(draft.editable().settings).tool_calling_enabled);
}

TEST(GoogleProviderSettingsController, DraftCredentialEditUsesInstanceId) {
  Backends backends;
  GoogleProviderSettingsController controller(backends.probe_provider, &backends.credential_store);
  auto draft = makeDraft();
  controller.setDraftSession(&draft);

  controller.setAuthToken(QStringLiteral("goog-token"));

  EXPECT_EQ(draft.credentialEdit(), ProviderDraftSession::CredentialEdit::Store);
  EXPECT_EQ(draft.credentialValue(), QStringLiteral("goog-token"));
}

TEST(GoogleProviderSettingsController, RefreshModelsUsesDraftUrlAndProbeProvider) {
  Backends backends;
  GoogleProviderSettingsController controller(backends.probe_provider, &backends.credential_store);
  auto draft = makeDraft({.base_url = QStringLiteral("https://edited.test")});
  controller.setDraftSession(&draft);
  backends.probe_http->enqueueBufferedSuccess(QByteArray(R"({"models":[
    {"name":"models/gemini-2.0-flash","supportedGenerationMethods":["generateContent"]},
    {"name":"models/gemini-embedding-001","supportedGenerationMethods":["embedContent"]}
  ]})"));

  controller.refreshModels();

  EXPECT_EQ(backends.probe_http->lastBufferedRequest().url,
            QStringLiteral("https://edited.test/v1beta/models?pageSize=1000"));
  EXPECT_EQ(controller.availableModelNames(), QStringList{QStringLiteral("gemini-2.0-flash")});
}

TEST(GoogleProviderSettingsController, RefreshModelsFailureDoesNotOverwriteConnectionTestMessage) {
  Backends backends;
  GoogleProviderSettingsController controller(backends.probe_provider, &backends.credential_store);
  auto draft = makeDraft();
  controller.setDraftSession(&draft);
  backends.probe_http->enqueueBufferedError(QStringLiteral("invalid_api_key"));

  controller.refreshModels();

  EXPECT_FALSE(controller.modelRefreshError().isEmpty());
  EXPECT_TRUE(controller.testConnectionMessage().isEmpty());
}

TEST(GoogleProviderSettingsController, TestConnectionUpdatesGoogleConnectionStatus) {
  Backends backends;
  GoogleProviderSettingsController controller(backends.probe_provider, &backends.credential_store);
  auto draft = makeDraft();
  controller.setDraftSession(&draft);
  backends.probe_http->enqueueBufferedSuccess(QByteArray(
      R"({"models":[{"name":"models/gemini-2.0-flash","supportedGenerationMethods":["generateContent"]}]})"));

  controller.testConnection();

  EXPECT_EQ(controller.testConnectionStatus(), QStringLiteral("success"));
  EXPECT_EQ(controller.googleConnectionStatus(), QStringLiteral("connected"));
}

TEST(GoogleProviderSettingsController, CancelDiscardsDraftEdits) {
  Backends backends;
  GoogleProviderSettingsController controller(backends.probe_provider, &backends.credential_store);
  auto draft = makeDraft({.base_url = QStringLiteral("https://saved"),
                          .temperature = 0.5,
                          .max_output_tokens = 2048,
                          .tool_calling_enabled = false});
  controller.setDraftSession(&draft);
  controller.setBaseUrl(QStringLiteral("https://edited"));
  controller.setTemperature(1.8);
  controller.setMaxOutputTokens(4096);
  controller.setToolCallingEnabled(true);

  controller.cancel();

  EXPECT_EQ(controller.baseUrl(), QStringLiteral("https://saved"));
  EXPECT_DOUBLE_EQ(controller.temperature(), 0.5);
  EXPECT_EQ(controller.maxOutputTokens(), 2048);
  EXPECT_FALSE(controller.toolCallingEnabled());
  EXPECT_FALSE(draft.dirty());
}

TEST(GoogleProviderSettingsController, ResetToDefaultsUpdatesDraftOnly) {
  Backends backends;
  GoogleProviderSettingsController controller(backends.probe_provider, &backends.credential_store);
  auto draft = makeDraft({.base_url = QStringLiteral("https://custom"),
                          .temperature = 1.8,
                          .max_output_tokens = 16384,
                          .tool_calling_enabled = true});
  controller.setDraftSession(&draft);
  controller.setAuthToken(QStringLiteral("goog-token"));

  controller.resetToDefaults();

  const GoogleProviderConfig defaults;
  EXPECT_EQ(controller.baseUrl(), defaults.base_url);
  EXPECT_DOUBLE_EQ(controller.temperature(), defaults.temperature);
  EXPECT_EQ(controller.maxOutputTokens(), defaults.max_output_tokens);
  EXPECT_EQ(controller.toolCallingEnabled(), defaults.tool_calling_enabled);
  EXPECT_TRUE(controller.authToken().isEmpty());
  EXPECT_TRUE(draft.dirty());
}

}  // namespace
}  // namespace holonight_application
