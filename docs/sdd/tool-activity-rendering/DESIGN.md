# Tool Activity Rendering and Extensible Tool Architecture — DESIGN.md

## 1. Design Goals

This design has two related goals:

1. Turn the current debugging-oriented Invocation/Result cards into a compact, trustworthy chat
   experience.
2. Establish durable boundaries so adding providers and richer tools does not create a matrix of
   `provider × tool × QML` special cases.

The central rule is:

> Providers translate protocol, tools execute behavior, presenters derive semantics, and QML
> renders visuals.

The current implementation conflates parts of these responsibilities: `ITool` combines definition
and execution, `ToolRegistry` produces an Anthropic-shaped array, `ToolCallEntry` is a wire-ledger
record, and `ToolCallCard.qml` renders that record directly. This cycle separates them without
discarding the working provider history model.

## 2. Architecture Summary

```text
Provider response
      │ native tool item/block/part
      ▼
ProviderToolCodec (one per provider dialect)
      │ normalized ToolRequestEvent
      ▼
ChatController / ToolOrchestrator
      │ resolves canonical tool, owns lifecycle, starts execution
      ▼
ToolRegistry ───── ToolExecutor
      │                 │ asynchronous ToolOutcome
      │                 ▼
      └────────── ToolInvocation aggregate
                         │ persisted as invocation/result ledger records
                         ▼
Transcript projection pairs by providerCallId
                         │
                         ▼
ToolPresenterRegistry ── presenter creates ToolPresentationModel
                         │ rendererKey + semantic detail data
                         ▼
ToolActivityCard.qml (shared shell)
                         │ Loader
                         ▼
ToolRendererRegistry.qml
             ├── ListFilesToolContent.qml
             └── GenericToolContent.qml
```

This is deliberately not a virtual base class for all providers. Existing providers remain concrete
types in `ProviderAdapterRouter`'s variant. A small provider-owned codec/helper may be composed into
each provider where tool support is implemented.

## 3. Canonical Tool Model

### 3.1 Stable identity

Use two names:

```cpp
struct ToolDefinition {
  QString id;             // Stable application ID: "filesystem.list"
  QString function_name;  // Protocol-safe name: "list_files"
  QString display_name;   // Translatable/fallback display label: "List files"
  QString renderer_key;   // Stable UI contract key: "filesystem.list"
  QString description;
  QJsonObject input_schema;
  ToolRisk risk;
};
```

The canonical ID selects executors, presenters, permissions, and renderers. The function name is the
name advertised on provider wires. Existing `ListFiles` history is supported as an alias resolving
to `filesystem.list`; it is not silently rewritten in persisted history.

Keeping identity separate prevents future provider naming restrictions from becoming UI identity.
It also lets the application rename a user-facing label without changing the model contract.

### 3.2 Execution location and source

The normalized model distinguishes where execution occurs:

```cpp
enum class ToolExecutionLocation : std::uint8_t {
  LocalClient,
  ProviderHosted,
  RemoteExternal,
};

enum class ToolSource : std::uint8_t {
  BuiltIn,
  Provider,
  Mcp,
};
```

`ChatController` dispatches only `LocalClient` requests to local executors. Provider-hosted tools
still produce activities for trust and debugging, but executing them again locally would be a
serious correctness bug.

### 3.3 Normalized invocation

```cpp
enum class ToolInvocationStatus : std::uint8_t {
  Requested,
  AwaitingApproval,
  Running,
  Completed,
  Failed,
  Denied,
  Cancelled,
};

struct ToolInvocation {
  QString id;                    // Application UUID; stable row identity.
  QString provider_call_id;      // Exact correlation ID supplied by provider.
  QString provider_instance_id;
  QString tool_id;               // Canonical ID when resolved; empty when unknown.
  QString function_name;         // Exact wire function name for history/fallback.
  ToolExecutionLocation location;
  ToolSource source;
  QJsonObject arguments;
  ToolInvocationStatus status;
  QDateTime requested_at;
  std::optional<QDateTime> started_at;
  std::optional<QDateTime> finished_at;
  std::optional<QJsonValue> result;
  std::optional<ToolError> error;
};
```

