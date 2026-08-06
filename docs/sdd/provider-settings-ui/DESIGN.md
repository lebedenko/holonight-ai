# Provider Settings UI Design

**Document Version**: 1.0
**Date**: 2026-07-22
**Modules**: `holonight_config` (new: `OllamaProviderConfig`, `ConfigRepository`, `resolveConfigFilePath()`),
`holonight_providers` (changed: `HttpClient`/`HttpRequest` gain headers, `OllamaProvider` gains
setters + a two-callback `refresh()` overload + request-body `options`), `holonight_application`
(new: `ProviderSettingsController`; changed: `ChatViewModel` gains `providerForSettings()` and
`syncAvailableModelsFromProvider()`), `qml/workspace/` (new: `SettingsWindow.qml` and children),
`tests/` (new/extended GTest coverage for all of the above)
**Traces to**: `docs/sdd/provider-settings-ui/SPEC.md` (19 functional, 7 non-functional, 10
constraint requirements)
**Precedent**: `docs/sdd/secret-service-credentials/DESIGN.md` (worker-thread façade shape — not
reused here, see §5.2), `docs/sdd/chat-window-qml/DESIGN.md` (QML singleton factory pattern,
metatype-extraction CMake wiring — reused directly, see §5.1), `docs/sdd/sqlite-conversation-persistence/DESIGN.md`
(`resolveDatabaseFilePath()` — direct precedent for `resolveConfigFilePath()`)
**Status**: Implemented and verified

---

## 0. Ground truth this design was checked against

Two places where the SPEC's own "Implementation Notes" describe an API slightly differently from
what actually ships today were resolved by reading the real source, not the SPEC's paraphrase:

- `holonight_credentials::CredentialStore`'s **actual** signal set (verified against
  `src/credentials/include/holonight_credentials/credential_store.h` and
  `src/credentials/src/detail/credential_store_worker.cpp`) is `storeCompleted(providerId)`,
  `retrieveCompleted(providerId, found, secret)`, `removeCompleted(providerId)`,
  `unavailable(reason)` — **no `operationFailed` signal and no `isReady()`/`ready()` exist**,
  despite `docs/sdd/secret-service-credentials/DESIGN.md` describing an earlier, richer revision
  of this API. `store()`/`remove()` **always** emit their `*Completed` signal, even when the
  underlying libsecret call failed — `CredentialStoreWorker::handleLibsecretError()` unconditionally
  flips `available_` to `false` and emits `unavailable(reason)` for *every* `GError`, with no
  recoverable-vs-fatal classification. §4.4 and §5.4 below design `ProviderSettingsController`
  around this exact, simpler, already-shipped contract.
- `apps/chat/CMakeLists.txt` **already implements** the multi-static-library metatype-combining
  pattern CLAUDE.md's "Future: QML Singletons From Static Libraries" section flags as unsolved —
  `cmake/combine-metatypes.cmake` already runs today, extracting `holonight_application`'s
  metatypes and merging them onto the `holonight-chat` executable. This means adding a second
  `QML_SINGLETON` type to `holonight_application` (§1.3) requires **zero** new CMake plumbing —
  see §6.3.

---

## 1. Components

| Component | File(s) | Role |
|---|---|---|
| `holonight_config::OllamaProviderConfig` | `src/config/include/holonight_config/provider_config.h` | Plain value struct — the four non-secret fields (REQ-F-016). |
| `holonight_config::resolveConfigFilePath()` | `src/config/include/holonight_config/config_path.h` | `$XDG_CONFIG_HOME/holonight-ai/config.json` resolution (REQ-C-002), mirrors `holonight_persistence::resolveDatabaseFilePath()`. |
| `holonight_config::ConfigRepository` | `src/config/include/holonight_config/config_repository.h` | Synchronous, path-injected JSON load/save with full REQ-NF-007 fallback rules (§2.1). |
| `holonight_providers::HttpRequest::headers` | `src/providers/include/holonight_providers/http_client.h` | New field carrying arbitrary extra headers (Authorization) through to `QtNetworkHttpClient`. |
| `holonight_providers::OllamaProvider` (extended) | `src/providers/include/holonight_providers/ollama_provider.h` | Gains `setBaseUrl`/`setAuthToken`/`setTemperature`/`setContextWindow` and a two-callback `refresh()` overload (§4.2). |
| `holonight_application::ChatViewModel` (extended) | `src/application/include/holonight_application/chat_view_model.h` | Gains `providerForSettings()` (share the live `OllamaProvider` with Settings) and `syncAvailableModelsFromProvider()` (REQ-F-012's "chat window updates immediately"). |
| `holonight_application::ProviderSettingsController` | `src/application/include/holonight_application/provider_settings_controller.h` | New `QML_SINGLETON` mediating the Providers page ↔ the shared `OllamaProvider`, a private probe `OllamaProvider`, `CredentialStore`, and `ConfigRepository` (§3). |
| `SettingsWindow.qml` + children | `qml/workspace/` | New QML `Window` nested inside `WorkspaceWindow.qml`'s object tree — deliberately **not** a second `QQuickView`/`QQmlEngine` (§5.1). |

### 1.1 Class relationship

```
                         ┌───────────────────────────┐
                         │        ChatViewModel        │  (QML_SINGLETON, existing)
                         │  - provider_: shared_ptr<OllamaProvider>│
                         │  + providerForSettings() const          │ (new, plain C++, §4.3)
                         │  + syncAvailableModelsFromProvider(pref)│ (new, plain C++, §4.3)
                         └───────────────┬─────────────┘
                                         │ shared_ptr (same instance, same object)
                                         │ + callback (model_sync_callback_)
              ┌──────────────────────────▼──────────────────────────┐
              │              ProviderSettingsController                │  (QML_SINGLETON, new)
              │  - provider_:        shared_ptr<OllamaProvider>  (shared — Save only) │
              │  - probe_provider_:   shared_ptr<OllamaProvider>  (private — Refresh/Test only) │
              │  - credential_store_: CredentialStore*  (owned in production, injected in tests)│
              │  - config_repository_: ConfigRepository (by value)                   │
              │  - model_sync_callback_: std::function<void(optional<ModelId>)>      │
              │  Q_PROPERTY baseUrl, defaultModel, contextWindow, temperature,        │
              │             authToken, hasStoredToken, credentialStoreAvailable,      │
              │             credentialOperationInProgress,                            │
              │             availableModelNames, modelRefreshInProgress/Error,        │
              │             testConnectionInProgress/Status/Message,                  │
              │             ollamaConnectionStatus, saveNotice                        │
              │  Q_INVOKABLE load(), refreshModels(), testConnection(), save(),       │
              │              cancel(), resetToDefaults()                             │
              └───────┬───────────────────────┬───────────────────────┬─────────────┘
                      │                        │                       │
       ┌──────────────▼───────────┐ ┌──────────▼───────────┐ ┌────────▼────────────────┐
       │ holonight_config::         │ │ holonight_credentials:: │ │ holonight_providers::     │
       │ ConfigRepository           │ │ CredentialStore (abstract)│ │ OllamaProvider ×2 instances│
       │  + loadOllamaConfig()      │ │  (real: SecretServiceCredentialStore│ │ (shared_provider_,       │
       │  + saveOllamaConfig(cfg)   │ │   in production; FakeCredentialStore│ │  probe_provider_)        │
       │    -> expected<void,QString>│ │   in tests)              │ └──────────────────────────┘
       └────────────────────────────┘ └──────────────────────────┘
```

---

## 2. Data Flow

### 2.1 `ConfigRepository::loadOllamaConfig()` — REQ-F-015 / REQ-NF-007 fallback table

| Condition | Result | Logged? |
|---|---|---|
| File does not exist | `OllamaProviderConfig{}` (defaults) | No |
| File exists, empty/whitespace-only | `OllamaProviderConfig{}` | No |
| `QFile::open()` fails (permission denied) | `OllamaProviderConfig{}` | `qWarning` with `QFile::errorString()` |
| Content is malformed JSON (`QJsonParseError`) | `OllamaProviderConfig{}` | `qWarning` with `QJsonParseError::errorString()` |
| Parsed JSON is not an object, or `"providers"` missing/not-object, or `"providers"."ollama"` missing/not-object | `OllamaProviderConfig{}` | No — an absent Ollama section is an ordinary first-run state, not a warning |
| `"providers"."ollama"` present, one or more fields missing or wrong JSON type | Present+correctly-typed fields load normally; missing/wrong-typed fields fall back individually to that field's default | No |

The per-field fallback needs no manual presence/type checking: `QJsonValue::toString(default)`,
`toInt(default)`, and `toDouble(default)` already return the supplied default both when the key is
absent *and* when it holds the wrong JSON type — one mechanism covers both REQ-NF-007 cases.

### 2.2 App startup — REQ-F-015

1. `ChatViewModel::create(qmlEngine, jsEngine)` (QML singleton factory, unchanged trigger — first
   QML reference to `ChatViewModel`) now does, **before** constructing `OllamaProvider`:
   ```cpp
   const holonight_config::ConfigRepository configRepository(holonight_config::resolveConfigFilePath());
   const holonight_config::OllamaProviderConfig config = configRepository.loadOllamaConfig();
   auto provider = std::make_shared<OllamaProvider>(std::move(http_client), config.base_url);
   provider->setContextWindow(config.context_window);
   provider->setTemperature(config.temperature);
   ```
2. `config.default_model` (a plain model-name string) is threaded into `ChatViewModel`'s
   constructor as a new `initial_default_model_id` (default `{}`, so both existing test call sites
   compile unchanged) and consulted only in the "no persisted `last_model_id`" branch of
   `adoptConversation()` — REQ-F-015 talks about "initializing the OllamaProvider with ... default
   model," but `OllamaProvider` itself has no model-selection concept (only `sendChat()`'s explicit
   `model` parameter); the actual effect of a configured default model is which entry
   `ChatViewModel::selectedModelId` starts on, so that is where it is applied.
