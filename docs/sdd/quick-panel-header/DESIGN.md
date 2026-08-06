# DESIGN: Quick panel header

**Spec:** `docs/sdd/quick-panel-header/SPEC.md`
**Status:** Draft
**Date:** 2026-07-28

## Overview

This cycle adds `qml/quickpanel/QuickPanelHeader.qml`, a new component that replaces the bare
`RowLayout { Item{Layout.fillWidth:true}; Button{text:"⤢"} }` top row in `QuickPanel.qml` with a
branded header: app icon + "Quick chat" title on the left, four icon buttons on the right (recent
chats dropdown, switch-to-workspace, pin placeholder, close panel). It is QML-only (REQ-C-001):
no `holonight_application`/`holonight_persistence` changes, no new Q_PROPERTY/Q_INVOKABLE members.
Every backend call the header makes — `ChatViewModel.createConversation()`,
`ChatViewModel.switchConversation(id)`, `ChatApplication.ShowWorkspace()`,
`ChatApplication.ClosePanel(bool)` — already exists and is invoked unmodified (REQ-C-002).

The dropdown is new ground for this codebase: no `Menu`/`Popup` component exists anywhere under
`qml/` today (confirmed by grep; the only `Popup` usages are `QtQuick.Controls.Basic`'s built-in
`ComboBox.popup` internals in `HeaderComboBox.qml`, not a hand-rolled one). This design builds it
from a bare `QtQuick.Controls.Basic Popup` styled with `HnSurfaceFrame`/`HoloniightPalette`,
following the project's established `HnSurfaceFrame` + `HoverHandler` + `TapHandler` idiom rather
than inventing a fourth pattern.

`ChatHeader.qml` (provider/model picker) and everything under `qml/workspace/` are untouched
(REQ-C-004, REQ-C-005) — both stay out of this design's file list entirely.

## Component structure

```text
qml/quickpanel/
├── QuickPanel.qml           (changed) — top RowLayout replaced by QuickPanelHeader; no other change
└── QuickPanelHeader.qml     (new) — the header row + dropdown popup + its delegate
```

