# Tool-Calling Framework (ListFiles Tool) — SPEC.md

## Overview

This spec defines a generic LLM tool-calling (function-calling) framework with a single concrete implementation: the `ListFiles` tool for the Anthropic provider.

The feature enables Claude models to autonomously list directory contents via structured tool calls, enhancing the chat experience with filesystem introspection capabilities while maintaining strict security boundaries (home-directory-only access, read-only operations) and visibility (all tool calls and results recorded and persisted).

**Scope:** Anthropic provider only. Other providers (Ollama, OpenAI, Google) are explicitly deferred to future cycles.

---

## Context & Assumptions

- The existing codebase defines domain types (`Message`, `MessageRole`, `MessageStatus`, `StreamEvent`) and provider orchestration patterns (`ChatController`, provider-specific adapters, settings controllers).
- Prior cycles have established constraints on `Message` and `StreamEvent` types; this spec explicitly amends those constraints for tool-related functionality (see "Superseded Requirements" section below).
- The roadmap (`docs/high-level-project-idea.md`) sketches future tool-runtime capabilities (approval gates, sandboxing, audit, permissions); this cycle is a minimal first slice.
- The five-provider orchestration pattern (request/result flow through `ChatController::handleStreamEvent()`) is the natural interception point for tool-call events.

---

## Superseded Requirements

This spec explicitly amends the following prior requirements. These are NOT contradictions—they represent evolved design decisions as the project moves from "no tools" to "first tool support."

### REQ-F-005 from `docs/sdd/domain-core-types/SPEC.md` — Partial Amendment

**Prior statement:** The `Message` domain type shall explicitly forbid `tool_calls` and `attachments` fields.

**Amended by this spec:** The `tool_calls` field is NOW REQUIRED on the `Message` domain type to support persisting and reconstructing tool-call events. The `attachments` field remains forbidden (not amended by this spec).

**Justification:** Supporting tool-calling requires recording tool invocations and results in the conversation history; domain messages must carry this metadata for both UI rendering and provider API reconstruction.

---

### REQ-F-020 from `docs/sdd/domain-core-types/SPEC.md` — Partial Amendment

**Prior statement:** `StreamEvent` shall NOT include `ToolCall` or `Usage` variants.

**Amended by this spec:** A new `ToolCall` variant is NOW REQUIRED on `StreamEvent` to signal tool invocations during streaming. The `Usage` variant was already amended by the `usage-cost-tracking` cycle (no longer live); only the ToolCall-forbidding portion was still standing and is now superseded by this spec.

**Justification:** Tool calls arrive as stream events from the provider; a dedicated variant allows the orchestration layer to dispatch them distinctly from text content or completion signals.

---

### Prior Anthropic Adapter Constraint from `docs/sdd/anthropic-provider-adapter/SPEC.md` — Partial Amendment

**Prior statement:** The Anthropic request body deliberately excludes `tools`, `tool_choice`, and `thinking`.

**Amended by this spec:** The `tools` field is NOW CONDITIONALLY INCLUDED in the Anthropic request body when tool-calling is enabled (see REQ-F-009 and REQ-F-010). The `tool_choice` and `thinking` fields remain excluded (not amended by this spec).

**Justification:** Tool-calling is now a user-opt-in feature; enabling it requires the request body to advertise available tools to the model.

---

## Functional Requirements

### REQ-F-001: Tool Registry and Generic Orchestration

**Ubiquitous statement:** The system shall provide a `ToolRegistry` component that allows tools to register themselves by name, permits the application to discover and iterate over registered tools, and enables invocation of tools generically by name without branching on specific tool identities.

**Acceptance Criterion:** (1) A `ToolRegistry` class or interface exists that supports: registering a tool with a unique name and JSON schema; iterating over or querying all registered tools; looking up a tool by name; invoking a tool by name with a JSON parameters object and receiving a JSON result object. (2) The ChatController's stream-event handling path (e.g., `handleStreamEvent()`) uses the registry to invoke tools without conditional branches on individual tool names (e.g., no `if (toolName == "ListFiles")` statements). (3) A new tool can be added to the system by implementing an interface and registering it, without modifying the orchestration layer's dispatch logic.

