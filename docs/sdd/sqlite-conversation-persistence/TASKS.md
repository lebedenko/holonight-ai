# SDD Tasks — sqlite-conversation-persistence

## Build Infrastructure

- [x] T-001: Update src/persistence/CMakeLists.txt — convert to STATIC and embed migrations
  - REQs: REQ-C-003
  - Check: src/persistence/CMakeLists.txt converts `holonight_persistence` from `INTERFACE` to `STATIC`, includes `qt_add_resources(holonight_persistence migrations/ RESOURCE_PREFIX /holonight_persistence)` pointing to `src/persistence/migrations/0001_init.sql`, and links `Qt6::Sql` and `Qt6::Core`.

- [x] T-002: Update src/application/CMakeLists.txt — link persistence module
  - REQs: REQ-C-006
  - Check: src/application/CMakeLists.txt adds `conversation_list_model.h` and `conversation_list_model.cpp` to `holonight_application` target sources, and target_link_libraries includes `holonight_persistence` as PUBLIC.

- [x] T-003: Update tests/CMakeLists.txt — add persistence test files and Qt6::Sql
  - REQs: REQ-C-005
  - Check: tests/CMakeLists.txt adds `test_conversation_record.cpp`, `test_database_path.cpp`, `test_migration_runner.cpp`, `test_conversation_repository_worker_threading.cpp`, `test_conversation_repository_sqlite.cpp`, and `test_conversation_list_model.cpp` to the test executable; target_link_libraries includes `Qt6::Sql` for the test target.

## Domain Additions

- [x] T-004: Add Conversation::setTitle() method to holonight_domain
  - REQs: REQ-F-009, REQ-F-027
  - Check: src/domain/include/holonight_domain/conversation.h declares `void setTitle(QString title)`, and conversation.cpp implements it by replacing `title_` and stamping `updated_at_` to current UTC time.

- [x] T-005: Add deriveConversationTitle() free function to holonight_domain
  - REQs: REQ-F-027
  - Check: src/domain/include/holonight_domain/conversation.h declares `[[nodiscard]] QString deriveConversationTitle(const QString& firstMessageText)`, and conversation.cpp implements it to extract the first line (up to first newline) or first 48 characters (whichever is shorter), with "…" ellipsis and "Untitled" fallback per DESIGN §3.1.

- [x] T-006: GTest coverage for Conversation::setTitle() and deriveConversationTitle()
  - REQs: REQ-NF-006, REQ-F-027
  - Check: tests/domain/test_conversation.cpp contains test cases verifying setTitle() updates title and stamped updated_at time, deriveConversationTitle() truncates first line at 48 chars with ellipsis, returns "Untitled" for empty first line, and returns single line unchanged if shorter than 48 chars.

## Database Path Resolution

- [x] T-007: Create database_path.h and implement resolveDatabaseFilePath()
  - REQs: REQ-F-001
  - Check: src/persistence/include/holonight_persistence/database_path.h declares `[[nodiscard]] QString resolveDatabaseFilePath()`, which reads `$XDG_DATA_HOME` environment variable, falls back to `~/.local/share` if unset or empty, appends `holonight-ai/` directory, creates the path with `QDir().mkpath()` (no error if existing), and returns the full path to `conversations.db`.

- [x] T-008: GTest coverage for resolveDatabaseFilePath() with XDG fallback
  - REQs: REQ-F-001, REQ-NF-006
  - Check: tests/persistence/test_database_path.cpp contains test cases verifying resolveDatabaseFilePath() returns `$XDG_DATA_HOME/holonight-ai/conversations.db` when XDG_DATA_HOME is set, returns `~/.local/share/holonight-ai/conversations.db` when XDG_DATA_HOME is unset or empty, and creates parent directories without error if missing.

## Migration System

- [x] T-009: Create 0001_init.sql migration file with schema
  - REQs: REQ-F-002, REQ-F-005, REQ-F-011
  - Check: src/persistence/migrations/0001_init.sql contains DDL to create `schema_version(version INTEGER PRIMARY KEY, applied_at TIMESTAMP)`, `conversations(id TEXT PRIMARY KEY, title TEXT NOT NULL, created_at TIMESTAMP NOT NULL, updated_at TIMESTAMP NOT NULL, last_model_id TEXT)`, `messages(id TEXT PRIMARY KEY, conversation_id TEXT NOT NULL FOREIGN KEY REFERENCES conversations(id) ON DELETE CASCADE, role TEXT NOT NULL, content TEXT NOT NULL, status TEXT NOT NULL, created_at TIMESTAMP NOT NULL)`, and indexes on `conversations(updated_at DESC)` and `messages(conversation_id, created_at)`.

