#include "holonight_application/provider_instance_deleter.h"

#include "holonight_application/provider_adapter_router.h"
#include "holonight_application/provider_instance_registry.h"
#include "holonight_application/provider_runtime_coordinator.h"

#include <algorithm>
#include <holonight_credentials/credential_store.h>
#include <utility>

namespace holonight_application {

using holonight_config::ProviderInstanceConfig;
using holonight_config::ProviderTombstone;
using holonight_config::UtilityConfig;
using holonight_credentials::CredentialStore;

ProviderInstanceDeleter::ProviderInstanceDeleter(ProviderInstanceRegistry* registry, ProviderAdapterRouter* router,
                                                 ProviderRuntimeCoordinator* runtime_coordinator,
                                                 CredentialStore* credential_store,
                                                 holonight_config::ConfigRepository config_repository, QObject* parent)
    : QObject(parent),
      registry_(registry),
      router_(router),
      runtime_coordinator_(runtime_coordinator),
      credential_store_(credential_store),
      config_repository_(std::move(config_repository)) {
  Q_ASSERT(registry_ != nullptr);
  Q_ASSERT(router_ != nullptr);
  Q_ASSERT(runtime_coordinator_ != nullptr);
  Q_ASSERT(credential_store_ != nullptr);
  connect(credential_store_, &CredentialStore::removeCompleted, this,
          &ProviderInstanceDeleter::finishCredentialCleanup);
  connect(credential_store_, &CredentialStore::unavailable, this, &ProviderInstanceDeleter::failCredentialCleanup);
}

bool ProviderInstanceDeleter::remove(const QString& instance_id, bool confirmed) {
  if (!confirmed) {
    setError(tr("Deletion requires confirmation."));
    return false;
  }
  if (credential_operation_in_progress_ || credential_retry_pending_) {
    setError(tr("Finish the pending credential cleanup before deleting another provider."));
    return false;
  }
  if (!router_->canDelete(instance_id)) {
    setError(router_->contains(instance_id) ? tr("This provider cannot be deleted while it is streaming a response.")
                                            : tr("The provider runtime is unavailable."));
    return false;
  }

  auto candidate = registry_->savedState();
  const auto instance = std::ranges::find(candidate.instances, instance_id, &ProviderInstanceConfig::id);
  if (instance == candidate.instances.end()) {
    setError(tr("The provider no longer exists."));
    return false;
  }
  const ProviderTombstone tombstone{
      .instance_id = instance->id, .type = instance->type, .last_display_name = instance->display_name};
  candidate.instances.erase(instance);
  if (std::ranges::find(candidate.tombstones, instance_id, &ProviderTombstone::instance_id) ==
      candidate.tombstones.end()) {
    candidate.tombstones.push_back(tombstone);
  }

  UtilityConfig utility = config_repository_.loadUtilityConfig();
  bool clearUtility = false;
  if (utility.default_utility_model.has_value() && utility.default_utility_model->provider_id == instance_id) {
    utility.default_utility_model.reset();
    clearUtility = true;
  }
  if (utility.chat_title_model_override.has_value() && utility.chat_title_model_override->provider_id == instance_id) {
    utility.chat_title_model_override.reset();
    clearUtility = true;
  }
  const auto saved = clearUtility ? config_repository_.saveProviderStateAndUtilityConfig(candidate, utility)
                                  : config_repository_.saveProviderState(candidate);
  if (!saved.has_value()) {
    setError(tr("Failed to delete provider: %1").arg(saved.error()));
    return false;
  }

  registry_->applyCommittedState(std::move(candidate));
  const bool removedAdapter = router_->remove(instance_id);
  const bool removedRuntime = runtime_coordinator_->unregisterProvider(instance_id);
  Q_ASSERT(removedAdapter);
  Q_UNUSED(removedAdapter)
  Q_UNUSED(removedRuntime)
  pending_credential_id_ = instance_id;
  setError({});
  emit deletionCompleted(instance_id);
  startCredentialCleanup();
  return true;
}

bool ProviderInstanceDeleter::retryCredentialCleanup() {
  if (!credential_retry_pending_ || credential_operation_in_progress_) {
    return false;
  }
  setError({});
  startCredentialCleanup();
  return true;
}

bool ProviderInstanceDeleter::credentialRetryPending() const { return credential_retry_pending_; }

QString ProviderInstanceDeleter::error() const { return error_; }

void ProviderInstanceDeleter::startCredentialCleanup() {
  credential_operation_in_progress_ = true;
  if (credential_retry_pending_) {
    credential_retry_pending_ = false;
    emit credentialRetryPendingChanged();
  }
  credential_store_->remove(pending_credential_id_);
}

void ProviderInstanceDeleter::finishCredentialCleanup(const QString& provider_id) {
  if (!credential_operation_in_progress_ || provider_id != pending_credential_id_) {
    return;
  }
  credential_operation_in_progress_ = false;
  pending_credential_id_.clear();
  setError({});
}

void ProviderInstanceDeleter::failCredentialCleanup(const QString& reason) {
  if (!credential_operation_in_progress_) {
    return;
  }
  credential_operation_in_progress_ = false;
  credential_retry_pending_ = true;
  emit credentialRetryPendingChanged();
  setError(tr("Provider was deleted, but credential cleanup failed: %1").arg(reason));
}

void ProviderInstanceDeleter::setError(QString error) {
  if (error_ == error) {
    return;
  }
  error_ = std::move(error);
  emit errorChanged();
}

}  // namespace holonight_application