3. The auth token is **not** read here — `ChatViewModel::create()` has no `CredentialStore` and
   must not gain one (REQ-C-004 keeps `CredentialStore` a `ProviderSettingsController`-only
   dependency). `OllamaProvider::auth_token_` starts empty; §2.3 explains how it gets seeded
   moments later, before the window is interactive.
4. `provider_->refresh([this] { onModelsRefreshed(); })` (unchanged call site) now fetches against
   the **saved** base URL from step 1, satisfying REQ-F-015's "constructed with these values before
   the chat window is ready for interaction."

### 2.3 Settings singleton construction — eager, at startup, sharing the main engine

`SettingsWindow.qml` is declared directly (not behind a `Loader`) as a child `Window` inside
`WorkspaceWindow.qml`'s root `Rectangle` (§5.1), so its QML bindings — and therefore the first
reference to the `ProviderSettingsController` singleton — evaluate at the same time
`WorkspaceWindow.qml` itself loads, i.e. at app startup, before the user has clicked the gear icon.
This is a deliberate choice (not merely "on first open") specifically so the auth token becomes
available on the shared `OllamaProvider` before the very first chat send, without requiring the
user to open Settings first:

1. `ProviderSettingsController::create(qmlEngine, jsEngine)`:
   - Resolves the already-constructed `ChatViewModel` singleton via
     `qmlEngine->singletonInstance<ChatViewModel*>("HolonightChat", "ChatViewModel")` — this forces
     `ChatViewModel` to exist first if it does not already (singleton creation order requirement;
     `WorkspaceWindow.qml`'s own top-level bindings already reference `ChatViewModel` directly, so
     in practice it is always created first regardless).
   - Builds `probe_provider_` — a **second**, private `OllamaProvider` over its own fresh
     `QtNetworkHttpClient`, entirely separate from `chatViewModel->providerForSettings()`.
   - Constructs the real `SecretServiceCredentialStore`, parented to the new controller (Qt
     ownership handles cleanup — §5.1).
   - Constructs `ConfigRepository` over `resolveConfigFilePath()`.
   - Constructs `ProviderSettingsController` with all of the above plus the model-sync callback
     (§4.3), then calls `load()` once (§2.4).
