# SDD Tasks — ollama-chat-backend

## Dependency Order

Tasks are ordered to respect implementation dependencies:
1. Domain additive methods (prerequisites for all other modules)
2. HTTP client abstraction (prerequisite for all provider/application implementations)
3. HTTP client implementations and test double
4. Ollama provider (requires http_client interface)
5. Chat controller (requires ollama_provider)
6. CMakeLists wiring (requires all .cpp implementations)
7. Test files (all use FakeHttpClient to avoid real network)
8. Full build+test verification

---

## Domain Module Additions

- [x] T-001: Message::setText() method
  - REQs: N/A (additive to existing holonight_domain)
  - Check: Edit src/domain/include/holonight_domain/message.h to add `void setText(QString text)` method; add test case in tests/domain/test_message.cpp; run `ctest -R "Message"` and all Message tests pass including new setText verification.

- [x] T-002: Conversation::replaceLastMessage() method
  - REQs: N/A (additive to existing holonight_domain)
  - Check: Edit src/domain/include/holonight_domain/conversation.h to add `bool replaceLastMessage(Message replacement)` method; add test case in tests/domain/test_conversation.cpp verifying replacement in-place, updated_at stamping, and empty-conversation no-op; run `ctest -R "Conversation"` and all Conversation tests pass.

---

## HTTP Client Abstraction (holonight_providers)

- [x] T-003: http_client.h abstract interface and value types
  - REQs: REQ-C-001
  - Check: Create src/providers/include/holonight_providers/http_client.h with enum HttpMethod{Get,Post}, struct HttpRequest, abstract class HttpRequestHandle with cancel() virtual method, abstract class HttpClient with send() and sendStreaming() virtual methods, using std::function callbacks; file compiles standalone in isolation (verify by including in a minimal test).

- [x] T-004: FakeHttpClient test double header
  - REQs: REQ-C-001, REQ-C-002
  - Check: Create tests/providers/fake_http_client.h implementing HttpClient interface with ability to queue canned NDJSON responses and error scenarios without touching real network; file compiles and can be instantiated in tests without Qt6::Network linkage.

- [x] T-005: QtNetworkHttpClient header and implementation
  - REQs: REQ-C-002, REQ-NF-001
  - Check: Create src/providers/include/holonight_providers/qt_network_http_client.h and src/providers/src/qt_network_http_client.cpp with QObject-derived HttpClient backed by QNetworkAccessManager; handles idle timeout via QTimer that resets on each data chunk; implements HTTP status checking for non-2xx errors; file compiles and object can be constructed/destroyed safely.

---

## Ollama Provider Implementation (holonight_providers)

