#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace holonight_credentials {

// Abstract interface (REQ-C-001). QObject-derived — results cross a real thread boundary in the
// real implementation (REQ-NF-001), so this uses Qt's signal/slot system rather than
// std::function callbacks (see docs/sdd/sqlite-conversation-persistence/DESIGN.md §4.3 for the
// fuller rationale, which applies unchanged here).
//
// Exactly one opaque secret per provider ID (REQ-F-007/REQ-C-005) — no credential "kind"/"scope"
// compound key. providerId is the stable provider-instance ID (a UUID for newly created instances;
// migrated instances may retain a legacy ID).
class CredentialStore : public QObject {
  Q_OBJECT

 public:
  explicit CredentialStore(QObject* parent = nullptr);
  ~CredentialStore() override = default;

  CredentialStore(const CredentialStore&) = delete;
  CredentialStore& operator=(const CredentialStore&) = delete;
  CredentialStore(CredentialStore&&) = delete;
  CredentialStore& operator=(CredentialStore&&) = delete;

  // Creates or overwrites the secret for providerId (REQ-F-001). Returns immediately; result
  // arrives via storeCompleted(). Never blocks the calling thread (REQ-NF-001).
  virtual void store(QString providerId, QString secret) = 0;

  // Fetches the secret for providerId, or resolves "not found" — a normal, non-error outcome
  // (REQ-F-002). Returns immediately; result arrives via retrieveCompleted().
  virtual void retrieve(QString providerId) = 0;

  // Deletes the secret for providerId; a no-op (still "succeeds") if none exists (REQ-F-003).
  // Returns immediately; result arrives via removeCompleted().
  virtual void remove(QString providerId) = 0;

  // Synchronous, read-only, never touches secret values (REQ-F-004). Both implementations answer
  // these from an in-memory representation on the calling thread — never a signal-based result,
  // never a libsecret/D-Bus round trip on this call.
  [[nodiscard]] virtual QStringList listConfiguredProviders() const = 0;
  [[nodiscard]] virtual bool hasCredential(const QString& providerId) const = 0;

  // Sticky: false once the real implementation has ever failed to reach the Secret Service
  // (REQ-F-005/006). FakeCredentialStore always returns true.
  [[nodiscard]] virtual bool isAvailable() const = 0;
  // True once the initial provider-id enumeration has completed. Until then an empty
  // hasCredential() result is provisional and callers should wait for ready().
  [[nodiscard]] virtual bool isReady() const { return true; }

 Q_SIGNALS:
  void storeCompleted(QString providerId);
  void retrieveCompleted(QString providerId, bool found, QString secret);
  void removeCompleted(QString providerId);

  // Emitted exactly once per instance, the first time the real implementation fails to reach the
  // Secret Service (REQ-F-005). FakeCredentialStore never emits this.
  void unavailable(QString reason);
  void ready();
};

}  // namespace holonight_credentials
