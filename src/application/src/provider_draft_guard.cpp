#include "holonight_application/provider_draft_guard.h"

#include "holonight_application/provider_draft_committer.h"
#include "holonight_application/provider_draft_session.h"
#include "holonight_application/provider_instance_registry.h"

#include <utility>

namespace holonight_application {

ProviderDraftGuard::ProviderDraftGuard(ProviderInstanceRegistry* registry, ProviderDraftCommitter* committer,
                                       QObject* parent)
    : QObject(parent), registry_(registry), committer_(committer) {
  Q_ASSERT(registry_ != nullptr);
  Q_ASSERT(committer_ != nullptr);
}

void ProviderDraftGuard::setDraftSession(ProviderDraftSession* draft) { draft_ = draft; }

ProviderDraftSession* ProviderDraftGuard::draftSession() const { return draft_; }

bool ProviderDraftGuard::navigationPending() const { return static_cast<bool>(pending_navigation_); }

void ProviderDraftGuard::requestNavigation(NavigationAction action) {
  if (!action) {
    return;
  }
  if (draft_ == nullptr || (!draft_->dirty() && !draft_->isAddition())) {
    action();
    return;
  }

  const bool was_pending = navigationPending();
  pending_navigation_ = std::move(action);
  if (!was_pending) {
    emit navigationPendingChanged();
  }
  emit confirmationRequested();
}

bool ProviderDraftGuard::saveAndContinue() {
  if (!navigationPending() || draft_ == nullptr || !committer_->save(draft_)) {
    return false;
  }
  completeNavigation();
  return true;
}

bool ProviderDraftGuard::discardAndContinue() {
  if (!navigationPending() || draft_ == nullptr) {
    return false;
  }
  if (draft_->isAddition()) {
    if (!registry_->cancelDraft()) {
      return false;
    }
    draft_.clear();
  } else {
    draft_->discard();
  }
  completeNavigation();
  return true;
}

void ProviderDraftGuard::cancelNavigation() { clearPendingNavigation(); }

void ProviderDraftGuard::completeNavigation() {
  auto action = std::move(pending_navigation_);
  pending_navigation_ = {};
  emit navigationPendingChanged();
  action();
}

void ProviderDraftGuard::clearPendingNavigation() {
  if (!navigationPending()) {
    return;
  }
  pending_navigation_ = {};
  emit navigationPendingChanged();
}

}  // namespace holonight_application
