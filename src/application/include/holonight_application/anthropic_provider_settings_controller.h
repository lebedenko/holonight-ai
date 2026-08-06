#pragma once

#include "holonight_application/provider_settings_controller_base.h"
#include "holonight_providers/anthropic_provider.h"

#include <QtQml/qqmlregistration.h>

#include <memory>

class QQmlEngine;
class QJSEngine;

namespace holonight_application {

class ProviderDraftSession;

// QML-facing Anthropic settings bridge. ProviderSettingsControllerBase owns the workflow shared
// with the Ollama and OpenAI controllers; this class retains Anthropic-specific temperature and
// output-token state and its stable QML singleton API.
class AnthropicProviderSettingsController : public ProviderSettingsControllerBase {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

  Q_PROPERTY(double temperature READ temperature WRITE setTemperature NOTIFY temperatureChanged)
  Q_PROPERTY(int maxOutputTokens READ maxOutputTokens WRITE setMaxOutputTokens NOTIFY maxOutputTokensChanged)
  Q_PROPERTY(
      bool toolCallingEnabled READ toolCallingEnabled WRITE setToolCallingEnabled NOTIFY toolCallingEnabledChanged)
  Q_PROPERTY(QString anthropicConnectionStatus READ anthropicConnectionStatus NOTIFY anthropicConnectionStatusChanged)

 public:
  static AnthropicProviderSettingsController* create(QQmlEngine* qml_engine, QJSEngine* js_engine);

  explicit AnthropicProviderSettingsController(
      const std::shared_ptr<holonight_providers::AnthropicProvider>& probe_provider,
      holonight_credentials::CredentialStore* credential_store, QObject* parent = nullptr);

  [[nodiscard]] double temperature() const;
  void setTemperature(double value);
  [[nodiscard]] int maxOutputTokens() const;
  void setMaxOutputTokens(int value);
  [[nodiscard]] bool toolCallingEnabled() const;
  void setToolCallingEnabled(bool value);
  [[nodiscard]] QString anthropicConnectionStatus() const;
  void setDraftSession(ProviderDraftSession* draft_session);

  Q_INVOKABLE void load();
  Q_INVOKABLE void save();
  Q_INVOKABLE void cancel();
  Q_INVOKABLE void resetToDefaults();

 Q_SIGNALS:
  void temperatureChanged();
  void maxOutputTokensChanged();
  void toolCallingEnabledChanged();
  void anthropicConnectionStatusChanged();

 private:
  double temperature_ = 1.0;
  int max_output_tokens_ = 1024;
  bool tool_calling_enabled_ = false;
  bool loading_draft_ = false;

  void loadDraft();
  void updateDraft();
  void connectDraftProperties();
};

}  // namespace holonight_application
