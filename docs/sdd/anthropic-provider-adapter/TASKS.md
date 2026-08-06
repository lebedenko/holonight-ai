# SDD Tasks — anthropic-provider-adapter

- [x] T-001: Add AnthropicProviderConfig struct to provider_config.h
  - REQs: REQ-F-033, REQ-F-035, REQ-F-036
  - Check: Struct in src/config/include/holonight_config/provider_config.h declares base_url (QString, default "https://api.anthropic.com"), default_model (QString, empty default), temperature (double, 1.0 default), max_output_tokens (int, 1024 default), and friend operator== with = default; no auth key field; compiles without warnings.

- [x] T-002: Implement ConfigRepository::loadAnthropicConfig() and saveAnthropicConfig()
  - REQs: REQ-F-034, REQ-F-036
  - Check: Methods in src/config/src/config_repository.cpp and header with signatures loadAnthropicConfig() const → AnthropicProviderConfig (never throws), saveAnthropicConfig(const AnthropicProviderConfig&) const → std::expected<void, QString>; round-trip save/load cycle returns all four fields byte-identical; missing/malformed config file returns defaults; config persists under "providers"."anthropic" key in config.json without clobbering ollama/openai sections.

- [x] T-003: Implement AnthropicProvider header (anthropic_provider.h)
  - REQs: REQ-F-001, REQ-F-006, REQ-F-007, REQ-F-024, REQ-F-025, REQ-F-039
  - Check: src/providers/include/holonight_providers/anthropic_provider.h declares constructor (HttpClient, base_url), availableModels(), refresh(on_complete), refresh(on_success, on_error), setBaseUrl/baseUrl, setAuthKey, setTemperature, setMaxOutputTokens, and sendChat(model, history, on_event, idle_timeout); sendChat returns HttpRequestHandlePtr; method surface is structurally equivalent to OpenAI/Ollama.

- [x] T-004: Implement AnthropicProvider request construction
  - REQs: REQ-F-001, REQ-F-002, REQ-F-003, REQ-F-004, REQ-F-005, REQ-F-006, REQ-F-007, REQ-F-008, REQ-F-038
  - Check: sendChat() constructs POST to /v1/messages with body containing model, messages array (User/Assistant only), max_tokens, temperature, stream: true; system messages hoisted to top-level "system" field (concatenated with \n\n if present, omitted entirely if zero messages); x-api-key header present when auth_key_ non-empty, absent when empty; anthropic-version: 2023-06-01 header always present.

- [x] T-005: Implement AnthropicProvider model discovery and denylist filtering
  - REQs: REQ-F-009, REQ-F-010, REQ-F-011, REQ-F-012
  - Check: refresh() fetches GET /v1/models with x-api-key and anthropic-version headers; denylist substrings (embedding, moderation, vision, ocr) filter response data array; hypothetical future model not matching denylist passes through (fail-open); availableModels() returns tagged ModelId entries with provider_id "anthropic"; previously-selected model preserved in ComboBox if still in list after refresh.

- [x] T-006: Implement AnthropicProvider SSE framing and event routing
  - REQs: REQ-F-013, REQ-F-014, REQ-F-015, REQ-F-016, REQ-F-017, REQ-F-019
  - Check: on_data callback reassembles fragmented chunks (split mid-JSON-object, mid-field) into complete SSE blocks separated by blank lines; processSseBlock extracts JSON payload; content_block_delta with text_delta emits ContentDelta event; message_stop emits Completed event; message_delta/message_start/ping are no-op; type-based routing verified via mock listener capturing exact event types.

- [x] T-007: Implement AnthropicProvider error handling and recovery
  - REQs: REQ-F-018, REQ-F-020, REQ-F-021, REQ-F-022, REQ-F-023
  - Check: 401 Unauthorized with error body emits Error event with extracted message; 429 Too Many Requests emits Error; 400 Bad Request emits Error; stream ending without message_stop emits Error with "Stream ended without completion"; network timeout/unreachable host emits Error with user-readable transport message; malformed JSON in SSE block calls failStream; no unhandled exceptions propagate.

