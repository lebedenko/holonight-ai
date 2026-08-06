# SPEC: SQLite Conversation Persistence

## Overview

This specification defines the third phase of the holonight-ai roadmap, adding durable conversation and message storage to the existing in-memory chat system. The cycle implements and integrates two modules:

1. **holonight_persistence module**: An SQLite-backed repository layer for conversations and messages, featuring a schema migration system (version table, numbered `.sql` migrations applied sequentially at startup), CRUD operations (create, read, list, update, delete), and thread-safe async access via a dedicated worker thread with Qt signals for result delivery.
2. **Conversation list UI (QML integration)**: A minimal conversation switcher in the workspace window enabling: list saved conversations (title + last-updated timestamp), select/switch to a conversation (loads its messages and restores the last-used model id), create a new conversation, rename a conversation (inline edit of title), and delete a conversation (with a non-modal inline confirmation step).

Both subsystems build on the existing in-memory Conversation, Message, and MessageId domain types and the in-flight ChatViewModel/ChatController orchestration (from prior cycles), and integrate with the OllamaProvider's model selection via a `last_model_id` field persisted per conversation (used to preselect the model picker on conversation load).

The worker-thread threading model ensures the GUI thread never blocks on synchronous SQL I/O, maintaining UI responsiveness guarantees. Startup database failure (corrupted file, permissions, disk full) is handled gracefully by falling back to in-memory-only mode with an inline banner notification, preserving application usability.

---

## Non-Goals

The following are **explicitly out of scope** for this cycle:

- **No credentials or secret-service work.** The `holonight_credentials` module (libsecret, KWallet) remains untouched. Conversation data is stored unencrypted on disk.
- **No additional providers.** Only the existing Ollama adapter is supported; no OpenAI, Anthropic, Google, or other provider integration.
- **No attachments, file uploads, or binary data.** Messages store only text content; no images, files, documents, or embedded media.
- **No full-text search.** Conversations and messages are not indexed for search; filtering/querying remains a future cycle.
- **No export/import of conversation data.** No format for exporting to JSON, markdown, or other formats.
- **No soft-delete, trash, or undo.** Deleted conversations and messages are permanently removed from disk; no recovery or undelete mechanism.
- **No multi-window or multi-process concurrent access.** Only a single `holonight-chat` process instance is supported, consistent with the project's current single-instance model. Database file locking is SQLite's default; no explicit file-locking design beyond SQLite's built-in mechanisms.
- **No background/periodic checkpointing during streaming.** Content deltas arriving during message streaming are NOT persisted individually (per write-timing decision); only final settled status is written.
- **No quick-panel (compact surface) integration.** The conversation list UI is workspace-only; the quick-panel remains a future "HoloNight integration" roadmap phase.
- **No migration rollback or downgrade.** Migrations are forward-only; there is no mechanism to revert to a prior schema version.

---

## Functional Requirements

### Database Initialization & Schema Management

**REQ-F-001: Database File Location with XDG Fallback**
> When the application starts, it shall locate or create the conversation database at `$XDG_DATA_HOME/holonight-ai/conversations.db`, falling back to `~/.local/share/holonight-ai/conversations.db` if `XDG_DATA_HOME` is unset or empty.

- **Acceptance Criterion**: The database file path is resolved correctly using the XDG Base Directory Specification; if `XDG_DATA_HOME` is set, the file is created/opened at that path; if unset, `~/.local/share` is used; parent directories (`holonight-ai/`) are created if missing (no error if directory already exists); verified by inspecting file system after app initialization.

**REQ-F-002: Database Creation on First Run**
> On the application's first run, if the database file does not exist, it shall be created with an empty `schema_version` table and the full initial schema (migration 0001) applied automatically.

- **Acceptance Criterion**: Running the app for the first time creates `conversations.db` if missing; the file contains `schema_version` table with columns `(version INTEGER PRIMARY KEY, applied_at TIMESTAMP)`; migration 0001 is applied, creating `conversations` and `messages` tables with expected schemas (verified by querying table structure with `.schema` in sqlite3 CLI); the app does not crash or hang.

