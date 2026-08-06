# SDD Tasks — provider-settings-ui

## Foundation: holonight_config Module (5 tasks)

- [x] T-001: Create holonight_config module headers
  - REQs: REQ-NF-002
  - Check: Files exist at src/config/include/holonight_config/ with provider_config.h (OllamaProviderConfig struct), config_path.h (resolveConfigFilePath declaration), and config_repository.h (ConfigRepository class interface).

- [x] T-002: Implement config_path.cpp and config_repository.cpp
  - REQs: REQ-F-015, REQ-F-016, REQ-NF-002, REQ-NF-003, REQ-NF-007
  - Check: resolveConfigFilePath() creates $XDG_CONFIG_HOME/holonight-ai/ directory and returns path to config.json; ConfigRepository::loadOllamaConfig() handles missing files, empty files, malformed JSON, and wrong-typed fields per §2.1 fallback table without throwing; saveOllamaConfig() writes 2-space-indented JSON with fields (base_url, default_model, context_window, temperature) and no auth token.

- [x] T-003: Create src/config/CMakeLists.txt
  - REQs: REQ-NF-002
  - Check: File defines holonight_config STATIC target with AUTOMOC off (no Q_OBJECT), links Qt6::Core, requires C++23, has public include_directories.

- [x] T-004: Test config_path.cpp
  - REQs: REQ-NF-003
  - Check: tests/config/test_config_path.cpp runs ResolveConfigFilePathCreatesHolonightAiDirectory and ResolveConfigFilePathEndsWithConfigJson test cases; both pass.

- [x] T-005: Test config_repository.cpp
  - REQs: REQ-F-015, REQ-F-016, REQ-NF-003, REQ-NF-007
  - Check: tests/config/test_config_repository.cpp contains 12+ test cases (LoadMissingFileReturnsDefaults, LoadValidJsonRoundTripsAllFourFields, LoadMalformedJsonLogsWarningAndReturnsDefaults, etc.) all passing; all tests use QTemporaryDir for isolated file I/O.

## holonight_providers Module Changes (4 tasks)

- [x] T-006: Add HttpRequest::headers field and update QtNetworkHttpClient
  - REQs: REQ-F-010, REQ-F-017, REQ-NF-002
  - Check: HttpRequest struct in src/providers/include/holonight_providers/http_client.h has QHash<QString, QString> headers member; QtNetworkHttpClient::buildNetworkRequest() iterates headers and calls setRawHeader() for each key-value pair after setting Content-Type.

- [x] T-007: Add FakeHttpClient::lastBufferedRequest() and extend OllamaProvider setters
  - REQs: REQ-F-007, REQ-F-010, REQ-NF-002, REQ-NF-004
  - Check: FakeHttpClient in tests/providers/fake_http_client.h has last_buffered_request_ member and lastBufferedRequest() accessor; OllamaProvider header declares setBaseUrl(QString), setAuthToken(QString), setContextWindow(int), setTemperature(double) methods; all compile without errors.

- [x] T-008: Implement OllamaProvider refresh() overload, authHeaders(), and sendChat() options
  - REQs: REQ-F-007, REQ-F-011, REQ-F-012, REQ-NF-002, REQ-NF-005
  - Check: OllamaProvider.cpp implements two-callback refresh(on_success, on_error) overload and private authHeaders() method; fetchModelList() refactored to take two callbacks; sendChat() constructs "options" JSON object with temperature and num_ctx fields and includes authHeaders() in HttpRequest; setter methods update corresponding private members.

- [x] T-009: Test OllamaProvider setters, refresh, and auth headers
  - REQs: REQ-F-007, REQ-F-010, REQ-F-011, REQ-NF-005
  - Check: tests/providers/test_ollama_provider.cpp extended with 7+ new test cases (SetBaseUrlAffectsOnlySubsequentRefreshCalls, SetAuthTokenAddsBearerHeaderToFetchModelList, EmptyAuthTokenOmitsAuthorizationHeader, SetAuthTokenAddsBearerHeaderToSendChat, SendChatIncludesTemperatureAndContextWindowInOptions, RefreshTwoCallbackOverloadCallsOnSuccessOnHttpSuccess, RefreshTwoCallbackOverloadTreatsEmptyModelsArrayAsSuccess); all tests pass.

## holonight_application Module: ChatViewModel (2 tasks)

- [x] T-010: Update ChatViewModel constructor and create() to load config
  - REQs: REQ-F-015, REQ-F-012, REQ-NF-002
  - Check: ChatViewModel::create() loads OllamaProviderConfig via ConfigRepository before constructing OllamaProvider; applies config.base_url, context_window, and temperature to the provider; passes config.default_model as new initial_default_model_id parameter to constructor; constructor signature compiles unchanged with default parameter (backward-compatible).

- [x] T-011: Implement ChatViewModel::providerForSettings() and syncAvailableModelsFromProvider()
  - REQs: REQ-F-012, REQ-NF-005
  - Check: providerForSettings() returns const shared_ptr to the OllamaProvider (plain C++, not Q_PROPERTY); syncAvailableModelsFromProvider(preferredModelId) re-reads provider_->availableModels(), updates availableModels property, auto-selects preferred model if present in list, emits signals; both compile without errors.

