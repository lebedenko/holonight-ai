# OpenAI Provider Adapter — Specification

**Version:** 1.0  
**Status:** Ready for Implementation  
**Module:** `holonight_providers`  
**Architecture Tier:** Domain-agnostic provider adapter (same as `OllamaProvider`)

---

## Executive Summary

This specification defines the integration of OpenAI's Responses API (`POST /v1/responses`) into the holonight-ai chat application. The implementation reuses existing interfaces for credential storage, configuration persistence, application-layer retry logic, and streaming HTTP cancellation, requiring only new C++ adapter logic in `holonight_providers` and new QML UI in `qml/workspace/OpenAISettingsPanel.qml` to replace the current "Coming soon: OpenAI" placeholder.

---

## Functional Requirements

### F.1 API Request Construction

**REQ-F-001** (Ubiquitous)  
The system shall construct HTTP POST requests to the OpenAI Responses API endpoint `/v1/responses` using the configurable base URL (default: `https://api.openai.com/v1`) and Bearer token authentication.

- **Acceptance Criterion:** A test fixture verifies that an OpenAIProvider instance, when configured with a base URL and API key, constructs a request body with all required fields and an `Authorization: Bearer <key>` header for a non-empty message list; the HTTP method is POST; the URL path is `/v1/responses`.

**REQ-F-002** (Ubiquitous)  
The system shall populate the `input` array in every request with the complete conversation history as `{role, content}` tuples reconstructed from the locally persisted conversation.

- **Acceptance Criterion:** Given a 3-message conversation (user→assistant→user), the adapter constructs an `input` array with exactly 3 objects in send order; each object has exactly `role` and `content` fields with correct values; no fields are omitted or truncated.

**REQ-F-003** (Ubiquitous)  
The system shall always include `store: false` in the request body.

- **Acceptance Criterion:** Inspecting a captured/mocked HTTP request payload confirms the presence of `"store": false`.

**REQ-F-004** (Ubiquitous)  
The system shall NOT include a `previous_response_id` field in any request.

- **Acceptance Criterion:** A test captures the request body and confirms the absence of any `previous_response_id` key, even after sending multiple consecutive requests to the same provider instance.