2. `load()`'s `credential_store_->retrieve("ollama")` call resolves asynchronously; when
   `retrieveCompleted("ollama", found, secret)` arrives, the controller sets its own `authToken`
   property for display **and**, if `found`, calls `provider_->setAuthToken(secret)` on the
   **shared** provider — this is the actual mechanism by which a previously-saved token reaches the
   live provider at startup (not a per-request `CredentialStore` round trip — see §5.4 for why that
   literal reading of the SPEC's implementation note was rejected).

### 2.4 `load()` — REQ-F-005/006/008/009/010

Called once by the constructor and again by `cancel()`'s sibling reset path is **not** shared —
`cancel()` does not re-invoke `load()` (see §2.7). `load()` itself:

1. `config_repository_.loadOllamaConfig()` → sets `baseUrl`, `defaultModel`, `contextWindow`,
   `temperature` properties and stores the result as `last_saved_config_` (the Cancel baseline).
2. Points `probe_provider_` at the freshly-loaded URL (`probe_provider_->setBaseUrl(baseUrl)`) and
   calls the two-callback `refresh()` overload (§4.2) against it, populating `availableModelNames`
   / `ollamaConnectionStatus` — REQ-F-006's "initially populated ... using the saved URL."
3. `credential_store_->retrieve("ollama")` (async; handled per §2.3 step 2). `hasStoredToken` is
   set from this result too, independent of the in-panel `authToken` property's later live edits.
   `credentialOperationInProgress` remains true until retrieval completes, disabling token editing
   and Save so the late retrieval result cannot overwrite a user edit.
4. `credentialStoreAvailable` starts `true` and is set to `false` permanently the first time
   `CredentialStore::unavailable(reason)` fires (§2.8) — read at construction from
   `credential_store_->isAvailable()` in case the instance was already unavailable before this
   controller subscribed (defensive; in practice `ProviderSettingsController` always connects
   before any operation can complete, since it owns the `SecretServiceCredentialStore` instance).

### 2.5 `refreshModels()` — REQ-F-007

```cpp
void ProviderSettingsController::refreshModels() {
  probe_provider_->setBaseUrl(normalizeBaseUrl(base_url_));       // *in-panel*, possibly-unsaved URL
  probe_provider_->setAuthToken(auth_token_);                     // *in-panel*, possibly-unsaved token
  setModelRefreshInProgress(true);
  probe_provider_->refresh(
      [this] {
        setModelRefreshInProgress(false);
        setModelRefreshError(QString());
        setAvailableModelNames(toNames(probe_provider_->availableModels()));
        setOllamaConnectionStatus(QStringLiteral("connected"));
      },
      [this](const QString& reason) {
        setModelRefreshInProgress(false);
        setModelRefreshError(tr("Failed to fetch models: %1").arg(reason));
        setOllamaConnectionStatus(QStringLiteral("error"));
      });
}
```

Never touches `provider_` (the shared instance) or `config_repository_` — matches REQ-F-007's "the
button remains interactive even if the URL field has been edited but not yet saved" and, by
construction, cannot leak an unsaved edit into live chat traffic.

### 2.6 `testConnection()` — REQ-F-011

Structurally identical to §2.5 but does not touch `availableModelNames` at all (a connectivity
probe, not a model-list refresh) and uses distinct properties (`testConnectionInProgress`,
`testConnectionStatus` ∈ `{"idle","success","error"}`, `testConnectionMessage`). Success is defined
purely by the HTTP round trip completing with a 2xx status and parseable JSON — an empty
`"models": []` array is still success, because `fetchModelList`'s success callback fires regardless
of how many entries the array contains (REQ-F-011's "zero pulled models is a successful connection
test" falls out for free from `OllamaProvider`'s existing parse logic; no special-casing needed).
`ollamaConnectionStatus` (the row's status-dot source, §2.9) is updated the same way §2.5 updates
it, since both are equally valid "last known reachability" evidence for the Ollama row.

### 2.7 `cancel()` — REQ-F-013

```cpp
void ProviderSettingsController::cancel() {
  setBaseUrl(last_saved_config_.base_url);
  setDefaultModel(last_saved_config_.default_model);
  setContextWindow(last_saved_config_.context_window);
  setTemperature(last_saved_config_.temperature);
  setAuthToken(QString());   // REQ-F-013: never re-populated from the stored secret, even though
                              // one may exist — hasStoredToken (unaffected) still reflects it.
  setModelRefreshError(QString());
  setTestConnectionStatus(QStringLiteral("idle"));
}
```

No `ConfigRepository`, `CredentialStore`, or shared-`provider_` call of any kind — because §2.5/2.6
never touched any of those in the first place, there is nothing to revert on that side. This is the
direct payoff of keeping `probe_provider_` fully separate from `provider_` (§5.3): Cancel's
correctness reduces to "restore four in-panel values from a snapshot," not "undo partially-applied
side effects."

### 2.8 `resetToDefaults()` — REQ-F-014

```cpp
void ProviderSettingsController::resetToDefaults() {
  const holonight_config::OllamaProviderConfig factory{};   // struct's own default member initializers
  setBaseUrl(factory.base_url);
  setDefaultModel(factory.default_model);
  setContextWindow(factory.context_window);
  setTemperature(factory.temperature);
  setAuthToken(QString());
  // last_saved_config_ is deliberately NOT touched — REQ-F-014's "Reset then Cancel reverts to
  // last-saved values," not to factory defaults.
}
```

### 2.9 `save()` — REQ-F-012/016/017/018

```cpp
void ProviderSettingsController::save() {
  if (context_window_ < 128 || context_window_ > 1'000'000) {
    setSaveNotice(tr("Context window must be between 128 and 1,000,000."));
    return;
  }
  if (temperature_ < 0.0 || temperature_ > 2.0) {
    setSaveNotice(tr("Temperature must be between 0 and 2."));
    return;
  }

  const holonight_config::OllamaProviderConfig config{
      .base_url = normalizeBaseUrl(base_url_),
      .default_model = default_model_,
      .context_window = context_window_,
      .temperature = temperature_,
  };

  const auto saved = config_repository_.saveOllamaConfig(config);
  if (!saved.has_value()) {
    setSaveNotice(tr("Failed to save settings: %1").arg(saved.error()));
    return;   // REQ-F-016: write failure modifies no other state — provider/token untouched below
  }

  last_saved_config_ = config;

  provider_->setBaseUrl(config.base_url);
  provider_->setContextWindow(config.context_window);
  provider_->setTemperature(config.temperature);

  const std::optional<holonight_domain::ModelId> preferred =
      config.default_model.isEmpty()
          ? std::nullopt
          : std::optional{holonight_domain::ModelId{.provider_id = QStringLiteral("ollama"),
                                                     .model_name = config.default_model}};
  provider_->refresh([this, preferred] { model_sync_callback_(preferred); });

  setSaveNotice(tr("Settings saved."));

  pending_auth_token_ = auth_token_;  // immutable snapshot for this asynchronous Save
  setCredentialOperationInProgress(true);
  if (pending_auth_token_.isEmpty()) {
    if (has_stored_credential_) {
      credential_store_->remove(QStringLiteral("ollama"));
    }
  } else {
    credential_store_->store(QStringLiteral("ollama"), pending_auth_token_);
  }
}
```

Token persistence is deliberately **independent** of, and reported separately from, the
config-file/provider-application steps above — REQ-F-017's "partial success is acceptable and
should not be treated as total failure" is satisfied structurally: by the time `store()`/`remove()`
is even called, `config_repository_.saveOllamaConfig()` has already succeeded and `provider_` has
already been reconfigured. `storeCompleted`/`removeCompleted` handlers (§2.10) only ever add a
*second*, separate notice; they cannot undo the first.

`provider_->refresh(...)`'s completion calls `model_sync_callback_(preferred)`, which in production
is `chatViewModel->syncAvailableModelsFromProvider(preferred)` (§4.3) — this is the concrete
mechanism behind REQ-F-012's "new default model selection is available in the chat window's model
picker dropdown immediately."

`normalizeBaseUrl()` strips at most one trailing `/` (REQ-F-005's "with or without trailing slash"
acceptance criterion) before it is ever stored or applied — both `config.json` and the running
`OllamaProvider` always hold the normalized form.

### 2.10 Token store/remove completion — REQ-F-017/018, and the real `CredentialStore` contract (§0)

```cpp
connect(credential_store_, &CredentialStore::storeCompleted, this, [this](const QString& providerId) {
  if (providerId != QStringLiteral("ollama")) return;
  if (!credential_store_->isAvailable()) {
    // §0: store() always emits storeCompleted even on libsecret failure; isAvailable() flipping
    // false during this call is the only signal this API gives us that it actually failed.
    setSaveNotice(tr("Settings saved, but the token could not be stored: Secret Service unavailable."));
    setCredentialStoreAvailable(false);
    setCredentialOperationInProgress(false);
    return;
  }
  has_stored_credential_ = true;
  provider_->setAuthToken(pending_auth_token_);  // apply the value submitted by this Save, not a later edit
  setCredentialOperationInProgress(false);
  setSaveNotice(tr("Settings saved."));
});

connect(credential_store_, &CredentialStore::removeCompleted, this, [this](const QString& providerId) {
  if (providerId != QStringLiteral("ollama")) return;
  has_stored_credential_ = false;
  provider_->setAuthToken(QString());
  setCredentialOperationInProgress(false);
  if (!credential_store_->isAvailable()) {
    setSaveNotice(tr("Settings saved, but the token could not be removed: Secret Service unavailable."));
    setCredentialStoreAvailable(false);
  }
  // REQ-F-018: the in-panel field is already empty regardless of backend outcome — nothing to do
  // to authToken_ here.
});
```

Only one credential mutation may be outstanding. QML disables Save while
`credentialOperationInProgress` is true. `pending_auth_token_` is the immutable value submitted by
that operation, so editing the token field after submission cannot change what is applied to the
shared provider when the completion signal arrives. Initial retrieval uses the same busy state and
temporarily disables token editing to avoid a late retrieval overwriting user input.

### 2.11 `CredentialStore::unavailable(reason)` — REQ-F-019

```cpp
connect(credential_store_, &CredentialStore::unavailable, this, [this](const QString& reason) {
  setCredentialStoreAvailable(false);
  setSaveNotice(tr("Secret storage unavailable: %1").arg(reason));
});
```

`credentialStoreAvailable` drives the QML token field's `enabled` binding (REQ-F-019). Because the
real `CredentialStore`'s `available_` flag is **sticky and never resets to `true`** within one
running process (verified in `CredentialStoreWorker::handleLibsecretError()`, §0), REQ-F-019's last
bullet — "when the credential store becomes available later ... the field re-enables ... without
requiring app restart" — cannot happen within a single running `holonight-chat` process given the
already-shipped `CredentialStore` contract this cycle does not change. SPEC REQ-F-019 therefore
documents restart as the recovery boundary rather than promising unsupported hot recovery.

---

## 3. QML Component Tree (sketch — Stage 4 detail, not literal code)

```
WorkspaceWindow.qml (existing root Rectangle)
├── RowLayout (existing conversation/chat content — unchanged)
└── SettingsWindow.qml            [new: QML `Window`, declared eagerly, visible: false initially]
      "gear" Button in WorkspaceWindow's header row toggles settingsWindow.visible
      (REQ-F-001) — see §5.1 for why this must stay on the SAME QQmlEngine as WorkspaceWindow.

qml/workspace/SettingsWindow.qml
├── Window { id: root; visible: false; title: "Settings" }
│     RowLayout (fills window)
│     ├── SettingsSidebar.qml            [left column, fixed width]
│     │     ColumnLayout of 8 entries (REQ-F-002/REQ-C-005):
│     │       "General", "Providers" (selected by default, only enabled entry),
│     │       "Models", "Tools & MCP", "Permissions", "Storage", "Appearance", "Advanced"
│     │     Each non-Providers entry: reduced-opacity Text/Button + "Coming soon" label,
│     │     onClicked only updates a local `currentSection` property — no side effects.
│     └── StackLayout / Loader keyed on `currentSection`
│           ├── ProvidersPage.qml        [visible when currentSection === "Providers"]
│           │     RowLayout
│           │     ├── ProviderListPanel.qml   [left: 4 fixed-order rows, REQ-F-003]
│           │     │     ListView, static model: ["Ollama","OpenAI","Anthropic","Google"]
│           │     │     delegate: ProviderListDelegate.qml
│           │     │       - name Text
│           │     │       - status dot: Ollama → color from
│           │     │         ProviderSettingsController.ollamaConnectionStatus
│           │     │         ("unknown"→muted/gray, "connected"→success/green, "error"→error/red);
│           │     │         OpenAI/Anthropic/Google → always gray (REQ-C-006: never queried)
│           │     └── Detail panel (Loader, keyed on selected provider)
│           │           - Ollama selected → OllamaSettingsPanel.qml
│           │           - otherwise       → UnsupportedProviderPanel.qml (REQ-F-004:
│           │             name/icon + "Coming soon: <Provider>" text only, Cancel/Save/Reset
│           │             buttons present but bound to no-ops for these rows)
│           └── ComingSoonPage.qml       [visible for the other 7 sidebar sections, REQ-F-002]
│
qml/workspace/OllamaSettingsPanel.qml   (bound throughout to the ProviderSettingsController singleton)
├── TextField "Server URL"        ↔ ProviderSettingsController.baseUrl               (REQ-F-005)
├── RowLayout
│     ├── ComboBox "Default model"  model: ProviderSettingsController.availableModelNames,
│     │     currentText bound to defaultModel                                       (REQ-F-006)
│     └── Button "Refresh models"   onClicked: ProviderSettingsController.refreshModels()
│           enabled/loading via modelRefreshInProgress                              (REQ-F-007)
├── Text (visible when modelRefreshError.length > 0) — inline refresh error
├── SpinBox "Context window"      ↔ contextWindow, validator range [128, 1000000]    (REQ-F-008)
├── Slider + SpinBox "Temperature" ↔ temperature, range [0, 2]                        (REQ-F-009)
├── ColumnLayout "Authentication (optional)"
│     ├── TextField "Token" (echoMode toggled by an eye-icon Button) ↔ authToken,
│     │     enabled: credentialStoreAvailable && !credentialOperationInProgress       (REQ-F-010)
│     ├── Button "Clear" → ProviderSettingsController.authToken = ""
│     └── Text (visible when !credentialStoreAvailable) "Secret storage unavailable" (REQ-F-019)
├── Button "Test connection" → ProviderSettingsController.testConnection()
│     Text bound to testConnectionMessage, color keyed on testConnectionStatus       (REQ-F-011)
└── RowLayout (footer, shared by both Ollama and Unsupported panels)
      ├── Button "Reset"  → ProviderSettingsController.resetToDefaults()             (REQ-F-014)
      ├── Button "Cancel" → ProviderSettingsController.cancel()                      (REQ-F-013)
      └── Button "Save"   → ProviderSettingsController.save()
            enabled: !ProviderSettingsController.credentialOperationInProgress
            Text (visible when saveNotice.length > 0) — success/failure notice       (REQ-F-012/016)
```

All colors via `HoloniightPalette.<token>` (REQ-NF-001); no new corner-radius logic — existing
`HnButton`/`HnTextField`-equivalent plain `QtQuick.Controls.Basic` styling patterns already used in
`WorkspaceWindow.qml`/`ConversationListPanel.qml` carry over unchanged (REQ-C-009).

---

## 4. Interfaces / APIs

### 4.1 `src/config/include/holonight_config/provider_config.h` (new)

```cpp
#pragma once

#include <QString>

namespace holonight_config {

// Non-secret Ollama configuration persisted to config.json (REQ-F-016, REQ-C-003). No auth token
// field — the token is Secret Service's exclusive responsibility (REQ-C-003). Default member
// initializers double as REQ-F-015's built-in defaults for a missing file/section/field, and as
// REQ-F-014's factory-reset values.
struct OllamaProviderConfig {
  QString base_url = QStringLiteral("http://localhost:11434");
  QString default_model;   // empty ⇒ "no default model saved" (REQ-F-006)
  int context_window = 4096;
  double temperature = 0.7;

  friend bool operator==(const OllamaProviderConfig&, const OllamaProviderConfig&) = default;
};

}  // namespace holonight_config
```

### 4.2 `src/config/include/holonight_config/config_path.h` / `config_repository.h` (new)

```cpp
#pragma once

#include <QString>

namespace holonight_config {

// REQ-C-002: $XDG_CONFIG_HOME/holonight-ai/config.json via QStandardPaths::GenericConfigLocation
// (Qt's own XDG implementation — already falls back to ~/.config when XDG_CONFIG_HOME is unset).
// Creates the holonight-ai/ directory if missing; mkpath() is a no-op, not an error, if it already
// exists — mirrors holonight_persistence::resolveDatabaseFilePath() exactly.
[[nodiscard]] QString resolveConfigFilePath();

}  // namespace holonight_config
```

```cpp
#pragma once

#include "holonight_config/provider_config.h"

#include <QString>

#include <expected>

namespace holonight_config {

// Synchronous, GUI-thread JSON I/O — see DESIGN.md §5.2 for why this deliberately does NOT follow
// the worker-thread precedent SqliteConversationRepository/SecretServiceCredentialStore use.
// Constructed with an explicit file path so GTest can inject a temp-dir path (REQ-NF-003) instead
// of the real resolveConfigFilePath().
class ConfigRepository {
 public:
  explicit ConfigRepository(QString file_path);

  // REQ-F-015/REQ-NF-007: never throws. See DESIGN.md §2.1 for the full fallback table.
  [[nodiscard]] OllamaProviderConfig loadOllamaConfig() const;

  // REQ-F-016: creates the file (and its parent directory) if missing; writes with 2-space
  // indentation (QJsonDocument::Indented). Returns the failure reason (not thrown) on write
  // failure (permission denied, disk full, ...) so callers can surface REQ-F-016's inline error
  // without a try/catch — matches ChatController's existing std::expected convention.
  [[nodiscard]] std::expected<void, QString> saveOllamaConfig(const OllamaProviderConfig& config) const;

 private:
  QString file_path_;
};

}  // namespace holonight_config
```

JSON schema written/read (only the `"ollama"` key is ever populated this cycle — REQ-C-006):

```json
{
  "providers": {
    "ollama": {
      "base_url": "http://localhost:11434",
      "default_model": "llama3.2",
      "context_window": 4096,
      "temperature": 0.7
    }
  }
}
```

The `"providers"` wrapper object (rather than top-level `"ollama"` keys) is namespaced so a future
OpenAI/Anthropic/Google cycle can add sibling keys without a schema migration — REQ-C-006 only
forbids *populating* those keys this cycle, not reserving the shape for them.

### 4.3 `src/providers/include/holonight_providers/http_client.h` (changed)

```diff
 struct HttpRequest {
   HttpMethod method = HttpMethod::Get;
   QString url;
   QByteArray body;
   QString content_type = QStringLiteral("application/json");
+  // Extra headers beyond Content-Type — currently only Authorization: Bearer <token>
+  // (OllamaProvider::setAuthToken(), REQ-F's auth-token support). Empty by default; unrelated to
+  // and independent of content_type, which keeps its own dedicated field for backward
+  // compatibility with every existing call site.
+  QHash<QString, QString> headers;
 };
```

`QtNetworkHttpClient::buildNetworkRequest()` (`src/providers/src/qt_network_http_client.cpp`)
gains, after the existing `setHeader(ContentTypeHeader, ...)` line:

```cpp
for (auto it = request.headers.constBegin(); it != request.headers.constEnd(); ++it) {
  networkRequest.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
}
```

`FakeHttpClient::send()` (`tests/providers/fake_http_client.h`) currently discards its `request`
parameter entirely (`send(const HttpRequest& /*request*/, ...)`); it needs a `last_buffered_request_`
member + `lastBufferedRequest()` accessor added so GTest can assert on the Authorization header sent
by `fetchModelList()` — `sendStreaming()` already records the full `HttpRequest` per call
(`streamingCall(index).request`) and needs no change for the `sendChat()`-header tests.

### 4.4 `src/providers/include/holonight_providers/ollama_provider.h` (changed)

```diff
 class OllamaProvider {
  public:
   explicit OllamaProvider(std::shared_ptr<HttpClient> http_client,
                           QString base_url = QStringLiteral("http://localhost:11434"));

   [[nodiscard]] const std::vector<holonight_domain::ModelId>& availableModels() const;

   // Re-fetches /api/tags and replaces the cached list. on_complete (optional) fires once the
   // fetch settles, success or failure. Unchanged — still swallows the error string, exactly as
   // today, so ChatViewModel's construction-time call site needs zero changes.
   void refresh(const std::function<void()>& on_complete = {});

+  // REQ-F-007/011: distinguishes success from failure, unlike the single-callback overload above.
+  // Both overloads funnel into the same private fetchModelList(on_success, on_error); this one is
+  // new call surface, additive only — no existing caller is touched.
+  void refresh(const std::function<void()>& on_success, const std::function<void(const QString&)>& on_error);
+
+  // REQ-NF-005: reconfigures the base URL used by the NEXT refresh()/sendChat() call. Already
+  // in-flight requests are unaffected — see DESIGN.md §5.4 for why this needs no synchronization.
+  void setBaseUrl(QString base_url);
+  [[nodiscard]] const QString& baseUrl() const;
+
+  // REQ-F's Bearer-token support. Empty (default) ⇒ no Authorization header is sent. Never
+  // logged, never exposed via a getter (REQ-C-003) — write-only by design.
+  void setAuthToken(QString auth_token);
+
+  // REQ-F-008/009: applied to every subsequent sendChat()'s JSON "options" object (num_ctx,
+  // temperature). Defaults (4096 / 0.7) match holonight_config::OllamaProviderConfig{}'s
+  // defaults exactly — DESIGN.md §5.5 calls out why keeping these two independently-declared
+  // default sets numerically identical matters.
+  void setContextWindow(int context_window);
+  void setTemperature(double temperature);

   HttpRequestHandlePtr sendChat(const holonight_domain::ModelId& model,
                                 const std::vector<holonight_domain::Message>& history,
                                 const std::function<void(const holonight_domain::StreamEvent&)>& on_event,
                                 std::chrono::milliseconds idle_timeout = std::chrono::seconds{30});

  private:
-  void fetchModelList(const std::function<void()>& on_complete);
+  void fetchModelList(const std::function<void()>& on_success, const std::function<void(const QString&)>& on_error);
+  [[nodiscard]] QHash<QString, QString> authHeaders() const;

   std::shared_ptr<HttpClient> http_client_;
   QString base_url_;
   std::vector<holonight_domain::ModelId> available_models_;
+  QString auth_token_;
+  int context_window_ = 4096;
+  double temperature_ = 0.7;
 };
```

`src/providers/src/ollama_provider.cpp` changes:

```cpp
void OllamaProvider::setBaseUrl(QString base_url) { base_url_ = std::move(base_url); }
const QString& OllamaProvider::baseUrl() const { return base_url_; }
void OllamaProvider::setAuthToken(QString auth_token) { auth_token_ = std::move(auth_token); }
void OllamaProvider::setContextWindow(int context_window) { context_window_ = context_window; }
void OllamaProvider::setTemperature(double temperature) { temperature_ = temperature; }

QHash<QString, QString> OllamaProvider::authHeaders() const {
  if (auth_token_.isEmpty()) {
    return {};
  }
  return {{QStringLiteral("Authorization"), QStringLiteral("Bearer %1").arg(auth_token_)}};
}

void OllamaProvider::refresh(const std::function<void()>& on_complete) {
  fetchModelList(on_complete, [on_complete](const QString& /*reason*/) {
    if (on_complete) on_complete();
  });
}

void OllamaProvider::refresh(const std::function<void()>& on_success,
                             const std::function<void(const QString&)>& on_error) {
  fetchModelList(on_success, on_error);
}

void OllamaProvider::fetchModelList(const std::function<void()>& on_success,
                                    const std::function<void(const QString&)>& on_error) {
  const HttpRequest request{.method = HttpMethod::Get,
                            .url = base_url_ + QStringLiteral("/api/tags"),
                            .headers = authHeaders()};
  http_client_->send(
      request,
      [this, on_success](const QByteArray& body) {
        /* unchanged parse logic */
        if (on_success) on_success();
      },
      [on_error](const QString& error) {
        if (on_error) on_error(error);
      });
}
```

`sendChat()`'s request body gains an `options` object and the `headers` field:

```cpp
QJsonObject options;
options[QStringLiteral("temperature")] = temperature_;
options[QStringLiteral("num_ctx")] = context_window_;
body[QStringLiteral("options")] = options;
...
const HttpRequest request{.method = HttpMethod::Post,
                          .url = base_url_ + QStringLiteral("/api/chat"),
                          .body = QJsonDocument(body).toJson(QJsonDocument::Compact),
                          .content_type = QStringLiteral("application/json"),
                          .headers = authHeaders()};
```

### 4.5 `src/application/include/holonight_application/chat_view_model.h` (changed)

```diff
+  // Custom, DI-friendly constructor gains one new trailing parameter (default {} keeps every
+  // existing call site — production and GTest alike — compiling unchanged):
   explicit ChatViewModel(std::shared_ptr<holonight_providers::OllamaProvider> provider,
                          std::unique_ptr<holonight_persistence::ConversationRepository> repository,
+                         QString initial_default_model_id = {},
                          QObject* parent = nullptr);

+  // Plain C++ accessor for ProviderSettingsController (REQ-F-012/REQ-NF-005) — not a Q_PROPERTY;
+  // only C++ singleton-to-singleton wiring needs the shared_ptr itself, mirroring conversation()'s
+  // existing "plain C++ accessor" precedent (see chat-window-qml/DESIGN.md §6.6).
+  [[nodiscard]] std::shared_ptr<holonight_providers::OllamaProvider> providerForSettings() const;
+
+  // Re-reads provider_->availableModels() into the QML-facing cache and re-validates
+  // selected_model_id_ — the guts of the constructor's own onModelsRefreshed() callback, now
+  // reusable. `preferredModelId`, when present in the refreshed list, wins over the "keep current
+  // selection if still valid" fallback — this is how Settings' own "Default model" choice becomes
+  // the chat window's active selection after Save (REQ-F-012).
+  void syncAvailableModelsFromProvider(
+      const std::optional<holonight_domain::ModelId>& preferredModelId = std::nullopt);
```

`chat_view_model.cpp`:

```cpp
ChatViewModel* ChatViewModel::create(QQmlEngine* qml_engine, QJSEngine* js_engine) {
  Q_UNUSED(qml_engine)
  Q_UNUSED(js_engine)
  auto http_client = std::make_shared<QtNetworkHttpClient>();
  const holonight_config::ConfigRepository configRepository(holonight_config::resolveConfigFilePath());
  const holonight_config::OllamaProviderConfig config = configRepository.loadOllamaConfig();
  auto provider = std::make_shared<OllamaProvider>(std::move(http_client), config.base_url);
  provider->setContextWindow(config.context_window);
  provider->setTemperature(config.temperature);
  auto repository = std::make_unique<holonight_persistence::SqliteConversationRepository>(
      holonight_persistence::resolveDatabaseFilePath());
  return new ChatViewModel(std::move(provider), std::move(repository), config.default_model);
}

ChatViewModel::ChatViewModel(std::shared_ptr<OllamaProvider> provider,
                             std::unique_ptr<holonight_persistence::ConversationRepository> repository,
                             QString initial_default_model_id, QObject* parent)
    : QObject(parent), provider_(std::move(provider)), repository_(std::move(repository)),
      initial_default_model_id_(std::move(initial_default_model_id)), /* rest unchanged */ {
  provider_->refresh([this] { syncAvailableModelsFromProvider(); });   // was onModelsRefreshed()
  /* ... unchanged repository connect()s ... */
}

std::shared_ptr<OllamaProvider> ChatViewModel::providerForSettings() const { return provider_; }

void ChatViewModel::syncAvailableModelsFromProvider(const std::optional<ModelId>& preferredModelId) {
  const auto& models = provider_->availableModels();
  QVariantList modelsList;
  modelsList.reserve(static_cast<int>(models.size()));
  for (const ModelId& model : models) modelsList.append(modelIdToVariant(model));
  available_models_ = modelsList;
  emit availableModelsChanged();

  if (!models.empty()) {
    ModelId nextSelection = selected_model_id_;
    if (preferredModelId.has_value() && std::ranges::find(models, *preferredModelId) != models.end()) {
      nextSelection = *preferredModelId;
    } else if (std::ranges::find(models, selected_model_id_) == models.end()) {
      nextSelection = models.front();
    }
    if (nextSelection != selected_model_id_) {
      selected_model_id_ = nextSelection;
      emit selectedModelIdChanged();
    }
    setErrorMessage(QString());
  } else {
    setErrorMessage(QStringLiteral("No models available — check that Ollama is running and has at least one model pulled"));
  }
  refreshComputedProperties();
}
```

`onModelsRefreshed()` and its declaration are deleted (folded into `syncAvailableModelsFromProvider`);
`adoptConversation()`'s "no `last_model_id`" branch changes from unconditionally taking
`models.front()` to preferring `initial_default_model_id_` when it names a model present in
`provider_->availableModels()`.

### 4.6 `src/application/include/holonight_application/provider_settings_controller.h` (new)

```cpp
#pragma once

#include "holonight_providers/ollama_provider.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <holonight_config/config_repository.h>
#include <holonight_credentials/credential_store.h>
#include <holonight_domain/holonight_domain.h>
#include <memory>
#include <optional>

class QQmlEngine;
class QJSEngine;

namespace holonight_application {

// QML-facing settings bridge (REQ-F-004..REQ-F-019). Mediates between the Providers page and
// three backends: the SAME shared OllamaProvider ChatViewModel uses for real chat traffic, a
// private disposable probe OllamaProvider used only for in-panel Refresh/Test Connection (never
// the shared one — DESIGN.md §5.3), CredentialStore, and ConfigRepository. QML_SINGLETON, exactly
// like ChatViewModel, and safe to share instances with it ONLY because SettingsWindow.qml loads on
// the SAME QQmlEngine as WorkspaceWindow.qml — see DESIGN.md §5.1.
class ProviderSettingsController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

  Q_PROPERTY(QString baseUrl READ baseUrl WRITE setBaseUrl NOTIFY baseUrlChanged)
  Q_PROPERTY(QString defaultModel READ defaultModel WRITE setDefaultModel NOTIFY defaultModelChanged)
  Q_PROPERTY(int contextWindow READ contextWindow WRITE setContextWindow NOTIFY contextWindowChanged)
  Q_PROPERTY(double temperature READ temperature WRITE setTemperature NOTIFY temperatureChanged)
  Q_PROPERTY(QString authToken READ authToken WRITE setAuthToken NOTIFY authTokenChanged)
  Q_PROPERTY(bool hasStoredToken READ hasStoredToken NOTIFY hasStoredTokenChanged)
  Q_PROPERTY(bool credentialStoreAvailable READ credentialStoreAvailable NOTIFY credentialStoreAvailableChanged)
  Q_PROPERTY(bool credentialOperationInProgress READ credentialOperationInProgress
                 NOTIFY credentialOperationInProgressChanged)
  Q_PROPERTY(QStringList availableModelNames READ availableModelNames NOTIFY availableModelNamesChanged)
  Q_PROPERTY(bool modelRefreshInProgress READ modelRefreshInProgress NOTIFY modelRefreshInProgressChanged)
  Q_PROPERTY(QString modelRefreshError READ modelRefreshError NOTIFY modelRefreshErrorChanged)
  Q_PROPERTY(bool testConnectionInProgress READ testConnectionInProgress NOTIFY testConnectionInProgressChanged)
  Q_PROPERTY(QString testConnectionStatus READ testConnectionStatus NOTIFY testConnectionStatusChanged)
  Q_PROPERTY(QString testConnectionMessage READ testConnectionMessage NOTIFY testConnectionMessageChanged)
  Q_PROPERTY(QString ollamaConnectionStatus READ ollamaConnectionStatus NOTIFY ollamaConnectionStatusChanged)
  Q_PROPERTY(QString saveNotice READ saveNotice NOTIFY saveNoticeChanged)

 public:
  using ModelSyncCallback = std::function<void(const std::optional<holonight_domain::ModelId>&)>;

  static ProviderSettingsController* create(QQmlEngine* qml_engine, QJSEngine* js_engine);

  // DI constructor — production create() and GTest fixtures both funnel through this (mirrors
  // ChatViewModel's precedent). Both OllamaProvider arguments are already-constructed instances —
  // the constructor performs no HttpClient/OllamaProvider construction itself, keeping every
  // dependency injectable (a FakeHttpClient-backed OllamaProvider in tests, a real
  // QtNetworkHttpClient-backed one in production).
  explicit ProviderSettingsController(std::shared_ptr<holonight_providers::OllamaProvider> shared_provider,
                                      std::shared_ptr<holonight_providers::OllamaProvider> probe_provider,
                                      holonight_credentials::CredentialStore* credential_store,
                                      holonight_config::ConfigRepository config_repository,
                                      ModelSyncCallback model_sync_callback, QObject* parent = nullptr);

  [[nodiscard]] QString baseUrl() const;
  void setBaseUrl(QString url);
  [[nodiscard]] QString defaultModel() const;
  void setDefaultModel(QString model);
  [[nodiscard]] int contextWindow() const;
  void setContextWindow(int value);
  [[nodiscard]] double temperature() const;
  void setTemperature(double value);
  [[nodiscard]] QString authToken() const;
  void setAuthToken(QString token);
  [[nodiscard]] bool hasStoredToken() const;
  [[nodiscard]] bool credentialStoreAvailable() const;
  [[nodiscard]] bool credentialOperationInProgress() const;
  [[nodiscard]] QStringList availableModelNames() const;
  [[nodiscard]] bool modelRefreshInProgress() const;
  [[nodiscard]] QString modelRefreshError() const;
  [[nodiscard]] bool testConnectionInProgress() const;
  [[nodiscard]] QString testConnectionStatus() const;
  [[nodiscard]] QString testConnectionMessage() const;
  [[nodiscard]] QString ollamaConnectionStatus() const;
  [[nodiscard]] QString saveNotice() const;

  Q_INVOKABLE void load();               // REQ-F-005/006/008/009/010 — called once at construction
  Q_INVOKABLE void refreshModels();      // REQ-F-007 — probe_provider_ only
  Q_INVOKABLE void testConnection();     // REQ-F-011 — probe_provider_ only
  Q_INVOKABLE void save();               // REQ-F-012/016/017/018
  Q_INVOKABLE void cancel();             // REQ-F-013
  Q_INVOKABLE void resetToDefaults();    // REQ-F-014

 Q_SIGNALS:
  void baseUrlChanged();
  void defaultModelChanged();
  void contextWindowChanged();
  void temperatureChanged();
  void authTokenChanged();
  void hasStoredTokenChanged();
  void credentialStoreAvailableChanged();
  void credentialOperationInProgressChanged();
  void availableModelNamesChanged();
  void modelRefreshInProgressChanged();
  void modelRefreshErrorChanged();
  void testConnectionInProgressChanged();
  void testConnectionStatusChanged();
  void testConnectionMessageChanged();
  void ollamaConnectionStatusChanged();
  void saveNoticeChanged();

 private:
  void onTokenRetrieved(const QString& providerId, bool found, const QString& secret);
  void onTokenStored(const QString& providerId);
  void onTokenRemoved(const QString& providerId);
  void onCredentialStoreUnavailable(const QString& reason);

  std::shared_ptr<holonight_providers::OllamaProvider> provider_;         // shared — Save only
  std::shared_ptr<holonight_providers::OllamaProvider> probe_provider_;   // private — Refresh/Test only
  holonight_credentials::CredentialStore* credential_store_;              // not owned by this class
  holonight_config::ConfigRepository config_repository_;
  ModelSyncCallback model_sync_callback_;

  holonight_config::OllamaProviderConfig last_saved_config_;   // Cancel baseline (REQ-F-013)
  QString base_url_;
  QString default_model_;
  int context_window_ = 4096;
  double temperature_ = 0.7;
  QString auth_token_;          // in-panel-only value; never logged (REQ-C-003)
  QString pending_auth_token_;  // immutable snapshot of the single outstanding Save
  bool has_stored_credential_ = false;
  bool credential_store_available_ = true;
  bool credential_operation_in_progress_ = true;  // initial credential retrieval
  QStringList available_model_names_;
  bool model_refresh_in_progress_ = false;
  QString model_refresh_error_;
  bool test_connection_in_progress_ = false;
  QString test_connection_status_ = QStringLiteral("idle");
  QString test_connection_message_;
  QString ollama_connection_status_ = QStringLiteral("unknown");
  QString save_notice_;
};

}  // namespace holonight_application
```

`create()`:

```cpp
ProviderSettingsController* ProviderSettingsController::create(QQmlEngine* qml_engine, QJSEngine* js_engine) {
  Q_UNUSED(js_engine)
  auto* chatViewModel = qml_engine->singletonInstance<ChatViewModel*>("HolonightChat", "ChatViewModel");
  Q_ASSERT(chatViewModel != nullptr);

  auto probeHttpClient = std::make_shared<holonight_providers::QtNetworkHttpClient>();
  auto probeProvider = std::make_shared<holonight_providers::OllamaProvider>(std::move(probeHttpClient));

  auto* credentialStore = new holonight_credentials::SecretServiceCredentialStore();
  holonight_config::ConfigRepository configRepository(holonight_config::resolveConfigFilePath());

  auto* controller = new ProviderSettingsController(
      chatViewModel->providerForSettings(), std::move(probeProvider), credentialStore,
      std::move(configRepository),
      [chatViewModel](const std::optional<holonight_domain::ModelId>& preferred) {
        chatViewModel->syncAvailableModelsFromProvider(preferred);
      });
  credentialStore->setParent(controller);   // Qt-owned cleanup on engine teardown — see §5.1
  return controller;
}
```

---

## 5. Key Decisions With Rationale

### 5.1 `SettingsWindow.qml` is a nested QML `Window`, sharing `WorkspaceWindow.qml`'s single `QQmlEngine` — not a second `QQuickView`

SPEC's REQ-F-001 offers "either a second `QQuickView` or a QML `Window` item" as equally acceptable.
They are **not** equally acceptable once REQ-F-012 (live reconfiguration of the *running*
`OllamaProvider`) is factored in: `ChatViewModel` is a `QML_SINGLETON`, and Qt scopes
`QML_SINGLETON` instances **per `QQmlEngine`**, not globally per process. A second `QQuickView`
constructs its own, independent `QQmlEngine`; the very first QML reference to `ChatViewModel` inside
that second engine would silently construct a **second, unrelated** `ChatViewModel` (and therefore
a second, unrelated `OllamaProvider`) — Settings would then be editing a provider the real chat
window never uses, making REQ-F-012 unimplementable by construction. Declaring `SettingsWindow.qml`
as a plain QML `Window` item nested inside `WorkspaceWindow.qml`'s existing object tree keeps both
under the one `QQmlEngine` `ChatApplication` already owns via its single `QQuickView`
(`view_->engine()`) — `ChatViewModel` and the new `ProviderSettingsController` singleton are then
guaranteed to be the same two objects everywhere, with zero bridging code.

This also happens to satisfy REQ-C-010 more strongly than its own literal acceptance-criterion
wording ("stored as a member of `ChatApplication` ... `std::unique_ptr`, `QPointer`") anticipated:
the nested `Window` is a normal child object in the QML tree Qt's own parent-child `QObject`
ownership already tears down when `ChatApplication`'s single `QQuickView` (and its engine) is
destroyed — no explicit pointer, no manual cleanup code, and no possibility of a dangling reference
survives past app quit. `ChatApplication.cpp` itself needs **no changes** under this design; the
existing `QQuickView::closing → ChatViewModel::stop()` hook is unaffected, and no analogous hook is
needed for Settings since there is no second window resource to tear down. This is a deliberate,
justified deviation from REQ-F-001/REQ-C-010's literal implementation guess, in favor of a
mechanism that fully satisfies their actual intent (single instance; clean, dangling-pointer-free
teardown; independent open/close from the main window) with less code and one fewer failure mode.

`SettingsWindow.qml` is declared **eagerly** (not behind a `Loader`), so its bindings — and
`ProviderSettingsController`'s first construction — happen at `WorkspaceWindow.qml` load time, i.e.
app startup, which is what lets §2.3's startup token-seeding reach the shared `OllamaProvider`
before the first chat message, without requiring the user to have opened Settings first.

### 5.2 `ConfigRepository` is synchronous, GUI-thread I/O — no worker thread, unlike `SqliteConversationRepository`/`SecretServiceCredentialStore`

Both existing worker-thread façades exist because their underlying operation is either genuinely
slow/blocking at scale (SQLite queries against a growing conversation history) or crosses a D-Bus
service boundary with unpredictable latency (libsecret). Reading or writing a well-under-1KB JSON
file on local disk is neither: `QFile`/`QJsonDocument` operations on a file this size complete in
low single-digit milliseconds on any local filesystem, and REQ-NF-006 ("rendering does not block the
main thread") only explicitly calls out the *initial model refresh* and *credential store queries*
as needing to be asynchronous — config I/O is conspicuously absent from that list. Adding a
`QThread` + worker + queued-signal round trip here would be pure ceremony for an operation that
happens at most twice per Settings session (`load()` once, `save()` on click) and never on a hot
path. `ConfigRepository` is therefore a plain, synchronous, easily-unit-tested class — closer in
shape to `holonight_persistence::MigrationRunner` (also synchronous, also GTest-tested directly)
than to `SqliteConversationRepository`.

### 5.3 `ProviderSettingsController` owns a second, private, disposable `OllamaProvider` for in-panel Refresh/Test Connection — never the shared instance

REQ-F-007/011 both require Refresh-models and Test-connection to operate against the **currently
typed, possibly-unsaved** URL/token; REQ-F-013 requires Cancel to leave the running provider
completely untouched. Reusing the shared `OllamaProvider` for these in-panel probes — even
temporarily mutating its `base_url_`/`auth_token_` via `setBaseUrl()`/`setAuthToken()` while the
panel is open, then "reverting" on Cancel — would create a real window where the live chat provider
points at a URL the user has not committed to, and would require Cancel to reconstruct and reapply
whatever the pre-edit state was (itself only correct if no Save happened concurrently in another
code path). A second, throwaway `OllamaProvider` sidesteps the entire problem: §2.5–§2.7 show that
Cancel's implementation needs no interaction with the shared provider at all, because the shared
provider was never touched by anything except a successful `save()`. The cost is one extra
`QtNetworkHttpClient`/`OllamaProvider` pair per `ProviderSettingsController` instance — negligible,
since there is exactly one such controller for the app's lifetime (§5.1).

### 5.4 The auth token is cached in-memory on `OllamaProvider` (`setAuthToken()`), pushed by `ProviderSettingsController` — not fetched from `CredentialStore` per HTTP request

SPEC's own "Implementation Notes" §1 suggests "the token is retrieved from `CredentialStore` on
each request." Read literally, this is not implementable without changing `OllamaProvider`'s
fundamental shape: `CredentialStore::retrieve()` is asynchronous (a signal-based round trip,
potentially over D-Bus), while `OllamaProvider::fetchModelList()`/`sendChat()` build and issue their
`HttpRequest` synchronously, inline, on the calling thread — making every chat/refresh call first
`await` a `CredentialStore` round trip would turn every request into a two-hop async operation and
would require `holonight_providers` to depend on `holonight_credentials` (a dependency direction
nothing else in the module graph needs or wants; `holonight_providers` currently depends on nothing
but `holonight_domain` and Qt). Instead, `OllamaProvider::auth_token_` is a plain, in-memory
`QString` set only by explicit `setAuthToken()` calls from `ProviderSettingsController` — at startup
once `CredentialStore::retrieveCompleted` resolves (§2.3), and again after a successful
`store()`/`remove()` (§2.10). The token changes only when the user explicitly edits it in Settings;
caching it for the (many) chat requests between those edits, rather than re-fetching it from Secret
Service on every single one, is the correct trade-off, not a shortcut.

### 5.5 `ProviderSettingsController` lives in `holonight_application`, not a new module — sidesteps the QML-singleton-in-static-library problem entirely

CLAUDE.md's "Future: QML Singletons From Static Libraries" section describes a still-theoretical
problem — but `apps/chat/CMakeLists.txt` already solves it today for `holonight_application`'s
existing `QML_SINGLETON` types (`ChatViewModel`, `MessageListModel`) via `combine-metatypes.cmake`
(§0, §6.3). Placing the new `ProviderSettingsController` `QML_SINGLETON` inside `holonight_application`
— rather than the SPEC's alternative suggestion of registering it "directly on `apps/chat`" or in a
brand-new module — means it is automatically picked up by the *already-running*
`qt6_extract_metatypes(holonight_application ...)` call with **zero** new CMake code. The
alternative (a new `holonight_settings_ui` static library with its own `QML_SINGLETON`) would
require duplicating the extraction+combine wiring `apps/chat/CMakeLists.txt` already has for
`holonight_application`, or extending `combine-metatypes.cmake`'s `INPUT_FILES` list to include a
second library's output — both strictly more CMake surface for identical runtime behavior.

`holonight_application` already depends on `holonight_providers` and gains `holonight_config` +
`holonight_credentials` as two new `PUBLIC` link dependencies (§6.2) — a natural extension of its
existing role as the layer that mediates between QML and every backend module, exactly as
`ChatViewModel` already mediates `holonight_providers` + `holonight_persistence`.

### 5.6 `setBaseUrl()`/`setAuthToken()`/etc. are plain, unsynchronized setters — verified safe against in-flight requests

REQ-NF-005 requires reconfiguration to not affect in-flight requests, and asks whether this needs
extra synchronization. It does not, for two independent reasons:

1. **No cross-thread access.** `OllamaProvider`, `ChatViewModel`, and `ProviderSettingsController`
   all live on the Qt GUI/main thread; the only other thread that ever exists here is
   `SecretServiceCredentialStore`'s own worker thread, which never touches `OllamaProvider` — it
   only emits queued signals `ProviderSettingsController` receives back on the GUI thread. There is
   no data race by Qt's own signal/slot threading contract.
2. **No aliasing between a live request and `base_url_`.** `fetchModelList()`/`sendChat()` compute
   `base_url_ + "/api/tags"` (or `/api/chat`) once, by value, into the `HttpRequest.url` field
   passed to `http_client_->send()`/`sendStreaming()`. `QtNetworkHttpClient::issueRequest()`
   immediately turns that into a `QNetworkRequest` and hands it to `QNetworkAccessManager`, which
   owns its own copy of the target URL for the lifetime of the `QNetworkReply`. A later
   `setBaseUrl()` call mutates only the `OllamaProvider` instance's own `base_url_` member — it has
   no path back to an already-issued `QNetworkReply`. This is exactly REQ-NF-005's "reconfiguration
   applies only to new requests," confirmed by inspection rather than by adding a mutex that would
   protect against a race that cannot occur.

---

## 6. `CMakeLists.txt` Changes

### 6.1 Root `CMakeLists.txt`

```diff
 add_subdirectory(src/domain)
+add_subdirectory(src/config)
 add_subdirectory(src/application)
 add_subdirectory(src/providers)
 add_subdirectory(src/persistence)
 add_subdirectory(src/credentials)
 add_subdirectory(src/platform)
 add_subdirectory(apps/chat)
```

`src/config` must be added before `src/application` — `holonight_application`'s
`target_link_libraries(... PUBLIC holonight_config ...)` (§6.2) requires the `holonight_config`
target to already exist when that call runs.

### 6.2 `src/config/CMakeLists.txt` (new)

```cmake
add_library(holonight_config STATIC
    include/holonight_config/provider_config.h
    include/holonight_config/config_path.h
    include/holonight_config/config_repository.h
    src/config_path.cpp
    src/config_repository.cpp
)

target_include_directories(holonight_config PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(holonight_config PUBLIC
    Qt6::Core
)

target_compile_features(holonight_config PUBLIC cxx_std_23)
```

No `Q_OBJECT` type anywhere in this module (plain structs/free functions) — no `AUTOMOC` concerns,
no metatype extraction, no QML registration.

### 6.3 `src/application/CMakeLists.txt`

```diff
 add_library(holonight_application STATIC
     include/holonight_application/chat_controller.h
     include/holonight_application/chat_view_model.h
     include/holonight_application/message_list_model.h
     include/holonight_application/conversation_list_model.h
+    include/holonight_application/provider_settings_controller.h
     src/chat_controller.cpp
     src/chat_view_model.cpp
     src/message_list_model.cpp
     src/conversation_list_model.cpp
+    src/provider_settings_controller.cpp
 )
 ...
 target_link_libraries(holonight_application PUBLIC
     holonight_domain
     holonight_providers
     holonight_persistence
+    holonight_config
+    holonight_credentials
     Qt6::Core
     Qt6::Qml
 )
```

`apps/chat/CMakeLists.txt` needs **no changes at all** — its existing
`qt6_extract_metatypes(holonight_application OUTPUT_FILES ...)` /
`combine-metatypes.cmake` / `_qt_internal_qml_type_registration(holonight-chat)` sequence (§0)
already picks up every `QML_ELEMENT`/`QML_SINGLETON` type declared anywhere inside
`holonight_application`, including the new `ProviderSettingsController` — this is the direct payoff
of §5.5's placement decision. `holonight-chat` already links `holonight_credentials` directly too
(pre-existing), so `SecretServiceCredentialStore`'s symbols are already reachable from the
executable; `provider_settings_controller.cpp` reaches it transitively through
`holonight_application`'s new `PUBLIC` link to `holonight_credentials`.

### 6.4 `src/providers/CMakeLists.txt` — no target changes

`ollama_provider.cpp`/`qt_network_http_client.cpp` are modified files, not new ones — the existing
`add_library(holonight_providers STATIC ...)` source list is unchanged.

### 6.5 `qml/workspace/*.qml` — no `CMakeLists.txt` changes

`apps/chat/CMakeLists.txt`'s `file(GLOB_RECURSE HOLONIGHT_CHAT_QML_FILES ... CONFIGURE_DEPENDS
"${PROJECT_SOURCE_DIR}/qml/*.qml")` already picks up any new `.qml` file under `qml/workspace/` at
the next CMake reconfigure (`CONFIGURE_DEPENDS` triggers that automatically) — `SettingsWindow.qml`
and its children need no explicit listing anywhere.

### 6.6 `tests/CMakeLists.txt`

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
+  application/test_provider_settings_controller.cpp
   persistence/test_database_path.cpp
   persistence/test_migration_runner.cpp
   persistence/test_conversation_record.cpp
   persistence/test_conversation_repository_worker_threading.cpp
   persistence/test_conversation_repository_sqlite.cpp
   credentials/test_credential_store_fake.cpp
   credentials/test_credential_store_worker_threading.cpp
+  config/test_config_path.cpp
+  config/test_config_repository.cpp
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
   holonight_credentials
+  holonight_config
 )
```

`tests/providers/fake_http_client.h` needs the `lastBufferedRequest()` addition from §4.3 (header
change only, no new file, no new `CMakeLists.txt` entry — mirrors how `fake_credential_store.h` is
never itself listed).

---

## 7. GTest Plan

### 7.1 `config/test_config_path.cpp` (new)

- `ResolveConfigFilePathCreatesHolonightAiDirectory` — call `resolveConfigFilePath()`, assert the
  parent directory exists afterward (mirrors `test_database_path.cpp`'s own assertion shape).
- `ResolveConfigFilePathEndsWithConfigJson` — assert the returned path's filename is exactly
  `config.json`, parent directory basename is `holonight-ai`.

### 7.2 `config/test_config_repository.cpp` (new)

All cases construct `ConfigRepository` over a `QTemporaryDir`-scoped path (REQ-NF-003):

- `LoadMissingFileReturnsDefaults`
- `LoadEmptyFileReturnsDefaults`
- `LoadValidJsonRoundTripsAllFourFields`
- `LoadMalformedJsonLogsWarningAndReturnsDefaults` (assert via `QTest::ignoreMessage` or a captured
  `qInstallMessageHandler`, matching how other suites in this repo assert on `qWarning` output — if
  no existing precedent captures `qWarning`, this test only needs to assert the *defaults* are
  returned; capturing the exact log text is a nice-to-have, not required)
- `LoadMissingProvidersSectionReturnsDefaultsSilently`
- `LoadPartialOllamaObjectFillsOnlyMissingFieldsWithDefaults` — e.g. JSON with only `"base_url"`
  present, asserts `default_model`/`context_window`/`temperature` are all struct defaults while
  `base_url` is the JSON's value
- `LoadWrongTypedFieldFallsBackToDefaultForThatFieldOnly` — e.g. `"context_window": "not a number"`
- `SaveCreatesFileAndParentDirectoryIfMissing`
- `SaveThenLoadRoundTripsExactly`
- `SaveWritesTwoSpaceIndentedJson` (asserts on raw file bytes, not just parsed content — REQ-F-016's
  "human-readable")
- `SaveNeverWritesAnAuthTokenField` (REQ-C-003 audit — asserts the raw written bytes contain no
  `"token"`/`"auth"`/`"secret"`-named key, since `OllamaProviderConfig` structurally cannot carry one
  anyway, but this test guards against a future field being added carelessly)
- `SaveToUnwritableDirectoryReturnsErrorWithoutThrowing` — `chmod 000` a temp subdirectory; skip via
  `GTEST_SKIP()` if `geteuid() == 0` (root bypasses permission checks — mirrors the environment-adaptive
  skip idiom `test_credential_store_worker_threading.cpp` already uses for a different reason)

### 7.3 `providers/test_ollama_provider.cpp` (extended, existing file)

- `SetBaseUrlAffectsOnlySubsequentRefreshCalls` — call `refresh()`, then `setBaseUrl(...)`, then
  `refresh()` again; assert the *second* `FakeHttpClient` buffered call... (needs
  `lastBufferedRequest()`, §4.3) targeted the new URL, and that mutating `base_url_` mid-flight
  (before the first `FakeHttpClient` response is delivered, using
  `setBufferedResponsesDeferred(true)`) does not change the already-recorded first request's URL.
- `SetAuthTokenAddsBearerHeaderToFetchModelList` — assert `lastBufferedRequest().headers.value("Authorization")
  == "Bearer <token>"`.
- `EmptyAuthTokenOmitsAuthorizationHeader` — default-constructed provider, assert no `Authorization`
  key present at all (not merely empty-valued).
- `SetAuthTokenAddsBearerHeaderToSendChat` — via `streamingCall(0).request.headers`.
- `SendChatIncludesTemperatureAndContextWindowInOptions` — parse the request body JSON, assert
  `options.temperature`/`options.num_ctx` match `setTemperature()`/`setContextWindow()` values (and
  the untouched defaults 0.7/4096 when neither setter was called).
- `RefreshTwoCallbackOverloadCallsOnSuccessOnHttpSuccess`
- `RefreshTwoCallbackOverloadCallsOnErrorWithReasonOnHttpFailure`
- `RefreshTwoCallbackOverloadTreatsEmptyModelsArrayAsSuccess` (REQ-F-011's "zero models is still a
  successful connection")

### 7.4 `application/test_chat_view_model.cpp` (extended, existing file)

- `ProviderForSettingsReturnsTheSameSharedInstanceEveryCall`
- `SyncAvailableModelsFromProviderUpdatesAvailableModelsList`
- `SyncAvailableModelsFromProviderPrefersGivenModelIdWhenPresentInList`
- `SyncAvailableModelsFromProviderFallsBackToCurrentSelectionWhenPreferredModelAbsent`
- `ConstructorPrefersInitialDefaultModelIdOverFrontWhenNoLastModelId`

### 7.5 `application/test_provider_settings_controller.cpp` (new)

Every case constructs two real `OllamaProvider`s (shared + probe) over two independent
`FakeHttpClient`s, a `FakeCredentialStore` (`tests/credentials/fake_credential_store.h`, already
exists), and a `ConfigRepository` over a `QTemporaryDir` path — no real network, no real keyring, no
`QQmlEngine` (the constructor under test never touches QML machinery, only `create()` does, and
`create()` itself is production-only glue with no independent test value beyond what an integration
smoke test would give — not planned for this GTest suite).

- `LoadPopulatesPropertiesFromConfigRepository`
- `LoadRetrievesStoredTokenAndSetsHasStoredToken`
- `LoadWithNoStoredTokenLeavesAuthTokenEmpty`
- `RefreshModelsUsesProbeProviderNotSharedProvider` — assert the shared `FakeHttpClient` receives
  zero calls; the probe `FakeHttpClient` receives one.
- `RefreshModelsUsesCurrentInPanelUrlNotLastSavedUrl`
- `RefreshModelsFailurePopulatesModelRefreshErrorDistinctFromTestConnectionMessage`
- `TestConnectionSuccessSetsStatusSuccessAndOllamaConnectionStatusConnected`
- `TestConnectionFailureSetsStatusErrorWithoutTouchingAvailableModelNames`
- `TestConnectionWithZeroModelsIsStillSuccess`
- `SaveWritesConfigAndAppliesBaseUrlContextWindowTemperatureToSharedProvider`
- `SaveInvokesModelSyncCallbackWithConfiguredDefaultModel`
- `SaveWithEmptyDefaultModelInvokesCallbackWithNullopt`
- `SaveWithNonEmptyTokenCallsCredentialStoreStore`
- `SaveWithClearedTokenAndPriorStoredCredentialCallsCredentialStoreRemove`
- `SaveWithEmptyTokenAndNoPriorCredentialCallsNeitherStoreNorRemove`
- `SaveRejectsContextWindowOutOfRangeWithoutWritingConfigFile`
- `SaveRejectsTemperatureOutOfRangeWithoutWritingConfigFile`
- `SaveFailureLeavesSharedProviderAndLastSavedConfigUntouched` (inject a `ConfigRepository` pointed
  at an unwritable path — same `chmod 000` idiom as §7.2)
- `CancelRevertsAllFieldsToLastSavedConfigWithoutReReadingConfigFile`
- `CancelClearsAuthTokenFieldEvenWhenACredentialIsStored`
- `CancelAfterEditDoesNotCallSaveOllamaConfigOrCredentialStoreMethods`
- `ResetToDefaultsSetsFactoryValuesInPanelOnly`
- `ResetThenCancelRevertsToLastSavedNotFactoryDefaults` (REQ-F-014's explicit acceptance criterion)
- `CredentialStoreUnavailableSignalSetsCredentialStoreAvailableFalse`
- `StoreCompletedWithCredentialStoreNowUnavailableSurfacesFailureNotice` (§0/§2.10's "no
  `operationFailed` signal exists" workaround — the one test that most directly exercises the
  actual, simpler `CredentialStore` contract this design was built around)

---

## 8. Alternatives Considered

- **A second `QQuickView`/`QQmlEngine` for the Settings window** (rejected, §5.1): breaks
  `QML_SINGLETON` instance sharing between `ChatViewModel` and `ProviderSettingsController`,
  directly defeating REQ-F-012's live-reconfiguration requirement. A nested `Window` sharing the
  main engine has no such problem and needs no `ChatApplication.cpp` changes at all.
- **Reusing the shared `OllamaProvider` for in-panel Refresh/Test Connection, with Cancel
  reverting its mutated state** (rejected, §5.3): correctness would hinge on Cancel perfectly
  reconstructing pre-edit state, and creates a real window where live chat traffic could use an
  unsaved, user-rejected URL. A second, disposable probe `OllamaProvider` makes Cancel trivially
  correct by never mutating the shared instance in the first place.
- **Fetching the auth token from `CredentialStore` on every `OllamaProvider` HTTP request**
  (rejected, §5.4): SPEC's own literal suggestion. Would force every synchronous, inline
  request-building call in `OllamaProvider` into an async two-hop operation and introduce a
  `holonight_providers → holonight_credentials` dependency the rest of the module graph doesn't
  need. In-memory caching pushed by `ProviderSettingsController`, refreshed only on explicit user
  edits, is both simpler and behaviorally identical from the user's perspective.
- **A worker-thread `ConfigRepository`, matching `SqliteConversationRepository`'s shape**
  (rejected, §5.2): the precedent's justification (blocking SQL / D-Bus latency) does not apply to
  a sub-1KB local JSON file touched at most twice per Settings session; the added `QThread` +
  queued-signal machinery would be unjustified ceremony with no measurable benefit and REQ-NF-006
  never asks for it.
- **A new dedicated static library (e.g. `holonight_settings_ui`) for `ProviderSettingsController`**
  (rejected, §5.5): would require either duplicating `apps/chat/CMakeLists.txt`'s existing
  metatype-extraction wiring for a second static library, or extending
  `combine-metatypes.cmake`'s `INPUT_FILES` — both strictly more CMake surface than placing the
  controller inside the already-wired `holonight_application`, for identical runtime behavior.
- **Passing a `ChatViewModel*` pointer directly into `ProviderSettingsController`'s constructor**
  (rejected, §4.6): would force every `ProviderSettingsController` GTest to also construct a full
  `ChatViewModel` (itself requiring a `ConversationRepository`, an `OllamaProvider`, etc.) just to
  satisfy the type. A narrow `std::function<void(optional<ModelId>)>` callback captures exactly the
  one behavior needed (§4.3's `syncAvailableModelsFromProvider`) and lets tests pass a trivial
  lambda instead, matching `ChatController`'s own existing callback-based style.
- **Treating the "providers" JSON key as flat (`{"base_url": ..., ...}` at the document root)
  instead of namespaced under `"providers"."ollama"`** (rejected, §4.2): the namespaced form costs
  nothing today and avoids a schema migration whenever the OpenAI/Anthropic/Google cycles (REQ-C-006
  explicitly defers, not forbids forever) need sibling provider sections.

---

## 9. Known Risks

- **REQ-F-019's "re-enables when Secret Service becomes available again, without app restart" is not
  achievable within one running process**, given the already-shipped `CredentialStore`'s sticky,
  never-resets `available_` flag (§0, §2.11). This is an inherited limitation of a module this cycle
  does not modify, not a gap introduced by this design — flagged explicitly rather than silently
  designed around. A future cycle that wants true within-process recovery would need to change
  `SecretServiceCredentialStore`/`CredentialStoreWorker` itself (e.g. re-probing on the next
  operation after a cooldown), which is out of scope here.
- **No per-operation store/remove failure signal exists on `CredentialStore`.** §2.10's
  `isAvailable()`-after-`storeCompleted()` heuristic is exact against the *current* implementation
  (every `GError`, of any kind, flips `available_` false unconditionally — verified in §0), but is
  inherently fragile to a future change in `CredentialStoreWorker::handleLibsecretError()` that
  reintroduces the finer-grained recoverable/fatal classification `secret-service-credentials/DESIGN.md`
  originally described. If that classification is reintroduced without also reintroducing a
  per-operation failure signal, this heuristic would silently under-report token-save failures for
  any recoverable-but-not-sticky error. Test `StoreCompletedWithCredentialStoreNowUnavailableSurfacesFailureNotice`
  (§7.5) exists specifically to catch a regression here.
- **`options.temperature`/`options.num_ctx` are new fields in the `/api/chat` request body** that
  this design adds based on Ollama's documented API shape, not by testing against a live Ollama
  server as part of this cycle (per the "No visual verification" project convention, and because no
  network access is assumed at build/test time). If a user's installed Ollama version behaves
  differently for these fields than assumed, the failure mode is silently-ignored options (Ollama's
  documented behavior for unrecognized `options` keys), not a crash or malformed request — low
  severity, but worth a manual smoke-test checklist item handed to the user post-implementation, per
  the project's existing "hand the user a checklist" convention for anything this design cannot
  verify itself.
- **`resetToDefaults()`'s factory context-window/temperature values (4096 / 0.7) are asserted to
  match `holonight_config::OllamaProviderConfig{}`'s own defaults "because both are declared with
  the same literals," not by any shared single source of truth.** A future edit to one default
  without the other would silently desynchronize REQ-F-014's "Reset" behavior from REQ-F-015's
  "missing config file" behavior. Low risk (both live in files this same cycle introduces, reviewed
  together), but worth a code-comment cross-reference (already included in §4.4's diff) rather than
  a runtime `static_assert`, since `OllamaProvider`'s and `OllamaProviderConfig`'s default values are
  independently-typed literals (`int`/`double` member initializers) with no shared constant Qt's
  `Q_PROPERTY`/aggregate-initialization idioms make convenient to unify further.
- **The eight-sidebar-entries QML structure (§3) and the exact HoloNight component choice for the
  gear icon, eye-icon token-visibility toggle, and status dots are left at the sketch level**,
  per this design's explicit scope boundary (Stage 4/task-breakdown territory, matching
  `chat-window-qml/DESIGN.md`'s own precedent of not writing literal QML). None of REQ-NF-001's
  "no hardcoded hex, `HoloniightPalette` tokens only" constraints are at risk from this — every
  property/signal binding surface those QML files need is already fully specified in §4.6/§3.

---

## Document History

| Version | Date       | Author | Changes                          |
|---------|------------|--------|-----------------------------------|
| 1.0     | 2026-07-22 | Claude | Initial design from SPEC.md v1.0  |
