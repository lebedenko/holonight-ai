#include "holonight_application/openai_provider_settings_controller.h"

#include "holonight_application/chat_view_model.h"
#include "holonight_application/provider_draft_session.h"
#include "holonight_providers/qt_network_http_client.h"

#include <QQmlEngine>

#include <utility>

namespace holonight_application {

using holonight_credentials::CredentialStore;
using holonight_domain::ModelId;
using holonight_providers::OpenAIProvider;

OpenAIProviderSettingsController* OpenAIProviderSettingsController::create(QQmlEngine* qml_engine,
                                                                           QJSEngine* js_engine) {
  Q_UNUSED(js_engine)
  auto* chatViewModel = qml_engine->singletonInstance<ChatViewModel*>("HolonightChat", "ChatViewModel");
  Q_ASSERT(chatViewModel != nullptr);

  auto probeHttpClient = std::make_shared<holonight_providers::QtNetworkHttpClient>();
  auto probeProvider = std::make_shared<OpenAIProvider>(std::move(probeHttpClient));

  return new OpenAIProviderSettingsController(probeProvider, chatViewModel->credentialStoreForSettings());
}

OpenAIProviderSettingsController::OpenAIProviderSettingsController(
    const std::shared_ptr<OpenAIProvider>& probe_provider, CredentialStore* credential_store, QObject* parent)
    : ProviderSettingsControllerBase(QStringLiteral("openai"), credential_store,
                                     ProviderOperations{
                                         .configure_probe =
                                             [probe_provider](const QString& base_url, const QString& credential) {
                                               probe_provider->setBaseUrl(base_url);
                                               probe_provider->setAuthToken(credential);
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

void OpenAIProviderSettingsController::connectDraftProperties() {
  connect(this, &ProviderSettingsControllerBase::connectionStatusChanged, this,
          &OpenAIProviderSettingsController::openAiConnectionStatusChanged);
  connect(this, &ProviderSettingsControllerBase::baseUrlChanged, this, &OpenAIProviderSettingsController::updateDraft);
  connect(this, &ProviderSettingsControllerBase::defaultModelChanged, this,
          &OpenAIProviderSettingsController::updateDraft);
  connect(this, &OpenAIProviderSettingsController::temperatureChanged, this,
          &OpenAIProviderSettingsController::updateDraft);
  connect(this, &OpenAIProviderSettingsController::toolCallingEnabledChanged, this,
          &OpenAIProviderSettingsController::updateDraft);
}

double OpenAIProviderSettingsController::temperature() const { return temperature_; }

void OpenAIProviderSettingsController::setTemperature(double value) {
  if (temperature_ == value) {
    return;
  }
  temperature_ = value;
  emit temperatureChanged();
}

bool OpenAIProviderSettingsController::toolCallingEnabled() const { return tool_calling_enabled_; }

void OpenAIProviderSettingsController::setToolCallingEnabled(bool value) {
  if (tool_calling_enabled_ == value) {
    return;
  }
  tool_calling_enabled_ = value;
  emit toolCallingEnabledChanged();
}

QString OpenAIProviderSettingsController::openAiConnectionStatus() const { return connectionStatus(); }

void OpenAIProviderSettingsController::setDraftSession(ProviderDraftSession* draft_session) {
  if (draft_session == nullptr || draft_session->editable().type != holonight_config::ProviderType::OpenAi) {
    return;
  }
  retargetDraft(draft_session);
  connect(draft_session, &ProviderDraftSession::changed, this, &OpenAIProviderSettingsController::loadDraft,
          Qt::UniqueConnection);
  loadDraft();
  beginLoad();
}

void OpenAIProviderSettingsController::loadDraft() {
  if (draftSession() == nullptr || loading_draft_) {
    return;
  }
  const auto* config = std::get_if<holonight_config::OpenAIProviderConfig>(&draftSession()->editable().settings);
  if (config == nullptr) {
    return;
  }
  loading_draft_ = true;
  setBaseUrl(config->base_url);
  setDefaultModel(config->default_model);
  setTemperature(config->temperature);
  setToolCallingEnabled(config->tool_calling_enabled);
  loading_draft_ = false;
}

void OpenAIProviderSettingsController::updateDraft() {
  if (draftSession() == nullptr || loading_draft_) {
    return;
  }
  (void)draftSession()->setSettings(
      holonight_config::OpenAIProviderConfig{.base_url = baseUrl(),
                                             .default_model = defaultModel(),
                                             .temperature = temperature_,
                                             .tool_calling_enabled = tool_calling_enabled_});
}

void OpenAIProviderSettingsController::load() {
  loadDraft();
  beginLoad();
}

void OpenAIProviderSettingsController::save() {
  if (draftSession() == nullptr) {
    return;
  }
  updateDraft();
  setSaveNotice(draftSession()->validate() ? tr("Settings are ready to save.") : tr("Fix validation errors."),
                draftSession()->validationErrors().isEmpty() ? QStringLiteral("success") : QStringLiteral("error"));
}

void OpenAIProviderSettingsController::cancel() {
  if (draftSession() == nullptr) {
    return;
  }
  clearTransientState();
  draftSession()->discard();
}

void OpenAIProviderSettingsController::resetToDefaults() {
  if (draftSession() == nullptr) {
    return;
  }
  draftSession()->resetToDefaults();
  loadDraft();
  setAuthToken(QString());
}

}  // namespace holonight_application