**REQ-F-003: Sequential Schema Migration Application**
> The system shall implement a migration mechanism that applies numbered SQL migrations (e.g., `0001_init.sql`, `0002_*.sql`) sequentially at startup, checking `schema_version` table to determine which migrations have already been applied.

- **Acceptance Criterion**: A `migrations/` directory exists in the repository (or packaged in the app resources) containing `0001_init.sql` defining the initial schema; a migration runner queries `schema_version`, identifies unapplied migrations, and applies them in ascending order; each migration is wrapped in a transaction (all-or-nothing semantics); a row `(1, <timestamp>)` is inserted into `schema_version` after 0001 is applied; if 0002+ migrations exist in a future cycle, they are applied in order; verified by unit tests applying migrations to fresh in-memory databases.

**REQ-F-004: Schema Version Idempotency**
> If the database already exists and its schema version matches the application's latest version, the system shall skip migration application and proceed immediately.

- **Acceptance Criterion**: Subsequent app starts (after the database was already initialized) do not re-apply 0001; the `schema_version` table is queried, and if the latest version is already present, no additional migrations run; verified by startup logs/traces showing "migrations up to date" or similar.

### Conversation Data Model

**REQ-F-005: Conversation Table Schema**
> The `conversations` table shall store: `id` (UUID string, primary key), `title` (TEXT, initially auto-derived from first message), `created_at` (TIMESTAMP), `updated_at` (TIMESTAMP), `last_model_id` (TEXT, nullable, stores the model id used in the last send/regenerate action for that conversation).

- **Acceptance Criterion**: The schema migration 0001 creates a `conversations` table with columns: `id TEXT PRIMARY KEY`, `title TEXT NOT NULL`, `created_at TIMESTAMP NOT NULL`, `updated_at TIMESTAMP NOT NULL`, `last_model_id TEXT`; verified by running `.schema conversations` in sqlite3 CLI on the initialized database.

**REQ-F-006: Create New Conversation**
> When the user creates a new conversation (e.g., "New Chat" button in the UI), a conversation row is inserted with a generated `ConversationId`, empty/placeholder `title`, `created_at` and `updated_at` set to current time, and `last_model_id` set to NULL.

- **Acceptance Criterion**: A `ConversationRepository::createConversation()` method (or similar) accepts optional title text and returns a `Conversation*` with a new ID; the ID is inserted into the database on a worker thread; the method returns immediately (non-blocking); the conversation title is initially empty or "Untitled" if not provided; verified by unit test calling the method and querying the database to confirm the row exists.

**REQ-F-007: List All Conversations**
> The system shall provide a method to fetch all saved conversations, returning a list of `Conversation` objects sorted by `updated_at` descending (newest first), suitable for binding to a QML ListView.

- **Acceptance Criterion**: A `ConversationRepository::listConversations()` method returns a `QList<Conversation>` or similar; the list is sorted with most-recently-updated conversations at index 0; the method is thread-safe (async, results delivered via signal); verified by unit test populating the database with 3 conversations, calling the method, and confirming the returned list is in reverse-timestamp order.

**REQ-F-008: Load Conversation and Messages**
> When the user selects a conversation from the list, the system shall load that conversation's details and all its messages (in chronological order) from the database, including restoration of `last_model_id` to the model picker.

- **Acceptance Criterion**: A `ConversationRepository::loadConversation(ConversationId id)` method queries the database for the conversation row and all messages; returns a `Conversation*` with messages populated; the list of messages is in ascending order by `created_at`; `Conversation::lastModelId()` or similar accessor returns the stored `last_model_id`, allowing ChatViewModel to preselect the model; verified by unit test loading a saved conversation and confirming message count and order.

**REQ-F-009: Rename Conversation (Update Title)**
> When the user renames a conversation via the inline edit UI, the `title` field is updated in the database, and `updated_at` is refreshed to the current time.

- **Acceptance Criterion**: A `ConversationRepository::renameConversation(ConversationId id, QString newTitle)` method updates the database row's `title` and `updated_at`; the operation is async (returns immediately via signal); verified by unit test renaming a conversation and querying the database to confirm both fields changed.

