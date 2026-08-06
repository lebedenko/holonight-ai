# DESIGN: Desktop notifications

**Spec:** `docs/sdd/desktop-notifications/SPEC.md`
**Status:** Draft
**Date:** 2026-07-25

## Overview

This cycle adds one new class, `holonight_platform::DesktopNotifier`, that wraps the standard
`org.freedesktop.Notifications` D-Bus interface — the same "platform integration, app-owned,
wired via signals" shape already established by `holonight_platform::PanelSurface`
(`docs/sdd/quick-panel-window/DESIGN.md`). `ChatApplication` owns the notifier alongside the
workspace window and `PanelSurface`, evaluates the visibility gate, and connects notification
clicks straight to its existing `ShowWorkspace()` slot.

The trigger side is a small, surgical change to `ChatViewModel::onStreamEvent()` (already the
single synchronous point where `Completed`/`Error` events for the adopted conversation arrive): two
new one-shot signals, `responseReady(title)` and `requestFailed(title, message)`, are emitted
directly from the existing `Error`/new `Completed` branches. `ChatViewModel` has no notion of
D-Bus, visibility, or notification wording — it only reports "this adopted conversation just
finished/failed" with the raw title and error text. `ChatApplication` (which already aggregates
`workspaceVisible()`/`panelVisible()` and owns `ShowWorkspace()`) is the sole place that decides
whether to actually call the notifier and what strings to send. This keeps the module layering
intact: `holonight_application` never depends on `holonight_platform` or on the executable-layer
`ChatApplication`.

No QML surface, no new CMake link edges (Qt6::DBus is already `PUBLIC`-linked on
`holonight_platform` — see Key Decision 3), and no changes to the unrelated
`org.holonight.Chat1` service/adaptor.

## Components

### `holonight_platform::DesktopNotifier` (new)

`src/platform/include/holonight_platform/desktop_notifier.h` / `src/platform/src/desktop_notifier.cpp`.

Owns a single, long-lived `QDBusInterface` bound to `org.freedesktop.Notifications` and a set of
live notification IDs. Tracking every ID is necessary because a notification can remain live after
the workspace is reopened and a later request can produce another notification (REQ-NF-004).

```cpp
namespace holonight_platform {

// Fallback summary used when a conversation's title is empty (REQ-F-002/REQ-F-005).
[[nodiscard]] QString resolveNotificationSummary(const QString& title);

class DesktopNotifier : public QObject {
  Q_OBJECT

 public:
  explicit DesktopNotifier(QObject* parent = nullptr);
  ~DesktopNotifier() override;

  DesktopNotifier(const DesktopNotifier&) = delete;
  DesktopNotifier& operator=(const DesktopNotifier&) = delete;
  DesktopNotifier(DesktopNotifier&&) = delete;
  DesktopNotifier& operator=(DesktopNotifier&&) = delete;

  // Fire-and-forget: issues one Notify() call. Never throws, never blocks, never retries
  // (REQ-NF-002/REQ-NF-003). Safe to call even if no daemon is present.
  void notify(const QString& summary, const QString& body);

 Q_SIGNALS:
  // Emitted when the user clicks/activates the most recently sent notification. Connected
  // in-process to ChatApplication::ShowWorkspace() (REQ-C-001) — never a D-Bus round-trip.
  void activated();

 private Q_SLOTS:
  void handleNotifyFinished(QDBusPendingCallWatcher* watcher);
  void handleActionInvoked(uint id, const QString& actionKey);
  void handleNotificationClosed(uint id, uint reason);

 private:
  QDBusInterface interface_;
  QSet<uint> live_notification_ids_;
};

}  // namespace holonight_platform
```

`resolveNotificationSummary()` is a free function (mirrors `PanelSurface`'s
`preferredPanelWidth()` precedent) so the fallback rule is unit-testable without touching D-Bus:

```cpp
QString resolveNotificationSummary(const QString& title) {
  return title.isEmpty() ? QStringLiteral("New response") : title;
}
```

### `ChatApplication` (changed)

Gains a `std::unique_ptr<holonight_platform::DesktopNotifier> desktop_notifier_;` member,
constructed in the primary-instance branch alongside `panel_surface_`. Wires:

1. `DesktopNotifier::activated` → `ChatApplication::ShowWorkspace` (direct connect, REQ-C-001).
2. The `ChatViewModel` singleton's new `responseReady`/`requestFailed` signals → an inline gate
   check + `desktop_notifier_->notify(...)` call (REQ-F-001/F-004, REQ-NF-001).

