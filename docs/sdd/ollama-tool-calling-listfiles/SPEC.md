# SPEC: Ollama Provider Tool Calling with ListFiles

## Overview

This specification describes wiring the existing generic tool-calling framework (`ToolRegistry`, `ToolOrchestrator`, `ToolRequestEvent`) into the Ollama provider adapter, enabling it to call the existing `ListFilesTool` in the same manner as the Anthropic, Google, and OpenAI providers already do. Ollama's `/api/chat` endpoint delivers tool calls atomically within a single NDJSON streamed line (unlike Anthropic's incremental `input_json_delta` blocks) and returns `tool_calls[]` as an array of pre-parsed JSON objects—structurally similar to Google's atomic delivery pattern. This is the fourth provider to receive tool-calling support; the implementation follows an established, provider-agnostic domain pattern with Ollama-specific codec and wire-format handling.

---

## Functional Requirements

### REQ-F-001: Ollama Adapter Accepts Tool Catalog Parameter

**EARS Template:** Ubiquitous  
**Requirement:**  
The `OllamaProvider::sendChat()` method shall gain a 5-arg overload accepting an additional `const holonight_domain::ToolCatalogSnapshot& tool_catalog = {}` parameter, mirroring the existing 5-arg signature in `AnthropicProvider::sendChat()`, `GoogleProvider::sendChat()`, and `OpenAIProvider::sendChat()`.

**Acceptance Criterion:**
- `OllamaProvider::sendChat()` has a 5-arg overload with `const holonight_domain::ToolCatalogSnapshot& tool_catalog = {}` as the trailing parameter.
- Signature compiles without error and parameter is usable in the method body.
- Method implementation branches on `tool_catalog.client_tools` being empty vs. non-empty.

---

### REQ-F-002: Tool Catalog Included in Request Body When Enabled

**EARS Template:** Conditional  
**Requirement:**  
If the tools catalog is provided and non-empty, the Ollama provider shall include a `"tools"` array in the `/api/chat` POST request body, formatted as Ollama's tool schema with an array of objects containing `{"type":"function","function":{"name","description","parameters"}}` entries.

**Acceptance Criterion:**
- Fake-fixture test verifies that a non-empty tools catalog produces a valid `"tools"` key in the request JSON body.
- Test confirms schema matches Ollama's documented tool format.
- No-tools or empty-catalog case produces request body without `"tools"` key.
- Tool definitions are sourced directly from the provided `ToolCatalogSnapshot`.

---

### REQ-F-003: Tool Calls Delivered Atomically in NDJSON Chunk

**EARS Template:** Event-driven  
**Requirement:**  
When Ollama's `/api/chat` response stream contains a `message.tool_calls[]` array, the entire array arrives atomically in a single streamed NDJSON line (marked with `done:false`), followed by a final `done:true` closing line. The streaming parser shall extract the complete `tool_calls` array synchronously from the chunk, validate each entry, and emit a `ToolRequestEvent` per call without accumulating state across chunks.

**Acceptance Criterion:**
- Fake-NDJSON test feeds a single chunk containing `{"message":{"tool_calls":[{"function":{"name":"ListFiles","arguments":{...}}}]},"done":false}` and verifies exactly one `ToolRequestEvent` is emitted synchronously.
- Multiple calls in the array produce multiple `ToolRequestEvent`s in array order.
- Malformed JSON in `arguments` causes the parser to emit a stream error, not a malformed `ToolRequestEvent`.
- Test confirms no state accumulation across chunks is needed (full array is present in one chunk).

---

### REQ-F-004: Multiple Tool Calls in Single Response Emit Multiple Events

**EARS Template:** Conditional  
**Requirement:**  
If a single NDJSON chunk's `message.tool_calls[]` array contains multiple entries, the parser shall emit one `ToolRequestEvent` per entry, in array order, without collapsing or prioritizing a subset.

**Acceptance Criterion:**
- Fake-NDJSON test sends a chunk with `tool_calls: [{"function":{"name":"ListFiles","arguments":{...}}},{"function":{"name":"ListFiles","arguments":{...}}}]` and verifies two `ToolRequestEvent`s are emitted in the same order.
- Each event carries correct name and arguments for its corresponding entry.

---

### REQ-F-005: Tool Call ID Handling (Synthesized When Absent)

