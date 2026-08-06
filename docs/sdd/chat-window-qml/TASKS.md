# SDD Tasks — chat-window-qml

## Build Infrastructure & CMake (6 tasks)

- [x] T-001: Create cmake/combine-metatypes.cmake script
  - REQs: REQ-NF-002
  - Check: File exists at cmake/combine-metatypes.cmake, merges JSON metatype arrays from multiple input files (or single input) into one combined output file.

- [x] T-002: Create scripts/check-qmltypes.sh verification script
  - REQs: REQ-NF-003
  - Check: File exists at scripts/check-qmltypes.sh, is executable, reads HolonightChat.qmltypes and fails if file is empty, malformed, or missing ChatViewModel type entry.

- [x] T-003: Update src/application/CMakeLists.txt
  - REQs: REQ-NF-002
  - Check: holonight_application target lists chat_view_model.cpp and message_list_model.cpp as source files, and target_link_libraries includes Qt6::Qml.

- [x] T-004: Update apps/chat/CMakeLists.txt with metatype extraction and registration
  - REQs: REQ-NF-002, REQ-NF-003
  - Check: qt_add_qml_module call includes TYPEINFO HolonightChat.qmltypes and NO_GENERATE_QMLTYPES; qt6_extract_metatypes(holonight_application) and _qt_internal_qml_type_registration(holonight-chat) are invoked; build output includes HolonightChat.qmltypes file.

- [x] T-005: Add qmltypes-check task to Taskfile.yml
  - REQs: REQ-NF-003
  - Check: Taskfile.yml contains task qmltypes-check that depends on build and runs scripts/check-qmltypes.sh with BUILD_DIR parameter.

- [x] T-006: Add CI check to .github/workflows/ci.yml
  - REQs: REQ-NF-003
  - Check: CI workflow includes a step after the build step that runs scripts/check-qmltypes.sh build; CI job fails if check-qmltypes.sh exits non-zero.

## MessageListModel Class (2 tasks)

- [x] T-007: Create MessageListModel header
  - REQs: REQ-F-021, REQ-F-023
  - Check: File exists at src/application/include/holonight_application/message_list_model.h with QAbstractListModel subclass, Roles enum (IdRole, RoleRole, TextRole, StatusRole), QML_ELEMENT macro, and public method signatures for appendMessage(), updateLastMessage(), resetFrom().

- [x] T-008: Implement MessageListModel.cpp
  - REQs: REQ-F-008, REQ-F-021, REQ-F-023
  - Check: message_list_model.cpp compiles without errors; appendMessage() calls beginInsertRows/endInsertRows and adds exactly one row; updateLastMessage() emits dataChanged for last row only (never changes rowCount); toRow() converts Message domain type to Row struct with role/text/status as strings.

## ChatViewModel Class (4 tasks)

- [x] T-009: Create ChatViewModel header
  - REQs: REQ-F-001, REQ-C-002, REQ-NF-001
  - Check: File exists at src/application/include/holonight_application/chat_view_model.h with QML_ELEMENT, QML_SINGLETON macros; Q_PROPERTY declarations for availableModels, selectedModelId, canSend, canRegenerate, isStreaming, errorMessage, inputText, messages; Q_INVOKABLE send(QString), stop(), regenerate(); public conversation() method; static create(QQmlEngine*, QJSEngine*) factory.

- [x] T-010: Implement ChatViewModel constructor and initialization flow
  - REQs: REQ-F-001, REQ-F-002, REQ-F-003, REQ-F-004, REQ-F-019, REQ-C-001
  - Check: ChatViewModel constructor creates std::shared_ptr<Conversation> with generated ConversationId, constructs ChatController with OllamaProvider, creates MessageListModel as child QObject, calls provider_->refresh() with callback; onModelsRefreshed() updates availableModels property (emits signal), auto-selects first model if list non-empty, sets errorMessage if list is empty.

- [x] T-011: Implement ChatViewModel send and streaming flow
  - REQs: REQ-F-005, REQ-F-006, REQ-F-007, REQ-F-008, REQ-F-009, REQ-F-010, REQ-C-002
  - Check: send(text) invokable guards on canSend() (early return if false), builds QPointer-guarded lambda callback, calls ChatController::send() with conversation/selectedModelId/text; mirrors User and Assistant messages from conversation into message_model via appendMessage(); clears inputText property; refreshComputedProperties() flips isStreaming true; onStreamEvent callback updates last message via message_model_->updateLastMessage() and emits property signals; ContentDelta accumulates text, Completed/Error/Cancelled transition message status and refreshComputedProperties.

