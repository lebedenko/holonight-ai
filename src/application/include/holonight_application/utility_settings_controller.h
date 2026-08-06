#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <holonight_config/config_repository.h>
#include <holonight_config/provider_config.h>
#include <holonight_config/utility_config.h>
#include <memory>

class QQmlEngine;
class QJSEngine;

namespace holonight_application {

class ProviderAdapterRouter;
class ProviderRuntimeCoordinator;

// Independent draft/save/discard controller for the Background AI settings panel
// (docs/sdd/background-ai-settings-panel). Owns its own ProviderAdapterRouter +
// ProviderRuntimeCoordinator pair — deliberately not shared with ChatViewModel's or
// ProviderManagementController's — so its model pickers stay independent of the chat window's
// currently-selected provider, and so utility-specific generation parameters (temperature 0.3,
// small max-tokens) can be injected without touching the chat-configured settings for the same
// provider instance.
class UtilitySettingsController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

  Q_PROPERTY(QVariantList providerInstances READ providerInstances NOTIFY providerInstancesChanged)

  Q_PROPERTY(QString defaultProviderId READ defaultProviderId WRITE setDefaultProviderId NOTIFY draftChanged)
  Q_PROPERTY(QString defaultModelName READ defaultModelName WRITE setDefaultModelName NOTIFY draftChanged)
  Q_PROPERTY(QStringList defaultModelNames READ defaultModelNames NOTIFY defaultModelNamesChanged)

  Q_PROPERTY(bool chatTitleGenerationEnabled READ chatTitleGenerationEnabled WRITE setChatTitleGenerationEnabled NOTIFY
                 draftChanged)
  Q_PROPERTY(
      QString titleOverrideProviderId READ titleOverrideProviderId WRITE setTitleOverrideProviderId NOTIFY draftChanged)
  Q_PROPERTY(
      QString titleOverrideModelName READ titleOverrideModelName WRITE setTitleOverrideModelName NOTIFY draftChanged)
  Q_PROPERTY(QStringList titleOverrideModelNames READ titleOverrideModelNames NOTIFY titleOverrideModelNamesChanged)

  Q_PROPERTY(bool dirty READ dirty NOTIFY draftChanged)
  Q_PROPERTY(bool canSave READ canSave NOTIFY draftChanged)
  Q_PROPERTY(QString saveNotice READ saveNotice NOTIFY saveNoticeChanged)
  Q_PROPERTY(QString saveNoticeStatus READ saveNoticeStatus NOTIFY saveNoticeChanged)

 public:
  static UtilitySettingsController* create(QQmlEngine* qml_engine, QJSEngine* js_engine);
  explicit UtilitySettingsController(QQmlEngine* qml_engine, QObject* parent = nullptr);
  ~UtilitySettingsController() override;
  UtilitySettingsController(const UtilitySettingsController&) = delete;
  UtilitySettingsController& operator=(const UtilitySettingsController&) = delete;
  UtilitySettingsController(UtilitySettingsController&&) = delete;
  UtilitySettingsController& operator=(UtilitySettingsController&&) = delete;

  [[nodiscard]] QVariantList providerInstances() const;

  [[nodiscard]] QString defaultProviderId() const;
  void setDefaultProviderId(const QString& provider_id);
  [[nodiscard]] QString defaultModelName() const;
  void setDefaultModelName(const QString& model_name);
  [[nodiscard]] QStringList defaultModelNames() const;

  [[nodiscard]] bool chatTitleGenerationEnabled() const;
  void setChatTitleGenerationEnabled(bool enabled);
  [[nodiscard]] QString titleOverrideProviderId() const;
  void setTitleOverrideProviderId(const QString& provider_id);
  [[nodiscard]] QString titleOverrideModelName() const;
  void setTitleOverrideModelName(const QString& model_name);
  [[nodiscard]] QStringList titleOverrideModelNames() const;

  [[nodiscard]] bool dirty() const;
  [[nodiscard]] bool canSave() const;
  [[nodiscard]] QString saveNotice() const;
  [[nodiscard]] QString saveNoticeStatus() const;

  Q_INVOKABLE bool save();
  Q_INVOKABLE void discardDraft();
  Q_INVOKABLE void refreshModelsForInstance(const QString& providerInstanceId);

  void applyProviderState(holonight_config::ProviderState provider_state);

 Q_SIGNALS:
  void providerInstancesChanged();
  void draftChanged();
  void defaultModelNamesChanged();
  void titleOverrideModelNamesChanged();
  void saveNoticeChanged();

 private:
  void onProviderChanged(const QString& providerId);
  void setSaveNotice(QString text, QString status);
  // REQ-F-020/021: a selected override whose provider instance is no longer enabled/present is
  // silently dropped (reset to the null sentinel) rather than blocking Save.
  void reconcileDraftAgainstProviderState();
  [[nodiscard]] bool isDraftValid() const;

  QQmlEngine* qml_engine_;
  holonight_config::ConfigRepository config_repository_;
  std::unique_ptr<ProviderAdapterRouter> router_;
  std::unique_ptr<ProviderRuntimeCoordinator> runtime_coordinator_;
  holonight_config::ProviderState provider_state_;
  holonight_config::UtilityConfig saved_;
  holonight_config::UtilityConfig draft_;
  QString save_notice_;
  QString save_notice_status_ = QStringLiteral("idle");
};

}  // namespace holonight_application