## holonight_application Module: ProviderSettingsController (2 tasks)

- [x] T-012: Create ProviderSettingsController header and implement core methods
  - REQs: REQ-F-005, REQ-F-008, REQ-F-009, REQ-F-010, REQ-F-013, REQ-F-014, REQ-NF-002, REQ-NF-004
  - Check: src/application/include/holonight_application/provider_settings_controller.h declares QML_SINGLETON class with Q_PROPERTYs (baseUrl, defaultModel, contextWindow, temperature, authToken, hasStoredToken, credentialStoreAvailable, credentialOperationInProgress, availableModelNames, modelRefreshInProgress, modelRefreshError, testConnectionInProgress, testConnectionStatus, testConnectionMessage, ollamaConnectionStatus, saveNotice) and Q_INVOKABLEs (load, refreshModels, testConnection, save, cancel, resetToDefaults); DI constructor accepts shared provider, probe provider, CredentialStore*, ConfigRepository, and model-sync callback; src/application/src/provider_settings_controller.cpp implements load(), cancel(), resetToDefaults() as per DESIGN.md §2.4/§2.7/§2.8 (no provider/credential-store calls, only in-panel field restoration).

- [x] T-013: Implement ProviderSettingsController save(), refreshModels(), testConnection() and handlers
  - REQs: REQ-F-007, REQ-F-011, REQ-F-012, REQ-F-016, REQ-F-017, REQ-F-018, REQ-F-019, REQ-NF-002
  - Check: save() validates context_window (128-1M) and temperature (0-2), saves config to file, applies to shared provider, calls credential_store_->store()/remove(), invokes model_sync_callback_; refreshModels() and testConnection() use probe_provider_ only (never touch shared provider); storeCompleted/removeCompleted/unavailable signal handlers update properties and notices; credential store unavailability sets credentialStoreAvailable false and disables auth token field. The asynchronous credential operation snapshots the submitted token and disables additional Save operations until completion, so a later field edit cannot apply an unsaved token to the shared provider.

## CMakeLists & Module Registration (1 task)

- [x] T-014: Update root CMakeLists.txt, src/application/CMakeLists.txt, and app CMakeLists.txt
  - REQs: REQ-NF-002, REQ-C-010
  - Check: Root CMakeLists.txt adds add_subdirectory(src/config) before src/application; src/application/CMakeLists.txt lists ProviderSettingsController.h/cpp as sources and links holonight_config and holonight_credentials PUBLIC; apps/chat/CMakeLists.txt unchanged except tests/CMakeLists.txt adds test_config_path.cpp, test_config_repository.cpp, test_provider_settings_controller.cpp to test_holonight_ai target and links holonight_config.

## QML Implementation (7 tasks)

- [x] T-015: Implement SettingsWindow.qml root and SettingsSidebar.qml navigation
  - REQs: REQ-F-001, REQ-F-002, REQ-NF-001, REQ-C-005
  - Check: qml/workspace/SettingsWindow.qml declares Window with SettingsSidebar child; SettingsSidebar.qml displays 8 entries (General, Providers, Models, Tools & MCP, Permissions, Storage, Appearance, Advanced) with Providers selected by default and other 7 entries visually disabled/grayed; clicking non-Providers entries updates currentSection property; window is initially visible: false and toggled by gear icon.

- [x] T-016: Implement ComingSoonPage.qml placeholder
  - REQs: REQ-F-002, REQ-C-005
  - Check: qml/workspace/ComingSoonPage.qml renders static "Coming soon" message using HoloniightPalette colors; displays when currentSection is any non-Providers value (General, Models, Tools & MCP, Permissions, Storage, Appearance, Advanced).

- [x] T-017: Implement ProvidersPage.qml, ProviderListPanel.qml, and delegate
  - REQs: REQ-F-003, REQ-F-004, REQ-NF-001, REQ-C-001
  - Check: qml/workspace/ProvidersPage.qml contains RowLayout with ProviderListPanel on left; ProviderListPanel.qml displays ListView with static model [Ollama, OpenAI, Anthropic, Google] and ProviderListDelegate showing name and status dot (color bound to ProviderSettingsController.ollamaConnectionStatus for Ollama, always gray for others); Ollama selected by default; clicking rows updates selectedProvider and triggers detail panel Loader.

- [x] T-018: Implement OllamaSettingsPanel.qml with all form fields
  - REQs: REQ-F-005, REQ-F-006, REQ-F-007, REQ-F-008, REQ-F-009, REQ-F-010, REQ-NF-001, REQ-NF-004
  - Check: qml/workspace/OllamaSettingsPanel.qml contains TextField (Server URL) ↔ baseUrl, ComboBox (Default model) ↔ defaultModel populated from availableModelNames, Button (Refresh models) calls refreshModels(), SpinBox (Context window) ↔ contextWindow with range 128-1M, Slider+SpinBox (Temperature) ↔ temperature with range 0-2, TextField (Token) ↔ authToken with eye-icon toggle, Clear button, "Secret storage unavailable" notice when credentialStoreAvailable false, Test Connection button, and footer buttons (Reset/Cancel/Save) with saveNotice Text.

