#include "credentials/fake_credential_store.h"
#include "holonight_application/openai_provider_settings_controller.h"
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

using holonight_config::OpenAIProviderConfig;
using holonight_credentials::FakeCredentialStore;
using holonight_providers::FakeHttpClient;
using holonight_providers::OpenAIProvider;

struct Backends {
  std::shared_ptr<FakeHttpClient> probe_http = std::make_shared<FakeHttpClient>();
  std::shared_ptr<OpenAIProvider> probe_provider = std::make_shared<OpenAIProvider>(probe_http);
  FakeCredentialStore credential_store;
};

ProviderDraftSession makeDraft(OpenAIProviderConfig settings = {}) {
  return ProviderDraftSession(std::nullopt, {.id = QStringLiteral("openai-work"),
                                             .type = holonight_config::ProviderType::OpenAi,
                                             .display_name = QStringLiteral("Work"),
                                             .settings = std::move(settings)});
}

TEST(OpenAIProviderSettingsController, DraftSessionRetargetsAndStagesOpenAiFields) {
  Backends backends;
  OpenAIProviderSettingsController controller(backends.probe_provider, &backends.credential_store);
  auto draft = makeDraft({.base_url = QStringLiteral("https://work"),
                          .default_model = QStringLiteral("gpt-4o"),
                          .temperature = 0.5,
                          .tool_calling_enabled = true});

  controller.setDraftSession(&draft);

  EXPECT_EQ(controller.baseUrl(), QStringLiteral("https://work"));
  EXPECT_EQ(controller.defaultModel(), QStringLiteral("gpt-4o"));
  EXPECT_DOUBLE_EQ(controller.temperature(), 0.5);
  EXPECT_TRUE(controller.toolCallingEnabled());
  controller.setTemperature(0.25);
  controller.setToolCallingEnabled(false);
  EXPECT_DOUBLE_EQ(std::get<OpenAIProviderConfig>(draft.editable().settings).temperature, 0.25);
  EXPECT_FALSE(std::get<OpenAIProviderConfig>(draft.editable().settings).tool_calling_enabled);
}

TEST(OpenAIProviderSettingsController, DraftCredentialEditUsesInstanceId) {
  Backends backends;
  OpenAIProviderSettingsController controller(backends.probe_provider, &backends.credential_store);
  auto draft = makeDraft();
  controller.setDraftSession(&draft);

  controller.setAuthToken(QStringLiteral("sk-token"));

  EXPECT_EQ(draft.credentialEdit(), ProviderDraftSession::CredentialEdit::Store);
  EXPECT_EQ(draft.credentialValue(), QStringLiteral("sk-token"));
}

TEST(OpenAIProviderSettingsController, RefreshModelsUsesDraftUrlAndProbeProvider) {
  Backends backends;
  OpenAIProviderSettingsController controller(backends.probe_provider, &backends.credential_store);
  auto draft = makeDraft({.base_url = QStringLiteral("https://edited.test/v1")});
  controller.setDraftSession(&draft);
  backends.probe_http->enqueueBufferedSuccess(
      QByteArray(R"({"data":[{"id":"gpt-4o"},{"id":"whisper-1"},{"id":"gpt-4o-mini"}]})"));

  controller.refreshModels();

  EXPECT_EQ(backends.probe_http->lastBufferedRequest().url, QStringLiteral("https://edited.test/v1/models"));
  QStringList names = controller.availableModelNames();
  names.sort();
  EXPECT_EQ(names, (QStringList{QStringLiteral("gpt-4o"), QStringLiteral("gpt-4o-mini")}));
}

TEST(OpenAIProviderSettingsController, RefreshModelsFailureDoesNotOverwriteConnectionTestMessage) {
  Backends backends;
  OpenAIProviderSettingsController controller(backends.probe_provider, &backends.credential_store);
  auto draft = makeDraft();
  controller.setDraftSession(&draft);
  backends.probe_http->enqueueBufferedError(QStringLiteral("invalid_api_key"));

  controller.refreshModels();

  EXPECT_FALSE(controller.modelRefreshError().isEmpty());
  EXPECT_TRUE(controller.testConnectionMessage().isEmpty());
}

TEST(OpenAIProviderSettingsController, TestConnectionUpdatesOpenAiConnectionStatus) {
  Backends backends;
  OpenAIProviderSettingsController controller(backends.probe_provider, &backends.credential_store);
  auto draft = makeDraft();
  controller.setDraftSession(&draft);
  backends.probe_http->enqueueBufferedSuccess(QByteArray(R"({"data":[{"id":"gpt-4o"}]})"));

  controller.testConnection();

  EXPECT_EQ(controller.testConnectionStatus(), QStringLiteral("success"));
  EXPECT_EQ(controller.openAiConnectionStatus(), QStringLiteral("connected"));
}

TEST(OpenAIProviderSettingsController, CancelDiscardsDraftEdits) {
  Backends backends;
  OpenAIProviderSettingsController controller(backends.probe_provider, &backends.credential_store);
  auto draft = makeDraft({.base_url = QStringLiteral("https://saved"), .temperature = 0.5});
  controller.setDraftSession(&draft);
  controller.setBaseUrl(QStringLiteral("https://edited"));
  controller.setTemperature(1.5);

  controller.cancel();

  EXPECT_EQ(controller.baseUrl(), QStringLiteral("https://saved"));
  EXPECT_DOUBLE_EQ(controller.temperature(), 0.5);
  EXPECT_FALSE(draft.dirty());
}

TEST(OpenAIProviderSettingsController, ResetToDefaultsUpdatesDraftOnly) {
  Backends backends;
  OpenAIProviderSettingsController controller(backends.probe_provider, &backends.credential_store);
  auto draft = makeDraft({.base_url = QStringLiteral("https://custom"), .temperature = 1.5});
  controller.setDraftSession(&draft);
  controller.setAuthToken(QStringLiteral("sk-token"));

  controller.resetToDefaults();

  const OpenAIProviderConfig defaults;
  EXPECT_EQ(controller.baseUrl(), defaults.base_url);
  EXPECT_DOUBLE_EQ(controller.temperature(), defaults.temperature);
  EXPECT_TRUE(controller.authToken().isEmpty());
  EXPECT_TRUE(draft.dirty());
}

}  // namespace
}  // namespace holonight_application
