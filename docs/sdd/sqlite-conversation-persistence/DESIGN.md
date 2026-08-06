# SQLite Conversation Persistence Design

**Document Version**: 1.0
**Date**: 2026-07-22
**Modules**: `holonight_persistence` (new: SQLite repository, migrations, worker thread),
`holonight_domain` (additive: `Conversation::setTitle`, `deriveConversationTitle`),
`holonight_application` (new `ConversationListModel`; `ChatViewModel` grows a repository seam),
`qml/workspace/` (new conversation list panel)
**Traces to**: `docs/sdd/sqlite-conversation-persistence/SPEC.md` (27 functional, 10 non-functional,
10 constraint requirements)
**Status**: Design complete, pending Stage 3 (task breakdown)

---

## 1. Components

### 1.1 `holonight_domain` (additive change to an already-"done" module)

Following the precedent set in `docs/sdd/ollama-chat-backend/DESIGN.md` §4.4 (two additive methods
landed on a module a prior cycle marked complete), this cycle adds one method and one free function
— no new classes, no signature changes to anything existing:

- `Conversation::setTitle(QString title)` — replaces `title_` in place and stamps `updated_at_`,
  mirroring `replaceLastMessage`'s existing "mutate + stamp `updated_at_`" pattern. Needed so the
  in-memory active `Conversation` stays consistent with the database after an auto-derived or
  user-initiated rename (§4.2 explains why this is worth doing even though no QML surface reads it
  this cycle).
- `deriveConversationTitle(const QString& firstMessageText) -> QString` (free function,
  `conversation.h`/`conversation.cpp`) — the REQ-F-027 truncation rule, implemented as a pure,
  synchronously-testable string transform with no Qt-Sql/threading/UI dependency (§3.1, §4.1).

**Deliberately not added to `Conversation`**: a `lastModelId()`/`setLastModelId()` accessor. See Key
Decision §4.2 — `last_model_id` stays a `holonight_persistence`/`ChatViewModel` concern.

### 1.2 `holonight_persistence` (new — `INTERFACE` → `STATIC`)

| Class | File | Role |
|---|---|---|
| `ConversationRepository` | `conversation_repository.h` | Abstract, `QObject`-derived interface (REQ-C-006). Pure-virtual, non-blocking request methods; concrete result signals. |
| `SqliteConversationRepository` | `sqlite_conversation_repository.h` | GUI-thread-resident façade. Owns a `QThread` + a private worker moved onto it (REQ-F-022). |
| `detail::ConversationRepositoryWorker` | `detail/conversation_repository_worker.h` | Lives on the worker thread. Owns the single `QSqlDatabase` connection; every SQL statement in the system executes here. |
| `MigrationRunner` | `migration_runner.h` | Stateless: applies an ordered list of `.sql` migrations to any `QSqlDatabase` handed to it. Used by the worker at startup and directly by migration tests (REQ-NF-007). |
| `ConversationSummary` / `LoadedConversation` | `conversation_record.h` | Plain value DTOs carried across the worker→GUI thread signal boundary; `Q_DECLARE_METATYPE`'d here (not in `holonight_domain`). |
| `resolveDatabaseFilePath()` | `database_path.h` | Free function: XDG lookup + `mkpath` (REQ-F-001). |
| `FakeConversationRepository` (test-only) | `tests/persistence/fake_conversation_repository.h` | REQ-C-006/NF-008's injectable test double — same precedent as `tests/providers/fake_http_client.h`. |

Migrations ship as Qt resources (`migrations/0001_init.sql` → `qt_add_resources`), not a runtime
`migrations/` directory read from disk — see Key Decision §4.5.

### 1.3 `holonight_application` (additive — module is already `STATIC`)

- **`ConversationListModel`** (new) — a `QAbstractListModel`, `QML_ELEMENT`/`QML_UNCREATABLE`,
  built as a structural sibling of `MessageListModel`: narrow, purpose-built mutators
  (`setAll`, `upsertToFront`, `touchToFront`, `removeById`), never a generic "resync everything"
  path as the primary flow (§1.2/§6.4 of the chat-window-qml precedent).
- **`ChatViewModel`** (existing, grows) — gains a `std::unique_ptr<ConversationRepository>`, a
  `ConversationListModel*` property, four new invokables (`createConversation`,
  `switchConversation`, `renameConversation`, `deleteConversation`), a `dismissPersistenceBanner()`
  invokable, and a new `persistenceStatusMessage` property distinct from the existing
  `errorMessage` (§4.6). `ChatController`/`OllamaProvider` are untouched (REQ-C-007) — every
  persistence call site is inside `ChatViewModel`'s own methods, immediately after the points
  where it already knows `ChatController` mutated `conversation_`.

### 1.4 `qml/workspace/` (new)

- `ConversationListPanel.qml` — left-hand column: "New Chat" button + `ListView` bound to
  `ChatViewModel.conversationList`.
- `ConversationListDelegate.qml` — one row: title (or inline `TextField` while editing), formatted
  timestamp, edit icon, delete icon with local (QML-only) inline confirmation state (§4.9).
- `WorkspaceWindow.qml` gains a second banner `Rectangle` (bound to
  `ChatViewModel.persistenceStatusMessage`, §4.6) and a `RowLayout` wrapping the new panel next to
  the existing chat column.

### 1.5 Build system

`src/persistence/CMakeLists.txt` converts `INTERFACE` → `STATIC` (§6.1). `src/application/CMakeLists.txt`
gains one new source pair and a new `PUBLIC` link to `holonight_persistence` (§6.2). No change is
needed to `apps/chat/CMakeLists.txt` — `holonight_persistence` is already linked there (it was
already listed in the existing `target_link_libraries(holonight-chat PRIVATE ...)` block as an empty
`INTERFACE` target), and the metatype extract/merge step only ever inspects `holonight_application`
(unchanged target name), which registers the only new QML type this cycle
(`ConversationListModel`).

---

## 2. Data Flow

### 2.1 Startup (REQ-F-001–004, REQ-F-025, REQ-NF-002)

1. `ChatViewModel::create(QQmlEngine*, QJSEngine*)` now additionally builds
   `auto repository = std::make_unique<SqliteConversationRepository>(holonight_persistence::resolveDatabaseFilePath())`
   and passes it into `ChatViewModel`'s DI constructor alongside the existing `OllamaProvider`.
2. `ChatViewModel`'s constructor connects every `ConversationRepository` signal to a private slot
   (§3.10), then calls `repository_->initialize()` — non-blocking, returns immediately.
3. `SqliteConversationRepository`'s constructor already started `worker_thread_` and queued
   `openAndMigrate()` onto the worker via `QMetaObject::invokeMethod(worker_, &Worker::openAndMigrate, Qt::QueuedConnection)`.
4. On the worker thread, `openAndMigrate()`: calls `QSqlDatabase::addDatabase("QSQLITE", connection_name_)`
   (a fresh, internally-generated `QUuid`-based name — §4.4), `setDatabaseName(database_path_)`,
   `.open()`; on success, executes `PRAGMA foreign_keys = ON`, then
   `MigrationRunner::apply(db, MigrationRunner::builtInMigrations())`.
   - **All succeeds** → emits `initialized()`.
   - **`.open()` fails, or `MigrationRunner::apply` returns an error** → emits
     `unavailable(reason)`; the connection is closed and no further SQL is attempted this session
     (REQ-F-025).
5. `ChatViewModel::onRepositoryInitialized()`: sets `persistence_enabled_ = true`, calls
   `repository_->listConversations()`.