Use `QJsonValue` for results rather than `QJsonObject`: future tools may legitimately return an
array, scalar, or provider-native content reference. `ListFiles` remains an object.

### 3.4 Protocol ledger versus product aggregate

Do not make a destructive persistence rewrite in this cycle. The existing separate Invocation and
Result `ToolCallEntry` records remain the lossless protocol ledger used by provider history codecs.
The application projects them into `ToolInvocation` aggregates by provider call ID for lifecycle and
UI.

New optional fields (`tool_id`, normalized status, start/finish timestamps, execution location) can
be added to the JSON stored in `messages.tool_calls`. The decoder must tolerate their absence.

This split is intentional:

- the ledger preserves provider ordering and exact history requirements;
- the aggregate is convenient for orchestration and presentation;
- the transcript renders the aggregate once rather than exposing ledger mechanics.

A future persistence cycle may introduce a normalized `tool_invocations` table if querying,
approval auditing, or progress checkpoints justify it. That migration is not required merely to
improve rendering.

## 4. Tool Registration

### 4.1 Registration is a complete package

Refactor `ToolRegistry::registerTool(std::shared_ptr<ITool>)` toward:

```cpp
struct ToolRegistration {
  ToolDefinition definition;
  std::shared_ptr<IToolExecutor> executor;
  std::shared_ptr<IToolPresenter> presenter;
  QStringList legacy_aliases;
};

void ToolRegistry::registerTool(ToolRegistration registration);
```

For built-in local tools, all three parts are mandatory. This enforces the user's requirement that
each implemented tool has a renderer/presentation path. The registry rejects missing or duplicate
canonical IDs, function names, renderer keys, and aliases. Every built-in/local renderer key must
also resolve to a dedicated QML component. A contract test compares registered built-in tool keys
with `ToolRendererRegistry.registeredKeys()` so runtime generic fallback cannot hide an incomplete
tool package during development.

### 4.2 Definition, execution, and presentation are separate

```cpp
class IToolExecutor {
 public:
  virtual ~IToolExecutor() = default;

  [[nodiscard]] virtual ToolExecutionHandlePtr start(
      const ToolExecutionRequest& request,
      std::function<void(ToolOutcome)> on_finished) = 0;
};

class IToolPresenter {
 public:
  virtual ~IToolPresenter() = default;

  [[nodiscard]] virtual ToolPresentation present(
      const ToolInvocation& invocation) const = 0;
};
```

The executor cannot choose icons or labels. The presenter cannot execute work. Neither returns a QML
component or imports `QtQuick`.

### 4.3 Asynchronous execution handle

```cpp
class ToolExecutionHandle {
 public:
  virtual ~ToolExecutionHandle() = default;
  [[nodiscard]] virtual bool canCancel() const = 0;
  virtual void cancel() = 0;
};
```

`ListFilesExecutor` schedules filesystem work away from the GUI thread, then queues its callback to
the owning application thread. It may return a non-cancellable handle in this cycle. Long-running
tools can later implement cooperative cancellation without changing QML or `ChatController`.

The orchestrator guards completion with invocation ID plus generation/terminal state so a late
callback cannot resurrect a cancelled invocation.

## 5. Presentation Architecture

### 5.1 Presenter output

```cpp
struct ToolPresentation {
  QString renderer_key;       // "filesystem.list" or "generic"
  QString icon_name;
  QString title;              // Semantic state-aware title.
  QString summary;            // Compact result/error metadata.
  QString status_text;
  QVariantMap detail_data;    // Renderer-specific normalized fields.
  QString raw_arguments_json;
  QString raw_result_json;
  bool is_error = false;
  bool can_cancel = false;
};
```

The presenter owns semantic extraction and validation. For example, `ListFilesPresenter` counts
files/directories and converts result entries into a stable `QVariantList`. QML does not parse raw
JSON or know the wire schema.

