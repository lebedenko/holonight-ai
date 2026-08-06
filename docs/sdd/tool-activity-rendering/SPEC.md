# Tool Activity Rendering and Extensible Tool Architecture — SPEC.md

## Overview

This cycle replaces the current pair of raw tool-call/tool-result cards with one compact semantic
activity item per invocation. The item remains visible in the assistant turn, uses shared collapsed
chrome for every tool, and expands into tool-specific content. `ListFiles` is the first specialized
renderer: its expanded view presents file and directory rows rather than making raw JSON the primary
UI.

The cycle also establishes provider-neutral contracts for future tooling. A locally executable tool
is registered as a complete package containing its model-facing definition, executor, and presenter.
Provider adapters remain responsible for their own wire formats. QML remains responsible for visual
components. No tool implementation returns a QML URL or imports UI concepts.

## Context

The completed `tool-calling-listfiles` cycle introduced:

- `ITool`, `ToolRegistry`, and synchronous `ListFilesTool` execution;
- Anthropic `tool_use`/`tool_result` parsing and history reconstruction;
- separate Invocation and Result `ToolCallEntry` messages persisted in `messages.tool_calls`; and
- `ToolCallCard.qml`, which renders the two records separately and exposes raw JSON directly.

This specification evolves that proof of concept into a reusable product surface while preserving
the existing provider-facing history and stored conversations.

## Superseded Requirements

### `tool-calling-listfiles` REQ-F-007 and REQ-F-011 — Rendering Amendment

The requirement that invocation and result be separate visible message entries is superseded at the
presentation layer. They remain separate protocol/history records, joined by provider call ID, but
the transcript shall project them as one visible tool activity item.

### `tool-calling-listfiles` REQ-NF-002 — Execution Contract Amendment

The requirement that every tool execute synchronously is superseded. Tool execution shall expose an
asynchronous completion contract so running state can reach the event loop and future long-running
or cancellable tools do not block the UI. A tool may complete immediately, but callers shall use the
same asynchronous lifecycle contract.

## Functional Requirements

### REQ-F-001: Join Invocation and Result Into One Activity

**Event-driven:** WHEN an invocation record and its result share the same provider call ID, the
transcript shall render them as one tool activity rather than two message cards.

**Acceptance Criteria:**

1. The application projection uses the provider call ID (`tool_use_id` today) as the correlation
   key within one conversation.
2. A result updates the existing activity row in place; it does not insert a second visible row.
3. An invocation without a result remains a valid activity row in a non-terminal state.
4. Orphan results and duplicate IDs fail safely through the generic presentation path and never
   crash or hide unrelated transcript entries.
5. Provider-facing history remains reconstructable with the original invocation and result records.

### REQ-F-002: Normalized Tool Invocation Lifecycle

**Ubiquitous:** The application shall represent every tool activity with a normalized lifecycle
independent of provider wire terminology.

**Acceptance Criteria:**

1. The lifecycle contains `Requested`, `AwaitingApproval`, `Running`, `Completed`, `Failed`,
   `Denied`, and `Cancelled` states.
2. This cycle exercises `Requested`, `Running`, `Completed`, `Failed`, and `Cancelled`; approval
   states exist in the model but no approval workflow is added.
3. Invalid backward transitions are rejected.
4. Terminal activities remain immutable except for idempotent restoration of the same state.
5. Restored historical Invocation/Result pairs derive `Completed` or `Failed` deterministically.

### REQ-F-003: Semantic Compact Summary

**State-driven:** WHILE a tool activity is collapsed, it shall display a concise human-readable
summary of what is happening or what happened.

**Acceptance Criteria:**

1. `ListFiles` uses semantic labels such as `Listing ~/Pictures…`, `Listed ~/Pictures`, and
   `Couldn’t list ~/Pictures` rather than `Tool: ListFiles`.
2. A completed `ListFiles` activity shows its entry count; a failed activity shows a concise error.
3. Completed activities show duration when known.
4. The compact summary never exposes full raw arguments/results.
5. Unknown or historical tools receive a safe generic summary containing the tool display name and
   status.

### REQ-F-004: Shared Expandable Activity Shell

**Ubiquitous:** Every tool activity shall use one shared collapsed/expanded shell regardless of
tool identity.

**Acceptance Criteria:**

1. The shell owns status icon, title, short metadata, duration, disclosure affordance, expansion
   state, error styling, and accessibility behavior.
2. Successful activities default to collapsed after completion.
3. Running, failed, approval-required, denied, and cancelled activities default to expanded.
4. User expansion/collapse remains stable while the activity stays visible and is not reset by
   unrelated model updates.
5. The expanded body is loaded only while expanded.

### REQ-F-005: Mandatory Tool Presentation Contract

**Ubiquitous:** Every locally registered tool shall provide presentation behavior in addition to
its model-facing definition and executor.

**Acceptance Criteria:**

