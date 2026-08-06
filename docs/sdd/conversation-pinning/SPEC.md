# SPEC: Pin Conversations in the Sidebar

**Feature:** Add conversation pinning to create a persistent "Pinned" section above "Recent" in the left sidebar, enabling users to organize frequently-used conversations.

**Status:** Requirements Approved

**Date:** 2026-08-03

## Overview

The left sidebar conversation list currently displays all conversations in a single "Recent" section, ordered by most-recently-updated (`updated_at DESC`). This cycle adds pinning functionality: users can pin/unpin conversations via the existing 3-dot row context menu, which becomes a toggleable "Pin"/"Unpin" action; pinned conversations move to a new "Pinned" section positioned above "Recent" and sorted by pin-time (most-recently-pinned first). The Pinned section is hidden when empty. The search filter applies uniformly to both sections, and empty filtered sections hide their headers. Pinned state is persisted via SQLite migration and reflected to the UI through signals (no full-reload polling), exactly mirroring the existing rename/delete signal pattern. Deleting or renaming a pinned conversation works identically to unpinned conversations — no special validation or confirmation required.

## Scope

- Add a toggleable "Pin"/"Unpin" menu item to each conversation row's context menu, positioned between the existing "Rename" and "Delete" items.
- Introduce a new "Pinned" section header above "Recent", displayed only when at least one conversation is pinned.
- Move pinned conversations to the "Pinned" section; immediately move unpinned conversations back to "Recent".
- Sort Pinned conversations by pin-time (`pinned_at DESC`, most-recently-pinned first); preserve Recent's existing `updated_at DESC` sort.
- Extend the search/filter logic to apply to both Pinned and Recent sections uniformly; hide section headers when their sections are empty (due to filtering or having zero conversations).
- Persist pin state via a new SQLite migration adding a `pinned_at` column (nullable, storing timestamp or NULL for unpinned conversations) to the `conversations` table.
- Emit new `conversationPinned(id)` and `conversationUnpinned(id)` signals from `ChatViewModel` and wire them to `ConversationListModel` to update the display without full list reload.
- Add corresponding roles to `ConversationListModel` and update `ConversationRepository` with new pinning methods, mirroring the existing rename/delete pattern.
- Preserve all existing behaviors: renaming or deleting a pinned conversation works identically to unpinned (no special interactions, no forced unpin, no altered confirmation).

## Functional Requirements

### Context Menu and Toggle

**REQ-F-001 (Conditional)**
If a conversation is not pinned, the context menu shall display a "Pin" menu item positioned between the "Rename" and "Delete" items.

