# Desktop Notifications Specification

## Objective

Enable holonight-chat to notify the user when a chat response completes or a request fails, but only when the application surfaces are not visible on any display. Notifications shall provide minimal, non-intrusive feedback (response-ready confirmation or error details) and shall allow the user to return to the conversation window via notification interaction.

## Constraints

- The feature shall use the standard `org.freedesktop.Notifications` D-Bus interface (freedesktop.org Desktop Notifications Specification).
- The feature shall NOT use the application's existing `org.holonight.Chat1` D-Bus interface (reserved for panel lifecycle, unrelated to notifications).
- The feature shall fail silently if no notification daemon is available on the session bus.
- The feature shall not surface any error dialogs, logs, or user-facing messages if notification delivery fails.
- Only events from the currently-adopted conversation (the conversation open in `ChatViewModel`) shall trigger notifications.
- Notifications shall be suppressed (not sent) if either the workspace window or quick panel is visible on any output at the moment the trigger event occurs.

## Requirements

### Trigger: Completed Response

**REQ-F-001**: When a `holonight_domain::Completed` stream event is received for the currently-adopted conversation, and the visibility gate permits (workspace window not visible AND quick panel not open), the system shall send a "response ready" notification to the notification daemon.

- **Acceptance criterion**: A test harness that emits a Completed event while both surfaces are hidden shall verify that a D-Bus Notify() call is issued with the conversation title as summary and "Response ready" as body.

**REQ-F-002**: The "response ready" notification's summary shall be the conversation's display title; if the conversation has no title or title is empty, the summary shall be a fallback label such as "New response".

- **Acceptance criterion**: Inspecting the D-Bus Notify() call arguments shall confirm the summary equals the conversation's title field, or the hardcoded fallback label if title is empty.

**REQ-F-003**: The "response ready" notification's body shall be a generic message "Response ready" (or similar constant), without any excerpt or preview of the actual AI response text.

- **Acceptance criterion**: The D-Bus Notify() call's body argument shall not contain any text from `ContentDelta` events or the `Completed` event payload; body shall be exactly the generic constant.

### Trigger: Request Error

**REQ-F-004**: When a `holonight_domain::Error` stream event is received for the currently-adopted conversation, and the visibility gate permits (workspace window not visible AND quick panel not open), the system shall send a "request failed" notification to the notification daemon.

- **Acceptance criterion**: A test harness emitting an Error event while both surfaces are hidden shall verify that a D-Bus Notify() call is issued.

**REQ-F-005**: The "request failed" notification's summary shall be the conversation's display title; if the conversation has no title or title is empty, the summary shall be the same fallback label used for completed responses.

- **Acceptance criterion**: Inspecting the D-Bus Notify() call arguments shall confirm the summary matches the conversation's title field or the fallback label, identical behavior to REQ-F-002.

**REQ-F-006**: The "request failed" notification's body shall be the error message text from the `holonight_domain::Error::message` field.

- **Acceptance criterion**: The D-Bus Notify() call's body argument shall equal the Error event's message string verbatim.

### Visibility Gate

**REQ-F-007**: A notification shall fire if and only if, at the moment the trigger event (`Completed` or `Error`) is received, both conditions are true: the workspace window is not visible (ChatApplication::workspaceVisible() returns false) AND the quick panel is not open (ChatApplication::panelVisible() returns false).

- **Acceptance criterion**: Test three cases: (1) emit Completed while only workspace is visible—no notification sent; (2) emit Completed while only quick panel is open—no notification sent; (3) emit Completed while both are hidden—notification sent. All three cases must behave as specified.

**REQ-NF-001**: The visibility gate shall be evaluated synchronously when the stream event is consumed in `ChatViewModel::onStreamEvent()`; no asynchronous polling, buffering, or deferred evaluation of visibility shall occur.

- **Acceptance criterion**: Code review shall confirm that the visibility check occurs immediately upon stream event receipt, before any notification-send logic.

### Notification Interaction

**REQ-F-008**: When a user clicks a notification, the system shall invoke `ChatApplication::ShowWorkspace()` to raise the workspace window.

- **Acceptance criterion**: Manual verification: click a notification; confirm the workspace window becomes visible and focused via the existing ShowWorkspace() code path.

**REQ-C-001**: The mechanism to invoke `ShowWorkspace()` from a notification click shall be a direct in-process method call, not a separate D-Bus round-trip to the application.

- **Acceptance criterion**: Inspecting the notifier implementation shall confirm that the click handler directly calls `ChatApplication::ShowWorkspace()` (e.g., via a connected signal or callback), not via D-Bus.

### Robustness & Failure Handling

**REQ-NF-002**: If a D-Bus Notify() call fails, times out, or the notification daemon is unavailable, the system shall catch the exception or error, suppress any error output, and continue operating normally.

- **Acceptance criterion**: Kill or disable the system notification daemon; hide both surfaces; send a prompt and wait for completion. Verify: the app does not crash, hang, display an error dialog, log to stderr, or emit any user-facing message; the chat message appears in the conversation history normally.

**REQ-NF-003**: The system shall not retry or queue failed notifications; a single, non-blocking attempt per trigger event is sufficient.

- **Acceptance criterion**: Code review shall confirm there are no retry loops, timeouts, or queuing mechanisms in the notifier; each stream event results in at most one D-Bus Notify() call attempt.

