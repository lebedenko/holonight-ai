# DESIGN: Three-panel workspace layout

**Spec:** `docs/sdd/three-panel-window-layout/SPEC.md`
**Status:** Implemented
**Date:** 2026-07-24

## Overview

This change completes the workspace's visual structure in QML without changing application state
or behavior. `WorkspaceWindow.qml` remains the owner of the top-level layout and replaces its
unframed center column with three sibling framed regions:

```text
HnApplicationWindow
└── RowLayout
    ├── ConversationListPanel (HnSurfaceFrame root)
    ├── HnSurfaceFrame (center workspace)
    │   └── ColumnLayout
    │       ├── RowLayout (collapse and settings actions)
    │       └── ChatPanel
    └── HnSurfaceFrame (empty right panel)
```

The existing `ConversationListPanel` is already an `HnSurfaceFrame`, so it is used directly as
the left frame. The center and right frames are declared inline because they are specific to this
window and do not provide reusable behavior. No new QML component, C++ type, dependency, or CMake
change is required.

## Existing and proposed layout

The current `RowLayout` has two children: a 220 px `ConversationListPanel` and an unframed
`ColumnLayout` containing the window actions and `ChatPanel`. The proposed `RowLayout` has exactly
three direct visual children in left-to-right order:

| Region | QML type | Width policy | Content |
| --- | --- | --- | --- |
| Left | `ConversationListPanel` | `Layout.preferredWidth: 220` | Existing conversation controls |
| Center | `HnSurfaceFrame` | `Layout.fillWidth: true` | Existing action row and `ChatPanel` |
| Right | `HnSurfaceFrame` | `Layout.preferredWidth: 220` | None |

All three set `Layout.fillHeight: true`. The row continues to fill the
`HnApplicationWindow` client item and uses `HoloniightPalette.controlPadding` for the two gaps.
Only the center participates in horizontal expansion, so extra width is allocated to it rather
than either side panel.

`HnApplicationWindow.contentPadding` remains `HoloniightPalette.controlPadding`. At the 1000 px
minimum width, the row therefore has 988 px before its two 6 px default-scheme gaps, leaving a
536 px center frame after the two 220 px side frames. These figures describe the current default
metric values; the implementation binds to
the metric rather than duplicating its numeric value.

The window's initial dimensions are 1000 × 640, with a 1000 × 480 minimum.

## Frame containment and QML API

`WorkspaceWindow.qml` applies dock-specific frame properties at the layout call site. This keeps
the directional corner masks tied to adjacency rather than making them an intrinsic property of
conversation navigation.

The left frame is the root `HnSurfaceFrame` exposed by `ConversationListPanel`:

```qml
ConversationListPanel {
    Layout.preferredWidth: 220
    Layout.fillHeight: true
    surfaceRole: HnSurfaceRole.Panel
    chamferedCornersOverride: HnCornerMask.TopRight | HnCornerMask.BottomRight
    fillColor: HoloniightPalette.surfaceRaised
    borderColor: HoloniightPalette.borderPassive
    borderWidth: HoloniightPalette.borderWidth
}
```

The center is an inline `HnSurfaceFrame` and owns the complete existing center column:

```qml
HnSurfaceFrame {
    Layout.fillWidth: true
    Layout.fillHeight: true
    surfaceRole: HnSurfaceRole.Window
    fillColor: HoloniightPalette.surface
    borderColor: HoloniightPalette.borderPassive
    borderWidth: HoloniightPalette.borderWidth

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: HoloniightPalette.controlPadding
        spacing: HoloniightPalette.controlPadding

        // Existing action RowLayout.
        // Existing ChatPanel.
    }
}
```

The center intentionally omits `cornerStyleOverride`, `radiusOverride`, `chamferOverride`, and
`chamferedCornersOverride`. Their `HnSurfaceFrame` defaults remain `Inherit`, `NaN`, `NaN`, and
`HnCornerMask.Inherit`, respectively, so `HnAppearance` fully resolves the `Window` geometry.
The content layout adds explicit palette-based margins because `HnSurfaceFrame` does not pad or
clip its default `contentData` slot.

The right panel is a childless inline frame:

```qml
HnSurfaceFrame {
    Layout.preferredWidth: 220
    Layout.fillHeight: true
    surfaceRole: HnSurfaceRole.Panel
    chamferedCornersOverride: HnCornerMask.TopLeft | HnCornerMask.BottomLeft
    fillColor: HoloniightPalette.surfaceRaised
    borderColor: HoloniightPalette.borderPassive
    borderWidth: HoloniightPalette.borderWidth
}
```

There is deliberately no item declared in the right frame's default `contentData` slot: no
padding container, label, placeholder, control, handler, or future-content scaffold. The frame's
own internal shape and content-slot implementation are shared-component details, not workspace
content.

## Semantic frame treatment

The three frames use the existing `Holonight` APIs directly:

| Region | Role | Corner topology | Fill | Border | Border width |
| --- | --- | --- | --- | --- | --- |
| Left | `HnSurfaceRole.Panel` | Explicit top-right + bottom-right chamfers | `surfaceRaised` | `borderPassive` | `borderWidth` |
| Center | `HnSurfaceRole.Window` | Fully inherited | `surface` | `borderPassive` | `borderWidth` |
| Right | `HnSurfaceRole.Panel` | Explicit top-left + bottom-left chamfers | `surfaceRaised` | `borderPassive` | `borderWidth` |

