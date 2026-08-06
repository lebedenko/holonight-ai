# SDD Tasks — google-provider-adapter

## Foundational Configuration & Storage

- [x] T-001: Add `GoogleProviderConfig` struct to `provider_config.h`
  - REQs: REQ-F-034, REQ-F-036
  - Check: Struct defined with `base_url`, `default_model`, `temperature` (default 1.0), `max_output_tokens` (default 1024), and defaulted equality operator; no auth-key field present.

- [x] T-002: Implement `ConfigRepository::loadGoogleConfig()` and `saveGoogleConfig()`
  - REQs: REQ-F-035, REQ-F-037
  - Check: Both methods follow existing Anthropic pattern exactly (load returns defaults on missing/malformed file; save uses read-merge-write under `"providers"."google"` key without clobbering other providers).

## Core Provider Adapter

- [x] T-003: Create `GoogleProvider` class header and declare interface
  - REQs: REQ-F-001, REQ-F-025, REQ-F-026, REQ-F-040
  - Check: Header file at `src/providers/include/holonight_providers/google_provider.h` declares all methods (`sendChat()`, `refresh()`, `availableModels()`, `setBaseUrl()`, `setAuthKey()`, `setTemperature()`, `setMaxOutputTokens()`); return types and signatures match `AnthropicProvider` template exactly.

- [x] T-004: Implement request construction in `GoogleProvider::sendChat()`
  - REQs: REQ-F-001, REQ-F-002, REQ-F-003, REQ-F-004, REQ-F-005, REQ-F-006, REQ-F-007
  - Check: POST body contains `contents` array with `role: "user"` for User messages and `role: "model"` for Assistant messages (not `"assistant"`); System messages hoisted to top-level `systemInstruction` field (omitted if zero System messages); `generationConfig.temperature` and `generationConfig.maxOutputTokens` present; URL path is `{base_url}/v1beta/models/{model}:streamGenerateContent?alt=sse` (not `:generateContent`); `x-goog-api-key` header sent only when credential present.

- [x] T-005: Implement SSE streaming parser in `GoogleProvider::sendChat()`
  - REQs: REQ-F-012, REQ-F-013, REQ-F-014, REQ-F-015, REQ-F-018, REQ-F-019
  - Check: `StreamContext` buffer accumulates fragmented chunks; `findSseBlockDelimiter()` and `processSseBlock()` extract blank-line-delimited `data: {...}` blocks; `candidates[0].content.parts[0].text` emits `ContentDelta` event; `finishReason` terminal values emit `Completed`; error blocks/malformed JSON/stream-without-completion emit `Error`.

- [x] T-006: Implement safety/recitation error handling in SSE parser
  - REQs: REQ-F-016, REQ-F-024
  - Check: `finishReason: "SAFETY"` emits `Error` with message "Response blocked by Google's safety filters."; `finishReason: "RECITATION"` emits `Error` with message "Response blocked due to recitation concerns."; no silent suppression.

- [x] T-007: Implement model discovery endpoint (`GET /v1beta/models`) and denylist filtering
  - REQs: REQ-F-008, REQ-F-009, REQ-F-010, REQ-F-011
  - Check: `refresh()` makes GET request to `{base_url}/v1beta/models` with `x-goog-api-key` header; filters by denylist substrings (`embedding`, `moderation`, `vision`, `ocr`) AND requires `supportedGenerationMethods` to include `"generateContent"`; strips `"models/"` prefix from returned model names; future models not matching denylist and supporting `generateContent` pass through.

- [x] T-008: Implement cancellation support in `GoogleProvider`
  - REQs: REQ-F-025, REQ-F-026
  - Check: `sendChat()` returns `HttpRequestHandlePtr` (same type as siblings); returned handle's `cancel()` method halts streaming and prevents further event callbacks.

## Application Layer Integration

- [x] T-009: Update `ChatController` constructor and routing for fourth provider
  - REQs: REQ-F-040 (implied)
  - Check: Constructor takes fourth `shared_ptr<GoogleProvider>` parameter; `hasModelsFor()` includes `if (provider_id == "google") return !google_provider_->availableModels().empty();`; `dispatchSendChat()` includes `if (model.provider_id == "google") return google_provider_->sendChat(...)`.

