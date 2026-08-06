# Specification: Secret Service Credential Store (holonight_credentials)

## Purpose

The `holonight_credentials` module provides a threadsafe, asynchronous credential storage abstraction for storing and retrieving provider API keys (e.g., future OpenAI/Anthropic/Google adapters) backed by the desktop Secret Service (via libsecret) or KWallet on Linux systems. The module defines an abstract `CredentialStore` interface with two implementations: a real `SecretServiceCredentialStore` backed by libsecret's D-Bus integration, and a `FakeCredentialStore` test double for deterministic offline testing. This specification defines the minimal viable behavior needed to support future provider integrations.

This cycle **does not** integrate credentials into any provider, application layer, or QML code — that is deferred to future work. This module is infrastructure only.

---

## Functional Requirements

### REQ-F-001: Store Operation — Create or Overwrite Secret

**EARS Template:** Ubiquitous

The system shall provide a `store(providerId: QString, secret: QString)` operation that creates a new secret for a provider ID or overwrites an existing secret for the same provider ID.

**Acceptance Criteria:**
- GTest verifies that calling `store("openai", "sk-123...")` on an empty `FakeCredentialStore` results in the secret being stored.
- GTest verifies that calling `store("openai", "sk-old...")` followed by `store("openai", "sk-new...")` results in the secret being updated to "sk-new...".
- A successful `store` emits `storeCompleted(providerId)`; a failed request emits `operationFailed(providerId, operation, reason)` instead. GTest asserts the success signal is emitted in-process for the fake.
- For `SecretServiceCredentialStore`, a test harness confirms no libsecret call blocks the caller's thread (worker thread receives the operation via queued `QMetaObject::invokeMethod`).

---

### REQ-F-002: Retrieve Operation — Fetch Secret or Resolved Not-Found

**EARS Template:** Ubiquitous

The system shall provide a `retrieve(providerId: QString)` operation that fetches the secret for a provider ID, resolving to one of two outcomes: "found with secret value" or "not found". A "not found" result is a normal, successful outcome, not an error.

**Acceptance Criteria:**
- GTest verifies that calling `retrieve("openai")` on an empty `FakeCredentialStore` emits a `retrieveCompleted(providerId, found=false, secret="")` signal.
- GTest verifies that after `store("openai", "sk-123...")`, calling `retrieve("openai")` emits a `retrieveCompleted("openai", found=true, secret="sk-123...")` signal.
- The fake implementation emits the completion signal within the same call stack (synchronous, no event loop spin required).
- For `SecretServiceCredentialStore`, a test harness confirms the real libsecret D-Bus call happens on the worker thread, not the caller's thread.

---

### REQ-F-003: Remove Operation — Delete Secret or No-Op if Not Found

**EARS Template:** Ubiquitous

The system shall provide a `remove(providerId: QString)` operation that deletes the secret for a provider ID. If the provider ID has no stored secret, the operation succeeds silently (no error).

**Acceptance Criteria:**
- GTest verifies that calling `remove("nonexistent")` on an empty `FakeCredentialStore` emits a `removeCompleted(providerId)` signal without error.
- GTest verifies that after `store("openai", "sk-123...")`, calling `remove("openai")` emits a `removeCompleted("openai")` signal and subsequent `retrieve("openai")` resolves to "not found".
- The fake implementation emits the completion signal within the same call stack (synchronous).
- For `SecretServiceCredentialStore`, a test harness confirms removing a non-existent entry does not raise an exception or emit an error signal.

---

### REQ-F-004: Query Capability — List Configured Providers and Check for Credential Existence

**EARS Template:** Ubiquitous

The system shall provide query operations to discover which provider IDs have credentials stored, WITHOUT ever returning the secret values themselves:
- `listConfiguredProviders() -> QStringList` — returns a list of provider IDs with stored secrets.
- `hasCredential(providerId: QString) -> bool` — returns true if a secret exists for the given provider ID, false otherwise.

**Rationale:** `listConfiguredProviders()` enables UI code to populate a list of configured providers (e.g., in a settings dialog). `hasCredential()` enables efficient per-provider checks without fetching the full list. Both ship this cycle to avoid future integration friction.

**Acceptance Criteria:**
- GTest verifies that on an empty `FakeCredentialStore`, `listConfiguredProviders()` returns an empty list.
- GTest verifies that after `store("openai", "sk-...")` and `store("anthropic", "key-...")`, `listConfiguredProviders()` returns a list containing both `"openai"` and `"anthropic"`.
- GTest verifies that `hasCredential("openai")` returns true after storing a secret, and false after removing it.
- Both operations return results synchronously from an in-memory view and must not block the UI thread.
- `isReady()` is false until the real implementation has finished its initial enumeration; `ready()` is emitted once the view is complete. Before readiness, callers must not interpret an empty/false query result as authoritative.

