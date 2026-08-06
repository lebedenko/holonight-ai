# Secret Service Credential Store Design

**Document Version**: 1.0
**Date**: 2026-07-22
**Modules**: `holonight_credentials` (new: `CredentialStore` interface, `SecretServiceCredentialStore`,
`detail::CredentialStoreWorker`), `tests/credentials/` (new: `FakeCredentialStore` test double + GTest suite)
**Traces to**: `docs/sdd/secret-service-credentials/SPEC.md` (7 functional, 4 non-functional, 5 constraint
requirements)
**Precedent**: `docs/sdd/sqlite-conversation-persistence/DESIGN.md` and `src/persistence/` — worker-thread
façade pattern, signal-driven completion, sticky `unavailable` state. This module follows that shape
one level simpler: no migrations, no explicit `initialize()` gate, no per-operation non-fatal `error()`
signal (see §5 Key Decisions for why the error model differs).
**Status**: Design revised after review; implementation follow-up tasks are pending

---

## 1. Components

| Class | File | Role |
|---|---|---|
| `CredentialStore` | `credential_store.h` | Abstract, `QObject`-derived interface (REQ-C-001). Pure-virtual, non-blocking request methods; readiness/availability queries; success and failure signals. |
| `SecretServiceCredentialStore` | `secret_service_credential_store.h` | GUI-thread-resident façade. Owns a `QThread` + a private worker moved onto it (REQ-NF-001/REQ-C-002). Also owns the synchronous in-memory `known_provider_ids_` cache that answers `listConfiguredProviders()`/`hasCredential()` (REQ-F-004, §5.3). |
| `detail::CredentialStoreWorker` | `detail/credential_store_worker.h` | Lives on the worker thread. Every libsecret call in the system executes here, using libsecret's synchronous "simple password" API (§5.1). |
| `FakeCredentialStore` (test-only) | `tests/credentials/fake_credential_store.h` | REQ-C-001/NF-002's injectable, in-memory, fully synchronous test double — same precedent as `tests/persistence/fake_conversation_repository.h`. |

No DTO/record type is needed (unlike `holonight_persistence::ConversationSummary`/`LoadedConversation`):
every signal parameter is a plain `QString`/`bool`, both already-registered Qt meta-types (§6), so there
is no `conversation_record.h`-equivalent file and no `registerMetaTypes()` call anywhere in this module.

### 1.1 Class relationship

```
                    ┌───────────────────────┐
                    │   CredentialStore     │  (abstract, QObject)
                    │  + store()            │
                    │  + retrieve()         │
                    │  + remove()           │
                    │  + listConfiguredProviders() const │
                    │  + hasCredential() const           │
                    │  + isReady() const                 │
                    │  + isAvailable() const             │
                    │  Q_SIGNALS: storeCompleted,        │
                    │   retrieveCompleted, removeCompleted,│
                    │   operationFailed, ready, unavailable│
                    └───────────┬───────────┘
                                │
              ┌─────────────────┴─────────────────┐
              │                                     │
┌─────────────▼─────────────┐          ┌────────────▼─────────────┐
│ SecretServiceCredentialStore│          │   FakeCredentialStore    │
│  (GUI thread)               │          │  (GUI thread, no thread) │
│  - QThread worker_thread_    │          │  - QHash<QString,QString>│
│  - detail::CredentialStoreWorker* worker_│  secrets_ (in-memory)  │
│  - QSet<QString> known_provider_ids_     │  - bool available_=true│
│  - bool available_           │          └───────────────────────┘
└─────────────┬───────────────┘
              │ moveToThread
┌─────────────▼───────────────┐
│ detail::CredentialStoreWorker│
│  (worker thread)             │
│  - secret_password_store_sync│
│  - secret_password_lookup_sync│
│  - secret_password_clear_sync│
│  - secret_service_search_sync│ (startup enumeration only, §2.4)
│  - bool available_           │
└───────────────────────────────┘
```

---

## 2. Data Flow

### 2.1 Construction (no separate `initialize()` — a deliberate divergence from precedent, §5.2)

1. `SecretServiceCredentialStore`'s constructor creates `worker_ = new detail::CredentialStoreWorker(...)`,
   `worker_->moveToThread(&worker_thread_)`, wires the forwarding connections (§3.3), starts
   `worker_thread_`, then immediately posts one queued call:
   `QMetaObject::invokeMethod(worker_, [w = worker_] { w->loadKnownProviderIds(); }, Qt::QueuedConnection);`
