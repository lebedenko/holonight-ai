# Background AI Settings Panel — Architecture & Design

**Status:** Design (Stage 2 of SDD cycle)
**Companion:** `SPEC.md` (same directory) — every section below is traceable to REQ-F/REQ-NF/REQ-C IDs.

---

## 0. Wording correction carried from SPEC review

REQ-NF-005 says "single database transaction." `holonight_config`/`ConfigRepository` is JSON-file-based
(`config.json` via `QSaveFile`), not SQL — there is no database and no literal transaction. This design
implements REQ-NF-005's *intent* (all-or-nothing persistence, no partially-written state) via the existing
mechanism: `ConfigRepository::writeRootObject()` builds the complete JSON document in memory and writes it
through `QSaveFile`, which stages to a temporary file and only replaces the real file via `commit()` (an
atomic rename) if the full write succeeded — see `src/config/src/config_repository.cpp:191-212`. A single
call to `saveUtilityConfig()` (or `saveProviderStateAndUtilityConfig()` when a provider deletion also
touches utility state) is therefore the atomic unit for this feature: either the whole `UtilityConfig`
lands on disk, or the file on disk is untouched. All references to "atomicity" below mean this, not a SQL
transaction.

---

## 1. Scope recap

Three independent workstreams, all touching the same `UtilityConfig`/`UtilityTaskRunner` subsystem:

1. **New QML settings panel** ("Background AI") — default utility model picker, chat-title toggle, chat-title
   model override picker, Save/Discard.
2. **Two new `UtilityConfig` fields** — `chat_title_generation_enabled`, `chat_title_model_override` — plus
   their JSON persistence.
3. **`UtilityTaskRunner` correctness fixes** — router goes stale after startup (never resynced on provider
   changes), utility generation params leak the chat-configured temperature/max-tokens when the router path
   is active, and the three-tier model resolution for title generation was never wired up (present as a
   parameter that is always ignored with a "not implemented yet" warning).

Item 3 is the load-bearing part of this cycle: the sidebar/panel is inert without it, since REQ-F-020
requires Save to visibly affect the next title-generation call.

---

## 2. Current-state findings that shape this design

These were confirmed by reading the code, not assumed from the spec's problem statement:

- **`UtilityTaskRunner` already owns a `ProviderAdapterRouter`** (`adapter_router_`, built once in the
  constructor when `provider_state_.instances` is non-empty) and `dispatchSendChat()` already prefers it
  over the hardcoded-literal fallback. So REQ-F-014/015's "fix the hardcoded routing bug" is **already true
  for any app that has gone through the multi-instance provider migration** — the literal-string branch in
  `dispatchSendChat()` only fires when `provider_state_.instances` is empty (very old/never-migrated
  configs). The **actual** bug is that the router is built once and **never updated** — there is no
  `UtilityTaskRunner::applyProviderState()` counterpart to `ChatViewModel::applyProviderState()`, so adding,
  editing, or deleting a provider instance after startup silently desyncs `UtilityTaskRunner`'s router from
  reality. This matches REQ-F-014's acceptance criterion ("the router is updated whenever live provider
  state changes") almost exactly — that's the method this design adds.
- **REQ-F-016 (param isolation) is only implemented on the legacy no-instances fallback path.** In the
  legacy branch, `ollama_provider_->setTemperature(0.3)` etc. are called directly on the runner's own
  provider objects. In the router branch (the one that matters for real multi-instance setups), instances
  are added to `adapter_router_` with **their chat-configured settings verbatim**
  (`utility_task_runner.cpp:69-93`, `ProviderAdapterRouter::add()` → `createProviderAdapter()` →
  `applySettings()` copies `settings->temperature` unchanged). So today, in the common case, a utility call
  silently uses the chat window's temperature/max-tokens. This is the real gap this design closes for
  REQ-F-016/REQ-NF-001.
- **`resolveModel()`'s `taskOverride` parameter is dead code today.** It is accepted, logged as
  "not implemented yet, ignoring" in two places, and never consulted in the tier ladder. It is *not* the
  same thing as the new `UtilityConfig::chat_title_model_override` field — the parameter is a possible
  future per-call override (e.g. a "regenerate with a specific model" action), while the config field is
  the persisted, panel-editable default for the title-generation task specifically. This design leaves the
  parameter as-is (still unused, still logged) and implements REQ-F-012's three tiers by reading
  `utility_config_.chat_title_model_override` directly inside `resolveModel()`, since title generation is
  `resolveModel()`'s only caller today.
- **Provider deletion already has a partial safety net.** `ProviderInstanceDeleter::remove()`
  (`provider_instance_deleter.cpp:66-73`) already reloads `UtilityConfig`, clears
  `default_utility_model` if it pointed at the deleted instance, and — only in that case — calls
  `saveProviderStateAndUtilityConfig()` instead of `saveProviderState()` so the provider removal and the
  utility-config fix land in one write. This is the established pattern REQ-F-017/REQ-NF-003 must extend to
  the new `chat_title_model_override` field.
- **`SettingsSidebar`/`SettingsWindow` have no concept of "current section" today.** Only one section
  (`"providers"`) is `enabled: true`; the delegate's `checked` binding is literally
  `entryDelegate.modelData.enabled` (see `SettingsSidebar.qml:52`) — coincidentally correct only because
  exactly one section is enabled. `SettingsWindow.qml` hardcodes `ProviderListPanel` + `ProvidersPage` as
  the entire content area. Adding a second enabled section (REQ-F-001) requires introducing real
  section-selection state, not just adding a sidebar row.
- **`ProviderSettingsScaffold` is coupled to provider-*instance* CRUD**, not generic settings-panel
  chrome: it binds to a `providerController` with `displayName`, `enabled` (a provider-enable toggle),
  `deletionExplanation`, `canDelete`, and a delete-confirmation `Dialog` baked into the component
  (`ProviderSettingsScaffold.qml:15-60,143-193`). Background AI has no "instance," no delete action, and no
  per-instance enable switch. Reusing this component as-is would force fake/unused controller properties
  onto `UtilitySettingsController` purely to satisfy the scaffold's bindings. §9 documents the resulting
  deviation from REQ-C-002's literal wording.

