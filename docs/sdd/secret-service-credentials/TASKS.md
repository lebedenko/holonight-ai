# SDD Tasks — secret-service-credentials

Tasks T-001 through T-030 record the original implementation state. The revised design supersedes their
legacy failure/readiness semantics; T-031 through T-035 are required before the SDD is complete.

## Build Infrastructure

- [x] T-001: Update src/credentials/CMakeLists.txt — convert INTERFACE to STATIC
  - REQs: REQ-C-004
  - Check: src/credentials/CMakeLists.txt converts `holonight_credentials` from `INTERFACE` to `STATIC`, lists `credential_store.h`, `secret_service_credential_store.h`, and `detail/credential_store_worker.h` as target sources for AUTOMOC, links `Qt6::Core` and `libsecret` (via `pkg_check_modules(libsecret REQUIRED libsecret-1)`), and `cmake build` succeeds with no linker errors.

- [x] T-002: Update tests/CMakeLists.txt — add credentials test files and link holonight_credentials
  - REQs: REQ-NF-004, REQ-C-004
  - Check: tests/CMakeLists.txt adds `credentials/test_credential_store_fake.cpp` and `credentials/test_credential_store_worker_threading.cpp` to test_holonight_ai target sources, and target_link_libraries includes `holonight_credentials` as PRIVATE.

## Abstract Interface

- [x] T-003: Create credential_store.h — abstract CredentialStore interface
  - REQs: REQ-C-001, REQ-F-001, REQ-F-002, REQ-F-003, REQ-F-004
  - Check: src/credentials/include/holonight_credentials/credential_store.h declares `class CredentialStore : public QObject { Q_OBJECT }` with pure virtual methods `store()`, `retrieve()`, `remove()`, `listConfiguredProviders()`, `hasCredential()`, `isAvailable()` matching DESIGN §3.1 exactly; signals `storeCompleted()`, `retrieveCompleted()`, `removeCompleted()`, `unavailable()`; destructor is `= default`; and src/credentials/src/credential_store.cpp exists (minimal, containing destructor definition if needed for AUTOMOC). All three signal parameters (`QString providerId`, `bool found`, `QString secret`) use only standard Qt meta-types per DESIGN §6.

## libsecret Integration

- [x] T-004: Create libsecret schema helpers and error classification in secret_service_credential_store.cpp anonymous namespace
  - REQs: REQ-F-007, REQ-NF-003
  - Check: src/credentials/src/secret_service_credential_store.cpp defines anonymous namespace containing `credentialSchema()` returning static `SecretSchema` with one attribute `provider_id` (SECRET_SCHEMA_ATTRIBUTE_STRING) and `SECRET_SCHEMA_NONE` flags per DESIGN §4.1, `labelFor(providerId)` returning formatted UTF-8 label "Holonight AI: {providerId} API key", and `isRecognizedButBenign(GError&)` returning false per DESIGN §4.3; no `qDebug()`/`qWarning()` statements in these helpers contain the secret parameter or any credential value.

## Worker Thread Implementation — Header

- [x] T-005: Create detail/credential_store_worker.h — CredentialStoreWorker header
  - REQs: REQ-NF-001, REQ-C-001, REQ-C-002
  - Check: src/credentials/include/holonight_credentials/detail/credential_store_worker.h declares `class CredentialStoreWorker : public QObject` with Q_SLOTS for `loadKnownProviderIds()`, `store()`, `retrieve()`, `remove()` per DESIGN §3.2; Q_SIGNALS for `storeCompleted()`, `retrieveCompleted()`, `removeCompleted()`, `unavailable()`, and internal `knownProviderIdsLoaded()` per DESIGN §3.2; private methods `handleLibsecretError(GError*)` and member `bool available_`. Constructor signature matches DESIGN §3.2. Comments explain this class lives on worker thread only and is not part of public API.

## Worker Thread Implementation — Initialization

