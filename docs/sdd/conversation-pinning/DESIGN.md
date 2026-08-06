# DESIGN: Pin Conversations in the Sidebar

Implements `docs/sdd/conversation-pinning/SPEC.md` (REQ-F-001..012, REQ-NF-001..009,
REQ-C-001..004). Adds a nullable `pinned_at` column, two repository methods, two `ChatViewModel`
signals/invokables, two new `ConversationListModel` roles/mutators, and a second sidebar section —
no domain-type changes, no full-reload queries, no changes to rename/delete SQL (REQ-C-004).

The single most important structural fact this design is built around: **`ConversationListModel`
is one flat `std::vector<Row>` (`rows_`)**, not two lists and not a `QSortFilterProxyModel`. Every
existing mutator (`upsertToFront`, `touchToFront`, `setAll`) works by moving/inserting into that one
vector at specific indices. Introducing a Pinned section means introducing and defending an
**ordering invariant** on that same vector — pinned rows first (sorted `pinned_at` DESC), unpinned
rows after (sorted `updated_at` DESC) — rather than adding a second model. §6.1 justifies this over
the alternatives.

---

## 1. Components affected

### 1.1 `holonight_persistence` (schema, repository interface, worker, façade, test double)

- **`src/persistence/migrations/0007_add_pinned_at.sql`** (new) — see §4.1. Existing migrations run
  through `0006_drop_usage_cost_columns.sql`, so this is `0007`, not the `0005` example number used
  loosely in the SPEC text.
- **`src/persistence/src/migration_runner.cpp`** (modified) — `builtInMigrations()` needs a 7th
  hardcoded `Migration{.version = 7, .name = "0007_add_pinned_at", ...}` entry. This is the exact
  gotcha already recorded from the usage-cost-tracking cycle (CLAUDE.md / prior memory): the
  migration list is hand-maintained, not auto-discovered.
- **`src/persistence/CMakeLists.txt`** (modified) — `qt_add_resources(holonight_persistence
  "holonight_persistence_migrations" ...)` needs a 7th `FILES` line,
  `migrations/0007_add_pinned_at.sql`, or `readResource()` silently returns an empty string and the
  migration no-ops without error. Same two-place checklist as §1.1's migration_runner.cpp entry —
  call out as its own Stage-3 task line item.
- **`src/persistence/include/holonight_persistence/conversation_record.h`** (modified) —
  `ConversationSummary` gains `std::optional<QDateTime> pinned_at;` (nullopt = unpinned), following
  the existing `std::optional<holonight_domain::ModelId> last_model_id` precedent exactly (§3.4).
- **`src/persistence/include/holonight_persistence/conversation_repository.h`** (modified) — two new
  pure-virtual methods and two new signals (§3.1).
- **`src/persistence/include/holonight_persistence/sqlite_conversation_repository.h` /
  `src/sqlite_conversation_repository.cpp`** (modified) — two new façade methods that package the
  call and hand it to the worker via `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`, and two
  new `connect()` lines relaying the worker's completion signals up to
  `ConversationRepository`'s own signals — identical shape to the existing
  `renameConversation`/`deleteConversation` wiring (`sqlite_conversation_repository.cpp:88-104`).
- **`src/persistence/include/holonight_persistence/detail/conversation_repository_worker.h` /
  `src/detail/conversation_repository_worker.cpp`** (modified) — two new slots implementing the
  literal SQL from REQ-NF-002 (§3.2), plus:
  - `summaryFromRecord()` (worker.cpp:62-71) gains a 7th column read for `pinned_at`, via a new
    `decodeOptionalDateTime()` helper mirroring the existing `decodeOptionalNumeric<T>()` pattern
    (worker.cpp:141-147).
  - `listConversations()`'s `SELECT` column list and `ORDER BY` clause change (§4.2); `loadConversation()`'s
    `SELECT` column list changes too (both currently select the same 6 columns positionally,
    worker.cpp:348 and worker.cpp:368).
  - `createConversation()` and `materializeConversation()`'s `INSERT` statements are **unchanged** —
    `pinned_at` has no `NOT NULL`/`DEFAULT`, so SQLite fills it as `NULL` for any `INSERT` that
    doesn't name the column. This keeps REQ-C-001 (additive-only) and REQ-C-004 (no new logic in
    unrelated write paths) trivially satisfied for those two methods.
- **`tests/persistence/fake_conversation_repository.h`** (modified) — `ConversationRepository` is
  abstract; adding two pure-virtual methods breaks this test double's compilation until it overrides
  them. Needs `pinConversation`/`unpinConversation` overrides plus `pinned_at` bookkeeping in its
  in-memory `conversations_` map so `ChatViewModel` unit tests can exercise pin/unpin without a real
  database (mirrors how it already fakes `renameConversation`/`deleteConversation`).
- **`tests/persistence/test_conversation_repository_sqlite.cpp`** (modified/new cases) — pin/unpin
  round-trip against a real SQLite file: pin sets `pinned_at`, unpin clears it, rename/delete leave
  it untouched.

