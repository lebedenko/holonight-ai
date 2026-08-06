# SPEC: Three-panel workspace layout

**Feature:** Complete the main workspace with left, center, and right framed regions
**Status:** Implemented
**Date:** 2026-07-24

## Overview

The main workspace currently presents conversation navigation beside an unframed chat region.
This change establishes the intended three-column hierarchy by framing the existing left and
center regions and adding an empty right panel. The mockup is a hierarchy reference only; this
cycle does not introduce the features depicted inside its panels.

## Scope

- Arrange the workspace as a 220 px left panel, flexible center region, and 220 px right panel.
- Apply shared `HnSurfaceFrame` roles, directional corner masks, palette colors, borders, and
  spacing to those three regions.
- Keep the existing collapse and settings actions together with `ChatPanel` inside the center
  frame.
- Increase the workspace minimum width from 720 px to 900 px.

## Functional Requirements

**REQ-F-001 (Ubiquitous)**
The workspace shall present exactly three adjacent top-level content regions in left-to-right
order: conversation panel, center workspace, and right panel.

- **Acceptance Criterion:** Inspecting the running workspace shows three distinct, non-overlapping
  framed regions in that order, each filling the available content height.

**REQ-F-002 (Ubiquitous)**
The left and right regions shall each have a preferred width of 220 logical pixels, while the
center region shall consume the remaining horizontal space.

- **Acceptance Criterion:** At both 900 px and 960 px window widths, the two side regions measure
  220 logical pixels each, excluding layout spacing, and changing between those widths changes
  the center region's width rather than either side region's preferred width.

**REQ-F-003 (Ubiquitous)**
The left region shall use `HnSurfaceRole.Panel` with
`HnCornerMask.TopRight | HnCornerMask.BottomRight`, and the right region shall use
`HnSurfaceRole.Panel` with `HnCornerMask.TopLeft | HnCornerMask.BottomLeft`.

- **Acceptance Criterion:** Runtime inspection of both `HnSurfaceFrame` instances reports the
  `Panel` role and the specified masks; visually, the chamfered corners face the center seam and
  the exterior corners remain rounded.

**REQ-F-004 (Ubiquitous)**
The center region shall use `HnSurfaceRole.Window` and shall inherit its corner geometry without a
local corner-style, radius, chamfer-size, or corner-mask override.

- **Acceptance Criterion:** Runtime inspection reports the `Window` role and default inherited
  values for all four geometry override properties.

**REQ-F-005 (Ubiquitous)**
The center frame shall contain the existing collapse action, settings action, and `ChatPanel`,
preserving their current interactions.

- **Acceptance Criterion:** Both actions and the chat UI are descendants of the center frame;
  activating collapse still requests `ChatApplication.CollapseToPanel("")`, and activating
  settings still toggles the existing settings window.

**REQ-F-006 (Ubiquitous)**
The right panel shall contain no labels, placeholders, controls, mock content, or interaction
behavior.

- **Acceptance Criterion:** The right frame's content slot contains no visual or interactive
  child item.

**REQ-F-007 (State-driven)**
While the workspace width is either 900 px or 960 px, all three frames shall remain visible,
distinct, and free of overlap.

- **Acceptance Criterion:** Visual checks at exactly 900 px and exactly 960 px confirm visible
  fill and border boundaries for all three frames, with no frame covering another.

**REQ-F-008 (Event-driven)**
When the workspace is first shown, it shall use the existing 960 × 640 logical-pixel initial size
and expose minimum dimensions of 900 × 480 logical pixels.

- **Acceptance Criterion:** A newly created workspace reports `width: 960`, `height: 640`,
  `minimumWidth: 900`, and `minimumHeight: 480`.

**REQ-F-009 (Unwanted-behavior)**
If the active HoloNight appearance profile changes its inherited panel corner topology, the
workspace shall continue to apply the explicit inner-facing corner masks from REQ-F-003 to both
side panels.

- **Acceptance Criterion:** After selecting or simulating a profile with different panel corner
  topology, each side frame's resolved `chamferedCorners` still equals its explicit directional
  mask.

## Non-functional Requirements

**REQ-NF-001 (Ubiquitous)**
The side frames shall use `HoloniightPalette.surfaceRaised` for fill and
`HoloniightPalette.borderPassive` for the border; the center frame shall use
`HoloniightPalette.surface` for fill and `HoloniightPalette.borderPassive` for the border.

- **Acceptance Criterion:** Runtime property inspection confirms the specified semantic palette
  bindings on all three frames.

**REQ-NF-002 (Ubiquitous)**
Frame border width and spacing between major regions and center content shall use the applicable
shared `HoloniightPalette` metrics rather than new numeric styling constants.

- **Acceptance Criterion:** QML review finds palette metric bindings for frame borders and layout
  spacing, with no newly introduced literal border-width or spacing values.

**REQ-NF-003 (Ubiquitous)**
The changed QML shall remain compatible with the repository's configured build and QML lint
workflows.

- **Acceptance Criterion:** `task build` and `task qml-lint` both complete successfully.

## Constraints

**REQ-C-001 (Ubiquitous)**
This cycle shall not add search, context, activity, tools, profile, or other content or behavior
shown only in the mockup.

- **Acceptance Criterion:** A diff review finds no new mockup-derived controls, models, text, or
  application behavior.

**REQ-C-002 (Ubiquitous)**
This cycle shall not add panel resizing, panel collapsing, breakpoint-driven layout, or other
responsive panel behavior.

- **Acceptance Criterion:** A diff review finds no splitters, resize handles, panel visibility
  state, width breakpoints, or responsive layout branches.

**REQ-C-003 (Ubiquitous)**
The implementation shall use the existing `Holonight` shared frame and palette APIs and shall not
introduce a new framework, dependency, or custom frame-drawing abstraction.

- **Acceptance Criterion:** Build metadata contains no new dependency, and the workspace QML uses
  `HnSurfaceFrame`, `HnSurfaceRole`, `HnCornerMask`, and `HoloniightPalette` directly.

## Non-goals

- Implementing any content for the right panel.
- Reproducing the mockup's controls or exact corner artwork.
- Adding panel resizing, collapsing, or responsive behavior.
- Changing chat, conversation, settings, or quick-panel application behavior.
- Defining layout needs for future right-panel content.

## Traceable Acceptance Summary

| Check | Requirements |
| --- | --- |
| Inspect three-column ordering, sizing, frame roles, and containment | REQ-F-001–REQ-F-006 |
| Visually inspect distinct frames at 900 px and 960 px widths | REQ-F-002, REQ-F-003, REQ-F-007 |
| Inspect initial and minimum window dimensions | REQ-F-008 |
| Verify directional masks survive an appearance-profile topology change | REQ-F-003, REQ-F-009 |
| Inspect semantic color, border, and spacing bindings | REQ-NF-001, REQ-NF-002 |
| Run `task build` and `task qml-lint` | REQ-NF-003 |
| Review the diff for excluded features, responsive behavior, and dependencies | REQ-C-001–REQ-C-003 |

## Known Risk

At the 900 px minimum width, the center region is intentionally compact. Real right-panel content
may later justify panel collapsing, resizing, or a wider minimum, but those decisions are deferred
to a future cycle.
