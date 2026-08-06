# 0002: Anchor chat scrolling at the newest message

## Status

Accepted

## Context

Chat messages have variable heights and many delegates are virtualized. Qt therefore estimates a
`ListView`'s `contentHeight` from the delegates that currently exist. The previous implementation
reacted to that estimate with timers, forced layouts, and repeated positioning. Streaming text,
Markdown reflow, and viewport-height changes could feed those estimates back into layout and cause
visible jumps or sustained allocation work.

## Decision

The QML presentation model stores messages newest first while domain conversations and persistence
remain chronological. The message view uses `ListView.BottomToTop`, making row 0 the natural bottom
geometric origin.

An app-owned `BottomAnchoredListView` tracks whether the user is following the latest message.
Explicit sends reveal the latest row, model resets queue one `showLatest()` call, and deliberate
history scrolling pauses following. No behavior depends on `contentHeight`, timers, `forceLayout()`,
opacity hiding, or repeated positioning.

Streaming assistant text renders as plain text. Completed and error messages continue through the
lazy Markdown/content-block renderer. Layout containers own the vertical geometry: the message list
is the only fill-height child and surrounding controls use intrinsic heights.

## Consequences

The `MessageListModel` row order is an internal newest-first projection and streaming updates target
row 0. Visual order remains chronological. Delegate growth and changes to the available viewport
space remain anchored at the latest edge without consulting estimated total content geometry.