**REQ-NF-004**: Every successfully-created notification shall remain independently actionable while it is live. The system shall track each ID returned by `Notify()` and shall discard that ID after its default action is invoked or the daemon emits `NotificationClosed`.

- **Acceptance criterion**: Code review shall confirm that notification IDs are stored as a set rather than a single "last ID"; clicking any tracked notification invokes `ShowWorkspace()`; closed or already-activated IDs are ignored.

### Notification Scope & Excluded Triggers

**REQ-F-009**: Notifications shall only be sent for stream events from the conversation currently adopted into `ChatViewModel` (the conversation with an active chat session in the UI). Events from other conversations in `ChatController`'s concurrent streams shall not trigger notifications.

- **Acceptance criterion**: Test harness with two concurrent conversations: one adopted in ChatViewModel, one streaming in the background. Emit Completed events for both while surfaces are hidden. Verify: only the adopted conversation's event triggers a notification.

**REQ-F-010**: The following event types and conditions are explicitly excluded from notification scope: (1) `holonight_domain::Cancelled` stream events; (2) repository/persistence errors (e.g., `ChatViewModel::onRepositoryError`, SQLite failures); (3) credential-missing-at-startup conditions; (4) background (non-adopted) conversations completing or erroring.

- **Acceptance criterion**: Code review shall confirm that the notifier's trigger conditions check only for `Completed` and `Error` event types from the adopted conversation, and do not handle Cancelled, repository errors, or credential scenarios.

### D-Bus Mechanics

**REQ-C-002**: The system shall use `QDBusConnection::sessionBus()` and `QDBusInterface` (or equivalent Qt D-Bus bindings) to invoke the `org.freedesktop.Notifications` D-Bus interface's Notify() method.

- **Acceptance criterion**: Inspecting the notifier implementation shall confirm Qt D-Bus APIs are used (not raw sockets, custom serialization, or third-party libraries).

**REQ-C-003**: The D-Bus `Notify()` method call shall supply all eight positional arguments required by the protocol. Optional values may be represented by `0`, empty strings/collections, or `-1` as defined by the freedesktop specification.

- **Acceptance criterion**: The `Notify()` call shall supply, in order, app name, replacement ID, app icon, summary, body, actions, hints, and expiry timeout; summary and body shall have the values required above.

## Non-goals

- No per-conversation mute or global mute/settings toggle.
- No credential-missing-at-startup notifications.
- No notifications for repository or persistence errors (e.g., SQLite write failures).
- No notifications for background (non-adopted) conversations completing or erroring.
- No response-text preview or excerpt in "response ready" notification bodies.
- No focused-monitor or focused-output detection; the gate is simply "any surface visible anywhere."
- No new Wayland protocol integrations.
- No persistent notification history or replay.
- No notification categorization, sound effects, or urgency hints.

## Acceptance Checks

### End-to-End Scenarios

1. **Response Ready, Surfaces Hidden**
   - [ ] Open a conversation in the workspace window; send a prompt.
   - [ ] Hide the workspace window and close the quick panel.
   - [ ] Wait for the response to complete.
   - [ ] Verify: a desktop notification appears with the conversation's title as summary and "Response ready" as body.
   - [ ] Click the notification; verify the workspace window is raised and visible.

2. **Response Ready, Workspace Visible**
   - [ ] Keep the workspace window visible.
   - [ ] Send a prompt; wait for completion.
   - [ ] Verify: no notification appears (suppressed by visibility gate).

3. **Response Ready, Quick Panel Visible**
   - [ ] Open the quick panel on any output.
   - [ ] Send a prompt to the adopted conversation; wait for completion.
   - [ ] Verify: no notification appears (suppressed by visibility gate).

4. **Request Error, Surfaces Hidden**
   - [ ] Open a conversation.
   - [ ] Hide both the workspace window and quick panel.
   - [ ] Send a prompt to a provider configured to fail (e.g., invalid API key, unreachable endpoint).
   - [ ] Wait for the Error event.
   - [ ] Verify: a desktop notification appears with the conversation's title as summary and the error message from Error::message as body.
   - [ ] Click the notification; verify the workspace window is raised.

5. **Request Error, Surfaces Visible**
   - [ ] Keep the workspace window visible.
   - [ ] Send a prompt to a failing provider.
   - [ ] Verify: no notification appears (suppressed by visibility gate).

6. **No Notification Daemon Available**
   - [ ] Stop or disable the system notification daemon (e.g., `systemctl --user stop notification-daemon` or similar).
   - [ ] Hide both surfaces.
   - [ ] Send a prompt; wait for completion or error.
   - [ ] Verify: the application does not crash, hang, display an error dialog, or log any error message to the user.
   - [ ] Verify: the chat message appears normally in the conversation history.

7. **Background Conversation Isolation**
   - [ ] Open Conversation A in the workspace window (adopted).
   - [ ] Programmatically stream a prompt in Conversation B in the background (not visible in UI).
   - [ ] Keep the workspace window visible.
   - [ ] Wait for Conversation B to complete or error.
   - [ ] Verify: no notification fires (background conversation is out of scope).
   - [ ] Hide both surfaces; programmatically stream another prompt to Conversation B.
   - [ ] Verify: still no notification (Conversation B remains non-adopted).
   - [ ] Switch the UI to Conversation B (now adopted); hide surfaces; send a prompt; wait for completion.
   - [ ] Verify: now a notification fires (Conversation B is now the adopted conversation).