---

### REQ-F-002: ITool Interface and ListFiles Implementation

**Ubiquitous statement:** The system shall define an `ITool` interface (abstract base or protocol) with methods for name, JSON schema declaration, and synchronous execution; and shall provide a concrete `ListFilesTool` implementation adhering to this interface.

**Acceptance Criterion:** (1) An `ITool` interface exists with at minimum: `name(): QString` (or equivalent), `schema(): QJsonObject` (tool's parameter schema), and `execute(parameters: QJsonObject): QJsonObject` (invoke the tool and return result). (2) A `ListFilesTool` class implements this interface. (3) The ListFiles tool can be instantiated and registered with the ToolRegistry without additional boilerplate or adapter code.

---

### REQ-F-003: ListFiles Directory Listing Behavior

**Ubiquitous statement:** The system shall implement the `ListFiles` tool to list immediate (non-recursive) entries of a given directory path, reporting each entry's name and type (file or directory), and excluding dotfiles (hidden entries starting with `.`) by default.

**Acceptance Criterion:** (1) Given a valid directory path (e.g., `~/Documents`), ListFiles returns a JSON result object of the form:
```json
{
  "entries": [
    {"name": "report.pdf", "type": "file"},
    {"name": "subfolder", "type": "directory"},
    {"name": "notes.txt", "type": "file"}
  ]
}
```
(2) The listing is non-recursive (only immediate children, no nested subdirectories). (3) Entries beginning with `.` (dotfiles/hidden) are excluded from results even if they exist on the filesystem. (4) The order of entries is consistent (e.g., alphabetical) across multiple invocations of the same path.

---

### REQ-F-004: Path Safety and $HOME Boundary Enforcement

**Ubiquitous statement:** The system shall restrict all `ListFiles` invocations to paths that canonically resolve within the OS user's home directory (`$HOME`), rejecting any path that resolves outside this boundary or is invalid.

**Acceptance Criterion:** (1) Given a requested path (e.g., `~/Documents`, `$HOME/projects`, relative paths like `./folder`, or absolute paths like `/home/user/documents`), the ListFiles tool canonicalizes it (resolving `..`, `.`, and symlinks to an absolute real path). (2) The canonicalized path is compared against `$HOME`; if it does not fall under `$HOME`, a structured error result (see REQ-F-005) is returned instead of listing contents. (3) Paths resolving outside `$HOME` (e.g., `/etc/passwd`, `~/../etc/passwd`, or a symlink pointing to `/var`) are rejected with the same error format as a non-existent path, preventing user enumeration. (4) The comparison is exact and case-sensitive (on case-sensitive filesystems).

---

### REQ-F-005: Structured Error Results

**Ubiquitous statement:** The system shall return structured error results (as JSON objects, not exceptions or crashes) for all `ListFiles` error conditions: path does not exist, path is not a directory, permission denied reading the directory, and path resolves outside `$HOME`.

**Acceptance Criterion:** (1) All four error conditions (non-existent, not-a-directory, permission-denied, out-of-bounds) produce a JSON result object of the form:
```json
{
  "error": {
    "code": "ERROR_CODE_STRING",
    "message": "Human-readable error message."
  }
}
```
Example: `{"error": {"code": "PERMISSION_DENIED", "message": "Permission denied reading directory."}}`. (2) No exception or assertion escapes to the caller; the `execute()` method catches all errors and returns a result object. (3) No application crash or uncaught exception occurs. (4) The application's internal logging may record the error; the result object is returned to the stream-event handler and eventually to the Anthropic API as the tool's result content.

---

### REQ-F-006: Tool-Call Loop Safety Cap

**Ubiquitous statement:** The system shall enforce a configurable maximum number of consecutive tool calls within a single user turn (setting a named constant `MAX_TOOL_CALLS_PER_TURN` with a default value of 10), and shall terminate the tool-calling loop and surface a clear error to the user if the cap is exceeded.

**Acceptance Criterion:** (1) A named constant `MAX_TOOL_CALLS_PER_TURN` is defined in a configuration header or constants file (e.g., `src/application/include/holonight_application/tool_registry.h` or similar), with a default value of 10. (2) When the Anthropic provider returns the 11th tool-use block in a single user turn, the application stops processing further tool calls. (3) The application displays a user-visible error message in the chat transcript such as: "Tool-calling limit exceeded (max 10 calls per turn); stopping." or "Reached maximum tool calls; conversation resumed." (4) The 11th and subsequent tool calls are not executed; the chat-controller loop terminates cleanly. (5) The cap value can be tuned by changing the named constant (not by hand-editing code at runtime or per-conversation).

---

### REQ-F-007: Message Type and Rendering for Tool Calls and Results

**Ubiquitous statement:** The system shall record tool calls and tool results as message entries in the conversation, visually distinguishable from standard user and assistant text messages when rendered in the transcript.

**Acceptance Criterion:** (1) When a tool call is invoked (e.g., ListFiles with path `~/Documents`), a Message entry is created containing metadata about the tool call (at minimum: tool name and input parameters). (2) When the tool result is available (either success listing or error), a corresponding Message entry is created containing the result. (3) When rendering the conversation transcript, these tool-related entries are visually distinct from user and assistant text messages; they are not folded inline into an assistant message bubble or hidden in a collapsible section (they may use a distinct container, styling, or explicit label such as "Tool: ListFiles"). (4) The tool-call and tool-result messages appear in the transcript in the correct chronological order: they follow the user message that prompted them and precede any subsequent user message.

---

### REQ-F-008: Persistence of Tool Calls and Results

**Ubiquitous statement:** The system shall persist tool-call and tool-result messages to the SQLite conversation repository, ensuring they are fully recoverable when a conversation is reopened and the provider-facing history remains reconstructable.

**Acceptance Criterion:** (1) After a tool call and its result are recorded in the message list and rendered in the UI, the Message entries are inserted into the SQLite database via the existing ConversationRepository. (2) When the application exits and later reopens the same conversation, the tool-call and tool-result messages are retrieved from the database and appear in the transcript in the correct order. (3) When the chat history is reconstructed and sent to the Anthropic API for a follow-up turn, the tool-related messages are included, allowing the model to reason over prior tool invocations. (4) The database schema supports storing the tool call and result metadata without loss of fidelity.

---

### REQ-F-009: Tool-Calling Enablement Toggle in Anthropic Settings

**Ubiquitous statement:** The system shall expose a boolean setting in the Anthropic provider's settings UI that controls whether the `tools` array is populated and sent to the Anthropic Messages API.

**Acceptance Criterion:** (1) The Anthropic provider settings UI (via the existing `OpenAIProviderSettingsController` or Anthropic-specific subclass) displays a toggle or checkbox control labeled "Enable Tool Calling" or similar. (2) When this setting is disabled, the `tools` field is not included in the request body sent to the Anthropic API. (3) When this setting is enabled, the `tools` field is populated with the JSON schemas of all registered tools and included in the request body. (4) The setting is persisted per Anthropic provider instance in the provider configuration (holonight_config module) and survives application shutdown and restart.

---

### REQ-F-010: Tool-Calling Defaults to Disabled

**Ubiquitous statement:** The system shall default tool-calling to OFF (disabled) for both new Anthropic provider instances and existing instances upon application load after deployment.

**Acceptance Criterion:** (1) When a user creates a new Anthropic provider via the UI, the `toolCallingEnabled` setting (or equivalent boolean key) is initialized to `false`. (2) When an existing Anthropic provider instance is loaded from persistent storage (holonight_config) on app startup and the `toolCallingEnabled` setting key does not exist in the configuration, it defaults to `false`. (3) Tool-calling never becomes active without explicit user action (flipping the toggle in settings). (4) The setting's default does not change on subsequent app startups; once a user has set it, the setting persists per #9.

---

### REQ-F-011: Tool-Call Visibility as Compensating Control

**Ubiquitous statement:** The system shall record all tool calls and results visibly in the chat transcript (and persist them per REQ-F-008) as a compensating control for the absence of an approval gate before execution, justified by `ListFiles` being read-only and home-directory-restricted.

**Acceptance Criterion:** (1) Immediately after a tool call is executed, a message entry appears in the chat transcript showing at minimum the tool name and input parameters (not hidden, not deferred, not collapsed by default). (2) Immediately after a result is available, a second message entry shows the result (success listing or error message). (3) The entries remain visible as long as the conversation is open; they are not deleted, archived, or hidden from the user. (4) The entries are persisted (per REQ-F-008) and visible when the conversation is reopened. (5) This visibility requirement—combined with the read-only and boundary-restricted nature of ListFiles—serves as the primary security compensating control in place of a blocking approval dialog.

---

## Non-Functional Requirements

### REQ-NF-001: JSON Schema and Validity

**Ubiquitous statement:** Tool parameter schemas and result objects shall be valid JSON Schema v7 and well-formed JSON, respectively, to ensure compatibility with the Anthropic Messages API and downstream JSON parsing.

**Acceptance Criterion:** (1) The ListFiles tool's parameter schema (property describing the `path` parameter, constraints, etc.) is valid according to JSON Schema v7 specification. (2) All result objects returned by ListFiles (success or error) are well-formed JSON that parse successfully with standard JSON libraries (Qt's QJsonDocument, etc.). (3) A schema validation tool or test confirms the ListFiles schema is valid before or during CI. (4) Malformed JSON is never sent to the Anthropic API or stored in the database.

