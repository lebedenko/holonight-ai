# Background AI Settings Panel — Requirements Specification

## Context

The holonight-chat application already operates a background `UtilityTaskRunner` subsystem that executes AI tasks isolated from interactive chat — currently used only for automatic conversation-title generation. The system has a `ProviderAdapterRouter` pattern (already proven in `ChatViewModel`) for managing multiple provider instances at runtime, but `UtilityTaskRunner` currently routes provider calls via hardcoded string literals instead of using the router, causing silent misrouting for any provider instance with a UUID-style instance ID.

This specification formalizes the requirements to:
1. Expose background AI configuration in a new Settings panel,
2. Let users select a default provider instance and model for utility tasks,
3. Add a per-feature model override for chat-title generation,
4. Fix the provider routing bug in `UtilityTaskRunner` by adopting `ProviderAdapterRouter`,
5. Maintain isolation of utility-specific generation parameters from chat's own configured parameters.

---

## Non-Goals

The following items are **explicitly out of scope** for this iteration and shall not be implemented:

1. **Conversation compaction** — No UI, backend, or task orchestration for conversation compaction is built, started, or required this cycle.
2. **"Provider boundaries" configuration section** — The mockup included a section for provider-boundary settings; this is deferred and not a requirement.
3. **Local model scheduling configuration** — The mockup included local-model scheduling; this is deferred and not a requirement.
4. **"Generate sample title" preview button** — A UI button to generate a title sample and preview it requires a new non-conversation-bound generation path; this is deferred.
5. **"Run after" timing-selection control** — Only one timing trigger exists today (post-stream-completion); a selector UI for a single option is non-essential and deferred.

---

## Functional Requirements

### Sidebar & Navigation

**REQ-F-001** (Ubiquitous)
The system shall display a new "Background AI" entry in the Settings sidebar (`SettingsSidebar.qml`) with `enabled: true`, making it selectable like the "Providers" section today.

- **Acceptance Criterion:** Clicking "Background AI" in the sidebar navigates to the Background AI settings panel; the entry is not badged "Soon" and does not redirect to a disabled state.

**REQ-F-002** (Ubiquitous)
The system shall provide an icon asset file `assets/icons/settings-background-ai.svg` for the "Background AI" sidebar entry, following the same visual style and naming convention as existing settings-section icons.

- **Acceptance Criterion:** The sidebar renders the icon without distortion or error; the asset is an SVG matching the visual scale and color of sibling icon assets.

### Default Utility Model Selection

**REQ-F-003** (Ubiquitous)
The system shall display a "Default utility model" settings section within the Background AI panel, containing a two-tier picker: (1) a provider-instance selector, and (2) a model-name selector for the selected provider instance.

- **Acceptance Criterion:** Both pickers are populated and interactive; selecting a provider instance updates the model list to show only models available from that instance.