2. On the worker thread, `loadKnownProviderIds()` calls `secret_service_search_sync()` against the
   credential schema (§4) with an **empty** attribute table (matches every item under this schema name,
   regardless of `provider_id` — the D-Bus `SearchItems` method accepts an empty `a{ss}` for exactly this;
   only the `SECRET_SCHEMA_DONT_MATCH_NAME`-flagged case rejects an empty table, and this schema does not
   set that flag). No `SECRET_SEARCH_LOAD_SECRETS` flag is passed — the search only needs each result's
   `provider_id` attribute (via `secret_item_get_attributes()`), never the secret value itself (REQ-NF-003:
   the enumeration path never touches, logs, or transports a secret string).
   - **Success** → emits `knownProviderIdsLoaded(QStringList providerIds)` — a signal declared only on
     `detail::CredentialStoreWorker`, **not** mirrored onto the public `CredentialStore` interface (§5.3
     explains this deliberate one-signal exception to the "worker signals mirror the interface 1:1"
     precedent).
   - **Connectivity failure** → marks the instance unavailable and emits `unavailable(reason)`.
   - **Recoverable failure** (for example a locked collection) → emits `operationFailed` for initialization,
     leaves the instance available, and leaves `isReady()` false so initialization can be retried.
3. `SecretServiceCredentialStore`'s private slot on `knownProviderIdsLoaded` replaces
   `known_provider_ids_` wholesale (`QSet<QString>(providerIds.begin(), providerIds.end())`) — a one-time
   warm-up. Its private slot on `unavailable` sets `available_ = false` **before** re-emitting the public
   `unavailable(reason)` signal. On successful enumeration it sets `ready_ = true` and emits public
   `ready()` after replacing the cache, so query consumers can safely read an authoritative snapshot.
4. Construction itself never blocks: both the worker-thread start and the first `invokeMethod` return
   immediately; the enumeration and its result arrive later, asynchronously (REQ-NF-001).

### 2.2 `store()` end-to-end

1. GUI thread: `SecretServiceCredentialStore::store(providerId, secret)` packages both strings into a
   functor and calls `QMetaObject::invokeMethod(worker_, [...], Qt::QueuedConnection)`; returns
   immediately (<1ms, REQ-NF-001).
2. Worker thread, `CredentialStoreWorker::store(providerId, secret)`:
   - If `!available_`: emit `operationFailed(providerId, Store, reason)` and return.
   - Else: calls `secret_password_store_sync(credentialSchema(), SECRET_COLLECTION_DEFAULT, label,
     secret.toUtf8().constData(), /*cancellable*/ nullptr, &error, "provider_id",
     providerId.toUtf8().constData(), nullptr)` (§4.1) — this **blocks the worker thread**, which is
     exactly the point: the worker thread has no other job and no UI to keep responsive (§5.1).
   - If `error != nullptr`: classify it (§4.3), emit `operationFailed`, and return without a success signal.
     Only connection-loss errors also transition to sticky unavailable state.
   - On success only: `emit storeCompleted(providerId)`.
3. GUI thread, façade's forwarding slot inserts the provider into the cache and emits
   `storeCompleted(providerId)`.
4. Caller's slot (if connected) receives `storeCompleted(providerId)` on the GUI thread.

### 2.3 `retrieve()` — not-found vs. unavailable

1. GUI thread → worker thread via the same queued-invoke pattern.
2. Worker thread, `CredentialStoreWorker::retrieve(providerId)`:
   - If `!available_`: emit `operationFailed(providerId, Retrieve, reason)`; no libsecret call is attempted.
   - Else: `gchar* password = secret_password_lookup_sync(credentialSchema(), nullptr, &error,
     "provider_id", providerId.toUtf8().constData(), nullptr);`
     - `error != nullptr` → classify the error and emit `operationFailed`; do not emit `retrieveCompleted`.
     - `error == nullptr && password == nullptr` → genuine, ordinary miss: `emit
       retrieveCompleted(providerId, false, QString());` (found=false), **without** touching
       `available_` at all — this is the distinguishing branch libsecret's own contract guarantees (a
       clean miss is `NULL` secret with `NULL` error, never the reverse).
     - `error == nullptr && password != nullptr` → `const QString secret =
       QString::fromUtf8(password); secret_password_free(password); emit
       retrieveCompleted(providerId, true, secret);`
3. Façade forwards `retrieveCompleted` verbatim (no cache interaction — §5.3 explains why `retrieve()`
   never mutates `known_provider_ids_`).

### 2.4 `remove()`

Mirrors §2.2 exactly, using `secret_password_clear_sync()` (§4.1). A clean “nothing matched” result is
still successful and emits `removeCompleted(providerId)`; a `GError` emits `operationFailed` instead.
The façade removes the provider from its cache only after `removeCompleted`.

### 2.5 `listConfiguredProviders()` / `hasCredential()` — always synchronous, always GUI-thread-local

Neither method ever posts to the worker thread. Both read `known_provider_ids_` directly:

```cpp
QStringList SecretServiceCredentialStore::listConfiguredProviders() const {
  return QStringList(known_provider_ids_.begin(), known_provider_ids_.end());
}

bool SecretServiceCredentialStore::hasCredential(const QString& providerId) const {
  return known_provider_ids_.contains(providerId);
}
```

Until `isReady()` becomes true and `ready()` has been emitted, these cache reads are provisional and callers
must not interpret empty/false as authoritative. Once ready, the initial snapshot and all operations issued
before it are ordered on the same worker queue, so the cache cannot overwrite a newer mutation result.

---

## 3. Interfaces / APIs

### 3.1 `src/credentials/include/holonight_credentials/credential_store.h`

```cpp
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace holonight_credentials {

// Abstract interface (REQ-C-001). QObject-derived for the same reason
// holonight_persistence::ConversationRepository is: store/retrieve/remove results cross a real
// thread boundary in the real implementation (REQ-NF-001), so Qt's signal/slot system is used
// rather than std::function callbacks (see docs/sdd/sqlite-conversation-persistence/DESIGN.md §4.3
// for the fuller rationale, which applies unchanged here).
//
// Exactly one opaque secret per provider ID (REQ-F-007/REQ-C-005) — no credential "kind"/"scope"
// compound key. `providerId` is a free-form string ("openai", "anthropic", "google", ...).
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
  // never a libsecret/D-Bus round trip on this call (see Key Decision §5.3).
  [[nodiscard]] virtual QStringList listConfiguredProviders() const = 0;
  [[nodiscard]] virtual bool hasCredential(const QString& providerId) const = 0;
  [[nodiscard]] virtual bool isReady() const = 0;

  // Sticky: false once the real implementation has ever failed to reach the Secret Service
  // (REQ-F-005/006). FakeCredentialStore always returns true (REQ-F-005's "always available").
  [[nodiscard]] virtual bool isAvailable() const = 0;

 Q_SIGNALS:
  void storeCompleted(QString providerId);
  void retrieveCompleted(QString providerId, bool found, QString secret);
  void removeCompleted(QString providerId);
  void operationFailed(QString providerId, QString operation, QString reason);
  void ready();

  // Emitted exactly once per instance, the first time the real implementation fails to reach the
  // Secret Service (REQ-F-005). FakeCredentialStore never emits this.
  void unavailable(QString reason);
};

}  // namespace holonight_credentials
```

### 3.2 `src/credentials/include/holonight_credentials/detail/credential_store_worker.h`

```cpp
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace holonight_credentials::detail {

// Lives on the dedicated worker thread for its entire lifetime (REQ-NF-001). Every libsecret call
// in the system happens here, using libsecret's synchronous "simple password" API — safe to block
// this thread because it has no other responsibility and no UI to keep responsive (Key Decision
// §5.1). Not part of the public API; constructed and owned exclusively by
// SecretServiceCredentialStore. Its header is reachable (not physically hidden) specifically so
// tests/credentials/test_credential_store_worker_threading.cpp can white-box-verify thread affinity,
// mirroring detail::ConversationRepositoryWorker's precedent.
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
  // One-time startup enumeration (§2.1); also this module's only connectivity probe. Posted once,
  // automatically, by SecretServiceCredentialStore's constructor — nothing else calls this.
  void loadKnownProviderIds();

  void store(const QString& providerId, const QString& secret);
  void retrieve(const QString& providerId);
  void remove(const QString& providerId);

 Q_SIGNALS:
  void storeCompleted(QString providerId);
  void retrieveCompleted(QString providerId, bool found, QString secret);
  void removeCompleted(QString providerId);
  void operationFailed(QString providerId, QString operation, QString reason);
  void unavailable(QString reason);

  // Worker-internal only — deliberately not mirrored onto the public CredentialStore interface
  // (Key Decision §5.3). QStringList is an already-registered Qt meta-type (§6), so this crosses
  // the worker→GUI queued-signal boundary with no extra registration.
  void knownProviderIdsLoaded(QStringList providerIds);

 private:
  // Classifies and frees error. Only connection failures set available_ false and emit unavailable;
  // recoverable errors fail the current request only.
  QString handleLibsecretError(struct _GError* error);

  void cancelPendingOperations();

  bool available_ = true;
};

}  // namespace holonight_credentials::detail
```

`store()`/`retrieve()`/`remove()` take `const QString&` (not by value) — matching
`detail::ConversationRepositoryWorker`'s own slot signatures (§3.4 of the persistence DESIGN), since
these are ordinary invoked slots, not signals; no copy-into-a-queued-event is implied here the way it
is for the façade's own functor captures (§3.3).

### 3.3 `src/credentials/include/holonight_credentials/secret_service_credential_store.h`

