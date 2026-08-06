# DESIGN: Workspace visual hierarchy polish

**Spec:** `docs/sdd/workspace-visual-hierarchy-polish/SPEC.md`
**Status:** Implementation in progress
**Date:** 2026-07-26

## Overview

This is a presentation-only QML change. It preserves the established three-panel architecture and
uses semantic palette values already available in the `Holonight` module. The implementation
touches only the workspace composition, conversation delegate, and shared chat presentation.

```text
WorkspaceWindow
└── RowLayout
    ├── ConversationListPanel
    ├── center HnSurfaceFrame
    │   └── ColumnLayout
    │       ├── toolbar
    │       │   ├── workspace label
    │       │   ├── ModelPicker
    │       │   ├── collapse
    │       │   └── settings
    │       └── ChatPanel (showModelPicker: false)
    │           └── MessageList
    │               ├── UserMessageCard
    │               └── MessageBubble
    └── WorkspaceInspectorPanel
```

## Surface and border policy

The design treats semantic fill, spacing, and alignment as the primary depth signals:

| Layer | Treatment |
| --- | --- |
| Application | Existing `HnApplicationWindow` background |
| Structural regions | Existing left/right `surfaceRaised`, center `surface`, passive borders |
| User message | Right-aligned, width-limited `surfaceElevated`, no border |
| Assistant message | Left-aligned quieter `surface`, no border |
| Controls | Existing control surfaces and focus/active borders |

Structural borders are retained because they define the three-column silhouette and angular frame
identity. Focusable search and composer borders are also retained because they communicate state.
The passive border on `UserMessageCard` is removed; `MessageBubble` remains borderless. This gives
the requested reduction in line density without weakening panel boundaries or focus cues.

No shadows are proposed. The current QML uses semantic surface tokens but exposes no established
shadow token or reusable elevation component. Adding ad-hoc blur and offset constants would make
appearance profiles harder to maintain and is unnecessary for this pass.

## Message presentation

`MessageList` remains the role router. `UserMessageCard` gains a
`maximumWidthRatio` presentation property and sizes itself to at most that fraction of the list
width, aligned to the right. It retains its avatar, author label, timestamp, and content. Its fill
changes to `surfaceElevated` and its passive border is removed.

`MessageBubble` continues to handle assistant and any non-user roles. The existing left alignment
and `surface` fill are retained, as is its status display. No new role logic or avatar state is
introduced.

`ChatPanel.messageWidthRatio` remains the single full/compact width policy. `MessageList` passes it
to both loaded message components, so the workspace keeps 0.75 and compact presentation keeps 0.9
without duplicated constants.

## Sidebar density

`ConversationListDelegate.implicitHeight` changes from:

```qml
HoloniightPalette.controlHeight + HoloniightPalette.controlPadding / 2
```

to:

```qml
HoloniightPalette.controlHeight + HoloniightPalette.controlPadding
```

This adds the review's requested small vertical increment using the existing metric scale. The
delegate's internal margins, hover-revealed actions, selection accent, timestamp color, filtering,
and interaction handlers remain unchanged. Increasing `ListView.spacing` as well would compound
the density change and is therefore not included.

## Toolbar composition

The center toolbar absorbs the existing `ModelPicker` from `ChatPanel`:

```text
[holonight-ai] [ModelPicker grows] [spacer if needed] [collapse] [settings]
```

`WorkspaceWindow` configures `ChatPanel.showModelPicker: false`, preventing duplicate controls.
The picker still binds directly to `ChatViewModel` and keeps all current synchronization and
selection behavior. `ChatPanel` itself is not structurally changed, so the quick panel retains its
default `showModelPicker: true`.

The workspace label is intentionally static. There is no project/workspace model in the current
application, and adding one for a fixed repository label would expand this visual task into
application-state design.

## Workspace inspector

A new `qml/workspace/WorkspaceInspectorPanel.qml` component provides a meaningful but deliberately
read-only right panel. Giving the content a named component keeps `WorkspaceWindow.qml` focused on
composition and creates a natural boundary for future inspector work.

The component root is `HnSurfaceFrame`, replacing the current inline right frame while preserving:

- `Layout.preferredWidth: 220` at the call site;
- `HnSurfaceRole.Panel`;
- left-facing chamfer mask;
- `surfaceRaised` fill;
- passive semantic border.

Inside the frame, a padded `ColumnLayout` contains a heading and three small sections. Sections use
plain layout items and text rather than separate outlined cards, avoiding the border proliferation
called out by the review.

The active model display reads `ChatViewModel.selectedModelId` directly:

```qml
readonly property var selectedModel: ChatViewModel.selectedModelId
readonly property bool hasSelectedModel:
    selectedModel.provider_id && selectedModel.model_name
```

The visible value is `provider_id + "/" + model_name` or `No model selected`. Context displays the
literal workspace label `holonight-ai`; attachments displays `No attachments`. Long model names
wrap or elide within the 220 px panel. There are no controls, focus scopes, handlers, timers, or
mirrored writable properties.

This explicitly supersedes the earlier three-panel layout cycle's empty-right-panel requirement.
That requirement described the previous delivery boundary; the new spec owns the next state.

## Files

| File | Planned change |
| --- | --- |
| `qml/workspace/WorkspaceWindow.qml` | Compose the fuller toolbar, hide the nested picker, and use the inspector |
| `qml/workspace/WorkspaceInspectorPanel.qml` | Add read-only model/context/attachment presentation |
| `qml/workspace/ConversationListDelegate.qml` | Increase row height by a palette-derived half-step |
| `qml/shared/UserMessageCard.qml` | Constrain/right-align the card, elevate its fill, and remove its border |
| `qml/shared/MessageList.qml` | Pass the existing message-width policy to user cards |

No C++, CMake, test-model, persistence, provider, configuration, or dependency changes are planned.
The QML file is discovered by the existing recursive QML source registration.

## Implementation sequence

1. Add `WorkspaceInspectorPanel` with read-only bindings and constrained text.
2. Replace the inline empty right frame and enrich the center toolbar.
3. Apply the conversation row-height adjustment.
4. Apply the message alignment, width, fill, and border changes.
5. Run QML lint and build before broader tests.
6. Perform focused runtime checks at normal and minimum dimensions.

This order makes each diff independently reviewable and leaves the behavioral surfaces unchanged.

## Alternatives considered

- **Add shadows for every elevation level.** Rejected because the repository has no shared shadow
  vocabulary and semantic fill changes provide the needed hierarchy with less complexity.
- **Remove all borders.** Rejected because structural outlines are part of the established visual
  identity and focus borders carry interaction meaning.
- **Show token counts or conversation statistics.** Rejected because no authoritative state
  exists; placeholder numbers would be misleading.
- **Build interactive attachments, context, or tools.** Rejected as separate feature work that
  requires application and domain requirements.
- **Duplicate the model name in both toolbar and inspector.** The toolbar picker is interactive;
  the inspector's active-model row is contextual confirmation. This small duplication makes the
  otherwise static inspector meaningful and does not duplicate state.
- **Change the global background.** Rejected because the review explicitly favors its restraint
  and any vignette/noise work would need theme-level design.

## Validation

Run the narrowest checks first:

1. `task qml-lint`
2. `task build`
3. `task test`

Then manually verify:

- a mixed transcript clearly separates user and assistant messages;
- user messages stay within the configured width in workspace and compact presentations;
- model selection from the toolbar updates the active model and sending still works;
- there is one model picker in the workspace and the quick panel still shows its picker;
- the inspector updates when model selection changes and has no focusable elements;
- conversation activate, rename, delete, hover, and selection behavior still works;
- collapse and settings actions still work;
- at 1000 × 480, toolbar items do not overlap and inspector content stays inside its frame;
- no new runtime QML warnings appear.

## Risks

- A long provider/model identifier can exceed the inspector's narrow width. The value must wrap or
  elide rather than force panel growth.
- Moving the model picker changes keyboard traversal order in the workspace. Manual focus testing
  must confirm the picker remains reachable and the transcript/composer remain usable.
- Width-limiting `UserMessageCard` changes its implicit-height calculation. The card's internal
  wrapping must be validated with short and long messages.
- The earlier layout spec requires an empty right panel. This feature intentionally supersedes
  only that requirement; all other layout requirements remain in force.