- [x] T-010: Update `ChatViewModel` constructor and model syncing for fourth provider
  - REQs: REQ-F-040 (implied)
  - Check: Constructor takes fourth `shared_ptr<GoogleProvider>` parameter; `create()` factory loads `GoogleProviderConfig`, constructs `GoogleProvider` with loaded settings, passes it to `ChatViewModel` constructor; `syncAvailableModels()` unions four providers' lists; `syncAvailableGoogleModels()` added and called via settings-controller callback.

- [x] T-011: Implement `GoogleProviderSettingsController` as subclass of `ProviderSettingsControllerBase`
  - REQs: REQ-F-027, REQ-F-028, REQ-F-029, REQ-F-031, REQ-F-038, REQ-F-041, REQ-F-042
  - Check: Class inherits `ProviderSettingsControllerBase`; owns `temperature_` (0.0–2.0 validated in `save()`), `max_output_tokens_`, `googleConnectionStatus` Q_PROPERTY; implements `load()`, `save()`, `cancel()`, `resetToDefaults()`; registered as `QML_SINGLETON`; `create()` method resolves shared provider and probe provider from singletons; passes `ProviderOperations` struct with five closures to base class.

- [x] T-012: Implement credential operations in `GoogleProviderSettingsController`
  - REQs: REQ-F-029, REQ-F-030, REQ-F-031, REQ-F-032, REQ-F-038, REQ-F-039
  - Check: `saveCredential()` (base-class method) calls `credential_store_->store("google", token)` on Save; `onCredentialRemoved()` (base-class callback) calls `credential_store_->remove("google")` when key cleared; missing credential results in no `x-goog-api-key` header; unavailable credential store disables API key field in UI.

- [x] T-013: Implement test-connection and model-refresh operations in `GoogleProviderSettingsController`
  - REQs: REQ-F-008, REQ-F-033
  - Check: `testConnection()` (base-class method) issues `GET /v1beta/models` with current base URL and key; success displays "Connection successful"; error displays extracted Google error message; `refreshModels()` (base-class method) calls `probe_provider_->refresh()` and updates ComboBox via `operations_.probe_models()` closure.

## QML User Interface

- [x] T-014: Create `qml/workspace/GoogleSettingsPanel.qml`
  - REQs: REQ-F-027, REQ-F-028, REQ-F-029, REQ-F-030, REQ-F-031, REQ-F-032, REQ-F-033
  - Check: Panel loads with base URL, model ComboBox, temperature Slider/SpinBox (range 0.0–2.0, default 1.0), max-output-tokens SpinBox, API key TextField with show/hide toggle and Clear button, Test Connection button, Refresh models button, Save/Cancel/Reset buttons; binds to `GoogleProviderSettingsController` properties and methods; displays secret-storage-unavailable notice when applicable.

- [x] T-015: Update `qml/workspace/ProvidersPage.qml` to route "Google" provider
  - REQs: REQ-F-027 (implied)
  - Check: `Loader.sourceComponent` switch includes `case "Google": return googlePanel` (before `default:`); `googlePanel` Component loads `GoogleSettingsPanel {}`.

- [x] T-016: Update `qml/workspace/ProviderListDelegate.qml` to show Google connection status
  - REQs: REQ-F-027 (implied)
  - Check: `isGoogle` property added; `statusColor` binding includes `if (root.isGoogle)` branch reading `GoogleProviderSettingsController.googleConnectionStatus` and mapping "connected"/"error"/default to palette colors.

## CMake Build System

- [x] T-017: Add `GoogleProvider` sources to `src/providers/CMakeLists.txt`
  - REQs: (build infrastructure)
  - Check: `add_library(holonight_providers)` source list includes `include/holonight_providers/google_provider.h` and `src/google_provider.cpp`; no new `add_library()`, no new target.

- [x] T-018: Add `GoogleProviderSettingsController` sources to `src/application/CMakeLists.txt`
  - REQs: (build infrastructure)
  - Check: `add_library(holonight_application)` source list includes `include/holonight_application/google_provider_settings_controller.h` and `src/google_provider_settings_controller.cpp`; existing `chat_controller.*` and `chat_view_model.*` entries remain unchanged.