### 1.2 `holonight_application` (`ConversationListModel`, `ChatViewModel`)

- **`src/application/include/holonight_application/conversation_list_model.h` /
  `src/conversation_list_model.cpp`** (modified) — new `Roles` enum values, new `Row` fields, new
  mutators, and pin-aware fixes to two *existing* mutators (§3.3, §6.2 — this is the one place a
  naive "just add roles" reading of REQ-NF-003 would silently break REQ-F-011).
- **`src/application/include/holonight_application/chat_view_model.h` /
  `src/chat_view_model.cpp`** (modified) — two new `Q_INVOKABLE` methods, two new `Q_SIGNAL`s, two new
  private slots connected to the repository in the constructor (mirroring
  `chat_view_model.cpp:303-324`), and two call sites in the same block that connects
  `conversationRenamed`/`conversationDeleted` today.
- **`tests/application/test_conversation_list_model.cpp`** (modified) — new cases: pin moves a row
  from the unpinned tail into the pinned head at index 0; pinning a second conversation places it
  ahead of the first; unpin re-inserts by `updated_at` among unpinned rows; rename of a pinned row
  changes title/updated_at in place *without* moving it (REQ-F-011); `visibleCount()` helper (§3.3)
  counts correctly under a filter.
- **`tests/application/test_chat_view_model_*.cpp`** (whichever existing file covers
  rename/delete — modified) — `pinConversation`/`unpinConversation` invokables emit the new signals
  and update `conversationList()` in place, using `FakeConversationRepository`.

### 1.3 `qml/workspace/` (`ConversationListPanel.qml`, `ConversationListDelegate.qml`)

- **`qml/workspace/ConversationListDelegate.qml`** (modified) — one new `required property bool
  pinned`, one new `MenuItem` inserted between the existing Rename and Delete items, one new signal
  (§5.2).
- **`qml/workspace/ConversationListPanel.qml`** (modified) — the single `HnSectionHeader` + `ListView`
  block (lines 76-108 today) becomes two such blocks, each independently visibility-gated, both bound
  to the same `ChatViewModel.conversationList` model instance (§5.1).
- **`tests/qml/test_conversation_list_delegate.cpp`** (modified) — new cases: label reads "Pin" when
  `pinned: false` and "Unpin" when `pinned: true`; the new `pinToggleRequested()` signal fires on
  click; menu item order is Rename → Pin/Unpin → Delete.
- **New QML test** for `ConversationListPanel.qml` section visibility (REQ-F-007/009) if this
  project's QML test harness supports a panel-level test — otherwise cover the counting logic via
  the `ConversationListModel::visibleCount()` GTest cases above and treat full section-visibility
  behavior as manually-verified per this project's no-visual-verification convention (memory:
  `feedback_no_visual_verification.md`).