**REQ-F-005** (Ubiquitous)  
The system shall include the `temperature` parameter in every request, populated from the user-selected temperature setting in the OpenAI settings UI (range: 0.0–2.0, default: 1.0 per OpenAI's standard).

- **Acceptance Criterion:** Captured request payloads for three different temperature slider positions (0.2, 1.0, 1.8) each contain the correct `"temperature"` value as a JSON number.

**REQ-F-006** (Ubiquitous)  
The system shall NOT include `max_output_tokens` in the request body.

- **Acceptance Criterion:** A request payload inspection confirms the absence of `max_output_tokens`; the model's server-side default output ceiling applies.

**REQ-F-007** (Ubiquitous)  
The system shall set the `model` field to the user-selected model identifier (fetched via model discovery and chosen in the ComboBox).

- **Acceptance Criterion:** After selecting a model from the dropdown and sending a message, the captured request contains `"model": "<selected-id>"` matching the chosen model's exact ID string.

---

### F.2 SSE Stream Parsing

**REQ-F-008** (State-driven)  
While an OpenAI streaming response is in progress, the system shall buffer incoming bytes from the `HttpClient::sendStreaming()` callback and parse Server-Sent Events (SSE) framing: consecutive blocks separated by blank lines (one or more `\n\n` sequences), each prefixed with `data: `, payload parsed as JSON.

- **Acceptance Criterion:** A test delivers fragmented byte chunks (split mid-JSON-object, mid-field-name) via mock `on_data` callbacks; the adapter correctly reassembles and parses three distinct SSE blocks despite non-aligned chunk boundaries; each is dispatched as a separate event.

**REQ-F-009** (State-driven)  
While processing an SSE block, the system shall extract the `type` field from the JSON payload and route based on its value (e.g., `response.output_text.delta`, `response.completed`).

- **Acceptance Criterion:** A mock SSE stream with mixed event types (`response.output_text.delta`, `response.created`, `response.completed`) is parsed; type-based routing is confirmed by verifying the correct handler is invoked for each type (verified via call counts on mock/spy objects).

---

### F.3 Streaming Event Mapping to Domain Types

**REQ-F-010** (Event-driven)  
When an SSE event of type `response.output_text.delta` is received containing a `delta` string field, the system shall emit a domain `ContentDelta` stream event with the delta text.

- **Acceptance Criterion:** An SSE event `{"type": "response.output_text.delta", "delta": "Hello"}` emits a domain event of variant type `ContentDelta` with text "Hello"; captured in a test listener.

**REQ-F-011** (Event-driven)  
When an SSE event of type `response.refusal.delta` is received, the system shall emit a domain `ContentDelta` stream event with the delta text, treating refusals identically to output text.

- **Acceptance Criterion:** An SSE event `{"type": "response.refusal.delta", "delta": "I can't help with that"}` emits a `ContentDelta` event; the domain layer and UI render it as an assistant message (no special refusal flag, no error path).

**REQ-F-012** (Event-driven)  
When an SSE event of type `response.completed` is received, the system shall emit a domain `Completed` stream event.

- **Acceptance Criterion:** An SSE block `{"type": "response.completed"}` emits a domain event of variant type `Completed`; downstream message finalization logic proceeds.

**REQ-F-013** (Unwanted-behavior)  
If the OpenAI response includes an error SSE event (top-level `error` type), a non-2xx HTTP status code on the initial request, or the stream ends without emitting a terminal event (neither `response.completed` nor `response.failed`), then the system shall emit a domain `Error` stream event containing the raw error message text from OpenAI's JSON `error.message` field (or HTTP status line if the error predates SSE framing).

- **Acceptance Criterion:** A 401 Unauthorized response with body `{"error": {"message": "Invalid API key"}}` emits an `Error` event with message "Invalid API key"; a 429 response with body `{"error": {"message": "Rate limit exceeded"}}` emits an `Error` event with that message; a stream that delivers deltas but never a terminal event emits an `Error` event with a descriptive message (e.g., "Stream ended without completion").

**REQ-F-014** (Event-driven)  
When the OpenAI response includes `response.failed` or `response.incomplete` event types, the system shall emit a domain `Error` stream event with the error details.

- **Acceptance Criterion:** An SSE event `{"type": "response.failed", "error": {"message": "Model overloaded"}}` emits an `Error` event with message containing "overloaded".

**REQ-F-015** (Ubiquitous)  
The system shall silently ignore (no-op) all other SSE event types, including but not limited to `response.created`, `response.in_progress`, `response.output_item.*`, `response.content_part.*`, `response.function_call_arguments.*`, code-interpreter progress events, and annotation events.

- **Acceptance Criterion:** An SSE stream containing `response.created`, `response.in_progress`, `response.output_item.added`, and `response.completed` events is processed; only `output_text.delta` and `completed` events trigger domain event emissions (verified by checking emitted event count equals 2, not 5).

---

### F.4 Model Discovery and Filtering

**REQ-F-016** (Event-driven)  
When the user clicks the "Refresh models" button in the OpenAI settings panel, the system shall fetch the list of available models from `GET /v1/models` using the configured base URL and API key.

- **Acceptance Criterion:** A test mocks the HTTP GET endpoint; clicking the refresh button triggers a network request to `<base_url>/v1/models` with a Bearer Authorization header; the response is parsed and used to update the model list.

**REQ-F-017** (Ubiquitous)  
The system shall filter the returned model list using a denylist of substrings to exclude non-chat model families: `embedding-`, `tts-`, `whisper-`, `dall-e`, `gpt-image`, `omni-moderation`, `text-moderation`, `davinci-002`, `babbage-002`, `sora-`.

- **Acceptance Criterion:** A mock model list containing `["gpt-4o", "gpt-4-turbo", "text-embedding-3-large", "whisper-1", "dall-e-3", "gpt-4o-mini"]` is filtered; the result is `["gpt-4o", "gpt-4-turbo", "gpt-4o-mini"]`; all denylist entries are correctly excluded.

**REQ-F-018** (Ubiquitous)  
The system shall use fail-open filtering: any model ID not matching a denylist substring is included, allowing new chat models shipped by OpenAI in the future to appear automatically without code changes.

- **Acceptance Criterion:** A hypothetical future model ID `gpt-5-ultra` (not on the current denylist) is included in the filtered list; an unknown model `experimental-xyz` passes the denylist filter and is included.

**REQ-F-019** (Ubiquitous)  
The system shall populate the model ComboBox in the settings UI with the filtered list of model IDs, maintaining the user's previously selected model if it still exists in the new list.

- **Acceptance Criterion:** After refreshing with a new model list, the ComboBox displays all non-denylist models; if the user had selected `gpt-4o` and it remains in the new list, the ComboBox shows `gpt-4o` as selected; if the selected model was removed, the ComboBox reverts to the first available model.

---

### F.5 Error Handling and User Surfacing

**REQ-F-020** (Unwanted-behavior)  
If the user enters an invalid OpenAI API key and attempts to send a message, then the system shall surface a readable error message to the UI (e.g., "Authentication failed: Invalid API key") rather than crashing, hanging, or silently retrying indefinitely.

- **Acceptance Criterion:** A test sends a message with a mocked 401 Unauthorized response; the application emits an error message through the existing error-reporting pathway; the UI displays the error in the chat or message log; no unhandled exceptions, no silent retry loops, no frozen UI.

**REQ-F-021** (Unwanted-behavior)  
If the user triggers a request during a rate-limit condition (429 Too Many Requests), then the system shall surface the rate-limit error message to the UI; automatic retry shall not occur at the adapter level (application-layer manual retry already implemented).

- **Acceptance Criterion:** A mocked 429 response with `{"error": {"message": "Rate limit exceeded"}}` is delivered; an error message surfaces to the UI; the adapter does not automatically retry; the user can manually click "Retry" in the application's existing retry UI.

**REQ-F-022** (Unwanted-behavior)  
If the OpenAI API rejects a request due to an invalid parameter combination (400 Bad Request, for a reasoning model or any other reason), then the system shall surface the raw OpenAI error message to the UI.

- **Acceptance Criterion:** A test delivers a mocked 400 response `{"error": {"message": "temperature is not supported for this model"}}`; the UI displays this message verbatim; no client-side model detection or blocking of any model occurs.
- **Note (verified 2026-07-23 via manual testing against the live API):** as of this date, OpenAI's Responses API no longer rejects `temperature` for reasoning models (`o3-mini`, `o4-mini`, `gpt-5.4-mini` all completed normally with the adapter's unconditional `temperature` field present). The original premise — that sending `temperature` to a reasoning model reliably produces a 400 — no longer holds; this requirement now exists to handle whatever 400 response OpenAI *does* send, from any cause, not specifically a reasoning-model rejection. REQ-C-003/REQ-C-004 (no client-side reasoning-model special-casing) remain satisfied regardless.

