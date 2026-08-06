# Chat Window QML Integration Design

**Document Version**: 1.0
**Date**: 2026-07-21
**Modules**: `holonight_application` (new `ChatViewModel`/`MessageListModel`), `apps/chat` (CMake QML
type-registration wiring, `ChatApplication` window-close hook), `qml/workspace/WorkspaceWindow.qml`
(new UI — QML implementation detail, not elaborated at code level here since QML is Stage
4/task-breakdown territory, but every C++ surface it binds to is fixed below)
**Traces to**: `docs/sdd/chat-window-qml/SPEC.md` (27 functional, 7 non-functional, 2 constraint
requirements)
**Status**: Implemented

---

## Startup and restored-message layout

`WorkspaceWindow` is constructed hidden at a valid `1000 × 640` size. `ChatApplication` owns its
exposure: it creates the QML tree, wires panel, notification, window, and view-model signals, and
then calls the same `ShowWorkspace()` path used by D-Bus activation.

`MessageList` is a stable viewport owned by `ChatPanel`. While the initial conversation is
unresolved it shows `HnLoadingState` without changing the surrounding panel geometry. A model reset
conceals only the inner list for one deferred layout and end-positioning pass, then reveals the
final position without animation. Streaming still follows the bottom unless the user scrolls away.

Message cards derive width only from the allocated viewport. Completed-content loaders wait for a
positive layout width before creating Markdown or code blocks, so wrapped Markdown receives its
constraint before parsing and cannot feed its natural width back into the parent layout.

## 1. Components

### 1.1 `ChatViewModel` (new, `holonight_application`)

The QML-facing bridge. A `QML_SINGLETON` + `QML_ELEMENT` `QObject` that owns the ephemeral
`Conversation` (REQ-F-001), the `ChatController`, and the `OllamaProvider`/`HttpClient` stack, and
translates `ChatController`'s callback-based API into `Q_PROPERTY`/signal/invokable surface QML can
bind to (REQ-C-002).

Concrete surface (full rationale for each choice is in §6; full header is in §5):

| Kind | Name | Type | Notes |
|---|---|---|---|
| Property | `availableModels` | `QVariantList` | list of `{provider_id, model_name}` maps (REQ-F-002, REQ-F-017) |
| Property | `selectedModelId` | `QVariantMap` | read/write; `{provider_id, model_name}` (REQ-F-004, REQ-F-018) |
| Property | `canSend` | `bool` | readonly, computed (REQ-F-003, REQ-F-007) |
| Property | `canRegenerate` | `bool` | readonly, computed (REQ-F-014) |
| Property | `isStreaming` | `bool` | readonly, mirrors `ChatController::isStreaming()` (REQ-F-012) |
| Property | `errorMessage` | `QString` | readonly (REQ-F-010, REQ-F-019, REQ-F-020) |
| Property | `inputText` | `QString` | read/write (REQ-F-006, REQ-F-026) |
| Property | `messages` | `MessageListModel*` | readonly, `CONSTANT` — the ListView model (REQ-F-021) |
| Invokable | `send(QString text)` | — | (REQ-F-005) |
| Invokable | `stop()` | — | (REQ-F-013, REQ-F-024) |
| Invokable | `regenerate()` | — | (REQ-F-015, REQ-F-016) |
| C++-only | `conversation() const` | `Conversation*` | not a `Q_PROPERTY` — see §6.6 |

### 1.2 `MessageListModel` (new, `holonight_application`)

A `QAbstractListModel` mirroring `Conversation::messages()` for the `ListView` (REQ-F-021,
REQ-F-023). It is **not** a live view over `Conversation` (which has no change notification of its
own) — `ChatViewModel` writes to it explicitly, synchronously, immediately after each point where it
knows `ChatController` mutated the authoritative `Conversation`. Two mutators only:

- `appendMessage(const Message&)` — `beginInsertRows`/`endInsertRows`, for the User+Assistant pair
  `send()` adds.
- `updateLastMessage(const Message&)` — a targeted `dataChanged(index, index, {TextRole,
  StatusRole, RoleRole})` on the last row only. This is the REQ-F-008 hot path: every
  `ContentDelta` calls this, never a full model reset, so the `ListView` re-renders one delegate's
  `Text`/status label in place rather than re-laying-out the whole list.

### 1.3 `ChatApplication` (existing, `apps/chat/app`)

Gains one piece of wiring: a `QQuickView::closing` connection that fetches the `ChatViewModel`
singleton instance from the view's `QQmlEngine` and calls `stop()` on it before the window is
allowed to finish closing (REQ-F-024/REQ-C-001; see §3).

### 1.4 Build system (`src/application/CMakeLists.txt`, `apps/chat/CMakeLists.txt`, new
`cmake/combine-metatypes.cmake`, new `scripts/check-qmltypes.sh`)

`holonight_application` is already `STATIC` (converted in the prior `ollama-chat-backend` cycle —
no `INTERFACE`→`STATIC` conversion is needed this cycle, unlike what the task brief anticipated).
What *is* still needed, because `ChatViewModel`/`MessageListModel` are the first `QML_ELEMENT` types
in a static library in this repo: the metatype-extract-merge-register dance from `holonight-shell`,
ported down to a single input library. Full snippets in §4.

---

## 2. Data Flow

All flows assume exactly one `ChatViewModel` instance (the QML singleton), one `Conversation`, one
`ChatController`, for the process lifetime — matching the SPEC's "one ephemeral conversation" scope.

### 2.1 Construction / startup (REQ-F-001, REQ-F-002, REQ-F-003, REQ-F-004, REQ-F-019)

1. Qt's QML engine needs an instance of the singleton the first time QML references
   `ChatViewModel`. Because `ChatViewModel` is not default-constructible (it needs an
   `OllamaProvider`), it supplies a static factory instead of relying on Qt's auto-generated
   default-construction path (see §6.1):
   ```cpp
   ChatViewModel* ChatViewModel::create(QQmlEngine*, QJSEngine*) {
     auto http_client = std::make_shared<holonight_providers::QtNetworkHttpClient>();
     auto provider = std::make_shared<holonight_providers::OllamaProvider>(http_client);
     return new ChatViewModel(std::move(provider));
   }
   ```