No changes anywhere in `holonight_domain`, `holonight_config`, `holonight_providers`,
`holonight_credentials`, `holonight_platform`, or `qml/quickpanel/` — pinning is purely a sidebar +
persistence concern (Non-Goals), and the quick panel doesn't render the conversation list at all
(confirmed by inspection, same as the response-footer-stats cycle's equivalent note).

---

## 2. Data flow

### 2.1 Pin: user click → DB write → model update → UI

```
User clicks "Pin" in ConversationListDelegate's actionsMenu
  → MenuItem.onTriggered: root.pinToggleRequested()                         [NEW signal, delegate]
ConversationListPanel's delegate instantiation:
  onPinToggleRequested: delegate.pinned
      ? ChatViewModel.unpinConversation(delegate.conversationId)
      : ChatViewModel.pinConversation(delegate.conversationId)              [NEW, panel wiring]
→ ChatViewModel::pinConversation(conversationId)                            [NEW Q_INVOKABLE]
    repository_->pinConversation(conversationId);                           [existing async pattern]
→ SqliteConversationRepository::pinConversation() packages a functor,
  QMetaObject::invokeMethod(worker_, ..., Qt::QueuedConnection)              [existing pattern, unchanged shape]
    → ConversationRepositoryWorker::pinConversation(conversationId)  (worker thread)
        UPDATE conversations SET pinned_at = CURRENT_TIMESTAMP WHERE id = ?  [REQ-NF-002 literal SQL]
        re-SELECT pinned_at for this id (SQLite computes CURRENT_TIMESTAMP server-side; the
        worker has no other way to learn the exact value it just wrote — §6.4)
        on success: emit conversationPinned(conversationId, pinnedAt)        [NEW worker signal]
            (Qt::QueuedConnection back to the GUI thread)
← SqliteConversationRepository relays worker's conversationPinned(id, pinnedAt)
  to its own ConversationRepository::conversationPinned(id, pinnedAt) signal  [NEW repository signal]
→ ChatViewModel::onConversationPinned(conversationId, pinnedAt)              [NEW private slot]
    conversation_list_model_->markPinned(conversationId, pinnedAt);          [NEW mutator, §3.3]
    emit conversationPinned(QUuid::fromString(conversationId));              [NEW ChatViewModel signal, REQ-F-012/NF-007]
→ ConversationListModel::markPinned() moves the row to index 0 (§3.3),
    emits dataChanged({PinnedRole, PinnedAtRole}) and (if it moved) beginMoveRows/endMoveRows
→ Both ListViews in ConversationListPanel observe the model change:
    - Pinned-section delegate for this row: matchesSection flips false→true, becomes visible
    - Recent-section delegate for this row: matchesSection flips true→false, collapses to height 0
    - Panel's pinnedVisibleCount/recentVisibleCount recompute (Connections on the model's
      rowsInserted/rowsRemoved/dataChanged/modelReset — §5.1), Pinned header becomes visible if it
      was hidden (REQ-F-007)
```

No `fetchAll()`/`listConversations()` call anywhere in this path (REQ-C-002) — the only SQL
statements executed are the single `UPDATE` and the immediately-following single-row `SELECT` to
recover the exact timestamp SQLite assigned.

### 2.2 Unpin

Symmetric to §2.1: `ChatViewModel::unpinConversation()` → `repository_->unpinConversation()` →
worker's `UPDATE conversations SET pinned_at = NULL WHERE id = ?` (REQ-NF-002 literal SQL, no
re-SELECT needed since there is no generated value to recover) → `conversationUnpinned(id)` signal →
`ChatViewModel::onConversationUnpinned()` → `conversation_list_model_->markUnpinned(conversationId)`
(§3.3, re-inserts sorted by the row's already-known `updated_at` among the unpinned rows) →
`ChatViewModel::conversationUnpinned(QUuid)` emitted → the two ListViews' delegates flip visibility
the other way.

### 2.3 Rename of a pinned conversation (REQ-F-011) — unchanged trigger, corrected consumer

```
ChatViewModel::renameConversation() → repository_->renameConversation()     [UNCHANGED, REQ-C-004]
→ ConversationRepositoryWorker::renameConversation() — UNCHANGED SQL, still never reads/writes
  pinned_at, still re-SELECTs only (created_at, last_model_id)              [worker.cpp:446-452]
→ emits conversationRenamed(ConversationSummary{...})  — this summary's pinned_at field is
  default-constructed (nullopt), NOT the row's real pin state, because the rename query never
  fetches it (by design — REQ-C-004 forbids adding pin-awareness to rename's SQL)
→ ChatViewModel::onConversationRenamed() — UNCHANGED call site:
    conversation_list_model_->upsertToFront(summary);
→ ConversationListModel::upsertToFront() — MODIFIED (§3.3, §6.2):
    - looks up the existing row by id first
    - copies pinned/pinned_at FROM THE EXISTING ROW, ignoring summary.pinned_at entirely (the
      incoming summary's pinned_at is not authoritative — see above)
    - if the existing row is pinned: update title/updated_at/title_source in place, do NOT move it
      (REQ-F-011 — Pinned-section order depends only on pinned_at, which rename never touches)
    - if the existing row is unpinned: existing "move to front" behavior, but "front" now means
      "front of the unpinned sub-range" (index = pinnedRowCount()), not absolute index 0 (§3.3, §6.2)
```

This is the one place REQ-F-011 is actually enforced — not by adding a guard in the rename SQL
(REQ-C-004 forbids that), but by making the *model-side* consumer of the rename result pin-aware.

### 2.4 Delete of a pinned conversation (REQ-F-010)

Entirely unchanged: `ChatViewModel::deleteConversation()` → `repository_->deleteConversation()` →
worker's existing `DELETE FROM messages ...` + `DELETE FROM conversations ...` transaction (no
`pinned_at` reference anywhere, REQ-C-004) → `conversationDeleted(id)` →
`ChatViewModel::onConversationDeleted()` → `conversation_list_model_->removeById(id)` — `removeById`
needs no changes at all: it finds the row by id and erases it regardless of which "section" (pinned
or not) it was displaying in, since `rows_` is one vector.

---

## 3. Interfaces / APIs

### 3.1 `holonight_persistence::ConversationRepository` (abstract interface)

```cpp
// Fire-and-forget, matching every other method here (REQ-NF-002). Failures surface via the
// existing error(conversationId, message) signal, same as every other repository method.
virtual void pinConversation(QString conversationId) = 0;
virtual void unpinConversation(QString conversationId) = 0;

Q_SIGNALS:
  // NEW. Not mandated verbatim by SPEC (REQ-NF-002 only mandates the two methods above), but
  // required to implement REQ-NF-006's "upon success emits ... on the main thread" and REQ-NF-007's
  // ChatViewModel-level signals — mirrors the existing conversationRenamed/conversationDeleted
  // repository-signal pattern exactly. pinnedAt carries the exact CURRENT_TIMESTAMP value SQLite
  // assigned (§6.4); conversationUnpinned needs no payload beyond the id since NULL has no "value"
  // to report.
  void conversationPinned(QString conversationId, QDateTime pinnedAt);
  void conversationUnpinned(QString conversationId);
```