**REQ-F-023** (Unwanted-behavior)  
If a network error occurs (timeout, connection refused, DNS failure), then the system shall emit an `Error` domain event with a descriptive message (e.g., "Network error: connection timeout").

- **Acceptance Criterion:** A test with a mocked unreachable host triggers a timeout; an `Error` event is emitted with a user-readable message; no unhandled exceptions propagate to the UI.

---

### F.6 Cancellation

**REQ-F-024** (Event-driven)  
When the user clicks the Stop button during an active OpenAI streaming response, the system shall cancel the HTTP request via the existing `HttpRequestHandle::cancel()` mechanism.

- **Acceptance Criterion:** A test starts a mocked streaming request; the `cancel()` method is invoked while deltas are being emitted; the stream halts; no further deltas are processed; the HTTP connection is closed (verified by checking `on_data` callbacks cease).

**REQ-F-025** (Ubiquitous)  
The system shall return a `HttpRequestHandlePtr` from `OpenAIProvider::sendChat()`, enabling the application layer to manage cancellation.

- **Acceptance Criterion:** The `sendChat()` method signature matches the pattern of `OllamaProvider::sendChat()`, returning a handle that supports `cancel()`.

---

### F.7 Settings UI — OpenAI Provider Configuration Panel

**REQ-F-026** (Ubiquitous)  
The system shall display an `OpenAISettingsPanel.qml` component in the provider list (replacing the current "Coming soon: OpenAI" placeholder) with the following input fields and controls:
1. Base URL (TextField, default: `https://api.openai.com/v1`)
2. Default model (ComboBox, populated by dynamic model fetch)
3. Refresh models (Button)
4. Temperature (Slider + SpinBox, range: 0.0–2.0, default: 1.0)
5. API token (TextField with show/hide toggle and Clear button)
6. Secret storage unavailable (notice, displayed when credential store is unavailable)
7. Test Connection (Button)
8. Reset / Cancel / Save (footer buttons)