- [x] T-010: Create MigrationRunner class and implement builtInMigrations()/apply()
  - REQs: REQ-F-003, REQ-F-004
  - Check: src/persistence/include/holonight_persistence/migration_runner.h declares `MigrationRunner` with `struct Migration`, static `std::vector<Migration> builtInMigrations()`, `std::expected<int, QString> apply(QSqlDatabase&, const std::vector<Migration>&)`, `currentVersion()`, `applyOne()`, and `executeStatements()` methods; apply() queries schema_version table, applies unapplied migrations in ascending version order within transactions, and returns final version or error.

- [x] T-011: Implement MigrationRunner to read embedded migrations and apply sequentially
  - REQs: REQ-F-003, REQ-F-004, REQ-NF-009
  - Check: src/persistence/src/migration_runner.cpp implements builtInMigrations() to read `:/holonight_persistence/migrations/0001_init.sql` via QFile/QTextStream, executeStatements() to split SQL on `;` and execute each via QSqlQuery::exec(), apply() to wrap each migration in `db.transaction()`/`db.commit()`/`db.rollback()`, insert `(version, <now>)` into schema_version, and return version or error; migrations applied sequentially are idempotent (version check skips already-applied).

- [x] T-012: GTest coverage for MigrationRunner with fresh and existing databases
  - REQs: REQ-NF-007, REQ-NF-006
  - Check: tests/persistence/test_migration_runner.cpp contains test cases verifying builtInMigrations() returns at least one Migration with version 1 and name "0001_init", apply() on fresh `:memory:` database creates schema_version/conversations/messages tables with correct columns/types/indexes, re-applying migrations to existing database returns same version without re-executing statements, and schema_version table contains `(1, <timestamp>)` after successful application.

## Conversation Record Data Transfer Objects

- [x] T-013: Create conversation_record.h with ConversationSummary and LoadedConversation DTOs
  - REQs: REQ-C-002
  - Check: src/persistence/include/holonight_persistence/conversation_record.h declares `ConversationSummary` struct with fields `{QString id, QString title, QDateTime createdAt, updatedAt, std::optional<holonight_domain::ModelId> lastModelId}`, `LoadedConversation` struct with `{ConversationSummary summary, std::vector<holonight_domain::Message> messages}`, `Q_DECLARE_METATYPE` for both types and `holonight_domain::ModelId`, and `void registerMetaTypes()` function.

- [x] T-014: Implement conversation_record.cpp registerMetaTypes()
  - REQs: REQ-F-023
  - Check: src/persistence/src/conversation_record.cpp implements registerMetaTypes() to call `qRegisterMetaType<ConversationSummary>()`, `qRegisterMetaType<LoadedConversation>()`, and `qRegisterMetaType<holonight_domain::ModelId>()` before any cross-thread signal/slot connection using these types.

- [x] T-015: GTest coverage for conversation_record DTOs
  - REQs: REQ-NF-006
  - Check: tests/persistence/test_conversation_record.cpp contains test cases verifying ConversationSummary and LoadedConversation are equality-comparable, default-constructible, and registerMetaTypes() can be called multiple times idempotently (no crash or duplicate registration).

## Repository Abstract Interface

- [x] T-016: Create ConversationRepository abstract interface in conversation_repository.h
  - REQs: REQ-C-006, REQ-F-022, REQ-F-023
  - Check: src/persistence/include/holonight_persistence/conversation_repository.h declares `ConversationRepository : public QObject` with pure virtual methods `initialize()`, `createConversation(QString title = {})`, `listConversations()`, `loadConversation(QString)`, `renameConversation(QString, QString)`, `deleteConversation(QString)`, `updateLastModelId(QString, ModelId)`, `persistNewMessage(QString, Message)`, `persistMessageSettled(QString, Message)`, and Q_SIGNALS for `initialized()`, `unavailable(QString)`, `conversationCreated(ConversationSummary)`, `conversationListLoaded(QList<ConversationSummary>)`, `conversationLoaded(LoadedConversation)`, `conversationRenamed(ConversationSummary)`, `conversationDeleted(QString)`, `lastModelIdUpdated(QString, ModelId, QDateTime)`, `error(QString, QString)`.

