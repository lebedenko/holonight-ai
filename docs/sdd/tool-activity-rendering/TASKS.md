# SDD Tasks — tool-activity-rendering

All tasks are initially unchecked. Implement in dependency order and keep each task reviewable.

## Domain and Compatibility

- [x] T-001: Add normalized tool identity and lifecycle domain types
  - REQs: REQ-F-002, REQ-F-011, REQ-F-013
  - Check: `ToolInvocationStatus`, execution location/source, `ToolError`, and `ToolInvocation` exist;
    transition tests cover all valid, invalid, idempotent, and terminal transitions.

- [x] T-002: Extend ToolCallEntry encoding with backward-compatible normalized metadata
  - REQs: REQ-F-013, REQ-F-014
  - Check: canonical `tool_id`, exact function name, lifecycle/timestamps, and execution location
    round-trip when present; legacy rows without them still decode and `ListFiles` resolves through an
    alias.

- [x] T-003: Implement invocation/result correlation projector
  - REQs: REQ-F-001, REQ-F-014, REQ-F-015, REQ-NF-003
  - Check: chronological ledger entries pair by provider call ID into one aggregate; invocation-only,
    orphan-result, duplicate-ID, malformed, and restored interrupted cases have deterministic tests.

## Tool Package Contracts

- [x] T-004: Split model definition, executor, and presenter contracts
  - REQs: REQ-F-005, REQ-F-009, REQ-NF-001
  - Check: `ToolDefinition`, `IToolExecutor`, `ToolExecutionHandle`, `IToolPresenter`, and
    `ToolRegistration` compile without QML dependencies.

- [x] T-005: Refactor ToolRegistry to register complete tool packages
  - REQs: REQ-F-005, REQ-F-013
  - Check: registry rejects missing presenters/executors/renderer keys, duplicate canonical IDs/
    function names/renderer keys/aliases, resolves historical `ListFiles`, and provides a provider-
    neutral catalog snapshot.

- [x] T-006: Implement generic tool presenter
  - REQs: REQ-F-003, REQ-F-005, REQ-F-006, REQ-NF-003
  - Check: unknown/historical/provider-hosted tools receive safe semantic status text, generic
    renderer key, and deterministic structured/raw detail data.

- [x] T-007: Implement ListFiles presenter
  - REQs: REQ-F-003, REQ-F-007, REQ-NF-002
  - Check: success, empty, mixed file/folder, malformed entry, and structured error results produce
    correct semantic labels, counts, icons, renderer key, and normalized detail data.

## Execution and Orchestration

- [x] T-008: Convert ListFiles execution to the asynchronous executor contract
  - REQs: REQ-F-009, REQ-F-010
  - Check: filesystem enumeration runs off the GUI thread, completion returns to the application
    thread, all existing path-safety/error behavior remains covered, and handle reports non-
    cancellable for this cycle.

- [x] T-009: Add ToolOrchestrator lifecycle management
  - REQs: REQ-F-002, REQ-F-009, REQ-F-010
  - Check: Requested/Running are published before terminal outcome, exactly one terminal transition
    occurs, exceptions become Failed, and cancellation/late-callback guards are tested.

- [x] T-010: Update ChatController to consume normalized tool events and orchestrator outcomes
  - REQs: REQ-F-001, REQ-F-009, REQ-F-011, REQ-F-015
  - Check: controller no longer directly calls synchronous `ToolRegistry::invoke`; provider history
    records remain lossless; activity/model/persistence updates occur at both Running and terminal
    boundaries.

## Provider-Neutral Boundary and Anthropic Codec

- [x] T-011: Introduce provider-neutral ToolCatalogSnapshot and normalized provider tool events
  - REQs: REQ-F-011, REQ-F-012
  - Check: ProviderAdapterRouter no longer builds or passes an Anthropic-shaped tools array; execution
    location and exact provider correlation ID are preserved.

- [x] T-012: Extract Anthropic tool translation into a provider-owned codec/helper
  - REQs: REQ-F-011, REQ-F-012, REQ-C-002
  - Check: definition encoding, streamed input accumulation, client/server classification, local
    result encoding, and history reconstruction remain Anthropic-owned and pass focused tests.

- [x] T-013: Document unsupported-provider selection behavior in runtime policy
  - REQs: REQ-F-012, REQ-C-002
  - Check: OpenAI, Google, and Ollama receive no local catalog in this cycle; adding their codecs does
    not require executor/presenter/QML changes; contract expectations are captured in provider test
    comments or a shared test fixture plan.