- **Acceptance Criterion:** The OpenAI provider row in `ProvidersPage.qml` loads `OpenAISettingsPanel.qml` (not `UnsupportedProviderPanel.qml`); all listed fields are present and interactive; no context-window or max-output-tokens controls are present.

**REQ-F-027** (Event-driven)  
When the user modifies the Base URL, default model, or temperature in the OpenAI settings panel and clicks Save, the system shall persist these values to the config repository via `ConfigRepository::saveOpenAiConfig()`.

- **Acceptance Criterion:** After entering a custom base URL, selecting a model, adjusting temperature, and clicking Save, a subsequent application restart loads the same values; config is persisted to the underlying JSON/SQLite store.

**REQ-F-028** (Event-driven)  
When the user enters an API token in the token field and clicks Save, the system shall store the token in the credential store via `CredentialStore::store("openai", token)`.

- **Acceptance Criterion:** After entering a token and saving, a test retrieves the stored credential via `store->retrieve("openai")` and confirms it matches the entered value; the token is stored separately from config (not in JSON).

**REQ-F-029** (Event-driven)  
When the user clicks the eye-icon toggle on the token field, the system shall toggle the field's visibility between plain text and masked (asterisks/dots).

- **Acceptance Criterion:** Clicking the eye icon switches the TextField's `echoMode` from `TextInput.Password` to `TextInput.Normal` and back; the token value is hidden when masked and revealed when unmasked.

**REQ-F-030** (Event-driven)  
When the user clicks the Clear button next to the API token field, the system shall erase the field's content and (upon Save) remove the token from the credential store via `CredentialStore::remove("openai")`.

- **Acceptance Criterion:** After entering a token, clicking Clear blanks the field; clicking Save triggers `remove("openai")`; a subsequent query to the credential store returns no credential for "openai".

**REQ-F-031** (Conditional)  
Where the credential store is unavailable (e.g., D-Bus failure, Secret Service daemon not running), the system shall display a "Secret storage unavailable" notice in the settings panel and disable the token field.

- **Acceptance Criterion:** A test simulates a credential-store initialization failure; the OpenAI settings panel displays a warning message; the API token field is disabled (read-only, grayed out); the user cannot enter or save a token.

**REQ-F-032** (Event-driven)  
When the user clicks the Test Connection button, the system shall make a request to `GET /v1/models` using the current base URL and token settings; if successful, display a confirmation message ("Connection successful"); if unsuccessful, display the error message from OpenAI.

- **Acceptance Criterion:** With a valid token and base URL, clicking Test Connection triggers a fetch and displays "Connection successful" when the response is 2xx; with an invalid token, the error "Authentication failed: Invalid API key" is displayed; network errors display a descriptive message.

---

### F.8 Configuration Persistence

**REQ-F-033** (Ubiquitous)  
The system shall add a new `OpenAIProviderConfig` struct to `holonight_config` (same header/namespace as `OllamaProviderConfig`) with the following fields, using `QString`/Qt types to match the existing `OllamaProviderConfig` convention exactly:
- `base_url: QString` (default: `https://api.openai.com/v1`)
- `default_model: QString` (default: empty)
- `temperature: double` (default: 1.0, range: 0.0–2.0)
- `friend bool operator==(...) = default;` (matching `OllamaProviderConfig`)

- **Acceptance Criterion:** The struct is defined in `src/config/include/holonight_config/provider_config.h` alongside `OllamaProviderConfig`; it compiles without warnings; no field uses `std::string` anywhere the rest of the codebase uses `QString`.