---

## 3. Component map

### 3.1 C++ (all in `holonight_application` unless noted)

| Component | Change | Reason |
|---|---|---|
| `holonight_config::UtilityConfig` (`holonight_config`) | **Modified** — add `chat_title_generation_enabled`, `chat_title_model_override` | REQ-F-008/011 |
| `ConfigRepository` (`holonight_config`) | **Modified** — `applyUtilityConfig()`/`loadUtilityConfig()` read/write the two new fields | REQ-F-008/011, REQ-NF-002 |
| `UtilityTaskRunner` | **Modified** — new `applyProviderState()`, new `applyUtilityConfig()`, utility-param injection on router add/reconfigure, three-tier `resolveModel()`, title-toggle gate | REQ-F-009/012/013/014/015/016/017, REQ-NF-001/003/004 |
| `ChatViewModel` | **Modified** — `applyProviderState()` forwards to `utility_task_runner_->applyProviderState()`; new plain accessor `utilityTaskRunnerForSettings()` | REQ-F-020, REQ-C-001 |
| `ProviderInstanceDeleter` | **Modified** — also clears `chat_title_model_override` when it references the deleted instance | REQ-F-017, REQ-NF-003 |
| `UtilitySettingsController` (new) | **New** `QML_SINGLETON` — draft/save/discard for `UtilityConfig`, provider-instance + model listing independent of chat's selection | REQ-F-003–013, REQ-F-018–021, REQ-NF-004, REQ-C-002/006 |

### 3.2 QML

| Component | Change | Reason |
|---|---|---|
| `qml/workspace/SettingsSidebar.qml` | **Modified** — add "Background AI" entry, `enabled: true`; introduce real section-selection state | REQ-F-001 |
| `qml/workspace/SettingsWindow.qml` | **Modified** — switch content area between the existing Providers layout and the new Background AI panel based on selected section | REQ-F-001 (supporting) |
| `qml/workspace/BackgroundAiSettingsPanel.qml` (new) | **New** — top-level panel: header, "Default utility model" section, "Chat titles" section (toggle + override picker), footer Save/Discard | REQ-F-003/004/007/010/018/019, REQ-C-002/003/004/005/007 |
| `qml/workspace/ProviderModelPicker.qml` (new) | **New** — reusable two-tier provider-instance + model-name picker, used twice inside `BackgroundAiSettingsPanel` (default model, title override) | REQ-F-003/005/010, REQ-NF-004 |
| `assets/icons/settings-background-ai.svg` (new) | **New** icon asset | REQ-F-002 |

No `CMakeLists.txt` changes: `apps/chat/CMakeLists.txt` globs `qml/*.qml` and `assets/icons/*.svg` with
`CONFIGURE_DEPENDS` (lines 13-37) and `qt6_extract_metatypes()`/`combine-metatypes.cmake` already sweep every
`QML_SINGLETON` in `holonight_application` (per the project's established pattern — see CLAUDE.md "QML
Singletons From Static Libraries"). A new file under `qml/workspace/` or `assets/icons/` and a new
`QML_SINGLETON` class in `holonight_application` both build with a re-`configure`, satisfying REQ-C-006
exactly as it did for `ProviderSettingsController`/`OpenAIProviderSettingsController`.

---

## 4. Data model

### 4.1 `UtilityConfig` (`src/config/include/holonight_config/utility_config.h`)

```cpp
struct UtilityConfig {
  std::optional<holonight_domain::ModelId> default_utility_model;

  // REQ-F-008/REQ-NF-002: absent (nullopt) means "not yet set by the user" and is treated as `true`
  // everywhere it's consulted — see UtilityTaskRunner::requestTitleGeneration(). Distinguishing
  // "unset" from "explicitly false" (rather than a plain `bool` defaulting to true) lets
  // loadUtilityConfig() tell a legacy config apart from one where the user explicitly disabled the
  // toggle and then re-enabled it, and keeps this field's shape symmetric with the two ModelId
  // fields below for JSON round-tripping.
  std::optional<bool> chat_title_generation_enabled;

  // REQ-F-011: task-specific override for chat-title generation. Consulted before
  // default_utility_model in UtilityTaskRunner::resolveModel()'s tier ladder (REQ-F-012).
  std::optional<holonight_domain::ModelId> chat_title_model_override;

  friend bool operator==(const UtilityConfig&, const UtilityConfig&) = default;
};
```

Note: the file's existing top-of-file comment cites "REQ-F-001"/"REQ-F-002" — those are IDs from the
*original* `UtilityTaskRunner` cycle's spec, not this one. Do not confuse them with this SPEC's REQ IDs
when updating the comment; reword to avoid the collision (e.g. reference this feature's doc path instead of
bare REQ IDs, or update to the current REQ-F-008/011).

### 4.2 JSON shape (`config.json`, under the existing `"utility"` key)

```jsonc
{
  "utility": {
    "default_utility_model": { "provider_id": "…", "model_name": "…" },   // unchanged
    "chat_title_generation_enabled": true,                                 // new, omitted when unset
    "chat_title_model_override": { "provider_id": "…", "model_name": "…" } // new, omitted when unset
  }
}
```

`applyUtilityConfig(QJsonObject&, const UtilityConfig&)` (`config_repository.cpp:354-364`) gains two
symmetric blocks, following the existing `default_utility_model` pattern exactly:

```cpp
constexpr auto kChatTitleGenerationEnabledKey = "chat_title_generation_enabled";
constexpr auto kChatTitleModelOverrideKey = "chat_title_model_override";

void applyUtilityConfig(QJsonObject& root, const UtilityConfig& config) {
  QJsonObject utility = root.value(QLatin1String(kUtilityKey)).toObject();
  if (config.default_utility_model.has_value()) { /* unchanged */ }
  else { utility.remove(QLatin1String(kDefaultUtilityModelKey)); }

  if (config.chat_title_generation_enabled.has_value()) {
    utility[QLatin1String(kChatTitleGenerationEnabledKey)] = *config.chat_title_generation_enabled;
  } else {
    utility.remove(QLatin1String(kChatTitleGenerationEnabledKey));
  }

  if (config.chat_title_model_override.has_value()) {
    utility[QLatin1String(kChatTitleModelOverrideKey)] =
        QJsonObject{{QLatin1String(kProviderIdKey), config.chat_title_model_override->provider_id},
                    {QLatin1String(kModelNameKey), config.chat_title_model_override->model_name}};
  } else {
    utility.remove(QLatin1String(kChatTitleModelOverrideKey));
  }
  root[QLatin1String(kUtilityKey)] = utility;
}
```

`loadUtilityConfig()` gains matching reads: `chat_title_generation_enabled` reads a `QJsonValue::Bool` (only
set when present *and* boolean — absent/malformed both fall through to `std::nullopt`, giving the
REQ-NF-002 "true by default" behavior at the call site, not at the loader). `chat_title_model_override`
mirrors `default_utility_model`'s object-with-two-strings parse exactly (same empty-string rejection).

`saveUtilityConfig()` / `saveProviderStateAndUtilityConfig()` need no changes — both already funnel through
`applyUtilityConfig()` + `writeRootObject()`.

---

## 5. `UtilityTaskRunner` changes (the core fix)

### 5.1 New method: `applyProviderState()`

```cpp
// Mirrors ChatViewModel::applyProviderState() (REQ-C-001: same diff/update mechanism, no new
// pattern). Called by ChatViewModel::applyProviderState() so both routers stay in lockstep whenever
// Settings persists provider changes. Injects utility-specific generation parameters into a COPY of
// each instance before it reaches the router (REQ-F-016) — provider_state_ itself keeps the
// unmodified, chat-configured settings so resolveModel()'s last-resort "first available model of any
// enabled instance" scan and REQ-F-017's isUsable() checks stay accurate.
void UtilityTaskRunner::applyProviderState(holonight_config::ProviderState provider_state);
```

Body follows `ChatViewModel::applyProviderState()`'s three-phase shape exactly (diff removals first, then
add-or-reconfigure, then swap `provider_state_`):

