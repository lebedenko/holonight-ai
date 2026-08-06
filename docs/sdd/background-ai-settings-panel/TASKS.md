# SDD Tasks — background-ai-settings-panel

## Data Model & Persistence (Backend Foundation)

- [x] T-001: Add chat-title and model-override fields to UtilityConfig struct
  - REQs: REQ-F-008, REQ-F-011, REQ-NF-002
  - Check: `UtilityConfig` in `src/config/include/holonight_config/utility_config.h` adds `std::optional<bool> chat_title_generation_enabled` and `std::optional<holonight_domain::ModelId> chat_title_model_override` fields, both optional to support legacy config upgrade behavior.

- [x] T-002: Implement UtilityConfig JSON persistence for new fields
  - REQs: REQ-F-008, REQ-F-011, REQ-NF-002
  - Check: `ConfigRepository::applyUtilityConfig()` and `loadUtilityConfig()` round-trip both new fields to/from `config.json`; absent `chat_title_generation_enabled` field reads as `std::nullopt` and `.value_or(true)` correctly yields `true` at call sites.

## UtilityTaskRunner Correctness Fixes (Core Logic)

- [x] T-003: Add withUtilityGenerationParams() helper for parameter isolation
  - REQs: REQ-F-016, REQ-NF-001
  - Check: `src/application/src/utility_task_runner.cpp` defines namespace-scoped function that injects temperature 0.3 and provider-specific max-tokens (64 for Anthropic, 1024 for Google) into a copy of `ProviderInstanceConfig`, leaving the original unmodified.

- [x] T-004: Add UtilityTaskRunner::applyProviderState() method
  - REQs: REQ-F-014, REQ-F-015, REQ-F-016, REQ-F-017, REQ-NF-001, REQ-NF-003, REQ-C-001
  - Check: Method exists with signature `void applyProviderState(holonight_config::ProviderState provider_state)`; follows three-phase pattern (removals via `adapter_router_->remove()`, add-or-reconfigure via `withUtilityGenerationParams()`, swap `provider_state_`); ctest passes `UtilityTaskRunnerAppliesProviderState*` tests.

- [x] T-005: Extract registerRuntimeProvider() helper in UtilityTaskRunner
  - REQs: REQ-F-014
  - Check: Private method `registerRuntimeProvider(const ProviderInstanceConfig&)` extracted from constructor's inline lambda; both constructor and `applyProviderState()` call it with identical logic.

- [x] T-006: Store endpoints member in UtilityTaskRunner constructor
  - REQs: REQ-F-014
  - Check: `endpoints_` member is stored during construction so `applyProviderState()` can build HTTP clients for newly-added instances via `clientFor(instance.type, endpoints_)`.

- [x] T-007: Add UtilityTaskRunner::applyUtilityConfig() method
  - REQs: REQ-F-020
  - Check: Method signature `void applyUtilityConfig(holonight_config::UtilityConfig utility_config)` swaps the live `utility_config_` member; ctest passes `UtilityTaskRunnerAppliesConfig*` tests.

- [x] T-008: Implement three-tier model resolution in resolveModel()
  - REQs: REQ-F-012, REQ-F-013, REQ-NF-004
  - Check: `resolveModel()` checks tiers in order: (1) `taskOverride` if non-null and usable, (2) `utility_config_.chat_title_model_override` if non-null and usable, (3) `utility_config_.default_utility_model` if non-null and usable, (4) `chatFallbackModel` if usable, (5) first available model of any enabled instance; ctest passes `UtilityTaskRunnerThreeTierModelResolution*` tests.

- [x] T-009: Add title-generation enable/disable gate in requestTitleGeneration()
  - REQs: REQ-F-009, REQ-NF-002
  - Check: `requestTitleGeneration()` early-returns if `utility_config_.chat_title_generation_enabled.value_or(true)` is false; ctest passes `UtilityTaskRunnerTitleGenerationGate*` tests.

- [x] T-010: Remove dead "not implemented yet" warnings for taskOverride parameter
  - REQs: REQ-F-012
  - Check: Two `qWarning()` calls guarding unused `taskOverride` parameter are deleted from `resolveModel()` and `requestTitleGeneration()` to eliminate spurious log noise.

