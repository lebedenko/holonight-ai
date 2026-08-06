# Google (Gemini) Provider Tool Calling with ListFiles — DESIGN.md

## Reading Guide

This document designs **how** to wire the existing generic tool-calling framework (`ToolRegistry`,
`ToolOrchestrator`, `ToolRequestEvent`, `ChatController`) into the Google (Gemini) provider adapter,
per `docs/sdd/google-tool-calling-listfiles/SPEC.md` (REQ-F-001 through REQ-F-018, REQ-NF-001
through REQ-NF-004, REQ-C-001 through REQ-C-006). It does not relitigate scope: only `ListFiles` is
wired, only the Google adapter is touched (plus the minimal shared plumbing that touching it
requires — see §2), there is no approval gate, and the `kMaxToolCallsPerTurn` cap is unchanged.

**Correction to SPEC.md REQ-F-001:** the SPEC's guessed parameter type,
`std::optional<std::vector<domain::ToolDefinition>>`, does not match the live codebase.
`AnthropicProvider::sendChat()` (`src/providers/include/holonight_providers/anthropic_provider.h:61-65`)
actually takes a 5th parameter `const holonight_domain::ToolCatalogSnapshot& tool_catalog = {}`,
where `ToolCatalogSnapshot` is `struct { std::vector<ToolDefinition> client_tools; }`
(`src/domain/include/holonight_domain/tool_activity.h:125-127`). `GoogleProvider::sendChat()` gains
the identical 5th parameter, not an `optional<vector<...>>`.

All file paths, struct/field names, and call sites below were read from the live tree during this
pass (no excerpts assumed from SPEC.md's prose). Gemini's `functionCall`/`functionResponse`/
`thoughtSignature` wire shapes were independently verified via Context7 against
`ai.google.dev/gemini-api` docs (`generate-content/function-calling`,
`generate-content/thought-signatures`, `generate-content/gemini-3`,
`generate-content/whats-new-gemini-3.5`) during this design pass — see §3.5 for the exact JSON this
verification produced, and §9 for where those docs disagree with themselves.

---

## 1. Component Overview

| Component | Module | New/Modified | Rationale |
|---|---|---|---|
| `GoogleProvider::sendChat()` signature | `holonight_providers` | Modified (`google_provider.h`) | Add the 5th `tool_catalog` parameter, mirroring `AnthropicProvider::sendChat()` exactly (§3.1). |
| `GoogleToolCodec` | `holonight_providers` | New (`google_tool_codec.h/.cpp`) | Owns Gemini's wire representations for tool definitions, atomic `functionCall` decoding, and `functionCall`/`functionResponse` history reconstruction — the provider-owned-codec pattern `AnthropicToolCodec` already establishes (CLAUDE.md's module layout names this explicitly). Keeps `google_provider.cpp`'s `routeSseEvent()`/`sendChat()` thin, same shape as the Anthropic file (§3). |
| `GoogleProvider::routeSseEvent()` / `StreamContext` | `holonight_providers` | Modified (`google_provider.cpp`) | Stop ignoring `functionCall` parts; extract them atomically (no per-index accumulator, unlike Anthropic); add `provider_instance_id` to `StreamContext` (currently absent — Google never needed it before tool calls existed) (§3.3). |
| `ToolRequestEvent` | `holonight_domain` | Modified (`stream_event.h`) | Add `std::optional<QString> thought_signature` and `bool provider_call_id_synthesized` — neither field exists today; both are required to satisfy REQ-F-005/006/008/009 (§2). |
| `ToolCallEntry` | `holonight_domain` | Modified (`tool_call.h`) | Mirror the same two fields so the values survive from the in-turn `ToolRequestEvent` into persisted/replayable conversation history, where `GoogleToolCodec::encodeHistory()` reads them back on the follow-up turn (§2, §3.4). |
| `ChatController::handleToolCall()` + `makeInvocationCallEntry()`/`makeResultCallEntry()` | `holonight_application` | Modified (`chat_controller.cpp`) | Thread the two new fields from `ToolRequestEvent` into every `ToolCallEntry` construction site (§5) — this file is already provider-agnostic, so no `if (provider == "google")` branch is introduced. |
| `ProviderAdapterRouter::sendChat()` | `holonight_application` | Modified (`provider_adapter_router.cpp`) | Parallel Google gating branch alongside the existing Anthropic-only one (REQ-F-010/011) (§4). |
| `GoogleProviderConfig::tool_calling_enabled` | `holonight_config` | Modified (`provider_config.h`) | New boolean field, default `false`, mirroring `AnthropicProviderConfig`'s existing field exactly (§6). |
| `config_repository.cpp` parse/serialize | `holonight_config` | Modified | Extend the already-shared Anthropic/Google parse block and the `if constexpr` serialize branch to cover both types (§6). |
| `conversation_repository_worker.cpp` `encodeToolCalls()`/`decodeToolCalls()` | `holonight_persistence` | Modified | Additive optional-field round-trip for `thought_signature`/`provider_call_id_synthesized` — no schema migration, `tool_calls` is a JSON-blob TEXT column (§2.1). |
| `GoogleProviderSettingsController::toolCallingEnabled` | `holonight_application` | Modified (`google_provider_settings_controller.h/.cpp`) | New `Q_PROPERTY`, mirroring `AnthropicProviderSettingsController`'s identical property exactly (§7). |
| `qml/workspace/GoogleSettingsPanel.qml` | QML | Modified | New `Switch` row, copied verbatim from `AnthropicSettingsPanel.qml`'s "Enable tool calling" row (§7). |
| `tests/providers/test_google_provider.cpp` | tests | Modified | New fake-SSE tests mirroring `test_anthropic_provider.cpp`'s tool-calling suite; one **pre-existing** test (`SendChatIgnoresFunctionCallAndOtherNonTextParts`) must be split/renamed since its assertion becomes false (§8). |
| `tests/application/test_provider_adapter_router.cpp`, `tests/config/test_config_repository.cpp`, `tests/application/test_google_provider_settings_controller.cpp` | tests | Modified | New tests mirroring the Anthropic-equivalent test names 1:1 (§8). |