### 3.2 `holonight_persistence::detail::ConversationRepositoryWorker`

```cpp
public Q_SLOTS:
  void pinConversation(const QString& conversationId);
  void unpinConversation(const QString& conversationId);

Q_SIGNALS:
  void conversationPinned(holonight_persistence::QString conversationId, QDateTime pinnedAt);
  void conversationUnpinned(QString conversationId);
```

Implementation follows the exact `if (!available_) return;` / `QSqlQuery` / `reportOperationError()`
shape used by every other worker method. Literal SQL, per REQ-NF-002's acceptance criterion:

```sql
-- pin
UPDATE conversations SET pinned_at = CURRENT_TIMESTAMP WHERE id = :id;
-- then, to learn the value just written (no RETURNING in this codebase's SQLite usage elsewhere):
SELECT pinned_at FROM conversations WHERE id = :id;

-- unpin
UPDATE conversations SET pinned_at = NULL WHERE id = :id;
```

Both check `query.numRowsAffected()`/`query.next()` failure the same way `renameConversation()`
does today; a missing id reports through `reportOperationError()` like every other method (no new
error-return semantics, per REQ-NF-002).

### 3.3 `holonight_application::ConversationListModel`

```cpp
enum Roles : std::uint16_t {
  IdRole = Qt::UserRole + 1,
  TitleRole,
  UpdatedAtRole,
  TitleSourceRole,
  TitleGenerationInProgressRole,
  PinnedRole,      // NEW — bool
  PinnedAtRole,    // NEW — QDateTime (or QString "" when unpinned; see roleNames() below)
};
```

`roleNames()` gains two entries. **Naming note**: SPEC's prose loosely writes `model.Pinned` /
`model.PinnedAt`, but every existing QML-facing role name in this codebase is lowerCamel
(`conversationId`, `updatedAt`, `titleGenerationInProgress` — `conversation_list_model.cpp:70-78`).
The new roles follow that real convention: QML sees `model.pinned` / `model.pinnedAt`, matching the
existing `PinnedRole`/`PinnedAtRole` *C++ enum* names exactly the way `TitleGenerationInProgressRole`
maps to `"titleGenerationInProgress"`. REQ-NF-003 is satisfied on the enum-name axis it actually
specifies; the QML-string axis follows house style instead of the SPEC's loose PascalCase example.

```cpp
// Moves a row into the Pinned block at index 0 (most-recently-pinned always leads — REQ-F-005).
// No-op if the row doesn't exist or is already pinned. If already at index 0, updates fields only
// (no beginMoveRows). Emits dataChanged({PinnedRole, PinnedAtRole}) in all cases.
void markPinned(const QString& conversationId, const QDateTime& pinnedAt);

// Moves a row out of the Pinned block, re-inserting it among the unpinned rows at the position its
// existing updated_at implies (same ordering rule setAll()/touchToFront() already use for the
// unpinned tail). No-op if the row doesn't exist or is already unpinned.
void unpinConversation... // — named markUnpinned() to avoid confusion with ChatViewModel::unpinConversation()
void markUnpinned(const QString& conversationId);

// Number of rows currently matching `pinned`, restricted to titles containing `filterText`
// case-insensitively (empty filterText matches everything). Pure read, touches no repository —
// used only to drive ConversationListPanel.qml's per-section header visibility (§5.1), NOT a
// fetchAll()-style reload (REQ-C-002 is about repository queries; this never leaves the model).
Q_INVOKABLE int visibleCount(bool pinned, const QString& filterText) const;
```

`upsertToFront()` and `touchToFront()` signatures are unchanged, but their bodies change (§2.3, §6.2):
both now branch on the *existing* row's `pinned` flag (never the incoming parameter's, since neither
`ConversationSummary` from a rename nor the `(id, updatedAt)` pair from `lastModelIdUpdated` carries
authoritative pin data) and, when moving an unpinned row, insert at `pinnedRowCount()` instead of
absolute index `0`.

`Row` struct gains:

```cpp
bool pinned = false;
QDateTime pinned_at;  // valid only when pinned == true
```

`toRow()` reads `conversation.pinned_at.has_value()` / `*conversation.pinned_at` from the (now
extended) `ConversationSummary`.

### 3.4 `holonight_persistence::ConversationSummary`

```cpp
struct ConversationSummary {
  QString id;
  QString title;
  QDateTime created_at;
  QDateTime updated_at;
  std::optional<holonight_domain::ModelId> last_model_id;
  holonight_domain::TitleSource title_source = holonight_domain::TitleSource::Fallback;
  std::optional<QDateTime> pinned_at;  // NEW — nullopt = unpinned
  // operator== stays defaulted (member-wise); no manual update needed.
};
```

### 3.5 `holonight_application::ChatViewModel`