- [x] T-008: Add AnthropicProvider to CMake source lists
  - REQs: REQ-NF-001, REQ-NF-003
  - Check: src/providers/CMakeLists.txt add_library source list includes holonight_providers/anthropic_provider.h and src/anthropic_provider.cpp; src/application/CMakeLists.txt includes anthropic_provider_settings_controller.h/.cpp; tests/CMakeLists.txt test_holonight_ai source list includes providers/test_anthropic_provider.cpp and application/test_anthropic_provider_settings_controller.cpp; task build succeeds.

- [x] T-009: Implement provider-layer unit tests (test_anthropic_provider.cpp)
  - REQs: REQ-NF-001, REQ-NF-002
  - Check: tests/providers/test_anthropic_provider.cpp source file added to test_holonight_ai (not new executable); test cases cover request construction (0/1/2 system messages hoisted correctly, max_tokens always present), header presence/values, SSE fragmentation across five blocks, event routing (content_block_delta/message_stop/message_delta/message_start/ping), denylist filtering (embedding/moderation/vision/ocr excluded, future models pass-through), error mapping (401/429/400/malformed/stream-incomplete), cancellation; >80% line coverage on anthropic_provider.cpp achieved; all tests pass.

- [x] T-010: Extend ChatController for Anthropic provider routing
  - REQs: REQ-F-001, REQ-F-039
  - Check: ChatController constructor takes shared_ptr<AnthropicProvider> as third parameter after OpenAI; anthropic_provider_ member added; hasModelsFor() gains "if (provider_id == 'anthropic')" branch ahead of Ollama fallback; dispatchSendChat() gains matching branch; all call sites (ChatViewModel constructor, test fixtures) updated simultaneously to pass third argument.

- [x] T-011: Extend ChatViewModel for Anthropic provider and model sync
  - REQs: REQ-F-001, REQ-F-039
  - Check: ChatViewModel constructor takes shared_ptr<AnthropicProvider> as third parameter; anthropic_provider_ member added; anthropicProviderForSettings() getter implemented; syncAvailableModels() unions all three providers' model lists; new syncAvailableAnthropicModels(preferred) method sets anthropic_models_loaded_ flag before re-unioning; selected-model-stale check includes "provider_id == 'anthropic'" branch.

- [x] T-012: Implement AnthropicProviderSettingsController
  - REQs: REQ-F-026, REQ-F-027, REQ-F-028, REQ-F-030, REQ-F-031, REQ-F-032, REQ-F-040, REQ-F-041
  - Check: src/application/include/holonight_application/anthropic_provider_settings_controller.h and .cpp declare QML_SINGLETON with Q_PROPERTY (authToken, baseUrl, defaultModel, temperature, maxOutputTokens); create() factory chains off ProviderSettingsController.credentialStore(); save() validates temperature [0.0, 1.0], persists config, applies to shared provider, calls model_sync_callback; load() seeds from config and defers first refresh() to onTokenRetrieved(); credential-store unavailable displays warning and disables auth field; refreshModels/testConnection fetch /v1/models via probe_provider_ and display result.

- [x] T-013: Test AnthropicProviderConfig and ConfigRepository round-trip
  - REQs: REQ-F-034, REQ-F-036
  - Check: tests/config/test_config_repository.cpp gains test cases for AnthropicProviderConfig round-trip (all four fields); three-provider read-merge-write test saves Ollama config, then OpenAI, then Anthropic sequentially and verifies all three sections coexist in config.json and remain consistent.

- [x] T-014: Extend ChatController tests for Anthropic routing
  - REQs: REQ-F-001, REQ-F-039
  - Check: tests/application/test_chat_controller.cpp fixture gains makeAnthropicProvider() helper; all existing tests updated to construct ChatController with third Anthropic parameter; new test case sends ModelId{provider_id: "anthropic", ...} and verifies route hits Anthropic FakeHttpClient (not Ollama/OpenAI).

