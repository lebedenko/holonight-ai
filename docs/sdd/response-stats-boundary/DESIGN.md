# Response Statistics Boundary — Design

Status: Complete. This design implements `SPEC.md` without changing public C++ or message-delegate
interfaces.

## Component boundary

Add `qml/shared/ResponseStatsFooter.qml` as the final row in `MessageBubble`'s content layout. It
receives the existing assistant/status and flattened usage values unchanged. The component owns
the existing completed-assistant visibility expression and retains `durationMs !== undefined` as
the usage-presence check.

Move the existing token badges, duration badge, information button, and `ResponseStatsPopup`
instance into the component without changing their bindings or presentation. The popup continues
to reparent itself to the window overlay and anchors to the information button, so clipping,
stacking, and placement remain independent of the bubble surface.

`MessageBubble` keeps all required properties and passes them directly to the footer.
`ChatMessageDelegate` and every upstream model binding remain unchanged.

## Verification

A focused QML test instantiates `ResponseStatsFooter` in a real `QQuickWindow`. It covers status and
usage visibility, independent optional badges, abbreviated tokens, duration units, every popup
input, accessible button naming, and button-driven popup open/close behavior. Existing delegate
tests protect the unchanged upstream contract. Repository QML lint, formatting, and tests provide
the broader regression check.
