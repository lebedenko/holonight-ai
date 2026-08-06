# Assistant Response Rendering Boundary — Tasks

Status: Stage 3 (Implementation Plan). Tasks intentionally begin unchecked.

- [ ] **T-001 — Document the ownership boundary and compatibility contract**
  - Requirements: REQ-F-001 through REQ-F-006, REQ-NF-001 through REQ-NF-003, REQ-C-001 through REQ-C-003.
- [ ] **T-002 — Add pre-refactor QML behavior coverage**
  - Requirements: REQ-F-003, REQ-F-004, REQ-F-005, REQ-NF-002.
  - Cover mixed order, terminal copy availability/fidelity, and incomplete literal code.
- [ ] **T-003 — Extract and integrate `AssistantResponseContent`**
  - Requirements: REQ-F-001 through REQ-F-004, REQ-F-002, REQ-C-003.
- [ ] **T-004 — Gate the `ChatCodeBlock` highlighter lifecycle**
  - Requirements: REQ-F-005, REQ-F-006, REQ-NF-001.
- [ ] **T-005 — Add lifecycle regression coverage**
  - Requirements: REQ-F-003, REQ-F-005, REQ-F-006, REQ-NF-002.
  - Cover delegate identity and incomplete-to-complete highlighting.
- [ ] **T-006 — Run focused and repository verification**
  - Requirements: REQ-NF-003.
  - Run focused QML tests, `task qml-lint`, `task format-check`, and `task test`.
- [ ] **T-007 — Perform the manual long-response smoke test**
  - Requirements: REQ-F-003 through REQ-F-006, REQ-NF-001.
  - Exercise streaming completion, copy feedback, selection, scrolling, and theme switching.
