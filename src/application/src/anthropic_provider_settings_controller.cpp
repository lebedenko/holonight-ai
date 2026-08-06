#include "holonight_application/anthropic_provider_settings_controller.h"

#include "holonight_application/chat_view_model.h"
#include "holonight_application/provider_draft_session.h"
#include "holonight_providers/qt_network_http_client.h"

#include <QQmlEngine>

#include <utility>

namespace holonight_application {

using holonight_credentials::CredentialStore;
using holonight_domain::ModelId;
using holonight_providers::AnthropicProvider;

AnthropicProviderSettingsController* AnthropicProviderSettingsController::create(QQmlEngine* qml_engine,
                                                                                 QJSEngine* js_engine) {
  Q_UNUSED(js_engine)
  auto* chatViewModel = qml_engine->singletonInstance<ChatViewModel*>("HolonightChat", "ChatViewModel");
  Q_ASSERT(chatViewModel != nullptr);

  auto probeHttpClient = std::make_shared<holonight_providers::QtNetworkHttpClient>();
  auto probeProvider = std::make_shared<AnthropicProvider>(std::move(probeHttpClient));
  return new AnthropicProviderSettingsController(probeProvider, chatViewModel->credentialStoreForSettings());
}

AnthropicProviderSettingsController::AnthropicProviderSettingsController(
    const std::shared_ptr<AnthropicProvider>& probe_provider, CredentialStore* credential_store, QObject* parent)
    : ProviderSettingsControllerBase(QStringLiteral("anthropic"), credential_store,
                                     ProviderOperations{
                                         .configure_probe =
                                             [probe_provider](const QString& base_url, const QString& credential) {
                                               probe_provider->setBaseUrl(base_url);
                                               probe_provider->setAuthKey(credential);
                                             },
                                         .refresh_probe =
                                             [probe_provider](const auto& on_success, const auto& on_error) {
                                               probe_provider->refresh(on_success, on_error);
                                             },
                                         .probe_models = [probe_provider]() -> const std::vector<ModelId>& {
                                           return probe_provider->availableModels();
                                         },
                                     },
                                     true, parent) {
  connectDraftProperties();
}

void AnthropicProviderSettingsController::connectDraftProperties() {
  connect(this, &ProviderSettingsControllerBase::connectionStatusChanged, this,
          &AnthropicProviderSettingsController::anthropicConnectionStatusChanged);
  connect(this, &ProviderSettingsControllerBase::baseUrlChanged, this,
          &AnthropicProviderSettingsController::updateDraft);
  connect(this, &ProviderSettingsControllerBase::defaultModelChanged, this,
          &AnthropicProviderSettingsController::updateDraft);
  connect(this, &AnthropicProviderSettingsController::temperatureChanged, this,
          &AnthropicProviderSettingsController::updateDraft);
  connect(this, &AnthropicProviderSettingsController::maxOutputTokensChanged, this,
          &AnthropicProviderSettingsController::updateDraft);
  connect(this, &AnthropicProviderSettingsController::toolCallingEnabledChanged, this,
          &AnthropicProviderSettingsController::updateDraft);
}

double AnthropicProviderSettingsController::temperature() const { return temperature_; }

void AnthropicProviderSettingsController::setTemperature(double value) {
  if (temperature_ == value) {
    return;
  }
  temperature_ = value;
  emit temperatureChanged();
}

int AnthropicProviderSettingsController::maxOutputTokens() const { return max_output_tokens_; }

void AnthropicProviderSettingsController::setMaxOutputTokens(int value) {
  if (max_output_tokens_ == value) {
    return;
  }
  max_output_tokens_ = value;
  emit maxOutputTokensChanged();
}

bool AnthropicProviderSettingsController::toolCallingEnabled() const { return tool_calling_enabled_; }

void AnthropicProviderSettingsController::setToolCallingEnabled(bool value) {
  if (tool_calling_enabled_ == value) {
    return;
  }
  tool_calling_enabled_ = value;
  emit toolCallingEnabledChanged();
}

QString AnthropicProviderSettingsController::anthropicConnectionStatus() const { return connectionStatus(); }

void AnthropicProviderSettingsController::setDraftSession(ProviderDraftSession* draft_session) {
  if (draft_session == nullptr || draft_session->editable().type != holonight_config::ProviderType::Anthropic) {
    return;
  }
  retargetDraft(draft_session);
  connect(draft_session, &ProviderDraftSession::changed, this, &AnthropicProviderSettingsController::loadDraft,
          Qt::UniqueConnection);
  loadDraft();
  beginLoad();
}

void AnthropicProviderSettingsController::loadDraft() {
  if (draftSession() == nullptr || loading_draft_) {
    return;
  }
  const auto* config = std::get_if<holonight_config::AnthropicProviderConfig>(&draftSession()->editable().settings);
  if (config == nullptr) {
    return;
  }
  loading_draft_ = true;
  setBaseUrl(config->base_url);
  setDefaultModel(config->default_model);
  setTemperature(config->temperature);
  setMaxOutputTokens(config->max_output_tokens);
  setToolCallingEnabled(config->tool_calling_enabled);
  loading_draft_ = false;
}

void AnthropicProviderSettingsController::updateDraft() {
  if (draftSession() == nullptr || loading_draft_) {
    return;
  }
  (void)draftSession()->setSettings(
      holonight_config::AnthropicProviderConfig{.base_url = baseUrl(),
                                                .default_model = defaultModel(),
                                                .temperature = temperature_,
                                                .max_output_tokens = max_output_tokens_,
                                                .tool_calling_enabled = tool_calling_enabled_});
}

void AnthropicProviderSettingsController::load() {
  loadDraft();
  beginLoad();
}

void AnthropicProviderSettingsController::save() {
  if (draftSession() == nullptr) {
    return;
  }
  updateDraft();
  setSaveNotice(draftSession()->validate() ? tr("Settings are ready to save.") : tr("Fix validation errors."),
                draftSession()->validationErrors().isEmpty() ? QStringLiteral("success") : QStringLiteral("error"));
}

void AnthropicProviderSettingsController::cancel() {
  if (draftSession() == nullptr) {
    return;
  }
  clearTransientState();
  draftSession()->discard();
}

void AnthropicProviderSettingsController::resetToDefaults() {
  if (draftSession() == nullptr) {
    return;
  }
  draftSession()->resetToDefaults();
  loadDraft();
  setAuthToken(QString());
}

}  // namespace holonight_application