```cpp
#pragma once

#include "holonight_credentials/credential_store.h"
#include "holonight_credentials/detail/credential_store_worker.h"

#include <QSet>
#include <QThread>

namespace holonight_credentials {

// GUI-thread-resident façade (REQ-NF-001/REQ-C-002). Owns a QThread and a private
// detail::CredentialStoreWorker moved onto it — the exact SqliteConversationRepository shape
// (docs/sdd/sqlite-conversation-persistence/DESIGN.md §4.4). Every public store/retrieve/remove
// method does nothing but package the request and hand it to the worker via a functor-based,
// Qt::QueuedConnection QMetaObject::invokeMethod() call. listConfiguredProviders()/hasCredential()
// are the one deliberate exception: answered synchronously from known_provider_ids_, never posted
// to the worker (Key Decision §5.3).
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
  [[nodiscard]] bool isReady() const override;
  [[nodiscard]] bool isAvailable() const override;

 private:
  QThread worker_thread_;
  detail::CredentialStoreWorker* worker_;  // owned by worker_thread_ (moveToThread)
  QSet<QString> known_provider_ids_;       // GUI-thread-only cache backing §2.5's read methods
  bool available_ = true;
  bool ready_ = false;
};

}  // namespace holonight_credentials
```

Construction/destruction sketch (full rationale in §5.1/§5.2, matching
`SqliteConversationRepository`'s §3.5/§4.4 precedent exactly):

```cpp
SecretServiceCredentialStore::SecretServiceCredentialStore(QObject* parent)
    : CredentialStore(parent), worker_(new detail::CredentialStoreWorker) {
  worker_->moveToThread(&worker_thread_);
  connect(&worker_thread_, &QThread::finished, worker_, &QObject::deleteLater);

  connect(worker_, &detail::CredentialStoreWorker::storeCompleted, this,
          [this](const QString& providerId) {
            if (available_) {
              known_provider_ids_.insert(providerId);
            }
            emit storeCompleted(providerId);
          });
  connect(worker_, &detail::CredentialStoreWorker::retrieveCompleted, this,
          &CredentialStore::retrieveCompleted);
  connect(worker_, &detail::CredentialStoreWorker::removeCompleted, this,
          [this](const QString& providerId) {
            known_provider_ids_.remove(providerId);
            emit removeCompleted(providerId);
          });
  connect(worker_, &detail::CredentialStoreWorker::unavailable, this, [this](const QString& reason) {
    available_ = false;
    emit unavailable(reason);
  });
  connect(worker_, &detail::CredentialStoreWorker::knownProviderIdsLoaded, this,
          [this](const QStringList& providerIds) {
            known_provider_ids_ = QSet<QString>(providerIds.begin(), providerIds.end());
            ready_ = true;
            emit ready();
          });

  worker_thread_.start();
  QMetaObject::invokeMethod(worker_, [w = worker_] { w->loadKnownProviderIds(); }, Qt::QueuedConnection);
}

SecretServiceCredentialStore::~SecretServiceCredentialStore() {
  worker_->cancelPendingOperations();
  worker_thread_.quit();
  if (!worker_thread_.wait(kShutdownTimeoutMs)) {
    qFatal("Credential worker did not stop after cancellation");
  }
}

void SecretServiceCredentialStore::store(QString providerId, QString secret) {
  QMetaObject::invokeMethod(
      worker_,
      [worker = worker_, providerId = std::move(providerId), secret = std::move(secret)] {
        worker->store(providerId, secret);
      },
      Qt::QueuedConnection);
}

// retrieve()/remove() follow the identical shape — omitted here, see §2.2/§2.3.

QStringList SecretServiceCredentialStore::listConfiguredProviders() const {
  return QStringList(known_provider_ids_.begin(), known_provider_ids_.end());
}

bool SecretServiceCredentialStore::hasCredential(const QString& providerId) const {
  return known_provider_ids_.contains(providerId);
}

bool SecretServiceCredentialStore::isAvailable() const { return available_; }
bool SecretServiceCredentialStore::isReady() const { return ready_; }
```

### 3.4 `tests/credentials/fake_credential_store.h`

```cpp
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
    const auto it = secrets_.constFind(providerId);
    if (it == secrets_.constEnd()) {
      emit retrieveCompleted(providerId, false, QString());
      return;
    }
    emit retrieveCompleted(providerId, true, it.value());
  }

  void remove(QString providerId) override {
    secrets_.remove(providerId);
    emit removeCompleted(providerId);
  }

  [[nodiscard]] QStringList listConfiguredProviders() const override {
    return QStringList(secrets_.keyBegin(), secrets_.keyEnd());
  }

  [[nodiscard]] bool hasCredential(const QString& providerId) const override {
    return secrets_.contains(providerId);
  }

  [[nodiscard]] bool isAvailable() const override { return true; }
  [[nodiscard]] bool isReady() const override { return true; }

 private:
  QHash<QString, QString> secrets_;
};

}  // namespace holonight_credentials
```

---

## 4. libsecret Schema Design

### 4.1 Schema definition (`src/credentials/src/secret_service_credential_store.cpp`, anonymous namespace)

```cpp
#include <libsecret/secret.h>

namespace {

const QString kSchemaName = QStringLiteral("ai.holonight.ProviderCredential");

const SecretSchema* credentialSchema() {
  static const SecretSchema kSchema = {
      "ai.holonight.ProviderCredential", SECRET_SCHEMA_NONE,
      {
          {"provider_id", SECRET_SCHEMA_ATTRIBUTE_STRING},
          {nullptr, static_cast<SecretSchemaAttributeType>(0)},
      }};
  return &kSchema;
}

QByteArray labelFor(const QString& providerId) {
  return QStringLiteral("Holonight AI: %1 API key").arg(providerId).toUtf8();
}

}  // namespace
```

One attribute — `provider_id` (`SECRET_SCHEMA_ATTRIBUTE_STRING`) — matching REQ-C-005's single-key
data model exactly: one Secret Service item per provider ID, nothing else identifies a stored secret.
`SECRET_SCHEMA_NONE` (no flags) is used, not `SECRET_SCHEMA_DONT_MATCH_NAME`, specifically because the
startup enumeration (§2.1) needs to search with an **empty** attribute table, which
`SECRET_SCHEMA_DONT_MATCH_NAME` would reject with `SECRET_ERROR_EMPTY_TABLE` (confirmed against
libsecret's own attribute-validation source — see §8 Alternatives for why `DONT_MATCH_NAME` was
considered and rejected).

The **collection** is always `SECRET_COLLECTION_DEFAULT` (the user's default/login keyring) — see §5.5.

### 4.2 Calls the worker makes

| Operation | libsecret call | Blocking? |
|---|---|---|
| `store()` | `secret_password_store_sync(...)` with the worker's `GCancellable` | Yes — worker thread only (§5.1) |
| `retrieve()` | `secret_password_lookup_sync(...)` with the worker's `GCancellable`, then `secret_password_free(password)` | Yes |
| `remove()` | `secret_password_clear_sync(...)` with the worker's `GCancellable`; a clean false return remains success (§2.4) | Yes |
| `loadKnownProviderIds()` (startup only) | `secret_service_search_sync(...)` with the worker's `GCancellable`, then attributes-only enumeration (never `SECRET_SEARCH_LOAD_SECRETS`) | Yes |

The worker owns a `GCancellable` shared by its blocking calls. `cancelPendingOperations()` is the sole
thread-safe method called directly by the façade during teardown; it only invokes
`g_cancellable_cancel()`, which GLib permits from another thread. Teardown then requests thread exit and
uses a bounded wait. Failure to stop within that bound is a fail-fast invariant violation: returning would
destroy a running `QThread`, while waiting again would reintroduce an unbounded GUI hang. A test-injected
blocking backend verifies that cancellation keeps ordinary teardown within the bound.

### 4.3 Error classification

```cpp
namespace {

// Connection-loss errors (no session bus, missing service, or a disconnected transport) transition
// the instance to sticky unavailable state. SECRET_ERROR_IS_LOCKED, cancellation, permission/prompt
// rejection, and unknown operation errors fail only the current request. An unknown error is not by
// itself evidence that the service connection is permanently unusable.
bool isConnectionFailure(const GError& error);

}  // namespace
```

The "not found" outcome for `retrieve()` is **not** a `GError` case at all — it is the
`password == nullptr && error == nullptr` branch libsecret's own contract guarantees for a clean miss
(§2.3). This is the actual mechanism distinguishing REQ-F-002's "not found" from REQ-F-005/006's
"unavailable". Every `GError` emits `operationFailed`; classification determines only whether the
instance also transitions to unavailable.

---

## 5. Key Decisions With Rationale

### 5.1 libsecret's synchronous "simple password" API on the worker thread — not the async callback API

`secret_password_store_sync`/`_lookup_sync`/`_clear_sync` block the calling thread until the D-Bus round
trip completes. That is exactly acceptable here, and preferable to the async (`secret_password_store`,
callback-based) API, because:

- The worker thread's **only** job is to service credential requests, serially, one at a time — there is
  no UI, no other event source, nothing for a blocking call to starve. This is the identical argument
  `docs/sdd/sqlite-conversation-persistence/DESIGN.md` §4.4 already made for `QSqlDatabase`'s blocking
  API on `ConversationRepositoryWorker`'s thread.
- The async callback API would require the worker to run (and correctly integrate) its own `GMainLoop`
  iteration or a `GMainContext`-to-Qt-event-loop bridge for the callbacks to ever fire — genuine added
  complexity (a second event-loop flavor coexisting with Qt's on the same thread) for zero behavioral
  benefit, since REQ-NF-001's actual requirement ("never block the **caller's** thread") is already fully
  satisfied by the worker-thread boundary alone. Blocking the worker thread is invisible to every other
  part of the system.
- Using the sync API keeps every worker method a plain, linear, easy-to-review function — no
  `GAsyncReadyCallback` continuation-passing, no additional lifetime management for a pending
  `GCancellable`/callback pair across the request.

### 5.2 No `initialize()` method — construction implicitly probes availability

Unlike `ConversationRepository::initialize()` (which exists because SQLite needs an explicit
`open()` + migration step before any query is valid), libsecret's simple API has no analogous
"connect first" call — every `secret_password_*_sync()` call is already fully self-contained and
implicitly negotiates whatever D-Bus session/keyring daemon it needs. Requiring callers to invoke a
mirrored `initialize()` here would add an API step with no corresponding libsecret concept behind it —
pure ceremony. Instead, `SecretServiceCredentialStore`'s constructor posts one internal
`loadKnownProviderIds()` call (§2.1), which serves double duty as both the cache warm-up (§5.3) and the
module's only connectivity probe — if the Secret Service is unreachable, this is normally the first
thing to discover that, within milliseconds of construction, well before the caller's first real
`store()`/`retrieve()`. If a caller's first operation happens to race ahead of this probe completing,
that operation still correctly discovers unavailability itself (§2.2 step 2) — there is no dependency
the caller must wait on.

### 5.3 `listConfiguredProviders()`/`hasCredential()`: a façade-side cache, not signal-based, not a live libsecret query

REQ-F-004's two constraints — "return results synchronously" (echoing the fake's shape) and "must not
block the UI thread" (for the real implementation) — cannot both be satisfied by a live libsecret call on
the calling thread (that would block on D-Bus) nor by routing through the worker+signal round trip used
for `store`/`retrieve`/`remove` (that would make these two methods asynchronous, breaking the "same
synchronous signature as the fake" requirement). The resolution: `SecretServiceCredentialStore` maintains
`known_provider_ids_`, a plain `QSet<QString>` living entirely on the GUI thread, kept in sync by:

1. A one-time startup enumeration (§2.1) — the only place this module ever calls
   `secret_service_search_sync()` — seeding the cache with whatever was already stored in a prior
   session.
2. Every successful `storeCompleted`/`removeCompleted` the worker reports, incrementally
   inserting/removing exactly the one provider ID that changed (§2.2/§2.4). Failed requests never emit
   success completion signals.

`listConfiguredProviders()`/`hasCredential()` then read `known_provider_ids_` directly — zero D-Bus calls,
zero worker-thread round trip, zero signal emission, satisfying REQ-F-004's "synchronous" requirement
literally and identically for both `FakeCredentialStore` (which simply mirrors its own `secrets_` map)
and `SecretServiceCredentialStore` (which mirrors the cache). `isReady()`/`ready()` makes the initial
cache state explicit, so callers cannot mistake an unfinished enumeration for an authoritative miss.
This is option (b) from the task's framing,
chosen over option (a) ("also async/signal-based for the real implementation") specifically because (a)
would give the two implementations **different call shapes** for the same two methods — a real API
inconsistency a future caller would have to special-case, whereas SPEC's own acceptance criteria describe
one synchronous shape for both.

### 5.4 Single worker thread ⇒ FIFO ordering ⇒ no explicit synchronization needed between `store()`/`remove()` for the same provider ID

Every request — regardless of which public method dispatched it — is posted via
`Qt::QueuedConnection` onto the **same** worker thread's **single** event queue, and Qt delivers queued
events in the order they were posted (per sender/receiver pair, and in practice per-thread for a
single-threaded receiver processing one event loop). Given `SecretServiceCredentialStore`'s own public
methods are only ever called from the GUI thread (the same precondition REQ-NF-001 already assumes), two
calls like `store("openai", secretA)` followed immediately by `remove("openai")` are guaranteed to be
serviced by the worker in exactly that order — "last call wins" is well-defined, not a race. No mutex,
atomic, or additional queue discipline is introduced beyond what `QThread`'s own event loop already
provides.

### 5.5 Collection: always `SECRET_COLLECTION_DEFAULT`, never a caller-chosen collection

The module does not expose any way to pick a non-default keyring/collection. `SECRET_COLLECTION_DEFAULT`
resolves to the user's default ("login") collection under GNOME Keyring, and to KWallet's equivalent
default wallet under its Secret-Service-compatibility layer (`ksecretd`/`kwalletd`'s D-Bus
implementation) — the only collection choice that requires zero additional UI, zero configuration, and
matches every other desktop application's default expectation for "just store my password somewhere
reasonable."

---

## 6. `Q_DECLARE_METATYPE` / Cross-Thread Signal Registration

**None is needed.** Every signal parameter this module ever emits across the worker→GUI queued-connection
boundary is a `QString`, `bool`, or `QStringList` — all three are Qt meta-types Qt itself registers at
startup (`QString`/`bool` are fundamental to `QMetaType`; `QStringList` has shipped as a registered
meta-type since Qt 4). Unlike `holonight_persistence::ConversationSummary`/`LoadedConversation`/`ModelId`
(custom value types that needed `Q_DECLARE_METATYPE` + a `registerMetaTypes()` call, per that module's
§3.2/§8), `holonight_credentials` introduces no custom struct that ever crosses a thread boundary, so
there is **no `registerMetaTypes()`-equivalent function anywhere in this module** — a future implementer
should not add one "just in case"; it would be dead ceremony.

---

## 7. `CMakeLists.txt` Changes

### 7.1 `src/credentials/CMakeLists.txt` — `INTERFACE` → `STATIC`

```cmake
add_library(holonight_credentials STATIC
    include/holonight_credentials/credential_store.h
    include/holonight_credentials/secret_service_credential_store.h
    include/holonight_credentials/detail/credential_store_worker.h
    src/credential_store.cpp
    src/secret_service_credential_store.cpp
    src/detail/credential_store_worker.cpp
)

target_include_directories(holonight_credentials PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${LIBSECRET_INCLUDE_DIRS}
)

target_link_libraries(holonight_credentials PUBLIC
    Qt6::Core
    ${LIBSECRET_LIBRARIES}
)

target_compile_features(holonight_credentials PUBLIC cxx_std_23)
```

Every `Q_OBJECT` header (`credential_store.h`, `secret_service_credential_store.h`,
`detail/credential_store_worker.h`) is listed directly as a target source — the same AUTOMOC
from-scratch-discovery gotcha `sqlite-conversation-persistence`'s DESIGN §6.1 already documented
applies unchanged here. `CMAKE_AUTOMOC ON` is already global (root `CMakeLists.txt`), so no per-target
`AUTOMOC` property is added. `holonight_domain` is **not** linked — this module has no dependency on it
(unlike `holonight_persistence`, which needs `ModelId`/`Message`); `Qt6::Core` and libsecret are the only
dependencies, matching REQ-C-004 exactly. The existing root-level `pkg_check_modules(LIBSECRET REQUIRED
libsecret-1)` call is unchanged.

### 7.2 `tests/CMakeLists.txt`

```diff
 add_executable(test_holonight_ai
   main.cpp
   test_placeholder.cpp
   domain/test_message.cpp
   domain/test_conversation.cpp
   domain/test_model_id.cpp
   domain/test_stream_event.cpp
   providers/test_ollama_provider.cpp
   application/test_chat_controller.cpp
   application/test_chat_view_model.cpp
   application/test_conversation_list_model.cpp
   persistence/test_database_path.cpp
   persistence/test_migration_runner.cpp
   persistence/test_conversation_record.cpp
   persistence/test_conversation_repository_worker_threading.cpp
   persistence/test_conversation_repository_sqlite.cpp
+  credentials/test_credential_store_fake.cpp
+  credentials/test_credential_store_worker_threading.cpp
 )
 set_target_properties(test_holonight_ai PROPERTIES AUTOMOC ON)
 target_compile_features(test_holonight_ai PRIVATE cxx_std_23)
 target_include_directories(test_holonight_ai PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
 target_link_libraries(test_holonight_ai PRIVATE
   GTest::gtest
   GTest::gmock
   Qt6::Gui
   Qt6::Sql
   Qt6::Test
   holonight_domain
   holonight_providers
   holonight_application
   holonight_persistence
+  holonight_credentials
 )
```

`tests/credentials/fake_credential_store.h` needs no corresponding `.cpp` (header-only test double,
matching `fake_conversation_repository.h`) and needs no CMake listing beyond being `#include`d by the two
`.cpp` files above — mirroring how `fake_conversation_repository.h` is never itself named in
`tests/CMakeLists.txt`. No new third-party test dependency is introduced — `GTest::gtest`/`Qt6::Test`
(for `QSignalSpy`) already cover everything this module's tests need.

`test_credential_store_worker_threading.cpp` (REQ-NF-004's "optional/skippable" test file) is still
listed unconditionally in the target sources — matching how
`persistence/test_conversation_repository_worker_threading.cpp` is unconditionally built today — but its
individual `TEST()` cases guard any assertion that requires a real, reachable Secret Service (e.g. an
actual `store()`→`retrieve()` round trip) behind a runtime skip
(`GTEST_SKIP() << "no Secret Service reachable in this environment"`) triggered by the very first
`unavailable()` signal the worker itself reports — so the suite self-detects a missing keyring daemon
rather than depending on a compile-time `#ifdef`, and never fails CI for an environment property outside
this module's control. Only the thread-affinity assertion (`SlotsExecuteOnWorkerThread`, mirroring
`ConversationRepositoryWorkerThreading`'s own test, §7.3 of the persistence DESIGN) needs no Secret
Service at all and always runs.

---

## 8. Alternatives Considered

- **libsecret's async callback API instead of the synchronous "simple password" functions** (rejected,
  §5.1): would require the worker to run its own `GMainLoop`/`GMainContext` integration for callbacks to
  ever fire, adding a second event-loop flavor to reason about for no behavioral benefit — the worker
  thread already has nothing else to do, so blocking it is free.
- **A signal-based/asynchronous `listConfiguredProviders()`/`hasCredential()` for the real implementation**
  (rejected, §5.3): satisfies "must not block the UI thread" but gives `FakeCredentialStore` and
  `SecretServiceCredentialStore` different call shapes for the same two methods, contradicting the
  spec's implied single synchronous signature and forcing every future caller to special-case which
  implementation it holds.
- **A live `secret_service_search_sync()` call on every `listConfiguredProviders()`/`hasCredential()`
  invocation** (rejected, §5.3): technically synchronous-looking in code, but blocks on a D-Bus round
  trip — directly violates REQ-NF-001/REQ-F-004's "must not block the UI thread." Relying instead on a
  GUI-thread-local cache (§5.3) is provisional until startup enumeration completes. `isReady()` and
  `ready()` expose that boundary without a synchronous D-Bus call at construction time.
- **`SECRET_SCHEMA_DONT_MATCH_NAME` on the credential schema** (rejected, §4.1): would reject the startup
  enumeration's intentionally-empty attribute search with `SECRET_ERROR_EMPTY_TABLE`, breaking §2.1's
  cache warm-up; `SECRET_SCHEMA_NONE` (matching by schema name plus whatever attributes are given, empty
  or not) is what the enumeration actually needs.
- **A caller-selectable collection instead of always `SECRET_COLLECTION_DEFAULT`** (rejected, §5.5): no
  requirement asks for multi-collection support, and it would add UI/config surface (REQ-C-003's
  isolation constraint explicitly defers any settings UI to a future cycle).
- **Treating every `GError` as sticky unavailable** (rejected): locked collections, rejected prompts,
  cancellation, and unknown request errors may be recoverable. They use `operationFailed` without
  poisoning the instance; only classified connection failures emit sticky `unavailable`.

---

## 9. Known Risks

- **Startup cache warm-up is asynchronous.** The API exposes this through `isReady()` and `ready()`;
  callers must wait for readiness before treating query results as authoritative.
- **Cache staleness against concurrent external modification.** `known_provider_ids_` reflects only what
  this process's own `store()`/`remove()` calls (plus the one startup enumeration) have observed. If a
  secret is added or removed by another process — a second instance of this application, or a manual
  `secret-tool`/`kwalletmanager` edit — while this instance is running, the cache silently goes stale
  until the next `store()`/`remove()` call for that same provider ID happens to correct it. Accepted:
  this application is assumed single-instance-per-user, and no requirement asks for cross-process cache
  invalidation.
- **libsecret's synchronous "simple password" calls may pump a private, transient GLib main loop
  internally** to wait for the D-Bus reply, depending on the platform's GLib/D-Bus integration. This is
  safe and isolated on `CredentialStoreWorker`'s own dedicated `QThread` — a bare `QThread` with no GLib
  event-loop integration of its own, so libsecret's transient loop never contends with Qt's event
  processing — but would **not** be safe to call directly on the Qt GUI thread (already fully avoided by
  this design's worker-thread isolation, REQ-NF-001).
- **GNOME Keyring vs. KWallet's Secret-Service-compatibility layer may behave subtly differently** under
  the same libsecret calls — e.g., differing default-collection auto-creation behavior on a
  fresh/unconfigured desktop session, or differing prompt UX when a collection is locked. This module
  makes no GNOME-Keyring-specific or KWallet-specific assumption beyond the standard
  `org.freedesktop.Secret.Service` D-Bus contract libsecret itself targets; any divergence here is a
  desktop-environment compatibility question outside this module's control, not something this design
  attempts to paper over.
- **`store()` called concurrently with `remove()` for the same provider ID is well-ordered but not
  "atomic" from the caller's point of view** — §5.4 guarantees the worker services them in the order the
  GUI thread issued them, but two different call sites in application code racing to both mutate the same
  `providerId` is still an application-level logic question this module does not arbitrate (it never
  will, by design — the SPEC gives it no "compare and swap" or optimistic-locking concept for a single
  opaque secret per provider ID).

---

## Document History

| Version | Date       | Author | Changes                          |
|---------|------------|--------|-----------------------------------|
| 1.0     | 2026-07-22 | Claude | Initial design from SPEC.md v1.0  |
