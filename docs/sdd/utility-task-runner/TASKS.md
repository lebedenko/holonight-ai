# SDD Tasks — utility-task-runner

- [x] T-001: Define TitleSource enum in holonight_domain
  - REQs: REQ-F-005, REQ-C-003, REQ-F-006
  - Check: A `TitleSource` enum with exactly three values (`Fallback`, `Generated`, `Manual`) exists in `src/domain/include/holonight_domain/title_source.h`.

- [x] T-002: Define UtilityConfig struct in holonight_config
  - REQs: REQ-F-001
  - Check: `UtilityConfig` struct with a single optional `default_utility_model` field exists in `src/config/include/holonight_config/utility_config.h`.

- [x] T-003: Add loadUtilityConfig and saveUtilityConfig to ConfigRepository
  - REQs: REQ-F-001
  - Check: `ConfigRepository` declares `loadUtilityConfig()` and `saveUtilityConfig()` methods following the same pattern as `load*Config()` and `save*Config()` for the four existing provider configs.

- [x] T-004: Create 0003_add_title_source.sql migration file
  - REQs: REQ-F-009, REQ-F-010
  - Check: Migration file `src/persistence/migrations/0003_add_title_source.sql` exists and adds `title_source TEXT NOT NULL DEFAULT 'Manual'` column to the `conversations` table.

- [x] T-005: Register 0003_add_title_source migration in MigrationRunner
  - REQs: REQ-F-009
  - Check: Migration is registered in `MigrationRunner::builtInMigrations()` with version 3 and correctly reads the SQL resource.

- [x] T-006: Add title_source field to ConversationSummary
  - REQs: REQ-F-005, REQ-C-003
  - Check: `ConversationSummary` in `holonight_persistence/conversation_record.h` includes a `title_source` field of type `TitleSource` with default value `Fallback`.

- [x] T-007: Implement titleSourceFromText and titleSourceToText helpers
  - REQs: REQ-F-005, REQ-F-010
  - Check: Two helper functions exist to convert between `TitleSource` enum and its TEXT representation (`"Fallback"`, `"Generated"`, `"Manual"`), matching the pattern of existing `messageStatusFromText`/`messageStatusToText`.

- [x] T-008: Extend ConversationRepository::renameConversation() signature
  - REQs: REQ-F-006, REQ-C-003
  - Check: `renameConversation()` method signature adds a `TitleSource` parameter with default value `Manual`, preserving all existing call sites' behavior.

- [x] T-009: Update ConversationRepositoryWorker for title_source persistence
  - REQs: REQ-F-005, REQ-F-010, REQ-C-003, REQ-F-006, REQ-NF-004
  - Check: Worker's `listConversations()` and `loadConversation()` SELECT statements include `title_source` column; `createConversation()` explicitly binds `title_source = 'Fallback'` on INSERT; `renameConversation()` uses guarded `UPDATE ... WHERE title_source = 'Fallback'` for `Generated` writes only and checks `numRowsAffected() == 0` before emitting signal.

- [x] T-010: Create UtilityTaskRunner class skeleton with isolated provider instances
  - REQs: REQ-NF-001, REQ-NF-002
  - Check: `UtilityTaskRunner` class in `src/application/include/holonight_application/utility_task_runner.h` declares own `shared_ptr<>` instances for all four providers (Ollama, OpenAI, Anthropic, Google) and a `ProviderRuntimeCoordinator`, distinct from `ChatViewModel`'s instances.

- [x] T-011: Implement UtilityTaskRunner constructor with provider configuration
  - REQs: REQ-NF-001, REQ-NF-002
  - Check: Constructor accepts `CredentialStore*`, `UtilityConfig`, and endpoint URLs; creates four isolated provider instances; calls `setTemperature(0.3)` and `setMaxOutputTokens(64)` on each; constructs and configures `ProviderRuntimeCoordinator` with all four providers registered, each with a no-op `persist_models` lambda.

- [x] T-012: Implement UtilityTaskRunner::requestTitleGeneration() entry point
  - REQs: REQ-F-003, REQ-F-004, REQ-NF-003, REQ-NF-004, REQ-NF-005, REQ-F-002
  - Check: Method accepts `conversationId`, `firstUserText`, `firstAssistantText`, `chatFallbackModel`, and optional `taskOverride`; checks in-flight guard (no-op if already present); logs and ignores non-nullopt `taskOverride`; calls `resolveModel()` then `onProviderReady()`.

- [x] T-013: Implement UtilityTaskRunner::resolveModel() method
  - REQs: REQ-F-002, REQ-U-001
  - Check: Method returns the model to use following the three-tier fallback chain: per-task override (not implemented, logged if non-nullopt) → `default_utility_model` from config (if set) → fallback to `chatFallbackModel`; returns `std::nullopt` only if `default_utility_model.provider_id` is unknown (4 known IDs only).

- [x] T-014: Implement UtilityTaskRunner::onProviderReady() callback
  - REQs: REQ-F-003, REQ-NF-002, REQ-U-001
  - Check: Callback invoked after `ProviderRuntimeCoordinator::readiness()` reports provider status; if `MissingCredential` or `Unavailable`, logs and removes from in-flight guard; if `Ready`, calls `dispatchGeneration()`.