- [x] T-017: Create FakeConversationRepository test double
  - REQs: REQ-C-006, REQ-NF-008
  - Check: tests/persistence/fake_conversation_repository.h declares `FakeConversationRepository : public ConversationRepository` storing conversations and messages in memory (QMap/QList), implementing all virtual methods by synchronously emitting their corresponding signals (in-process, no threading), and exposing internal state accessors (`allConversations()`, `allMessages()`) for test assertions.

## Persistence Worker Implementation

- [x] T-018: Create ConversationRepositoryWorker header with slots and signals
  - REQs: REQ-F-022
  - Check: src/persistence/include/holonight_persistence/detail/conversation_repository_worker.h declares `ConversationRepositoryWorker : public QObject` with constructor `(QString databasePath, QString connectionName)`, Q_SLOTS for `openAndMigrate()`, `createConversation(QString)`, `listConversations()`, `loadConversation(QString)`, `renameConversation(QString, QString)`, `deleteConversation(QString)`, `updateLastModelId(QString, ModelId)`, `persistNewMessage(QString, Message)`, `persistMessageSettled(QString, Message)`, and Q_SIGNALS matching ConversationRepository's interface plus a private `bool available_` flag and helper methods `connection()`, `markUnavailable(QString)`, `reportOperationError(QString, QSqlError)`, `isFatal(QSqlError)`.

- [x] T-019: Implement ConversationRepositoryWorker::openAndMigrate() and initialization
  - REQs: REQ-F-001, REQ-F-002, REQ-F-003, REQ-F-004, REQ-F-025
  - Check: src/persistence/src/detail/conversation_repository_worker.cpp implements openAndMigrate() to call `QSqlDatabase::addDatabase("QSQLITE", connection_name_)`, set database name, `.open()`, execute `PRAGMA foreign_keys = ON`, apply MigrationRunner::builtInMigrations(), emit `initialized()` on success, or emit `unavailable(reason)` on failure with `available_ = false`.

- [x] T-020: Implement ConversationRepositoryWorker CRUD slots (create, list, load, rename, delete)
  - REQs: REQ-F-006, REQ-F-007, REQ-F-008, REQ-F-009, REQ-F-010, REQ-F-021
  - Check: src/persistence/src/detail/conversation_repository_worker.cpp implements createConversation() to INSERT with generated UUID and kDefaultConversationTitle, listConversations() to SELECT all sorted by `updated_at DESC` and emit `conversationListLoaded()`, loadConversation() to SELECT one conversation and all its messages ordered by `created_at ASC, rowid ASC` and emit `conversationLoaded()`, renameConversation() to UPDATE title and updated_at then emit `conversationRenamed()`, deleteConversation() to DELETE messages then conversation within one transaction then emit `conversationDeleted()`.

- [x] T-021: Implement ConversationRepositoryWorker message and model ID persistence slots
  - REQs: REQ-F-012, REQ-F-013, REQ-F-014, REQ-F-020, REQ-F-021
  - Check: src/persistence/src/detail/conversation_repository_worker.cpp implements persistNewMessage() to INSERT message with all fields, updateLastModelId() to UPDATE last_model_id and updated_at then emit `lastModelIdUpdated()`, persistMessageSettled() to UPDATE message content and status (called only for terminal statuses per DESIGN §2.7).

- [x] T-022: Implement ConversationRepositoryWorker error classification (fatal vs non-fatal)
  - REQs: REQ-NF-004, REQ-NF-005
  - Check: src/persistence/src/detail/conversation_repository_worker.cpp implements isFatal() to return true for `QSqlError::ConnectionError` or `!db.isOpen()`, markUnavailable() to set `available_ = false` and emit `unavailable()`, reportOperationError() to check isFatal() and call markUnavailable() or emit `error()` accordingly; all SQL-executing slots guard with `if (!available_)` early return.