**EARS Template:** Conditional  
**Requirement:**  
The NDJSON parser shall read the optional `id` field from each tool-call entry if present and non-empty. If the `id` field is absent or empty, the parser shall synthesize a UUID-based `provider_call_id` internally. In both cases, the `provider_call_id_synthesized` boolean flag shall be set appropriately in the emitted `ToolRequestEvent` to distinguish model-provided from client-synthesized IDs.

**Acceptance Criterion:**
- Fake-NDJSON test with `"id":"call_xyz"` field verifies `provider_call_id` in emitted `ToolRequestEvent` equals `"call_xyz"` and `provider_call_id_synthesized == false`.
- Fake-NDJSON test without `id` field verifies `provider_call_id` contains a syntactically valid UUID and `provider_call_id_synthesized == true`.
- Both cases produce a valid `ToolRequestEvent` without errors.
- **Note/Risk:** Real-world Ollama responses are expected to omit the `id` field essentially always, making the `provider_call_id_synthesized == true` path the common case for Ollama (in contrast to other providers where model-provided IDs are more frequent).

---

### REQ-F-006: Function Arguments Parsed as JSON Object

**EARS Template:** Ubiquitous  
**Requirement:**  
The Ollama provider shall treat the `arguments` field in each tool call as a pre-parsed JSON object (not a JSON string), matching Ollama's wire format. The parser shall validate that `arguments` is a valid JSON object and reject entries with malformed or non-object arguments.