```cpp
void UtilityTaskRunner::applyProviderState(holonight_config::ProviderState provider_state) {
  if (!adapter_router_) {
    provider_state_ = std::move(provider_state);   // legacy fallback path: nothing to resync
    return;
  }

  for (const auto& previous : provider_state_.instances) {
    if (std::ranges::find(provider_state.instances, previous.id,
                           &holonight_config::ProviderInstanceConfig::id) == provider_state.instances.end()) {
      static_cast<void>(provider_runtime_coordinator_->unregisterProvider(previous.id));
      static_cast<void>(adapter_router_->remove(previous.id));   // REQ-F-017: no-op if streaming
    }
  }

  for (const auto& instance : provider_state.instances) {
    const auto utilityInstance = withUtilityGenerationParams(instance);   // REQ-F-016
    if (!adapter_router_->contains(instance.id)) {
      if (instance.enabled && adapter_router_->add(utilityInstance, clientFor(instance.type, endpoints_))) {
        registerRuntimeProvider(instance);   // extracted from the constructor's inline lambdas
      }
    } else {
      static_cast<void>(adapter_router_->reconfigure(utilityInstance));
      static_cast<void>(adapter_router_->setEnabled(instance.id, instance.enabled));
      provider_runtime_coordinator_->setEnabled(instance.id, instance.enabled);
    }
  }

  provider_state_ = std::move(provider_state);
}
```

`endpoints_` needs to become a stored member (currently `endpoints` is a constructor-local parameter,
consumed only during construction) so `applyProviderState()` can build HTTP clients for newly-added
instances the same way the constructor does. The per-instance `provider_runtime_coordinator_->registerProvider(...)`
lambda block in the constructor (`utility_task_runner.cpp:76-91`) is extracted into a private
`registerRuntimeProvider(const ProviderInstanceConfig&)` helper so both the constructor and
`applyProviderState()` share it — same de-duplication `ChatViewModel` already does with its own
`registerRuntimeProvider()`.

### 5.2 New method: `applyUtilityConfig()`

```cpp
// REQ-F-020: swaps the live UtilityConfig consulted by resolveModel()/requestTitleGeneration(),
// independent of provider/router changes. Distinct from applyProviderState() because Save on the
// Background AI panel changes config fields, not the provider roster — SPEC's REQ-F-020 acceptance
// criterion names applyProviderState() as "the" update path, but that method's job (router sync) is
// orthogonal to this one's (config swap); both are needed and both are called from
// UtilitySettingsController::save() via ChatViewModel::utilityTaskRunnerForSettings().
void UtilityTaskRunner::applyUtilityConfig(holonight_config::UtilityConfig utility_config) {
  utility_config_ = std::move(utility_config);
}
```

No extra bookkeeping needed: `resolveModel()` and `requestTitleGeneration()` read `utility_config_` fresh on
every call, and in-flight generations (`generations_`) already snapshot their resolved `ModelId` into
`InFlightGeneration::model` at dispatch time, so swapping `utility_config_` mid-flight cannot corrupt an
already-dispatched request.

### 5.3 Utility-param injection helper (REQ-F-016 / REQ-NF-001)

```cpp
namespace {
constexpr double kUtilityTemperature = 0.3;
constexpr int kAnthropicUtilityMaxOutputTokens = 64;    // promoted from the existing inline literal
constexpr int kGoogleUtilityMaxOutputTokens = 1024;     // already named; reused, not duplicated

holonight_config::ProviderInstanceConfig withUtilityGenerationParams(
    holonight_config::ProviderInstanceConfig instance) {
  std::visit(
      [](auto& settings) {
        settings.temperature = kUtilityTemperature;
        using Settings = std::decay_t<decltype(settings)>;
        if constexpr (std::is_same_v<Settings, holonight_config::AnthropicProviderConfig>) {
          settings.max_output_tokens = kAnthropicUtilityMaxOutputTokens;
        } else if constexpr (std::is_same_v<Settings, holonight_config::GoogleProviderConfig>) {
          settings.max_output_tokens = kGoogleUtilityMaxOutputTokens;
        }
        // Ollama/OpenAI configs have no max_output_tokens field — temperature alone applies.
      },
      instance.settings);
  return instance;
}
}  // namespace
```

