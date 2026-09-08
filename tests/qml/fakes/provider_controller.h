#pragma once

#include <QAbstractListModel>
#include <QString>

#include <vector>

struct ProviderRow {
  QString instance_id;
  QString provider_type;
  QString display_name;
  bool enabled = true;
};

class FakeProviderModel final : public QAbstractListModel {
  Q_OBJECT

 public:
  enum Role { InstanceIdRole = Qt::UserRole + 1, ProviderTypeRole, DisplayNameRole, EnabledRole };

  [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
  }

  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
      return {};
    }
    const auto& row = rows_[static_cast<std::size_t>(index.row())];
    switch (role) {
      case InstanceIdRole:
        return row.instance_id;
      case ProviderTypeRole:
        return row.provider_type;
      case DisplayNameRole:
        return row.display_name;
      case EnabledRole:
        return row.enabled;
      default:
        return {};
    }
  }

  [[nodiscard]] QHash<int, QByteArray> roleNames() const override {
    return {{InstanceIdRole, "instanceId"},
            {ProviderTypeRole, "providerType"},
            {DisplayNameRole, "displayName"},
            {EnabledRole, "enabled"}};
  }

  void prepend(ProviderRow row) {
    beginInsertRows({}, 0, 0);
    rows_.insert(rows_.begin(), std::move(row));
    endInsertRows();
  }

  [[nodiscard]] const ProviderRow& row(int index) const { return rows_.at(static_cast<std::size_t>(index)); }

 private:
  std::vector<ProviderRow> rows_;
};

class FakeProviderController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool addition MEMBER addition CONSTANT)
  Q_PROPERTY(QAbstractItemModel* instances READ instances CONSTANT)
  Q_PROPERTY(QString selectedInstanceId READ selectedInstanceId NOTIFY selectionChanged)
  Q_PROPERTY(QString selectedProviderType READ selectedProviderType NOTIFY selectionChanged)
  Q_PROPERTY(QString displayName READ displayName WRITE setDisplayName NOTIFY draftChanged)
  Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY draftChanged)
  Q_PROPERTY(bool dirty READ dirty WRITE setDirty NOTIFY draftChanged)
  Q_PROPERTY(bool canSave READ canSave WRITE setCanSave NOTIFY draftChanged)
  Q_PROPERTY(bool canDelete READ canDelete WRITE setCanDelete NOTIFY draftChanged)
  Q_PROPERTY(QString nameValidationError READ nameValidationError WRITE setNameValidationError NOTIFY draftChanged)
  Q_PROPERTY(QString deletionExplanation READ deletionExplanation WRITE setDeletionExplanation NOTIFY draftChanged)
  Q_PROPERTY(bool navigationPromptVisible READ navigationPromptVisible WRITE setNavigationPromptVisible NOTIFY
                 navigationPromptVisibleChanged)

 public:
  bool addition = false;
  [[nodiscard]] QAbstractItemModel* instances() { return &model; }
  [[nodiscard]] QString selectedInstanceId() const { return selected_instance_id_; }
  [[nodiscard]] QString selectedProviderType() const { return selected_provider_type_; }
  [[nodiscard]] QString displayName() const { return display_name_; }
  [[nodiscard]] bool enabled() const { return enabled_; }
  [[nodiscard]] bool dirty() const { return dirty_; }
  [[nodiscard]] bool canSave() const { return can_save_; }
  [[nodiscard]] bool canDelete() const { return can_delete_; }
  [[nodiscard]] QString nameValidationError() const { return name_validation_error_; }
  [[nodiscard]] QString deletionExplanation() const { return deletion_explanation_; }
  [[nodiscard]] bool navigationPromptVisible() const { return navigation_prompt_visible_; }

  void setDisplayName(QString value) {
    display_name_ = std::move(value);
    Q_EMIT draftChanged();
  }
  void setEnabled(bool value) {
    enabled_ = value;
    Q_EMIT draftChanged();
  }
  void setDirty(bool value) {
    dirty_ = value;
    Q_EMIT draftChanged();
  }
  void setCanSave(bool value) {
    can_save_ = value;
    Q_EMIT draftChanged();
  }
  void setCanDelete(bool value) {
    can_delete_ = value;
    Q_EMIT draftChanged();
  }
  void setNameValidationError(QString value) {
    name_validation_error_ = std::move(value);
    Q_EMIT draftChanged();
  }
  void setDeletionExplanation(QString value) {
    deletion_explanation_ = std::move(value);
    Q_EMIT draftChanged();
  }
  void setNavigationPromptVisible(bool value) {
    navigation_prompt_visible_ = value;
    Q_EMIT navigationPromptVisibleChanged();
  }

  Q_INVOKABLE bool addProvider(const QString& provider_type) {
    const QString instance_id = QStringLiteral("new-%1").arg(provider_type);
    model.prepend({.instance_id = instance_id,
                   .provider_type = provider_type,
                   .display_name = QStringLiteral("New provider"),
                   .enabled = true});
    selected_instance_id_ = instance_id;
    selected_provider_type_ = provider_type;
    Q_EMIT selectionChanged();
    return true;
  }
  Q_INVOKABLE void requestSelection(const QString& instance_id) {
    selected_instance_id_ = instance_id;
    Q_EMIT selectionChanged();
  }
  Q_INVOKABLE void requestClose() { ++request_close_count; }
  Q_INVOKABLE void cancelNavigation() { ++cancel_count; }
  Q_INVOKABLE void discardAndContinue() { ++discard_count; }
  Q_INVOKABLE void saveAndContinue() { ++save_count; }
  Q_INVOKABLE bool deleteSelected(bool confirmed) {
    if (confirmed) {
      ++delete_count;
    }
    return confirmed;
  }

  FakeProviderModel model;
  int request_close_count = 0;
  int cancel_count = 0;
  int discard_count = 0;
  int save_count = 0;
  int delete_count = 0;

 Q_SIGNALS:
  void selectionChanged();
  void draftChanged();
  void navigationPromptVisibleChanged();
  void closeApproved();

 private:
  QString selected_instance_id_;
  QString selected_provider_type_;
  QString display_name_ = QStringLiteral("Provider");
  QString name_validation_error_;
  QString deletion_explanation_;
  bool enabled_ = true;
  bool dirty_ = false;
  bool can_save_ = true;
  bool can_delete_ = true;
  bool navigation_prompt_visible_ = false;
};