**REQ-F-034** (Ubiquitous)  
The system shall implement `ConfigRepository::loadOpenAiConfig()` and `ConfigRepository::saveOpenAiConfig()` methods with signatures and error-handling conventions that exactly match `loadOllamaConfig`/`saveOllamaConfig`: `loadOpenAiConfig()` returns a plain `OpenAIProviderConfig` (never throws; returns default-constructed values on a missing/malformed file, mirroring `loadOllamaConfig`'s existing fallback table — NOT wrapped in `std::expected`), and `saveOpenAiConfig(const OpenAIProviderConfig&)` returns `std::expected<void, QString>` (`QString`, not `std::string`) with a failure reason on write error.

- **Acceptance Criterion:** Both methods are declared and defined with the exact signatures `OpenAIProviderConfig loadOpenAiConfig() const` and `std::expected<void, QString> saveOpenAiConfig(const OpenAIProviderConfig&) const`; neither throws; a missing config file makes `loadOpenAiConfig()` return default values, not an error.

**REQ-F-035** (Ubiquitous)  
The system shall NOT include API token/secret fields in the `OpenAIProviderConfig` struct; token storage remains exclusively in the credential store (Secret Service / KWallet via `CredentialStore`).

- **Acceptance Criterion:** The `OpenAIProviderConfig` struct has no `token`, `secret`, `api_key`, or similar field; the struct compiles and is used to persist only URL, model, and temperature.

---

### F.9 Credential Integration

**REQ-F-036** (Ubiquitous)  
The system shall reuse the existing `holonight_credentials::CredentialStore` interface without modification, calling `store(providerId, secret)` with `providerId = "openai"` to save the API token.

- **Acceptance Criterion:** The OpenAIProvider instance calls `credential_store->store("openai", token)` when saving settings; the same provider instance calls `credential_store->retrieve("openai")` when preparing a request; both operations use the provider ID "openai".

**REQ-F-037** (Unwanted-behavior)  
If no credential is stored for `"openai"` (empty/not-found result from `CredentialStore::retrieve("openai")`), then the system shall send the request without an `Authorization` header — mirroring `OllamaProvider::authHeaders()`'s existing behavior of omitting the header entirely when its token is empty, with no client-side gating — allowing OpenAI's resulting 401 response to surface through the normal REQ-F-020 error path.

- **Acceptance Criterion:** A test with no stored credential for "openai" sends a request with no `Authorization` header present at all; the mocked 401 response is surfaced as an error via the same path as REQ-F-020; no crash, no silent failure, no client-side pre-request blocking.

---

### F.10 Application-Layer Integration

**Note on current architecture (verified against code, not assumed):** there is no provider registry, factory, or base class today. `OllamaProvider` is a plain concrete class with no virtual interface, and `ChatController` is constructed directly with a concrete `std::shared_ptr<holonight_providers::OllamaProvider>` (`src/application/src/chat_controller.cpp`). `OpenAIProvider` shall likewise be a plain concrete class mirroring `OllamaProvider`'s method shape (`sendChat()` returning `HttpRequestHandlePtr`, `availableModels()`, `refresh()`) — it shall NOT introduce a virtual base class or dynamic-dispatch interface for this cycle, since none exists to extend. The exact mechanism by which the application layer selects between an Ollama-backed and an OpenAI-backed `ChatController` (e.g., which provider instance backs the active conversation, and how the user's provider choice is threaded through) is an open architectural decision left to Stage 2 (Design) — it is out of scope for this Spec to prescribe.

**REQ-F-038** (Ubiquitous)  
The system shall provide an `OpenAIProvider` class in `holonight_providers` with a method surface (`sendChat()`, `availableModels()`, `refresh()`, `setBaseUrl()`/`baseUrl()`, `setAuthToken()`, `setTemperature()`) structurally equivalent to `OllamaProvider`'s, so that Stage 2's design for wiring it into the application layer can follow the same shape without inventing new provider-facing conventions.

- **Acceptance Criterion:** `OpenAIProvider`'s public method signatures are reviewed side-by-side with `OllamaProvider`'s; each has a structurally equivalent counterpart (same parameter shapes, same callback-based async pattern) except where a requirement in this Spec explicitly says otherwise (e.g., no `setContextWindow()`).