`ChatApplication` already fetches the `ChatViewModel` singleton via
`engine_->singletonInstance<...>("HolonightChat", "ChatViewModel")` in its `aboutToQuit` handler —
the same call (which lazily creates the singleton if not already instantiated) is reused here.

### `ChatViewModel` (changed)

Two new one-shot `Q_SIGNALS`, emitted from `onStreamEvent()`:

```cpp
Q_SIGNALS:
  ...
  void responseReady(const QString& title);
  void requestFailed(const QString& title, const QString& message);
```

No new state, no new properties — these are pure event signals, not derived-state properties like
`isStreaming`/`errorMessage`. See Key Decision 1 for why existing properties are insufficient.

## Data flow

```text
ChatController::handleStreamEvent()
  → on_event callback (bound in ChatViewModel::send()/regenerate() to conversation_)
    → ChatViewModel::onStreamEvent(event)
        [Error]     setErrorMessage(msg); emit requestFailed(conversation_->title(), msg)
        [Completed] emit responseReady(conversation_->title())
      (direct Qt connection, same thread — synchronous, no queuing)
        → ChatApplication's connected lambda
            if (workspaceVisible() || panelVisible()) return;   // REQ-F-007 gate
            summary = resolveNotificationSummary(title);         // REQ-F-002/F-005
            body    = "Response ready"  |  message                // REQ-F-003/F-006
            desktop_notifier_->notify(summary, body)
              → QDBusInterface::asyncCall("Notify", ...)
                → org.freedesktop.Notifications daemon
                  (user clicks the notification)
                → daemon emits ActionInvoked(id, "default")
              → DesktopNotifier::handleActionInvoked(id, key)
                  if (live_notification_ids_.remove(id) && key == "default") emit activated()
                    → ChatApplication::ShowWorkspace()   // REQ-F-008/REQ-C-001, in-process
                → daemon emits NotificationClosed(id, reason)
              → DesktopNotifier removes id from live_notification_ids_
```

`Cancelled` events and repository/persistence errors never reach this path at all: `onStreamEvent()`
only branches on `Error`/`Completed`, so `Cancelled` falls through both arms untouched (REQ-F-010),
and repository errors are reported through the unrelated `onRepositoryError()` handler, which this
change does not touch.

**Why "adopted conversation only" needs no explicit ID check (REQ-F-009):** `onStreamEvent()` is
only ever invoked via the `on_event` callback that `ChatViewModel::send()`/`regenerate()` bind
against `conversation_` — the conversation currently adopted into the view model. A conversation
streaming through `ChatController` directly (bypassing `ChatViewModel`, as REQ-F-009's acceptance
test harness does) never has a callback wired to `ChatViewModel::onStreamEvent()` at all, so it
structurally cannot reach `responseReady`/`requestFailed`. Scoping falls out of the existing
callback-wiring architecture rather than needing a new conversation-ID comparison.

## Interfaces / APIs

### D-Bus: `org.freedesktop.Notifications.Notify`

```
UINT32 Notify(STRING app_name, UINT32 replaces_id, STRING app_icon,
              STRING summary, STRING body, ARRAY actions,
              DICT hints, INT32 expire_timeout)
```

| Argument | Value | Rationale |
| --- | --- | --- |
| `app_name` | `"holonight-chat"` | Matches `QGuiApplication::applicationName()` set in `ChatApplication`'s constructor. |
| `replaces_id` | `0` | Always create a new notification. Multiple notifications may remain live and their returned IDs are tracked independently. |
| `app_icon` | `""` | No app icon asset exists in this repo today (checked: no `.desktop` file, no icon under `data/`); optional per REQ-C-003. |
| `summary` | `resolveNotificationSummary(title)` | REQ-F-002/F-005. |
| `body` | `"Response ready"` (Completed) or `Error::message` verbatim (Error) | REQ-F-003/F-006. |
| `actions` | `{"default", ""}` | The freedesktop convention for "clicking the notification body itself" (not a labeled button) is an action with key `"default"`; most daemons (GNOME Shell, KDE Plasma, dunst, mako) dispatch `ActionInvoked(id, "default")` on body-click when this key is present, and quietly ignore it as a button since the label is empty. |
| `hints` | `{}` (empty) | No urgency/category/sound needed (SPEC non-goals). |
| `expire_timeout` | `-1` | Server default; no requirement to control on-screen duration. |

Issued via `interface_.asyncCall(QStringLiteral("Notify"), ...)` — never a blocking call, so a slow
or hung daemon cannot stall the chat UI thread (supports REQ-NF-002/REQ-NF-003).

