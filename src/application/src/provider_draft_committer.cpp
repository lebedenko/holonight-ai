#include "holonight_application/provider_draft_committer.h"

#include "holonight_application/provider_draft_session.h"
#include "holonight_application/provider_instance_registry.h"

#include <algorithm>
#include <holonight_credentials/credential_store.h>
#include <utility>

namespace holonight_application {

using holonight_config::ProviderInstanceConfig;
using holonight_config::ProviderState;
using holonight_credentials::CredentialStore;

ProviderDraftCommitter::ProviderDraftCommitter(ProviderInstanceRegistry* registry, CredentialStore* credential_store,
                                               holonight_config::ConfigRepository config_repository, QObject* parent)
    : QObject(parent),
      registry_(registry),
      credential_store_(credential_store),
      config_repository_(std::move(config_repository)) {
  Q_ASSERT(registry_ != nullptr);
  Q_ASSERT(credential_store_ != nullptr);
  connect(credential_store_, &CredentialStore::storeCompleted, this,
          &ProviderDraftCommitter::finishCredentialOperation);
  connect(credential_store_, &CredentialStore::removeCompleted, this,
          &ProviderDraftCommitter::finishCredentialOperation);
  connect(credential_store_, &CredentialStore::unavailable, this, &ProviderDraftCommitter::failCredentialOperation);
}

bool ProviderDraftCommitter::save(ProviderDraftSession* draft) {
  if (draft == nullptr || credential_operation_in_progress_ || !draft->validate()) {
    if (credential_operation_in_progress_) {
      setError(tr("A credential operation is already in progress."));
    }
    return false;
  }

  ProviderState candidate = registry_->savedState();
  const ProviderInstanceConfig committed = draft->editable();
  const auto existing = std::ranges::find(candidate.instances, committed.id, &ProviderInstanceConfig::id);
  if (existing == candidate.instances.end()) {
    candidate.instances.insert(candidate.instances.begin(), committed);
  } else {
    *existing = committed;
  }

  const auto saved = config_repository_.saveProviderState(candidate);
  if (!saved.has_value()) {
    setError(tr("Failed to save settings: %1").arg(saved.error()));
    return false;
  }

  registry_->applyCommittedState(std::move(candidate));
  draft->commitConfiguration();
  pending_draft_ = draft;
  pending_provider_id_ = committed.id;
  setError({});

  if (draft->credentialEdit() == ProviderDraftSession::CredentialEdit::Keep) {
    pending_draft_ = nullptr;
    pending_provider_id_.clear();
    emit saveCompleted();
    return true;
  }
  startCredentialOperation();
  return true;
}

bool ProviderDraftCommitter::retryCredential() {
  if (!credential_retry_pending_ || pending_draft_ == nullptr || credential_operation_in_progress_) {
    return false;
  }
  setError({});
  startCredentialOperation();
  return true;
}

bool ProviderDraftCommitter::credentialRetryPending() const { return credential_retry_pending_; }

QString ProviderDraftCommitter::error() const { return error_; }

void ProviderDraftCommitter::startCredentialOperation() {
  credential_operation_in_progress_ = true;
  if (credential_retry_pending_) {
    credential_retry_pending_ = false;
    emit credentialRetryPendingChanged();
  }
  if (pending_draft_->credentialEdit() == ProviderDraftSession::CredentialEdit::Store) {
    credential_store_->store(pending_provider_id_, pending_draft_->credentialValue());
  } else {
    credential_store_->remove(pending_provider_id_);
  }
}

void ProviderDraftCommitter::finishCredentialOperation(const QString& provider_id) {
  if (!credential_operation_in_progress_ || provider_id != pending_provider_id_) {
    return;
  }
  credential_operation_in_progress_ = false;
  pending_draft_->commitCredential();
  pending_draft_ = nullptr;
  pending_provider_id_.clear();
  setError({});
  emit saveCompleted();
}

void ProviderDraftCommitter::failCredentialOperation(const QString& reason) {
  if (!credential_operation_in_progress_) {
    return;
  }
  credential_operation_in_progress_ = false;
  credential_retry_pending_ = true;
  emit credentialRetryPendingChanged();
  setError(tr("Settings were saved, but the credential operation failed: %1").arg(reason));
}

void ProviderDraftCommitter::setError(QString error) {
  if (error_ == error) {
    return;
  }
  error_ = std::move(error);
  emit errorChanged();
}

}  // namespace holonight_application