- [x] T-006: Implement CredentialStoreWorker::loadKnownProviderIds() — startup enumeration and connectivity probe
  - REQs: REQ-F-004, REQ-F-005
  - Check: src/credentials/src/detail/credential_store_worker.cpp implements loadKnownProviderIds() to call `secret_service_search_sync(nullptr, credentialSchema(), nullptr, SECRET_SEARCH_NONE, nullptr, &error)` (empty attribute table per DESIGN §2.1/§4.1), iterate results calling `secret_item_get_attributes()` to extract `provider_id` attributes (never SECRET_SEARCH_LOAD_SECRETS, per REQ-NF-003 "no secret logging"), emit `knownProviderIdsLoaded(QStringList)` on success, or call `handleLibsecretError(error)` on failure; this is this module's only connectivity probe (DESIGN §5.2).

## Worker Thread Implementation — Store Operation

- [x] T-007: Implement CredentialStoreWorker::store() — create or overwrite secret via libsecret
  - REQs: REQ-F-001, REQ-F-005, REQ-NF-001
  - Check: src/credentials/src/detail/credential_store_worker.cpp implements store() to if `!available_` emit `storeCompleted()` immediately (benign no-op per DESIGN §2.2 step 2 "skip straight to step 4"), else call `secret_password_store_sync(credentialSchema(), SECRET_COLLECTION_DEFAULT, labelFor(providerId), secret.toUtf8().constData(), nullptr, &error, "provider_id", providerId.toUtf8().constData(), nullptr)` per DESIGN §4.2; on error call `handleLibsecretError(error)`, always emit `storeCompleted(providerId)` at end per DESIGN §2.2 step 2 "always (success, benign no-op, or just-detected failure alike)"; no blocking on caller's thread, all operations on worker thread only.

## Worker Thread Implementation — Retrieve Operation

- [x] T-008: Implement CredentialStoreWorker::retrieve() — fetch secret or not-found via libsecret
  - REQs: REQ-F-002, REQ-F-005, REQ-F-006, REQ-NF-001
  - Check: src/credentials/src/detail/credential_store_worker.cpp implements retrieve() to if `!available_` emit `retrieveCompleted(providerId, false, QString())` immediately (REQ-F-006 "not found without error"), else call `gchar* password = secret_password_lookup_sync(credentialSchema(), nullptr, &error, "provider_id", providerId.toUtf8().constData(), nullptr)` per DESIGN §4.2, then: if `error != nullptr` call `handleLibsecretError(error)` then emit `retrieveCompleted(providerId, false, QString())` (REQ-F-006 "exact same wire shape as genuine miss"), or if `error == nullptr && password == nullptr` emit `retrieveCompleted(providerId, false, QString())` (genuine miss, available_ untouched per DESIGN §2.3), or if `error == nullptr && password != nullptr` convert to QString, call `secret_password_free(password)`, emit `retrieveCompleted(providerId, true, secret)` (REQ-F-002 success case); never log or touch the secret value in debug output (REQ-NF-003).

## Worker Thread Implementation — Remove Operation

- [x] T-009: Implement CredentialStoreWorker::remove() — delete secret or no-op if not found via libsecret
  - REQs: REQ-F-003, REQ-F-005, REQ-NF-001
  - Check: src/credentials/src/detail/credential_store_worker.cpp implements remove() to if `!available_` emit `removeCompleted(providerId)` immediately (benign no-op), else call `secret_password_clear_sync(credentialSchema(), nullptr, &error, "provider_id", providerId.toUtf8().constData(), nullptr)` per DESIGN §4.2 (return value intentionally discarded per DESIGN §2.4 — REQ-F-003 "still succeeds if none exists"), on error call `handleLibsecretError(error)`, always emit `removeCompleted(providerId)`; no error signal on non-existent entries (REQ-F-003 "no error").

## Worker Thread Implementation — Error Handling