---

### REQ-F-005: Unavailable State — Sticky Flag and Graceful Degradation

**EARS Template:** Event-driven; State-driven

When the `SecretServiceCredentialStore` cannot establish or has irrecoverably lost its Secret Service connection (e.g., no keyring daemon or D-Bus session), it shall emit `unavailable(reason: QString)` exactly once. While unavailable, subsequent requests shall fail promptly through `operationFailed(...)`; they shall never emit a success completion, crash, hang, or access secrets. Recoverable operation errors, including a locked collection or a rejected unlock prompt, shall fail only that request and shall not make the instance permanently unavailable.

**Acceptance Criteria:**
- Integration test (or mock-based unit test) for `SecretServiceCredentialStore` simulates D-Bus connection failure and verifies:
  - `unavailable(reason)` signal is emitted exactly once.
  - After unavailable is emitted, `store()` and `retrieve()` emit `operationFailed(...)` promptly and do not emit their success completion signals.
  - A simulated locked-collection error emits `operationFailed(...)` but leaves `isAvailable()` true so a later request may retry.
  - Subsequent operations do not re-emit `unavailable()`.
- The `FakeCredentialStore` is always available (no unavailable() signal) — it exists precisely to avoid this async complexity in unit tests.

---

### REQ-F-006: Distinguish Unavailable State from Not-Found Retrieve Result

**EARS Template:** State-driven; Conditional

The unavailable state and all operational failures shall be distinguishable from a normal "not found" retrieve result. A genuine miss emits `retrieveCompleted(providerId, found=false, secret="")`; an unavailable or otherwise failed lookup emits `operationFailed(...)` and no `retrieveCompleted` signal.

**Acceptance Criteria:**
- `SecretServiceCredentialStore` exposes a public read-only property or method `bool isAvailable() const` (or a stored `available` state).
- GTest verifies that after `unavailable()` is emitted, `isAvailable()` returns false.
- GTest verifies that calling `retrieve("openai")` while unavailable emits `operationFailed(...)`, not `retrieveCompleted(..., found=false)`.

---

### REQ-F-007: Data Model — One Secret Per Provider ID

**EARS Template:** Ubiquitous

The credential store associates exactly one opaque secret string per provider ID. A provider ID is a free-form string (e.g., `"openai"`, `"anthropic"`, `"google"`). Storing a new secret for the same provider ID overwrites the previous one. The secret is an opaque QString — no parsing, decoding, or structure assumed by the store itself.

**Acceptance Criteria:**
- GTest verifies that a `FakeCredentialStore` can store secrets for multiple distinct provider IDs (e.g., `"openai"`, `"anthropic"`) without collision.
- GTest verifies that storing a secret for a provider ID does not affect secrets for other provider IDs.
- No credential "kind" or "type" field — this is explicitly out of scope for this cycle.

---

## Non-Functional Requirements

### REQ-NF-001: Threading — No UI Thread Blocking (Real Implementation)

**EARS Template:** Ubiquitous; Unwanted-behavior

The `SecretServiceCredentialStore` shall never block the caller's (UI) thread on any operation. All libsecret D-Bus calls shall execute on an internal worker QThread. Public methods shall return immediately after dispatching the operation to the worker via queued `QMetaObject::invokeMethod`.

**Unwanted-behavior:** The system shall not perform a synchronous D-Bus call on the UI thread.

**Acceptance Criteria:**
- Code review confirms `SecretServiceCredentialStore` contains a member `QThread m_workerThread` and an internal worker QObject that receives operations via queued `QMetaObject::invokeMethod`.
- GTest or integration test verifies that calling `store("openai", "sk-...")` returns to the caller in <1ms (threshold to confirm no D-Bus RPC blocking).
- The pattern matches the precedent `SqliteConversationRepository` worker-thread pattern.
- Destruction requests cancellation of any in-flight libsecret call before joining the worker thread. Shutdown has a documented bounded timeout and must not wait indefinitely on the UI thread.

---

### REQ-NF-002: Threading — Synchronous Test Double (Fake Implementation)

**EARS Template:** Ubiquitous

The `FakeCredentialStore` shall be entirely synchronous and in-process, with no background threads. All operations complete and emit signals within the same call stack. This is by design to enable deterministic GTest assertions without async/timing complexity.

**Acceptance Criteria:**
- GTest verifies that calling `store("openai", "sk-...")` and immediately checking `hasCredential("openai")` returns true (no event loop required between them).
- GTest verifies that `retrieve()` signals are emitted before the call returns to the caller (no `QTimer::singleShot` or queued signals).

---

### REQ-NF-003: Security — No Secret Logging

**EARS Template:** Unwanted-behavior

The system shall not log secret values (the actual credential strings) in any debug output, warning, error message, or trace. This applies to both `SecretServiceCredentialStore` and `FakeCredentialStore`.