- [x] T-015: Implement AnthropicProviderSettingsController tests
  - REQs: REQ-F-006, REQ-F-027, REQ-F-028
  - Check: tests/application/test_anthropic_provider_settings_controller.cpp source file (mirrors openai_provider_settings_controller test structure); test cases verify maxOutputTokens round-trip through save/load/resetToDefaults, temperature validation [0.0, 1.0] differs from OpenAI [0.0, 2.0], authToken property maps to provider.setAuthKey(), credential store unavailable warning, model refresh triggers model_sync_callback.

- [x] T-016: Create AnthropicSettingsPanel.qml
  - REQs: REQ-F-026, REQ-F-027, REQ-F-028, REQ-F-029, REQ-F-030, REQ-F-032
  - Check: qml/workspace/AnthropicSettingsPanel.qml created (copied from OpenAISettingsPanel.qml with deltas); includes TextField (base URL, default "https://api.anthropic.com"), ComboBox (model, populated by refresh), Refresh button, Slider + SpinBox (temperature, range 0.0–1.0), SpinBox (max output tokens, default 1024), TextField + eye-toggle + Clear button (API key), Test Connection button, Save/Cancel/Reset buttons; binds to AnthropicProviderSettingsController; displays "Secret storage unavailable" message and disables key field when credential store unavailable.

- [x] T-017: Route Anthropic provider in ProvidersPage.qml
  - REQs: REQ-F-026
  - Check: qml/workspace/ProvidersPage.qml Loader.sourceComponent switch includes case "Anthropic": return anthropicPanel branch; anthropicPanel Component block loads AnthropicSettingsPanel{}; "Google" case still routes to unsupportedPanel.

- [x] T-018: Update ProviderListDelegate status indicator for Anthropic
  - REQs: REQ-F-026
  - Check: qml/workspace/ProviderListDelegate.qml defines readonly property isAnthropic; statusColor binding includes switch on AnthropicProviderSettingsController.anthropicConnectionStatus returning success/error/muted colors for Anthropic provider row (consistent with OpenAI row pattern).

- [x] T-019: Pass clang-format and clang-tidy checks
  - REQs: REQ-NF-003
  - Check: task format-check reports no formatting violations; task tidy reports no clang-tidy warnings on all new/modified C++ files; code follows naming conventions (CamelCase classes, camelBack functions, lower_case members, lower_case_ private members).

- [x] T-020: Pass QML linting
  - REQs: REQ-NF-003
  - Check: task qml-lint reports no errors on AnthropicSettingsPanel.qml, ProvidersPage.qml, and ProviderListDelegate.qml; all QML bindings and property accesses resolve.

- [x] T-021: Full build and test suite passes
  - REQs: REQ-NF-001, REQ-NF-002, REQ-NF-003
  - Check: task configure-tests && task build && task test succeeds with zero test failures; all new test files (test_anthropic_provider.cpp, test_anthropic_provider_settings_controller.cpp, extended test_chat_controller.cpp and test_config_repository.cpp) pass; >80% line coverage on anthropic_provider.cpp verified via task coverage.

- [x] T-022: Manual verification with real Anthropic API key
  - REQs: REQ-F-001, REQ-F-006, REQ-F-007, REQ-F-024, REQ-F-026, REQ-F-027, REQ-F-028, REQ-F-032, REQ-NF-004, REQ-NF-005
  - Check: User enters valid Anthropic API key and base URL in settings panel, clicks Save, refreshes model list, selects a model and temperature/max_tokens, sends a message with system context, observes streamed reply token-by-token; max_tokens field appears in request; system message hoisted to top-level field; Stop button halts stream; invalid key displays 401 error; settings persist after app restart; Test Connection succeeds with valid key and shows error with invalid key; all operations complete within 10-second timeout for model discovery.