**REQ-F-010: Delete Conversation with Cascade**
> When the user deletes a conversation, all its messages are deleted first (cascade delete), then the conversation row itself is deleted. The operation is permanent; no soft-delete or recovery mechanism.

- **Acceptance Criterion**: A `ConversationRepository::deleteConversation(ConversationId id)` method executes: `DELETE FROM messages WHERE conversation_id = ?` followed by `DELETE FROM conversations WHERE id = ?` in a single transaction; the operation is async; both rows are confirmed deleted from the database after the method completes; verified by unit test deleting a conversation with 5 messages and querying to confirm both tables are empty for that ID.

### Message Data Model

**REQ-F-011: Message Table Schema**
> The `messages` table shall store: `id` (UUID string, primary key), `conversation_id` (UUID string, foreign key to `conversations.id`), `role` (TEXT: "user" or "assistant"), `content` (TEXT, the message body), `status` (TEXT: "pending", "streaming", "complete", "error", "cancelled"), `created_at` (TIMESTAMP).

- **Acceptance Criterion**: Migration 0001 creates a `messages` table with columns: `id TEXT PRIMARY KEY`, `conversation_id TEXT NOT NULL`, `role TEXT NOT NULL`, `content TEXT NOT NULL`, `status TEXT NOT NULL`, `created_at TIMESTAMP NOT NULL`, and a foreign key constraint `FOREIGN KEY(conversation_id) REFERENCES conversations(id) ON DELETE CASCADE`; verified by `.schema messages` in sqlite3 CLI.

**REQ-F-012: Write Message on Creation (Placeholder)**
> When a message is created by the application (e.g., user sends text, creating a User message and a placeholder Assistant message), a row is inserted into the `messages` table immediately with the message's initial state (role, empty/placeholder content, status).

- **Acceptance Criterion**: When `ChatController::send()` is called, two messages are appended to the Conversation in-memory; the `ConversationRepository` receives a signal or callback to persist both messages; User message is written with `status = "complete"` and full user text because it is already locally accepted; Assistant message is written with `status = "streaming"` and empty/placeholder content; verified by unit test calling send() and querying the database to confirm both rows exist.

**REQ-F-013: Update Message on Status Settle**
> As the message stream progresses, content deltas are accumulated in-memory but NOT written to disk per-delta (no per-token writes). When the stream settles to a final status (Complete, Error, or Cancelled), the message row's `content` and `status` fields are updated atomically in a single write.

- **Acceptance Criterion**: During streaming, `Conversation::messages().back()` is updated in-memory on each `StreamEvent::ContentDelta`, but the database is not queried; when `StreamEvent::Completed` or `StreamEvent::Error` arrives, the repository writes the message to disk with final `status` and `content`; verified by unit test streaming 10 tokens, querying the database mid-stream (expecting no updates), then streaming completion and querying again (expecting final content written); zero per-token DB writes confirmed via query count assertions.

**REQ-F-014: Update Last Model ID on Send/Regenerate**
> When the user sends a message or regenerates an assistant response with a selected model, the `last_model_id` field of the active conversation is updated in the database to that model's ID.

- **Acceptance Criterion**: `ConversationRepository::updateLastModelId(ConversationId id, ModelId modelId)` updates the `last_model_id` column and `updated_at`; this is called by ChatViewModel or ChatController whenever a model is selected for a send/regenerate action; verified by unit test selecting a model, calling send(), and confirming the database row reflects the new `last_model_id`.

### Conversation List UI

**REQ-F-015: Display Conversation List**
> The workspace window shall display a list of all saved conversations, showing the conversation title and last-updated timestamp, sorted by most-recent first. The list is bound to a Qt model (QAbstractListModel or similar) that updates whenever conversations are created, updated, or deleted.

- **Acceptance Criterion**: A `ConversationListModel` Qt class (or similar) inherits `QAbstractListModel` and exposes `listConversations()` results to QML via `roles`; QML ListView binds to `model: conversationListModel` and displays each conversation; selecting a conversation from the list triggers a signal or invokable to switch to it; verified by manual test running the app and seeing a list of saved conversations with titles and dates.

