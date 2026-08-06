#include "holonight_credentials/secret_service_credential_store.h"

#include <utility>

namespace holonight_credentials {

// The façade never calls libsecret directly — every libsecret call happens on the worker thread,
// inside detail::CredentialStoreWorker (src/detail/credential_store_worker.cpp), which is where
// the schema/label helpers and error classification actually live.

SecretServiceCredentialStore::SecretServiceCredentialStore(QObject* parent)
    : CredentialStore(parent), worker_(new detail::CredentialStoreWorker) {
  worker_->moveToThread(&worker_thread_);

  // Documented Qt worker-object cleanup idiom: deleteLater() runs on the worker thread's own
  // finishing sequence, which is why the destructor must call quit()+wait() rather than
  // `delete worker_` — deleting directly from the GUI thread would destroy a QObject whose
  // affinity is the worker thread.
  connect(&worker_thread_, &QThread::finished, worker_, &QObject::deleteLater);

  connect(worker_, &detail::CredentialStoreWorker::storeCompleted, this, [this](const QString& providerId) {
    if (available_) {
      known_provider_ids_.insert(providerId);
    }
    emit storeCompleted(providerId);
  });
  connect(worker_, &detail::CredentialStoreWorker::retrieveCompleted, this, &CredentialStore::retrieveCompleted);
  connect(worker_, &detail::CredentialStoreWorker::removeCompleted, this, [this](const QString& providerId) {
    known_provider_ids_.remove(providerId);
    emit removeCompleted(providerId);
  });
  connect(worker_, &detail::CredentialStoreWorker::unavailable, this, [this](const QString& reason) {
    available_ = false;
    if (!ready_) {
      ready_ = true;
      emit ready();
    }
    emit unavailable(reason);
  });
  connect(worker_, &detail::CredentialStoreWorker::knownProviderIdsLoaded, this,
          [this](const QStringList& providerIds) {
            known_provider_ids_ = QSet<QString>(providerIds.begin(), providerIds.end());
            if (!ready_) {
              ready_ = true;
              emit ready();
            }
          });

  worker_thread_.start();
  QMetaObject::invokeMethod(worker_, [worker = worker_] { worker->loadKnownProviderIds(); }, Qt::QueuedConnection);
}

SecretServiceCredentialStore::~SecretServiceCredentialStore() {
  worker_thread_.quit();
  worker_thread_.wait();
}

void SecretServiceCredentialStore::store(QString providerId, QString secret) {
  QMetaObject::invokeMethod(
      worker_,
      [worker = worker_, providerId = std::move(providerId), secret = std::move(secret)] {
        worker->store(providerId, secret);
      },
      Qt::QueuedConnection);
}

void SecretServiceCredentialStore::retrieve(QString providerId) {
  QMetaObject::invokeMethod(
      worker_, [worker = worker_, providerId = std::move(providerId)] { worker->retrieve(providerId); },
      Qt::QueuedConnection);
}

void SecretServiceCredentialStore::remove(QString providerId) {
  QMetaObject::invokeMethod(
      worker_, [worker = worker_, providerId = std::move(providerId)] { worker->remove(providerId); },
      Qt::QueuedConnection);
}

QStringList SecretServiceCredentialStore::listConfiguredProviders() const {
  return {known_provider_ids_.begin(), known_provider_ids_.end()};
}

bool SecretServiceCredentialStore::hasCredential(const QString& providerId) const {
  return known_provider_ids_.contains(providerId);
}

bool SecretServiceCredentialStore::isAvailable() const { return available_; }

bool SecretServiceCredentialStore::isReady() const { return ready_; }

}  // namespace holonight_credentials
