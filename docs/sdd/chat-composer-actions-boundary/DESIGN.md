# Chat Composer Actions Boundary — Design

Status: Complete. This design implements `SPEC.md` without changing upstream chat contracts.

## Component boundary

Add `qml/shared/ChatComposerActions.qml` as the final item in `ChatComposer`'s content layout. The `RowLayout`
receives presentation policy as four required Boolean properties and owns all footer controls. Retry and send clicks
are exposed as signals, keeping the component independent of `ChatViewModel`.

The component imports both `Holonight.Controls` and `Holonight` explicitly. QML imports are file-local, and the
unqualified controls must resolve to the Holonight implementations rather than the simultaneously imported
`QtQuick.Controls.Basic` types.

`ChatComposer` continues to derive submission eligibility from streaming state, send policy, and trimmed editor
content. It binds that result and regeneration policy into the action row, translates action signals into
`ChatViewModel.regenerate()` and `submit()`, and retains every editor and draft responsibility.

## Verification

A focused QML test instantiates `ChatComposerActions` directly and covers desktop/compact visibility, disabled
placeholder presentation and accessibility, retry policy and forwarding, send state and forwarding, and stable seam
names. The existing composer test remains integration coverage for focus, draft synchronization, keyboard handling,
submission restrictions, restoration, and successful submission.