### D-Bus: `org.freedesktop.Notifications.ActionInvoked` (signal, subscribed)

```
ActionInvoked(UINT32 id, STRING action_key)
```

Connected once in `DesktopNotifier`'s constructor via
`QDBusConnection::sessionBus().connect("org.freedesktop.Notifications", "/org/freedesktop/Notifications",
"org.freedesktop.Notifications", "ActionInvoked", this, SLOT(handleActionInvoked(uint,QString)))`
— the well-known service name scopes delivery to the notification daemon rather than accepting a
matching signal from any session-bus participant. `handleActionInvoked` only emits `activated()`
when `id` is present in `live_notification_ids_` and `action_key == "default"`, then removes the ID.
The constructor also subscribes to the daemon's `NotificationClosed` signal and removes the closed
ID, keeping the set bounded and preventing stale IDs from matching after their notification is gone.

### New `ChatViewModel` signals

```cpp
void responseReady(const QString& title);
void requestFailed(const QString& title, const QString& message);
```

`title` is `conversation_->title()`, unresolved (may in principle be empty; see Known Risks) —
fallback substitution happens in `ChatApplication`, not here (Key Decision 2).

## Key decisions

### 1. New dedicated signals on `ChatViewModel`, not the existing `isStreamingChanged`/`errorMessageChanged`

`isStreamingChanged`/`errorMessageChanged` are *derived-state* notifications: `isStreaming` flips
`true→false` on every terminal transition (`Completed`, `Error`, *and* `Cancelled` all end
streaming), and `errorMessageChanged` only fires for the `Error` branch and is also reset to empty
on the next `send()`/`regenerate()` — neither carries enough information to distinguish "just
completed" from "just cancelled," and reusing them would force `ChatApplication` to inspect
`conversation()->messages().back().status()` after the fact to reconstruct what actually happened,
duplicating the switch `onStreamEvent()` already performs. `Cancelled` is explicitly out of scope
(REQ-F-010); a listener on `isStreamingChanged` alone cannot tell it apart from `Completed` without
re-deriving state that already existed, transiently, inside `onStreamEvent()`. Two dedicated,
one-shot signals fired directly from the point of dispatch are unambiguous by construction and
need no downstream re-derivation.

### 2. Trigger detection and visibility gate both live in `ChatApplication`, not `ChatViewModel`

`holonight_application` (where `ChatViewModel` lives) cannot depend on `ChatApplication`
(executable-layer, `apps/chat`) — that would invert the module layering documented in
`CLAUDE.md`. `ChatViewModel` therefore cannot itself call `workspaceVisible()`/`panelVisible()`,
own a `DesktopNotifier`, or know `org.freedesktop.Notifications` exists. `ChatApplication` already
owns both visibility signals and `ShowWorkspace()` — mirroring exactly how it already owns
`PanelSurface` rather than pushing panel lifecycle into `ChatViewModel`. Splitting the work this
way keeps `ChatViewModel` a notification-agnostic event source and `ChatApplication` the single
place that turns "adopted conversation" events plus "surface visibility" state into a concrete
D-Bus call — the same aggregation role it already plays for `PanelSurface`/workspace
orchestration.

**On REQ-NF-001's literal wording** ("evaluated synchronously... in
`ChatViewModel::onStreamEvent()`"): the gate's boolean expression executes in `ChatApplication`'s
connected slot, not textually inside `onStreamEvent()`'s function body — but Qt's default
`AutoConnection` resolves to a **direct, same-thread call** here (both objects live on the GUI
thread), so `ChatApplication`'s slot runs synchronously, in the same call stack, as part of the
`emit` statement inside `onStreamEvent()`. There is no queued connection, no event-loop trip, no
polling, and no buffering — the substantive guarantee REQ-NF-001 protects (visibility is read at
the moment of the triggering event, not some stale/deferred value) holds exactly as if the check
were inlined. This is called out explicitly here since a narrower literal reading is plausible and
worth pre-empting.

### 3. `Qt6::DBus` needs no new CMake link — it is already `PUBLIC` on `holonight_platform`