- [x] T-023: GTest white-box thread affinity test for ConversationRepositoryWorker
  - REQs: REQ-F-024, REQ-NF-001
  - Check: tests/persistence/test_conversation_repository_worker_threading.cpp constructs ConversationRepositoryWorker, moves it to a QThread, invokes openAndMigrate() via QMetaObject::invokeMethod, connects to initialized signal, and verifies via test helper `QThread::currentThread() == &worker_thread` inside the worker's slot that all SQL operations execute on the worker thread, not the main thread.

## SQLite Repository Façade

- [x] T-024: Create SqliteConversationRepository header with moveToThread setup
  - REQs: REQ-F-022, REQ-F-023, REQ-F-024
  - Check: src/persistence/include/holonight_persistence/sqlite_conversation_repository.h declares `SqliteConversationRepository : public ConversationRepository` with constructor `(QString databasePath)`, destructor calling `worker_thread_.quit(); worker_thread_.wait()`, and public method overrides implementing every ConversationRepository method via queued functor-based QMetaObject::invokeMethod calls to the worker.

- [x] T-025: Implement SqliteConversationRepository constructor and moveToThread wiring
  - REQs: REQ-F-022, REQ-F-023
  - Check: src/persistence/src/sqlite_conversation_repository.cpp implements constructor to call registerMetaTypes(), construct detail::ConversationRepositoryWorker with generated QUuid connection name, call worker_->moveToThread(&worker_thread_), connect `worker_thread_.finished` to `worker_.deleteLater`, connect every worker signal to corresponding ConversationRepository signal, and start the thread.

- [x] T-026: Implement SqliteConversationRepository public methods via functor invokeMethod
  - REQs: REQ-F-022, REQ-F-023
  - Check: src/persistence/src/sqlite_conversation_repository.cpp implements every public method (initialize, createConversation, listConversations, etc.) as a one-liner calling `QMetaObject::invokeMethod(worker_, [w = worker_, ...]{ w->methodName(...); }, Qt::QueuedConnection)` with captured arguments; no SQL types appear in SqliteConversationRepository's own method bodies.

- [x] T-027: Implement SqliteConversationRepository destructor with clean thread teardown
  - REQs: REQ-F-024
  - Check: src/persistence/src/sqlite_conversation_repository.cpp implements `~SqliteConversationRepository()` to call `worker_thread_.quit()` then `worker_thread_.wait()`, ensuring the worker thread stops and its events are processed before destruction completes.

- [x] T-028: GTest black-box QSignalSpy tests for SqliteConversationRepository
  - REQs: REQ-C-006, REQ-NF-001
  - Check: tests/persistence/test_conversation_repository_sqlite.cpp contains test cases using in-memory `:memory:` database verifying SqliteConversationRepository::createConversation() emits conversationCreated within <100ms, listConversations() sorted by updated_at DESC, loadConversation() returns messages in chronological order, renameConversation() moves row to front (updated_at refreshed), deleteConversation() cascades, no database writes occur on create-list-load-rename without explicit repository calls, and fatal errors emit unavailable() signal and subsequent calls are no-ops.

## Conversation List Model

- [x] T-029: Create ConversationListModel header with roles and mutators
  - REQs: REQ-F-015, REQ-C-008
  - Check: src/application/include/holonight_application/conversation_list_model.h declares `ConversationListModel : public QAbstractListModel` with `QML_ELEMENT` macro, `Roles` enum with `IdRole`, `TitleRole`, `UpdatedAtRole`, methods `rowCount()`, `data()`, `roleNames()`, `setAll(QList<ConversationSummary>)`, `upsertToFront(ConversationSummary)`, `touchToFront(QString id, QDateTime updatedAt)`, `removeById(QString id)`, `firstConversationId()`, `isEmpty()`.

- [x] T-030: Implement ConversationListModel CRUD mutators
  - REQs: REQ-F-015, REQ-F-016, REQ-F-021
  - Check: src/application/src/conversation_list_model.cpp implements setAll() calling beginResetModel/endResetModel, upsertToFront() checking if id exists and either inserting at front or moving existing to front via beginMoveRows/endMoveRows, touchToFront() moving to front and updating timestamp only (no title change), removeById() finding and removing by id, firstConversationId() returning optional first id, isEmpty() checking rowCount.

- [x] T-031: GTest coverage for ConversationListModel CRUD operations
  - REQs: REQ-NF-006, REQ-F-015
  - Check: tests/application/test_conversation_list_model.cpp contains test cases verifying setAll() populates model and emits reset, upsertToFront() on new summary inserts at row 0 and existing summary moves to row 0, touchToFront() preserves title and updates timestamp, removeById() removes row, firstConversationId() returns optional, isEmpty() correct, and model roles return correct data.