**Module layering preserved:** identical to the Anthropic precedent. `holonight_providers` still never
depends on `holonight_application`; the tools catalog crosses the boundary as the plain
`ToolCatalogSnapshot` value type, computed by `ProviderAdapterRouter` and handed down into
`GoogleProvider::sendChat()`.

---

## 2. Domain Model Changes

Verified against the live `stream_event.h`/`tool_call.h` — neither the "prior investigation"'s
implied shape nor the old `tool-calling-listfiles` DESIGN.md's `ToolCallEntry` shape has a field for
anything resembling `thought_signature`. Both must be added new.

### `holonight_domain::ToolRequestEvent` (`src/domain/include/holonight_domain/stream_event.h:68-77`)

```cpp
struct ToolRequestEvent {
  QString provider_call_id;
  QString provider_instance_id;
  QString function_name;
  QJsonObject arguments;
  ToolExecutionLocation execution_location = ToolExecutionLocation::LocalClient;
  ToolSource source = ToolSource::BuiltIn;

  // New (REQ-F-005): Gemini 3.x thinking-model functionCall parts carry an opaque signature that
  // must be echoed verbatim in the follow-up turn's reconstructed history. std::nullopt when absent
  // (older/non-thinking models, and unconditionally for every non-Google provider — Anthropic/
  // OpenAI/Ollama never populate this).
  std::optional<QString> thought_signature;

  // New (REQ-F-006/009): true when `provider_call_id` above was client-synthesized (Gemini omitted
  // "id") rather than sourced from the model. Threaded through so the follow-up-turn codec knows
  // whether to omit "id" from the reconstructed functionResponse (REQ-F-009) — string-format
  // sniffing on provider_call_id itself was rejected as a design; see §9.2.
  bool provider_call_id_synthesized = false;

  friend bool operator==(const ToolRequestEvent&, const ToolRequestEvent&) = default;
};
```

### `holonight_domain::ToolCallEntry` (`src/domain/include/holonight_domain/tool_call.h:18-35`)

Same two fields, added for the same reason `tool_use_id`/`is_error` etc. already live here: this is
the type that survives into `Message::toolCalls()`, gets persisted
(`conversation_repository_worker.cpp`), and is read back by `GoogleToolCodec::encodeHistory()` on
every subsequent turn of the same conversation — not just the immediate follow-up.

```cpp
struct ToolCallEntry {
  ToolCallKind kind = ToolCallKind::Invocation;
  QString tool_use_id;
  QString tool_name;
  QString tool_id;
  QString function_name;
  QJsonObject input;
  QJsonObject result;
  bool is_error = false;
  ToolInvocationStatus status = ToolInvocationStatus::Requested;
  QDateTime requested_at;
  std::optional<QDateTime> started_at;
  std::optional<QDateTime> finished_at;
  ToolExecutionLocation execution_location = ToolExecutionLocation::LocalClient;
  bool can_cancel = false;

  // New — mirrors ToolRequestEvent's fields of the same name/purpose (see above). Populated only on
  // Invocation-kind entries sourced from a Google functionCall; every other provider's entries leave
  // these at their defaults (nullopt / false), and Result-kind entries never set them.
  std::optional<QString> thought_signature;
  bool provider_call_id_synthesized = false;

  friend bool operator==(const ToolCallEntry&, const ToolCallEntry&) = default;
};
```

`operator==`'s `= default` picks up the two new members automatically — no manual update needed,
but any existing test relying on `ToolCallEntry` equality with a hand-built expected value must add
these fields explicitly or their default (`nullopt`/`false`) will simply compare equal to a
not-yet-migrated construction site, which is what makes this change backward-compatible for every
non-Google call site without touching them.

### 2.1 Knock-on: persistence round-trip (necessary, not scope creep)

`tool_calls` is a single JSON-array-in-a-TEXT-column (`conversation_repository_worker.cpp:243-283`,
manually field-by-field encoded/decoded — no ORM). Without extending `encodeToolCalls()`/
`decodeToolCalls()`, a conversation that reaches disk (any completed turn) and is reloaded (app
restart, or switching conversations in the sidebar) would silently drop `thought_signature` and
`provider_call_id_synthesized` on the next send, breaking REQ-F-008's "reasoning continuity" the
moment the app restarts mid-conversation — this is exactly the kind of persisted-then-replayed round
trip `ChatController::send()`'s `conversation.messages()` already exercises for ordinary tool calls
today. No SQL migration file is needed (unlike the original `0008_add_message_tool_calls.sql`
migration that introduced the column itself) — this is two more optional keys in the same JSON blob,
the same category of additive change already used for `can_cancel`/`tool_id`/`function_name` at
`conversation_repository_worker.cpp:253-263`:

```cpp
// encodeToolCalls(), inside the per-entry loop, alongside the existing can_cancel guard:
if (entry.thought_signature.has_value()) {
  object[QStringLiteral("thought_signature")] = *entry.thought_signature;
}
if (entry.provider_call_id_synthesized) {
  object[QStringLiteral("provider_call_id_synthesized")] = true;
}
```

```cpp
// decodeToolCalls(), alongside the existing entry.can_cancel = ...; line:
if (object.contains(QStringLiteral("thought_signature"))) {
  entry.thought_signature = object.value(QStringLiteral("thought_signature")).toString();
}
entry.provider_call_id_synthesized = object.value(QStringLiteral("provider_call_id_synthesized")).toBool();
```

