# SDD Tasks — three-panel-window-layout

- [x] T-001: Implement the framed three-region workspace layout
  - REQs: REQ-F-001, REQ-F-002, REQ-F-003, REQ-F-004, REQ-F-005, REQ-F-006, REQ-F-008, REQ-F-009, REQ-NF-001, REQ-NF-002, REQ-C-001, REQ-C-002, REQ-C-003
  - Check: Source review confirms `WorkspaceWindow.qml` contains the ordered 220 px left, flexible center, and empty 220 px right frames with the specified roles, masks, palette bindings, containment, and 900 × 480 minimum size, while preserving the 960 × 640 initial size and adding no excluded behavior or dependencies.

- [x] T-002: Run the automated QML validation workflows
  - REQs: REQ-NF-003
  - Check: `task build` and `task qml-lint` both exit successfully.

- [x] T-003: Inspect runtime layout, frame properties, and preserved interactions
  - REQs: REQ-F-001, REQ-F-002, REQ-F-003, REQ-F-004, REQ-F-005, REQ-F-006, REQ-F-007, REQ-F-008, REQ-F-009, REQ-NF-001, REQ-NF-002
  - Check: Runtime inspection at exactly 900 px and 960 px verifies three distinct full-height non-overlapping frames, fixed 220 px side widths, the specified roles, geometry overrides, colors and metrics, an empty right content slot, 960 × 640 initial and 900 × 480 minimum dimensions, working collapse/settings/chat/conversation interactions, and directional side masks that remain 6 and 9 under a changed panel topology.
