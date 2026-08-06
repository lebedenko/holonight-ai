# SDD Tasks — chat-message-redesign

- [x] T-001: Add model_id field to Message domain type
  - REQs: REQ-F-007
  - Check: Message class has std::optional<ModelId> model_id_ member with modelId() getter and setModelId() setter, using the 6th constructor parameter defaulting to std::nullopt.

- [x] T-002: Create 0002_add_message_model_id.sql migration file
  - REQs: REQ-F-008
  - Check: File src/persistence/migrations/0002_add_message_model_id.sql contains ALTER TABLE messages ADD COLUMN model_id TEXT;

- [x] T-003: Register migration 0002 in MigrationRunner::builtInMigrations()
  - REQs: REQ-F-008
  - Check: MigrationRunner::builtInMigrations() includes a Migration entry with version 2 reading from 0002_add_message_model_id.sql resource.

- [x] T-004: Add migration file to persistence CMakeLists.txt resource list
  - REQs: REQ-F-008
  - Check: src/persistence/CMakeLists.txt qt_add_resources() FILES list includes migrations/0002_add_message_model_id.sql.

- [x] T-005: Update ConversationRepositoryWorker to persist and restore model_id
  - REQs: REQ-F-008, REQ-C-004
  - Check: persistNewMessage() and persistMessageSettled() bind model_id via encodeModelId(), messageFromRecord() decodes model_id via decodeModelId(), and loadConversation() SELECT includes the model_id column.

- [x] T-006: Update ChatController::send() to pass model parameter to Message constructor
  - REQs: REQ-F-010
  - Check: Assistant Message in send() is constructed with six arguments including QDateTime{} and model: Message(MessageId::generate(), MessageRole::Assistant, QString(), MessageStatus::Pending, QDateTime{}, model).

- [x] T-007: Update ChatController::regenerate() to pass model parameter to Message constructor
  - REQs: REQ-F-010
  - Check: Regenerated Message in regenerate() is constructed with six arguments including model: Message(messages.back().id(), MessageRole::Assistant, QString(), MessageStatus::Pending, messages.back().createdAt(), model).

- [x] T-008: Add ModelNameRole to MessageListModel roles and data flow
  - REQs: REQ-F-009
  - Check: MessageListModel::Roles enum includes ModelNameRole, roleNames() registers {ModelNameRole, "modelName"}, and toRow() populates model_name from message.modelId()->model_name or empty string for User/System messages.

- [x] T-009: Update MessageListModel::updateLastMessage to emit ModelNameRole
  - REQs: REQ-F-009
  - Check: updateLastMessage() includes ModelNameRole in the dataChanged() role list.

- [x] T-010: Redesign MessageBubble.qml root as HnSurfaceFrame with chamfered corner
  - REQs: REQ-C-001, REQ-F-001
  - Check: MessageBubble.qml root is HnSurfaceFrame with chamferedCornersOverride: HnCornerMask.TopRight, isUser branch removed entirely, status line removed, and new required properties modelName and createdAt declared.

- [x] T-011: Add chamfered corner override to UserMessageCard.qml
  - REQs: REQ-C-001, REQ-F-001
  - Check: UserMessageCard.qml HnSurfaceFrame has chamferedCornersOverride: HnCornerMask.TopRight set.

- [x] T-012: Update MessageList.qml delegate to expose modelName role
  - REQs: REQ-F-009
  - Check: MessageList delegate Item declares required property string modelName; MessageBubble instantiation passes modelName: modelName and createdAt: createdAt.

- [x] T-013: Add header row to MessageBubble with icon, model name, and timestamp
  - REQs: REQ-F-002, REQ-F-003
  - Check: MessageBubble header RowLayout contains icon-frame with solid blue circle, model name text bound to modelName property, and timestamp text bound to createdAt property, visible only when messageRole === "assistant".

- [x] T-014: Switch MessageBubble text rendering to Markdown format
  - REQs: REQ-F-004, REQ-C-002
  - Check: MessageBubble body Text has textFormat: Text.MarkdownText and onLinkActivated: link => Qt.openUrlExternally(link); fenced code blocks render as monospace text within the Text element without separate frame or wrapper.

- [x] T-015: Add error-state visual tinting to MessageBubble frame
  - REQs: REQ-F-005
  - Check: MessageBubble frame applies borderColor: HoloniightPalette.error and borderWidth: HoloniightPalette.focusBorderWidth when messageStatus === "error".

- [x] T-016: Remove per-message status text line from messages
  - REQs: REQ-C-003
  - Check: MessageBubble contains no Text element displaying "Pending", "Streaming…", "Complete", "Error", or "Cancelled" status labels.

