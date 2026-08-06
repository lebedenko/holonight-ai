#pragma once

#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

#include <holonight_config/config_repository.h>
#include <memory>

class QAbstractItemModel;
class QQmlEngine;
class QJSEngine;

namespace holonight_application {

class ProviderAdapterRouter;
class ProviderDraftCommitter;
class ProviderDraftGuard;
class ProviderDraftSession;
class ProviderInstanceRegistry;
class ProviderInstanceDeleter;
class ProviderRuntimeCoordinator;

class ProviderManagementController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

  Q_PROPERTY(QAbstractItemModel* instances READ instances CONSTANT)
  Q_PROPERTY(QString selectedInstanceId READ selectedInstanceId NOTIFY selectionChanged)
  Q_PROPERTY(QString selectedProviderType READ selectedProviderType NOTIFY selectionChanged)
  Q_PROPERTY(QString displayName READ displayName WRITE setDisplayName NOTIFY draftChanged)
  Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY draftChanged)
  Q_PROPERTY(bool dirty READ dirty NOTIFY draftChanged)
  Q_PROPERTY(bool addition READ addition NOTIFY draftChanged)
  Q_PROPERTY(bool canSave READ canSave NOTIFY draftChanged)
  Q_PROPERTY(bool canDelete READ canDelete NOTIFY draftChanged)
  Q_PROPERTY(QString nameValidationError READ nameValidationError NOTIFY draftChanged)
  Q_PROPERTY(QString deletionExplanation READ deletionExplanation NOTIFY draftChanged)
  Q_PROPERTY(QStringList validationErrors READ validationErrors NOTIFY draftChanged)
  Q_PROPERTY(QString error READ error NOTIFY errorChanged)
  Q_PROPERTY(bool navigationPromptVisible READ navigationPromptVisible NOTIFY navigationPromptVisibleChanged)

 public:
  static ProviderManagementController* create(QQmlEngine* qml_engine, QJSEngine* js_engine);
  explicit ProviderManagementController(QQmlEngine* qml_engine, QObject* parent = nullptr);
  ~ProviderManagementController() override;
  ProviderManagementController(const ProviderManagementController&) = delete;
  ProviderManagementController& operator=(const ProviderManagementController&) = delete;
  ProviderManagementController(ProviderManagementController&&) = delete;
  ProviderManagementController& operator=(ProviderManagementController&&) = delete;

  [[nodiscard]] QAbstractItemModel* instances() const;
  [[nodiscard]] QString selectedInstanceId() const;
  [[nodiscard]] QString selectedProviderType() const;
  [[nodiscard]] QString displayName() const;
  void setDisplayName(QString name);
  [[nodiscard]] bool enabled() const;
  void setEnabled(bool enabled);
  [[nodiscard]] bool dirty() const;
  [[nodiscard]] bool addition() const;
  [[nodiscard]] bool canSave() const;
  [[nodiscard]] bool canDelete() const;
  [[nodiscard]] QString nameValidationError() const;
  [[nodiscard]] QString deletionExplanation() const;
  [[nodiscard]] QStringList validationErrors() const;
  [[nodiscard]] QString error() const;
  [[nodiscard]] bool navigationPromptVisible() const;

  Q_INVOKABLE bool addProvider(const QString& provider_type);
  Q_INVOKABLE void requestSelection(const QString& instance_id);
  Q_INVOKABLE void requestClose();
  Q_INVOKABLE bool save();
  Q_INVOKABLE void discardDraft();
  Q_INVOKABLE void discardAndContinue();
  Q_INVOKABLE void saveAndContinue();
  Q_INVOKABLE void cancelNavigation();
  Q_INVOKABLE bool deleteSelected(bool confirmed);

 Q_SIGNALS:
  void selectionChanged();
  void draftChanged();
  void errorChanged();
  void navigationPromptVisibleChanged();
  void closeApproved();

 private:
  void selectNow(const QString& instance_id);
  void setDraft(std::unique_ptr<ProviderDraftSession> draft);
  void retargetProviderController();
  void updateError();

  QQmlEngine* qml_engine_;
  holonight_config::ConfigRepository config_repository_;
  std::unique_ptr<ProviderInstanceRegistry> registry_;
  std::unique_ptr<ProviderDraftCommitter> committer_;
  std::unique_ptr<ProviderDraftGuard> guard_;
  std::unique_ptr<ProviderAdapterRouter> router_;
  std::unique_ptr<ProviderRuntimeCoordinator> runtime_coordinator_;
  std::unique_ptr<ProviderInstanceDeleter> deleter_;
  std::unique_ptr<ProviderDraftSession> draft_;
  bool navigation_prompt_visible_ = false;
};

}  // namespace holonight_application