```cpp
Q_INVOKABLE void pinConversation(const QString& conversationId);
Q_INVOKABLE void unpinConversation(const QString& conversationId);

Q_SIGNALS:
  void conversationPinned(QUuid id);
  void conversationUnpinned(QUuid id);

private:
  void onConversationPinned(const QString& conversationId, const QDateTime& pinnedAt);
  void onConversationUnpinned(const QString& conversationId);
```

Every existing public method/signal on `ChatViewModel` (`renameConversation`, `deleteConversation`,
`createConversation`, `switchConversation`, and note that `conversationRenamed`/`conversationDeleted`
are *not* currently public `ChatViewModel` signals at all — see §6.3) is untouched, satisfying
REQ-C-003 by construction: nothing is edited, only appended.

### 3.6 QML — `ConversationListDelegate.qml`

```qml
required property bool pinned   // NEW — populated from model.pinned automatically (ComponentBehavior: Bound)

signal pinToggleRequested()     // NEW

// inserted between the existing Rename and Delete MenuItems (REQ-F-001/002):
MenuItem {
    text: root.pinned ? qsTr("Unpin") : qsTr("Pin")
    hoverEnabled: true
    icon.source: "qrc:/HolonightChat/assets/icons/pin.svg"   // already exists in assets/icons/
    onTriggered: root.pinToggleRequested()
}
```

### 3.7 QML — `ConversationListPanel.qml`

```qml
property bool pinnedSectionVisible: false   // recomputed, see §5.1
property bool recentSectionVisible: true

delegate: ConversationListDelegate {
    ...
    pinned: model.pinned            // NEW binding, in addition to the existing required-prop set
    showSection: /* "pinned" or "recent" per-ListView, see §5.1 */
    onPinToggleRequested: delegate.pinned
        ? ChatViewModel.unpinConversation(delegate.conversationId)
        : ChatViewModel.pinConversation(delegate.conversationId)
}
```

---

## 4. Data model / schema

### 4.1 Migration file

`src/persistence/migrations/0007_add_pinned_at.sql` — the entire file, per REQ-NF-001/REQ-C-001
verbatim:

```sql
ALTER TABLE conversations ADD COLUMN pinned_at TEXT;
```

No `DEFAULT`, no `NOT NULL`. SQLite gives every existing row (and every future `INSERT` that omits
the column) `NULL` for free — no backfill statement is needed or permitted (REQ-C-001).

### 4.2 Read-path SQL changes

`listConversations()`'s query becomes:

```sql
SELECT id, title, created_at, updated_at, last_model_id, title_source, pinned_at
FROM conversations
ORDER BY (pinned_at IS NULL) ASC, pinned_at DESC, updated_at DESC, rowid DESC
```

- `(pinned_at IS NULL) ASC` puts every pinned row (`0`) before every unpinned row (`1`) —
  this is what makes `setAll()`'s initial load already satisfy the "pinned block precedes recent
  block" invariant the in-memory model then maintains incrementally.
- Within the pinned block, `pinned_at DESC` gives most-recently-pinned-first (REQ-F-005).
- Within the unpinned block, `updated_at DESC` is the pre-existing, unchanged Recent ordering
  (REQ-NF-009).
- `rowid DESC` remains the existing final tiebreaker (was already present for the `updated_at` tie
  case; now also breaks `pinned_at` ties — see §6.5 for why this is an imperfect tiebreaker for pins
  specifically, and why that's an accepted, narrow residual risk rather than something this design
  fixes).

`loadConversation()`'s single-row `SELECT` gets the same 7th column appended (column list only; no
`ORDER BY` relevant to a single-row lookup).

### 4.3 Sort ordering summary

| Section | Order | Field that drives it | Changed by rename? | Changed by message activity? |
|---|---|---|---|---|
| Pinned | most-recently-pinned first | `pinned_at DESC` | No (REQ-F-011) | No |
| Recent | most-recently-updated first | `updated_at DESC` | Yes (bumps `updated_at`, unchanged) | Yes (unchanged) |

---

## 5. QML structural change

### 5.1 Panel: two `ListView`s, not one with `section.property`

**Decision: two separate `HnSectionHeader` + `ListView` blocks, stacked vertically, both bound to
the same `ChatViewModel.conversationList` model instance.** Rejected alternative: a single `ListView`
using QML's built-in `section.property`/`section.delegate` grouping. Rationale (§6.1 expands):

1. The "Recent" header in the *current* codebase (`ConversationListPanel.qml:76-87`) is already a
   plain sibling `HnSectionHeader` above a `ListView`, not a `ListView.section` construct — there is
   no existing `section.property` usage anywhere in this codebase to extend. Two stacked blocks is
   the zero-new-concepts option.
2. QML's built-in section headers are driven by *model content* (which section values are present),
   not by *delegate visibility*. This codebase's search filter already works by collapsing a
   delegate to `height: 0` while it stays a member of the model (`ConversationListDelegate.qml:32-33`
   — `visible: matchesFilter`, `height: matchesFilter ? implicitHeight : 0`). A built-in
   `section.property` header has no way to hide itself when every row in its section is
   height-collapsed-but-still-present — it would keep showing a "Pinned" header over an empty-looking
   gap during a search that matches nothing pinned, violating REQ-F-009. Two independently-gated
   wrapper blocks sidestep this entirely: each wrapper's own `visible` binding is computed
   separately (below), not derived from the ListView's internal section machinery.