- [x] T-012: Implement ChatViewModel stop and regenerate flows
  - REQs: REQ-F-011, REQ-F-012, REQ-F-013, REQ-F-014, REQ-F-015, REQ-F-016
  - Check: stop() invokable calls ChatController::stop(conversationId), is idempotent (safe to call twice), calls refreshComputedProperties to flip isStreaming false; regenerate() invokable guards on canRegenerate(), calls ChatController::regenerate() if guard passes (no-op if false), updates last message in place via updateLastMessage (not appendMessage), no-op when last message is not eligible Assistant.

## ChatApplication Shutdown Wiring (2 tasks)

- [x] T-013: Wire QQuickView::closing signal in ChatApplication.cpp
  - REQs: REQ-F-024, REQ-C-001
  - Check: ChatApplication constructor connects view_->closing signal to a lambda that fetches the ChatViewModel singleton instance from the QQmlEngine via singletonInstance<ChatViewModel*>("HolonightChat", "ChatViewModel") and calls stop() on it.

- [x] T-014: Implement ChatViewModel destructor
  - REQs: REQ-F-024, REQ-C-001
  - Check: ChatViewModel destructor is defined, calls stop() synchronously at the start of the destructor body, runs before any member (chat_controller_, conversation_, message_model_) is destroyed.

## GTest Unit Tests (4 tasks)

- [x] T-015: Test initialization and model population flow
  - REQs: REQ-F-001, REQ-F-002, REQ-F-004, REQ-NF-005
  - Check: test_chat_view_model.cpp exists and contains test cases verifying: ChatViewModel construction with FakeHttpClient yields non-null stable Conversation pointer, ConversationId is non-empty, OllamaProvider::refresh() is called exactly once, availableModels property populates and emits signal once, selectedModelId auto-selects first model when list populates.

- [x] T-016: Test send flow and streaming state management
  - REQs: REQ-F-003, REQ-F-005, REQ-F-006, REQ-F-007, REQ-F-008, REQ-F-009, REQ-NF-005
  - Check: test cases verify: empty model list makes canSend false; send("text") appends exactly two messages (User and Assistant) to model, increases rowCount by 2, clears inputText, sets isStreaming true, guards against send when canSend is false; ContentDelta callbacks update last message in place via updateLastMessage (rowCount unchanged), accumulate text across multiple deltas, emit property change signals; Completed transitions status to "complete" and isStreaming to false.

- [x] T-017: Test error handling, stop, and regenerate
  - REQs: REQ-F-010, REQ-F-011, REQ-F-013, REQ-F-014, REQ-F-015, REQ-F-016, REQ-NF-005
  - Check: test cases verify: StreamEvent::Error sets errorMessage property and transitions message status to "error"; stop() is idempotent (call twice, no crash), transitions in-flight message status to "cancelled", flips isStreaming false; canRegenerate returns true only when last message is Assistant with Complete or Error status; regenerate() guards on canRegenerate, calls ChatController only if guard passes (no-op if false), updates model in place not appended; regenerate() called on empty or user-message list is no-op.

- [x] T-018: Test shutdown safety with AddressSanitizer
  - REQs: REQ-F-024, REQ-C-001, REQ-NF-005
  - Check: test constructs ChatViewModel with FakeHttpClient, calls send(), then destroys the fixture mid-stream (or explicitly calls stop() then goes out of scope); test passes under ASan/Valgrind build with zero "heap-use-after-free" or "double-free" errors reported.

## QML Implementation (5 tasks)

- [x] T-019: Implement message list ListView and delegate
  - REQs: REQ-F-008, REQ-F-021, REQ-F-022, REQ-F-023
  - Check: WorkspaceWindow.qml contains ListView bound to ChatViewModel.messages, delegate renders each message with role (left-align for Assistant, right-align for User), text (plain text, no Markdown interpretation), and status indicator ("Streaming...", "Error", "Cancelled", "Complete"); messages display in chronological order (oldest first); status indicator updates on message status change without requiring manual refresh.

