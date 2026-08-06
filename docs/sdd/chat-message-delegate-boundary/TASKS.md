# Chat Message Delegate Boundary — Tasks

Status: Complete

- [x] **T-001 — Add pre-refactor routing coverage**
  - Requirements: REQ-F-002, REQ-F-003, REQ-NF-002, REQ-C-004.
  - Check: focused QML tests demonstrate the current user, assistant, system-fallback, and
    non-null-tool precedence behavior before extraction.
- [x] **T-002 — Extract `ChatMessageDelegate`**
  - Requirements: REQ-F-001 through REQ-F-005, REQ-NF-001, REQ-NF-002, REQ-C-001 through
    REQ-C-004.
  - Check: `qml/shared/ChatMessageDelegate.qml` contains the unchanged role contract, routing,
    loader lifecycle, three card components, card bindings, and forwarding signals; run
    `task qml-lint`.
- [x] **T-003 — Integrate the delegate with `MessageList`**
  - Requirements: REQ-F-001, REQ-F-005, REQ-F-006, REQ-NF-001.
  - Check: `MessageList` uses `ChatMessageDelegate`, still calls `ChatViewModel.stop()`, and
    preserves the outer delegate's top around expansion; run the focused delegate and
    bottom-anchor tests.
- [x] **T-004 — Add delegate lifecycle and signal tests**
  - Requirements: REQ-F-002 through REQ-F-005, REQ-NF-001, REQ-NF-002.
  - Check: tests verify every card's property forwarding, null-tool routing, stable loaded-item
    identity across ordinary updates, zero/positive-width loader behavior, implicit-height
    propagation, tool-use-ID forwarding, and expansion signals.
- [x] **T-005 — Run focused automated verification**
  - Requirements: REQ-F-003 through REQ-F-006, REQ-NF-001 through REQ-NF-003.
  - Check: run `QT_QPA_PLATFORM=offscreen build/tests/test_holonight_ai
    --gtest_filter='ChatMessageDelegateQml.*:AssistantResponseContentQml.*:ToolActivityCardQml.*:BottomAnchoredListView.*'`.
- [x] **T-006 — Run repository verification**
  - Requirements: REQ-NF-003.
  - Check: `task qml-lint`, `task format-check`, and `task test` all pass.
- [x] **T-007 — Perform a manual transcript smoke test**
  - Requirements: REQ-F-003 through REQ-F-006, REQ-NF-001.
  - Check: in `task run`, exercise user, assistant, and tool rows; streaming updates; tool
    expansion and stop; following-latest behavior; and detached scrolling, observing no route,
    flicker, lifecycle, or scroll-position regression.
  - Outcome: manually verified by the user.
