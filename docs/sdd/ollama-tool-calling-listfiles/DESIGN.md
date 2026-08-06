# Ollama Provider Tool Calling with ListFiles — DESIGN.md

## Reading Guide

This document designs **how** to wire the existing generic tool-calling framework (`ToolRegistry`,
`ToolOrchestrator`, `ToolRequestEvent`, `ChatController`) into the Ollama provider adapter, per
`docs/sdd/ollama-tool-calling-listfiles/SPEC.md` (REQ-F-001 through REQ-F-019, REQ-NF-001 through
REQ-NF-004, REQ-C-001 through REQ-C-008). It does not relitigate scope: only `ListFiles` is wired,
only the Ollama adapter (plus its config/settings-controller/QML) is touched, there is no approval
gate, `think`/`thinking` stays untouched, and the shared `kMaxToolCallsPerTurn` cap is unchanged.

This is the **fourth** provider to receive tool calling, after Anthropic (incremental
`input_json_delta` accumulation), OpenAI (atomic `response.completed`, string-encoded arguments,
opaque reasoning replay), and Google (atomic `functionCall` parts, `thoughtSignature` replay). Every
domain-level field this cycle needs — `provider_call_id_synthesized` (added by Google's cycle),
`provider_item_id`/`provider_context` (added by OpenAI's cycle) — **already exists** on
`ToolRequestEvent`/`ToolCallEntry`. This is the first tool-calling cycle that requires **zero**
domain-model changes (§2). The two fields Google's cycle added specifically for itself —
`thought_signature` — are simply never set by this cycle's code, per REQ-C-004.

All file paths, struct/field names, and call sites below were read from the live tree during this
pass. Ollama's `/api/chat` `tools`/`tool_calls` wire shapes are not independently re-verified against
an external doc source during this pass (Context7 has no Ollama library indexed the way it does
`ai.google.dev`) — the shapes below follow the SPEC's own pre-established acceptance criteria
(REQ-F-002/006/007) plus the already-shipped `OllamaProvider::sendChat()`/`processLine()` code's
existing conventions, which this design extends rather than replaces. §10 flags this as a residual
risk explicitly, same as the Google cycle flagged its own doc-inconsistency risk.

---

## 1. Component Overview

| Component | Module | New/Modified | Rationale |
|---|---|---|---|
| `OllamaProvider::sendChat()` signature | `holonight_providers` | Modified (`ollama_provider.h`) | Add the 5th `tool_catalog` parameter, byte-for-byte the same shape as `GoogleProvider::sendChat()`/`AnthropicProvider::sendChat()` (§3.1). |
| `OllamaToolCodec` | `holonight_providers` | New (`ollama_tool_codec.h/.cpp`) | Owns Ollama's wire representations for tool definitions, atomic `tool_calls[]` decoding, and assistant/tool message-history reconstruction — same provider-owned-codec pattern `AnthropicToolCodec`/`GoogleToolCodec`/`OpenAIToolCodec` already establish. Keeps `ollama_provider.cpp`'s `processLine()`/`sendChat()` thin (§3). |
| `OllamaProvider::processLine()` / `StreamContext` | `holonight_providers` | Modified (`ollama_provider.cpp`) | Stop ignoring `message.tool_calls`; decode it atomically (no per-index accumulator — the whole array is present in one NDJSON line, REQ-F-003); add `provider_instance_id` to `StreamContext` (currently absent — Ollama never needed it before tool calls existed, same gap Google's `StreamContext` had before its own cycle) (§3.3). |
| `ToolRequestEvent` / `ToolCallEntry` | `holonight_domain` | **Unmodified** | Every field this cycle needs already exists (§2) — no domain-layer diff at all, a first for this feature's four provider cycles. |
| `ChatController` | `holonight_application` | **Unmodified** | Already fully provider-agnostic (dispatches via `model.provider_id`/`router_`, never branches on provider type for tool handling) — confirmed by re-reading `chat_controller.cpp` during this pass; nothing here reads a provider-specific field this cycle would need to add (§5). |
| `ProviderAdapterRouter::sendChat()` | `holonight_application` | Modified (`provider_adapter_router.cpp`) | Parallel Ollama gating branch alongside the existing OpenAI/Anthropic/Google ones (REQ-F-008/009) (§4). |
| `ChatController::dispatchSendChat()` | `holonight_application` | **Unmodified** — correction below | (§5) |
| `OllamaProviderConfig::tool_calling_enabled` | `holonight_config` | Modified (`provider_config.h`) | New boolean field, default `false`, mirroring the other three configs' identical field (§6). |
| `config_repository.cpp` parse/serialize | `holonight_config` | Modified | Extend Ollama's own parse branch (previously the only one of the four `ProviderType` cases with no tool-calling key at all) and the `if constexpr` serialize branch (§6). |
| `ProviderSettingsController::toolCallingEnabled` | `holonight_application` | Modified (`provider_settings_controller.h/.cpp`) | New `Q_PROPERTY` on the existing, misleadingly-generically-named Ollama controller — mirroring `AnthropicProviderSettingsController`'s identical property exactly (§7). |
| `qml/workspace/OllamaSettingsPanel.qml` | QML | Modified | New `Switch` row, copied from `GoogleSettingsPanel.qml`'s "Enable tool calling" row with `GoogleProviderSettingsController` → `ProviderSettingsController` substitution (§7). |
| `tests/providers/test_ollama_tool_codec.cpp` | tests | New | Mirrors `test_openai_tool_codec.cpp`'s structure (§8). |
| `tests/providers/test_ollama_provider.cpp` | tests | Modified | New fake-NDJSON tests appended to the existing fixture (`sendChatParsesMultiLineNdjson`'s helpers already exist there) (§8). |
| `tests/application/test_provider_adapter_router.cpp`, `tests/config/test_config_repository.cpp` | tests | Modified | New tests mirroring the Google-equivalent test names 1:1 (§8). |

**Module layering preserved:** identical to the three precedents. `holonight_providers` still never
depends on `holonight_application`; the tools catalog crosses the boundary as the plain
`ToolCatalogSnapshot` value type, computed by `ProviderAdapterRouter` and handed down into
`OllamaProvider::sendChat()`.

---

## 2. Domain Model Changes: none

Re-read `stream_event.h:69-92` (`ToolRequestEvent`) and `tool_call.h:19-44` (`ToolCallEntry`) during
this pass. Both already carry every field a fourth provider's codec needs:

```cpp
struct ToolRequestEvent {
  QString provider_call_id;
  QString provider_instance_id;
  QString function_name;
  QJsonObject arguments;
  ToolExecutionLocation execution_location = ToolExecutionLocation::LocalClient;
  ToolSource source = ToolSource::BuiltIn;
  std::optional<QString> thought_signature;        // Google-only; Ollama's codec never sets this.
  bool provider_call_id_synthesized = false;        // <- Ollama's codec sets this on every call (§9.2).
  std::optional<QString> provider_item_id;          // OpenAI-only; Ollama's codec never sets this.
  std::vector<QJsonObject> provider_context;        // OpenAI-only; Ollama's codec never sets this.
};
```

`OllamaToolCodec::decodeRequests()` (§3.2) constructs `ToolRequestEvent` leaving
`thought_signature`/`provider_item_id`/`provider_context` at their defaults — REQ-C-004 forbids
touching `think`/`thinking` at all, and Ollama's wire format has no analog to OpenAI's
encrypted-reasoning replay requirement, so there is nothing for these fields to carry for this
provider. `ToolCallEntry` (persisted/replayed form) mirrors the same shape for the same reason.

This means §2 of the three precedent DESIGN.md documents — each of which had to add new optional
fields to these two structs and, as a consequence, touch `conversation_repository_worker.cpp`'s
`encodeToolCalls()`/`decodeToolCalls()` — has **no equivalent section in this cycle**. The
persistence round-trip for `provider_call_id_synthesized` is already wired (Google's cycle added it
generically, not gated to Google), so Ollama's synthesized IDs persist and reload correctly with zero
new code.

---

## 3. Ollama Adapter Changes

### 3.1 `OllamaProvider::sendChat()` signature

`src/providers/include/holonight_providers/ollama_provider.h:51-54`, changed to:

```cpp
HttpRequestHandlePtr sendChat(const holonight_domain::ModelId& model,
                              const std::vector<holonight_domain::Message>& history,
                              const std::function<void(const holonight_domain::StreamEvent&)>& on_event,
                              std::chrono::milliseconds idle_timeout = std::chrono::seconds{30},
                              const holonight_domain::ToolCatalogSnapshot& tool_catalog = {});
```

Byte-for-byte the same shape as `GoogleProvider::sendChat()` (`google_provider.h:67-70`) and
`AnthropicProvider::sendChat()` — default-constructed `ToolCatalogSnapshot{}` keeps every existing
call site (every test in `test_ollama_provider.cpp` predating this cycle, `ChatController`'s legacy
3-arg call at `chat_controller.cpp:148`) compiling and behaving unchanged.

### 3.2 New `OllamaToolCodec`

New files: `src/providers/include/holonight_providers/ollama_tool_codec.h`,
`src/providers/src/ollama_tool_codec.cpp`. Structurally parallel to `GoogleToolCodec` per the
grill session's decision — atomic delivery, `arguments` already a parsed JSON object, not a string —
but with one important wire-shape difference from Google's `functionCall`/`functionResponse`
correlation, explained in §3.2.3.

```cpp
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <expected>
#include <holonight_domain/holonight_domain.h>
#include <vector>

namespace holonight_providers {

// Owns every Ollama /api/chat wire representation used by local tool calling -- mirrors
// GoogleToolCodec's/OpenAIToolCodec's role for their adapters. The application boundary stays
// provider-neutral.
class OllamaToolCodec {
 public:
  // Full "tools" array value for the request body: empty when catalog.client_tools is empty
  // (sendChat() then omits the "tools" key entirely, REQ-F-002); otherwise one
  // {"type":"function","function":{"name","description","parameters"}} object per tool, in
  // registration order.
  [[nodiscard]] static QJsonArray encodeDefinitions(const holonight_domain::ToolCatalogSnapshot& catalog);

  // Builds Ollama's flat "messages" array from `history` (REQ-F-007/018). Unlike Google, Ollama's
  // wire format keeps System-role messages inline (no hoisting) -- roleToOllamaString() already
  // handles System today and this codec does not change that. Consecutive Assistant-role messages
  // (plain text and/or Invocation tool-calls, all from the same original model turn) are grouped
  // into one wire {"role":"assistant", ...} object with a merged "tool_calls" array (§3.2.2);
  // Result-kind entries are never grouped with each other -- each becomes its own standalone
  // {"role":"tool", ...} message, matching REQ-F-007's literal one-message-per-result shape.
  [[nodiscard]] static QJsonArray encodeHistory(const std::vector<holonight_domain::Message>& history);

  // One ToolRequestEvent per entry of `tool_calls` (REQ-F-003/004/019), in array order
  // (REQ-NF-004). `tool_calls` is the *already-extracted* `message.tool_calls` QJsonArray from one
  // NDJSON line -- the caller (OllamaProvider::processLine()) is responsible for locating that
  // array; this function only validates and decodes its entries. Returns an error (not a partial
  // vector) if ANY entry is malformed -- REQ-F-003's acceptance criterion ("malformed JSON in
  // arguments causes the parser to emit a stream error, not a malformed ToolRequestEvent") requires
  // all-or-nothing emission, the same contract OpenAIToolCodec::decodeRequests() already has.
  [[nodiscard]] static std::expected<std::vector<holonight_domain::ToolRequestEvent>, QString> decodeRequests(
      const QString& provider_instance_id, const QJsonArray& tool_calls);
};

}  // namespace holonight_providers
```

#### 3.2.1 `encodeDefinitions()`

```cpp
QJsonArray OllamaToolCodec::encodeDefinitions(const holonight_domain::ToolCatalogSnapshot& catalog) {
  QJsonArray tools;
  for (const auto& definition : catalog.client_tools) {
    tools.append(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("function")},
        {QStringLiteral("function"),
         QJsonObject{{QStringLiteral("name"), definition.function_name},
                    {QStringLiteral("description"), definition.description},
                    {QStringLiteral("parameters"), definition.input_schema}}}});
  }
  return tools;  // empty catalog -> empty array, REQ-F-017's own acceptance criterion verbatim.
}
```

Empty catalog returns an empty `QJsonArray{}`, not `std::nullopt` — `OllamaProvider::sendChat()`
(§3.4) is the layer that decides whether to omit the `"tools"` key from the request body at all,
exactly mirroring `GoogleProvider::sendChat()`'s `if (!tools.isEmpty()) { body[...] = tools; }` guard
(`google_provider.cpp:430-433`).

#### 3.2.2 `encodeHistory()` — grouping rationale

Ollama's `/api/chat` message array has no per-message content-block array the way Anthropic's does;
an assistant turn that both talks and calls a tool is represented as one message object with a
non-empty `"content"` string *and* a `"tool_calls"` array on it, e.g.:

```json
{"role": "assistant", "content": "Let me check.", "tool_calls": [
  {"function": {"name": "list_files", "arguments": {"path": "~/Documents"}}}
]}
```

`ChatController`'s existing framework (established in the origin `tool-calling-listfiles` cycle, and
unchanged since — confirmed by re-reading `chat_controller.cpp`) always creates **one domain
`Message` per tool-use block**, not one `Message` per provider turn — a text preamble and its
following tool call arrive as two separate `Assistant`-role `Message`s in `conversation.messages()`.
Re-serializing them as two separate wire objects (`{"role":"assistant","content":"Let me check."}`
then `{"role":"assistant","content":"","tool_calls":[...]}`) would be schema-valid JSON (REQ-NF-003
only requires that) but would not reproduce the single-message shape Ollama's own chat template
expects a genuine one-turn tool call to have — the same reasoning `GoogleToolCodec::encodeHistory()`
already used to justify its `ContentGroupBuilder` (google-tool-calling-listfiles/DESIGN.md §9.3).
This design follows the same established precedent rather than deviating from it: adjacent
`Assistant`-role messages are grouped into one wire message, merging their text (concatenated) and
their `tool_calls` fragments (concatenated, in order — REQ-NF-004 applies to the reconstructed
history exactly as it does to the initial decode).

**Result-kind entries are deliberately never merged with each other**, even when adjacent (e.g. two
`ListFiles` results from a parallel-call turn). Ollama's `"role":"tool"` message shape (REQ-F-007) is
singular — one `"content"`, one `"tool_name"`, no array to merge into — so N results become N
consecutive standalone `{"role":"tool", ...}` wire messages, matching Ollama's own documented shape
for multiple tool results.

```cpp
// Illustrative grouping loop (ollama_tool_codec.cpp) -- only merges consecutive Assistant-mapped
// wire messages; every other message (User/System/tool-result) is emitted standalone.
QJsonArray OllamaToolCodec::encodeHistory(const std::vector<Message>& history) {
  QJsonArray messages;
  bool hasOpenAssistantGroup = false;
  QJsonObject* openGroup = nullptr;  // points into `messages`'s last element while a group is open

  auto flush = [&] { hasOpenAssistantGroup = false; openGroup = nullptr; };

  for (const Message& message : history) {
    if (!message.toolCalls().empty() && message.toolCalls().front().kind == ToolCallKind::Result) {
      flush();
      for (const auto& entry : message.toolCalls()) {
        messages.append(QJsonObject{
            {QStringLiteral("role"), QStringLiteral("tool")},
            {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(entry.result).toJson(QJsonDocument::Compact))},
            {QStringLiteral("tool_name"), entry.function_name.isEmpty() ? entry.tool_name : entry.function_name}});
      }
      continue;
    }

    // Assistant-mapped: plain text, or an Invocation-carrying message.
    if (message.role() != MessageRole::Assistant) {
      flush();
      messages.append(plainMessage(message));  // {"role": roleToOllamaString(...), "content": text}
      continue;
    }

    if (!hasOpenAssistantGroup) {
      messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                                  {QStringLiteral("content"), message.text()}});
      hasOpenAssistantGroup = true;
      openGroup = &/* last element, taken by reference via QJsonArray's detach-on-write semantics --
                       actual implementation indexes messages[messages.size()-1] and re-inserts, since
                       QJsonArray/QJsonObject do not expose stable references (Qt JSON's own
                       nested-mutation-through-reference pitfall, same one flagged in the Google
                       DESIGN.md §3.4) */;
      continue;
    }
    // merge additional text (rare) and tool_calls into the still-open assistant group...
  }
  flush();
  return messages;
}
```

(The pseudo-reference `openGroup` above is illustrative of the *intent*; the actual implementation
must index-and-reassign into `messages` on every append, exactly as `GoogleToolCodec`'s
`ContentGroupBuilder::add()` already does via its own `current_parts_` member rather than holding a
`QJsonObject&` into the output array — Qt's implicit-sharing JSON types do not support stable
in-place mutation the way `std::vector<T>::back()` does.)

Per-`ToolCallEntry` fragment for an Invocation, appended to the open group's `"tool_calls"` array:

```cpp
QJsonObject{{QStringLiteral("function"),
            QJsonObject{{QStringLiteral("name"),
                        entry.function_name.isEmpty() ? entry.tool_name : entry.function_name},
                       {QStringLiteral("arguments"), entry.input}}}}
```

No `"id"` field on this object — see §3.2.3.

#### 3.2.3 Wire-format difference from Anthropic/Google/OpenAI: no ID correlation on the wire

Anthropic's `tool_use_id`/Google's `functionCall.id`/OpenAI's `call_id` all exist because those three
providers' wire *result* messages carry a correlating ID back (`tool_result.tool_use_id`,
`functionResponse.id`, `function_call_output.call_id`) so the model can match a result to the call
that requested it when multiple calls are in flight. **Ollama's `"role":"tool"` result message, per
REQ-F-007's own acceptance criterion, has no such field at all** — `{"role":"tool","content":...,
"tool_name":...}` correlates by `tool_name` (and message position) only, not by ID. Consequently:

- `OllamaToolCodec::encodeHistory()`'s reconstructed `tool_calls[].function` objects never include an
  `"id"` key, synthesized or otherwise — there is nothing on the wire for it to correlate with.
- `provider_call_id`/`provider_call_id_synthesized` still exist and are still populated by
  `decodeRequests()` (§3.2.4) — they remain load-bearing for `ToolOrchestrator`'s in-memory
  correlation (matching a `ToolRequestEvent` to its eventual result) and for the persisted
  `ToolCallEntry.tool_use_id` field the QML transcript renders — just not for anything Ollama's own
  `/api/chat` endpoint reads back.

This is flagged explicitly (not silently assumed) because it is the one point where this cycle's
codec is structurally simpler than its three predecessors', not because the domain-level ID handling
itself changes.

#### 3.2.4 `decodeRequests()`

```cpp
std::expected<std::vector<ToolRequestEvent>, QString> OllamaToolCodec::decodeRequests(
    const QString& provider_instance_id, const QJsonArray& tool_calls) {
  std::vector<ToolRequestEvent> events;
  events.reserve(static_cast<std::size_t>(tool_calls.size()));

  for (const auto& callValue : tool_calls) {
    const QJsonObject call = callValue.toObject();
    const QJsonObject function = call.value(QStringLiteral("function")).toObject();
    const QString name = function.value(QStringLiteral("name")).toString();
    if (name.isEmpty()) {
      return std::unexpected(QStringLiteral("Ollama tool call missing function name"));
    }

    const QJsonValue argumentsValue = function.value(QStringLiteral("arguments"));
    if (!argumentsValue.isUndefined() && !argumentsValue.isObject()) {
      // REQ-F-006: arguments must be a pre-parsed JSON object, never a JSON-encoded string.
      return std::unexpected(QStringLiteral("Ollama tool call arguments are not a JSON object"));
    }

    QString providerCallId;
    bool synthesized = false;
    const QString rawId = call.value(QStringLiteral("id")).toString();
    if (!rawId.isEmpty()) {
      providerCallId = rawId;
    } else {
      providerCallId = QUuid::createUuid().toString(QUuid::WithoutBraces);  // REQ-F-005
      synthesized = true;
    }

    events.push_back(ToolRequestEvent{
        .provider_call_id = providerCallId,
        .provider_instance_id = provider_instance_id,
        .function_name = name,
        .arguments = argumentsValue.toObject(),
        .execution_location = ToolExecutionLocation::LocalClient,
        .source = ToolSource::BuiltIn,
        .provider_call_id_synthesized = synthesized,
        // thought_signature/provider_item_id/provider_context: left at defaults (§2, REQ-C-004).
    });
  }

  return events;  // REQ-NF-004: vector order == tool_calls[] array order, by construction.
}
```

Validation happens for every entry **before** any `ToolRequestEvent` is constructed and returned —
mirroring `OpenAIToolCodec::decodeRequests()`'s all-or-nothing contract exactly (its own
`RejectsMalformedArgumentsWithoutReturningPartialCalls` test, §8, is the template for Ollama's
equivalent). A `QJsonValue` that is `isUndefined()` (key entirely absent) is treated as "no
arguments" → empty `QJsonObject{}`, not an error — Ollama's own `/api/chat` documentation shows
zero-argument tool calls omitting the `arguments` key entirely, and `ListFilesTool`'s own schema
happens to require a `path`, but that is `ListFilesTool::execute()`'s concern (it already returns
`INVALID_PARAMETERS` for a missing key), not the codec's.

### 3.3 NDJSON parsing changes (`ollama_provider.cpp`)

`StreamContext` (`ollama_provider.cpp:25-32`) gains one field, exactly mirroring
`GoogleProvider`'s identical addition:

```cpp
struct StreamContext {
  QByteArray buffer;
  QString provider_instance_id;  // NEW -- Ollama never needed this before tool calls existed.
  HttpRequestHandlePtr handle;
  bool terminal = false;
  bool completed = false;
  holonight_domain::Usage usage;
  std::optional<QString> model_identifier;
};
```

set once in `sendChat()` (`context->provider_instance_id = instance_id_;`), added alongside the
existing `context->model_identifier = model.model_name;` line.

`processLine()` (`ollama_provider.cpp:60-113`) currently extracts `message.content`, emits
`ContentDelta` unconditionally (even when empty — pre-existing behavior, unchanged by this cycle, not
a REQ-C-004-adjacent concern), then checks `done`. Changed to additionally inspect
`message.tool_calls` between those two steps:

```cpp
const QJsonObject message = object.value(QStringLiteral("message")).toObject();
const QString content = message.value(QStringLiteral("content")).toString();
on_event(StreamEvent{ContentDelta{content}});  // unchanged

if (object.contains(QStringLiteral("model"))) {
  context->model_identifier = object.value(QStringLiteral("model")).toString();
}

if (message.contains(QStringLiteral("tool_calls"))) {  // REQ-F-003
  const QJsonArray toolCalls = message.value(QStringLiteral("tool_calls")).toArray();
  auto decoded = OllamaToolCodec::decodeRequests(context->provider_instance_id, toolCalls);
  if (!decoded.has_value()) {
    failStream(context, on_event, decoded.error());
    return;
  }
  for (const auto& event : *decoded) {  // REQ-F-004/NF-004: array order preserved
    on_event(StreamEvent{event});
  }
}

if (object.value(QStringLiteral("done")).toBool(false)) {
  // ... unchanged terminal handling ...
}
```

No per-index accumulator is introduced — like Google, Ollama delivers the entire `tool_calls` array
atomically in one NDJSON line (REQ-F-003), so `OllamaToolCodec::decodeRequests()` is a pure,
stateless function called at most once per `processLine()` invocation. Unlike Anthropic's
`content_block_start`/`input_json_delta`/`content_block_stop` triad, there is no streaming
accumulation state to add to `StreamContext` for tool calls at all — only the one new
`provider_instance_id` field, needed purely to populate `ToolRequestEvent::provider_instance_id`.

Ollama's existing NDJSON framing has **no equivalent to Anthropic's "no visible content" guard**
(`ollama_provider.cpp`'s `onFinished()` only checks `!context->completed`, unrelated to text
content) — so unlike the Anthropic cycle, this design needs no analogous fix to avoid a false-positive
failure on a tool-only turn. Confirmed by re-reading `onFinished()`/`processLine()` in full during
this pass.

### 3.4 `sendChat()` request-body changes

`ollama_provider.cpp:200-226`'s message-building loop is replaced by a call into the codec, and the
`"tools"` key is added conditionally:

```cpp
HttpRequestHandlePtr OllamaProvider::sendChat(const ModelId& model, const std::vector<Message>& history,
                                              const std::function<void(const StreamEvent&)>& on_event,
                                              std::chrono::milliseconds idle_timeout,
                                              const holonight_domain::ToolCatalogSnapshot& tool_catalog) {
  const QJsonArray messages = OllamaToolCodec::encodeHistory(history);

  QJsonObject options;
  options[QStringLiteral("temperature")] = temperature_;
  options[QStringLiteral("num_ctx")] = context_window_;

  QJsonObject body;
  body[QStringLiteral("model")] = model.model_name;
  body[QStringLiteral("stream")] = true;
  body[QStringLiteral("messages")] = messages;
  body[QStringLiteral("options")] = options;
  const QJsonArray tools = OllamaToolCodec::encodeDefinitions(tool_catalog);
  if (!tools.isEmpty()) {
    body[QStringLiteral("tools")] = tools;  // REQ-F-002
  }
  // Deliberately absent: "think" (REQ-C-004 -- this cycle never sets it, regardless of whether
  // tool_calling_enabled is true).

  // ... request/context construction unchanged except context->provider_instance_id = instance_id_ ...
}
```

Every existing 4-arg call site (tests, and `ChatController`'s legacy path, §5) keeps compiling and
producing byte-identical request bodies for the no-tools case, since `OllamaToolCodec::encodeHistory()`
with no tool-carrying `Message`s in `history` produces the same flat `{"role","content"}`-per-message
array `sendChat()`'s old inline loop did (verified by inspection: the grouping logic in §3.2.2 only
activates for `Assistant`-role messages that are adjacent to *another* Assistant-role message, and
`Message::text()`/`roleToOllamaString()` are otherwise unchanged).

---

## 4. `ProviderAdapterRouter` Changes

`src/application/src/provider_adapter_router.cpp:223-238`. Current three-provider gating
(REQ-F-008/009 for OpenAI/Anthropic/Google, already shipped):

```cpp
holonight_domain::ToolCatalogSnapshot toolCatalog;
if (tool_registry_ != nullptr) {
  if (const auto* openAiConfig =
          std::get_if<holonight_config::OpenAIProviderConfig>(&record_iterator->config.settings);
      openAiConfig != nullptr && openAiConfig->tool_calling_enabled) {
    toolCatalog = tool_registry_->catalogSnapshot();
  } else if (const auto* anthropicConfig =
                 std::get_if<holonight_config::AnthropicProviderConfig>(&record_iterator->config.settings);
             anthropicConfig != nullptr && anthropicConfig->tool_calling_enabled) {
    toolCatalog = tool_registry_->catalogSnapshot();
  } else if (const auto* googleConfig =
                 std::get_if<holonight_config::GoogleProviderConfig>(&record_iterator->config.settings);
             googleConfig != nullptr && googleConfig->tool_calling_enabled) {
    toolCatalog = tool_registry_->catalogSnapshot();
  }
}
```
```cpp
if constexpr (std::is_same_v<Provider, holonight_providers::OpenAIProvider> ||
              std::is_same_v<Provider, holonight_providers::AnthropicProvider> ||
              std::is_same_v<Provider, holonight_providers::GoogleProvider>) {
  return provider->sendChat(model, history, handler, idleTimeout, toolCatalog);
} else {
  return provider->sendChat(model, history, handler, idleTimeout);
}
```

Changed to a fourth parallel `else if` branch and a fourth `if constexpr` disjunct (REQ-F-008/009):

```cpp
holonight_domain::ToolCatalogSnapshot toolCatalog;
if (tool_registry_ != nullptr) {
  if (const auto* openAiConfig =
          std::get_if<holonight_config::OpenAIProviderConfig>(&record_iterator->config.settings);
      openAiConfig != nullptr && openAiConfig->tool_calling_enabled) {
    toolCatalog = tool_registry_->catalogSnapshot();
  } else if (const auto* anthropicConfig =
                 std::get_if<holonight_config::AnthropicProviderConfig>(&record_iterator->config.settings);
             anthropicConfig != nullptr && anthropicConfig->tool_calling_enabled) {
    toolCatalog = tool_registry_->catalogSnapshot();
  } else if (const auto* googleConfig =
                 std::get_if<holonight_config::GoogleProviderConfig>(&record_iterator->config.settings);
             googleConfig != nullptr && googleConfig->tool_calling_enabled) {
    toolCatalog = tool_registry_->catalogSnapshot();
  } else if (const auto* ollamaConfig =
                 std::get_if<holonight_config::OllamaProviderConfig>(&record_iterator->config.settings);
             ollamaConfig != nullptr && ollamaConfig->tool_calling_enabled) {
    toolCatalog = tool_registry_->catalogSnapshot();
  }
}
```
```cpp
if constexpr (std::is_same_v<Provider, holonight_providers::OpenAIProvider> ||
              std::is_same_v<Provider, holonight_providers::AnthropicProvider> ||
              std::is_same_v<Provider, holonight_providers::GoogleProvider> ||
              std::is_same_v<Provider, holonight_providers::OllamaProvider>) {
  return provider->sendChat(model, history, handler, idleTimeout, toolCatalog);
} else {
  return provider->sendChat(model, history, handler, idleTimeout);
}
```

With all four `ProviderSettings` alternatives now covered, the trailing `else` branch of the
`if constexpr` becomes dead code (every `Provider` alternative in the `RuntimeRecord::adapter`
variant matches one of the four `is_same_v` disjuncts). It is **kept** rather than deleted — this
mirrors how each of the three prior cycles left the fallback in place when adding their own branch,
and removing it now would be an unrelated structural change outside REQ-C-002's scope (this cycle
touches "the Ollama provider adapter... and related config," not a `ProviderAdapterRouter` cleanup).
`record_iterator->config.settings`'s variant guarantees at most one of the four `get_if`s is non-null
for a given record, so the `else if` chain is not a correctness shortcut, same reasoning the Google
DESIGN.md already gave for its own three-way chain.

---

## 5. `ChatController` — no changes (correction to task framing)

The task brief for this cycle names `ChatController::dispatchSendChat()`
(`chat_controller.cpp:133-149`) as having "the same gap" as `ProviderAdapterRouter::sendChat()`.
Re-reading the live function during this pass shows that is not quite accurate, and this design
follows the live code rather than the framing, per this project's own established practice (the
Google DESIGN.md made an analogous correction to its SPEC's guessed parameter type):

```cpp
holonight_providers::HttpRequestHandlePtr ChatController::dispatchSendChat(
    const ModelId& model, const std::vector<Message>& history,
    const std::function<void(const StreamEvent&)>& on_event) {
  if (router_ != nullptr) {
    return router_->sendChat(model, history, on_event);
  }
  if (model.provider_id == QStringLiteral("openai")) {
    return openai_provider_->sendChat(model, history, on_event);
  }
  if (model.provider_id == QStringLiteral("anthropic")) {
    return anthropic_provider_->sendChat(model, history, on_event);
  }
  if (model.provider_id == QStringLiteral("google")) {
    return google_provider_->sendChat(model, history, on_event);
  }
  return ollama_provider_->sendChat(model, history, on_event);
}
```

This function has **no `if constexpr`/tool-catalog-gating logic of any kind, for any provider** — it
is a flat 3-arg passthrough for all four providers, including OpenAI/Anthropic/Google, which have had
tool calling for one to three cycles already. It is the `router_ != nullptr` branch's sibling
provider-direct constructor path — confirmed (as the Google DESIGN.md already established at its
own §3.1) to be legacy/test-only: production composition (`apps/chat`) always constructs
`ChatController` via the `ProviderAdapterRouter*` constructor, so `router_` is never null in the
running application, and `dispatchSendChat()`'s four `if (provider_id == ...)` branches only run
under direct-provider-constructor tests. Since none of the four branches passes a tools catalog
today, extending this function for Ollama specifically (adding a 5th branch with gating logic) would
be introducing tool-calling support to the legacy path for the *first* time, for *every* provider at
once — squarely outside REQ-C-002's "only the Ollama provider adapter... and related config" scope,
and not something OpenAI's or Anthropic's or Google's own tool-calling cycles did either (confirmed:
neither `openai-tool-calling-listfiles/DESIGN.md` nor `google-tool-calling-listfiles/DESIGN.md`
touches this function). **No change to `chat_controller.cpp` in this cycle.**

`ChatController`'s tool-execution plumbing itself (`handleStreamEvent()`'s `ToolRequestEvent` branch,
`makeInvocationCallEntry()`/`makeResultCallEntry()`, the `kMaxToolCallsPerTurn` counter) is already
fully provider-agnostic — it consumes `ToolRequestEvent`/`ToolCallEntry` values regardless of which
provider's codec produced them, confirmed by re-reading `chat_controller.cpp` in full during this
pass. Ollama's `ToolRequestEvent`s flow through the exact same code path OpenAI's/Anthropic's/
Google's already do, with zero new branches.

---

## 6. Config/Persistence Changes

### `holonight_config::OllamaProviderConfig` (`src/config/include/holonight_config/provider_config.h:16-23`)

```cpp
struct OllamaProviderConfig {
  QString base_url = QStringLiteral("http://localhost:11434");
  QString default_model;
  int context_window = 4096;
  double temperature = 0.7;
  // REQ-F-010/011/012/013: mirrors AnthropicProviderConfig::tool_calling_enabled /
  // GoogleProviderConfig::tool_calling_enabled / OpenAIProviderConfig::tool_calling_enabled exactly
  // -- whether the tools[] array (ListFiles et al.) is sent to Ollama's /api/chat endpoint at all.
  // Defaults to false for both new and pre-existing instances; a missing key on disk decodes as
  // false (config_repository.cpp), never activating tool-calling without explicit user opt-in.
  bool tool_calling_enabled = false;

  friend bool operator==(const OllamaProviderConfig&, const OllamaProviderConfig&) = default;
};
```

This is the last of the four `ProviderSettings` alternatives to gain this field — after this change,
`tool_calling_enabled` exists on all four (§9.1 discusses the resulting `config_repository.cpp`
simplification opportunity this creates, and why it is deliberately not taken in this cycle).

### `src/config/src/config_repository.cpp`

`parseSettings()`'s `ProviderType::Ollama` case (`:80-94`) is the **only** one of the four that
currently has no tool-calling key handling at all (OpenAI's case reads it directly, Anthropic's and
Google's shared case reads it once before the type split). Changed to read it the same
tolerant-missing way OpenAI's case already does (`.toBool(false)` — REQ-F-012's "missing key ⇒
false" behavior):

```cpp
case ProviderType::Ollama: {
  if (!hasFiniteNumber(object, kContextWindowKey)) {
    return std::nullopt;
  }
  OllamaProviderConfig config;
  config.base_url = object.value(QLatin1String(kBaseUrlKey)).toString(config.base_url);
  config.default_model = object.value(QLatin1String(kDefaultModelKey)).toString(config.default_model);
  config.context_window = object.value(QLatin1String(kContextWindowKey)).toInt(config.context_window);
  config.temperature = object.value(QLatin1String(kTemperatureKey)).toDouble(config.temperature);
  config.tool_calling_enabled = object.value(QLatin1String(kToolCallingEnabledKey)).toBool(false);  // NEW
  if (config.base_url.trimmed().isEmpty() || config.context_window <= 0 || config.temperature < 0.0 ||
      config.temperature > 2.0) {
    return std::nullopt;
  }
  return config;
}
```

`serializeSettings()`'s `if constexpr` (`:157-160`) currently guards
`object[kToolCallingEnabledKey] = config.tool_calling_enabled;` with
`is_same_v<Config, OpenAIProviderConfig> || ... AnthropicProviderConfig ... || ... GoogleProviderConfig`.
Extended to all four:

```cpp
if constexpr (std::is_same_v<Config, OllamaProviderConfig> || std::is_same_v<Config, OpenAIProviderConfig> ||
              std::is_same_v<Config, AnthropicProviderConfig> || std::is_same_v<Config, GoogleProviderConfig>) {
  object[QLatin1String(kToolCallingEnabledKey)] = config.tool_calling_enabled;
}
```

(§9.1 discusses — and rejects, for this cycle — collapsing this now-universal-across-all-four-types
`if constexpr` into the same always-run block as `base_url`/`default_model`/`temperature` just above
it.)

### `holonight_persistence`

No changes. `ToolCallEntry`'s `provider_call_id_synthesized` round-trip through
`conversation_repository_worker.cpp`'s `encodeToolCalls()`/`decodeToolCalls()` was already wired
generically by Google's cycle (its own §2.1) — it is not gated to Google-sourced entries, so Ollama's
synthesized IDs persist and reload with zero new code, confirmed by re-reading the worker file's
current `encodeToolCalls()`/`decodeToolCalls()` implementation during this pass.

---

## 7. Settings Controller + QML Changes

### `ProviderSettingsController` (`provider_settings_controller.h/.cpp`)

This is Ollama's existing, oddly-generically-named settings controller (predates the
per-provider-named convention `AnthropicProviderSettingsController`/`GoogleProviderSettingsController`
established later — its class comment already says so). Gains `toolCallingEnabled`, mirroring
`AnthropicProviderSettingsController`'s identical property (`anthropic_provider_settings_controller.h`)
exactly:

Header additions (`provider_settings_controller.h`):
```cpp
Q_PROPERTY(bool toolCallingEnabled READ toolCallingEnabled WRITE setToolCallingEnabled NOTIFY toolCallingEnabledChanged)
...
[[nodiscard]] bool toolCallingEnabled() const;
void setToolCallingEnabled(bool value);
...
 Q_SIGNALS:
  void toolCallingEnabledChanged();
...
 private:
  bool tool_calling_enabled_ = false;
```

`.cpp` additions, mirroring `anthropic_provider_settings_controller.cpp` line-for-line:
- `connectDraftProperties()` (`provider_settings_controller.cpp:49-56`): add
  `connect(this, &ProviderSettingsController::toolCallingEnabledChanged, this, &ProviderSettingsController::updateDraft);`
- Getter/setter, identical shape to `AnthropicProviderSettingsController::toolCallingEnabled()`/
  `setToolCallingEnabled()` (`anthropic_provider_settings_controller.cpp:84-92`).
- `loadDraft()` (`:91-105`): add `setToolCallingEnabled(config->tool_calling_enabled);` after the
  existing `setTemperature(config->temperature);` line.
- `updateDraft()` (`:107-115`): add `.tool_calling_enabled = tool_calling_enabled_` to the
  `OllamaProviderConfig{...}` aggregate initializer passed to `draftSession()->setSettings(...)`.

### `qml/workspace/OllamaSettingsPanel.qml`

Confirmed by reading the live file: `OllamaSettingsPanel.qml` has four `ProviderFormActionRow`
blocks today (server URL; default model + refresh; context window/temperature; authentication + test
connection) and no reset-panel toggle for tool calling. `GoogleSettingsPanel.qml`'s "Enable tool
calling" row (`GoogleSettingsPanel.qml:100-112`) is inserted between the context-window/temperature
row and the authentication row, with `GoogleProviderSettingsController` → `ProviderSettingsController`
substitution only (this is the one panel among the four where the controller's QML type name does
**not** contain "Ollama" — `ProviderSettingsController` is itself the singleton name, per its
`QML_SINGLETON` declaration):

```qml
ProviderFormActionRow {
    Layout.fillWidth: true
    fieldContent: Component {
        HnFormField {
            labelText: qsTr("Enable tool calling")
            helperText: qsTr("Lets the model list files under your home directory.")
            Switch {
                checked: ProviderSettingsController.toolCallingEnabled
                onToggled: ProviderSettingsController.toolCallingEnabled = checked
            }
        }
    }
}
```

No other QML file references `toolCallingEnabled` in an Ollama context (verified by grep across
`qml/`) — this is the only insertion point.

---

## 8. Testing Strategy

### 8.1 `tests/providers/test_ollama_tool_codec.cpp` (new)

Mirrors `test_openai_tool_codec.cpp`'s structure (four `TEST(OllamaToolCodec, ...)` cases covering
encode-definitions, decode-requests, and encode-history — same shape as OpenAI's file, since Ollama's
codec, like OpenAI's, decodes a whole array at once rather than Google's per-part `decodeRequest()`):

```cpp
#include "holonight_providers/ollama_tool_codec.h"

#include <QJsonDocument>

#include <gtest/gtest.h>

namespace holonight_providers {
namespace {

TEST(OllamaToolCodec, EncodesToolDefinitionsAsFunctionObjects) {
  const holonight_domain::ToolCatalogSnapshot catalog{
      .client_tools = {{
          .function_name = QStringLiteral("list_files"),
          .description = QStringLiteral("List files"),
          .input_schema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}},
      }}};

  const QJsonArray definitions = OllamaToolCodec::encodeDefinitions(catalog);

  ASSERT_EQ(definitions.size(), 1);
  const QJsonObject entry = definitions.at(0).toObject();
  EXPECT_EQ(entry.value(QStringLiteral("type")), QStringLiteral("function"));
  const QJsonObject function = entry.value(QStringLiteral("function")).toObject();
  EXPECT_EQ(function.value(QStringLiteral("name")), QStringLiteral("list_files"));
  EXPECT_TRUE(function.value(QStringLiteral("parameters")).isObject());
}

TEST(OllamaToolCodec, EncodeDefinitionsReturnsEmptyArrayForEmptyCatalog) {
  EXPECT_TRUE(OllamaToolCodec::encodeDefinitions({}).isEmpty());
}

TEST(OllamaToolCodec, DecodeRequestsUsesProvidedIdVerbatim) {
  const QJsonArray toolCalls{QJsonObject{
      {QStringLiteral("id"), QStringLiteral("call_xyz")},
      {QStringLiteral("function"),
       QJsonObject{{QStringLiteral("name"), QStringLiteral("list_files")},
                  {QStringLiteral("arguments"), QJsonObject{{QStringLiteral("path"), QStringLiteral("~")}}}}}}};

  const auto decoded = OllamaToolCodec::decodeRequests(QStringLiteral("ollama-1"), toolCalls);

  ASSERT_TRUE(decoded.has_value());
  ASSERT_EQ(decoded->size(), 1U);
  EXPECT_EQ(decoded->at(0).provider_call_id, QStringLiteral("call_xyz"));
  EXPECT_FALSE(decoded->at(0).provider_call_id_synthesized);
}

TEST(OllamaToolCodec, DecodeRequestsSynthesizesUuidWhenIdAbsent) {
  const QJsonArray toolCalls{QJsonObject{
      {QStringLiteral("function"),
       QJsonObject{{QStringLiteral("name"), QStringLiteral("list_files")},
                  {QStringLiteral("arguments"), QJsonObject{{QStringLiteral("path"), QStringLiteral("~")}}}}}}};

  const auto decoded = OllamaToolCodec::decodeRequests(QStringLiteral("ollama-1"), toolCalls);

  ASSERT_TRUE(decoded.has_value());
  ASSERT_EQ(decoded->size(), 1U);
  EXPECT_TRUE(decoded->at(0).provider_call_id_synthesized);
  EXPECT_FALSE(QUuid::fromString(decoded->at(0).provider_call_id).isNull());
}

TEST(OllamaToolCodec, DecodeRequestsEmitsMultipleEventsInArrayOrder) {
  // two entries, distinct "path" arguments -> asserts decoded->at(0)/at(1) preserve array order
  // (REQ-F-004/NF-004).
}

TEST(OllamaToolCodec, DecodeRequestsFailsOnMissingFunctionName) { /* REQ-F-003 */ }

TEST(OllamaToolCodec, DecodeRequestsFailsWhenArgumentsIsAJsonStringNotObject) {
  // "arguments":"not an object" -> REQ-F-006's acceptance criterion.
}

TEST(OllamaToolCodec, DecodeRequestsRejectsAllCallsWhenAnyEntryIsMalformed) {
  // mirrors OpenAIToolCodec's RejectsMalformedArgumentsWithoutReturningPartialCalls -- one good
  // entry followed by one malformed entry must return std::unexpected, not a 1-element vector.
}

TEST(OllamaToolCodec, EncodeHistoryReconstructsToolCallAndResultAsSeparateWireMessages) {
  // Invocation + Result Message pair -> asserts an {"role":"assistant",...,"tool_calls":[...]}
  // message immediately followed by an {"role":"tool","content":...,"tool_name":...} message,
  // matching REQ-F-007's literal shape.
}

TEST(OllamaToolCodec, EncodeHistoryGroupsConsecutiveAssistantTextAndInvocationIntoOneMessage) {
  // plain-text Assistant Message immediately followed by an Invocation Assistant Message -> asserts
  // ONE wire message with both the original text in "content" and the tool call in "tool_calls"
  // (§3.2.2 grouping decision).
}

TEST(OllamaToolCodec, EncodeHistoryDoesNotMergeConsecutiveResultMessages) {
  // two Result Messages back-to-back (parallel-call turn) -> asserts TWO separate "role":"tool"
  // wire messages, not one with an array (§3.2.3).
}

}  // namespace
}  // namespace holonight_providers
```

### 8.2 `tests/providers/test_ollama_provider.cpp` (extended)

New `TEST(OllamaProvider, ...)` cases appended to the existing fixture (which already has
`sendChatParsesMultiLineNdjson`-style NDJSON-line helpers to build on):

1. **`SendChatIncludesToolsArrayInRequestBodyWhenProvided`** — non-empty `ToolCatalogSnapshot` ⇒
   request body's `"tools"` is `[{"type":"function","function":{"name":"list_files",...}}]`.
2. **`SendChatOmitsToolsKeyWhenCatalogEmpty`** — default (empty) catalog ⇒ no `"tools"` key
   (regression guard — every pre-existing `SendChat*` test in this file must keep passing
   unmodified, since a tool-call-free `history` must still serialize to the exact same `"messages"`
   array `sendChat()`'s old inline loop produced).
3. **`SendChatEmitsToolRequestEventOnSingleToolCallLine`** — one NDJSON line with
   `"message":{"tool_calls":[...]},"done":false` ⇒ exactly one `ToolRequestEvent`, correct
   `function_name`/`arguments`, `execution_location == LocalClient`, `source == BuiltIn`
   (REQ-F-003).
4. **`SendChatEmitsMultipleToolRequestEventsForMultipleToolCallsInOrder`** — two entries in one
   line's `tool_calls` array ⇒ two `ToolRequestEvent`s in array order (REQ-F-004/NF-004).
5. **`SendChatFailsStreamOnMalformedToolCallArguments`** — `"arguments":"not an object"` ⇒ an
   `Error` event, not a garbage `ToolRequestEvent` (REQ-F-003/006's acceptance criterion).
6. **`SendChatContinuesToEmitDoneLineAfterToolCallLine`** — feeds the two-line sequence REQ-F-003
   describes literally (`tool_calls` line with `done:false`, then a closing `done:true` line with no
   `tool_calls`) ⇒ asserts both a `ToolRequestEvent` and, afterward, a `Completed` event fire, and
   that usage/model-identifier stamping on the `Completed` event is unaffected by the intervening
   tool call.

No test in either file makes a live network call (REQ-NF-002) — every new test uses the same
`FakeHttpClient`/manual NDJSON-line construction already used by every pre-existing test in
`test_ollama_provider.cpp`.

### 8.3 `ProviderAdapterRouter` tests (`tests/application/test_provider_adapter_router.cpp`)

Mirroring `googleConfig()`/`SendsToolsArrayToGoogleWhenToolCallingEnabled` /
`OmitsToolsArrayWhenGoogleToolCallingDisabled` 1:1:
- `ollamaConfig(QString id, bool toolCallingEnabled)` helper, same shape as `googleConfig()`.
- `SendsToolsArrayToOllamaWhenToolCallingEnabled`
- `OmitsToolsArrayWhenOllamaToolCallingDisabled`
- `OmitsToolsArrayWithoutRegistryEvenWhenOllamaToolCallingEnabled`

### 8.4 `config_repository` tests (`tests/config/test_config_repository.cpp`)

Mirroring `GoogleToolCallingEnabledRoundTripsTrue`/`GoogleToolCallingEnabledRoundTripsFalse`/
`GoogleToolCallingEnabledDefaultsToFalseWhenKeyMissingFromOnDiskConfig` 1:1:
- `OllamaToolCallingEnabledRoundTripsTrue` / `OllamaToolCallingEnabledRoundTripsFalse`
- `OllamaToolCallingEnabledDefaultsToFalseWhenKeyMissingFromOnDiskConfig`

### 8.5 `ProviderSettingsController` tests

Mirroring the Anthropic/Google controllers' existing `tool_calling_enabled`-load/save coverage
(`test_anthropic_provider_settings_controller.cpp`'s equivalents), added to whichever existing
`test_provider_settings_controller.cpp` (Ollama's own, not to be confused with the per-provider
controller test files) already exercises `loadDraft()`/`updateDraft()`/`save()` for this class.

---

## 9. Alternatives Considered

### 9.1 Lifting `kToolCallingEnabledKey` serialization out of the `if constexpr`

With this cycle's change, all four `ProviderSettings` alternatives have `tool_calling_enabled`,
exactly the way all four already have `base_url`/`default_model`/`temperature` (serialized
unconditionally, before the `if constexpr`, `config_repository.cpp:147-149`). Considered collapsing
`serializeSettings()`'s tool-calling `if constexpr` into that same unconditional block, since it is
no longer discriminating anything. Rejected for this cycle: REQ-C-002 scopes this cycle to "the
Ollama provider adapter... and related config" — restructuring a block that already correctly serves
all four types (merely with one redundant-looking `if constexpr` condition) is a code-cleanup change
orthogonal to shipping Ollama's tool-calling support, and every one of the three prior cycles made the
equivalent minimal, additive edit to this same function rather than opportunistically restructuring
it. Flagged as a natural follow-up for a future `simplify`-style pass, not deferred silently.

### 9.2 Wire-level ID correlation: omit entirely vs. synthesize-and-still-send

Considered inventing a synthetic `"id"` field on the wire `tool_calls[].function` object even though
Ollama's own result-message shape (REQ-F-007) has nowhere to echo it back — i.e., sending an `id` the
codec knows will never be read. Rejected: it would be dead weight on every request body for no
behavioral benefit (Ollama's server does not consume or require it), and would misleadingly suggest
to a future reader of a captured request body that Ollama's tool protocol is ID-correlated the way
Anthropic's/Google's/OpenAI's are, when REQ-F-007's own message shape proves it is not. The domain-
layer `provider_call_id`/`provider_call_id_synthesized` fields remain fully populated and functional
for `ToolOrchestrator`'s in-process correlation and for transcript rendering — only the wire
serialization omits it, and §3.2.3 documents why explicitly rather than leaving a silent asymmetry
between decode-side (reads `id` when present) and encode-side (never writes one) behavior.

### 9.3 Grouping consecutive Result-kind entries into one wire message

Considered merging N parallel `ListFiles` results from one turn into a single `"role":"tool"` message
with an array `"content"`, symmetric with how Invocation entries merge into one `"tool_calls"` array
(§3.2.2). Rejected: REQ-F-007's own acceptance criterion describes the tool-result shape as singular
(`"content":<result text>` — one result, not an array of results), and Ollama's own documented
multi-tool-result convention is N consecutive standalone `"role":"tool"` messages, not one merged
message — mirroring N separate function outputs the way OpenAI's Responses API also does (`api.output`
gets N separate `function_call_output` items, never merged). Grouping only the invocation side (where
Ollama's own wire format demonstrably supports an array) while leaving the result side unmerged (where
it demonstrably does not) is not an inconsistency — it is fidelity to two different documented shapes
for two different message kinds.

---

## 10. Known Risks

1. **Ollama's `tool_calls`/`tools` wire shapes were verified against Ollama's official docs
   (`docs/api.md`, `api/types.go`) via Context7 during the grill session preceding this SPEC** —
   `tools:[{"type":"function","function":{name,description,parameters}}]` request shape,
   `message.tool_calls:[{"function":{"name","arguments"}}]` atomic (not incremental) response
   delivery, `arguments` as a pre-parsed JSON object, `{"role":"tool","content","tool_name"}`
   result-message shape, and the near-always-absent `id` field are all doc-confirmed, not guessed —
   REQ-F-002/006/007 encode this directly. What remains genuinely unverified is *live-server/live-model*
   behavior: doc examples are not a substitute for an actual response from a real local Ollama
   installation and a real tool-calling-capable model. A fake-fixture test suite built against the
   documented shapes cannot catch a real server/model combination that deviates from its own docs (e.g.
   splitting `tool_calls` across two NDJSON lines, a model fine-tune emitting a non-object `arguments`
   despite the docs, or a server version populating `id` after all). Recommend a one-time manual smoke
   test against a real local Ollama server with a tool-calling-capable model (e.g. `qwen2.5` or
   `llama3.1`) before this ships, treating the doc-verified shapes as high-confidence but not a
   substitute for that smoke test.

2. **`provider_call_id_synthesized == true` is expected to be the dominant, near-universal path for
   Ollama in practice** (SPEC's own REQ-F-005 note, restated here as a design-level risk, not just a
   test-coverage note). Every other provider's tool-calling cycle could reasonably expect
   model-provided IDs to be common; this cycle's own `decodeRequests()` (§3.2.4) handles the
   synthesized path correctly and it is exercised by dedicated tests (§8.1), but the *practical*
   consequence — nearly every Ollama tool-call `ToolCallEntry` in a real user's persisted conversation
   will have `provider_call_id_synthesized == true` and a locally-generated UUID as its
   `tool_use_id` — has no functional impact (§3.2.3 already establishes the wire format never reads
   this ID back) but is worth naming explicitly so a future engineer investigating "why do all my
   Ollama tool calls have random UUIDs instead of provider IDs" does not mistake it for a bug.

3. **Real-world Ollama tool-call streaming behavior may vary across local server versions and
   models**, more so than the three cloud-hosted providers, where the vendor controls both server and
   API version simultaneously. A self-hosted Ollama instance's version, and the specific local model's
   own tool-calling fine-tuning quality, are both fully outside this codebase's control — a chunk
   shape assumption that holds for one server/model combination (e.g. always populating `"id"`, or
   never splitting `tool_calls` across two NDJSON lines) might not hold for another. REQ-C-005 already
   accepts "an HTTP error for the request" as the compensating control when a *model* does not support
   tool calling; this risk is the narrower, adjacent case of a model/server that claims to support tool
   calling but delivers a wire shape this codec's atomic, single-line assumption (REQ-F-003) does not
   anticipate. `OllamaToolCodec::decodeRequests()`'s all-or-nothing validation (§3.2.4) fails closed
   (a stream error, not a crash or silent partial tool call) for any shape it does not recognize, which
   is the best available mitigation without a captured real-world fixture corpus to test against.

4. **`OllamaProviderConfig::context_window`/`temperature` validation in `parseSettings()` is
   unaffected, but is co-located with the new `tool_calling_enabled` read** (§6) — a future edit to
   either validation block risks an unrelated merge conflict or copy-paste error in the other, purely
   because they now sit in the same `case ProviderType::Ollama:` block. Low risk (the same is already
   true of the other three providers' cases, and none has had an incident), noted for completeness
   rather than as a novel concern this cycle introduces.
