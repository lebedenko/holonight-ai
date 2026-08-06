#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

// Forward-declared rather than including <libsecret/secret.h> here — this header is included by
// tests/credentials/test_credential_store_worker_threading.cpp, which has no need to see the
// libsecret C API surface, only the Qt-facing slot/signal shape. glib.h itself forward-declares
// GError the same way (typedef struct _GError GError;), so this is compatible wherever both
// headers end up included in the same translation unit.
struct _GError;  // NOLINT(readability-identifier-naming): glib's own struct tag (glib.h itself
                 // typedefs `struct _GError` to `GError`); not ours to rename.

namespace holonight_credentials::detail {

// Lives on the dedicated worker thread for its entire lifetime (REQ-NF-001). Every libsecret call
// in the system executes here, using libsecret's synchronous "simple password" API — safe to
// block this thread because it has no other responsibility and no UI to keep responsive. Not part
// of the public API; constructed and owned exclusively by SecretServiceCredentialStore. Its header
// is reachable (not physically hidden) specifically so
// tests/credentials/test_credential_store_worker_threading.cpp can white-box-verify thread
// affinity, mirroring detail::ConversationRepositoryWorker's precedent.
class CredentialStoreWorker : public QObject {
  Q_OBJECT

 public:
  explicit CredentialStoreWorker(QObject* parent = nullptr);
  ~CredentialStoreWorker() override = default;

  CredentialStoreWorker(const CredentialStoreWorker&) = delete;
  CredentialStoreWorker& operator=(const CredentialStoreWorker&) = delete;
  CredentialStoreWorker(CredentialStoreWorker&&) = delete;
  CredentialStoreWorker& operator=(CredentialStoreWorker&&) = delete;

 public Q_SLOTS:
  // One-time startup enumeration; also this module's only connectivity probe. Posted once,
  // automatically, by SecretServiceCredentialStore's constructor — nothing else calls this.
  void loadKnownProviderIds();

  void store(const QString& providerId, const QString& secret);
  void retrieve(const QString& providerId);
  void remove(const QString& providerId);

 Q_SIGNALS:
  void storeCompleted(QString providerId);
  void retrieveCompleted(QString providerId, bool found, QString secret);
  void removeCompleted(QString providerId);
  void unavailable(QString reason);

  // Worker-internal only — deliberately not mirrored onto the public CredentialStore interface.
  // QStringList is an already-registered Qt meta-type, so this crosses the worker->GUI queued
  // signal boundary with no extra registration.
  void knownProviderIdsLoaded(QStringList providerIds);

 private:
  // Frees error. Sets available_ = false and emits unavailable(reason) the first time this is
  // called; a no-op on every subsequent call (REQ-F-005's "exactly once").
  void handleLibsecretError(struct _GError* error);

  bool available_ = true;
};

}  // namespace holonight_credentials::detail