- [x] T-017: Remove showMessageStatus property chain from MessageList and ChatPanel
  - REQs: REQ-C-003
  - Check: MessageList.qml no longer declares showMessageStatus property; ChatPanel.qml no longer has showMessageStatus binding; MessageBubble instantiation does not bind showStatus.

- [x] T-018: Create StreamingStatusBar.qml component
  - REQs: REQ-F-011, REQ-F-012
  - Check: qml/shared/StreamingStatusBar.qml exists with a Rectangle containing RowLayout with status dot, "Generating response…" text, and Stop button calling ChatViewModel.stop(), with visibility bound to ChatViewModel.isStreaming.

- [x] T-019: Insert StreamingStatusBar into ChatPanel
  - REQs: REQ-F-011, REQ-F-012
  - Check: ChatPanel.qml ColumnLayout contains StreamingStatusBar with Layout.fillWidth: true positioned between MessageList and ChatComposer.

- [x] T-020: Update ChatComposer button text to constant "Send" label
  - REQs: REQ-F-013
  - Check: ChatComposer primary action button text is always qsTr("Send") independent of ChatViewModel.isStreaming state.

- [x] T-021: Disable ChatComposer button during streaming
  - REQs: REQ-F-014
  - Check: ChatComposer Send button has enabled: !ChatViewModel.isStreaming && ChatViewModel.canSend binding.

- [x] T-022: Remove Stop button and mode-switching logic from ChatComposer
  - REQs: REQ-C-005
  - Check: ChatComposer no longer contains a Stop button or conditional text toggle between "Stop" and "Send".

- [x] T-023: Run full test suite and verify no regressions
  - REQs: REQ-F-001, REQ-F-002, REQ-F-003, REQ-F-004, REQ-F-005, REQ-F-006, REQ-F-007, REQ-F-008, REQ-F-009, REQ-F-010, REQ-F-011, REQ-F-012, REQ-F-013, REQ-F-014, REQ-C-001, REQ-C-002, REQ-C-003, REQ-C-004, REQ-C-005
  - Check: `task test` completes successfully with all tests passing and no new clang-tidy warnings introduced.

- [x] T-024: Post-implementation visual bugfix — make message frame borders always visible
  - REQs: REQ-F-001
  - Check: `UserMessageCard.qml` and `MessageBubble.qml` both set a non-zero default `borderWidth`/`borderColor` on their `HnSurfaceFrame`, so the chamfer is visible without relying on fill-color contrast alone.

- [x] T-025: Post-implementation visual bugfix — move assistant icon outside the message frame
  - REQs: REQ-F-002, REQ-F-003
  - Check: `MessageBubble.qml`'s icon badge is a sibling `RowLayout` item to the left of `HnSurfaceFrame` (not inside its content padding), rendered as a rounded-square container (not circular) with the blue-circle placeholder inside; model name + timestamp remain the frame's internal header row.

- [x] T-026: Post-implementation visual bugfix — bordered, all-rounded-corner chrome for StreamingStatusBar
  - REQs: REQ-F-011
  - Check: `StreamingStatusBar.qml`'s `Rectangle` sets `border.color`/`border.width` (uniform `radius`, no chamfer), matching the cropped mockup reference.

- [x] T-027: Hide assistant message row until first token arrives
  - REQs: REQ-F-006B
  - Check: `MessageBubble.qml` computes `isWaitingForFirstToken` for empty assistant messages in either Pending or Streaming status and sets `visible`/`implicitHeight` to collapse the row while true.

- [x] T-028: Re-verify build, qmllint, and full test suite after visual bugfixes
  - REQs: REQ-F-001, REQ-F-002, REQ-F-003, REQ-F-011, REQ-F-006B
  - Check: `task build`, `task qml-lint`, and `task test` all pass with zero errors/warnings after T-024–T-027.

- [x] T-029: Double inter-message spacing, in-frame padding, and header-to-content spacing; fix icon to 32x32
  - REQs: REQ-F-001, REQ-F-002, REQ-F-003
  - Check: `MessageList.qml`'s `ListView.spacing`, both cards' `anchors.margins`, and both cards' header/content `ColumnLayout.spacing` are doubled from their prior values; `MessageBubble.qml`'s `iconSize` is a literal `32`; frame width/height formulas updated to account for the doubled margins.

- [x] T-030: Tighten icon-to-frame spacing and halve message frame border opacity
  - REQs: REQ-F-001, REQ-F-002
  - Check: `MessageBubble.qml`'s `iconSpacing` reduced from `controlPadding / 2` to `controlPadding / 4`; both `MessageBubble.qml` and `UserMessageCard.qml` compute a `subtleBorderColor` (alpha halved via `Qt.rgba`) and use it as the default (non-error) `borderColor`.
