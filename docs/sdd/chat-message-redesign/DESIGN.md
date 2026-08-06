# Chat Message Redesign — Design

Stage 2 (Design) output for `docs/sdd/chat-message-redesign/SPEC.md`. Read alongside the SPEC —
this document does not restate acceptance criteria, only the decisions needed to implement them.

## 0. Summary of approach

- `MessageBubble.qml` and `UserMessageCard.qml` are redesigned **in place** — no new message-card
  component. `MessageBubble.qml` gains a header row, drops its now-dead `isUser` branch (see
  §4.1), and switches to `HnSurfaceFrame` + `Text.MarkdownText`.
- One new QML file: `qml/shared/StreamingStatusBar.qml`, inserted into `ChatPanel.qml` between
  `MessageList` and `ChatComposer`.
- `holonight_domain::Message` gains one new field (`std::optional<ModelId> model_id_`) via a
  backward-compatible defaulted 6th constructor parameter — every existing call site keeps
  compiling unchanged (verified by grep, see §4.6).
- One new SQL migration (`0002_add_message_model_id.sql`) adds a nullable `messages.model_id`
  column, encoded/decoded with the **existing** `encodeModelId`/`decodeModelId` free functions in
  `conversation_repository_worker.cpp` — no new encode/decode code.
- `MessageListModel` gains one new role, `ModelNameRole` → `"modelName"`.
- `ChatController::send()`/`regenerate()` pass their existing `const ModelId&` parameter straight
  into the `Message` constructor — no new plumbing.
- No CMake changes except appending the new `.sql` file to persistence's existing
  `qt_add_resources()` file list; `apps/chat/CMakeLists.txt` globs `qml/**`, so the new QML file
  needs no CMake edit.

---

## 1. Components

### 1.1 QML — modified in place

| File | Change |
|---|---|
| `qml/shared/MessageBubble.qml` | Root becomes `HnSurfaceFrame` (was `Item` + `Rectangle`). Adds header `RowLayout` (icon-frame + model name + timestamp), visible only when `messageRole === "assistant"`. Body `Text` switches to `textFormat: Text.MarkdownText`. Status `Text` element deleted. `isUser`-branch removed entirely (dead code, see §4.1). Border tints red on `messageStatus === "error"`. New required properties: `modelName`, `createdAt`. |
| `qml/shared/UserMessageCard.qml` | One line added: `chamferedCornersOverride: HnCornerMask.TopRight` on the existing `HnSurfaceFrame`. No other changes. |
| `qml/shared/MessageList.qml` | Drop `showMessageStatus` property. Delegate gains `required property string modelName` (bound to the new `modelName` role). `MessageBubble` instantiation drops `showStatus:`, adds `modelName:` and `createdAt:`. |
| `qml/shared/ChatPanel.qml` | Drop `showMessageStatus` property and its wiring to `MessageList`. Insert `StreamingStatusBar { Layout.fillWidth: true }` between `MessageList` and `ChatComposer` in the `ColumnLayout`. |
| `qml/shared/ChatComposer.qml` | Remove the Send/Stop toggle. Button text becomes the literal `qsTr("Send")`; `enabled: !ChatViewModel.isStreaming && ChatViewModel.canSend`; `onClicked` always calls `ChatViewModel.send(ChatViewModel.inputText)`. |

`qml/quickpanel/QuickPanel.qml` needs **no changes** — it embeds `ChatPanel { compact: true }`, so
every change above (status bar, header, markdown, chamfer) is inherited automatically, satisfying
every REQ's "identical in both surfaces" clause for free.

### 1.2 QML — new file