No new file for the dropdown delegate. REQ-F-014 requires a delegate distinct from
`ConversationListDelegate.qml`, but does not require a distinct *file* — the delegate is small
(title `Text` + active-highlight fill + accent bar, ~15 lines) and has exactly one call site,
matching this project's "rule of three" precedent (confirmed at three-plus cycles: extract only on
a real second/third usage). It lives as an inline `delegate:` on the `ListView` inside
`QuickPanelHeader.qml`. `InlineIcon.qml` (`qml/workspace/InlineIcon.qml`) is reused as-is for the
dropdown's "New chat" glyph and is already visible from `qml/quickpanel/` — all `qml/**/*.qml`
files compile into the single `HolonightChat` QML module (confirmed via
`apps/chat/CMakeLists.txt`'s `file(GLOB_RECURSE HOLONIGHT_CHAT_QML_FILES qml/**/*.qml)`), so
module-internal types are visible across the `shared/`/`workspace/`/`quickpanel/` split without
per-directory imports; `ChatHeader.qml` already relies on the same fact for `InlineIcon`.

Ownership/containment:

```text
QuickPanel.qml
└── HnSurfaceFrame (unchanged) → ColumnLayout (unchanged)
    ├── QuickPanelHeader              — NEW, replaces the old RowLayout+Button row
    ├── ChatHeader { compact: true }  — unchanged
    └── ChatPanel                     — unchanged

QuickPanelHeader (Item or RowLayout root, anchors.fill: parent semantics per REQ-F-001)
├── RowLayout (fills header)
│   ├── Left group
│   │   ├── HnIcon (holonight-ai.svg, controlHeight, tinted: false)
│   │   └── Text ("Quick chat", bold, textPrimary)
│   ├── Item { Layout.fillWidth: true }           — pushes right group to the edge
│   └── Right group (RowLayout, 4 buttons, in spec order)
│       ├── HeaderAction  → dropdownTrigger.onClicked: dropdownPopup.open()/toggle
│       ├── HeaderAction  → onClicked: ChatApplication.ShowWorkspace()
│       ├── HeaderAction  (enabled: false, pin) — no onClicked handler at all
│       └── HeaderAction  → onClicked: ChatApplication.ClosePanel(false)
└── Popup { id: dropdownPopup }                    — anchored under the dropdown trigger button
    └── contentItem: ColumnLayout
        ├── ItemDelegate/inline row — "New chat" → ChatViewModel.createConversation(); dropdownPopup.close()
        ├── Rectangle (1px separator) — visible: ChatViewModel.conversationList.rowCount() > 0 (or similar)
        └── ListView
            model: ChatViewModel.conversationList
            (10-item cap applied in the view, see Data flow)
            delegate: Item { ... }  — inline, new, simplified row (title + active highlight)
```

## Data flow

### Recent-conversations list: direct model binding with a view-level cap, no proxy model

`ConversationListModel` is `QML_UNCREATABLE` ("Instantiated only by ChatViewModel; do not
construct from QML") and already exposes `IdRole`/`TitleRole`/`UpdatedAtRole` pre-sorted
`updated_at DESC` by the repository's SQL (per its header comment and `ConversationListPanel.qml`'s
own unmodified `ListView { model: ChatViewModel.conversationList }` binding). REQ-F-008's "up to 10
most recent, no re-sorting required" is satisfied without any new C++ by binding the dropdown's
`ListView.model` directly to `ChatViewModel.conversationList` and applying the 10-item cap in QML,
one of two ways:

- `Repeater`/`ListView` with `model: ChatViewModel.conversationList` and each delegate gated
  `visible: index < 10; height: index < 10 ? implicitHeight : 0` (self-limiting delegate, same
  idiom as the left-sidebar-panel cycle's self-filtering delegate); or
- a plain `ListView` with `interactive: contentHeight > implicitHeight` and no cap at all, relying
  on `implicitHeight: Math.min(contentHeight, controlHeight * 10)` to visually clip beyond 10 rows
  while leaving all rows instantiated.

**Chosen:** the first (delegate self-limiting via `index < 10`), because REQ-F-008's acceptance
criterion is explicit about *row count* ("displays exactly 10 conversation rows" when 15 exist),
not just visible height — instantiating all N delegates and only clipping visually would leave
extra delegates alive (wasted work, and a risk that scrolling/clip edge cases expose the 11th+ row
under REQ-F-008's own AC). Capping via `visible`/`height` on the delegate mirrors the exact
`matchesFilter` pattern `ConversationListDelegate.qml` already uses for filtering, so it introduces
no new technique to the codebase.

`QuickPanelHeaderDropdownDelegate`'s inline `Item` binds `required property int index` (from
`ListView`) to gate itself; `ChatViewModel.conversationList`'s existing "already sorted DESC" order
means "first 10" is simply "index 0–9," no client-side re-sort needed.

### Why no proxy model

A `QSortFilterProxyModel`-style wrapper (or a small new `RecentConversationsModel` C++ class) was
considered and rejected: REQ-C-001 forbids new C++ entirely for this cycle, and even absent that
constraint, a proxy would duplicate what a one-line `index < 10` binding already achieves. The
"rule of three" and REQ-C-002 ("pure backend reuse") both push toward the thinnest possible QML
layer over the existing model.

### Active-conversation highlight refresh (REQ-F-010, REQ-F-015)

Each dropdown row binds `isActive: model.idRole === ChatViewModel.activeConversationId` (a plain,
live QML binding — role property and singleton property are both already reactive). When
`ChatViewModel.activeConversationIdChanged` fires after `switchConversation()`, every instantiated
row's `isActive` binding re-evaluates automatically; no `Connections` block is needed for this part
(contrast `ChatHeader.qml`'s `Connections { target: ChatViewModel }`, which exists there only
because `syncProviderSelection()`/`syncModelSelection()` are imperative index-lookup *functions*,
not declarative bindings — the dropdown highlight has no such imperative step).

`conversationList`'s own `rowsInserted`/`dataChanged`/`modelReset` signals (already emitted by
`ConversationListModel::setAll`/`upsertToFront`/`touchToFront`/`removeById`) drive the `ListView`'s
normal re-instantiation — REQ-F-015 ("refresh dynamically... without closing and reopening") is
satisfied for free by binding directly to the live model rather than snapshotting it into a JS
array on open.

### Dropdown open/close state

`Popup`'s own `visible`/`opened` property *is* the state — no separate `property bool
dropdownOpen` is introduced. The trigger button's `onClicked` toggles it
(`dropdownPopup.visible ? dropdownPopup.close() : dropdownPopup.open()`); "New chat" and each
conversation row call `dropdownPopup.close()` after their respective action (REQ-F-006, REQ-F-009).

## Interfaces

### `QuickPanelHeader.qml` (new)

No public signals or invokables — matching `ChatHeader.qml`'s pattern of *some* direct singleton
calls (its provider/model combos) alongside signals only where the parent must react
(`providerSettingsRequested`, `collapseRequested`). Nothing in this header needs parent
involvement: all four buttons and the dropdown's two action rows terminate directly in
`ChatViewModel`/`ChatApplication` singleton calls, so no signal is emitted upward.

| Member | Kind | Notes |
| --- | --- | --- |
| *(none)* | — | pure leaf component; `QuickPanel.qml` instantiates it with no property bindings, no signal handlers |

Internal (non-public) state: `property alias dropdownVisible: dropdownPopup.visible` is not even
needed externally — kept private.

### Dropdown delegate (inline, in `QuickPanelHeader.qml`)

```qml
// inline delegate inside the ListView, not a separate file (see Component structure)
Item {
    id: row
    required property string idRole
    required property string titleRole
    required property int index
    visible: row.index < 10
    height: row.visible ? HoloniightPalette.controlHeight : 0
    readonly property bool isActive: row.idRole === ChatViewModel.activeConversationId
    // fillColor: isActive ? surfaceElevated : hovered ? surfaceHover : transparent
    // left accent Rectangle, visible: isActive, color: borderActive
    // Text { text: row.titleRole }
    // TapHandler → ChatViewModel.switchConversation(row.idRole); dropdownPopup.close()
}
```

No `renameRequested`/`deleteConfirmed` signals — REQ-F-014 explicitly excludes rename/delete
affordances.

### `QuickPanel.qml` (changed)

No new properties or signals. The only edit is replacing the top `RowLayout` block with
`QuickPanelHeader { Layout.fillWidth: true }`.

## Icon button reuse decision

**Chosen: `HeaderAction.qml`, not `SidebarIconButton.qml`.**

Both exist today as near-identical hover/press icon buttons:

- `HeaderAction.qml` (`qml/shared/`) — a `Control`-based component with `enabled`, `hoverEnabled`,
  `Keys.onSpacePressed`/`Keys.onReturnPressed` (keyboard activation), `focusPolicy: Qt.StrongFocus`,
  and a `TapHandler` gated `enabled: root.enabled`. Already used by `ChatHeader.qml`, the sibling
  component this header sits directly above/beside in the same window.
- `SidebarIconButton.qml` (`qml/workspace/`) — an `HnSurfaceFrame`-based component, no `enabled`
  property, no keyboard handling, `TapHandler` always enabled. Used by `ConversationListPanel.qml`
  and `ConversationListDelegate.qml`, both strictly workspace-only per `CLAUDE.md`'s directory
  convention ("`qml/shared/` — components used by both the workspace window and the quick panel").

Three things decide it in `HeaderAction.qml`'s favor:

1. **Location matches the convention already documented in `CLAUDE.md`.** `qml/shared/` is for
   components used by both the workspace window and the quick panel; `qml/workspace/` is
   workspace-only by the left-sidebar-panel cycle's own explicit note ("If a future cycle needs the
   same glyphs in the quick panel, promote both files then — not preemptively"). `QuickPanelHeader`
   lives in `qml/quickpanel/`, so pulling from `qml/workspace/` would be exactly the cross-boundary
   reuse that convention warns against; `HeaderAction` has no such boundary problem.
2. **REQ-F-017/REQ-F-019 need a real `enabled: false` state with no-op click semantics.**
   `HeaderAction` already threads `enabled` through both its `TapHandler` (`enabled: root.enabled`)
   and its Control base (which auto-suppresses hover/keyboard activation when disabled) — REQ-F-019
   ("no signal emission... even if clickable by accident") is satisfied by the component's existing
   disabled-state machinery, not by a new one this design would have to invent on top of
   `SidebarIconButton`, which has no `enabled` property today.
3. **Sibling consistency.** `ChatHeader.qml`, which sits directly below this header in the same
   `ColumnLayout` and same window, already uses `HeaderAction` for its two action buttons (provider
   settings gear, collapse chevron). Using the same component for the header immediately above it
   keeps focus order, hover styling, and keyboard activation (`Space`/`Return`) uniform across both
   rows of the quick panel's top of the window — a real visual/behavioral seam would appear if the
   panel used one button style two pixels above another button style two pixels below.

## Dropdown/popup design

### Primitive choice: `QtQuick.Controls.Basic Popup`, not `Menu`, not a custom `Item` overlay

Three options:

1. **`Menu`/`MenuItem`.** Qt's semantic menu component. Rejected: `MenuItem` delegates are styled
   through the platform/style's `MenuItem.qml`, which this project would have to override anyway to
   get `HoloniightPalette` colors and the `ConversationListDelegate`-style active-highlight bar
   (REQ-F-010) — at that point `Menu` buys nothing over a plain `Popup` except a menu-specific
   keyboard-navigation/submenu model this dropdown does not need (no submenus, no
   checkable/exclusive items). It also couples the dismissal semantics to `Menu`'s own
   `closePolicy`/`cascade` behavior, which is one more API surface to verify against REQ-F-011/012
   than a bare `Popup`.
2. **Custom `Item` + `MouseArea` overlay**, manually parented near the window root, with a
   full-window-covering invisible `MouseArea` behind it to catch outside clicks and manual
   `Keys.onEscapePressed` wiring. Rejected: this reimplements exactly what `Popup`'s built-in
   `closePolicy` already provides (`Popup.CloseOnEscape | Popup.CloseOnPressOutside`), with more
   code and more chances to get z-ordering, focus-scope, or event-acceptance wrong — especially
   risky inside a `wlr-layer-shell` surface (see Known risks) where a hand-rolled full-window
   overlay would need to reason about the layer surface's own geometry, not a normal window's.
3. **Chosen: `Popup`.** `QtQuick.Controls.Basic Popup` (already imported in this directory —
   `QuickPanel.qml` imports `QtQuick.Controls.Basic`) gives dismissal, modality, and positioning for
   free via documented properties, and is exactly the primitive `HeaderComboBox.qml` already
   customizes (`popup: Popup { ... contentItem: ListView { ... } background: Rectangle { ... } }`)
   — this design's `dropdownPopup` mirrors that existing in-codebase customization pattern (styled
   `contentItem`/`background`, `HoloniightPalette` colors) rather than introducing a new one.

### Anchoring / positioning

```qml
Popup {
    id: dropdownPopup
    y: dropdownTrigger.height + HoloniightPalette.controlPadding / 2
    x: dropdownTrigger.x
    width: Math.max(220, dropdownTrigger.width * 4)
    // parent: the right-side button row's Item, so x/y are relative to the trigger's row
    padding: 1
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: HnSurfaceFrame {
        surfaceRole: HnSurfaceRole.Menu
        borderColor: HoloniightPalette.borderPassive
    }
}
```

`HnSurfaceRole.Menu` (from `HnShapeTypes`'s `HnSurfaceRole` enum, alongside `Window`/`Panel`/
`Popup`/`Card`/`Control`/etc.) is the semantically exact role for this element — it already exists
in `holonight-qt`'s theme vocabulary and has not yet been used anywhere in this codebase, but its
existence means no new role/token is invented, only a first real consumer of one that was always
meant for this purpose. Its fill is therefore role-derived (`surfaceRaised`) rather than overridden
with a base-surface palette color.

### Dismissal — outside click (REQ-F-011)

`closePolicy: Popup.CloseOnPressOutside` (a built-in `Popup` flag) closes the popup on any press
outside its bounds — inside the quick panel, on another header button, or (because the quick panel
surface itself is the outermost `Item`) anywhere else the layer-shell surface receives input. This
requires no custom `MouseArea`/hit-testing; it is `Popup`'s documented behavior, matching REQ-F-011
exactly ("clicks outside the dropdown's bounds... within the quick panel or elsewhere on screen").

### Dismissal — Escape without leaking to `QuickPanel.qml`'s root handler (REQ-F-012)

This is the one requirement needing explicit reasoning about event order, since `QuickPanel.qml`'s
root `Item` already has:

```qml
Item {
    id: root
    Keys.onEscapePressed: ChatApplication.ClosePanel(true)
    ...
}
```

Two complementary mechanisms make the dropdown's Escape win:

1. **`Popup { focus: true }`** gives the popup's contents Qt Quick's active focus scope while open.
   Qt Quick key events are dispatched to the item with active focus first and only *bubble* upward
   through ancestor `Keys` handlers if the focused item's own `Keys` handling does not accept the
   event (or if nothing in the focused scope handles it at all). `Popup`'s built-in
   `closePolicy: Popup.CloseOnEscape` consumes the Escape key press internally as part of closing
   itself — the event is handled at the popup's focus scope and never reaches `QuickPanel.qml`'s
   root `Item`, because the root's own `Keys.onEscapePressed` only fires when *the root itself* (or
   an item that does not accept the event) has focus, and while the popup is open, focus belongs to
   the popup's scope, not the root.
2. **Belt-and-suspenders guard**, in case `Popup`'s internal consumption ever proves incomplete
   under this project's Qt version or under layer-shell-surface-specific focus routing (an unknown
   flagged in Known risks): `QuickPanel.qml`'s existing handler gains an explicit guard,

   ```qml
   Keys.onEscapePressed: {
       if (!quickPanelHeader.dropdownOpen)
           ChatApplication.ClosePanel(true)
   }
   ```

   requiring `QuickPanelHeader.qml` to expose one new tiny read-only surface,
   `readonly property alias dropdownOpen: dropdownPopup.visible`, purely for this guard (the one
   exception to "no public interface" in the Interfaces section above — added specifically to make
   REQ-F-012 verifiable by construction rather than by trusting undocumented focus-routing behavior
   inside a non-standard surface type). This is cheap, explicit, and turns an "unverified Qt
   internals" risk into a plain boolean condition anyone reading `QuickPanel.qml` can see is
   correct.

Both mechanisms together satisfy REQ-F-012's AC set: Escape closes the dropdown, the quick panel
stays open, and `ChatApplication.ClosePanel()` is not invoked while the dropdown is open.

### Empty-state handling (REQ-F-013)

With zero conversations, `ChatViewModel.conversationList.rowCount() === 0`. The separator
`Rectangle` binds `visible: ChatViewModel.conversationList !== null` (always true once
`ChatViewModel` exists) — REQ-F-013 explicitly allows *either* hiding the separator *or* showing it
with nothing below, so this design takes the simpler of the two allowed behaviors: always show the
separator beneath "New chat," and let the empty `ListView` render zero delegates, contributing zero
height. No "No conversations" placeholder `Text` is added (explicitly forbidden by REQ-F-013's AC).
This requires no conditional logic at all — an empty model driving an empty `ListView` is Qt Quick's
default, crash-free behavior; nothing needs to be written to prevent breakage.

### Delegate design (REQ-F-014, REQ-F-010)

Simplified relative to `ConversationListDelegate.qml`: title `Text` (elided, `textPrimary`), the
`fillColor`/left-accent-`Rectangle` highlight pair copied in *reduced* form (fill + one `Rectangle`,
no `HoverHandler`-driven reveal buttons, no `MouseArea`-gated rename/delete state machine, no inline
`TextField`, no confirm-delete row). Concretely, only these two visual elements are reused from
`ConversationListDelegate.qml`'s pattern:

```qml
fillColor: row.isActive ? HoloniightPalette.surfaceElevated : "transparent"
// left accent bar:
Rectangle {
    anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
    width: HoloniightPalette.focusBorderWidth
    color: HoloniightPalette.borderActive
    visible: row.isActive
}
```

everything else in `ConversationListDelegate.qml` (editing/confirmingDelete state, rename
`TextField`, two `SidebarIconButton`s, `updatedAt` display, filter matching) is intentionally
omitted, satisfying REQ-F-014's "shall NOT include rename/delete affordances, context menus, or
drag-and-drop" and "does not reference or extend `ConversationListDelegate.qml`."

## Key decisions with rationale

1. **No C++ changes (REQ-C-001).** Every data point the header needs — recent conversations,
   active id, panel/workspace control — is already exposed by `ChatViewModel`/`ChatApplication`.
   Confirmed by reading `chat_view_model.h` (`conversationList`, `activeConversationId`,
   `createConversation()`, `switchConversation(QString)` are all already `Q_PROPERTY`/`Q_INVOKABLE`)
   and `ChatApplication.h`/`.cpp` (`ShowWorkspace()` and `ClosePanel(bool)` already implement exactly
   the close-without-workspace vs. close-and-show-workspace distinction REQ-F-016/REQ-F-020 need —
   `ClosePanel(false)` for the X button, `ClosePanel(true)` remains the existing Escape path,
   unchanged).
2. **10-item slicing happens in QML (delegate `index < 10` gate), not a new proxy model** — see
   Data flow. REQ-C-001 forbids new C++ outright for this cycle, and even without that constraint a
   one-line binding is proportionate to the requirement; a proxy model would be new infrastructure
   for a fixed, small, never-reused cap.
3. **Pin button uses `enabled: false`, not opacity-only.** REQ-F-017 offers three equally valid
   options (`enabled: false` / reduced opacity / desaturated color); `enabled: false` is chosen
   because it is the only one of the three that *also* satisfies REQ-F-019 ("no signal emission...
   even if clickable by accident") without extra guard code — `HeaderAction`'s `TapHandler.enabled:
   root.enabled` and its `Keys.onSpacePressed`/`onReturnPressed` handlers are both already gated by
   the Control base's `enabled` semantics, so disabling the control is a true behavioral no-op, not
   just a visual one. Opacity-only would still require a separate `if (!root.enabled) return` guard
   in `onClicked` to satisfy REQ-F-019 — redundant with what `enabled: false` gives for free.
4. **`Popup` over `Menu`/custom overlay** — see Dropdown/popup design; the deciding factor is
   `closePolicy`'s built-in outside-click/Escape handling matching REQ-F-011/012 with no bespoke
   event plumbing, and precedent already set by `HeaderComboBox.qml`'s customized `Popup`.
5. **Dropdown delegate stays inline, not a new file** — one call site, ~15 lines, matches this
   project's repeatedly-applied "rule of three" (do not extract until a second/third usage exists).
6. **`HeaderAction.qml` over `SidebarIconButton.qml`** — see Icon button reuse decision; directory
   convention (`qml/shared/` vs. workspace-only `qml/workspace/`), built-in `enabled` semantics, and
   sibling consistency with `ChatHeader.qml` all point the same direction.
7. **`readonly property alias dropdownOpen`** is the one piece of public surface added to
   `QuickPanelHeader.qml` beyond "none," justified narrowly as a defensive guard for REQ-F-012 (see
   Dismissal — Escape) rather than a general-purpose API.

## Alternatives considered

**Dropdown primitive:** `Menu`/`MenuItem` (rejected — styling override cost with no benefit over
`Popup`, unneeded submenu/checkable machinery); custom `Item` + full-surface `MouseArea` overlay
(rejected — reimplements `Popup.closePolicy` with more code and more risk inside an already
non-standard layer-shell surface); **chosen:** `QtQuick.Controls.Basic Popup`.

**Recent-list sourcing:** new C++ `RecentConversationsModel`/`QSortFilterProxyModel` wrapper
(rejected outright by REQ-C-001, and redundant even ignoring that constraint); un-capped `ListView`
relying only on visual clipping via `implicitHeight: Math.min(...)` (rejected — leaves 11th+ row
delegates alive, contradicting REQ-F-008's exact-row-count AC); **chosen:** direct
`ChatViewModel.conversationList` binding with a delegate-level `index < 10` visibility/height gate.

**Dropdown delegate:** extend/reuse `ConversationListDelegate.qml` with a "simple mode" flag
(rejected — REQ-F-014 explicitly forbids referencing or extending it, and a mode flag would leave
dead rename/delete code paths reachable in the simplified context); a new standalone
`QuickPanelHeaderDropdownDelegate.qml` file (considered and valid per the spec's own suggested
naming, but rejected in favor of inlining — see Key decision 5, "rule of three"); **chosen:** inline
delegate inside `QuickPanelHeader.qml`'s `ListView`.

**Icon button component:** `SidebarIconButton.qml` (rejected — workspace-only per `CLAUDE.md`
convention, no `enabled` property, would need REQ-F-019's no-op guard hand-written); a brand-new
third icon-button component tailored to this header (rejected — the task brief itself requires
picking one of the two existing patterns or justifying a new one, and neither existing pattern has
a real gap here, so introducing a third would violate this project's demonstrated aversion to
parallel near-duplicate components, as documented across nearly every prior SDD cycle's Key
Decisions); **chosen:** `HeaderAction.qml`.

**Escape-priority mechanism:** rely solely on `Popup`'s built-in focus-scope event consumption with
no guard in `QuickPanel.qml` (rejected as too speculative given the layer-shell-surface unknown
flagged below — a design should not assert confidence it does not have about undocumented focus
routing under `wlr-layer-shell`); rely solely on an explicit `dropdownOpen` guard and disable
`Popup.CloseOnEscape` entirely, doing all Escape handling by hand (rejected — throws away `Popup`'s
correct, tested default behavior for no benefit, and duplicates logic in two places); **chosen:**
both together (`Popup`'s own `CloseOnEscape` as the primary mechanism, `dropdownOpen` guard on
`QuickPanel.qml`'s handler as a cheap, verifiable belt-and-suspenders backstop).

## Known risks

- **Popup z-ordering inside a `wlr-layer-shell` surface is a genuine open unknown, not something
  this design can assert confidence about.** `QuickPanel.qml`'s root `HnSurfaceFrame` renders
  inside a Wayland layer-shell popup surface (`holonight_platform::PanelSurface`), not a normal
  `QQuickWindow`. `Popup` in Qt Quick Controls normally reparents/raises itself within the same
  window's scene graph, which should work identically inside a layer-shell surface's `QQuickWindow`
  — but this project's own memory notes (quick-panel-window cycle) flag that this second window
  needed C++ instantiation specifically *because* the spec pinned Wayland-specific behavior, and
  that `HnSurfaceFrame` is an `Item`, not a `Window`. There is no existing `Popup` usage anywhere in
  this codebase running inside a layer-shell surface to confirm against (`HeaderComboBox.qml`'s
  `Popup` only runs inside the normal workspace window today). This needs manual verification during
  implementation (does the popup render above `ChatHeader`/`ChatPanel` correctly, and does it stay
  within the layer-shell surface's own bounds rather than attempting to exceed them) — flagged here
  explicitly rather than assumed to be fine.
- **Removing the old "⤢" `Button` row is safe, but only because nothing depends on its exact
  structure.** Confirmed by reading `QuickPanel.qml`: the old row's only behavior
  (`onClicked: ChatApplication.ShowWorkspace()`) is preserved verbatim as the new header's second
  right-side button; `ChatHeader.qml`'s `collapseRequested` signal is unrelated (it is
  `ChatHeader`'s own internal chevron, not this row) and is untouched per REQ-C-004. No other file
  references the old row's `Button` by id or structure (it was anonymous/inline), so this removal
  carries no hidden coupling.
- **`Escape`-priority reasoning (Dismissal section) is standard Qt Quick focus-scope behavior but
  is not verified by this design or by an automated test** — per this project's established
  convention (no self-performed visual/interactive GUI verification), REQ-F-012's AC needs a manual
  check during implementation that pressing Escape with the dropdown open does not also trigger
  `ChatApplication.ClosePanel(true)` in the same keystroke.
- **`qmllint`/theming pitfalls specific to this codebase:** REQ-NF-001 (no hardcoded hex/`rgb()`)
  and REQ-NF-002 (`pragma ComponentBehavior: Bound` as the first non-comment line) are easy to
  violate accidentally when hand-writing a new `Popup`'s `background`/`contentItem` — every color in
  `dropdownPopup`'s background, separator `Rectangle`, and delegate fill must trace to a
  `HoloniightPalette` token (verified against the pattern already used in `HeaderComboBox.qml`'s own
  `Popup` customization, which does the same thing correctly today). `task qml-lint` is the
  automated gate; it does not, however, check *semantic* palette-token correctness (e.g., using
  `surfaceRaised` where `surfaceElevated` was intended) — that remains a manual review item.
- **No visual verification is performed by this design or its implementation agent** (project
  convention, reconfirmed across every prior cycle's memory) — all of REQ-F's "visually appears,"
  "highlight is visible," and "dropdown renders correctly" acceptance criteria need a human
  checklist pass; `task build` and `task qml-lint` are the only automated gates this cycle can
  self-verify before handing off.
- **`ConversationListModel::rowCount()` access from QML for the empty-state check** (Dropdown
  design, REQ-F-013) relies on `ChatViewModel.conversationList` exposing the standard
  `QAbstractListModel` `count`/`rowCount` surface to QML, which Qt provides automatically for any
  `QAbstractListModel`-derived type exposed as a `Q_PROPERTY` — not something `ConversationListModel`
  had to opt into, but worth confirming empirically during implementation that `count` (the QML-side
  alias Qt Quick models expose) reads as expected rather than requiring `rowCount()` called through
  `model.rowCount()`.