---

## Non-Functional Requirements

### N.1 Testing and Code Coverage

**REQ-NF-001** (Ubiquitous)  
All new C++ code in `OpenAIProvider` shall have matching GTest unit tests covering normal flow, error cases, and edge cases (stream fragmentation, malformed JSON, missing fields, empty model list, etc.).

- **Acceptance Criterion:** A test executable `test_openai_provider` is added under `tests/` (following the pattern of `test_ollama_provider`); it achieves >80% line coverage of `src/providers/src/openai_provider.cpp`; test cases cover request construction, SSE parsing, error handling, and model filtering.

**REQ-NF-002** (Ubiquitous)  
All tests shall use a fake/injectable `HttpClient` implementation (mirroring the existing test pattern for `OllamaProvider`) to avoid real network calls during CI.

- **Acceptance Criterion:** Tests pass in isolation without network access; HTTP responses are mocked via constructor injection; no real API calls are made during `ctest` execution.

**REQ-NF-003** (Ubiquitous)  
The code shall adhere to the project's clang-format style (Google-based, 2-space indent, 120-column limit, C++23) and pass clang-tidy linting with no warnings (WarningsAsErrors: '*').

- **Acceptance Criterion:** `task format-check` reports no formatting violations; `task tidy` reports no clang-tidy warnings; the code follows the existing naming conventions (CamelCase classes, camelBack functions, lower_case members).

---

### N.2 Performance and Responsiveness

**REQ-NF-004** (Ubiquitous)  
SSE stream parsing shall be non-blocking: incoming byte chunks shall be buffered and parsed incrementally without blocking the UI event loop.

- **Acceptance Criterion:** A test with rapid SSE block arrivals (simulating high-throughput streaming) does not stall the event loop; the HTTP client's `on_data` callback returns promptly after buffer management and event dispatch.

**REQ-NF-005** (Ubiquitous)  
Model discovery (GET /v1/models) shall complete within a configurable timeout (default: 10 seconds); if the request times out, an error message shall be displayed and the model list shall not be updated.

- **Acceptance Criterion:** A test mocks a delayed response (>10 seconds); the request times out; an error message surfaces to the UI; the ComboBox is not updated.

---

### N.3 Security

**REQ-NF-006** (Ubiquitous)  
API tokens shall never be logged, printed to stdout/stderr, or persisted in plaintext to config files; they shall be stored exclusively via the credential store (Secret Service / KWallet).

- **Acceptance Criterion:** Grep the codebase for calls to `qDebug()`, `std::cout`, `std::cerr`, config file writes that involve the token variable; no matches where the token is logged; credential store is the sole persistence mechanism.

---

## Constraint Requirements (Explicitly Out-of-Scope)

**REQ-C-001** (Ubiquitous)  
The system shall NOT implement tool/function calling support for this cycle, even if the OpenAI API response includes `response.function_call_arguments.*` events.

- **Acceptance Criterion:** A test delivers an SSE event with `type: "response.function_call_arguments.delta"`; the event is silently ignored (no-op); no function_call-related domain events are emitted; no tool registry or execution occurs.

**REQ-C-002** (Ubiquitous)  
The system shall NOT support image, file, or attachment input for this cycle.

- **Acceptance Criterion:** The OpenAI settings UI has no file-upload field, no image-picker, no attachment controls; the `input` array contains only `{role, content}` tuples where `content` is a string, not multimodal.

**REQ-C-003** (Ubiquitous)  
The system shall NOT implement detection or special handling of reasoning models (e.g., `o3-mini`, `o4-mini`, `gpt-5.4-mini`).

- **Acceptance Criterion:** A test selects a reasoning model from the dropdown and sends a message; the request is constructed and sent normally (with `temperature` present, same as any other model); no client-side blocking or warning occurs.
- **Note (verified 2026-07-23 via manual testing against the live API):** OpenAI's current API tolerates `temperature` on reasoning models and completes normally — it does not reject the request. This requirement is about the adapter never special-casing these models client-side, which holds independent of how the API responds; if a future OpenAI API version *does* reject the combination, REQ-F-022 already covers surfacing that error.

