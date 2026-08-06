#pragma once

#include "holonight_application/provider_settings_controller_base.h"
#include "holonight_providers/ollama_provider.h"

#include <QtQml/qqmlregistration.h>

#include <memory>

class QQmlEngine;
class QJSEngine;

namespace holonight_application {

class ProviderDraftSession;

// QML-facing Ollama settings bridge. ProviderSettingsControllerBase owns the workflow shared with
// the OpenAI and Anthropic controllers; this class retains Ollama-specific draft fields,
// validation, and its stable QML singleton API.
class ProviderSettingsController : public ProviderSettingsControllerBase {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

  Q_PROPERTY(int contextWindow READ contextWindow WRITE setContextWindow NOTIFY contextWindowChanged)
  Q_PROPERTY(double temperature READ temperature WRITE setTemperature NOTIFY temperatureChanged)
  Q_PROPERTY(QString ollamaConnectionStatus READ ollamaConnectionStatus NOTIFY ollamaConnectionStatusChanged)
  Q_PROPERTY(
      bool toolCallingEnabled READ toolCallingEnabled WRITE setToolCallingEnabled NOTIFY toolCallingEnabledChanged)

 public:
  static ProviderSettingsController* create(QQmlEngine* qml_engine, QJSEngine* js_engine);

  explicit ProviderSettingsController(const std::shared_ptr<holonight_providers::OllamaProvider>& probe_provider,
                                      holonight_credentials::CredentialStore* credential_store,
                                      QObject* parent = nullptr);

  [[nodiscard]] int contextWindow() const;
  void setContextWindow(int value);
  [[nodiscard]] double temperature() const;
  void setTemperature(double value);
  [[nodiscard]] QString ollamaConnectionStatus() const;
  [[nodiscard]] bool toolCallingEnabled() const;
  void setToolCallingEnabled(bool value);
  void setDraftSession(ProviderDraftSession* draft_session);

  // Plain C++ accessor (not a Q_PROPERTY) so OpenAIProviderSettingsController::create() can reuse
  // this same CredentialStore instance instead of opening a second Secret Service session
  // (DESIGN.md §5.3).
  [[nodiscard]] holonight_credentials::CredentialStore* credentialStore() const {
    return ProviderSettingsControllerBase::credentialStore();
  }

  Q_INVOKABLE void load();
  Q_INVOKABLE void save();
  Q_INVOKABLE void cancel();
  Q_INVOKABLE void resetToDefaults();  // REQ-F-014

 Q_SIGNALS:
  void contextWindowChanged();
  void temperatureChanged();
  void ollamaConnectionStatusChanged();
  void toolCallingEnabledChanged();

 private:
  int context_window_ = 4096;
  double temperature_ = 0.7;
  bool tool_calling_enabled_ = false;
  bool loading_draft_ = false;

  void loadDraft();
  void updateDraft();
  void connectDraftProperties();
};

}  // namespace holonight_application
