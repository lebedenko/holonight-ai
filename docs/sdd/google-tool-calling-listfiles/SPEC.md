# SPEC: Google (Gemini) Provider Tool Calling with ListFiles

## Overview

This specification describes wiring the existing generic tool-calling framework (`ToolRegistry`, `ToolOrchestrator`, `ToolRequestEvent`) into the Google (Gemini) provider adapter, enabling it to call the existing `ListFilesTool` in the same manner as the Anthropic provider adapter already does. Gemini's `streamGenerateContent` API delivers function calls atomically and may emit multiple function calls in parallel within a single streamed chunk — structurally different from Anthropic's incremental `tool_use` blocks, but the same provider-agnostic domain events emerge at the application layer.

---

## Functional Requirements

### REQ-F-001: Google Adapter Accepts Tool Catalog Parameter

**EARS Template:** Ubiquitous
**Requirement:**
The `GoogleProvider::sendChat()` method shall accept an optional tools/catalog parameter of type `std::optional<std::vector<domain::ToolDefinition>>`, mirroring the existing signature in `AnthropicProvider::sendChat()`.

**Acceptance Criterion:**
- `GoogleProvider::sendChat()` method signature includes `std::optional<std::vector<domain::ToolDefinition>>` parameter.
- Signature compiles without error and parameter is usable in the method body.

---

### REQ-F-002: Tool Catalog Included in Request Body

**EARS Template:** Conditional
**Requirement:**
If the tools catalog is provided and non-empty, the Google provider shall include a `"tools"` array in the `streamGenerateContent` request body, formatted as Gemini's `Tool` schema (with `"functionDeclarations"` array containing function definitions with `name`, `description`, and `parameters` fields).

**Acceptance Criterion:**
- Fake-fixture test verifies that a non-empty tools catalog produces a valid `"tools"` key in the request JSON body.
- Test confirms schema matches Gemini's documented `Tool` format.
- No-tools or empty-catalog case produces request body without `"tools"` key.

---

### REQ-F-003: SSE Parser Atomically Extracts Function Call Parts

