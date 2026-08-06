# DESIGN: Left sidebar panel redesign

**Spec:** `docs/sdd/left-sidebar-panel/SPEC.md`
**Status:** Draft
**Date:** 2026-07-24

## Overview

This cycle replaces `ConversationListPanel.qml`'s minimal content (a "New Chat" button over a
plain `ListView`) with a five-region layout — header, primary action, search, labeled recent
list, and five-icon navigation footer — and redesigns `ConversationListDelegate.qml`'s row presentation. The panel's own
`HnSurfaceFrame` root, its `surfaceRole`, and `WorkspaceWindow.qml`'s call-site frame overrides
(`chamferedCornersOverride`, `fillColor`, `borderColor`, `borderWidth`) are untouched (REQ-C-001);
only `Layout.preferredWidth` (220→320) and the window's `minimumWidth` (900→1000) change at the
call site.

The UI structure is QML-only and adds no C++ types, invokables, model roles, or CMake changes.
`ConversationListModel` changes only the display formatting and refresh cadence of its existing
`updatedAt` role so it exposes compact relative time. The `ChatViewModel` C++ surface (`conversationList`,
`activeConversationId`, `createConversation()`, `switchConversation()`, `renameConversation()`,
`deleteConversation()`, and the `conversationId`/`title`/`updatedAt` roles) remains unchanged.