- [x] T-019: Implement UnsupportedProviderPanel.qml
  - REQs: REQ-F-004, REQ-NF-001, REQ-C-001
  - Check: qml/workspace/UnsupportedProviderPanel.qml displays provider name/icon and static "Coming soon: [Provider]" text; Cancel/Save/Reset buttons present but bound to no-ops or disabled for OpenAI, Anthropic, Google rows.

- [x] T-020: Wire settings gear icon button in WorkspaceWindow.qml
  - REQs: REQ-F-001, REQ-NF-001
  - Check: WorkspaceWindow.qml header row contains gear-icon Button; clicking button shows the already-constructed SettingsWindow child; clicking the window-manager close control hides it; Cancel only discards edits and leaves the window open; reopening reuses the same independently movable and closable window instance.

- [x] T-021: Verify QML module registration complete
  - REQs: REQ-NF-001, REQ-NF-002
  - Check: All new QML files (SettingsWindow.qml, SettingsSidebar.qml, ProvidersPage.qml, ProviderListPanel.qml, ProviderListDelegate.qml, OllamaSettingsPanel.qml, UnsupportedProviderPanel.qml, ComingSoonPage.qml) are picked up by apps/chat/CMakeLists.txt's GLOB_RECURSE qml/*.qml pattern; ProviderSettingsController is registered as QML_SINGLETON via qt_add_qml_module; no new CMake code required (CONFIGURE_DEPENDS triggers automatic reconfigure).

## Test Suite: ProviderSettingsController (1 task)

- [x] T-022: Implement test_provider_settings_controller.cpp
  - REQs: REQ-F-012, REQ-F-013, REQ-F-014, REQ-F-017, REQ-F-018, REQ-NF-003, REQ-NF-004
  - Check: tests/application/test_provider_settings_controller.cpp contains 18+ test cases (LoadPopulatesPropertiesFromConfigRepository, RefreshModelsUsesProbeProviderNotSharedProvider, SaveWritesConfigAndAppliesBaseUrlContextWindowTemperatureToSharedProvider, SaveAppliesSubmittedTokenNotLaterFieldEdit, SaveIsRejectedWhileCredentialOperationIsPending, LateInitialRetrieveDoesNotOverwriteAnEdit, CancelRevertsAllFieldsToLastSavedConfig, ResetThenCancelRevertsToLastSavedNotFactoryDefaults, etc.) all passing with FakeHttpClient, FakeCredentialStore, and temp-dir ConfigRepository; no real network or keyring calls.

## Verification & Quality Checks (5 tasks)

- [x] T-023: Run task qml-lint and verify zero errors
  - REQs: REQ-NF-001
  - Check: task qml-lint completes with exit code 0 and outputs zero errors/warnings for all new QML files under qml/workspace/; no "type unknown" or "undeclared" errors reported for ProviderSettingsController properties/invokables.

- [x] T-024: Run task format-check and verify C++ code style
  - REQs: REQ-NF-002
  - Check: task format-check exits with code 0 and reports zero formatting violations in all new/modified .h and .cpp files (src/config/*, src/application/provider_settings_controller.*, src/providers/ollama_provider.cpp, src/application/chat_view_model.cpp); 2-space indent, 120-column lines, Google-based style.

- [x] T-025: Run task tidy and verify clang-tidy compliance
  - REQs: REQ-NF-002
  - Check: task tidy runs with WarningsAsErrors:'*' and exits with code 0; no warnings or errors reported for holonight_config, holonight_application (new code), or holonight_providers changes; build/tidy.log shows "passed" for all modified targets.

- [x] T-026: Run task test and verify all GTest suites pass
  - REQs: REQ-NF-003, REQ-NF-005, REQ-NF-006
  - Check: task configure-tests && task test executes successfully; ctest runs 30+ new test cases across test_config_path.cpp, test_config_repository.cpp, test_ollama_provider.cpp (extended), test_chat_view_model.cpp (extended), and test_provider_settings_controller.cpp; all tests PASS with zero timeouts or real network calls. Memory-sanitizer cleanliness is claimed only when a separately documented sanitizer or Valgrind command has been run.

- [x] T-027: Execute manual smoke test protocol against local Ollama
  - REQs: REQ-NF-006
  - Check: Execute 8-step checklist: (1) Start Ollama with ≥1 model, (2) task run holonight-chat, (3) Click gear icon, Settings window opens with Providers selected, (4) Ollama row visible with green dot, refresh button fetches models, ComboBox populates, (5) Edit URL field, click Test Connection, success/error message appears, (6) Enter auth token if applicable, click Save, provider updates, next message uses new config, (7) Click OpenAI/Anthropic/Google rows, "Coming soon" message displays, (8) Close Settings window, chat window remains responsive; all 8 steps succeed without crashes.

---

**Total Tasks**: 27
**First Task**: T-001
**Last Task**: T-027