- [x] T-010: Implement CredentialStoreWorker::handleLibsecretError() — error classification and unavailable state
  - REQs: REQ-F-005, REQ-NF-003
  - Check: src/credentials/src/detail/credential_store_worker.cpp implements handleLibsecretError() to call `g_error_free(error)`, check if `available_ == true` (only emit unavailable once per DESIGN §2.2/§5.4), if true set `available_ = false` and emit `unavailable(reason)` with a human-readable message (e.g., "Failed to reach Secret Service: {error description}") derived from GError's domain/code per DESIGN §4.3 (collapse all domains to "unavailable"), then return; all GError messages logged via `qWarning()` must NOT contain any credential values, only provider IDs and generic error text (REQ-NF-003).

## SecretServiceCredentialStore Façade — Header

- [x] T-011: Create secret_service_credential_store.h — SecretServiceCredentialStore façade header
  - REQs: REQ-C-001, REQ-C-002, REQ-NF-001
  - Check: src/credentials/include/holonight_credentials/secret_service_credential_store.h declares `class SecretServiceCredentialStore : public CredentialStore` with constructor `(QObject* parent = nullptr)`, destructor calling `worker_thread_.quit(); worker_thread_.wait()` per DESIGN §3.3, public overrides for `store()`, `retrieve()`, `remove()`, `listConfiguredProviders()`, `hasCredential()`, `isAvailable()`; private members `QThread worker_thread_`, `detail::CredentialStoreWorker* worker_`, `QSet<QString> known_provider_ids_`, `bool available_`; comments reference DESIGN §3.3/§5.3 for the cache architecture.

## SecretServiceCredentialStore Façade — Constructor & Initialization

- [x] T-012: Implement SecretServiceCredentialStore constructor — moveToThread wiring and signal forwarding
  - REQs: REQ-NF-001, REQ-F-004, REQ-F-005
  - Check: src/credentials/src/secret_service_credential_store.cpp implements constructor per DESIGN §3.3 sketch: constructs `worker_ = new detail::CredentialStoreWorker`, calls `worker_->moveToThread(&worker_thread_)`, connects `worker_thread_.finished` to `worker_.deleteLater`, wires all four worker signals (`storeCompleted`, `retrieveCompleted`, `removeCompleted`, `unavailable`) to corresponding façade signals, adds additional forwarding slots for `storeCompleted` to `if (available_) known_provider_ids_.insert(providerId); emit storeCompleted(providerId);` and `removeCompleted` to `known_provider_ids_.remove(providerId); emit removeCompleted(providerId);` per DESIGN §2.2 step 3/§2.4 (guarded by `available_` check), connects `knownProviderIdsLoaded` to update `known_provider_ids_` wholesale (DESIGN §2.1), calls `worker_thread_.start()`, posts `QMetaObject::invokeMethod(worker_, [w=worker_]{w->loadKnownProviderIds();}, Qt::QueuedConnection)` to trigger startup enumeration (DESIGN §2.1); construction never blocks (REQ-NF-001).

## SecretServiceCredentialStore Façade — Destructor

- [x] T-013: Implement SecretServiceCredentialStore destructor — clean thread teardown
  - REQs: REQ-NF-001
  - Check: src/credentials/src/secret_service_credential_store.cpp implements `~SecretServiceCredentialStore()` to call `worker_thread_.quit()` then `worker_thread_.wait()` per DESIGN §3.3 sketch, ensuring worker thread stops and all pending operations complete before destruction finishes.

## SecretServiceCredentialStore Façade — store() Dispatcher

- [x] T-014: Implement SecretServiceCredentialStore::store() — queued dispatcher to worker
  - REQs: REQ-F-001, REQ-NF-001
  - Check: src/credentials/src/secret_service_credential_store.cpp implements `store(providerId, secret)` to package both strings into a lambda and call `QMetaObject::invokeMethod(worker_, [w=worker_, providerId=std::move(providerId), secret=std::move(secret)]{w->store(providerId, secret);}, Qt::QueuedConnection)`, returning immediately (<1ms per REQ-NF-001 benchmark); this mirrors SqliteConversationRepository pattern per REQ-C-002.

## SecretServiceCredentialStore Façade — retrieve() Dispatcher

