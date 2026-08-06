# Chat Message Delegate Boundary — Specification

Status: Complete. This cycle succeeds `assistant-response-rendering-boundary` and preserves the
existing message rendering and viewport behavior.

## Functional requirements

### REQ-F-001: Dedicated row delegate

`ChatMessageDelegate` shall own per-row card selection and configuration. `MessageList` shall use
that component as its model delegate instead of defining the routing and cards inline.

### REQ-F-002: Explicit role contract

`ChatMessageDelegate` shall require the existing identity, content, status, timestamp, provider,
usage, and tool-call roles: `messageId`, `role`, `text`, `status`, `createdAt`, `modelName`,
`providerId`, `providerType`, `providerName`, `contentBlocks`, `inputTokenCount`,
`outputTokenCount`, `reasoningTokenCount`, `cacheCreationTokenCount`, `cacheReadTokenCount`,
`totalTokenCount`, `durationMs`, and `toolCall`. The extraction shall preserve each role's existing
type and its distinction between undefined, null, and a concrete value.

### REQ-F-003: Routing compatibility

The delegate shall apply the existing routing precedence exactly:

1. A `toolCall` that is neither undefined nor null selects `ToolActivityCard`.
2. Otherwise, role `user` selects `UserMessageCard`.
3. Every remaining role, including `system`, selects `MessageBubble`.

### REQ-F-004: Layout and lifecycle compatibility

The extraction shall preserve the configured message-width ratio, loader activation only after its
width is positive, delegate height derived from the loaded card's implicit height, row visibility,
and loaded-card identity while the selected route remains unchanged. Ordinary updates to role data
that do not change the route shall update bindings without recreating the loaded card.

### REQ-F-005: Tool interaction forwarding

For a tool row, `ChatMessageDelegate` shall emit the current tool-use ID when the card requests a
stop and shall forward the card's user-expansion-started and user-expansion-finished events.

### REQ-F-006: Viewport ownership

`MessageList` shall continue to own the loading state, `BottomAnchoredListView`, invocation of
`ChatViewModel.stop()`, and item-top preservation around user-driven tool expansion. The outer
delegate item shall remain the item passed to `beginPreservingItemTop()`.

## Non-functional requirements

- **REQ-NF-001: No rendering regression.** The refactor shall introduce no visible flicker, card
  recreation from ordinary role updates, or bottom-anchored/detached scroll-position regression.
- **REQ-NF-002: Focused testability.** Stable object names shall be added only at the delegate,
  loader, and instantiated-card seams required by focused tests.
- **REQ-NF-003: Repository quality.** QML lint, formatting checks, and automated tests shall remain
  clean.

## Constraints

- **REQ-C-001:** Do not change C++ code, model roles, or their data semantics.
- **REQ-C-002:** Do not visually redesign or rename any message card.
- **REQ-C-003:** Do not decompose response statistics or tool-card content in this cycle.
- **REQ-C-004:** Do not change the `system`-role fallback to `MessageBubble`.

## Acceptance scenarios

1. Given a row with role `user` and a null or undefined `toolCall`, the delegate loads one
   `UserMessageCard` with the existing text, timestamp, width, and maximum-width-ratio bindings.
2. Given a row with role `assistant` and a null or undefined `toolCall`, the delegate loads one
   `MessageBubble` with all existing content, status, provider, timestamp, and usage bindings.
3. Given a `system` row, or any other non-user role, with a null or undefined `toolCall`, the
   delegate loads `MessageBubble` rather than introducing a new route.
4. Given any role with a non-null `toolCall`, including role `user`, the delegate loads
   `ToolActivityCard`; tool routing takes precedence over role routing.
5. Given a loaded route, changing text, status, provider, timestamp, content, usage, or tool data
   without changing that route updates the card through bindings while preserving the loaded-card
   object identity.
6. Given a tool card that requests stop, the delegate forwards its tool-use ID and `MessageList`
   calls `ChatViewModel.stop()`; expansion start and finish reach the list's item-top preservation
   operations with the outer delegate item at expansion start.
7. Given transcript growth, streaming height changes, tool expansion, following-latest mode, or a
   user-detached viewport, the existing bottom-anchor and scroll-preservation behavior remains
   unchanged.