This is flagged explicitly rather than silently bundled: REQ-C-002 says "only the Google provider
adapter... and related config" are touched, and `holonight_persistence` is shared infrastructure, not
Google-specific. It is included here as the same category of unavoidable cross-cutting touch the
*precedent* Anthropic cycle itself made when it first introduced `ToolCallEntry`/`Message::tool_calls_`
into `holonight_domain` (see that cycle's own DESIGN.md §1 row for exactly this) — adding a new
optional field to an existing domain type invariably touches its (de)serializer. It is zero-risk to
the three untouched providers (additive, defaulted).

---

## 3. Google Adapter Changes

### 3.1 `GoogleProvider::sendChat()` signature

`src/providers/include/holonight_providers/google_provider.h:67-70`, changed to:

```cpp
HttpRequestHandlePtr sendChat(const holonight_domain::ModelId& model,
                              const std::vector<holonight_domain::Message>& history,
                              const std::function<void(const holonight_domain::StreamEvent&)>& on_event,
                              std::chrono::milliseconds idle_timeout = std::chrono::seconds{30},
                              const holonight_domain::ToolCatalogSnapshot& tool_catalog = {});
```

Byte-for-byte the same shape as `AnthropicProvider::sendChat()`
(`anthropic_provider.h:61-65`) — default-constructed `ToolCatalogSnapshot{}` keeps every existing
call site (tests predating this cycle, `ChatController::dispatchSendChat()`'s legacy 3-arg call at
`chat_controller.cpp:141`) compiling and behaving unchanged, exactly like Anthropic's own comment at
`anthropic_provider.h:56-58` already documents for itself.

### 3.2 New `GoogleToolCodec`

New files, structurally parallel to `anthropic_tool_codec.h/.cpp`:
`src/providers/include/holonight_providers/google_tool_codec.h`,
`src/providers/src/google_tool_codec.cpp`.

```cpp
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

#include <holonight_domain/holonight_domain.h>

namespace holonight_providers {

// Owns every Gemini wire representation used by local tool calling — mirrors AnthropicToolCodec's
// role for the Google adapter. The application boundary stays provider-neutral.
class GoogleToolCodec {
 public:
  // Full "tools" array value for the request body: empty when catalog.client_tools is empty
  // (sendChat() then omits the "tools" key entirely, REQ-F-002); otherwise a single-element array
  // `[{"functionDeclarations": [...]}]` per Gemini's Tool schema — NOT one Tool object per function
  // (that is Anthropic's shape, not Gemini's).
  [[nodiscard]] static QJsonArray encodeDefinitions(const holonight_domain::ToolCatalogSnapshot& catalog);

  // Builds Gemini's "contents" array from `history`, hoisting System-role messages into
  // `system_parts` (REQ-F-003/004, behavior unchanged from GoogleProvider::sendChat()'s current
  // inline loop, now relocated here) and reconstructing functionCall/functionResponse parts from
  // ToolCallEntry (REQ-F-007/008/009). Consecutive same-role Messages are grouped into one Content
  // entry with multiple parts (§3.4/§9.3) — mirrors Gemini's own documented parallel-call shape and
  // AnthropicToolCodec::encodeHistory()'s MessageGroupBuilder precedent.
  [[nodiscard]] static QJsonArray encodeHistory(const std::vector<holonight_domain::Message>& history,
                                                QStringList& system_parts);

  // One ToolRequestEvent per functionCall Part object (REQ-F-003/004/005/006). `part` is the
  // *Part*-level JSON object — the object with sibling keys "functionCall" and, optionally,
  // "thoughtSignature" — not the inner functionCall object alone (§3.5). Returns std::nullopt when
  // the part is malformed (missing "name", or "args" present but not a JSON object) so the caller
  // can fail the stream (REQ-F-003's malformed-JSON acceptance criterion) instead of emitting a
  // garbage event.
  [[nodiscard]] static std::optional<holonight_domain::ToolRequestEvent> decodeRequest(
      QString provider_instance_id, const QJsonObject& part);

  // functionResponse Part for one Result-kind ToolCallEntry. Omits "id" when
  // entry.provider_call_id_synthesized is true (REQ-F-009); includes it verbatim (from
  // entry.tool_use_id) otherwise.
  [[nodiscard]] static QJsonObject encodeFunctionResponse(const holonight_domain::ToolCallEntry& entry);
};

}  // namespace holonight_providers
```

`decodeRequest()` returns `std::optional<ToolRequestEvent>` rather than a bare `ToolRequestEvent`
(unlike `AnthropicToolCodec::decodeRequest()`, which cannot fail — Anthropic's `content_block_stop`
handler already validated the accumulated JSON via `QJsonDocument::fromJson()` before ever calling
it). Gemini's atomic delivery means this codec function is the *only* place malformed-`args`
detection can happen, so it must be able to signal failure to its caller.

### 3.3 SSE parsing changes (`google_provider.cpp`)

`StreamContext` (`google_provider.cpp:33-40`) gains one field:

```cpp
struct StreamContext {
  QByteArray buffer;
  QString provider_instance_id;  // NEW — Google never needed this before tool calls existed.
  HttpRequestHandlePtr handle;
  bool terminal = false;
  bool completed = false;
  holonight_domain::Usage usage;
  std::optional<QString> model_identifier;
};
```

set once in `sendChat()` (`context->provider_instance_id = instance_id_;`, mirroring
`anthropic_provider.cpp:447`'s identical line) — currently Google's `StreamContext` has no analog to
Anthropic's `provider_instance_id` at all, because nothing needed it before `ToolRequestEvent`
(which requires it) could ever be emitted.

`routeSseEvent()` (`google_provider.cpp:216-273`): the existing part-scanning loop
(`google_provider.cpp:243-251`) currently reads:

```cpp
for (const auto& partValue : parts) {
  const QJsonObject part = partValue.toObject();
  if (part.contains(QStringLiteral("text"))) {  // ignores functionCall/inlineData parts (REQ-C-001/002)
    deltaText += part.value(QStringLiteral("text")).toString();
  }
}
```

Changed to extract text and collect functionCall parts in the same pass, then decode+emit
functionCall events **after** any text delta but **before** the `finishReason` handling below it —
Gemini routinely delivers text, a functionCall, and a terminal `finishReason` all in the same final
chunk, so ordering the three `on_event()` calls within one `routeSseEvent()` invocation is this
function's sole responsibility (there is no second SSE block to separate them, unlike Anthropic's
`content_block_stop`/`message_stop` distinct event types):

```cpp
QString deltaText;
QJsonArray functionCallParts;  // Part-level objects, not just the inner functionCall sub-object.
for (const auto& partValue : parts) {
  const QJsonObject part = partValue.toObject();
  if (part.contains(QStringLiteral("text"))) {
    deltaText += part.value(QStringLiteral("text")).toString();
  } else if (part.contains(QStringLiteral("functionCall"))) {
    functionCallParts.append(part);
  }
  // inlineData and any other part kind remain a deliberate no-op (unchanged).
}
if (!deltaText.isEmpty()) {
  on_event(StreamEvent{ContentDelta{deltaText}});
}

for (const auto& partValue : functionCallParts) {  // REQ-F-004/NF-004: array order preserved.
  auto request = GoogleToolCodec::decodeRequest(context->provider_instance_id, partValue.toObject());
  if (!request.has_value()) {
    failStream(context, on_event, QStringLiteral("Malformed function call from Google"));
    return;
  }
  on_event(StreamEvent{*request});
}
```

No per-index accumulator (`StreamContext::tool_use_blocks`-equivalent) is introduced — Gemini
delivers `name`+`args` atomically in one chunk (REQ-F-003), so there is nothing to accumulate across
chunks; `GoogleToolCodec::decodeRequest()` is a pure, stateless function called once per part.

The removed comment `// ignores functionCall/inlineData parts (REQ-C-001/002)` referenced the *old*
`tool-calling-listfiles` SPEC's constraint tags, which no longer apply to Google now that this cycle
wires it — the replacement comment above says so plainly rather than leaving a stale REQ reference.

### 3.4 History-reconstruction changes

`sendChat()`'s current inline history loop (`google_provider.cpp:412-425`) is replaced by a call into
the new codec:

```cpp
QStringList systemParts;
const QJsonArray contents = GoogleToolCodec::encodeHistory(history, systemParts);
```

`GoogleToolCodec::encodeHistory()` groups consecutive same-role `Message`s into one Gemini `Content`
entry with a multi-`Part` array — the same `MessageGroupBuilder` pattern
`AnthropicToolCodec::encodeHistory()` already uses (`anthropic_tool_codec.cpp:32-80`), adapted to
Gemini's `{role, parts}` shape instead of Anthropic's `{role, content}`. This choice is deliberate,
not incidental — see §9.3 for why grouping (rather than one `Content` entry per tool-call `Message`,
which is how `ChatController` currently appends them one-at-a-time) was chosen.

Per `Message`:
- `MessageRole::System` → hoisted into `system_parts` exactly as today (REQ-F-003/004, unchanged).
- Non-empty `toolCalls()`, `ToolCallKind::Invocation` entries → one `Content{role: "model", parts: [...]}`,
  one `functionCall` Part per entry:
  ```cpp
  QJsonObject functionCallPart{
      {QStringLiteral("functionCall"),
       QJsonObject{{QStringLiteral("name"),
                    entry.function_name.isEmpty() ? entry.tool_name : entry.function_name},
                   {QStringLiteral("args"), entry.input}}}};
  if (!entry.provider_call_id_synthesized && !entry.tool_use_id.isEmpty()) {
    functionCallPart[QStringLiteral("functionCall")].toObject()[QStringLiteral("id")] = entry.tool_use_id;
    // (illustrative — actual implementation builds the inner object before insertion, QJsonObject's
    // nested-mutation-through-reference pitfall applies here same as everywhere else in Qt.)
  }
  if (entry.thought_signature.has_value()) {
    functionCallPart[QStringLiteral("thoughtSignature")] = *entry.thought_signature;  // sibling of "functionCall", not nested inside it (§3.5).
  }
  ```
- Non-empty `toolCalls()`, `ToolCallKind::Result` entries → one `Content{role: "user", parts: [...]}`,
  one `functionResponse` Part per entry via `GoogleToolCodec::encodeFunctionResponse()`:
  ```cpp
  QJsonObject GoogleToolCodec::encodeFunctionResponse(const ToolCallEntry& entry) {
    QJsonObject functionResponse{
        {QStringLiteral("name"), entry.function_name.isEmpty() ? entry.tool_name : entry.function_name},
        {QStringLiteral("response"), entry.result}};
    if (!entry.provider_call_id_synthesized && !entry.tool_use_id.isEmpty()) {
      functionResponse[QStringLiteral("id")] = entry.tool_use_id;  // REQ-F-009
    }
    return QJsonObject{{QStringLiteral("functionResponse"), functionResponse}};
  }
  ```
  This directly satisfies REQ-F-009: a synthesized `provider_call_id` (client-side UUID, used only
  for `ToolOrchestrator` correlation, per SPEC's own glossary) never leaks into the wire-level
  `functionResponse.id` field the model never asked for. `Message::toolCalls()`' `Result`-kind
  entries are already role-`User` by construction (`chat_controller.cpp`'s `on_terminal` callback
  builds `Message result(..., MessageRole::User, ...)`), so `roleToGoogleString()`'s existing
  `User → "user"` mapping requires no change for this to land in the right `Content.role`.
- Otherwise (plain text, no tool calls) → unchanged from today's behavior: `{role, parts: [{"text": ...}]}`.

### 3.5 Verified Gemini wire shapes (Context7, `ai.google.dev/gemini-api`)

Fetched live during this design pass (not assumed from SPEC.md):

```json
{
  "role": "model",
  "parts": [
    {
      "functionCall": { "name": "check_weather", "args": { "city": "Paris" } },
      "thoughtSignature": "<Signature_A>"
    },
    {
      "functionCall": { "name": "check_weather", "args": { "city": "London" } }
    }
  ]
}
```
(source: `generate-content/gemini-3`) — confirms **`thoughtSignature` is a sibling key of
`functionCall` within the same `Part` object**, not nested inside `functionCall`'s own object. Also
confirms REQ-F-005's "attached only when present" framing is literal: the docs' own parallel-call
example shows the signature on the *first* call only, never on subsequent parallel calls in the same
turn — `GoogleToolCodec::decodeRequest()` naturally reproduces this since it checks `part.contains(
"thoughtSignature")` independently per part, with no cross-part state.

```
"For Gemini 3 models, a unique `id` is always returned with every `functionCall`, which must be
included in your `functionResponse`..."
```
(source: `generate-content/function-calling`) — confirms REQ-F-006/009's id-present-vs-absent split
maps onto a real model-generation boundary (Gemini 3.x vs. older), not a hypothetical.

`functionResponse` shape confirmed as `{"name": ..., "response": {...}, "id": ...}` (id conditional)
across three independent doc pages (`function-calling`, `whats-new-gemini-3.5`, `thought-signatures`).

**One inconsistency found and resolved (§9.1):** the general `thought-signatures` doc page uses
snake_case `thought_signature` in its own JSON examples, while the Gemini-3-specific page and the
`whats-new-gemini-3.5` page both use camelCase `thoughtSignature`. This design chooses **camelCase**
— consistent with every other Gemini REST field this codebase already sends/parses successfully
(`functionCall`, `functionResponse`, `systemInstruction`, `generationConfig`, `maxOutputTokens`,
`usageMetadata`, `promptTokenCount`, `modelVersion`, `finishReason`, `promptFeedback`,
`blockReason` — all camelCase, verified live in the existing `google_provider.cpp`). Flagged as a
residual risk in §10.

---

## 4. `ProviderAdapterRouter` Changes

`src/application/src/provider_adapter_router.cpp:210-265`. Current Anthropic-only gating
(REQ-F-010/011):

```cpp
holonight_domain::ToolCatalogSnapshot toolCatalog;
if (tool_registry_ != nullptr) {
  if (const auto* anthropicConfig =
          std::get_if<holonight_config::AnthropicProviderConfig>(&record_iterator->config.settings);
      anthropicConfig != nullptr && anthropicConfig->tool_calling_enabled) {
    toolCatalog = tool_registry_->catalogSnapshot();
  }
}
```
```cpp
if constexpr (std::is_same_v<Provider, holonight_providers::AnthropicProvider>) {
  return provider->sendChat(model, history, handler, idleTimeout, toolCatalog);
} else {
  return provider->sendChat(model, history, handler, idleTimeout);
}
```

Changed to an `else if` parallel branch (REQ-F-010/011) — deliberately not a single combined
condition, since the two `config.settings` alternatives are different types and `std::get_if` must
name each explicitly:

```cpp
holonight_domain::ToolCatalogSnapshot toolCatalog;
if (tool_registry_ != nullptr) {
  if (const auto* anthropicConfig =
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
if constexpr (std::is_same_v<Provider, holonight_providers::AnthropicProvider> ||
              std::is_same_v<Provider, holonight_providers::GoogleProvider>) {
  return provider->sendChat(model, history, handler, idleTimeout, toolCatalog);
} else {
  return provider->sendChat(model, history, handler, idleTimeout);
}
```

The `record_iterator->config.settings` variant guarantees at most one of `anthropicConfig`/
`googleConfig` is non-null for a given record (the variant holds exactly one alternative, matching
`record_iterator->adapter`'s own type by construction in `createProviderAdapter()`), so `else if`
here is not a correctness shortcut — it is simply avoiding one redundant `get_if` call once the
first branch's config type is confirmed to not match.

The stale scope comment directly above this block (`provider_adapter_router.cpp:222-224`,
*"Runtime policy (REQ-C-002): only Anthropic receives the provider-neutral local catalog this cycle.
OpenAI, Google, and Ollama intentionally receive none..."*) is rewritten to describe the new
two-provider reality, not deleted — it is exactly the kind of comment a future third adapter's
author needs to find and extend correctly.

`OmitsToolsArrayWithoutRegistryEvenWhenEnabled` (`test_provider_adapter_router.cpp:244`) already
exercises the "`tool_registry_ == nullptr` ⇒ never send tools regardless of the flag" guard for
Anthropic; the mirrored Google test (§8) exercises the identical guard for the new branch.

---

## 5. `ChatController` Changes

`src/application/src/chat_controller.cpp` is already fully provider-agnostic (it dispatches via
`model.provider_id`/`router_`, never branches on provider type for tool handling) — no new
provider-specific logic is added here, only field threading so `thought_signature`/
`provider_call_id_synthesized` survive from `ToolRequestEvent` into every `ToolCallEntry` this file
constructs:

- `makeInvocationCallEntry()` (`chat_controller.cpp:35-56`) — add
  `.thought_signature = tool_request.thought_signature,` and
  `.provider_call_id_synthesized = tool_request.provider_call_id_synthesized,` to the aggregate
  initializer, reading from its existing `tool_request` parameter.
- `makeResultCallEntry()` (`chat_controller.cpp:58-73`) — Result-kind entries never carry a
  `thought_signature` per Gemini's own schema (only `functionCall` parts do); left unchanged, matching
  `ToolCallEntry`'s own field-comment ("Result-kind entries never set them," §2).
- `handleToolCall()`'s two direct `ToolCallEntry{...}` construction sites
  (`chat_controller.cpp:296-304` for the provider-hosted early-return path, and `:351-358` for the
  `Requested`-status invocation) — same two fields added, read from the function's existing
  `tool_call` parameter (the `ToolRequestEvent`). The provider-hosted path is not exercised by this
  cycle (`ListFilesTool` is `LocalClient`/`BuiltIn`, per `list_files_tool.cpp:105-127`) but is kept
  consistent for whenever a future provider-hosted Google tool exists.

No other `chat_controller.cpp` logic changes: `continueToolLoop()`, `stop()`, `dispatchSendChat()`'s
legacy 3-arg call sites, and the tool-call-limit/turn-counting logic in `handleToolCall()`
(`chat_controller.cpp:313-335`) are untouched — REQ-C-004's cap applies via the same
`stream.tool_calls_this_turn` counter regardless of which provider emitted the `ToolRequestEvent`
that incremented it (see §10's note on why parallel calls interact with this counter identically to
today, not differently).

---

## 6. Config/Persistence Changes

### `holonight_config::GoogleProviderConfig` (`src/config/include/holonight_config/provider_config.h:60-67`)

```cpp
struct GoogleProviderConfig {
  QString base_url = QStringLiteral("https://generativelanguage.googleapis.com");
  QString default_model;
  double temperature = 1.0;
  int max_output_tokens = 8192;
  // REQ-F-012/013/014: mirrors AnthropicProviderConfig::tool_calling_enabled exactly (see that
  // field's own comment at :44-47) — whether the tools[] array is sent to Gemini's
  // streamGenerateContent API at all. Defaults to false for both new and pre-existing instances; a
  // missing key on disk decodes as false (config_repository.cpp), never activating tool-calling
  // without explicit user opt-in.
  bool tool_calling_enabled = false;

  friend bool operator==(const GoogleProviderConfig&, const GoogleProviderConfig&) = default;
};
```

### `src/config/src/config_repository.cpp`

`parseSettings()`'s existing shared `Anthropic`/`Google` case (`:105-136`) currently only reads
`kToolCallingEnabledKey` inside the `if (type == ProviderType::Anthropic)` sub-branch (`:122-130`),
leaving Google's branch (`:132-135`) never reading the key at all. Changed to read it once, shared,
before the type split (REQ-F-014's "missing key ⇒ false" behavior, `.toBool(false)`, applies
identically to both — same line already used for Anthropic):

```cpp
const bool toolCallingEnabled = object.value(QLatin1String(kToolCallingEnabledKey)).toBool(false);
if (type == ProviderType::Anthropic) {
  return AnthropicProviderConfig{.base_url = baseUrl,
                                 .default_model = defaultModel,
                                 .temperature = temperature,
                                 .max_output_tokens = maxOutputTokens,
                                 .tool_calling_enabled = toolCallingEnabled};
}
return GoogleProviderConfig{.base_url = baseUrl,
                            .default_model = defaultModel,
                            .temperature = temperature,
                            .max_output_tokens = maxOutputTokens,
                            .tool_calling_enabled = toolCallingEnabled};
```

`serializeSettings()`'s `if constexpr` (`:150-156`) currently guards
`object[kToolCallingEnabledKey] = config.tool_calling_enabled;` with
`is_same_v<Config, AnthropicProviderConfig>` only. Extended to both:

```cpp
if constexpr (std::is_same_v<Config, AnthropicProviderConfig> || std::is_same_v<Config, GoogleProviderConfig>) {
  object[QLatin1String(kToolCallingEnabledKey)] = config.tool_calling_enabled;
}
```

(The pre-existing `kMaxOutputTokensKey` guard immediately above already covers both types together —
this is the same established pattern, not a new one.)

### `holonight_persistence`

See §2.1 — additive `encodeToolCalls()`/`decodeToolCalls()` extension, no migration file.

---

## 7. Settings Controller + QML Changes

### `GoogleProviderSettingsController` (`google_provider_settings_controller.h/.cpp`)

Mirrors `AnthropicProviderSettingsController`'s `toolCallingEnabled` property
(`anthropic_provider_settings_controller.h:27-28,42-43,55,61`) exactly:

Header additions:
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
- `connectDraftProperties()`: add
  `connect(this, &GoogleProviderSettingsController::toolCallingEnabledChanged, this, &GoogleProviderSettingsController::updateDraft);`
- Getter/setter, identical shape to `AnthropicProviderSettingsController::toolCallingEnabled()`/`setToolCallingEnabled()` (`:84-92`).
- `loadDraft()`: add `setToolCallingEnabled(config->tool_calling_enabled);` after the existing
  `setMaxOutputTokens(config->max_output_tokens);` line.
- `updateDraft()`: add `.tool_calling_enabled = tool_calling_enabled_` to the
  `GoogleProviderConfig{...}` aggregate passed to `draftSession()->setSettings(...)`.

### `qml/workspace/GoogleSettingsPanel.qml`

Confirmed by grep as the only Google-settings QML file (there is no separate "advanced" panel).
The Anthropic panel's toggle row (`AnthropicSettingsPanel.qml:100-112`) is inserted verbatim into the
Google panel between the existing "Temperature/Max output tokens" `ProviderFormActionRow`
(`GoogleSettingsPanel.qml:67-99`) and the "API key" `ProviderFormActionRow` (currently
`GoogleSettingsPanel.qml:100-129`), with `Anthropic` → `Google` substitutions only:

```qml
ProviderFormActionRow {
    Layout.fillWidth: true
    fieldContent: Component {
        HnFormField {
            labelText: qsTr("Enable tool calling")
            helperText: qsTr("Lets the model list files under your home directory.")
            Switch {
                checked: GoogleProviderSettingsController.toolCallingEnabled
                onToggled: GoogleProviderSettingsController.toolCallingEnabled = checked
            }
        }
    }
}
```

No other QML file references `toolCallingEnabled` or "Enable Tool Calling" (verified by grep across
`qml/`) — this is the only insertion point.

---

## 8. Testing Strategy

All new provider-level tests live in `tests/providers/test_google_provider.cpp`, mirroring
`test_anthropic_provider.cpp`'s fixture-building style (`makeFakeWithEmptyModels()`, `testModel()`,
`sseBlock()`-equivalent helpers) but adapted to Gemini's chunk shape — Gemini has no
`content_block_start`/`content_block_delta`/`content_block_stop` lifecycle, so there is no
`toolUseStart()`/`inputJsonDelta()`/`contentBlockStop()` triad to mirror; instead, new helpers build
one complete `candidates[0].content.parts[]` chunk per test, since that is how Gemini actually
delivers a `functionCall`:

```cpp
QJsonObject functionCallPart(const QString& name, const QJsonObject& args, const QString& id = QString(),
                             const QString& thoughtSignature = QString()) {
  QJsonObject functionCall{{QStringLiteral("name"), name}, {QStringLiteral("args"), args}};
  if (!id.isEmpty()) functionCall[QStringLiteral("id")] = id;
  QJsonObject part{{QStringLiteral("functionCall"), functionCall}};
  if (!thoughtSignature.isEmpty()) part[QStringLiteral("thoughtSignature")] = thoughtSignature;
  return part;
}

QByteArray candidateChunk(const QJsonArray& parts, const QString& finishReason = QString()) {
  QJsonObject candidate{{QStringLiteral("content"), QJsonObject{{QStringLiteral("parts"), parts}}}};
  if (!finishReason.isEmpty()) candidate[QStringLiteral("finishReason")] = finishReason;
  return QByteArray("data: ") +
         QJsonDocument(QJsonObject{{QStringLiteral("candidates"), QJsonArray{candidate}}}).toJson(QJsonDocument::Compact) +
         "\n\n";
}
```

New `TEST(GoogleProvider, ...)` cases (naming mirrors the SPEC's own outline 1:1):

1. **`SendChatIncludesToolsArrayInRequestBodyWhenProvided`** — non-empty `ToolCatalogSnapshot` ⇒
   request body's `"tools"` is `[{"functionDeclarations": [{"name": "list_files", ...}]}]`.
2. **`SendChatOmitsToolsKeyWhenToolsAbsentOrEmpty`** — default (empty) catalog ⇒ no `"tools"` key
   (regression guard: every existing non-tool `SendChat*` test in this file must keep passing
   unmodified, since `GoogleToolCodec::encodeHistory()` must be byte-identical to today's inline loop
   for tool-call-free history).
3. **`SendChatEmitsToolCallOnAtomicFunctionCallPart`** — one `functionCallPart(...)` chunk ⇒ exactly
   one `ToolRequestEvent`, correct `name`/`args`, `execution_location == LocalClient`,
   `source == BuiltIn`.
4. **`SendChatEmitsMultipleToolCallsForMultipleFunctionCallPartsInOrder`** — two `functionCallPart(...)`
   entries in one chunk ⇒ two `ToolRequestEvent`s in array order (REQ-F-004/NF-004).
5. **`SendChatCapturesThoughtSignatureWhenPresent`** / **`SendChatOmitsThoughtSignatureWhenAbsent`** —
   split pair mirroring Anthropic's granularity; first asserts `thought_signature.has_value()` and its
   value, second asserts `std::nullopt` for a chunk with no `thoughtSignature` key.
6. **`SendChatUsesProviderSuppliedIdVerbatim`** / **`SendChatSynthesizesUuidWhenIdAbsent`** — first
   asserts `provider_call_id == "call_123"` and `provider_call_id_synthesized == false`; second
   asserts a syntactically valid UUID (`QUuid::fromString(...)` non-null) and
   `provider_call_id_synthesized == true`.
7. **`SendChatFailsStreamOnFunctionCallMissingName`** / **`SendChatFailsStreamOnNonObjectArgs`** —
   malformed-part coverage (REQ-F-003's acceptance criterion), asserting an `Error` event, not a
   garbage `ToolRequestEvent`.
8. **`SendChatReconstructsFunctionCallAndFunctionResponseAcrossTurns`** — full round-trip mirroring
   `SendChatReconstructsToolUseAndToolResultAsSeparateMessagesAcrossTurns`
   (`test_anthropic_provider.cpp:568-620`): builds an `Invocation`+`Result` `ToolCallEntry` pair via
   `Message::setToolCalls()`, calls `sendChat()`, and inspects the request body's `contents` array for
   the reconstructed `functionCall`/`functionResponse` parts, correct `name`/`args`/`response`.
9. **`SendChatEchoesThoughtSignatureOnFollowUp`** — same round-trip shape as #8, with
   `thought_signature` set on the `Invocation` entry; asserts the reconstructed `functionCall` Part
   contains the same `thoughtSignature` value. Non-signature round-trip variant asserts the key is
   absent entirely (not present-and-null).
10. **`SendChatOmitsIdInFunctionResponseWhenIdWasSynthesized`** / **`SendChatIncludesIdInFunctionResponseWhenIdWasFromModel`**
    — round-trip pair keyed on `provider_call_id_synthesized`, asserting `functionResponse.id`
    presence/absence (REQ-F-009).
11. **`SendChatGroupsParallelFunctionCallsIntoOneContentEntry`** — round-trip with two `Invocation`
    entries from the same original turn; asserts both `functionCall` parts land in a single `Content`
    element's `parts` array (§3.4/§9.3 grouping decision), not two separate `Content` elements.

**Pre-existing test requiring modification:** `SendChatIgnoresFunctionCallAndOtherNonTextParts`
(`test_google_provider.cpp:497-509`) currently asserts `EXPECT_TRUE(events.empty())` for a chunk
containing a bare `{"functionCall":{"name":"x"}}` part — this assertion becomes **false** once
functionCall parsing ships (it will now emit a `ToolRequestEvent`). Split into two tests:
`SendChatIgnoresInlineDataAndOtherNonTextNonFunctionCallParts` (keeps the original intent — non-text,
non-functionCall parts remain ignored, using an `inlineData` part instead) and folds the
`functionCall`-specific half into new test #3 above.

**`ProviderAdapterRouter` tests** (`tests/application/test_provider_adapter_router.cpp`), mirroring
`anthropicConfig()`/`SendsToolsArrayToAnthropicWhenToolCallingEnabled` (`:34-41`, `:180-198`) and
`OmitsToolsArrayWhenAnthropicToolCallingDisabled`/`OmitsToolsArrayWithoutRegistryEvenWhenEnabled`
(`:227-259`) 1:1:
- `googleConfig(QString id, bool toolCallingEnabled)` helper, same shape as `anthropicConfig()`.
- `SendsToolsArrayToGoogleWhenToolCallingEnabled`
- `OmitsToolsArrayWhenGoogleToolCallingDisabled`
- `OmitsToolsArrayWithoutRegistryEvenWhenGoogleToolCallingEnabled`

**`config_repository` tests** (`tests/config/test_config_repository.cpp`), mirroring
`AnthropicToolCallingEnabledRoundTripsTrue`/`AnthropicToolCallingEnabledDefaultsToFalseWhenKeyMissingFromOnDiskConfig`
(`:389-429`) 1:1:
- `GoogleToolCallingEnabledRoundTripsTrue` / `GoogleToolCallingEnabledRoundTripsFalse`
- `GoogleToolCallingEnabledDefaultsToFalseWhenKeyMissingFromOnDiskConfig`

**`GoogleProviderSettingsController` tests**, mirroring the Anthropic controller's existing
`tool_calling_enabled`-load/save coverage (`test_anthropic_provider_settings_controller.cpp:43,55,119,141,151`).

**Persistence round-trip test** (new, `holonight_persistence` test target — see §10 risk #4): a
`ToolCallEntry` with `thought_signature` set and `provider_call_id_synthesized == true` survives
`encodeToolCalls()` → `decodeToolCalls()` unchanged.

No test makes a live network call (REQ-NF-002) — every new test uses `FakeHttpClient`, same as every
existing test in these files.

---

## 9. Alternatives Considered

### 9.1 `thought_signature` casing: snake_case vs camelCase

Google's own docs disagree with themselves (§3.5). Considered defensively accepting *either* key
name in `decodeRequest()`. Rejected: REQ-NF-001 requires deterministic parsing, and silently
accepting two spellings for a security/correctness-relevant echo field (feeding the wrong value back
to a thinking model corrupts its reasoning chain, not just a display glitch) hides a real
ambiguity behind "it works either way" rather than surfacing it. Chose camelCase only, matching this
codebase's own already-live-verified 100%-camelCase Gemini field convention, and flagged the
ambiguity explicitly in §10 rather than papering over it with dual-key acceptance.

### 9.2 ID-synthesis signaling: format heuristic vs explicit boolean

Considered inferring "was this ID synthesized" from `provider_call_id`'s format (e.g., "looks like a
`QUuid`" vs "looks like Gemini's own `function-call-<uuid>` shape from the docs' own example").
Rejected: Gemini's real IDs are themselves UUID-shaped in the observed example
(`function-call-f3b9ecb3-d55f-4076-98c8-b13e9d1c0e01`), so a naive "is this a bare UUID" heuristic
would misclassify a real model-provided ID as synthesized (or vice versa) depending on exactly how
Gemini formats it, which is not contractually documented. Chose an explicit
`provider_call_id_synthesized` boolean, set exactly once at `decodeRequest()` time from the one
authoritative signal (whether the wire JSON contained an `"id"` key at all) and threaded verbatim —
the same category of explicit-flag design this codebase already uses for `is_error`/`can_cancel`
rather than inferring either from other state.

### 9.3 History reconstruction: one `Content` per tool-call `Message` vs grouped

`ChatController` already appends one `Message` per `ToolCallEntry` (one per `handleToolCall()` call,
§10 risk #2) — the simplest codec would emit one Gemini `Content` entry per such `Message`,
1:1, requiring zero grouping logic. Rejected in favor of `AnthropicToolCodec`-style grouping (§3.4)
because Google's *own* documented example for parallel function calls
(§3.5) shows both `functionCall` parts inside a **single** `Content` entry's `parts` array, and
both `functionResponse` parts inside a single following `Content` entry — not four separate
`Content` entries. Un-grouped output would still be schema-valid JSON (REQ-NF-003 only requires
that), but would diverge from Gemini's own documented/idiomatic shape for a case (parallel calls)
this SPEC explicitly targets (REQ-F-004), for no implementation-complexity savings once
`AnthropicToolCodec`'s `MessageGroupBuilder` already exists as a template to adapt.

---

## 10. Known Risks

1. **`thoughtSignature` key casing is unverified against a live captured response** (§9.1) — chosen
   by convention-consistency with the rest of this file's already-correct camelCase fields, not by
   directly observing a real Gemini 3.x thinking-model response during this design pass (no live API
   calls are made, per REQ-NF-002, and this codebase has no captured fixture from a real account).
   A fake-fixture test suite cannot catch a wrong key name — it will stay green while the real
   integration silently never populates `thought_signature` from a genuine response. Recommend a
   one-time manual verification against a real Gemini 3.x streaming response before this ships, or
   accepting the risk explicitly as "best available evidence from vendor docs."

2. **Multiple tool calls in one turn leave stray empty placeholder `Message`s** — pre-existing
   `ChatController::handleToolCall()`'s `on_terminal` callback (`chat_controller.cpp:398-401`)
   unconditionally appends a fresh `Assistant`/`Streaming` placeholder `Message` after *every* tool
   result, not only the last one of the turn. For N sequential/parallel tool calls in one turn, N−1
   of these placeholders become permanent, never-transitioned, empty stray messages in
   `conversation.messages()` (though `GoogleToolCodec::encodeHistory()` and
   `AnthropicToolCodec::encodeHistory()` both already skip empty-text/no-tool-calls messages when
   building the wire request, so this does not corrupt what gets *sent* to either provider — only
   what gets *persisted and rendered* in the QML message list). This is inherited framework behavior,
   not introduced by this cycle, and reproducible today via Anthropic parallel `tool_use` blocks —
   but Gemini's atomic multi-`functionCall`-in-one-chunk delivery (REQ-F-004) makes triggering it via
   a single ordinary turn far more likely in practice than Anthropic (which requires the model to
   deliberately choose parallel `tool_use`). Out of REQ-C-002's scope to fix this cycle; flagged for
   a dedicated follow-up rather than silently absorbed into this one's diff.

3. **Backward compatibility of on-disk `GoogleProviderConfig`** — REQ-F-014 requires a missing
   `tool_calling_enabled` key to decode as `false`. Verified the shared parse-block change (§6)
   preserves this via the same `.toBool(false)` pattern Anthropic's REQ-F-010(2) already established
   and already has a passing regression test for
   (`AnthropicToolCallingEnabledDefaultsToFalseWhenKeyMissingFromOnDiskConfig`) — low risk, mirrored
   directly by the new `GoogleToolCallingEnabledDefaultsToFalseWhenKeyMissingFromOnDiskConfig` test
   (§8).

4. **`thought_signature`/`provider_call_id_synthesized` persistence round-trip has no existing
   integration test class to extend** — this codebase's persistence tests exercise
   `ConversationRepositoryWorker` directly (per the `MEMORY.md` "Usage cost tracking cycle" note on
   this exact technique), but none currently reload a conversation mid-tool-call-turn and re-send it
   through a live provider adapter. §8's new unit test covers the `encodeToolCalls()`/
   `decodeToolCalls()` round-trip in isolation, which is necessary but not fully sufficient evidence
   that a reloaded thinking-model conversation continues correctly end-to-end — flagged as residual
   risk rather than claimed as fully covered.

5. **Non-thinking Gemini models must see zero behavior change** — `GoogleToolCodec::decodeRequest()`
   only sets `.thought_signature` when the Part JSON actually contains a `thoughtSignature` key
   (`part.contains(...)` guard, §3.3), so `gemini-2.x`-class non-thinking models produce
   `std::nullopt` exactly as before this cycle. Low residual risk (a straightforward presence check),
   covered explicitly by test #5 in §8 per REQ-F-005's own acceptance criteria.

6. **`kMaxToolCallsPerTurn` accounting under parallel calls** — REQ-C-004 says the existing cap
   "applies unchanged," and it does structurally (`ChatController::handleToolCall()`'s
   `stream.tool_calls_this_turn` counter increments once per `ToolRequestEvent`, provider-agnostically,
   §5) — but Gemini's atomic multi-`functionCall`-per-chunk delivery means a *single* streamed chunk
   can synchronously push several calls toward the same 10-call budget before any of them has
   actually executed, something Anthropic's strictly-sequential `content_block_stop` events cannot do
   in one synchronous burst. This matches the existing per-event accounting exactly (no code
   divergence), but is worth confirming as the intended interpretation given Google's delivery pattern
   makes the edge more reachable in ordinary use than it was for Anthropic.
