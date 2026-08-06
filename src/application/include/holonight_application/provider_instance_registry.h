#pragma once

#include "holonight_application/provider_instance_list_model.h"

#include <QHash>
#include <QObject>
#include <QString>

#include <holonight_config/provider_config.h>
#include <holonight_domain/model_id.h>
#include <optional>
#include <vector>

namespace holonight_application {

struct HistoricalProviderIdentity {
  QString instance_id;
  std::optional<holonight_config::ProviderType> type;
  QString display_name;
  bool tombstone = false;

  friend bool operator==(const HistoricalProviderIdentity&, const HistoricalProviderIdentity&) = default;
};

[[nodiscard]] HistoricalProviderIdentity resolveHistoricalProviderIdentity(const holonight_config::ProviderState& state,
                                                                           const QString& instance_id);

class ProviderInstanceRegistry : public QObject {
  Q_OBJECT

 public:
  explicit ProviderInstanceRegistry(holonight_config::ProviderState state = {}, QObject* parent = nullptr);

  [[nodiscard]] ProviderInstanceListModel* instances();
  [[nodiscard]] const ProviderInstanceListModel* instances() const;
  [[nodiscard]] const holonight_config::ProviderState& savedState() const;
  [[nodiscard]] QString selectedSettingsInstanceId() const;
  [[nodiscard]] std::optional<holonight_domain::ModelId> selectedChatModel() const;
  [[nodiscard]] HistoricalProviderIdentity historicalIdentity(const QString& instance_id) const;

  [[nodiscard]] std::optional<holonight_config::ProviderInstanceConfig> addDraft(holonight_config::ProviderType type);
  [[nodiscard]] bool cancelDraft();
  [[nodiscard]] bool selectSettingsInstance(const QString& instanceId);
  void clearSettingsSelection();

  void setRuntimeModels(const QString& instanceId, std::vector<holonight_domain::ModelId> models);
  void setRuntimeAvailable(const QString& instanceId, bool available);
  [[nodiscard]] bool selectChatModel(const holonight_domain::ModelId& model);
  [[nodiscard]] bool applyEnabledState(const QString& instanceId, bool enabled);
  [[nodiscard]] bool applyRemoval(const QString& instanceId);
  void applyCommittedState(holonight_config::ProviderState state);

  [[nodiscard]] bool isDisplayNameAvailable(const QString& displayName) const;
  [[nodiscard]] QString nextAvailableDisplayName(holonight_config::ProviderType type) const;

 signals:
  void selectedSettingsInstanceIdChanged();
  void selectedChatModelChanged();

 private:
  [[nodiscard]] static holonight_config::ProviderSettings defaultSettings(holonight_config::ProviderType type);
  [[nodiscard]] bool isUsable(const holonight_domain::ModelId& model) const;
  void validateChatSelection();

  holonight_config::ProviderState saved_state_;
  ProviderInstanceListModel instances_;
  QString selected_settings_instance_id_;
  QHash<QString, std::vector<holonight_domain::ModelId>> model_caches_;
  QHash<QString, bool> runtime_availability_;
  std::optional<holonight_domain::ModelId> selected_chat_model_;
};

}  // namespace holonight_application