1. Tool registration requires a stable canonical tool ID, protocol-safe function name, definition,
   executor, and presenter.
2. Registration rejects a local tool that has no presenter.
3. The presenter produces semantic labels, icon name, result summary, renderer key, and normalized
   detail data from a normalized invocation.
4. The presenter has no dependency on QML component types or resource URLs.
5. Every built-in/local tool declares a non-empty renderer key that resolves to a dedicated expanded
   QML renderer; registration/contract tests fail when that mapping is missing.
6. A generic presenter/renderer exists only for unknown, historical, provider-hosted, or externally
   sourced tools that are not local registrations.

### REQ-F-006: Renderer Registry and Generic Fallback

**Ubiquitous:** The QML layer shall select expanded content through a renderer registry keyed by the
presenter’s renderer key.

**Acceptance Criteria:**

1. `ToolActivityCard.qml` contains a `Loader` for the expanded body.
2. `ToolRendererRegistry` resolves known renderer keys to QML `Component`s.
3. `filesystem.list` resolves to a dedicated ListFiles renderer.
4. Missing or invalid renderer keys resolve to a generic structured-data renderer.
5. The shared shell contains no conditional branch on concrete tool IDs.
6. Runtime fallback does not weaken the development contract: all registered built-in/local tools
   must have a dedicated renderer mapping.

### REQ-F-007: Specialized ListFiles Expanded Renderer

**State-driven:** WHEN a `filesystem.list` activity is expanded, its primary content shall be a
structured directory listing.

**Acceptance Criteria:**

1. The header shows path, terminal status, duration, and counts of files/directories where known.
2. Entries render one per row with distinct semantic file and directory icons.
3. Entry order matches the deterministic order returned by `ListFilesTool`.
4. A bounded preview is shown initially; `View all` reveals the remaining entries without invoking
   the tool again.
5. Empty directories have an explicit empty state.
6. Structured tool errors render as an error explanation rather than an empty listing.

### REQ-F-008: Raw Data Disclosure

**Event-driven:** WHEN the user requests diagnostic details, the activity shall expose its original
arguments and result without making them the default product UI.

**Acceptance Criteria:**

1. Expanded activities provide a `View raw` disclosure.
2. Raw arguments and result use read-only selectable fixed-width text.
3. `Copy result` copies the complete result, not a truncated preview.
4. Raw data is serialized deterministically and malformed historical data is represented safely.
5. Raw details never replace the specialized body when a specialized renderer is available.

### REQ-F-009: Observable Non-Blocking Execution

**Event-driven:** WHEN a local tool is accepted for execution, the application shall publish
`Running` before executing work and publish exactly one terminal outcome afterward.

**Acceptance Criteria:**

1. Tool execution does not block the GUI thread.
2. The running activity can be rendered before completion.
3. Completion callbacks are delivered on the controller/application thread.
4. Exceptions and executor failures become structured `Failed` outcomes.
5. A tool that completes immediately still follows the same lifecycle contract.

### REQ-F-010: Optional Cancellation

**Conditional:** IF an executor advertises cancellation support, THEN a running activity shall
offer a Stop action.

**Acceptance Criteria:**

1. Stop is absent for non-cancellable tools.
2. Stop requests cancellation through an opaque execution handle, never through QML-owned worker
   state.
3. Confirmed cancellation transitions the activity to `Cancelled` exactly once.
4. Late completion after cancellation is ignored.
5. `ListFiles` need not advertise cancellation in this cycle.

### REQ-F-011: Provider-Neutral Tool Events

**Ubiquitous:** Provider adapters shall normalize tool-related protocol events before application
orchestration sees them.

**Acceptance Criteria:**

1. A normalized request includes provider call ID, protocol function name, parsed arguments,
   execution location, and provider instance identity.
2. Application orchestration dispatches only client-executed local tool requests.
3. Provider-hosted tool observations are visible/presentable but are not sent to `ToolRegistry` for
   local execution.
4. Provider-specific content blocks/items/parts do not leak into `ToolActivityCard` or specialized
   renderers.
5. Correlation IDs are preserved exactly when the provider supplies them.

### REQ-F-012: Provider-Owned Wire Codecs

**Ubiquitous:** Each provider integration shall own translation between the provider-neutral tool
catalog/events and its native request, stream, result, and history shapes.

**Acceptance Criteria:**

1. The application/router passes a provider-neutral catalog snapshot, not an Anthropic-shaped JSON
   array.
2. Anthropic translation remains in the Anthropic provider/codec.
3. Future OpenAI, Google, and Ollama integrations can be added without changing tool executors,
   presenters, or QML renderers.
4. A provider may decline unsupported tools or schema features without changing the canonical tool
   definition.
5. Adding provider support requires provider codec tests for definition encoding, streamed call
   decoding, result encoding, and history reconstruction.

### REQ-F-013: Stable Identity and Aliases

