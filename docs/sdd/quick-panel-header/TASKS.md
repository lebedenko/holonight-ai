# SDD Tasks — quick-panel-header

- [x] T-001: Scaffold QuickPanelHeader.qml with RowLayout structure and left side
  - REQs: REQ-F-001, REQ-F-002, REQ-F-003, REQ-NF-001, REQ-NF-002, REQ-NF-004
  - Check: QuickPanelHeader.qml exists with RowLayout, pragma ComponentBehavior: Bound at top, left side displays palette-colored HnIcon and bold "Quick chat" text with no hardcoded colors

- [x] T-002: Add four HeaderAction buttons with accessibility labels
  - REQs: REQ-F-004, REQ-NF-003
  - Check: Right-aligned container holds exactly four HeaderAction buttons with unique qsTr() Accessible.name strings for dropdown/workspace/pin/close

- [x] T-003: Wire all four buttons to their respective methods and states
  - REQs: REQ-F-005, REQ-F-016, REQ-F-017, REQ-F-018, REQ-F-019, REQ-F-020, REQ-C-002, REQ-C-003
  - Check: Dropdown trigger toggles Popup, workspace button calls ChatApplication.ShowWorkspace(), pin button has enabled: false with accessible text indicating "not yet implemented", close button calls ChatApplication.ClosePanel(false)

- [x] T-004: Create dropdown Popup with HnSurfaceFrame styling and dismissal policies
  - REQs: REQ-F-005, REQ-F-011, REQ-F-012, REQ-NF-001
  - Check: Popup renders below trigger button with HnSurfaceFrame background using HoloniightPalette tokens; closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside configured; readonly property alias dropdownOpen: dropdownPopup.visible exposed

- [x] T-005: Add "New chat" item to dropdown and wire createConversation call
  - REQs: REQ-F-006
  - Check: First dropdown item displays "New chat" via qsTr(); clicking it invokes ChatViewModel.createConversation() and closes dropdown without closing panel

- [x] T-006: Add separator line below "New chat"
  - REQs: REQ-F-007, REQ-F-013
  - Check: Horizontal 1px Rectangle separator visible between "New chat" and conversation list when dropdown is open (visible even if conversation list is empty)

- [x] T-007: Bind ListView to conversationList with inline delegate and 10-item cap
  - REQs: REQ-F-008, REQ-F-014, REQ-F-015
  - Check: ListView displays exactly min(10, conversationList.rowCount()) conversation rows; delegate uses index < 10 visibility gate; model updates refresh list dynamically without closing dropdown

- [x] T-008: Implement conversation row content with title text and switchConversation handler
  - REQs: REQ-F-009
  - Check: Each row displays TitleRole text; clicking invokes ChatViewModel.switchConversation(idRole) and closes dropdown; quick panel remains open

- [x] T-009: Add active conversation highlight styling to delegate rows
  - REQs: REQ-F-010
  - Check: Row whose idRole matches ChatViewModel.activeConversationId shows surfaceElevated background fill and left-edge borderActive accent bar; highlight updates dynamically when activeConversationId changes

- [x] T-010: Integrate QuickPanelHeader into QuickPanel.qml, replacing old button row
  - REQs: REQ-C-004, REQ-F-015
  - Check: QuickPanel.qml ColumnLayout has QuickPanelHeader as first child; old RowLayout with "⤢" button removed; ChatHeader.qml remains unchanged and positioned directly below new header

- [x] T-011: Add !dropdownOpen guard to QuickPanel.qml Escape handler
  - REQs: REQ-F-012
  - Check: Keys.onEscapePressed checks !quickPanelHeader.dropdownOpen before calling ChatApplication.ClosePanel(true); pressing Escape with dropdown open closes dropdown only, not the panel

- [x] T-012: Run task qml-lint and task build verification
  - REQs: REQ-C-001, REQ-C-005, REQ-C-006, REQ-C-007, REQ-C-008, REQ-NF-001, REQ-NF-002
  - Check: task qml-lint completes without errors; task build succeeds; git diff src/ shows no changes; all colors in QuickPanelHeader.qml and modified QuickPanel.qml use HoloniightPalette tokens only; manual visual/interactive verification checklist (Popup z-ordering in layer-shell, Escape priority, active highlight visibility) documented for user verification
