# HoloNight AI

A standalone C++23/Qt6 desktop assistant application, part of the HoloNight group of projects.
Built as `holonight-chat`, a normal desktop Qt Quick window (not a Wayland layer-shell surface)
with a second, compact quick-panel window sharing the same conversation state; shell-driven
activation of the quick panel over D-Bus remains future work.

**Status: usable chat window against Ollama, OpenAI, Anthropic, and Google (Gemini), with
SQLite-backed conversation persistence, a Settings window for live per-provider configuration,
Secret Service-backed credential storage, a compact quick-panel surface, desktop notifications
for responses/errors completing while the app is out of view, native block-based Markdown/code
rendering for completed messages (fenced code blocks get syntax highlighting, a language label, and
a copy button; live theme switching updates code colors instantly), per-provider message icons
(theme-tinted for Ollama/OpenAI/Anthropic, original multi-color branding preserved for Google), and
automatic conversation-title generation (a background, non-streaming model call replaces the
synchronous fallback title with a short generated one after the first assistant response
completes; silently skipped/retried-never on failure, and never overwrites a manually-renamed
title), a Background AI settings page (default utility model, a per-feature chat-title model
override, and an on/off toggle for automatic title generation, all draft/Save/Discard like the
Providers page), and Anthropic, Google, OpenAI, and Ollama, opt-in tool calling (a read-only `ListFiles` tool restricted to
the user's home directory; each invocation/result pair renders as one compact semantic activity
with a structured directory preview, raw-data disclosure, and copy-result action).**
Domain types, Ollama, OpenAI,
Anthropic, and Google provider adapters, chat send/stop/retry orchestration (routed to whichever
provider the selected model belongs to), a QML chat window (message list, input box, model picker
unioning all four providers' models, send/stop/retry, inline error banner), durable conversation
storage (save/list/switch/rename/delete/pin conversations via a left-hand conversation list panel
with live search filtering, hover-revealed rename/delete/pin actions, pinned conversations shown in
a separate section above Recent, and a "New chat" action, each conversation remembering its own
last-used model), a three-region framed workspace (conversation
navigation, chat, and an empty right panel reserved for future content), a Settings window
(per-provider base URL/default model/temperature, Ollama also has context window, Anthropic and
Google also have max output tokens, API key/auth token, test connection, non-secret config
persisted to `config.json`, tokens persisted via Secret Service), and a quick-panel window (toggled
from the workspace window, embeds the same shared chat presentation in compact mode,
expand-to-workspace and Escape-to-hide) are implemented and tested end to end. See
[`docs/high-level-project-idea.md`](docs/high-level-project-idea.md) for the architecture
rationale and roadmap, and [`docs/adr/`](docs/adr/) for recorded decisions.

## Requirements

- Qt 6 (`Core`, `Gui`, `Quick`, `Qml`, `Network`, `Concurrent`, `Sql`, `DBus`)
- CMake 3.25+
- Ninja
- `libsecret-1` (for credential storage via the desktop Secret Service)
- [`holonight-qt`](../holonight-qt) checked out as a sibling directory (`../holonight-qt`) —
  supplies the shared `HolonightQt` CMake package and the `Holonight` QML design-system module
- GTest (optional, for tests — fetched automatically if not installed)

## Building

All workflows go through [`task`](https://taskfile.dev):

```bash
task configure    # configure CMake
task build        # build build/holonight-chat
task run          # build and launch the window
task test         # build and run unit tests
task format       # auto-format with clang-format
task format-check # check formatting without modifying files
task tidy         # run clang-tidy
task qml-lint     # lint QML files
task qmltypes-check # verify generated HolonightChat QML type metadata isn't empty
task coverage     # build with coverage and generate build/coverage/index.html
task clean        # remove build/
```

`task build:qt-dependency` (a dependency of most other tasks) builds and installs the sibling
`../holonight-qt` and `../holonight-config` checkouts at the exact revisions in Taskfile.yml.
Dependencies are built and staged under `build/dependencies/`, with provider tests/examples off
and Wayland support enabled. `HolonightQt_DIR` determines the QML path used by tests and lint.

```bash
task configure-tests
task test
```

## Runtime style selection

Runtime standard controls use `import QtQuick.Controls as Controls`; palette/primitives and
composites remain explicit `Holonight.Core` and `Holonight.Controls` APIs. The executable embeds
`:/qtquickcontrols2.conf` with `Style=Holonight`. Environment, `-style Fusion` and
`QT_QUICK_CONTROLS_CONF` overrides remain supported. The build executable uses its configured
dependency path; installed execution discovers `../lib/qt6/qml` relative to the executable
(using the configured install libdir). Activation paths are fixed at CMake configure time.
See [UQC-104 acceptance](docs/sdd/unified-qtquick-controls/SPEC.md).

For machines with limited memory, configure `-DTIDY_JOBS=2` before running the tidy target.

## Architecture

Modular monolith, static-library targets per layer under `src/`:

| Target                     | Purpose                                                         |
| --------------------------- | ---------------------------------------------------------------- |
| `holonight_domain`          | Conversation, message, model, and stream-event domain types      |
| `holonight_config`          | Non-secret provider configuration (`config.json`) load/save      |
| `holonight_application`     | Sending, retrying, cancellation, and request orchestration       |
| `holonight_providers`       | Ollama, OpenAI, Anthropic, and Google provider adapters           |
| `holonight_persistence`     | SQLite repositories and schema migrations                        |
| `holonight_credentials`     | Secret Service / KWallet credential storage (via `libsecret`)    |
| `holonight_platform`        | D-Bus activation, notifications, desktop integration              |
| `holonight_rendering`       | Markdown/code block parsing (MD4C) and syntax highlighting (KSyntaxHighlighting) |

The targets above are `STATIC` libraries with real, headlessly-tested logic:

- `holonight_domain` — `Message`, `Conversation`, `ModelId`, `StreamEvent`, and their UUID-backed
  id types (see [`docs/sdd/domain-core-types/`](docs/sdd/domain-core-types/)); the normalized
  `ToolInvocation` lifecycle and provider-neutral `ToolRequestEvent`; and lossless Invocation/Result
  `ToolCallEntry` ledger records carrying stable tool identity, correlation, execution location,
  lifecycle, timestamps, arguments, and results. The ledger remains attached to `Message` through
  `toolCalls()` for persistence and provider-history reconstruction.
- `holonight_config` — `OllamaProviderConfig` (base URL, default model, context window,
  temperature), `OpenAIProviderConfig` (base URL, default model, temperature),
  `AnthropicProviderConfig` (base URL, default model, temperature, max output tokens),
  `GoogleProviderConfig` (base URL, default model, temperature, max output tokens), and
  `UtilityConfig` (an optional `default_utility_model`, `chat_title_generation_enabled` toggle, and
  `chat_title_model_override`, used to pick and gate the model for background utility tasks such as
  title generation) — none store
  an auth token — and `ConfigRepository`, synchronous JSON load/save against
  `$XDG_CONFIG_HOME/holonight-ai/config.json`. Ordered live instances and historical tombstones
  live under the versioned `"provider_instances"` key; model caches remain keyed by instance ID,
  and the top-level `"utility"` key can select an instance-scoped utility model. Whole-state
  writes preserve unrelated root keys.
- `holonight_providers` — `OllamaProvider`, an adapter over Ollama's `/api/chat` (streaming) and
  `/api/tags` endpoints; owns an `OllamaToolCodec` that formats tool definitions as Ollama's
  `{"type":"function","function":{...}}` schema, decodes the `message.tool_calls[]` array that
  arrives atomically (not incrementally) in a single NDJSON line — each entry becomes its own
  `ToolRequestEvent`, in array order — and reconstructs `{"role":"tool","content",...,"tool_name"}`
  result messages for request history (Ollama's tool-result shape has no ID field, so correlation
  is by `tool_name`/position only; a client-synthesized `provider_call_id` is used internally but
  never sent on the wire). `OpenAIProvider`, an adapter over OpenAI's Responses API
  (`/v1/responses` streaming via SSE, `/v1/models` with fail-open denylist-based filtering of
  non-chat-completions models); owns an `OpenAIToolCodec` that decodes atomic `function_call` output
  items (arguments arrive as a JSON-encoded string, unlike Ollama/Google's pre-parsed objects) and
  replays opaque `reasoning`/`encrypted_content` items verbatim so a reasoning model's chain stays
  intact across a tool round-trip. `AnthropicProvider`, an adapter over Anthropic's Messages API
  (`/v1/messages` streaming via SSE, `/v1/models` with fail-open denylist-based filtering) — auth
  via an `x-api-key` header plus a mandatory `anthropic-version` header on every request, and
  system-role messages hoisted into the top-level `system` field rather than the `messages` array;
  owns an `AnthropicToolCodec` that translates the provider-neutral tool catalog and events to and
  from Anthropic `tool_use`/`tool_result` wire shapes, and reconstructs those shapes from
  `Message::toolCalls()` for request history.
  `GoogleProvider`, an adapter over Google's Gemini Developer API
  (`/v1beta/models/{model}:streamGenerateContent?alt=sse` streaming, `/v1beta/models` with
  fail-open denylist filtering *and* a `supportedGenerationMethods` check) — auth via an
  `x-goog-api-key` header, `user`/`model` role naming (not `assistant`), system-role messages
  hoisted into a top-level `systemInstruction` object, and no top-level `"model"` field in the
  request body (model selection lives entirely in the URL path); owns a `GoogleToolCodec` mirroring
  `AnthropicToolCodec`'s role but for Gemini's atomic (non-incremental) `functionCall` parts — a
  single streamed chunk can carry multiple `functionCall` parts for parallel tool calls, each
  emitted as its own `ToolRequestEvent` in array order — and Gemini's optional `thought_signature`/
  `id` fields on thinking-capable models, echoed verbatim on the follow-up turn only when the model
  itself supplied them (a client-synthesized correlation ID is used internally when the model omits
  `id`, but is never echoed back on the wire). All four providers are built on an
  injectable `HttpClient` interface so they're fully testable without a running server, and support
  a configurable base URL, auth token, and temperature (Ollama also supports context window;
  Anthropic and Google also support a max output tokens field).
- `holonight_application` — `ProviderInstanceRegistry` owns ordered instance identity and
  tombstones, while `ProviderRuntimeCoordinator` owns one concrete adapter per enabled instance.
  `ChatController` orchestrates send/stop/retry and per-conversation streaming state, routing each
  request by the selected `ModelId.provider_id` instance ID; `ChatViewModel` and
  `MessageListModel` expose dependent instance/model selections and `ChatController`'s API as Qt
  properties/signals for `WorkspaceWindow.qml`; `ConversationListModel`, mirroring saved
  conversations for the conversation list panel; `ProviderSettingsController`/
  `OpenAIProviderSettingsController`/`AnthropicProviderSettingsController`/
  `GoogleProviderSettingsController` (the latter three sharing common workflow via
  `ProviderSettingsControllerBase`), the QML-facing bridges for the Settings window's Providers
  page — each edits the selected `ProviderDraftSession`, uses a private disposable probe for
  Refresh models / Test connection, and applies changes to the live registry only after Save
  persists successfully. `UtilityTaskRunner` owns its own `ProviderAdapterRouter` (kept separate
  from `ChatViewModel`'s so utility-specific generation parameters — low temperature, short
  max-output — never leak into real chat calls), resynced via `applyProviderState()` whenever
  Settings persists a provider change, and fires a single one-shot, non-streaming call to generate
  a short conversation title once the first assistant response completes — model resolution tries a
  task-specific override, then `UtilityConfig.default_utility_model`, then the conversation's active
  chat model, then any enabled instance's first available model, generation is skipped entirely if
  disabled via the Background AI settings toggle, fires at most once per conversation (gated by the
  persisted `title_source` column), and any failure (missing credentials, empty/errored response,
  unknown provider) silently and permanently keeps the fallback title with no retry or user-facing
  error. `UtilitySettingsController` is the QML-facing bridge for the Background AI settings page —
  a fourth independent `ProviderAdapterRouter`/`ProviderRuntimeCoordinator` pair (after
  `ChatViewModel`, `UtilityTaskRunner`, `ProviderManagementController`) so its model pickers stay
  independent of the chat window's active provider — following the same
  draft/Save/Discard convention as the Providers page. `ToolRegistry` owns complete local tool
  packages: a provider-neutral `ToolDefinition`, asynchronous `IToolExecutor`, and semantic
  `IToolPresenter`. `ProviderAdapterRouter` passes a neutral catalog snapshot to supporting
  providers; `ToolOrchestrator` publishes Running and exactly one terminal outcome; and
  `ChatController` dispatches only local-client requests, caps a turn at 10 calls, and retains
  lossless Invocation/Result ledger messages. `ListFiles` is the first package and remains a
  read-only, home-directory-confined operation — see
  [`docs/sdd/tool-activity-rendering/`](docs/sdd/tool-activity-rendering/).
- `holonight_persistence` — `SqliteConversationRepository`, a worker-thread-backed
  `ConversationRepository` implementation (SQLite via `Qt6::Sql`, never blocking the GUI thread),
  with a small forward-only SQL migration runner (`MigrationRunner`, migrations embedded as Qt
  resources under `src/persistence/migrations/`). Each conversation also carries a nullable
  `pinned_at` column (`pinConversation()`/`unpinConversation()` set/clear it; pinned conversations
  sort before unpinned ones, most-recently-pinned first, unaffected by rename/message activity) and
  a `title_source` column (`Fallback`, `Generated`, `Manual`) that gates automatic title generation: new
  conversations are born `Fallback` (eligible for generation), a successful generation flips them
  to `Generated`, and any manual rename flips them to `Manual` permanently — conversations that
  existed before this column was added were backfilled to `Manual` so old titles are never
  retroactively replaced. Conversations and messages persist to
  `$XDG_DATA_HOME/holonight-ai/conversations.db` (falling back to `~/.local/share`); if the
  database can't be opened, the app falls back to in-memory-only mode with a sticky inline banner
  rather than crashing. A `usage` table records one row per assistant message (token counts and
  client-measured `duration_ms`, plus Ollama's supplementary server-measured timings), surfaced in
  the workspace window as a compact footer badge row and info popup on each completed assistant
  message (see [`docs/sdd/usage-cost-tracking/`](docs/sdd/usage-cost-tracking/) for the backend and
  [`docs/sdd/response-footer-stats/`](docs/sdd/response-footer-stats/) for the UI — the latter's
  SPEC.md also documents why per-message cost/pricing was built then removed: no provider API
  exposes pricing data, and a hand-maintained price table wasn't a workable substitute). Each
  message also carries a nullable `tool_calls` column (backward-compatible JSON-encoded
  `ToolCallEntry` list) for tool-calling turns. Lifecycle and identity metadata round-trip when
  present; legacy rows remain readable; and unmatched nonterminal invocations restore as
  Cancelled rather than appearing to run forever.
- `holonight_credentials` — `SecretServiceCredentialStore`, a worker-thread-backed `CredentialStore`
  implementation storing one opaque API-key/secret string per provider instance ID via the desktop
  Secret Service (`libsecret`), never blocking the GUI thread. Wired into the Settings window's
  Ollama, OpenAI, Anthropic, and Google auth-token/API-key fields via `ProviderSettingsController`/
  `OpenAIProviderSettingsController`/`AnthropicProviderSettingsController`/
  `GoogleProviderSettingsController` (all four share the same `CredentialStore` instance).
- `holonight_rendering` — `MessageContentParser`, splitting a completed message's raw text into
  `ContentBlock`s (Markdown prose vs. fenced code, via MD4C) once per message and caching the
  result on `MessageListModel::ContentBlocksRole`; `CodeHighlighter`, a `KSyntaxHighlighting`-backed
  `QSyntaxHighlighter` subclass attached to each code block's `TextEdit`, resolving a
  `KSyntaxHighlighting::Definition` from the block's (normalized) language and reloading its
  `Theme` on `HoloniightPalette::paletteChanged` without re-parsing the code. `MessageBubble.qml`
  renders the plain `Text.MarkdownText` element while a message is `pending`/`streaming` and swaps
  to the block-based `Repeater`/`HnCodeBlock`/`MarkdownBlock` renderer once it's `complete`/`error`.
  `MessageListModel` pairs tool Invocation/Result ledger messages by provider call ID into one
  chronological activity row. `ToolActivityCard.qml` supplies shared lifecycle, disclosure,
  accessibility, raw-data, and copy controls; `ToolRendererRegistry.qml` selects
  `ListFilesToolContent.qml` for `filesystem.list` and falls back to `GenericToolContent.qml` for
  unknown, historical, provider-hosted, or malformed tool data. Expanded content is instantiated
  only while the activity is open.

See [`docs/sdd/ollama-chat-backend/`](docs/sdd/ollama-chat-backend/),
[`docs/sdd/chat-window-qml/`](docs/sdd/chat-window-qml/),
[`docs/sdd/sqlite-conversation-persistence/`](docs/sdd/sqlite-conversation-persistence/),
[`docs/sdd/secret-service-credentials/`](docs/sdd/secret-service-credentials/),
[`docs/sdd/provider-settings-ui/`](docs/sdd/provider-settings-ui/),
[`docs/sdd/openai-provider-adapter/`](docs/sdd/openai-provider-adapter/),
[`docs/sdd/anthropic-provider-adapter/`](docs/sdd/anthropic-provider-adapter/),
[`docs/sdd/google-provider-adapter/`](docs/sdd/google-provider-adapter/),
[`docs/sdd/three-panel-window-layout/`](docs/sdd/three-panel-window-layout/),
[`docs/sdd/left-sidebar-panel/`](docs/sdd/left-sidebar-panel/),
[`docs/sdd/desktop-notifications/`](docs/sdd/desktop-notifications/),
[`docs/sdd/advanced-markdown-rendering/`](docs/sdd/advanced-markdown-rendering/),
[`docs/sdd/provider-icons/`](docs/sdd/provider-icons/),
[`docs/sdd/utility-task-runner/`](docs/sdd/utility-task-runner/),
[`docs/sdd/background-ai-settings-panel/`](docs/sdd/background-ai-settings-panel/),
[`docs/sdd/usage-cost-tracking/`](docs/sdd/usage-cost-tracking/),
[`docs/sdd/response-footer-stats/`](docs/sdd/response-footer-stats/),
[`docs/sdd/conversation-pinning/`](docs/sdd/conversation-pinning/),
[`docs/sdd/tool-calling-listfiles/`](docs/sdd/tool-calling-listfiles/),
[`docs/sdd/tool-activity-rendering/`](docs/sdd/tool-activity-rendering/),
[`docs/sdd/google-tool-calling-listfiles/`](docs/sdd/google-tool-calling-listfiles/),
[`docs/sdd/openai-tool-calling-listfiles/`](docs/sdd/openai-tool-calling-listfiles/), and
[`docs/sdd/ollama-tool-calling-listfiles/`](docs/sdd/ollama-tool-calling-listfiles/) for the
spec/design behind the backend, QML, persistence, credentials, settings UI, workspace framing,
conversation list panel, desktop notifications, Markdown/code block rendering, per-provider message
icons, automatic title generation, the Background AI settings page, per-message token/duration
tracking, conversation pinning, Anthropic tool calling, semantic tool-activity rendering, Google tool
calling, OpenAI tool calling, and Ollama tool calling.

`holonight_platform` is a `STATIC` library holding OS/desktop-integration code with no QML surface
of its own: `PanelSurface` (the `wlr-layer-shell` quick-panel surface — see
[`docs/sdd/quick-panel-window/`](docs/sdd/quick-panel-window/)) and `DesktopNotifier` (a thin
`org.freedesktop.Notifications` D-Bus client — see
[`docs/sdd/desktop-notifications/`](docs/sdd/desktop-notifications/)). `ChatApplication`
(`apps/chat/app/ChatApplication.cpp`) owns both, and fires a notification when the currently-open
conversation's response completes or errors while neither the workspace window nor the quick panel
is visible; clicking a notification raises the workspace window.

`apps/chat/` builds the `holonight-chat` executable and owns the QML module (`HolonightChat`,
resource prefix `/`). QML sources live at the top level under `qml/shared/` (components shared
between surfaces), `qml/workspace/` (the full window, including the conversation list panel and the
Settings window — all implemented, backed by `ChatViewModel`/`ProviderSettingsController`/
`OpenAIProviderSettingsController`/`AnthropicProviderSettingsController`/
`GoogleProviderSettingsController`), and `qml/quickpanel/`
(the compact quick-panel window — a second top-level window instantiated alongside the workspace
window in `ChatApplication.cpp`, sharing the same `QQmlEngine`/singletons, toggled via a button in
the workspace window; see [`docs/sdd/quick-panel-window/`](docs/sdd/quick-panel-window/)) —
per-directory layout, not flat `.qml` files, following `holonight-shell`'s convention. Because
`ChatViewModel`/`MessageListModel`/`ConversationListModel`/`ProviderSettingsController`/
`OpenAIProviderSettingsController`/`AnthropicProviderSettingsController`/
`GoogleProviderSettingsController`/`UtilitySettingsController` are `QML_ELEMENT` types
living in a static library rather than the executable target, `apps/chat/CMakeLists.txt` also
extracts and merges their metatypes manually
(`qt6_extract_metatypes` + `cmake/combine-metatypes.cmake` + `_qt_internal_qml_type_registration`)
— see that file's comments (including why `INTERFACE_SOURCES` must be cleared both before *and*
after `qt6_extract_metatypes()`) and `scripts/check-qmltypes.sh` (`task qmltypes-check`) before
adding another QML type to a static-library module.

## Configuration

Provider settings persist to `$XDG_CONFIG_HOME/holonight-ai/config.json` (falling back to
`~/.config`) and are editable from Settings > Providers. A fresh installation has no configured
instances; Add provider creates an enabled draft with a new immutable UUID. Multiple instances of
the same provider type are supported. Edits, credentials, enable/disable, and new instances remain
draft-only until Save; leaving a dirty draft offers Save, Discard, or Cancel.

Disabled instances remain configurable but are excluded from new chat and utility work. Deletion
requires confirmation, is blocked during an active stream, and removes runtime/config/cache state
without rewriting conversations. A minimal non-secret tombstone preserves the deleted instance's
last saved name, type, and ID so historical messages retain their attribution and icon.

Auth tokens/API keys are never written to `config.json` — they live exclusively in the desktop
Secret Service, keyed by provider instance ID. See
[`docs/provider-configuration.md`](docs/provider-configuration.md) for identity, migration, draft,
disable/delete, and fallback semantics, plus
[`docs/config.example.json`](docs/config.example.json) for the complete version-1 JSON shape.

Each Anthropic, Google, OpenAI, and Ollama instance also has an "Enable Tool Calling" toggle (off by
default) gating a built-in, read-only `ListFiles` tool restricted to paths under the user's home
directory. See [`docs/sdd/tool-activity-rendering/`](docs/sdd/tool-activity-rendering/) for the
current architecture, [`docs/sdd/tool-calling-listfiles/`](docs/sdd/tool-calling-listfiles/) for the
original Anthropic feature,
[`docs/sdd/google-tool-calling-listfiles/`](docs/sdd/google-tool-calling-listfiles/) for Google's,
[`docs/sdd/openai-tool-calling-listfiles/`](docs/sdd/openai-tool-calling-listfiles/) for OpenAI's,
and [`docs/sdd/ollama-tool-calling-listfiles/`](docs/sdd/ollama-tool-calling-listfiles/) for
Ollama's.

Tool policy will evolve along three independent axes. **Availability** decides whether a provider,
model, and application build can advertise a tool. **Permission** decides whether a particular
invocation may execute and whether approval is required. **Disclosure** decides which semantic and
raw details are shown before, during, and after execution. The current Anthropic toggle controls
availability only; `ListFiles` executes without an approval workflow, while raw arguments/results
remain behind explicit disclosure. Future policy work must not collapse these axes into one flag.

A top-level `"utility"` key in the same `config.json` holds `UtilityConfig`: `default_utility_model`
(provider instance ID + model name) picks the model used for background utility tasks (currently
just conversation-title generation); `chat_title_generation_enabled` toggles automatic title
generation on/off (absent ⇒ enabled, so upgraded configs keep working); `chat_title_model_override`
picks a task-specific model just for title generation, taking priority over `default_utility_model`
when set. All three are editable from Settings > Background AI (draft-only until Save, matching the
Providers page's Save/Discard convention) — see
[`docs/sdd/background-ai-settings-panel/`](docs/sdd/background-ai-settings-panel/) for the spec and
design. Model resolution for a given task tries, in order: a task-specific override, the default
utility model, the conversation's active chat model, then the first available model of any enabled
instance — falling through silently (no error) at each unusable tier.

## License

GPL-3.0-or-later — see [LICENSE](LICENSE).