Structure:

```qml
ColumnLayout {
    id: pinnedSection
    Layout.fillWidth: true
    visible: root.pinnedVisibleCount > 0
    spacing: 0

    HnSectionHeader { titleText: qsTr("Pinned"); /* pin icon leadingContent */ }
    ListView {
        Layout.fillWidth: true
        model: ChatViewModel.conversationList
        delegate: ConversationListDelegate {
            width: ListView.view.width
            isActive: conversationId === ChatViewModel.activeConversationId
            filterText: root.searchText
            showOnlyPinned: true            // NEW property, see below
            onPinToggleRequested: ...
        }
    }
}

// existing "Recent" block, unchanged except:
ListView {
    delegate: ConversationListDelegate {
        ...
        showOnlyPinned: false
    }
}
```

`ConversationListDelegate` gains one more property beyond `pinned` (§3.6): a plain (non-model)
`property bool showOnlyPinned` set per-`ListView`, combined into visibility:

```qml
readonly property bool matchesSection: root.showOnlyPinned === root.pinned
visible: root.matchesFilter && root.matchesSection
height: visible ? implicitHeight : 0
```

Each row therefore exists in *both* ListViews' delegate instantiations, but is only ever
non-zero-height in one of them — directly extending the already-established zero-height-hiding
technique (REQ-F-003's "appears in exactly one section at a time" is enforced by this
mutual-exclusivity of `matchesSection`, not by the row only existing in one model).

**Section header visibility (REQ-F-007, REQ-F-009).** `pinnedVisibleCount` /
`recentVisibleCount` are plain `int` properties on `ConversationListPanel`, recomputed by a JS
function that calls the new `ConversationListModel::visibleCount(pinned, filterText)` helper
(§3.3) for both `true` and `false`. Recompute is triggered by:

- `root.searchText` changing (existing `property alias searchText: searchField.text`)
- a `Connections { target: ChatViewModel.conversationList }` block handling `modelReset`,
  `rowsInserted`, `rowsRemoved`, and `dataChanged` (the last one specifically because `markPinned`/
  `markUnpinned` change `PinnedRole` via `dataChanged`, not a structural signal, on a row that stays
  at the same index in the edge case where it doesn't need to move)

This is plumbing, not a new architectural concept — but it is real, non-trivial wiring, and is
called out as its own Known Risk in §6.6.

### 5.2 Delegate: menu item insertion (REQ-F-001/002, REQ-NF-005)

`ConversationListDelegate.qml`'s `actionsMenu` (currently Rename, Delete —
`ConversationListDelegate.qml:113-130`) becomes Rename, Pin/Unpin, Delete:

```qml
Menu {
    id: actionsMenu
    y: parent.height

    MenuItem {
        text: qsTr("Rename")
        ... // unchanged
    }

    MenuItem {                                            // NEW
        text: root.pinned ? qsTr("Unpin") : qsTr("Pin")
        hoverEnabled: true
        icon.source: "qrc:/HolonightChat/assets/icons/pin.svg"
        onTriggered: root.pinToggleRequested()
    }

    MenuItem {
        text: qsTr("Delete")
        ... // unchanged
    }
}
```

`assets/icons/pin.svg` already exists in this project (used elsewhere, e.g. panel-pin/collapse
affordances) — no new SVG asset is needed. There is no separate "unpin" icon; the same pin glyph is
reused for both states since only the *label* toggles (Non-Goals explicitly excludes adding a
distinct visual pin indicator beyond section placement, and REQ-NF-005 explicitly rules out any
extra visual complexity here).

**One signal, not two** (`pinToggleRequested()` rather than separate `pinRequested()` /
`unpinRequested()`): the delegate already knows its own `pinned` state, so a single toggle-shaped
signal plus a one-line ternary at the call site (§3.7) is strictly less surface area than two
signals that would only ever be used mutually exclusively. This mirrors the existing precedent of
`deleteConfirmed()` being one signal even though the row's own inline Yes/No buttons (lines 193-204)
already branch internally — the *branch* lives where the state already is (the delegate), the
*signal* just reports "the user finished the action," and picks which repository call to make where
the id is naturally in scope (the panel). See §6.3 for how this reasoning was checked against
REQ-NF-005's literal (single-`MenuItem`) example.

---

## 6. Key decisions, alternatives, and risks

### 6.1 One `ConversationListModel`, invariant-ordered, not two models or a proxy model

**Alternatives considered:**
- *Two `ConversationListModel` instances* (`pinnedConversationList` + `conversationList`), with
  pin/unpin literally moving a `Row` from one model's vector to the other's. Rejected: REQ-NF-003
  explicitly specifies `PinnedRole`/`PinnedAtRole` as roles *on* `ConversationListModel`, implying
  QML determines placement from a single model's row data, not from which of two model instances a
  row lives in. Two instances would also double the object graph exposed as `Q_PROPERTY` on
  `ChatViewModel` for no behavioral gain, and would need to reconcile two independent `rows_`
  vectors' worth of `beginInsertRows`/`beginRemoveRows` bookkeeping on every single pin/unpin instead
  of one.