**Acceptance Criteria:**
- Code review of `SecretServiceCredentialStore` and `FakeCredentialStore` confirms no `qDebug()`, `qWarning()`, or `qCritical()` statements contain the `secret` parameter or return value.
- Error-path logging (e.g., D-Bus connection failures) may include the provider ID and generic error messages, but not the secret value.

---

### REQ-NF-004: Testability — GTest Suite Written Against Fake, Not Real Secret Service

**EARS Template:** Ubiquitous; State-driven

The GTest test suite for this module shall be written primarily against `FakeCredentialStore` and shall not require a live Secret Service, keyring daemon, or D-Bus session to pass. Any test that requires access to a real Secret Service is explicitly out of scope or marked as optional/skippable (e.g., with `#ifdef INTEGRATION_TEST_REQUIRES_KEYRING`).

**Rationale:** CI/headless test runners typically lack a running keyring daemon and D-Bus session. The fake implementation exists precisely to make the suite deterministic offline.

**Acceptance Criteria:**
- Test file `tests/credentials/test_credential_store_fake.cpp` contains ≥6 GTest cases exercising store, retrieve, remove, listConfiguredProviders, hasCredential, and unavailable-distinction, all against `FakeCredentialStore`, all passing without external daemon dependencies.
- Any test file that uses `SecretServiceCredentialStore` (e.g. `tests/credentials/test_credential_store_worker_threading.cpp`, mirroring `tests/persistence/test_conversation_repository_worker_threading.cpp`) is clearly marked (filename or skip guard) and is optional or CI-skipped.

---

## Constraint Requirements

### REQ-C-001: Class Shape — Abstract Interface + Real + Fake Implementations

**EARS Template:** Ubiquitous

The module shall define three classes:
1. `CredentialStore` — abstract QObject-based interface (pure virtual methods for store, retrieve, remove, listConfiguredProviders, hasCredential, isReady, and signals for successful completions, `operationFailed`, `ready`, and `unavailable`).
2. `SecretServiceCredentialStore : public CredentialStore` — real implementation using libsecret, with internal worker-thread pattern.
3. `FakeCredentialStore : public CredentialStore` — in-memory QMap-backed test double, synchronous, no threading, for GTest.

**Acceptance Criteria:**
- Header file `src/credentials/include/holonight_credentials/credential_store.h` defines `class CredentialStore : public QObject { Q_OBJECT ... }` with pure virtual methods and signals, matching the snake_case file / `holonight_{module}` include-directory convention already used by `src/persistence/include/holonight_persistence/*.h`.
- Header file `src/credentials/include/holonight_credentials/secret_service_credential_store.h` defines `class SecretServiceCredentialStore : public CredentialStore { ... }`, with its worker declared in `src/credentials/include/holonight_credentials/detail/credential_store_worker.h` (mirroring `detail/conversation_repository_worker.h`).
- The fake test double lives at `tests/credentials/fake_credential_store.h` (test-only, mirroring `tests/persistence/fake_conversation_repository.h`), not under `src/credentials/include/`.
- All three classes compile and link without error.

---

### REQ-C-002: Worker-Thread Pattern Matches Persistence Precedent

**EARS Template:** Ubiquitous

The `SecretServiceCredentialStore` worker-thread implementation shall follow the exact pattern established by `SqliteConversationRepository` in `src/persistence/`: a private QObject on a QThread, public methods dispatch via queued `QMetaObject::invokeMethod`, results emitted as signals.

**Acceptance Criteria:**
- Code review confirms `SecretServiceCredentialStore` contains a pattern analogous to `SqliteConversationRepository::m_worker` and `m_workerThread`.
- No raw libsecret calls on the public methods; all D-Bus operations happen on the worker thread.

---

### REQ-C-003: Module Isolation — No Provider/Application/QML Integration This Cycle

**EARS Template:** Unwanted-behavior; Constraint

The module shall be implemented and tested in isolation. No changes to `holonight_providers`, `holonight_application`, or QML code shall be made this cycle to wire credentials into any provider or settings UI.

**Non-goals:**
- No OpenAI, Anthropic, or Google provider integration.
- No settings UI for managing credentials.
- No QML bindings or singleton types.

**Acceptance Criteria:**
- No provider, application-layer, or QML integration changes. Necessary root build wiring and project-status documentation are allowed.
- The module compiles and all tests pass with no changes to `src/providers/`, `src/application/`, or `qml/`.

---

### REQ-C-004: CMake Target and Linking

**EARS Template:** Ubiquitous

The module shall be built as a CMake `STATIC` library target `holonight_credentials`, linked against `libsecret` (via pkg-config) and `Qt6::Core`. The target shall be exported and linked into the `holonight-chat` executable (already done in `apps/chat/CMakeLists.txt`).

