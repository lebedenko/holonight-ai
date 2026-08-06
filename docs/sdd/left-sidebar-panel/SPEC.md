# SPEC: Left Sidebar Panel Redesign

**Feature:** Redesign the conversation list panel with header, search, navigation footer, and refined row interactions
**Status:** Requirements Approved
**Date:** 2026-07-24

## Overview

The main workspace's left sidebar currently displays a minimal "New Chat" button above a plain conversation list. This cycle redesigns that panel to present a complete interface hierarchy matching `docs/mockups/main-window.png`: a static header with logo and title, a full-width "New chat" primary button, a functional search field that filters conversations by title, a "Recent" section containing the filterable conversation list with hover-revealed row actions and selected-row highlight styling, and a separator-backed five-icon navigation footer. The panel width increases from 220 to 320 logical pixels; the workspace minimum width increases correspondingly to preserve center-region usability. All row-level interactions (rename, delete) and model roles remain unchanged; the existing `updatedAt` role changes presentation from an absolute date to compact relative time.

## Scope

- Increase the left sidebar panel width to exactly 320 logical pixels.
- Increase the workspace minimum width to 1000 logical pixels (from 900) to preserve usable center-region width.
- Add a static header containing a hexagon+dot logo mark and "HoloNight AI" title text.
- Add a full-width "New chat" primary button with a plus icon.
- Add a search input field that filters the conversation list by case-insensitive substring match on conversation titles.
- Wire the "/" keyboard shortcut to focus the search field when unfocused.
- Add a "Recent" section label with a leading clock icon.
- Redesign conversation rows to display a leading folder icon, title with single-line ellipsis, and compact relative timestamp on the same line, with edit/delete actions revealed only on row hover.
- Refine selected-row styling: surfaceElevated background with a 2px left accent bar in a primary semantic color.
- Refine hover (non-selected) row styling: surfaceHover background.
- Add a separator and five-icon navigation footer matching the mockup; wire only the settings gear to the existing settings window and leave the other four icons inert.
- Preserve all existing conversation model, rename, delete, and "New Chat" creation behaviors; preserve the outer frame geometry (role, chamfer, fill, border); only the panel width changes.

## Functional Requirements

### Panel Layout and Dimensions

**REQ-F-001 (Ubiquitous)**
The left sidebar panel shall have a preferred width of exactly 320 logical pixels.

- **Acceptance Criterion:** At a workspace width of 960 logical pixels, the left panel's width measures 320 pixels (excluding any layout spacing outside the panel's bounds), and changing the workspace width changes the center and right panel dimensions, not the left panel's preferred width.

**REQ-F-002 (Ubiquitous)**
The workspace containing the sidebar panel shall have a minimum width of 1000 logical pixels, increased from the prior 900 logical pixels.

- **Acceptance Criterion:** The workspace's `minimumWidth` property equals 1000; at this minimum width, the center frame (between left and right panels) measures at least 436 logical pixels wide, computed as: 1000 (min width) − 2×6 (contentPadding) − 2×6 (RowLayout gaps) − 320 (left panel) − 220 (right panel) = 436 pixels; this preserves the center width that existed at the prior 900 px minimum with 220 px sides.

### Header

**REQ-F-003 (Ubiquitous)**
The panel shall display a static header at its top containing a hexagon+dot logo mark and "HoloNight AI" title text, stacked vertically or horizontally as the mockup shows.

- **Acceptance Criterion:** Visually inspecting the running panel shows the logo mark (rendered as inline QML `Shape`/`ShapePath`, not an external asset) and the literal text "HoloNight AI" in a single header section at the panel's top.

### New Chat Button

**REQ-F-004 (Ubiquitous)**
The panel shall display a full-width "New chat" primary button below the header, using `HnSurfaceRole.Control` geometry (rounded corners, radius inherited from `HnSurfaceRole.Control`).

- **Acceptance Criterion:** Clicking the button invokes `ChatViewModel.createConversation()` and creates a new conversation; the button uses `HoloniightPalette.primary` for fill color and `HoloniightPalette.onPrimary` for text/icon color; the button is visually flush to the panel's left and right edges (accounting for standard content margins) and spans the full width.

**REQ-F-005 (Ubiquitous)**
The "New chat" button shall contain a plus icon (rendered as inline QML `Shape`/`ShapePath`) and the text "New chat".

- **Acceptance Criterion:** The button's visual content shows both a plus icon and the text "New chat", positioned as shown in the mockup.

### Search Field

