# Chat Message Redesign — EARS Requirements Specification

## Overview

This specification defines the visual and structural redesign of chat message rendering in the holonight-ai desktop application. The changes apply uniformly to both the `WorkspaceWindow` (full chat interface) and `QuickPanel` (compact layer-shell surface), and are driven by approved mockups (`docs/mockups/main-window.png` and `docs/mockups/left-sidebar.png`). The redesign introduces: chamfered-corner message frames, assistant message headers with model information, Markdown content rendering, removal of inline status text, error/cancellation visual signals, a consolidated streaming status bar, and simplified composer controls. All implementations must reuse existing Qt6/QML patterns and mechanisms (e.g., `HnSurfaceFrame`, `encodeModelId`/`decodeModelId` encode/decode functions) to avoid duplication.

## Non-Goals

- Per-provider or per-model icon/logo system (planned for a future cycle).
- Rich code-block artifact rendering (filename header, syntax highlighting, Copy button) — deferred to a future feature.
- Any change to provider adapters, streaming protocol, or the SSE/event pipeline itself.
- Any change to how conversations, messages, or models are loaded, searched, or listed in the sidebar.

---

## Requirements

### Message Frame Visual Design

#### REQ-F-001: Message Frame Corner Treatment
**Statement:** The system shall render both user and assistant message frames with exactly three rounded corners and one chamfered corner (top-right chamfered), with a visible border at all times (not only on error) so the corner treatment reads clearly against the window background.

**Acceptance criteria:**
- Both `UserMessageCard.qml` and `MessageBubble.qml` (or their replacement components) render the frame with 3 rounded and 1 chamfered corner.
- The chamfered corner is positioned at the top-right of the frame.
- Both frames set a non-zero `borderWidth` (`HoloniightPalette.borderWidth`/`borderPassive` by default) so the shape's outline — and therefore the chamfer — is always visible, regardless of fill-color contrast against the background.
- The visual treatment is identical in both `WorkspaceWindow` and `QuickPanel` surfaces.

#### REQ-C-001: Message Frame Implementation via HnSurfaceFrame
**Statement:** The system shall reuse the existing `HnSurfaceFrame` QML component and its `chamferedCornersOverride`/`HnCornerMask` mechanism to render message frames.

**Acceptance criteria:**
- Message frame components use `HnSurfaceFrame` as the root or frame element, not a new corner-drawing primitive.
- The existing `chamferedCornersOverride` property is set to achieve the 3-rounded + 1-chamfered layout.
- No new QML drawing components or C++ corner-mask classes are introduced.

---

### Assistant Message Headers

