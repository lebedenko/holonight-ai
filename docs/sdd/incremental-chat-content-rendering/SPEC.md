# Incremental Chat Content Rendering — Specification

Status: Stage 1 (Specification) of a fresh SDD cycle.

This cycle succeeds the completed `advanced-markdown-rendering` cycle. It changes assistant-message
rendering from a finished-message-only block list to a persistent block model that is updated while
the response streams. The predecessor's specification, design, and completed task history remain an
unchanged record of that earlier behavior.

## Scope

Assistant response text is incrementally separated into ordered top-level Markdown and fenced-code
blocks. The same persistent model is exposed for streaming and terminal responses, allowing QML
delegates to retain identity while the changing tail is updated. Raw response Markdown remains the
source of truth for parsing, persistence, fallback, and full-response copy.

## Functional requirements

### REQ-F-001: Incremental semantic parsing

The system shall parse accumulated assistant response source during `pending` and `streaming`, as
well as when it reaches a terminal status.

- Parsing shall produce only top-level `Markdown` and fenced `Code` blocks, in source order.
- Consecutive non-code Markdown shall remain coalesced into bounded Markdown blocks; each fenced
  code region shall be one Code block.
- An opening fence without a closing fence shall produce an incomplete Code block immediately;
  its code text shall exclude the opening fence and retain every received code character.
- Appending source shall not reorder already stable blocks.

### REQ-F-002: Stable block identity and completion

Every block shall have a non-empty identity and an explicit completion state.

- A block ID shall be derived deterministically from its source position/type, not from its current
  text or a parse invocation counter.
- A block whose semantic boundary is known shall be `complete`; the mutable final block shall be
  incomplete while more source can extend or reinterpret it.
- Closing an incomplete fence shall retain that Code block's ID and change it to complete.
- Re-parsing an unchanged source/status pair shall produce the same IDs, order, text, language, and
  completion states.

### REQ-F-003: Persistent per-message content model

Each assistant message shall own one persistent list model for its content blocks.

- The model shall expose `blockId`, `type`, `text`, `language`, and `complete` roles.
- The `contentBlocks` role on `MessageListModel` shall return that child model, preserving the role
  name while replacing the predecessor's `QVariantList` value.
- The child model object shall survive text/status updates for the same message and shall update
  matching rows in place.
- Restored non-empty assistant messages shall receive the same model and block rendering as live
  messages.

### REQ-F-004: Coalesced publication cadence

Streaming source changes shall be coalesced with a fixed 50 ms publication cadence.

- The first unpublished streaming update shall start a single-shot timer only if it is not already
  active; later chunks shall not restart or postpone that timer.
- During continuous streaming, observable child-model publications shall occur no more than once
  per 50 ms interval.
- The most recent accumulated source shall be used when the timer fires.
- The cadence is fixed for this cycle and is not user-configurable.

### REQ-F-005: Immediate terminal flush

Transitions to `complete`, `error`, or `cancelled` shall synchronously publish the latest source and
terminal completion state, without waiting for a pending coalescing timer.

- A pending timer shall be stopped or rendered harmless after the terminal flush.
- Non-empty complete, error, and cancelled responses shall retain and render all accumulated text.
- An empty terminal assistant placeholder shall remain non-rendering, preserving current behavior.
- No stale post-terminal timer event shall overwrite the terminal model state.

### REQ-F-006: Incomplete code presentation

An unfinished fenced Code block shall be visible immediately as literal plain text.

- HTML, entities, and Markdown-like syntax in code shall remain literal.
- Incomplete code shall be selectable and horizontally scrollable under the existing code-block
  interaction policy.
- Syntax highlighting shall not run while `complete` is false.
- Once the block becomes complete, the existing highlighter shall be enabled without replacing the
  delegate or changing the block ID.
- Unknown or missing languages shall continue to display as plain text after completion.

### REQ-F-007: Lossless source retention and copying

The child content model shall retain the complete raw Markdown source independently of its parsed
rows and expose a full-response copy operation to QML.

- Source retention shall preserve fences, info strings, whitespace, Unicode, and unfinished input
  exactly as received.
- Copying the response from its footer shall put that exact source on the system clipboard, rather
  than concatenating rendered block texts.
- The footer shall provide short success feedback after a successful copy.
- Existing code-block copy shall continue to copy code only.

### REQ-F-008: Parser-failure fallback

If semantic parsing fails, the system shall preserve content and remain usable.

- The complete raw source shall be published as one Markdown block (or no row for empty source).
- The fallback row shall have a stable ID and a completion state consistent with message status.
- A parser failure shall not clear existing content, crash QML, or disable later updates.
- Terminal copy shall still reproduce the exact raw source.

### REQ-F-009: Stable suffix reconciliation

Each publication shall reconcile parsed output with existing rows by preserving the longest stable
prefix, updating a matching tail row in place, and replacing only a divergent suffix.

- Rows with the same block ID and type shall receive role-level `dataChanged` updates rather than
  removal/insertion.