**REQ-F-006 (Ubiquitous)**
The panel shall display a search input field below the "New chat" button, labeled by placeholder text "Search conversations", accepting user text input.

- **Acceptance Criterion:** A text input field is visible with placeholder text "Search conversations"; typing in the field does not cause errors or warnings; the field accepts and displays all printable characters.

**REQ-F-007 (Ubiquitous)**
The search input shall include a magnifier icon (rendered as inline QML `Shape`/`ShapePath`) on its left side and a "/" keyboard-hint affordance on its right side.

- **Acceptance Criterion:** Visual inspection shows a magnifier icon on the left and a "/" symbol or glyph on the right of the search field; the "/" is static/non-interactive text (not a clickable button).

**REQ-F-008 (Event-driven)**
When the user presses the "/" key while the search field does not have focus, the system shall transfer focus to the search field.

- **Acceptance Criterion:** Pressing "/" when focus is anywhere in the panel except the search field (e.g., on a conversation row, the New Chat button, or the panel background) moves keyboard focus to the search field and the "/" key press does not appear as text in the field.

**REQ-F-009 (Event-driven)**
When the user types or clears text in the search field, the system shall update the displayed conversation list in real time, filtering to show only conversations whose title contains the typed text as a case-insensitive substring.

- **Acceptance Criterion:** Typing "test" in the search field displays only conversations with "test", "Test", "TEST", or similar case variants in their title; clearing the search field displays all conversations; a conversation with title "Testing model" matches a search for "test", and a conversation with title "test" matches a search for "TEST"; filtering is live (not deferred to a button press).

**REQ-F-010 (State-driven)**
While a search filter is active (search text is non-empty), the conversation list shall display only matching conversations, but all row-level interactions (activate, rename, delete) and the "New Chat" button shall remain fully functional on visible rows.

- **Acceptance Criterion:** With a search filter active, double-clicking a visible conversation row enters rename mode, the Delete button (hover-revealed) opens the delete confirmation, clicking a row activates it, and clicking "New Chat" creates a new conversation; filtered-out conversations are not interactable (they are absent from view).

### Section Label

**REQ-F-011 (Ubiquitous)**
The panel shall display a "Recent" section label with a clock icon before the conversation list.

- **Acceptance Criterion:** Above the conversation list, a visually distinct row shows a leading clock icon followed by the text "Recent", as in the authoritative mockup; the icon is rendered as inline QML `Shape`/`ShapePath` and uses a semantic palette token.

### Conversation Rows

**REQ-F-012 (Ubiquitous)**
Each conversation row shall display a leading folder icon, the conversation title (with single-line text ellipsis), and a compact relative timestamp on the title line.

- **Acceptance Criterion:** Each row shows, from left to right, a folder icon, a title truncated to a single line with "…" if it overflows, and the `updatedAt` text aligned at the right. `ConversationListModel` formats `updatedAt` as `now` for values less than one minute old, `Nm ago` below one hour, `Nh ago` below one day, and `Nd ago` thereafter.

**REQ-F-013 (Ubiquitous)**
The leading icon in each conversation row shall be the single consistent neutral folder icon shown in the mockup (rendered as inline QML `Shape`/`ShapePath`), not varied per conversation.

- **Acceptance Criterion:** Every conversation row displays the same icon shape; QML review finds a single icon definition used for all rows, not conditional icon switching based on conversation properties.

**REQ-F-014 (Event-driven)**
When the user hovers over a non-selected conversation row, the system shall apply a `HoloniightPalette.surfaceHover` background fill to that row.

- **Acceptance Criterion:** Moving the mouse over a non-active conversation row changes its background color to the `surfaceHover` palette token; moving away removes the hover background; this does not apply to the selected row (which has its own distinct background).