**REQ-F-016: Create New Conversation from UI**
> A "New Chat" button in the conversation list area creates a new conversation in the database and switches to it immediately, making it the active conversation.

- **Acceptance Criterion**: QML "New Chat" button's `onClicked` calls `ChatViewModel::createConversation()` invokable; the method calls `ConversationRepository::createConversation()` (async), and when the result signal fires, the new conversation is added to the list model and automatically selected; the active Conversation object in ChatViewModel is switched to the new one; verified by manual test clicking "New Chat", seeing it appear at the top of the list, and having an empty message view ready to start typing.

**REQ-F-017: Switch to a Saved Conversation**
> When the user clicks on a saved conversation in the list, the system loads that conversation's data and messages from the database, replaces the active Conversation object, and updates the message list view and model picker.

- **Acceptance Criterion**: QML list delegate's `onClicked` or similar calls `ChatViewModel::switchConversation(ConversationId id)` invokable; the invokable calls `ConversationRepository::loadConversation(id)` (async); when the result arrives, the active Conversation is replaced, message list updates to show the loaded messages, and the model picker preselects the conversation's `last_model_id` (or the first available model if last_model_id is NULL); verified by manual test switching between two conversations and confirming message histories are correct.

**REQ-F-018: Rename Conversation (Inline Edit)**
> The conversation list displays an edit button or supports double-clicking to inline-edit the conversation title. When the user confirms the edit (Enter key or focus lost), the title is updated in the database and the list updates to reflect the change.

- **Acceptance Criterion**: Each list item shows an edit icon or supports double-click to enter edit mode; QML TextEdit/TextField appears inline with the title text selected; pressing Enter or losing focus calls `ChatViewModel::renameConversation(ConversationId id, QString newTitle)` invokable; the method calls `ConversationRepository::renameConversation()` (async), and when the result signal fires, the list model updates to display the new title; verified by manual test double-clicking a conversation, typing a new name, pressing Enter, and seeing the list update.

**REQ-F-019: Delete Conversation with Inline Confirmation**
> A delete button (trash icon) next to each conversation triggers a non-modal inline confirmation step (no `Dialog` or `MessageDialog` popup). The confirmation shows a "Delete? [Yes] [No]" prompt or similar, allowing the user to confirm or cancel. On confirmation, the conversation is deleted from the database.

- **Acceptance Criterion**: Clicking the delete icon does not immediately remove the conversation; instead, a confirmation UI appears (e.g., the list item expands to show "Delete [conversation name]?" with Yes/No buttons, or a banner appears below the list item). Clicking "No" cancels and restores the normal list view. Clicking "Yes" calls `ChatViewModel::deleteConversation(ConversationId id)` invokable, which calls `ConversationRepository::deleteConversation()` (async); when the result signal fires, the list item is removed from the list model; if the deleted conversation was active, the conversation switches to the first remaining conversation or creates a new one; verified by manual test deleting a conversation, confirming the deletion, and seeing it removed from the list; also test canceling the deletion and confirming the conversation remains.

### Message Write Timing & Streaming Integration

**REQ-F-020: Message Persistence During Chat Session**
> For each active Conversation, the persistence layer is notified of: (1) new messages appended (User and initial Assistant placeholder), (2) streaming content deltas (NOT written to disk per-delta), (3) final message status settlements (Complete, Error, Cancelled). Only (1) and (3) result in database writes; (2) is accumulated in-memory only.

- **Acceptance Criterion**: The Conversation or ChatController notifies the repository layer via signals/callbacks when: (a) a User message is created (write immediately), (b) an Assistant placeholder message is created (write immediately with empty content and status=streaming), (c) streaming deltas arrive (no DB write, content accumulated in-memory), (d) the stream settles (one DB write with final content and status); unit test confirms: sending a 3-message conversation with a 100-token streamed response results in exactly 4 database writes (1 user + 1 assistant-init + 1 user + 1 assistant-final), not 1 user + 1 assistant + 100+ updates.

**REQ-F-021: Conversation Updated Timestamp on Message Send**
> When a message is sent in an active conversation, the conversation's `updated_at` field is refreshed to the current time, marking it as recently active (so it sorts to the top of the conversation list).