| File | Purpose |
|---|---|
| `qml/shared/StreamingStatusBar.qml` | REQ-F-011/012. A `Rectangle` (chrome styled like `ChatNoticeStack.qml`'s banners) containing a `RowLayout`: status dot, `"Generating response…"` text, `Stop` button (`onClicked: ChatViewModel.stop()`). `visible: ChatViewModel.isStreaming` — `ColumnLayout`/`RowLayout` (Qt Quick Layouts) exclude invisible children from layout automatically, so no `Layout.maximumHeight` trick is needed. |

### 1.3 C++ domain (`holonight_domain`)

| File | Change |
|---|---|
| `src/domain/include/holonight_domain/message.h` | Add `#include <holonight_domain/model_id.h>` and `#include <optional>`. `Message` gains `std::optional<ModelId> model_id_`, a 6th defaulted constructor parameter, `modelId()` getter, `setModelId()` setter. `operator==` stays `= default` (picks up the new member automatically). |
| `src/domain/src/message.cpp` | Constructor initializer list gains `model_id_(std::move(modelId))`; add the two new method bodies. |

### 1.4 C++ application (`holonight_application`)

| File | Change |
|---|---|
| `src/application/include/holonight_application/message_list_model.h` | `Roles` enum gains `ModelNameRole` (appended last, after `CreatedAtRole` — safe, doesn't renumber existing roles). `Row` struct gains `QString model_name`. |
| `src/application/src/message_list_model.cpp` | `toRow()` populates `model_name`; `data()` handles `ModelNameRole`; `roleNames()` registers `{ModelNameRole, "modelName"}`; `updateLastMessage()`'s `dataChanged` role list gains `ModelNameRole` (needed so `regenerate()`'s model-name change is picked up by the QML delegate). |
| `src/application/src/chat_controller.cpp` | `send()`: assistant placeholder constructed with the 6th arg `model`. `regenerate()`: regenerated message constructed with the 6th arg `model`. See exact diffs in §3.1 — **note the `QDateTime{}` placeholder gotcha**. |

`ChatViewModel`, `ChatController`'s public signatures, `Conversation::appendMessage`/
`replaceLastMessage`, and `MessageListModel::appendMessage`/`updateLastMessage` all take `Message`
by value/reference already — none of them need signature changes; the new field rides along
transparently once the constructor call sites are updated.

### 1.5 C++ persistence (`holonight_persistence`)

| File | Change |
|---|---|
| `src/persistence/migrations/0002_add_message_model_id.sql` | New file: `ALTER TABLE messages ADD COLUMN model_id TEXT;` |
| `src/persistence/CMakeLists.txt` | Append `migrations/0002_add_message_model_id.sql` to the existing `qt_add_resources(... FILES migrations/0001_init.sql)` list. |
| `src/persistence/src/migration_runner.cpp` | `builtInMigrations()` gains a second `Migration{.version = 2, .name = "0002_add_message_model_id", .sql = readResource(":/holonight_persistence/migrations/0002_add_message_model_id.sql")}` entry. |
| `src/persistence/src/detail/conversation_repository_worker.cpp` | `messageFromRecord()` reads a 6th column and calls the **existing** `decodeModelId()`. `persistNewMessage()`'s INSERT gains a `model_id` column + bind. `persistMessageSettled()`'s UPDATE gains a `model_id` column + bind (needed so a `regenerate()`-changed model is actually saved — see §2.1). Both `loadConversation()`'s `SELECT` and the migration touch only this one file. |

No change to `ConversationRepository`'s abstract interface, `SqliteConversationRepository`, or
`ConversationRepositoryWorker`'s header — the new column is entirely internal to this one
`.cpp`'s SQL text and row (de)serialization.

---

## 2. Data flow

### 2.1 `model_id`: send/regenerate → Message → SQLite → MessageListModel → QML header

```
ChatViewModel::send(text)
  └─ ChatController::send(conversation, selected_model_id_, text, onEvent)
       └─ constructs assistant placeholder Message(..., modelId = selected_model_id_)
            [Message::model_id_ now set; never reconstructed for the lifetime of the stream —
             ChatController::handleStreamEvent() only ever calls setText()/transitionTo() on the
             same Message object (chat_controller.cpp:134-165), so model_id_ survives every
             ContentDelta/Completed/Error/Cancelled transition untouched]
  └─ ChatViewModel appends the Message to MessageListModel (appendMessage) and, if persistence is
     enabled, calls repository_->persistNewMessage(conversationId, assistantMessage)
       └─ ConversationRepositoryWorker::persistNewMessage() binds
          encodeModelId(*message.modelId()) (or NULL) into the new `model_id` column
  └─ on each StreamEvent, ChatViewModel::onStreamEvent() calls
     message_model_->updateLastMessage(conversation_->messages().back())
       └─ MessageListModel::toRow() re-reads modelId()->model_name into Row::model_name,
          emits dataChanged for {..., ModelNameRole}
  └─ on stream terminal state, ChatViewModel calls
     repository_->persistMessageSettled(conversationId, lastMessage)
       └─ worker's UPDATE now also writes model_id (covers regenerate() picking a different model)
  └─ MessageList.qml delegate exposes the `modelName` role → MessageBubble.modelName →
     header Text
```

`ChatController::regenerate()` follows the identical path, except the placeholder is constructed
with `messages.back().id()` (same message id) and the *new* `model` parameter — so a regenerate
with a different model overwrites `model_id_` on the same row, and `persistMessageSettled()`'s
UPDATE (not `persistNewMessage()`'s INSERT — the row already exists) is what actually saves the
change to SQLite.

On load (`ConversationRepositoryWorker::loadConversation()`), `messageFromRecord()` decodes column
6 via the existing `decodeModelId()` and threads it through the `Message` constructor's 6th
parameter, so restored conversations show the correct model name in assistant headers without any
special-casing in `ChatViewModel::adoptConversation()`.

### 2.2 `isStreaming` → status bar & composer button

`ChatViewModel::isStreaming` (already a `NOTIFY`-backed `Q_PROPERTY`, unchanged) drives two
independent QML bindings, both declarative, no new signals needed:

- `StreamingStatusBar.visible: ChatViewModel.isStreaming` — Qt Quick Layouts drops invisible
  children from layout automatically, so the composer slides up/down with no extra height
  bookkeeping.
- `ChatComposer`'s Send button: `enabled: !ChatViewModel.isStreaming && ChatViewModel.canSend`.

Both bars/buttons in `WorkspaceWindow` and the `QuickPanel` bind to the same `ChatViewModel`
singleton, so they change in lockstep automatically — no cross-surface sync code required.

---

## 3. Interfaces / APIs

### 3.1 `Message` (domain)

`src/domain/include/holonight_domain/message.h`:

```cpp
#include <holonight_domain/model_id.h>   // new
#include <optional>                      // new

class Message {
 public:
  Message();
  Message(MessageId messageId, MessageRole role, QString text, MessageStatus status = MessageStatus::Pending,
          QDateTime createdAt = {}, std::optional<ModelId> modelId = std::nullopt);  // 6th param added

  // ... existing getters unchanged ...
  [[nodiscard]] const std::optional<ModelId>& modelId() const;   // new
  void setModelId(std::optional<ModelId> modelId);                // new

 private:
  // ... existing members unchanged ...
  std::optional<ModelId> model_id_;   // new, default-initialized to std::nullopt
};
```

`src/domain/src/message.cpp` constructor:

```cpp
Message::Message(MessageId messageId, MessageRole role, QString text, MessageStatus status, QDateTime createdAt,
                 std::optional<ModelId> modelId)
    : id_(std::move(messageId)),
      role_(role),
      text_(std::move(text)),
      status_(status),
      created_at_(createdAt.isValid() ? createdAt : QDateTime::currentDateTimeUtc()),
      model_id_(std::move(modelId)) {}

const std::optional<ModelId>& Message::modelId() const { return model_id_; }
void Message::setModelId(std::optional<ModelId> modelId) { model_id_ = std::move(modelId); }
```

**Call-site diff — implementer gotcha.** `chat_controller.cpp`'s `send()` currently omits the
`createdAt` argument to fall through to the constructor's own default:

```cpp
// today
Message assistantMessage(MessageId::generate(), MessageRole::Assistant, QString(), MessageStatus::Pending);
// after — must supply QDateTime{} explicitly to reach the 6th (modelId) slot; C++ has no
// "skip a defaulted positional argument" syntax
Message assistantMessage(MessageId::generate(), MessageRole::Assistant, QString(), MessageStatus::Pending,
                         QDateTime{}, model);
```

`regenerate()` already passes `createdAt` explicitly, so it only gains the trailing `model` arg:

```cpp
Message regeneratedMessage(messages.back().id(), MessageRole::Assistant, QString(), MessageStatus::Pending,
                           messages.back().createdAt(), model);
```

The `User` message construction in `send()` (`Message(MessageId::generate(), MessageRole::User,
std::move(user_text), MessageStatus::Complete)`) is **unchanged** — it stays 4-arg and defaults
`modelId` to `nullopt`, satisfying REQ-F-007's "never for User or System messages" without any
role check anywhere: the persistence layer just serializes whatever `Message::modelId()` holds, so
"only assistant messages get a model_id" is enforced entirely by which `ChatController` code paths
choose to pass one — the same trust-the-caller layering the domain module already uses elsewhere
(`Message` doesn't self-validate role/field combinations today either).

### 3.2 `MessageListModel::Roles` (application)

```cpp
enum Roles : std::uint16_t {
  IdRole = Qt::UserRole + 1,
  RoleRole,
  TextRole,
  StatusRole,
  CreatedAtRole,
  ModelNameRole,   // new — "modelName" — model_name string, "" for User/System rows
};
```

`roleNames()` gains `{ModelNameRole, QByteArrayLiteral("modelName")}`. `toRow()` gains:

```cpp
.model_name = message.modelId() ? message.modelId()->model_name : QString(),
```

`updateLastMessage()`'s `dataChanged` call:

```cpp
emit dataChanged(changedIndex, changedIndex, {RoleRole, TextRole, StatusRole, CreatedAtRole, ModelNameRole});
```

### 3.3 SQL migration

`src/persistence/migrations/0002_add_message_model_id.sql`:

```sql
ALTER TABLE messages ADD COLUMN model_id TEXT;
```

`MigrationRunner::builtInMigrations()`:

```cpp
return {
    Migration{
        .version = 1,
        .name = QStringLiteral("0001_init"),
        .sql = readResource(QStringLiteral(":/holonight_persistence/migrations/0001_init.sql")),
    },
    Migration{
        .version = 2,
        .name = QStringLiteral("0002_add_message_model_id"),
        .sql = readResource(QStringLiteral(":/holonight_persistence/migrations/0002_add_message_model_id.sql")),
    },
};
```

`src/persistence/CMakeLists.txt`'s `qt_add_resources()` `FILES` list gains
`migrations/0002_add_message_model_id.sql`.

### 3.4 `encodeModelId`/`decodeModelId` reuse (REQ-C-004)

No new functions. Both are already free functions in the anonymous namespace of
`conversation_repository_worker.cpp`, operating on a plain `holonight_domain::ModelId` — they are
column-agnostic today (used for `conversations.last_model_id`) and are called verbatim for the new
`messages.model_id` column:

```cpp
// persistNewMessage() — INSERT gains a column
query.prepare(
    QStringLiteral("INSERT INTO messages (id, conversation_id, role, content, status, created_at, model_id) "
                   "VALUES (:id, :conversation_id, :role, :content, :status, :created_at, :model_id)"));
// ...
query.bindValue(QStringLiteral(":model_id"),
                message.modelId() ? QVariant(encodeModelId(*message.modelId())) : QVariant());
```

```cpp
// persistMessageSettled() — UPDATE gains a column (regenerate() may have changed the model)
query.prepare(QStringLiteral("UPDATE messages SET content = :content, status = :status, model_id = :model_id "
                             "WHERE id = :id"));
// ...
query.bindValue(QStringLiteral(":model_id"),
                message.modelId() ? QVariant(encodeModelId(*message.modelId())) : QVariant());
```

```cpp
// messageFromRecord() — SELECT gains a column
holonight_domain::Message messageFromRecord(const QSqlQuery& query) {
  return {holonight_domain::MessageId::fromString(query.value(0).toString()),
          messageRoleFromText(query.value(1).toString()), query.value(2).toString(),
          messageStatusFromText(query.value(3).toString()), query.value(4).toDateTime(),
          decodeModelId(query.value(5))};
}
```

with the corresponding `SELECT id, role, content, status, created_at, model_id FROM messages ...`
in `loadConversation()`.

**Contrast with the existing `normalizeNullToEmpty()` gotcha**: that helper exists because
`messages.content` is `NOT NULL`, so a null `QString` must be *coerced to empty string* before
binding. `model_id` is nullable by design (this migration), so the correct bind for "no model" is
the **opposite**: an unset/invalid `QVariant()`, not an empty string — do not run `model_id`
through `normalizeNullToEmpty()`.

### 3.5 QML component property lists

**`MessageBubble.qml`** (full replacement):

```qml
required property string messageRole    // "assistant" | "system" (never "user" — see §4.1)
required property string messageText
required property string messageStatus  // used only for the error-tint check now
required property string modelName      // new
required property date createdAt        // new
property real maximumWidthRatio: 0.75

readonly property bool isAssistant: messageRole === "assistant"
readonly property bool isError: messageStatus === "error"
```

Frame: `HnSurfaceFrame { chamferedCornersOverride: HnCornerMask.TopRight; borderColor: isError ?
HoloniightPalette.error : HoloniightPalette.borderPassive; borderWidth: isError ?
HoloniightPalette.focusBorderWidth : 0; fillColor: HoloniightPalette.surface }`. Header `RowLayout`
`visible: root.isAssistant`. Body `Text { textFormat: Text.MarkdownText; onLinkActivated: link =>
Qt.openUrlExternally(link) }`.

**`UserMessageCard.qml`**: one new property value, `chamferedCornersOverride:
HnCornerMask.TopRight`, on the existing `HnSurfaceFrame`. No property list changes.

**`MessageList.qml`**: delegate `Item` gains `required property string modelName`. `showMessageStatus`
property removed. `MessageBubble` instantiation:

```qml
MessageBubble {
    width: messageDelegate.width
    messageRole: messageDelegate.role
    messageText: messageDelegate.text
    messageStatus: messageDelegate.status
    modelName: messageDelegate.modelName
    createdAt: messageDelegate.createdAt
    maximumWidthRatio: root.messageWidthRatio
}
```

**`StreamingStatusBar.qml`** (new): no external API beyond `Layout.fillWidth: true` set by its
parent; internally reads `ChatViewModel.isStreaming` and calls `ChatViewModel.stop()` directly, so
`ChatPanel.qml` needs no new pass-through properties.

**`ChatComposer.qml`**: no property-list change; only the Send/Stop `Button`'s `text`/`enabled`/
`onClicked` bindings change (see §1.1).

---

## 4. Key decisions & rationale

### 4.1 `MessageBubble.qml`'s `isUser` branch is dead code — delete it, don't preserve it

`MessageList.qml:72` already routes `role === "user"` to `UserMessageCard` and everything else to
`MessageBubble`. `MessageBubble`'s `isUser`/right-alignment/`HoloniightPalette.primary`-fill branch
(lines 13, 25-28, 44 in the current file) is therefore unreachable in production — `messageRole`
passed into `MessageBubble` is always `"assistant"` or `"system"`. The redesign removes this
branch outright rather than adapting it, simplifying the new header-row logic to a single
`isAssistant` flag instead of a `isUser`/`isAssistant` pair.

### 4.2 Reuse `HnSurfaceFrame` for both cards, not a new corner-drawing primitive (REQ-C-001)

`HnSurfaceFrame` already supports the exact 3-rounded/1-chamfered shape via
`chamferedCornersOverride: HnCornerMask.TopRight` (see `WorkspaceWindow.qml`'s own
`HnCornerMask.TopRight | HnCornerMask.BottomRight` usage for the sidebar). No new
`HnCornerMask`/`HnAppearance` values are needed — `TopRight` alone gives 3 rounded + 1 chamfered.

### 4.3 Preserve `MessageBubble`'s shrink-to-content width, don't adopt `UserMessageCard`'s fixed-ratio width

`UserMessageCard` sets `width: root.width * root.maximumWidthRatio` (always exactly 75% wide).
`MessageBubble` today computes `Math.min(ratio * width, content-implicit-width + padding)`
(shrinks to fit short replies). The redesign keeps `MessageBubble`'s shrink-to-fit formula (minus
the now-removed status-text width term) rather than unifying on `UserMessageCard`'s fixed width.
Nothing in SPEC.md requires unifying the two sizing behaviors, the mockups aren't machine-readable
from this stage, and changing a visible sizing behavior with no spec line item backing it is a
regression risk better left to explicit user-visual feedback (per the project's no-self-screenshot
convention) than a Stage 2 guess.

### 4.4 Icon-frame placeholder: inline `Rectangle`+`Rectangle`, not `InlineIcon.qml`, not a new shared component

`InlineIcon.qml` (`qml/workspace/InlineIcon.qml`) renders arbitrary SVG path data via
`QtQuick.Shapes` — built for line icons, not a solid filled circle; using it here would mean
authoring a throwaway SVG circle path for no benefit over a plain `Rectangle { radius: width/2 }`,
which is also exactly the pattern `UserMessageCard.qml` already uses for its own avatar (lines
31-45). The assistant icon-frame mirrors that same nested-`Rectangle` structure (outer 24×24-ish
frame sized off `HoloniightPalette.controlHeight * 0.75` to match `UserMessageCard`'s avatar size,
inner circle filled `HoloniightPalette.accentBlue`, no letter). A shared `MessageAvatarIcon.qml`
component was considered and rejected — see §5.

### 4.5 `StreamingStatusBar.qml` placement and chrome

Lives in `ChatPanel.qml`'s `ColumnLayout`, between `MessageList` and `ChatComposer` (matches
REQ-F-011's "positioned above the composer" literally, and keeps it inside the same
`ColumnLayout` so Qt Quick Layouts handles show/hide spacing with zero extra code). Chrome
(`Rectangle` + `radius: HoloniightPalette.radiusControl` + `color:
HoloniightPalette.surfaceVariant`) mirrors `ChatNoticeStack.qml`'s existing banner styling
(explicitly named as the reference in the task) rather than inventing new visual chrome.

### 4.6 Error-tint token: border only, `HoloniightPalette.error` / `focusBorderWidth`, fill untouched

`HoloniightPalette` (see `holonight-qt/qml/holoniightpalette.h`) exposes `error`/`onError` as a
strong, saturated pair designed for the existing `ChatNoticeStack` banner (opaque fill + light
text) — not a subtle card-background tint. Reusing it as a message-frame *background* would either
force reusing `onError` as the body-text color (breaking Markdown text's normal
`HoloniightPalette.textPrimary` styling and contrast guarantees the sibling `holonight-qt` test
suite enforces) or require a new, undefined "error surface fill" token, which is out of scope
(SPEC.md explicitly defers exact token choice to design but doesn't authorize adding new palette
tokens). The chosen design instead tints only `borderColor: HoloniightPalette.error` and bumps
`borderWidth` to `HoloniightPalette.focusBorderWidth` (the palette's existing "make this border
more prominent" width token, already used by `ChatComposer.qml` for focus) — visually distinct,
body text untouched, no new tokens.

### 4.7 No role-based guard in the persistence layer for REQ-F-007's "never for User/System"

See §3.1 — persistence just serializes `Message::modelId()` verbatim; the constraint is enforced
entirely by `ChatController` only ever passing a `ModelId` when constructing/replacing an
*assistant* message. Matches the existing pattern where `Message` performs no self-validation of
role/field combinations elsewhere.

### 4.8 `persistMessageSettled()` must also write `model_id`, not just `persistNewMessage()`

A first read of REQ-F-008/010 might conclude only the INSERT path (`persistNewMessage()`) needs
the new column, since the row is created once. But `regenerate()` reuses the same message id and
row — `ChatController::regenerate()` constructs a fresh in-memory `Message` with a (possibly
different) `model_id_`, and that message only reaches SQLite via `persistMessageSettled()`'s
UPDATE once the regenerated stream terminates (`chat_view_model.cpp:493`). Without updating
`persistMessageSettled()`'s SQL too, a regenerate-with-a-different-model would show the new model
in the live UI (`MessageListModel` mirrors `Conversation` synchronously) but silently revert to
the old model name after an app restart reloads from SQLite. Both statements are updated.

---

## 5. Alternatives considered

| Decision point | Chosen | Rejected alternative | Why rejected |
|---|---|---|---|
| Assistant message component | Redesign `MessageBubble.qml` in place | New `AssistantMessageCard.qml` mirroring `UserMessageCard.qml` | `MessageBubble` already owns 100% of the assistant+system rendering path and most needed logic (frame, error tint) is genuinely shared with the system-message case; a parallel component would duplicate that and immediately need its own upkeep. Task's own guidance recommends in-place reuse absent a strong reason otherwise. |
| `model_id` storage shape | Single `TEXT` column, `encodeModelId`/`decodeModelId` (REQ-C-004, mandatory) | Two columns (`model_provider_id`, `model_name`) | REQ-C-004 explicitly mandates the encoded-string pattern for consistency with `conversations.last_model_id`; two columns would also mean writing new (de)serialization code instead of reusing the existing functions verbatim. |
| `MessageListModel` role surface | `modelName` string only (REQ-F-009, mandatory) | Expose full `ModelId` (provider_id + model_name) as a `QVariantMap` role | REQ-F-009 explicitly forbids exposing `provider_id` to QML; a string-only role also can't tempt a future QML change into doing per-provider branching, matching the non-goal ("no per-provider icon/logo system"). |
| Icon placeholder reuse | Inline `Rectangle`+`Rectangle`, matching `UserMessageCard`'s existing avatar pattern | Shared `MessageAvatarIcon.qml` component | Only two call sites, with genuinely different content (lettered "A" avatar vs. plain blue dot) — the project's established "rule of three" precedent (see `chat_controller.h`'s no-dynamic-dispatch-until-forced-again comment, and the four-provider-adapter history) argues against abstracting a second, still-different consumer. |
| Streaming status bar chrome | `Rectangle` card matching `ChatNoticeStack.qml` | Bare `RowLayout` with no background | Task explicitly names `ChatNoticeStack.qml` as the styling reference; matching it keeps the app's transient-bar visual language consistent instead of introducing a third distinct "bar" look (composer already has its own `HnSurfaceFrame` chrome, notices have theirs). |
| Error visual tint | Border-only (`borderColor`/`borderWidth`) | Background/fill tint using `HoloniightPalette.error`/`onError` | See §4.6 — fill tint would force a text-color change that fights Markdown body-text styling and palette contrast guarantees; no dedicated "error surface" token exists to add without exceeding this design's scope. |
| `Message` constructor evolution | Append a 6th defaulted parameter | Reorder/insert `modelId` earlier in the parameter list, or add a builder/setter-only approach | Appending at the end is the only option that keeps every existing positional call site (production and test) compiling with zero changes (see §4.6 discussion and §6 verification); inserting earlier would silently miscompile or require touching every call site for no benefit. |

---

## 6. Known risks

1. **`Text.MarkdownText` + streaming `implicitHeight` churn.** Every `ContentDelta` token calls
   `MessageListModel::updateLastMessage()` → `dataChanged` → the bound `Text` element reparses its
   full accumulated string as Markdown (not just the delta) and relayouts. Markdown parsing
   (`QTextDocument`-based) is heavier per-token than the current `Text.PlainText` path. `MessageList`'s
   existing `updateFollowPosition()` (`onContentHeightChanged`) already handles height changes
   correctly, but no profiling has been done here — if token-by-token streaming visibly stutters at
   high tokens/sec, batching delta application is a follow-up, not something this design attempts
   (SPEC.md's non-goals explicitly exclude touching the streaming/event pipeline).
2. **Fenced code blocks and `Text.Wrap`.** REQ-C-002 wants plain monospace text with no card. Qt's
   `Text.Wrap` mode does break mid-word when a single "word" (e.g., an unbroken long code line)
   exceeds the available width, so long code lines won't cause horizontal overflow, but they will
   wrap ungracefully (no soft-wrap-at-punctuation). Accepted per REQ-C-002's explicit "plain
   monospace text... no distinct card" constraint — a real code viewer is out of scope
   (non-goals).
3. **Legacy rows have no `model_id`.** Messages persisted before this migration ships (or any
   User/System row) decode to `std::nullopt` → `modelName` role is `""`. REQ-F-002 requires the
   header to render for *every* assistant message unconditionally, so old conversations will show
   the icon with a blank name next to it rather than hiding the header. No backfill is planned or
   requested by SPEC.md.
4. **`Message` constructor call-site audit.** `grep -rn "Message("` across `src/` and `tests/`
   confirms every production call site passes ≤5 positional args (the 6th `modelId` defaults
   safely) and the one brace-init call (`messageFromRecord`) also passes ≤5 members before this
   change, both compatible with a trailing defaulted member. Test files
   (`tests/domain/test_conversation.cpp`, `tests/application/test_chat_view_model.cpp`,
   `tests/persistence/test_conversation_repository_sqlite.cpp`, `tests/persistence/
   fake_conversation_repository.h`, and provider/settings test files) were located but not
   individually re-verified beyond the grep pass — run the full suite after this change, don't
   trust the grep alone.
5. **Migration idempotency.** `ALTER TABLE messages ADD COLUMN model_id TEXT` is safe SQLite DDL
   (nullable column, no default-value backfill needed) and `MigrationRunner::apply()` already
   guards re-application via `schema_version` (each migration runs inside its own transaction,
   gated by `version > current`), matching the existing `0001_init` precedent exactly — low risk.
6. **HnSurfaceFrame `buildPath()` recomputation on error-tint changes.** `borderColor`/
   `borderWidth` becoming dynamic (bound to `isError`) means `resolvedShape`/`pathData` (and the
   underlying `Shape`/`ShapePath`) can recompute more often than the previous static-Rectangle
   version. This is scoped to one message row's frame, not the whole `ListView`, so impact should
   be negligible, but it's a new dynamic dependency that didn't exist when `MessageBubble` was a
   plain `Rectangle`.
7. **Composer's Stop control is now single-sourced.** REQ-C-005 intentionally removes the
   composer's own Stop path so `StreamingStatusBar` is the only way to stop a stream. This is a
   deliberate SPEC requirement, not an oversight, but it does mean there's no longer a redundant
   fallback if the status bar ever fails to render for some reason (e.g., a future regression in
   its `visible` binding) — worth a manual QA pass specifically on "start streaming, confirm the
   bar and its Stop button both appear in `WorkspaceWindow` and `QuickPanel`."
8. **Markdown link interactivity is an interpretive addition.** REQ-F-004's acceptance criteria
   only require links to "render correctly," not necessarily be clickable. This design adds
   `onLinkActivated: link => Qt.openUrlExternally(link)` as a reasonable interpretation of a chat
   UI's link support, but it's beyond the letter of the acceptance criteria — flagging in case the
   user wants it scoped out.
