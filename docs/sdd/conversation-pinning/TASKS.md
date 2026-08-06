# SDD Tasks — conversation-pinning

- [x] T-001: Add pinned_at migration file and register in runner
  - REQs: REQ-C-001, REQ-NF-001
  - Check: Running `task configure && task build` completes without error and `sqlite3` query confirms `conversations` table has a nullable `pinned_at TEXT` column.

- [x] T-002: Extend ConversationRecord with pinned_at field
  - REQs: REQ-NF-001
  - Check: `ConversationSummary` struct contains `std::optional<QDateTime> pinned_at;` member and persists through `summaryFromRecord()` round-trip.

- [x] T-003: Add pinning methods and signals to ConversationRepository interface
  - REQs: REQ-NF-002, REQ-NF-007
  - Check: Header declares pure-virtual `pinConversation(QString conversationId)` and `unpinConversation(QString conversationId)` methods, and declares signals `conversationPinned(QString conversationId, QDateTime pinnedAt)` and `conversationUnpinned(QString conversationId)`.

- [x] T-004: Implement pin/unpin slots in ConversationRepositoryWorker
  - REQs: REQ-NF-002, REQ-NF-009
  - Check: Worker's `listConversations()` returns rows ordered by `(pinned_at IS NULL) ASC, pinned_at DESC, updated_at DESC, rowid DESC`; `pinConversation()` slot updates `pinned_at` to CURRENT_TIMESTAMP; `unpinConversation()` slot sets `pinned_at` to NULL; `summaryFromRecord()` reads 7th column as `pinned_at`.

- [x] T-005: Add façade methods and signal relays to SqliteConversationRepository
  - REQs: REQ-NF-002
  - Check: Public `pinConversation()` and `unpinConversation()` methods invoke corresponding worker methods via `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` and relay worker's `conversationPinned`/`conversationUnpinned` signals to repository signals.

- [x] T-006: Override pin/unpin in FakeConversationRepository test double
  - REQs: REQ-NF-002
  - Check: Fake implementation overrides `pinConversation()` and `unpinConversation()`, updates in-memory `pinned_at` bookkeeping, and emits `conversationPinned` and `conversationUnpinned` signals without database calls.

- [x] T-007: Add SQLite round-trip tests for pin/unpin/rename/delete
  - REQs: REQ-NF-001, REQ-NF-002
  - Check: GTest cases verify pin sets `pinned_at` to non-NULL; unpin clears it to NULL; rename/delete operations leave `pinned_at` unchanged; list query returns pinned conversations before unpinned conversations, both groups sorted by their respective order-by fields.

- [x] T-008: Add PinnedRole/PinnedAtRole and pinning methods to ConversationListModel
  - REQs: REQ-NF-003, REQ-NF-008
  - Check: `Roles` enum defines `PinnedRole` and `PinnedAtRole`; `Row` struct has `bool pinned` and `QDateTime pinned_at` members; class provides `void markPinned(const QString& conversationId, const QDateTime& pinnedAt)`, `void markUnpinned(const QString& conversationId)`, and `Q_INVOKABLE int visibleCount(bool pinned, const QString& filterText) const` methods.

- [x] T-009: Fix upsertToFront/touchToFront semantics with dedicated row-move GTest
  - REQs: REQ-F-003, REQ-F-004, REQ-F-011, REQ-C-002
  - Check: GTest case "PinUnpinReorder" verifies that pinning A, B, C then unpinning B produces pinned block [A, C] at indices 0–1, B re-inserted in Recent block at correct `updated_at DESC` position, no duplication, and `beginMoveRows` indices are exactly correct; renamed pinned row does not move at all.

- [x] T-010: Add pin/unpin invokables and signals to ChatViewModel
  - REQs: REQ-NF-006, REQ-NF-007, REQ-F-012, REQ-C-002, REQ-C-003
  - Check: ChatViewModel defines `Q_INVOKABLE void pinConversation(const QString&)` and `unpinConversation(const QString&)`; declares signals `conversationPinned(QUuid id)` and `conversationUnpinned(QUuid id)`; constructor connects repository's `conversationPinned`/`conversationUnpinned` signals to private slots that convert QString ↔ QUuid and call `ConversationListModel` mutators.

- [x] T-011: Add application integration tests for ChatViewModel + ConversationListModel
  - REQs: REQ-F-003, REQ-F-004, REQ-F-011, REQ-F-012, REQ-C-002
  - Check: GTest cases verify `ChatViewModel::pinConversation()` invokable updates `conversationList()` in place via signal (no full reload); unpinned pinned conversation re-inserts it correctly; renamed pinned conversation stays in Pinned section at same position; signals propagate correctly with FakeConversationRepository.

- [x] T-012: Add toggleable Pin/Unpin menu item to ConversationListDelegate.qml
  - REQs: REQ-F-001, REQ-F-002, REQ-NF-005
  - Check: Delegate declares `required property bool pinned`; menu item's label reads `qsTr("Pin")` when `pinned === false` and `qsTr("Unpin")` when `pinned === true`; item is positioned between Rename and Delete items; click emits `pinToggleRequested()` signal.

- [x] T-013: Restructure ConversationListPanel.qml into two filtered sections
  - REQs: REQ-F-008, REQ-F-009, REQ-NF-004
  - Check: Panel contains two ListView sections (Pinned above Recent) both sourced from `ChatViewModel.conversationList`; both sections apply filter uniformly; Pinned section header height is `0` when `pinnedVisibleCount === 0`; Recent section header height is `0` when `recentVisibleCount === 0`; `pinnedVisibleCount` and `recentVisibleCount` are recomputed on searchText change and model-change signals (modelReset, rowsInserted, rowsRemoved, rowsMoved, dataChanged).

- [x] T-014: Add QML tests and verification checklist for panel visibility
  - REQs: REQ-F-007, REQ-F-009
  - Check: ConversationListDelegate unit tests verify label text and signal fire per `pinned` state; ConversationListPanel visibility verified via manual checklist (per project "no visual verification" convention): Pinned header absent when zero pinned conversations exist; Recent header absent when filter results in zero unpinned matches.