- **Acceptance Criterion**: After a User message is appended to a conversation and persisted, `ConversationRepository::updateLastMessageTime(ConversationId id)` is called (or equivalent), updating `updated_at`; the conversation list model is notified and re-sorts to reflect the changed timestamp; verified by unit test sending a message to an old conversation and confirming it appears at index 0 in the sorted list.

### Worker Thread & Async Persistence

**REQ-F-022: Dedicated Worker Thread for Database Access**
> Database operations (queries, inserts, updates, deletes) execute on a dedicated worker thread, not the GUI/main thread. The thread owns its own `QSqlDatabase` connection with driver `QSQLITE`.

- **Acceptance Criterion**: A `ConversationRepository` class (or similar) runs a `QThread` internally; all database methods (createConversation, listConversations, etc.) queue work items to the thread; the thread executes the work and returns results via Qt signals; the GUI thread never calls `QSqlDatabase::exec()` or similar directly; verified by unit test and code review confirming no blocking SQL I/O on the GUI thread; unit tests verify thread ownership by inspecting `QThread::currentThread()` inside the repository's work methods.

**REQ-F-023: Result Delivery via Qt Signals**
> Repository methods are non-blocking and return immediately. Results are delivered asynchronously via Qt signals (e.g., `conversationCreated(Conversation*)`, `conversationListLoaded(QList<Conversation>)`, `error(QString errorMessage)`).

- **Acceptance Criterion**: Each repository method has a corresponding signal; e.g., `void createConversation()` returns void immediately, and the result is emitted via `void conversationCreated(const Conversation&)` signal when the worker thread completes; ChatViewModel connects to these signals and updates its properties accordingly; verified by unit test connecting to signals, calling a repository method, and confirming the signal fires with the expected result within a reasonable time (e.g., <100ms for in-memory test database).

**REQ-F-024: GUI Thread Non-Blocking Guarantee**
> The system shall guarantee that the GUI/main thread never blocks waiting for database I/O. All database access is async and non-blocking by design.

- **Acceptance Criterion**: Running the app and performing database operations (send message, create conversation, switch conversation) results in fluid, responsive UI with no visible stalls or frame drops; verified by manual test sending a message and switching conversations rapidly without observing UI freezes; profiling with Qt Creator shows no 100ms+ blocking calls from the GUI thread into database code.

### Startup Failure Handling

**REQ-F-025: Graceful Fallback on Database Initialization Failure**
> If the database file cannot be opened, created, or a migration fails at startup (e.g., corrupted file, disk full, permission denied), the application shall NOT crash. Instead, it falls back to in-memory-only mode and displays an inline banner informing the user that persistence is unavailable.

- **Acceptance Criterion**: Running the app with: (1) `$XDG_DATA_HOME` pointing to a read-only directory, (2) a corrupted/unreadable existing `conversations.db` file, or (3) simulated disk-full condition during migration, results in: (a) no crash, (b) the app continues to run with in-memory conversations, (c) an inline banner appears at the top of the window with text such as "Persistence unavailable: conversations will not be saved after restart"; verified by manual test and/or unit test with mocked QSqlDatabase::open() returning false, confirming the banner appears and the app is usable.

**REQ-F-026: Persistence Unavailable Banner**
> The inline banner informing the user that persistence is unavailable shall persist for the lifetime of the session (or until manually dismissed, if a close button is provided). It shall not be a blocking modal dialog.

- **Acceptance Criterion**: A QML banner or similar UI element appears below the window's top bar with the message; it does not prevent interaction with the chat (send/receive messages, open conversation list, etc.); reusing the existing `ChatViewModel.errorMessage` inline-banner pattern from the prior chat-window-qml cycle (or an equivalent non-modal banner); verified by manual test confirming the window remains fully usable while the banner is displayed.

### Title Auto-Derivation

**REQ-F-027: Auto-Derive Conversation Title from First User Message**
> When the user sends the first message in a new conversation with no explicit title, the conversation's title is automatically set to the first ~40–50 characters of the user's message (or the first line, whichever is shorter). The title is persisted to the database and remains user-editable via rename.