The brief for this cycle assumed `Qt6::DBus` was "likely NOT yet linked" to `holonight_platform`
since `PanelSurface` itself never touches D-Bus. Checking `src/platform/CMakeLists.txt` shows it
already is (`target_link_libraries(holonight_platform PUBLIC ... Qt6::DBus ...)`). Cross-checking
`apps/chat/CMakeLists.txt` explains why: it links `holonight_platform` but lists no explicit
`Qt6::DBus` of its own, yet `ChatApplication.cpp`/`ChatDbusAdaptor.cpp` use `QDBusConnection`,
`QDBusInterface`, and `QDBusAbstractAdaptor` directly — they only compile today because
`holonight_platform`'s `PUBLIC` link propagates `Qt6::DBus` transitively to the executable. Adding
`DesktopNotifier` to `holonight_platform` needs zero new `target_link_libraries` lines anywhere;
the only CMake change is adding `include/holonight_platform/desktop_notifier.h` and
`src/desktop_notifier.cpp` to `holonight_platform`'s existing `add_library()` file list. This also
means `tests/CMakeLists.txt` needs no new link either — `test_holonight_ai` already links
`holonight_platform`, which carries `Qt6::DBus`'s include paths and library along with it.

### 4. Fallback-summary resolution lives in `holonight_platform`, not `ChatViewModel`

`ChatViewModel::responseReady`/`requestFailed` carry the raw, unresolved `conversation_->title()`.
The "New response" wording is a notification-UX detail with no other consumer — folding it into
`ChatViewModel` would leak presentation-layer string constants into the application/view-model
layer for a feature `ChatViewModel` otherwise knows nothing about. Instead
`resolveNotificationSummary()` is a small free function next to `DesktopNotifier` in
`holonight_platform`, called from `ChatApplication`'s connection lambdas. This is also what makes
REQ-F-002/REQ-F-005's fallback rule unit-testable in isolation (see Testing).

## Alternatives considered

- **`ChatViewModel` owns the `DesktopNotifier` and gate directly**, using properties pushed down
  from `ChatApplication` (e.g. `ChatViewModel::setWorkspaceVisible(bool)`). Rejected: this makes
  `holonight_application` depend on `holonight_platform` for a concern (D-Bus notification
  delivery) that has nothing to do with conversation/streaming state, and still requires
  `ChatApplication` to push visibility into `ChatViewModel` on every change — no simpler than the
  chosen direction, and it blurs the "application logic vs. platform I/O" boundary the module
  layout otherwise keeps clean.
- **Reuse `isStreamingChanged`/`errorMessageChanged`** instead of adding new signals. Rejected —
  see Key Decision 1; cannot distinguish Completed from Cancelled without re-deriving state
  `onStreamEvent()` already had.
