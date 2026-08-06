# Anthropic Provider Adapter — Specification

**Version:** 1.0  
**Status:** Ready for Implementation  
**Module:** `holonight_providers`  
**Architecture Tier:** Domain-agnostic provider adapter (same as `OllamaProvider` and `OpenAIProvider`)

---

## Executive Summary

This specification defines the integration of Anthropic's Messages API (`POST /v1/messages`) into the holonight-ai chat application. The implementation reuses existing interfaces for credential storage, configuration persistence, application-layer retry logic, and streaming HTTP cancellation, requiring only new C++ adapter logic in `holonight_providers` and a new QML UI component in `qml/workspace/AnthropicSettingsPanel.qml` to activate the current "Anthropic" placeholder in the provider list.

Key differences from Ollama and OpenAI adapters:
1. API key carried in `x-api-key` header (not `Authorization: Bearer`).
2. Mandatory `anthropic-version` header with hardcoded protocol version.
3. System-prompt messages hoisted to top-level `system` field (not inline in `messages` array).
4. **Configurable `max_tokens` field** (required by Anthropic's API on every request, unlike OpenAI where it is optional).
5. SSE streaming with event types: `message_start`, `content_block_delta`, `message_delta`, `message_stop`, `error`, `ping`.

---

## Functional Requirements

### F.1 API Request Construction

**REQ-F-001** (Ubiquitous)  
The system shall construct HTTP POST requests to the Anthropic Messages API endpoint `/v1/messages` using the configurable base URL (default: `https://api.anthropic.com`) and `x-api-key` header authentication.

- **Acceptance Criterion:** A test fixture verifies that an AnthropicProvider instance, when configured with a base URL and API key, constructs a request body with all required fields and an `x-api-key: <key>` header for a non-empty message list; the HTTP method is POST; the URL path is `/v1/messages`.

**REQ-F-002** (Ubiquitous)  
The system shall include an `anthropic-version` HTTP header on every request with a hardcoded protocol version value (fixed: `2023-06-01`).

- **Acceptance Criterion:** Inspecting captured HTTP request headers confirms the presence of `anthropic-version: 2023-06-01` on every request, including model-discovery GET requests.

**REQ-F-003** (Ubiquitous)  
The system shall populate the `messages` array in every request with only `User`- and `Assistant`-role messages reconstructed from the locally persisted conversation, preserving send order.

- **Acceptance Criterion:** Given a conversation with `System`, `User`, `Assistant`, `User` roles, the adapter constructs a `messages` array with exactly 3 objects (excluding System); the roles and content are in send order and correspond to User/Assistant pairs only.

**REQ-F-004** (Ubiquitous)  
The system shall extract all `MessageRole::System` messages from the conversation's message list, concatenate their text content in order separated by blank lines (`\n\n`), and include the result as a top-level `system` field string in the request body.

- **Acceptance Criterion:** Given a conversation with two System messages ("Context A" and "Context B") followed by a User message, the request body contains `"system": "Context A\n\nContext B"` and does not include System-role entries in the `messages` array.

**REQ-F-005** (Ubiquitous)  
If there are zero `MessageRole::System` messages in the conversation, the `system` field shall be omitted entirely from the request body.

- **Acceptance Criterion:** A test sends a message conversation containing only User/Assistant roles; the captured request body has no `system` key; the request is valid.

**REQ-F-006** (Ubiquitous)  
The system shall include the `temperature` parameter in every request, populated from the user-selected temperature setting in the Anthropic settings UI (range: 0.0–1.0, default: 1.0 per Anthropic's standard).

- **Acceptance Criterion:** Captured request payloads for three different temperature slider positions (0.2, 1.0, 0.8) each contain the correct `"temperature"` value as a JSON number.

**REQ-F-007** (Ubiquitous)  
The system shall include a `max_tokens` parameter in every request, populated from the user-configured max-output-tokens setting in the Anthropic settings UI (default: 1024, typical range: 100–16384 or higher depending on model context window).

- **Acceptance Criterion:** After entering a max-output-tokens value (e.g., 2048) in the settings panel and saving, sending a message results in a captured request containing `"max_tokens": 2048`.

**REQ-F-008** (Ubiquitous)  
The system shall set the `model` field to the user-selected model identifier (fetched via model discovery and chosen in the ComboBox).

- **Acceptance Criterion:** After selecting a model from the dropdown and sending a message, the captured request contains `"model": "<selected-id>"` matching the chosen model's exact ID string.

---

### F.2 Model Discovery and Filtering

**REQ-F-009** (Event-driven)  
When the user clicks the "Refresh models" button in the Anthropic settings panel, the system shall fetch the list of available models from `GET /v1/models` using the configured base URL and API key (via `x-api-key` header and hardcoded `anthropic-version`).

- **Acceptance Criterion:** A test mocks the HTTP GET endpoint; clicking the refresh button triggers a network request to `<base_url>/v1/models` with both `x-api-key` and `anthropic-version` headers; the response is parsed and used to update the model list.

**REQ-F-010** (Ubiquitous)  
The system shall filter the returned model list using a denylist of substrings to exclude non-chat model families: `embedding`, `moderation`, `vision`, `ocr`.

- **Acceptance Criterion:** A mock model list containing `["claude-3-opus-20250219", "claude-3-sonnet-20250219", "claude-3-haiku-20250219", "claude-3-5-sonnet-20241022", "embedding-001", "moderation-latest"]` is filtered; the result contains only the `claude-*` chat models and excludes embedding/moderation families.

**REQ-F-011** (Ubiquitous)  
The system shall use fail-open filtering: any model ID not matching a denylist substring is included, allowing new chat models shipped by Anthropic in the future to appear automatically without code changes.

- **Acceptance Criterion:** A hypothetical future model ID `claude-4-ultra-future` (not on the current denylist) is included in the filtered list; an unknown model `gpt-xyz` passes the denylist filter and is included.

**REQ-F-012** (Ubiquitous)  
The system shall populate the model ComboBox in the settings UI with the filtered list of model IDs, maintaining the user's previously selected model if it still exists in the new list.

- **Acceptance Criterion:** After refreshing with a new model list, the ComboBox displays all non-denylist models; if the user had selected `claude-3-opus-20250219` and it remains in the new list, the ComboBox shows it as selected; if the selected model was removed, the ComboBox reverts to the first available model.

---

### F.3 Streaming Event Mapping to Domain Types

**REQ-F-013** (State-driven)  
While an Anthropic streaming response is in progress, the system shall buffer incoming bytes from the `HttpClient::sendStreaming()` callback and parse Server-Sent Events (SSE) framing: consecutive blocks separated by blank lines (`\n\n`), each prefixed with `data: `, payload parsed as JSON.

- **Acceptance Criterion:** A test delivers fragmented byte chunks (split mid-JSON-object, mid-field-name) via mock `on_data` callbacks; the adapter correctly reassembles and parses five distinct SSE blocks despite non-aligned chunk boundaries; each is dispatched as a separate event.

**REQ-F-014** (State-driven)  
While processing an SSE block, the system shall extract the `type` field from the JSON payload and route based on its value (e.g., `content_block_delta`, `message_delta`, `message_stop`).

- **Acceptance Criterion:** A mock SSE stream with mixed event types (`message_start`, `content_block_delta`, `message_delta`, `message_stop`) is parsed; type-based routing is confirmed by verifying the correct handler is invoked for each type.

**REQ-F-015** (Event-driven)  
When an SSE event of type `content_block_delta` with `delta.type == "text_delta"` is received containing a `delta.text` string field, the system shall emit a domain `ContentDelta` stream event with the delta text.

- **Acceptance Criterion:** An SSE event `{"type": "content_block_delta", "delta": {"type": "text_delta", "text": "Hello"}}` emits a domain event of variant type `ContentDelta` with text "Hello"; captured in a test listener.

**REQ-F-016** (Event-driven)  
When an SSE event of type `message_stop` is received, the system shall emit a domain `Completed` stream event.

- **Acceptance Criterion:** An SSE block `{"type": "message_stop"}` emits a domain event of variant type `Completed`; downstream message finalization logic proceeds.

**REQ-F-017** (Event-driven)  
When an SSE event of type `message_delta` is received containing usage information (e.g., `usage.output_tokens`), the system shall extract and forward this to the domain layer if the codebase already tracks usage in `StreamEvent` (optional; if not currently tracked, this shall be a no-op).

- **Acceptance Criterion:** If `StreamEvent` has a `Usage` variant, an SSE event `{"type": "message_delta", "usage": {"output_tokens": 42}}` emits a domain `Usage` event with output token count; if no `Usage` variant exists, the adapter silently ignores the usage data.

**REQ-F-018** (Unwanted-behavior)  
If the Anthropic response includes an error SSE event (`type: "error"`), a non-2xx HTTP status code on the initial request, or the stream ends without emitting a terminal event (neither `message_stop`), then the system shall emit a domain `Error` stream event containing the extracted error message from Anthropic's JSON structure.

- **Acceptance Criterion:** A 401 Unauthorized response with body `{"type": "error", "error": {"type": "authentication_error", "message": "Invalid x-api-key"}}` emits an `Error` event with message "Invalid x-api-key"; a 429 response with body `{"type": "error", "error": {"type": "rate_limit_error", "message": "Rate limit exceeded"}}` emits an `Error` event with that message; a stream that delivers deltas but never emits `message_stop` emits an `Error` event with a descriptive message (e.g., "Stream ended without completion").

**REQ-F-019** (Ubiquitous)  
The system shall silently ignore (no-op) all SSE event types except `message_start`, `content_block_delta`, `message_delta`, `message_stop`, and `error`, including but not limited to `ping` keepalive events.

- **Acceptance Criterion:** An SSE stream containing `ping`, `message_start`, `content_block_delta`, `message_stop` events is processed; `ping` events trigger no domain event emissions; only content deltas and completion events are captured (verified by checking emitted event count).

---

### F.4 Error Handling and User Surfacing

**REQ-F-020** (Unwanted-behavior)  
If the user enters an invalid Anthropic API key and attempts to send a message, then the system shall surface a readable error message to the UI (e.g., "Authentication failed: Invalid x-api-key") rather than crashing, hanging, or silently retrying indefinitely.

- **Acceptance Criterion:** A test sends a message with a mocked 401 Unauthorized response containing error details; the application emits an error message through the existing error-reporting pathway; the UI displays the error in the chat or message log; no unhandled exceptions, no silent retry loops, no frozen UI.

**REQ-F-021** (Unwanted-behavior)  
If the user triggers a request during a rate-limit condition (429 Too Many Requests), then the system shall surface the rate-limit error message to the UI; automatic retry shall not occur at the adapter level.

- **Acceptance Criterion:** A mocked 429 response with Anthropic's error structure is delivered; an error message surfaces to the UI; the adapter does not automatically retry; the user can manually click "Retry" in the application's existing retry UI.

**REQ-F-022** (Unwanted-behavior)  
If the Anthropic API rejects a request due to an invalid parameter combination (400 Bad Request), then the system shall surface the raw Anthropic error message to the UI.

- **Acceptance Criterion:** A test delivers a mocked 400 response with error message; the UI displays this message verbatim; no client-side model detection or parameter pre-validation blocks the request.

**REQ-F-023** (Unwanted-behavior)  
If a network error occurs (timeout, connection refused, DNS failure), then the system shall emit an `Error` domain event with a descriptive message (e.g., "Network error: connection timeout").

- **Acceptance Criterion:** A test with a mocked unreachable host triggers a timeout; an `Error` event is emitted with a user-readable message; no unhandled exceptions propagate to the UI.

---

### F.5 Cancellation

**REQ-F-024** (Event-driven)  
When the user clicks the Stop button during an active Anthropic streaming response, the system shall cancel the HTTP request via the existing `HttpRequestHandle::cancel()` mechanism.

- **Acceptance Criterion:** A test starts a mocked streaming request; the `cancel()` method is invoked while deltas are being emitted; the stream halts; no further deltas are processed; the HTTP connection is closed.

**REQ-F-025** (Ubiquitous)  
The system shall return a `HttpRequestHandlePtr` from `AnthropicProvider::sendChat()`, enabling the application layer to manage cancellation.

- **Acceptance Criterion:** The `sendChat()` method signature matches the pattern of `OllamaProvider::sendChat()` and `OpenAIProvider::sendChat()`, returning a handle that supports `cancel()`.

---

### F.6 Settings UI — Anthropic Provider Configuration Panel

**REQ-F-026** (Ubiquitous)  
The system shall display an `AnthropicSettingsPanel.qml` component in the provider list (replacing the current "Anthropic" placeholder) with the following input fields and controls:
1. Base URL (TextField, default: `https://api.anthropic.com`)
2. Default model (ComboBox, populated by dynamic model fetch)
3. Refresh models (Button)
4. Temperature (Slider + SpinBox, range: 0.0–1.0, default: 1.0)
5. Max output tokens (SpinBox, range: typical 100–16384+, default: 1024)
6. API key (TextField with show/hide toggle and Clear button)
7. Secret storage unavailable (notice, displayed when credential store is unavailable)
8. Test Connection (Button)
9. Reset / Cancel / Save (footer buttons)

- **Acceptance Criterion:** The Anthropic provider row in `ProvidersPage.qml` loads `AnthropicSettingsPanel.qml` (not a placeholder); all listed fields are present and interactive; max-output-tokens control is distinct from the temperature control.

**REQ-F-027** (Event-driven)  
When the user modifies the Base URL, default model, temperature, or max-output-tokens in the Anthropic settings panel and clicks Save, the system shall persist these values to the config repository via `ConfigRepository::saveAnthropicConfig()`.

- **Acceptance Criterion:** After entering a custom base URL, selecting a model, adjusting temperature and max-output-tokens, and clicking Save, a subsequent application restart loads the same values; config is persisted to the underlying JSON/SQLite store.

**REQ-F-028** (Event-driven)  
When the user enters an API key in the key field and clicks Save, the system shall store the key in the credential store via `CredentialStore::store("anthropic", key)`.

- **Acceptance Criterion:** After entering a key and saving, a test retrieves the stored credential via `store->retrieve("anthropic")` and confirms it matches the entered value; the key is stored separately from config (not in JSON).

**REQ-F-029** (Event-driven)  
When the user clicks the eye-icon toggle on the API key field, the system shall toggle the field's visibility between plain text and masked (asterisks/dots).

- **Acceptance Criterion:** Clicking the eye icon switches the TextField's `echoMode` from `TextInput.Password` to `TextInput.Normal` and back; the key value is hidden when masked and revealed when unmasked.

**REQ-F-030** (Event-driven)  
When the user clicks the Clear button next to the API key field, the system shall erase the field's content and (upon Save) remove the key from the credential store via `CredentialStore::remove("anthropic")`.

- **Acceptance Criterion:** After entering a key, clicking Clear blanks the field; clicking Save triggers `remove("anthropic")`; a subsequent query to the credential store returns no credential for "anthropic".

**REQ-F-031** (Conditional)  
Where the credential store is unavailable (e.g., D-Bus failure, Secret Service daemon not running), the system shall display a "Secret storage unavailable" notice in the settings panel and disable the API key field.

- **Acceptance Criterion:** A test simulates a credential-store initialization failure; the Anthropic settings panel displays a warning message; the API key field is disabled (read-only, grayed out); the user cannot enter or save a key.

**REQ-F-032** (Event-driven)  
When the user clicks the Test Connection button, the system shall make a request to `GET /v1/models` using the current base URL and key settings (with `x-api-key` header and hardcoded `anthropic-version`); if successful, display a confirmation message ("Connection successful"); if unsuccessful, display the error message from Anthropic.

- **Acceptance Criterion:** With a valid key and base URL, clicking Test Connection triggers a fetch and displays "Connection successful" when the response is 2xx; with an invalid key, the error message is displayed; network errors display a descriptive message.

---

### F.7 Configuration Persistence

**REQ-F-033** (Ubiquitous)  
The system shall add a new `AnthropicProviderConfig` struct to `holonight_config` with the following fields, using `QString`/Qt types to match existing conventions:
- `base_url: QString` (default: `https://api.anthropic.com`)
- `default_model: QString` (default: empty)
- `temperature: double` (default: 1.0, range: 0.0–1.0)
- `max_output_tokens: int` (default: 1024, typical range: 100–16384)
- `friend bool operator==(...) = default;` (matching existing pattern)

- **Acceptance Criterion:** The struct is defined in `src/config/include/holonight_config/provider_config.h` alongside `OllamaProviderConfig` and `OpenAIProviderConfig`; it compiles without warnings; no field uses `std::string` where `QString` is used elsewhere.

**REQ-F-034** (Ubiquitous)  
The system shall implement `ConfigRepository::loadAnthropicConfig()` and `ConfigRepository::saveAnthropicConfig()` methods with signatures and error-handling conventions that exactly match `loadOllamaConfig`/`saveOllamaConfig`: `loadAnthropicConfig()` returns a plain `AnthropicProviderConfig` (never throws; returns default-constructed values on a missing/malformed file), and `saveAnthropicConfig(const AnthropicProviderConfig&)` returns `std::expected<void, QString>` with a failure reason on write error.

- **Acceptance Criterion:** Both methods are declared and defined with the exact signatures `AnthropicProviderConfig loadAnthropicConfig() const` and `std::expected<void, QString> saveAnthropicConfig(const AnthropicProviderConfig&) const`; neither throws; a missing config file makes `loadAnthropicConfig()` return default values.

**REQ-F-035** (Ubiquitous)  
The system shall NOT include API key/secret fields in the `AnthropicProviderConfig` struct; key storage remains exclusively in the credential store (Secret Service / KWallet via `CredentialStore`).

- **Acceptance Criterion:** The `AnthropicProviderConfig` struct has no `key`, `secret`, `api_key`, or similar field; the struct compiles and is used to persist only URL, model, temperature, and max_tokens.

**REQ-F-036** (Ubiquitous)  
The system shall persist config under the existing namespaced `"providers"."anthropic"` key in `config.json`, using the same read-merge-write pattern already used for `"ollama"` and `"openai"`, without modifying those sections.

- **Acceptance Criterion:** Inspecting `config.json` after saving Anthropic settings shows keys under `"providers"."anthropic"` (e.g., `{"providers": {"anthropic": {"base_url": "...", "temperature": 1.0, ...}}}`); the `"ollama"` and `"openai"` sections remain unchanged.

---

### F.8 Credential Integration

**REQ-F-037** (Ubiquitous)  
The system shall reuse the existing `holonight_credentials::CredentialStore` interface without modification, calling `store(providerId, secret)` with `providerId = "anthropic"` to save the API key.

- **Acceptance Criterion:** The AnthropicProvider instance calls `credential_store->store("anthropic", key)` when saving settings; the same provider instance calls `credential_store->retrieve("anthropic")` when preparing a request; both operations use the provider ID "anthropic".

**REQ-F-038** (Unwanted-behavior)  
If no credential is stored for `"anthropic"` (empty/not-found result from `CredentialStore::retrieve("anthropic")`), then the system shall send the request without an `x-api-key` header — mirroring OllamaProvider's existing behavior of omitting the auth header entirely when its token is empty — allowing Anthropic's resulting 401 response to surface through the normal REQ-F-020 error path.

- **Acceptance Criterion:** A test with no stored credential for "anthropic" sends a request with no `x-api-key` header present at all; the mocked 401 response is surfaced as an error via the same path as REQ-F-020; no crash, no silent failure, no client-side pre-request blocking.

---

### F.9 Application-Layer Integration

**Note on current architecture:** AnthropicProvider shall likewise be a plain concrete class mirroring OllamaProvider and OpenAIProvider's method shapes — it shall NOT introduce a virtual base class or dynamic-dispatch interface, since none exists in the current codebase. The exact mechanism by which the application layer selects which provider instance backs the active conversation (e.g., provider routing in `ChatController`) is left to Stage 2 (Design) and out of scope for this Spec.

**REQ-F-039** (Ubiquitous)  
The system shall provide an `AnthropicProvider` class in `holonight_providers` with a method surface (`sendChat()`, `availableModels()`, `refresh()`, `setBaseUrl()`/`baseUrl()`, `setAuthKey()`, `setTemperature()`, `setMaxOutputTokens()`) structurally equivalent to `OllamaProvider` and `OpenAIProvider`, so that Stage 2's design for wiring it into the application layer can follow the same shape.

- **Acceptance Criterion:** `AnthropicProvider`'s public method signatures are reviewed side-by-side with `OllamaProvider` and `OpenAIProvider`; each has a structurally equivalent counterpart (same parameter shapes, same callback-based async pattern) except where a requirement in this Spec explicitly says otherwise.

**REQ-F-040** (Ubiquitous)  
The system shall add an `AnthropicProviderSettingsController` QML-facing bridge in `holonight_application`, mirroring `OpenAIProviderSettingsController`, wiring the Settings window's Anthropic provider panel.

- **Acceptance Criterion:** The bridge exists, is registered as a `QML_SINGLETON` in the `holonight_application` target, and provides methods/properties for loading/saving Anthropic config, testing connection, and refreshing models.

**REQ-F-041** (Ubiquitous)  
The `AnthropicProviderSettingsController` shall reuse the same `holonight_credentials::CredentialStore` instance already shared by the other two provider settings controllers, requiring no changes to the credential store's storage format or API.

- **Acceptance Criterion:** The controller instantiates or accepts a shared `CredentialStore` pointer; credentials are stored with provider ID "anthropic"; no new credential-store methods or storage format changes are required.

---

## Non-Functional Requirements

### N.1 Testing and Code Coverage

**REQ-NF-001** (Ubiquitous)  
All new C++ code in `AnthropicProvider` shall have matching GTest unit tests covering normal flow, error cases, and edge cases (stream fragmentation, malformed JSON, missing fields, empty model list, system-prompt hoisting, SSE ping events, etc.).

- **Acceptance Criterion:** A test executable `test_anthropic_provider` is added under `tests/`; it achieves >80% line coverage of `src/providers/src/anthropic_provider.cpp`; test cases cover request construction, system-prompt handling, SSE parsing, event mapping, error handling, model filtering, and cancellation.

**REQ-NF-002** (Ubiquitous)  
All tests shall use a fake/injectable `HttpClient` implementation to avoid real network calls during CI.

- **Acceptance Criterion:** Tests pass in isolation without network access; HTTP responses are mocked via constructor injection; no real API calls are made during `ctest` execution.

**REQ-NF-003** (Ubiquitous)  
The code shall adhere to the project's clang-format style (Google-based, 2-space indent, 120-column limit, C++23) and pass clang-tidy linting with no warnings (WarningsAsErrors: '*').

- **Acceptance Criterion:** `task format-check` reports no formatting violations; `task tidy` reports no clang-tidy warnings; the code follows existing naming conventions (CamelCase classes, camelBack functions, lower_case members).

---

### N.2 Performance and Responsiveness

**REQ-NF-004** (Ubiquitous)  
SSE stream parsing shall be non-blocking: incoming byte chunks shall be buffered and parsed incrementally without blocking the UI event loop.

- **Acceptance Criterion:** A test with rapid SSE block arrivals does not stall the event loop; the HTTP client's `on_data` callback returns promptly after buffer management and event dispatch.

**REQ-NF-005** (Ubiquitous)  
Model discovery (GET /v1/models) shall complete within a configurable timeout (default: 10 seconds); if the request times out, an error message shall be displayed and the model list shall not be updated.

- **Acceptance Criterion:** A test mocks a delayed response (>10 seconds); the request times out; an error message surfaces to the UI; the ComboBox is not updated.

---

### N.3 Security

**REQ-NF-006** (Ubiquitous)  
API keys shall never be logged, printed to stdout/stderr, or persisted in plaintext to config files; they shall be stored exclusively via the credential store (Secret Service / KWallet).

- **Acceptance Criterion:** Grep the codebase for calls to `qDebug()`, `std::cout`, `std::cerr`, config file writes that involve the key variable; no matches where the key is logged; credential store is the sole persistence mechanism.

---

## Constraint Requirements (Explicitly Out-of-Scope)

**REQ-C-001** (Ubiquitous)  
The system shall NOT implement tool/function calling support for this cycle, even if Anthropic's API response includes tool-related fields.

- **Acceptance Criterion:** A test delivers an SSE response containing tool definitions or usage; the adapter ignores them; no tool-registry or execution occurs.

**REQ-C-002** (Ubiquitous)  
The system shall NOT support image, file, or attachment input for this cycle.

- **Acceptance Criterion:** The Anthropic settings UI has no file-upload field, no image-picker, no attachment controls; the `messages` array contains only `{"role": "user"|"assistant", "content": "<text>"}` (text-only).

**REQ-C-003** (Ubiquitous)  
The system shall NOT implement or expose `max_output_tokens` with different validation rules or ranges than the standard Anthropic API (typically 100–context_window_size).

- **Acceptance Criterion:** The settings UI accepts numeric input for max_tokens in a reasonable range; no special-casing or model-specific validation occurs in the adapter.

**REQ-C-004** (Ubiquitous)  
The system shall NOT include any fields in the request not explicitly required by this Spec or by Anthropic's API documentation (e.g., no `betas`, `thinking`, `budget_tokens`, or experimental features).

- **Acceptance Criterion:** Request payloads contain only documented fields: `model`, `messages`, `system` (if applicable), `temperature`, `max_tokens`, plus standard HTTP headers (`x-api-key`, `anthropic-version`, `content-type`).

**REQ-C-005** (Ubiquitous)  
The system shall NOT use server-side conversation retention or multi-turn state management beyond sending the full conversation history on each request.

- **Acceptance Criterion:** Every request includes the complete conversation history; no server-side conversation ID or session token is retained.

**REQ-C-006** (Ubiquitous)  
The system shall NOT implement automatic retry logic on 429 (rate limit) or 5xx (server error) responses at the adapter level; the application-layer manual retry UI already implemented shall be the sole retry mechanism.

- **Acceptance Criterion:** A test mocks a 429 response; the adapter emits an Error event; no automatic retry occurs; the application's existing "Retry" button allows manual retry.

**REQ-C-007** (Ubiquitous)  
The system shall NOT support alternate base URLs (e.g., Azure, on-premises deployments) with custom authentication schemes.

- **Acceptance Criterion:** The base URL field accepts only standard `https://api.anthropic.com` or compatible proxies with `x-api-key` auth; no endpoint routing or alternate auth schemes occur.

---

## Non-Goals

The following features are explicitly deferred to future cycles and shall NOT be implemented in this cycle:

- **Google provider adapter** — separate future cycle.
- **Changes to credential store format or API** — only Anthropic-specific header application.
- **Changes to domain types** — no new `MessageRole`, `Message`, `Conversation`, or `StreamEvent` variants; adapter maps onto existing shapes exactly.
- **Image/file/attachment support** — text-only, matching current Ollama/OpenAI scope.
- **Tool/function calling** — deferred to future cycle.
- **Reasoning/extended-thinking mode** — deferred to future cycle.
- **Shared system-prompt helper** — only Anthropic needs hoisting (Ollama/OpenAI keep sending system inline).
- **Model-specific parameter constraints** — e.g., no client-side temperature validation per model.

---

## Acceptance Criteria Summary

A user can:
1. Enter an Anthropic API key and base URL in the provider settings panel.
2. Click "Refresh models" and see live-fetched models (denylist-filtered) populate the ComboBox.
3. Select a model, temperature (0.0–1.0), and max-output-tokens setting, then click Save.
4. Send a chat message (with or without system context) and observe a streamed reply appear token-by-token in the message list.
5. Encounter errors (401, 429, 400, network failure) and see a readable error message in the UI.
6. Click Stop during streaming and halt the request.
7. Reload the application and see all Anthropic settings (URL, model, temperature, max_tokens) restored.

All new C++ code passes GTest with >80% line coverage; no real network calls occur in CI; code adheres to clang-format and clang-tidy standards. System prompts are correctly hoisted to the `system` field; `max_tokens` is sent on every request; `x-api-key` header is used for authentication; `anthropic-version` header is hardcoded and present on every request.

---

## Verification Strategy

### Unit Tests
- `tests/test_anthropic_provider.cpp` — request construction (system-prompt hoisting, max_tokens presence), SSE parsing (including ping events), event mapping, model filtering, error cases, cancellation.

### Integration Tests
- QML panel tests — settings persistence (including max_tokens), credential storage/retrieval, UI state transitions.

### Manual Testing Checklist (provided to user after implementation)
1. Add valid Anthropic API key; fetch and select a model; send a message; observe streamed reply token-by-token.
2. Enter a message with system context; verify it appears in the request's top-level `system` field (not inline in `messages`).
3. Enter a max-output-tokens value (e.g., 500); verify it appears in the request.
4. Enter invalid API key; observe 401 error surface in UI.
5. Trigger rate limit; observe 429 error; manually retry via application UI.
6. Click Stop during streaming; verify stream halts.
7. Customize base URL; verify requests use the new endpoint.
8. Verify all settings (URL, model, temperature, max_tokens) persist across application restart.
9. Test connection with valid and invalid credentials; verify error messages are readable.

---

## Verification by Requirements

### System-Prompt Handling (REQ-F-004, REQ-F-005, REQ-NF-001)
- Test concatenates two System messages; verifies top-level `system` field contains both messages separated by `\n\n`; no System entries in `messages` array.
- Test sends message with zero System messages; verifies request lacks `system` field entirely.

### Max-Tokens Handling (REQ-F-007, REQ-NF-001)
- Test sets max_output_tokens to 2048 in settings; captures request; verifies `"max_tokens": 2048` is present.
- Default value (1024) is used if user does not configure.

### Authentication (REQ-F-001, REQ-F-002, REQ-F-038)
- Test verifies every request includes `x-api-key: <key>` header (not `Authorization: Bearer`).
- Test verifies every request includes `anthropic-version: 2023-06-01` header.
- Test with missing credential verifies `x-api-key` header is absent and 401 error surfaces.

### SSE Parsing (REQ-F-013, REQ-F-014, REQ-F-019)
- Test delivers fragmented SSE blocks; verifies correct assembly and parsing.
- Test delivers `message_start`, `ping`, `content_block_delta`, `message_stop` events; verifies `ping` is ignored, others are processed.

### Model Discovery (REQ-F-009, REQ-F-010, REQ-F-011, REQ-F-012)
- Test calls refresh; verifies GET request to `/v1/models` with both auth headers.
- Test filters model list with denylist; verifies embedding/moderation models are excluded.
- Test with future model ID (not on denylist) verifies it passes through.

---

## Dependencies and Integration Points

- **Existing interfaces used:** `HttpClient::sendStreaming()`, `CredentialStore`, `ConfigRepository`, `HttpRequestHandle::cancel()`, domain `StreamEvent` type.
- **New QML file:** `qml/workspace/AnthropicSettingsPanel.qml` (replaces placeholder).
- **New C++ files:** `src/providers/src/anthropic_provider.cpp`, `src/providers/include/holonight_providers/anthropic_provider.h`, `tests/test_anthropic_provider.cpp`.
- **Modified files:** `src/config/include/holonight_config/provider_config.h` (add struct), `src/config/src/config_repository.cpp` (add methods), provider routing in `ChatController` or similar (Stage 2 decision), `qml/workspace/ProviderListPanel.qml` or `ProvidersPage.qml` (replace placeholder).
- **New QML controller:** `AnthropicProviderSettingsController` registered in `holonight_application`.

---

**End of Specification**