**Acceptance Criterion:**
- Fake-NDJSON test with `"arguments":{"path":"~/Documents"}` (object, not string) verifies the parser extracts it correctly.
- Fake-NDJSON test with `"arguments":"not an object"` (string) causes the parser to emit a stream error.
- Test confirms no JSON-string-unwrapping step is needed (unlike OpenAI's format).

---

### REQ-F-007: Tool Result Reconstruction on Follow-Up Turn

**EARS Template:** State-driven  
**Requirement:**  
While reconstructing the conversation history for a follow-up turn after a tool call completes, the Ollama provider shall append a new message entry to the `messages` array with `{"role":"tool","content":<result text>,"tool_name":<name>}`, using Ollama's tool-result message format.

**Acceptance Criterion:**
- Fake-integration test covering a complete tool-call round-trip (call → tool execution → follow-up request) verifies the follow-up request body contains the original assistant message with tool calls, followed by a new tool-result message.
- Tool result uses the correct tool name and result content.
- Message history maintains correct chronological order: assistant message (with tool call) → tool result message → next assistant response.

---

### REQ-F-008: ProviderAdapterRouter Gates Ollama Tool Catalog by Setting

**EARS Template:** Conditional  
**Requirement:**  
The `ProviderAdapterRouter::sendChat()` method shall check `OllamaProviderConfig::tool_calling_enabled` (or its equivalent boolean flag) before passing the tools catalog to `OllamaProvider::sendChat()`. If `tool_calling_enabled` is false, no tools catalog shall be passed, and the request body shall omit the `"tools"` key.

**Acceptance Criterion:**
- Unit test verifies `ProviderAdapterRouter::sendChat()` for Ollama provider with `tool_calling_enabled == true` passes a non-empty tools catalog.
- Unit test verifies same router with `tool_calling_enabled == false` passes an empty/absent tools catalog.
- Router dispatch logic correctly identifies when to apply Ollama-specific gating via `if constexpr` or variant dispatch.

---

### REQ-F-009: ProviderAdapterRouter Dispatch Selects Ollama Provider Correctly

**EARS Template:** Conditional  
**Requirement:**  
The `ProviderAdapterRouter` shall include a dispatch branch (via `std::get_if`, `if constexpr`, or variant visitor pattern) that correctly routes calls to `OllamaProvider::sendChat()` with the appropriate (4-arg or 5-arg with tools) overload based on the configuration type and `tool_calling_enabled` flag.

**Acceptance Criterion:**
- Code review confirms dispatch logic exists, compiles, and has been extended to include the Ollama provider's tool-calling-enabled branch.
- Unit test verifies an `OllamaProviderConfig` instance with `tool_calling_enabled == true` triggers the 5-arg overload path.
- Unit test verifies an `OllamaProviderConfig` instance with `tool_calling_enabled == false` triggers the 4-arg fallback path.

---

### REQ-F-010: OllamaProviderConfig Includes tool_calling_enabled Field

**EARS Template:** Ubiquitous  
**Requirement:**  
The `OllamaProviderConfig` struct shall include a boolean `tool_calling_enabled` field, defaulting to `false`.

**Acceptance Criterion:**
- Field is declared in `holonight_config/provider_config.h` or equivalent.
- Default initialization (in constructor or field initializer) sets it to `false`.
- Compiles without error and is serializable/deserializable via the existing config persistence layer.

---

### REQ-F-011: New Ollama Instances Default tool_calling_enabled to False

**EARS Template:** Ubiquitous  
**Requirement:**  
When a new `OllamaProviderConfig` instance is constructed (by user adding a new Ollama provider via settings), `tool_calling_enabled` shall default to `false`.

**Acceptance Criterion:**
- Default constructor or factory function for new provider instances sets the flag to `false`.
- Unit test confirms new instance has `tool_calling_enabled == false`.

---

### REQ-F-012: Legacy Ollama Instances Loaded from Storage Default tool_calling_enabled to False

**EARS Template:** Conditional  
**Requirement:**  
When loading an `OllamaProviderConfig` from persistent storage (e.g., JSON file created before this feature was implemented), if the `tool_calling_enabled` key is absent, the deserialized instance shall default the field to `false` for backward compatibility.

**Acceptance Criterion:**
- Unit test loads an Ollama provider config JSON fragment without a `tool_calling_enabled` key and verifies the deserialized object has `tool_calling_enabled == false`.
- No hard error or exception is raised for missing key.

---

### REQ-F-013: OllamaProviderConfig Round-Trip Serialization

**EARS Template:** Ubiquitous  
**Requirement:**  
The `OllamaProviderConfig::tool_calling_enabled` field shall serialize to JSON and deserialize back to the same value, maintaining consistency across app restarts.

**Acceptance Criterion:**
- Unit test serializes an `OllamaProviderConfig` with `tool_calling_enabled == true`, deserializes it, and verifies the value is `true`.
- Test repeats with `false`.
- JSON shape matches the schema used by `config_repository`.

---

### REQ-F-014: ProviderSettingsController Exposes toolCallingEnabled Property

**EARS Template:** Ubiquitous  
**Requirement:**  
The existing `ProviderSettingsController` class (Ollama's per-instance settings controller, predating per-provider naming conventions) shall add a `Q_PROPERTY(bool toolCallingEnabled READ toolCallingEnabled WRITE setToolCallingEnabled NOTIFY toolCallingEnabledChanged)` that exposes the underlying `OllamaProviderConfig::tool_calling_enabled` flag.

**Acceptance Criterion:**
- Property is declared in the class header.
- `toolCallingEnabled()` getter returns the current value.
- `setToolCallingEnabled(bool)` setter updates the value, persists it to config, and emits the change signal.
- Property binds successfully from QML without errors.

---

### REQ-F-015: QML Toggle UI for Tool Calling

**EARS Template:** Ubiquitous  
**Requirement:**  
The Ollama provider settings panel QML (in `qml/workspace/OllamaSettingsPanel.qml` or equivalent) shall include a toggle control (checkbox or switch) bound to `providerSettingsController.toolCallingEnabled`, allowing the user to enable/disable tool calling for the Ollama provider.

**Acceptance Criterion:**
- QML file declares a control (e.g., `CheckBox` or `Switch`) with `checked: providerSettingsController.toolCallingEnabled`.
- Control includes a label such as "Enable Tool Calling" or "Allow Function Calls".
- Manual QML inspection confirms binding is syntactically valid and mirrors the pattern already established in `AnthropicSettingsPanel.qml`, `GoogleSettingsPanel.qml`, and `OpenAISettingsPanel.qml`.

---

### REQ-F-016: Toggle Updates Underlying Provider Configuration

**EARS Template:** Event-driven  
**Requirement:**  
When the user toggles the tool-calling control in the settings UI, the change shall be written to the underlying `OllamaProviderConfig` and persisted to storage (via `ConfigRepository`), so that the setting is restored on app restart.

**Acceptance Criterion:**
- Integration test (or manual walk-through checklist) toggles the QML control, restarts the app, and verifies the setting is restored.
- Config is visibly saved to disk (e.g., by inspecting the JSON file or internal serialization).

---

### REQ-F-017: OllamaToolCodec Encodes Tool Definitions

**EARS Template:** Ubiquitous  
**Requirement:**  
The `OllamaToolCodec` class shall provide an `encodeDefinitions(ToolCatalogSnapshot)` method that transforms the tool catalog into a JSON array formatted for Ollama's `/api/chat` `"tools"` field, matching the structure `[{"type":"function","function":{"name","description","parameters"}}]`.

**Acceptance Criterion:**
- Method signature exists and is callable.
- Test provides a `ToolCatalogSnapshot` containing `ListFilesTool` and verifies the returned JSON array is valid and matches Ollama's schema.
- Empty catalog returns an empty JSON array.

---

### REQ-F-018: OllamaToolCodec Encodes Message History

**EARS Template:** Ubiquitous  
**Requirement:**  
The `OllamaToolCodec` class shall provide an `encodeHistory(vector<Message>)` method that transforms the message history (including prior tool calls and results) into the format expected by Ollama's `/api/chat` `messages` array, reconstructing assistant messages with tool calls and tool-result messages in the correct order.

**Acceptance Criterion:**
- Method signature exists and is callable.
- Test provides a message history containing an assistant message with tool calls, followed by tool-result messages, and verifies the returned JSON array correctly reconstructs them for Ollama.
- Messages maintain chronological order and correct role/content/tool_name fields.

---

### REQ-F-019: OllamaToolCodec Decodes Tool Requests

**EARS Template:** Ubiquitous  
**Requirement:**  
The `OllamaToolCodec` class shall provide a `decodeRequests(...)` method that parses tool calls from an Ollama NDJSON response chunk, extracting the `message.tool_calls[]` array and emitting a sequence of `ToolRequestEvent` objects with the correct name, arguments, optional ID, and `provider_call_id_synthesized` flag.

**Acceptance Criterion:**
- Method signature exists and is callable with an Ollama NDJSON chunk (or parsed message object).
- Test provides a chunk with single or multiple tool calls and verifies correct `ToolRequestEvent` emission.
- Test with missing `id` verifies `provider_call_id_synthesized == true`.
- Test with present `id` verifies `provider_call_id_synthesized == false`.
- Malformed `arguments` JSON causes the method to return an error, not malformed events.

---

## Non-Functional Requirements

### REQ-NF-001: Deterministic Behavior Except UUID Generation

**EARS Template:** Ubiquitous  
**Requirement:**  
NDJSON parsing and tool-request emission shall be deterministic and repeatable, with the sole exception of synthesized UUIDs (for client-side call IDs when Ollama does not supply an `id`). All parsing logic shall produce identical results for identical input.

**Acceptance Criterion:**
- Fake-NDJSON tests run multiple times with the same input produce identical `ToolRequestEvent` sequences (except UUID values, which may differ but remain valid).
- No random sleeps, non-deterministic JSON key ordering, or state pollution from prior test runs.

---

### REQ-NF-002: No Live API Calls in Tests

**EARS Template:** Unwanted behaviour  
**Requirement:**  
Test suites covering Ollama provider tool calling shall NOT make live HTTP requests to Ollama's `/api/chat` endpoint. All tests shall use fake/mocked NDJSON responses and request-body verification.

**Acceptance Criterion:**
- Code review of `tests/providers/test_ollama_tool_codec.cpp` (or equivalent) confirms all networking is mocked.
- No hardcoded Ollama endpoints or live model names in test source.
- Tests run without internet access and complete quickly (unit-test speed).

---

### REQ-NF-003: JSON Validity in Request Bodies

**EARS Template:** Ubiquitous  
**Requirement:**  
All generated request bodies (e.g., `"tools"` array, message history reconstruction) shall be syntactically valid JSON, consumable by `QJsonDocument` and `QJsonObject` without parse errors.

**Acceptance Criterion:**
- Unit test parses generated request JSON with `QJsonDocument::fromJson()` and verifies `!doc.isNull()`.
- Fake-fixture test confirms no malformed JSON is ever constructed for the `"tools"` array or message history reconstruction, across all covered scenarios.

---

### REQ-NF-004: Parallel Tool Calls Preserve Array Order

**EARS Template:** Ubiquitous  
**Requirement:**  
When a single NDJSON chunk's `tool_calls[]` array contains multiple calls, the order of emitted `ToolRequestEvent`s shall match the order in the array.

**Acceptance Criterion:**
- Fake-NDJSON test with ordered calls `[call_A, call_B, call_C]` verifies events are emitted in that same order.
- Test confirms event metadata (name, arguments) matches the corresponding array index.

---

## Constraints & Non-Goals

### REQ-C-001: ListFilesTool Only This Cycle

**Constraint:**  
This cycle wires the Ollama provider to the existing `ListFilesTool` only. No new tool implementations, tool definitions, or tool-specific logic are introduced in this cycle.

**Scope Boundary:**  
Additional tools may be wired in future cycles; this cycle establishes the plumbing infrastructure for Ollama, mirroring the existing implementations for Anthropic, Google, and OpenAI.

---

### REQ-C-002: Ollama Provider Adapter Only

**Constraint:**  
This cycle modifies only the Ollama provider adapter (`src/providers/src/ollama_provider.cpp`, `src/providers/include/holonight_providers/ollama_provider.h`, `src/providers/include/holonight_providers/ollama_tool_codec.h`, and related config). Anthropic, Google, and OpenAI provider adapters remain completely untouched.

**Scope Boundary:**  
Future cycles may enhance or extend these other adapters; those changes are out of scope.

---

### REQ-C-003: ListFilesTool Behavior Unchanged

**Constraint:**  
The `ListFilesTool` implementation (directory enumeration, `$HOME` boundary enforcement, error taxonomy, read-only semantics) shall remain completely unchanged. This cycle is pure wiring/plumbing on the Ollama provider adapter and router sides.

**Scope Boundary:**  
Tool implementation enhancements are future work, not this cycle.

---

### REQ-C-004: No Reasoning/Thinking Token Features

**Constraint:**  
Ollama's optional `think:true` request flag and `thinking` response field are explicitly NOT touched by this cycle. Tool-calling requests shall never set `think:true`, and tool-calling responses shall never parse or render a `thinking` field.

**Scope Boundary:**  
Reasoning-token support (if implemented at all) is deferred to a separate, dedicated cycle and requires UI surface definition and token-accounting work.

---

### REQ-C-005: No Per-Model Capability Queries

**Constraint:**  
Tool-calling capability is not filtered or queried per model (e.g., via Ollama's `/api/show` endpoint's `capabilities` array). Tool calling is enabled or disabled uniformly via the `tool_calling_enabled` boolean flag per provider instance, regardless of which model is selected.

**Scope Boundary:**  
If a user enables tool calling on a model that does not support it, Ollama's server returns an HTTP error for the request; this error flows through the existing error-propagation path unchanged (no new error-handling code required). Per-model capability filtering is deferred to a future cycle if justified by user requests or error frequency.

---

### REQ-C-006: kMaxToolCallsPerTurn Cap Applies Universally

**Constraint:**  
The existing `kMaxToolCallsPerTurn` safety cap (default 10, defined in the generic tool-registry/orchestrator layer) shall apply to Ollama provider tool calls without modification. No Ollama-specific cap is introduced.

**Scope Boundary:**  
Provider-specific caps are not justified by current threat model; the unified cap is sufficient.

---

### REQ-C-007: No Approval Gate for Tool Execution

**Constraint:**  
No user confirmation dialog, blocking approval flow, or explicit permission grant is required before tool execution. This cycle does not introduce any approval mechanism.

**Scope Boundary:**  
Visibility in the chat transcript is the sole compensating control (same design rationale as other providers: `ListFilesTool` is read-only and home-directory-restricted by design).

---

### REQ-C-008: Visibility in Transcript is Sole Compensating Control

**Constraint:**  
Users rely on seeing tool calls and results rendered in the chat transcript to understand tool execution. The application provides no other disclosure or audit mechanism for tool invocations in this cycle.

**Scope Boundary:**  
Audit logging or tool-execution history UI may be added in future work; not this cycle.

---

## Testing Strategy

### Test Coverage Outline (Informational)

The following fake-fixture tests (modeled on existing provider-specific test files) shall be implemented to verify conformance:

1. **OllamaToolCodecEncodeDefinitionsFormatsToolsArray** — Verify non-empty tool catalog produces valid `"tools"` JSON matching Ollama schema.
2. **OllamaToolCodecEncodeDefinitionsReturnsEmptyArrayWhenNoCatalog** — Verify empty/absent catalog produces empty array.
3. **OllamaToolCodecDecodeRequestsEmitsSingleEventForSingleToolCall** — Verify single `tool_calls[]` entry → one `ToolRequestEvent`.
4. **OllamaToolCodecDecodeRequestsEmitsMultipleEventsForMultipleToolCalls** — Verify multiple entries → multiple events in order.
5. **OllamaToolCodecDecodeRequestsSynthesizesIdWhenAbsent** — Verify missing `id` → synthesized UUID with `provider_call_id_synthesized == true`.
6. **OllamaToolCodecDecodeRequestsUsesProvidedId** — Verify present `id` → used directly with `provider_call_id_synthesized == false`.
7. **OllamaToolCodecDecodeRequestsFailsOnMalformedArguments** — Verify invalid JSON in `arguments` causes stream error.
8. **OllamaToolCodecEncodeHistoryReconstructsToolCallsAndResults** — Verify follow-up history includes assistant message with tool calls and tool-result messages.
9. **OllamaToolCodecEncodeHistoryMaintainsCorrectMessageOrder** — Verify messages maintain chronological order in reconstructed history.
10. **OllamaProviderIncludesToolsArrayWhenSettingEnabled** — Verify `/api/chat` request body includes `"tools"` when `tool_calling_enabled == true`.
11. **OllamaProviderOmitsToolsArrayWhenSettingDisabled** — Verify request body omits `"tools"` when `tool_calling_enabled == false`.
12. **ProviderAdapterRouterGatesToolCatalogByOllamaToolCallingEnabled** — Verify router respects the toggle.
13. **ProviderSettingsControllerPersistesToolCallingEnabledToggle** — Verify setting survives app restart.
14. **OllamaSettingsPanelToggleBindsToToolCallingEnabledProperty** — Verify QML binding compiles and toggles correctly.

---

## Related Documents

- **Precedent (Anthropic Tool Calling):** `docs/sdd/tool-calling-listfiles/SPEC.md` — establishes domain patterns (`ToolRequestEvent`, `ToolOrchestrator`, provider-agnostic `ChatController` dispatch).
- **Other Provider Precedents:** `docs/sdd/google-tool-calling-listfiles/SPEC.md` and `docs/sdd/openai-tool-calling-listfiles/SPEC.md` — parallel codec and routing patterns.
- **Architecture:** `docs/high-level-project-idea.md`, `docs/provider-configuration.md`, `docs/sdd/tool-activity-rendering/`.
- **Ollama API Reference:** Ollama's `/api/chat` endpoint schema for `tools` array and `tool_calls` response format (external reference).

---

## Glossary

| Term | Definition |
|------|-----------|
| `tool_calls` | Ollama's response field containing an array of invoked tools with `name`, `arguments` (pre-parsed JSON object), and optional `id`. |
| `tool_name` | Field name in Ollama's tool-result message, identifying which tool produced the result. |
| `provider_call_id` | Domain-layer correlation ID (`ToolRequestEvent` field) — sourced from model-provided `id` or synthesized UUID. |
| `provider_call_id_synthesized` | Boolean flag indicating whether `provider_call_id` was synthesized by the adapter (true) or provided by the model (false). |
| `ToolRequestEvent` | Domain event emitted when a provider detects a tool call; consumed by `ToolOrchestrator` and `ChatController`. |
| `ProviderAdapterRouter` | Application-layer component that gates tool catalogs and routes calls to provider adapters based on configuration. |
| `OllamaToolCodec` | Ollama-specific codec class implementing `encodeDefinitions()`, `encodeHistory()`, and `decodeRequests()`. |

---

## Acceptance Criteria Summary

All requirements are independently verifiable:
- **Functional requirements** (REQ-F-001 to REQ-F-019) are testable via fake-NDJSON fixtures, unit tests, and integration tests.
- **Non-functional requirements** (REQ-NF-001 to REQ-NF-004) are verifiable by code review and test-run inspection.
- **Constraints** (REQ-C-001 to REQ-C-008) are verifiable by code review and scope boundary inspection.

---

## Version History

| Date | Author | Status | Notes |
|------|--------|--------|-------|
| 2026-08-05 | Claude Code | Draft | Initial EARS specification for Ollama tool-calling feature. |
