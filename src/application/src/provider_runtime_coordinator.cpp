#include "holonight_application/provider_runtime_coordinator.h"

#include <utility>

namespace holonight_application {

using holonight_credentials::CredentialStore;

ProviderRuntimeCoordinator::ProviderRuntimeCoordinator(CredentialStore* credential_store, QObject* parent)
    : QObject(parent), credential_store_(credential_store) {
  connect(credential_store_, &CredentialStore::retrieveCompleted, this,
          &ProviderRuntimeCoordinator::onCredentialRetrieved);
  connect(credential_store_, &CredentialStore::ready, this, &ProviderRuntimeCoordinator::onCredentialStoreReady);
  connect(credential_store_, &CredentialStore::unavailable, this,
          &ProviderRuntimeCoordinator::onCredentialStoreUnavailable);
  connect(credential_store_, &CredentialStore::storeCompleted, this, [this](const QString& provider_id) {
    const auto found = registrations_.constFind(provider_id);
    if (found != registrations_.cend() && found->prepare_pending &&
        found->credential_policy == CredentialPolicy::Required) {
      resolveCredential(provider_id);
    }
  });
  connect(credential_store_, &CredentialStore::removeCompleted, this, [this](const QString& provider_id) {
    auto found = registrations_.find(provider_id);
    if (found == registrations_.end() || !found->prepare_pending ||
        found->credential_policy != CredentialPolicy::Required) {
      return;
    }
    found->readiness = ProviderReadiness::MissingCredential;
    found->status_message =
        tr("%1 isn’t configured. Add an API key in Settings → Providers → %1.").arg(found->display_name);
    emit providerChanged(provider_id);
  });
}

void ProviderRuntimeCoordinator::registerProvider(QString provider_id, QString display_name,
                                                  CredentialPolicy credential_policy,
                                                  ProviderRuntimeOperations operations) {
  registrations_.insert(std::move(provider_id), Registration{.display_name = std::move(display_name),
                                                             .credential_policy = credential_policy,
                                                             .operations = std::move(operations)});
}

bool ProviderRuntimeCoordinator::unregisterProvider(const QString& provider_id) {
  return static_cast<int>(registrations_.remove(provider_id)) > 0;
}

bool ProviderRuntimeCoordinator::contains(const QString& provider_id) const {
  return registrations_.contains(provider_id);
}

void ProviderRuntimeCoordinator::prepare(const QString& provider_id) {
  auto found = registrations_.find(provider_id);
  if (found == registrations_.end()) {
    return;
  }
  if (found->prepare_pending || !found->enabled) {
    return;
  }
  found->prepare_pending = true;
  if (found->credential_policy == CredentialPolicy::Optional) {
    found->readiness = ProviderReadiness::Ready;
    found->status_message.clear();
    emit providerChanged(provider_id);
    startRefresh(provider_id, false);
    return;
  }
  resolveCredential(provider_id);
}

void ProviderRuntimeCoordinator::resolveCredential(const QString& provider_id) {
  auto& registration = registrations_[provider_id];
  if (!credential_store_->isAvailable()) {
    registration.readiness = ProviderReadiness::Unavailable;
    registration.status_message = tr("Credentials are unavailable. Check the system Secret Service.");
    emit providerChanged(provider_id);
    return;
  }
  if (!credential_store_->isReady()) {
    registration.readiness = ProviderReadiness::Unresolved;
    registration.status_message = tr("Checking provider credentials…");
    emit providerChanged(provider_id);
    return;
  }
  registration.readiness = ProviderReadiness::Unresolved;
  registration.status_message = tr("Checking provider credentials…");
  emit providerChanged(provider_id);
  credential_store_->retrieve(provider_id);
}

void ProviderRuntimeCoordinator::onCredentialRetrieved(const QString& provider_id, bool found, const QString& secret) {
  auto registration = registrations_.find(provider_id);
  if (registration == registrations_.end() || !registration->enabled ||
      registration->credential_policy == CredentialPolicy::Optional) {
    return;
  }
  if (!found) {
    registration->readiness = ProviderReadiness::MissingCredential;
    registration->status_message =
        tr("%1 isn’t configured. Add an API key in Settings → Providers → %1.").arg(registration->display_name);
    emit providerChanged(provider_id);
    return;
  }
  registration->operations.set_credential(secret);
  registration->readiness = ProviderReadiness::Ready;
  registration->status_message.clear();
  emit providerChanged(provider_id);
  startRefresh(provider_id, false);
}

void ProviderRuntimeCoordinator::forceRefresh(const QString& provider_id) { startRefresh(provider_id, true); }

void ProviderRuntimeCoordinator::setEnabled(const QString& provider_id, bool enabled) {
  auto found = registrations_.find(provider_id);
  if (found == registrations_.end() || found->enabled == enabled) {
    return;
  }
  found->enabled = enabled;
  if (!enabled) {
    ++found->refresh_generation;
    found->prepare_pending = false;
    found->refresh_requested = false;
    found->readiness = ProviderReadiness::Unresolved;
    found->refresh_state = ProviderRefreshState::NotStarted;
    found->status_message.clear();
    emit providerChanged(provider_id);
    return;
  }
  prepare(provider_id);
}

bool ProviderRuntimeCoordinator::isEnabled(const QString& provider_id) const {
  const auto found = registrations_.constFind(provider_id);
  return found != registrations_.cend() && found->enabled;
}

void ProviderRuntimeCoordinator::startRefresh(const QString& provider_id, bool force) {
  auto registration = registrations_.find(provider_id);
  if (registration == registrations_.end() || !registration->enabled ||
      registration->readiness != ProviderReadiness::Ready) {
    return;
  }
  if (registration->refresh_state == ProviderRefreshState::InProgress) {
    registration->refresh_requested = registration->refresh_requested || force;
    return;
  }
  if (!force && registration->refresh_state != ProviderRefreshState::NotStarted) {
    return;
  }
  registration->refresh_state = ProviderRefreshState::InProgress;
  const std::uint64_t generation = registration->refresh_generation;
  emit providerChanged(provider_id);
  registration->operations.refresh(
      [this, provider_id, generation] {
        auto found = registrations_.find(provider_id);
        if (found == registrations_.end() || !found->enabled || found->refresh_generation != generation) {
          return;
        }
        auto& current = found.value();
        current.operations.persist_models();
        current.refresh_state = ProviderRefreshState::Succeeded;
        current.status_message.clear();
        const bool repeat = std::exchange(current.refresh_requested, false);
        emit providerChanged(provider_id);
        if (repeat) {
          startRefresh(provider_id, true);
        }
      },
      [this, provider_id, generation](const QString& reason) {
        auto found = registrations_.find(provider_id);
        if (found == registrations_.end() || !found->enabled || found->refresh_generation != generation) {
          return;
        }
        auto& current = found.value();
        current.refresh_state = ProviderRefreshState::Failed;
        current.status_message = tr("Couldn’t refresh %1 models: %2").arg(current.display_name, reason);
        const bool repeat = std::exchange(current.refresh_requested, false);
        emit providerChanged(provider_id);
        if (repeat) {
          startRefresh(provider_id, true);
        }
      });
}

void ProviderRuntimeCoordinator::onCredentialStoreReady() {
  for (auto it = registrations_.cbegin(); it != registrations_.cend(); ++it) {
    if (it->enabled && it->prepare_pending && it->readiness == ProviderReadiness::Unresolved) {
      resolveCredential(it.key());
    }
  }
}

void ProviderRuntimeCoordinator::onCredentialStoreUnavailable(const QString& reason) {
  Q_UNUSED(reason)
  for (auto it = registrations_.begin(); it != registrations_.end(); ++it) {
    if (it->enabled && it->credential_policy == CredentialPolicy::Required && it->prepare_pending) {
      it->readiness = ProviderReadiness::Unavailable;
      it->status_message = tr("Credentials are unavailable. Check the system Secret Service.");
      emit providerChanged(it.key());
    }
  }
}

ProviderReadiness ProviderRuntimeCoordinator::readiness(const QString& provider_id) const {
  const auto found = registrations_.constFind(provider_id);
  return found == registrations_.cend() ? ProviderReadiness::Unresolved : found->readiness;
}

ProviderRefreshState ProviderRuntimeCoordinator::refreshState(const QString& provider_id) const {
  const auto found = registrations_.constFind(provider_id);
  return found == registrations_.cend() ? ProviderRefreshState::NotStarted : found->refresh_state;
}

QString ProviderRuntimeCoordinator::statusMessage(const QString& provider_id) const {
  const auto found = registrations_.constFind(provider_id);
  return found == registrations_.cend() ? QString() : found->status_message;
}

bool ProviderRuntimeCoordinator::isReady(const QString& provider_id) const {
  return readiness(provider_id) == ProviderReadiness::Ready;
}

}  // namespace holonight_application