Raw JSON strings are computed lazily or on expansion to avoid formatting large results for
collapsed rows.

### 5.2 Shared shell

Rename/replace `ToolCallCard.qml` with `ToolActivityCard.qml`. It owns:

- status glyph and semantic color;
- compact title, summary, and duration;
- disclosure button and expansion state;
- Stop button when `canCancel` and status is Running;
- raw-data disclosure and copy action;
- error/approval styling; and
- accessibility.

The shell knows statuses but not concrete tool IDs.

### 5.3 Renderer registry

`ToolRendererRegistry.qml` is a QML singleton or statically owned helper containing `Component`
instances:

```qml
QtObject {
    readonly property Component genericRenderer: Component { GenericToolContent {} }
    readonly property Component listFilesRenderer: Component { ListFilesToolContent {} }

    function componentFor(rendererKey) {
        return rendererKey === "filesystem.list" ? listFilesRenderer : genericRenderer
    }
}
```

The only concrete-ID switch lives at the visual composition boundary. Adding a specialized renderer
requires registering one component there; the generic shell and orchestration stay unchanged.

Every built-in/local tool gets a dedicated expanded renderer, even if its first implementation is a
small wrapper around shared key/value primitives. This keeps the content contract tool-specific and
gives it a safe place to evolve without changing the shared shell. Generic fallback is reserved for
unknown, historical, provider-hosted, or external tools.

An alternative considered was returning `qrc:/...qml` from C++. That was rejected because it makes
application code own UI resource paths, delays errors until runtime, and prevents static QML tooling
from seeing component relationships.

### 5.4 ListFiles renderer

`ListFilesToolContent.qml` receives only normalized presentation data:

```text
path
fileCount
directoryCount
entries: [{name, kind, iconName}]
errorCode
errorMessage
```

The first N entries (recommended N = 20) render initially. `View all` expands the same in-memory
data; it does not execute the tool again. File and folder rows use semantic icons from the existing
icon system. Unknown entry kinds use a neutral document icon.

### 5.5 Generic renderer

`GenericToolContent.qml` renders normalized argument/result sections as key/value content where
possible and provides raw disclosure for everything else. It is the compatibility boundary for:

- historical tools removed from the registry;
- provider-hosted tools without a custom presenter;
- future external/MCP tools;
- malformed or version-skewed result data; and
- incomplete third-party metadata that cannot resolve a dedicated local renderer.

## 6. Transcript Projection and Pairing

### 6.1 Keep chronology, suppress protocol duplication

`MessageListModel` currently projects each `Message` directly. Introduce an internal transcript-row
variant:

```cpp
using TranscriptRow = std::variant<MessageRow, ToolActivityRow>;
```

While consuming chronological conversation messages:

1. An Invocation creates a `ToolActivityRow` at that exact position.
2. A matching Result updates that activity and contributes no visible row.
3. Ordinary messages remain unchanged.
4. An orphan Result creates a generic diagnostic activity at its own position.

The model may retain its public class name for this cycle to avoid a broad QML registration rename,
but comments should state that it is now a transcript projection rather than a one-message/one-row
model.

### 6.2 Live updates

The current “update newest message” assumption is insufficient once a result updates an older
activity row. Add APIs keyed by stable row/invocation ID, such as:

```cpp
void upsertToolActivity(const ToolInvocation& invocation,
                        const ToolPresentation& presentation);
```

Emit `dataChanged` only for the affected row. Do not reset the full model during streaming; resets
would lose expansion state and scroll anchoring.

### 6.3 Expansion state

Expansion is transient view state keyed by application invocation ID. It belongs in the model or
delegate state cache, not SQLite. When status first becomes terminal:

- Completed defaults collapsed unless the user already chose a state.
- Failed/Denied/Cancelled remain expanded.
- unrelated updates never reset the choice.

## 7. Provider Architecture

### 7.1 Common provider contract

