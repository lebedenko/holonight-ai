# SPEC: Ollama Chat Backend

## Overview

This specification defines the backend-only slice of the usable chat window for holonight-ai, a C++23/Qt6 desktop chat application. This cycle delivers two module additions:

1. **holonight_providers module**: An Ollama provider adapter that communicates with a local Ollama server via its HTTP API (`/api/chat` streaming endpoint and `/api/tags` model list endpoint).
2. **holonight_application module**: Orchestration logic for sending a message to the selected model, streaming the response back to the conversation, stopping mid-stream, and retrying/regenerating the last assistant response.

Both modules build on existing domain types from `holonight_domain` (Message, Conversation, MessageId, MessageRole, MessageStatus, StreamEvent, ModelId, TransitionError) and are fully unit-testable with GTest without requiring a real Ollama server or network connectivity.

---

## Non-Goals

The following are **explicitly out of scope** for this cycle:

- **No QML/UI work.** This specification covers backend logic only. Conversation rendering, message input, model selection UI, and error display are deferred to a future UI cycle.
- **No persistence.** Conversations exist only in memory. Application restart discards all state. The `holonight_persistence` module is not touched; no SQLite, file I/O, or save/load hooks are implemented.
- **No authentication or credentials.** The `holonight_credentials` module is not touched. The adapter accepts a configurable base URL only; no API keys, tokens, authentication headers, or credential storage.
- **No multi-provider abstraction.** A small extensible provider interface is designed to allow future adapters (OpenAI, Anthropic, Google), but only the Ollama adapter is implemented in this cycle. No generalized multi-provider orchestration framework.
- **No automatic model list polling.** Model list is fetched once at initialization and cached. An explicit `refresh()` method allows manual re-fetch. No background polling, no periodic updates.

---

## Functional Requirements

### REQ-F-001: Send message to Ollama and stream response

**Statement:** When a user sends a message and a model is selected, the application shall send the message to the selected Ollama model via the `/api/chat` endpoint and stream the model's response back as a series of `StreamEvent` objects.

**Acceptance criteria:**
- A `Message` with `MessageStatus::Pending` is created from the user input and appended to the conversation.
- The message is transitioned to `MessageStatus::Streaming` before the first network call.
- An HTTP POST request is made to `{base_url}/api/chat` with a JSON body containing the conversation history and selected model.
- Each NDJSON chunk received from Ollama is parsed and emitted as a `StreamEvent::ContentDelta` containing the partial text.
- When Ollama returns a final message chunk (with `done: true`), a `StreamEvent::Completed` is emitted.
- The message is transitioned to `MessageStatus::Complete` after completion.
- The caller receives all `StreamEvent` objects via a signal/callback mechanism (exact event delivery pattern is a Design decision; spec only requires events are emitted in order).

### REQ-F-002: Parse NDJSON streaming response from Ollama

**Statement:** The adapter shall correctly parse newline-delimited JSON (NDJSON) chunks from Ollama's `/api/chat` streaming endpoint.

**Acceptance criteria:**
- Each line of the response body is parsed as a separate JSON object.
- The adapter extracts the `message.content` field from each chunk to form the delta text.
- Empty or whitespace-only lines are skipped without error.
- Malformed JSON in a chunk is treated as a fatal stream error (see REQ-F-011).

### REQ-F-003: Stop in-flight request mid-stream

**Statement:** When a stop request is issued for an in-flight message, the application shall abort the network request and halt streaming.

**Acceptance criteria:**
- A public stop method (e.g., `stopCurrent()`, `cancel()`, or similar — Design will finalize the name) exists on the orchestrator.
- Calling stop with an in-flight request terminates the HTTP connection gracefully.
- No further `StreamEvent::ContentDelta` chunks are emitted after stop is called.
- A `StreamEvent::Cancelled` is emitted to signal the stream termination (see REQ-F-004).

### REQ-F-004: Cancelled message status and event emission