The side-panel masks point their chamfers toward the center seams and leave exterior corners
rounded. Because `HnSurfaceFrame` applies `chamferedCornersOverride` after the active appearance
profile resolves its role topology, the directional masks remain 6 for the left frame and 9 for
the right frame even when the profile selects another panel topology. Radius and chamfer
dimensions still come from the `Panel` role and active appearance profile; this design introduces
no local size or style override.

The raised side fills identify persistent structural panels. The quieter center `surface` keeps
the chat visually primary, while passive one-pixel semantic borders distinguish all three idle
regions without implying focus or selection. Major-region gaps, center inset, center child
spacing, and frame strokes all bind to the shared palette metrics.

## Data and action flow

The layout does not introduce state or signals. Existing interactions move under the center
frame without being rewritten:

- The collapse button continues to call `ChatApplication.CollapseToPanel("")`.
- The settings button activates `SettingsWindow`'s loader on first use and then toggles the loaded
  window.
- `SettingsWindow` remains in the workspace's QQmlEngine, so it uses the same singleton instances
  without making hidden visual construction part of application startup.
- `ChatPanel` retains its existing bindings to the shared chat components and view model.
- `ConversationListPanel` retains all create, switch, rename, and delete flows.
- The right frame has no model, focus handling, event handler, or application call.

Moving the action row and `ChatPanel` inside the frame's `contentData` item changes only visual
containment. It does not add an intermediate loader, component instance, or state boundary.

## Decisions and rationale

### Keep layout-specific styling in `WorkspaceWindow.qml`

Directional masks describe how each panel meets its siblings. Applying them where the siblings
are arranged makes the relationship explicit and avoids making `ConversationListPanel` assume it
is always docked on the left. This also keeps the production change focused in one QML file.

### Use one frame for the entire center workspace

The action row and chat form one functional region. Placing both inside one `Window` frame
satisfies the containment requirement and avoids a floating toolbar above a separately framed
chat surface.

### Keep the right frame inline and empty

An empty `RightPanel.qml` would have neither an API nor behavior and would obscure the acceptance
criterion that its content slot is empty. A dedicated component can be introduced later when
real right-panel content establishes a meaningful boundary.

### Use preferred side widths without responsive policy

`Layout.preferredWidth: 220` matches the existing left-panel convention. Since the window cannot
shrink below 1000 px and only the center uses `Layout.fillWidth`, both side regions retain 220 px
at the required validation widths. Minimum/maximum locks, splitters, and breakpoint logic are
unnecessary for this cycle.

## Alternatives considered

- **Make all three regions `Panel` frames or force the center fully chamfered.** Rejected because
  it gives the chat the same structural emphasis as the side panels and contradicts the approved
  inherited `Window` treatment.
- **Reduce or hard-code the side-panel chamfer size.** Rejected because the shared `Panel` role
  and appearance profile already own shape scale. Only the seam-facing topology is a
  product-specific requirement.
- **Change the shared frame guide or add dock-left/dock-right roles.** Rejected because
  `chamferedCornersOverride` already expresses the required adjacency without expanding the
  shared API.
- **Frame only `ChatPanel` and leave the action row outside.** Rejected because the controls would
  no longer be descendants of the center workspace frame.
- **Add placeholder content or a reusable right-panel component.** Rejected because the right
  panel has no present behavior and mockup-derived content is explicitly out of scope.
- **Add resizing, collapsing, or responsive breakpoints.** Rejected as a separate product
  decision; the 1000 px minimum provides the required fixed three-region baseline.

## Validation

| Validation | Requirements covered |
| --- | --- |
| Review `WorkspaceWindow.qml` for three direct `RowLayout` regions in the specified order and an empty right frame content slot | REQ-F-001, REQ-F-006, REQ-C-001–REQ-C-003 |
| Inspect layout properties at 1000 px: side widths 220, center receives the width difference, all three fill height | REQ-F-002, REQ-F-007 |
| Inspect frame properties: side roles/masks resolve to `Panel`/6 and `Panel`/9; center is `Window` with all four override inputs at their defaults | REQ-F-003, REQ-F-004 |
| Exercise collapse, settings, conversation navigation, and chat after the reparenting | REQ-F-005 |
| Inspect a fresh window's 1000 × 640 initial and 1000 × 480 minimum dimensions | REQ-F-008 |
| Launch with or switch to an appearance profile that changes panel topology and confirm the side resolved masks remain 6 and 9 | REQ-F-009 |
| Inspect live palette bindings and source review for `surfaceRaised`, `surface`, `borderPassive`, `borderWidth`, and `controlPadding` | REQ-NF-001, REQ-NF-002 |
| Run `task build` and `task qml-lint` | REQ-NF-003 |

Visual validation at exactly 1000 px confirms that fills and passive borders remain
distinct and that no frame overlaps another. Qt Quick runtime inspection can verify widths,
roles, override inputs, and resolved `chamferedCorners`; no new application-facing diagnostics
are needed.

## Known risks

- The center is intentionally compact at the 1000 px minimum. The calculated default-scheme frame
  width is 536 px before its internal padding; future right-panel content may require a wider
  minimum or responsive panel policy.
- Large user-configured shape scales can make seam chamfers more visually prominent, although
  `HnSurfaceFrame` clamps geometry and the explicit masks keep the topology valid.
- Reparenting the center layout introduces frame-content margins. This reduces chat space
  slightly, so the two required window widths must be checked visually as well as by property
  inspection.
- QML lint validates type and binding correctness but not rendered corner direction or exact
  layout allocation; those remain runtime inspection checks.