## ChatViewModel Wiring

- [x] T-011: Forward applyProviderState() call to UtilityTaskRunner
  - REQs: REQ-F-014, REQ-C-001
  - Check: `ChatViewModel::applyProviderState()` adds single-line call to `utility_task_runner_->applyProviderState(provider_state_)` at end of existing diff/add/reconfigure loop, ensuring router stays in lockstep with provider changes.

- [x] T-012: Add utilityTaskRunnerForSettings() plain accessor
  - REQs: REQ-F-020
  - Check: Non-Q_PROPERTY accessor `UtilityTaskRunner* utilityTaskRunnerForSettings() const` returns `utility_task_runner_.get()`; used by `UtilitySettingsController::save()` to push new config into live runner.

## Provider Deletion Integration

- [x] T-013: Extend ProviderInstanceDeleter to clear chat_title_model_override
  - REQs: REQ-F-017, REQ-NF-003
  - Check: `ProviderInstanceDeleter::remove()` checks both `utility.default_utility_model` and `utility.chat_title_model_override` against deleted instance ID; if either matches, clears it and sets `clearUtility = true` to trigger `saveProviderStateAndUtilityConfig()` atomic write (not two separate writes).

## QML Infrastructure & Navigation

- [x] T-014: Add section-selection state to SettingsSidebar
  - REQs: REQ-F-001
  - Check: `SettingsSidebar.qml` adds `property string currentSection: "providers"` and `signal sectionSelected(string sectionId)`; delegate's `checked` binding checks `root.currentSection`; "Background AI" entry added to sections array with `"id": "background-ai"` and `"enabled": true`.

- [x] T-015: Wire SettingsWindow to swap content by section
  - REQs: REQ-F-001
  - Check: `SettingsWindow.qml` adds `property string currentSection: "providers"`; existing `ProviderListPanel`/`ProvidersPage` visibility bound to `root.currentSection === "providers"`; `BackgroundAiSettingsPanel` added with `visible: root.currentSection === "background-ai"`.

## UtilitySettingsController (New QML Singleton)

- [x] T-016: Implement UtilitySettingsController class
  - REQs: REQ-F-003, REQ-F-004, REQ-F-005, REQ-F-006, REQ-F-007, REQ-F-008, REQ-F-010, REQ-F-011, REQ-F-018, REQ-F-020, REQ-NF-004, REQ-C-002, REQ-C-006
  - Check: `src/application/include/holonight_application/utility_settings_controller.h` defines `QML_SINGLETON` class with properties `providerInstances`, `defaultProviderId`, `defaultModelName`, `defaultModelNames`, `chatTitleGenerationEnabled`, `titleOverrideProviderId`, `titleOverrideModelName`, `titleOverrideModelNames`, `dirty`, `canSave`, `saveNotice`, `saveNoticeStatus` and methods `save()`, `discardDraft()`, `refreshModelsForInstance()`; owns independent `ProviderAdapterRouter` and `ProviderRuntimeCoordinator` with utility-param injection.

- [x] T-017: Implement UtilitySettingsController::save() with validation
  - REQs: REQ-F-020, REQ-F-021, REQ-C-001
  - Check: `save()` calls `reconcileDraftAgainstProviderState()` to silently drop invalid references, persists draft to `config_repository_.saveUtilityConfig()`, calls `utility_task_runner_->applyUtilityConfig()` to apply live effect, sets `saveNotice` to success message, and returns true on success.

- [x] T-018: Implement UtilitySettingsController::discardDraft()
  - REQs: REQ-F-019
  - Check: Method resets `draft_` to `saved_` and emits `draftChanged()` signal; picker state visually reverts to last-saved values.

- [x] T-019: Implement UtilitySettingsController::refreshModelsForInstance()
  - REQs: REQ-F-005, REQ-NF-004
  - Check: Method triggers `provider_runtime_coordinator_` refresh for the given instance ID; model list updates asynchronously via signal connection to `providerChanged` signal.

