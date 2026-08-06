# Incremental Chat Content Rendering — Design

Status: Stage 2 (Design/Architecture) of the SDD cycle. This document maps `SPEC.md` onto the current
repository. The completed `advanced-markdown-rendering` documents remain unchanged.

## Overview

The current `MessageListModel` lazily caches a `QVariantList<ContentBlock>` only for complete/error
messages, and `MessageBubble.qml` switches between a message-wide streaming `TextEdit` and a finished
block `Repeater`. This cycle replaces that snapshot with one persistent `MessageContentModel` per
assistant row. Streaming changes are parsed and reconciled at most once every 50 ms; terminal changes
flush immediately. QML always renders non-empty assistant source through that child model.

The authoritative response remains `MessageListModel::Row::text`. Parsed blocks are a presentation
projection, never a persistence format. This keeps restored conversations compatible and guarantees
that fallback and clipboard operations cannot lose Markdown syntax (REQ-F-007, REQ-NF-002).

## Components and interfaces

### `ContentBlock` (`src/rendering`)

Extend the existing value type with:

- `QString id`, exposed as a constant `id` property;
- `bool complete`, exposed as a constant `complete` property;
- updated `markdown(...)` and `code(...)` factories accepting both fields.

The discriminated payload remains Markdown text or Code text/language. IDs are source-derived: use a
type-prefixed source-start UTF-16 offset (for example `markdown:0`, `code:27`). The start offset and
type stay stable while a tail grows or an opening fence gains a closing fence; content hashes are
deliberately excluded. Parser output therefore supports deterministic matching without process-local
counters (REQ-F-002, REQ-F-009).

### `MessageContentParser` (`src/rendering`)

Change the parse API to accept the raw Markdown and whether the message is terminal. A small result
type may carry `blocks` plus a success/fallback indicator so tests can force and observe failure
without inferring it from content.

MD4C continues to identify completed top-level fenced code. The parser also scans the unconsumed tail
for an opening backtick/tilde fence that MD4C cannot close yet. That tail becomes an incomplete Code
block with its opening fence/info string removed but code preserved literally. Non-code source ranges
remain Markdown. Block ranges are bounded by completed fences or the current source end; no special
table, quote, image, or tool-call types are introduced (REQ-F-001, REQ-C-001).

For a non-terminal parse, only boundaries that cannot be extended by appended text are marked
complete; the final block is incomplete. At a terminal status, all emitted blocks become complete,
including an unclosed fence, because no more source will arrive. That unclosed Code block remains
plain code with no invented closing fence. Empty source yields no blocks. Any parser/callback failure
yields one source-derived Markdown fallback containing the complete raw source (REQ-F-005,
REQ-F-008).

### `MessageContentModel` (`src/application`)

Add `MessageContentModel : public QAbstractListModel` with these roles:

| Role | Value |
|---|---|
| `blockId` | deterministic `ContentBlock::id()` |
| `type` | existing `ContentBlockType` enum |
| `text` | raw Markdown slice or literal code |
| `language` | normalized code language; empty for Markdown |
| `complete` | whether the block can still change semantically |

The model owns `std::vector<ContentBlock> blocks_`, the exact `QString source_`, a single-shot
`QTimer`, the most recently requested terminal state, and a publication counter or observable signal
usable by deterministic cadence tests. It exposes a C++ update entry point such as
`setSource(QString source, bool terminal)`, plus a QML-callable `copyRawMarkdown()` or an exact
`rawMarkdown` accessor consumed by the existing clipboard mechanism. Clipboard mutation stays at the
QML/application boundary; reconciliation never rebuilds source from rows (REQ-F-003, REQ-F-007).

The class belongs to `holonight_application`, because it owns Qt presentation timing and model
signals. Parsing/value types remain in `holonight_rendering`. Register the child model as an
uncreatable QML type only if QML type information needs it; QML consumes the returned QObject model
directly, so no new creatable API or `holonight-qt` registration is required (REQ-F-012, REQ-C-003).

## Update cadence and terminal flushing

On a non-terminal `setSource` call, store the latest source immediately. If the single-shot timer is
inactive, start it for 50 ms; if active, do nothing to its deadline. This is a non-restarting debounce
variant (a throttle): continuous chunks cannot postpone publication indefinitely and cannot publish
more often than once per interval.

When the timer fires, parse the latest stored source and reconcile once. If source changes while
publication is executing, normal queued UI-thread ordering causes that later call to schedule the
next 50 ms window.

On `complete`, `error`, or `cancelled`, stop the timer, store the final source/status, parse with
`terminal=true`, and reconcile synchronously before returning to the event loop. A generation token
or the stopped timer's state prevents a queued timeout from applying stale output afterward
(REQ-F-004, REQ-F-005, REQ-NF-001).

## Reconciliation algorithm

Given old rows and newly parsed rows:

1. Walk from index zero while old/new IDs and types match.
2. For each matching row, emit `dataChanged` only for text, language, or complete roles whose values
   changed; unchanged prefix rows emit nothing.
3. A matching mutable tail is updated in place even when its content/completion changes.
4. At the first ID/type divergence, find no speculative moves: remove the old suffix with
   `beginRemoveRows`/`endRemoveRows`, then insert the new suffix with
   `beginInsertRows`/`endInsertRows`.

