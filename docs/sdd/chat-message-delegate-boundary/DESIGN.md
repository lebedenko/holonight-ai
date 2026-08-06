# Chat Message Delegate Boundary — Design

Status: Complete. This design implements `SPEC.md` as a behavior-preserving QML ownership refactor
without changing C++ or model interfaces.

## Component boundary

Add `qml/shared/ChatMessageDelegate.qml` with `pragma ComponentBehavior: Bound`. Its root is an
`Item` containing the same required role properties currently declared by the inline
`MessageList` delegate (identity, role, text, status, timestamp, model/provider attribution,
content blocks, token usage, duration, and tool call), plus a required `real messageWidthRatio`
input (REQ-F-001, REQ-F-002).

The root owns the existing routing function, `Loader`, and three local `Component` definitions for
`UserMessageCard`, `MessageBubble`, and `ToolActivityCard`. Preserve `active: width > 0`, select a
source only while active, set the loader width from its parent, and calculate the outer item's
height from the loaded item's implicit height or zero when no item exists. Each card keeps its
current full-width binding and receives `messageWidthRatio` as `maximumWidthRatio` (REQ-F-003,
REQ-F-004).

Give the root, loader, and each instantiated card variant stable, test-facing object names. No
internal visual elements receive new object names (REQ-NF-002).

## Routing and card bindings

The route function first tests `toolCall !== undefined && toolCall !== null`, then tests
`role === "user"`, and otherwise chooses the message bubble. The explicit null check is required:
a null tool call must not select the tool card, and tool routing must remain ahead of role routing
(REQ-F-003, REQ-C-004).

Preserve all current bindings without reinterpretation:

- `UserMessageCard` receives `text` as `messageText`, `createdAt`, and the width ratio.
- `MessageBubble` receives role, text, status, model name, `providerId`, provider type/name,
  timestamp, content blocks, all six token counts, duration, and the width ratio. `providerId`
  remains forwarded even if the current card does not consume it visibly.
- `ToolActivityCard` receives `toolCall`, `createdAt`, and the width ratio.

Because `sourceComponent` depends only on the selected route, updates to ordinary data properties
flow through bindings and must not replace `Loader.item` while the route stays the same
(REQ-F-004, REQ-NF-001).

## Interaction and viewport ownership

Declare `stopRequested(string toolUseId)`, `userExpansionStarted()`, and
`userExpansionFinished()` on the delegate. The tool card emits the first with the tool-use ID from
its stop request and forwards both expansion lifecycle signals (REQ-F-005).

Replace the inline delegate in `MessageList.qml` with `ChatMessageDelegate`, bind its width to
`ListView.view.width`, and pass `root.messageWidthRatio`. `MessageList` handles the three signals:
it invokes `ChatViewModel.stop()` for stop, calls
`messageView.beginPreservingItemTop(messageDelegate)` for expansion start, and calls
`messageView.finishPreservingItemTop()` for expansion finish. This intentionally passes the outer
`ChatMessageDelegate`, not its loader item or tool card (REQ-F-006).

The loading state, `BottomAnchoredListView`, model binding, visibility, spacing, clipping, and
`followingLatest` alias stay in `MessageList`. The existing QML source glob registers the new
application component, so application CMake does not change (REQ-F-006, REQ-C-001).

## Verification

Add one focused QML test source for `ChatMessageDelegate` and list only that source in
`tests/CMakeLists.txt`. Tests instantiate the component with controlled role data and assert user,
assistant, system-fallback, and tool-precedence routes through the stable seam object names. They
also cover full property forwarding, null-tool behavior, loader inactivity at zero width,
width/implicit-height behavior, loaded-item identity across ordinary updates, and all tool signals
(REQ-F-002 through REQ-F-005, REQ-NF-001, REQ-NF-002).

Existing assistant-content, tool-card, and bottom-anchor tests remain the integration safety net
for rendering and viewport compatibility. QML lint, formatting, and the full suite provide the
repository-level check (REQ-F-006, REQ-NF-003).