- **Extend `org.holonight.Chat1`** (the app's own service) with a `NotificationSent`-style
  signal instead of talking to `org.freedesktop.Notifications` directly. Rejected outright per
  SPEC's own constraint — that interface is reserved for panel lifecycle and is a different
  concern (control-plane for `holonight-shell`, not desktop-notification delivery).
- **Add `Qt6::DBus` freshly to `holonight_platform` or to `apps/chat`.** Moot — see Key Decision 3,
  it is already present and transitively available.
- **Poll `QDBusInterface::isValid()` before calling `Notify()`** to short-circuit when no daemon is
  present. Rejected: `isValid()` can be false simply because the service hasn't been D-Bus-activated
  yet (many daemons are activatable, not always-running), so this would produce false negatives.
  Always attempting the `asyncCall` and silently discarding an error result on the pending-reply
  watcher is both simpler and correct for every case (present, activatable-but-not-running, and
  genuinely absent).
- **Match notification clicks without a tracked ID** (treat any `ActionInvoked` as "our notification was
  clicked"). Rejected: this session bus is shared with every other app on the desktop; an unmatched
  handler would raise the workspace window in response to a click on some unrelated app's
  notification.
- **Track only the most recently returned notification ID.** Rejected: reopening the workspace
  does not necessarily dismiss the previous notification, so a later request can leave multiple
  notifications live. A set also avoids asynchronous `Notify()` reply ordering deciding which
  notification remains actionable.

## Testing strategy

Following this repo's precedent (`docs/sdd/quick-panel-window/DESIGN.md`'s "Testing" section and
`tests/platform/test_panel_surface_policy.cpp`, which tests only `PanelSurface`'s pure
`preferredPanelWidth()` policy function, not live Wayland placement):

**Automated (GTest, headless, `QT_QPA_PLATFORM=offscreen`):**

- `resolveNotificationSummary()` — new `tests/platform/test_desktop_notifier_policy.cpp` (mirrors
  `test_panel_surface_policy.cpp`'s naming/shape): empty title → `"New response"`; non-empty title
  → returned unchanged. Covers REQ-F-002/REQ-F-005 in isolation.
- `ChatViewModel::responseReady`/`requestFailed` emission — extends the existing
  `tests/application/test_chat_view_model.cpp` fixture (already drives `Completed`/`Error` stream
  events through a `FakeHttpClient`-backed provider). New cases via `QSignalSpy`:
  - A `Completed` event emits exactly one `responseReady` with the conversation's current title.
  - An `Error` event emits exactly one `requestFailed` with the title and `Error::message`
    verbatim (REQ-F-006's "verbatim" is exactly what a `QSignalSpy` argument comparison checks).
  - A `Cancelled` event (via `stop()`) emits neither signal (REQ-F-010).
  - These two signals fire independently of `isStreamingChanged`/`errorMessageChanged` (both still
    fire too, unchanged) — regression coverage that the new signals are additive, not a
    replacement.

**Not practically automatable, needs manual verification** (mirrors quick-panel-window's own
`TASKS.md` "Run the live compositor smoke test" item — `apps/chat` has no GTest coverage today for
any `ChatApplication` orchestration method, e.g. `CollapseToPanel`/`ShowPanel` are likewise only
manually verified; this feature inherits that existing gap rather than introducing a new one):

- The visibility-gate wiring inside `ChatApplication` (workspace visible / panel visible / both
  hidden → notify-or-not, REQ-F-007) — `ChatApplication` derives from `QGuiApplication` and
  registers real D-Bus services and a real workspace window in its constructor, so it cannot be
  instantiated headlessly in GTest. SPEC's own Acceptance Checks scenarios 1–3 and 7 are the
  manual test script for this.
- `DesktopNotifier`'s actual `Notify()` delivery, on-screen appearance, and click → `activated()` →
  `ShowWorkspace()` round-trip (REQ-F-008) — needs a real `org.freedesktop.Notifications` daemon on
  the session bus.
- REQ-NF-002's "no daemon available → silent no-op, no crash" behavior — SPEC's Acceptance Check
  scenario 6 (stop/disable the daemon, confirm no crash/dialog/log) is inherently an integration
  scenario against the real session bus.

Per this project's own convention (no self-driven GUI/D-Bus verification — see prior cycles' "no
visual verification" note), these manual items are handed to the user as a checklist rather than
attempted by the implementing agent.

## Known risks / open questions

- **Click-before-ID-stored race.** `Notify()`'s returned ID arrives asynchronously
  (`QDBusPendingCallWatcher`); `handleActionInvoked()` only matches clicks against
  `live_notification_ids_` once that reply has landed. In practice a notification cannot be clicked
  before the daemon has displayed it, and the daemon cannot display it before generating and
  returning the ID in the same D-Bus round trip that this code is already waiting on — so the
  window is negligible, but it is not formally zero if a daemon displays optimistically before
  replying. Not mitigated further; flagged as an accepted risk given REQ-NF-003's "no queuing"
  simplicity mandate.
- **Freedesktop Notifications capability variance.** Not every daemon honors `actions`/`"default"`
  identically (some minimal daemons, e.g. bare `notify-send`-only setups without a full daemon,
  don't support click actions at all). REQ-F-008 becomes a no-op (no click signal ever arrives) on
  such daemons rather than failing — consistent with REQ-NF-002's silent-degradation intent, but
  worth knowing during manual verification if a different daemon than expected is running.
  `GetCapabilities()` is deliberately not queried up front — SPEC has no requirement to adapt
  behavior per-daemon, and querying it would add a second round-trip and a new failure path for a
  feature whose spec explicitly wants minimal mechanics.
- **`conversation_->title()` is effectively never empty today** (`kDefaultConversationTitle =
  "New Chat"`, `holonight_persistence/conversation_record.h`), so REQ-F-002/F-005's fallback branch
  is close to dead code under current conversation-creation logic. It is still implemented per the
  spec's literal wording and as defensive coverage against any future change to title
  initialization; `resolveNotificationSummary()`'s unit test exercises the empty-string branch
  directly regardless of whether production code can currently reach it.
- **Pre-existing conversation-identity ambiguity in `onStreamEvent()`.** As noted in Data Flow,
  scoping to the adopted conversation relies on `onStreamEvent()` only ever being invoked for
  `conversation_`. This is existing, unmodified architecture (the `on_event` callback carries no
  conversation ID of its own) — this feature does not add a new race here, but it does inherit
  whatever pre-existing risk exists if `ChatController::stop()`'s cancellation and
  `adoptConversation()`'s swap are ever not perfectly sequenced. Out of scope to fix in this cycle;
  flagged so it isn't mistaken for something this design introduced.
- **No app icon asset exists yet** — `app_icon` is sent empty. If/when the project adds a real
  icon (`.desktop` file, icon theme entry), wiring it into `Notify()`'s `app_icon` argument is a
  trivial follow-up, not a structural change.