**EARS Template:** Event-driven
**Requirement:**
When the SSE parser encounters a streamed chunk with a `functionCall` part in `candidates[0].content.parts[]`, it shall synchronously extract the complete `name` and `args` fields (which arrive atomically in a single Gemini response chunk, unlike Anthropic's incremental `input_json_delta` fragments), validate the resulting JSON, and immediately emit a `ToolRequestEvent`.

**Acceptance Criterion:**
- Fake-SSE test feeds a single chunk containing `{"functionCall": {"name": "ListFiles", "args": {...}}}` and verifies exactly one `ToolRequestEvent` is emitted synchronously with correct `name` and `args`.
- Malformed JSON in `args` causes the parser to emit a stream error, not a malformed `ToolRequestEvent`.
- Test confirms no state accumulation across chunks is needed (unlike Anthropic's `input_json_delta` accumulation).

---

### REQ-F-004: Multiple Function Calls in Single Chunk Emit Multiple Events

**EARS Template:** Conditional
**Requirement:**
If a single streamed chunk contains multiple `functionCall` parts within the parts array, the SSE parser shall emit one `ToolRequestEvent` per `functionCall`, in array order, without collapsing or prioritizing a subset.

**Acceptance Criterion:**
- Fake-SSE test sends a chunk with two `functionCall` parts and verifies two `ToolRequestEvent`s are emitted in the same order.
- Each event carries correct name and args for its corresponding part.

---

### REQ-F-005: Thought Signature Captured When Present

**EARS Template:** Conditional
**Requirement:**
If a `functionCall` part carries an optional `thought_signature` string (present in Gemini 3.x thinking-capable models), the SSE parser shall capture it alongside the call metadata for later echoing in the follow-up turn's reconstructed history.

**Acceptance Criterion:**
- Fake-SSE test includes `"thought_signature": "xyz123"` in a `functionCall` part and verifies the signature is captured in the emitted `ToolRequestEvent` or internal context.
- Test confirms the signature persists through the tool-calling lifecycle.
- Non-thinking-model test (no `thought_signature` field) produces a valid event without error.

---

### REQ-F-006: Provider Call ID Used or Synthesized

**EARS Template:** Conditional
**Requirement:**
The SSE parser shall use the `id` field from a `functionCall` part if present (Gemini 3.x strict-matching models), and shall assign a synthesized UUID-based `provider_call_id` if absent (older/non-thinking models). No hard error shall occur when `id` is missing.

**Acceptance Criterion:**
- Fake-SSE test with `"id": "call_123"` field verifies `provider_call_id` in emitted `ToolRequestEvent` equals `"call_123"`.
- Fake-SSE test without `id` field verifies `provider_call_id` contains a syntactically valid UUID.
- Both cases produce a valid `ToolRequestEvent` without errors.

---

### REQ-F-007: Function Response Reconstructed on Follow-Up Turn

**EARS Template:** State-driven
**Requirement:**
While reconstructing the conversation history for a follow-up turn after a tool call completes, the Google provider shall append the original `functionCall` part and a corresponding `functionResponse` part to the message history in the request body, using Gemini's `Tool` response schema with `"functionResponse": {"name": ..., "id": ..., "response": {...}}`.

**Acceptance Criterion:**
- Fake-integration test covering a complete tool-call round-trip (call → tool execution → follow-up request) verifies the follow-up request body contains both the original `functionCall` and `functionResponse` parts in the correct sequence.
- Response reconstruction uses the correct tool name and result.

---

### REQ-F-008: Thought Signature Echoed on Follow-Up (Thinking Models)

**EARS Template:** State-driven
**Requirement:**
While reconstructing history for a follow-up turn, if the original `functionCall` carried a `thought_signature`, the reconstructed history shall include the same `thought_signature` value in the `functionCall` part. This enables Gemini 3.x thinking models to maintain reasoning continuity across tool calls.

**Acceptance Criterion:**
- Fake-integration test confirms a round-trip with `thought_signature` present in the original call results in the same signature being echoed in the follow-up request's history.
- Non-thinking-model round-trip (no signature in original) produces history without a `thought_signature` field.

---

### REQ-F-009: Provider Call ID Omitted When Absent in Original

**EARS Template:** Conditional
**Requirement:**
When a `functionCall` part lacked an `id` field originally (client-synthesized UUID was used internally for correlation only), the reconstructed `functionResponse` part on the follow-up turn shall NOT include an `id` field—only the `name` and `response` fields. The internal correlation remains valid because `ToolOrchestrator` uses the internal UUID, but the protocol-level echo omits what the model never sent.

**Acceptance Criterion:**
- Fake-integration test with a synthesized (non-model) ID verifies the follow-up request's `functionResponse` part has no `id` field.
- Fake-integration test with a model-provided `id` verifies the follow-up request's `functionResponse` includes that `id` verbatim.
- Both cases handle tool results correctly and allow the conversation to continue.

---

### REQ-F-010: ProviderAdapterRouter Gates Google Tool Catalog by Setting

**EARS Template:** Conditional
**Requirement:**
The `ProviderAdapterRouter::sendChat()` method shall check `GoogleProviderConfig::tool_calling_enabled` (or its equivalent boolean flag) before passing the tools catalog to `GoogleProvider::sendChat()`. If `tool_calling_enabled` is false, no tools catalog shall be passed, and the request body shall omit the `"tools"` key.

**Acceptance Criterion:**
- Unit test verifies `ProviderAdapterRouter::sendChat()` for Google provider with `tool_calling_enabled == true` passes a non-empty tools catalog.
- Unit test verifies same router with `tool_calling_enabled == false` passes an empty/absent tools catalog.
- Router dispatch logic correctly identifies when to apply Google-specific gating.

---

### REQ-F-011: ProviderAdapterRouter Dispatch Selects Google Provider Correctly

**EARS Template:** Conditional
**Requirement:**
The `ProviderAdapterRouter` shall include a dispatch branch (via `std::get_if`, `if constexpr`, or variant visitor pattern) that correctly routes calls to `GoogleProvider::sendChat()` with the appropriate (4-arg or 5-arg with tools) overload based on the configuration type and `tool_calling_enabled` flag.

**Acceptance Criterion:**
- Code review confirms dispatch logic exists and compiles.
- Unit test verifies a `GoogleProviderConfig` instance triggers the Google provider codepath, not Anthropic or Ollama.

---

### REQ-F-012: GoogleProviderConfig Includes tool_calling_enabled Field

**EARS Template:** Ubiquitous
**Requirement:**
The `GoogleProviderConfig` struct shall include a boolean `tool_calling_enabled` field, defaulting to `false`.

**Acceptance Criterion:**
- Field is declared in `holonight_config/provider_config.h` or equivalent.
- Default initialization sets it to `false`.
- Compiles without error.

---

### REQ-F-013: New Google Instances Default tool_calling_enabled to False

**EARS Template:** Ubiquitous
**Requirement:**
When a new `GoogleProviderConfig` instance is constructed (by user adding a new Google provider via settings), `tool_calling_enabled` shall default to `false`.

**Acceptance Criterion:**
- Default constructor or factory function for new provider instances sets the flag to `false`.
- Unit test confirms new instance has `tool_calling_enabled == false`.

---

### REQ-F-014: Legacy Google Instances Loaded from Storage Default tool_calling_enabled to False

**EARS Template:** Conditional
**Requirement:**
When loading a `GoogleProviderConfig` from persistent storage (e.g., JSON file created before this feature was implemented), if the `tool_calling_enabled` key is absent, the deserialized instance shall default the field to `false` for backward compatibility.

**Acceptance Criterion:**
- Unit test loads a Google provider config JSON fragment without a `tool_calling_enabled` key and verifies the deserialized object has `tool_calling_enabled == false`.
- No hard error or exception is raised for missing key.

---

### REQ-F-015: GoogleProviderConfig Round-Trip Serialization

**EARS Template:** Ubiquitous
**Requirement:**
The `GoogleProviderConfig::tool_calling_enabled` field shall serialize to JSON and deserialize back to the same value, maintaining consistency across app restarts.

**Acceptance Criterion:**
- Unit test serializes a `GoogleProviderConfig` with `tool_calling_enabled == true`, deserializes it, and verifies the value is `true`.
- Test repeats with `false`.
- JSON shape matches the schema used by `config_repository`.

---

### REQ-F-016: GoogleProviderSettingsController Exposes toolCallingEnabled Property

**EARS Template:** Ubiquitous
**Requirement:**
The `GoogleProviderSettingsController` class shall include a `Q_PROPERTY(bool toolCallingEnabled READ toolCallingEnabled WRITE setToolCallingEnabled NOTIFY toolCallingEnabledChanged)` that exposes the underlying `GoogleProviderConfig::tool_calling_enabled` flag.

**Acceptance Criterion:**
- Property is declared in the class header.
- `toolCallingEnabled()` getter returns the current value.
- `setToolCallingEnabled(bool)` setter updates the value and emits the change signal.
- Property binds successfully from QML.

---

### REQ-F-017: QML Toggle UI for Tool Calling

**EARS Template:** Ubiquitous
**Requirement:**
The Google provider settings panel QML (in `qml/workspace/` or equivalent) shall include a toggle control (checkbox or switch) bound to `GoogleProviderSettingsController.toolCallingEnabled`, allowing the user to enable/disable tool calling for the Google provider.

**Acceptance Criterion:**
- QML file declares a control (e.g., `CheckBox` or `Switch`) with `checked: googleProviderSettings.toolCallingEnabled`.
- Control includes a label such as "Enable Tool Calling" or "Allow Function Calls".
- Manual QML inspection confirms binding is syntactically valid.

---

### REQ-F-018: Toggle Updates Underlying Provider Configuration

**EARS Template:** Event-driven
**Requirement:**
When the user toggles the tool-calling control in the settings UI, the change shall be written to the underlying `GoogleProviderConfig` and persisted to storage (via `ConfigRepository`), so that the setting is restored on app restart.

**Acceptance Criterion:**
- Integration test (or manual walk-through checklist) toggles the QML control, restarts the app, and verifies the setting is restored.
- Config is visibly saved to disk (e.g., by inspecting the JSON file).

---

## Non-Functional Requirements

### REQ-NF-001: Deterministic Behavior Except UUID Generation

**EARS Template:** Ubiquitous
**Requirement:**
SSE parsing and tool-request emission shall be deterministic and repeatable, with the sole exception of synthesized UUIDs (for client-side call IDs when Gemini does not supply an `id`). All parsing logic shall produce identical results for identical input.

**Acceptance Criterion:**
- Fake-SSE tests run multiple times with the same input produce identical `ToolRequestEvent` sequences (except UUID values, which may differ but remain valid).
- No random sleeps, non-deterministic JSON key ordering, or state pollution from prior test runs.

---

### REQ-NF-002: No Live API Calls in Tests

**EARS Template:** Unwanted behaviour
**Requirement:**
Test suites covering Google provider tool calling shall NOT make live HTTP requests to Gemini's API. All tests shall use fake/mocked SSE responses and request-body verification.

**Acceptance Criterion:**
- Code review of `tests/providers/test_google_provider.cpp` (or equivalent) confirms all networking is mocked.
- No hardcoded API keys or Google endpoints in test source.
- Tests run without internet access and complete quickly.

---

### REQ-NF-003: JSON Validity in Request Bodies

**EARS Template:** Ubiquitous
**Requirement:**
All generated request bodies (e.g., `"tools"` array, `functionResponse` reconstructions) shall be syntactically valid JSON, consumable by `QJsonDocument` and `QJsonObject` without parse errors.

**Acceptance Criterion:**
- Unit test parses generated request JSON with `QJsonDocument::fromJson()` and verifies `!doc.isNull()`.
- Fake-fixture test confirms no malformed JSON is ever constructed for the `"tools"` array or `functionResponse` reconstruction, across all covered scenarios (no live API call — see REQ-NF-002).

---

### REQ-NF-004: Parallel Function Calls Preserve Array Order

**EARS Template:** Ubiquitous
**Requirement:**
When a single SSE chunk contains multiple `functionCall` parts, the order of emitted `ToolRequestEvent`s shall match the order in the parts array.

**Acceptance Criterion:**
- Fake-SSE test with ordered calls `[call_A, call_B, call_C]` verifies events are emitted in that same order.
- Test confirms event metadata (name, args) matches the corresponding part index.

---

## Constraints

### REQ-C-001: Only ListFilesTool Wired This Cycle

**Requirement:**
This cycle wires the Google provider to the existing `ListFilesTool` only. No new tool implementations, tool definitions, or tool-specific logic are introduced in this cycle.

**Scope Boundary:**
Additional tools may be wired in future cycles; this cycle establishes the plumbing infrastructure for Google, mirroring Anthropic's implementation.

---

### REQ-C-002: Only Google Provider Adapter Touched

**Requirement:**
This cycle modifies only the Google provider adapter (`src/providers/src/google_provider.cpp`, `src/providers/include/holonight_providers/google_provider.h`, and related config). Ollama and OpenAI provider adapters remain completely untouched.

**Scope Boundary:**
Future cycles may wire tool calling to Ollama or OpenAI; those adaptations are out of scope.

---

### REQ-C-003: ListFilesTool Behavior Unchanged

**Requirement:**
The `ListFilesTool` implementation (directory enumeration, `$HOME` boundary enforcement, error taxonomy, read-only semantics) shall remain completely unchanged. This cycle is pure wiring/plumbing on the provider adapter and router sides.

**Scope Boundary:**
Tool implementation enhancements or additional tools are future work, not this cycle.

---

### REQ-C-004: kMaxToolCallsPerTurn Cap Applies Universally

**Requirement:**
The existing `kMaxToolCallsPerTurn` safety cap (default 10, defined in the generic tool-registry/orchestrator layer) shall apply to Google provider tool calls without modification. No Google-specific cap is introduced.

**Scope Boundary:**
Provider-specific caps are not justified by current threat model; the unified cap is sufficient.

---

### REQ-C-005: No Approval Gate for Tool Execution

**Requirement:**
No user confirmation dialog, blocking approval flow, or explicit permission grant is required before tool execution. This cycle does not introduce any approval mechanism.

**Scope Boundary:**
Visibility in the chat transcript is the sole compensating control (same design rationale as Anthropic tool calling: `ListFilesTool` is read-only and home-directory-restricted by design).

---

### REQ-C-006: Visibility in Transcript is Sole Compensating Control

**Requirement:**
Users rely on seeing tool calls and results rendered in the chat transcript to understand tool execution. The application provides no other disclosure or audit mechanism for tool invocations in this cycle.

**Scope Boundary:**
Audit logging or tool-execution history UI may be added in future work; not this cycle.

---

## Testing Strategy

### Test Coverage Outline (Informational)

The following fake-fixture tests (modeled on `tests/providers/test_anthropic_provider.cpp`) shall be implemented to verify conformance:

1. **SendChatIncludesToolsArrayInRequestBodyWhenProvided** — Verify non-empty tools catalog produces valid `"tools"` key in request.
2. **SendChatOmitsToolsKeyWhenToolsAbsentOrEmpty** — Verify empty/absent catalog produces no `"tools"` key.
3. **SendChatEmitsToolCallOnAtomicFunctionCallPart** — Verify single `functionCall` chunk produces exactly one `ToolRequestEvent`.
4. **SendChatEmitsMultipleToolCallsForMultipleFunctionCallParts** — Verify parallel calls in one chunk → multiple events in order.
5. **SendChatCapturesThoughtSignatureWhenPresent** — Verify `thought_signature` is captured alongside call metadata.
6. **SendChatHandlesMissingIdByGeneratingUUID** — Verify missing `id` field → synthesized `provider_call_id`.
7. **SendChatReconstructsFunctionCallAndResponseAcrossTurns** — Verify follow-up history includes both parts with correct metadata.
8. **SendChatEchoesThoughtSignatureOnFollowUp** — Verify signature round-trips in reconstructed history.
9. **SendChatOmitsIdInResponseWhenIdWasAbsentInOriginal** — Verify synthesized IDs do not appear in follow-up response.
10. **SendChatIncludesIdInResponseWhenIdWasInOriginal** — Verify model-provided IDs are echoed verbatim.
11. **SendChatFailsStreamOnMalformedFunctionCallArgs** — Verify invalid JSON in `args` causes stream error.
12. **ProviderAdapterRouterGatesToolCatalogByGoogleToolCallingEnabled** — Verify router respects the toggle.

---

## Related Documents

- **Precedent (Anthropic Tool Calling):** `docs/sdd/tool-calling-listfiles/SPEC.md` — establishes domain patterns (`ToolRequestEvent`, `ToolOrchestrator`, provider-agnostic `ChatController` dispatch).
- **Architecture:** `docs/high-level-project-idea.md`, `docs/provider-configuration.md`, `docs/sdd/tool-activity-rendering/`.
- **Gemini API Reference:** Google's `streamGenerateContent` schema for `Tool`, `functionCall`, `functionResponse`, and `thought_signature` (external reference).

---

## Glossary

| Term | Definition |
|------|-----------|
| `functionCall` | Gemini's schema for requesting tool execution, carrying `name`, `args`, optional `id`, and optional `thought_signature`. |
| `functionResponse` | Gemini's schema for returning tool results, carrying `name`, `id` (conditional), and `response`. |
| `thought_signature` | Optional string field in Gemini 3.x thinking-model `functionCall` parts; must be echoed in reconstructed history. |
| `provider_call_id` | Domain-layer correlation ID (`ToolRequestEvent` field) — sourced from model-provided `id` or synthesized UUID. |
| `ToolRequestEvent` | Domain event emitted when a provider detects a tool call; consumed by `ToolOrchestrator` and `ChatController`. |
| `ProviderAdapterRouter` | Application-layer component that gates tool catalogs and routes calls to provider adapters based on configuration. |

---

## Acceptance Criteria Summary

All requirements are independently verifiable:
- **Functional requirements** (REQ-F-001 to REQ-F-018) are testable via fake-SSE fixtures, unit tests, and integration tests.
- **Non-functional requirements** (REQ-NF-001 to REQ-NF-004) are verifiable by code review and test-run inspection.
- **Constraints** (REQ-C-001 to REQ-C-006) are verifiable by code review and scope boundary inspection.

---

## Version History

| Date | Author | Status | Notes |
|------|--------|--------|-------|
| 2026-08-05 | Claude Code | Draft | Initial EARS specification for Google tool-calling feature. |
