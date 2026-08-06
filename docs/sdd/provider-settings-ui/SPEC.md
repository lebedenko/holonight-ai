# Provider Settings UI Specification

**Feature Slug:** `provider-settings-ui`
**Cycle:** Provider Settings UI Implementation
**Date:** 2026-07-22
**Status:** Approved (from requirements grilling)

**Source references:** `docs/mockups/settings.png` is a directional visual reference for information
architecture, control grouping, and interaction affordances, not a pixel-perfect acceptance image.
It is not a commitment to implement the mockup's Add provider, Enabled, Capabilities, or Tool access
controls in this cycle. ADR 0001
(`docs/adr/0001-standalone-repository.md`) is authoritative for the process and dependency boundary:
Settings belongs to the standalone `holonight-chat` process, uses `import Holonight` for shared
styling, and must not depend on `holonight-shell`.

---

## Overview

This cycle delivers a Settings window with a sidebar navigation shell and a functional Providers page for managing LLM provider configuration (base URL, model selection, context window, temperature, authentication token). The window is accessed via a gear icon button added to the main chat window header. Only Ollama configuration is functional this cycle; OpenAI, Anthropic, and Google provider rows are clickable but show a static "not yet supported" message. Non-secret provider config (URL, model, context, temperature) persists to `$XDG_CONFIG_HOME/holonight-ai/config.json`; secrets persist to Secret Service via the existing `CredentialStore` abstraction. A critical architectural change enables live reconfiguration of the running `OllamaProvider` instance without app restart, allowing settings changes to apply immediately to the next chat interaction.

---

## Functional Requirements

### Settings Window & Navigation

#### REQ-F-001: Settings Window Opens from Chat Window
**Statement:** When a user clicks a settings icon button in the main chat window header, the system shall open a Settings window.