**Statement:** When a message stream is stopped mid-transfer, the message shall transition to `MessageStatus::Cancelled` and a `StreamEvent::Cancelled` shall be emitted.

**Acceptance criteria:**
- After stop is called, `message.transitionTo(MessageStatus::Cancelled)` is invoked and succeeds (does not return a `TransitionError`).
- A `StreamEvent::Cancelled{}` is emitted exactly once after the stop.
- The partial text accumulated before cancellation is retained in the message (not discarded).
- The message's text field reflects all content received up to the cancellation point.

### REQ-F-005: Retry/regenerate last assistant message

**Statement:** When a retry/regenerate request is issued for the last assistant message in the conversation, the application shall replace that message in-place with a fresh response from Ollama, keeping the conversation linear.

**Acceptance criteria:**
- A public regenerate method exists on the orchestrator that targets the last assistant message.
- Regenerate is available regardless of the previous message's status (Complete, Errored, or Cancelled).
- The previous assistant message's text and status are replaced with the new response.
- No new message entry is appended; the conversation remains the same length.
- The entire message object is updated in-place (the same `MessageId` is retained but status and text change).
- Streaming behavior for the regenerate response is identical to a normal send (see REQ-F-001).

### REQ-F-006: Fetch model list from Ollama at application initialization

**Statement:** When the application initializes its chat view model, it shall call the Ollama adapter's `refresh()` method once to fetch the list of available models from Ollama's `/api/tags` endpoint.

**Acceptance criteria:**
- One HTTP GET request is made to `{base_url}/api/tags` by `refresh()` during chat view-model initialization.
- The response JSON is parsed to extract the `models` array.
- Each model entry is mapped to a `ModelId` with `provider_id = "ollama"` and `model_name` extracted from the model's `name` field.
- If the fetch fails or the server is unreachable, the adapter retains an empty model list (error handling is a Design decision; spec only requires graceful degradation).
- The fetched list is cached in memory.

### REQ-F-007: Cache model list in memory after fetch

**Statement:** The adapter shall retain the fetched model list in memory throughout the adapter's lifetime without automatic refresh.

**Acceptance criteria:**
- Multiple calls to `getAvailableModels()` or similar (Design will finalize the accessor name) return the same cached list.
- The list does not change between calls unless an explicit `refresh()` call (see REQ-F-008) is invoked.
- Memory usage for the cache is proportional to the number of models (no redundant copies).

### REQ-F-008: Manual refresh of model list via refresh() method

**Statement:** The adapter shall provide a public `refresh()` method that re-fetches the model list from `/api/tags` and updates the cache.

**Acceptance criteria:**
- A public method named `refresh()` (or similar — Design finalizes) exists on the adapter.
- Calling `refresh()` triggers a new HTTP GET to `{base_url}/api/tags`.
- The cached model list is replaced with the newly fetched list.
- The refresh succeeds even if the previous fetch (at construction) had failed.
- Multiple sequential `refresh()` calls are supported.

### REQ-F-009: Reject send if no model selected

**Statement:** If no model is selected or the model list is empty, the application shall reject a send request synchronously before any network call is attempted.

**Acceptance criteria:**
- A domain-level validation error is returned (exact error type is a Design decision; spec only requires it is synchronous and happens before HTTP).
- No HTTP request reaches the network stack.
- The rejection is immediate and does not involve async network I/O.
- An appropriate error message is produced for the UI to display (e.g., "No model selected" or "No models available").

### REQ-F-010: Surface server unreachable/connection errors as inline Message

**Statement:** If the Ollama server is unreachable or a network error occurs during send, an `Error`-status `Message` shall be appended to the conversation with a descriptive error.