---

### REQ-NF-002: Non-Blocking Tool Execution

**Event-driven statement:** WHEN the model requests a tool call via the Anthropic Messages API, the system shall invoke the tool synchronously and immediately within the event-handling loop, without blocking the UI or forcing the user to approve the call.

**Acceptance Criterion:** (1) Tool execution (the `execute()` method) completes synchronously; no thread blocking or async/await is required for tool execution itself (error handling and result collection are part of the synchronous call). (2) The UI remains responsive during tool execution; the chat message list does not freeze or display a "waiting for approval" overlay. (3) The tool result is collected and returned to the stream-event handler within the provider's response-streaming loop, ensuring the model sees the result and can issue follow-up tool calls (up to the cap in REQ-F-006) within the same turn. (4) No dialog box or confirmation prompt blocks the tool execution.

---

### REQ-NF-003: Deterministic Tool Listing Order

**State-driven statement:** The tool registry shall maintain a deterministic, stable order when enumerating registered tools (e.g., registration order or alphabetical), ensuring the `tools` array sent to the Anthropic API has consistent ordering across multiple requests.

**Acceptance Criterion:** (1) The ToolRegistry's iteration method (or the code that builds the `tools` array for the API request) returns tools in a deterministic order. (2) Running the same API request twice in the same session produces an identical `tools` array JSON structure (same order, same schemas). (3) Tools are registered once at startup and not dynamically added/removed during runtime.