6. `ChatViewModel::onConversationListLoaded(conversations)`: `conversation_list_model_->setAll(conversations)`.
   - If non-empty: calls `switchConversation(conversations.front().id)` (already sorted
     `updated_at DESC` by the repository's SQL — REQ-F-007) — auto-loads the most recently active
     conversation (REQ-NF-002's 50-conversation-in-3-seconds bound is met trivially: this is one
     indexed `SELECT` plus one `loadConversation` round trip, both on the worker thread, never
     blocking the GUI thread that is already rendering the empty window).
   - If empty (brand-new database): calls `createConversation()` directly — the very first run
     behaves exactly like clicking "New Chat" (REQ-F-016).
7. `ChatViewModel::onRepositoryUnavailable(reason)`: sets `persistence_enabled_ = false`,
   `setPersistenceStatusMessage(reason)`, and constructs the **same ephemeral, never-persisted**
   `Conversation` the pre-persistence code path used
   (`ConversationId::generate(), kDefaultConversationTitle, now`) — the app is fully usable,
   exactly as before this cycle, just unsaved (REQ-F-025).

### 2.2 Create new conversation (REQ-F-006, REQ-F-016)

1. QML: `ConversationListPanel`'s "New Chat" button calls `ChatViewModel.createConversation()`.
2. `ChatViewModel::createConversation()`: if `chat_controller_->isStreaming(conversation_->id())` is
   about to be orphaned by replacing `conversation_`, calls `stop()` first (§4.7 — the
   stop-before-replace invariant extended from window-close to every path that reassigns
   `conversation_`). Then, if `persistence_enabled_`, calls `repository_->createConversation()`
   (no title argument — the worker inserts `kDefaultConversationTitle`, "New Chat"). If persistence
   is disabled, falls back to constructing a fresh ephemeral `Conversation` directly and adopting it
   (§2.7) — creating conversations must keep working in fallback mode too.
3. Worker inserts the row (`id`, `"New Chat"`, `created_at = updated_at = now`, `last_model_id = NULL`),
   emits `conversationCreated(summary)`.
4. `ChatViewModel::onConversationCreated(summary)`: `conversation_list_model_->upsertToFront(summary)`,
   then adopts it as the active conversation (§2.7) — "switches to it immediately" (REQ-F-016).

### 2.3 Switch to a saved conversation (REQ-F-017)

1. QML delegate `onClicked` calls `ChatViewModel.switchConversation(id)`.
2. `ChatViewModel::switchConversation(id)`: guards `id != activeConversationId()` (no-op if already
   active); calls `stop()` first — **mandatory**, not optional: `conversation_` is a `shared_ptr`
   about to be replaced, and `ChatController` may hold a live in-flight reference into the old one
   (the accepted risk explicitly flagged in `docs/sdd/ollama-chat-backend/DESIGN.md` §5, "must be
   revisited once a `holonight_persistence`-backed conversation store exists" — this is that
   revisit). Then calls `repository_->loadConversation(id)`.
3. Worker runs `SELECT * FROM conversations WHERE id = ?` + `SELECT * FROM messages WHERE
   conversation_id = ? ORDER BY created_at ASC, rowid ASC` (the `rowid` tie-breaker matters: the
   User+Assistant-placeholder pair from a single `send()` can share an identical millisecond
   timestamp on a fast machine — §4.8), emits `conversationLoaded(LoadedConversation{summary, messages})`.
4. `ChatViewModel::onConversationLoaded(loaded)`: adopts it as the active conversation (§2.7).

### 2.4 The `adoptConversation` sequence (shared by §2.2/2.3/startup and delete's fallback, §2.6)

1. `conversation_ = std::make_shared<Conversation>(id, summary.title, summary.createdAt)` (a fresh
   domain object rebuilt from the record — `Conversation` has no "replace all messages" bulk API,
   nor does it need one: it is reconstructed, not mutated, on every switch), then
   `conversation_->appendMessage(...)` for each loaded message in order.
2. `message_model_->resetFrom(conversation_->messages())` — the one flow in this repo that actually
   exercises `MessageListModel::resetFrom` (previously a defensive-only escape hatch per the
   chat-window-qml DESIGN §5's own footnote — this cycle gives it a real caller).
3. `selected_model_id_ = summary.lastModelId.value_or(availableModelsFirstOrDefault())`, emit
   `selectedModelIdChanged()` — REQ-F-017's "model picker preselects `last_model_id`... or the first
   available model if NULL."
4. `active_conversation_id_ = id`, emit `activeConversationIdChanged()`.
5. `setErrorMessage(QString())`, `refreshComputedProperties()` — same cleanup `send()` already does.

### 2.5 Rename (REQ-F-009, REQ-F-018)

1. QML delegate's inline `TextField`, on `onEditingFinished`, calls
   `ChatViewModel.renameConversation(id, text)`.
2. `ChatViewModel::renameConversation(id, newTitle)`: if `id == activeConversationId()`, calls
   `conversation_->setTitle(newTitle)` immediately (keeps the in-memory object consistent — §4.2).
   Calls `repository_->renameConversation(id, newTitle)` if `persistence_enabled_`.
3. Worker: `UPDATE conversations SET title = :title, updated_at = :now WHERE id = :id`, emits
   `conversationRenamed(summary)`.
4. `ChatViewModel::onConversationRenamed(summary)`: `conversation_list_model_->upsertToFront(summary)`
   (rename refreshes `updated_at`, so the row moves to the top — REQ-F-009's acceptance criterion
   says exactly this).

### 2.6 Delete with inline confirmation (REQ-F-010, REQ-F-019, REQ-NF-009)

1. The confirmation step itself is pure QML state (§4.9) — no C++ involvement until "Yes" is
   clicked, which calls `ChatViewModel.deleteConversation(id)`.
2. `ChatViewModel::deleteConversation(id)`: records `const bool wasActive = (id == activeConversationId())`;
   if `wasActive`, calls `stop()` (about to possibly replace `conversation_` in step 4). Calls
   `repository_->deleteConversation(id)`.
3. Worker, inside one `db.transaction()`: `DELETE FROM messages WHERE conversation_id = :id;` then
   `DELETE FROM conversations WHERE id = :id;`; `db.commit()` (or `db.rollback()` on either
   statement's failure, reporting the transaction as a whole via `error(id, message)` rather than
   `conversationDeleted`). Emits `conversationDeleted(id)` on success. See §4.10 for why both
   statements are issued explicitly even though `ON DELETE CASCADE` alone would suffice.
4. `ChatViewModel::onConversationDeleted(id)`: `conversation_list_model_->removeById(id)`; if
   `wasActive`: `if (auto first = conversation_list_model_->firstConversationId()) { switchConversation(*first); } else { createConversation(); }`
   — REQ-F-019's "switches to the first remaining conversation or creates a new one," expressed with
   the exact two methods already built for §2.2/§2.3, no new code path.

### 2.7 Message persistence during a chat session (REQ-F-012, REQ-F-013, REQ-F-020, REQ-F-021)

Extends the existing `send()`/`onStreamEvent()` flow from `docs/sdd/chat-window-qml/DESIGN.md` §2.2
with four new call sites, all guarded by `if (persistence_enabled_)`:

1. In `send(text)`, **before** calling `chat_controller_->send(...)`: snapshot
   `const bool isFirstMessage = conversation_->messages().empty();`.
2. Immediately after mirroring the User+Assistant-placeholder pair into `message_model_` (the
   existing `msgs[msgs.size()-2]`/`msgs[msgs.size()-1]` reads), add:
   ```cpp
   repository_->persistNewMessage(conversation_->id().toString(), msgs[msgs.size() - 2]);  // User
   repository_->persistNewMessage(conversation_->id().toString(), msgs[msgs.size() - 1]);  // Assistant placeholder
   repository_->updateLastModelId(conversation_->id().toString(), selected_model_id_);
   ```
   `updateLastModelId` deliberately does double duty for REQ-F-014 *and* REQ-F-021 — see Key
   Decision §4.11 for why a separate `updateLastMessageTime` is not introduced.
3. If `isFirstMessage && conversation_->title() == holonight_persistence::kDefaultConversationTitle`:
   `const QString title = holonight_domain::deriveConversationTitle(text); conversation_->setTitle(title); repository_->renameConversation(conversation_->id().toString(), title);`
   — REQ-F-027, reusing rename's existing plumbing rather than a parallel "set initial title" method
   (the guard on the placeholder title, not just `isFirstMessage`, is what stops this from clobbering
   a title the user already set manually before ever sending anything).
4. In `onStreamEvent(event)`, only when the just-updated last message's status is terminal
   (`Complete`, `Error`, or `Cancelled` — never `Streaming`, i.e. never per-`ContentDelta`):
   ```cpp
   repository_->persistMessageSettled(conversation_->id().toString(), conversation_->messages().back());
   ```
   — this is the entire mechanism behind REQ-F-013/REQ-F-020's "exactly 4 writes for a 3-message,
   100-token-streamed conversation" acceptance criterion: two `persistNewMessage` calls from step 2
   (once per `send()`), one `updateLastModelId` (folds in the `updated_at` bump), one
   `persistMessageSettled` on the terminal event. Zero calls happen from any `ContentDelta` branch.
5. `worker->updateLastModelId(...)` emits `lastModelIdUpdated(id, modelId, updatedAt)`;
   `ChatViewModel::onLastModelIdUpdated` calls `conversation_list_model_->touchToFront(id, updatedAt)`
   — REQ-F-021's "list model is notified and re-sorts."

### 2.8 Startup failure vs. mid-session fatal failure (REQ-F-025, REQ-F-026, REQ-NF-005)

Both funnel through the same `unavailable(QString reason)` signal (§4.6) — see §2.1 step 4 for the
startup case. Mid-session: the worker classifies every `QSqlError` it encounters while servicing a
request (§4.10):

- `QSqlError::type() == QSqlError::ConnectionError`, or the connection reports `!db.isOpen()`
  immediately after the failing call → **fatal**: the worker stops serving further requests
  (`available_ = false`), closes its connection, and emits `unavailable(reason)` — REQ-NF-005's
  option (a), "disable persistence for the remainder of the session," with no recovery attempt
  (option (b) is explicitly not implemented — see §4.10).
- Any other `QSqlError` type (e.g. a constraint violation, a malformed statement) → **non-fatal**:
  emits `error(conversationId, message)` only; the connection and worker remain available for the
  next request (REQ-NF-004).

`ChatViewModel::onRepositoryUnavailable` is identical whether it fires during startup or mid-session
— it always sets `persistence_enabled_ = false` and raises the sticky banner. `onRepositoryError`
instead reuses the **existing, transient** `errorMessage` property (§4.6) — the chat session
continues exactly as REQ-NF-004 specifies.

---

## 3. Interfaces / APIs

### 3.1 `holonight_domain` additions

```cpp
// conversation.h — one new method on the existing class, one new free function
namespace holonight_domain {

class Conversation {
  ...
  // Replaces title_ in place and stamps updated_at_ — the same "mutate + stamp" pattern as
  // replaceLastMessage. Used for both auto-derived titles and user-initiated renames.
  void setTitle(QString title);
  ...
};

// Pure string transform (REQ-F-027): first line, or first 48 characters, whichever is shorter.
// No Qt-Sql/threading/UI dependency — testable with plain GTest TEST() cases.
[[nodiscard]] QString deriveConversationTitle(const QString& firstMessageText);

}  // namespace holonight_domain
```

```cpp
// conversation.cpp
void Conversation::setTitle(QString title) {
  title_ = std::move(title);
  updated_at_ = QDateTime::currentDateTimeUtc();
}

QString deriveConversationTitle(const QString& firstMessageText) {
  static constexpr qsizetype kMaxTitleLength = 48;
  const qsizetype newlineIndex = firstMessageText.indexOf(QLatin1Char('\n'));
  QString firstLine = (newlineIndex >= 0 ? firstMessageText.left(newlineIndex) : firstMessageText).trimmed();
  if (firstLine.isEmpty()) {
    return QStringLiteral("Untitled");
  }
  if (firstLine.length() > kMaxTitleLength) {
    return firstLine.left(kMaxTitleLength).trimmed() + QStringLiteral("…");  // "…"
  }
  return firstLine;
}
```

### 3.2 `src/persistence/include/holonight_persistence/conversation_record.h`

```cpp
#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>

#include <holonight_domain/holonight_domain.h>
#include <optional>
#include <vector>

namespace holonight_persistence {

inline const QString kDefaultConversationTitle = QStringLiteral("New Chat");

struct ConversationSummary {
  QString id;
  QString title;
  QDateTime createdAt;
  QDateTime updatedAt;
  std::optional<holonight_domain::ModelId> lastModelId;

  friend bool operator==(const ConversationSummary&, const ConversationSummary&) = default;
};

struct LoadedConversation {
  ConversationSummary summary;
  std::vector<holonight_domain::Message> messages;  // chronological, ascending created_at

  friend bool operator==(const LoadedConversation&, const LoadedConversation&) = default;
};

// Idempotent. Called once from SqliteConversationRepository's constructor (and directly by tests
// that connect to a repository's signals without going through that constructor) — every type used
// as a queued cross-thread signal parameter must be registered before the first such emission, or
// the connection silently drops the call with a runtime warning, not a compile error (§8 risk).
void registerMetaTypes();

}  // namespace holonight_persistence

Q_DECLARE_METATYPE(holonight_persistence::ConversationSummary)
Q_DECLARE_METATYPE(holonight_persistence::LoadedConversation)
Q_DECLARE_METATYPE(holonight_domain::ModelId)
```

`ModelId` is registered here, in the downstream consumer, not inside `holonight_domain` itself —
`Q_DECLARE_METATYPE` needs no access to a type's internals and doesn't require touching
`model_id.h`, preserving `holonight_domain`'s existing "no Qt-registration wrappers" boundary
(chat-window-qml DESIGN §6.2 made the same choice for QML registration; this extends it to
`QMetaType` registration).

### 3.3 `src/persistence/include/holonight_persistence/conversation_repository.h`

```cpp
#pragma once

#include "holonight_persistence/conversation_record.h"

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

#include <holonight_domain/holonight_domain.h>

namespace holonight_persistence {

// Abstract interface (REQ-C-006). QObject-derived — unlike holonight_providers::HttpClient's
// plain-callback shape, this interface's results must cross a real thread boundary (REQ-F-022), so
// it uses Qt's signal/slot system rather than std::function (see Key Decision §4.3 for why these
// two sibling modules deliberately use different idioms).
class ConversationRepository : public QObject {
  Q_OBJECT

 public:
  explicit ConversationRepository(QObject* parent = nullptr);
  ~ConversationRepository() override = default;

  ConversationRepository(const ConversationRepository&) = delete;
  ConversationRepository& operator=(const ConversationRepository&) = delete;
  ConversationRepository(ConversationRepository&&) = delete;
  ConversationRepository& operator=(ConversationRepository&&) = delete;

  // Opens/creates the database and applies pending migrations (REQ-F-001..004). Call exactly once
  // before any method below. Non-blocking; emits initialized() or unavailable(reason).
  virtual void initialize() = 0;

  // Every method below returns void immediately (REQ-F-023/REQ-NF-001); results/errors arrive later
  // via the signals below.
  virtual void createConversation(QString title = {}) = 0;
  virtual void listConversations() = 0;
  virtual void loadConversation(QString conversationId) = 0;
  virtual void renameConversation(QString conversationId, QString newTitle) = 0;
  virtual void deleteConversation(QString conversationId) = 0;
  virtual void updateLastModelId(QString conversationId, holonight_domain::ModelId modelId) = 0;
  virtual void persistNewMessage(QString conversationId, holonight_domain::Message message) = 0;
  virtual void persistMessageSettled(QString conversationId, holonight_domain::Message message) = 0;

 Q_SIGNALS:
  void initialized();
  // Fires on startup failure (REQ-F-025) AND on a fatal mid-session failure (REQ-NF-005) — see
  // §2.8/§4.10 for the classification rule and why one signal covers both.
  void unavailable(QString reason);

  void conversationCreated(holonight_persistence::ConversationSummary conversation);
  void conversationListLoaded(QList<holonight_persistence::ConversationSummary> conversations);
  void conversationLoaded(holonight_persistence::LoadedConversation conversation);
  void conversationRenamed(holonight_persistence::ConversationSummary conversation);
  void conversationDeleted(QString conversationId);
  void lastModelIdUpdated(QString conversationId, holonight_domain::ModelId modelId, QDateTime updatedAt);

  // Non-fatal, single-operation failure (REQ-NF-004) — the repository remains usable afterward.
  void error(QString conversationId, QString message);
};

}  // namespace holonight_persistence
```

### 3.4 `src/persistence/include/holonight_persistence/detail/conversation_repository_worker.h`

```cpp
#pragma once

#include "holonight_persistence/conversation_record.h"

#include <QObject>
#include <QSqlError>
#include <QString>

#include <holonight_domain/holonight_domain.h>

namespace holonight_persistence::detail {

// Lives on the dedicated worker thread for its entire lifetime (REQ-F-022). Owns the single
// QSqlDatabase connection. Not part of the public API — constructed and owned exclusively by
// SqliteConversationRepository; its header is reachable (not physically hidden) specifically so
// tests/persistence/test_conversation_repository_worker_threading.cpp can white-box-verify thread
// affinity (§7.2) without SqliteConversationRepository needing to expose that as a public seam.
class ConversationRepositoryWorker : public QObject {
  Q_OBJECT

 public:
  // connectionName must be unique across the process (Qt requires this for QSqlDatabase — §4.4);
  // SqliteConversationRepository always generates one internally, callers never supply it.
  ConversationRepositoryWorker(QString databasePath, QString connectionName, QObject* parent = nullptr);
  ~ConversationRepositoryWorker() override;

 public Q_SLOTS:
  void openAndMigrate();
  void createConversation(QString title);
  void listConversations();
  void loadConversation(QString conversationId);
  void renameConversation(QString conversationId, QString newTitle);
  void deleteConversation(QString conversationId);
  void updateLastModelId(QString conversationId, holonight_domain::ModelId modelId);
  void persistNewMessage(QString conversationId, holonight_domain::Message message);
  void persistMessageSettled(QString conversationId, holonight_domain::Message message);

 Q_SIGNALS:
  void initialized();
  void unavailable(QString reason);
  void conversationCreated(holonight_persistence::ConversationSummary conversation);
  void conversationListLoaded(QList<holonight_persistence::ConversationSummary> conversations);
  void conversationLoaded(holonight_persistence::LoadedConversation conversation);
  void conversationRenamed(holonight_persistence::ConversationSummary conversation);
  void conversationDeleted(QString conversationId);
  void lastModelIdUpdated(QString conversationId, holonight_domain::ModelId modelId, QDateTime updatedAt);
  void error(QString conversationId, QString message);

 private:
  [[nodiscard]] QSqlDatabase connection();
  void markUnavailable(const QString& reason);
  void reportOperationError(const QString& conversationId, const QSqlError& sqlError);
  [[nodiscard]] static bool isFatal(const QSqlError& sqlError);

  QString database_path_;
  QString connection_name_;
  bool available_ = false;
};

}  // namespace holonight_persistence::detail
```

### 3.5 `src/persistence/include/holonight_persistence/sqlite_conversation_repository.h`

```cpp
#pragma once

#include "holonight_persistence/conversation_repository.h"
#include "holonight_persistence/detail/conversation_repository_worker.h"

#include <QThread>

namespace holonight_persistence {

// GUI-thread-resident façade (REQ-F-022/023/024). Owns a QThread and a private
// detail::ConversationRepositoryWorker moved onto it (QObject::moveToThread — see Key Decision
// §4.4 for why this is chosen over subclassing QThread). Every public method here does nothing but
// package the request and hand it to the worker via a functor-based, context-guarded
// QMetaObject::invokeMethod(..., Qt::QueuedConnection) call — no SQL type is ever named in this
// class's own logic.
class SqliteConversationRepository : public ConversationRepository {
  Q_OBJECT

 public:
  explicit SqliteConversationRepository(QString databasePath, QObject* parent = nullptr);
  ~SqliteConversationRepository() override;

  void initialize() override;
  void createConversation(QString title = {}) override;
  void listConversations() override;
  void loadConversation(QString conversationId) override;
  void renameConversation(QString conversationId, QString newTitle) override;
  void deleteConversation(QString conversationId) override;
  void updateLastModelId(QString conversationId, holonight_domain::ModelId modelId) override;
  void persistNewMessage(QString conversationId, holonight_domain::Message message) override;
  void persistMessageSettled(QString conversationId, holonight_domain::Message message) override;

 private:
  QThread worker_thread_;
  detail::ConversationRepositoryWorker* worker_;  // owned by worker_thread_ (moveToThread)
};

}  // namespace holonight_persistence
```

Construction sketch (full rationale in §4.4):

```cpp
SqliteConversationRepository::SqliteConversationRepository(QString databasePath, QObject* parent)
    : ConversationRepository(parent),
      worker_(new detail::ConversationRepositoryWorker(
          std::move(databasePath), QUuid::createUuid().toString(QUuid::WithoutBraces))) {
  registerMetaTypes();
  worker_->moveToThread(&worker_thread_);
  connect(&worker_thread_, &QThread::finished, worker_, &QObject::deleteLater);

  connect(worker_, &detail::ConversationRepositoryWorker::initialized, this, &ConversationRepository::initialized);
  connect(worker_, &detail::ConversationRepositoryWorker::unavailable, this, &ConversationRepository::unavailable);
  connect(worker_, &detail::ConversationRepositoryWorker::conversationCreated, this,
          &ConversationRepository::conversationCreated);
  // ... one forwarding connect() per remaining signal, all AutoConnection (resolves to Queued —
  // sender/receiver live on different threads, §4.4).

  worker_thread_.start();
}

void SqliteConversationRepository::initialize() {
  QMetaObject::invokeMethod(worker_, [w = worker_] { w->openAndMigrate(); }, Qt::QueuedConnection);
}

void SqliteConversationRepository::loadConversation(QString conversationId) {
  QMetaObject::invokeMethod(
      worker_, [w = worker_, id = std::move(conversationId)] { w->loadConversation(id); }, Qt::QueuedConnection);
}

SqliteConversationRepository::~SqliteConversationRepository() {
  worker_thread_.quit();
  worker_thread_.wait();
}
```

### 3.6 `src/persistence/include/holonight_persistence/migration_runner.h`

```cpp
#pragma once

#include <QSqlDatabase>
#include <QString>

#include <expected>
#include <vector>

namespace holonight_persistence {

class MigrationRunner {
 public:
  struct Migration {
    int version = 0;
    QString name;  // e.g. "0001_init" — logging/error messages only, never parsed
    QString sql;   // semicolon-separated DDL statements, no embedded literal ';' (§4.7)
  };

  // Reads every migration embedded via Qt resources (src/persistence/CMakeLists.txt), in ascending
  // version order (REQ-C-003).
  [[nodiscard]] static std::vector<Migration> builtInMigrations();

  // Applies every migration whose version exceeds the database's current schema_version, each
  // inside its own transaction, in ascending order (REQ-F-003/004, REQ-NF-009). Bootstraps
  // schema_version's absence as version 0. Returns the resulting version, or the first failure.
  [[nodiscard]] static std::expected<int, QString> apply(QSqlDatabase& db, const std::vector<Migration>& migrations);

 private:
  [[nodiscard]] static std::expected<int, QString> currentVersion(QSqlDatabase& db);
  [[nodiscard]] static std::expected<void, QString> applyOne(QSqlDatabase& db, const Migration& migration);
  [[nodiscard]] static std::expected<void, QString> executeStatements(QSqlDatabase& db, const QString& sql);
};

}  // namespace holonight_persistence
```

`apply()`'s per-migration sequence: `db.transaction()`; `executeStatements(db, migration.sql)`
(migration DDL only — no `schema_version` bookkeeping in the `.sql` file itself); on success, one
more `QSqlQuery` inserting `(migration.version, <now>)` into `schema_version`; `db.commit()` — or
`db.rollback()` and return the error on any failure. This is why `0001_init.sql` (§3.8) is
responsible for *creating* the `schema_version` table but never for inserting into it — the runner
owns that generically, so migration 0002+ files (a future cycle) never need to repeat that logic.

`executeStatements` splits on `;`, trims, skips empty fragments, and runs each through its own
`QSqlQuery::exec()` — the QSQLite driver does not reliably support multiple statements in one
`exec()` call, so this project never relies on that:

```cpp
std::expected<void, QString> MigrationRunner::executeStatements(QSqlDatabase& db, const QString& sql) {
  for (const QString& raw : sql.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
    const QString statement = raw.trimmed();
    if (statement.isEmpty()) continue;
    QSqlQuery query(db);
    if (!query.exec(statement)) {
      return std::unexpected(QStringLiteral("migration '%1' failed: %2").arg(/*name*/ QString{}, query.lastError().text()));
    }
  }
  return {};
}
```

### 3.7 `src/persistence/include/holonight_persistence/database_path.h`

```cpp
#pragma once

#include <QString>

namespace holonight_persistence {

// REQ-F-001: $XDG_DATA_HOME/holonight-ai/conversations.db, falling back to
// ~/.local/share/holonight-ai/conversations.db. Creates the holonight-ai/ directory if missing
// (mkpath is a no-op, not an error, if it already exists). Stateless free function — testable by
// toggling the XDG_DATA_HOME environment variable around the call (tests/persistence/test_database_path.cpp).
[[nodiscard]] QString resolveDatabaseFilePath();

}  // namespace holonight_persistence
```

### 3.8 `src/persistence/migrations/0001_init.sql`

```sql
CREATE TABLE schema_version (
    version INTEGER PRIMARY KEY,
    applied_at TIMESTAMP NOT NULL
);

CREATE TABLE conversations (
    id TEXT PRIMARY KEY,
    title TEXT NOT NULL,
    created_at TIMESTAMP NOT NULL,
    updated_at TIMESTAMP NOT NULL,
    last_model_id TEXT
);

CREATE TABLE messages (
    id TEXT PRIMARY KEY,
    conversation_id TEXT NOT NULL,
    role TEXT NOT NULL,
    content TEXT NOT NULL,
    status TEXT NOT NULL,
    created_at TIMESTAMP NOT NULL,
    FOREIGN KEY (conversation_id) REFERENCES conversations(id) ON DELETE CASCADE
);

CREATE INDEX idx_conversations_updated_at ON conversations(updated_at DESC);
CREATE INDEX idx_messages_conversation_created ON messages(conversation_id, created_at);
```

(No `INSERT INTO schema_version` here — `MigrationRunner::applyOne` adds that row generically, §3.6.)

### 3.9 `src/application/include/holonight_application/conversation_list_model.h`

```cpp
#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <holonight_persistence/conversation_record.h>
#include <optional>
#include <vector>

namespace holonight_application {

class ConversationListModel : public QAbstractListModel {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Instantiated only by ChatViewModel; do not construct from QML")

 public:
  enum Roles : std::uint16_t {  // NOLINT(cppcoreguidelines-use-enum-class)
    IdRole = Qt::UserRole + 1,
    TitleRole,
    UpdatedAtRole,  // pre-formatted QString for display
  };
  Q_ENUM(Roles)

  explicit ConversationListModel(QObject* parent = nullptr);

  [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  // Full reset — conversationListLoaded, already sorted updated_at DESC by the repository's SQL.
  void setAll(const QList<holonight_persistence::ConversationSummary>& conversations);

  // Inserts (if new) or moves-to-front-and-updates (if existing) — conversationCreated and
  // conversationRenamed both imply a fresh updated_at (REQ-F-021).
  void upsertToFront(const holonight_persistence::ConversationSummary& conversation);

  // Moves an existing row to front and refreshes its timestamp only — lastModelIdUpdated.
  void touchToFront(const QString& id, const QDateTime& updatedAt);

  void removeById(const QString& id);

  // Plain C++ accessors (not Q_PROPERTY/Q_INVOKABLE) — used by ChatViewModel's delete-fallback
  // logic (REQ-F-019), mirroring ChatViewModel::conversation()'s own C++-only precedent.
  [[nodiscard]] std::optional<QString> firstConversationId() const;
  [[nodiscard]] bool isEmpty() const;

 private:
  struct Row {
    QString id;
    QString title;
    QDateTime updatedAt;
  };

  [[nodiscard]] static Row toRow(const holonight_persistence::ConversationSummary& conversation);

  std::vector<Row> rows_;
};

}  // namespace holonight_application
```

### 3.10 `ChatViewModel` growth (additions to the existing header)

```cpp
// New Q_PROPERTYs
Q_PROPERTY(holonight_application::ConversationListModel* conversationList READ conversationList CONSTANT)
Q_PROPERTY(QString activeConversationId READ activeConversationId NOTIFY activeConversationIdChanged)
Q_PROPERTY(QString persistenceStatusMessage READ persistenceStatusMessage NOTIFY persistenceStatusMessageChanged)

// New constructor parameter
explicit ChatViewModel(std::shared_ptr<holonight_providers::OllamaProvider> provider,
                       std::unique_ptr<holonight_persistence::ConversationRepository> repository,
                       QObject* parent = nullptr);

// New invokables
Q_INVOKABLE void createConversation();
Q_INVOKABLE void switchConversation(const QString& conversationId);
Q_INVOKABLE void renameConversation(const QString& conversationId, const QString& newTitle);
Q_INVOKABLE void deleteConversation(const QString& conversationId);
Q_INVOKABLE void dismissPersistenceBanner();

// New signals
void activeConversationIdChanged();
void persistenceStatusMessageChanged();

// New private members
std::unique_ptr<holonight_persistence::ConversationRepository> repository_;
ConversationListModel* conversation_list_model_;  // child QObject, parented to `this`
QString active_conversation_id_;
QString persistence_status_message_;
bool persistence_enabled_ = false;
```

`create()`'s factory grows accordingly:

```cpp
ChatViewModel* ChatViewModel::create(QQmlEngine*, QJSEngine*) {
  auto http_client = std::make_shared<holonight_providers::QtNetworkHttpClient>();
  auto provider = std::make_shared<holonight_providers::OllamaProvider>(http_client);
  auto repository = std::make_unique<holonight_persistence::SqliteConversationRepository>(
      holonight_persistence::resolveDatabaseFilePath());
  return new ChatViewModel(std::move(provider), std::move(repository));
}
```

### 3.11 QML skeleton — `qml/workspace/ConversationListPanel.qml`

```qml
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import HolonightChat
import Holonight

ColumnLayout {
    id: root
    spacing: HoloniightPalette.controlPadding

    Button {
        Layout.fillWidth: true
        text: qsTr("New Chat")
        onClicked: ChatViewModel.createConversation()
    }

    ListView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        model: ChatViewModel.conversationList

        delegate: ConversationListDelegate {
            width: ListView.view.width
            required property string conversationId
            required property string title
            required property string updatedAt

            isActive: conversationId === ChatViewModel.activeConversationId
            onActivateRequested: ChatViewModel.switchConversation(conversationId)
            onRenameRequested: newTitle => ChatViewModel.renameConversation(conversationId, newTitle)
            onDeleteConfirmed: ChatViewModel.deleteConversation(conversationId)
        }
    }
}
```

`ConversationListDelegate.qml` owns the double-click-to-rename `TextField` swap and the
click-to-confirm trash icon entirely as local `property bool editing`/`property bool confirmingDelete`
state (§4.9) — neither reaches `ChatViewModel` until the user actually commits an edit or confirms
a delete.

`WorkspaceWindow.qml` adds, alongside the existing error banner:

```qml
Rectangle {
    Layout.fillWidth: true
    visible: ChatViewModel.persistenceStatusMessage.length > 0
    color: HoloniightPalette.surfaceMuted
    // ... same shape as the existing errorMessage banner, plus a dismiss button calling
    // ChatViewModel.dismissPersistenceBanner()
}
```

---

## 4. Key Decisions With Rationale

### 4.1 `deriveConversationTitle` is a free function in `holonight_domain`, not a `ChatViewModel` or repository method

It is a pure string transform with no dependency on Qt-Sql, threading, or any repository/view-model
state — the same reasoning that already puts `Message`'s status machine in `holonight_domain` rather
than in `ChatController`. Keeping it there means it is unit-testable with a plain
`TEST(Conversation, DeriveTitleTruncatesFirstLine)` in `tests/domain/test_conversation.cpp`,
independent of any persistence or QML plumbing, and reusable if a future cycle ever needs to
re-derive a title elsewhere.

### 4.2 `last_model_id` is NOT added to `holonight_domain::Conversation`

The SPEC's own acceptance criterion phrasing ("`Conversation::lastModelId()` **or similar**
accessor") leaves this open. `ChatViewModel` already owns a `selected_model_id_`
(`QVariantMap`-backed) property for exactly this purpose — adding a second, parallel storage
location on `Conversation` would create two sources of truth for "what model is this conversation
using" with no code path that needs both. `last_model_id` instead lives purely in
`holonight_persistence::ConversationSummary`/`LoadedConversation` (the DB-shaped record) and flows
into `ChatViewModel::selected_model_id_` at load time (§2.4) — `Conversation` itself stays a pure
in-memory transcript, exactly as the two prior cycles established it, with zero new
persistence-shaped fields. `Conversation::setTitle` is kept, by contrast, purely so the *existing*
`title_` field (which `Conversation` already had before this cycle) doesn't silently go stale — a
narrower, already-present-field consistency fix, not a new persistence-shaped addition.

### 4.3 `ConversationRepository` uses `QObject` signals, not `std::function` callbacks — a deliberate divergence from `HttpClient`

`holonight_providers::HttpClient` (the prior cycle's interface precedent) uses plain
`std::function` callbacks specifically because its calls always resolve on the same
(Qt event-loop) thread they were issued from — `QNetworkReply`'s own callbacks already run there.
`ConversationRepository`'s calls resolve on a **different thread** by design (REQ-F-022/024): a
worker thread's result must cross back to the GUI thread, and Qt's own signal/slot system with
automatic queued connections is the idiomatic, battle-tested mechanism for exactly this — the
alternative (hand-rolling a thread-safe callback queue drained by a `QTimer` on the GUI thread) would
just re-implement what `QObject`/`QMetaObject::invokeMethod` already provides for free, including the
context-object safety Qt's queued connections already give against a destroyed receiver (mirroring
the `QPointer` guard `ChatViewModel` hand-rolled for `HttpClient` callbacks in the previous cycle —
here it comes for free from `connect()`'s own object-lifetime tracking).

### 4.4 Threading mechanism: a private worker `QObject` + `moveToThread`, not a `QThread` subclass

Qt's own documentation discourages subclassing `QThread` except when overriding `run()` is itself
the point; the recommended, more composable pattern for "run this existing QObject-based logic on a
background thread" is exactly what's used here: `detail::ConversationRepositoryWorker` is a normal
`QObject` moved onto a plain `QThread` member of `SqliteConversationRepository`. This keeps
`QSqlDatabase`/`QSqlQuery` usage entirely inside ordinary member functions (no `run()` override to
reason about), lets the worker be default-thread-affine to wherever it's moved (needed since
`QSqlDatabase` connections are only usable from the thread that created them — the worker's
`openAndMigrate()` calls `QSqlDatabase::addDatabase` itself, on the worker thread, for exactly this
reason), and matches the exact pattern from Qt's "Using a Worker Object" documentation
(`moveToThread` + `connect(thread, &QThread::finished, worker, &QObject::deleteLater)` +
`thread.quit(); thread.wait();` at teardown — §3.5's destructor).

Every public method on `SqliteConversationRepository` uses the **functor-based**
`QMetaObject::invokeMethod(worker_, [...]{ ... }, Qt::QueuedConnection)` overload rather than the
older string-slot-name + `Q_ARG` form — it is compile-time type-checked, and, crucially, its captured
arguments (a `QString conversationId`, a `holonight_domain::Message`) travel inside the posted
functor's own closure, not through `QMetaType`'s `QVariant`-boxing machinery — so **no
`Q_DECLARE_METATYPE` registration is needed for the request direction at all**. Registration is only
required for the **result** direction (`conversationCreated`, `conversationListLoaded`, etc. — real
`Q_SIGNALS`, not functor invocations), which is exactly what `registerMetaTypes()` (§3.2) covers.

The connection name each `SqliteConversationRepository` instance uses is **always internally
generated** (`QUuid::createUuid().toString(QUuid::WithoutBraces)`), never caller-supplied — Qt
requires unique `QSqlDatabase` connection names process-wide, and pushing that requirement onto
every call site (production code, every GTest fixture) would be a needless, easy-to-forget
foot-gun. Generating it once, internally, at construction guarantees uniqueness by construction
regardless of how many repository instances a test binary creates over its lifetime (§7.1).

### 4.5 Migrations are packaged as Qt resources, not a runtime `migrations/` directory

`0001_init.sql` is embedded via `qt_add_resources(holonight_persistence ...)` into the binary at
`:/holonight_persistence/migrations/0001_init.sql`, read via `QFile`/`QTextStream` at startup —
mirroring how `apps/chat` already embeds every `.qml` file via `qt_add_qml_module`'s resource
system. A filesystem `migrations/` directory read relative to the executable would need its own
fragile path-resolution logic (differing between an in-place build, `task run`, and an installed
`DESTINATION ${CMAKE_INSTALL_BINDIR}` layout) for no benefit — Qt resources guarantee the migration
content is always found, identically, in every build/run/test configuration, with zero working-
directory sensitivity. Compiled-in C++ string constants (the third option REQ-C-003 gestures at)
were rejected as strictly worse for the same content: `.sql` files under `src/persistence/migrations/`
stay syntax-highlighted, diffable, and reviewable as SQL, rather than as escaped C++ string literals.

### 4.6 Two distinct banner mechanisms — `errorMessage` (existing, transient) vs. `persistenceStatusMessage` (new, sticky)

`ChatViewModel::send()` already unconditionally clears `error_message_` on every call (chat-window-qml
DESIGN §2.2 step 6) — reusing it for "persistence is unavailable" would mean the banner silently
disappears the next time the user sends a message, directly contradicting REQ-F-026's "shall persist
for the lifetime of the session (or until manually dismissed)." So a second property,
`persistenceStatusMessage`, is added: set once by `onRepositoryUnavailable` (fired either at startup
or, per §2.8, on a fatal mid-session failure), cleared only by the new
`dismissPersistenceBanner()` invokable — never implicitly by `send()`/`regenerate()`/`stop()`.
Conversely, REQ-NF-004's *single-operation* failures (`ConversationRepository::error(...)`) **do**
reuse the existing, transient `errorMessage` — that requirement's own wording ("user sees an inline
error message but can continue chatting") describes exactly the same transient shape `errorMessage`
already has for stream failures; introducing a third banner type for this case would add a
distinction with no behavioral difference.

### 4.7 Delete issues both `DELETE` statements explicitly, even though `ON DELETE CASCADE` alone would suffice

REQ-F-010's acceptance criterion is explicit and binding ("executes: `DELETE FROM messages...`
followed by `DELETE FROM conversations...`"), not hedged with "or equivalent" the way REQ-F-021 is.
Beyond honoring that literally, there is a genuine reason to do both: SQLite disables foreign-key
enforcement (and therefore `ON DELETE CASCADE`) **per connection** unless `PRAGMA foreign_keys = ON`
is explicitly issued after every `.open()` call (§2.1 step 4 does this once, at startup, on the
worker's single long-lived connection) — relying solely on the cascade would silently leave orphaned
`messages` rows if that `PRAGMA` were ever accidentally dropped from the startup sequence in a future
change. Issuing both statements explicitly, inside one transaction (REQ-NF-009), makes deletion
correct independent of the `PRAGMA`'s state, while the foreign key constraint itself remains
defense-in-depth (REQ-NF-010's "attempting to insert a message with a non-existent conversation_id
fails").

### 4.8 `ORDER BY created_at ASC, rowid ASC` for messages (and `updated_at DESC, rowid DESC` for conversations)

A single `send()` call inserts the User message and the Assistant placeholder back-to-back, close
enough in wall-clock time that they can share an identical millisecond-resolution `created_at` value
on a fast machine — `created_at` alone is not a reliable total order. SQLite's implicit `rowid`
(present on every table here, since none uses `WITHOUT ROWID`) reflects true insertion order and
costs nothing extra to sort by, making the tie-break deterministic without adding a dedicated
sequence column.

### 4.9 Rename/delete-confirmation UI state lives entirely in QML, not in `ChatViewModel`

Whether a given row is currently showing its inline `TextField` or its "Delete? [Yes] [No]" prompt
is pure presentation state with no business-logic implication and no other component ever needs to
observe it — keeping it as a per-delegate `property bool` (toggled by clicks, reset on
cancel/commit) avoids adding "which conversation is currently mid-edit" tracking to `ChatViewModel`
for a concern QML can represent locally and more simply. `ChatViewModel` only ever hears about the
*committed* result (`renameConversation`/`deleteConversation`, called once the user confirms).

### 4.10 Mid-session failure classification: fatal (connection-level) vs. non-fatal (statement-level), no recovery attempt

REQ-NF-005 explicitly frames recovery/rebuild as optional ("(a) disable persistence... or (b)
attempt a recovery/rebuild... exact strategy is a Design decision"). This design implements only
(a): the worker inspects `QSqlError::type()` after any failing statement; `ConnectionError` (or the
connection reporting `!db.isOpen()`) is treated as fatal — the worker marks itself unavailable and
emits `unavailable(reason)`, permanently, for the rest of the session — while any other error type
(a constraint violation, a malformed statement — both attributable to this code's own bugs rather
than the underlying storage) is treated as a recoverable, single-operation `error(...)` that leaves
the connection open for the next request. Attempting an actual rebuild/recovery (option (b)) was
rejected as unjustified scope for this cycle: SQLite corruption recovery is a nontrivial, failure-
mode-specific undertaking (`.recover`-style dump-and-rebuild), and REQ-NF-005's own acceptance
criterion accepts "code review" as sufficient verification for whichever option is chosen — the
simpler, always-safe option (a) is the correct default until real-world corruption reports justify
building (b).

### 4.11 REQ-F-014 and REQ-F-021 share one repository call — `updateLastModelId`, not a separate `updateLastMessageTime`

REQ-F-021's acceptance criterion itself says "`ConversationRepository::updateLastMessageTime(...)`
(**or equivalent**)" — every code path that persists a new message via `send()`/`regenerate()`
always has a concrete selected model at that point (`ChatViewModel::canSend()` already requires one),
so there is no scenario where a message is persisted without also knowing the model id to stamp.
Consolidating into `updateLastModelId` (already required by REQ-F-014, already documented as
bumping `updated_at`) means exactly one `UPDATE conversations SET last_model_id = ?, updated_at = ?`
per send, not two separate statements/round trips doing overlapping work.

### 4.12 `ConversationListModel` follows `MessageListModel`'s narrow-mutator precedent, not a generic resync method

`setAll`/`upsertToFront`/`touchToFront`/`removeById` are each a direct, minimal `QAbstractListModel`
operation (`beginResetModel`/`beginInsertRows`/`beginMoveRows`/`beginRemoveRows` respectively) tied
to exactly one repository signal each — the same reasoning `docs/sdd/chat-window-qml/DESIGN.md` §6.5
already gave for `MessageListModel`'s `appendMessage`/`updateLastMessage` split: `ChatViewModel`
always knows exactly which operation applies at each call site, so a targeted call is simpler and
cheaper than diffing the whole list after every event.

---

## 5. Alternatives Considered

- **A generic `ConversationRepository::resync()`/full-reload method as the primary update path**
  (rejected, §4.12): would force a full `ListView` rebind on every single-row change (create,
  rename, or a message send bumping `updated_at`) — the exact anti-pattern REQ-F-008's streaming
  requirement already ruled out for `MessageListModel` in the prior cycle, generalized here.
- **`std::function` callbacks for `ConversationRepository` instead of `QObject` signals** (rejected,
  §4.3): would require hand-rolling the exact cross-thread marshaling and receiver-lifetime safety
  Qt's signal/slot system already provides, for no benefit — unlike `HttpClient`, this interface's
  calls genuinely cross a real thread boundary.
- **Subclassing `QThread` and overriding `run()`** (rejected, §4.4): Qt's own guidance reserves this
  for cases that need to customize the thread's event-loop behavior itself; the worker-object +
  `moveToThread` pattern is the documented, more testable default for "run existing QObject logic
  in the background," and keeps `QSqlDatabase` usage in ordinary member functions.
- **A runtime `migrations/` directory read from disk relative to the executable** (rejected, §4.5):
  fragile across build/run/install layouts; Qt resources make migration content location-independent.
- **Reusing `errorMessage` for the persistence-unavailable banner** (rejected, §4.6): `send()`
  already unconditionally clears `errorMessage`, which would make the "unavailable" banner
  disappear on the next message — directly contradicting REQ-F-026's session-lifetime requirement.
- **A `ConversationListViewModel` singleton, separate from `ChatViewModel`** (considered, then
  rejected): every REQ-F-01x/016–019 acceptance criterion names `ChatViewModel::createConversation()`
  /`switchConversation()`/`renameConversation()`/`deleteConversation()` literally — introducing a
  second singleton the SPEC never asks for would add an extra QML import and an extra cross-object
  wiring seam for no requirement that needs it.
- **Attempting database recovery/rebuild on corruption (REQ-NF-005 option (b))** (rejected, §4.10):
  unjustified scope for this cycle; disabling persistence for the remainder of the session is
  simpler, always safe, and explicitly permitted by the SPEC's own phrasing.
- **Parsing the migration version number out of each `.sql` filename** (rejected, §3.6): an explicit
  `{version, resourcePath}` pairing hardcoded in `MigrationRunner::builtInMigrations()` is simpler
  and avoids a regex/filename-convention dependency for a one-entry list today.
- **Adding `Conversation::lastModelId()`/`setLastModelId()` to `holonight_domain`** (rejected, §4.2):
  would create a second source of truth alongside `ChatViewModel::selected_model_id_` with no code
  path needing both.

---

## 6. `CMakeLists.txt` Changes

### 6.1 `src/persistence/CMakeLists.txt` — `INTERFACE` → `STATIC`

```cmake
add_library(holonight_persistence STATIC
    include/holonight_persistence/conversation_record.h
    include/holonight_persistence/conversation_repository.h
    include/holonight_persistence/sqlite_conversation_repository.h
    include/holonight_persistence/detail/conversation_repository_worker.h
    include/holonight_persistence/migration_runner.h
    include/holonight_persistence/database_path.h
    src/conversation_record.cpp
    src/conversation_repository.cpp
    src/sqlite_conversation_repository.cpp
    src/conversation_repository_worker.cpp
    src/migration_runner.cpp
    src/database_path.cpp
)

target_include_directories(holonight_persistence PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(holonight_persistence PUBLIC
    holonight_domain
    Qt6::Core
    Qt6::Sql
)

target_compile_features(holonight_persistence PUBLIC cxx_std_23)

qt_add_resources(holonight_persistence "holonight_persistence_migrations"
    PREFIX "/holonight_persistence"
    FILES
        migrations/0001_init.sql
)
```

Every `Q_OBJECT` header (`conversation_repository.h`, `sqlite_conversation_repository.h`,
`detail/conversation_repository_worker.h`) is listed directly as a target source, not left to be
discovered only transitively via `#include` — per the `feedback_chat_window_qml`-cycle memory's
gotcha #1: AUTOMOC's from-scratch dependency scan needs this to reliably find them.
`CMAKE_AUTOMOC ON` is already set globally in the root `CMakeLists.txt`, so no per-target
`AUTOMOC` property is needed here (matching the same conclusion `holonight_application`'s own
CMakeLists reached in the chat-window-qml cycle).

### 6.2 `src/application/CMakeLists.txt` — add `ConversationListModel`, link `holonight_persistence`

```diff
 add_library(holonight_application STATIC
     src/chat_controller.cpp
     src/chat_view_model.cpp
     src/message_list_model.cpp
+    src/conversation_list_model.cpp
 )

 target_link_libraries(holonight_application PUBLIC
     holonight_domain
     holonight_providers
+    holonight_persistence
     Qt6::Core
     Qt6::Qml
 )
```

(`conversation_list_model.h` also needs listing as a target source for the same AUTOMOC reason as
§6.1 — it is a new `Q_OBJECT` type.) `holonight_persistence` is `PUBLIC` because
`chat_view_model.h` includes `holonight_persistence/conversation_repository.h` by value
(`std::unique_ptr<ConversationRepository>` member) — consumers of `holonight_application`'s headers
need that transitively, exactly as `holonight_providers` already is.

### 6.3 `apps/chat/CMakeLists.txt` — **no change needed**

`holonight_persistence` is already listed in the existing `target_link_libraries(holonight-chat
PRIVATE ...)` block (it was linked even while still an empty `INTERFACE` target). The metatype
extract/merge/register sequence only inspects `holonight_application` — the sole new `QML_ELEMENT`
type this cycle (`ConversationListModel`) lives there, so it is picked up automatically by the
existing `qt6_extract_metatypes(holonight_application ...)` call with no additional wiring.

### 6.4 `tests/CMakeLists.txt`

```diff
 add_executable(test_holonight_ai
   main.cpp
   test_placeholder.cpp
   domain/test_message.cpp
   domain/test_conversation.cpp
   domain/test_model_id.cpp
   domain/test_stream_event.cpp
   providers/test_ollama_provider.cpp
   application/test_chat_controller.cpp
   application/test_chat_view_model.cpp
+  application/test_conversation_list_model.cpp
+  persistence/test_database_path.cpp
+  persistence/test_migrations.cpp
+  persistence/test_conversation_repository_worker_threading.cpp
+  persistence/test_conversation_repository.cpp
 )
 target_link_libraries(test_holonight_ai PRIVATE
   GTest::gtest
   GTest::gmock
   Qt6::Gui
+  Qt6::Sql
   holonight_domain
   holonight_providers
   holonight_application
+  holonight_persistence
 )
```

`Qt6::Sql` is added explicitly (not only transitively via `holonight_persistence`) because
`test_migrations.cpp` and `test_database_path.cpp` construct `QSqlDatabase`/`QSqlQuery` directly.

---

## 7. Test Strategy

### 7.1 Unique in-memory connections (REQ-NF-006, REQ-C-004)

`SqliteConversationRepository` always generates its own connection name internally (§4.4), so
black-box tests never need to think about uniqueness — each `SqliteConversationRepository` instance
constructed in a test is independent by construction, even across dozens of `TEST()` cases in one
binary. For the lower-level, direct-`QSqlDatabase` tests (`test_migrations.cpp`,
`test_database_path.cpp`), a tiny shared helper avoids collisions explicitly:

```cpp
// tests/persistence/test_support.h
inline QString uniqueTestConnectionName() {
  static std::atomic<int> counter{0};
  return QStringLiteral("holonight_test_conn_%1").arg(counter.fetch_add(1));
}
```

Every test that opens `QSqlDatabase::addDatabase("QSQLITE", uniqueTestConnectionName())` with
`setDatabaseName(":memory:")` gets a private, empty, isolated database, and removes the connection
(`QSqlDatabase::removeDatabase(name)`) in a fixture `TearDown()`/RAII wrapper to avoid leaking
registered connection names across the whole `test_holonight_ai` binary.

### 7.2 `tests/persistence/test_migrations.cpp` (REQ-NF-007)

Pure, synchronous — no threading, no repository, no signals:

```cpp
TEST(MigrationRunner, AppliesInitMigrationToFreshDatabase) {
  QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", uniqueTestConnectionName());
  db.setDatabaseName(":memory:");
  ASSERT_TRUE(db.open());

  const auto result = MigrationRunner::apply(db, MigrationRunner::builtInMigrations());

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, 1);
  // SELECT name FROM sqlite_master WHERE type='table' → conversations, messages, schema_version
  // PRAGMA table_info(conversations) → matches REQ-F-005's five columns
}

TEST(MigrationRunner, SkipsAlreadyAppliedMigrations) { /* apply() twice, second call is a no-op */ }
```

### 7.3 `tests/persistence/test_conversation_repository_worker_threading.cpp` (REQ-F-022, white-box)

Constructs `detail::ConversationRepositoryWorker` directly (not through
`SqliteConversationRepository`), moves it to a test-owned `QThread`, and connects to one of its own
signals with `Qt::DirectConnection` — which, unlike the outer, already-marshaled
`ConversationRepository`-level signal, still runs the slot on whichever thread actually emitted it,
letting the test observe the worker's real execution thread directly:

```cpp
TEST(ConversationRepositoryWorkerThreading, SlotsExecuteOnWorkerThread) {
  QThread worker_thread;
  auto* worker = new detail::ConversationRepositoryWorker(":memory:", uniqueTestConnectionName());
  worker->moveToThread(&worker_thread);
  worker_thread.start();

  QThread* observed = nullptr;
  QObject::connect(worker, &detail::ConversationRepositoryWorker::initialized, worker,
                    [&] { observed = QThread::currentThread(); }, Qt::DirectConnection);

  QMetaObject::invokeMethod(worker, [worker] { worker->openAndMigrate(); }, Qt::QueuedConnection);
  QTest::qWait(200);  // or a QSignalSpy::wait() on a proxy signal

  EXPECT_EQ(observed, &worker_thread);
  EXPECT_NE(observed, QThread::currentThread());

  worker_thread.quit();
  worker_thread.wait();
}
```

### 7.4 `tests/persistence/test_conversation_repository.cpp` (REQ-NF-006, black-box)

Exercises `SqliteConversationRepository`'s public API end-to-end against `:memory:`, using
`QSignalSpy::wait()` to pump the (already-running, offscreen-`QGuiApplication`-backed, per
`tests/main.cpp`) event loop until the queued cross-thread result arrives:

```cpp
TEST(SqliteConversationRepository, CreateThenListReturnsNewConversationFirst) {
  SqliteConversationRepository repository(":memory:");
  QSignalSpy initialized(&repository, &ConversationRepository::initialized);
  repository.initialize();
  ASSERT_TRUE(initialized.wait());

  QSignalSpy created(&repository, &ConversationRepository::conversationCreated);
  repository.createConversation();
  ASSERT_TRUE(created.wait());

  QSignalSpy listed(&repository, &ConversationRepository::conversationListLoaded);
  repository.listConversations();
  ASSERT_TRUE(listed.wait());
  const auto conversations = listed.at(0).at(0).value<QList<ConversationSummary>>();
  ASSERT_EQ(conversations.size(), 1);
  EXPECT_EQ(conversations.front().title, kDefaultConversationTitle);
}
```

Further cases in this file: rename moves to front, delete cascades (REQ-NF-009 — query `messages`
table directly after via a second, test-owned `:memory:`-independent path is not possible since
`:memory:` is private per-connection; instead the test asserts via the repository's own
`loadConversation`/`listConversations` results, treating it as black-box, exactly as `FakeHttpClient`
tests never inspect `QNetworkReply` internals either), zero-per-token-write count (send a message,
stream several `persistNewMessage`/`persistMessageSettled` calls directly against the repository,
then `loadConversation` and assert message count/content matches only the settled writes), and the
fatal-vs-non-fatal error classification (§4.10) by pointing the repository at an unwritable path and
asserting `unavailable` fires instead of `initialized`.

### 7.5 `tests/application/test_conversation_list_model.cpp`

Plain `QAbstractListModel` unit tests — `setAll`/`upsertToFront`/`touchToFront`/`removeById` each
checked for correct `rowCount()`/role data/ordering, plus `beginInsertRows`/`beginMoveRows` signal
emission counts via `QSignalSpy` on `rowsInserted`/`rowsMoved`/`rowsRemoved` — no repository, no
threading, matching `test_message_list_model.cpp`'s existing style (implied by the precedent, not
separately confirmed in this repo — `MessageListModel` itself is tested indirectly through
`test_chat_view_model.cpp` today; `ConversationListModel` gets its own direct test file since it has
more mutator variety worth covering directly).

### 7.6 `tests/application/test_chat_view_model.cpp` (extended)

Using the existing `Fixture` pattern plus a new `FakeConversationRepository` (mirroring
`FakeHttpClient`'s synchronous-inline-emission style — no `QThread`, no event loop needed for these
cases):

- Startup: repository initializes, list loads empty → `createConversation()` fires automatically;
  list loads non-empty → `switchConversation()` on the front entry automatically.
- Persistence-unavailable fallback: `FakeConversationRepository::simulateInitializationFailure(...)`;
  assert `persistenceStatusMessage` non-empty, `conversation() != nullptr` (fallback ephemeral
  conversation still constructed), `canSend()` still becomes true once models load.
- Send persists exactly the two new-message writes + one `updateLastModelId`, and exactly one
  `persistMessageSettled` on `Completed` (not on any `ContentDelta`) — REQ-F-013/020's core claim,
  now assertable via the fake's call-count-recording hooks.
- Title auto-derivation: first send on a `kDefaultConversationTitle`-titled conversation triggers
  `renameConversation`; a second send does not re-trigger it.
- Switch calls `stop()` first when a stream is in flight — assert via the fake's stream-cancellation
  hook (mirrors `ChatController::stop()`'s existing idempotence test from the chat-window-qml cycle).
- Delete-of-active-conversation redirects to the first remaining conversation, or creates a new one
  if none remain.
- Non-fatal repository `error(...)` reuses `errorMessage` (not `persistenceStatusMessage`); fatal
  `unavailable(...)` mid-session sets `persistenceStatusMessage` and `persistence_enabled_ = false`
  (assert a subsequent `send()` no longer calls into the fake's persistence methods).

---

## 8. Known Risks

- **`Q_DECLARE_METATYPE`/`qRegisterMetaType` omission is a silent runtime failure, not a compile
  error.** If a future signal gains a new parameter type crossing the worker→GUI boundary without
  updating `registerMetaTypes()` (§3.2), Qt emits a runtime warning
  ("`QObject::connect: Cannot queue arguments of type '...'`") and the connection simply never
  delivers that call — easy to miss in manual testing, since everything else keeps working. Mitigated
  by keeping `registerMetaTypes()` colocated with the DTOs it registers (§3.2) as the single place
  to update when either struct's shape changes, and by `test_conversation_repository.cpp`'s
  `QSignalSpy::wait()`-based tests, which would time out (a loud test failure) if this ever regressed.
- **`QThread::finished` → `deleteLater` teardown ordering.** `SqliteConversationRepository`'s
  destructor (`worker_thread_.quit(); worker_thread_.wait();`) relies on Qt's documented
  worker-object cleanup idiom (§4.4) firing `deleteLater` on the worker from within the worker
  thread's own finishing sequence. This is Qt's own canonical pattern, not a novel risk this design
  introduces, but it is a std::unique subtlety worth a comment at the call site so a future reader
  doesn't "simplify" it into a bare `delete worker_` and reintroduce a cross-thread destruction bug.
- **`stop()`-before-replace at every `conversation_`-reassignment site.** §2.3/§4.7 extend the
  existing "stop before releasing a Conversation with an in-flight stream" invariant (an accepted
  risk explicitly carried forward from `docs/sdd/ollama-chat-backend/DESIGN.md` §5) to
  `switchConversation`/`createConversation`/delete's active-conversation fallback. Nothing in the
  type system enforces this — a future new call site that reassigns `conversation_` without first
  calling `stop()` reintroduces the dangling-reference risk `ChatController` has always had. Same
  residual risk as before, now with three call sites needing the discipline instead of one
  (window-close).
- **Millisecond-timestamp collisions beyond the two-message-per-send case.** §4.8's `rowid`
  tie-breaker resolves ordering for the common case (one send's User+Assistant pair), but if a future
  feature ever batch-inserts many messages in one transaction with genuinely ambiguous intended order,
  `rowid` (insertion order) is the only signal available — this is correct today because insertion
  order and intended chronological order are always identical in every flow this design describes,
  but it is worth re-examining if a future import/bulk-insert feature changes that assumption.
- **No pagination for `listConversations()` (REQ-NF-002's "exact threshold decided at Design
  stage").** This design implements a single, unindexed-by-nothing-but-`updated_at` full-table
  `SELECT` with no `LIMIT`, for every call. `idx_conversations_updated_at` keeps this fast well past
  the 50-conversation/3-second acceptance bound, but if real-world usage ever grows into the
  thousands, the naive full-list-every-time approach (and `ConversationListModel::setAll`'s full
  `beginResetModel`) would need revisiting with actual pagination/virtualized loading — deferred
  because no acceptance criterion in this cycle's SPEC requires it yet.
- **SQLite corruption detection heuristic (§4.10) is a best-effort classification, not a guarantee.**
  `QSqlError::type() == ConnectionError` reliably catches "the file is gone" or "permission denied"
  cases, but some corruption modes surface as a `StatementError` on an otherwise-valid-looking query
  (e.g. a damaged B-tree page hit mid-scan) — such a case would be misclassified as non-fatal here,
  leaving the worker attempting further (also-failing) operations against a connection that should
  have been retired. Accepted for this cycle per REQ-NF-005's own "code review" acceptance bar;
  revisit if this misclassification is ever observed in practice (e.g. by also treating N consecutive
  non-fatal errors within a short window as fatal).

---

## Document History

| Version | Date       | Author | Changes                          |
|---------|------------|--------|-----------------------------------|
| 1.0     | 2026-07-22 | Claude | Initial design from SPEC.md v1.0  |