- [x] T-020: Wire UtilitySettingsController property setters to update draft and trigger model refresh
  - REQs: REQ-F-006, REQ-F-005
  - Check: `setDefaultProviderId()` and `setTitleOverrideProviderId()` setters clear paired model-name property (REQ-F-004 null sentinel), read `router_->availableModels(id)` synchronously if cached, emit paired `*ModelNamesChanged` signal, and emit `draftChanged()` to mark dirty state.

- [x] T-021: Register UtilitySettingsController as QML singleton
  - REQs: REQ-C-006
  - Check: Class is annotated `QML_ELEMENT` and `QML_SINGLETON`; `apps/chat/CMakeLists.txt`'s existing `qt6_extract_metatypes(holonight_application)` and `combine-metatypes.cmake` pipeline picks it up; no new CMake macros needed.

## QML Components & UI

- [x] T-022: Create ProviderModelPicker reusable component
  - REQs: REQ-F-003, REQ-F-005, REQ-F-010, REQ-NF-004
  - Check: `qml/workspace/ProviderModelPicker.qml` is a `ColumnLayout` with properties `providerInstances`, `selectedProviderId`, `selectedModelName`, `modelNames` and signals `providerSelected`, `modelSelected`; contains two `HnFormField`s with `HnIconComboBox`; model-picker visibility bound to `selectedProviderId.length > 0`.

- [x] T-023: Create BackgroundAiSettingsPanel top-level component
  - REQs: REQ-F-001, REQ-F-003, REQ-F-007, REQ-F-010, REQ-F-018, REQ-F-019
  - Check: `qml/workspace/BackgroundAiSettingsPanel.qml` is an `HnSurfaceFrame` with "Background AI" header, scrollable card containing "Default utility model" section with one `ProviderModelPicker`, "Chat titles" section with enable toggle and "Model override for this feature" section with second `ProviderModelPicker` (disabled when toggle is off), footer `HnActionBar` with `saveNotice` Text and Discard/Save buttons.

- [x] T-024: Wire BackgroundAiSettingsPanel to UtilitySettingsController
  - REQs: REQ-F-006, REQ-F-018, REQ-F-020
  - Check: Panel binds to `UtilitySettingsController` singleton: pickers bind to controller properties, Save/Discard buttons invoke controller methods, `saveNotice` text updated from controller, all changes update `draftChanged()` signal.

- [x] T-025: Create settings-background-ai icon asset
  - REQs: REQ-F-002
  - Check: `assets/icons/settings-background-ai.svg` exists with `viewBox="0 0 24 24"`, `.ColorScheme-Text { color: #232629; }` style block, `stroke="currentColor" stroke-width="1.8"` paths; icon renders without distortion in sidebar at same scale/style as sibling settings icons.

## C++ Unit Tests

- [x] T-026: Write UtilityConfig persistence tests
  - REQs: REQ-F-008, REQ-F-011, REQ-NF-002
  - Check: `tests/config/test_config_repository.cpp` adds tests verifying `chat_title_generation_enabled` and `chat_title_model_override` round-trip through JSON; legacy config without the field yields `nullopt` on load (REQ-NF-002 "absent ⇒ true" behavior); ctest passes `ConfigRepositoryUtility*` tests.

- [x] T-027: Write UtilityTaskRunner::applyProviderState() tests
  - REQs: REQ-F-014, REQ-F-015, REQ-F-016
  - Check: `tests/application/test_utility_task_runner.cpp` adds tests that: (a) verify router instance list matches newly-applied provider state after call; (b) verify UUID-style instance IDs route correctly to their adapters (not fallback to hardcoded Ollama); (c) verify utility-param injection (temperature 0.3, small max-tokens) on added instances; ctest passes `UtilityTaskRunnerAppliesProviderState*` tests.

- [x] T-028: Write UtilityTaskRunner three-tier model resolution tests
  - REQs: REQ-F-012, REQ-F-013
  - Check: `tests/application/test_utility_task_runner.cpp` adds tests verifying: (a) task override tier (if non-null, used first); (b) chat-title model override tier (if non-null, used before default); (c) default utility model tier (if non-null, used before fallback); (d) chat fallback tier; ctest passes `UtilityTaskRunnerThreeTierModelResolution*` tests.