- **Acceptance Criterion**: A new conversation created with `createConversation()` (no title provided) has an empty or "Untitled" title initially; when the first User message is sent, the title is derived from the message content (first 40–50 chars or first line, implementation detail finalized in Design); `ConversationRepository::updateConversationTitle(ConversationId, QString)` (or integrated into message-creation logic) updates the database; the updated title appears in the conversation list; verified by manual test creating a new conversation, sending "Hello, this is a long first message that should be truncated", and observing the conversation list shows title as "Hello, this is a long first message that s..." or similar truncation.

---

## Non-Functional Requirements

### Threading & Responsiveness

**REQ-NF-001: GUI Thread Never Blocks on Database I/O**
> Database operations are fully asynchronous. The GUI thread never calls blocking SQL queries; all I/O runs on the worker thread.

- **Acceptance Criterion**: Running under a profiler (Qt Creator, perf, or similar) shows no 50ms+ blocking calls from the main thread into SQLite or QSqlDatabase code; sending messages, switching conversations, and renaming conversations do not produce visible frame stalls or UI freezes; verified by manual responsiveness test and code review confirming all database paths are async.

**REQ-NF-002: Startup Performance with Large Conversation Count**
> Application startup time shall not increase significantly with conversation count. Listing conversations is paginated or lazy-loaded if the count exceeds ~100 conversations (exact threshold decided at Design stage).

- **Acceptance Criterion**: Starting the app with 50 saved conversations completes within 3 seconds (including database open, migration check, initial conversation list query); verified by unit test and/or manual test with a seeded database of 50+ conversations, measuring startup time.

**REQ-NF-003: Message Load Performance**
> Loading a conversation with a large message history (e.g., 1000 messages) from the database shall complete within a reasonable time (Design stage finalizes acceptable bound, e.g., <1 second) and not block the GUI thread.

- **Acceptance Criterion**: The repository's `loadConversation()` method fetches all messages for a 1000-message conversation on the worker thread and delivers results via signal within <2 seconds; verified by unit test with a seeded in-memory database of 1000 messages, confirming load time and no GUI blocking.

### Error Resilience

**REQ-NF-004: Partial Failure Does Not Disable Chat**
> If a single database operation fails (e.g., updating the last_model_id fails), the failure is logged and the chat session continues. The user sees an inline error message but can continue chatting; only a complete database unavailability (cannot open at startup) triggers the fallback-to-in-memory mode.

- **Acceptance Criterion**: Unit test confirms that calling `updateLastModelId()` with a simulated DB error (mocked QSqlQuery::exec() returns false) emits an error signal but does not crash the app or prevent further sends; inline error message is displayed (or silently logged, depending on Design); verified by running the app with a mocked DB failure during a send operation and confirming the app remains usable.

**REQ-NF-005: Graceful Handling of Database Corruption**
> If the database file becomes corrupted mid-session (e.g., disk error while writing), the system shall detect the error, log it, display an inline error banner, and either: (a) disable persistence for the remainder of the session, or (b) attempt a recovery/rebuild of the database. (Exact strategy is a Design decision.)

- **Acceptance Criterion**: Unit test or integration test with a corrupted database file (or simulated corruption via mocked QSqlQuery) confirms the app does not crash, displays an error message to the user, and remains usable for in-memory chat; verified by code review and testing.

### Constraint: Testability

**REQ-NF-006: Full GTest Coverage for Persistence Layer**
> All persistence functionality shall be verifiable through unit tests using in-memory SQLite (`:memory:` database) with no real filesystem I/O.

- **Acceptance Criterion**: At least one test file exists (e.g., `tests/persistence/test_conversation_repository.cpp`); test fixtures use `QSqlDatabase::addDatabase("QSQLITE", "test_connection")` with database name `:memory:`; at least one test file exists for migrations (e.g., `tests/persistence/test_migrations.cpp`); `task test` runs all persistence tests via CTest; tests pass without writing to disk; verified by test execution and filesystem inspection (no files created in `$HOME/.local/share` during `task test`).

**REQ-NF-007: Migration Testing**
> Migrations must be testable. A fresh in-memory database can have migrations applied and the resulting schema verified without network or filesystem access.

