# Incremental Chat Content Rendering — Tasks

Status: Stage 3 (Implementation Plan). All tasks are intentionally unchecked. Tasks are ordered by
dependency and trace back to `SPEC.md`.

- [ ] **T-001 — Add block identity and completion metadata**
  - Requirements: REQ-F-002, REQ-NF-002.
  - Add source-derived `id` and `complete` properties to `ContentBlock`; update factories, equality,
    metatype behavior, and existing rendering tests.
  - Verify: `ctest --test-dir build --output-on-failure -R 'content_block|language_alias'`.

- [ ] **T-002 — Extend parser behavior and parser tests**
  - Requirements: REQ-F-001, REQ-F-002, REQ-F-008, REQ-NF-002, REQ-C-001, REQ-C-002.
  - Accept terminal state, emit deterministic bounded Markdown/Code blocks, handle incomplete
    backtick and tilde fences, and preserve one-block lossless fallback on forced failure.
  - Verify: `ctest --test-dir build --output-on-failure -R message_content_parser` passes tests for
    ordering, IDs, completion, source fidelity, incomplete fences, Unicode, empty input, and fallback.

- [ ] **T-003 — Add the persistent content model**
  - Requirements: REQ-F-003, REQ-F-007, REQ-F-009, REQ-C-003.
  - Implement `MessageContentModel : QAbstractListModel` with `blockId`, `type`, `text`, `language`,
    and `complete` roles plus exact raw-source retention and a QML copy surface.
  - Verify: `ctest --test-dir build --output-on-failure -R message_content_model` confirms role values,
    empty behavior, raw-source fidelity, and stable QObject identity.

- [ ] **T-004 — Add coalescing, diffing, terminal flushing, and model tests**
  - Requirements: REQ-F-004, REQ-F-005, REQ-F-008, REQ-F-009, REQ-NF-001.
  - Implement the non-restarting 50 ms single-shot timer, latest-source publication, longest-stable-
    prefix reconciliation, matching-tail updates, divergent-suffix replacement, and synchronous
    complete/error/cancelled flush.
  - Verify: `ctest --test-dir build --output-on-failure -R message_content_model` observes no publish
    before 50 ms, no timer restart, at most one publish per window, immediate terminal rows, narrow
    Qt model signals, and no stale timeout after termination.

- [ ] **T-005 — Integrate child models into `MessageListModel`**
  - Requirements: REQ-F-003, REQ-F-005, REQ-F-010, REQ-C-003.
  - Replace the finished-only `QVariantList` cache with owned per-assistant child models, preserve the
    `contentBlocks` role name, forward streaming/terminal updates, and construct terminal models for
    restored conversations.
  - Verify: `ctest --test-dir build --output-on-failure -R message_list_model` confirms the same child
    pointer survives updates and complete/error/cancelled/restored rows expose correct content.

- [ ] **T-006 — Rename `HnCodeBlock` to `ChatCodeBlock`**
  - Requirements: REQ-F-012, REQ-C-002.
  - Rename the QML file/type and update QML module sources, usages, tests, comments, and object names;
    make no changes in `holonight-qt`.
  - Verify: `rg -n 'HnCodeBlock' apps qml src tests` returns no matches, and
    `rg -n 'ChatCodeBlock' apps qml tests` finds the registered component and its consumers.

- [ ] **T-007 — Switch assistant QML to streaming block rendering**
  - Requirements: REQ-F-001, REQ-F-010, REQ-F-011, REQ-C-003.
  - Bind the assistant repeater to child-model roles for every non-empty status, eliminate the
    assistant message-wide streaming renderer, retain per-block selection, and leave user messages
    unchanged.
  - Verify: the focused QML test binary (listed by `ctest --test-dir build -N | rg -i qml`) shows prose
    and incomplete fenced code simultaneously during streaming with no renderer swap at completion.

- [ ] **T-008 — Gate highlighting on code-block completion**
  - Requirements: REQ-F-006, REQ-NF-003.
  - Keep incomplete code literal/plain, activate the existing highlighter only when the same block
    becomes complete, retain unknown-language fallback, and preserve live palette refresh.
  - Verify: focused rendering/QML tests observe no active highlighter before completion, activation
    for completed known code, plain text for unknown code, and stable `blockId` across the transition.

- [ ] **T-009 — Add full-response copy and feedback**
  - Requirements: REQ-F-007, REQ-NF-002.
  - Add an assistant-footer copy control backed by the child model's exact raw Markdown, with brief
    success feedback; preserve code-only copy behavior.
  - Verify: focused QML tests compare clipboard text exactly with source containing fences,
    whitespace, Unicode, and special characters and observe the temporary copied state.

- [ ] **T-010 — Cover all terminal and fallback paths**
  - Requirements: REQ-F-005, REQ-F-008, REQ-F-010, REQ-NF-002.
  - Add integration coverage for complete, error, non-empty cancelled, empty terminal, restored, and
    forced parser-failure messages, including pending-timer cancellation.
  - Verify: `ctest --test-dir build --output-on-failure -R 'message_content|message_list_model'` passes
    with no lost source, blank non-empty response, or delayed terminal update.

- [ ] **T-011 — Add QML rendering and clipboard regression tests**
  - Requirements: REQ-F-006, REQ-F-007, REQ-F-010, REQ-F-011, REQ-NF-003.
  - Cover ordered mixed delegates, stable delegate identity, selection, follow/detached scrolling,
    incomplete/unknown code, completion highlighting, full/code copy, feedback, and theme refresh.
  - Verify: run the QML test executable identified by `ctest --test-dir build -N | rg -i qml`; all new
    object-tree, clipboard, selection, scrolling, and palette assertions pass offscreen.

- [ ] **T-012 — Run automated verification**
  - Requirements: REQ-F-001 through REQ-F-012, REQ-NF-001 through REQ-NF-003, REQ-C-001 through
    REQ-C-003.
  - Run the narrow rendering/application/QML tests first, then repository lint, format, and full tests.
  - Verify: `task qml-lint`, `task format-check`, and `task test` all exit successfully; record any
    environment-only blocker exactly rather than checking this task prematurely.

- [ ] **T-013 — Perform the manual long-stream interaction check**
  - Requirements: REQ-F-004, REQ-F-005, REQ-F-006, REQ-F-011, REQ-NF-001, REQ-NF-003.
  - Stream a long response with prose, known/unknown fenced code, and an initially incomplete fence;
    exercise following and detached scrolling, selection, completion, cancellation/error where
    practical, response/code copy, and light/dark theme switching.
  - Verify: observe responsive input, publications no faster than the 50 ms cadence, stable rows and
    selection, immediate terminal content, correct clipboard text, and live completed-code theme
    changes; record the manual result before checking the task.
