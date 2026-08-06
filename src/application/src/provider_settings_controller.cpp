#include "holonight_application/provider_settings_controller.h"

#include "holonight_application/chat_view_model.h"
#include "holonight_application/provider_draft_session.h"
#include "holonight_providers/qt_network_http_client.h"

#include <QQmlEngine>

#include <utility>

namespace holonight_application {

using holonight_credentials::CredentialStore;
using holonight_domain::ModelId;
using holonight_providers::OllamaProvider;

ProviderSettingsController* ProviderSettingsController::create(QQmlEngine* qml_engine, QJSEngine* js_engine) {
  Q_UNUSED(js_engine)
  auto* chatViewModel = qml_engine->singletonInstance<ChatViewModel*>("HolonightChat", "ChatViewModel");
  Q_ASSERT(chatViewModel != nullptr);

  auto probeHttpClient = std::make_shared<holonight_providers::QtNetworkHttpClient>();
  auto probeProvider = std::make_shared<OllamaProvider>(std::move(probeHttpClient));

  return new ProviderSettingsController(probeProvider, chatViewModel->credentialStoreForSettings());
}

ProviderSettingsController::ProviderSettingsController(const std::shared_ptr<OllamaProvider>& probe_provider,
                                                       CredentialStore* credential_store, QObject* parent)
    : ProviderSettingsControllerBase(QStringLiteral("ollama"), credential_store,
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
                                     false, parent) {
  connectDraftProperties();
}

void ProviderSettingsController::connectDraftProperties() {
  connect(this, &ProviderSettingsControllerBase::connectionStatusChanged, this,
          &ProviderSettingsController::ollamaConnectionStatusChanged);
  connect(this, &ProviderSettingsControllerBase::baseUrlChanged, this, &ProviderSettingsController::updateDraft);
  connect(this, &ProviderSettingsControllerBase::defaultModelChanged, this, &ProviderSettingsController::updateDraft);
  connect(this, &ProviderSettingsController::contextWindowChanged, this, &ProviderSettingsController::updateDraft);
  connect(this, &ProviderSettingsController::temperatureChanged, this, &ProviderSettingsController::updateDraft);
  connect(this, &ProviderSettingsController::toolCallingEnabledChanged, this, &ProviderSettingsController::updateDraft);
}

int ProviderSettingsController::contextWindow() const { return context_window_; }

void ProviderSettingsController::setContextWindow(int value) {
  if (context_window_ == value) {
    return;
  }
  context_window_ = value;
  emit contextWindowChanged();
}

double ProviderSettingsController::temperature() const { return temperature_; }

void ProviderSettingsController::setTemperature(double value) {
  if (temperature_ == value) {
    return;
  }
  temperature_ = value;
  emit temperatureChanged();
}

QString ProviderSettingsController::ollamaConnectionStatus() const { return connectionStatus(); }

bool ProviderSettingsController::toolCallingEnabled() const { return tool_calling_enabled_; }

void ProviderSettingsController::setToolCallingEnabled(bool value) {
  if (tool_calling_enabled_ == value) {
    return;
  }
  tool_calling_enabled_ = value;
  emit toolCallingEnabledChanged();
}

void ProviderSettingsController::setDraftSession(ProviderDraftSession* draft_session) {
  if (draft_session == nullptr || draft_session->editable().type != holonight_config::ProviderType::Ollama) {
    return;
  }
  retargetDraft(draft_session);
  connect(draft_session, &ProviderDraftSession::changed, this, &ProviderSettingsController::loadDraft,
          Qt::UniqueConnection);
  loadDraft();
  beginLoad();
}

void ProviderSettingsController::loadDraft() {
  if (draftSession() == nullptr || loading_draft_) {
    return;
  }
  const auto* config = std::get_if<holonight_config::OllamaProviderConfig>(&draftSession()->editable().settings);
  if (config == nullptr) {
    return;
  }
  loading_draft_ = true;
  setBaseUrl(config->base_url);
  setDefaultModel(config->default_model);
  setContextWindow(config->context_window);
  setTemperature(config->temperature);
  setToolCallingEnabled(config->tool_calling_enabled);
  loading_draft_ = false;
}

void ProviderSettingsController::updateDraft() {
  if (draftSession() == nullptr || loading_draft_) {
    return;
  }
  (void)draftSession()->setSettings(
      holonight_config::OllamaProviderConfig{.base_url = baseUrl(),
                                             .default_model = defaultModel(),
                                             .context_window = context_window_,
                                             .temperature = temperature_,
                                             .tool_calling_enabled = tool_calling_enabled_});
}

void ProviderSettingsController::load() {
  loadDraft();
  beginLoad();
}

void ProviderSettingsController::save() {
  if (draftSession() == nullptr) {
    return;
  }
  updateDraft();
  setSaveNotice(draftSession()->validate() ? tr("Settings are ready to save.") : tr("Fix validation errors."),
                draftSession()->validationErrors().isEmpty() ? QStringLiteral("success") : QStringLiteral("error"));
}

void ProviderSettingsController::cancel() {
  if (draftSession() == nullptr) {
    return;
  }
  clearTransientState();
  draftSession()->discard();
}

void ProviderSettingsController::resetToDefaults() {
  if (draftSession() == nullptr) {
    return;
  }
  draftSession()->resetToDefaults();
  loadDraft();
  setAuthToken(QString());
}

}  // namespace holonight_application