---

## Constraints & Non-Goals

### REQ-C-001: ListFiles Tool Only

**Constraint:** No `ReadFile`, `WriteFile`, `RunCommand`, or other tools besides `ListFiles` shall be implemented in this cycle.

**Rationale:** Scope is deliberately narrow to ensure thorough testing and integration of the first tool. Other tools are explicitly deferred to future cycles, each of which will define their own SPECs and implement provider-specific adapters.

---

### REQ-C-002: Anthropic Provider Only

**Constraint:** Tool-calling framework and the `ListFiles` tool shall NOT be wired into the Ollama, OpenAI, or Google provider adapters in this cycle.

**Rationale:** Each provider has distinct tool-calling syntax, semantics, and request/response formats (Ollama may not support tools; OpenAI uses `tools` array and `function` types; Google uses `tools` array with `functionDeclarations`). Each provider integration is deferred to separate, provider-specific future cycles to avoid cross-provider complexity.

---

### REQ-C-003: Hardcoded $HOME Root, No Runtime Configuration

**Constraint:** The `$HOME` root path enforced for `ListFiles` shall be hardcoded based on the OS user's home directory and shall not be configurable per conversation, per app instance, or via user settings in this cycle.

**Rationale:** Simplifies the initial implementation and eliminates the need for per-instance root-path storage and validation. Security is ensured by the hardcoded boundary. Per-instance configuration is deferred to a future cycle if needed.