## ChatViewModel & Persistence Integration

- [x] T-032: Add Conversation repository seam to ChatViewModel constructor
  - REQs: REQ-C-007, REQ-F-001
  - Check: src/application/include/holonight_application/chat_view_model.h declares ChatViewModel constructor to accept `std::unique_ptr<holonight_persistence::ConversationRepository> repository` parameter, stores it as `repository_` member, constructs `ConversationListModel* conversation_list_model_`, and adds Q_PROPERTY `conversationList`, `activeConversationId`, `persistenceStatusMessage`.

- [x] T-033: Add persistence-related properties and invokables to ChatViewModel
  - REQs: REQ-F-006, REQ-F-016, REQ-F-017, REQ-F-018, REQ-F-019
  - Check: src/application/include/holonight_application/chat_view_model.h declares new Q_PROPERTIES for `conversationList`, `activeConversationId`, `persistenceStatusMessage`, new Q_INVOKABLE methods `createConversation()`, `switchConversation(QString)`, `renameConversation(QString, QString)`, `deleteConversation(QString)`, `dismissPersistenceBanner()`, and new Q_SIGNALS `activeConversationIdChanged()`, `persistenceStatusMessageChanged()`.

- [x] T-034: Update ChatViewModel::create() factory to instantiate SqliteConversationRepository
  - REQs: REQ-F-001
  - Check: src/application/src/chat_view_model.cpp updates static create() factory to construct `auto repository = std::make_unique<holonight_persistence::SqliteConversationRepository>(holonight_persistence::resolveDatabaseFilePath())` and pass it to the ChatViewModel constructor.

- [x] T-035: Implement ChatViewModel startup sequence (initialize→listConversations→auto-switch)
  - REQs: REQ-F-001, REQ-F-002, REQ-F-025, REQ-NF-002
  - Check: src/application/src/chat_view_model.cpp implements constructor to call `repository_->initialize()` (non-blocking), connect `repository_` signals to private slots `onRepositoryInitialized()`, `onRepositoryUnavailable()`, `onConversationListLoaded()`, etc.; onRepositoryInitialized() calls `repository_->listConversations()`; onConversationListLoaded() calls `conversation_list_model_->setAll()`, then if non-empty switchConversation(first), else createConversation(); onRepositoryUnavailable() sets `persistence_enabled_ = false`, `setPersistenceStatusMessage()`, and constructs ephemeral Conversation as fallback.

- [x] T-036: Implement ChatViewModel::createConversation() invokable
  - REQs: REQ-F-006, REQ-F-016
  - Check: src/application/src/chat_view_model.cpp implements createConversation() to call stop() first (if streaming), then if `persistence_enabled_` call `repository_->createConversation()`, else construct ephemeral Conversation; connects `onConversationCreated(summary)` to call `conversation_list_model_->upsertToFront()` and adopt via adoptConversation().

- [x] T-037: Implement ChatViewModel::switchConversation() and adoptConversation()
  - REQs: REQ-F-017, REQ-F-021
  - Check: src/application/src/chat_view_model.cpp implements switchConversation() to guard `id != activeConversationId()`, call stop() (mandatory), call `repository_->loadConversation(id)`, onConversationLoaded(loaded) to call adoptConversation(); adoptConversation() reconstructs Conversation from record, calls `message_model_->resetFrom()`, sets `selected_model_id_` to lastModelId or first available, emits `activeConversationIdChanged()`, clears errorMessage, refreshComputedProperties().

- [x] T-038: Implement ChatViewModel::renameConversation() invokable
  - REQs: REQ-F-009, REQ-F-018
  - Check: src/application/src/chat_view_model.cpp implements renameConversation() to if `id == activeConversationId()` call `conversation_->setTitle()`, if `persistence_enabled_` call `repository_->renameConversation()`, onConversationRenamed(summary) to call `conversation_list_model_->upsertToFront()`.

- [x] T-039: Implement ChatViewModel::deleteConversation() invokable with fallback
  - REQs: REQ-F-010, REQ-F-019
  - Check: src/application/src/chat_view_model.cpp implements deleteConversation() to record `wasActive = (id == activeConversationId())`, call stop() if wasActive, call `repository_->deleteConversation()`, onConversationDeleted(id) to call `conversation_list_model_->removeById()`, if wasActive call switchConversation(first) or createConversation().