The router should stop constructing Anthropic JSON. It passes a provider-neutral catalog snapshot:

```cpp
struct ToolCatalogSnapshot {
  std::vector<ToolDefinition> client_tools;
};
```

Each supporting provider translates this snapshot with its own codec and emits normalized tool
events. The codec responsibilities are:

1. encode tool definitions for a request;
2. decode streamed or buffered call arguments;
3. classify execution location/source;
4. encode local results for continuation; and
5. reconstruct provider history from the protocol ledger.

Do not put these methods on `IToolExecutor`; a filesystem tool must not know Anthropic content
blocks or Google parts.

### 7.2 Anthropic

Anthropic client tools use `tool_use` blocks correlated to `tool_result` blocks by ID. The adapter
already implements most of this path. Refactor its array construction and history helpers behind an
Anthropic codec, and emit the normalized execution location.

Anthropic also distinguishes client-executed and provider-hosted/server tools. Server tool
observations must enter the transcript but bypass the local executor. Mixed client/server turns and
`pause_turn` are provider-loop concerns, not generic renderer concerns.

### 7.3 OpenAI Responses API

OpenAI function calls arrive as `function_call` output items with a `call_id`; local results return
as `function_call_output` items referencing that `call_id`. The OpenAI codec maps those IDs into the
same normalized correlation field used by Anthropic.

Built-in/provider-hosted tool items and custom function calls must be classified separately. The
codec also owns streamed argument accumulation and strict-schema dialect requirements. The rest of
the application sees the same `ToolRequestEvent` and `ToolInvocation` types.

### 7.4 Google Gemini

The current `GoogleProvider` uses `streamGenerateContent`, so its follow-on codec should target that
API’s `functionCall`/`functionResponse` parts and preserve any provider-required history metadata,
including thought signatures where applicable. It must iterate all returned parts rather than
assuming the function call is last.

Google's newer Interactions API uses function-call steps and call IDs. Migrating the provider API is
a separate decision; it should be implemented as a provider adapter/codec change without changing
tool executors, presenters, transcript rows, or QML renderers.

### 7.5 Ollama

Ollama accepts nested function definitions and returns `message.tool_calls`; tool results use tool
role messages. Models and versions may offer different correlation fidelity. When a native call ID
is absent, the Ollama codec creates a conversation/turn-scoped synthetic correlation ID from the
response sequence and call index. That synthetic ID is application identity only and must not be
invented on the provider wire.

Ollama model capability detection belongs in provider selection/policy. A tool being registered
does not imply every Ollama model should receive it.

### 7.6 Provider support matrix

| Concern | Anthropic | OpenAI Responses | Google generateContent | Ollama |
|---|---|---|---|---|
| Definition wrapper | `name`, `description`, `input_schema` | function tool | `functionDeclarations` | `type:function` + nested `function` |
| Call correlation | `tool_use.id` | `call_id` | function call ID where supplied | native ID if supplied, else scoped synthetic ID |
| Local result | `tool_result` | `function_call_output` | `functionResponse` | `role: tool` message |
| Hosted tools | Distinct server tool blocks | Built-in tool items | Built-in tool parts/steps | Provider-specific capabilities |
| History caveat | tool result placement/order | preserve output items/call IDs | preserve model parts/signatures | preserve assistant tool calls + tool messages |

The table is a codec checklist, not a shared JSON schema.

## 8. Availability, Permission, and Disclosure

Future configuration must treat these as independent decisions:

1. **Availability:** Is the tool advertised to this provider/model?
2. **Permission:** May this invocation execute locally?
3. **Disclosure:** May its arguments/result be sent to this provider?

The current Anthropic `tool_calling_enabled` toggle controls only coarse availability. Do not reuse
it as an approval or disclosure grant. `ToolRisk` should describe at least local side effects and
data disclosure separately; “read-only” filesystem access can still reveal sensitive filenames to
a cloud model.

Approval UI can later reuse `ToolActivityCard`'s expanded shell and the predeclared
`AwaitingApproval`, `Denied`, and `Cancelled` states.

