#pragma once

#include "holonight_application/provider_settings_controller_base.h"
#include "holonight_providers/openai_provider.h"

#include <QtQml/qqmlregistration.h>

#include <memory>

class QQmlEngine;
class QJSEngine;

namespace holonight_application {

class ProviderDraftSession;

// QML-facing OpenAI settings bridge. ProviderSettingsControllerBase owns the workflow shared with
// the Ollama and Anthropic controllers; this class retains OpenAI-specific temperature state and
// its stable QML singleton API.
class OpenAIProviderSettingsController : public ProviderSettingsControllerBase {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

  Q_PROPERTY(double temperature READ temperature WRITE setTemperature NOTIFY temperatureChanged)
  Q_PROPERTY(
      bool toolCallingEnabled READ toolCallingEnabled WRITE setToolCallingEnabled NOTIFY toolCallingEnabledChanged)
  Q_PROPERTY(QString openAiConnectionStatus READ openAiConnectionStatus NOTIFY openAiConnectionStatusChanged)

 public:
  static OpenAIProviderSettingsController* create(QQmlEngine* qml_engine, QJSEngine* js_engine);

  explicit OpenAIProviderSettingsController(const std::shared_ptr<holonight_providers::OpenAIProvider>& probe_provider,
                                            holonight_credentials::CredentialStore* credential_store,
                                            QObject* parent = nullptr);

  [[nodiscard]] double temperature() const;
  void setTemperature(double value);
  [[nodiscard]] bool toolCallingEnabled() const;
  void setToolCallingEnabled(bool value);
  [[nodiscard]] QString openAiConnectionStatus() const;
  void setDraftSession(ProviderDraftSession* draft_session);

  Q_INVOKABLE void load();
  Q_INVOKABLE void save();
  Q_INVOKABLE void cancel();
  Q_INVOKABLE void resetToDefaults();

 Q_SIGNALS:
  void temperatureChanged();
  void toolCallingEnabledChanged();
  void openAiConnectionStatusChanged();

 private:
  double temperature_ = 1.0;
  bool tool_calling_enabled_ = false;
  bool loading_draft_ = false;

  void loadDraft();
  void updateDraft();
  void connectDraftProperties();
};

}  // namespace holonight_application