**Acceptance criteria:**
- When the HTTP request fails (connection refused, timeout, DNS failure, etc.), the in-flight message is transitioned to `MessageStatus::Error`.
- A `StreamEvent::Error` with a human-readable error message is emitted (e.g., "Failed to connect to Ollama server at http://localhost:11434").
- The error message is appended to the conversation as an inline assistant message so future UI can render it inline (the exact message role/content is a Design decision).
- Subsequent send requests can proceed (the connection error does not permanently disable the adapter).

### REQ-F-011: Surface Ollama-side errors as inline Message

**Statement:** If Ollama returns an HTTP error or an error in the response stream, an `Error`-status `Message` shall be appended to the conversation with the error details.

**Acceptance criteria:**
- If Ollama returns HTTP 5xx or other error status, the message is transitioned to `MessageStatus::Error` and a `StreamEvent::Error` is emitted with the status/reason.
- If a malformed JSON chunk is encountered during streaming, the stream is aborted, the message transitions to `MessageStatus::Error`, and `StreamEvent::Error` is emitted.
- If Ollama returns an error field in the JSON response, that error is extracted and emitted as `StreamEvent::Error`.
- The error is surfaced inline as a message in the conversation (same pattern as REQ-F-010).

### REQ-F-012: Multiple conversations support concurrent in-flight streams

**Statement:** The application shall support multiple different conversations each having their own independent in-flight stream simultaneously.

**Acceptance criteria:**
- Two distinct `Conversation` objects can each initiate a send request without blocking one another.
- Each conversation's stream is processed independently; data from Conversation A does not affect Conversation B.
- Stopping a stream in Conversation A does not affect an in-flight stream in Conversation B.
- No global request-level lock or serialization prevents concurrent sends across different conversations.

### REQ-F-013: Single in-flight stream per conversation

**Statement:** While a stream is in-flight for a given conversation, the application shall not allow a second send request for the same conversation.

**Acceptance criteria:**
- If a send request is issued while a previous send is still streaming for the same conversation, the second send is rejected with an error (exact error/mechanism is a Design decision; spec only requires rejection before network call).
- The rejection happens synchronously before any new HTTP request is made.
- Once the in-flight stream completes, cancels, or errors, a new send becomes available for that conversation.

---

## Non-Functional Requirements

### REQ-NF-001: Idle timeout for streaming requests

**Statement:** While a stream is in-flight, if no data is received from Ollama for more than a configurable timeout window (default 30 seconds), the application shall abort the request and surface an error.

**Acceptance criteria:**
- A configurable idle timeout parameter is supported (default 30 seconds; Design finalizes the config mechanism).
- If no bytes are received for the timeout duration, the HTTP connection is terminated.
- The message is transitioned to `MessageStatus::Error`.
- A `StreamEvent::Error` is emitted with a message indicating timeout (e.g., "Ollama server did not respond within 30 seconds").
- The error is surfaced inline as a message in the conversation.
- The timeout counter resets whenever data is received (a slow stream with intermittent chunks does not trigger the timeout).

---

## Constraint Requirements

### REQ-C-001: Abstract HTTP client interface for dependency injection

**Statement:** The adapter shall be built around an injectable/abstract HTTP client interface so that unit tests can substitute a fake implementation without requiring a real network socket or Ollama server.

**Acceptance criteria:**
- An abstract base class or interface (e.g., `HttpClient` or `NetworkClient`) is defined with virtual methods for GET, POST, streaming requests, and timeout handling.
- The Ollama adapter depends on this interface, not a concrete HTTP implementation.
- A test-only fake implementation exists that accepts pre-recorded NDJSON responses and error scenarios.
- Unit tests use the fake to verify parsing, event emission, and state transitions without network I/O.
- The adapter can be instantiated in unit tests with the fake client, allowing tests to run in any environment (CI, sandboxed, offline).

### REQ-C-002: Testability without real Ollama server

**Statement:** All functionality specified in this document shall be verifiable through unit tests that do not require a running Ollama server or network connectivity.