- [x] T-015: Implement SecretServiceCredentialStore::retrieve() — queued dispatcher to worker
  - REQs: REQ-F-002, REQ-NF-001
  - Check: src/credentials/src/secret_service_credential_store.cpp implements `retrieve(providerId)` with identical functor-based `QMetaObject::invokeMethod` pattern as store(), returning immediately to caller; worker thread executes on its own thread boundary.

## SecretServiceCredentialStore Façade — remove() Dispatcher

- [x] T-016: Implement SecretServiceCredentialStore::remove() — queued dispatcher to worker
  - REQs: REQ-F-003, REQ-NF-001
  - Check: src/credentials/src/secret_service_credential_store.cpp implements `remove(providerId)` with identical functor-based `QMetaObject::invokeMethod` pattern, returning immediately; mirrors store()/retrieve() shape for consistency.

## SecretServiceCredentialStore Façade — Cache-Read Methods

- [x] T-017: Implement SecretServiceCredentialStore cache-read methods — listConfiguredProviders(), hasCredential(), isAvailable()
  - REQs: REQ-F-004, REQ-F-005, REQ-F-006
  - Check: src/credentials/src/secret_service_credential_store.cpp implements all three as per DESIGN §2.5/§5.3: `listConfiguredProviders()` returns `QStringList(known_provider_ids_.begin(), known_provider_ids_.end())` (synchronous, GUI-thread-local, never posts to worker), `hasCredential(providerId)` returns `known_provider_ids_.contains(providerId)` (synchronous, no signal), `isAvailable()` returns `available_`; no libsecret or D-Bus calls in any of these three methods (REQ-NF-001 requires no UI-thread blocking, these three read-only operations satisfy this by avoiding any round-trip); all three return immediately, no event-loop wait required.

## Test Double