- [x] T-015: Implement UtilityTaskRunner::dispatchGeneration() method
  - REQs: REQ-F-004, REQ-NF-003
  - Check: Method builds a single synthetic prompt message from first user and first assistant text with the template "Write a short, descriptive title (max 6 words, no quotes, no trailing punctuation) for this exchange.\n\nUser: {user}\n\nAssistant: {assistant}"; calls `dispatchSendChat()` and stores the returned handle and empty accumulated buffer in `generations_[conversationKey]`.

- [x] T-016: Implement UtilityTaskRunner::dispatchSendChat() provider routing
  - REQs: REQ-NF-001
  - Check: Method accepts a `ModelId` and message history; routes to the correct isolated provider instance's `sendChat()` based on `model.provider_id`; returns the `HttpRequestHandlePtr` and provides callback that accumulates `ContentDelta.text`, emits `titleGenerated()` on `Completed` with non-empty text, logs on error/empty/cancel and removes from `generations_`.

- [x] T-017: Implement title-generation success and failure handling in dispatchSendChat callback
  - REQs: REQ-F-007, REQ-F-008, REQ-U-002
  - Check: Callback's `Completed` branch emits `titleGenerated(conversationId, title)` only if accumulated text is non-empty (trimmed); `Error` and empty-result branches log via `qWarning()` with contextual message and do not emit signal; all branches remove conversation from in-flight guard.

- [x] T-018: Create UtilityTaskRunner instance in ChatViewModel::create()
  - REQs: REQ-NF-001, REQ-NF-002, REQ-C-001
  - Check: `ChatViewModel` declares a `std::unique_ptr<UtilityTaskRunner>` member; constructor instantiates it with the `CredentialStore*`, loaded `UtilityConfig`, and provider endpoint URLs; connects `titleGenerated()` signal to `onTitleGenerated()` slot.

- [x] T-019: Add trigger condition to ChatViewModel::onStreamEvent()
  - REQs: REQ-F-003, REQ-F-006, REQ-NF-004, REQ-NF-005
  - Check: After existing `persistMessageSettled()` call, a guard condition evaluates to true if and only if `persistence_enabled_ && conversation_->messages().size() == 2 && lastMessage.role() == Assistant && lastMessage.status() == Complete && current_title_source_ == TitleSource::Fallback`; if true, calls `utility_task_runner_->requestTitleGeneration()` with first user message, last message text, and current chat model.

- [x] T-020: Implement ChatViewModel::onTitleGenerated() signal handler
  - REQs: REQ-NF-003, REQ-F-006
  - Check: Slot receives `conversationId` and `title` from `titleGenerated()` signal; calls `repository_->renameConversation(conversationId, title, TitleSource::Generated)`; on success, `conversationRenamed` signal updates `current_title_source_`, `ConversationListModel`, and QML bindings without any additional wiring.

- [x] T-021: Update ChatViewModel::send() to explicitly pass TitleSource::Fallback
  - REQs: REQ-C-003
  - Check: The existing `repository_->renameConversation()` call in `ChatViewModel::send()` that sets the fallback title is updated to explicitly pass `TitleSource::Fallback` as the third argument (not relying on the default).

- [x] T-022: Update ConversationListModel to track title_source
  - REQs: REQ-NF-003, REQ-F-006
  - Check: `ConversationListModel` stores and updates `title_source` for each conversation received via `conversationRenamed` signal, allowing QML to bind to it if needed (e.g., for future "Regenerate Title" button).

- [x] T-023: Test title_source persistence round-trip
  - REQs: REQ-F-005, REQ-F-010, REQ-C-003
  - Check: A test verifies that creating a new conversation sets `title_source = Fallback`, loading an existing conversation reads its `title_source` correctly, and after migration all pre-existing conversations have `title_source = Manual`.

- [x] T-024: Test UtilityConfig load/save edge cases
  - REQs: REQ-F-001
  - Check: Tests verify that `loadUtilityConfig()` returns a default (unset) config if `config.json` lacks a `"utility"` key, if the key is null, or if the value is malformed; `saveUtilityConfig()` correctly round-trips a set model and an unset model; both methods never throw.

- [x] T-025: Test UtilityTaskRunner model resolution chain
  - REQs: REQ-F-002, REQ-U-001
  - Check: Tests verify that an unset `default_utility_model` falls through to `chatFallbackModel`; a set `default_utility_model` is used if its provider_id is valid; an unknown `provider_id` in `default_utility_model` returns `std::nullopt` (skip generation).

- [x] T-026: Test title generation fires exactly once per conversation
  - REQs: REQ-F-003, REQ-F-007, REQ-NF-004, REQ-NF-005
  - Check: A test with a mocked provider verifies that two rapid `Complete` messages in the same conversation produce exactly one `titleGenerated()` signal, and that a second generation attempt for an already-generated conversation is a no-op.

- [x] T-027: Test failure handling preserves fallback title
  - REQs: REQ-F-007, REQ-F-008, REQ-U-001, REQ-U-002
  - Check: Tests verify that on empty accumulated response, provider error, missing credentials, or unknown provider_id, no `titleGenerated()` signal is emitted and the fallback title remains unchanged; failures are logged but not surfaced to the UI.

- [x] T-028: Test createConversation regression for Fallback binding
  - REQs: REQ-C-003
  - Check: A regression test verifies that a new conversation's `title_source` column is explicitly bound to `'Fallback'` in `createConversation()`'s INSERT and is not implicitly derived from the migration's DEFAULT value.