- Rows before the first divergence shall produce no structural model signals.
- A divergent suffix shall be removed and inserted with correct Qt model notifications.
- Delegate instances for stable rows shall remain alive across stream updates.

### REQ-F-010: Unified assistant rendering

All non-empty assistant content, regardless of status, shall render through the child block model.

- The message-wide streaming `TextEdit` branch shall be removed for assistant messages.
- Markdown rows shall use `MarkdownBlock`; Code rows shall use the project-local code component.
- Per-block selection shall remain available; cross-block drag selection is not required.
- User-message rendering shall remain unchanged.

### REQ-F-011: Scrolling and delegate stability

Incremental row updates shall integrate with the existing bottom-anchored transcript behavior.

- When the transcript is following the newest response, streamed growth shall keep the newest
  content visible.
- When the user has scrolled away, incremental publications shall not force the view back to the
  bottom or discard selection in stable delegates.
- Completing a block, including enabling highlighting, shall not recreate unaffected delegates.

### REQ-F-012: Local component naming and ownership

The local `HnCodeBlock` component shall be renamed to `ChatCodeBlock` and remain owned by this
repository together with parsing and chat-block presentation.

- All local QML, resources, registrations, and tests shall use `ChatCodeBlock`.
- `MarkdownBlock`, `ChatCodeBlock`, `ContentBlock`, `MessageContentParser`, and the child content
  model shall remain in `holonight-ai`.
- No control, parser, highlighter, or KSyntaxHighlighting dependency shall be added to
  `holonight-qt` in this cycle.

## Non-functional requirements

### REQ-NF-001: Bounded UI-thread work

Streaming updates shall avoid unbounded work per provider chunk. Parsing/model publication may run
on the UI thread, but it shall occur only at the coalesced cadence, operate on one message, and emit
the narrowest correct model signals. A long continuously streamed response shall remain responsive
to scrolling, selection, and window input.

### REQ-NF-002: Source fidelity

No parsing, normalization, rendering, highlighting, or reconciliation step shall mutate or replace
the canonical raw Markdown source. Copy and fallback behavior shall be byte-for-byte equivalent at
the QString character level to the accumulated response.

### REQ-NF-003: Graceful highlighting and theme behavior

Unknown language definitions and incomplete blocks shall use plain text without errors. Completed
known-language blocks shall retain the existing live HoloNight theme refresh behavior; switching
themes shall not alter text, block IDs, row order, selection, or scroll position.

## Constraints

### REQ-C-001: Supported block types

The semantic model shall contain exactly Markdown and fenced Code blocks. Tables, quotes, images,
lists, headings, inline code, and other Markdown constructs remain source within Markdown blocks.

### REQ-C-002: Existing rendering dependencies

The implementation shall extend the existing `holonight_rendering` static library and its MD4C and
KSyntaxHighlighting integration. It shall not introduce WebEngine, JavaScript rendering, a new
Markdown framework, or a new shared-library dependency.

### REQ-C-003: Compatibility boundary

The public QML role name `contentBlocks` shall remain unchanged. Its value becomes a persistent
`QAbstractListModel` child object, and all in-repository consumers shall migrate atomically.

## Non-goals

- Dedicated table, quote, image, or tool-call Markdown blocks.
- Cross-block drag selection.
- Refactoring user-message rendering.
- Configurable update cadence.
- WebEngine or JavaScript Markdown rendering.
- Moving code rendering, parsing, or KSyntaxHighlighting into `holonight-qt`.
- Long-code expansion, line numbers, wrap toggles, save, run, editor, or project-insertion actions.

## End-to-end acceptance criteria

1. A streamed response containing prose, two fenced code blocks, and trailing prose displays those
   blocks in source order and preserves every source character for full-response copy.
2. An incomplete fence appears as an incomplete plain-text Code row; closing it updates the same row
   and delegate to complete, then enables highlighting for a known language.
3. Stable prefix rows keep their IDs and delegate instances while only the changing tail updates;
   structural signals are limited to a genuinely divergent suffix.
4. Continuous rapid chunks yield at most one publication per 50 ms window, while complete, error,
   and cancelled transitions make the latest text visible synchronously.
5. Full-response copy matches the raw source exactly, including fences, whitespace, Unicode, and an
   unfinished final fence; code copy still copies code alone.
6. Text remains selectable within each block. Streaming growth follows the bottom only when already
   following it and does not steal position or stable selection after the user scrolls away.
7. Unknown and missing fence languages remain readable plain text; known completed code re-highlights
   on theme changes without changing model identity, ordering, source, or transcript position.
8. A forced parser failure renders all non-empty source in one Markdown fallback row and preserves
   terminal copy fidelity without a crash or blank state.
9. Restored complete, error, and non-empty cancelled assistant messages use the same ordered block
   renderer; empty terminal placeholders remain hidden.
10. A manual long-stream check shows bounded publication frequency and no perceptible loss of window,
    scrolling, selection, completion, or theme-switch responsiveness.
