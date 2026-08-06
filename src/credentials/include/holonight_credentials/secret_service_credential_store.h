#pragma once

#include "holonight_credentials/credential_store.h"
#include "holonight_credentials/detail/credential_store_worker.h"

#include <QSet>
#include <QThread>

namespace holonight_credentials {

// GUI-thread-resident façade (REQ-NF-001/REQ-C-002). Owns a QThread and a private
// detail::CredentialStoreWorker moved onto it — the exact SqliteConversationRepository shape.
// Every public store/retrieve/remove method does nothing but package the request and hand it to
// the worker via a functor-based, Qt::QueuedConnection QMetaObject::invokeMethod() call.
// listConfiguredProviders()/hasCredential() are the one deliberate exception: answered
// synchronously from known_provider_ids_, never posted to the worker (see
// docs/sdd/secret-service-credentials/DESIGN.md §5.3).
class SecretServiceCredentialStore : public CredentialStore {
  Q_OBJECT

 public:
  explicit SecretServiceCredentialStore(QObject* parent = nullptr);
  ~SecretServiceCredentialStore() override;

  SecretServiceCredentialStore(const SecretServiceCredentialStore&) = delete;
  SecretServiceCredentialStore& operator=(const SecretServiceCredentialStore&) = delete;
  SecretServiceCredentialStore(SecretServiceCredentialStore&&) = delete;
  SecretServiceCredentialStore& operator=(SecretServiceCredentialStore&&) = delete;

  void store(QString providerId, QString secret) override;
  void retrieve(QString providerId) override;
  void remove(QString providerId) override;

  [[nodiscard]] QStringList listConfiguredProviders() const override;
  [[nodiscard]] bool hasCredential(const QString& providerId) const override;
  [[nodiscard]] bool isAvailable() const override;
  [[nodiscard]] bool isReady() const override;

 private:
  QThread worker_thread_;
  detail::CredentialStoreWorker* worker_;  // owned by worker_thread_ (moveToThread)
  QSet<QString> known_provider_ids_;       // GUI-thread-only cache backing the read methods above
  bool available_ = true;
  bool ready_ = false;
};

}  // namespace holonight_credentials
