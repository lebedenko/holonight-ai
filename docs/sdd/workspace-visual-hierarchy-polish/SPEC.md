# SPEC: Workspace visual hierarchy polish

**Feature:** Refine depth, scanability, density, and information balance in the main workspace
**Status:** Implementation in progress
**Date:** 2026-07-26
**Source:** `/tmp/window-layout-review-mid-work.md`

## Overview

The current workspace has a strong three-column composition and a cohesive HoloNight identity,
but its major surfaces sit at similar visual depth, user and assistant messages could be easier to
scan, conversation rows are dense, the right panel reads as unfinished, and the center toolbar
does not use its available width. This cycle applies a focused hierarchy pass while preserving the
existing layout, palette, angular frame language, and chat behavior.

The review's suggestions are interpreted conservatively. The implementation uses existing
semantic surfaces and spacing instead of introducing shadows, textures, fake statistics, or new
application state.

## Scope

- Establish a clear surface hierarchy among structural panels, messages, and interactive controls.
- Make user and assistant messages visually distinguishable by alignment, width, surface value,
  and avatar treatment.
- Increase conversation-row vertical breathing room by one half-control-padding step.
- Replace the empty right panel with a read-only workspace inspector based only on existing state.
- Move the workspace model picker into the center toolbar and add a workspace label.
- Remove passive borders from non-interactive message cards while retaining boundaries on
  structural panels and focusable controls.
- Preserve the current background, three-column widths, minimum window size, and all behavior.

## Functional Requirements

### Surface hierarchy

**REQ-F-001 (Ubiquitous)**
The workspace shall present four distinguishable visual levels using existing semantic palette
tokens: application background, structural panels, message content, and interactive controls.

- **Acceptance Criterion:** Visual inspection in the supported dark appearance shows the three
  structural regions separated from the application background, message surfaces separated from
  their containing chat surface, and controls remaining the highest-emphasis interactive layer.

**REQ-F-002 (Ubiquitous)**
The left and right structural panels shall retain `HoloniightPalette.surfaceRaised`; the center
workspace shall retain `HoloniightPalette.surface`; message components shall use existing
`surface`, `surfaceRaised`, or `surfaceElevated` tokens to create contrast without a new shadow
implementation.

- **Acceptance Criterion:** Source review finds only existing semantic palette bindings and no
  custom shadow, blur, glow, gradient, or literal color added for elevation.

### Message hierarchy

**REQ-F-003 (Ubiquitous)**
User messages shall be right-aligned, limited to 75 percent of the available transcript width in
the full workspace, and rendered on a darker or more elevated solid semantic surface than
assistant messages.

- **Acceptance Criterion:** A transcript containing both roles shows the user message on the right
  at no more than 75 percent width and the assistant response on the left; their fill values are
  visibly different in the supported dark appearance.

**REQ-F-004 (Ubiquitous)**
Assistant messages shall remain left-aligned and use the quieter of the two message surfaces.

- **Acceptance Criterion:** Assistant messages begin at the left transcript edge and do not use
  the user message's emphasized surface treatment.

**REQ-F-005 (Ubiquitous)**
The user-message avatar shall remain visible and use the existing primary accent treatment; no
assistant avatar, generated portrait, or new identity asset shall be introduced.

- **Acceptance Criterion:** User messages retain the circular accent avatar, while source and
  resource review find no new avatar asset or assistant-avatar implementation.

### Sidebar density

**REQ-F-006 (Ubiquitous)**
Each conversation row shall gain `HoloniightPalette.controlPadding / 2` of vertical space relative
to its current implicit height, without changing its title, timestamp, icon, or actions.

- **Acceptance Criterion:** `ConversationListDelegate.implicitHeight` equals
  `HoloniightPalette.controlHeight + HoloniightPalette.controlPadding`, and activate, rename,
  delete, hover, and selection behavior remain intact.

**REQ-F-007 (Ubiquitous)**
Conversation timestamps shall retain the muted semantic text color and shall not compete with the
conversation title.

- **Acceptance Criterion:** Timestamp text remains bound to `HoloniightPalette.textMuted`, while
  title text remains bound to `HoloniightPalette.textPrimary`.

### Center toolbar

**REQ-F-008 (Ubiquitous)**
The center toolbar shall show the workspace label `holonight-ai`, the existing model picker, the
collapse action, and the settings action in that left-to-right grouping.

- **Acceptance Criterion:** Visual inspection shows the workspace label and model picker occupying
  the toolbar's left and flexible space, followed by the existing collapse and settings actions.

**REQ-F-009 (Event-driven)**
When the user changes the model from the toolbar picker, the system shall update
`ChatViewModel.selectedModelId` through the existing `ModelPicker` behavior.

- **Acceptance Criterion:** Selecting another available model updates the active selection and
  subsequent sends use it exactly as before this cycle.

**REQ-F-010 (Ubiquitous)**
The full workspace shall contain exactly one visible model picker; the quick panel shall retain
its existing model-picker policy.

- **Acceptance Criterion:** `WorkspaceWindow` hosts the picker in its toolbar and configures its
  `ChatPanel` with `showModelPicker: false`; `QuickPanel.qml` is behaviorally unchanged.

### Right workspace inspector

**REQ-F-011 (Ubiquitous)**
The right panel shall contain a dedicated read-only workspace inspector with a heading and three
sections: active model, context, and attachments.