---

### REQ-C-004: No MCP (Model Context Protocol) Integration

**Constraint:** This feature shall NOT integrate with the Model Context Protocol (MCP) or any external tool-discovery or tool-invocation mechanism.

**Rationale:** MCP is out of scope; the tool registry is internal-only and controls only tools implemented within this application. MCP support is deferred to a future cycle.

---

### REQ-C-005: No Blocking User Approval Gate

**Constraint:** Tool execution shall NOT be blocked or delayed by a user confirmation dialog, approval workflow, or explicit user interaction.

**Rationale:** `ListFiles` is read-only and home-directory-restricted by design. Visibility in the transcript (REQ-F-011) serves as the primary compensating control. A future cycle may introduce approval gates for higher-risk tools (write, execute, external APIs, etc.).

---

### REQ-C-006: Shallow Listing Only

**Constraint:** `ListFiles` shall NOT recursively list subdirectories or read file contents.

**Rationale:** Scope is minimal. Recursive directory listing and file-reading capabilities are deferred to separate, dedicated tools in future cycles (if implemented at all).

---

## Summary of Acceptance Criteria

| REQ ID | Criterion Summary |
|--------|-------------------|
| REQ-F-001 | ToolRegistry exists; ChatController uses it generically without branching on tool names. |
| REQ-F-002 | ITool interface defined; ListFilesTool implements it; can be instantiated and registered. |
| REQ-F-003 | ListFiles returns entries with name, type; excludes dotfiles. |
| REQ-F-004 | Canonicalized paths checked against `$HOME`; out-of-bounds returns error, not crash. |
| REQ-F-005 | All four error conditions return structured JSON error objects; no exceptions escape. |
| REQ-F-006 | `MAX_TOOL_CALLS_PER_TURN` constant defined (default 10); exceeding cap shows error to user. |
| REQ-F-007 | Tool calls and results recorded as distinct message entries; visibly distinguished in transcript. |
| REQ-F-008 | Tool-call/result messages persisted to SQLite; recoverable on conversation reopen. |
| REQ-F-009 | Anthropic settings UI exposes tool-calling toggle; controls whether tools sent to API. |
| REQ-F-010 | `toolCallingEnabled` defaults to false for new and existing instances. |
| REQ-F-011 | All tool calls and results immediately visible in transcript as compensating control. |
| REQ-NF-001 | Tool schemas and results are valid JSON Schema v7 and well-formed JSON. |
| REQ-NF-002 | Tool execution is non-blocking; no UI freeze or approval dialog. |
| REQ-NF-003 | Tool registry maintains deterministic ordering when enumerating tools. |
| REQ-C-001 | Only ListFiles tool in scope; other tools deferred. |
| REQ-C-002 | Only Anthropic provider; other providers deferred. |
| REQ-C-003 | `$HOME` root hardcoded; no per-conversation or per-instance configuration. |
| REQ-C-004 | No MCP integration. |
| REQ-C-005 | No user approval gate before tool execution. |
| REQ-C-006 | Shallow listing only; no recursive or file-content features. |

---

## Related Documentation

- `docs/high-level-project-idea.md` — Future tool-runtime roadmap (approval gates, sandboxing, audit, permissions).
- `docs/sdd/domain-core-types/SPEC.md` — Prior REQ-F-005, REQ-F-020 (amended by this spec).
- `docs/sdd/anthropic-provider-adapter/SPEC.md` — Prior constraint on `tools`/`tool_choice`/`thinking` (amended for `tools` by this spec).
- `docs/provider-configuration.md` — Provider instance scoping and configuration persistence.
- `src/domain/include/holonight_domain/message.h` — Message domain type (to be extended with `tool_calls` field per amended REQ-F-005).
- `src/domain/include/holonight_domain/stream_event.h` — StreamEvent variant (to be extended with `ToolCall` variant per amended REQ-F-020).