This preserves the longest stable prefix and the matching tail delegate. Source-derived IDs prevent
text growth from looking like replacement. A conservative suffix replacement is preferable to row
moves because top-level blocks are ordered source ranges and only the tail should normally diverge
(REQ-F-002, REQ-F-009, REQ-F-011).

## `MessageListModel` integration and lifetime

Replace `Row::cached_content_blocks` with `std::unique_ptr<MessageContentModel>` (or an equivalent
stable QObject owner). Each assistant text row receives one model at row construction, including
restored rows. User/system and tool-activity rows may expose null or an empty child model according to
the existing delegate branches; they do not enter this renderer.

`MessageListModel` owns every child for at least as long as its row. Child objects should be parented
to `MessageListModel` so QML references cannot outlive the parent; vector moves transfer the owning
pointer without moving the QObject. Removing/resetting rows destroys their children only after the
outer model's proper row notifications. Updating the same message calls its existing child's
`setSource` and never swaps the QObject pointer (REQ-F-003).

`ContentBlocksRole` keeps the QML name `contentBlocks` but returns
`QVariant::fromValue<QObject*>(row.content_model.get())`. `updateNewestMessage()` forwards every
assistant source/status change. Terminal detection covers complete, error, and cancelled. The outer
model emits `ContentBlocksRole` only when the pointer itself changes, not for child row updates. Reset
and restore create the model from stored text with terminal parsing, so no database schema or message
serialization changes are needed (REQ-F-003, REQ-F-005, REQ-C-003).

## QML rendering

`MessageBubble.qml` removes the assistant message-wide streaming renderer and its
`isBlockRendering` switch. Its block `Repeater` is active for every non-empty assistant response and
binds directly to the persistent child model. Delegates use named roles (`type`, `text`, `language`,
`complete`, `blockId`) rather than `modelData` value objects. `MarkdownBlock` accepts text; the renamed
`ChatCodeBlock` accepts code/language/completion (or the model roles individually).

Rename `qml/shared/HnCodeBlock.qml` to `qml/shared/ChatCodeBlock.qml` and update
`apps/chat/CMakeLists.txt`, QML users, object names where relevant, and tests. The new name makes its
app-specific ownership explicit and avoids suggesting it is a reusable `Holonight.Controls` control
(REQ-F-010, REQ-F-012).

`ChatCodeBlock` always uses `TextEdit.PlainText`. While `complete == false`, it does not attach or
activate `CodeHighlighter`; its language header may show the normalized label but code remains plain.
When the same row becomes complete, attach/configure the existing highlighter and rehighlight. An
unknown definition stays `plain text`. Existing palette-change connections continue to refresh only
active highlighters (REQ-F-006, REQ-NF-003).

Add a full-response copy control to the assistant response footer. It invokes the content model's raw
copy surface and changes its label/icon to `Copied` briefly using the existing timer pattern. It must
be available for non-empty complete, error, and cancelled responses; any status-specific stats
visibility remains separate. Code-block copy continues to copy only its `text` role (REQ-F-007).

## Scrolling and selection

The existing newest-first/bottom-anchored `MessageList` policy remains authoritative. Child
`dataChanged` and suffix insert/remove signals change delegate implicit heights without resetting the
outer message row. When the view is in its existing follow-newest state, it reapplies the bottom
anchor after layout growth; when detached, it preserves the current visible position. Because stable
rows and the outer child-model pointer survive, selection within an unaffected `TextEdit` survives
tail updates. Cross-block selection remains out of scope (REQ-F-011).

## Failure and terminal paths

- **Complete:** synchronously flush all source; all blocks are complete and known code can highlight.
- **Error:** synchronously flush accumulated non-empty source and render it; error chrome remains.
- **Cancelled:** synchronously flush non-empty source; an empty placeholder remains hidden.
- **Empty:** child model has zero rows and raw source is empty; no synthetic Markdown row.
- **Parser failure:** publish one lossless Markdown fallback row with a stable source-start ID; later
  updates may parse normally and reconcile its divergent suffix.
- **Restored conversation:** treat stored assistant messages as terminal and build their child models
  immediately; no streaming timer is involved.

These paths share one source/model pipeline, avoiding the predecessor's blank or flickering renderer
switch (REQ-F-005, REQ-F-008, REQ-F-010).

## Ownership decision

Shared-control extraction is rejected for this cycle. There is no second consumer, the block types
and response-footer copy behavior are chat-application concepts, and extraction would add MD4C and/or
KSyntaxHighlighting policy to `holonight-qt`. Keeping `MarkdownBlock`, `ChatCodeBlock`, parsing, and
the child model local creates the smallest dependency surface. A shared component can be reconsidered
only when a second real consumer establishes a stable reusable API (REQ-F-012, REQ-C-002).

## Verification strategy

Parser tests cover source ranges, deterministic IDs, completion flags, incomplete backtick/tilde
fences, Unicode, terminal unclosed fences, and forced fallback. Child-model tests use a controllable
event loop/clock to verify non-restarting 50 ms coalescing, immediate terminal flush, narrow signals,
stable row identity, and raw source. `MessageListModel` tests verify pointer lifetime across updates,
restoration, and every terminal status. QML tests verify unified rendering, delegate persistence,
plain incomplete code, clipboard fidelity/feedback, and empty-terminal hiding. Manual validation
covers a long stream, scroll following/detachment, selection, completion highlighting, and live theme
switching (REQ-NF-001, REQ-NF-003).
