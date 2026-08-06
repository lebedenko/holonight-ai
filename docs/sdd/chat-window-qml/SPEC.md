# Chat Window QML Integration — Requirements Specification

## Overview

This specification defines the first usable chat window UI for holonight-chat, wiring the existing C++ backend (ChatController, OllamaProvider, Conversation, Message domain types) into Qt6/QML (WorkspaceWindow.qml), creating a functional chat experience: message list, text input, model picker, send/stop/retry controls, and inline error handling.

**Scope**: one ephemeral, in-memory Conversation created at app start, destroyed at app exit. No persistence, no multi-conversation switcher, no Markdown/rich-text rendering. Ollama backend only.

**Responsibility boundaries**:
- Backend (not touched): ChatController, OllamaProvider, domain types (holonight_domain), HttpClient
- QML: WorkspaceWindow.qml, model picker, message list, error display, input controls
- New C++ bridge: ChatViewModel (holonight_application) — QML-registered singleton, owns Conversation, ChatController, OllamaProvider; exposes properties, signals, invokables for QML binding

---

## Functional Requirements

### Initialization & Conversation Setup

**REQ-F-001: Ephemeral Conversation Creation**
> When the application window initializes, the system shall create exactly one ephemeral Conversation object in memory, seeded with a generated ConversationId, and retain it for the lifetime of the application window.

- **Acceptance Criterion**: A `ChatViewModel::conversation() const` method returns a non-null `Conversation*` immediately after window load; the returned `Conversation` is the same object on every call during the window's lifetime; the `ConversationId` is stable and non-empty (verified via `Conversation::id().toString()` returning a non-empty string).

**REQ-F-002: Model List Population at Startup**
> When ChatViewModel initializes, the system shall call `OllamaProvider::refresh()` exactly once to fetch the available model list from Ollama, and fire an `onModelsChanged` signal when the list updates.

- **Acceptance Criterion**: `refresh()` is called once during `ChatViewModel` construction or immediately after QML engine binds the singleton; `ChatViewModel::availableModels` (a QList<ModelId>) changes once and triggers a Qt property-change notification/signal; the property is readable in QML as `ChatViewModel.availableModels` within one frame after init completes.

**REQ-F-003: Empty Model List State**
> If the model list is empty after refresh (Ollama unreachable or no models pulled — `OllamaProvider::refresh()` does not distinguish the two causes, and this cycle does not extend it to; see REQ-F-019, which now uses identical wording for the same reason), the system shall display a clear "No models available — check that Ollama is running and has at least one model pulled" message in the model picker UI, and disable the send button.

- **Acceptance Criterion**: When `ChatViewModel::availableModels` is empty, `ChatViewModel::canSend` property is false; QML model picker shows the placeholder text "No models available — check that Ollama is running and has at least one model pulled"; send button's `enabled` property is bound to `ChatViewModel::canSend` and reflects false.

**REQ-F-004: Model Selection Default**
> When the model list first populates (or populates after being empty), the system shall automatically select the first available model as the current selection.

- **Acceptance Criterion**: `ChatViewModel::selectedModelId` property is non-null immediately after models list populates with at least one entry; QML model picker's `currentIndex` or equivalent read-only binding reflects index 0; no user action is required to make a model selectable.

### Message Sending

**REQ-F-005: User Message Submission**
> When the user enters text into the input box and presses Send (or equivalent), the system shall call `ChatController::send(conversation, selectedModelId, userText, streamEventCallback)`, appending a User message and a placeholder Assistant message to the conversation.

- **Acceptance Criterion**: `ChatViewModel::send(QString text)` invokable exists, is callable from QML, and internally calls `ChatController::send()` with the active conversation, current selectedModelId, and the text; User and Assistant messages are appended to `Conversation::messages()` synchronously on return (verified by message count increasing); `ChatController::isStreaming()` returns true immediately after `send()` returns.

**REQ-F-006: Input Box Clearance on Send**
> When a message is successfully submitted, the system shall clear the input text box.