## Transcript Projection

- [x] T-014: Extend MessageListModel into a pairing-aware transcript projection
  - REQs: REQ-F-001, REQ-F-014, REQ-F-015
  - Check: one ToolActivity row replaces Invocation/Result rows, result updates the correct existing
    row in place, ordinary message ordering is unchanged, and full model resets are not used for live
    completion.

- [x] T-015: Expose normalized tool activity roles to QML
  - REQs: REQ-F-002, REQ-F-003, REQ-F-004, REQ-F-006
  - Check: stable invocation ID, canonical tool ID, status, title, summary, duration, renderer key,
    detail data, raw data availability, error state, and cancellation capability are exposed with
    focused role/dataChanged tests.

- [x] T-016: Preserve transient expansion state across updates
  - REQs: REQ-F-004
  - Check: completed defaults collapsed, running/failed/cancelled default expanded, explicit user
    choice survives status and unrelated row updates, and expansion is not persisted.

## QML Shared Shell and Renderers

- [x] T-017: Replace ToolCallCard with ToolActivityCard shared shell
  - REQs: REQ-F-003, REQ-F-004, REQ-F-016
  - Check: compact semantic row, lifecycle/status glyph, duration/summary, disclosure, semantic error
    state, conditional Stop action, and keyboard/accessibility behavior render without tool-ID
    branches.

- [x] T-018: Add ToolRendererRegistry and generic expanded renderer
  - REQs: REQ-F-006, REQ-F-008, REQ-NF-003, REQ-NF-004
  - Check: known keys select registered components, missing keys select GenericToolContent, collapsed
    activities do not instantiate expanded content, generic arguments/results remain readable, and
    a contract test proves every registered built-in/local renderer key has a dedicated component.

- [x] T-019: Add ListFilesToolContent specialized renderer
  - REQs: REQ-F-007, REQ-NF-004
  - Check: path/count summary, semantic file/folder icons, deterministic rows, bounded preview, View
    all, empty state, and structured error state are covered by headless QML tests.

- [x] T-020: Add raw disclosure and copy-result actions
  - REQs: REQ-F-008, REQ-F-016
  - Check: raw JSON is hidden by default, selectable when opened, copy uses complete result, malformed
    historical data is safe, and controls have accessible names.

- [x] T-021: Integrate activity rows into MessageList chronology
  - REQs: REQ-F-001, REQ-F-015
  - Check: preliminary assistant text, one activity item, and final assistant text render in order;
    raw Result row is suppressed; consecutive calls remain separate.

## Persistence and Regression Coverage

- [x] T-022: Persist lifecycle metadata and restore interrupted activities
  - REQs: REQ-F-014
  - Check: new metadata round-trips through SQLite JSON, legacy rows remain loadable, provider-facing
    history is unchanged, and incomplete restored execution displays Cancelled/interrupted rather
    than Running.

- [x] T-023: Add end-to-end ChatViewModel regression coverage
  - REQs: REQ-F-001 through REQ-F-010, REQ-F-014, REQ-F-015
  - Check: fake Anthropic stream plus fake asynchronous executor demonstrates visible Running,
    terminal in-place update, immediate persistence, specialized presenter selection, and no
    duplicate transcript row.

- [x] T-024: Verify generic fallback and historical conversation compatibility
  - REQs: REQ-F-006, REQ-F-013, REQ-F-014, REQ-NF-003
  - Check: pre-cycle `ListFiles` data, unknown tool names, orphan results, and malformed presentation
    data all remain visible and do not prevent conversation loading.

## Documentation and Full Verification

- [x] T-025: Update README and architecture documentation after implementation
  - REQs: All
  - Check: documented module responsibilities match final code; availability, permission, and
    disclosure are described as separate future policy axes.

- [x] T-026: Run full verification and manual smoke test
  - REQs: All
  - Check: focused tests, `task test`, `task format-check`, and `task qml-lint` pass; manual workspace
    smoke test confirms one compact completed ListFiles row, expansion with formatted entries, View
    all/raw/copy behavior, and historical conversation restoration.
  - Verification completed 2026-08-05: full build and 615/615 CTest cases passed (one
    environment-dependent Secret Service integration test skipped); format-check and qmllint passed.
    Manual workspace smoke testing confirmed compact and expanded ListFiles rendering, disclosure
    resizing and scroll positioning, themed actions, raw/copy behavior, and historical restoration.
