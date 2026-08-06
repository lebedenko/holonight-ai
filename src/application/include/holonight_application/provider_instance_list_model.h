#pragma once

#include <QAbstractListModel>

#include <cstdint>
#include <holonight_config/provider_config.h>
#include <optional>
#include <vector>

namespace holonight_application {

class ProviderInstanceListModel : public QAbstractListModel {
  Q_OBJECT

 public:
  enum Roles : std::uint16_t {  // NOLINT(cppcoreguidelines-use-enum-class): Qt model roles are int-compatible.
    InstanceIdRole = Qt::UserRole + 1,
    ProviderTypeRole,
    DisplayNameRole,
    EnabledRole,
    DraftRole,
  };
  Q_ENUM(Roles)

  explicit ProviderInstanceListModel(QObject* parent = nullptr);

  [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  [[nodiscard]] const holonight_config::ProviderInstanceConfig* find(const QString& instanceId) const;
  [[nodiscard]] bool containsName(const QString& displayName) const;
  [[nodiscard]] bool hasDraft() const;
  [[nodiscard]] std::optional<QString> draftId() const;

 private:
  friend class ProviderInstanceRegistry;

  struct Row {
    holonight_config::ProviderInstanceConfig config;
    bool draft = false;
  };

  void setSavedInstances(std::vector<holonight_config::ProviderInstanceConfig> instances);
  void prependDraft(holonight_config::ProviderInstanceConfig instance);
  bool removeDraft(const QString& instanceId);
  bool setEnabled(const QString& instanceId, bool enabled);
  bool removeSaved(const QString& instanceId);

  std::vector<Row> rows_;
};

}  // namespace holonight_application
