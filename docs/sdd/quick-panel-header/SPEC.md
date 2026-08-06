# Quick Panel Header — EARS Specification

**Feature:** New header component for the wlr-layer-shell quick-panel surface, replacing the bare top row containing only an external-link button. The header provides app branding, a "New/recent chats" dropdown menu, workspace-switch affordance, and panel-close controls.

**Component:** `qml/quickpanel/QuickPanelHeader.qml` (new file, integrated as top row in `qml/quickpanel/QuickPanel.qml`'s `ColumnLayout`).

---

## Constraints

### REQ-C-001: No New C++ Code
The <system> shall implement only QML — no new C++ classes, no new Q_INVOKABLE/Q_PROPERTY members beyond what `ChatViewModel` and `ChatApplication` already expose, and no changes to `holonight_application` module's CMake or source tree.

**AC:** Running `git diff src/` from the project root produces no output after implementation.

### REQ-C-002: Pure Backend Reuse
Where existing C++ backend methods exist (e.g., `createConversation()`, `switchConversation()`, `ShowWorkspace()`, `ClosePanel()`), the <system> shall invoke them without modification or wrapping.

**AC:** Grepping `QuickPanelHeader.qml` for method calls shows only direct invocations: `ChatViewModel.createConversation()`, `ChatViewModel.switchConversation()`, `ChatApplication.ShowWorkspace()`, `ChatApplication.ClosePanel()`.

### REQ-C-003: No Persistence or Pin Functionality
If the Pin button is rendered, the <system> shall NOT implement any pinned-conversation list, persistence layer, or UI for managing pinned chats — pin functionality is explicitly out of scope for this cycle.

**AC:** Searching the codebase for new database migrations, new `holonight_persistence` module changes, or new Pin-related Q_PROPERTY/Q_INVOKABLE members yields zero results post-implementation.

### REQ-C-004: ChatHeader.qml Unchanged
The <system> shall NOT modify `qml/shared/ChatHeader.qml` (the provider/model picker row). It remains directly below the new header in `QuickPanel.qml`'s `ColumnLayout`, unchanged in behavior or appearance.

**AC:** File `qml/shared/ChatHeader.qml` remains identical to pre-implementation version; diff output is empty.

### REQ-C-005: No New Workspace Window Changes
The <system> shall NOT modify the workspace window's own header, layout, or any component within `qml/workspace/`. All workspace-related changes are out of scope.

**AC:** Directory `qml/workspace/` contains no new or modified files post-implementation.

---

## Structural & Layout Requirements

### REQ-F-001: Header Component Root
The <system> shall define `QuickPanelHeader.qml` as a QML component containing a row-based layout (e.g., `RowLayout`) with two halves: a left-aligned section and a right-aligned section.

**AC:** File `qml/quickpanel/QuickPanelHeader.qml` exists and contains a top-level layout (RowLayout or equivalent) with `anchors.fill: parent` or similar fill behavior, and child items are visually separated into left and right alignment groups.

### REQ-F-002: Left Side — App Icon + Title
The <system> shall render the left side of the header with (1) an `HnIcon` displaying the holonight-ai application icon at `HoloniightPalette.controlHeight` size, and (2) adjacent bold text reading "Quick chat" (via `qsTr()`).

**AC:**
- Icon uses `source: "qrc:/HolonightChat/assets/holonight-ai.svg"`, `tinted: false`, and `size: HoloniightPalette.controlHeight`.
- Text element has `font.bold: true`, color `HoloniightPalette.textPrimary`, and text content matches `qsTr("Quick chat")` at design time (inspector shows localized string).

### REQ-F-003: Icon Styling Reuse
The <system> shall reuse the exact icon styling pattern from `qml/workspace/ConversationListPanel.qml` to ensure visual consistency (no new icon patterns, no re-inventing icon size/tint/color logic).

**AC:** Diff between the icon element in `QuickPanelHeader.qml` and the icon in `ConversationListPanel.qml` (excluding layout properties) is zero or contains only comment additions.

### REQ-F-004: Right Side — Four Icon Buttons
The <system> shall arrange four icon buttons on the right side of the header in left-to-right order: (1) New/recent-chats dropdown trigger, (2) Switch to workspace, (3) Pin (placeholder), (4) Close panel.

**AC:** Right-aligned container has exactly four button-like items; manual inspection or QML property dump shows four children with icon children or similar; buttons visually appear in the order specified, left to right.

---

## New/Recent Chats Dropdown Button (REQ-F-005 to REQ-F-015)

### REQ-F-005: Dropdown Trigger Button Definition
When the user clicks the first right-side button (New/recent-chats trigger), the <system> shall open a popup/dropdown menu below or anchored to the button.

**AC:** Clicking the button makes a popup/dropdown component appear (not previously visible); QML object tree shows a `Popup` or `Menu` node becomes active/visible.

### REQ-F-006: Dropdown Menu Contents — New Chat Item
The <system> shall include a "New chat" menu item as the first item in the dropdown. Clicking it shall invoke `ChatViewModel.createConversation()` and close the dropdown without closing the quick panel.

**AC:**
- Dropdown's first delegate/item displays text "New chat" (via `qsTr()`).
- Clicking it logs a method call or emits a signal ending with `ChatViewModel.createConversation()`.
- Dropdown closes (becomes invisible); quick panel remains open (verified by no `ChatApplication.ClosePanel()` call and no `QuickPanel.close()` invocation).

### REQ-F-007: Dropdown Menu Contents — Separator
The <system> shall display a separator/divider line below the "New chat" item.

**AC:** A horizontal line element (e.g., `Rectangle { height: 1px }` or a `MenuSeparator`) is visible between "New chat" and the first conversation row, visible at all times when the dropdown is open and the conversation list is non-empty.

### REQ-F-008: Dropdown Menu Contents — Recent Conversations
The <system> shall populate the dropdown below the separator with up to 10 most-recent conversations sourced from `ChatViewModel.conversationList`, taking rows in order (no re-sorting required; the repository's SQL already orders `updated_at DESC`).

**AC:**
- Dropdown delegate is bound to `ChatViewModel.conversationList` (or a proxy model limiting to 10 rows).
- When `conversationList` contains 5 conversations, the dropdown displays exactly 5 conversation rows below the separator.
- When `conversationList` contains 15 conversations, the dropdown displays exactly 10 conversation rows (first 10 by order).
- When `conversationList` is empty, no conversation rows appear (see REQ-F-012).

### REQ-F-009: Dropdown Row Content & Click Behavior
The <system> shall render each conversation row in the dropdown with its `TitleRole` text (conversation title). Clicking a row shall invoke `ChatViewModel.switchConversation(conversationId)` (passing the row's `IdRole` value) and close the dropdown without closing the quick panel.

**AC:**
- Each row displays the conversation's title text (from model's `TitleRole`).
- Clicking a row triggers a method call to `ChatViewModel.switchConversation()` with a string argument matching the conversation's `IdRole`.
- Dropdown closes; quick panel remains open.

### REQ-F-010: Active Conversation Highlight
While a conversation row's `IdRole` matches `ChatViewModel.activeConversationId`, the <system> shall visually highlight that row using the pattern from `qml/workspace/ConversationListDelegate.qml`: the row's fill color shall be `HoloniightPalette.surfaceElevated` (or equivalent highlight), and a left accent `Rectangle` in `HoloniightPalette.borderActive` shall be visible (no rename/delete affordances; simpler than `ConversationListDelegate.qml`).

**AC:**
- Inspecting the active row's properties shows `fillColor` or `color` equals `HoloniightPalette.surfaceElevated` (or similar).
- A vertical accent bar/rectangle is visible on the left edge of the active row, colored `HoloniightPalette.borderActive`.
- The highlight updates dynamically when `ChatViewModel.activeConversationIdChanged` fires (e.g., after switching to a different conversation via `switchConversation()`).

### REQ-F-011: Dropdown Dismissal — Outside Click
When the user clicks outside the dropdown's bounds (but still within the quick panel or elsewhere on screen), the <system> shall close the dropdown.

**AC:** Clicking in an empty area of the quick panel, on another button, or outside the quick panel entirely closes the dropdown without side effects; quick panel remains open (or closes if the click was on the Close Panel button, per REQ-F-019).

### REQ-F-012: Dropdown Dismissal — Escape Key
When the user presses Escape while the dropdown is open, the <system> shall close only the dropdown; the `Keys.onEscapePressed` handler in `QuickPanel.qml` (which calls `ChatApplication.ClosePanel(true)`) shall NOT fire.

**AC:**
- Pressing Escape closes the dropdown (it becomes invisible).
- Quick panel remains open; no `ChatApplication.ClosePanel()` message appears in console/logs immediately after Escape.
- The quickpanel's own Escape handler is not invoked (e.g., workspace window does not pop to foreground as a side effect).

### REQ-F-013: Empty Conversation List — Separator Handling
If `ChatViewModel.conversationList` contains zero conversations, the <system> shall either hide the separator line or show it without any conversation rows below it; the dropdown shall not render broken, crash, or show placeholder text like "No conversations."

**AC:**
- With zero conversations, the dropdown displays "New chat" and either no separator (or a separator with nothing below it).
- No "No conversations" or similar placeholder text appears.
- No console errors, warnings about empty models, or visual glitches.

### REQ-F-014: Dropdown Delegate Simplicity
The <system> shall use a new, simpler dropdown row delegate (distinct from `ConversationListDelegate.qml`). The delegate shall include the title text and active-highlight styling (per REQ-F-010) but shall NOT include rename/delete affordances, context menus, or drag-and-drop.

**AC:** The dropdown's delegate component is a new file or inline delegate (e.g., `QuickPanelHeaderDropdownDelegate.qml` or an inline `delegate: Item { ... }` in the Repeater/ListView) that does not reference or extend `ConversationListDelegate.qml` and contains no delete-button, rename-button, or context-menu bindings.

### REQ-F-015: Recent Conversations Update Binding
When `ChatViewModel.conversationList` is updated (e.g., after creating a new conversation), the <system> shall refresh the dropdown's conversation list dynamically without requiring the user to close and reopen the dropdown.

**AC:** After calling `ChatViewModel.createConversation()` and a new conversation row appears in `conversationList`, re-opening the dropdown shows the new conversation in its list; or if the dropdown is already open, the list auto-updates (Repeater/ListView re-instantiates delegates).

---

## Switch to Workspace Button (REQ-F-016)

### REQ-F-016: Workspace Switch Button
The <system> shall render a second right-side button with an external-link-style icon. Clicking it shall invoke `ChatApplication.ShowWorkspace()`, which closes the panel surface and shows/raises/activates the workspace window.

**AC:**
- Button has an icon resembling an external link or similar "switch window" affordance.
- Clicking the button triggers `ChatApplication.ShowWorkspace()`.
- Console/logs show the method is called; quick panel closes and workspace window becomes visible/focused.

---

## Pin Button (REQ-F-017 to REQ-F-019)

### REQ-F-017: Pin Button Rendering & Disabled State
The <system> shall render a third right-side button with a pin-style icon in a visually muted/disabled appearance (e.g., `enabled: false`, reduced opacity, grayed-out color, or similar visual cue).

**AC:**
- Button is present and rendered with a pin icon.
- Button has `enabled: false` OR has reduced icon opacity (e.g., `opacity: 0.5`) OR uses a desaturated/grayed color scheme.
- Visual inspection or property dump confirms the button is not highlighted on hover or focus (or is unresponsive to interaction).

### REQ-F-018: Pin Button Accessibility & Not-Yet-Implemented Indication
The <system> shall provide an `Accessible.name` or `Accessible.description` on the Pin button indicating the feature is not yet implemented (e.g., "Pin conversation (not yet implemented)" or equivalent wording).

**AC:** QML source shows an `Accessible.name` or `Accessible.description` property on the pin button containing text indicating unavailability or work-in-progress status; accessibility tooling or manual QML inspection confirms the accessible name is present.

### REQ-F-019: Pin Button No-Op Behavior
If the Pin button remains clickable by accident (despite disabled state), the <system> shall ensure clicking it produces no visible state change, no signal emission, no method invocation, and no console output.

**AC:** Clicking the pin button produces: no change to UI state (no modal, no notification, no highlight change), no console logs/errors, no invocation of C++ methods, and no change to any application property.

---

## Close Panel Button (REQ-F-020)

### REQ-F-020: Close Panel Button
The <system> shall render a fourth right-side button with an X-style icon. Clicking it shall invoke `ChatApplication.ClosePanel(false)` (distinct from the Escape-key handler in `QuickPanel.qml`, which calls `ClosePanel(true)`), closing the panel surface WITHOUT showing the workspace window.

**AC:**
- Button has an X icon or similar "close" affordance.
- Clicking the button logs a call to `ChatApplication.ClosePanel(false)` (with `false` argument, not `true`).
- Quick panel closes; workspace window does NOT pop to foreground (distinguish from Escape behavior).

---

## Theming & Accessibility (REQ-NF-001 to REQ-NF-004)

### REQ-NF-001: Palette Token Usage
The <system> shall use only palette tokens from `HoloniightPalette` (imported from `Holonight.Core`) for colors — no hardcoded hex values (e.g., `#FFFFFF`, `rgb(0, 0, 0)`). All text, background, border, and icon colors shall derive from the palette.

**AC:** Grepping `QuickPanelHeader.qml` for patterns like `#[0-9a-fA-F]{6}`, `rgb(`, or `Qt.rgba(` yields zero results (exclude comments, strings, or import paths). All color assignments reference `HoloniightPalette.<token>`.

### REQ-NF-002: Component Behavior Pragma
The <system> shall include `pragma ComponentBehavior: Bound` at the top of `QuickPanelHeader.qml` to align with project convention (already present in every QML file in this directory).

**AC:** First non-comment line of `QuickPanelHeader.qml` is `pragma ComponentBehavior: Bound`.

### REQ-NF-003: Button Accessibility Names
The <system> shall assign an `Accessible.name` property to each of the four right-side buttons, using localized strings (e.g., `qsTr("New chat")`, `qsTr("Switch to workspace")`).

**AC:** Each of the four buttons (dropdown, workspace, pin, close) has an `Accessible.name` property with a unique, descriptive `qsTr()` string.

### REQ-NF-004: Reuse Control Height Constant
The <system> shall use `HoloniightPalette.controlHeight` for icon sizing and vertical spacing to maintain visual consistency with the existing application (e.g., icon size, button size, row height).

**AC:** Icon sizes and button dimensions reference `HoloniightPalette.controlHeight` or `HoloniightPalette.controlHeightSmall` (or similar canonical constants), not hardcoded pixel values.

---

## Non-Goals & Out of Scope

### REQ-C-006: Pin/Pinned Chat Functionality (Explicitly Out of Scope)
The <system> shall NOT implement pinned-conversation storage, a pinned list UI, or any persistence related to pin state. The Pin button is a placeholder only.

**AC:** Searching the codebase for "pin" or "pinned" (case-insensitive, excluding comments and test files) in `src/`, `apps/`, or database schema yields zero new implementation code; only the no-op button and its accessible description exist.

### REQ-C-007: Persistence Changes (Out of Scope)
The <system> shall NOT add new database migrations, new columns to `conversations` table, or any changes to `holonight_persistence` module.

**AC:** Directory `src/persistence/migrations/` contains no new `.sql` files; `ConversationRepository` API is unchanged.

### REQ-C-008: Chat Header Reuse (No Duplication)
The <system> shall NOT duplicate or reimplement the provider/model picker logic currently in `ChatHeader.qml`. The existing `ChatHeader.qml` remains unchanged and positioned directly below the new header.

**AC:** `qml/shared/ChatHeader.qml` file is identical pre- and post-implementation (no edits, no new properties, no binding changes).

---

## Summary of Requirement Categories

| Category      | Count | IDs              |
|---------------|-------|------------------|
| Constraints   | 8     | REQ-C-001 to -008 |
| Functional    | 20    | REQ-F-001 to -020 |
| Non-Functional| 4     | REQ-NF-001 to -004|
| **Total**     | **32** |                  |