- **Acceptance Criterion:** Visual inspection shows all three labeled sections inside the right
  panel, using the existing panel padding and semantic typography colors.

**REQ-F-012 (State-driven)**
While a model is selected, the active-model section shall display its existing provider ID and
model name; while none is selected, it shall display `No model selected`.

- **Acceptance Criterion:** The displayed value follows `ChatViewModel.selectedModelId` without a
  new C++ property, cache, or duplicated selection state.

**REQ-F-013 (Ubiquitous)**
The context section shall identify the current workspace as `holonight-ai`, and the attachments
section shall display the truthful empty state `No attachments`.

- **Acceptance Criterion:** The two values are visible and do not imply that context inspection or
  attachment support is available.

**REQ-F-014 (Unwanted-behavior)**
The inspector shall not expose controls, receive keyboard focus, or mutate application state.

- **Acceptance Criterion:** Source review finds no button, input, pointer handler, shortcut,
  signal handler, invokable call, or writable mirrored state in the inspector.

### Border restraint

**REQ-F-015 (Ubiquitous)**
Non-interactive user and assistant message surfaces shall not draw passive outline borders.

- **Acceptance Criterion:** Source review finds no non-zero border on either message component;
  their boundaries remain legible through alignment, spacing, and semantic fill differences.

**REQ-F-016 (Ubiquitous)**
Structural panel borders and focus-dependent borders on search and composer controls shall remain
unchanged.

- **Acceptance Criterion:** The three workspace frames retain their passive semantic borders, and
  focused search/composer controls still change to the existing focus border treatment.

## Non-functional Requirements

**REQ-NF-001 (Ubiquitous)**
All new colors and layout measurements shall use existing `HoloniightPalette` tokens or arithmetic
derived from them.

- **Acceptance Criterion:** Changed QML introduces no literal color, pixel spacing, radius,
  border-width, or shadow constants.

**REQ-NF-002 (Ubiquitous)**
The implementation shall reuse `ModelPicker`, `HnSurfaceFrame`, and existing QML layout types
rather than introducing a styling framework or dependency.

- **Acceptance Criterion:** Build metadata and dependencies are unchanged; the only new component,
  if used, is the feature-scoped read-only inspector.

**REQ-NF-003 (Ubiquitous)**
The layout shall remain usable at the current `minimumWidth: 1000` and `minimumHeight: 480`.

- **Acceptance Criterion:** At exactly 1000 × 480, all three panels remain visible, toolbar
  controls do not overlap, inspector labels elide or wrap within the right panel, and chat input
  remains reachable.

**REQ-NF-004 (Ubiquitous)**
Changed QML shall pass the repository's build and QML lint workflows.

- **Acceptance Criterion:** `task build` and `task qml-lint` complete successfully without new
  diagnostics caused by this feature.

## Constraints

**REQ-C-001 (Ubiquitous)**
This cycle shall not change window dimensions, column widths, panel corner masks, or appearance
profile behavior.

- **Acceptance Criterion:** `WorkspaceWindow` remains initially 960 × 640, has a 1000 × 480
  minimum, uses a 320 px left panel and 220 px right panel, and retains the current roles and
  directional masks.

**REQ-C-002 (Ubiquitous)**
This cycle shall not add token counts, conversation statistics, prompt history, plugin/tool state,
attachment behavior, or context-inspection behavior.

- **Acceptance Criterion:** The inspector displays only the state required by REQ-F-012 and the
  truthful labels required by REQ-F-013; no new C++ API, model role, or mock numeric value exists.

**REQ-C-003 (Ubiquitous)**
This cycle shall not add background patterns, grids, particles, noise, vignettes, radial glows, or
animation.

- **Acceptance Criterion:** No background visual or animation file is changed.

**REQ-C-004 (Ubiquitous)**
Chat, conversation, settings, persistence, provider, and quick-panel behavior shall remain
unchanged except for relocating the full workspace's existing model picker.

- **Acceptance Criterion:** Existing automated tests pass, and manual checks confirm send, stop,
  retry, model selection, conversation activation, rename, delete, collapse, and settings actions.

## Non-goals

- Implementing live token usage, statistics, prompt history, attachments, plugins, or tools.
- Adding a resizable or collapsible right panel.
- Creating new palette tokens or modifying the shared HoloNight theme.
- Adding shadows, glow, texture, noise, or animated background decoration.
- Redesigning settings or the quick panel.
- Changing fonts, typography scale, or conversation timestamp formatting.

## Traceable Acceptance Summary

| Check | Requirements |
| --- | --- |
| Inspect surface values and absence of decorative elevation effects | REQ-F-001, REQ-F-002, REQ-NF-001 |
| Compare user and assistant messages in a mixed transcript | REQ-F-003–REQ-F-005, REQ-F-015 |
| Exercise conversation-row interactions after spacing change | REQ-F-006, REQ-F-007, REQ-C-004 |
| Exercise the toolbar and verify one workspace model picker | REQ-F-008–REQ-F-010 |
| Inspect model, context, and attachment inspector states | REQ-F-011–REQ-F-014, REQ-C-002 |
| Inspect retained structural and focus borders | REQ-F-016 |
| Check the workspace at exactly 1000 × 480 | REQ-NF-003, REQ-C-001 |
| Run `task build`, `task qml-lint`, and `task test` | REQ-NF-004, REQ-C-004 |
| Review the diff for excluded background, backend, and dependency work | REQ-C-002–REQ-C-004 |