- *`QSortFilterProxyModel` pair* wrapping one source model. Rejected: this codebase has zero
  existing `QSortFilterProxyModel` usage; introducing it here means a third mental model (source
  index vs. proxy index mapping) for a codebase whose entire existing precedent — MessageListModel,
  ConversationListModel — is "one flat `QAbstractListModel` with hand-rolled narrow mutators." Given
  the "rule of three" pattern already established in this project's cycle history (memory:
  `project_google_provider_adapter.md`), a genuinely new abstraction should earn its place by solving
  a problem hand-rolled code can't — here it can, cleanly, at this data scale (dozens to low
  hundreds of conversations, not a case where index-mapping overhead buys real performance).

**Chosen:** one model, invariant-maintained ordering ([pinned block, sorted `pinned_at` DESC] ++
[unpinned block, sorted `updated_at` DESC]), extended narrow mutators. This keeps `ChatViewModel`'s
`Q_PROPERTY(ConversationListModel* conversationList ...)` completely unchanged (REQ-C-003-adjacent —
not literally required by REQ-C-003, but consistent with its spirit of additive-only surface area).

### 6.2 `upsertToFront()`/`touchToFront()` must stop meaning "index 0" — this is the design's real risk

This is the one place a shallow implementation would pass every *individually*-obvious test and
still violate REQ-F-011/REQ-NF-009. Before this cycle, "move to front" and "index 0" were the same
thing, because the model had one section. After this cycle, they are not: a *renamed or newly
created unpinned* conversation must move to the front of the **unpinned sub-range** (index
`pinnedRowCount()`), never to absolute index 0, or it would visually jump above every pinned
conversation in the same flat `rows_` vector. Conversely, a *pinned* conversation being renamed must
not move at all (its position is governed solely by `pinned_at`, which rename never touches).

**Risk:** `beginMoveRows(QModelIndex(), fromRow, fromRow, QModelIndex(), destinationRow)`'s
`destinationRow` argument has well-known, easy-to-get-backwards Qt semantics when `destinationRow` is
not `0` (Qt's documented convention: when moving forward, the destination index is expressed in
terms of the row indices *before* removal, which is one higher than the intuitive "insert at this
index after removal" reading many implementations reach for first). The existing code sidesteps this
entirely today by only ever moving to/from index `0`. This cycle's `markUnpinned()` (moving a row
from the pinned block into the middle of the unpinned block) is the first mutator in this model that
needs a non-zero, non-front destination. **Mitigation:** treat this as a named implementation task
with its own targeted GTest cases (pin A, pin B, pin C, unpin B — assert final order is exactly
[A, C] pinned-removed / recent-with-B-correctly-placed), not something to eyeball from the diff.

### 6.3 `ChatViewModel` gets its own public `conversationPinned`/`conversationUnpinned` signals — a deliberate deviation from the rename/delete precedent

Today, `ChatViewModel` does **not** re-expose `conversationRenamed`/`conversationDeleted` as its own
public signals at all — only `ConversationRepository` has those, and `ChatViewModel`'s private
`onConversationRenamed`/`onConversationDeleted` slots consume them internally to mutate
`conversation_list_model_`, with no further public notification. REQ-NF-007 explicitly requires
`ChatViewModel` itself to declare `conversationPinned(QUuid)`/`conversationUnpinned(QUuid)` as its
own signals. This design honors that literal requirement rather than silently "normalizing" pinning
to match rename/delete's narrower precedent, on the read that the SPEC is deliberately widening the
public surface here (e.g., for a future consumer — desktop notification, analytics — that wants to
observe pin events without reaching past `ChatViewModel` into the repository). Cost: one more
`QUuid`-vs-`QString` conversion at the `onConversationPinned`/`onConversationUnpinned` boundary,
since the repository/model layer works in `QString` ids (matching every existing conversation id
type in this codebase) while REQ-NF-007 specifies `QUuid` for the public signal parameter.

### 6.4 Repository-level success signals, even though REQ-NF-002 only mandates the two methods

REQ-NF-002's acceptance criterion lists only the two pure-virtual methods; it doesn't mention new
repository signals. But REQ-NF-006's acceptance criterion requires "upon success emits
conversationPinned(conversationId) on the main thread" — which is impossible without *some* signal
crossing the worker-thread boundary back to `ChatViewModel`, since every repository method here is
fire-and-forget-void (REQ-NF-002 itself: "returning void"). Adding
`ConversationRepository::conversationPinned(QString, QDateTime)` /
`conversationUnpinned(QString)` is therefore necessary, not optional, to satisfy REQ-NF-006/007
together — this is flagged explicitly so a reviewer checking REQ-NF-002's acceptance criterion
literally ("two pure-virtual methods... no new error-return semantics required") doesn't read the
new signals as scope creep.