Used at both call sites that add an instance to `adapter_router_`: the constructor's per-instance loop
(`utility_task_runner.cpp:69-93`) and the new `applyProviderState()`. `provider_state_` (the member used by
`resolveModel()`'s fallback scan and by REQ-F-017's usability checks) always stores the **original**,
un-mutated `ProviderInstanceConfig` — only the router-bound copy is modified. This keeps chat's configured
temperature/max-tokens fully unaffected (REQ-NF-001's second half: "chat parameters remain unaffected").

### 5.4 `resolveModel()` — three-tier resolution (REQ-F-012/013)

```cpp
std::optional<holonight_domain::ModelId> UtilityTaskRunner::resolveModel(
    const std::optional<holonight_domain::ModelId>& taskOverride,
    const holonight_domain::ModelId& chatFallbackModel) const {
  // taskOverride: reserved for a future per-call override (e.g. a "regenerate with this model"
  // action); always std::nullopt from today's sole caller, requestTitleGeneration(). NOT the same
  // as utility_config_.chat_title_model_override, which is this method's actual tier-1 source below.
  const auto isUsable = [this](const holonight_domain::ModelId& model) { /* unchanged */ };

  if (adapter_router_) {
    if (taskOverride.has_value() && isUsable(*taskOverride)) return *taskOverride;
    if (utility_config_.chat_title_model_override.has_value() &&
        isUsable(*utility_config_.chat_title_model_override)) {
      return utility_config_.chat_title_model_override;
    }
    if (utility_config_.default_utility_model.has_value() && isUsable(*utility_config_.default_utility_model)) {
      return utility_config_.default_utility_model;
    }
    if (isUsable(chatFallbackModel)) return chatFallbackModel;
    /* … existing "first available model of any enabled instance" last resort, unchanged … */
  }

  // Legacy (adapter_router_ == nullptr) branch: same tier order, using the four-literal isKnownProvider
  // check already present, extended with the same chat_title_model_override tier for consistency.
}
```

The two `qWarning(...".. not implemented yet, ignoring")` call sites (one in `requestTitleGeneration()`, one
at `resolveModel()`'s entry) are deleted — they were guarding the *parameter*, which remains unused and no
longer needs a runtime warning since it was never wired to anything reachable from a real call site; this
avoids spurious log noise once the config-driven tiers are live.

### 5.5 Title-generation gate (REQ-F-009/REQ-NF-002)

```cpp
void UtilityTaskRunner::requestTitleGeneration(...) {
  if (!utility_config_.chat_title_generation_enabled.value_or(true)) {
    return;   // REQ-F-009: disabled — no dispatch, no generations_ entry, no log noise
  }
  const QString conversationKey = conversationId.toString();
  if (generations_.contains(conversationKey)) return;   // unchanged
  ...
}
```

Placed before the `generations_.contains()` no-op-if-already-attempted check so toggling the setting
mid-session never leaves a phantom "already attempted" entry for a conversation that was never actually
dispatched. `.value_or(true)` is the single point implementing REQ-NF-002's "absent ⇒ true" rule; no
special-casing needed elsewhere.

### 5.6 Graceful degradation on deletion (REQ-F-017/REQ-NF-003)

No new code is needed here beyond §5.1/§5.3: once `applyProviderState()` removes a deleted instance from
`adapter_router_`, `isUsable()` (which checks `adapter_router_->isEnabled(...)` and
`adapter_router_->availableModels(...)`) returns `false` for any `ModelId` pointing at it, so
`resolveModel()`'s existing tier-skip logic already falls through cleanly — this is the same defensive
pattern the pre-existing `UnknownProviderIdInDefaultModelSkipsGenerationEntirely` test already exercises for
the no-router legacy path. The only genuinely new piece is §6.2 (deleter also clears
`chat_title_model_override`), so the *persisted* config doesn't keep a permanently-dangling reference.

---

## 6. `ChatViewModel` / provider-deletion wiring

### 6.1 `ChatViewModel::applyProviderState()` — one new line

```cpp
void ChatViewModel::applyProviderState(holonight_config::ProviderState provider_state) {
  if (!adapter_router_) return;
  /* … existing diff/add/reconfigure loop, unchanged … */
  provider_state_ = std::move(provider_state);
  message_model_->setProviderState(provider_state_);
  syncAvailableModels();
  if (utility_task_runner_) {
    utility_task_runner_->applyProviderState(provider_state_);   // NEW — closes the REQ-F-014 gap
  }
}
```

This is the actual fix for "the router is updated whenever live provider state changes" — today nothing
calls it for `UtilityTaskRunner`, so its router silently drifts from `ChatViewModel`'s after the first
provider add/edit/delete post-startup.

### 6.2 New plain accessor

```cpp
// Plain C++ accessor (not a Q_PROPERTY — only C++ singleton-to-singleton wiring needs it), mirroring
// providerForSettings()/openAiProviderForSettings() etc. Lets UtilitySettingsController push a saved
// UtilityConfig into the live runner without ChatViewModel exposing utility_task_runner_ itself.
[[nodiscard]] UtilityTaskRunner* utilityTaskRunnerForSettings() const { return utility_task_runner_.get(); }
```

### 6.3 `ProviderInstanceDeleter::remove()` — extend the existing clear

```cpp
UtilityConfig utility = config_repository_.loadUtilityConfig();
bool clearUtility = false;
if (utility.default_utility_model.has_value() && utility.default_utility_model->provider_id == instance_id) {
  utility.default_utility_model.reset();
  clearUtility = true;
}
if (utility.chat_title_model_override.has_value() && utility.chat_title_model_override->provider_id == instance_id) {
  utility.chat_title_model_override.reset();   // NEW
  clearUtility = true;
}
const auto saved = clearUtility ? config_repository_.saveProviderStateAndUtilityConfig(candidate, utility)
                                : config_repository_.saveProviderState(candidate);
```

Same one-write atomicity guarantee as today's `default_utility_model` clear (§0): provider removal and the
utility-config fix land in a single `writeRootObject()` call, never split across two writes.

---

## 7. `UtilitySettingsController` (new QML_SINGLETON)

### 7.1 Why a new, independent router instead of reusing an existing one

`ProviderManagementController` already owns a `ProviderAdapterRouter` (`router_`,
`provider_management_controller.h:94`) built from every saved instance, used for provider CRUD. Reusing it
for Background AI's model pickers was considered and rejected: that router's adapters carry the **chat**
generation settings (no utility-param injection — it exists purely to support instance add/edit/delete and
connection testing), and coupling the two would mean a picker's model-listing behavior implicitly depends on
`ProviderManagementController`'s lifecycle/selection state, which is exactly the "independent of chat's
provider selection" property REQ-F-005/REQ-NF-004 rule out coupling to. Instead:

```cpp
class UtilitySettingsController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

  // ---- provider-instance picker source (shared by both ProviderModelPicker instances) ----
  Q_PROPERTY(QVariantList providerInstances READ providerInstances NOTIFY providerInstancesChanged)

  // ---- "Default utility model" section (REQ-F-003/004/005/006) ----
  Q_PROPERTY(QString defaultProviderId READ defaultProviderId WRITE setDefaultProviderId NOTIFY draftChanged)
  Q_PROPERTY(QString defaultModelName READ defaultModelName WRITE setDefaultModelName NOTIFY draftChanged)
  Q_PROPERTY(QStringList defaultModelNames READ defaultModelNames NOTIFY defaultModelNamesChanged)

  // ---- "Chat titles" section (REQ-F-007/008/010/011) ----
  Q_PROPERTY(bool chatTitleGenerationEnabled READ chatTitleGenerationEnabled
             WRITE setChatTitleGenerationEnabled NOTIFY draftChanged)
  Q_PROPERTY(QString titleOverrideProviderId READ titleOverrideProviderId
             WRITE setTitleOverrideProviderId NOTIFY draftChanged)
  Q_PROPERTY(QString titleOverrideModelName READ titleOverrideModelName
             WRITE setTitleOverrideModelName NOTIFY draftChanged)
  Q_PROPERTY(QStringList titleOverrideModelNames READ titleOverrideModelNames NOTIFY titleOverrideModelNamesChanged)

  // ---- draft/save/discard plumbing (REQ-F-018/019/020/021, REQ-C-002) ----
  Q_PROPERTY(bool dirty READ dirty NOTIFY draftChanged)
  Q_PROPERTY(bool canSave READ canSave NOTIFY draftChanged)
  Q_PROPERTY(QString saveNotice READ saveNotice NOTIFY saveNoticeChanged)
  Q_PROPERTY(QString saveNoticeStatus READ saveNoticeStatus NOTIFY saveNoticeChanged)

 public:
  static UtilitySettingsController* create(QQmlEngine*, QJSEngine*);
  explicit UtilitySettingsController(QQmlEngine* qml_engine, QObject* parent = nullptr);

  Q_INVOKABLE bool save();
  Q_INVOKABLE void discardDraft();
  Q_INVOKABLE void refreshModelsForInstance(const QString& providerInstanceId);  // triggers a
                                                                                  // ProviderRuntimeCoordinator
                                                                                  // refresh; property update
                                                                                  // arrives via signal
 private:
  holonight_config::ConfigRepository config_repository_;
  std::unique_ptr<ProviderAdapterRouter> router_;              // OWN router, utility-param-injected
  std::unique_ptr<ProviderRuntimeCoordinator> runtime_coordinator_;
  holonight_config::UtilityConfig saved_;   // last persisted value, for discardDraft()
  holonight_config::UtilityConfig draft_;   // edited in place by the property setters
  holonight_config::ProviderState provider_state_;   // for the instance picker + Save-time validation
};
```

`providerInstances` is a `QVariantList` of `{id, name}` maps — same shape `ChatViewModel::availableProviders()`
already produces for QML, plus a synthetic leading entry `{id: "", name: qsTr("Use chat model")}` for
REQ-F-004's null sentinel. `defaultProviderId == ""` / `titleOverrideProviderId == ""` is the sentinel for
"unset" in both pickers; setting a picker's provider id to `""` also clears its paired model-name property,
matching REQ-F-004's "clears `default_utility_model`" acceptance criterion.

`router_` is built at construction the same way `UtilityTaskRunner`'s is: iterate
`config_repository_.loadProviderState().instances`, add each enabled one through
`withUtilityGenerationParams()` + `ProviderAdapterRouter::add()`, and register it with its own
`ProviderRuntimeCoordinator` for credential-gated refresh. This is the pattern's fourth independent
instantiation (`ChatViewModel`, `UtilityTaskRunner`, `ProviderManagementController`, now this) — see §9 for
why that's still the right call rather than a factor-out.

`defaultModelNames`/`titleOverrideModelNames` are recomputed (and their `Changed` signal emitted) whenever:
(a) the paired `*ProviderId` setter runs, reading `router_->availableModels(id)` synchronously if already
cached, or an empty list pending refresh; (b) `runtime_coordinator_`'s `providerChanged` signal fires for the
currently-selected instance ID in either picker (mirrors `ChatViewModel::onProviderChanged()`'s "only
re-sync if it's the ID currently in play" filter). This is what makes REQ-F-005/REQ-NF-004 concretely true:
switching `ChatViewModel::selectedProviderId` (the chat window's own dropdown) never touches this
controller's `router_`/`runtime_coordinator_` at all — the two are fully separate object graphs.

### 7.2 `save()` (REQ-F-020, REQ-C-001)

```cpp
bool UtilitySettingsController::save() {
  // REQ-F-020 validation: a selected override whose provider instance is no longer enabled/present
  // is silently dropped (reset to the null sentinel) rather than blocking Save — consistent with
  // REQ-F-021's "log + fall back, never hard-fail" posture elsewhere in this feature.
  reconcileDraftAgainstProviderState();

  const auto result = config_repository_.saveUtilityConfig(draft_);   // single write (§0)
  if (!result.has_value()) {
    setSaveNotice(tr("Failed to save: %1").arg(result.error()), QStringLiteral("error"));
    return false;
  }

  saved_ = draft_;
  auto* chat = qml_engine_->singletonInstance<ChatViewModel*>("HolonightChat", "ChatViewModel");
  Q_ASSERT(chat != nullptr);
  if (auto* runner = chat->utilityTaskRunnerForSettings()) {
    runner->applyUtilityConfig(saved_);   // REQ-F-020: live effect without restart
  }
  setSaveNotice(tr("Saved"), QStringLiteral("success"));
  emit draftChanged();
  return true;
}

void UtilitySettingsController::discardDraft() {
  draft_ = saved_;
  emit draftChanged();
}
```

`saveNotice`/`saveNoticeStatus` reuse the exact naming `ProviderSettingsController` already exposes and
`OllamaSettingsPanel.qml` already binds (`ollama_settings_panel.qml:167-168`), so the new panel's footer
notice Text element is a drop-in copy of the existing one.

### 7.3 Registration

Added to `holonight_application`'s existing `QML_SINGLETON` set exactly like `ProviderSettingsController`/
`OpenAIProviderSettingsController` — `apps/chat/CMakeLists.txt`'s `qt6_extract_metatypes(holonight_application ...)`
+ `combine-metatypes.cmake` pipeline already walks every `QML_ELEMENT`/`QML_SINGLETON` type in that static
library, so no CMake edit is needed (REQ-C-006). Remember the double-clear-of-`INTERFACE_SOURCES` gotcha
documented in CLAUDE.md if a future maintainer touches that macro invocation while implementing this.

---

## 8. QML

### 8.1 Sidebar section selection (REQ-F-001)

`SettingsSidebar.qml` today has no selection state — add one:

```qml
HnSurfaceFrame {
    id: root
    property string currentSection: "providers"
    signal sectionSelected(string sectionId)

    readonly property var sections: [
        { "id": "general",       "name": qsTr("General"),       "icon": "general",        "enabled": false },
        { "id": "providers",     "name": qsTr("Providers"),     "icon": "providers",      "enabled": true  },
        { "id": "background-ai", "name": qsTr("Background AI"), "icon": "background-ai",  "enabled": true  }, // NEW
        { "id": "models",        "name": qsTr("Models"),        "icon": "models",         "enabled": false },
        ... // rest unchanged
    ]
    ...
    delegate: HnNavigationDelegate {
        ...
        checked: entryDelegate.modelData.enabled && entryDelegate.modelData.id === root.currentSection
        onClicked: if (entryDelegate.modelData.enabled) root.sectionSelected(entryDelegate.modelData.id)
    }
}
```

(`HnNavigationDelegate` extends `HnSelectableDelegate`, which already exposes `onClicked`/`hovered` —
confirmed in `holonight-qt/qml/controls/HnNavigationDelegate.qml`; no new control needed.)

`SettingsWindow.qml` gains the matching state and swaps its content area:

```qml
HnApplicationWindow {
    property string currentSection: "providers"
    ...
    RowLayout {
        SettingsSidebar {
            currentSection: root.currentSection
            onSectionSelected: id => root.currentSection = id
        }

        ProviderListPanel {
            visible: root.currentSection === "providers"
            ...
        }
        ProvidersPage {
            visible: root.currentSection === "providers"
            ...
        }
        BackgroundAiSettingsPanel {
            visible: root.currentSection === "background-ai"
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
}
```

`BackgroundAiSettingsPanel` occupies the combined width of `ProviderListPanel` + `ProvidersPage` (no
per-item list panel — there's only one Background AI "page," not a collection of instances), so it should be
`Layout.fillWidth: true` rather than mimicking either fixed-width sibling.

The `onClosing`/dirty-navigation-prompt guard in `SettingsWindow.qml` (`root.providerController.dirty`) is
scoped to `ProviderManagementController` only. This design leaves the Background AI panel's own dirty state
**out of** that shared close-guard for this cycle (see §11) — closing the Settings window while Background
AI has unsaved changes silently discards them, same as clicking Discard. REQ-F-018/019 only require
Discard/Save to work explicitly, not a close-time prompt, so this is in-scope-compliant but worth flagging.

### 8.2 `BackgroundAiSettingsPanel.qml` (new)

Structural shell borrows `ProviderSettingsScaffold`'s *layout skeleton* (header `RowLayout` → scrollable
`HnSurfaceFrame`/`Card` → `HnActionBar` footer with notice + Save/Discard) but is its own top-level
`HnSurfaceFrame` rather than an instantiation of `ProviderSettingsScaffold`, because that component's
`providerController` contract (name field, enable switch, delete button/dialog) doesn't fit a
non-instance settings surface — see §2's finding and §9's rationale for the resulting REQ-C-002 deviation.

```qml
HnSurfaceFrame {
    id: root
    surfaceRole: HnSurfaceRole.Window

    ColumnLayout {
        anchors.fill: parent
        Text { text: qsTr("Background AI"); font.bold: true /* header, no enable switch, no delete */ }

        ScrollView {
            Layout.fillWidth: true; Layout.fillHeight: true
            HnSurfaceFrame {
                surfaceRole: HnSurfaceRole.Card
                ColumnLayout {
                    // "Default utility model" section (REQ-F-003/004/005/006)
                    Text { text: qsTr("Default utility model") }
                    ProviderModelPicker {
                        providerInstances: UtilitySettingsController.providerInstances
                        selectedProviderId: UtilitySettingsController.defaultProviderId
                        modelNames: UtilitySettingsController.defaultModelNames
                        selectedModelName: UtilitySettingsController.defaultModelName
                        onProviderSelected: id => UtilitySettingsController.defaultProviderId = id
                        onModelSelected: name => UtilitySettingsController.defaultModelName = name
                    }

                    // "Chat titles" section (REQ-F-007/008/009/010/011)
                    Text { text: qsTr("Chat titles") }
                    Switch {
                        text: qsTr("Automatically generate titles")
                        checked: UtilitySettingsController.chatTitleGenerationEnabled
                        onToggled: UtilitySettingsController.chatTitleGenerationEnabled = checked
                    }
                    Text { text: qsTr("Model override for this feature") }
                    ProviderModelPicker {
                        enabled: UtilitySettingsController.chatTitleGenerationEnabled
                        providerInstances: UtilitySettingsController.providerInstances
                        selectedProviderId: UtilitySettingsController.titleOverrideProviderId
                        modelNames: UtilitySettingsController.titleOverrideModelNames
                        selectedModelName: UtilitySettingsController.titleOverrideModelName
                        onProviderSelected: id => UtilitySettingsController.titleOverrideProviderId = id
                        onModelSelected: name => UtilitySettingsController.titleOverrideModelName = name
                    }
                }
            }
        }

        HnActionBar {
            centerContent: Component {
                Text {
                    text: UtilitySettingsController.saveNotice
                    color: /* same status-color ternary as OllamaSettingsPanel.qml:152-155 */
                }
            }
            trailingContent: Component {
                RowLayout {
                    Button {
                        text: qsTr("Discard")
                        enabled: UtilitySettingsController.dirty
                        onClicked: UtilitySettingsController.discardDraft()
                    }
                    ProviderActionButton {
                        text: qsTr("Save changes")
                        highlighted: true
                        enabled: UtilitySettingsController.canSave
                        onClicked: UtilitySettingsController.save()
                    }
                }
            }
        }
    }
}
```

No "instance name" field, no enable `Switch` in the header, no delete button/dialog, no
`ProviderFormActionRow`'s trailing action-button slot (no "Test connection"/"Refresh models" action button
at this level — model refresh is triggered implicitly by `ProviderModelPicker` selecting a provider
instance). `HnFormField`/`HnIconComboBox` are still reused *inside* `ProviderModelPicker` for label+combo
styling consistency, matching REQ-C-002's spirit even where the outer scaffold isn't literally reused.

### 8.3 `ProviderModelPicker.qml` (new, reusable)

```qml
ColumnLayout {
    id: root
    property var providerInstances: []      // [{id, name}], includes the "Use chat model" sentinel
    property string selectedProviderId: ""
    property string selectedModelName: ""
    property var modelNames: []
    signal providerSelected(string id)
    signal modelSelected(string name)

    HnFormField {
        labelText: qsTr("Provider instance")
        HnIconComboBox {
            model: root.providerInstances
            textRole: "name"
            valueRole: "id"
            currentIndex: indexOfValue(root.selectedProviderId)
            onActivated: index => root.providerSelected(valueAt(index))
        }
    }
    HnFormField {
        labelText: qsTr("Model")
        visible: root.selectedProviderId.length > 0
        HnIconComboBox {
            model: root.modelNames
            currentIndex: find(root.selectedModelName)
            onActivated: index => root.modelSelected(textAt(index))
        }
    }
}
```

Used twice per REQ-F-010 ("identical UI structure to the Default utility model section") — a single
component definition guarantees that identity rather than relying on two hand-copied blocks staying in
sync.

### 8.4 Icon asset (REQ-F-002)

`assets/icons/settings-background-ai.svg`, matching the sibling icons' convention exactly: `viewBox="0 0 24
24"`, a `.ColorScheme-Text { color: #232629; }` style block, `stroke="currentColor" stroke-width="1.8"
stroke-linecap="round" stroke-linejoin="round"`, no fill. Suggested glyph: a small chip/circuit-node shape
(rounded rect with 2-3 short leader lines) to read as "background process," distinct from
`settings-providers.svg`'s two stacked server-rack bars. Exact path data is an implementation-stage detail,
not a design decision.

---

## 9. Key decisions & rationale

1. **Two runner-side apply methods, not one.** REQ-F-020's acceptance criterion names
   `applyProviderState()` as *the* update mechanism, but that method's job (§5.1: sync the router to the
   provider roster) is orthogonal to swapping the config values a save actually changes (§5.2). Conflating
   them into one method would force `UtilitySettingsController::save()` to also pass a full
   `ProviderState` it doesn't own or modify, or force `ChatViewModel::applyProviderState()` to take an
   `UtilityConfig` parameter it doesn't have on hand when provider settings (not Background AI settings)
   are what changed. Two small, single-purpose methods composed at the two real call sites (provider-state
   commit; Background AI save) is simpler than one method serving both.
2. **`chat_title_model_override` tier logic lives inside `resolveModel()`, keyed off the config field, not
   a `task_type` parameter.** `resolveModel()` has exactly one caller (`requestTitleGeneration`) today, so a
   generic task-type dispatch would be speculative generality for a currently-single-task subsystem. If a
   second utility task type is ever added, this is the natural place to introduce the parameter — revisit
   then, not now.
3. **`UtilitySettingsController` owns a fourth independent `ProviderAdapterRouter`+`ProviderRuntimeCoordinator`
   pair** rather than sharing `ProviderManagementController`'s or `ChatViewModel`'s. This is deliberate
   duplication in service of REQ-NF-004's independence requirement (§7.1) and matches this codebase's
   already-established "rule of three" precedent (per `MEMORY.md`'s Google-provider-cycle note: shared
   interface/abstraction only gets introduced once a *fourth* real need appears, and even then only if the
   duplication is actually costly). Here it's the fourth instantiation of the exact same
   router-plus-coordinator pattern; a shared `ProviderRuntimeFacade`-style extraction is now arguably
   justified on repetition-count grounds alone, but is out of scope for this cycle (no spec requirement
   calls for it, and it would touch three already-stable, well-tested classes for a refactor with no new
   behavior). Flagged as a candidate follow-up in §11, not undertaken here.
4. **`BackgroundAiSettingsPanel` does not instantiate `ProviderSettingsScaffold`.** See §2's finding — the
   scaffold's `providerController` contract assumes an instance-CRUD controller shape
   (`displayName`/`enabled`/`canDelete`/`deletionExplanation`) that `UtilitySettingsController` has no
   reason to implement. Building fake versions of those properties (e.g. `canDelete: false` always,
   `deletionExplanation: ""` always) purely to satisfy the scaffold's bindings was rejected as more
   confusing than a small bespoke top-level `HnSurfaceFrame` that mirrors the *visible* structure
   (header/scroll-card/action-bar) without the CRUD-specific parts. REQ-C-002's literal wording ("including a
   `ProviderSettingsScaffold` wrapper") is satisfied in spirit (draft+Save/Discard convention, `HnFormField`
   usage, footer notice styling) but not the letter; documented here per this cycle's instruction to
   describe deviations explicitly rather than silently drift from the spec text.
5. **Provider-instance deletion cleanup extends the existing single-write pattern rather than adding a
   second write.** `ProviderInstanceDeleter::remove()` already special-cases "does this deletion invalidate
   `default_utility_model`" to choose between `saveProviderState()` and
   `saveProviderStateAndUtilityConfig()`. Extending the same boolean (`clearUtility`) to also check
   `chat_title_model_override` keeps the file at one write in the common case and preserves the atomicity
   guarantee (§0) rather than introducing a follow-up `saveUtilityConfig()` call that could, in principle,
   race with something else touching `config.json` between the two writes (unlikely given
   `ConfigRepository` is GUI-thread-synchronous, but avoidable at zero cost by not introducing the second
   write at all).

---

## 10. Alternatives considered

- **Route `chat_title_model_override` resolution through a `task_type` enum parameter on `resolveModel()`
  now, anticipating future task types.** Rejected per decision 2 above — no second task type exists yet to
  validate the enum's shape against; adding it speculatively risks guessing wrong and having to redesign
  once conversation compaction (explicitly out of scope, REQ-C-003) eventually needs its own utility task.
- **Give `UtilitySettingsController` a close-time dirty-navigation guard identical to
  `ProviderManagementController`'s (`guard_`/`ProviderDraftGuard`/confirmation dialog).** Rejected for this
  cycle: `ProviderDraftGuard` is built around *navigating between provider instances* mid-edit, a concept
  Background AI's single-page panel doesn't have (there's nothing to navigate away *to* within the panel).
  A simpler "closing the Settings window while dirty" prompt could be added later by reusing
  `SettingsWindow.qml`'s existing `onClosing` handler and OR-ing in `UtilitySettingsController.dirty`, but
  REQ-F-018/019 don't require it and no acceptance criterion tests for it — left as an open question in §11
  rather than built speculatively.
- **Reuse `ProviderManagementController`'s already-built `router_` for the settings pickers' model
  listing**, avoiding a fourth router instance. Rejected — see decision 3; the independence requirement
  (REQ-NF-004) and the utility-param injection requirement (REQ-F-016, which that router doesn't apply)
  both argue against sharing it.
- **Store `chat_title_generation_enabled` as a plain `bool` (default `true`) instead of
  `std::optional<bool>`.** Rejected because the spec explicitly types it as `std::optional<bool>`
  (REQ-F-008) precisely so `loadUtilityConfig()` can distinguish "field absent in a legacy config" from
  "field present and `false`" — a plain `bool` member initialized to `true` would make that distinction
  impossible to observe from the loaded struct alone (both cases would just read `true`/default), which
  matters for REQ-NF-002's acceptance criterion ("a config lacking the field behaves as if set to true").

---

## 11. Risks / open questions

1. **Live-open-panel reconciliation on external provider deletion is best-effort, not push-based.** If the
   Background AI panel is open with a draft referencing provider instance X, and X is deleted via the
   Providers section in the same session, `UtilitySettingsController`'s in-memory `draft_`/`providerInstances`
   are not proactively updated (no signal wiring between `ProviderManagementController`/`ProviderInstanceDeleter`
   and `UtilitySettingsController` is proposed in this design). The picker will still show the
   now-stale/deleted instance until the panel is reopened or Save is clicked (Save's
   `reconcileDraftAgainstProviderState()` step, §7.2, will correct it then). Given REQ-F-017's acceptance
   criterion is phrased as "does not crash" + "falls back," and the persisted config is already corrected
   proactively by `ProviderInstanceDeleter` (§6.3) regardless of whether the panel is open, this is judged
   acceptable for this cycle but should be confirmed with the user/product owner before implementation
   starts, since it's a plausible source of "why does the panel still show a provider I deleted" bug reports.
2. **`kAnthropicUtilityMaxOutputTokens`/`kGoogleUtilityMaxOutputTokens` values (64/1024) are carried over
   unchanged from the existing legacy-path constants**, not re-derived from REQ-F-016's qualitative "small
   max-tokens" wording. Worth a product sanity check that 64 tokens is still an appropriate ceiling for
   title generation now that the router path (real multi-instance configs) will finally enforce it too —
   previously this ceiling only applied to the never-really-exercised no-instances fallback path.
   Ollama/OpenAI utility calls get temperature isolation but no max-tokens cap (neither config struct has
   the field) — same gap already existed pre-cycle, not introduced by this design, but worth naming.
3. **§9 decision 3's "fourth instantiation of the same router pattern" observation is a legitimate
   refactor candidate** (`ChatViewModel`, `UtilityTaskRunner`, `ProviderManagementController`,
   `UtilitySettingsController` all now independently build "router + coordinator from `ProviderState`, with
   per-instance HTTP client construction and cached-model restoration"). Not undertaken here per the
   "rule of three (or four)" convention already used in this codebase, but flagged so Stage 3 (task
   breakdown) can decide whether to fold a small `ProviderRouterFactory` helper into this cycle's task list
   or explicitly defer it.
4. **No close-time dirty guard for the Background AI panel** (§10) — confirm with the user whether silently
   discarding unsaved Background AI changes on window-close (vs. the Providers section's explicit
   save/discard/cancel prompt) is acceptable, or whether `SettingsWindow.qml`'s existing `onClosing` guard
   should be extended to also check `UtilitySettingsController.dirty`.
5. **REQ-F-020's "validate that all draft state is consistent" is implemented as silent auto-correction**
   (§7.2: invalid references are reset to the null sentinel, not surfaced as a blocking validation error).
   If product wants an explicit "your title-override provider was removed, please reselect" notice instead
   of silent reset-on-save, `saveNotice`/`saveNoticeStatus` already has the plumbing to carry that message —
   this is a one-line change in `reconcileDraftAgainstProviderState()`'s wording, not a structural one, but
   worth confirming the desired UX before implementation.