#### REQ-F-002: Assistant Message Header Row
**Statement:** Every assistant (AI) message shall display a provider/model icon positioned outside and to the left of the message frame (not indented inside the frame's padding), and a header row inside the frame's top area containing the model name (top-left) and the message's creation timestamp (top-right).

**Acceptance criteria:**
- The icon badge is a sibling element to the left of `HnSurfaceFrame`, top-aligned with the frame, not a child inside the frame's content padding.
- The frame's internal header row (model name left, timestamp right) is rendered above the message text body, inside the frame.
- The header/icon layout matches `docs/mockups/main-window.png`'s assistant message treatment.
- The icon and header are present in both `WorkspaceWindow` and `QuickPanel` rendering.

#### REQ-F-003: Assistant Message Header Icon Placeholder
**Statement:** The assistant message icon shall display a placeholder consisting of a small solid blue circle inside a rounded-square icon-frame container.

**Acceptance criteria:**
- The icon is a solid-filled blue circle (color token to be determined during design refinement, not in this spec).
- The icon-frame container is a rounded square (not circular), sized to match the existing avatar footprint (`HoloniightPalette.controlHeight * 0.75`).
- No per-provider or per-model branding, logos, or dynamic icon content is included.

#### REQ-F-006B: Assistant Row Hidden Until First Token
**Statement:** While an assistant message has `Pending` or `Streaming` status and empty text (i.e., before the first `ContentDelta` arrives), the system shall not render its icon, header, or frame in the message list; the global streaming status bar (REQ-F-011) remains the sole progress indicator during this interval.

**Acceptance criteria:**
- `MessageBubble.qml` collapses to zero visible height when `messageRole === "assistant"`, `messageStatus` is `"pending"` or `"streaming"`, and `messageText` is empty.
- The row reappears automatically once text becomes non-empty (typically on the first `ContentDelta`), with no special-casing needed elsewhere.
- This is a QML-only visibility change; `ChatController`/`ChatViewModel`/`MessageListModel` row-append timing and indices are unaffected.

---

### Message Content Rendering

#### REQ-F-004: Markdown Content Rendering
**Statement:** Both user and assistant message bodies shall render using Qt's native Markdown support, supporting at minimum headers, bold, italic, lists, links, and inline code.

**Acceptance criteria:**
- Message `Text` elements use `textFormat: Text.MarkdownText`.
- Markdown constructs (e.g., `**bold**`, `*italic*`, `# header`, `- list item`, `[link](url)`, `` `code` ``) render correctly.
- The rendered output is visually consistent between `WorkspaceWindow` and `QuickPanel`.

#### REQ-C-002: Fenced Code Block Rendering Constraint
**Statement:** Fenced code blocks (triple-backtick) shall render as plain monospace text via the native Markdown text element, without a distinct card/widget, background frame, filename header, or copy button.

**Acceptance criteria:**
- Code blocks render as plain monospace text within the native Markdown `Text` element.
- No separate `Frame`, `Rectangle`, or custom code-block widget wraps the code.
- No filename header, syntax highlighting, or copy button is present.
- This constraint applies in both surfaces.

---

### Message Status Handling

#### REQ-F-005: Error Message Visual Indication
**Statement:** If an assistant message transitions to Error status, the message frame's border or background color shall be visually tinted to indicate an error state.

**Acceptance criteria:**
- Messages in `Message::Status::Error` display a visually distinct border/background tint (color token determined at design stage).
- The error text itself renders as the message body content (existing behavior: `ChatController` sets `Message::setText()` to the error message upon Error transition).
- The visual tinting is applied in both surfaces.

#### REQ-F-006: Cancelled Message Partial Text Display
**Statement:** A cancelled assistant message shall display accumulated partial text with no special error styling or separate status label.

**Acceptance criteria:**
- Messages in `Message::Status::Cancelled` render their accumulated text content without additional styling.
- No separate "Cancelled" label, error-tint, or visual distinction is applied beyond the partial text itself.

#### REQ-C-003: Removal of Per-Message Status Line
**Statement:** The existing inline status text under each message ("Pending", "Streaming…", "Complete", "Error", "Cancelled") shall be removed entirely from the message frame in both `WorkspaceWindow` and `QuickPanel`.

**Acceptance criteria:**
- The `showMessageStatus`/`showStatus` property chain (`ChatPanel.qml` → `MessageList.qml` → `MessageBubble.qml`) is removed as dead code.
- No status text line appears below any message in either surface.
- The message frame only shows the header row (for assistant messages), body content, and (for errors) visual frame tinting.

---

### Model Tracking in Domain and Persistence

#### REQ-F-007: Message Domain Model ID Field
**Statement:** The `holonight_domain::Message` type shall gain an optional field tracking which model generated each specific assistant reply, storing both `provider_id` and `model_name` via the existing `holonight_domain::ModelId` struct.

**Acceptance criteria:**
- `Message` has an optional `ModelId` member field (e.g., `std::optional<ModelId> model_id_`).
- The field is set only for assistant-role messages, never for User or System messages.
- The field is populated when messages are created or regenerated by `ChatController`.

#### REQ-F-008: Message Model ID SQLite Persistence
**Statement:** The `holonight_domain::Message` model ID field shall be persisted to SQLite via a new nullable `model_id` column in the `messages` table, migrated via a new schema migration.

**Acceptance criteria:**
- A new migration adds a nullable `model_id` column to the `messages` table.
- `ConversationRepositoryWorker` (or equivalent persistence layer) reads and writes the `model_id` column.
- `Message` objects loaded from SQLite correctly populate the optional `model_id` field.
- `Message` objects saved to SQLite correctly store the `model_id` (or `NULL` for User/System messages).

#### REQ-C-004: Model ID Encode/Decode Pattern Constraint
**Statement:** The system shall persist the `model_id` field using the same encode/decode string pattern already established for `conversations.last_model_id`, utilizing the `\x1F` unit-separator between `provider_id` and `model_name`.

**Acceptance criteria:**
- The encode function matches the existing `encodeModelId()` pattern found in `src/persistence/src/detail/conversation_repository_worker.cpp`.
- The decode function matches the existing `decodeModelId()` pattern.
- The `\x1F` separator is used consistently in the encoded string.
- Code reuses the existing encode/decode functions or applies the same logic.

#### REQ-F-009: MessageListModel Model Name Role
**Statement:** The `holonight_application::MessageListModel` shall expose a new QML role (e.g., `modelName`) that provides the model name without exposing `provider_id`, enabling QML to display the assistant message header without accessing internal model details.

**Acceptance criteria:**
- A new role (e.g., `"modelName"`) is registered in `MessageListModel::roleNames()`.
- QML can bind to this role to retrieve the model name for display in the assistant message header.
- The role returns an empty string or placeholder for User or System messages.

#### REQ-F-010: ChatController Sets Message Model ID
**Statement:** When `ChatController::send()` or `ChatController::regenerate()` creates or replaces an assistant `Message`, the system shall populate the message's model ID field with the `const ModelId&` parameter already received by both methods.

**Acceptance criteria:**
- New assistant `Message` objects created by `ChatController::send()` have their `model_id` field set from the method's `model` parameter.
- Regenerated assistant `Message` objects (via `ChatController::regenerate()`) have their `model_id` field updated to the provided model.
- The model ID is persisted to SQLite as part of the existing message save flow.

---

### Streaming Status Indicator and Composer Controls

#### REQ-F-011: Global Streaming Status Bar
**Statement:** While `ChatViewModel.isStreaming` is true, the system shall display a horizontal status bar positioned above the message composer (`ChatComposer.qml`) in both surfaces, containing: a status dot indicator (left), the text "Generating response…" (left-aligned), and a "Stop" button (right-aligned), rendered inside a visibly bordered container with all four corners rounded (no chamfer — unlike message frames).

**Acceptance criteria:**
- The status bar is visible in the DOM/QML hierarchy when `ChatViewModel.isStreaming === true`.
- The status bar is hidden when `ChatViewModel.isStreaming === false`.
- The status bar is positioned immediately above `ChatComposer.qml` in both `ChatPanel.qml` contexts.
- The bar contains all three elements: status dot, text, and Stop button, with correct alignment.
- The bar has a visible border (`HoloniightPalette.borderPassive`/`borderWidth`) and uniformly rounded corners (`HoloniightPalette.radiusControl`), matching `docs/mockups/main-window.png`'s status-bar treatment.
- The visual treatment is identical in both `WorkspaceWindow` and `QuickPanel`.

#### REQ-F-012: Streaming Status Bar Stop Button
**Statement:** When the user clicks the "Stop" button in the streaming status bar, the system shall invoke `ChatViewModel.stop()`.

**Acceptance criteria:**
- The Stop button in the status bar is clickable and has `onClicked` binding that calls `ChatViewModel.stop()`.
- Pressing the button produces the same effect as the existing composer Stop control.
- The button remains clickable only while the status bar is visible (i.e., while streaming).

#### REQ-F-013: Composer Button Constant "Send" Label
**Statement:** The composer's primary action button (`ChatComposer.qml`) shall always display the label "Send", regardless of streaming state.

**Acceptance criteria:**
- The button text is "Send" at all times.
- No label toggle to "Stop" occurs during streaming.
- The button text does not change in response to `ChatViewModel.isStreaming` state changes.

#### REQ-F-014: Composer Button Disabled During Streaming
**Statement:** While `ChatViewModel.isStreaming` is true, the composer's primary action button shall be disabled (not clickable, visually grayed out or equivalent).

**Acceptance criteria:**
- The button's `enabled` property is `false` when `ChatViewModel.isStreaming === true`.
- The button's `enabled` property is `true` when `ChatViewModel.isStreaming === false`.
- The button cannot be clicked while disabled (same behavior as the existing `canSend` guard).

#### REQ-C-005: Stop Control Exclusivity
**Statement:** The Stop control shall exist exclusively in the new streaming status bar; no duplicate Stop control, label toggle, or mode switch shall remain in the composer.

**Acceptance criteria:**
- The existing "Stop" label toggle or stop-mode button logic in `ChatComposer.qml` is removed.
- The composer's only action button is the "Send" button (no separate Stop button or conditional Stop/Send toggle).
- All streaming-stop functionality is consolidated into the status bar.