**Acceptance Criteria:**
- `src/credentials/CMakeLists.txt` defines `add_library(holonight_credentials STATIC ...)`.
- The target links `Qt6::Core` and `libsecret` (via `pkg_check_modules(libsecret REQUIRED libsecret-1)`).
- `cmake build` succeeds with no linker errors.

---

### REQ-C-005: No Compound "Credential Kind" Key

**EARS Template:** Unwanted-behavior; Constraint

The data model shall store exactly one secret per provider ID. No compound key (e.g., provider ID + credential type/kind/scope) shall be introduced this cycle.

**Acceptance Criteria:**
- The store method signature is `store(providerId: QString, secret: QString)`, not `store(providerId, kind, secret)` or similar.
- Documentation/code comments explicitly state this limitation and defer multi-type credentials to future work.

---

## Assumptions and Open Questions

### Assumption A-001: Signal Emission on Both Real and Fake

Both `SecretServiceCredentialStore` and `FakeCredentialStore` emit completion signals (e.g., `storeCompleted`, `retrieveCompleted`, `removeCompleted`) as their primary completion mechanism. For the fake, these signals are emitted synchronously in-process. For the real implementation, signals are emitted on the UI thread (main thread) after the worker thread completes the operation.

**Justification:** This mirrors the `ConversationRepository` precedent (signals drive the application layer) and decouples the public API from threading complexity — all callers use the same signal-driven interface regardless of implementation.

---

### Assumption A-002: Availability Query Method

Both `hasCredential(providerId)` and `listConfiguredProviders()` are included (not just one). `hasCredential()` is cheap for single-provider checks in settings/UI code, and `listConfiguredProviders()` is cheap for populating a list of all configured providers.

**Justification:** Both are low-overhead read-only queries and serve different UI needs without adding complexity to the store itself.

---

### Assumption A-003: No Return Values on Completion Signals

Completion signals (e.g., `storeCompleted(providerId)`, `removeCompleted(providerId)`) are emitted only after successful completion. Failed requests emit `operationFailed(providerId, operation, reason)` instead. This preserves simple success signals while preventing a caller from reporting that an API key was saved or removed when persistence failed.

**Justification:** Availability describes the service connection; request outcome describes an individual operation. Keeping these observations separate supports recoverable failures without weakening the success contract.

---

## Summary of Acceptance Criteria Grouping

| Requirement | Primary Acceptance | Secondary Acceptance |
|---|---|---|
| REQ-F-001 (Store) | GTest: store/overwrite works on fake | GTest: signal emitted in-call-stack; threading verified for real |
| REQ-F-002 (Retrieve) | GTest: fetch or not-found on fake | GTest: signal emitted; threading verified for real |
| REQ-F-003 (Remove) | GTest: delete/no-op on fake | GTest: signal emitted; no error on non-existent |
| REQ-F-004 (Query) | GTest: list & hasCredential work on fake | GTest: real cache exposes readiness before authoritative reads |
| REQ-F-005 (Unavailable) | Integration/mock test: unavailable state, graceful degradation | GTest: subsequent ops don't re-emit unavailable |
| REQ-F-006 (Distinguish) | GTest: unavailable state separate from not-found | GTest: isAvailable() independent observation |
| REQ-F-007 (Data Model) | GTest: no collisions between provider IDs | GTest: no credential-kind field |
| REQ-NF-001 (Threading Real) | Code review: worker thread pattern; benchmark: <1ms return |  |
| REQ-NF-002 (Threading Fake) | GTest: synchronous signal emission | GTest: no event-loop required between ops |
| REQ-NF-003 (Security) | Code review: no secret in qDebug/logs |  |
| REQ-NF-004 (Testability) | GTest suite on fake, no keyring daemon required | Optional: integration tests marked/skipped |
| REQ-C-001 (Class Shape) | Code review: three classes defined; compile/link success |  |
| REQ-C-002 (Precedent) | Code review: worker-thread pattern matches SqliteConversationRepository |  |
| REQ-C-003 (Isolation) | Git diff: no provider/application/QML integration changes | Root build/docs changes allowed |
| REQ-C-004 (CMake) | CMake build succeeds; no linker errors |  |
| REQ-C-005 (No Compound Key) | Code review: single providerId + secret signature |  |

---

## References

- **Precedent:** `docs/sdd/sqlite-conversation-persistence/DESIGN.md` and `src/persistence/` — worker-thread pattern, signal-driven completion, unavailable state.
- **Project conventions:** `/home/andrii/Projects/pet/holonight/holonight-ai/CLAUDE.md` — C++23/Qt6 style, naming conventions, testing patterns.
- **libsecret:** D-Bus-based credential storage on Linux; via pkg-config `libsecret-1`.
- **Future integration:** wiring into providers and application/QML is deferred post-SDD cycle.