**Acceptance criteria:**
- A gear/settings icon button is visible in `WorkspaceWindow.qml`'s header area, positioned near the model picker or in a visually consistent location.
- Clicking the icon opens a new top-level window (either a second `QQuickView` or a QML `Window` item, consistent with `ChatApplication.cpp`'s existing patterns).
- The Settings window is independently movable and closable.
- Closing the Settings window does not close the main chat window.
- Opening Settings again after closing it reuses the same Settings window instance; the application
  does not create multiple Settings windows.

#### REQ-F-002: Settings Window Displays Sidebar Navigation
**Statement:** The Settings window shall display a persistent left sidebar with navigation entries for General, Providers, Models, Tools & MCP, Permissions, Storage, Appearance, and Advanced.

**Acceptance criteria:**
- All eight sidebar entries are visually present and distinguishable.
- "Providers" entry is highlighted/selected by default when the window opens.
- Clicking a non-Providers entry (General, Models, Tools & MCP, Permissions, Storage, Appearance, Advanced) visually indicates the selection but displays a placeholder message indicating that section is "Coming soon" or similar, with no interactive controls.
- All non-Providers entries are styled to appear disabled or placeholder (e.g., reduced opacity, disabled color, or an explicit "Coming soon" label), making it clear they are not functional this cycle.
- Non-Providers entries do not throw errors or log warnings when clicked.

### Providers Page

#### REQ-F-003: Providers Page Lists Four Provider Rows
**Statement:** The Providers page of the Settings window shall display four provider rows: Ollama, OpenAI, Anthropic, and Google.

**Acceptance criteria:**
- All four provider names are visible and centered in a list format (as per the mockup: vertical list with status indicator dots).
- Each row includes a provider name and an icon or visual indicator for connection status (e.g., dot color: green for connected, red for disconnected, gray for not-yet-tested or not-configured).
- Rows are clickable and selectable, highlighting the active selection.
- The list order is stable across app sessions (not random or reordered).

#### REQ-F-004: Non-Ollama Providers Show Unsupported Message
**Statement:** When a user selects the OpenAI, Anthropic, or Google provider row, the system shall display a static message indicating the provider is not yet supported.

**Acceptance criteria:**
- The detail panel on the right displays only the provider name/icon and a message such as "This provider is not yet supported" or "Coming soon: [Provider Name]".
- No form fields, configuration options, or credential entry fields are visible for these providers.
- The Cancel/Save/Reset buttons remain visible but have no effect when clicked for these providers (or are disabled; either is acceptable).
- No errors, warnings, or console messages are logged when selecting these providers.

#### REQ-F-005: Ollama Detail Panel Displays URL Configuration Field
**Statement:** When Ollama is selected, the system shall display a text input field for the Ollama server base URL.

**Acceptance criteria:**
- The field is labeled "Server URL" or equivalent.
- The field displays the currently configured base URL (loaded from config.json at startup, or the default `http://localhost:11434` if no config exists).
- The user can type or edit the URL value.
- The field allows URLs with or without trailing slashes (e.g., both `http://localhost:11434` and `http://localhost:11434/` are accepted).
- Invalid URLs (e.g., missing protocol, malformed host) are accepted in the field without rejection (validation/rejection happens only on Save or Test Connection, not on keystroke).

#### REQ-F-006: Ollama Detail Panel Displays Model Selection Dropdown
**Statement:** When Ollama is selected, the system shall display a dropdown field for selecting the default model.

**Acceptance criteria:**
- The field is labeled "Default model" or equivalent.
- The dropdown is initially populated by calling `OllamaProvider::refresh()` against the currently saved server URL (at page load time, using the saved URL from config.json or the default).
- The dropdown lists available models returned by the refresh call (GET `/api/tags` against the Ollama server).
- If no models are available (empty list from server, or server is unreachable), the dropdown shows empty or displays a placeholder like "No models available" without error.
- The dropdown displays the currently selected default model (loaded from config.json, or empty if none is saved).
- The dropdown is editable or has a clear visual way to select/deselect a model.

#### REQ-F-007: Ollama Detail Panel Includes Model Refresh Button
**Statement:** When Ollama is selected, the system shall display a "Refresh models" button adjacent to the model dropdown.

**Acceptance criteria:**
- Clicking the button triggers a call to `OllamaProvider::refresh()` against the base URL currently entered in the URL field (not the saved URL).
- After refresh completes, the dropdown repopulates with the returned models.
- If refresh fails (server unreachable, malformed response), an inline error message appears below the dropdown (e.g., "Failed to fetch models: [reason]"), distinct from a successful "No models" response.
- The button is disabled or shows a loading state while refresh is in progress.
- The button remains interactive even if the URL field has been edited but not yet saved.

#### REQ-F-008: Ollama Detail Panel Displays Context Window Numeric Field
**Statement:** When Ollama is selected, the system shall display a numeric input field for the context window size.

**Acceptance criteria:**
- The field is labeled "Context window" or "Context length" or equivalent.
- The field accepts positive integer values (e.g., 1024, 2048, 4096).
- The field displays the currently configured context window (loaded from config.json, or a sensible default if not configured, e.g., 2048 or 4096).
- The field enforces a valid range (e.g., min 128, max 1000000) or displays validation feedback on Save if out of range.
- Negative or zero values are rejected on Save, with an inline error message.

#### REQ-F-009: Ollama Detail Panel Displays Temperature Control
**Statement:** When Ollama is selected, the system shall display a temperature control with a numeric field and/or slider, range 0–2.

**Acceptance criteria:**
- The control is labeled "Temperature" or equivalent.
- The control accepts and displays decimal values (e.g., 0.7, 1.5, 2.0).
- Following the mockup, a numeric field and slider are both shown and remain synchronized.
- The slider spans from 0 to 2 with endpoint labels, and the numeric field accepts values within
  0–2 (inclusive).
- The control displays the currently configured temperature (loaded from config.json, or a default like 0.7 or 1.0 if not configured).
- Attempting to enter a value outside 0–2 either prevents entry or shows an error on Save.

#### REQ-F-010: Ollama Detail Panel Displays Auth Token Field
**Statement:** When Ollama is selected, the system shall display a text input field for an optional API/auth token under an "Authentication (optional)" section header.

**Acceptance criteria:**
- The field is labeled "Token", "API Token", or "Authorization token" and is clearly marked as optional.
- The field initially displays empty or shows a placeholder like "No token configured".
- A "show/hide" toggle (eye icon or similar) appears adjacent to the field, allowing the user to toggle between masked (dots/asterisks) and plaintext display.
- A "Clear" button appears adjacent to the field, allowing the user to empty the field (clearing any previously retrieved or entered token).
- When the token field is non-empty and the user clicks Save, the token is stored via `CredentialStore::store("ollama", token)` (the secret token persists to Secret Service/keyring, not to the JSON config file).
- When the user clicks Clear and then Save, the token is removed via `CredentialStore::remove("ollama")`.
- If a token is previously stored in Secret Service, it is retrieved and displayed in the field on page load (fetched via the equivalent of `CredentialStore::retrieve("ollama")`).
- A Save operation applies the exact token value submitted by that Save. Editing the field while an
  asynchronous credential operation is pending must not cause a different, unsaved token to be
  applied to the running provider.

#### REQ-F-011: Test Connection Button Provides Reachability Feedback
**Statement:** When a user clicks a "Test connection" button on the Ollama detail panel, the system shall perform a reachability check against the currently entered server URL.

**Acceptance criteria:**
- The button is labeled "Test connection" or equivalent and is visible below or adjacent to the URL field.
- Clicking the button triggers a GET request to `/api/tags` at the base URL currently entered in the URL field (not the saved URL), with the auth token if configured.
- If the request succeeds (HTTP 200, valid response), an inline success indicator appears (e.g., green text or icon, "Connected" message).
- If the request fails (timeout, connection refused, non-2xx response, malformed JSON), an inline error message appears (e.g., red text, "Connection failed: [reason]"), distinct from the model refresh error (which is shown separately).
- A server with zero pulled models is considered a successful connection test (do not conflate "no models" with "connection failed").
- The button is disabled or shows a loading state while the test is in progress.
- Test connection does not modify any saved state (it is a read-only probe).

#### REQ-F-012: Ollama Configuration Changes Apply Immediately on Save
**Statement:** When a user clicks the Save button after modifying Ollama configuration, the system shall apply the changes to the running `OllamaProvider` instance immediately without requiring an app restart.

**Acceptance criteria:**
- After Save is clicked, the new base URL is reflected in the running `OllamaProvider` instance.
- After Save is clicked, the new default model selection is available in the chat window's model picker dropdown immediately (via `ChatViewModel::availableModels()`).
- After credential persistence succeeds, the submitted auth token is cached on the running
  `OllamaProvider`; subsequent HTTP requests include its Bearer header. The provider does not query
  Secret Service for every request.
- The next chat interaction uses the new configuration (new base URL, model, temperature, context window, auth header).
- In-flight chat streams are not required to switch mid-stream to the new configuration; they may complete with the old configuration.
- A Cancel operation (see REQ-F-013) reverts in-panel fields without applying changes to the running provider.

#### REQ-F-013: Cancel Button Discards Unsaved Edits
**Statement:** When a user clicks the Cancel button, the system shall discard any unsaved changes and revert in-panel fields to their last-saved values.

**Acceptance criteria:**
- All input fields on the Providers page are reset to the last-saved state (values loaded from config.json and Secret Service).
- The model dropdown reverts to the last-saved selected model.
- The URL field reverts to the last-saved URL.
- The auth token field is cleared (not populated with the previous value, for security—the user sees empty even if a token is stored in Secret Service).
- No changes are persisted to config.json or Secret Service.
- The Settings window remains open after Cancel (does not force-close); closing is user-initiated or explicitly closed only if desired.

#### REQ-F-014: Reset Button Restores Provider Defaults
**Statement:** When a user clicks the Reset button on the Ollama detail panel, the system shall restore the in-panel fields to the provider's factory defaults without persisting to storage.

**Acceptance criteria:**
- The URL field is reset to the built-in default: `http://localhost:11434`.
- The default model field is reset to empty or to a placeholder (no default model selected until user refreshes).
- The context window field is reset to a sensible factory default (e.g., 2048 or 4096, consistent with Ollama's own defaults).
- The temperature field is reset to a sensible factory default (e.g., 0.7 or 1.0).
- The auth token field is cleared (empty).
- These in-panel changes are visible immediately but are not persisted to config.json or Secret Service until the user clicks Save.
- Clicking Reset and then Cancel reverts to the last-saved values (Reset does not permanently modify storage unless Save is clicked).

#### REQ-F-015: Config File Is Read at Application Startup
**Statement:** When the application starts, the system shall read `$XDG_CONFIG_HOME/holonight-ai/config.json` to initialize the default Ollama provider configuration.

**Acceptance criteria:**
- If the file exists and is valid JSON, its contents are parsed and used to initialize the `OllamaProvider` with the saved base URL, default model, context window, and temperature values.
- If the file does not exist, the system falls back to built-in defaults: base URL `http://localhost:11434`, empty/no default model, sensible context/temperature defaults.
- If the file exists but is invalid JSON, corrupt, or unreadable, the system logs a non-fatal warning, falls back to built-in defaults, and continues (no crash, graceful degradation).
- The auth token is not read from config.json; it is retrieved separately from Secret Service via `CredentialStore::retrieve("ollama")` at startup.
- The `OllamaProvider` instance is constructed with these initialized values before the chat window is ready for interaction.

#### REQ-F-016: Config File Is Updated on Save
**Statement:** When a user clicks Save on the Providers page, the system shall persist non-secret configuration to `$XDG_CONFIG_HOME/holonight-ai/config.json`.

**Acceptance criteria:**
- The config file is created if it does not exist (respecting `$XDG_CONFIG_HOME` directory structure; if `$XDG_CONFIG_HOME` is not set, use `~/.config` as fallback, per XDG spec).
- The saved configuration includes: base URL, default model ID, context window size, and temperature value.
- The auth token is NOT written to this file (only to Secret Service).
- The file is formatted as valid JSON (human-readable with indentation, e.g., 2-space indent).
- After write, the application should not crash or become unstable; the running `OllamaProvider` is updated synchronously (see REQ-F-012).
- If the write fails (permission denied, disk full), an error dialog or inline notification appears to the user, and no other state is modified.

### Credential Store Integration

#### REQ-F-017: Auth Token Is Persisted to Secret Service
**Statement:** When a user enters a non-empty auth token and clicks Save, the system shall persist the token to Secret Service via `CredentialStore::store("ollama", token)`.

**Acceptance criteria:**
- The `CredentialStore` is the same singleton instance used elsewhere in the app (injected or provided by a factory; see REQ-C-004).
- The token is stored asynchronously if the credential store uses worker threads (e.g., `SecretServiceCredentialStore`).
- On completion (signal `storeCompleted`), the Providers page remains responsive and no user-blocking wait dialog is shown (store operation is fire-and-forget from the UI perspective, or a brief non-blocking indicator is shown).
- If the store operation fails, a user-visible notification appears (e.g., "Failed to save token: [reason]"), but the non-secret config is still persisted (partial success is acceptable and should not be treated as total failure).

#### REQ-F-018: Auth Token Is Cleared from Secret Service on Remove
**Statement:** When a user clicks the Clear button in the auth token field and then clicks Save, the system shall remove the token from Secret Service via `CredentialStore::remove("ollama")`.

**Acceptance criteria:**
- The remove operation is asynchronous (same pattern as store; signal `removeCompleted`).
- On completion, the token field remains empty and focused (if desired).
- If the remove operation fails, a notification appears, but the in-panel state reflects the clear action (user sees empty field even if backend removal fails).
- Subsequent attempts to retrieve the token (e.g., on app restart, via `CredentialStore::retrieve("ollama")`) return "not found" (empty token).

#### REQ-F-019: Graceful Degradation When Credential Store Is Unavailable
**Statement:** Where the credential store is unavailable (when `CredentialStore::isAvailable()` returns false), the system shall disable the auth token field but keep all other Providers configuration fields editable.

**Acceptance criteria:**
- The token field is visually disabled (e.g., grayed out, read-only, non-focusable).
- An inline notice appears adjacent to the token field (e.g., "Secret storage unavailable"), explaining why it cannot be edited.
- The URL, model, context window, and temperature fields remain fully editable and can be saved normally to config.json.
- The Save button is active and saves the non-secret config even if the token field is disabled.
- The Providers page does not show an error dialog, crash, or become unusable when credential store is unavailable.
- With the current `CredentialStore` contract, unavailability is sticky for the lifetime of the
  store instance. The token field may remain disabled until the application restarts; non-secret
  provider settings remain usable throughout.

---

## Non-Functional Requirements

#### REQ-NF-001: QML Code Follows Project Conventions
**Statement:** The QML code for the Settings window and Providers page shall follow the naming, theming, and structure conventions established in the existing `holonight-ai` codebase.

**Acceptance criteria:**
- All hardcoded colors use `HoloniightPalette.<token>` tokens (imported via `import Holonight`), not hardcoded hex values.
- Component names follow `CapitalCase` (e.g., `SettingsWindow`, `ProvidersPanel`, `ProviderListDelegate`).
- Signal handlers and property bindings follow `camelCase` function/property names.
- QML module URI is `HolonightChat` (same as existing chat window QML); all new QML files are registered via `qt_add_qml_module` in `apps/chat/CMakeLists.txt`.
- QML files are organized under `qml/workspace/` subdirectory (or `qml/shared/` if reusable by quick panel; a design choice, documented in code comments).
- A linting pass (`task qml-lint`) produces no warnings or errors for new QML files.

#### REQ-NF-002: C++ Code Follows Project Naming and Style Conventions
**Statement:** Any new C++ code (e.g., config file I/O module, Settings controller) shall adhere to the naming and style conventions defined in `.clang-format` and `.clang-tidy`.

**Acceptance criteria:**
- All classes and public functions are `CapitalCase` (e.g., `ProviderSettingsModel`, `ConfigRepository`).
- All member functions are `camelCase` (e.g., `loadConfig()`, `saveConfig()`, `refresh()`).
- All private data members are `lower_case_` (trailing underscore).
- The code passes `task tidy` without errors (given `WarningsAsErrors:'*'` in `.clang-tidy`, as noted in project memory).
- The code passes `task format-check` without formatting changes needed.
- Lines are <= 120 characters, using 2-space indentation.
- The code uses C++23 features where appropriate (e.g., `std::optional`, `std::format` if C++20+).

#### REQ-NF-003: Config Module Is Testable with GTest
**Statement:** The config file I/O module shall be designed to be testable using GTest with injected dependencies (temp directory paths, fake file I/O if needed).

**Acceptance criteria:**
- A new test executable (e.g., `test_provider_settings`) or extension to an existing test executable includes GTest cases for config file reading and writing.
- Test cases use a temporary directory (`std::filesystem::temp_directory_path()` or similar) for isolated file I/O, not the user's real `$XDG_CONFIG_HOME`.
- Test cases verify:
  - Config is correctly read from a valid JSON file.
  - Config is correctly written to a new file.
  - Invalid/corrupt JSON is handled gracefully (defaults are used, no crash).
  - Missing `$XDG_CONFIG_HOME` directory is handled (created or uses fallback).
  - All required fields (URL, model, context, temperature) are round-tripped correctly.
- The config module does not directly interact with Secret Service in tests; a fake `CredentialStore` (the existing `FakeCredentialStore`) is injected for any credential retrieval in tests.
- Tests are run as part of `task test` and CTest.

#### REQ-NF-004: Settings Controller Integrates CredentialStore for Testing
**Statement:** Any C++ settings controller or settings model shall inject and use an abstract `CredentialStore` interface (not the concrete `SecretServiceCredentialStore`) for testability.

**Acceptance criteria:**
- A `ProviderSettingsController` or equivalent class accepts a `CredentialStore*` pointer in its constructor or via a setter, allowing test code to inject `FakeCredentialStore`.
- In production, the `ChatApplication` or startup code injects the real `SecretServiceCredentialStore` singleton.
- GTest cases use `FakeCredentialStore` to verify token store/retrieve/remove behavior without touching the real keyring daemon.
- No test case uses the real Secret Service; all credential interactions go through the fake.

#### REQ-NF-005: Ollama Reconfiguration Does Not Require App Restart
**Statement:** The existing `OllamaProvider` shall support reconfiguration of its base URL and authentication details without requiring the `ChatApplication` to restart.

**Acceptance criteria:**
- `OllamaProvider` exposes a method (e.g., `setBaseUrl(const QString& url)`, `setAuthToken(const QString& token)`) or equivalent to update configuration at runtime.
- Calling this method on the running instance updates the `base_url()` accessor and all subsequent HTTP requests use the new URL.
- Auth token is fetched from `CredentialStore` on each HTTP request (or cached and refreshed on demand), not hardcoded at construction time.
- Existing in-flight HTTP requests are not interrupted or modified; reconfiguration applies only to new requests (streaming chats are allowed to complete with the old URL if they started before reconfiguration).
- No crashes, memory leaks, or race conditions occur during or after reconfiguration (thread-safe if `OllamaProvider` is used from QML's main thread, no additional synchronization required).

#### REQ-NF-006: Settings Window Rendering Does Not Block Main Thread
**Statement:** Loading and displaying the Settings window, including initial model refresh and credential store queries, shall not block the chat window or other UI interactions.

**Acceptance criteria:**
- The initial model refresh call (GET `/api/tags`) on Providers page load is performed asynchronously (e.g., via `QNetworkAccessManager` signal/slot, not blocking I/O).
- Credential store retrieval (if applicable) is asynchronous.
- While these operations are in progress, the user can interact with the chat window normally, type messages, send chats, etc.
- The UI remains responsive even if the Ollama server is unreachable (requests time out gracefully with an error message, not a frozen UI).

#### REQ-NF-007: Config File JSON Parsing Is Robust
**Statement:** The config file parsing module shall handle malformed, partial, or missing JSON gracefully without crashing or logging unhelpful errors.

**Acceptance criteria:**
- If the JSON file is empty, the parser falls back to defaults without logging an error (empty config is valid: use defaults).
- If the JSON is malformed (e.g., missing closing brace), the parser logs a non-fatal warning (e.g., "Invalid config.json syntax, using defaults") and falls back to defaults.
- If required fields are missing from valid JSON (e.g., `url` field is absent), defaults are used for those fields.
- If the file is unreadable (permission denied), a non-fatal warning is logged and defaults are used.
- In all cases, the app continues and is usable; the Settings window shows the defaults as current values, and the user can correct and re-save.

---

## Constraints

#### REQ-C-001: Only Ollama Configuration Is Functional This Cycle
**Statement:** The system shall provide functional configuration UI only for Ollama; OpenAI, Anthropic, and Google providers shall display static "not yet supported" messages.

**Acceptance criteria:**
- Ollama detail panel allows editing URL, model, context, temperature, auth token; other three show placeholder messages.
- No credential store or config file entries are created for OpenAI/Anthropic/Google this cycle.
- No providers module adapters are implemented for these three (they are out of scope).
- The spec for those providers is reserved for future cycles (no implementation or half-wired integrations this cycle).
- The mockup's Add provider and per-provider Enabled controls are not implemented this cycle; the
  provider list is fixed and Ollama remains available to chat after successful configuration.

#### REQ-C-002: Config File Location Follows XDG Basedir Spec
**Statement:** The system shall store non-secret provider configuration at `$XDG_CONFIG_HOME/holonight-ai/config.json`, with a fallback to `~/.config/holonight-ai/config.json` if `$XDG_CONFIG_HOME` is unset.

**Acceptance criteria:**
- The code uses `QStandardPaths::GenericConfigLocation` (Qt's XDG implementation) to determine the base directory.
- The filename is always `config.json` (not `settings.json`, `provider-config.json`, or other variants).
- The file is stored under a `holonight-ai/` subdirectory (to coexist with other potential `holonight/` configs from sibling projects without collision).
- No config is written to the user's home directory root, system-wide `/etc/`, or other non-standard locations.

#### REQ-C-003: Secret Service Is Used for Auth Token Storage Only
**Statement:** Auth tokens (bearer tokens, API keys) shall be persisted only to Secret Service via `CredentialStore`; they shall never be written to `config.json` or any other plain-text persistent storage.

**Acceptance criteria:**
- An audit of the written `config.json` file shows no token field or value.
- The settings controller code explicitly avoids persisting token fields to config file (code review check).
- Any attempt to log, print, or display a token in debug output excludes the full token value (e.g., "Token length: N characters" instead of full plaintext).

#### REQ-C-004: CredentialStore Is Injected, Not Hardcoded
**Statement:** The Settings UI or its backing controller shall not instantiate or hardcode a reference to `SecretServiceCredentialStore`; it shall accept a `CredentialStore` interface pointer for testability and modularity.

**Acceptance criteria:**
- The settings controller/model is initialized with a `CredentialStore*` parameter (constructor, init method, or QML property).
- Production code (e.g., in `ChatApplication`) injects the real singleton instance.
- Test code injects `FakeCredentialStore` without conditional compilation or special build flags (true dependency injection).

#### REQ-C-005: Sidebar Placeholders Are Visually Disabled, Not Removed
**Statement:** The system shall display all eight sidebar entries (General, Providers, Models, Tools & MCP, Permissions, Storage, Appearance, Advanced) at all times; non-Providers entries shall appear disabled or marked "Coming soon", not hidden.

**Acceptance criteria:**
- All eight entries are present in the sidebar QML (not conditionally hidden via `visible: false` or `if` statements that exclude them).
- Non-Providers entries have reduced opacity, grayed-out color, or a visual "disabled" marker (per HoloNight design system).
- A "Coming soon" label or tooltip appears when hovering or selecting these entries, reinforcing that they are not yet functional.
- The sidebar does not dynamically shrink or expand; layout is stable.

#### REQ-C-006: No Additional Provider Adapters This Cycle
**Statement:** The system shall not implement provider adapters, credential wiring, or model refresh logic for OpenAI, Anthropic, or Google this cycle.

**Acceptance criteria:**
- No `OpenAIProvider`, `AnthropicProvider`, or `GoogleAIProvider` classes are added to `holonight_providers`.
- No config.json fields are added for these providers (e.g., no `openai.api_key`, `anthropic.url`, etc.).
- The existing `OllamaProvider` is the only functional provider in the module.
- Choosing these providers in the UI shows only static text; the backend is not consulted or connected.

#### REQ-C-007: No Capabilities or Tool Access Panels This Cycle
**Statement:** The mockup includes Capabilities and Tool access sections; these are out of scope for this cycle and shall not be implemented.

**Acceptance criteria:**
- No Capabilities or Tool access sections are visible in the Providers detail panel.
- MCP integration is not wired (see REQ-C-008).
- The spec does not define any requirements for these features; they are deferred to the Tools & MCP cycle.

#### REQ-C-008: No MCP/Tools Integration This Cycle
**Statement:** The system shall not wire any MCP server integration or tool-access logic in the Settings window; this is reserved for the "Tools & MCP" sidebar section in a future cycle.

**Acceptance criteria:**
- No MCP server definitions are loaded or queried when opening Providers page.
- The "Tools & MCP" sidebar entry shows the "Coming soon" placeholder (per REQ-C-005), not any actual tool selection UI.
- No tool-access or tool-restriction logic is added to the settings controller.

#### REQ-C-009: No Dynamic Corner-Radius Theming This Cycle
**Statement:** The Settings window shall use existing `HoloniightPalette` tokens for colors and fixed corner-radius values defined in the HoloNight design system (`holonight-qt`); no new dynamic or themeable corner-radius logic shall be added in this cycle.

**Acceptance criteria:**
- The QML uses `HoloniightPalette.<token>` for all colors (e.g., `HoloniightPalette.surfaceColor`, `HoloniightPalette.textColor`).
- Corner-radius values (e.g., on buttons, input fields, card backgrounds) are hardcoded constants or inherited from HoloNight component definitions (e.g., `HnButton`, `HnTextField`).
- No new CSS properties, dpiScaling, or per-screen corner-radius adjustments are introduced.
- This constraint is a note that theming work is out of scope; use what exists.

#### REQ-C-010: Settings Window Lifecycle Matches ChatApplication Lifecycle
**Statement:** The Settings window shall be owned and managed by `ChatApplication` or a component created by it, following the same ownership and cleanup patterns as the existing `WorkspaceWindow`.

**Acceptance criteria:**
- The Settings window is instantiated once at app startup or on first open, not recreated on every toggle.
- When the main chat window closes (app termination), any open Settings window is also closed cleanly without dangling pointers or unfinished async operations.
- The window is stored as a member of `ChatApplication` or a managed pointer (e.g., `std::unique_ptr`, `QPointer`).
- No settings window instance persists after `ChatApplication::quit()` is called.

---

## Assumptions & Open Questions

### Resolved (No Longer Open)
All major architectural and design questions have been resolved via requirements grilling. The decisions listed in the pre-submission context are authoritative and fully captured in the requirements above.

### Implementation Notes (Not Open, But Worth Stating)

1. **Auth Token in OllamaProvider:**
   - The existing `OllamaProvider::sendChat()` and HTTP client must be updated to include an `Authorization: Bearer <token>` header if a token is configured.
   - The token is retrieved from `CredentialStore::retrieve("ollama")` on each request (or cached with a refresh mechanism).
   - This is a change to `holonight_providers`, not a Settings UI concern, but it is required for end-to-end functionality.

2. **Reconfiguration Mechanism for OllamaProvider:**
   - The `ChatViewModel` must be updated to allow `OllamaProvider` reconfiguration (or a new provider instance must be injected at runtime).
   - Today, `ChatViewModel` self-constructs one `OllamaProvider` (see CLAUDE.md comment). This cycle must establish a path to update this provider's configuration without replacing it (simpler: setters on the provider; more complex: factory/injection pattern).
   - The simplest approach: add `setBaseUrl()`, `setAuthToken()`, and any other setters to `OllamaProvider`, and call them from the Settings controller when Save is clicked.

3. **Config Module Implementation:**
   - A new `holonight_config` or `holonight_settings` static library should be created (or added to an existing module) to handle config.json I/O.
   - This module should be link-dependency-free except for Qt (no external JSON libraries; use Qt's `QJsonDocument`/`QJsonObject`).
   - It should export a single class (e.g., `ProviderConfig` struct + `ProviderConfigRepository` or `ConfigLoader` class).

4. **Settings Controller:**
   - A `ProviderSettingsController` C++ class should mediate between the QML UI and the `OllamaProvider` / `CredentialStore` / config module.
   - This controller should be exposed to QML as a singleton or property (if QML directly manipulates, consider wrapping in C++ for type safety and testability).
   - Alternatively, a lightweight QML-only design is acceptable if the C++ integration (CredentialStore, OllamaProvider reconfiguration) is wrapped in a C++ adapter layer.

5. **QML Singleton Registration:**
   - The Settings controller/model should be registered as a QML singleton (if needed) via `qt_add_qml_module` in `apps/chat/CMakeLists.txt`, following the pattern established for `ChatViewModel`.

6. **Settings Window Ownership:**
   - A new `SettingsWindow.qml` should be created under `qml/workspace/`.
   - `ChatApplication.cpp` should instantiate and own the `QQuickView` or `Window` object (if not auto-created by QML engine).
   - A button in `WorkspaceWindow.qml` should signal `ChatApplication` to show/raise the Settings window.

7. **Testing Config Parsing:**
   - Write a GTest for config file parsing using a temp directory and mock JSON files.
   - Test valid JSON, invalid JSON, missing fields, missing file, permission errors, etc.
   - Use `QTest` or vanilla GTest assertions to verify round-trip correctness.

---

## References

- **Project Documentation:**
  - `/home/andrii/Projects/pet/holonight/holonight-ai/CLAUDE.md` — Module layout, QML structure, naming conventions, build tasks.
  - `/home/andrii/Projects/pet/holonight/holonight-ai/docs/high-level-project-idea.md` — Architecture rationale, roadmap, config file structure notes.
  - `/home/andrii/Projects/pet/holonight/holonight-ai/docs/mockups/settings.png` — UI mockup (sidebar, Providers list, detail panel layout).

- **Prior SDD Cycles (Precedent for Patterns):**
  - `/home/andrii/Projects/pet/holonight/holonight-ai/docs/sdd/secret-service-credentials/SPEC.md` — Credential store abstraction, worker-thread async pattern, testing with fake implementations, `operationFailed` signal pattern.
  - `/home/andrii/Projects/pet/holonight/holonight-ai/docs/sdd/sqlite-conversation-persistence/SPEC.md` — Repository pattern, worker-thread QSqlDatabase usage, migration/schema testing, module organization.
  - `/home/andrii/Projects/pet/holonight/holonight-ai/docs/sdd/ollama-chat-backend/SPEC.md` — OllamaProvider design, streaming via signals, HTTP client injection, ChatController/ChatViewModel integration.
  - `/home/andrii/Projects/pet/holonight/holonight-ai/docs/sdd/chat-window-qml/SPEC.md` — QML window lifecycle, model/view binding patterns, ComboBox and ListView usage, theming via `HoloniightPalette`.

- **Sibling Project Reference:**
  - `../holonight-shell/docs/dev-setup.md` — QML type metadata generation pattern (`scripts/combine-metatypes.cmake`), applicable if Settings controller is exposed as a QML singleton with C++ types.

- **Design System:**
  - `../holonight-qt/` — Holonight design system, `HoloniightPalette` tokens, component library (imported as `import Holonight` in QML).

- **Testing Patterns:**
  - `.clang-tidy`, `.clang-format` — Code style and linting rules (identical to `holonight-shell` baseline).
  - `tests/test_placeholder.cpp` — Existing GTest wiring and CTest integration.

---

## Summary

This specification defines a functional Settings window with a sidebar navigation shell and a complete Ollama provider configuration UI. The window is accessed via a gear icon in the chat window header and allows users to configure and test the Ollama server URL, model selection, context window, temperature, and optional authentication token. Non-secret config persists to JSON; secrets persist to Secret Service. Configuration changes apply immediately to the running chat application without restart. OpenAI, Anthropic, and Google provider rows are present but non-functional placeholders this cycle. All other sidebar sections (General, Models, Tools & MCP, Permissions, Storage, Appearance, Advanced) are visually present but disabled, marked as "Coming soon". The spec establishes testability via dependency injection, graceful degradation when Secret Service is unavailable, and robust error handling for config file I/O. Implementation follows existing project conventions (EARS requirements, GTest, QML structure, naming) and is deferred to the architecture and task-breakdown phases.