2. `ChatViewModel`'s constructor runs synchronously:
   - `conversation_ = std::make_shared<Conversation>(ConversationId::generate(), "Chat",
     QDateTime::currentDateTimeUtc())` — REQ-F-001's "exactly one ephemeral Conversation ...
     retained for the lifetime of the application window." `shared_ptr` (not a plain member) per
     REQ-C-001's binding: "`ChatViewModel` must hold a `shared_ptr<Conversation>`."
   - `chat_controller_ = std::make_unique<ChatController>(provider_)`.
   - `message_model_ = new MessageListModel(this)` — empty at construction (no messages yet).
   - `provider_->refresh([this] { onModelsRefreshed(); })` — REQ-F-002's "call `refresh()` exactly
     once ... during construction."
3. `onModelsRefreshed()` reads `provider_->availableModels()`:
   - Rebuilds `available_models_` (`QVariantList`) and emits `availableModelsChanged()` — this is
     the "`onModelsChanged` signal" REQ-F-002 refers to (Qt's own `NOTIFY` mechanism, not a
     hand-rolled second signal — see §6.2).
   - If the list is non-empty: `selected_model_id_ = models.front()`, emit
     `selectedModelIdChanged()` (REQ-F-004 — index-0 auto-select).
   - If the list is empty (Ollama unreachable *or* zero models pulled — `OllamaProvider::refresh()`
     does not distinguish the two; see the ambiguity flagged in §6.2): `setErrorMessage("Failed to
     fetch models — Ollama may not be running")` (REQ-F-019). REQ-F-003's *different* wording ("No
     models available — please ensure Ollama is running...") is a QML-side placeholder string shown
     whenever `availableModels.length === 0`, not driven by `errorMessage` — the two requirements
     describe two different UI surfaces (model-picker placeholder vs. inline error banner) that
     happen to both key off the same empty-list condition.
   - Calls `refreshComputedProperties()` (below), which computes and emits `canSendChanged` — false
     while the list is empty, satisfying REQ-F-003's "disable the send button."

### 2.2 Send (REQ-F-005, REQ-F-006, REQ-F-007, REQ-F-008, REQ-F-009, REQ-F-010, REQ-F-024/C-002)

