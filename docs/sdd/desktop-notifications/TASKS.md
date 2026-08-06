# SDD Tasks — desktop-notifications

- [x] T-001: Implement `holonight_platform::DesktopNotifier` class and `resolveNotificationSummary()` function
  - REQs: REQ-F-001, REQ-F-002, REQ-F-003, REQ-F-004, REQ-F-005, REQ-F-006, REQ-NF-002, REQ-NF-003, REQ-C-002, REQ-C-003
  - Check: `resolveNotificationSummary()` returns the input title unchanged if non-empty, else returns "New response"; `DesktopNotifier::notify(summary, body)` issues one async `QDBusInterface::asyncCall("Notify", ...)` with all eight protocol arguments and never throws, blocks, or retries; daemon signals are filtered by the `org.freedesktop.Notifications` service; each returned ID is tracked until its default action or `NotificationClosed`.

- [x] T-002: Add `responseReady` and `requestFailed` signals to `ChatViewModel`
  - REQs: REQ-F-001, REQ-F-004, REQ-F-010
  - Check: `ChatViewModel::onStreamEvent()` emits `responseReady(conversation_->title())` exactly once per Completed event, emits `requestFailed(conversation_->title(), message)` exactly once per Error event, and emits neither signal on Cancelled events.

- [x] T-003: Wire `DesktopNotifier`, visibility gate, and click handler in `ChatApplication`
  - REQs: REQ-F-001, REQ-F-004, REQ-F-007, REQ-F-008, REQ-NF-001, REQ-C-001
  - Check: `ChatApplication` constructs `desktop_notifier_` in the primary-instance branch; when `ChatViewModel::responseReady` or `requestFailed` is emitted, the connected lambda evaluates `!workspaceVisible() && !panelVisible()` synchronously and calls `desktop_notifier_->notify(resolveNotificationSummary(title), body)` only if both surfaces are hidden; `DesktopNotifier::activated` is connected directly to `ChatApplication::ShowWorkspace()` via direct in-process connect.

- [x] T-004: Implement unit tests for `resolveNotificationSummary()` policy
  - REQs: REQ-F-002, REQ-F-005
  - Check: `tests/platform/test_desktop_notifier_policy.cpp` passes: empty title input yields "New response"; non-empty title is returned unchanged.

- [x] T-005: Extend `ChatViewModel` tests with Completed/Error/Cancelled signal verification
  - REQs: REQ-F-001, REQ-F-004, REQ-F-010
  - Check: `tests/application/test_chat_view_model.cpp` QSignalSpy cases confirm: Completed event emits `responseReady` once with current title; Error event emits `requestFailed` once with title and message verbatim; Cancelled event emits neither; both new signals fire independently of existing `isStreamingChanged`/`errorMessageChanged`.

- [x] T-006: Run automated validation workflows
  - REQs: REQ-F-002, REQ-F-005, REQ-F-010
  - Check: `task build` configures and compiles without warnings or errors; `task test` passes all GTest cases including new `test_desktop_notifier_policy.cpp` and extended `test_chat_view_model.cpp`; CMake file list in `src/platform/CMakeLists.txt` includes `desktop_notifier.h` and `desktop_notifier.cpp`.

- [ ] T-007: Run live compositor smoke test for end-to-end notification scenarios
  - REQs: REQ-F-001, REQ-F-002, REQ-F-003, REQ-F-004, REQ-F-005, REQ-F-006, REQ-F-007, REQ-F-008, REQ-NF-002, REQ-NF-004, REQ-C-001
  - Check: Execute the seven Acceptance Check scenarios from SPEC.md (scenarios 1–7): (1) Response ready with both surfaces hidden shows notification with conversation title and "Response ready" body, click raises workspace; (2) Response ready with workspace visible—no notification; (3) Response ready with quick panel open—no notification; (4) Error with both surfaces hidden shows notification with title and error message body, click raises workspace; (5) Error with workspace visible—no notification; (6) No notification daemon available—app does not crash, log, or show error dialog; (7) Background conversation isolation—only adopted conversation's events trigger notifications.
