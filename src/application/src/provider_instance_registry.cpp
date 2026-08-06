#include "holonight_application/provider_instance_registry.h"

#include <QSet>
#include <QUuid>

#include <algorithm>
#include <iterator>
#include <utility>

namespace holonight_application {

using holonight_config::AnthropicProviderConfig;
using holonight_config::GoogleProviderConfig;
using holonight_config::OllamaProviderConfig;
using holonight_config::OpenAIProviderConfig;
using holonight_config::ProviderInstanceConfig;
using holonight_config::ProviderSettings;
using holonight_config::ProviderState;
using holonight_config::ProviderType;

HistoricalProviderIdentity resolveHistoricalProviderIdentity(const ProviderState& state, const QString& instance_id) {
  const auto live = std::ranges::find(state.instances, instance_id, &ProviderInstanceConfig::id);
  if (live != state.instances.end()) {
    return {.instance_id = instance_id, .type = live->type, .display_name = live->display_name, .tombstone = false};
  }

  const auto deleted =
      std::ranges::find(state.tombstones, instance_id, &holonight_config::ProviderTombstone::instance_id);
  if (deleted != state.tombstones.end()) {
    return {.instance_id = instance_id,
            .type = deleted->type,
            .display_name = deleted->last_display_name,
            .tombstone = true};
  }

  return {.instance_id = instance_id, .type = std::nullopt, .display_name = QStringLiteral("Unknown provider")};
}

ProviderInstanceRegistry::ProviderInstanceRegistry(ProviderState state, QObject* parent)
    : QObject(parent), saved_state_(std::move(state)), instances_(this) {
  instances_.setSavedInstances(saved_state_.instances);
}

ProviderInstanceListModel* ProviderInstanceRegistry::instances() { return &instances_; }

const ProviderInstanceListModel* ProviderInstanceRegistry::instances() const { return &instances_; }

const ProviderState& ProviderInstanceRegistry::savedState() const { return saved_state_; }

QString ProviderInstanceRegistry::selectedSettingsInstanceId() const { return selected_settings_instance_id_; }

std::optional<holonight_domain::ModelId> ProviderInstanceRegistry::selectedChatModel() const {
  return selected_chat_model_;
}

HistoricalProviderIdentity ProviderInstanceRegistry::historicalIdentity(const QString& instance_id) const {
  return resolveHistoricalProviderIdentity(saved_state_, instance_id);
}

std::optional<ProviderInstanceConfig> ProviderInstanceRegistry::addDraft(ProviderType type) {
  if (instances_.hasDraft()) {
    return std::nullopt;
  }

  ProviderInstanceConfig instance{
      .id = QUuid::createUuid().toString(QUuid::WithoutBraces),
      .type = type,
      .display_name = nextAvailableDisplayName(type),
      .enabled = true,
      .settings = defaultSettings(type),
  };
  instances_.prependDraft(instance);
  selected_settings_instance_id_ = instance.id;
  emit selectedSettingsInstanceIdChanged();
  return instance;
}

bool ProviderInstanceRegistry::cancelDraft() {
  const auto draftId = instances_.draftId();
  if (!draftId.has_value() || !instances_.removeDraft(*draftId)) {
    return false;
  }
  if (selected_settings_instance_id_ == *draftId) {
    selected_settings_instance_id_.clear();
    emit selectedSettingsInstanceIdChanged();
  }
  return true;
}

bool ProviderInstanceRegistry::selectSettingsInstance(const QString& instanceId) {
  if (instances_.find(instanceId) == nullptr) {
    return false;
  }
  if (selected_settings_instance_id_ == instanceId) {
    return true;
  }
  selected_settings_instance_id_ = instanceId;
  emit selectedSettingsInstanceIdChanged();
  return true;
}

void ProviderInstanceRegistry::clearSettingsSelection() {
  if (selected_settings_instance_id_.isEmpty()) {
    return;
  }
  selected_settings_instance_id_.clear();
  emit selectedSettingsInstanceIdChanged();
}

void ProviderInstanceRegistry::setRuntimeModels(const QString& instanceId,
                                                std::vector<holonight_domain::ModelId> models) {
  if (instances_.find(instanceId) == nullptr) {
    return;
  }
  std::erase_if(models, [&instanceId](const auto& model) {
    return model.provider_id != instanceId || model.model_name.isEmpty();
  });
  model_caches_.insert(instanceId, std::move(models));
  validateChatSelection();
}

void ProviderInstanceRegistry::setRuntimeAvailable(const QString& instanceId, bool available) {
  if (instances_.find(instanceId) == nullptr) {
    return;
  }
  runtime_availability_.insert(instanceId, available);
  validateChatSelection();
}

bool ProviderInstanceRegistry::selectChatModel(const holonight_domain::ModelId& model) {
  if (!isUsable(model)) {
    return false;
  }
  if (selected_chat_model_ == model) {
    return true;
  }
  selected_chat_model_ = model;
  emit selectedChatModelChanged();
  return true;
}

bool ProviderInstanceRegistry::applyEnabledState(const QString& instanceId, bool enabled) {
  auto saved = std::ranges::find(saved_state_.instances, instanceId, &ProviderInstanceConfig::id);
  if (saved == saved_state_.instances.end()) {
    return false;
  }
  saved->enabled = enabled;
  instances_.setEnabled(instanceId, enabled);
  validateChatSelection();
  return true;
}

bool ProviderInstanceRegistry::applyRemoval(const QString& instanceId) {
  const auto saved = std::ranges::find(saved_state_.instances, instanceId, &ProviderInstanceConfig::id);
  if (saved == saved_state_.instances.end()) {
    return false;
  }
  saved_state_.instances.erase(saved);
  instances_.removeSaved(instanceId);
  model_caches_.remove(instanceId);
  runtime_availability_.remove(instanceId);
  if (selected_settings_instance_id_ == instanceId) {
    selected_settings_instance_id_.clear();
    emit selectedSettingsInstanceIdChanged();
  }
  validateChatSelection();
  return true;
}

void ProviderInstanceRegistry::applyCommittedState(ProviderState state) {
  saved_state_ = std::move(state);
  instances_.setSavedInstances(saved_state_.instances);
  QSet<QString> savedIds;
  for (const auto& instance : saved_state_.instances) {
    savedIds.insert(instance.id);
  }
  for (auto it = model_caches_.begin(); it != model_caches_.end();) {
    it = savedIds.contains(it.key()) ? std::next(it) : model_caches_.erase(it);
  }
  for (auto it = runtime_availability_.begin(); it != runtime_availability_.end();) {
    it = savedIds.contains(it.key()) ? std::next(it) : runtime_availability_.erase(it);
  }
  if (!selected_settings_instance_id_.isEmpty() && !savedIds.contains(selected_settings_instance_id_)) {
    selected_settings_instance_id_.clear();
    emit selectedSettingsInstanceIdChanged();
  }
  validateChatSelection();
}

bool ProviderInstanceRegistry::isDisplayNameAvailable(const QString& displayName) const {
  return !displayName.trimmed().isEmpty() && !instances_.containsName(displayName);
}

QString ProviderInstanceRegistry::nextAvailableDisplayName(ProviderType type) const {
  QString baseName = holonight_config::defaultProviderDisplayName(type);
  if (isDisplayNameAvailable(baseName)) {
    return baseName;
  }
  for (int suffix = 2;; ++suffix) {
    const QString candidate = QStringLiteral("%1 %2").arg(baseName).arg(suffix);
    if (isDisplayNameAvailable(candidate)) {
      return candidate;
    }
  }
}

ProviderSettings ProviderInstanceRegistry::defaultSettings(ProviderType type) {
  switch (type) {
    case ProviderType::Ollama:
      return OllamaProviderConfig{};
    case ProviderType::OpenAi:
      return OpenAIProviderConfig{};
    case ProviderType::Anthropic:
      return AnthropicProviderConfig{};
    case ProviderType::Google:
      return GoogleProviderConfig{};
  }
  return OllamaProviderConfig{};
}

bool ProviderInstanceRegistry::isUsable(const holonight_domain::ModelId& model) const {
  const auto* instance = instances_.find(model.provider_id);
  if (instance == nullptr || !instance->enabled || !runtime_availability_.value(model.provider_id, false)) {
    return false;
  }
  const auto models = model_caches_.constFind(model.provider_id);
  return models != model_caches_.cend() && std::ranges::find(*models, model) != models->end();
}

void ProviderInstanceRegistry::validateChatSelection() {
  if (selected_chat_model_.has_value() && isUsable(*selected_chat_model_)) {
    return;
  }

  std::optional<holonight_domain::ModelId> fallback;
  for (const auto& instance : saved_state_.instances) {
    if (!instance.enabled || !runtime_availability_.value(instance.id, false)) {
      continue;
    }
    const auto models = model_caches_.constFind(instance.id);
    if (models != model_caches_.cend() && !models->empty()) {
      fallback = models->front();
      break;
    }
  }
  if (selected_chat_model_ == fallback) {
    return;
  }
  selected_chat_model_ = std::move(fallback);
  emit selectedChatModelChanged();
}

}  // namespace holonight_application
