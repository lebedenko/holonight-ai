# SDD Tasks — left-sidebar-panel

- [x] T-001: Create InlineIcon.qml
  - REQs: REQ-NF-001
  - Check: InlineIcon.qml exists in qml/workspace/, contains Shape/ShapePath root, accepts required svgPath property and optional iconColor/strokeWidth properties, defaults to 16×16 implicit size

- [x] T-002: Create SidebarIconButton.qml
  - REQs: REQ-NF-001, REQ-NF-002
  - Check: SidebarIconButton.qml exists in qml/workspace/, has HnSurfaceFrame root with surfaceRole Control, embeds InlineIcon, includes HoverHandler and TapHandler, emits clicked() signal, icons use palette-token bindings

- [x] T-003: Update ConversationListDelegate root and styling
  - REQs: REQ-F-014, REQ-F-015, REQ-C-001
  - Check: ConversationListDelegate.qml root is HnSurfaceFrame (not Rectangle) with conditional fillColor: active rows show surfaceElevated, hovered non-active show surfaceHover, other rows transparent

- [x] T-004: Add leading folder icon and relative timestamp to delegate rows
  - REQs: REQ-F-012, REQ-F-013, REQ-F-017, REQ-NF-001, REQ-NF-002
  - Check: Each row displays the same leading folder InlineIcon, an ellipsized title, and compact updatedAt text on one line; clicking invokes ChatViewModel.switchConversation(conversationId)

- [x] T-005: Add selected-row accent bar to delegate
  - REQs: REQ-F-015, REQ-NF-002, REQ-NF-004
  - Check: Active rows display a 2px left Rectangle border with HoloniightPalette.borderActive color; non-active rows have no accent bar

- [x] T-006: Add HoverHandler to delegate
  - REQs: REQ-F-014
  - Check: Passive HoverHandler updates root.hovered property without interfering with existing MouseArea click handling

- [x] T-007: Add edit/delete action buttons to delegate
  - REQs: REQ-F-016, REQ-NF-001, REQ-NF-002
  - Check: Edit and delete SidebarIconButton instances are visible only when row is hovered and not in editing/confirming state; existing rename/delete signal handlers and state machines remain intact

- [x] T-008: Implement search filter logic in delegate
  - REQs: REQ-F-009, REQ-F-010, REQ-C-002
  - Check: Delegate's matchesFilter readonly binding correctly implements case-insensitive substring match against filterText; filtered (non-matching) rows set height to 0; rename/delete/activate work on visible rows

- [x] T-009: Update ConversationListPanel layout and width
  - REQs: REQ-F-001, REQ-C-001
  - Check: ConversationListPanel's Layout.preferredWidth is 320, ColumnLayout added as main structure, root HnSurfaceFrame surfaceRole/fillColor/borderColor remain unchanged

- [x] T-010: Add header to ConversationListPanel
  - REQs: REQ-F-003, REQ-NF-001, REQ-NF-002
  - Check: Panel displays header section with hexagon InlineIcon and dot InlineIcon and "HoloNight AI" text using semantic palette colors (no hex literals)

- [x] T-011: Add "New chat" button to ConversationListPanel
  - REQs: REQ-F-004, REQ-F-005, REQ-NF-001, REQ-NF-002
  - Check: Button spans full width with plus InlineIcon and "New chat" text, invokes ChatViewModel.createConversation() on click, uses HoloniightPalette.primary and onPrimary colors

- [x] T-012: Add search field to ConversationListPanel with "/" shortcut
  - REQs: REQ-F-006, REQ-F-007, REQ-F-008, REQ-NF-001, REQ-NF-002
  - Check: Search field displays magnifier InlineIcon on left and "/" text on right, has "Search conversations" placeholder, accepts text input, "/" Shortcut with enabled: !searchField.activeFocus transfers focus to field without appending "/" to text

- [x] T-013: Add "Recent" section label to ConversationListPanel
  - REQs: REQ-F-011, REQ-NF-001, REQ-NF-002
  - Check: A leading clock InlineIcon followed by "Recent" appears above the ListView using semantic palette colors

- [x] T-014: Add separator and five-icon footer to ConversationListPanel
  - REQs: REQ-F-018, REQ-F-019, REQ-F-020, REQ-NF-001, REQ-NF-002
  - Check: A full-width separator precedes layout, documentation, code, settings, and help SidebarIconButtons; settings emits settingsToggleRequested and the other four have no handlers; no profile card is present

- [x] T-015: Wire search filtering from panel to ListView
  - REQs: REQ-F-009, REQ-C-002
  - Check: ListView.model remains ChatViewModel.conversationList, delegates' filterText property bound to panel's searchText, searchText clears to "" on New Chat, filtering is live and has no effect on model

- [x] T-016: Update WorkspaceWindow.qml dimensions and wiring
  - REQs: REQ-F-002, REQ-F-019
  - Check: WorkspaceWindow.minimumWidth is set to 1000, ConversationListPanel.onSettingsToggleRequested handler toggles settingsWindow.visible, center frame width at minimum is ≥436 pixels

- [x] T-017: Format and refresh compact relative timestamps
  - REQs: REQ-F-012, REQ-C-005, REQ-C-006
  - Check: ConversationListModel's existing updatedAt role returns now/Nm ago/Nh ago/Nd ago and emits UpdatedAtRole dataChanged notifications once per minute without adding roles or invokables

- [x] T-018: Final build and QML lint verification
  - REQs: REQ-NF-006, REQ-NF-003, REQ-NF-005, REQ-C-003, REQ-C-004, REQ-C-005, REQ-C-006
  - Check: task build completes with zero errors, task qml-lint finds zero errors or new warnings, QML review confirms all colors use HoloniightPalette tokens (no hex), all spacing/dimensions use palette metrics, no new per-conversation model roles (pinned/error/kind/unread) or row-level error/status styling are present, no new C++ targets/dependencies/invokables/model roles added