- [x] T-020: Implement input text box and send button
  - REQs: REQ-F-005, REQ-F-006, REQ-F-007, REQ-F-026, REQ-F-027
  - Check: WorkspaceWindow.qml contains TextEdit or TextField bound to ChatViewModel.inputText; pressing Enter (or Ctrl+Enter if multi-line) triggers ChatViewModel.send(ChatViewModel.inputText); Shift+Enter inserts newline; clicking Send button calls send(); button labeled "Send", enabled binding uses ChatViewModel.canSend, disabled while streaming; after send, input field is visually cleared within one event loop.

- [x] T-021: Implement model picker ComboBox
  - REQs: REQ-F-003, REQ-F-004, REQ-F-017, REQ-F-018
  - Check: WorkspaceWindow.qml contains ComboBox bound to ChatViewModel.availableModels; each delegate displays model as "{provider_id}/{model_name}"; currentIndex reflects and updates ChatViewModel.selectedModelId; placeholder text "No models available — check that Ollama is running and has at least one model pulled" appears when list is empty; picker disabled when no models available.

- [x] T-022: Implement stop and regenerate buttons
  - REQs: REQ-F-012, REQ-F-013, REQ-F-014, REQ-F-015
  - Check: WorkspaceWindow.qml contains stop button visible (or enabled) when ChatViewModel.isStreaming is true, clicking calls ChatViewModel.stop(); regenerate button enabled when ChatViewModel.canRegenerate is true, clicking calls ChatViewModel.regenerate(); both buttons disabled/hidden appropriately when conditions are not met.

- [x] T-023: Implement inline error display banner
  - REQs: REQ-F-010, REQ-F-019, REQ-F-020, REQ-NF-004
  - Check: WorkspaceWindow.qml displays inline error banner (Text item or similar, not modal dialog) when ChatViewModel.errorMessage is non-empty; banner shows error text and is non-blocking (input field, buttons remain usable); banner is dismissible (via close button or auto-clears on new message send); no modal dialogs (Dialog/MessageDialog) shown for errors or startup model-load failures.

## Verification Tasks (5 tasks)

- [x] T-024: Run qmllint and verify QML type metadata
  - REQs: REQ-NF-001, REQ-NF-002
  - Check: task qmllint executes without "type unknown" or "undeclared" errors for ChatViewModel.send, ChatViewModel.messages, ChatViewModel.availableModels, and all property accesses in WorkspaceWindow.qml; qmllint recognizes ChatViewModel as a valid imported type.

- [x] T-025: Run clang-format on new C++ files
  - REQs: (code style)
  - Check: task format-check reports no formatting violations in chat_view_model.h/cpp and message_list_model.h/cpp; task format auto-formats all new files and build succeeds without errors.

- [x] T-026: Run clang-tidy on new C++ files
  - REQs: (code quality)
  - Check: task tidy runs on holonight_application target with -WarningsAsErrors=* and reports zero errors or warnings in chat_view_model.cpp and message_list_model.cpp; build/tidy.log shows "passed" for holonight_application.

- [x] T-027: Run GTest suite with coverage
  - REQs: REQ-NF-005
  - Check: task configure-tests and task test execute, test_holonight_ai binary builds, ctest runs test_chat_view_model.* tests (8+ test cases), all tests PASS, zero real network calls or Ollama dependency.

- [x] T-028: Execute manual smoke test protocol against local Ollama
  - REQs: REQ-NF-006, REQ-F-024, REQ-F-025
  - Check: Execute all 9 steps: (1) Start local Ollama with ≥1 model pulled, (2) task run holonight-chat, (3) Verify model picker displays model list, (4) Send "Hello", observe streamed response render token-by-token in message list, (5) Click stop mid-stream, verify message status shows Cancelled, (6) Click regenerate, observe new stream begin, (7) Change model selection in picker, send message with different model, (8) Stop Ollama service, send message, verify inline error displays (no modal), (9) Close window mid-stream, window closes within 1 second with exit code 0 and no crash; all 9 steps complete successfully.

---

**Total Tasks**: 28
**Section Summary**:
- Build infrastructure: 6 tasks
- C++ classes (MessageListModel + ChatViewModel): 6 tasks
- Shutdown wiring: 2 tasks
- GTest coverage: 4 tasks
- QML implementation: 5 tasks
- Verification: 5 tasks
