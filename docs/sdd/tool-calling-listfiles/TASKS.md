# SDD Tasks — tool-calling-listfiles

- [x] T-001: Amend Message and StreamEvent domain types to support tool calls
  - REQs: REQ-F-005 (amended), REQ-F-020 (amended)
  - Check: `Message` class has `toolCalls()` getter and `setToolCalls()` setter; `StreamEvent` variant includes `ToolCall{id, name, input}`; new `ToolCallEntry` struct with `kind`, `tool_use_id`, `tool_name`, `input`, `result`, `is_error` fields exists in `src/domain/include/holonight_domain/tool_call.h`.

- [x] T-002: Implement ToolRegistry and ITool interface
  - REQs: REQ-F-001, REQ-F-002, REQ-NF-003
  - Check: `ITool` interface in `src/application/include/holonight_application/tools/i_tool.h` has `name()`, `description()`, `schema()`, `execute()` methods; `ToolRegistry` class in `src/application/include/holonight_application/tools/tool_registry.h` has `registerTool()`, `tools()`, `find()`, `invoke()`, `toAnthropicToolsArray()` methods; `kMaxToolCallsPerTurn = 10` constant defined in tool_registry.h; unit tests for register/lookup/invoke pass.

- [x] T-003: Implement ListFilesTool
  - REQs: REQ-F-002, REQ-F-003, REQ-F-004, REQ-F-005, REQ-NF-001
  - Check: `ListFilesTool` in `src/application/src/tools/list_files_tool.cpp` compiles; listing `~/Documents` returns `{"entries":[{"name":"...", "type":"file"|"directory"}, ...]}` with dotfiles excluded and entries sorted alphabetically; paths outside `$HOME` return `{"error":{"code":"NOT_FOUND",...}}`; all four error codes (`NOT_FOUND`, `NOT_A_DIRECTORY`, `PERMISSION_DENIED`, `INVALID_PARAMETERS`) return structured JSON; no exceptions escape `execute()`.

- [x] T-004: Create persistence migration and register it
  - REQs: REQ-F-008
  - Check: File `src/persistence/migrations/0008_add_message_tool_calls.sql` exists with `ALTER TABLE messages ADD COLUMN tool_calls TEXT`; `MigrationRunner::builtInMigrations()` in `src/persistence/src/migration_runner.cpp` includes version 8 entry; test that opens fresh database verifies `MigrationRunner::currentVersion()` reaches 8.

- [x] T-005: Implement persistence encode/decode for tool_calls in ConversationRepositoryWorker
  - REQs: REQ-F-008
  - Check: `messageFromRecord()`, `persistNewMessage()`, and `materializeConversation()` in `src/persistence/src/detail/conversation_repository_worker.cpp` handle `tool_calls` column via `encodeToolCalls()` and `decodeToolCalls()` helpers; roundtrip test stores and retrieves a `Message` with `ToolCallEntry`, verifies equality.

- [x] T-006: Add tool_calling_enabled to AnthropicProviderConfig and config_repository
  - REQs: REQ-F-009, REQ-F-010
  - Check: `AnthropicProviderConfig` in `src/config/include/holonight_config/provider_config.h` has `bool tool_calling_enabled = false` field; `src/config/src/config_repository.cpp` serializes/deserializes `tool_calling_enabled` with fallback default `false` for missing keys; test loads existing config without key, verifies field is `false`.

- [x] T-007: Implement Anthropic provider tool-calling wiring (SSE parsing, history reconstruction, signature)
  - REQs: REQ-F-008, REQ-NF-001
  - Check: `AnthropicProvider::sendChat()` signature accepts `const QJsonArray& tools = {}`; SSE parser in `routeSseEvent()` emits `StreamEvent::ToolCall` for `content_block_start`/`content_block_delta`/`content_block_stop` sequences; `message_stop` guard includes `!context->has_tool_call` condition; history reconstruction in `sendChat()` builds `tool_use` and `tool_result` content blocks from `Message::toolCalls()` with same-role grouping; mocked SSE roundtrip test passes.

- [x] T-008: Wire ToolRegistry into ProviderAdapterRouter
  - REQs: REQ-F-009
  - Check: `ProviderAdapterRouter` constructor accepts and stores `std::shared_ptr<ToolRegistry>`; `sendChat()` checks if provider is Anthropic and `tool_calling_enabled` is true on its config; passes `tool_registry_->toAnthropicToolsArray()` to `AnthropicProvider::sendChat()` when enabled, empty array when disabled; unit test with mock Anthropic config variations passes.

- [x] T-009: Implement ChatController tool-calling orchestration loop
  - REQs: REQ-F-001, REQ-F-006, REQ-F-007, REQ-F-011, REQ-NF-002
  - Check: `ChatController` constructor accepts `std::shared_ptr<ToolRegistry>`; `handleStreamEvent()` has `ToolCall` visitor branch that increments `tool_calls_this_turn` counter, rejects 11th+ calls with "Tool-calling limit exceeded" error, executes tool via registry without branching on tool name, creates Invocation and Result `Message` entries; `continueToolLoop()` re-dispatches while keeping counter active; test executes 11+ tool calls, verifies 11th is rejected and error is displayed.

- [x] T-010: Add toolCallingEnabled property to AnthropicProviderSettingsController and QML panel
  - REQs: REQ-F-009
  - Check: `AnthropicProviderSettingsController` has `Q_PROPERTY(bool toolCallingEnabled ...)` with read/write; `setToolCallingEnabled()` updates draft config and emits signal; Anthropic settings QML panel includes Switch/CheckBox control labeled "Enable Tool Calling" bound to `toolCallingEnabled`; setting toggles and persists across restart.

- [x] T-011: Implement QML rendering for tool-call and tool-result messages
  - REQs: REQ-F-007, REQ-F-011
  - Check: `MessageListModel` exposes tool_call data via custom model role; `ToolCallCard.qml` component renders Invocation entries (tool name + input parameters) and Result entries (success listing or error) distinctly from text messages; tool-call messages appear in transcript in chronological order, not folded into `MessageBubble`; manual verification: send message triggering ListFiles, verify cards render.

- [x] T-012: Full project verification — build, test, QML lint, smoke test
  - REQs: All (REQ-F-001 through REQ-F-011, REQ-NF-001 through REQ-NF-003, REQ-C-001 through REQ-C-006)
  - Check: `task configure-tests && task build && task test` completes with zero test failures; `task qml-lint` passes; each acceptance check from T-001 through T-011 still holds; manual smoke test in running app: toggle tool-calling on, send message, trigger ListFiles, verify entry appears in conversation.