- **Acceptance Criterion:** Right-clicking (or using the row's 3-dot button) on an unpinned conversation shows a menu with three items in order: "Rename", "Pin", "Delete"; the menu closes after selecting any item.

**REQ-F-002 (Conditional)**
If a conversation is pinned, the context menu shall display an "Unpin" menu item in place of "Pin" at the same position, with "Rename" and "Delete" remaining in order.

- **Acceptance Criterion:** Right-clicking (or using the 3-dot button) on a pinned conversation shows a menu with three items in order: "Rename", "Unpin", "Delete"; clicking "Unpin" unpin the conversation.

### Pinning and Section Movement

**REQ-F-003 (Event-driven)**
When a conversation is pinned, the system shall immediately move it from the "Recent" section to the "Pinned" section positioned above "Recent", with no duplication.

- **Acceptance Criterion:** Clicking "Pin" on a conversation removes it from the Recent section's visible rows and inserts it into the Pinned section; the conversation appears in exactly one section at a time; the UI updates without requiring a full list reload or application restart.

**REQ-F-004 (Event-driven)**
When a conversation is unpinned, the system shall immediately move it back to the "Recent" section, ordered by `updated_at DESC` with other Recent conversations (no change to Recent's existing sort order).

- **Acceptance Criterion:** Clicking "Unpin" on a pinned conversation removes it from the Pinned section and re-inserts it into the Recent section at the position determined by its `updated_at` timestamp; the UI updates without full reload.

### Pinned Section Order and Visibility

**REQ-F-005 (State-driven)**
While the Pinned section contains one or more conversations, they shall be displayed ordered by `pinned_at DESC` (most-recently-pinned first).

- **Acceptance Criterion:** Pin conversation A, then pin conversation B; the Pinned section displays B above A (B is most-recently pinned). Update the title of conversation B (via rename); the Pinned section order remains unchanged (B still above A). Unpin B; re-pin B; the Pinned section now displays B first again.

**REQ-F-006 (Unwanted behaviour)**
The system shall not enforce an upper limit on the number of simultaneously pinned conversations.

- **Acceptance Criterion:** Pin 5, 10, or 50 conversations; all remain pinned without error or truncation; the Pinned section grows as needed without a "max pinned" cap.

**REQ-F-007 (State-driven)**
While the number of pinned conversations is zero, the "Pinned" section header and section shall not be displayed; the sidebar shows only the "Recent" section, appearing exactly as it does today.

- **Acceptance Criterion:** At application start (no pinned conversations), only the "Recent" section header is visible. Pin a conversation; the "Pinned" header and section appear above Recent. Unpin all pinned conversations; the "Pinned" header and section disappear again, leaving only "Recent" visible.

### Search and Filter Integration

**REQ-F-008 (State-driven)**
While a search filter is active (search text is non-empty), both the Pinned and Recent sections shall be filtered by the same case-insensitive substring match against conversation titles.

- **Acceptance Criterion:** Pin conversation "Important Meeting" and leave conversation "Recent Work" unpinned. Type "import" in the search field; only "Important Meeting" is visible (in Pinned section). Type "recent" in the search field; only "Recent Work" is visible (in Recent section). Type ""; all conversations reappear.

**REQ-F-009 (State-driven)**
While a search filter is active, if a section (Pinned or Recent) has zero matching rows, that section's header shall not be displayed.

- **Acceptance Criterion:** Pin conversation "Alpha" and "Beta". Type "gamma" in the search field; both sections have zero matches; neither the "Pinned" nor "Recent" header is displayed, only empty space or a "no results" message (existing search behavior is preserved). Clear the search; both headers and conversations reappear.

### Deletion and Renaming Behavior

**REQ-F-010 (Unwanted behaviour)**
When deleting a conversation, the system shall not require it to be unpinned first; a pinned conversation deletes immediately via the existing delete-confirmation flow, without special validation or unpin enforcement.

- **Acceptance Criterion:** Pin a conversation. Click Delete on the pinned conversation's row. The delete confirmation (existing UI) appears. Confirm deletion. The conversation is deleted from both the database and the Pinned section; no additional "Unpin first" warning or step appears.

**REQ-F-011 (Unwanted behaviour)**
When renaming a conversation, the system shall not modify its pin state or its position within the Pinned section; renaming a pinned conversation preserves its pinned status and does not change `pinned_at` or Pinned-section sort order.

- **Acceptance Criterion:** Pin conversation A ("Title A"), then pin conversation B ("Title B"). Rename A to "New Title A". The conversation remains in Pinned section, its position relative to B does not change (B remains more recently pinned), and `pinned_at` for A is unchanged.

### Signal-Driven Updates

**REQ-F-012 (Event-driven)**
When a conversation is pinned or unpinned, the `ChatViewModel` shall emit a corresponding `conversationPinned(id)` or `conversationUnpinned(id)` signal, and the `ConversationListModel` shall process these signals to update the model without requiring a full list reload or data refresh.

- **Acceptance Criterion:** Pin a conversation via the UI. Observe that `conversationPinned(id)` signal fires from `ChatViewModel`. The `ConversationListModel` updates to reflect the move from Recent to Pinned section without calling `fetchAll()` or re-querying the database. UI updates immediately. Unpin the conversation; `conversationUnpinned(id)` fires, model updates, and UI reflects the move back to Recent.

## Non-functional Requirements

### SQLite Persistence

**REQ-NF-001 (Ubiquitous)**
Conversation pin state shall be persisted to the SQLite `conversations` table via a new numbered migration adding a nullable `pinned_at` column, following the project's existing migration pattern (plain `ALTER TABLE ... ADD COLUMN pinned_at TEXT`, no destructive or backfill operations beyond DEFAULT NULL).

- **Acceptance Criterion:** A new migration file (e.g., `0005_add_pinned_at.sql`) exists in `src/persistence/migrations/`, contains the exact SQL `ALTER TABLE conversations ADD COLUMN pinned_at TEXT;`, and is invoked automatically by the migration runner on next application launch; the column is nullable and defaults to NULL for all existing rows.

**REQ-NF-002 (Ubiquitous)**
The `ConversationRepository` interface shall define two new pure-virtual methods: `pinConversation(id)` and `unpinConversation(id)`, both returning void; the SQLite implementation shall update the `pinned_at` column to the current timestamp (for pin) or NULL (for unpin) respectively.

- **Acceptance Criterion:** `ConversationRepository` base class adds these two virtual methods. `ConversationRepositoryWorker` (SQLite backend) implements both, executing `UPDATE conversations SET pinned_at = CURRENT_TIMESTAMP WHERE id = ?` for pin and `UPDATE conversations SET pinned_at = NULL WHERE id = ?` for unpin. No new error-return semantics required; failures are logged as existing database errors are.

**REQ-NF-003 (Ubiquitous)**
The `ConversationListModel` shall add two new roles: `PinnedRole` (returns boolean true/false) and `PinnedAtRole` (returns the pinned_at timestamp as a QDateTime or string for sort/display purposes).

- **Acceptance Criterion:** `ConversationListModel::roleNames()` includes both roles with readable names (e.g., "Pinned", "PinnedAt"). QML code can access `model.Pinned` to determine section placement and `model.PinnedAt` for debug logging; internal model sorting uses `PinnedAt` to order Pinned conversations.

### QML and UI Modeling

**REQ-NF-004 (Ubiquitous)**
The `ConversationListPanel.qml` shall separate the conversation list into two logical sections (Pinned and Recent) using conditional visibility or separate ListView/Repeater delegates; the implementation shall apply filtering to both sections using the same filter logic.

- **Acceptance Criterion:** QML code shows either two ListView instances (one per section, both bound to the same filtered model but rendered in different DOM order) or a single ListView with dynamically grouped sections via a `section.property`-based model or computed filter. All pinned conversations appear above all recent conversations, and filtering (search text) applies uniformly to both logical groups.

**REQ-NF-005 (Ubiquitous)**
The context menu in `ConversationListDelegate.qml` shall be updated to include the toggleable "Pin"/"Unpin" menu item; the menu shall remain simple (no nested submenus or dynamic filtering of disabled items — the toggle is implicit in the two-item set).

- **Acceptance Criterion:** QML code shows a `MenuItem` with label binding (e.g., `text: model.Pinned ? "Unpin" : "Pin"`) and an `onTriggered` handler calling `ChatViewModel.pinConversation(id)` or `ChatViewModel.unpinConversation(id)` as appropriate.

### ChatViewModel Invokables

**REQ-NF-006 (Ubiquitous)**
The `ChatViewModel` class shall add two new Q_INVOKABLE methods: `pinConversation(id)` and `unpinConversation(id)`. These methods shall delegate to the `ConversationRepository` interface, emit the corresponding `conversationPinned(id)` or `conversationUnpinned(id)` signals, and follow the same async pattern as the existing `renameConversation()` and `deleteConversation()` methods (i.e., invoke the repository on a background thread, emit signals on completion).

- **Acceptance Criterion:** Calling `ChatViewModel::pinConversation(conversationId)` from QML or C++ queues the pin operation on the repository worker thread, which updates the database, and upon success emits `conversationPinned(conversationId)` on the main thread; same pattern for unpin. If the repository method is not yet implemented at the time of QML binding, the operation proceeds without error (the invokable exists and is callable).

**REQ-NF-007 (Ubiquitous)**
The `ChatViewModel` shall emit two new signals: `conversationPinned(QUuid id)` and `conversationUnpinned(QUuid id)`, both with a single `id` parameter matching the conversation ID.

- **Acceptance Criterion:** `ChatViewModel` class definition includes `Q_SIGNAL void conversationPinned(QUuid id);` and `Q_SIGNAL void conversationUnpinned(QUuid id);` (or equivalent C++23 signal syntax); these signals are connected to `ConversationListModel` slots or handlers to update the model's internal state and notify views of changes.

### Model Update Mechanism

**REQ-NF-008 (Ubiquitous)**
The `ConversationListModel` shall define two private slots (or handlers): `onConversationPinned(QUuid id)` and `onConversationUnpinned(QUuid id)`. Each handler shall locate the conversation in the model by id, update its `Pinned`/`PinnedAt` roles, emit `dataChanged()` for that row, and adjust the model's internal sort order to move the conversation between sections without requiring a full reload.

- **Acceptance Criterion:** When `ChatViewModel::conversationPinned(id)` is emitted, `ConversationListModel::onConversationPinned(id)` is triggered (via signal connection in `ChatViewModel` constructor or connection in QML). The row is updated in place (same model index or moved if sections are separate), and the UI reflects the change immediately; no `fetchAll()`, `fetchRecent()`, or full-reload query is issued. Same for unpin.

### No Changes to Existing Behaviors

**REQ-NF-009 (Ubiquitous)**
The system shall not change the sort order of Recent conversations (they remain sorted by `updated_at DESC`). Renaming, message additions, or other updates to a conversation shall not change its `pinned_at` timestamp or Pinned-section position.

- **Acceptance Criterion:** Create and name conversation "A", then conversation "B". Unpin both (if pinned). Pinning B first, then A, puts A in the Pinned section above B. Add a message to B (or rename B); B's position in Pinned relative to A does not change. Verify Recent section still sorts by `updated_at DESC` independently.

## Constraints

### Migration and Schema

**REQ-C-001 (Ubiquitous)**
The system shall use an additive-only SQLite migration adding the `pinned_at` column; no destructive schema changes (DROP, RENAME, DELETE, no backfill logic) are permitted. The migration shall follow the existing numbered-migration pattern (e.g., `0005_add_pinned_at.sql`).

- **Acceptance Criterion:** The new migration file contains a single `ALTER TABLE conversations ADD COLUMN pinned_at TEXT;` statement and is placed in the numbered sequence in `src/persistence/migrations/`. The migration runner automatically applies it; existing conversations have `pinned_at = NULL`. No data loss, no rollback steps, no conditional migration logic.

### Signal-Driven Workflow

**REQ-C-002 (Ubiquitous)**
The system shall not issue full-list reload queries (`fetchAll()` or equivalent) when a conversation is pinned or unpinned. Updates shall be driven entirely by the `conversationPinned` / `conversationUnpinned` signals, mirroring the existing `conversationRenamed` and `conversationDeleted` signal pattern.

- **Acceptance Criterion:** Code review of `ChatViewModel`, `ConversationRepository`, and `ConversationListModel` finds no `fetchAll()` or full-reload query issued on pin/unpin. The `onConversationPinned` / `onConversationUnpinned` handlers update the model in place. Running the app with query logging (or profiling) shows only single-row UPDATE statements for pin/unpin, not SELECT statements fetching the full conversation list.

### No Breaking Changes to Existing API

**REQ-C-003 (Ubiquitous)**
Existing `ChatViewModel` methods and signals (`renameConversation()`, `deleteConversation()`, `createConversation()`, `switchConversation()`, `conversationRenamed`, `conversationDeleted`) shall remain unchanged; the new pinning methods and signals are additions only, not replacements or modifications.

- **Acceptance Criterion:** Code review finds no changes to existing method signatures, return types, or signal parameters. The new `pinConversation()`, `unpinConversation()`, `conversationPinned`, and `conversationUnpinned` are additions. Existing QML and C++ code using rename/delete/create continues to work without modification.

### No Changes to Conversation Deletion or Renaming Logic

**REQ-C-004 (Ubiquitous)**
The system shall not add new logic to the delete or rename flows to unpin conversations, emit special signals, or validate pin state. Deletion and renaming proceed identically whether the conversation is pinned or unpinned.

- **Acceptance Criterion:** Code review of `ConversationRepository::deleteConversation()` and `renameConversation()` methods finds no reference to `pinned_at`, no additional SQL operations related to pinning, and no new conditional logic based on pin state. Delete and rename behave identically for pinned and unpinned conversations.

## Non-Goals

- Adding keyboard shortcuts (e.g., Ctrl+P) to pin/unpin conversations.
- Implementing drag-to-reorder conversations within the Pinned section.
- Providing "pin all" or "unpin all" bulk actions.
- Adding a visual pin indicator icon (e.g., a pushpin) within the row or section header (section placement alone indicates pin status).
- Enforcing a maximum number of pinnable conversations (no limit/cap).
- Adding a "favorites" or "starred" synonym for pinning (single terminology: "Pinned" only).
- Modifying the search/filter UI or adding a dedicated "Pinned" filter toggle (search applies uniformly to both sections).
- Altering the conversation model types, domain, or message-handling logic (pinning is purely a UI/persistence concern).
- Changing the behavior of Recently, updated_at timestamps, or conversation creation/deletion flows.

## Traceable Acceptance Summary

| Check | Requirements |
| --- | --- |
| Right-click an unpinned conversation; verify "Pin" appears between "Rename" and "Delete" in the menu | REQ-F-001 |
| Right-click a pinned conversation; verify "Unpin" appears at the same position, replacing "Pin" | REQ-F-002 |
| Click "Pin" on a conversation; verify it disappears from Recent and appears in Pinned section above Recent | REQ-F-003 |
| Click "Unpin" on a pinned conversation; verify it returns to Recent, ordered by `updated_at DESC` | REQ-F-004 |
| Pin conversation A, then B; verify B appears above A in Pinned (most-recently-pinned first) | REQ-F-005 |
| Pin 5, 10, or more conversations without error or truncation | REQ-F-006 |
| At app start, verify no pinned conversations exist and "Pinned" header is hidden (sidebar shows only "Recent") | REQ-F-007 |
| Pin a conversation, then search for another; verify filter applies to both sections | REQ-F-008 |
| With search filter active, verify empty sections' headers are hidden | REQ-F-009 |
| Pin a conversation, then delete it via the existing delete-confirmation flow; verify no "Unpin first" requirement appears | REQ-F-010 |
| Pin a conversation, rename it; verify pin state and Pinned-section order are unchanged | REQ-F-011 |
| Pin a conversation via the UI; observe `conversationPinned(id)` signal and verify UI updates without full reload | REQ-F-012 |
| Code review migration files: verify new `0005_add_pinned_at.sql` (or appropriate number) adds `pinned_at TEXT` column | REQ-NF-001 |
| Code review `ConversationRepository`: verify `pinConversation(id)` and `unpinConversation(id)` pure-virtual methods exist | REQ-NF-002 |
| Code review `ConversationListModel`: verify `PinnedRole` and `PinnedAtRole` roles are added to `roleNames()` | REQ-NF-003 |
| QML code review `ConversationListPanel`: verify Pinned and Recent sections are rendered separately with applied filter | REQ-NF-004 |
| QML code review `ConversationListDelegate`: verify "Pin"/"Unpin" menu item label binds to `model.Pinned` state | REQ-NF-005 |
| Code review `ChatViewModel`: verify `pinConversation(id)` and `unpinConversation(id)` Q_INVOKABLE methods exist and delegate to repository | REQ-NF-006 |
| Code review `ChatViewModel`: verify `conversationPinned(QUuid)` and `conversationUnpinned(QUuid)` signals are declared | REQ-NF-007 |
| Code review `ConversationListModel`: verify `onConversationPinned(id)` and `onConversationUnpinned(id)` handlers update model in place without full reload | REQ-NF-008 |
| Verify Recent section remains sorted by `updated_at DESC` independently; renaming/updating a pinned conversation does not change its Pinned position | REQ-NF-009 |
| Code review migration files: verify single `ALTER TABLE` statement, no destructive SQL, follows existing pattern | REQ-C-001 |
| Code review repository and model: verify no `fetchAll()` or full-reload queries on pin/unpin; only signal-driven updates | REQ-C-002 |
| Code review `ChatViewModel`: verify existing methods/signals (`renameConversation`, `deleteConversation`, etc.) are unchanged | REQ-C-003 |
| Code review `ConversationRepository` delete/rename methods: verify no pinning-related logic, SQL, or conditional checks | REQ-C-004 |

## Known Risks

- **Signal-Model Sync:** If the `conversationPinned` / `conversationUnpinned` signals fire before the repository update completes (e.g., due to async-thread race), the model may temporarily show stale pin state. Mitigation: ensure signals are emitted *after* the repository confirms the update (standard pattern from existing rename/delete cycles; verify in code review).
- **Filtered Section Visibility:** If a pinned conversation is hidden by the active search filter, the Pinned section header is hidden, but the conversation still exists in the database with `pinned_at != NULL`. Clearing the search will show the Pinned section again. This is expected and documented in REQ-F-009; no risk if properly implemented.
- **Sort Stability:** If multiple conversations have the same `pinned_at` timestamp (rare but possible if pin operations occur in rapid succession), the secondary sort order is undefined. Mitigation: add a tiebreaker (e.g., conversation ID) if needed, or accept natural database order as a minor UX inconsistency.