- **Acceptance Criterion**: After calling `ChatViewModel::send()`, the `ChatViewModel::inputText` property (bound to QML's TextEdit/TextField) is set to empty string; QML input field is visually empty within one event loop iteration.

**REQ-F-007: Send Button Disabled During Streaming or Model Unselected**
> While `ChatController::isStreaming()` is true, or if no model is selected, the system shall disable (gray out) the send button and prevent text submission.

- **Acceptance Criterion**: `ChatViewModel::canSend` property returns false when `isStreaming` is true or `availableModels` is empty; QML send button's `enabled` property is bound to `ChatViewModel::canSend`; pressing Send key in input field has no effect when `canSend` is false (verified by no new message in conversation list).

### Streaming & Live Response Display

**REQ-F-008: Token-by-Token Response Display**
> As StreamEvents arrive from ChatController, the system shall append each ContentDelta text chunk to the last Assistant message's text, updating the message list live.

- **Acceptance Criterion**: On each `StreamEvent::ContentDelta{QString text}`, `Message::setText()` or equivalent updates the last message's text; QML message list item bound to `Conversation::messages()` re-renders (property change triggers QML binding update); user sees new text appear incrementally within one frame of each delta arriving.

**REQ-F-009: Streaming Completion Signal**
> When a stream completes (StreamEvent::Completed fires), the system shall transition the last Assistant message's status to Complete.

- **Acceptance Criterion**: After final `StreamEvent::Completed`, `Conversation::messages().back().status()` returns `MessageStatus::Complete`; `ChatViewModel::isStreaming` returns false; QML re-render reflects the status change (e.g., stop button disappears).

**REQ-F-010: Stream Error Handling**
> When a StreamEvent::Error arrives, the system shall transition the last Assistant message's status to Error, set its text to the error message, and display an inline error banner or message in the UI (not a modal dialog).

- **Acceptance Criterion**: On `StreamEvent::Error{QString message}`, the assistant message's status is transitioned to `Error`; `ChatViewModel::errorMessage` property is set to the error text; QML error display binding shows the message without blocking interaction; send/input controls remain usable; after dismissing the error display, the user can retry or send new messages.

**REQ-F-011: Stream Cancellation**
> When a stream is cancelled (ChatController::stop() is called), the system shall transition the last Assistant message's status to Cancelled and stop accepting new deltas.

- **Acceptance Criterion**: After `stop()` is called, `ChatController::isStreaming()` returns false; `Conversation::messages().back().status()` transitions to `Cancelled`; no further `StreamEvent` callbacks fire for that stream; `ChatViewModel::isStreaming` reflects false within one event loop iteration.

### Stop Button

**REQ-F-012: Stop Button Visibility and Enablement**
> While ChatController::isStreaming() is true for the active conversation, the system shall show and enable the stop button; otherwise, it shall hide or disable it.

- **Acceptance Criterion**: `ChatViewModel::isStreaming` property reflects `ChatController::isStreaming(conversationId)` and triggers a Qt signal on change; QML stop button's `visible` property is bound to `ChatViewModel::isStreaming` and updates within one frame; stop button is hidden/disabled when no stream is in flight.

**REQ-F-013: Stop Button Action**
> When the user clicks the stop button, the system shall call `ChatController::stop(conversationId)` for the active conversation.

- **Acceptance Criterion**: QML stop button's `onClicked` handler calls `ChatViewModel::stop()` invokable; `stop()` internally calls `ChatController::stop(conversationId)`; the call is idempotent and does not raise an error if no stream is active.

### Retry/Regenerate

**REQ-F-014: Regenerate Button Enablement Rule**
> The regenerate (retry) button shall be enabled if and only if the last message in the conversation is an Assistant message with status Complete or Error.

- **Acceptance Criterion**: `ChatViewModel::canRegenerate` boolean property returns true only when `Conversation::messages().back().role() == MessageRole::Assistant && (status == Complete || status == Error)`; returns false if last message is User, Pending, Streaming, or Cancelled; QML retry button's `enabled` property is bound to this.

**REQ-F-015: Regenerate Action**
> When the user clicks the regenerate button, the system shall call `ChatController::regenerate(conversation, selectedModelId, streamEventCallback)`, re-running the last assistant response in place.

- **Acceptance Criterion**: QML retry button's `onClicked` calls `ChatViewModel::regenerate()` invokable; `regenerate()` calls `ChatController::regenerate()` with the active conversation and current model; `isStreaming` becomes true; the last message's text is cleared or marked as Streaming; new stream events append to the cleared text.

**REQ-F-016: Regenerate Rejection Handling**
> If regenerate() is called when the last message is not an eligible Assistant message, the system shall reject the call silently (no-op, no error modal) and leave the conversation unchanged.

- **Acceptance Criterion**: Calling `ChatViewModel::regenerate()` when `canRegenerate` is false does not crash, does not append messages, does not invoke ChatController; `isStreaming` remains false; UI remains responsive.

### Model Picker

**REQ-F-017: Model Picker Population**
> The model picker (dropdown/combobox) shall display all models from ChatViewModel::availableModels, with each model's display label as "{provider_id}/{model_name}".

- **Acceptance Criterion**: QML model picker's model binding is `ChatViewModel.availableModels`; each delegate renders `model.provider_id + "/" + model.model_name`; if list has N items, picker shows N options; selecting an option updates `ChatViewModel::selectedModelId`.

**REQ-F-018: Model Selection Persistence During Chat**
> Once a model is selected, the system shall use that model for all subsequent send/regenerate calls in the current session, until the user changes the selection.

- **Acceptance Criterion**: `ChatViewModel::selectedModelId` is readable/writable (via QML property binding); `send()` and `regenerate()` invokables use the current `selectedModelId` at the time of the call; changing the picker selection updates `selectedModelId` before the next send.

### Error Display

**REQ-F-019: Model Refresh Failure (Startup)**
> If OllamaProvider::refresh() settles with an empty model list at startup — network failure and "zero models pulled" are indistinguishable at this API boundary, and this cycle does not extend `OllamaProvider` to tell them apart — the system shall display the same inline message "No models available — check that Ollama is running and has at least one model pulled" without blocking the window (this is the same message and the same empty-list condition as REQ-F-003; the two requirements describe two UI surfaces — model-picker placeholder and inline error banner — that key off one signal).

- **Acceptance Criterion**: When `refresh()` completes with an empty model list, `ChatViewModel::errorMessage` is set to the message above; QML shows the error text in an inline banner (not a modal); send button is disabled; user can retry by restarting the app or (if a manual refresh action is added in a later cycle) triggering it; window remains interactive.

**REQ-F-020: Stream Error Display**
> When a StreamEvent::Error arrives during message streaming, the system shall display the error text inline in the message list (as part of or adjacent to the assistant message) and allow the user to retry the message without dismissing a dialog.

- **Acceptance Criterion**: `ChatViewModel::errorMessage` is updated; QML message list or error banner displays the text; retry button is enabled (per REQ-F-014); user can click retry or send a new message without closing any modal; error display remains visible until a new message is sent or the conversation is cleared.

### Message List & Rendering

**REQ-F-021: Message List Display**
> The system shall display all messages in Conversation::messages() in chronological order (oldest first, newest last), showing message role (User vs. Assistant), text, and status.

- **Acceptance Criterion**: QML ListView or Column binding `ChatViewModel.conversation().messages()` displays each message; User messages render with one visual style (e.g., right-aligned, blue background), Assistant messages with another (e.g., left-aligned, gray background); messages appear in order; new messages added to the conversation appear immediately in the list.

**REQ-F-022: Plain Text Rendering**
> The system shall render message text as plain text only, without Markdown interpretation, code highlighting, or rich-text formatting.

- **Acceptance Criterion**: If a message contains `**bold**` or `` `code` `` or `[link](url)`, it renders as literal text, not formatted; QML text item uses `Text.PlainText` or equivalent; no HTML/Markdown parser is invoked.

**REQ-F-023: Message Status Display**
> The system shall visually indicate the status of each message (Pending, Streaming, Complete, Error, Cancelled) via styling or an indicator label.

- **Acceptance Criterion**: Each message's delegate shows or hides a "Streaming..." spinner / "(Error)" label / "(Cancelled)" label based on `Message::status()`; status changes trigger re-render without requiring user interaction.

### Window Lifecycle & Shutdown Safety

**REQ-F-024: Shutdown Safety — Stop Before Destruction**
> Before the application window closes, the system shall call `ChatController::stop()` for the active conversation's ID, regardless of whether a stream is in flight, to safely destroy the Conversation without risking a dangling-pointer dereference in any pending callbacks.

- **Acceptance Criterion**: `holonight-chat` main window's `onDestroying` or `closeEvent()` handler (in C++ or QML) calls `ChatViewModel::stop()` or equivalent; `stop()` internally calls `ChatController::stop(conversationId)` synchronously; the call is idempotent (safe to call even if no stream is active); the Conversation is not destroyed until after `stop()` completes; verified by running `task run`, sending a message, closing the window mid-stream, and confirming no crash or memory error in valgrind/asan.

**REQ-F-025: Graceful Window Close**
> After ChatController::stop() is called, the window shall close cleanly without errors, memory leaks, or hangs.

- **Acceptance Criterion**: Closing `holonight-chat` mid-stream does not hang (window closes within 1 second); no crash or stack trace; no valgrind errors on a clean build; process exits with code 0.

### Input Controls

**REQ-F-026: Text Input Box**
> The system shall provide a text input box (QML TextEdit or TextField) where the user types a message, bound to `ChatViewModel::inputText`, and can submit via pressing Enter (or Ctrl+Enter on multi-line) or clicking a Send button.

- **Acceptance Criterion**: QML has a TextEdit/TextField with `text: ChatViewModel.inputText`; typing updates the property; Shift+Enter inserts a newline; Enter (or Ctrl+Enter if configured) triggers `ChatViewModel::send(ChatViewModel.inputText)` and clears the input; Send button's `onClicked` calls the same function.

**REQ-F-027: Send Button Labeling**
> The send button shall be labeled "Send" in idle state and may show a loading indicator while `isStreaming` is true (optional, but state must be visually clear).

- **Acceptance Criterion**: Button text is "Send"; while `ChatViewModel::isStreaming` is true, button is disabled (grayed out) per REQ-F-007; after a message is sent, the button's visual state reflects disabled status immediately.

---

## Non-Functional Requirements

### Build Infrastructure & CMake

**REQ-NF-001: QML Type Registration for ChatViewModel**
> ChatViewModel shall be registered as a QML type with both `Q_OBJECT` macros, `QML_ELEMENT`, and `QML_SINGLETON` annotations (matching holonight-shell's convention for service/viewmodel singletons).

- **Acceptance Criterion**: `ChatViewModel` header has `Q_OBJECT`, `QML_ELEMENT`, `QML_SINGLETON` macros; generated QML type is automatically imported as `import HolonightChat` and accessed in QML as `ChatViewModel.method()` / `ChatViewModel.property`; `qmllint` recognizes the type without errors.

**REQ-NF-002: Metatype Combining for Static Libraries**
> Because ChatViewModel lives in a static library (holonight_application) rather than the executable target, the build must extract and merge metatypes from all linked static libraries into the executable's metatype registry, following the holonight-shell pattern.

- **Acceptance Criterion**: `CMakeLists.txt` for `holonight-chat` executable or `holonight_application` includes `qt6_extract_metatypes()` calls; a CMake script (e.g., `cmake/combine-metatypes.cmake`) merges per-library metatype JSON files; `_qt_internal_assign_build_metatypes_files_and_properties()` and `_qt_internal_qml_type_registration()` are invoked; the generated `build/apps/chat/HolonightChat.qmltypes` file is non-empty (not just `Module {}`) and includes `ChatViewModel` type definition; verified by `qmllint` recognizing `ChatViewModel` properties without "type unknown" warnings.

**REQ-NF-003: QML Type Verification Task**
> The build/CI must include a verification step that checks the generated `.qmltypes` file is not empty and not just `Module {}`, ensuring QML tooling will recognize registered types.

- **Acceptance Criterion**: A `task check-qmltypes` or equivalent (or `task build` includes it) runs a script checking `build/apps/chat/HolonightChat.qmltypes` for non-empty content and `ChatViewModel` type entry; CI job includes this check; if the file is invalid, the build fails with a clear error message.

### Error UX Pattern

**REQ-NF-004: Non-Blocking Error Display**
> All errors (startup model-refresh failure, mid-stream error, etc.) shall be displayed as inline banners, badges, or message-list insertions, never as modal dialogs that block interaction.

- **Acceptance Criterion**: Running against an unreachable Ollama instance displays "Failed to fetch models" in an inline banner; the window and input controls remain fully usable; sending a message when Ollama is unreachable displays an inline error in the message list or banner, not a modal; verified manually by running without Ollama and confirming no dialog box appears.

### Testing & Validation

**REQ-NF-005: GTest Coverage for ChatViewModel Bridging**
> ChatViewModel's bridging logic (message list updates, streaming state changes, error propagation, model list updates) must have GTest coverage using the existing fake HttpClient test double, with no real network sockets or running Ollama required.

- **Acceptance Criterion**: `tests/application/test_chat_view_model.cpp` exists; at least 8 test cases cover: (1) initialization and conversation creation, (2) model list population, (3) send and message append, (4) streaming state tracking, (5) error handling, (6) stop button, (7) regenerate enablement/action, (8) shutdown safety; all tests pass with `task test`; zero real network calls (verified by mocking HttpClient); test builds and runs without Ollama running.

**REQ-NF-006: Manual Smoke Test Protocol**
> Before marking this cycle complete, a manual smoke test against a real local Ollama instance must verify end-to-end functionality.

- **Acceptance Criterion**: Protocol: (1) start local Ollama with at least one model pulled, (2) `task run` holonight-chat, (3) model picker shows model list, (4) send a message ("Hello"), see streamed response render live token-by-token, (5) press stop mid-stream, see message status become Cancelled, (6) retry the response, see new stream begin, (7) change model selection, send again with new model, (8) stop Ollama, send a message, see inline error "Failed to fetch models" or equivalent, (9) close window mid-stream with no crash; all 9 steps pass; verified by manual tester or recorded video.

### Plain Text Rendering

**REQ-NF-007: Text Rendering — No Markdown or Rich Text**
> Message rendering shall not interpret Markdown, HTML, or rich-text markup; text shall be displayed as-is with word wrapping only.

- **Acceptance Criterion**: QML Text item or equivalent uses `textFormat: Text.PlainText` or `TextEdit.readOnly: true` with `selectByMouse: true`; a test message containing `**bold**`, `[link](url)`, or `` `code` `` renders as literal text; no Markdown libraries are linked or invoked.

---

## Constraints

### Hard Lifecycle Requirement

**REQ-C-001: ChatController Dangling-Pointer Safety**
> ChatController holds a raw, non-owning `Conversation*` pointer during in-flight streams. The system MUST call `ChatController::stop()` and ensure all callbacks have settled before destroying the Conversation object, or a dangling-pointer dereference will occur on the next callback.

- **Binding**: This is a hard requirement established by ChatController's design (see `holonight_application/chat_controller.h`). `ChatViewModel` owns the Conversation and must enforce this pattern:
  - `ChatViewModel` must hold a `shared_ptr<Conversation>` and keep it alive for the entire window lifetime.
  - Before destroying the Conversation (at window close or ChatViewModel destruction), `ChatViewModel::stop()` must synchronously call `ChatController::stop(conversationId)`.
  - The app's main window close handler must call this stop before any cleanup proceeds.
  - Verified by running under AddressSanitizer/Valgrind and closing the window mid-stream; if this is violated, ASAN will report "heap-use-after-free" on the stream callback.

### Callback-to-Signal Bridging

**REQ-C-002: Callback Bridge Pattern**
> ChatController uses `std::function<void(const StreamEvent&)>` callbacks for streaming events, not Qt signals. ChatViewModel must bridge this into Qt properties and signals so QML can bind and react to state changes.

- **Binding**: This is a first-time bridging requirement for this codebase (all other async communication has been GTest-tested with callback fakes, but this is the first QML/UI feature). Implementation approach:
  - ChatViewModel's stream callback captures `this` and uses Qt property setters + `emit` calls to notify QML.
  - `ChatViewModel::isStreaming`, `errorMessage`, and `availableModels` are Qt `Q_PROPERTY` with getter, setter, and `changed()` signal.
  - `Conversation::messages()` is wrapped as a Qt property so QML ListView can bind to it.
  - Verified by: (1) GTest calling fake callback functions and confirming property signals fire, (2) manual QML binding test confirming text updates appear live.

---

## Non-Goals (Explicitly Out of Scope)

The following features/concerns are intentionally deferred to later cycles and MUST NOT be implemented in this cycle:

- **Conversation Persistence**: No database, no loading/saving conversations across app restarts; the Conversation is ephemeral and destroyed on window close.
- **Multi-Conversation UI**: No conversation list, switcher, sidebar, or ability to have multiple simultaneous conversations; exactly one conversation per session.
- **Conversation Management**: No editing/deleting individual messages, no clearing conversation history, no exporting.
- **Markdown & Rich-Text Rendering**: No Markdown parser, no syntax highlighting, no code block rendering, no formatted links or emphasis.
- **Manual Model List Refresh**: No "refresh models" button or UI action; refresh happens once at startup only.
- **Model Filtering/Search**: Model picker is a simple list; no search/filter box.
- **Provider Plugins Other Than Ollama**: No OpenAI, Anthropic, Google, or other provider support this cycle; Ollama only.
- **Attachments/File Uploads**: No image, file, or document upload support.
- **Editing Previous User Messages**: No edit-and-resend for user messages; retry regenerates the assistant response only.
- **System Prompt/Role Selection**: No user-facing system prompt customization or persona switching.
- **Credential Management / Authentication**: No credentials dialog, no key storage; Ollama is assumed to be running locally and unauthenticated.
- **`holonight_persistence` Module**: No persistence layer or migrations.
- **`holonight_credentials` Module**: No secret storage (libsecret/KWallet).
- **`holonight_platform` Module**: No D-Bus, notifications, or desktop integration.

---

## Acceptance Criteria Summary

| Requirement | Verification Method | Pass Criteria |
|-------------|---------------------|---------------|
| REQ-F-001 | Unit test: ChatViewModel construction | `Conversation*` stable and non-null |
| REQ-F-002 | Unit test: OllamaProvider mock | `refresh()` called once; models populated |
| REQ-F-003 | Manual + unit test: empty model list | Picker shows placeholder; send disabled |
| REQ-F-004 | Unit test: model population | First model auto-selected |
| REQ-F-005 | Unit test: ChatController mock | `send()` appends User + Assistant messages |
| REQ-F-006 | Unit test + QML binding | `inputText` cleared after send |
| REQ-F-007 | Unit test + QML binding | `canSend` reflects `isStreaming` |
| REQ-F-008 | Unit test: StreamEvent callback | Message text updates on each ContentDelta |
| REQ-F-009 | Unit test: StreamEvent::Completed | Status transitions to Complete |
| REQ-F-010 | Unit test: StreamEvent::Error | Error text displayed inline, not modal |
| REQ-F-011 | Unit test: stop() call | Status transitions to Cancelled |
| REQ-F-012 | QML binding test | Stop button `visible` = `isStreaming` |
| REQ-F-013 | Unit test: stop() invokable | `ChatController::stop()` called |
| REQ-F-014 | Unit test: enablement logic | `canRegenerate` true iff last=Assistant (Complete\|Error) |
| REQ-F-015 | Unit test: regenerate() call | Message re-runs via `ChatController::regenerate()` |
| REQ-F-016 | Unit test: rejection | No-op if ineligible; no error |
| REQ-F-017 | QML binding test | Model picker displays all models |
| REQ-F-018 | Unit test: model persistence | Selected model used in send/regenerate |
| REQ-F-019 | Manual: Ollama offline at startup | Inline error; no modal; send disabled |
| REQ-F-020 | Manual: error during stream | Error displayed inline; retry available |
| REQ-F-021 | QML list binding test | Messages render in chronological order |
| REQ-F-022 | Manual: markdown in message | Text renders literally, not formatted |
| REQ-F-023 | QML binding test | Status indicator updates on state change |
| REQ-F-024 | AddressSanitizer: close mid-stream | No heap-use-after-free; no crash |
| REQ-F-025 | Manual: close mid-stream | Window closes in <1s; exit code 0 |
| REQ-F-026 | Manual: type and submit | Enter submits; Shift+Enter newline |
| REQ-F-027 | Manual: button state | "Send" label; disabled while streaming |
| REQ-NF-001 | qmllint | No "type unknown" for ChatViewModel |
| REQ-NF-002 | Build artifact check | `.qmltypes` non-empty; has ChatViewModel |
| REQ-NF-003 | CI job | `check-qmltypes` passes; fails on invalid |
| REQ-NF-004 | Manual: error UX | No modal dialogs; window usable after error |
| REQ-NF-005 | GTest: 8+ test cases | All pass; no real network; no Ollama required |
| REQ-NF-006 | Manual smoke test: 9 steps | All steps pass; no crash |
| REQ-NF-007 | Code review: Text.PlainText | No Markdown libraries linked |
| REQ-C-001 | AddressSanitizer: close mid-stream | No use-after-free |
| REQ-C-002 | Unit test: callbacks → signals | Callback fires → property signal emitted |

---

## Document Version

- **Created**: 2026-07-21
- **Status**: Final for SDD cycle start
- **Approval**: Grilling session decisions (items 1–11 above) bound into all constraints and non-goals.