**Acceptance criteria:**
- `task test` executes all tests for `holonight_application` and `holonight_providers` modules without requiring Ollama to be installed or running.
- Mock/fake HTTP responses are used to simulate Ollama's responses.
- At least one test exercises the full send-stream-complete flow with a fake HTTP client.
- At least one test exercises stop-mid-stream with partial content.
- At least one test exercises retry/regenerate.
- At least one test exercises connection failure / unreachable server.
- At least one test exercises the model list fetch and cache.
- Tests pass in a CI environment without external service dependencies.

### REQ-C-003: No authentication or credentials handling

**Statement:** The adapter shall not implement any authentication mechanism (API keys, tokens, headers, or credential storage).

**Acceptance criteria:**
- No `Authorization` header is added to requests.
- No credential lookup or fetching from `holonight_credentials` module or other secret storage.
- The adapter accepts only a `base_url` configuration parameter.
- All Ollama requests use standard HTTP without authentication.

### REQ-C-004: No persistence or save/load hooks

**Statement:** The adapter and orchestrator shall not implement or invoke any persistence mechanism.

**Acceptance criteria:**
- No SQLite queries or file I/O.
- No hooks to `holonight_persistence` module.
- No save/load methods or conversation serialization.
- Model list and conversations exist only in memory.
- Application restart discards all state (conversations and model cache).

### REQ-C-005: Use Ollama /api/chat endpoint for streaming

**Statement:** The adapter shall use only Ollama's `/api/chat` endpoint (NDJSON streaming) for chat; no other endpoints for chat logic.

**Acceptance criteria:**
- POST requests for send/regenerate target `{base_url}/api/chat`.
- The request body includes the conversation history in Ollama's expected format.
- NDJSON responses are processed line-by-line.
- No use of `/api/generate` or other chat-like endpoints.

### REQ-C-006: Configurable base URL with default

**Statement:** The adapter shall accept a configurable base URL for the Ollama server, defaulting to `http://localhost:11434`.

**Acceptance criteria:**
- A constructor or factory method allows passing a custom base URL.
- If no URL is provided, the default `http://localhost:11434` is used.
- All requests are formed as `{base_url}/api/chat`, `{base_url}/api/tags`, etc.
- The URL is configurable at adapter construction time (not hardcoded).

### REQ-C-007: Fully unit-tested with GTest

**Statement:** All code in `holonight_providers` (Ollama adapter) and `holonight_application` (orchestration) shall be covered by GTest unit tests.

**Acceptance criteria:**
- Test executables are built alongside the modules.
- At least one test file exists for the Ollama adapter (e.g., `tests/providers/test_ollama_adapter.cpp`).
- At least one test file exists for the orchestrator (e.g., `tests/application/test_orchestrator.cpp`).
- Core functionality (send, stream, stop, retry, model list) is tested.
- Error paths (connection failure, timeout, malformed response) are tested.
- `task test` runs all tests via CTest.
- Tests use GTest framework (assertions, fixtures, mocking as appropriate).

### REQ-C-008: Backend-only, no QML/UI implementation

**Statement:** This specification defines backend logic only. No QML components, UI rendering, or visual presentation shall be implemented or modified in this cycle.

**Acceptance criteria:**
- No `.qml` files are added or modified.
- No changes to `apps/chat/CMakeLists.txt` QML module registration.
- The `HolonightChat` QML module imports remain unchanged.
- All deliverables are C++ libraries with domain/application/provider APIs.
- A future UI cycle will consume these APIs to render the chat window.

---

## Summary

This specification defines a backend-only chat system integrating with Ollama. The system sends messages, streams responses via `StreamEvent` objects, supports stopping and regenerating messages, and manages a cached model list. Errors (connection, timeout, Ollama-side) are surfaced as inline conversation messages. The design prioritizes testability through abstract HTTP client injection and concurrency safety (per-conversation streams). QML/UI, persistence, authentication, and multi-provider support are explicitly deferred to future cycles.
