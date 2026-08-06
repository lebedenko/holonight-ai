#pragma once

#include "holonight_credentials/credential_store.h"

#include <QHash>
#include <QString>

namespace holonight_credentials {

// Test double for CredentialStore (REQ-C-001/NF-002). Every method fires its result signal
// synchronously and inline — no QThread, no event loop needed — mirroring
// tests/persistence/fake_conversation_repository.h. Always available (REQ-F-005): isAvailable()
// unconditionally returns true, unavailable() is never emitted.
class FakeCredentialStore : public CredentialStore {
 public:
  explicit FakeCredentialStore(QObject* parent = nullptr) : CredentialStore(parent) {}

  void store(QString providerId, QString secret) override {
    secrets_.insert(providerId, secret);
    emit storeCompleted(providerId);
  }

  void retrieve(QString providerId) override {
    const auto found = secrets_.constFind(providerId);
    if (found == secrets_.constEnd()) {
      emit retrieveCompleted(providerId, false, QString());
      return;
    }
    emit retrieveCompleted(providerId, true, found.value());
  }

  void remove(QString providerId) override {
    secrets_.remove(providerId);
    emit removeCompleted(providerId);
  }

  [[nodiscard]] QStringList listConfiguredProviders() const override {
    return {secrets_.keyBegin(), secrets_.keyEnd()};
  }

  [[nodiscard]] bool hasCredential(const QString& providerId) const override { return secrets_.contains(providerId); }

  [[nodiscard]] bool isAvailable() const override { return true; }

 private:
  QHash<QString, QString> secrets_;
};

}  // namespace holonight_credentials
