# Google Gemini Provider Adapter — Specification

**Version:** 1.0  
**Status:** Ready for Implementation  
**Module:** `holonight_providers`  
**Architecture Tier:** Domain-agnostic provider adapter (same as `OllamaProvider`, `OpenAIProvider`, and `AnthropicProvider`)

---

## Executive Summary

This specification defines the integration of Google's Gemini Developer API (`https://generativelanguage.googleapis.com/v1beta`) into the holonight-ai chat application. The implementation reuses existing interfaces for credential storage, configuration persistence, application-layer retry logic, and streaming HTTP cancellation, requiring only new C++ adapter logic in `holonight_providers` and enabling the current "Google" placeholder in the provider list to become a fully functional settings panel in `qml/workspace/GoogleSettingsPanel.qml`.

Key differences from Ollama, OpenAI, and Anthropic adapters:

1. API key carried in `x-goog-api-key` header (not `Authorization: Bearer` or `x-api-key`).
2. Request body uses `contents: [{role: "user"|"model", parts: [{text}]}]` structure (role: "model" not "assistant").
3. System-prompt messages hoisted to top-level `systemInstruction: {parts: [{text}]}` field (analogous to Anthropic's `system` field hoisting).
4. **Configurable `maxOutputTokens` field** in `generationConfig` (required-like, similar to Anthropic's `max_tokens`).
5. **Temperature range: 0.0–2.0** (wider than Anthropic's 0.0–1.0 and OpenAI's typical ranges).
6. SSE streaming with safety-filter blocking: responses may include `finishReason: "SAFETY"` or `"RECITATION"` with no text content instead of normal completions.
7. Model discovery via `GET /v1beta/models` with `supportedGenerationMethods` filtering to exclude non-chat models.

---

## Functional Requirements

### F.1 API Request Construction

**REQ-F-001** (Ubiquitous)  
The system shall construct HTTP POST requests to the Gemini API endpoint `/v1beta/models/{model}:generateContent` using the configurable base URL (default: `https://generativelanguage.googleapis.com`) and `x-goog-api-key` header authentication.

- **Acceptance Criterion:** A test fixture verifies that a GoogleProvider instance, when configured with a base URL and API key, constructs a request body with all required fields and an `x-goog-api-key: <key>` header for a non-empty message list; the HTTP method is POST; the URL path is `/v1beta/models/{model}:generateContent`.

**REQ-F-002** (Ubiquitous)  
The system shall populate the `contents` array in every request with only `User`- and `Model`-role messages reconstructed from the locally persisted conversation, preserving send order.

- **Acceptance Criterion:** Given a conversation with `System`, `User`, `Assistant`, `User` roles, the adapter constructs a `contents` array with exactly 3 objects (excluding System), using `role: "user"` for User-role messages and `role: "model"` for Assistant-role messages; the roles and content are in send order.

**REQ-F-003** (Ubiquitous)  
The system shall extract all `MessageRole::System` messages from the conversation's message list, concatenate their text content in order separated by blank lines (`\n\n`), and include the result as a top-level `systemInstruction` object with structure `{parts: [{text: "..."}]}` in the request body.

- **Acceptance Criterion:** Given a conversation with two System messages ("Context A" and "Context B") followed by a User message, the request body contains `"systemInstruction": {"parts": [{"text": "Context A\n\nContext B"}]}` and does not include System-role entries in the `contents` array.

**REQ-F-004** (Ubiquitous)  
If there are zero `MessageRole::System` messages in the conversation, the `systemInstruction` field shall be omitted entirely from the request body.

- **Acceptance Criterion:** A test sends a message conversation containing only User/Assistant roles; the captured request body has no `systemInstruction` key; the request is valid.

**REQ-F-005** (Ubiquitous)  
The system shall include the `temperature` parameter in `generationConfig` on every request, populated from the user-selected temperature setting in the Google settings UI (range: 0.0–2.0, default: 1.0).

- **Acceptance Criterion:** Captured request payloads for three different temperature slider positions (0.2, 1.0, 1.8) each contain the correct `"generationConfig": {"temperature": <value>}` structure.

**REQ-F-006** (Ubiquitous)  
The system shall include a `maxOutputTokens` parameter in `generationConfig` on every request, populated from the user-configured max-output-tokens setting in the Google settings UI (default: 1024, typical range: 100–4096 or higher depending on model context window).

- **Acceptance Criterion:** After entering a max-output-tokens value (e.g., 2048) in the settings panel and saving, sending a message results in a captured request containing `"generationConfig": {"maxOutputTokens": 2048}`.

**REQ-F-007** (Ubiquitous)  
The system shall set the `model` field to the user-selected model identifier (fetched via model discovery and chosen in the ComboBox).

- **Acceptance Criterion:** After selecting a model from the dropdown and sending a message, the captured request URL path contains `/v1beta/models/<selected-id>:generateContent`.

---

### F.2 Model Discovery and Filtering

**REQ-F-008** (Event-driven)  
When the user clicks the "Refresh models" button in the Google settings panel, the system shall fetch the list of available models from `GET /v1beta/models` using the configured base URL and API key (via `x-goog-api-key` header).

- **Acceptance Criterion:** A test mocks the HTTP GET endpoint; clicking the refresh button triggers a network request to `<base_url>/v1beta/models` with `x-goog-api-key` header; the response is parsed and used to update the model list.

**REQ-F-009** (Ubiquitous)  
The system shall filter the returned model list using both a denylist of substrings AND inspection of the `supportedGenerationMethods` field to exclude non-chat models: exclude any model ID containing substrings `embedding`, `moderation`, `vision`, `ocr`, AND exclude any model whose `supportedGenerationMethods` array does not include `"generateContent"` (the Gemini chat method).

- **Acceptance Criterion:** A mock model list containing `["models/gemini-2.0-flash", "models/gemini-2.0-flash-exp", "models/text-embedding-004", "models/aqa", "models/multi-candidate-vision"]` with varying `supportedGenerationMethods` (some with `["generateContent"]`, others with only `["embedContent"]` or `["imageAnalysis"]`) is filtered; the result contains only models with substring-safe IDs that also support `generateContent`.

**REQ-F-010** (Ubiquitous)  
The system shall use fail-open filtering: any model ID not matching a denylist substring AND supporting `generateContent` is included, allowing new Gemini chat models shipped by Google in the future to appear automatically without code changes.

- **Acceptance Criterion:** A hypothetical future model ID `models/gemini-3-ultra` (not on the current denylist) supporting `generateContent` is included in the filtered list; an unknown model `models/future-chat` with `generateContent` support passes the filter.

**REQ-F-011** (Ubiquitous)  
The system shall populate the model ComboBox in the settings UI with the filtered list of model IDs, maintaining the user's previously selected model if it still exists in the new list.

- **Acceptance Criterion:** After refreshing with a new model list, the ComboBox displays all non-denylist models supporting `generateContent`; if the user had selected `models/gemini-2.0-flash` and it remains in the new list, the ComboBox shows it as selected; if the selected model was removed, the ComboBox reverts to the first available model.

---

### F.3 Streaming Event Mapping to Domain Types

**REQ-F-012** (State-driven)  
While a Gemini streaming response is in progress, the system shall buffer incoming bytes from the `HttpClient::sendStreaming()` callback and parse Server-Sent Events (SSE) framing: consecutive blocks separated by blank lines (`\n\n`), each prefixed with `data: `, payload parsed as JSON.

- **Acceptance Criterion:** A test delivers fragmented byte chunks (split mid-JSON-object, mid-field-name) via mock `on_data` callbacks; the adapter correctly reassembles and parses five distinct SSE blocks despite non-aligned chunk boundaries; each is dispatched as a separate event.

**REQ-F-013** (State-driven)  
While processing an SSE block, the system shall extract the response object structure (Gemini's `candidates` array) and route based on the content and finish reason.

- **Acceptance Criterion:** A mock SSE stream with mixed event types (partial content deltas, completion events) is parsed; the adapter extracts text from `candidates[0].content.parts[0].text` and handles finish reasons correctly.

**REQ-F-014** (Event-driven)  
When an SSE event containing `candidates[0].content.parts[0].text` is received with text content, the system shall emit a domain `ContentDelta` stream event with the delta text.

- **Acceptance Criterion:** An SSE event `{"candidates": [{"content": {"parts": [{"text": "Hello"}]}}]}` emits a domain event of variant type `ContentDelta` with text "Hello"; captured in a test listener.

**REQ-F-015** (Event-driven)  
When an SSE event is received with `candidates[0].finishReason` set to `"STOP"`, the system shall emit a domain `Completed` stream event. Token exhaustion, filtering, malformed output, and unknown terminal reasons shall emit a user-readable domain `Error` instead of marking a truncated or rejected response successful.

- **Acceptance Criterion:** An SSE block containing `{"candidates": [{"finishReason": "STOP"}]}` emits a domain event of variant type `Completed`; downstream message finalization logic proceeds.

**REQ-F-016** (Event-driven)  
When an SSE event is received with `candidates[0].finishReason` set to `"SAFETY"` or `"RECITATION"` with no or minimal text content, the system shall emit a domain `Error` stream event with a user-readable message (e.g., "Response blocked by Google's safety filters.").

- **Acceptance Criterion:** An SSE event `{"candidates": [{"finishReason": "SAFETY", "content": {"parts": []}}]}` emits an `Error` event with message "Response blocked by Google's safety filters."; an event with `finishReason: "RECITATION"` emits an `Error` event with message "Response blocked due to recitation concerns.".

**REQ-F-017** (Event-driven)  
When an SSE event is received containing usage information (e.g., `usageMetadata.inputTokenCount`, `usageMetadata.outputTokenCount`), the system shall extract and forward this to the domain layer if the codebase already tracks usage in `StreamEvent` (optional; if not currently tracked, this shall be a no-op).

- **Acceptance Criterion:** If `StreamEvent` has a `Usage` variant, an SSE event containing `"usageMetadata": {"inputTokenCount": 10, "outputTokenCount": 42}` emits a domain `Usage` event with output token count 42; if no `Usage` variant exists, the adapter silently ignores the usage data.

**REQ-F-018** (Unwanted-behavior)  
If the Gemini response includes an error structure in the SSE payload, a non-2xx HTTP status code on the initial request, or the stream ends without emitting a terminal finish reason, then the system shall emit a domain `Error` stream event containing the extracted error message from Google's JSON structure.

- **Acceptance Criterion:** A 401 Unauthorized response with body `{"error": {"code": 401, "message": "API key not valid"}}` emits an `Error` event with message extracted from the error structure; a 429 response with body `{"error": {"code": 429, "message": "Resource exhausted"}}` emits an `Error` event with that message; a stream that delivers deltas but never emits a terminal finish reason emits an `Error` event with a descriptive message (e.g., "Stream ended without completion").

**REQ-F-019** (Ubiquitous)  
The system shall handle all SSE events that do not contain `candidates[0].content.parts[0].text` or `candidates[0].finishReason` as partial/intermediate events, buffer them safely, and emit appropriate domain events only when enough data is available.

- **Acceptance Criterion:** An SSE stream containing multiple partial events (empty text, only usage data, only cite metadata) is processed without crashing; only events with text or terminal finish reasons trigger domain event emissions.

---

### F.4 Error Handling and User Surfacing

**REQ-F-020** (Unwanted-behavior)  
If the user enters an invalid Google API key and attempts to send a message, then the system shall surface a readable error message to the UI (e.g., "Authentication failed: Invalid API key") rather than crashing, hanging, or silently retrying indefinitely.

- **Acceptance Criterion:** A test sends a message with a mocked 401 Unauthorized response containing error details; the application emits an error message through the existing error-reporting pathway; the UI displays the error in the chat or message log; no unhandled exceptions, no silent retry loops, no frozen UI.

**REQ-F-021** (Unwanted-behavior)  
If the user triggers a request during a rate-limit condition (429 Too Many Requests), then the system shall surface the rate-limit error message to the UI; automatic retry shall not occur at the adapter level.

- **Acceptance Criterion:** A mocked 429 response with Google's error structure is delivered; an error message surfaces to the UI; the adapter does not automatically retry; the user can manually click "Retry" in the application's existing retry UI.

**REQ-F-022** (Unwanted-behavior)  
If the Gemini API rejects a request due to an invalid parameter combination (400 Bad Request), then the system shall surface the raw Google error message to the UI.

- **Acceptance Criterion:** A test delivers a mocked 400 response with error message; the UI displays this message verbatim; no client-side model detection or parameter pre-validation blocks the request.

**REQ-F-023** (Unwanted-behavior)  
If a network error occurs (timeout, connection refused, DNS failure), then the system shall emit an `Error` domain event with a descriptive message (e.g., "Network error: connection timeout").

- **Acceptance Criterion:** A test with a mocked unreachable host triggers a timeout; an `Error` event is emitted with a user-readable message; no unhandled exceptions propagate to the UI.

**REQ-F-024** (Unwanted-behavior)  
If the Gemini API response includes a safety-filter block with `finishReason: "SAFETY"` or `"RECITATION"` and no generated text, the system shall surface this as a readable error message (distinct from a network error or authentication failure).

- **Acceptance Criterion:** An SSE event with `finishReason: "SAFETY"` and empty text emits an `Error` event with message "Response blocked by Google's safety filters."; the user sees this message in the chat UI, not a generic "error" placeholder.

---

### F.5 Cancellation

**REQ-F-025** (Event-driven)  
When the user clicks the Stop button during an active Gemini streaming response, the system shall cancel the HTTP request via the existing `HttpRequestHandle::cancel()` mechanism.

- **Acceptance Criterion:** A test starts a mocked streaming request; the `cancel()` method is invoked while deltas are being emitted; the stream halts; no further deltas are processed; the HTTP connection is closed.

**REQ-F-026** (Ubiquitous)  
The system shall return a `HttpRequestHandlePtr` from `GoogleProvider::sendChat()`, enabling the application layer to manage cancellation.

- **Acceptance Criterion:** The `sendChat()` method signature matches the pattern of `OllamaProvider::sendChat()`, `OpenAIProvider::sendChat()`, and `AnthropicProvider::sendChat()`, returning a handle that supports `cancel()`.

---

### F.6 Settings UI — Google Provider Configuration Panel

**REQ-F-027** (Ubiquitous)  
The system shall display a `GoogleSettingsPanel.qml` component in the provider list (replacing the current "Google" placeholder) with the following input fields and controls:
1. Base URL (TextField, default: `https://generativelanguage.googleapis.com`)
2. Default model (ComboBox, populated by dynamic model fetch)
3. Refresh models (Button)
4. Temperature (Slider + SpinBox, range: 0.0–2.0, default: 1.0)
5. Max output tokens (SpinBox, range: typical 100–4096+, default: 1024)
6. API key (TextField with show/hide toggle and Clear button)
7. Secret storage unavailable (notice, displayed when credential store is unavailable)
8. Test Connection (Button)
9. Reset / Cancel / Save (footer buttons)

- **Acceptance Criterion:** The Google provider row in `ProvidersPage.qml` loads `GoogleSettingsPanel.qml` (not a placeholder); all listed fields are present and interactive; the temperature control supports the wider 0.0–2.0 range (distinct from Anthropic's 0.0–1.0).

**REQ-F-028** (Event-driven)  
When the user modifies the Base URL, default model, temperature, or max-output-tokens in the Google settings panel and clicks Save, the system shall persist these values to the config repository via `ConfigRepository::saveGoogleConfig()`.

- **Acceptance Criterion:** After entering a custom base URL, selecting a model, adjusting temperature and max-output-tokens, and clicking Save, a subsequent application restart loads the same values; config is persisted to the underlying JSON store.

**REQ-F-029** (Event-driven)  
When the user enters an API key in the key field and clicks Save, the system shall store the key in the credential store via `CredentialStore::store("google", key)`.

- **Acceptance Criterion:** After entering a key and saving, a test retrieves the stored credential via `store->retrieve("google")` and confirms it matches the entered value; the key is stored separately from config (not in JSON).

**REQ-F-030** (Event-driven)  
When the user clicks the eye-icon toggle on the API key field, the system shall toggle the field's visibility between plain text and masked (asterisks/dots).

- **Acceptance Criterion:** Clicking the eye icon switches the TextField's `echoMode` from `TextInput.Password` to `TextInput.Normal` and back; the key value is hidden when masked and revealed when unmasked.

**REQ-F-031** (Event-driven)  
When the user clicks the Clear button next to the API key field, the system shall erase the field's content and (upon Save) remove the key from the credential store via `CredentialStore::remove("google")`.

- **Acceptance Criterion:** After entering a key, clicking Clear blanks the field; clicking Save triggers `remove("google")`; a subsequent query to the credential store returns no credential for "google".

**REQ-F-032** (Conditional)  
Where the credential store is unavailable (e.g., D-Bus failure, Secret Service daemon not running), the system shall display a "Secret storage unavailable" notice in the settings panel and disable the API key field.

- **Acceptance Criterion:** A test simulates a credential-store initialization failure; the Google settings panel displays a warning message; the API key field is disabled (read-only, grayed out); the user cannot enter or save a key.

**REQ-F-033** (Event-driven)  
When the user clicks the Test Connection button, the system shall make a request to `GET /v1beta/models` using the current base URL and key settings (with `x-goog-api-key` header); if successful, display a confirmation message ("Connection successful"); if unsuccessful, display the error message from Google.

- **Acceptance Criterion:** With a valid key and base URL, clicking Test Connection triggers a fetch and displays "Connection successful" when the response is 2xx; with an invalid key, the error message is displayed; network errors display a descriptive message.

---

### F.7 Configuration Persistence

**REQ-F-034** (Ubiquitous)  
The system shall add a new `GoogleProviderConfig` struct to `holonight_config` with the following fields, using `QString`/Qt types to match existing conventions:
- `base_url: QString` (default: `https://generativelanguage.googleapis.com`)
- `default_model: QString` (default: empty)
- `temperature: double` (default: 1.0, range: 0.0–2.0)
- `max_output_tokens: int` (default: 1024, typical range: 100–4096)
- `friend bool operator==(...) = default;` (matching existing pattern)

- **Acceptance Criterion:** The struct is defined in `src/config/include/holonight_config/provider_config.h` alongside `OllamaProviderConfig`, `OpenAIProviderConfig`, and `AnthropicProviderConfig`; it compiles without warnings; no field uses `std::string` where `QString` is used elsewhere.

**REQ-F-035** (Ubiquitous)  
The system shall implement `ConfigRepository::loadGoogleConfig()` and `ConfigRepository::saveGoogleConfig()` methods with signatures and error-handling conventions that exactly match `loadOllamaConfig`/`saveOllamaConfig`: `loadGoogleConfig()` returns a plain `GoogleProviderConfig` (never throws; returns default-constructed values on a missing/malformed file), and `saveGoogleConfig(const GoogleProviderConfig&)` returns `std::expected<void, QString>` with a failure reason on write error.

- **Acceptance Criterion:** Both methods are declared and defined with the exact signatures `GoogleProviderConfig loadGoogleConfig() const` and `std::expected<void, QString> saveGoogleConfig(const GoogleProviderConfig&) const`; neither throws; a missing config file makes `loadGoogleConfig()` return default values.

**REQ-F-036** (Ubiquitous)  
The system shall NOT include API key/secret fields in the `GoogleProviderConfig` struct; key storage remains exclusively in the credential store (Secret Service / KWallet via `CredentialStore`).

- **Acceptance Criterion:** The `GoogleProviderConfig` struct has no `key`, `secret`, `api_key`, or similar field; the struct compiles and is used to persist only URL, model, temperature, and max_output_tokens.

**REQ-F-037** (Ubiquitous)  
The system shall persist config under the existing namespaced `"providers"."google"` key in `config.json`, using the same read-merge-write pattern already used for `"ollama"`, `"openai"`, and `"anthropic"`, without modifying those sections.

- **Acceptance Criterion:** Inspecting `config.json` after saving Google settings shows keys under `"providers"."google"` (e.g., `{"providers": {"google": {"base_url": "...", "temperature": 1.0, ...}}}`); the `"ollama"`, `"openai"`, and `"anthropic"` sections remain unchanged.

---

### F.8 Credential Integration

**REQ-F-038** (Ubiquitous)  
The system shall reuse the existing `holonight_credentials::CredentialStore` interface without modification, calling `store(providerId, secret)` with `providerId = "google"` to save the API key.

- **Acceptance Criterion:** The GoogleProvider instance calls `credential_store->store("google", key)` when saving settings; the same provider instance calls `credential_store->retrieve("google")` when preparing a request; both operations use the provider ID "google".

**REQ-F-039** (Unwanted-behavior)  
If no credential is stored for `"google"` (empty/not-found result from `CredentialStore::retrieve("google")`), then the system shall send the request without an `x-goog-api-key` header — mirroring OllamaProvider's existing behavior of omitting the auth header entirely when its token is empty — allowing Google's resulting 401 response to surface through the normal REQ-F-020 error path.

- **Acceptance Criterion:** A test with no stored credential for "google" sends a request with no `x-goog-api-key` header present at all; the mocked 401 response is surfaced as an error via the same path as REQ-F-020; no crash, no silent failure, no client-side pre-request blocking.

---

### F.9 Application-Layer Integration

**Note on current architecture:** GoogleProvider shall likewise be a plain concrete class mirroring OllamaProvider, OpenAIProvider, and AnthropicProvider's method shapes — it shall NOT introduce a virtual base class or dynamic-dispatch interface, since none exists in the current codebase. The exact mechanism by which the application layer selects which provider instance backs the active conversation (e.g., provider routing in `ChatController`) is left to Stage 2 (Design) and out of scope for this Spec.

**REQ-F-040** (Ubiquitous)  
The system shall provide a `GoogleProvider` class in `holonight_providers` with a method surface (`sendChat()`, `availableModels()`, `refresh()`, `setBaseUrl()`/`baseUrl()`, `setAuthKey()`, `setTemperature()`, `setMaxOutputTokens()`) structurally equivalent to `OllamaProvider`, `OpenAIProvider`, and `AnthropicProvider`, so that Stage 2's design for wiring it into the application layer can follow the same shape.

- **Acceptance Criterion:** `GoogleProvider`'s public method signatures are reviewed side-by-side with `OllamaProvider`, `OpenAIProvider`, and `AnthropicProvider`; each has a structurally equivalent counterpart (same parameter shapes, same callback-based async pattern) except where a requirement in this Spec explicitly says otherwise (e.g., `setAuthKey()` instead of `setAuthToken()` mirrors AnthropicProvider's naming).

**REQ-F-041** (Ubiquitous)  
The system shall add a `GoogleProviderSettingsController` QML-facing bridge in `holonight_application`, mirroring `OpenAIProviderSettingsController` and `AnthropicProviderSettingsController`, wiring the Settings window's Google provider panel.

- **Acceptance Criterion:** The bridge exists, is registered as a `QML_SINGLETON` in the `holonight_application` target, and provides methods/properties for loading/saving Google config, testing connection, and refreshing models.

**REQ-F-042** (Ubiquitous)  
The `GoogleProviderSettingsController` shall reuse the same `holonight_credentials::CredentialStore` instance already shared by the other provider settings controllers, requiring no changes to the credential store's storage format or API.

- **Acceptance Criterion:** The controller instantiates or accepts a shared `CredentialStore` pointer; credentials are stored with provider ID "google"; no new credential-store methods or storage format changes are required.

---

## Non-Functional Requirements

### N.1 Testing and Code Coverage

**REQ-NF-001** (Ubiquitous)  
All new C++ code in `GoogleProvider` shall have matching GTest unit tests covering normal flow, error cases, and edge cases (stream fragmentation, malformed JSON, missing fields, empty model list, system-prompt hoisting, safety-filter blocks, role mapping, etc.).

- **Acceptance Criterion:** A test executable `test_google_provider` is added under `tests/`; it achieves >80% line coverage of `src/providers/src/google_provider.cpp`; test cases cover request construction, role mapping (user→"user", assistant→"model"), system-prompt handling, SSE parsing, event mapping, error handling (including safety/recitation blocks), model filtering with `supportedGenerationMethods`, and cancellation.

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
Model discovery (GET /v1beta/models) shall complete within a configurable timeout (default: 10 seconds); if the request times out, an error message shall be displayed and the model list shall not be updated.

- **Acceptance Criterion:** A test mocks a delayed response (>10 seconds); the request times out; an error message surfaces to the UI; the ComboBox is not updated.

---

### N.3 Security

**REQ-NF-006** (Ubiquitous)  
API keys shall never be logged, printed to stdout/stderr, or persisted in plaintext to config files; they shall be stored exclusively via the credential store (Secret Service / KWallet).

- **Acceptance Criterion:** Grep the codebase for calls to `qDebug()`, `std::cout`, `std::cerr`, config file writes that involve the key variable; no matches where the key is logged; credential store is the sole persistence mechanism.

---

## Constraint Requirements (Explicitly Out-of-Scope)

**REQ-C-001** (Ubiquitous)  
The system shall NOT implement tool/function calling support for this cycle, even if Gemini's API response includes tool-related fields (e.g., `functionCall` parts).

- **Acceptance Criterion:** A test delivers a mock response containing tool definitions or usage; the adapter ignores them; no tool-registry or execution occurs.

**REQ-C-002** (Ubiquitous)  
The system shall NOT support image, file, or attachment input for this cycle.

- **Acceptance Criterion:** The Google settings UI has no file-upload field, no image-picker, no attachment controls; the `contents` array contains only `{role: "user"|"model", parts: [{text}]}` (text-only).

**REQ-C-003** (Ubiquitous)  
The system shall NOT implement or expose `maxOutputTokens` with different validation rules or ranges than the standard Gemini API (typically 100–context_window_size).

- **Acceptance Criterion:** The settings UI accepts numeric input for maxOutputTokens in a reasonable range; no special-casing or model-specific validation occurs in the adapter.

**REQ-C-004** (Ubiquitous)  
The system shall NOT include any fields in the request not explicitly required by this Spec or by Google's Gemini API documentation (e.g., no `safetySettings`, `tools`, `toolConfig`, or experimental features).

- **Acceptance Criterion:** Request payloads contain only documented fields: `contents` array, `systemInstruction` (if applicable), `generationConfig` with temperature/maxOutputTokens, plus standard HTTP headers (`x-goog-api-key`, `content-type`).

**REQ-C-005** (Ubiquitous)  
The system shall NOT use server-side conversation retention or multi-turn state management beyond sending the full conversation history on each request.

- **Acceptance Criterion:** Every request includes the complete conversation history; no server-side conversation ID or session token is retained.

**REQ-C-006** (Ubiquitous)  
The system shall NOT implement automatic retry logic on 429 (rate limit) or 5xx (server error) responses at the adapter level; the application-layer manual retry UI already implemented shall be the sole retry mechanism.

- **Acceptance Criterion:** A test mocks a 429 response; the adapter emits an Error event; no automatic retry occurs; the application's existing "Retry" button allows manual retry.

**REQ-C-007** (Ubiquitous)  
The system shall NOT support Vertex AI endpoints, alternate authentication schemes (OAuth, service accounts, GCP project routing), or on-premises/custom deployments.

- **Acceptance Criterion:** The base URL field accepts only standard `https://generativelanguage.googleapis.com` or compatible proxies with `x-goog-api-key` auth; no endpoint routing or alternate auth schemes occur.

**REQ-C-008** (Ubiquitous)  
The system shall NOT support custom `safetySettings` configuration in the UI or payload; safety filtering is left to Google's API defaults.

- **Acceptance Criterion:** The Google settings panel has no checkbox or dropdown for safety levels; no `safetySettings` field is included in the request body.

**REQ-C-009** (Ubiquitous)  
The system shall NOT support Google Search grounding or any other Gemini extension features (codeExecution, retrieval, etc.) for this cycle.

- **Acceptance Criterion:** The settings panel has no toggle or configuration for Grounding or extensions; no `tools` or `toolConfig` fields appear in requests; responses mentioning grounding results are treated as unsupported fields.

---

## Non-Goals

The following features are explicitly deferred to future cycles and shall NOT be implemented in this cycle:

- **Multimodal input support** — image, file, and video attachments deferred to a future cycle.
- **Tool/function calling** — deferred to future cycle.
- **Vertex AI integration** — only Gemini Developer API is supported.
- **Google Search grounding tool** — deferred to future cycle.
- **Custom safety settings configuration** — API defaults only.
- **Changes to credential store format or API** — only Google-specific header application.
- **Changes to domain types** — no new `MessageRole`, `Message`, `Conversation`, or `StreamEvent` variants; adapter maps onto existing shapes exactly.
- **Model-specific parameter constraints** — e.g., no client-side temperature validation per model.
- **Extended thinking or reasoning mode** — deferred to future cycle.

---

## Acceptance Criteria Summary

A user can:
1. Enter a Google Gemini API key and base URL in the provider settings panel.
2. Click "Refresh models" and see live-fetched models (denylist + `supportedGenerationMethods`-filtered) populate the ComboBox.
3. Select a model, temperature (0.0–2.0), and max-output-tokens setting, then click Save.
4. Send a chat message (with or without system context) and observe a streamed reply appear token-by-token in the message list.
5. Encounter errors (401, 429, 400, network failure, safety-filter blocks) and see a readable error message in the UI (including distinct messages for safety/recitation blocks).
6. Click Stop during streaming and halt the request.
7. Reload the application and see all Google settings (URL, model, temperature, max_output_tokens) restored.

All new C++ code passes GTest with >80% line coverage; no real network calls occur in CI; code adheres to clang-format and clang-tidy standards. System prompts are correctly hoisted to the `systemInstruction` field; `maxOutputTokens` is sent on every request; roles are correctly mapped (`"user"` for user messages, `"model"` for assistant messages); `x-goog-api-key` header is used for authentication; temperature range supports 0.0–2.0 per Gemini's documented range.

---

## Verification Strategy

### Unit Tests
- `tests/test_google_provider.cpp` — request construction (role mapping user→"user", assistant→"model", system-prompt hoisting, maxOutputTokens presence), SSE parsing (including fragmentation), event mapping, model filtering with `supportedGenerationMethods`, error cases (including safety/recitation blocks), cancellation.

### Integration Tests
- QML panel tests — settings persistence (including maxOutputTokens and wider temperature range), credential storage/retrieval, UI state transitions.

### Manual Testing Checklist (provided to user after implementation)
1. Add valid Google Gemini API key; fetch and select a model; send a message; observe streamed reply token-by-token.
2. Enter a message with system context; verify it appears in the request's top-level `systemInstruction` field (not inline in `contents`).
3. Enter a temperature value in the wider range (e.g., 1.8); verify it appears in `generationConfig.temperature`.
4. Enter a max-output-tokens value (e.g., 2048); verify it appears in `generationConfig.maxOutputTokens`.
5. Enter invalid API key; observe 401 error surface in UI.
6. Trigger rate limit; observe 429 error; manually retry via application UI.
7. Receive a safety-filter block response; verify a distinct error message appears (e.g., "Response blocked by Google's safety filters.").
8. Click Stop during streaming; verify stream halts.
9. Customize base URL; verify requests use the new endpoint.
10. Verify all settings (URL, model, temperature, maxOutputTokens) persist across application restart.
11. Test connection with valid and invalid credentials; verify error messages are readable.
12. Refresh models with a live connection; verify only chat-capable models (supporting `generateContent`) appear; verify denylist filtering excludes embedding/vision models.

---

## Verification by Requirements

### Role Mapping (REQ-F-002, REQ-NF-001)
- Test constructs `contents` array with a User message and an Assistant message; verifies `role: "user"` for User and `role: "model"` for Assistant (not `"assistant"`).
- Test with mixed roles verifies correct mapping throughout the array.

### System-Prompt Handling (REQ-F-003, REQ-F-004, REQ-NF-001)
- Test concatenates two System messages; verifies top-level `systemInstruction: {parts: [{text: "..."}]}` contains both messages separated by `\n\n`; no System entries in `contents` array.
- Test sends message with zero System messages; verifies request lacks `systemInstruction` field entirely.

### MaxOutputTokens Handling (REQ-F-006, REQ-NF-001)
- Test sets maxOutputTokens to 2048 in settings; captures request; verifies `"generationConfig": {"maxOutputTokens": 2048}` is present.
- Default value (1024) is used if user does not configure.

### Temperature Range (REQ-F-005, REQ-NF-001)
- Test verifies temperature accepts values across the full 0.0–2.0 range (distinct from Anthropic's 0.0–1.0).
- Slider and field accept and display values like 1.8 correctly.

### Authentication (REQ-F-001, REQ-F-039)
- Test verifies every request includes `x-goog-api-key: <key>` header (not `x-api-key` or `Authorization: Bearer`).
- Test with missing credential verifies `x-goog-api-key` header is absent and 401 error surfaces.

### SSE Parsing (REQ-F-012, REQ-F-013, REQ-F-019)
- Test delivers fragmented SSE blocks; verifies correct assembly and parsing.
- Test delivers mixed events; verifies correct extraction of text and finish reasons.

### Role Mapping Edge Cases (REQ-F-002, REQ-NF-001)
- Test with only User messages; verifies `contents` array contains only `role: "user"` entries.
- Test with alternating User/Assistant; verifies roles alternate correctly (`"user"`, `"model"`, `"user"`, etc.).

### Model Discovery (REQ-F-008, REQ-F-009, REQ-F-010, REQ-F-011)
- Test calls refresh; verifies GET request to `/v1beta/models` with `x-goog-api-key` header.
- Test filters model list using both denylist AND `supportedGenerationMethods` inspection; verifies embedding/vision/moderation models excluded; only `generateContent`-supporting models included.
- Test with future model ID (not on denylist) supporting `generateContent` verifies it passes through.

### Safety-Filter Handling (REQ-F-016, REQ-F-024)
- Test with `finishReason: "SAFETY"` verifies distinct error message "Response blocked by Google's safety filters." is emitted.
- Test with `finishReason: "RECITATION"` verifies distinct error message "Response blocked due to recitation concerns." is emitted.

---

## Dependencies and Integration Points

- **Existing interfaces used:** `HttpClient::sendStreaming()`, `CredentialStore`, `ConfigRepository`, `HttpRequestHandle::cancel()`, domain `StreamEvent` type.
- **New QML file:** `qml/workspace/GoogleSettingsPanel.qml` (replaces placeholder).
- **New C++ files:** `src/providers/src/google_provider.cpp`, `src/providers/include/holonight_providers/google_provider.h`, `tests/test_google_provider.cpp`.
- **Modified files:** 
  - `src/config/include/holonight_config/provider_config.h` (add `GoogleProviderConfig` struct)
  - `src/config/src/config_repository.cpp` (add `loadGoogleConfig()` and `saveGoogleConfig()` methods)
  - `src/config/include/holonight_config/config_repository.h` (add method declarations)
  - Provider routing in `ChatController` or similar (Stage 2 decision)
  - `qml/workspace/ProviderListPanel.qml` or `ProvidersPage.qml` (replace placeholder with functional panel reference)
- **New QML controller:** `GoogleProviderSettingsController` registered in `holonight_application`.

---

**End of Specification**