**REQ-F-015 (Event-driven)**
When a conversation row is in the selected/active state (the `activeConversationId` matches the row's `conversationId`), the system shall apply a `HoloniightPalette.surfaceElevated` background fill and a 2 logical-pixel left accent bar in a primary semantic palette color (e.g., `HoloniightPalette.primary` or `HoloniightPalette.borderActive`).

- **Acceptance Criterion:** The active conversation row shows a visually distinct background fill matching the `surfaceElevated` token and a 2 px vertical bar on the row's left edge in the chosen primary color; row title text remains `textPrimary` (not inverted), distinct from the prior full-`primary`-fill treatment.

**REQ-F-016 (Ubiquitous)**
Each conversation row shall display edit and delete action buttons that are revealed only when the row is hovered (not always-visible).

- **Acceptance Criterion:** At rest, a non-hovered row shows only the icon, title, and timestamp; moving the mouse over the row reveals an edit icon button and a delete icon button on the row's right side; moving away from the row hides them; clicking the edit button or double-clicking the row enters inline rename mode; clicking the delete button shows an inline confirmation; both behaviors are preserved from the prior implementation.

**REQ-F-017 (Event-driven)**
When the user activates/clicks a conversation row, the system shall call `ChatViewModel.switchConversation(conversationId)` with the row's conversation ID.

- **Acceptance Criterion:** Clicking a conversation row changes `ChatViewModel.activeConversationId` and updates the center panel to show that conversation; the click is processed even when a search filter is active, as long as the row is visible.

### Footer

**REQ-F-018 (Ubiquitous)**
The panel shall display a separator line followed by five evenly spaced icon buttons at its bottom, matching the mockup's navigation footer: layout, documentation, code, settings, and help.

- **Acceptance Criterion:** The footer is positioned at the panel's bottom, separated from the list by a full-width semantic-color line, and contains all five inline-shape icon buttons in the specified left-to-right order. No avatar, profile name, or profile card is present.

**REQ-F-019 (Event-driven)**
When the user clicks the settings gear icon button in the footer, the system shall toggle the visibility of the existing settings window (the same `settingsWindow.visible` state toggled by the center panel's existing settings button).

- **Acceptance Criterion:** Clicking the footer's gear icon opens the settings window if closed, and closes it if already open; the behavior is identical to the existing settings button in the center panel, indicating both toggle the same state; QML review shows both buttons bind to the same `settingsWindow.visible` property or a common visibility-toggle handler.

**REQ-F-020 (Ubiquitous)**
The layout, documentation, code, and help footer icon buttons shall be rendered and visible but shall have no click handlers, wired behavior, or application effects.

- **Acceptance Criterion:** All four buttons are visually present; clicking any of them produces no visible change, console message, state change, or navigation; QML review finds them defined without `onClicked` handlers or bound invokables.

## Non-functional Requirements

### Icon Implementation

**REQ-NF-001 (Ubiquitous)**
All icons in the panel (header logo, New Chat button plus, search magnifier, section label clock, conversation row leading icon, edit/delete actions, all five footer icons, and any other icon) shall be rendered as inline QML `Shape`/`ShapePath` components using SVG path data, not external asset files, icon fonts, or host system icon-theme lookups via `HnIcon`.

- **Acceptance Criterion:** QML review finds `Shape` and `ShapePath` definitions for every icon, no `HnIcon` usage, no references to external files or assets, and no hardcoded image paths; icons are self-contained SVG path strings within the QML code.

**REQ-NF-002 (Ubiquitous)**
All icon colors shall use semantic palette bindings (e.g., `HoloniightPalette.textMuted`, `HoloniightPalette.onPrimary`, `HoloniightPalette.textPrimary`) rather than literal hex color values.

- **Acceptance Criterion:** QML review finds palette token bindings for icon stroke and fill colors; no hardcoded hex color values (e.g., `#ffffff` or `#333333`) appear in icon definitions.

### Color and Styling

**REQ-NF-003 (Ubiquitous)**
All fill colors, text colors, and border colors shall use semantic `HoloniightPalette` tokens (e.g., `surfaceRaised`, `textPrimary`, `borderPassive`, `borderActive`) rather than literal hex values or RGB constants.

- **Acceptance Criterion:** QML review finds palette token bindings for every color; no hardcoded hex strings, `Qt.rgba()` constants, or color() literals appear in the panel's QML code.

**REQ-NF-004 (Ubiquitous)**
Frame radius values shall be derived from `HnSurfaceRole` definitions (e.g., `HnSurfaceRole.Control` for buttons, `HnSurfaceRole.Card` for card-like elements) rather than hardcoded radius literals.

- **Acceptance Criterion:** QML review finds `HnSurfaceFrame` or `HnSurfaceRole` bindings for corner styles; no hardcoded `radius: 10` or `cornerRadius: 6` values appear in custom components.

### Palette and Layout Metrics

**REQ-NF-005 (Ubiquitous)**
Spacing, margins, padding, border widths, and other layout metrics shall use `HoloniightPalette` metric tokens (e.g., `controlHeight`, `controlPadding`, `borderWidth`, `separatorWidth`) rather than new numeric literals.

- **Acceptance Criterion:** QML review finds metric bindings like `controlPadding`, `borderWidth`, `controlHeight`; no new numeric spacing constants (e.g., `12`, `8`, `4`) are introduced in the panel code.

**REQ-NF-006 (Ubiquitous)**
The implementation shall remain compatible with the repository's configured build, format, and QML lint workflows.

- **Acceptance Criterion:** Running `task build` and `task qml-lint` completes successfully with no errors or new warnings; QML review finds no formatting violations.

## Constraints

### Outer Frame Geometry

**REQ-C-001 (Ubiquitous)**
The left sidebar panel's outer frame role, directional corner mask, fill color (`surfaceRaised`), and border color (`borderPassive`) shall remain unchanged from the prior cycle; only the panel's `Layout.preferredWidth` increases from 220 to 320 logical pixels. This constraint applies only to the frame's own geometry/color properties — it does not restrict the content nested inside the frame, which REQ-F-003 through REQ-F-020 require to be redesigned.

- **Acceptance Criterion:** In `ConversationListPanel.qml`, the root `HnSurfaceFrame`'s `surfaceRole` property is unchanged; in `WorkspaceWindow.qml`, the `ConversationListPanel` instance's `chamferedCornersOverride`, `fillColor`, `borderColor`, and `borderWidth` bindings are unchanged and only `Layout.preferredWidth` changes from 220 to 320. The `ColumnLayout` (and all items nested inside it) that the frame contains is extensively redesigned per REQ-F-003 through REQ-F-020 — this criterion does not require that content to be unchanged.

### Search and Filter Scope

**REQ-C-002 (Ubiquitous)**
The search filter shall operate on the displayed conversation list only; filtering shall not be persisted, shall not modify the underlying `ConversationListModel`, and shall not create or use a `QSortFilterProxyModel` or other C++ proxy layer.

- **Acceptance Criterion:** Closing and reopening the panel, or clicking "New Chat" to create a new conversation, resets any active search filter to empty; QML review finds a client-side filtered array or model (e.g., JavaScript array, QML-computed filtered list), not a C++ proxy; the underlying `ChatViewModel.conversationList` model is not modified.

### No New Per-Conversation Data

**REQ-C-003 (Ubiquitous)**
This cycle shall not add pinned/starred, error, category, kind, unread-count, or any other per-conversation state or presentation flag beyond the existing `id`, `title`, and `updatedAt` roles provided by `ConversationListModel`.

- **Acceptance Criterion:** QML review finds no new bindings to hypothetical roles like `isPinned`, `hasError`, `kind`, `unreadCount`, or similar; the model is used with its existing three roles only.

### No Row-Level Error or Status Styling

**REQ-C-004 (Ubiquitous)**
Conversation rows shall not display error states, warning icons, status indicators, or styling variations based on message-level errors or transient conversation health; all rows render in an identical neutral style (varying only for selected/hover states).

- **Acceptance Criterion:** QML review finds no conditional icon switching, color binds, or styles based on message errors or conversation status; all rows use the same icon, neutral text color, and layout.

### No New C++ Surface, CMake, or Dependency Changes

**REQ-C-005 (Ubiquitous)**
This cycle shall not introduce new C++ classes, new invokables on `ChatViewModel`, new roles on `ConversationListModel`, new CMake targets, new dependencies, or changes to the build system. The existing `ConversationListModel::UpdatedAtRole` may change its display formatting and emit periodic `dataChanged` notifications so relative labels remain current.

- **Acceptance Criterion:** CMake review finds no new `add_library()` or `add_executable()` calls, no new `find_package()` or `target_link_libraries()`, and no changes to version constraints; QML review finds no calls to new `ChatViewModel` invokables or accesses to new model roles; the implementation uses only the existing `createConversation()`, `switchConversation()`, `renameConversation()`, `deleteConversation()`, and `dismissPersistenceBanner()` invokables, and the existing `id`, `title`, `updatedAt`, and `activeConversationId` properties/roles.

### No Persistence or Domain Changes

**REQ-C-006 (Ubiquitous)**
This cycle shall not modify the conversation persistence schema, add migrations, change conversation message-handling logic, or alter any domain types. Presentation-only formatting and refresh behavior for `ConversationListModel::UpdatedAtRole` is permitted.

- **Acceptance Criterion:** No `.sql` migration files are added; no changes appear in `src/domain/` or `src/persistence/`; `ChatViewModel` behavior is unchanged; and `ConversationListModel` changes are limited to relative-time presentation and refresh notifications.

## Non-goals

- Adding a "Pinned" or "Favorites" section or pinning/starring functionality.
- Displaying per-conversation error states, warning icons, or status badges.
- Varying the leading icon per conversation (e.g., different icons for different conversation types/categories).
- Displaying a user profile card, username, avatar, authentication, or logout behavior.
- Using `HnIcon` or the host system's freedesktop icon theme for any icons.
- Adding panel width resizing, collapsing, breakpoint-driven responsive behavior, or splitter handles.
- Changing conversation model ordering (the C++ layer's recency-based sort is preserved).
- Modifying the panel's outer frame geometry, role, chamfer mask, or border treatment (only width changes).
- Adding new roles or invokables to `ChatViewModel` or `ConversationListModel`.
- Implementing nested conversation hierarchies, conversation tagging, or multi-selection.

## Traceable Acceptance Summary

| Check | Requirements |
| --- | --- |
| Verify panel width is exactly 320 px and workspace minimumWidth is exactly 1000 px; confirm center width at minimum is ≥436 px | REQ-F-001, REQ-F-002 |
| Visually inspect header (logo + "HoloNight AI" title) at panel top | REQ-F-003 |
| Verify "New chat" button is full-width, uses Control role, primary colors, invokes `createConversation()` | REQ-F-004, REQ-F-005 |
| Verify search field shows placeholder, accepts text input, displays magnifier and "/" affordance | REQ-F-006, REQ-F-007 |
| Test "/" keyboard shortcut focuses search field when unfocused | REQ-F-008 |
| Type in search field; verify list updates live, filtering conversations by case-insensitive title substring | REQ-F-009 |
| Verify filtered rows are interactable (click, rename, delete) and "New Chat" works while search is active | REQ-F-010 |
| Verify a leading clock icon and "Recent" label appear above conversation list | REQ-F-011 |
| Visually inspect each row: folder icon, title with ellipsis, and right-aligned relative time on one line | REQ-F-012 |
| Verify all rows use the same consistent leading icon (not varied per row) | REQ-F-013 |
| Hover over non-selected rows; verify surfaceHover background appears and disappears | REQ-F-014 |
| Click a row to make it active; verify surfaceElevated background and 2px left accent bar appear; verify title text uses textPrimary | REQ-F-015 |
| Hover over rows; verify edit and delete action buttons (icon buttons) are revealed only on hover, not at rest | REQ-F-016 |
| Click a conversation row; verify `activeConversationId` changes and chat updates | REQ-F-017 |
| Verify a separator and five footer icons appear, with no profile card | REQ-F-018 |
| Click footer gear button; verify it toggles settings window visibility (same state as center panel's settings button) | REQ-F-019 |
| Click layout, documentation, code, and help footer buttons; verify they have no effect | REQ-F-020 |
| QML code review: verify all icons are inline Shape/ShapePath, no HnIcon, no asset files, no icon font, no hardcoded hex colors | REQ-NF-001, REQ-NF-002 |
| QML code review: verify all fill, text, border colors use HoloniightPalette tokens; no hardcoded hex or RGB | REQ-NF-003 |
| QML code review: verify frame radii come from HnSurfaceRole, not hardcoded literals | REQ-NF-004 |
| QML code review: verify spacing and metrics use HoloniightPalette metric tokens (controlPadding, borderWidth, etc.); no new numeric literals | REQ-NF-005 |
| Run `task build` and `task qml-lint`; verify no errors or new warnings | REQ-NF-006 |
| Verify the frame's `surfaceRole` (in `ConversationListPanel.qml`) and its call-site `chamferedCornersOverride`/`fillColor`/`borderColor`/`borderWidth` (in `WorkspaceWindow.qml`) are unchanged; only `Layout.preferredWidth` (220→320) and `minimumWidth` (900→1000) change | REQ-C-001 |
| Clear search, open/close panel, create new conversation; verify filter state is not persisted; code review finds no QSortFilterProxyModel | REQ-C-002 |
| Code review: verify no new model roles (pinned, error, kind, unread, etc.) are added or accessed | REQ-C-003 |
| Code review: verify no conditional row styling based on error state or per-row status; all rows use neutral icon and colors | REQ-C-004 |
| CMake and header review: verify no new C++ classes, invokables, roles, targets, or dependency changes; relative-time formatting uses the existing role | REQ-C-005 |
| Code review: verify no schema migrations, domain-type changes, or message-handling logic changes | REQ-C-006 |

## Known Risks

The "/" keyboard shortcut to focus the search field competes with any "/" keyboard binding defined globally in the app (e.g., a "/" for quick-search in some other panel). Future cycles may need to refine shortcut priority or scoping. For now, the requirement is scoped to "when focus is not already in the search field"; conflicts are deferred to a future QML event-priority review if they arise.