## 9. Error Handling

- Unknown function name: create a Failed generic activity and return the provider’s local-tool error
  result; never dispatch an alias ambiguously.
- Malformed arguments: preserve raw input for diagnostics, mark Failed, and do not execute.
- Presenter failure/malformed result: use generic presenter; execution result remains intact for the
  model.
- Renderer key missing: use generic QML component.
- Duplicate provider call ID: fail the duplicate without overwriting the first invocation.
- Process exit with Running invocation: restore as Cancelled/interrupted.
- Late callback after terminal state: ignore and log; never append a second result.

## 10. Testing Strategy

### Domain/application tests

- lifecycle transition table including invalid/terminal transitions;
- alias resolution and duplicate registration rejection;
- mandatory presenter enforcement;
- pair invocation/result by provider call ID;
- orphan/duplicate/malformed fallback behavior;
- asynchronous Running-before-terminal ordering and late-callback suppression;
- presenter outputs for ListFiles success, empty, and error results; and
- persistence decode of legacy and new JSON shapes.

### Provider codec tests

- Anthropic definition encoding, streamed JSON accumulation, client/server classification, result
  encoding, and history reconstruction.
- Future provider cycles must add the same contract suite for their codec.

### QML tests

- one visible activity for an Invocation/Result pair;
- completed collapsed, failed/running expanded;
- specialized renderer selection and generic fallback;
- bounded preview/View all;
- raw disclosure and copy action;
- expansion retained during status/model updates; and
- keyboard/accessibility behavior.

## 11. Delivery Order

1. Introduce normalized identities, lifecycle, presenter contracts, and legacy decoding.
2. Refactor tool registration into complete packages.
3. Add asynchronous execution/orchestration.
4. Add pairing-aware transcript projection.
5. Build shared shell, renderer registry, generic renderer, and ListFiles renderer.
6. Refactor Anthropic translation behind the provider-neutral catalog seam.
7. Verify persistence restoration and the complete UI flow.

This order keeps intermediate changes testable and avoids building QML against an unstable model.

## 12. Alternatives Considered

### Put QML metadata directly on `ITool`

Rejected. Icon/summary metadata is reasonable near a tool, but returning QML component paths from an
executor couples application logic to resource layout. The presenter + renderer-key split keeps the
tool package cohesive without reversing dependencies.

### Allow built-in tools to use the generic renderer permanently

Rejected. It would make renderer completeness unenforceable and encourage raw protocol-shaped UI to
return over time. A simple tool may build its dedicated component from shared generic primitives,
but it still owns a stable renderer key and an explicit content contract.

### Replace all tool messages with one new SQL table immediately

Rejected for this cycle. The current protocol ledger works and is required for history replay. A
presentation projection solves the user-visible problem with a backward-compatible path. Approval
auditing may justify a normalized table later.

### One universal provider tool JSON format

Rejected. Providers differ in definition wrappers, event streams, correlation, hosted tools, result
placement, and history rules. Normalized domain events plus provider-owned codecs share semantics
without pretending the protocols are identical.

### Keep synchronous execution because ListFiles is fast

Rejected. Directory enumeration can still block, and the next tools will not all be fast. More
importantly, synchronous execution prevents a reliable visible Running state and forces another
redesign for cancellation.

## 13. Primary Provider References

- Anthropic tool-use architecture and client/server distinction:
  <https://platform.claude.com/docs/en/agents-and-tools/tool-use/how-tool-use-works>
- OpenAI function calling and `call_id`/`function_call_output`:
  <https://developers.openai.com/api/docs/guides/function-calling>
- Google Gemini function calling (current Interactions API):
  <https://ai.google.dev/gemini-api/docs/function-calling>
- Google Gemini generateContent function calling used by this repository:
  <https://ai.google.dev/gemini-api/docs/generate-content/function-calling>
- Ollama tool calling:
  <https://docs.ollama.com/capabilities/tool-calling>