**Ubiquitous:** Tool identity shall be stable across UI, persistence, and provider protocols.

**Acceptance Criteria:**

1. `filesystem.list` is the canonical application tool ID.
2. A separate protocol-safe function name is used on provider wires.
3. Historical `ListFiles` records remain resolvable through an alias.
4. Renderer selection uses canonical tool ID, never provider function name.
5. Unknown aliases fall back safely without dispatching a different tool.

### REQ-F-014: Persistence and Restoration

**Event-driven:** WHEN a conversation is reopened, terminal and incomplete tool activities shall be
reconstructed with the same correlation, semantic renderer, chronological position, and raw data.

**Acceptance Criteria:**

1. Existing `messages.tool_calls` data remains readable without a destructive migration.
2. New optional lifecycle/presentation identity fields are encoded backward-compatibly.
3. Invocation and result records remain lossless for provider history reconstruction.
4. An invocation left non-terminal by process termination restores as `Cancelled` or an explicit
   interrupted state mapped to cancelled presentation; it never appears indefinitely running.
5. No view-only expansion state is persisted to SQLite in this cycle.

### REQ-F-015: Transcript Placement

**Ubiquitous:** Tool activities shall remain in chronological order inside the assistant’s turn.

**Acceptance Criteria:**

1. Preliminary assistant text appears before the corresponding activity.
2. Follow-up assistant text appears after the activity.
3. Pairing/suppression of the raw result row does not reorder adjacent user or assistant messages.
4. Multiple consecutive tool activities remain individually visible in this cycle.
5. Grouping several activities into a single summary is deferred.

### REQ-F-016: Accessibility and Keyboard Interaction

**Ubiquitous:** Tool activity controls shall be usable without a mouse and expose meaningful
accessible state.

**Acceptance Criteria:**

1. The disclosure control is keyboard focusable and toggles with Enter/Space.
2. Accessible name includes semantic summary and lifecycle status.
3. Expansion state is exposed as expanded/collapsed.
4. Stop, Copy result, View all, and View raw have descriptive accessible names when present.
5. Status is not communicated by color alone.

## Non-Functional Requirements

### REQ-NF-001: Layering

Tool executors and presenters shall live in `holonight_application`; normalized invocation types
shall live in `holonight_domain`; provider wire codecs shall live in `holonight_providers`; QML
renderers shall live under `qml/shared/`. No lower layer shall depend on QML.

### REQ-NF-002: Deterministic Presentation

Given the same canonical tool ID, arguments, result, status, and timestamps, a presenter shall
produce the same renderer key and semantic detail model independent of locale. Human-readable QML
labels may be translated, but matching and field extraction shall not depend on translated text.

### REQ-NF-003: Robust Fallback

Malformed, incomplete, unknown, or future-version tool data shall render through the generic
fallback without crashing, silently disappearing, or preventing the conversation from loading.

### REQ-NF-004: Bounded UI Work

Collapsed activities shall not instantiate specialized renderers or format full raw JSON. Large
results shall use bounded previews and must not create unbounded delegates until the user requests
`View all`.

### REQ-NF-005: Disclosure Safety

Presentation metadata shall distinguish read-only from harmless. Labels and future approval copy
must be able to explain that filenames or tool results may be disclosed to a configured cloud
provider even when the tool has no local side effects.

### REQ-NF-006: Testability

Lifecycle transitions, correlation, presenter outputs, provider codec mappings, transcript
projection, and renderer selection shall be independently testable without live provider calls or
the real home directory.

## Constraints and Non-Goals

### REQ-C-001: First Specialized Renderer Only

Only the generic renderer and `ListFiles` specialized renderer are implemented in this cycle.

### REQ-C-002: No New Provider Wiring

Anthropic remains the only provider that executes local tools in this cycle. OpenAI, Google, and
Ollama codec designs are documented for follow-on cycles but are not enabled here.

### REQ-C-003: No Approval Workflow Yet

Approval lifecycle states and presentation seams are included, but Allow/Deny policy and UI are
deferred.

### REQ-C-004: No Tool Grouping

Consecutive activity grouping (`Used 4 tools`) is deferred.

### REQ-C-005: No MCP Integration

MCP discovery/execution is out of scope, but unknown/external tool fallback must not prevent a
future MCP source from supplying descriptors and presenters.

### REQ-C-006: No New Concrete Tools

No ReadFile, WriteFile, shell, edit, or network tool is introduced.

## Definition of Done

- Invocation and result render as one semantic, expandable activity.
- `ListFiles` renders icon-bearing entry rows with a bounded preview.
- Raw JSON is available only through explicit disclosure.
- Local tool registration enforces a presenter.
- Execution lifecycle is observable without blocking the GUI thread.
- Existing conversations and Anthropic history round-trip correctly.
- Focused C++/QML tests, full `task test`, `task format-check`, and `task qml-lint` pass.
