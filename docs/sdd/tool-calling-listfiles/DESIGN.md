# Tool-Calling Framework (ListFiles Tool) — DESIGN.md

## Reading Guide

This document designs **how** to build the generic tool-calling framework and the `ListFiles`
tool defined in `docs/sdd/tool-calling-listfiles/SPEC.md` (20 EARS requirements — REQ-F-001
through REQ-F-011, REQ-NF-001 through REQ-NF-003, REQ-C-001 through REQ-C-006). It does not
relitigate scope: `ListFiles` is the only tool, only the Anthropic provider is wired, there is no
approval gate, and `$HOME` is hardcoded — all per the SPEC's constraints section.

All file paths, struct names, and signatures below were verified against the current codebase
(commit `288840c`, branch `main`) rather than assumed. Anthropic Messages API wire shapes
(`tool_use`, `tool_result`, streaming delta events) were independently verified against current
Anthropic API documentation via Context7 during this design pass — notably that the API itself
combines consecutive same-role messages, and that `tool_result`'s `is_error` field is documented
as required, not optional.

---

## 1. Component Overview

| Component | Module | New/Modified | Rationale |
|---|---|---|---|
| `ITool` interface | `holonight_application` | New | Pure orchestration contract; no filesystem or network dependency of its own. Lives next to `ChatController`, which is the only consumer of the abstraction. |
| `ToolRegistry` | `holonight_application` | New | Owns tool lifetime/lookup; instantiated once at the same composition root that wires `ChatController` and `ProviderAdapterRouter` together. Must NOT live in `holonight_providers` (see §5 dependency-direction note) or `holonight_domain` (it's a runtime service, not a value type). |
| `ListFilesTool` | `holonight_application` | New | Implements `ITool`. Filesystem-only (`std::filesystem`), no Qt network client needed, so it does not belong in `holonight_providers`. Grouped under a new `tools/` subdirectory of `holonight_application` alongside `ITool`/`ToolRegistry`. |
| `Message::tool_calls_` field + `ToolCallEntry` type | `holonight_domain` | Modified (`message.h`) | Amends REQ-F-005 per the SPEC's Superseded Requirements section. Domain types are the shared vocabulary between persistence, providers, and QML; the field must live where `Message` already lives. |
| `StreamEvent::ToolCall` variant | `holonight_domain` | Modified (`stream_event.h`) | Amends REQ-F-020 per the SPEC's Superseded Requirements section. Same rationale — `StreamEvent` is a domain type. |
| `AnthropicProvider` tools wiring (SSE parsing, request-body `tools`, history reconstruction) | `holonight_providers` | Modified (`anthropic_provider.h/.cpp`) | Anthropic wire-format knowledge is adapter-local, matching the existing pattern (e.g. `roleToAnthropicString`, `extractAnthropicErrorMessage` are already private to this file). |
| `ChatController` tool-call loop | `holonight_application` | Modified (`chat_controller.h/.cpp`) | `handleStreamEvent()` is already the SPEC's named interception point (context bullet 4). |
| `ProviderAdapterRouter` tools-array assembly | `holonight_application` | Modified (`provider_adapter_router.h/.cpp`) | Already the layer that resolves per-instance config before calling into a provider adapter; the natural place to decide *whether* to pass a tools array. |
| `AnthropicProviderConfig.tool_calling_enabled` | `holonight_config` | Modified (`provider_config.h`, `config_repository.cpp`) | Per-instance settings already live here (REQ-F-009/010). |
| `AnthropicProviderSettingsController.toolCallingEnabled` | `holonight_application` | Modified (`anthropic_provider_settings_controller.h/.cpp`) | Existing QML-facing settings controller; same `Q_PROPERTY` pattern as `temperature`/`maxOutputTokens`. |
| Migration `0008_add_message_tool_calls.sql` + worker mapping | `holonight_persistence` | New + Modified | New nullable column on `messages`; worker mapping functions extended (see §7). |
| Tool-call/result message rendering | QML (`qml/workspace/`) | Modified | New branch in the existing message-list delegate dispatch (see §9). |

**Module layering preserved:** `holonight_providers` never depends on `holonight_application`
(the reverse is already true throughout the codebase — `ChatController` in `holonight_application`
depends on the provider adapters, not vice versa). `ToolRegistry` therefore cannot be referenced
directly by `AnthropicProvider`; the tools JSON array crosses the boundary as a plain `QJsonArray`
function parameter, computed by `ProviderAdapterRouter` (which *does* live in
`holonight_application`, alongside `ToolRegistry`) and handed down into
`AnthropicProvider::sendChat()`. This is the same "pass data down, don't reach up" pattern already
used for `model`/`history`/`on_event`.

---

## 2. `ITool` Interface + `ToolRegistry`

New files: `src/application/include/holonight_application/tools/i_tool.h`,
`src/application/include/holonight_application/tools/tool_registry.h`,
`src/application/src/tools/tool_registry.cpp`.

### `ITool`

```cpp
namespace holonight_application {

class ITool {
 public:
  virtual ~ITool() = default;

  // Stable, unique identifier sent to Anthropic as tools[].name and matched against tool_use
  // blocks' "name" field on the way back in. Must not change across releases once a tool ships.
  [[nodiscard]] virtual QString name() const = 0;

  // Human-readable sentence(s) sent as tools[].description — the model's only signal for *when*
  // to call this tool, so this is a functional field, not documentation. (Beyond REQ-F-002's
  // minimum method list; required by the real Anthropic wire format — see §5.)
  [[nodiscard]] virtual QString description() const = 0;

  // JSON Schema v7 object describing the tool's input parameters — becomes tools[].input_schema
  // verbatim (REQ-NF-001).
  [[nodiscard]] virtual QJsonObject schema() const = 0;

  // Synchronous, never throws (REQ-F-005(2), REQ-NF-002(1)). Implementations must catch every
  // exception/filesystem error internally and return a structured {"error": {...}} object instead
  // of letting anything escape — ToolRegistry::invoke() does not wrap execute() in a try/catch as
  // a safety net; each ITool is solely responsible for its own error containment.
  [[nodiscard]] virtual QJsonObject execute(const QJsonObject& parameters) = 0;
};

}  // namespace holonight_application
```

### `ToolRegistry`

```cpp
namespace holonight_application {

// REQ-F-006. Named exactly as the SPEC's acceptance criteria phrase it ("MAX_TOOL_CALLS_PER_TURN")
// for grep-ability from the spec/tests, but spelled kMaxToolCallsPerTurn to follow this codebase's
// own constant convention (see e.g. kModelDiscoveryTimeout in anthropic_provider.cpp) — the two
// names refer to the same constant; there is no second, ALL_CAPS symbol.
inline constexpr int kMaxToolCallsPerTurn = 10;

class ToolRegistry {
 public:
  // Last-registration-wins is deliberately NOT the behavior: registering a duplicate name is a
  // programming error (two tools racing for one wire identity), caught by a debug assertion.
  // Only ever called at startup (REQ-NF-003(3): "tools are registered once at startup and not
  // dynamically added/removed during runtime"), so this is not a runtime hot path.
  void registerTool(std::shared_ptr<ITool> tool);

  // Registration order (REQ-NF-003) — a std::vector, not QHash, is the source of iteration order;
  // the QHash below is purely a lookup index.
  [[nodiscard]] const std::vector<std::shared_ptr<ITool>>& tools() const;

  [[nodiscard]] std::shared_ptr<ITool> find(const QString& name) const;  // nullptr if unknown

  // tools[] array for the Anthropic request body: {"name","description","input_schema"} per tool,
  // in registration order (REQ-NF-003).
  [[nodiscard]] QJsonArray toAnthropicToolsArray() const;

  // Generic name-based dispatch (REQ-F-001(2)): the ONLY call site ChatController needs, with zero
  // `if (name == "ListFiles")` branches. If `name` is not registered (should not happen — Anthropic
  // only ever offers tools this registry itself advertised — but a model could hypothetically
  // hallucinate a name), returns a structured error object shaped exactly like ListFiles' own error
  // results (kInvalidTool code) rather than crashing or returning std::nullopt.
  [[nodiscard]] QJsonObject invoke(const QString& name, const QJsonObject& parameters) const;

 private:
  std::vector<std::shared_ptr<ITool>> tools_;
  QHash<QString, std::shared_ptr<ITool>> by_name_;
};

}  // namespace holonight_application
```

### Registration / DI point

`ToolRegistry` is instantiated once in `apps/chat`'s composition root — the same place that
already constructs `ProviderAdapterRouter` and wires it into `ChatController` (mirroring how
`Clock` is already constructor-injected into `ChatController` today). `ListFilesTool` is
constructed and registered there:

```cpp
auto toolRegistry = std::make_shared<holonight_application::ToolRegistry>();
toolRegistry->registerTool(std::make_shared<holonight_application::ListFilesTool>());
// ... passed into both ProviderAdapterRouter (to build tools[]) and ChatController
// (to execute tool_use blocks) as a new constructor parameter on each.
```

A new tool in a future cycle needs exactly one line added at this call site plus its own `ITool`
implementation — no change to `ChatController` or `ProviderAdapterRouter` dispatch logic
(REQ-F-001(3)).

---

## 3. `ListFilesTool`

New files: `src/application/include/holonight_application/tools/list_files_tool.h`,
`src/application/src/tools/list_files_tool.cpp`.

### Path canonicalization: `std::filesystem`, not `QFileInfo`

Decision: use `std::filesystem::canonical(path, error_code)` (the non-throwing overload — REQ-F-005(2)
forbids exceptions escaping `execute()`, so the throwing overload would need to be wrapped in a
`try/catch` anyway; the `error_code` overload avoids exception-based control flow for an *expected*
condition).

Rejected alternative: `QFileInfo::canonicalFilePath()`. It resolves symlinks and `..`/`.` correctly,
but returns an empty `QString` on any failure without discriminating *why* (nonexistent path vs.
permission-denied on an intermediate directory vs. a filesystem error) — building the four-way error
taxonomy in REQ-F-005 would require re-deriving the failure reason with a second syscall anyway.
`std::filesystem::canonical`'s `std::error_code` output carries the underlying `errno`
(`ec == std::errc::no_such_file_or_directory`, `ec == std::errc::permission_denied`, etc.), which
maps directly onto the error taxonomy below. `std::filesystem` is also already idiomatic for this
C++23 codebase.

Steps inside `ListFilesTool::execute()`:

1. Read `parameters["path"]` as a `QString`; if missing or not a string, expand nothing — return
   `INVALID_PARAMETERS` (see error taxonomy).
2. Convert to `std::filesystem::path` (`.toStdString()`); if it starts with `~`, expand to
   `$HOME` first (Qt's `QDir::homePath()` — see below — handles `~` expansion implicitly if the
   raw string is passed through `QDir::homePath() + path.mid(1)`, or the tool can do the
   substitution itself before handing off to `std::filesystem`).
3. `auto canonical = std::filesystem::canonical(requested, ec);`
   - `ec == std::errc::no_such_file_or_directory` → error (see taxonomy: `NOT_FOUND`).
   - `ec == std::errc::permission_denied` (an intermediate path component isn't traversable) →
     `PERMISSION_DENIED`.
   - any other `ec` → treated as `NOT_FOUND` (fail closed; never crash).
4. `std::filesystem::is_directory(canonical, ec)` — if `false` → `NOT_A_DIRECTORY`. If `ec` is set
   here (e.g. a symlink loop slipped past step 3, or a race where the path was removed between
   steps) → `NOT_FOUND`.
5. **Boundary check, performed strictly after canonicalization** (REQ-F-004(1)/(3) — this is what
   defeats a symlink pointing outside `$HOME`): compare `canonical` against a canonicalized,
   cached `$HOME` (`std::filesystem::canonical(homePath)`, computed once at tool construction —
   REQ-C-003 says `$HOME` is hardcoded per-process, not reconfigurable, so computing it once is
   correct, not a caching risk). A path is in-bounds iff `canonical == home` or `canonical`'s
   string representation starts with `home.string() + separator` (exact, case-sensitive comparison
   per REQ-F-004(4) — no `QString::compare(Qt::CaseInsensitive)` anywhere in this path).
   Out-of-bounds → same wire error as `NOT_FOUND` (see taxonomy rationale below).
6. `std::filesystem::directory_iterator(canonical, ec)` — iterate immediate children only
   (REQ-C-006, no recursion). For each entry: skip if `entry.path().filename()` starts with `.`
   (REQ-F-003(3)). Classify type via `entry.is_directory(ec)` (follows symlinks — a symlink to a
   directory is reported as `"directory"`, matching `ls -l` intuition); if that call itself errors
   (e.g. a broken symlink), classify as `"file"` rather than aborting the whole listing — one bad
   entry must not fail the entire tool call.
7. Sort entries by `name`, ascending, case-sensitive (`QString::localeAwareCompare` is
   deliberately NOT used — plain byte-wise ordering keeps REQ-F-003(4)'s "consistent order"
   independent of the process locale).
8. Build `{"entries": [{"name":..., "type": "file"|"directory"}, ...]}` and return.

### `$HOME` resolution

`QDir::homePath()` is used to obtain the raw home-directory string (not a bare `getenv("HOME")`
call) — Qt already handles the cross-platform fallback when `$HOME` is unset (falls back to
`QStandardPaths`/passwd-entry lookups on Linux), which is more robust than trusting the environment
variable to always be present. The resulting `QString` is converted to `std::filesystem::path` once
and canonicalized once at `ListFilesTool` construction time; every `execute()` call reuses that
cached, canonical `$HOME` root.

### JSON Schema (REQ-NF-001)

```json
{
  "type": "object",
  "properties": {
    "path": {
      "type": "string",
      "description": "Directory path to list, relative to or within the user's home directory (e.g. '~/Documents', '.', 'projects/foo')."
    }
  },
  "required": ["path"]
}
```

### Error-code taxonomy

Four *conditions* are named in REQ-F-005, but only **three distinct wire codes** are returned:

| Condition | Wire `code` | Rationale |
|---|---|---|
| Path does not exist | `NOT_FOUND` | — |
| Path resolves outside `$HOME` | `NOT_FOUND` (same code, same message text) | **Deliberate collapse.** REQ-F-004(3) states out-of-bounds paths "are rejected with the same error format as a non-existent path, preventing user enumeration." Returning a distinct `OUTSIDE_ROOT` code would itself be an enumeration oracle — an attacker probing `/etc/shadow` vs. `/nonexistent-xyz` could distinguish "exists but forbidden" from "doesn't exist" purely from the code string, defeating the stated anti-enumeration goal. Both conditions therefore return `{"error":{"code":"NOT_FOUND","message":"No such file or directory."}}` verbatim. The *internal* application log (REQ-F-005(3): "internal logging may record the error") is free to log the true reason (e.g. `"ListFiles: rejected out-of-bounds path /etc/shadow"`) for debugging — only the value returned to the model/API is unified. |
| Path exists but is not a directory | `NOT_A_DIRECTORY` | — |
| Path exists, is a directory, but is unreadable | `PERMISSION_DENIED` | — |
| (internal only) malformed/missing `path` parameter | `INVALID_PARAMETERS` | Not one of REQ-F-005's four filesystem conditions, but `execute()` must still not throw on a malformed input object — this code covers that defensively. |

Example success and error shapes match REQ-F-003/REQ-F-005 verbatim (already given in the SPEC);
no deviation.

---

## 4. Domain Model Changes

### `Message.tool_calls_`

```cpp
// stream_event.h and message.h both need this; defined once in a new small header,
// src/domain/include/holonight_domain/tool_call.h, included by both.

enum class ToolCallKind : std::uint8_t { Invocation, Result };

struct ToolCallEntry {
  ToolCallKind kind;
  QString tool_use_id;   // Anthropic's tool_use block id ("toolu_xxx"); the join key between an
                          // Invocation entry and its later Result entry.
  QString tool_name;      // Populated for Invocation; empty for Result (the result's own message
                          // doesn't strictly need it, but callers/UI find it convenient to not
                          // have to hop back to the paired Invocation entry to render a label).
  QJsonObject input;      // Invocation only: the tool's parameters, exactly as sent to execute().
  QJsonObject result;     // Result only: the tool's JSON result, exactly as returned by execute()
                          // (either the success shape or the {"error": {...}} shape).
  bool is_error = false;  // Result only: mirrors Anthropic's tool_result.is_error (a required
                          // field on the wire — see §5). Always false for Invocation entries.

  friend bool operator==(const ToolCallEntry&, const ToolCallEntry&) = default;
};
```

`Message` gains:

```cpp
std::vector<ToolCallEntry> tool_calls_;
[[nodiscard]] const std::vector<ToolCallEntry>& toolCalls() const;
void setToolCalls(std::vector<ToolCallEntry> entries);
```

**Design decision — one domain `Message` per tool-use block, not one `Message` per provider turn.**
The field is a `std::vector` (matching the SPEC's plural field name, `tool_calls`, and leaving
headroom for a future cycle that batches multiple simultaneous `tool_use` blocks into one entry),
but this cycle's orchestration code (§6) always constructs single-entry vectors: one `Message` with
role `Assistant` and exactly one `Invocation` entry for each `tool_use` block, and a second,
separate `Message` with role `User` and exactly one `Result` entry for its outcome. This is a
deliberate choice, not an oversight — see §10, "one Message per tool-use block vs. one Message per
provider turn," for the alternative considered and why it was rejected.

`role()` is Assistant for Invocation-carrying messages and User for Result-carrying messages **not**
because those messages have a human/assistant author in the UI sense, but because that is exactly
how the Anthropic Messages API models them (`tool_use` lives inside an assistant turn; `tool_result`
must be sent back inside a user turn) — reusing the existing `MessageRole` enum for this avoids
touching `domain-core-types`' `MessageRole{System, User, Assistant}` definition, which this SPEC's
Superseded Requirements section does *not* amend. `text_` is empty (`QString()`) for both — all
tool-call content lives in `tool_calls_`.

### `StreamEvent::ToolCall`

```cpp
struct ToolCall {
  QString id;          // Anthropic tool_use block id ("toolu_xxx")
  QString name;         // tool name, e.g. "ListFiles"
  QJsonObject input;    // fully-accumulated, parsed input object (see §5 — only emitted once
                        // input_json_delta accumulation is complete and valid JSON)

  friend bool operator==(const ToolCall&, const ToolCall&) = default;
};

using StreamEvent = std::variant<ContentDelta, Completed, Error, Cancelled, ToolCall>;
```

`ToolCall` carries no `Usage`/`model_identifier` (unlike `Completed`/`Error`/`Cancelled`) — it is a
mid-stream signal, not a terminal one; usage/model-identifier are stamped once, at the terminal
`Completed` event of each HTTP round-trip, exactly as today.

### Round-trip

- **Outbound (model → app):** `AnthropicProvider` accumulates a `tool_use` block's streamed
  `input_json_delta` fragments, parses the complete JSON at `content_block_stop`, and emits
  `StreamEvent::ToolCall{id, name, input}` (§5). `ChatController::handleStreamEvent()` turns that
  into a `Message{role=Assistant, tool_calls=[{kind=Invocation, tool_use_id=id, tool_name=name,
  input=input}]}`, executes the tool via `ToolRegistry::invoke(name, input)`, and turns the result
  into a second `Message{role=User, tool_calls=[{kind=Result, tool_use_id=id, result=<json>,
  is_error=<bool>}]}` (§6).
- **Inbound (app → model, on the follow-up request):** `AnthropicProvider::sendChat()` walks
  `history`, and for any `Message` whose `tool_calls()` is non-empty, builds the matching
  `tool_use`/`tool_result` content block instead of a plain text block, keyed off `tool_use_id`/
  `tool_name`/`input`/`result`/`is_error` (§5).

---

## 5. Anthropic Adapter Changes

All changes are in `src/providers/src/anthropic_provider.cpp` /
`src/providers/include/holonight_providers/anthropic_provider.h`.

### 5.1 `sendChat()` signature

```cpp
HttpRequestHandlePtr sendChat(const holonight_domain::ModelId& model,
                              const std::vector<holonight_domain::Message>& history,
                              const std::function<void(const holonight_domain::StreamEvent&)>& on_event,
                              std::chrono::milliseconds idle_timeout = std::chrono::seconds{30},
                              const QJsonArray& tools = {});
```

`tools` defaults to an empty array, so the four other call sites in the codebase that don't need
tool-calling (tests constructing `AnthropicProvider` directly, any code that predates this cycle)
keep compiling unchanged. `ProviderAdapterRouter` is the only caller that ever passes a non-empty
array (§6.1).

```cpp
if (!tools.isEmpty()) {
  body[QStringLiteral("tools")] = tools;  // amends the "Deliberately absent: tools, ..." comment
}
```

### 5.2 Building `tools[]`

`ToolRegistry::toAnthropicToolsArray()` (§2) already produces the exact `{"name", "description",
"input_schema"}` shape Anthropic expects per tool, in registration order (REQ-NF-003). No
transformation happens in `AnthropicProvider` — it receives the array pre-built and passes it
through verbatim, keeping wire-format knowledge for the tools *list* in `ToolRegistry` and
wire-format knowledge for tool-use *events* in the adapter (the two halves of the Anthropic tools
contract are naturally split across the module boundary this way, since only the adapter parses SSE).

### 5.3 SSE parsing — `content_block_start` / `input_json_delta` / `content_block_stop`

Verified against current Anthropic API documentation: a `tool_use` content block streams as
`content_block_start` (with `content_block: {"type":"tool_use","id":"toolu_...","name":"..."}`,
`input` implicitly empty at this point), zero or more `content_block_delta` events of type
`input_json_delta` carrying a `partial_json` string fragment, then `content_block_stop`. The
fragments are **not individually valid JSON** — only the fully concatenated string is guaranteed
to parse (see §11 risk (b)).

`StreamContext` (currently `buffer, handle, stop_reason, has_visible_content, terminal, completed,
usage, model_identifier`) gains:

```cpp
struct ToolUseAccumulator {
  QString id;
  QString name;
  QString partial_json;
};
QHash<int, ToolUseAccumulator> tool_use_blocks;  // keyed by SSE "index"
bool has_tool_call = false;
```

`routeSseEvent()` gains three branches (currently `content_block_start`/`content_block_stop` fall
into the "everything else is a no-op" bucket — REQ-F-019 in the prior cycle's SPEC; this cycle
narrows that no-op to exclude tool_use-typed blocks specifically):

```cpp
if (type == QStringLiteral("content_block_start")) {
  const int index = object[QStringLiteral("index")].toInt();
  const QJsonObject block = object[QStringLiteral("content_block")].toObject();
  if (block[QStringLiteral("type")].toString() == QStringLiteral("tool_use")) {
    context->tool_use_blocks[index] = StreamContext::ToolUseAccumulator{
        .id = block[QStringLiteral("id")].toString(),
        .name = block[QStringLiteral("name")].toString(),
        .partial_json = QString()};
  }
  return;
}
```

`content_block_delta`'s existing branch only handles `delta.type == "text_delta"`; add a sibling
check before the "any other delta.type is a no-op" comment:

```cpp
if (delta.value(QStringLiteral("type")).toString() == QStringLiteral("input_json_delta")) {
  const int index = object[QStringLiteral("index")].toInt();
  auto it = context->tool_use_blocks.find(index);
  if (it != context->tool_use_blocks.end()) {
    it->partial_json += delta[QStringLiteral("partial_json")].toString();
  }
  return;
}
```

```cpp
if (type == QStringLiteral("content_block_stop")) {
  const int index = object[QStringLiteral("index")].toInt();
  auto it = context->tool_use_blocks.find(index);
  if (it != context->tool_use_blocks.end()) {
    const QByteArray raw = it->partial_json.isEmpty() ? QByteArrayLiteral("{}") : it->partial_json.toUtf8();
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
      failStream(context, on_event, QStringLiteral("Malformed tool input from Anthropic: %1").arg(parseError.errorString()));
      return;
    }
    context->has_tool_call = true;
    on_event(StreamEvent{holonight_domain::ToolCall{.id = it->id, .name = it->name, .input = doc.object()}});
    context->tool_use_blocks.erase(it);
  }
  return;
}
```

### 5.4 `message_stop` — the "no visible content" guard must not fire on tool-only turns

Existing code fails the stream if `message_stop` arrives without `has_visible_content` ("Anthropic
completed without returning text content"). A turn that is *entirely* a tool call has no text
content by design — the guard becomes:

```cpp
if (!context->has_visible_content && !context->has_tool_call) {
  failStream(context, on_event, QStringLiteral("Anthropic completed without returning text content"));
  return;
}
```

`Completed` is still emitted unconditionally at `message_stop` regardless of `stop_reason` — the
adapter does not special-case `stop_reason == "tool_use"` itself; that distinction is
`ChatController`'s to make (§6), by checking whether any `ToolCall` events arrived during this
particular stream. This keeps the adapter's contract simple: "one HTTP stream, one terminal event,"
unchanged from before this cycle.

### 5.5 Reconstructing history for the follow-up request

Per-message content-block construction:

- Plain message (`tool_calls().empty()`) → unchanged: `entry["content"] = message.text()` (a bare
  string), preserving today's wire format exactly for ordinary turns even when tool-calling is
  enabled but wasn't exercised.
- `Invocation` message → `{"type":"tool_use","id":entry.tool_use_id,"name":entry.tool_name,
  "input":entry.input}`.
- `Result` message → `{"type":"tool_result","tool_use_id":entry.tool_use_id,
  "content":<QJsonDocument(entry.result).toJson(Compact) as a UTF-8 string>,"is_error":entry.is_error}`.
  `is_error` is included unconditionally (`true` or `false`) — Context7-verified: Anthropic's
  `BetaToolResultBlockParam.IsError` is documented as **required**, not optional, so this is not an
  "omit when false" field the way some other optional booleans in this codebase are handled.
  `content` is sent as a JSON-stringified string (not a nested content-block array) — simpler, and
  Anthropic's `content` field on `tool_result` explicitly accepts "a string or a list of blocks," so
  a string is valid and sufficient for a structured-but-flat JSON payload like ListFiles' results.

**Grouping same-role messages.** When a model turn produces text *and* a tool call (e.g. "Let me
check that…" followed by a `tool_use` block), those become two separate domain `Message`s (a plain
Assistant text message, then an Assistant Invocation message) that must be re-serialized as a
**single** Anthropic API message object with a two-element `content` array — not two consecutive
`"assistant"`-role entries. Context7-verified: the Anthropic Messages API's own documented behavior
is "Consecutive messages with the same role will be combined" — so relying on the API's own merge
would technically work, but this design does the grouping client-side anyway, for two reasons: (1)
it makes the exact array-vs-string `content` shape sent for each logical turn deterministic and
testable without depending on undocumented merge-ordering behavior, and (2) it avoids ever sending
a request that *looks* malformed to a future reader debugging a captured request body. The
algorithm: walk `history` in order (after hoisting `System`-role messages as today); for each
non-system message, compute its mapped Anthropic role (`roleToAnthropicString`, unchanged); if that
role equals the role of the **last** API message object being built, append this message's content
block(s) into that object's `content` array (promoting a bare-string `content` to a one-element
array first, if the prior message hadn't needed array form); otherwise start a new API message
object. This is a linear pass, no lookahead, and requires no new state beyond a pointer/index to the
last-appended `QJsonObject`.

---

## 6. Orchestration Loop (`ChatController`)

`ChatController` gains a `std::shared_ptr<holonight_application::ToolRegistry> tool_registry_`
constructor parameter (both existing constructors — the five-provider one and the
`ProviderAdapterRouter*` one — need it added; the five-provider constructor is legacy/test-only per
the module's own comments, so it may take a default-constructed empty registry rather than every
call site needing an update — a detail for the task-breakdown stage to size).

`InFlightStream` (private to `chat_controller.cpp`/`.h`) gains:

```cpp
struct InFlightStream {
  // ... existing fields unchanged ...
  int tool_calls_this_turn = 0;      // cumulative across every re-dispatch within one user turn
  bool tool_call_seen_this_stream = false;  // reset at the start of each dispatchSendChat() round
};
```

### 6.1 `ProviderAdapterRouter::sendChat()` — deciding whether to send `tools[]`

`ProviderAdapterRouter` gains a `std::shared_ptr<ToolRegistry>` member (constructor-injected from
the same composition root as §2). Inside `sendChat()`, when the resolved `RuntimeRecord::adapter`
variant holds an `AnthropicProvider`, the router additionally consults that instance's
`AnthropicProviderConfig::tool_calling_enabled` (already reachable from the same per-instance config
the router already holds to build/reconfigure adapters — no new lookup path) and passes
`tool_registry_->toAnthropicToolsArray()` if true, or an empty `QJsonArray{}` if false — REQ-F-009
in one branch. Ollama/OpenAI/Google adapters are untouched (REQ-C-002).

### 6.2 State machine

```
                     ┌─────────────────────────────────────────────────────┐
                     │  send()/regenerate() → startStream()                │
                     │  tool_calls_this_turn := 0 (NEW turn, cap resets)   │
                     └───────────────────────┬─────────────────────────────┘
                                              │ dispatchSendChat()
                                              ▼
                     ┌─────────────────────────────────────────────────────┐
                     │  ONE Anthropic HTTP stream in flight                │
                     │  tool_call_seen_this_stream := false                │
                     └───────────────────────┬─────────────────────────────┘
                                              │ handleStreamEvent()
              ┌───────────────┬──────────────┼───────────────┬─────────────┐
              ▼               ▼              ▼               ▼             │
        ContentDelta      ToolCall       Error/Cancelled   Completed       │
        (unchanged:       (NEW branch,   (unchanged:       (existing       │
        append to         see below)     terminal, remove  terminal path   │
        working_message)                 from in_flight_)  UNLESS         │
                                                             tool_call_seen_
                                                             this_stream)
                                                                    │
                                    ┌───────────────────────────────┴─────┐
                                    │ tool_call_seen_this_stream == false │───▶ finalize as today
                                    │ tool_call_seen_this_stream == true  │───▶ continueToolLoop()
                                    └──────────────────────────────────────┘
```

**`ToolCall` branch of `handleStreamEvent()`'s `std::visit`:**

```cpp
} else if constexpr (std::is_same_v<T, holonight_domain::ToolCall>) {
  if (stream.tool_calls_this_turn >= kMaxToolCallsPerTurn) {
    // REQ-F-006(2)/(4): the 11th call is neither executed nor counted further.
    // Cancel the underlying HTTP stream — no more of the model's turn is relevant.
    if (stream.handle) { stream.handle->cancel(); }
    static_cast<void>(stream.working_message.transitionTo(MessageStatus::Error));
    stream.working_message.setText(QStringLiteral(
        "Tool-calling limit exceeded (max %1 calls per turn); stopping.").arg(kMaxToolCallsPerTurn));
    stream.conversation->replaceLastMessage(stream.working_message);
    if (stream.on_event) {
      stream.on_event(StreamEvent{holonight_domain::Error{
          .message = QStringLiteral("Tool-calling limit exceeded (max %1 calls per turn); stopping.")
                        .arg(kMaxToolCallsPerTurn)}});
    }
    in_flight_.remove(conversation_key);
    return;
  }

  stream.tool_calls_this_turn += 1;
  stream.tool_call_seen_this_stream = true;

  // 1. Finalize any in-progress prose bubble so its partial text is preserved as its own message
  //    and future ContentDelta text (if the model resumes talking after this tool call, in the
  //    SAME stream) starts a fresh bubble rather than appending onto stale text. See §11(a).
  static_cast<void>(stream.working_message.transitionTo(MessageStatus::Complete));
  stream.conversation->replaceLastMessage(stream.working_message);

  // 2. Record + persist the invocation as its own, visually distinct Message (REQ-F-007(1)).
  Message invocation(MessageId::generate(), MessageRole::Assistant, QString(), MessageStatus::Complete);
  invocation.setToolCalls({ToolCallEntry{.kind = ToolCallKind::Invocation,
                                         .tool_use_id = value.id,
                                         .tool_name = value.name,
                                         .input = value.input}});
  stream.conversation->appendMessage(invocation);
  if (stream.on_event) { stream.on_event(event); }  // UI/persistence layer observes immediately

  // 3. Execute synchronously via the registry — no branching on tool identity (REQ-F-001(2)),
  //    no blocking dialog (REQ-C-005), completes within this call (REQ-NF-002).
  const QJsonObject result = tool_registry_->invoke(value.name, value.input);

  // 4. Record + persist the result as its own Message (REQ-F-007(2)).
  Message resultMessage(MessageId::generate(), MessageRole::User, QString(), MessageStatus::Complete);
  resultMessage.setToolCalls({ToolCallEntry{.kind = ToolCallKind::Result,
                                            .tool_use_id = value.id,
                                            .result = result,
                                            .is_error = result.contains(QStringLiteral("error"))}});
  stream.conversation->appendMessage(resultMessage);
  // (a second stream.on_event firing for the result — see §9 for how the UI distinguishes the two)

  // 5. Fresh placeholder for whatever the model says/does next, matching startStream()'s pattern.
  Message nextPlaceholder(MessageId::generate(), MessageRole::Assistant, QString(), MessageStatus::Streaming);
  stream.conversation->appendMessage(nextPlaceholder);
  stream.working_message = nextPlaceholder;
  stream.accumulated_text.clear();

  // Persistence of `invocation`/`resultMessage`/`nextPlaceholder` happens through whatever
  // mechanism already persists the placeholder assistant Message created in startStream() today —
  // this design does not introduce a second, parallel persistence path (see §7).
}
```

Note this branch does **not** re-invoke `dispatchSendChat()` itself — it only appends the
invocation/result/placeholder messages and executes the tool. Re-dispatch happens once, in the
`Completed` branch, after the *entire* current HTTP stream has finished (there may be more
`ToolCall` events, or trailing text, still arriving in the same response).

**`Completed` branch — amended:**

```cpp
} else if constexpr (std::is_same_v<T, holonight_domain::Completed>) {
  if (stream.tool_call_seen_this_stream) {
    continueToolLoop(conversation_key, stream, value);  // NEW: re-dispatch, do NOT remove from in_flight_
    return;
  }
  // ... existing finalize-and-remove-from-in_flight_ logic, unchanged ...
}
```

`continueToolLoop()` (new private method) mirrors `startStream()`: it takes `stream.conversation`'s
current message list (now including every invocation/result/placeholder pair appended so far this
turn) as the new `history`, re-invokes `dispatchSendChat()` with it, and updates
`in_flight_[conversation_key].handle` to the new `HttpRequestHandlePtr` — reusing the *same*
`in_flight_` entry (so `tool_calls_this_turn` persists across the re-dispatch, and `stop()`/
cancellation during a tool-loop round still works via the existing `in_flight_.find()` guard,
unchanged). It resets `tool_call_seen_this_stream = false` before dispatching, so the *next* round's
`Completed` event correctly falls through to normal finalization if the model doesn't call another
tool.

### 6.3 Cap enforcement (REQ-F-006)

The cap is enforced **before** executing a tool call, not after — `stream.tool_calls_this_turn` is
checked and only incremented on a call that will actually run. The 11th `ToolCall` event is
observed, immediately rejected, and the stream is torn down; no `Message` is created for the
rejected call (REQ-F-006(4): "not executed"). The counter lives on `InFlightStream` and is reset to
`0` only in `send()`/`regenerate()` (a genuinely new user turn), never in `continueToolLoop()` — so
the cap is correctly scoped to "one user turn," which in this design spans however many
`dispatchSendChat()` round-trips the tool loop performs, exactly matching REQ-F-006's "consecutive
tool calls within a single user turn" wording.

---

## 7. Persistence

### 7.1 Migration

New file: `src/persistence/migrations/0008_add_message_tool_calls.sql`:

```sql
ALTER TABLE messages ADD COLUMN tool_calls TEXT;
```

Nullable (no `DEFAULT`), following the `0007_add_pinned_at.sql` precedent for an optional column
rather than `0003_add_title_source.sql`'s `NOT NULL DEFAULT` precedent — most historical messages
and most future messages will have no tool-call data, and `NULL` cleanly means "not applicable"
without a magic default value to special-case.

**Gotcha, called out explicitly per this codebase's own documented pattern:** this migration file
is inert until it is added to the hardcoded list in
`MigrationRunner::builtInMigrations()` (`src/persistence/src/migration_runner.cpp`) — adding the
`.sql` resource alone does nothing; a `Migration{.version = 8, .name =
QStringLiteral("0008_add_message_tool_calls"), .sql = readResource(...)}` entry must be appended to
that function's return list by hand.

### 7.2 Serialization: JSON column, not a side table

`tool_calls` is serialized as a single JSON-array-in-a-TEXT-column, mirroring the codebase's
existing precedent of packing a compound value into one column when the value is 1:1 with its owning
row (`model_id`'s unit-separator encoding is the closest existing analogue, though this uses
`QJsonDocument`/`QJsonArray` rather than a hand-rolled separator, since the payload here is
naturally tree-shaped — a tool's `input`/`result` are themselves JSON objects with unknown-in-advance
shape, unlike `ModelId`'s two flat strings).

Encoding: `std::vector<ToolCallEntry>` → `QJsonArray` → `QJsonDocument(array).toJson(Compact)` →
`QString` bound to `:tool_calls`. Each `ToolCallEntry` becomes one object:
```json
{"kind": "invocation", "tool_use_id": "toolu_...", "tool_name": "ListFiles", "input": {...}}
{"kind": "result", "tool_use_id": "toolu_...", "result": {...}, "is_error": false}
```
(`kind` distinguishes the two on decode; the unused fields for each kind — e.g. `tool_name`/`input`
on a Result entry — are simply omitted rather than written as `null`, keeping stored JSON minimal.)

### 7.3 Touch points in `conversation_repository_worker.cpp`

Per the codebase's own verified mapping, four functions need updating, all in
`src/persistence/src/detail/conversation_repository_worker.cpp`:

1. **`messageFromRecord(const QSqlQuery&)`** — the `SELECT id, role, content, status, created_at,
   model_id FROM messages ...` query gains `tool_calls` as its 7th positional column; the free
   function decodes it via a new `decodeToolCalls(QVariant)` helper (empty vector if `NULL`) and
   passes it to the (also-updated) `Message` constructor or via `setToolCalls()` after construction.
2. **`materializeConversation()`'s INSERT** and **`persistNewMessage()`'s INSERT** — both add
   `tool_calls` to the column list and `:tool_calls` to the `VALUES` list, bound via a new
   `encodeToolCalls(const std::vector<ToolCallEntry>&)` helper (`QVariant()` when the vector is
   empty, matching the existing `model_id` null-when-absent idiom exactly).
3. **`persistMessageSettled()`** (the UPDATE path, today touching `content, status, model_id`) —
   tool-call/result messages in this design are always fully-formed at creation time (never
   streamed incrementally the way assistant text is), so they are written once via
   `persistNewMessage()` and never go through `persistMessageSettled()`'s partial-update path. This
   is called out explicitly as a design decision: `persistMessageSettled()` itself does **not** need
   a `tool_calls` parameter, since no code path ever mutates a tool-call `Message`'s `tool_calls_`
   field after construction.

### 7.4 Recoverability

Because `ConversationRepository`'s existing `loadConversation()` path already re-runs
`messageFromRecord()` for every row (ordered by `created_at ASC, rowid ASC`), and `tool_calls`
round-trips through the same encode/decode helpers used on write, reopening a conversation
reconstructs the exact `Message` sequence — including interleaved Invocation/Result pairs — with no
additional reconciliation logic (REQ-F-008(2)). Because `AnthropicProvider::sendChat()`'s history
reconstruction (§5.5) reads `tool_calls()` off of whatever `Message` vector it's handed, a
follow-up turn on a *reopened* conversation reconstructs the same `tool_use`/`tool_result` blocks a
live in-session follow-up would (REQ-F-008(3)) — persistence and live-session history
reconstruction share the same domain-level code path by construction, not by parallel
re-implementation.

---

## 8. Settings Toggle

### `holonight_config`

`AnthropicProviderConfig` (`src/config/include/holonight_config/provider_config.h`) gains:

```cpp
struct AnthropicProviderConfig {
  QString base_url = QStringLiteral("https://api.anthropic.com");
  QString default_model;
  double temperature = 1.0;
  int max_output_tokens = 1024;
  bool tool_calling_enabled = false;  // REQ-F-010: off by default for new instances
  friend bool operator==(const AnthropicProviderConfig&, const AnthropicProviderConfig&) = default;
};
```

`src/config/src/config_repository.cpp`:
- New key constant: `constexpr auto kToolCallingEnabledKey = "tool_calling_enabled";`
- `serializeSettings()`: extend the existing Anthropic-specific `if constexpr` branch (the one that
  already writes `kMaxOutputTokensKey` for `AnthropicProviderConfig`) to also write
  `object[kToolCallingEnabledKey] = config.tool_calling_enabled;`.
- `parseSettings(ProviderType::Anthropic, ...)`: read the key with a **tolerant-missing** guard —
  unlike `kMaxOutputTokensKey` (which is validated as required via `hasFiniteNumber`), a *missing*
  `tool_calling_enabled` key must decode as `false`, not fail parsing outright (REQ-F-010(2): "When
  an existing Anthropic provider instance is loaded ... and the toolCallingEnabled setting key does
  not exist ... it defaults to false"). Concretely:
  `object.value(kToolCallingEnabledKey).toBool(false)` — `QJsonValue::toBool(false)` already returns
  the supplied default both when the key is absent (an undefined `QJsonValue`) and when present but
  not a JSON boolean, which is exactly the desired forward-compatible/tolerant behavior for a
  pre-existing on-disk config written before this cycle shipped.

### `holonight_application`

`AnthropicProviderSettingsController` gains, following the `temperature`/`maxOutputTokens`
`Q_PROPERTY` pattern verbatim:

```cpp
Q_PROPERTY(bool toolCallingEnabled READ toolCallingEnabled WRITE setToolCallingEnabled NOTIFY toolCallingEnabledChanged)
```
```cpp
void AnthropicProviderSettingsController::setToolCallingEnabled(bool value) {
  if (tool_calling_enabled_ == value) return;
  tool_calling_enabled_ = value;
  emit toolCallingEnabledChanged();
}
```
wired into `connectDraftProperties()`/`updateDraft()`/`loadDraft()` exactly as `temperature` already
is, adding `.tool_calling_enabled = tool_calling_enabled_` to the `AnthropicProviderConfig{...}`
designated-initializer in `updateDraft()` and reading it back via
`std::get_if<AnthropicProviderConfig>(&draftSession()->editable().settings)->tool_calling_enabled`
in `loadDraft()`.

### QML

The Anthropic provider settings panel (wherever `temperature`/`maxOutputTokens` are currently
exposed as editable fields — the implementing engineer should locate that existing panel under
`qml/workspace/` rather than assume a filename) gains one new control bound to
`toolCallingEnabled` — a `Switch`/`CheckBox` labeled "Enable Tool Calling" (REQ-F-009(1)), following
whatever control style the existing temperature/max-tokens fields already use for consistency.

---

## 9. UI Rendering

Per the verified delegate structure in `qml/shared/MessageList.qml` /
`qml/shared/MessageBubble.qml`: message-level dispatch is already a `Loader` keyed on the `role`
role-name string (`"user"` → `UserMessageCard`, else → `MessageBubble`), and a *second*, nested
`Loader` inside `MessageBubble` dispatches per content block on a `ContentBlockType` enum
(`Markdown`/`Code`) sourced from `holonight_rendering::ContentBlock`.

Tool-call/result messages must NOT be folded into `MessageBubble` (REQ-F-007(3) is explicit: "not
folded inline into an assistant message bubble"), so they need a **third message-level branch**
sitting alongside the existing `"user"`/other split, not a third `ContentBlockType` inside
`MessageBubble`. Concretely:

- `MessageListModel` (`src/application/include/holonight_application/message_list_model.h`) needs a
  way to expose "this row is a tool-call/result entry, and here is its structured data" to QML.
  Given the existing `Row` struct's `mutable std::optional<QVariantList> cached_content_blocks`
  precedent (lazily parsed from `row.text` via `MessageContentParser`), the natural analogous
  addition is a new role (e.g. `ToolCallRole`) exposing a `QVariantMap`/gadget built directly from
  the `Message`'s `tool_calls()` — **not** routed through `MessageContentParser` (that parser's job
  is markdown/code-fence text parsing; tool-call data is already structured `QJsonObject`, so
  wrapping it in `holonight_rendering::ContentBlock`'s markdown/code duality would be a category
  error). `Row` gains a `std::optional<QVariantMap> tool_call` field (or similar — populated at row
  construction time from `Message::toolCalls()`, empty/`std::nullopt` for ordinary text messages).
- `MessageList.qml`'s top-level `Loader`'s `sourceComponent` selector gains a third branch: when the
  row's tool-call role data is present, load a new `ToolCallCard.qml` (or similarly named)
  component instead of either `UserMessageCard` or `MessageBubble`. This is additive to the existing
  ternary — the implementing engineer should convert the current
  `role === "user" ? userMessageComponent : messageBubbleComponent` ternary into a small
  three-way dispatch (e.g. a JS function or a chain) rather than nesting ternaries, since a third
  branch is being added, not a fourth.
- `ToolCallCard.qml` renders differently depending on whether the row's `tool_call.kind` is
  `"invocation"` (show tool name + input parameters, e.g. "Tool: ListFiles — path: ~/Documents") or
  `"result"` (show the outcome — either the entries list or the error message), giving each of the
  two REQ-F-007-mandated message entries (call, then result) its own visually distinct card, in the
  chronological order they were appended in §6.2 (REQ-F-007(4)).

This document deliberately does not name the exact existing panel/component files beyond what was
independently verified (`MessageList.qml`, `MessageBubble.qml`) — the implementing engineer should
confirm current file names under `qml/workspace/` and `qml/shared/` before writing code, per this
project's own convention of verifying against the live tree rather than a design doc's assumptions.

---

## 10. Alternatives Considered

**1. Tool-call data in a separate SQL table vs. a JSON column on `messages`.**
Rejected a `tool_calls(message_id, kind, tool_use_id, tool_name, input, result, is_error)` side
table. A side table would give proper columnar types and queryability (e.g. "find all ListFiles
calls across all conversations"), but REQ-F-008's only requirement is round-tripping through one
conversation's reconstruction — there is no cross-conversation querying requirement in this SPEC,
and a side table would need a `JOIN` on every `loadConversation()` call plus insert-ordering
coordination between two tables inside the same transaction. A JSON column keeps the existing
single-`INSERT INTO messages`-per-row shape intact and is strictly less migration/worker-code churn
for a first cycle explicitly scoped to one tool. If a future cycle needs to query tool-call history
across conversations, that is a clean, additive migration at that point — nothing about the JSON
column choice forecloses it.

**2. A generic `extra_metadata` JSON blob on `Message` vs. a typed `tool_calls` field.**
Rejected a fully generic `QJsonObject metadata` catch-all field that tool-calling would be the first
consumer of. The SPEC's Superseded Requirements section names the field `tool_calls` specifically
(amending REQ-F-005's prior forbidding of that exact field name) — inventing a differently-named
generic field would technically satisfy the spirit but not the letter of the amendment, and a typed
`std::vector<ToolCallEntry>` gives compile-time safety for every consumer (persistence encode/decode,
the Anthropic adapter's history reconstruction, the UI's row mapping) that a loosely-typed
`QJsonObject` metadata bag would defer to runtime key-lookups everywhere. A generic metadata field
is the right shape once there are two or more unrelated metadata concerns wanting to ride along on
`Message` — with exactly one concern (tool calls) in this cycle, that generality is speculative.

**3. Synchronous vs. asynchronous tool execution.**
Rejected async (`QFuture`/callback-based) execution for `ITool::execute()`, even though `ListFiles`
touches the filesystem and filesystem I/O can, in principle, block. REQ-NF-002 explicitly mandates
synchronous execution ("the system shall invoke the tool synchronously and immediately within the
event-handling loop") — this is not a discretionary choice this design is free to relitigate. It is
also the right choice operationally for THIS tool: `ListFiles` only ever reads directory entries
(`readdir`) for one non-recursive directory level, which is a bounded, typically-sub-millisecond
syscall, not a risk to UI responsiveness. A future tool with genuinely unbounded I/O (e.g. reading
an arbitrarily large file, or a network-backed tool) would need to revisit `ITool`'s synchronous
contract — explicitly flagged as future-cycle scope, not something this design pre-solves.

**4. One domain `Message` per tool-use block vs. one `Message` per provider turn (bundling text +
tool_use + tool_result into a single `Message` with multiple `tool_calls` entries).**
Rejected bundling. A bundled-per-turn `Message` would map more directly onto Anthropic's actual wire
representation (one assistant API message can legitimately contain a text block AND a tool_use block
together) and would avoid the client-side same-role-grouping logic in §5.5 entirely. It was rejected
because REQ-F-007(3) requires tool-call/result entries to be "visually distinct" and "not folded
inline into an assistant message bubble" — a single `Message` object is fundamentally one row in
`MessageListModel`, so bundling prose and a tool call into one `Message` would force the QML layer
to do the "is this really one thing or two visually distinct things" splitting that this design
instead resolves once, at the point the data is created (§6.2), where it is cheapest to reason about
correctly. The cost is the grouping/merging logic in §5.5 on the way back out to the API — judged a
better place to pay that cost than in the rendering layer, since §5.5's logic is a pure, easily
unit-testable transformation over an already-ordered list, while a rendering-layer split would need
to reverse-engineer intent from a bundled data shape.

---

## 11. Known Risks

**(a) The `Message` domain-type change touches every existing call site that constructs a `Message`,
including every existing test.** Adding `tool_calls_` as a new field means every `Message{...}`
aggregate-initialization or constructor call across `holonight_domain`, `holonight_persistence`,
`holonight_application`, and `holonight_providers` test suites needs to keep compiling — the
constructor signature listed in §4 keeps `tool_calls` as a trailing, defaulted parameter
(`std::vector<ToolCallEntry> tool_calls = {}`) specifically so existing call sites do not need
mechanical edits merely to keep building. `operator==`'s `= default` on `Message`, however, means
any test asserting `Message` equality will now also compare `tool_calls_` — tests that construct an
"expected" `Message` without an explicit `tool_calls` argument still compare correctly against
another default-empty `Message` (both empty vectors), so this should be a non-issue in practice, but
it is worth a mechanical `task test` pass immediately after the domain-type change lands, before any
of the orchestration/adapter/persistence layers are built on top of it — isolating whether any
failures are pre-existing-test breakage (structural) vs. new-feature-test failures (expected).

**(b) `input_json_delta` accumulation into valid JSON needs careful buffering — partial JSON is not
valid JSON.** §5.3's `ToolUseAccumulator::partial_json` is a raw string concatenation of fragments
as they arrive; nothing about that string is parseable until `content_block_stop` fires for that
block's index. Two failure modes to guard against, both already addressed by the design in §5.3 but
worth calling out explicitly for the implementing engineer: (1) a `content_block_stop` arriving for
an index the accumulator never saw a `content_block_start` for (defensive: the `tool_use_blocks.find(index)
== end()` check already makes this a no-op rather than a crash); (2) the accumulated string being
empty (a tool called with zero parameters) — Anthropic represents this as an empty `partial_json`
across zero delta events, which `QJsonDocument::fromJson("{}")` (the explicit fallback in §5.3, not
`fromJson("")`, which fails to parse) handles correctly. A live-API integration smoke test (or at
minimum a fake-network-client test that drives exactly this SSE sequence, per the existing
injectable-`HttpClient` test pattern for other adapters) is the recommended verification for this
risk during implementation, not just a unit test on the accumulation logic in isolation.

**(c) The tool-call loop must not deadlock or re-enter `dispatchSendChat()` in a way that races with
cancellation or `regenerate()`.** §6.2's `continueToolLoop()` reuses the *same* `in_flight_` map
entry across re-dispatches specifically so that `ChatController::stop()`'s existing
`in_flight_.find(key)` guard continues to work unmodified during a tool loop — a user hitting "stop"
mid-tool-loop cancels whatever HTTP stream is currently in flight (via `stream.handle->cancel()`)
and removes the entry, exactly as today. The risk is more subtle for `regenerate()`: it currently
checks `in_flight_.contains(conversationKey)` and rejects if streaming — that guard is unchanged and
correctly rejects a `regenerate()` call while a tool loop is mid-flight (the conversation key is
still present in `in_flight_` for the entire duration of a multi-round tool loop, not just the first
round), so no new race is introduced there. The genuinely new risk is **within**
`continueToolLoop()` itself: it must call `dispatchSendChat()` and update
`in_flight_[key].handle` in a way that a `Cancelled`/`Error` event arriving asynchronously between
"the previous stream ended" and "the new stream's handle is recorded" cannot land on a stale handle
or a since-removed map entry. `startStream()`'s existing code already has this exact narrow window
(`dispatchSendChat()` is called, and only *after* it returns is `entryIt->handle` set) — mirroring
that same ordering in `continueToolLoop()`, rather than inventing a different one, is the safest
choice, precisely because it inherits a pattern already exercised by every other stream dispatch.

**(d) `migration_runner.cpp`'s hardcoded migration list.** Already called out in §7.1: adding
`0008_add_message_tool_calls.sql` as a resource file is necessary but not sufficient — it must also
be appended to `MigrationRunner::builtInMigrations()`'s returned `std::vector`. This has bitten a
prior cycle in this codebase (per this project's own accumulated implementation notes) and is
restated here as a design-level risk specifically so the task-breakdown stage schedules a task (or a
checklist item on the migration task) that verifies the new migration is actually reachable — e.g. a
test that opens a fresh database and asserts `MigrationRunner::currentVersion()` reaches `8`, not
just that the `.sql` file parses.

**(e) (additional, surfaced during this design pass) A tool-only turn leaves the prose placeholder
`Message` finalized with empty text.** §6.2 step 1 finalizes `stream.working_message` (originally
created empty by `send()`/`regenerate()`) as `MessageStatus::Complete` before appending the
invocation `Message`, even when the model never streamed any `ContentDelta` text before calling a
tool — meaning an empty-text Assistant `Message` is persisted and enters the conversation's message
list. `holonight_domain::Conversation` has no `removeLastMessage()`/message-deletion API (verified —
only `appendMessage()`/`replaceLastMessage()` exist), so silently dropping the empty placeholder
before finalizing is not currently possible without a `Conversation` API addition, which this design
does not propose taking on as in-scope. This is flagged as an open question for the implementing
engineer to resolve against actual UI behavior: `send()`/`regenerate()` already create this exact
empty-text placeholder transiently (before the first `ContentDelta` arrives) today, so if
`qml/shared/MessageBubble.qml` (or `MessageListModel`) already suppresses rendering of an empty-text
Assistant message, no further work is needed — the tool-only-turn case merely makes that dormant
empty message *permanent* rather than transient. If no such suppression exists today, a small
follow-up (skip appending/persisting a `Complete`-status Assistant message whose `text()` is empty
and whose `tool_calls()` is also empty) should be added as part of this cycle's implementation, not
deferred, since it is a direct, visible artifact of this feature's own control flow.