- **Acceptance Criterion**: A test fixture sets up an empty in-memory SQLite database, applies migration 0001, and verifies the resulting tables and columns match the expected schema; `SELECT * FROM sqlite_master WHERE type='table'` returns the correct table list; `PRAGMA table_info(conversations)` returns the expected columns and types; test passes via `task test` without requiring any file on disk.

**REQ-NF-008: Repository Mocking for Application Tests**
> The `ConversationRepository` interface shall be abstract/mockable so that higher-level tests (e.g., ChatViewModel tests) can provide a fake repository without requiring real database access.

- **Acceptance Criterion**: An abstract `ConversationRepository` base class or interface is defined with virtual methods for all CRUD operations; a `FakeConversationRepository` test double is provided that stores conversations/messages in memory; ChatViewModel tests use the fake repository to verify signal emission and state updates without touching SQLite; verified by code review and test structure.

### Data Integrity

**REQ-NF-009: Atomic Transaction Semantics for Delete**
> Conversation deletion is atomic: the conversation row and all related messages are deleted in a single transaction, or the entire operation is rolled back. No partial deletes.

- **Acceptance Criterion**: A conversation with 5 messages is deleted; either the conversation and all 5 messages are removed from the database, or (on error/rollback) the conversation and all 5 messages remain; verified by unit test querying the database after a delete operation and confirming either full presence or full absence, never a partial state.

**REQ-NF-010: Foreign Key Constraints Enforced**
> The database schema enforces referential integrity: all `conversation_id` values in the `messages` table must exist in the `conversations` table, and deleting a conversation cascades to delete its messages.

- **Acceptance Criterion**: Attempting to insert a message with a non-existent `conversation_id` fails (FOREIGN KEY constraint error); verified by unit test attempting an invalid insert and confirming the database rejects it; deleting a conversation removes all its messages via cascade (verified by test above, REQ-NF-009).

---

## Constraint Requirements

### Database Technology

**REQ-C-001: SQLite as Storage Backend**
> The persistence layer shall use SQLite as the storage backend, accessed via Qt6's `QSqlDatabase` API with the `QSQLITE` driver.

- **Acceptance Criterion**: No other database engine (PostgreSQL, MySQL, MariaDB) is supported; all queries use `QSqlDatabase` and `QSqlQuery` classes; the driver name is `QSQLITE`; verified by code review and CMakeLists.txt linkage (Qt6::Sql dependency).

**REQ-C-002: Schema Versioning Table**
> The database shall maintain a `schema_version` table with columns `version` (INTEGER PRIMARY KEY) and `applied_at` (TIMESTAMP), tracking which migrations have been applied.

- **Acceptance Criterion**: Migration 0001 creates this table; after applying migration 0001, a row `(1, <current-timestamp>)` is inserted; any subsequent migration 0002+ inserts a corresponding row; queries to `schema_version` determine the current version and which migrations to apply; verified by unit test.

**REQ-C-003: SQL Migrations in `.sql` Files**
> Schema migrations are defined as numbered SQL files (e.g., `0001_init.sql`, `0002_add_column.sql`) embedded in the application (as Qt resources or packaged files) and executed sequentially at startup.

- **Acceptance Criterion**: A `migrations/` directory (or Qt resource path) contains `.sql` files; the migration runner reads these files in numeric order, parses SQL statements, and executes them; `task test` includes a test verifying all migration files can be parsed and applied; verified by code review of migration file naming and runner logic.

### Testing Constraints

**REQ-C-004: Zero Filesystem I/O in Test Suite**
> All persistence tests use in-memory SQLite (`:memory:`) and do not create files in `$HOME/.local/share` or any other filesystem location.

- **Acceptance Criterion**: Running `task test` (filtered to persistence tests) produces no files in `~/.local/share/holonight-ai/` or other user directories; verified by inspecting the filesystem before and after `task test` and confirming no new files; also verified by test code using `:memory:` database names exclusively.

**REQ-C-005: GTest Framework**
> All persistence tests are written using GTest, following this project's existing test infrastructure (same as `holonight_domain` and `holonight_application` tests).