- [x] T-006: OllamaProvider header: class definition and interface
  - REQs: REQ-F-006, REQ-F-008, REQ-C-006
  - Check: Create src/providers/include/holonight_providers/ollama_provider.h with class OllamaProvider holding std::shared_ptr<HttpClient>, constructor accepting HttpClient and optional base_url (default http://localhost:11434), methods availableModels() const, refresh(std::function<void()> on_complete={}), and sendChat(...); file compiles with no .cpp yet.

- [x] T-007: OllamaProvider implementation: model list fetch and cache
  - REQs: REQ-F-006, REQ-F-007, REQ-C-005, REQ-C-006
  - Check: Create src/providers/src/ollama_provider.cpp; refresh() calls http_client->send(GET /api/tags) and parses models into the available_models_ cache; availableModels() returns the cached list; test with FakeHttpClient shows refresh populates the list and repeated availableModels() calls return the same list.

- [x] T-008: OllamaProvider implementation: sendChat with NDJSON parsing
  - REQs: REQ-F-001, REQ-F-002, REQ-C-005
  - Check: Implement OllamaProvider::sendChat() in .cpp: builds Ollama /api/chat POST JSON from message history, calls httpClient->sendStreaming(), buffers incomplete lines, parses each complete NDJSON line for message.content deltas and done flag, emits StreamEvent::ContentDelta and StreamEvent::Completed via callback; test with FakeHttpClient shows correct parsing of multi-line NDJSON and NDJSON split across multiple onData chunks.

- [x] T-009: OllamaProvider implementation: error handling in streaming
  - REQs: REQ-F-011
  - Check: Implement error paths in sendChat: detect and emit StreamEvent::Error for malformed JSON on a line, non-2xx HTTP status passed via onError callback, and error field in NDJSON; test shows all three error sources reach callback as StreamEvent::Error.

- [x] T-010: OllamaProvider implementation: refresh() method
  - REQs: REQ-F-008
  - Check: Implement refresh() method in OllamaProvider: re-fetches /api/tags, entirely replaces (not merges) available_models_, calls on_complete callback; test shows refresh succeeds even if initial fetch failed, and cached list is updated to new list.

---

## Chat Controller Implementation (holonight_application)

- [x] T-011: ChatController header: types and class declaration
  - REQs: REQ-F-009, REQ-F-013
  - Check: Create src/application/include/holonight_application/chat_controller.h with enum class SendRejectReason{NoModelSelected, AlreadyStreaming, NoAssistantMessageToRegenerate}, struct SendRejected{reason}, and class ChatController(std::shared_ptr<OllamaProvider> provider); declare send(), regenerate(), stop(), isStreaming() methods; file compiles.

- [x] T-012: ChatController implementation: happy-path send and streaming flow
  - REQs: REQ-F-001, REQ-F-004, REQ-F-012, REQ-F-013
  - Check: Create src/application/src/chat_controller.cpp; implement send(): validates model (reject NoModelSelected), validates no in-flight for conversation (reject AlreadyStreaming), creates user Message(Complete), creates assistant Message(Pending), transitions to Streaming before network call, calls provider->sendChat() with history, accumulates content deltas via setText/replaceLastMessage, transitions to Complete on Completed event, clears in-flight entry; test with FakeHttpClient shows full send→stream→complete flow emits ContentDelta events and final Completed.

- [x] T-013: ChatController implementation: stop and cancellation
  - REQs: REQ-F-003, REQ-F-004
  - Check: Implement stop(ConversationId) method: synchronously transitions message to Cancelled (retains accumulated partial text), emits StreamEvent::Cancelled once, clears in-flight entry before calling handle->cancel(); test shows stop mid-stream produces Cancelled event with partial content retained and no further ContentDelta after cancellation.

- [x] T-014: ChatController implementation: regenerate
  - REQs: REQ-F-005
  - Check: Implement regenerate(conversation, model, on_event): scans for last Assistant message (reject NoAssistantMessageToRegenerate if none), constructs new Message reusing same MessageId with empty text and Pending status, replaces in conversation, transitions to Streaming, sends history excluding the regenerated message placeholder, streams deltas as normal; test shows same MessageId retained, conversation.messages().size() unchanged, full stream works end-to-end.

- [x] T-015: ChatController implementation: sync validation and rejection paths
  - REQs: REQ-F-009, REQ-F-013
  - Check: Verify send() and regenerate() reject before any HTTP call: NoModelSelected when availableModels empty or model.model_name empty, AlreadyStreaming when in_flight_[conversationId.toString()] exists; test shows rejections are std::unexpected(SendRejected{reason}) and isStreaming() matches in-flight entry existence.

- [x] T-016: ChatController implementation: error handling and state transitions
  - REQs: REQ-F-010, REQ-F-011, REQ-NF-001
  - Check: Implement error path in streaming callbacks: on StreamEvent::Error from provider, transition message to Error, setText(error_message), replaceLastMessage, emit Error event to caller, clear in-flight entry; test covers network connection failure, HTTP error status, malformed JSON, and idle timeout (via FakeHttpClient simulateIdleTimeout hook).

- [x] T-017: ChatController implementation: concurrent per-conversation streams
  - REQs: REQ-F-012
  - Check: Verify in_flight_ is QHash<QString, InFlightStream> keyed by ConversationId::toString(); test shows two distinct Conversation objects can each call send() and receive independent streams without blocking or data corruption, stopping one does not affect the other.

---

## CMakeLists.txt Wiring

- [x] T-018: CMakeLists.txt for src/providers: INTERFACE → STATIC
  - REQs: REQ-C-007
  - Check: Modify src/providers/CMakeLists.txt: change add_library(holonight_providers INTERFACE) to add_library(holonight_providers STATIC src/http_client.cpp src/qt_network_http_client.cpp src/ollama_provider.cpp); add set_target_properties(holonight_providers PROPERTIES AUTOMOC ON) (required for QtNetworkHttpClient QObject); change target_include_directories and target_link_libraries INTERFACE to PUBLIC; run `task configure` and verify build succeeds.

- [x] T-019: CMakeLists.txt for src/application: INTERFACE → STATIC
  - REQs: REQ-C-007
  - Check: Modify src/application/CMakeLists.txt: change add_library(holonight_application INTERFACE) to add_library(holonight_application STATIC src/chat_controller.cpp); add target_link_libraries PUBLIC holonight_providers (ChatController depends on OllamaProvider headers); change target_include_directories and target_link_libraries INTERFACE to PUBLIC (no Qt6::Network added here; reaches it transitively via holonight_providers); run `task configure` and verify build succeeds.

- [x] T-020: CMakeLists.txt for tests: add new test targets
  - REQs: REQ-C-007
  - Check: Modify tests/CMakeLists.txt: add domain/test_conversation_replace_last_message.cpp (or extend domain/test_conversation.cpp with new TEST cases for replaceLastMessage and setText), add providers/test_ollama_provider.cpp, add application/test_chat_controller.cpp to test_holonight_ai executable sources; add target_link_libraries holonight_providers and holonight_application; run `task configure-tests` and verify build succeeds.

---

## Test Files

- [x] T-021: tests/providers/test_ollama_provider.cpp: comprehensive provider testing
  - REQs: REQ-F-002, REQ-F-006, REQ-F-007, REQ-F-008, REQ-C-001, REQ-C-002
  - Check: Create tests/providers/test_ollama_provider.cpp with TEST cases: NDJSON parsing (single-line, multi-line, chunk-boundary splits, empty lines), model fetch at construction (success and failure paths), model cache persistence across calls, refresh() replaces cache, model list empty on initial fetch failure (graceful degradation), malformed JSON treated as fatal error; run `ctest -R OllamaProvider` and all cases pass.

- [x] T-022: tests/application/test_chat_controller.cpp: comprehensive controller testing
  - REQs: REQ-F-001, REQ-F-003, REQ-F-004, REQ-F-005, REQ-F-009, REQ-F-010, REQ-F-011, REQ-F-012, REQ-F-013, REQ-NF-001, REQ-C-002
  - Check: Create tests/application/test_chat_controller.cpp with TEST cases: full send→stream→complete flow with ContentDelta and Completed events, stop-mid-stream retains partial text and emits Cancelled, regenerate preserves MessageId and conversation length, concurrent per-conversation sends do not interfere, reject NoModelSelected (empty model_name), reject AlreadyStreaming (existing in-flight), connection error surfaces Error status and message, timeout via idle_timeout triggers Error, isStreaming() tracks in-flight state correctly; run `ctest -R ChatController` and all cases pass with StopMidStreamRetainsPartialText and RegeneratePreservesMessageId as key cases.

---

## Full Build and Verification

- [x] T-023: Full project build, test, and linting verification
  - REQs: REQ-C-007, REQ-C-008
  - Check: Run `task test` (builds with GTest + CTest) and all holonight_domain, holonight_providers, and holonight_application tests pass without failures; run `task format-check` and no formatting violations are reported in new .cpp/.h files; run `task tidy` and no WarningsAsErrors violations are reported in new code (verify against .clang-tidy config's strict checks); all three commands succeed cleanly.