**REQ-C-004** (Ubiquitous)  
The system shall NOT include a `reasoning.effort` parameter in any request.

- **Acceptance Criterion:** Request payload inspection confirms the absence of `reasoning_effort` in every request, including ones targeting reasoning models.

**REQ-C-005** (Ubiquitous)  
The system shall NOT use `previous_response_id` or any server-side conversation retention mechanism.

- **Acceptance Criterion:** Request payloads never include `previous_response_id`; every request is stateless and includes the full conversation history; `store: false` is always set.

**REQ-C-006** (Ubiquitous)  
The system shall NOT implement automatic retry logic on 429 (rate limit) or 5xx (server error) responses at the adapter level; the application-layer manual retry UI already implemented shall be the sole retry mechanism.

- **Acceptance Criterion:** A test mocks a 429 response; the adapter emits an Error event; no automatic retry occurs; the application's existing "Retry" button allows manual retry.

**REQ-C-007** (Ubiquitous)  
The system shall NOT support Azure OpenAI endpoints or alternate authentication schemes (e.g., Azure entra ID).

- **Acceptance Criterion:** The base URL field accepts only standard `https://api.openai.com/v1` or compatible proxies; no Azure-specific auth header generation or endpoint routing occurs.

**REQ-C-008** (Ubiquitous)  
The system shall NOT expose or accept `max_output_tokens` in the settings UI or request body for this cycle.

- **Acceptance Criterion:** The OpenAI settings panel has no "Max output tokens" field; no `max_output_tokens` key appears in request payloads; the model's server-side default output ceiling applies.

---

## Verification Strategy

### Unit Tests
- `tests/test_openai_provider.cpp` — request construction, SSE parsing, event mapping, model filtering, error cases, cancellation.

### Integration Tests
- QML panel tests — settings persistence, credential storage/retrieval, UI state transitions.

### Manual Testing Checklist (provided to user after implementation)
1. Add valid OpenAI API key; fetch and select a model; send a message; observe streamed reply token-by-token.
2. Enter invalid API key; observe 401 error surface in UI.
3. Trigger rate limit (request many messages rapidly); observe 429 error; manually retry via application UI.
4. Select a reasoning model (e.g., `o3-mini`, `o4-mini`, `gpt-5.4-mini`); send a message; verify it completes normally with no client-side blocking or special-casing (as of 2026-07-23, OpenAI's API tolerates `temperature` on these models rather than rejecting it with a 400 — see REQ-C-003's note).
5. Click Stop during streaming; verify stream halts.
6. Customize base URL; verify requests use the new endpoint.
7. Verify all settings persist across application restart.

---

## Acceptance Criteria Summary

A user can:
1. Enter an OpenAI API key and base URL in the provider settings panel.
2. Click "Refresh models" and see live-fetched models (denylist-filtered) populate the ComboBox.
3. Select a model and temperature setting, then click Save.
4. Send a chat message and observe a streamed reply appear token-by-token in the message list.
5. Encounter errors (401, 429, 400, network failure) and see a readable error message in the UI.
6. Click Stop during streaming and halt the request.
7. Reload the application and see all OpenAI settings (URL, model, temperature) restored.

All new C++ code passes GTest with >80% line coverage; no real network calls occur in CI; code adheres to clang-format and clang-tidy standards.

---

## Dependencies and Integration Points

- **Existing interfaces used:** `HttpClient::sendStreaming()`, `CredentialStore`, `ConfigRepository`, `HttpRequestHandle::cancel()`, domain `StreamEvent` type.
- **New QML file:** `qml/workspace/OpenAISettingsPanel.qml` (replaces `UnsupportedProviderPanel.qml` for OpenAI).
- **New C++ files:** `src/providers/src/openai_provider.cpp`, `src/providers/include/holonight_providers/openai_provider.h`, `tests/test_openai_provider.cpp`.
- **Modified files:** `src/config/include/holonight_config/config_repository.h` (add struct and methods), `src/config/src/config_repository.cpp` (implement methods), provider registry/factory (register "openai"), `qml/workspace/ProviderListPanel.qml` or `ProvidersPage.qml` (replace placeholder).

---

**End of Specification**
