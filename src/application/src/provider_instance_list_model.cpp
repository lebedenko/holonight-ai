#include "holonight_application/provider_instance_list_model.h"

#include <QModelIndex>
#include <QVariant>

#include <algorithm>

namespace holonight_application {

ProviderInstanceListModel::ProviderInstanceListModel(QObject* parent) : QAbstractListModel(parent) {}

int ProviderInstanceListModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant ProviderInstanceListModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || static_cast<std::size_t>(index.row()) >= rows_.size()) {
    return {};
  }

  const Row& row = rows_[static_cast<std::size_t>(index.row())];
  switch (role) {
    case InstanceIdRole:
      return row.config.id;
    case ProviderTypeRole:
      return holonight_config::providerTypeToString(row.config.type);
    case DisplayNameRole:
      return row.config.display_name;
    case EnabledRole:
      return row.config.enabled;
    case DraftRole:
      return row.draft;
    default:
      return {};
  }
}

QHash<int, QByteArray> ProviderInstanceListModel::roleNames() const {
  return {
      {InstanceIdRole, QByteArrayLiteral("instanceId")},
      {ProviderTypeRole, QByteArrayLiteral("providerType")},
      {DisplayNameRole, QByteArrayLiteral("displayName")},
      {EnabledRole, QByteArrayLiteral("enabled")},
      {DraftRole, QByteArrayLiteral("draft")},
  };
}

const holonight_config::ProviderInstanceConfig* ProviderInstanceListModel::find(const QString& instanceId) const {
  const auto match = std::ranges::find_if(rows_, [&instanceId](const Row& row) { return row.config.id == instanceId; });
  return match == rows_.end() ? nullptr : &match->config;
}

bool ProviderInstanceListModel::containsName(const QString& displayName) const {
  const QString normalizedName = displayName.trimmed().toCaseFolded();
  return std::ranges::any_of(rows_, [&normalizedName](const Row& row) {
    return row.config.display_name.trimmed().toCaseFolded() == normalizedName;
  });
}

bool ProviderInstanceListModel::hasDraft() const {
  return std::ranges::any_of(rows_, [](const Row& row) { return row.draft; });
}

std::optional<QString> ProviderInstanceListModel::draftId() const {
  const auto match = std::ranges::find_if(rows_, [](const Row& row) { return row.draft; });
  if (match == rows_.end()) {
    return std::nullopt;
  }
  return match->config.id;
}

void ProviderInstanceListModel::setSavedInstances(std::vector<holonight_config::ProviderInstanceConfig> instances) {
  beginResetModel();
  rows_.clear();
  rows_.reserve(instances.size());
  for (holonight_config::ProviderInstanceConfig& instance : instances) {
    rows_.push_back(Row{.config = std::move(instance), .draft = false});
  }
  endResetModel();
}

void ProviderInstanceListModel::prependDraft(holonight_config::ProviderInstanceConfig instance) {
  beginInsertRows(QModelIndex(), 0, 0);
  rows_.insert(rows_.begin(), Row{.config = std::move(instance), .draft = true});
  endInsertRows();
}

bool ProviderInstanceListModel::removeDraft(const QString& instanceId) {
  const auto match =
      std::ranges::find_if(rows_, [&instanceId](const Row& row) { return row.draft && row.config.id == instanceId; });
  if (match == rows_.end()) {
    return false;
  }

  const int row = static_cast<int>(std::distance(rows_.begin(), match));
  beginRemoveRows(QModelIndex(), row, row);
  rows_.erase(match);
  endRemoveRows();
  return true;
}

bool ProviderInstanceListModel::setEnabled(const QString& instanceId, bool enabled) {
  const auto match = std::ranges::find_if(rows_, [&instanceId](const Row& row) { return row.config.id == instanceId; });
  if (match == rows_.end() || match->config.enabled == enabled) {
    return match != rows_.end();
  }
  match->config.enabled = enabled;
  const QModelIndex changed = index(static_cast<int>(std::distance(rows_.begin(), match)));
  emit dataChanged(changed, changed, {EnabledRole});
  return true;
}

bool ProviderInstanceListModel::removeSaved(const QString& instanceId) {
  const auto match =
      std::ranges::find_if(rows_, [&instanceId](const Row& row) { return !row.draft && row.config.id == instanceId; });
  if (match == rows_.end()) {
    return false;
  }
  const int row = static_cast<int>(std::distance(rows_.begin(), match));
  beginRemoveRows(QModelIndex(), row, row);
  rows_.erase(match);
  endRemoveRows();
  return true;
}

}  // namespace holonight_application