**REQ-F-004** (Ubiquitous)
The system shall include an explicit "Use chat model" option in the provider-instance picker of the Default utility model section, representing an unset/null state (no specific utility model selected; fall back to the current chat window's selected model).

- **Acceptance Criterion:** Selecting "Use chat model" clears the `UtilityConfig::default_utility_model`, causes the UI picker to reset to the null state, and `UtilityTaskRunner::resolveModel()` correctly returns the chat model when invoked with a null default.

**REQ-F-005** (Conditional)
If a provider instance is selected in the Default utility model picker, the system shall query and display the model names available from that specific provider instance, independent of which provider is currently selected in the chat window.

- **Acceptance Criterion:** Switching between provider instances in the picker updates the model list without changing the active chat provider; model names reflect the correct provider's actual catalog.

**REQ-F-006** (Event-driven)
When the user selects a provider instance and model from the Default utility model pickers, the system shall update the draft state of `UtilityConfig::default_utility_model` (holding the selected `ModelId`), but shall not persist the change until the user clicks the Save button.

- **Acceptance Criterion:** Changing the picker values updates the draft form state; closing the panel without clicking Save leaves `UtilityConfig::default_utility_model` in the database unchanged.

### Chat-Title Generation Toggle

**REQ-F-007** (Ubiquitous)
The system shall display a "Chat titles" settings section within the Background AI panel, containing an enable/disable toggle for automatic chat-title generation.

- **Acceptance Criterion:** The toggle is present, clickable, and its state is reflected in a new `enabled` boolean field on `UtilityConfig`.

**REQ-F-008** (Ubiquitous)
The system shall add a new `std::optional<bool> chat_title_generation_enabled` field to the `UtilityConfig` struct (default: true on new configs, preserving the existing trigger behavior if the field is absent in legacy configs).

- **Acceptance Criterion:** Reading a config from the database populates the field; new instances default to true; the field is persisted in `UtilityConfig`'s JSON serialization.

**REQ-F-009** (Event-driven)
When `UtilityConfig::chat_title_generation_enabled` is false, the system shall not invoke the chat-title generation task, even when a conversation stream completes.

- **Acceptance Criterion:** Disabling the toggle, saving, streaming a conversation to completion, and checking logs/database shows no title-generation dispatch; enabling the toggle re-activates title generation on the next stream completion.

### Chat-Title Model Override

**REQ-F-010** (Ubiquitous)
The system shall display a provider-instance and model picker (identical UI structure to the "Default utility model" section) within the "Chat titles" section, labeled "Model override for this feature," allowing the user to specify a task-specific model override that overrides the default utility model when generating titles.

- **Acceptance Criterion:** The picker is present and allows selecting a provider instance and model; the selected `ModelId` is stored separately from the default utility model.

**REQ-F-011** (Ubiquitous)
The system shall add a new `std::optional<holonight_domain::ModelId> chat_title_model_override` field to `UtilityConfig`, representing the task-specific model override for chat-title generation.

- **Acceptance Criterion:** The field is present on `UtilityConfig`; it persists to/from JSON; it can be null (no override).

**REQ-F-012** (Event-driven)
When `UtilityTaskRunner::resolveModel()` is invoked for a title-generation task (`task_type` indicates title generation), the system shall apply a three-tier resolution: (1) if a task-specific model override is set on `UtilityConfig::chat_title_model_override`, use it; (2) else if a default utility model is set on `UtilityConfig::default_utility_model`, use it; (3) else use the model currently selected in the chat window.

- **Acceptance Criterion:** Each tier is checked in order; the first non-null result is returned; logs confirm which tier was used; task dispatch logs confirm the resolved model ID is used for the actual generation.

**REQ-F-013** (Conditional)
If the user leaves the "Model override for this feature" picker in the null state, the system shall not override the default utility model for title generation; title generation shall resolve to the default utility model or fall back to the chat model, as per REQ-F-012.

- **Acceptance Criterion:** Leaving the override picker empty (null), saving, and generating a title results in the default utility model being used (or chat model if no default is set).

### Provider Routing Bug Fix

**REQ-F-014** (Ubiquitous)
The system shall refactor `UtilityTaskRunner` to own a `ProviderAdapterRouter` instance, following the same pattern as `ChatViewModel::applyProviderState()`, maintaining a working adapter for each live, enabled provider instance keyed by instance ID.

- **Acceptance Criterion:** `UtilityTaskRunner::m_router` is a `ProviderAdapterRouter`; the router is updated whenever live provider state changes (via `applyProviderState()`); `dispatchSendChat()` resolves the provider adapter from the router by instance ID instead of string-literal comparison.

**REQ-F-015** (Event-driven)
When `UtilityTaskRunner::dispatchSendChat()` is called with a `ModelId`, the system shall use the `ProviderAdapterRouter` to resolve the adapter for the model's `provider_id` (instance ID), ensuring that UUID-style instance IDs and legacy literal-string instance IDs are routed correctly to their registered adapters.

- **Acceptance Criterion:** Dispatching a task with a UUID-style provider instance ID routes to the correct provider adapter (not silently to Ollama); logs confirm the resolved adapter's type matches the provider instance's declared type.

**REQ-F-016** (Ubiquitous)
The system shall inject utility-specific generation parameters (temperature 0.3, small max-tokens) into a copy of each provider instance's configuration before adding it to the utility `ProviderAdapterRouter`, ensuring that utility calls do not share or interfere with the chat window's configured generation parameters for the same provider instance.

- **Acceptance Criterion:** A title-generation call with a provider instance uses temperature 0.3 and a small max-tokens value; the chat window's configured temperature/max-tokens for the same instance remain unchanged; switching between chat and utility calls does not carry over generation params.

**REQ-F-017** (Unwanted-behavior)
If a provider instance is deleted or disabled while referenced as the default utility model or chat-title model override, the system shall gracefully remove it from the `UtilityTaskRunner`'s router and fall back to the next tier in `resolveModel()` without crashing or logging an unhandled exception.

- **Acceptance Criterion:** Deleting a provider instance that is set as the default utility model or title override does not crash the app; subsequent task dispatch falls back to the chat model; the settings panel updates to reflect the removed instance (e.g., picker resets to null or "Use chat model").

### Save & Discard Behavior

**REQ-F-018** (Ubiquitous)
The system shall follow the existing draft + explicit Save/Discard pattern: changes to the Background AI settings panel shall be held in a draft state and not persisted until the user clicks a Save button.

- **Acceptance Criterion:** Modifying pickers, toggles, or fields, then closing the panel without saving leaves the persisted config unchanged; clicking Save persists all draft changes atomically.

**REQ-F-019** (Ubiquitous)
The system shall display a "Discard" button alongside the Save button, allowing the user to cancel all unsaved changes and revert the form to the last-saved state.

- **Acceptance Criterion:** Clicking Discard resets all pickers, toggles, and fields to their last-saved values; draft state is cleared.

**REQ-F-020** (Event-driven)
When the user clicks the Save button, the system shall validate that all draft state is consistent (e.g., if a model override is selected, its provider instance is still enabled), persist the `UtilityConfig` to the database, and immediately apply the new configuration to `UtilityTaskRunner` by calling `applyProviderState()` to update the internal router.

- **Acceptance Criterion:** Saving writes the config to the database; the next utility task dispatch uses the new settings; no error message is shown if all fields are valid.

**REQ-F-021** (Conditional)
If the persisted config becomes invalid after saving (e.g., the selected default provider instance is deleted externally), the system shall log a warning but continue operation, falling back to the next tier in `resolveModel()` without crashing.

- **Acceptance Criterion:** External deletion of a provider instance is detected on next task dispatch; a warning is logged; task dispatch gracefully falls back to the chat model.

---

## Non-Functional Requirements

**REQ-NF-001** (Isolation of generation parameters)
Utility-task generation parameters (temperature 0.3, small max-tokens) shall be isolated from the chat window's configured generation parameters for the same provider instance, such that changes to chat parameters do not affect utility tasks and vice versa.

- **Acceptance Criterion:** Temperature and max-tokens in a utility-task request match the utility-specific values (0.3, small), not the chat window's configured values; chat parameters remain unaffected by utility task dispatch.

**REQ-NF-002** (Backward compatibility of title generation)
If `UtilityConfig::chat_title_generation_enabled` is absent (legacy config), the system shall default to true (enabled), preserving existing behavior and not introducing a regression where titles stop generating for upgraded users.

- **Acceptance Criterion:** A config lacking the `chat_title_generation_enabled` field behaves as if the field is set to true; no titles are skipped after upgrade.

**REQ-NF-003** (No cascade crashes on provider deletion)
Deletion or disabling of a provider instance shall not cause a crash, null-pointer dereference, or unhandled exception in `UtilityTaskRunner`, even if that instance was referenced as the default utility model or chat-title override.

- **Acceptance Criterion:** Deleting a referenced provider instance does not crash; logs show a graceful fallback; subsequent task dispatch succeeds.

**REQ-NF-004** (Model name availability independent of chat selection)
The system shall be able to query and display model names for a provider instance without requiring that instance to be selected in the chat window, enabling the settings panel to show accurate model lists regardless of the chat window's current provider state.

- **Acceptance Criterion:** Selecting a provider instance in the settings panel's picker populates its model list correctly even if the chat window is using a different provider; switching the chat window's provider does not change the model list in the settings picker.

**REQ-NF-005** (Atomicity of config persistence)
Saving the Background AI settings panel shall persist all draft changes to `UtilityConfig` atomically in a single database transaction, ensuring that partial saves or corruption does not leave the config in an inconsistent state.

- **Acceptance Criterion:** A database transaction encompasses the entire save operation; if any field fails to persist, the entire transaction rolls back and no partial state is committed.

---

## Constraint Requirements

**REQ-C-001** (Architectural reuse)
The Background AI settings panel shall reuse the existing `ProviderAdapterRouter` pattern and update mechanism (via `applyProviderState()`) already proven in `ChatViewModel`, with no new routing paradigm or state-management pattern introduced.

- **Acceptance Criterion:** `UtilityTaskRunner` calls `applyProviderState()` to update its router; diffs are computed the same way as in `ChatViewModel`; no new routing code is added.

**REQ-C-002** (Draft + Save/Discard convention)
The Background AI settings panel shall follow the existing draft + explicit Save/Discard pattern used by other settings panels (e.g., `OllamaSettingsPanel.qml`), including a `ProviderSettingsScaffold` wrapper, form fields bound to draft properties on a controller, and explicit `save()`/`discardDraft()` actions.

- **Acceptance Criterion:** The panel uses `ProviderSettingsScaffold`, has Save and Discard buttons, and draft state is not applied to the live system until Save is clicked.

**REQ-C-003** (No conversation compaction)
The Background AI settings panel shall not include any UI, configuration options, or task orchestration for conversation compaction, even if future title-generation enhancements might benefit from it.

- **Acceptance Criterion:** No "Compact conversation" option, button, or backend task appears in the panel or in `UtilityTaskRunner`; conversation compaction is a separate, future requirement.

**REQ-C-004** (No provider-boundary configuration)
The Background AI settings panel shall not include a "Provider boundaries" section or any configuration for limiting which providers can access which conversations, even if a mockup included such a section.

- **Acceptance Criterion:** No provider-boundary picker, toggle, or option appears in the panel; this is deferred to a future iteration.

**REQ-C-005** (No local model scheduling)
The Background AI settings panel shall not include any UI for scheduling when local models are run, task queueing, or resource-aware scheduling of utility tasks, even if a mockup included such controls.

- **Acceptance Criterion:** No "Run on schedule", "Batch after", or task-timing UI appears in the panel; `UtilityTaskRunner` continues to dispatch tasks immediately on their trigger event.

**REQ-C-006** (QML type registration non-requirement)
CMake wiring for QML type registration is a non-requirement; the new `UtilitySettingsController` (or equivalent) shall be added as an additional `QML_SINGLETON` on the `holonight_application` static library using the already-proven pattern (zero new CMake changes required beyond standard `qt6_extract_metatypes()` handling).

- **Acceptance Criterion:** No new CMake macro or QML module registration code is needed; the controller is registered using existing patterns proven with `ChatViewModel`, `ProviderSettingsController`, and `OpenAIProviderSettingsController`.

**REQ-C-007** (No preview/sample generation)
The Background AI settings panel shall not include a "Generate sample title" button or any UI to preview generated titles, deferring this to a future iteration that may add non-conversation-bound generation paths.

- **Acceptance Criterion:** No "Preview", "Generate sample", or "Test" button appears in the panel; title generation is tested only by saving the settings and streaming a conversation to completion.

---

## Summary of Verifiable Acceptance Criteria

Every functional, non-functional, and constraint requirement above includes at least one independently falsifiable acceptance criterion. Key criteria to verify in testing:

1. **Navigation & UI presence:** Sidebar entry exists, icon renders, panel loads.
2. **Provider routing:** UUID-style instance IDs route correctly; legacy literal IDs still work.
3. **Model pickers:** Independent query of model names for non-chat-selected provider; fallback to chat model when no utility model is set.
4. **Title generation toggle:** Can be enabled/disabled; disabled state prevents task dispatch; logs confirm state.
5. **Model override tier resolution:** Three-tier fallback works; logs confirm which tier was used.
6. **Graceful degradation:** Deleting a referenced provider instance does not crash; fallback works.
7. **Draft + Save/Discard:** Unsaved changes do not persist; Save persists atomically; Discard reverts draft.
8. **Generation parameter isolation:** Utility tasks use 0.3 temperature, small max-tokens; chat params unaffected.
9. **No regression:** Title generation enabled by default; existing configs behave the same after upgrade.

---

**Document Version:** 1.0
**Status:** Specification (Ready for Architecture & Design phase)