- [x] T-040: Implement ChatViewModel message persistence call sites (persistNewMessage, updateLastModelId, persistMessageSettled)
  - REQs: REQ-F-012, REQ-F-013, REQ-F-020, REQ-F-021
  - Check: src/application/src/chat_view_model.cpp modifies send() to after appending User+Assistant messages to message_model_, if `persistence_enabled_` call `repository_->persistNewMessage()` for both, call `repository_->updateLastModelId()`, and if isFirstMessage and title is kDefaultConversationTitle derive title and call `repository_->renameConversation()`; modifies onStreamEvent() to when stream settles to terminal status (not on ContentDelta) if `persistence_enabled_` call `repository_->persistMessageSettled()`.

- [x] T-041: Implement ChatViewModel title auto-derivation guard during send
  - REQs: REQ-F-027
  - Check: src/application/src/chat_view_model.cpp implements send() to snapshot `isFirstMessage = conversation_->messages().empty()` before send, and after persistNewMessage calls check `if (isFirstMessage && conversation_->title() == kDefaultConversationTitle)` then derive and rename via `deriveConversationTitle(text)` + `conversation_->setTitle()` + `repository_->renameConversation()`.

- [x] T-042: Connect lastModelIdUpdated signal to ConversationListModel::touchToFront()
  - REQs: REQ-F-021
  - Check: src/application/src/chat_view_model.cpp in constructor connects `repository_->lastModelIdUpdated` to private slot `onLastModelIdUpdated()` that calls `conversation_list_model_->touchToFront(id, updatedAt)`.

- [x] T-043: Implement ChatViewModel::dismissPersistenceBanner() invokable
  - REQs: REQ-F-026
  - Check: src/application/src/chat_view_model.cpp implements dismissPersistenceBanner() to call `setPersistenceStatusMessage(QString())`.

- [x] T-044: GTest coverage for ChatViewModel startup and fallback paths
  - REQs: REQ-NF-006, REQ-F-001, REQ-F-025
  - Check: tests/application/test_chat_view_model.cpp contains test cases verifying ChatViewModel constructor with real repository (using FakeConversationRepository) emits initialized signal, calls listConversations, auto-switches to first on non-empty list, creates new conversation on empty list; test with repository returning unavailable signal verifies persistence_enabled_ is false and ephemeral Conversation constructed; test verifies exactly 4 database writes for 3-message conversation: 2 persistNewMessage + 1 updateLastModelId + 1 persistMessageSettled.

- [x] T-045: GTest coverage for ChatViewModel CRUD invokables and title derivation
  - REQs: REQ-NF-006, REQ-F-006, REQ-F-016, REQ-F-017, REQ-F-018, REQ-F-019, REQ-F-027
  - Check: tests/application/test_chat_view_model.cpp contains test cases verifying createConversation() emits conversationCreated signal and adopts as active, switchConversation() calls stop() before loadConversation, renameConversation() calls repository_ and updates title in-memory if active, deleteConversation() falls back to first or create-new if deleting active, title derivation truncates first message on first send, stop-before-switch guard prevents orphaned streaming in old conversation.

## QML Conversation List UI

- [x] T-046: Implement ConversationListPanel.qml with New Chat button and ListView
  - REQs: REQ-F-015, REQ-F-016
  - Check: qml/workspace/ConversationListPanel.qml contains ColumnLayout with Button "New Chat" calling ChatViewModel.createConversation() and ListView bound to ChatViewModel.conversationList with ConversationListDelegate as delegate, exposing conversationId/title/updatedAt as required properties.

- [x] T-047: Implement ConversationListDelegate.qml with inline edit and delete UI
  - REQs: REQ-F-017, REQ-F-018, REQ-F-019
  - Check: qml/workspace/ConversationListDelegate.qml contains Row showing title (or inline TextField on double-click), formatted updatedAt, edit icon, and delete icon; clicking delete shows inline "Delete? [Yes] [No]" prompt (pure QML state, local `property bool confirmingDelete`); Yes calls onDeleteConfirmed signal, No restores normal view; double-click or edit icon shows inline TextField calling onRenameRequested on focus loss or Enter key; onActivateRequested fired when clicked and not in edit/delete mode.

