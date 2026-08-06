# SDD Tasks — workspace-visual-hierarchy-polish

**Spec:** `docs/sdd/workspace-visual-hierarchy-polish/SPEC.md`
**Design:** `docs/sdd/workspace-visual-hierarchy-polish/DESIGN.md`
**Status:** Implementation in progress

- [x] T-001: Add the read-only workspace inspector
  - REQs: REQ-F-011, REQ-F-012, REQ-F-013, REQ-F-014, REQ-NF-001, REQ-NF-002, REQ-C-002
  - Files: `qml/workspace/WorkspaceInspectorPanel.qml`
  - Check: The panel displays live selected-model state plus truthful context and attachment empty
    states, contains no interactive item, and constrains long values to its width.

- [x] T-002: Integrate the inspector without changing the three-column contract
  - Depends on: T-001
  - REQs: REQ-F-001, REQ-F-002, REQ-F-016, REQ-NF-003, REQ-C-001
  - Files: `qml/workspace/WorkspaceWindow.qml`
  - Check: The new panel replaces the empty right frame while retaining its 220 px width, role,
    directional corner mask, fill, border, and layout position.

- [x] T-003: Rebalance the center toolbar around workspace and model context
  - REQs: REQ-F-008, REQ-F-009, REQ-F-010, REQ-NF-002, REQ-C-004
  - Files: `qml/workspace/WorkspaceWindow.qml`
  - Check: The toolbar contains `holonight-ai`, one functional `ModelPicker`, collapse, and settings;
    the nested workspace picker is hidden and the quick-panel policy is unchanged.

- [x] T-004: Increase conversation-row breathing room
  - REQs: REQ-F-006, REQ-F-007, REQ-NF-001, REQ-C-004
  - Files: `qml/workspace/ConversationListDelegate.qml`
  - Check: The row height uses `controlHeight + controlPadding`; timestamp/title semantics and all
    row interactions remain unchanged.

- [x] T-005: Strengthen user/assistant message hierarchy
  - REQs: REQ-F-003, REQ-F-004, REQ-F-005, REQ-F-015, REQ-NF-001, REQ-NF-002
  - Files: `qml/shared/UserMessageCard.qml`, `qml/shared/MessageList.qml`
  - Check: User cards are right-aligned, width-limited by the existing ratio, use an emphasized
    semantic fill, retain the avatar, and draw no border; assistant messages remain left-aligned
    on the quieter borderless surface.

- [x] T-006: Perform source-level scope and accessibility review
  - Depends on: T-001, T-002, T-003, T-004, T-005
  - REQs: REQ-F-014, REQ-F-016, REQ-NF-001, REQ-NF-002, REQ-C-001, REQ-C-002, REQ-C-003, REQ-C-004
  - Check: The diff contains no literal styling constants, fake metrics, new backend/API surface,
    background effects, dependency changes, or unintended focusable inspector content.

- [x] T-007: Run focused static verification
  - Depends on: T-006
  - REQs: REQ-NF-004
  - Check: `task qml-lint` and `task build` complete successfully with no new feature diagnostics.

- [x] T-008: Run regression tests
  - Depends on: T-007
  - REQs: REQ-C-004
  - Check: `task test` completes successfully; any pre-existing failure is recorded separately and
    distinguished from this change.

- [ ] T-009: Manually validate hierarchy and behavior
  - Depends on: T-008
  - REQs: REQ-F-001–REQ-F-016, REQ-NF-003, REQ-C-001, REQ-C-004
  - Check: At normal size and exactly 1000 × 480, inspect mixed-role messages, long content, the
    toolbar, inspector updates, keyboard focus, conversation actions, send/stop/retry, collapse,
    settings, and the quick-panel picker; record results before marking the feature implemented.