Two small reusable QML components are introduced — `InlineIcon.qml` (a parametrized
`Shape`/`ShapePath`/`PathSvg` glyph, REQ-NF-001) and `SidebarIconButton.qml` (a hover/press-aware
icon button built on the `HnSurfaceFrame` + `HoverHandler` + `TapHandler` idiom already documented
in `theme-frames-usage.md`'s "Interactive control frame" example) — because both patterns repeat
at least four times in this redesign (twelve-plus icon usages; seven hover-icon-button usages:
row edit, row delete, and five footer buttons). Everything that appears exactly once (header,
"New chat" button, search field, section label, footer bar) stays inline in
`ConversationListPanel.qml`, consistent with this project's precedent of not extracting a
component until a real second/third usage exists (see the "rule of three" note in the Anthropic
provider adapter cycle memory).

`docs/mockups/main-window.png` is authoritative for visual layout except where the spec explicitly
overrides it. The footer is the deliberate exception: the mockup's profile card is omitted because
there is no profile use case, while its lower separator and five-icon navigation row are retained.

## Component structure

```text
qml/workspace/
├── ConversationListPanel.qml     (changed) — header, New chat, search, Recent label,
│                                   ListView, footer; owns searchText and the "/" Shortcut
├── ConversationListDelegate.qml  (changed) — HnSurfaceFrame root (was Rectangle); leading
│                                   icon, title+timestamp, hover-revealed edit/delete
│                                   SidebarIconButtons, selected accent bar, self-filtering
├── InlineIcon.qml                (new) — generic 16×16 Shape/ShapePath/PathSvg glyph
├── SidebarIconButton.qml         (new) — hover/press icon button (HnSurfaceFrame +
│                                   HoverHandler + TapHandler + InlineIcon)
└── WorkspaceWindow.qml           (changed) — Layout.preferredWidth 220→320,
                                    minimumWidth 900→1000, wires
                                    ConversationListPanel.onSettingsToggleRequested
```

`InlineIcon.qml` and `SidebarIconButton.qml` live under `qml/workspace/`, not `qml/shared/`,
because `qml/shared/` is reserved (per `CLAUDE.md`) for components used by both the workspace
window and the quick panel; the quick panel does not use this icon set. If a future cycle needs
the same glyphs in the quick panel, promote both files then — not preemptively.

Ownership/ containment after this change:

```text
ConversationListPanel (HnSurfaceFrame, surfaceRole unchanged)
└── ColumnLayout
    ├── Header row            — InlineIcon (hexagon) + InlineIcon (dot) + Text "HoloNight AI"
    ├── "New chat" Button     — InlineIcon (plus) + Text "New chat"
    ├── Search field          — InlineIcon (magnifier) + TextField + Text "/"
    ├── "Recent" label row    — InlineIcon (clock) + Text "Recent"
    ├── ListView (unchanged model binding: ChatViewModel.conversationList)
    │   └── delegate: ConversationListDelegate (HnSurfaceFrame)
    │       ├── InlineIcon (leading icon)
    │       ├── Text (title) + Text (updatedAt)
    │       ├── SidebarIconButton (edit)   — hover-revealed
    │       ├── SidebarIconButton (delete) — hover-revealed
    │       ├── inline rename TextField (unchanged state machine)
    │       └── inline delete-confirm row (unchanged state machine)
    ├── Rectangle (separator)
    └── Footer row
        ├── SidebarIconButton (layout)        — inert
        ├── SidebarIconButton (documentation) — inert
        ├── SidebarIconButton (code)          — inert
        ├── SidebarIconButton (gear)          → settingsToggleRequested()
        └── SidebarIconButton (help)          — inert
```

## Data flow

### Search filtering lifecycle

`ConversationListPanel` owns `property string searchText: ""`, bound to the search `TextField`'s
`text`. The `ListView`'s `model` stays `ChatViewModel.conversationList`, untouched — no JS array
rebuild, no `DelegateModel`, no proxy model (REQ-C-002). Each `ConversationListDelegate` filters
*itself*:

```qml
// ConversationListDelegate.qml
readonly property bool matchesFilter:
    root.filterText.length === 0
    || root.title.toLowerCase().includes(root.filterText.toLowerCase())

visible: root.matchesFilter
height: root.matchesFilter ? implicitHeight : 0
```

`filterText` is a required property the panel binds from its own `searchText` at the delegate
instantiation site (`filterText: panel.searchText`). Because `title` is itself a live role-bound
property (already flowing from `ConversationListModel` today) and `filterText` is a live property
too, `matchesFilter` is a normal reactive QML binding — no manual `Connections` to
`rowsInserted`/`dataChanged`/`modelReset`, no imperative recompute function, and no risk of it
going stale:

- **Typing/clearing in the search field** changes `panel.searchText` → every delegate's
  `matchesFilter` re-evaluates immediately (REQ-F-009).
- **Renaming a conversation** changes the `title` role → `dataChanged` updates the delegate's
  `title` required property (existing, unmodified mechanism) → `matchesFilter` re-evaluates.
- **Creating a conversation** inserts a new delegate; it evaluates `matchesFilter` against the
  *current* `searchText` on construction, and the New Chat handler additionally clears
  `searchText` to `""` so the freshly created (and now-active) conversation is always visible
  regardless of a stale filter (REQ-C-002's "reset on New Chat").
- **Deleting a conversation** destroys its delegate via the normal `ListView`/model row-removal
  path; nothing filter-specific is needed.

A filtered-out row must not just look empty, it must be *absent* (REQ-F-010's "not interactable").
`visible: false` disables input delivery and rendering for the item and its children, and
`height: 0` collapses its layout footprint, together satisfying "filtered-out conversations are
not interactable... absent from view."

**Spacing correctness for hidden rows.** `ListView.spacing` is applied uniformly between every
consecutive delegate in model order regardless of an individual delegate's `visible` value — a
`height: 0` filtered-out row would still leave a residual `spacing`-sized gap in the list, since
the list view's layout pass, not the delegate's own bounds, owns that gap. To keep filtering exact
(zero visual footprint, not "near zero"), this design moves the vertical rhythm *into* the
delegate itself: `ListView.spacing: 0`, and `ConversationListDelegate` reserves its own top margin
(`HoloniightPalette.controlPadding / 2`, matching today's `ListView.spacing` value) as part of its
own `implicitHeight`. A filtered delegate's `height: 0` then removes the row *and* its margin in
one step, and a visible list has no observable difference from today's spacing.

Resetting the filter is just clearing `searchText` — every `matchesFilter` binding flips back to
`true` and the untouched underlying model re-populates the full list, satisfying "closing/reopening
the panel... resets the filter" (there is no filter state to persist in the first place — it is a
plain, non-persisted QML property, initialized to `""` on every fresh `ConversationListPanel`
instantiation).

### Row activation

Unchanged mechanism: `ConversationListDelegate`'s existing `MouseArea` (`enabled: !editing &&
!confirmingDelete`) calls `root.activateRequested()` on click; `ConversationListPanel`'s existing
`onActivateRequested: ChatViewModel.switchConversation(delegate.conversationId)` is unchanged.
Filtering does not gate this — a visible, filtered-in row's `MouseArea` behaves exactly as before.

### Rename / delete

Unchanged state machine and unchanged signals (`renameRequested(string)`, `deleteConfirmed()`).
The only change is *how* rename/delete are entered: today's always-visible "Edit"/"Delete"
`Button`s become two `SidebarIconButton`s whose visibility is gated by row hover (see Key Decision
3). Double-click-to-rename on the row itself is unchanged.

### Relative timestamp lifecycle

`ConversationListModel::UpdatedAtRole` continues to expose a pre-formatted `QString`, but now
formats the stored UTC `QDateTime` relative to `QDateTime::currentDateTimeUtc()`:

- less than 60 seconds, including future clock skew: `now`;
- less than 60 minutes: `Nm ago`;
- less than 24 hours: `Nh ago`;
- otherwise: `Nd ago`.

A one-minute `QTimer` owned by the model emits `dataChanged` for `UpdatedAtRole` across the
current row range. This keeps visible labels current without adding a role, invokable, proxy
model, persistence field, or QML parsing of locale-sensitive date strings.

### Settings toggle from the footer

`ConversationListPanel` cannot reach `settingsWindow` — that instance is declared as a sibling in
`WorkspaceWindow.qml`, not a child of the panel. Rather than reach across the tree or duplicate
window state, the panel gains one new signal:

```qml
// ConversationListPanel.qml
signal settingsToggleRequested()
```

emitted by the footer's gear `SidebarIconButton.onClicked`. `WorkspaceWindow.qml` wires it exactly
like the existing center-panel gear `Button`:

```qml
ConversationListPanel {
    // ...existing Layout/frame properties...
    onSettingsToggleRequested: settingsWindow.visible = !settingsWindow.visible
}
```

Both call sites toggle the same `settingsWindow.visible` boolean with the same `!` expression,
satisfying REQ-F-019's "identical behavior... common visibility-toggle handler" acceptance
criterion by inspection (two call sites, same expression, same target property — no shared
function was introduced because the expression is a one-line toggle and a named handler function
would be pure ceremony for a single reused line).

## Interfaces

### `ConversationListPanel.qml` (root `HnSurfaceFrame`, changed)

| Member | Kind | Notes |
| --- | --- | --- |
| `surfaceRole`, `fillColor` | existing properties | **unchanged** (REQ-C-001) |
| `searchText` | new internal `property string` | not exposed as `required`; default `""` |
| `settingsToggleRequested()` | new signal | emitted by the footer gear button; no arguments |

No other public property/signal changes. `Layout.preferredWidth` is set at the call site in
`WorkspaceWindow.qml`, not on the component itself (unchanged pattern).

### `ConversationListDelegate.qml` (root type changes `Rectangle` → `HnSurfaceFrame`)

| Member | Kind | Notes |
| --- | --- | --- |
| `conversationId`, `title`, `updatedAt` | `required property string` | **unchanged names/types** |
| `isActive` | `property bool` | **unchanged** |
| `filterText` | new `property string`, default `""` | bound by the panel to its `searchText` |
| `activateRequested()`, `renameRequested(string)`, `deleteConfirmed()` | signals | **unchanged** — no new signals for hover/filter behavior |
| `editing`, `confirmingDelete` | internal `property bool` | **unchanged**, still private state |
| `matchesFilter` | new `readonly property bool` | derived, see Data flow |
| `hovered` | new `readonly property bool`, bound to a `HoverHandler` | drives edit/delete reveal and the hover fill |

### `InlineIcon.qml` (new)

```qml
Shape {
    required property string svgPath   // path data authored in a 16×16 coordinate space
    property color iconColor: HoloniightPalette.textPrimary
    property real strokeWidth: 0       // 0 = filled glyph; >0 = stroked/outline glyph
    implicitWidth: 16
    implicitHeight: 16
}
```

No signals, no invokables. Callers needing a different rendered size apply `scale`/`width`+`height`
on the instance; path data itself always targets the 16×16 grid so every icon in the panel shares
one authoring convention (also satisfies REQ-F-013's "single consistent... icon" spirit by
construction — one sizing contract for every glyph in the file).

### `SidebarIconButton.qml` (new)

```qml
HnSurfaceFrame {
    required property string svgPath
    property color iconColor: HoloniightPalette.textSecondary
    property real iconStrokeWidth: 0
    signal clicked()
}
```

`surfaceRole: HnSurfaceRole.Control`. Fires `clicked()` unconditionally on tap; whether anything
listens is the call site's decision — this is how REQ-F-020's four inert navigation buttons are
expressed (the component detects and emits the tap; those footer instances attach no
`onClicked:`).

## Key decisions

### 1. File/component structure — two extractions, not per-icon components

Twelve-plus icon usages (logo hexagon, logo dot, plus, magnifier, clock, row folder, edit,
delete, and five footer icons) all need identical machinery: a `Shape` containing one `ShapePath`
with a `PathSvg`, colored via a palette token, per REQ-NF-001/002. Writing that out at every call
site would duplicate boilerplate and create independent chances to miss a palette-token
binding (the exact defect REQ-NF-002's acceptance criterion checks for). A single parametrized
`InlineIcon.qml` centralizes that risk to one file. This is a generic *rendering* abstraction (one
shape-drawing mechanism), not a semantic one — it does not know what a "gear" or "trash" icon is;
each call site supplies its own path data and color. That keeps it from becoming an "icon
registry" or asset-catalog abstraction, which would be over-engineering for this hand-picked
glyphs used in exactly one panel.

`SidebarIconButton.qml` is justified the same way: seven call sites (delegate edit/delete and five
footer icons) need the identical hover/press/tap/icon composition already
documented as the project's own idiom in `theme-frames-usage.md`'s "Interactive control frame"
example. Extracting it once, rather than pasting that `HoverHandler`/`TapHandler`/`HnSurfaceFrame`
block four times, is the same boilerplate-and-risk argument.

Everything else (header block, "New chat" button, search field, "Recent" label, separator, footer bar) has
exactly one call site each and stays inline in `ConversationListPanel.qml`. This matches this
codebase's established precedent (reconfirmed at three call sites in the Anthropic provider
adapter cycle) of not adding a component for a single usage.

### 2. Search/filter mechanism — self-filtering delegates, not a rebuilt array or `DelegateModel`

Three approaches were compared:

1. **Rebuild a JS array of plain row objects** on every keystroke and on model
   insert/remove/rename, feeding it as `ListView.model`. Rejected: requires reading roles off the
   raw `QAbstractListModel` via `model.data(index, roleInt)`, which means either hardcoding role
   integers in QML (fragile, couples to `ConversationListModel::Roles`' enum ordering) or wiring
   `Connections` to `rowsInserted`/`rowsRemoved`/`dataChanged`/`modelReset` to trigger an
   imperative recompute — more moving parts, and a real risk of missing one of those four signals
   and silently going stale under a live filter (exactly what REQ-F-009's "filtering is live"
   criterion would catch).
2. **`DelegateModel` with `filterOnGroup`.** This is the Qt-sanctioned "QML-only filtering"
   mechanism and is not a C++ proxy, so it would satisfy REQ-C-002. Rejected as unnecessary
   machinery here: it requires maintaining group membership (`inItems`/`inSelected` flags) per row
   imperatively in response to the same four model signals as option 1, for no benefit over option
   3 below, which needs none of that.
3. **Chosen: per-delegate self-filtering `visible`/`height` binding**, bound directly off the
   delegate's own already-live `title` role and a `filterText` property the panel feeds it. This
   needs zero manual model introspection and zero imperative recompute — it rides entirely on
   bindings QML already re-evaluates for free, and it is trivially correct for every REQ-C-002
   scenario (typing, clearing, rename, create, delete) because each of those is *already* a role or
   property change that a normal QML binding reacts to.

Its only real cost is the `ListView.spacing` interaction addressed in Data Flow (moving spacing
into the delegate's own height rather than the `ListView`'s inter-item gap).

### 3. Hover-reveal action buttons — `HoverHandler` composes with the existing `MouseArea`

`ConversationListDelegate` keeps its existing `MouseArea` (click → `activateRequested()`,
double-click → enter rename) unchanged. A separate `HoverHandler` (a passive pointer handler that
never grabs the event and therefore does not compete with the `MouseArea`'s exclusive grab) is
added purely to track `root.hovered`. The two `SidebarIconButton`s (edit, delete) bind
`visible: root.hovered && !root.editing && !root.confirmingDelete` — hidden at rest, revealed on
hover, and suppressed while the row is already mid-rename or mid-delete-confirm so they never
overlap the inline `TextField` or the Yes/No confirm row. No new signal is introduced: clicking
edit calls the same `root.editing = true; renameField.forceActiveFocus(); ...` block the
double-click handler already runs (both paths converge on the same three statements); clicking
delete sets `root.confirmingDelete = true`, identical to today's "Delete" `Button`. `Yes`/`No`
still call `deleteConfirmed()`/reset `confirmingDelete`, both unchanged existing signals/state.

### 4. Selected/hover row styling

`ConversationListDelegate`'s root becomes an `HnSurfaceFrame` (`surfaceRole:
HnSurfaceRole.Control`) instead of a `Rectangle`, both to move off the deprecated-adjacent
`radiusControl` compatibility alias the current code uses (REQ-NF-004 wants `HnSurfaceRole`-derived
geometry) and to match this project's own documented `HnSurfaceFrame` + `HoverHandler` +
`TapHandler` idiom.

```qml
fillColor: root.isActive ? HoloniightPalette.surfaceElevated
           : root.hovered ? HoloniightPalette.surfaceHover
           : "transparent"
borderWidth: 0
```

`"transparent"` for the resting state (not a hex literal — it is the existing pattern already used
by `ProviderListDelegate.qml`'s `background`) lets an idle row show the panel's own `surface` fill
underneath rather than painting an extra flat layer. `borderWidth: 0` explicitly suppresses
`HnSurfaceFrame`'s default `borderPassive` stroke, since rows were never bordered before and REQ-F
014/015 only ask for fill changes.

The selected-row accent bar is a plain `Rectangle`, anchored to the frame's left edge, `visible:
root.isActive`:

```qml
width: HoloniightPalette.focusBorderWidth   // = 2, already the palette's "2px" metric
color: HoloniightPalette.borderActive        // "current, selected, or active border" — exact match
```

Reusing `focusBorderWidth` (whose value happens to be exactly the required 2 px) avoids introducing
a new numeric literal for the accent bar's width, keeping REQ-NF-005 satisfied without inventing a
token. `borderActive` is chosen over `primary` for the accent color because its documented meaning
("current, selected, or active border") is the precise semantic this bar expresses, leaving
`primary` reserved for genuine primary-action fills (the "New chat" button, links) — this also
avoids using `primary` for two visually different things (a full button fill vs. a 2 px hairline)
under one token.

Title text stays `HoloniightPalette.textPrimary` and relative timestamp stays `HoloniightPalette.textMuted`
in *every* state (active, hovered, resting) — the prior `isActive ? onPrimary : textPrimary`
inversion is removed entirely, per REQ-F-015's explicit "title text remains textPrimary (not
inverted)" acceptance criterion.

### 5. "/" focus shortcut placement

A `Shortcut` is declared inside `ConversationListPanel.qml` (not globally in
`WorkspaceWindow.qml`, since only this panel needs it):

```qml
Shortcut {
    sequence: "/"
    enabled: !searchField.activeFocus
    onActivated: searchField.forceActiveFocus()
}
```

`enabled: !searchField.activeFocus` directly satisfies "does not fire when the search field
already has focus." For the second concern — not firing while typing "/" into the delegate's
inline rename `TextField` elsewhere in the panel — this relies on standard Qt shortcut-dispatch
behavior: a focused text-editing control (`TextField`/`TextInput`) consumes a plain, unmodified
printable-character key press as text *before* the ambient `Shortcut` system sees it, so a "/"
typed into an active rename field never reaches this `Shortcut` regardless of which row's rename
field currently has focus (the panel has no way to reference "whichever delegate is mid-rename"
by id, since that `TextField` is private to each `ConversationListDelegate` instance — it does not
need to, because the platform's own focused-widget-precedence handles it). This assumption is
called out again in Known Risks since it is standard Qt behavior, not something this design can
verify without running the app (per this project's convention of handing visual/interactive
verification to the user rather than self-testing headless GUI behavior).

### 6. Width / `minimumWidth` arithmetic

In `WorkspaceWindow.qml`:

- `ConversationListPanel { Layout.preferredWidth: 220 }` → `320` (REQ-F-001).
- `HnApplicationWindow.minimumWidth: 900` → `1000` (REQ-F-002).
- The right panel's `Layout.preferredWidth: 220` is **unchanged**.

Verifying REQ-F-002's arithmetic against the actual bindings already in the file:
`contentPadding: HoloniightPalette.controlPadding` (2 edges) + the `RowLayout`'s
`spacing: HoloniightPalette.controlPadding` (2 gaps, left↔center and center↔right) + the two side
panels:

```text
1000 − 2×6 (contentPadding) − 2×6 (RowLayout gaps) − 320 (left) − 220 (right) = 436
```

matching REQ-F-002's stated 436 px minimum center width exactly, using the default scheme's
`controlPadding = 6`. `320` and `1000` are the only two literals introduced; both are the panel's
and window's own outer-size contract explicitly mandated by number in REQ-F-001/002, not internal
spacing/padding governed by REQ-NF-005 (which applies to margins and gaps *within* the panel's
content, not to the two dimensions the requirements name directly).

### 7. Icon inventory

Every icon below is an `InlineIcon` instance with 16×16 path data and a semantic color:

| Icon | Usage | Mode | Color |
| --- | --- | --- | --- |
| Hexagon outline | Header logo (outer mark) | stroked (`strokeWidth: HoloniightPalette.borderWidth`) | `HoloniightPalette.accentCyan` |
| Dot | Header logo (inner mark), composed as a second `InlineIcon` layered over the hexagon | filled | `HoloniightPalette.primary` |
| Plus | "New chat" button | filled | `HoloniightPalette.onPrimary` (button fill is `primary`) |
| Magnifier | Search field, left side | stroked (`strokeWidth: HoloniightPalette.borderWidth`) | `HoloniightPalette.textMuted` |
| Clock | "Recent" section label | filled | `HoloniightPalette.textMuted` |
| Folder | Every conversation row, identical instance (REQ-F-013) | stroked | `HoloniightPalette.textMuted` |
| Edit pencil | Row hover action, inside `SidebarIconButton` | filled | `HoloniightPalette.textSecondary` |
| Trash | Row hover action, inside `SidebarIconButton` | filled | `HoloniightPalette.error` |
| Layout | Footer inert button | filled | `HoloniightPalette.textSecondary` |
| Documentation | Footer inert button | stroked | `HoloniightPalette.textSecondary` |
| Code braces | Footer inert button | stroked | `HoloniightPalette.textSecondary` |
| Gear | Footer settings button, inside `SidebarIconButton` | filled | `HoloniightPalette.textSecondary` |
| Help | Footer inert button | stroked | `HoloniightPalette.textSecondary` |

The logo mark composes two `InlineIcon` instances (hexagon + dot) rather than one dual-color
`Shape`, keeping `InlineIcon` single-color and simple (see Decision 1) instead of adding a
multi-path/multi-color variant for one usage.

The trash icon's `HoloniightPalette.error` color is a direct, sanctioned use of the token's
documented meaning ("destructive actions" — `theme-colors-usage.md`'s status table), distinct from
REQ-C-004's prohibition on *row-level* error/status styling: REQ-C-004 forbids conditioning a
row's appearance on conversation health/error state; it does not forbid a delete button looking
like a delete button. Every row still renders with the identical neutral icon, colors, and layout
regardless of any conversation property — only the *hover-revealed delete affordance* uses
`error`, and it does so unconditionally (present identically on every row), not as a per-row status
signal.

Exact path-data strings are an implementation-time detail (authoring 10 small SVGs), not a design
commitment; the table above fixes the mode (filled vs. stroked), color token, and coordinate-space
convention (16×16) each icon must use.

## Alternatives considered

- **One large per-icon `Shape` in each call site**, no shared component. Rejected — see Decision
  1; the near-identical blocks are well past this project's "rule of three."
- **A generic icon *registry* singleton** (e.g., `IconCatalog` exposing named path constants).
  Rejected as unnecessary indirection: nothing in this panel needs runtime icon lookup by name,
  and a registry would be a new piece of infrastructure for ten hand-picked, panel-local glyphs.
- **`DelegateModel`-based filtering.** Rejected — see Decision 2; more moving parts than
  self-filtering delegates for identical guarantees.
- **A rebuilt JS-array `ListView.model` for filtering.** Rejected — see Decision 2; requires raw
  role-index access or manual signal wiring that self-filtering avoids entirely.
- **`MouseArea` alone for hover detection** (`hoverEnabled: true`, track `containsMouse`).
  Rejected in favor of `HoverHandler`: the delegate already has a click-and-double-click
  `MouseArea`; adding hover tracking to the *same* `MouseArea` would tangle hover state with the
  click/double-click/rename-suppression logic already governing `enabled`. A separate passive
  `HoverHandler` keeps hover detection orthogonal to click handling, and matches the documented
  project idiom exactly.
- **Keep the delegate root as `Rectangle`, add `HnAppearance.roundedRadius()` for the corner.**
  Rejected — Decision 4 needs a real `HnSurfaceFrame` for the resolved `HnSurfaceRole.Control`
  radius (REQ-NF-004 wants the frame-role mechanism specifically, not a computed-radius
  `Rectangle` workaround) and gets the `HoverHandler`/`TapHandler` idiom pairing "for free" by
  using the same frame type as the new icon buttons.
- **Global `Shortcut` in `WorkspaceWindow.qml` for "/".** Rejected — the shortcut only concerns the
  sidebar's own search field; scoping it inside `ConversationListPanel.qml` keeps the concern
  local and avoids `WorkspaceWindow.qml` needing to know about `ConversationListPanel`'s internal
  `searchField` id.
- **Vertical header instead of horizontal logo-and-title row.** Rejected because the authoritative
  mockup uses a horizontal header.
- **`primary` for the selected-row accent bar** (spec's first suggested option). Rejected in favor
  of `borderActive` — see Decision 4; `borderActive`'s documented meaning is a more precise match
  and avoids double-using `primary` for two visually distinct treatments in the same panel.

## Known risks

- **"/" shortcut and focused-text-widget precedence** (Decision 5) is standard Qt behavior but is
  not verified by this design or by an automated test — it needs manual confirmation during
  implementation that typing "/" inside an active rename `TextField` types a literal "/" rather
  than stealing focus. This mirrors the spec's own "Known Risks" note about "/" scoping.
- **Delegate-owned spacing instead of `ListView.spacing`** (Data Flow) is a deliberate deviation
  from the current code's `ListView.spacing: controlPadding / 2`. If a future change reintroduces
  `ListView.spacing` without removing the delegate's own top margin, rows will double-space; this
  needs a code-review note at implementation time, not just a runtime check.
- **`HnSurfaceFrame` delegate root's `borderWidth: 0` override** suppresses the frame's default
  `borderPassive` stroke; if a later `HnSurfaceFrame` default changes, someone must remember this
  explicit override exists and is intentional (rows have never been bordered).
- **No visual verification is performed by this design or its implementation agent** (project
  convention) — REQ-F-003 through REQ-F-020's "visually inspect..." acceptance criteria and
  REQ-NF-001–005's "QML review finds..." criteria need a human checklist pass; `task build` and
  `task qml-lint` (REQ-NF-006) are the only automated gates this cycle can self-verify.
- **`InlineIcon`/`SidebarIconButton` placement under `qml/workspace/`** is correct for today's
  scope but will need relocation to `qml/shared/` (and a `CLAUDE.md` update) the first time the
  quick panel needs the same glyphs — flagged so that future move isn't mistaken for scope creep
  in *this* cycle.
- **Hand-authored SVG path strings** are an implementation-time craft risk (visually
  inconsistent stroke weights or off-grid coordinates) independent of this design's structural
  choices; the 16×16 coordinate-space convention (Decision 7 / Interfaces) bounds but does not
  eliminate that risk.