1. QML: `onClicked: ChatViewModel.send(ChatViewModel.inputText)` (or `Keys.onReturnPressed`).
2. `ChatViewModel::send(text)` guards on `canSend()`; if false, no-op (REQ-F-007 — "prevent text
   submission").
3. Builds the callback with a `QPointer` guard (see §2.5) and calls `chat_controller_->send(*conversation_,
   selected_model_id_, text, onEvent)`.
4. **By the time this call returns**, `ChatController::send()` has already, synchronously, before any
   network I/O: appended the User message (`MessageStatus::Complete`) and the Assistant placeholder
   (`MessageStatus::Streaming`) to `conversation_` (see `chat_controller.cpp` — `appendMessage` calls
   happen inside `send()`/`startStream()`, before `provider_->sendChat()` is invoked). So
   `ChatViewModel::send()` reads `conversation_->messages()`, which now has two more entries than
   before, and mirrors exactly those two into `message_model_`:
   ```cpp
   const auto& msgs = conversation_->messages();
   message_model_->appendMessage(msgs[msgs.size() - 2]);  // User, Complete
   message_model_->appendMessage(msgs[msgs.size() - 1]);  // Assistant, Streaming
   ```
5. `setInputText(QString())` — REQ-F-006, clears the box synchronously, same call stack.
6. `setErrorMessage(QString())` — clears any stale error from a previous failed attempt so
   REQ-F-010's "user can retry" doesn't leave a dismissed-but-still-shown-later error.
7. `refreshComputedProperties()` — `isStreaming` flips true (REQ-F-005's "isStreaming() returns true
   immediately after send() returns"), `canSend`/`canRegenerate` recompute.
8. Later, for every `StreamEvent` the provider emits, `ChatController::handleStreamEvent()` mutates
   `conversation_` (via `replaceLastMessage`) **before** invoking the `on_event` callback — true for
   all four `StreamEvent` variants (verified by reading `chat_controller.cpp`: `replaceLastMessage`
   precedes `stream.on_event(event)` in every branch of the `std::visit`). So `ChatViewModel`'s
   callback (`onStreamEvent`, guarded by `QPointer`, see §2.5) can unconditionally read
   `conversation_->messages().back()` as the current truth and mirror it:
   ```cpp
   void ChatViewModel::onStreamEvent(const StreamEvent& event) {
     message_model_->updateLastMessage(conversation_->messages().back());
     if (const auto* err = std::get_if<holonight_domain::Error>(&event)) {
       setErrorMessage(err->message);       // REQ-F-010/REQ-F-020
     }
     refreshComputedProperties();           // isStreaming/canRegenerate flip on Completed/Error/Cancelled
   }
   ```
   - `ContentDelta`: `message_model_->updateLastMessage(...)` fires the targeted `dataChanged` —
     REQ-F-008's "user sees new text appear incrementally within one frame."
   - `Completed`: status is now `Complete`; `refreshComputedProperties()` flips `isStreaming` false
     (REQ-F-009) and `canRegenerate` true (REQ-F-014).
   - `Error`: status is now `Error`, text is the error string; `errorMessage` is set (REQ-F-010);
     `isStreaming` flips false, `canRegenerate` flips true (an `Error`-terminated assistant message
     is eligible for regenerate per REQ-F-014).
   - `Cancelled`: covered by §2.3.

### 2.3 Stop (REQ-F-011, REQ-F-012, REQ-F-013)

1. QML: `onClicked: ChatViewModel.stop()`.
2. `ChatViewModel::stop()` calls `chat_controller_->stop(conversation_->id())` — idempotent, safe
   with nothing in flight (REQ-F-013).
3. If something *was* in flight, `ChatController::stop()` synchronously (before returning):
   transitions the working message to `Cancelled`, calls `conversation_->replaceLastMessage(...)`,
   and invokes `on_event(Cancelled{})` — which is `ChatViewModel::onStreamEvent`, which does the same
   `updateLastMessage`/`refreshComputedProperties` dance as §2.2 step 8. By the time
   `chat_controller_->stop()` returns to `ChatViewModel::stop()`, `message_model_` and `isStreaming`
   are already consistent. `ChatViewModel::stop()` calls `refreshComputedProperties()` once more
   anyway — a harmless, idempotent no-op in the already-updated case, but the only path that keeps
   state correct in the nothing-was-in-flight case (REQ-F-013's idempotence).

### 2.4 Regenerate (REQ-F-014, REQ-F-015, REQ-F-016)

1. QML: `onClicked: ChatViewModel.regenerate()`, `enabled: ChatViewModel.canRegenerate`.
2. `ChatViewModel::regenerate()` guards on `canRegenerate()`; false → no-op, no call into
   `ChatController`, conversation unchanged (REQ-F-016, verbatim).
3. If true: calls `chat_controller_->regenerate(*conversation_, selected_model_id_, onEvent)` (same
   `QPointer`-guarded lambda pattern as send). `ChatController::regenerate()` synchronously replaces
   the last message in place (same `MessageId`, cleared text, `MessageStatus::Pending` then
   `Streaming`) before returning — `ChatViewModel` mirrors that with a single
   `message_model_->updateLastMessage(conversation_->messages().back())` call (not `appendMessage` —
   the row count does not change, REQ-F-015's "re-running... in place").
4. `setErrorMessage(QString())`, `refreshComputedProperties()` — same as send.

### 2.5 The callback-lifetime guard (REQ-C-002 concern; addresses the async-callback-outliving-`this` risk)

`ChatController::in_flight_` (a `QHash` member of `ChatController`, which is itself owned by
`ChatViewModel` via `std::unique_ptr`) stores the `std::function<void(const StreamEvent&)>` that
`ChatViewModel::send()`/`regenerate()` hands it. That `std::function` captures `this`
(`ChatViewModel*`). Two independent facts make a stale-pointer callback firing *look* unlikely:

- `ChatController::stop()` erases the `in_flight_` entry (and thus drops the stored lambda)
  **before** it calls `handle->cancel()` — so even if the underlying `QNetworkReply` abort later
  fires an async `onFinished`/`onError` signal, `ChatController::handleStreamEvent()` looks up the
  now-missing key and returns immediately, never reaching `ChatViewModel`'s lambda again.
- `ChatController` is owned (by value, via `unique_ptr`) inside `ChatViewModel`, so it is always
  destroyed strictly before or during `~ChatViewModel()`, never after.

Despite that, the lambda additionally captures a `QPointer<ChatViewModel>` and checks it first, as
cheap, explicit insurance rather than relying entirely on `ChatController`'s internal bookkeeping
staying correct forever:

```cpp
QPointer<ChatViewModel> guard(this);
chat_controller_->send(*conversation_, selected_model_id_, text,
    [guard](const holonight_domain::StreamEvent& event) {
      if (auto* self = guard.data()) {
        self->onStreamEvent(event);
      }
    });
```

This is deliberately defensive rather than provably-necessary-today: it costs one pointer
comparison per event and protects against a *future* refactor of `ChatController` (e.g., if a later
change makes `stop()` async, or if `ChatController`'s lifetime is ever decoupled from
`ChatViewModel`'s) silently reintroducing a use-after-free that today's design happens to avoid by
construction. See §6.3/§7 for the alternative considered (skip the guard, rely solely on
`ChatController`'s ordering) and why it was rejected.

---

## 3. Stop-Before-Destroy Lifecycle (REQ-C-001, REQ-F-024, REQ-F-025)

**Recommendation: `QQuickView::closing(QQuickCloseEvent*)`, wired in `ChatApplication.cpp`, plus a
defensive `stop()` call in `ChatViewModel`'s own destructor.**

### 3.1 Primary hook: `QQuickView::closing`

`ChatApplication` currently owns a bare `QQuickView` (not a `QQuickWindow` subclass, not a
`qt6_add_executable`-managed engine wrapper), so the closing signal is available directly:

```cpp
ChatApplication::ChatApplication(int& argc, char** argv) : QGuiApplication(argc, argv) {
  ...
  view_ = std::make_unique<QQuickView>();
  ...
  view_->setSource(QUrl(QStringLiteral("qrc:/HolonightChat/workspace/WorkspaceWindow.qml")));
  QObject::connect(view_.get(), &QQuickView::closing, view_.get(),
                    [engine = view_->engine()](QQuickCloseEvent*) {
                      if (auto* view_model = engine->singletonInstance<holonight_application::ChatViewModel*>(
                              "HolonightChat", "ChatViewModel")) {
                        view_model->stop();
                      }
                    });
  view_->show();
}
```

`QQmlEngine::singletonInstance<T>(uri, typeName)` retrieves the already-created singleton instance
(it will already exist by the time the window can be closing, since QML bound to it during load) —
no risk of triggering a fresh `create()` call at shutdown.

**Why `closing` over the alternatives** (see §7 for the full rejected-alternatives writeup):

- `QWindow::visibleChanged` fires on minimize/hide too, not just an actual close — would call
  `stop()` on every minimize. Harmless given `stop()`'s idempotence, but semantically wrong (aborts
  a legitimate in-flight generation just because the user alt-tabbed away) and not what REQ-F-024
  asks for ("before the application window closes").
- `QCoreApplication::aboutToQuit` fires once, application-wide, later in the shutdown sequence
  (after windows have already started tearing down in the general case) — less precise than a
  per-window "about to close" hook, and mixes an app-lifecycle concern into what REQ-F-024 frames as
  a window-close concern. For this single-window app the practical difference is small, but
  `closing()` is the more correct, idiomatic Qt hook for "this window is about to go away," and it
  fires *before* any teardown begins, giving a clean synchronous call point.
- A QML `Component.onDestruction` handler on the root item was considered and rejected — see §7.

### 3.2 Secondary/defensive hook: `ChatViewModel`'s destructor

```cpp
ChatViewModel::~ChatViewModel() { stop(); }
```

Idempotent, so redundant with §3.1 in the normal window-close path — but it is the only safety net
for any construction/destruction path that never goes through `ChatApplication`'s `QQuickView` at
all: unit tests that construct/destruct a `ChatViewModel` directly (§9), and any future code path
(a second window, a different embedding host) that might reuse `ChatViewModel` without replicating
the `closing()` wiring. Because `~ChatViewModel()`'s body runs *before* any of its members
(`chat_controller_`, `conversation_`, ...) are destroyed, calling `stop()` first guarantees
`chat_controller_->in_flight_` is empty before `conversation_` (the `shared_ptr<Conversation>`) is
released — satisfying REQ-C-001 regardless of member-destruction order.

---

## 4. CMake Changes

### 4.1 `src/application/CMakeLists.txt`

Already `STATIC` (prior cycle) — add the two new source files and `Qt6::Qml` (needed for
`QML_ELEMENT`/`QML_SINGLETON`, declared via `<QtQml/qqmlregistration.h>`):

```cmake
add_library(holonight_application STATIC
    src/chat_controller.cpp
    src/chat_view_model.cpp
    src/message_list_model.cpp
)

target_include_directories(holonight_application PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(holonight_application PUBLIC
    holonight_domain
    holonight_providers
    Qt6::Core
    Qt6::Qml
)

target_compile_features(holonight_application PUBLIC cxx_std_23)
```

(`CMAKE_AUTOMOC` is already `ON` globally in the root `CMakeLists.txt` — no per-target `AUTOMOC`
property needed, unlike `holonight_providers`, which sets it redundantly.)

### 4.2 `apps/chat/CMakeLists.txt` — metatype extract/merge/register

Two changes to the existing `qt_add_qml_module()` call (add `TYPEINFO`/`NO_GENERATE_QMLTYPES`, so
the manual registration below owns `.qmltypes` generation instead of Qt's default path):

```cmake
qt_add_qml_module(holonight-chat
    URI HolonightChat
    VERSION 1.0
    RESOURCE_PREFIX "/"
    TYPEINFO HolonightChat.qmltypes
    NO_GENERATE_QMLTYPES
    NO_IMPORT_SCAN
    QML_FILES ${HOLONIGHT_CHAT_QML_FILES}
)
```

Then, after the existing `target_link_libraries(holonight-chat PRIVATE ...)` block, the
`holonight-shell`-derived extract/merge/register sequence — simplified to holonight-ai's single
static library needing this treatment (`holonight_application`; `holonight-shell`'s three-library
`_HOLONIGHT_*_METATYPES` variables collapse to one):

```cmake
# ChatViewModel/MessageListModel (QML_ELEMENT types) live in holonight_application, a static
# library — qt_add_qml_module() only sees metatypes attached directly to the holonight-chat
# executable target, so they must be extracted and merged in manually. See CLAUDE.md's "Future:
# QML Singletons From Static Libraries" section and holonight-shell/apps/shell/CMakeLists.txt for
# the pattern this is ported from.
set_property(TARGET holonight_application PROPERTY INTERFACE_SOURCES "")

qt6_extract_metatypes(holonight_application OUTPUT_FILES _HOLONIGHT_APPLICATION_METATYPES)

set(_HOLONIGHT_CHAT_METATYPES "${CMAKE_CURRENT_BINARY_DIR}/meta_types/qt6holonight-chat_metatypes.json")
add_custom_command(
    OUTPUT "${_HOLONIGHT_CHAT_METATYPES}"
    DEPENDS
        ${_HOLONIGHT_APPLICATION_METATYPES}
        "${PROJECT_SOURCE_DIR}/cmake/combine-metatypes.cmake"
    COMMAND ${CMAKE_COMMAND}
        "-DINPUT_FILES=${_HOLONIGHT_APPLICATION_METATYPES}"
        "-DOUTPUT=${_HOLONIGHT_CHAT_METATYPES}"
        -P "${PROJECT_SOURCE_DIR}/cmake/combine-metatypes.cmake"
    COMMENT "Collecting HolonightChat C++ metatypes"
    VERBATIM
)
_qt_internal_assign_build_metatypes_files_and_properties(holonight-chat
    METATYPES_FILE_NAME "qt6holonight-chat_metatypes.json"
    METATYPES_FILE_PATH "${_HOLONIGHT_CHAT_METATYPES}"
)
set_target_properties(holonight-chat PROPERTIES _qt_internal_has_qmltypes TRUE)
_qt_internal_qml_type_registration(holonight-chat)
```

`holonight-chat_qmltyperegistration` (the target `_qt_internal_qml_type_registration` creates) is
already depended on by the root `CMakeLists.txt`'s `qml-lint` custom target — that dependency
requires no change; it will simply start doing real work once this wiring lands.

### 4.3 New file: `cmake/combine-metatypes.cmake`

Ported **verbatim, unmodified**, from `holonight-shell/scripts/collect-moc-metatypes.cmake` (see §7
for why the general N-file merge logic is kept as-is rather than hand-simplified to a single-file
copy, even though only one input is wired up today):

```cmake
if(NOT DEFINED INPUT_FILES)
  message(FATAL_ERROR "INPUT_FILES is required")
endif()

if(NOT DEFINED OUTPUT)
  message(FATAL_ERROR "OUTPUT is required")
endif()

string(REPLACE "|" ";" input_files "${INPUT_FILES}")

set(json_files "")
foreach(input_file IN LISTS input_files)
  string(STRIP "${input_file}" input_file)
  if(input_file STREQUAL "")
    continue()
  endif()

  if(NOT EXISTS "${input_file}")
    message(FATAL_ERROR "Input metatypes file does not exist: ${input_file}")
  endif()
  list(APPEND json_files "${input_file}")
endforeach()
list(SORT json_files)

file(WRITE "${OUTPUT}" "[\n")
set(first_entry TRUE)
foreach(json_file IN LISTS json_files)
  file(READ "${json_file}" content)
  string(STRIP "${content}" content)
  if(content STREQUAL "")
    continue()
  endif()
  string(SUBSTRING "${content}" 0 1 first_char)
  string(LENGTH "${content}" content_length)
  math(EXPR last_char_index "${content_length} - 1")
  string(SUBSTRING "${content}" "${last_char_index}" 1 last_char)
  if(first_char STREQUAL "[" AND last_char STREQUAL "]")
    math(EXPR inner_length "${content_length} - 2")
    string(SUBSTRING "${content}" 1 "${inner_length}" content)
    string(STRIP "${content}" content)
    if(content STREQUAL "")
      continue()
    endif()
  endif()

  if(first_entry)
    set(first_entry FALSE)
  else()
    file(APPEND "${OUTPUT}" ",\n")
  endif()
  file(APPEND "${OUTPUT}" "${content}")
endforeach()
file(APPEND "${OUTPUT}" "\n]\n")
```

(Since `INPUT_FILES` is `|`-joined and the loop already tolerates a single entry cleanly, no
special-casing is needed for the one-library case — this is what "simplify the call site, not the
script" means concretely: the CMake *call* passes one path instead of three, everything downstream
is unchanged.)

### 4.4 New file: `scripts/check-qmltypes.sh` (REQ-NF-003)

Ported and trimmed from `holonight-shell/scripts/check-qmltypes.sh` down to this project's single
required type and module path:

```bash
#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${1:-${BUILD_DIR:-build}}"
if [[ "${build_dir}" = /* ]]; then
  build_root="${build_dir}"
else
  build_root="${repo_root}/${build_dir}"
fi

chat_module_dir="${build_root}/apps/chat/HolonightChat"
qmltypes_file="${chat_module_dir}/HolonightChat.qmltypes"

required_types=(
  "ChatViewModel"
)

if [[ ! -s "${qmltypes_file}" ]]; then
  echo "Missing or empty qmltypes file: ${qmltypes_file}" >&2
  echo "Build holonight-chat before running this check." >&2
  exit 1
fi

if ! grep -q 'Module {' "${qmltypes_file}"; then
  echo "Malformed qmltypes file: ${qmltypes_file}" >&2
  exit 1
fi

missing_types=()
for type_name in "${required_types[@]}"; do
  if ! grep -q "name: \"${type_name}\"" "${qmltypes_file}"; then
    missing_types+=("${type_name}")
  fi
done

if (( ${#missing_types[@]} > 0 )); then
  {
    echo "Generated qmltypes file is missing required HolonightChat types:"
    printf '  %s\n' "${missing_types[@]}"
    echo
    echo "Checked: ${qmltypes_file}"
  } >&2
  exit 1
fi

echo "QML type metadata check passed."
```

### 4.5 `Taskfile.yml` — new `qmltypes-check` task

```yaml
  qmltypes-check:
    desc: Verify generated HolonightChat QML type metadata
    deps: [build]
    cmds:
      - scripts/check-qmltypes.sh {{.BUILD_DIR}}
```

### 4.6 CI (`.github/workflows/ci.yml`)

Add a step invoking `scripts/check-qmltypes.sh build` after the existing build step, alongside the
`qml-lint` step (REQ-NF-003's "CI job includes this check" — failing loudly if `.qmltypes` regresses
to `Module {}`).

---

## 5. Interfaces / APIs

### 5.1 `src/application/include/holonight_application/message_list_model.h`

```cpp
#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <holonight_domain/holonight_domain.h>
#include <vector>

namespace holonight_application {

// Bridges Conversation::messages() (a plain std::vector<Message>, with no change notification of
// its own) to a QML ListView. ChatViewModel is the sole writer: it calls appendMessage()/
// updateLastMessage() synchronously, immediately after ChatController mutates the authoritative
// Conversation, so this model's rows are always a faithful mirror, never a second source of truth.
class MessageListModel : public QAbstractListModel {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Instantiated only by ChatViewModel; do not construct from QML")

 public:
  enum Roles : std::uint16_t {  // NOLINT(cppcoreguidelines-use-enum-class): Qt model roles are int-compatible.
    IdRole = Qt::UserRole + 1,
    RoleRole,    // "user" | "assistant" | "system"
    TextRole,
    StatusRole,  // "pending" | "streaming" | "complete" | "error" | "cancelled"
  };
  Q_ENUM(Roles)

  explicit MessageListModel(QObject* parent = nullptr);

  [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  // Appends one row (beginInsertRows/endInsertRows) — used for the User+Assistant-placeholder pair
  // send() adds, and any future single-message append.
  void appendMessage(const holonight_domain::Message& message);

  // Updates only the last row in place, emitting a targeted dataChanged for that row only — the
  // hot path for token-by-token ContentDelta streaming (REQ-F-008). Never resets the model. No-op
  // if the model is currently empty.
  void updateLastMessage(const holonight_domain::Message& message);

  // Full rebuild from the authoritative Conversation. Not exercised by any flow in this cycle's
  // SPEC — present only as a defensive repair path if the model and Conversation ever drift.
  void resetFrom(const std::vector<holonight_domain::Message>& messages);

 private:
  struct Row {
    QString id;
    QString role;
    QString text;
    QString status;
  };

  [[nodiscard]] static Row toRow(const holonight_domain::Message& message);

  std::vector<Row> rows_;
};

}  // namespace holonight_application
```

### 5.2 `src/application/include/holonight_application/chat_view_model.h`

```cpp
#pragma once

#include "holonight_application/message_list_model.h"
#include "holonight_providers/ollama_provider.h"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <holonight_domain/holonight_domain.h>
#include <memory>

class QQmlEngine;
class QJSEngine;

namespace holonight_application {

class ChatController;

// QML-facing bridge (REQ-C-002). Owns the one ephemeral Conversation (REQ-F-001), the
// ChatController, and the OllamaProvider for the lifetime of the application window.
class ChatViewModel : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

  Q_PROPERTY(QVariantList availableModels READ availableModels NOTIFY availableModelsChanged)
  Q_PROPERTY(QVariantMap selectedModelId READ selectedModelId WRITE setSelectedModelId NOTIFY selectedModelIdChanged)
  Q_PROPERTY(bool canSend READ canSend NOTIFY canSendChanged)
  Q_PROPERTY(bool canRegenerate READ canRegenerate NOTIFY canRegenerateChanged)
  Q_PROPERTY(bool isStreaming READ isStreaming NOTIFY isStreamingChanged)
  Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
  Q_PROPERTY(QString inputText READ inputText WRITE setInputText NOTIFY inputTextChanged)
  Q_PROPERTY(holonight_application::MessageListModel* messages READ messages CONSTANT)

 public:
  // Custom singleton factory: ChatViewModel is not default-constructible (it needs an
  // OllamaProvider), so Qt's auto-generated self-instantiation path does not apply — this is
  // registered instead (see DESIGN.md §6.1).
  static ChatViewModel* create(QQmlEngine* qml_engine, QJSEngine* js_engine);

  // DI constructor: both the production create() factory and GTest fixtures funnel through this,
  // so tests never need a QQmlEngine or a real network stack (see DESIGN.md §9).
  explicit ChatViewModel(std::shared_ptr<holonight_providers::OllamaProvider> provider, QObject* parent = nullptr);
  ~ChatViewModel() override;

  ChatViewModel(const ChatViewModel&) = delete;
  ChatViewModel& operator=(const ChatViewModel&) = delete;
  ChatViewModel(ChatViewModel&&) = delete;
  ChatViewModel& operator=(ChatViewModel&&) = delete;

  [[nodiscard]] QVariantList availableModels() const;
  [[nodiscard]] QVariantMap selectedModelId() const;
  void setSelectedModelId(const QVariantMap& model_id);
  [[nodiscard]] bool canSend() const;
  [[nodiscard]] bool canRegenerate() const;
  [[nodiscard]] bool isStreaming() const;
  [[nodiscard]] QString errorMessage() const;
  [[nodiscard]] QString inputText() const;
  void setInputText(QString text);
  [[nodiscard]] MessageListModel* messages() const;

  // Plain C++ accessor (REQ-F-001's acceptance criterion calls this out explicitly). Not a
  // Q_PROPERTY: Conversation is not a Qt/QML-registered type (see §6.6) and QML never needs it
  // directly — it renders through the `messages` MessageListModel instead.
  [[nodiscard]] holonight_domain::Conversation* conversation() const;

  Q_INVOKABLE void send(const QString& text);
  Q_INVOKABLE void stop();
  Q_INVOKABLE void regenerate();

 Q_SIGNALS:
  void availableModelsChanged();
  void selectedModelIdChanged();
  void canSendChanged();
  void canRegenerateChanged();
  void isStreamingChanged();
  void errorMessageChanged();
  void inputTextChanged();

 private:
  void onModelsRefreshed();
  void onStreamEvent(const holonight_domain::StreamEvent& event);
  void refreshComputedProperties();
  void setErrorMessage(QString message);

  std::shared_ptr<holonight_providers::OllamaProvider> provider_;
  std::unique_ptr<ChatController> chat_controller_;
  std::shared_ptr<holonight_domain::Conversation> conversation_;
  MessageListModel* message_model_;  // child QObject, parented to `this`

  QVariantList available_models_;
  holonight_domain::ModelId selected_model_id_;
  QString error_message_;
  QString input_text_;
  bool cached_can_send_ = false;
  bool cached_can_regenerate_ = false;
  bool cached_is_streaming_ = false;
};

}  // namespace holonight_application
```

---

## 6. Key Decisions with Rationale

### 6.1 Custom `static create(QQmlEngine*, QJSEngine*)` instead of relying on `QML_SINGLETON`'s default self-instantiation

`ChatViewModel` needs a `shared_ptr<OllamaProvider>` at construction — it is not
default-constructible, so Qt's usual "just `new` it" auto-registration for `QML_SINGLETON` types
(the path `WorkspaceModel` uses in `holonight-shell`, since it only needs a `parent`) does not
apply. Providing `create()` is Qt's documented escape hatch for exactly this case, and it doubles as
the seam that keeps production wiring (real `QtNetworkHttpClient`) and test wiring (`FakeHttpClient`
via the DI constructor) from ever touching the same code path.

### 6.2 `availableModels`/`selectedModelId` as `QVariantList`/`QVariantMap`, not a registered `ModelId` QML value type

`holonight_domain::ModelId` is a plain struct with no Qt/QML registration, and `holonight_domain` is
explicitly out of this cycle's scope (only additive C++ methods were in-scope in the prior cycle;
this cycle's non-goals don't call out touching it, but the SPEC's responsibility boundaries list
`holonight_domain` under "Backend (not touched)"). Rather than adding a `Q_GADGET`/`QML_VALUE_TYPE`
to a domain type from an application-layer design, `ChatViewModel` converts at the boundary:
`QVariantMap{"provider_id": ..., "model_name": ...}` — chosen specifically because REQ-F-017's
acceptance criterion literally reads `model.provider_id + "/" + model.model_name`, i.e. the QML
delegate context needs exactly those two keys, which a `QVariantMap` (or a `QVariantList` of them)
provides for free through Qt's automatic `QVariantMap`→QML-object-property exposure — no custom
role/property plumbing required.

**Resolved ambiguity**: `OllamaProvider::refresh(on_complete)` does not tell the caller *why* the
resulting list is empty — network failure and "Ollama is up but has zero models pulled" are
indistinguishable at this API boundary. REQ-F-003 and REQ-F-019 originally implied two different
pieces of UI/copy for what could be two different causes; resolved (2026-07-21) to use a single
merged message for both — see SPEC.md's amended REQ-F-003/REQ-F-019 wording. `holonight_providers`
stays untouched this cycle; distinguishing the two causes is deferred to whenever a future cycle
extends `OllamaProvider::refresh()`'s callback to report success/failure explicitly.

### 6.3 `QPointer<ChatViewModel>` guard on the stream-event lambda

Addressed in full in §2.5. Short version: not strictly required given `ChatController`'s current
stop()-clears-`in_flight_`-before-cancel ordering, but cheap, explicit, and protects against future
changes to that ordering. Rejected alternative: skip the guard entirely, since today's
`ChatController` already prevents the callback from firing after `stop()` — see §7.

### 6.4 `MessageListModel` (a `QAbstractListModel`), not a recomputed `QVariantList`

Directly requested by the task brief's framing and confirmed necessary by REQ-F-008: a
`QVariantList` rebuilt on every `ContentDelta` would force the entire `ListView` to re-bind (every
delegate destroyed/recreated, scroll position potentially lost, no way to animate just the changed
row) on every streamed token — unacceptable for "live," "token-by-token" rendering. A
`QAbstractListModel` with a single `dataChanged` call per delta lets Qt Quick's `ListView` update
exactly the one visible delegate bound to the changed row.

### 6.5 Two mutators only (`appendMessage`, `updateLastMessage`), no generic "sync with Conversation" method as the primary path

`ChatController`'s mutation points are fully enumerable from reading `chat_controller.cpp` (append
twice in `send()`, replace-in-place in `regenerate()`/every `StreamEvent` branch) — `ChatViewModel`
knows exactly which of the two operations applies at each call site, so a targeted call is both
simpler and cheaper than diffing the whole vector after every operation. `resetFrom()` exists only
as an escape hatch, not a routinely-exercised path (see §8 for the risk this narrows but does not
eliminate).

### 6.6 `conversation()` returns a raw, non-`Q_PROPERTY` `Conversation*`

REQ-F-001's acceptance criterion is phrased as a plain C++ method call
(`ChatViewModel::conversation() const`), and `Conversation` has no Qt/QML registration (see §6.2)
— exposing an opaque, unusable pointer as a `Q_PROPERTY`/`Q_INVOKABLE` to QML would satisfy nothing
QML actually needs (QML renders through the `messages` `MessageListModel`, never touches
`Conversation` directly). Kept as a plain public method for the unit test in REQ-F-001's acceptance
criterion and for the window-close hook's C++ callers, not registered for QML.

### 6.7 Stop hook: `QQuickView::closing` + destructor, not destructor alone

See §3 and §7.

---

## 7. Alternatives Considered

- **`QVariantList` for `messages` instead of `MessageListModel`.** Rejected — see §6.4; fails
  REQ-F-008's "live... without full-list rebinding" framing outright once streaming produces more
  than a handful of deltas per response.
- **Exposing `Conversation` via `Q_GADGET`, with `messages()` returning a `QVariantList` snapshot
  computed on demand.** Rejected for the same reason as above (would still need to be recomputed and
  rebound on every delta to reflect live text), and additionally would require modifying
  `holonight_domain`, which is out of this cycle's touched-module list.
  `MessageListModel` gets the same "expose to QML" outcome without touching the domain module at
  all.
- **Calling `ChatController::stop()` only from `ChatViewModel`'s destructor, skipping the
  `QQuickView::closing` hook.** Rejected: by the time `~ChatViewModel()` runs during normal
  `QQuickView`/`QQmlEngine` teardown, `Component.onDestruction` handlers and QML-side bindings may
  already be unwinding, and the *order* in which Qt tears down the engine, the singleton, and the
  window is not a contract this design wants to depend on for a hard safety requirement. An explicit
  `closing()` connection makes REQ-F-024's "before the application window closes" a directly-visible,
  ordered call in `ChatApplication.cpp`, independent of engine-teardown internals. The destructor
  call is retained as a defensive fallback (§3.2), not removed — but is not the *sole* mechanism, per
  this rejection.
- **Skipping the `QPointer<ChatViewModel>` guard on the stream-event lambda entirely, relying solely
  on `ChatController`'s existing ordering.** Considered, since today's `ChatController::stop()`
  already erases the `in_flight_` entry — and thus drops the stored lambda — before calling
  `handle->cancel()`, so nothing should currently reach `ChatViewModel` after destruction begins.
  Rejected as the sole safeguard (kept the guard) because that correctness depends entirely on
  `ChatController`'s internal sequencing staying exactly as it is today; the guard costs one pointer
  check per event and keeps the pointer safe even if a future change to `ChatController` loosens
  that ordering. Also rejected: relying on the guard *instead of* fixing `ChatController` if a real dangling-callback bug
  were ever found — the guard is insurance, not a substitute for correct ownership; if ASan ever
  reported a use-after-free here, the fix would be in `ChatController`'s sequencing, not a bigger
  guard.
- **`QML` `Component.onDestruction` on the `WorkspaceWindow.qml` root item, instead of a C++
  `QQuickView::closing` hook, for the stop-before-destroy wiring.** Rejected: pushes a
  safety-critical, C++-object-lifetime concern (REQ-C-001 talks about `ChatController`/`Conversation`
  pointer lifetime, purely a C++ concern) into QML, adds a round-trip through the QML engine for no
  benefit, and is less discoverable for a future engineer auditing "where is shutdown safety
  enforced" than a single, clearly-commented connection in `ChatApplication.cpp`.
- **Hand-simplifying `cmake/combine-metatypes.cmake` to a single-file copy (`file(COPY ...)` or a
  one-line `configure_file`) instead of porting the general N-file JSON-array-merge script
  verbatim.** Rejected — see §4.3: the general script already handles the one-input case correctly,
  porting it unmodified keeps the two sibling repos' CMake infrastructure diffable/auditable against
  each other, and it costs nothing to leave the merge logic general for whenever
  `holonight_persistence`/`holonight_platform` eventually gain their own `QML_ELEMENT` types.

---

## 8. Known Risks

1. **Carried forward from the prior cycle** (`project_ollama_chat_backend` memory):
   `ChatController` holds a raw, non-owning `Conversation*` while a stream is in flight. This design
   satisfies REQ-C-001 by having `ChatViewModel` own `Conversation` via `shared_ptr` and calling
   `stop()` synchronously from both the `QQuickView::closing` hook and its own destructor before any
   path that could destroy `conversation_`. The residual risk is entirely in *keeping both of those
   call sites wired correctly over time* — if a future refactor removes either hook without
   replacing it, the dangling-pointer bug returns. Nothing in the type system enforces this; only
   the AddressSanitizer manual-smoke-test step (REQ-F-024's acceptance criterion) catches a
   regression here.
2. **New risk this design introduces: `MessageListModel`/`Conversation` can drift if a mutator call
   is ever missed.** Because `MessageListModel` is a manually-synchronized mirror, not a live
   projection, any future code path that mutates `conversation_` without also calling
   `appendMessage`/`updateLastMessage` (e.g., a later feature that edits a message in place through
   some new `ChatController` method) will silently desync the UI from the authoritative data — the
   `ListView` will show stale content with no error. `resetFrom()` exists as an escape hatch but nothing
   currently calls it. Mitigated only by keeping `ChatViewModel`'s mutation call sites minimal and
   fully enumerated (§6.5), and by test coverage asserting model row count/content after each
   operation (§9).
3. **`beginInsertRows`/`dataChanged` call discipline under rapid streaming.** `updateLastMessage`
   must never be called while a `beginInsertRows`/`endInsertRows` pair from a preceding
   `appendMessage` is still open, and vice versa — `QAbstractListModel` will assert/crash in debug
   Qt builds if insert/change notifications are misordered or nested. This is not a risk under the
   current design (every call site here is a single, non-reentrant, non-nested call — no signal
   emitted by `MessageListModel` itself can re-enter `ChatViewModel` synchronously), but it is a
   sharp edge for whoever implements the class, worth an explicit unit test (§9) rather than trusting
   code review alone.
4. ~~Empty-list ambiguity (REQ-F-003 vs. REQ-F-019 UI copy)~~ — resolved (2026-07-21): both cases
   now use one merged message, per SPEC.md's amended wording. No longer an open risk this cycle;
   revisit only if a future cycle extends `OllamaProvider::refresh()` to report a failure reason.

---

## 9. Test Strategy

`tests/application/test_chat_view_model.cpp`, following the existing `test_chat_controller.cpp`
fixture pattern (`FakeHttpClient` → `OllamaProvider` → now one level up, `ChatViewModel`). Added to
`tests/CMakeLists.txt`'s `test_holonight_ai` sources list; no other CMake changes needed (the test
target already links `holonight_application`, and the existing `tests/main.cpp` already constructs
a `QGuiApplication` on `QT_QPA_PLATFORM=offscreen` for the whole test binary — no per-test
`QCoreApplication`/`QQmlEngine` is required, and none is created).

**Why no event loop is needed**: every `HttpClient`/`OllamaProvider`/`ChatController` callback in the
existing test doubles fires synchronously and inline (`FakeHttpClient::emitData`/`emitFinished`/
`emitError` call the stored `std::function` directly, no `QTimer`, no queued `QMetaObject::invoke`).
`ChatViewModel`'s own `Q_PROPERTY` `NOTIFY` signals are plain `emit` calls on the same thread with no
explicit `Qt::QueuedConnection` anywhere in this design — so a `QSignalSpy` on a directly-connected
slot (or just reading the property synchronously after the triggering call returns) is sufficient.
No `QTest::qWait`/`processEvents` spinning is needed anywhere in this suite, matching
`test_chat_controller.cpp`'s existing style exactly.

Fixture, mirroring `test_chat_controller.cpp`:

```cpp
struct Fixture {
  std::shared_ptr<FakeHttpClient> http_client = std::make_shared<FakeHttpClient>();
  std::shared_ptr<OllamaProvider> provider = makeProvider(http_client);  // enqueues model-list JSON first
  ChatViewModel view_model{provider};
};
```

REQ-NF-005's eight required cases, mapped concretely:

1. **Initialization / conversation creation (REQ-F-001)** — construct `Fixture`; assert
   `view_model.conversation() != nullptr`, `conversation()->id().toString()` non-empty, and that two
   separate calls to `conversation()` return the identical pointer.
2. **Model list population (REQ-F-002, REQ-F-004)** — `FakeHttpClient` pre-loaded with a
   buffered-success `/api/tags` body (as `makeProvider` in `test_chat_controller.cpp` already does)
   before `ChatViewModel` construction (its constructor calls `refresh()` synchronously via the fake,
   so population happens inline during construction, no waiting needed); assert
   `availableModels().size() == N`, `QSignalSpy` on `availableModelsChanged` recorded exactly one
   emission, and `selectedModelId()` equals the first entry.
3. **Empty model list (REQ-F-003)** — construct with a `FakeHttpClient` that has no queued response
   (`FakeHttpClient::send` calls `on_error` — see `fake_http_client.h`); assert `availableModels()`
   is empty and `canSend()` is false.
4. **Send appends messages and flips `isStreaming` (REQ-F-005, REQ-F-006, REQ-F-007)** — call
   `send("hello")`; assert `messages()->rowCount() == 2`, `inputText().isEmpty()`, `isStreaming()` is
   true, and `canSend()` is false while streaming.
5. **Token-by-token streaming updates the model in place (REQ-F-008, REQ-F-009)** — after `send()`,
   drive `http_client->emitData(0, ...)` twice with different `content` chunks; assert
   `messages()->data(messages()->index(1), MessageListModel::TextRole)` accumulates both chunks and
   `rowCount()` stays `2` throughout (no row added/removed by a delta). Then `emitFinished(0)` /
   final `done:true` chunk; assert status role is `"complete"` and `isStreaming()` is false.
6. **Stream error handling (REQ-F-010, REQ-F-020)** — `http_client->emitError(0, "boom")`; assert
   `errorMessage() == "boom"`, last row's `StatusRole == "error"`, `canRegenerate()` is true,
   `isStreaming()` is false.
7. **Stop mid-stream (REQ-F-011, REQ-F-012, REQ-F-013)** — after `send()` and one partial
   `emitData`, call `view_model.stop()`; assert last row's `StatusRole == "cancelled"`, text retains
   the partial chunk, `isStreaming()` is false; call `stop()` a second time and assert no crash / no
   further changes (idempotence).
8. **Regenerate enablement and action (REQ-F-014, REQ-F-015, REQ-F-016)** — after a completed send,
   assert `canRegenerate()` true; call `regenerate()`, assert `messages()->rowCount()` is still `2`
   (in-place, not appended) and the row's text is cleared/status is `"streaming"`; separately,
   construct a fresh fixture with no messages sent and assert `regenerate()` is a no-op
   (`rowCount() == 0`, `isStreaming()` false, no `FakeHttpClient` streaming call recorded).
9. **Shutdown safety (REQ-F-024/C-001, exceeding the required 8)** — start a `send()`, then destroy
   the `Fixture` (or explicitly call `view_model.stop()` then let it go out of scope) mid-stream;
   run under the project's existing ASan-enabled build config and assert no crash — this is the unit
   -test-level companion to REQ-F-024's manual valgrind/ASan acceptance step, exercising the same
   code path (`ChatController::stop()` before `Conversation` destruction) deterministically and
   quickly in CI, rather than relying solely on the manual protocol.

All nine cases use only `FakeHttpClient` — zero real sockets, zero dependency on a running Ollama
instance, matching REQ-NF-005's binding requirement exactly.

---

## Document Version

- **Created**: 2026-07-21
- **Status**: Design complete, pending Stage 3 (task breakdown)
- **Traces to**: `docs/sdd/chat-window-qml/SPEC.md`
