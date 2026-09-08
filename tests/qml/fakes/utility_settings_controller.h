#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>

// Mirrors UtilitySettingsController's QML-visible surface (docs/sdd/background-ai-settings-panel)
// so BackgroundAiSettingsPanel.qml can be exercised without a real ConfigRepository/ChatViewModel —
// the same technique test_provider_management.cpp already uses for ProviderManagementController via
// FakeProviderController.
class FakeUtilitySettingsController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList providerInstances READ providerInstances WRITE setProviderInstances NOTIFY changed)
  Q_PROPERTY(QString defaultProviderId READ defaultProviderId WRITE setDefaultProviderId NOTIFY changed)
  Q_PROPERTY(QString defaultModelName READ defaultModelName WRITE setDefaultModelName NOTIFY changed)
  Q_PROPERTY(QStringList defaultModelNames READ defaultModelNames WRITE setDefaultModelNames NOTIFY changed)
  Q_PROPERTY(bool chatTitleGenerationEnabled READ chatTitleGenerationEnabled WRITE setChatTitleGenerationEnabled NOTIFY
                 changed)
  Q_PROPERTY(
      QString titleOverrideProviderId READ titleOverrideProviderId WRITE setTitleOverrideProviderId NOTIFY changed)
  Q_PROPERTY(QString titleOverrideModelName READ titleOverrideModelName WRITE setTitleOverrideModelName NOTIFY changed)
  Q_PROPERTY(
      QStringList titleOverrideModelNames READ titleOverrideModelNames WRITE setTitleOverrideModelNames NOTIFY changed)
  Q_PROPERTY(bool dirty READ dirty WRITE setDirty NOTIFY changed)
  Q_PROPERTY(bool canSave READ canSave WRITE setCanSave NOTIFY changed)
  Q_PROPERTY(QString saveNotice READ saveNotice WRITE setSaveNotice NOTIFY changed)
  Q_PROPERTY(QString saveNoticeStatus READ saveNoticeStatus WRITE setSaveNoticeStatus NOTIFY changed)

 public:
  [[nodiscard]] QVariantList providerInstances() const { return provider_instances_; }
  void setProviderInstances(QVariantList value) {
    provider_instances_ = std::move(value);
    Q_EMIT changed();
  }
  [[nodiscard]] QString defaultProviderId() const { return default_provider_id_; }
  void setDefaultProviderId(QString value) {
    default_provider_id_ = std::move(value);
    Q_EMIT changed();
  }
  [[nodiscard]] QString defaultModelName() const { return default_model_name_; }
  void setDefaultModelName(QString value) {
    default_model_name_ = std::move(value);
    Q_EMIT changed();
  }
  [[nodiscard]] QStringList defaultModelNames() const { return default_model_names_; }
  void setDefaultModelNames(QStringList value) {
    default_model_names_ = std::move(value);
    Q_EMIT changed();
  }
  [[nodiscard]] bool chatTitleGenerationEnabled() const { return chat_title_generation_enabled_; }
  void setChatTitleGenerationEnabled(bool value) {
    chat_title_generation_enabled_ = value;
    Q_EMIT changed();
  }
  [[nodiscard]] QString titleOverrideProviderId() const { return title_override_provider_id_; }
  void setTitleOverrideProviderId(QString value) {
    title_override_provider_id_ = std::move(value);
    Q_EMIT changed();
  }
  [[nodiscard]] QString titleOverrideModelName() const { return title_override_model_name_; }
  void setTitleOverrideModelName(QString value) {
    title_override_model_name_ = std::move(value);
    Q_EMIT changed();
  }
  [[nodiscard]] QStringList titleOverrideModelNames() const { return title_override_model_names_; }
  void setTitleOverrideModelNames(QStringList value) {
    title_override_model_names_ = std::move(value);
    Q_EMIT changed();
  }
  [[nodiscard]] bool dirty() const { return dirty_; }
  void setDirty(bool value) {
    dirty_ = value;
    Q_EMIT changed();
  }
  [[nodiscard]] bool canSave() const { return can_save_; }
  void setCanSave(bool value) {
    can_save_ = value;
    Q_EMIT changed();
  }
  [[nodiscard]] QString saveNotice() const { return save_notice_; }
  void setSaveNotice(QString value) {
    save_notice_ = std::move(value);
    Q_EMIT changed();
  }
  [[nodiscard]] QString saveNoticeStatus() const { return save_notice_status_; }
  void setSaveNoticeStatus(QString value) {
    save_notice_status_ = std::move(value);
    Q_EMIT changed();
  }

  Q_INVOKABLE bool save() {
    ++save_count_;
    return true;
  }
  Q_INVOKABLE void discardDraft() { ++discard_count_; }
  Q_INVOKABLE void refreshModelsForInstance(const QString& providerInstanceId) {
    last_refresh_requested_ = providerInstanceId;
  }

  [[nodiscard]] int saveCount() const { return save_count_; }
  [[nodiscard]] int discardCount() const { return discard_count_; }
  [[nodiscard]] QString lastRefreshRequested() const { return last_refresh_requested_; }

 Q_SIGNALS:
  void changed();

 private:
  QVariantList provider_instances_;
  QString default_provider_id_;
  QString default_model_name_;
  QStringList default_model_names_;
  bool chat_title_generation_enabled_ = true;
  QString title_override_provider_id_;
  QString title_override_model_name_;
  QStringList title_override_model_names_;
  bool dirty_ = false;
  bool can_save_ = false;
  QString save_notice_;
  QString save_notice_status_ = QStringLiteral("idle");
  int save_count_ = 0;
  int discard_count_ = 0;
  QString last_refresh_requested_;
};