- [x] T-019: Add test sources to `tests/CMakeLists.txt`
  - REQs: (build infrastructure, Corrections to SPEC.md #2)
  - Check: `add_executable(test_holonight_ai)` source list includes `providers/test_google_provider.cpp` and `application/test_google_provider_settings_controller.cpp`; **no new `add_executable()`, no new `gtest_discover_tests()` call**.

## Unit & Integration Tests

- [x] T-020: Implement `tests/providers/test_google_provider.cpp` with request/response tests
  - REQs: REQ-NF-001, REQ-NF-002, REQ-F-001, REQ-F-002, REQ-F-003, REQ-F-004, REQ-F-005, REQ-F-006, REQ-F-007
  - Check: Tests verify request construction (role mapping, system-instruction hoisting, generationConfig fields, URL path with `:streamGenerateContent?alt=sse` suffix and no top-level `"model"` body field, `x-goog-api-key` header presence/absence per credential availability); uses `FakeHttpClient` with no real network calls.

- [x] T-021: Implement SSE parsing and event-mapping tests in `test_google_provider.cpp`
  - REQs: REQ-NF-001, REQ-NF-002, REQ-F-012, REQ-F-013, REQ-F-014, REQ-F-015, REQ-F-016, REQ-F-018, REQ-F-019
  - Check: Tests deliver fragmented chunks splitting mid-JSON; verify five distinct SSE blocks parsed correctly; verify `ContentDelta` events from `candidates[0].content.parts[0].text`; verify `Completed` from terminal `finishReason`; verify safety/recitation blocks emit distinct error messages; verify stream-without-completion emits `Error`.

- [x] T-022: Implement model-filtering tests in `test_google_provider.cpp`
  - REQs: REQ-NF-001, REQ-NF-002, REQ-F-009, REQ-F-010, REQ-F-011
  - Check: Mock `GET /v1beta/models` response with denylisted IDs, allowed IDs with `supportedGenerationMethods: ["generateContent"]`, IDs with only non-chat methods, IDs missing the field, and fail-open future-model probe; assert only non-denylist IDs supporting `generateContent` appear; assert `"models/"` prefix stripped.

- [x] T-023: Implement error-handling tests in `test_google_provider.cpp`
  - REQs: REQ-NF-001, REQ-NF-002, REQ-F-020, REQ-F-021, REQ-F-022, REQ-F-023, REQ-F-024
  - Check: Tests mock 401/429/400 responses with error bodies; verify extracted `error.message` appears in `Error` events; verify timeout/connection-refused emits `Error`; verify no unhandled exceptions; verify safety-filter blocks emit distinct messages.

- [x] T-024: Implement `tests/application/test_google_provider_settings_controller.cpp`
  - REQs: REQ-NF-001, REQ-NF-002, REQ-F-028, REQ-F-029, REQ-F-030, REQ-F-031, REQ-F-032, REQ-F-033
  - Check: Fixture mocks `ProviderSettingsControllerBase` and credential store; tests `load()`/`save()`/`cancel()`/`resetToDefaults()` operations; verifies temperature validation range [0.0, 2.0] enforced in `save()`; verifies `maxOutputTokens` round-tripping; verifies credential store operations; mirrors `test_anthropic_provider_settings_controller.cpp` structure.

- [x] T-025: Update `tests/application/test_chat_controller.cpp` for fourth provider
  - REQs: REQ-NF-001, REQ-NF-002
  - Check: Fixture adds `makeGoogleProvider()` helper; all `ChatController` constructor calls updated with fourth parameter; new test cases verify `ModelId{provider_id: "google", ...}` routes to Google provider's `FakeHttpClient`.

- [x] T-026: Update `tests/config/test_config_repository.cpp` for Google config
  - REQs: REQ-NF-001, REQ-NF-002, REQ-F-035, REQ-F-037
  - Check: New round-trip tests for `loadGoogleConfig()`/`saveGoogleConfig()` including `max_output_tokens`; multi-provider regression test saves Ollama/OpenAI/Anthropic/Google in sequence and verifies all four coexist in `config.json`.

## Code Quality & Build Verification

- [x] T-027: Run full build, test suite, format check, and linting
  - REQs: REQ-NF-001, REQ-NF-002, REQ-NF-003, REQ-NF-004
  - Check: `task build` completes without errors; `task test` passes all tests including new `test_holonight_ai` cases; `task format-check` reports no violations; `task tidy` reports no clang-tidy warnings; code follows C++23 style, CamelCase classes, camelBack functions, lower_case members.