**Why `pinnedAt` needs a re-`SELECT`:** SQLite's `CURRENT_TIMESTAMP` is evaluated server-side inside
the `UPDATE` statement; the C++ layer has no return value from an `UPDATE` other than affected-row
count. Every other write path in this file that needs a timestamp for its emitted signal
(`createConversation`, `renameConversation`, `updateLastModelId`) instead computes the timestamp in
C++ first via `nextOperationTimestamp()` and binds it as a parameter — which also gives those paths
monotonically-increasing, millisecond-resolution timestamps even within one event-loop tick. Pin
deliberately does *not* follow that existing convention, because REQ-NF-002's acceptance criterion
pins the literal SQL text (`pinned_at = CURRENT_TIMESTAMP`), and following it exactly means SQLite,
not the worker, produces the value — hence the re-`SELECT` to learn what was written. §6.5 covers
the consequence.

### 6.5 Same-second pin collisions are the common case, not the rare edge case the SPEC's Known Risks section implies

SPEC's Known Risks section says: "If multiple conversations have the same `pinned_at` timestamp
(rare but possible if pin operations occur in rapid succession)..." — this undersells the actual
exposure once `CURRENT_TIMESTAMP` (§6.4) is taken literally: SQLite's `CURRENT_TIMESTAMP` has
**one-second** resolution (`YYYY-MM-DD HH:MM:SS` text, no fractional seconds). Two ordinary,
human-paced pin clicks within the same wall-clock second — not a rapid-fire edge case at all — will
produce identical `pinned_at` values.

**Mitigation actually adopted, in two layers:**
1. **Live session (the common case):** `ConversationListModel::markPinned()` always inserts the
   newly-pinned row at absolute index `0`, unconditionally — it never consults `pinned_at` for
   *placement* during a live pin action, only for *display* and for the *initial* SQL-driven load
   order. This means REQ-F-005's acceptance criterion (pin A, then B; B ends up above A) holds
   exactly regardless of whether A and B's `pinned_at` values happen to collide to the same second,
   because the in-memory model's insertion order — not the timestamp — governs live re-ordering.
2. **Cross-restart (the residual gap):** on a fresh `listConversations()` load (app restart), two
   same-second-pinned rows fall back to the SQL's `rowid DESC` tiebreaker (§4.2), which reflects
   original row-creation order, not pin order. If conversation X was created before Y but Y was
   pinned before X, and both landed in the same `pinned_at` second, a restart could show X above Y —
   backwards from the live-session order the user actually saw. This is accepted as a narrow,
   cosmetic, restart-only inconsistency rather than fixed, because fixing it fully requires either
   (a) millisecond-resolution timestamps bound from C++ (breaking REQ-NF-002's literal SQL mandate)
   or (b) a dedicated monotonic `pin_sequence` column (out of scope — REQ-C-001 permits only the one
   additive `pinned_at TEXT` column). Flagging this now so it isn't rediscovered as a "bug" later
   without this context.

### 6.6 Section-header visibility recompute is signal-driven QML wiring with no single source of truth to unit-test end-to-end

§5.1's `pinnedVisibleCount`/`recentVisibleCount` depend on catching every model signal that could
change which rows are visible under the current filter: `modelReset` (initial load),
`rowsInserted`/`rowsRemoved` (pin/unpin moving rows across the pinned/unpinned boundary — implemented
as move, so this actually needs `rowsMoved` too, not just insert/remove — correction from the first
draft of this section), and `dataChanged` (title edits changing filter-match state). Missing any one
of these means a header can go stale (e.g., stay hidden after the last matching row appears). Because
this logic lives in QML plumbing rather than a single C++ function with one clear input/output
contract, it isn't fully covered by `ConversationListModel::visibleCount()`'s own GTest coverage —
that only proves the *counting logic* is correct given a model state, not that the *panel* recomputes
at the right times. Mitigation: an explicit QML-test (or, if infeasible per this project's QML test
harness, a manually-verified checklist item per the no-visual-verification convention) exercising
"pin the last unfiltered row while a search is active → header appears" and its inverse.

### 6.7 Carried forward from SPEC's own Known Risks

- **Signal-model sync ordering** (SPEC): satisfied structurally by this design — `ChatViewModel`
  only emits its public `conversationPinned`/`conversationUnpinned` from inside
  `onConversationPinned`/`onConversationUnpinned`, which only fire after the repository's own signal
  arrives, which only happens after the worker's `UPDATE` (and, for pin, the confirming re-`SELECT`)
  succeeds — there is no path that emits before the write is durable, mirroring
  `onConversationRenamed`/`onConversationDeleted`'s existing ordering exactly.
- **Filtered section visibility** (SPEC): satisfied by construction — §5.1's `matchesSection`/
  `matchesFilter` combination never removes a row from the database or the model, only from the
  currently-rendered subset; clearing the filter is a pure QML-property change with no repository
  round-trip.