- [x] T-048: Add conversation list panel and persistence banner to WorkspaceWindow.qml
  - REQs: REQ-F-015, REQ-F-026
  - Check: qml/workspace/WorkspaceWindow.qml adds RowLayout wrapping existing chat column with new ConversationListPanel on left side, adds Rectangle banner below top bar bound to `ChatViewModel.persistenceStatusMessage.length > 0` visible binding with error text and dismiss button calling ChatViewModel.dismissPersistenceBanner(), banner styled same as existing errorMessage banner.

## Verification Tasks

- [x] T-049: Run qmllint on ConversationListPanel.qml and ConversationListDelegate.qml
  - REQs: (code quality)
  - Check: `task qmllint` reports no "unknown type" or "undeclared" errors for ChatViewModel properties and invokables accessed in qml/workspace/ConversationListPanel.qml and qml/workspace/ConversationListDelegate.qml; all property bindings and signal handlers are recognized as valid.

- [x] T-050: Run clang-format on new C++ persistence and application files
  - REQs: (code style)
  - Check: `task format-check` reports no formatting violations in src/persistence/include/holonight_persistence/*.h, src/persistence/src/*.cpp, src/application/include/holonight_application/conversation_list_model.h, src/application/src/conversation_list_model.cpp, and modified src/application/src/chat_view_model.cpp; `task format` auto-formats all files and build succeeds.

- [x] T-051: Run clang-tidy with WarningsAsErrors=* on persistence and application modules
  - REQs: (code quality)
  - Check: `task tidy` runs with -WarningsAsErrors='*' on holonight_persistence and holonight_application targets, reports zero errors or warnings, build/tidy.log shows "passed" for both targets.

- [x] T-052: Run GTest suite via task test with coverage
  - REQs: REQ-NF-006, REQ-C-005
  - Check: `task configure-tests && task test` builds and executes all GTest test executables including test_conversation_record.cpp, test_database_path.cpp, test_migration_runner.cpp, test_conversation_repository_worker_threading.cpp, test_conversation_repository_sqlite.cpp, test_conversation_list_model.cpp, test_chat_view_model.cpp; all tests PASS; zero files created in ~/.local/share/holonight-ai/ or any filesystem location during test execution (`:memory:` databases only).

- [x] T-053: Execute manual smoke test protocol against local Ollama
  - REQs: REQ-NF-006, REQ-F-024, REQ-F-025
  - Check: Execute all 13 steps: (1) Start Ollama with ≥1 model pulled, (2) `task run` launches holonight-chat, (3) Verify conversation list panel appears on left with "New Chat" button, (4) Send "Hello" message, observe streamed response in message list, (5) Click "New Chat" to create second conversation, (6) Verify first conversation appears in list, (7) Click first conversation to switch, verify message history reloads, (8) Double-click conversation title to inline-edit, change name, press Enter, verify list updates with new title at top, (9) Click delete icon on a conversation, confirm prompt appears, click Yes, verify conversation removed, click first conversation's delete, click No, verify conversation remains, (10) Stop Ollama service mid-chat and send a message, verify the existing transient inline error banner (`errorMessage`, "Failed to connect to Ollama...") appears — not the persistence banner — and clears on the next successful send, (11) Restart app, verify first saved conversation reloads with correct messages, title, and reselected model, (12) Relaunch with `$XDG_DATA_HOME` pointed at a read-only directory, verify the app starts without crashing, shows the sticky "Persistence unavailable" banner (`persistenceStatusMessage`), and chat remains fully usable in ephemeral/in-memory mode, (13) Close app mid-stream (in normal mode), window closes within 1 second with exit code 0 and no crash; all 13 steps complete successfully.

---

**Total Tasks**: 53
**Section Summary**:
- Build infrastructure: 3 tasks
- Domain additions: 3 tasks
- Database path resolution: 2 tasks
- Migration system: 3 tasks
- Conversation record DTOs: 3 tasks
- Repository abstract interface: 2 tasks
- Persistence worker implementation: 7 tasks
- SQLite repository façade: 5 tasks
- Conversation list model: 3 tasks
- ChatViewModel & persistence integration: 13 tasks
- QML conversation list UI: 3 tasks
- Verification tasks: 5 tasks