- **Acceptance Criterion**: Test files are located in `tests/persistence/` and named `test_*.cpp`; tests use `TEST()` or `TEST_F()` macros, `EXPECT_*` assertions, and GTest fixtures; test executables are built and linked with `gtest_main`; `task test` runs them via CTest; verified by code review and `task test` execution.

**REQ-C-006: Repository Interface for Dependency Injection**
> The `ConversationRepository` class shall expose an abstract interface (base class or pure virtual methods) that allows tests and higher-level code to inject a fake implementation without coupling to the real SQLite-backed implementation.

- **Acceptance Criterion**: An `IConversationRepository` interface or `ConversationRepository` abstract base class is defined with virtual methods for: createConversation, listConversations, loadConversation, renameConversation, deleteConversation, updateLastModelId, etc.; a concrete `SqliteConversationRepository` implements this interface; tests can provide a `FakeConversationRepository` or `MockConversationRepository` for testing higher-level components (ChatViewModel) without touching the database; verified by code review and test structure.

### Integration Constraints

**REQ-C-007: No Persistence Changes to ChatController or OllamaProvider**
> The `holonight_providers` and `holonight_application` modules' existing ChatController and OllamaProvider classes are NOT modified to add persistence logic. Persistence is decoupled and wired at the ChatViewModel / UI layer only.

- **Acceptance Criterion**: No imports of `holonight_persistence` appear in `ChatController` or `OllamaProvider` code; persistence integration happens via ChatViewModel receiving signals from the repository and notifying it of message/conversation state changes; verified by code review of ChatController and OllamaProvider files.

**REQ-C-008: QML Integration for Conversation List**
> The conversation list UI is implemented in QML (following the project's existing qml/workspace/ convention) and binds to C++ models and viewmodel invokables provided by ChatViewModel or a new `ConversationListViewModel` singleton.

- **Acceptance Criterion**: QML files in `qml/workspace/` (or new subdirectory) implement the conversation list view; a Qt model class (e.g., `ConversationListModel`) is registered as QML_ELEMENT and provides roles for conversation title, last-updated timestamp, and conversation ID; QML binds to `model: conversationListModel` and displays the list; verified by code review of QML files and CMakeLists.txt registration.

**REQ-C-009: No Multi-Process Database Access Design**
> The database access design assumes a single `holonight-chat` process instance. No file-locking, lease-based concurrency, or multi-process synchronization logic is implemented beyond SQLite's built-in single-writer locking.

- **Acceptance Criterion**: The worker thread holds a single `QSqlDatabase` connection; there is no multi-process coordination; running a second `holonight-chat` process instance may result in `SQLITE_BUSY` errors if both attempt to write simultaneously (acceptable, no explicit error handling beyond SQLite's default behavior); verified by code review confirming no multi-process logic exists.

**REQ-C-010: No Encryption or Credential Storage**
> Conversation data is stored unencrypted in the SQLite file. The `holonight_credentials` module (libsecret, KWallet) is not used for conversation persistence.

- **Acceptance Criterion**: No encryption key derivation or libsecret/KWallet integration in the `holonight_persistence` module; conversations are stored as plain-text SQL rows; verified by code review (no imports of holonight_credentials; no encryption libraries linked).

---

## Summary

This specification defines SQLite-backed persistence for conversations and messages in holonight-chat, the third phase of the roadmap. The system implements:

1. **Durable storage** via SQLite, with a sequential schema migration system enabling future schema evolution.
2. **Conversation CRUD** (create, list, load, rename, delete) and message persistence (write on creation and status settlement, not per-token during streaming).
3. **Async worker-thread access** ensuring the GUI thread never blocks, with results delivered via Qt signals.
4. **Minimal QML conversation list UI** (list, switch, create, rename, delete with inline confirmation).
5. **Graceful startup fallback** to in-memory mode if the database is unavailable, with an inline banner notification.
6. **Full GTest testability** using in-memory SQLite and a mockable repository interface, with zero real filesystem I/O in tests.

The design prioritizes UI responsiveness (worker thread, signals), data integrity (atomic deletes, foreign keys, migrations), and testability (dependency injection, in-memory database support). Encryption, multi-process access, and additional providers remain out of scope.