- [x] T-029: Write UtilityTaskRunner title-generation gate tests
  - REQs: REQ-F-009
  - Check: `tests/application/test_utility_task_runner.cpp` adds test verifying that when `chat_title_generation_enabled` is `false`, `requestTitleGeneration()` returns early without queuing a generation; setting to `true` re-enables dispatch; ctest passes `UtilityTaskRunnerTitleGenerationGate*` tests.

- [x] T-030: Write ProviderInstanceDeleter extension tests
  - REQs: REQ-F-017, REQ-NF-003
  - Check: `tests/application/test_provider_instance_deleter.cpp` adds tests verifying that deleting a provider instance that is set as default utility model or title override clears both fields atomically; subsequent deletions of other instances leave the config unchanged; ctest passes `ProviderInstanceDeleterClearsUtilityConfig*` tests.

- [x] T-031: Write UtilityTaskRunner graceful degradation test
  - REQs: REQ-F-017, REQ-NF-003
  - Check: `tests/application/test_utility_task_runner.cpp` adds test verifying that after deleting a provider instance referenced in default or override, subsequent title-generation dispatch does not crash; falls back to chat model via `isUsable()` tier-skip logic; ctest passes `UtilityTaskRunnerDeletedProviderFallback*` test.

## QML & Integration Tests

- [x] T-032: Write QML module registration test for UtilitySettingsController
  - REQs: REQ-C-006
  - Check: `tests/qml/test_canonical_modules.cpp` or similar verifies that `UtilitySettingsController` singleton is registered in QML module `HolonightChat` and accessible from QML; no "unknown type" errors in qml engine load.

- [x] T-033: Write ProviderModelPicker QML component test
  - REQs: REQ-F-003, REQ-F-005
  - Check: `tests/qml/test_provider_model_picker.cpp` (new) verifies: (a) provider instance picker updates model list when selection changes; (b) model list is empty when provider instance is unset; (c) selecting "Use chat model" (null sentinel) clears both pickers; ctest passes `ProviderModelPickerBehavior*` tests.

- [x] T-034: Write BackgroundAiSettingsPanel QML integration test
  - REQs: REQ-F-001, REQ-F-018, REQ-F-019, REQ-F-020
  - Check: `tests/qml/test_background_ai_settings_panel.cpp` (new) verifies: (a) panel loads without error; (b) Save/Discard buttons enabled state tracks dirty; (c) changing pickers/toggles marks dirty without persisting; (d) clicking Discard reverts to saved values; (e) clicking Save persists and updates controller's saved state; ctest passes `BackgroundAiSettingsPanelBehavior*` tests.

- [x] T-035: Write end-to-end title generation flow test
  - REQs: REQ-F-009, REQ-F-012, REQ-F-020
  - Check: `tests/application/test_utility_task_runner.cpp` or `test_chat_view_model.cpp` adds test that: saves Background AI settings (disables title generation or sets model override), calls `requestTitleGeneration()`, verifies dispatch uses correct model from tier ladder and respects enable/disable toggle; ctest passes `UtilityTaskRunnerEndToEndTitleGeneration*` test.

---

## Summary

**Total: 35 tasks** across 5 major areas:

1. **Data Model & Persistence (2 tasks):** UtilityConfig struct fields + JSON round-trip
2. **UtilityTaskRunner Correctness Fixes (8 tasks):** Router resync, param isolation, three-tier resolution, title gate
3. **ChatViewModel Wiring (2 tasks):** Forwarding provider-state changes, accessor for settings wiring
4. **Provider Deletion Integration (1 task):** Extend cleanup to new override field
5. **QML Infrastructure (2 tasks):** Sidebar section selection, SettingsWindow content swapping
6. **UtilitySettingsController (6 tasks):** New singleton with independent router, property wiring, persistence
7. **QML Components & UI (3 tasks):** ProviderModelPicker, BackgroundAiSettingsPanel, icon asset
8. **C++ Unit Tests (6 tasks):** Config, router resync, three-tier resolution, title gate, deleter extension, graceful degradation
9. **QML & Integration Tests (5 tasks):** Module registration, component behavior, panel integration, end-to-end flow

Ordering ensures data model changes precede logic that consumes them, C++ fixes land before QML surfaces them, and tests verify each layer independently before integration.