- [x] T-018: Create FakeCredentialStore — header-only test double implementation
  - REQs: REQ-C-001, REQ-NF-002, REQ-NF-004
  - Check: tests/credentials/fake_credential_store.h declares `class FakeCredentialStore : public CredentialStore` with inline implementations of all virtual methods, backed by `QHash<QString, QString> secrets_` (in-memory storage), `store()` inserts and emits `storeCompleted()` synchronously in-call-stack (REQ-NF-002 "synchronous"), `retrieve()` looks up secret, emits `retrieveCompleted()` with `found=true/false` before returning, `remove()` deletes and emits `removeCompleted()` synchronously, `listConfiguredProviders()` returns `QStringList(secrets_.keys())` immediately, `hasCredential()` checks `secrets_.contains()`, `isAvailable()` unconditionally returns `true` (REQ-F-005's "always available"), `unavailable()` signal never emitted; all signal emissions happen within the same call stack (no queued connections, no event-loop needed between operations per REQ-NF-002).

## GTest Coverage — Store/Overwrite (REQ-F-001)

- [x] T-019: GTest — FakeCredentialStore store/overwrite operations
  - REQs: REQ-F-001, REQ-NF-002, REQ-NF-004
  - Check: tests/credentials/test_credential_store_fake.cpp contains ≥2 test cases: (1) `TEST(FakeCredentialStoreTest, StoreCreatesSecret)` constructs FakeCredentialStore, calls `store("openai", "sk-123...")`, connects QSignalSpy to `storeCompleted`, verifies spy count==1 and providerId=="openai" before call returns (synchronous, per REQ-NF-002), (2) `TEST(FakeCredentialStoreTest, StoreOverwritesExisting)` calls `store("openai", "sk-old...")` then `store("openai", "sk-new...")`, verifies second signal emitted, calls `retrieve("openai")` and verifies secret=="sk-new..." via spy (overwrite confirmed); both cases verify signal emitted in-call-stack (spy count increments before function return).

## GTest Coverage — Retrieve Found/Not-Found (REQ-F-002)

- [x] T-020: GTest — FakeCredentialStore retrieve found and not-found outcomes
  - REQs: REQ-F-002, REQ-NF-002, REQ-NF-004
  - Check: tests/credentials/test_credential_store_fake.cpp contains ≥2 test cases: (1) `TEST(FakeCredentialStoreTest, RetrieveNotFound)` constructs FakeCredentialStore, calls `retrieve("nonexistent")`, connects QSignalSpy to `retrieveCompleted`, verifies spy emitted with (providerId=="nonexistent", found==false, secret==""), (2) `TEST(FakeCredentialStoreTest, RetrieveFound)` calls `store("openai", "sk-test...")`, then `retrieve("openai")`, verifies spy emitted with (providerId=="openai", found==true, secret=="sk-test..."); both verify "not found" is a normal outcome, not an error (REQ-F-002 "not found...a normal, non-error outcome"); signals emitted synchronously in-call-stack (spy sees them before call returns per REQ-NF-002).

## GTest Coverage — Remove Delete/No-Op (REQ-F-003)

- [x] T-021: GTest — FakeCredentialStore remove delete and no-op outcomes
  - REQs: REQ-F-003, REQ-NF-002, REQ-NF-004
  - Check: tests/credentials/test_credential_store_fake.cpp contains ≥2 test cases: (1) `TEST(FakeCredentialStoreTest, RemoveNonexistent)` constructs FakeCredentialStore, calls `remove("nonexistent")`, connects QSignalSpy to `removeCompleted`, verifies spy emitted with providerId=="nonexistent", no error signal emitted, is benign no-op (REQ-F-003 "succeeds silently if not found"), (2) `TEST(FakeCredentialStoreTest, RemoveExisting)` stores secret for "openai", calls `remove("openai")`, verifies `removeCompleted` signal, then calls `retrieve("openai")` and verifies found==false (deletion confirmed); both cases verify remove() always succeeds and emits completion signal, never hangs or throws.

## GTest Coverage — Query Operations listConfiguredProviders/hasCredential (REQ-F-004)

- [x] T-022: GTest — FakeCredentialStore listConfiguredProviders() and hasCredential() query methods
  - REQs: REQ-F-004, REQ-NF-002, REQ-NF-004
  - Check: tests/credentials/test_credential_store_fake.cpp contains ≥3 test cases: (1) `TEST(FakeCredentialStoreTest, ListConfiguredProvidersEmpty)` on fresh FakeCredentialStore calls `listConfiguredProviders()`, verifies returns empty QStringList, (2) `TEST(FakeCredentialStoreTest, ListConfiguredProvidersPopulated)` calls `store("openai", "...")` and `store("anthropic", "...")`, calls `listConfiguredProviders()`, verifies returns list containing both "openai" and "anthropic", (3) `TEST(FakeCredentialStoreTest, HasCredentialAfterStoreAndRemove)` stores secret for "openai", calls `hasCredential("openai")`, verifies returns true, calls `remove("openai")`, calls `hasCredential("openai")`, verifies returns false; all three return synchronously without any signal (REQ-F-004 "synchronously, never signal-based"); both methods return immediately, no event-loop spin required (REQ-NF-002).

## GTest Coverage — Availability and Unavailable State (REQ-F-005, REQ-F-006, REQ-F-007, REQ-NF-002)

- [x] T-023: GTest — FakeCredentialStore isAvailable() always true and unavailable() never emitted
  - REQs: REQ-F-005, REQ-F-006, REQ-F-007, REQ-NF-002, REQ-NF-004
  - Check: tests/credentials/test_credential_store_fake.cpp contains ≥2 test cases: (1) `TEST(FakeCredentialStoreTest, AlwaysAvailable)` constructs FakeCredentialStore, calls `isAvailable()`, verifies returns true, even after heavy usage (multiple store/retrieve/remove calls), performs `store()`, `retrieve()`, `remove()`, calls `isAvailable()` again and verifies still true (REQ-F-005 "always available"), (2) `TEST(FakeCredentialStoreTest, NeverEmitsUnavailable)` constructs FakeCredentialStore, connects QSignalSpy to `unavailable` signal, performs various operations, spins event loop, verifies spy.count()==0 (signal never emitted per REQ-F-005 "FakeCredentialStore is always available"); this establishes FakeCredentialStore's deterministic, off-line contract for test purposes, contrasting with SecretServiceCredentialStore's potential failure mode.

## GTest Coverage — CredentialStoreWorker Thread Affinity (REQ-NF-001)

- [x] T-024: GTest — CredentialStoreWorker executes all slots on dedicated worker thread (thread-affinity assertion)
  - REQs: REQ-NF-001
  - Check: tests/credentials/test_credential_store_worker_threading.cpp contains white-box test `TEST(CredentialStoreWorkerThreadingTest, SlotsExecuteOnWorkerThread)` that constructs `detail::CredentialStoreWorker`, moves it to a `QThread`, invokes `loadKnownProviderIds()` via `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`, connects a test helper slot to `knownProviderIdsLoaded` that captures `QThread::currentThread()`, spins event loop until signal arrives, verifies captured thread pointer equals the dedicated worker thread (not main thread per REQ-NF-001); this mirrors `test_conversation_repository_worker_threading.cpp`'s `ConversationRepositoryWorkerThreading` pattern, always runs regardless of Secret Service availability (no daemon needed for this assertion).

## GTest Coverage — SecretServiceCredentialStore Threading Performance (REQ-NF-001)

- [x] T-025: GTest — SecretServiceCredentialStore dispatches return immediately without blocking caller
  - REQs: REQ-NF-001
  - Check: tests/credentials/test_credential_store_worker_threading.cpp contains performance test `TEST(SecretServiceCredentialStoreThreadingTest, StoreReturnsImmediately)` that constructs `SecretServiceCredentialStore`, calls `store("openai", "sk-test...")`, measures elapsed time via `std::chrono` until function returns, verifies elapsed < 1ms per REQ-NF-001 benchmark (no D-Bus blocking on caller's thread); may be skipped with `GTEST_SKIP()` if no Secret Service is reachable (optional per DESIGN §7.2 "clearly marked and optional"), but if reachable the assertion confirms no UI-thread blocking happens.

## GTest Coverage — SecretServiceCredentialStore Real-Implementation Round-Trip (Optional, REQ-NF-001, REQ-F-005, REQ-F-006)

- [x] T-026: GTest — SecretServiceCredentialStore real-implementation store/retrieve/remove round-trip (optional, self-skipping)
  - REQs: REQ-F-001, REQ-F-002, REQ-F-003, REQ-F-005, REQ-F-006, REQ-NF-001, REQ-NF-004
  - Check: tests/credentials/test_credential_store_worker_threading.cpp contains integration-style tests `TEST(SecretServiceCredentialStoreIntegrationTest, StoreAndRetrieveWithRealService)` that constructs `SecretServiceCredentialStore`, connects `unavailable` signal spy, spins event loop briefly to allow startup enumeration to complete, checks if spy caught `unavailable(reason)` — if yes, calls `GTEST_SKIP() << "Secret Service not reachable..."` (per DESIGN §7.2 self-skip pattern), else proceeds to `store("test-provider-xxx", "test-secret-xxx")`, `retrieve("test-provider-xxx")`, verifies retrieved secret matches, `remove("test-provider-xxx")`, verifies subsequent retrieve resolves to found==false; the suite self-detects absence of keyring daemon via `unavailable` signal (no compile-time `#ifdef` needed) and never fails CI for missing daemon (REQ-NF-004 "never fail CI for environment outside module control"); any test that requires real Secret Service is guarded by this skip pattern.

## Verification Tasks

- [x] T-027: Run clang-format format-check on all credentials module files
  - REQs: (code style)
  - Check: `task format-check` reports zero formatting violations in src/credentials/include/holonight_credentials/*.h, src/credentials/src/*.cpp; `task format` auto-formats all files and project rebuilds without errors.

- [x] T-028: Run clang-tidy with WarningsAsErrors=* on holonight_credentials target
  - REQs: (code quality)
  - Check: `task tidy` runs clang-tidy with `-WarningsAsErrors='*'` on holonight_credentials target, reports zero errors or warnings, build/tidy.log shows "passed" for holonight_credentials (no enum-size, identifier-length, value-param, or other warnings raised).

- [x] T-029: Run full GTest suite via task test
  - REQs: REQ-NF-004, REQ-F-006
  - Check: `task configure-tests && task test` builds and executes all GTest cases including test_credential_store_fake.cpp and test_credential_store_worker_threading.cpp; all cases PASS or SKIP (for unavailable Secret Service); zero files created in ~/.local/share/ or any user filesystem location during test execution (all testing uses in-memory stores per REQ-NF-004).

- [x] T-030: Verify REQ-C-003 module isolation — no provider/application/QML integration changes
  - REQs: REQ-C-003
  - Check: Run `git diff --stat` after all tasks complete and verify there are no modifications under `src/providers/`, `src/application/`, or `qml/`. Root build wiring and project-status documentation are allowed.

## Review Follow-up Tasks

- [ ] T-031: Add explicit cache readiness to `CredentialStore`
  - REQs: REQ-F-004, REQ-C-001
  - Check: add `isReady()` and `ready()`; the real store emits `ready()` only after installing the initial provider snapshot, the fake is immediately ready, and tests prove pre-ready query results are not treated as authoritative.

- [ ] T-032: Add observable per-operation failure results
  - REQs: REQ-F-001, REQ-F-002, REQ-F-003, REQ-F-005, REQ-F-006
  - Check: add `operationFailed(providerId, operation, reason)`; completion signals are emitted only on success; an unavailable request and an injected libsecret error never emit a false success or a false not-found result.

- [ ] T-033: Classify connection failures separately from recoverable errors
  - REQs: REQ-F-005, REQ-F-006
  - Check: only missing/disconnected D-Bus or Secret Service transitions to sticky unavailable; locked collection, prompt rejection, cancellation, and unknown operation errors fail the current request while permitting retry.

- [ ] T-034: Make worker shutdown cancellable and bounded
  - REQs: REQ-NF-001
  - Check: pass an owned `GCancellable` to every blocking libsecret call, cancel it during destruction, use a documented timeout, and verify with a test-injected blocking operation that normal destruction completes within the bound.

- [ ] T-035: Re-run credential tests and quality checks after review follow-ups
  - REQs: REQ-NF-001, REQ-NF-003, REQ-NF-004
  - Check: `task format-check`, credential-focused tests, and `task test` pass; optional live-service tests either pass or skip for an unavailable environment.

---

**Total Tasks**: 35 (30 original implementation tasks complete; 5 review follow-ups pending)

**Section Summary**:
| Section | Task Count |
|---------|-----------|
| Build Infrastructure | 2 |
| Abstract Interface | 1 |
| libsecret Integration | 1 |
| Worker Thread Implementation — Header | 1 |
| Worker Thread Implementation — Initialization | 1 |
| Worker Thread Implementation — Store | 1 |
| Worker Thread Implementation — Retrieve | 1 |
| Worker Thread Implementation — Remove | 1 |
| Worker Thread Implementation — Error Handling | 1 |
| SecretServiceCredentialStore Façade — Header | 1 |
| SecretServiceCredentialStore Façade — Constructor | 1 |
| SecretServiceCredentialStore Façade — Destructor | 1 |
| SecretServiceCredentialStore Façade — store() Dispatcher | 1 |
| SecretServiceCredentialStore Façade — retrieve() Dispatcher | 1 |
| SecretServiceCredentialStore Façade — remove() Dispatcher | 1 |
| SecretServiceCredentialStore Façade — Cache-Read Methods | 1 |
| Test Double | 1 |
| GTest Coverage — Store/Overwrite | 1 |
| GTest Coverage — Retrieve | 1 |
| GTest Coverage — Remove | 1 |
| GTest Coverage — Query | 1 |
| GTest Coverage — Availability | 1 |
| GTest Coverage — Threading (Worker Affinity) | 1 |
| GTest Coverage — Threading (Façade Performance) | 1 |
| GTest Coverage — Real-Implementation Round-Trip | 1 |
| Verification Tasks | 4 |
| Review Follow-up Tasks | 5 |
| **TOTAL** | **35** |
