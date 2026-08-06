# Anthropic Provider Adapter Design

**Document Version**: 1.0
**Date**: 2026-07-24
**Modules**: `holonight_providers` (new: `AnthropicProvider`), `holonight_config` (new:
`AnthropicProviderConfig`; changed: `ConfigRepository` gains `loadAnthropicConfig()`/
`saveAnthropicConfig()`), `holonight_application` (new: `AnthropicProviderSettingsController`;
changed: `ChatController`, `ChatViewModel`), `qml/workspace/` (new: `AnthropicSettingsPanel.qml`;
changed: `ProvidersPage.qml`, `ProviderListDelegate.qml`), `tests/` (new + extended GTest coverage)
**Traces to**: `docs/sdd/anthropic-provider-adapter/SPEC.md` (41 functional, 6 non-functional, 7
constraint requirements)
**Precedent**: `docs/sdd/openai-provider-adapter/DESIGN.md` (structurally the closest prior design —
shared/probe-provider pattern, three-provider `ChatController` routing question one cycle early,
SSE `StreamContext`/`failStream()` shape — reused directly), `src/providers/src/openai_provider.cpp`
(concrete SSE implementation this design's parser mirrors almost line-for-line)
**Status**: Ready for Stage 3 (Tasks)

---

## Corrections to SPEC.md

**REQ-NF-001's acceptance criterion is wrong about the test target shape and must not be followed
literally.** It reads: *"A test executable `test_anthropic_provider` is added under `tests/`."* This
repo does not build one test executable per feature — `tests/CMakeLists.txt` builds exactly **one**
shared GTest binary, `test_holonight_ai`, via a single `add_executable(test_holonight_ai ...)` whose
`SOURCES` list already contains one `.cpp` file per feature area (`providers/test_ollama_provider.cpp`,
`providers/test_openai_provider.cpp`, `application/test_provider_settings_controller.cpp`, etc.). The
OpenAI cycle's own DESIGN.md (§9) already made and documented this same correction against its own
Spec's identically-worded acceptance criterion — this is an established, repeated pattern in how these
Specs get worded, not a one-off typo.

**What to actually build**: `tests/providers/test_anthropic_provider.cpp` (new) and
`tests/application/test_anthropic_provider_settings_controller.cpp` (new) are added as **source files**
to the existing `add_executable(test_holonight_ai ...)` list in `tests/CMakeLists.txt` (§8 has the
exact diff). No new `add_executable`, no new CTest target, no new `gtest_discover_tests()` call. `task
test` / `ctest -R AnthropicProvider --output-on-failure` continue to work exactly as they do for
`test_ollama_provider.cpp`/`test_openai_provider.cpp` today.

No other correction to SPEC.md was found — the rest of the Spec (request/response shapes, header
names, SSE event types, config field additions) was independently verified against the real Anthropic
Messages API docs (Context7 `/anthropics/anthropic-sdk-python`) during this design and matches.

---

## 0. Ground truth this design was checked against

Six things were verified by reading the real source (not assumed), each shaping a concrete decision
below:

1. **`OpenAIProvider` is the structural template, confirmed field-for-field.** Constructor takes
   `shared_ptr<HttpClient>` + `QString base_url`; `refresh()` has the same two overloads; `setBaseUrl`/
   `baseUrl` mutate/read a plain member, read only at request-construction time; `sendChat()` returns
   `HttpRequestHandlePtr` and takes the same four parameters (`ModelId`, `history`,
   `on_event`, `idle_timeout = 30s` default) as `OllamaProvider::sendChat()`. `AnthropicProvider`
   reuses this exact shape (REQ-F-039) — see §3.1.

2. **`holonight_domain::StreamEvent` is `std::variant<ContentDelta, Completed, Error, Cancelled>`**
   (`src/domain/include/holonight_domain/stream_event.h:29`) — **no `Usage` variant exists**. REQ-F-017
   is therefore unconditionally the no-op branch of its own acceptance criterion ("if no `Usage`
   variant exists, the adapter silently ignores the usage data"). No domain change is made (matches the
   Non-Goals list's "no new `StreamEvent` variants").

3. **`ChatController`'s constructor is already a "add one more provider" precedent, not a new
   pattern.** The OpenAI cycle turned a one-provider constructor into a two-provider one with the exact
   same shape this design needs to extend to three: named `shared_ptr<T>` members, an `if (provider_id
   == "...")` chain in `hasModelsFor()`/`dispatchSendChat()`, no shared base class. §5.1 below resolves
   SPEC.md §F.9's explicitly-deferred "how does a third provider_id get routed" question by extending
   this exact chain, and explicitly confronts the OpenAI DESIGN.md's own "rule of three" flag (§8.1 of
   that document predicted this exact moment).

4. **`QtNetworkHttpClient`'s non-2xx error-body surfacing fix (`OpenAIProvider` DESIGN.md §3.8/§5.5) is
   already generic, not OpenAI-specific.** The fix changed the fallback string from
   `"Ollama returned HTTP status %1"` to `"Request failed with HTTP status %1"` and made both `send()`
   and `sendStreaming()` prefer the raw response body over `errorString()` for every caller, not just
   OpenAI's. **`AnthropicProvider` needs no further `QtNetworkHttpClient` change** — it inherits this
   fix for free, exactly as `OllamaProvider` retroactively did.

5. **`CredentialStore` is already provider-ID-agnostic and needs zero changes.**
   `src/credentials/include/holonight_credentials/credential_store.h`'s own doc comment states
   `providerId` is "a free-form string (`"openai"`, `"anthropic"`, `"google"`, ...)" — Anthropic was
   already anticipated by name in this interface's original design. `store("anthropic", key)` /
   `retrieve("anthropic")` / `remove("anthropic")` work unmodified (REQ-F-037/041 are satisfied by
   doing nothing to this module).

6. **`ProviderListPanel.qml`'s model array already contains `"Anthropic"`**
   (`qml/workspace/ProviderListPanel.qml:11`: `model: ["Ollama", "OpenAI", "Anthropic", "Google"]`).
   Only the *routing* (`ProvidersPage.qml`'s `Loader.sourceComponent` switch and
   `ProviderListDelegate.qml`'s status-dot binding) needs to change — the list itself does not (§7).

---

## 1. Components

| Component | File(s) | Role |
|---|---|---|
| `holonight_providers::AnthropicProvider` | `src/providers/include/holonight_providers/anthropic_provider.h` (new), `src/providers/src/anthropic_provider.cpp` (new) | Concrete adapter mirroring `OpenAIProvider`'s shape: `x-api-key`/`anthropic-version` headers, system-prompt hoisting, `max_tokens`-required request construction, SSE parsing, model discovery + denylist filtering. |
| `holonight_config::AnthropicProviderConfig` | `src/config/include/holonight_config/provider_config.h` (changed — struct added alongside `OllamaProviderConfig`/`OpenAIProviderConfig`) | Non-secret config: `base_url`, `default_model`, `temperature`, `max_output_tokens`. |
| `holonight_config::ConfigRepository` (changed) | `src/config/include/holonight_config/config_repository.h`, `src/config/src/config_repository.cpp` | Gains `loadAnthropicConfig()`/`saveAnthropicConfig()`, same read-merge-write convention already established for Ollama/OpenAI. |
| `holonight_application::ChatController` (changed) | `src/application/include/holonight_application/chat_controller.h`, `.cpp` | Constructor now takes **three** concrete providers; `hasModelsFor()`/`dispatchSendChat()` gain a third `provider_id == "anthropic"` branch (§5.1). |
| `holonight_application::ChatViewModel` (changed) | `src/application/include/holonight_application/chat_view_model.h`, `.cpp` | Owns a third provider instance; `syncAvailableModels()` unions three providers' model lists; gains `anthropicProviderForSettings()` and `syncAvailableAnthropicModels()`. |
| `holonight_application::AnthropicProviderSettingsController` | `src/application/include/holonight_application/anthropic_provider_settings_controller.h` (new), `.cpp` (new) | New `QML_SINGLETON`, structurally parallel to `OpenAIProviderSettingsController`, reusing the same shared `CredentialStore` instance (§5.3). Gains a `maxOutputTokens` property neither sibling controller has. |
| `qml/workspace/AnthropicSettingsPanel.qml` | new | Mirrors `OpenAISettingsPanel.qml`'s structure with a temperature range of 0.0–1.0 and an added "Max output tokens" `SpinBox`; binds to `AnthropicProviderSettingsController`. |
| `qml/workspace/ProvidersPage.qml` (changed) | existing | Loader routes `"Anthropic"` to the new panel instead of `UnsupportedProviderPanel.qml`. |
| `qml/workspace/ProviderListDelegate.qml` (changed) | existing | Status dot now also reads `AnthropicProviderSettingsController.anthropicConnectionStatus` for the Anthropic row (was hardcoded gray, same as OpenAI's row was before its own cycle). |
| `tests/providers/test_anthropic_provider.cpp` | new (source file added to the existing single `test_holonight_ai` binary — see Corrections above and §8) | Request construction (system hoisting, `max_tokens`), SSE parsing/fragmentation, event routing, denylist filtering, cancellation, error mapping. |
| `tests/application/test_chat_controller.cpp` (changed) | existing | Fixture gains an `AnthropicProvider`; new tests cover provider routing for `provider_id == "anthropic"`. |
| `tests/application/test_anthropic_provider_settings_controller.cpp` | new | Mirrors `test_openai_provider_settings_controller.cpp`, plus `maxOutputTokens` round-trip coverage. |
| `tests/config/test_config_repository.cpp` (changed) | existing | New round-trip tests for `AnthropicProviderConfig` (including `max_output_tokens`); a new three-provider read-merge-write test (saving Anthropic doesn't clobber Ollama/OpenAI sections and vice versa). |

### 1.1 Class relationship

```
                              ┌─────────────────────────────────────────────────┐
                              │                  ChatViewModel                    │ (QML_SINGLETON, existing)
                              │  - ollama_provider_: shared_ptr<OllamaProvider>     │
                              │  - openai_provider_: shared_ptr<OpenAIProvider>     │
                              │  - anthropic_provider_: shared_ptr<AnthropicProvider>│ (new)
                              │  - chat_controller_: unique_ptr<ChatController>     │
                              │  + providerForSettings() / openAiProviderForSettings()│
                              │  + anthropicProviderForSettings() const              │ (new)
                              │  + syncAvailableModels(preferred)  — unions all three│
                              └───────────────┬───────────────────────────────────────┘
                                              │ owns
                              ┌───────────────▼───────────────────────────────────────┐
                              │                   ChatController                        │ (changed)
                              │  - ollama_provider_, openai_provider_, anthropic_provider_│
                              │  routes sendChat()/availableModels() checks on           │
                              │  ModelId::provider_id — plain if/else chain, no vtable   │
                              └─────────────────────────────────────────────────────────┘

  ┌────────────────────────────┐ shared_ptr (Save only)  ┌────────────────────────────────┐
  │  ProviderSettingsController  │◄────────────────────────┤     ChatViewModel (Ollama half)   │
  │  (existing, unchanged)       │                          └────────────────────────────────┘
  │  + credentialStore() const   │ (existing accessor, reused by BOTH controllers below)
  └───────────────┬───────────────┘
                  │ engine forces this singleton to exist first
  ┌───────────────▼───────────────────────────┐ shared_ptr (Save only) ┌────────────────────────────────┐
  │      OpenAIProviderSettingsController        │◄───────────────────────┤   ChatViewModel (OpenAI half)     │
  │      (existing, unchanged)                   │                        └────────────────────────────────┘
  └───────────────────────────────────────────────┘

                  │ engine ALSO forces ProviderSettingsController to exist first (independent of the OpenAI
                  │ chain above — does not need OpenAIProviderSettingsController to exist at all, §5.3)
  ┌───────────────▼─────────────────────────────────┐ shared_ptr (Save only) ┌──────────────────────────────────┐
  │      AnthropicProviderSettingsController            │◄───────────────────────┤ ChatViewModel (Anthropic half)     │
  │      (new, structurally parallel)                   │                        └──────────────────────────────────┘
  │  - provider_/probe_provider_: AnthropicProvider ×2                             │
  │  - credential_store_: SAME CredentialStore* as the other two controllers        │
  │  Q_PROPERTY baseUrl, defaultModel, temperature (0.0–1.0), maxOutputTokens (NEW), │
  │             authToken, hasStoredToken, credentialStoreAvailable,                │
  │             credentialOperationInProgress, availableModelNames,                 │
  │             modelRefreshInProgress/Error, testConnectionInProgress/Status/       │
  │             Message, anthropicConnectionStatus, saveNotice                      │
  └──────────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Data Flow

### 2.1 Chat-send path (REQ-F-001–008, F-013–019, F-024, F-025)

1. User selects a model in `WorkspaceWindow.qml`'s combo box →
   `ChatViewModel.selectedModelId = {provider_id: "anthropic", model_name: "claude-3-5-sonnet-20241022"}`
   (unchanged QML — the combo box is already provider-agnostic, exactly as the OpenAI cycle found).
2. `ChatViewModel::send(text)` → `chat_controller_->send(*conversation_, selected_model_id_, text,
   onEventCallback)` (unchanged call site).
3. `ChatController::send()` validates via `hasModelsFor(model.provider_id)`, then `startStream()` calls
   `dispatchSendChat(model, history, on_event, idle_timeout)`:
   ```cpp
   if (model.provider_id == QStringLiteral("openai")) {
     return openai_provider_->sendChat(model, history, on_event, idle_timeout);
   }
   if (model.provider_id == QStringLiteral("anthropic")) {
     return anthropic_provider_->sendChat(model, history, on_event, idle_timeout);
   }
   return ollama_provider_->sendChat(model, history, on_event, idle_timeout);
   ```
4. `AnthropicProvider::sendChat()` partitions `history` into hoisted `system` text (REQ-F-004/005) and
   a `messages` array of User/Assistant entries (REQ-F-003), builds the `/v1/messages` POST body
   (§3.2), issues it via `http_client_->sendStreaming()` with `requestHeaders()` (`x-api-key` +
   `anthropic-version`, §5.5), and returns the `HttpRequestHandlePtr` — identical shape to
   `OpenAIProvider::sendChat()`, so `ChatController::startStream()`'s handle bookkeeping is untouched.
5. Raw bytes arrive via `on_data`; the SSE parser (§4) accumulates them into `StreamContext::buffer`,
   frames complete `data: {...}` blocks on blank lines, parses each as JSON, and routes on `type` (§4.3)
   into the **same** `holonight_domain::StreamEvent` variant every provider uses
   (`ContentDelta`/`Completed`/`Error`/`Cancelled`) via the **same** `on_event` callback signature.
6. From here the flow is unchanged and provider-agnostic — `ChatController::handleStreamEvent()`,
   `ChatViewModel::onStreamEvent()`, `MessageListModel`, persistence, and `canSend`/`isStreaming`
   recomputation touch only `holonight_domain::StreamEvent`, never a provider type. **None of this code
   needs to change.**
7. Stop button → `ChatViewModel::stop()` → `ChatController::stop(conversation_id)` → looks up
   `in_flight_[key].handle` (opaque, already provider-agnostic) → `handle->cancel()` (REQ-F-024). No
   routing needed here — the handle was already resolved to the right provider at step 4.

### 2.2 Model-discovery / settings path (REQ-F-009–012, REQ-F-032) — shared/probe flow

Mirrors `OpenAIProviderSettingsController`'s flow (`openai-provider-adapter/DESIGN.md` §2.2) exactly,
on a third, independent pair of provider instances:

1. `AnthropicProviderSettingsController::create()` builds a **probe** `AnthropicProvider` over its own
   fresh `QtNetworkHttpClient`, entirely separate from `chatViewModel->anthropicProviderForSettings()`
   (the shared instance `ChatController` uses for real chat traffic).
2. `load()` (called once at construction) reads `AnthropicProviderConfig` via
   `config_repository_.loadAnthropicConfig()`, seeds `base_url_`/`default_model_`/`temperature_`/
   `max_output_tokens_`, points `probe_provider_->setBaseUrl(...)` at the loaded URL, then calls
   `credential_store_->retrieve("anthropic")` — asynchronous, so the **initial** model fetch is
   deferred to `onTokenRetrieved()` rather than fired here (§5.8 — the same async-startup race the
   OpenAI cycle already solved applies unchanged).
3. `refreshModels()`/`testConnection()` point `probe_provider_->setBaseUrl(...)` and
   `probe_provider_->setAuthKey(...)` at the **in-panel** (possibly unsaved) values and call
   `probe_provider_->refresh(on_success, on_error)`, which issues `GET /v1/models` with `x-api-key` +
   `anthropic-version` headers (REQ-F-009) and, on success, replaces `probe_provider_`'s cached,
   denylist-filtered model list (§6).
4. Editing the panel — including repeated Refresh/Test Connection clicks — **never** touches `provider_`
   (the shared instance), so live chat traffic is unaffected until Save (same rationale as
   `provider-settings-ui/DESIGN.md` §5.3, restated for a third provider).
5. `save()` (REQ-F-027): validates `temperature` ∈ [0.0, 1.0] (REQ-F-006 — note this range differs from
   OpenAI's [0.0, 2.0]), calls `config_repository_.saveAnthropicConfig(config)`, applies the new
   `base_url`/`temperature`/`max_output_tokens` to `provider_` (the shared instance), and calls
   `provider_->refresh([this, preferred] { model_sync_callback_(preferred); })` —
   `model_sync_callback_` is `chatViewModel->syncAvailableAnthropicModels(preferred)` in production,
   which marks `anthropic_models_loaded_ = true` and re-unions all three providers' lists.
6. Token save/remove: identical shape to Ollama/OpenAI's `save()`/`onTokenStored()`/`onTokenRemoved()`,
   using `credential_store_->store("anthropic", token)` / `remove("anthropic")` (REQ-F-028/030/037) —
   the only difference from the other two controllers is the provider-ID string literal and that the
   provider-side setter is named `setAuthKey()`, not `setAuthToken()` (§5.5/§5.9).

### 2.3 Config load fallback table (REQ-F-034)

Identical structure to `loadOpenAiConfig()`'s existing table, keyed at `"providers"."anthropic"`:

| Condition | Result |
|---|---|
| File missing / empty / malformed JSON / not an object | `AnthropicProviderConfig{}` (defaults: `base_url="https://api.anthropic.com"`, `default_model=""`, `temperature=1.0`, `max_output_tokens=1024`) |
| `"providers"` missing or `"providers"."anthropic"` missing/not-object | `AnthropicProviderConfig{}` |
| Present, one or more fields missing/wrong-typed | Present fields load; missing/wrong-typed fields individually fall back to that field's default (`QJsonValue::toString(default)`/`toDouble(default)`/`toInt(default)`) |

### 2.4 Persisted JSON shape (REQ-F-036)

```jsonc
{
  "providers": {
    "ollama": { "base_url": "...", "default_model": "...", "context_window": 4096, "temperature": 0.7 },
    "openai": { "base_url": "...", "default_model": "...", "temperature": 1.0 },
    "anthropic": {
      "base_url": "https://api.anthropic.com",
      "default_model": "claude-3-5-sonnet-20241022",
      "temperature": 1.0,
      "max_output_tokens": 2048
    }
  }
}
```

`saveAnthropicConfig()` reads the whole `root` via `readRootObject()`, mutates only
`root["providers"]["anthropic"]`, and writes back via `writeRootObject()` — the `"ollama"` and
`"openai"` keys are read, kept, and rewritten byte-identical (already-fixed read-merge-write behavior
from the OpenAI cycle; no further change to that mechanism is needed, only a third caller of it).

---

## 3. Interfaces / APIs

### 3.1 `src/providers/include/holonight_providers/anthropic_provider.h` (new)

```cpp
#pragma once

#include "holonight_providers/http_client.h"

#include <QHash>
#include <QString>

#include <chrono>
#include <functional>
#include <holonight_domain/holonight_domain.h>
#include <memory>
#include <vector>

namespace holonight_providers {

// Adapter for Anthropic's Messages API (POST /v1/messages). Deliberately a plain concrete class,
// structurally parallel to OllamaProvider/OpenAIProvider but sharing no base class with either
// (REQ-F-039 — no virtual base, no dynamic-dispatch interface for this cycle; see DESIGN.md §5.1 for
// why this holds even now that a third such provider exists).
class AnthropicProvider {
 public:
  // Model discovery is explicit: callers invoke refresh() once ready to receive the async result.
  explicit AnthropicProvider(std::shared_ptr<HttpClient> http_client,
                             QString base_url = QStringLiteral("https://api.anthropic.com"));

  [[nodiscard]] const std::vector<holonight_domain::ModelId>& availableModels() const;

  // Re-fetches GET /v1/models (denylist-filtered, REQ-F-010/011) and replaces the cached list.
  void refresh(const std::function<void()>& on_complete = {});
  void refresh(const std::function<void()>& on_success, const std::function<void(const QString&)>& on_error);

  // Reconfigures the base URL used by the NEXT refresh()/sendChat() call (REQ-F-001).
  void setBaseUrl(QString base_url);
  [[nodiscard]] const QString& baseUrl() const;

  // x-api-key support (REQ-F-001/038). Named setAuthKey(), not setAuthToken() — Anthropic's scheme is
  // a raw API key header, not a Bearer token (REQ-F-039's method-surface list names this explicitly).
  // Empty (default) ⇒ no x-api-key header is sent; anthropic-version is still sent regardless
  // (REQ-F-002). Never logged, never exposed via a getter — write-only by design (REQ-NF-006).
  void setAuthKey(QString auth_key);

  // Applied to every subsequent sendChat()'s "temperature" field. Range 0.0–1.0 (REQ-F-006) — NOT
  // validated here (the provider trusts its caller; range enforcement is the settings controller's
  // job, REQ-C-003 forbids provider-level model-specific validation).
  void setTemperature(double temperature);

  // Applied to every subsequent sendChat()'s required "max_tokens" field (REQ-F-007). Default 1024
  // matches holonight_config::AnthropicProviderConfig{}'s own default.
  void setMaxOutputTokens(int max_output_tokens);

  // Sends `history` to /v1/messages and streams the response as StreamEvents (SSE framing, §4).
  // System-role messages in `history` are hoisted to the top-level "system" field, not sent inline
  // (REQ-F-004/005) — the only behavioral divergence from Ollama/OpenAI's sendChat() at the call-site
  // level; the signature itself is identical.
  HttpRequestHandlePtr sendChat(const holonight_domain::ModelId& model,
                                const std::vector<holonight_domain::Message>& history,
                                const std::function<void(const holonight_domain::StreamEvent&)>& on_event,
                                std::chrono::milliseconds idle_timeout = std::chrono::seconds{30});

 private:
  void fetchModelList(const std::function<void()>& on_success, const std::function<void(const QString&)>& on_error);

  // Always includes anthropic-version (REQ-F-002, every request including model-discovery GETs);
  // includes x-api-key only when auth_key_ is non-empty (REQ-F-038). Two headers instead of the
  // single conditional Authorization header Ollama/OpenAI's authHeaders() returns — hence the
  // renamed method (requestHeaders(), not authHeaders()): the anthropic-version entry is present
  // unconditionally, so "auth" is no longer an accurate name for what this returns.
  [[nodiscard]] QHash<QString, QString> requestHeaders() const;

  std::shared_ptr<HttpClient> http_client_;
  QString base_url_;
  std::vector<holonight_domain::ModelId> available_models_;
  QString auth_key_;
  double temperature_ = 1.0;
  int max_output_tokens_ = 1024;
};

}  // namespace holonight_providers
```

### 3.2 Request body construction (REQ-F-001–008)

Full example request body, given a conversation with two System messages, a User message, and an
Assistant reply, then a final User message (`temperature = 0.8`, `max_output_tokens = 2048`, model
`claude-3-5-sonnet-20241022`):

```json
{
  "model": "claude-3-5-sonnet-20241022",
  "system": "You are a terse, technical assistant.\n\nAlways answer in one paragraph.",
  "messages": [
    { "role": "user", "content": "What is a Wayland layer-shell surface?" },
    { "role": "assistant", "content": "A layer-shell surface is a compositor-managed overlay..." },
    { "role": "user", "content": "Does holonight-chat use one?" }
  ],
  "max_tokens": 2048,
  "temperature": 0.8,
  "stream": true
}
```

If there are zero System messages, the `"system"` key is omitted entirely (REQ-F-005) — not sent as
`"system": ""`.

```cpp
QStringList systemParts;
QJsonArray messages;
for (const Message& message : history) {
  if (message.role() == MessageRole::System) {
    if (!message.text().isEmpty()) {
      systemParts << message.text();
    }
    continue;  // REQ-F-003: System messages never appear in the `messages` array.
  }
  QJsonObject entry;
  entry[QStringLiteral("role")] = roleToAnthropicString(message.role());  // "user" | "assistant"
  entry[QStringLiteral("content")] = message.text();
  messages.append(entry);
}

QJsonObject body;
body[QStringLiteral("model")] = model.model_name;
body[QStringLiteral("messages")] = messages;
body[QStringLiteral("max_tokens")] = max_output_tokens_;  // REQ-F-007: always present, unlike OpenAI
body[QStringLiteral("temperature")] = temperature_;       // REQ-F-006
body[QStringLiteral("stream")] = true;
if (!systemParts.isEmpty()) {
  body[QStringLiteral("system")] = systemParts.join(QStringLiteral("\n\n"));  // REQ-F-004
}
// Deliberately absent: tools, tool_choice, thinking, budget_tokens, top_k, top_p, metadata,
// stop_sequences, betas (REQ-C-001/C-004).

const HttpRequest request{.method = HttpMethod::Post,
                          .url = base_url_ + QStringLiteral("/v1/messages"),
                          .body = QJsonDocument(body).toJson(QJsonDocument::Compact),
                          .content_type = QStringLiteral("application/json"),
                          .headers = requestHeaders()};
```

`requestHeaders()`:

```cpp
QHash<QString, QString> AnthropicProvider::requestHeaders() const {
  QHash<QString, QString> headers{{QStringLiteral("anthropic-version"), QStringLiteral("2023-06-01")}};
  if (!auth_key_.isEmpty()) {
    headers.insert(QStringLiteral("x-api-key"), auth_key_);  // REQ-F-001/038
  }
  return headers;
}
```

Example streaming response sequence for the request above (matches Anthropic's documented SSE shape,
verified against the current Messages API streaming format):

```
event: message_start
data: {"type":"message_start","message":{"id":"msg_01Abc","type":"message","role":"assistant","model":"claude-3-5-sonnet-20241022","content":[],"stop_reason":null,"usage":{"input_tokens":48,"output_tokens":1}}}

event: content_block_start
data: {"type":"content_block_start","index":0,"content_block":{"type":"text","text":""}}

event: ping
data: {"type":"ping"}

event: content_block_delta
data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"Yes"}}

event: content_block_delta
data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":", the chat process owns"}}

event: content_block_delta
data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":" a wlr-layer-shell surface."}}

event: content_block_stop
data: {"type":"content_block_stop","index":0}

event: message_delta
data: {"type":"message_delta","delta":{"stop_reason":"end_turn","stop_sequence":null},"usage":{"output_tokens":24}}

event: message_stop
data: {"type":"message_stop"}
```

`AnthropicProvider` produces exactly three `ContentDelta` events ("Yes", ", the chat process owns", "
a wlr-layer-shell surface."), then one `Completed` event on `message_stop`. `message_start`,
`content_block_start`/`content_block_stop`, `ping`, and `message_delta` are all recognized-but-no-op
or silently-ignored per §4.3.

Error response example (401, non-streaming body — arrives via the same recovery path as §4.6):

```json
{ "type": "error", "error": { "type": "authentication_error", "message": "Invalid x-api-key" } }
```

### 3.3 `src/config/include/holonight_config/provider_config.h` (changed — struct added)

```cpp
// Non-secret Anthropic configuration persisted to config.json (REQ-F-033/034). No auth key field —
// Secret Service's exclusive responsibility (REQ-F-035/REQ-NF-006). max_output_tokens is required
// (REQ-F-007) — unlike OpenAIProviderConfig, which has no such field (REQ-C-008 there forbids one).
struct AnthropicProviderConfig {
  QString base_url = QStringLiteral("https://api.anthropic.com");
  QString default_model;  // empty ⇒ "no default model saved"
  double temperature = 1.0;
  int max_output_tokens = 1024;

  friend bool operator==(const AnthropicProviderConfig&, const AnthropicProviderConfig&) = default;
};
```

### 3.4 `src/config/include/holonight_config/config_repository.h` / `.cpp` (changed)

```cpp
class ConfigRepository {
 public:
  // ... existing Ollama/OpenAI methods unchanged ...

  // REQ-F-034: never throws, same fallback semantics as loadOllamaConfig()/loadOpenAiConfig().
  [[nodiscard]] AnthropicProviderConfig loadAnthropicConfig() const;

  // REQ-F-034: same std::expected<void, QString> convention as the other two save methods.
  [[nodiscard]] std::expected<void, QString> saveAnthropicConfig(const AnthropicProviderConfig& config) const;

  // readRootObject()/writeRootObject() are reused unchanged — no new private helpers needed.
};
```

`config_repository.cpp` gains two new `constexpr auto` key literals (`kAnthropicKey = "anthropic"`,
`kMaxOutputTokensKey = "max_output_tokens"`) alongside the existing ones, and two new methods that
follow `loadOpenAiConfig()`/`saveOpenAiConfig()` byte-for-byte, substituting `AnthropicProviderConfig`
and adding one extra field read/write:

```cpp
AnthropicProviderConfig ConfigRepository::loadAnthropicConfig() const {
  AnthropicProviderConfig defaults{};
  const QJsonValue providersValue = readRootObject().value(QLatin1String(kProvidersKey));
  if (!providersValue.isObject()) return defaults;
  const QJsonValue anthropicValue = providersValue.toObject().value(QLatin1String(kAnthropicKey));
  if (!anthropicValue.isObject()) return defaults;

  const QJsonObject anthropic = anthropicValue.toObject();
  AnthropicProviderConfig config;
  config.base_url = anthropic.value(QLatin1String(kBaseUrlKey)).toString(defaults.base_url);
  config.default_model = anthropic.value(QLatin1String(kDefaultModelKey)).toString(defaults.default_model);
  config.temperature = anthropic.value(QLatin1String(kTemperatureKey)).toDouble(defaults.temperature);
  config.max_output_tokens = anthropic.value(QLatin1String(kMaxOutputTokensKey)).toInt(defaults.max_output_tokens);
  return config;
}

std::expected<void, QString> ConfigRepository::saveAnthropicConfig(const AnthropicProviderConfig& config) const {
  QJsonObject root = readRootObject();
  QJsonObject providers = root.value(QLatin1String(kProvidersKey)).toObject();

  QJsonObject anthropic;
  anthropic[QLatin1String(kBaseUrlKey)] = config.base_url;
  anthropic[QLatin1String(kDefaultModelKey)] = config.default_model;
  anthropic[QLatin1String(kTemperatureKey)] = config.temperature;
  anthropic[QLatin1String(kMaxOutputTokensKey)] = config.max_output_tokens;

  providers[QLatin1String(kAnthropicKey)] = anthropic;
  root[QLatin1String(kProvidersKey)] = providers;
  return writeRootObject(root);
}
```

### 3.5 `src/application/include/holonight_application/chat_controller.h` / `.cpp` (changed)

```diff
 class ChatController {
  public:
-  explicit ChatController(std::shared_ptr<holonight_providers::OllamaProvider> ollama_provider,
-                          std::shared_ptr<holonight_providers::OpenAIProvider> openai_provider);
+  // Takes all three concrete providers explicitly — no virtual base, no registry (§5.1). Breaking
+  // change: every existing call site (ChatViewModel's constructor, test fixtures) must add the third
+  // argument in the same change; there is no sensible default for a shared_ptr<AnthropicProvider>.
+  explicit ChatController(std::shared_ptr<holonight_providers::OllamaProvider> ollama_provider,
+                          std::shared_ptr<holonight_providers::OpenAIProvider> openai_provider,
+                          std::shared_ptr<holonight_providers::AnthropicProvider> anthropic_provider);

   // send()/regenerate()/stop()/isStreaming() — signatures unchanged.

  private:
   [[nodiscard]] bool hasModelsFor(const QString& provider_id) const;
   holonight_providers::HttpRequestHandlePtr dispatchSendChat(...);  // signature unchanged

   std::shared_ptr<holonight_providers::OllamaProvider> ollama_provider_;
   std::shared_ptr<holonight_providers::OpenAIProvider> openai_provider_;
+  std::shared_ptr<holonight_providers::AnthropicProvider> anthropic_provider_;
   QHash<QString, InFlightStream> in_flight_;
 };
```

```cpp
bool ChatController::hasModelsFor(const QString& provider_id) const {
  if (provider_id == QStringLiteral("openai")) return !openai_provider_->availableModels().empty();
  if (provider_id == QStringLiteral("anthropic")) return !anthropic_provider_->availableModels().empty();
  return !ollama_provider_->availableModels().empty();
}

HttpRequestHandlePtr ChatController::dispatchSendChat(const ModelId& model, const std::vector<Message>& history,
                                                      const std::function<void(const StreamEvent&)>& on_event) {
  if (model.provider_id == QStringLiteral("openai")) {
    return openai_provider_->sendChat(model, history, on_event);
  }
  if (model.provider_id == QStringLiteral("anthropic")) {
    return anthropic_provider_->sendChat(model, history, on_event);
  }
  return ollama_provider_->sendChat(model, history, on_event);
}
```

### 3.6 `src/application/include/holonight_application/chat_view_model.h` / `.cpp` (changed)

```diff
   explicit ChatViewModel(std::shared_ptr<holonight_providers::OllamaProvider> ollama_provider,
                          std::shared_ptr<holonight_providers::OpenAIProvider> openai_provider,
+                         std::shared_ptr<holonight_providers::AnthropicProvider> anthropic_provider,
                          std::unique_ptr<holonight_persistence::ConversationRepository> repository,
                          QString initial_default_model_id = {}, QObject* parent = nullptr);

   [[nodiscard]] std::shared_ptr<holonight_providers::OpenAIProvider> openAiProviderForSettings() const;
+  [[nodiscard]] std::shared_ptr<holonight_providers::AnthropicProvider> anthropicProviderForSettings() const;

   // syncAvailableModels() now unions THREE providers' availableModels() lists, not two.
   void syncAvailableModels(const std::optional<holonight_domain::ModelId>& preferredModelId = std::nullopt);
   void syncAvailableOpenAiModels(const std::optional<holonight_domain::ModelId>& preferredModelId = std::nullopt);
+  // Mirrors syncAvailableOpenAiModels(): marks anthropic_models_loaded_ before re-unioning.
+  void syncAvailableAnthropicModels(const std::optional<holonight_domain::ModelId>& preferredModelId = std::nullopt);

  private:
   std::shared_ptr<holonight_providers::OllamaProvider> ollama_provider_;
   std::shared_ptr<holonight_providers::OpenAIProvider> openai_provider_;
+  std::shared_ptr<holonight_providers::AnthropicProvider> anthropic_provider_;
   ...
   bool ollama_models_loaded_ = false;
   bool openai_models_loaded_ = false;
+  bool anthropic_models_loaded_ = false;
```

`syncAvailableModels()`'s body change is purely additive — one more `models.insert(models.end(),
anthropicModels.begin(), anthropicModels.end())` and one more `else if (selected_model_id_.provider_id
== "anthropic") selectedProviderReported = anthropic_models_loaded_;` branch in the existing
stale-selection check. `create()`'s factory gains a third `QtNetworkHttpClient` + `AnthropicProvider`,
seeded from `configRepository.loadAnthropicConfig()` (`setTemperature`, `setMaxOutputTokens`),
constructed **before** `ollama_provider_->refresh()` is called — model discovery for Anthropic is
**not** started here either (same reasoning as OpenAI's own comment in this file: it would race ahead
of the async credential retrieval and always 401 once, §5.8).

`ChatViewModel`'s own model-discovery-at-startup behavior for Anthropic is therefore identical to
OpenAI's: **no `anthropic_provider_->refresh()` call in `ChatViewModel`'s constructor** —
`AnthropicProviderSettingsController::onTokenRetrieved()` is what fires the shared provider's first
`refresh()`, exactly mirroring `OpenAIProviderSettingsController::onTokenRetrieved()` (§2.2 step 2,
§5.8).

### 3.7 `src/application/include/holonight_application/anthropic_provider_settings_controller.h` (new)

Structurally identical to `OpenAIProviderSettingsController` (`openai_provider_settings_controller.h`)
with these deltas:

```diff
 class AnthropicProviderSettingsController : public QObject {
   Q_OBJECT
   QML_ELEMENT
   QML_SINGLETON

   Q_PROPERTY(QString baseUrl READ baseUrl WRITE setBaseUrl NOTIFY baseUrlChanged)
   Q_PROPERTY(QString defaultModel READ defaultModel WRITE setDefaultModel NOTIFY defaultModelChanged)
   Q_PROPERTY(double temperature READ temperature WRITE setTemperature NOTIFY temperatureChanged)
+  Q_PROPERTY(int maxOutputTokens READ maxOutputTokens WRITE setMaxOutputTokens NOTIFY maxOutputTokensChanged)
   Q_PROPERTY(QString authToken READ authToken WRITE setAuthToken NOTIFY authTokenChanged)
   Q_PROPERTY(bool hasStoredToken READ hasStoredToken NOTIFY hasStoredTokenChanged)
   Q_PROPERTY(bool credentialStoreAvailable READ credentialStoreAvailable NOTIFY credentialStoreAvailableChanged)
   Q_PROPERTY(bool credentialOperationInProgress READ credentialOperationInProgress NOTIFY credentialOperationInProgressChanged)
   Q_PROPERTY(QStringList availableModelNames READ availableModelNames NOTIFY availableModelNamesChanged)
   Q_PROPERTY(bool modelRefreshInProgress READ modelRefreshInProgress NOTIFY modelRefreshInProgressChanged)
   Q_PROPERTY(QString modelRefreshError READ modelRefreshError NOTIFY modelRefreshErrorChanged)
   Q_PROPERTY(bool testConnectionInProgress READ testConnectionInProgress NOTIFY testConnectionInProgressChanged)
   Q_PROPERTY(QString testConnectionStatus READ testConnectionStatus NOTIFY testConnectionStatusChanged)
   Q_PROPERTY(QString testConnectionMessage READ testConnectionMessage NOTIFY testConnectionMessageChanged)
-  Q_PROPERTY(QString openAiConnectionStatus READ openAiConnectionStatus NOTIFY openAiConnectionStatusChanged)
+  Q_PROPERTY(QString anthropicConnectionStatus READ anthropicConnectionStatus NOTIFY anthropicConnectionStatusChanged)
   Q_PROPERTY(QString saveNotice READ saveNotice NOTIFY saveNoticeChanged)

  public:
   using ModelSyncCallback = std::function<void(const std::optional<holonight_domain::ModelId>&)>;  // reused as-is

   static AnthropicProviderSettingsController* create(QQmlEngine* qml_engine, QJSEngine* js_engine);

   explicit AnthropicProviderSettingsController(std::shared_ptr<holonight_providers::AnthropicProvider> shared_provider,
                                                std::shared_ptr<holonight_providers::AnthropicProvider> probe_provider,
                                                holonight_credentials::CredentialStore* credential_store,
                                                holonight_config::ConfigRepository config_repository,
                                                ModelSyncCallback model_sync_callback, QObject* parent = nullptr);

+  [[nodiscard]] int maxOutputTokens() const;
+  void setMaxOutputTokens(int value);
   // ... every other accessor/Q_INVOKABLE identical in name and shape to OpenAIProviderSettingsController's
   // (load/refreshModels/testConnection/save/cancel/resetToDefaults) ...

 Q_SIGNALS:
+  void maxOutputTokensChanged();
   // ... rest identical ...

  private:
   // ... setAnthropicConnectionStatus(QString) replaces setOpenAiConnectionStatus(QString) ...
   std::shared_ptr<holonight_providers::AnthropicProvider> provider_;        // shared — Save only
   std::shared_ptr<holonight_providers::AnthropicProvider> probe_provider_;  // private — Refresh/Test only
   holonight_config::AnthropicProviderConfig last_saved_config_;
   QString base_url_;
   QString default_model_;
   double temperature_ = 1.0;
+  int max_output_tokens_ = 1024;
   QString auth_token_;  // in-panel-only; maps to provider_->setAuthKey(), NOT setAuthToken() (§5.9)
   ...
 };
```

The `authToken` Q_PROPERTY name is deliberately kept identical to the Ollama/OpenAI controllers' (not
renamed to `apiKey`) even though the underlying `AnthropicProvider` method is `setAuthKey()` — see
§5.9 for why.

`AnthropicProviderSettingsController::save()`'s validation differs from OpenAI's in exactly one
constant: `if (temperature_ < 0.0 || temperature_ > 1.0)` (REQ-F-006's range), not OpenAI's `> 2.0`.
`max_output_tokens_` is written into `AnthropicProviderConfig::max_output_tokens` with no client-side
range validation beyond "is a positive int" (REQ-C-003 forbids model-specific range enforcement).

`create()`:

```cpp
AnthropicProviderSettingsController* AnthropicProviderSettingsController::create(QQmlEngine* qml_engine,
                                                                                 QJSEngine* js_engine) {
  Q_UNUSED(js_engine)
  auto* chatViewModel = qml_engine->singletonInstance<ChatViewModel*>("HolonightChat", "ChatViewModel");
  Q_ASSERT(chatViewModel != nullptr);

  // Forces ProviderSettingsController to exist first, exactly as OpenAIProviderSettingsController::
  // create() does — reuses its CredentialStore instance directly. Does NOT need
  // OpenAIProviderSettingsController to exist at all (§5.3): both settings controllers independently
  // chain off ProviderSettingsController, they don't chain off each other.
  auto* ollamaSettings =
      qml_engine->singletonInstance<ProviderSettingsController*>("HolonightChat", "ProviderSettingsController");
  Q_ASSERT(ollamaSettings != nullptr);

  auto probeHttpClient = std::make_shared<holonight_providers::QtNetworkHttpClient>();
  auto probeProvider = std::make_shared<holonight_providers::AnthropicProvider>(std::move(probeHttpClient));

  holonight_config::ConfigRepository configRepository(holonight_config::resolveConfigFilePath());

  return new AnthropicProviderSettingsController(
      chatViewModel->anthropicProviderForSettings(), std::move(probeProvider),
      ollamaSettings->credentialStore(),  // reused, not owned by this controller
      std::move(configRepository),
      [chatViewModel](const std::optional<holonight_domain::ModelId>& preferred) {
        chatViewModel->syncAvailableAnthropicModels(preferred);
      });
}
```

### 3.8 `holonight_credentials::CredentialStore` — no changes

Confirmed in §0 item 5: the interface is already provider-ID-agnostic. `AnthropicProviderSettingsController`
calls `credential_store_->store(QStringLiteral("anthropic"), token)` /
`retrieve(QStringLiteral("anthropic"))` / `remove(QStringLiteral("anthropic"))` directly, identically
to how the other two controllers use their own provider-ID string literal.

---

## 4. SSE Parser Design (REQ-F-013–019)

`AnthropicProvider::sendChat()` uses the same `StreamContext` + `failStream()` shape as
`OpenAIProvider::sendChat()` (`src/providers/src/openai_provider.cpp:26-44`), unchanged:

```cpp
struct StreamContext {
  QByteArray buffer;
  HttpRequestHandlePtr handle;
  bool terminal = false;
  bool completed = false;
};
```

### 4.1 Framing — identical blank-line SSE framing to OpenAI's parser

Anthropic's Messages API streams classic SSE, same wire shape as OpenAI's Responses API (`event:
<name>\ndata: <json>\n\n`, blank line terminates each block). `AnthropicProvider` reuses
`OpenAIProvider`'s exact framing algorithm (`findSseBlockDelimiter()`/block accumulation loop in
`onData`, `src/providers/src/openai_provider.cpp:179-189, 290-305`) verbatim — copied into
`anthropic_provider.cpp`'s own anonymous namespace, not extracted into a shared header. This repeats
the exact reasoning `openai_provider.cpp` already used for `roleToOpenAiString()`
(`openai_provider.h`'s own comment: "a 6-line switch statement duplicated once is simpler ... than
extracting a cross-provider helper for three string literals"): the framing loop is ~15 lines, has
zero Anthropic/OpenAI-specific content, and a third near-identical copy is the "rule of three" trigger
for actually extracting a shared `sse_framing.h` helper — flagged explicitly as a candidate follow-up
in §9, not done in this cycle (YAGNI: no third near-identical copy existed to justify it until now, and
extracting it is a pure refactor with no REQ attached, better done as its own reviewable change).

```cpp
auto onData = [context, on_event](const QByteArray& chunk) {
  if (context->terminal) return;
  context->buffer += chunk;
  auto [blockEnd, delimiterSize] = findSseBlockDelimiter(context->buffer);
  while (blockEnd != -1) {
    const QByteArray block = context->buffer.left(blockEnd);
    context->buffer.remove(0, blockEnd + delimiterSize);
    processSseBlock(block, context, on_event);
    if (context->terminal) return;
    std::tie(blockEnd, delimiterSize) = findSseBlockDelimiter(context->buffer);
  }
};
```

Handles both `\n\n` and `\r\n\r\n` block terminators (same as OpenAI's parser); tolerates fragmented
chunk boundaries anywhere, including mid-JSON-object and mid-field-name (REQ-F-013's acceptance
criterion), for the same byte-level-framing-not-decoded-text reasoning already established (OpenAI
DESIGN.md §4.1).

### 4.2 `processSseBlock()` / `extractSseDataPayload()` — extracting the payload

Reused verbatim from `openai_provider.cpp` (`extractSseDataPayload()`,
`src/providers/src/openai_provider.cpp:76-97`): joins one or more `data:` lines within a block with
`\n`, tolerates CRLF, tolerates the one-optional-space-after-colon SSE rule, ignores `event:`/`id:`/
comment lines (the JSON payload's own `"type"` field is authoritative, matching REQ-F-014). A block
whose payload fails to parse as JSON calls `failStream()` rather than silently skipping it — same
"malformed payload in a supposedly-complete block indicates something is genuinely wrong" reasoning as
OpenAI's parser (OpenAI DESIGN.md §4.4), extended unchanged to Anthropic.

### 4.3 `routeSseEvent()` — REQ-F-014–019, REQ-C-001

```cpp
void routeSseEvent(const QJsonObject& object, const std::shared_ptr<StreamContext>& context,
                   const std::function<void(const StreamEvent&)>& on_event) {
  const QString type = object.value(QStringLiteral("type")).toString();

  if (type == QStringLiteral("content_block_delta")) {                                    // REQ-F-015
    const QJsonObject delta = object.value(QStringLiteral("delta")).toObject();
    if (delta.value(QStringLiteral("type")).toString() == QStringLiteral("text_delta")) {
      on_event(StreamEvent{ContentDelta{delta.value(QStringLiteral("text")).toString()}});
    }
    // Any other delta.type (e.g. input_json_delta for tool_use) is a deliberate no-op — REQ-C-001.
    return;
  }

  if (type == QStringLiteral("message_stop")) {                                            // REQ-F-016
    context->terminal = true;
    context->completed = true;
    on_event(StreamEvent{holonight_domain::Completed{}});
    return;
  }

  if (type == QStringLiteral("message_delta")) {                                           // REQ-F-017
    // Usage tracking is an unconditional no-op: StreamEvent has no Usage variant (§0 item 2). Recognized
    // explicitly (not falling into the default-ignore path) so REQ-F-014's "type-based routing"
    // acceptance criterion has an observable handler to verify, and so a future cycle adding usage
    // tracking has an obvious insertion point. delta.stop_reason is likewise not surfaced.
    return;
  }

  if (type == QStringLiteral("message_start")) {
    // No domain-layer equivalent to "stream started" exists or is requested this cycle. Recognized
    // explicitly for the same reason as message_delta above.
    return;
  }

  if (type == QStringLiteral("error")) {                                                   // REQ-F-018
    failStream(context, on_event, extractAnthropicErrorMessage(object));
    return;
  }

  // Everything else — ping, content_block_start, content_block_stop, and any future event type — is
  // a deliberate no-op (REQ-F-019). No default branch needed; falling off the end of this function
  // *is* the no-op, matching OpenAIProvider's identical closing comment.
}

QString extractAnthropicErrorMessage(const QJsonObject& object) {
  const QString message =
      object.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
  return message.isEmpty() ? QStringLiteral("Anthropic returned an error") : message;
}
```

### 4.4 `on_finished` — REQ-F-018's "stream ends without completion"

Identical shape to `OpenAIProvider`'s `onFinished` (`openai_provider.cpp:307-319`): processes any
trailing unterminated block, then calls `failStream(context, on_event, "Stream ended without
completion")` if `context->completed` is still `false`.

### 4.5 `on_error` — recovering the JSON error body from the buffer (REQ-F-018/020/021/022/023)

Identical shape to `OpenAIProvider`'s `onError` (`openai_provider.cpp:321-326`), reusing the
byte-array-based sibling of §4.3's helper:

```cpp
QString extractAnthropicErrorMessage(const QByteArray& raw_body, const QString& fallback) {
  QJsonParseError parseError{};
  const QJsonDocument doc = QJsonDocument::fromJson(raw_body, &parseError);
  if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
    const QString message =
        doc.object().value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
    if (!message.isEmpty()) return message;
  }
  return fallback;
}

auto onError = [context, on_event](const QString& transportMessage) {
  if (context->terminal) return;
  failStream(context, on_event, extractAnthropicErrorMessage(context->buffer, transportMessage));
};
```

A genuine transport failure (DNS failure, connection refused, idle timeout — REQ-F-023) leaves
`context->buffer` empty or non-JSON, so `transportMessage` (already fixed to carry real body text by
`QtNetworkHttpClient`'s generic error-body fix, §0 item 4) is used verbatim. `fetchModelList()`'s
`on_error` for `GET /v1/models` reuses the same overload, wrapping the raw error string as
`error.toUtf8()`, exactly as `OpenAIProvider::fetchModelList()` does.

### 4.6 Same content-block-index simplification as OpenAI, made explicit

`content_block_delta`'s `index` field (which content block a delta belongs to) is read from the JSON
but never inspected — every `text_delta` across every index is forwarded as a `ContentDelta`,
concatenated in arrival order by `ChatController::handleStreamEvent()`'s existing
`accumulated_text += value.text` logic. This is correct **for this cycle's scope** because REQ-C-001
(no tool calling) and REQ-C-002 (text-only) together guarantee a real Anthropic response for a
text-only, tool-free request contains exactly one content block (index 0) of type `"text"` — a second
block would only appear if tool use or extended thinking were enabled, neither of which this adapter
requests. Documented here rather than silently relied upon, since it is the one place a future
tool-calling cycle would need to revisit this parser (§9).

---

## 5. Key Decisions With Rationale

### 5.1 `ChatController` grows a third named provider and a third `if` branch — the "rule of three" is deliberately not acted on

**Decision**: `ChatController`'s constructor takes `shared_ptr<OllamaProvider>`,
`shared_ptr<OpenAIProvider>`, **and** `shared_ptr<AnthropicProvider>` explicitly. `hasModelsFor()` and
`dispatchSendChat()` each grow one more `if (provider_id == "anthropic") ...` branch ahead of the
Ollama fallback. No registry, no `std::variant<shared_ptr<...>>` member, no virtual `ChatProvider`
interface.

**Rationale**: this is precisely the moment `openai-provider-adapter/DESIGN.md` §5.1 flagged as the
signal to reconsider: *"Rule of three, deliberately not applied at two... If/when a third provider
adapter cycle starts, that is the point at which 'two duplicated branches becoming three' is the
concrete signal to introduce a real dispatch mechanism."* That moment has now arrived, and this design
still does not introduce one, for a reason stronger than stylistic consistency: **this Spec's own
REQ-F-039 explicitly re-states the constraint for this exact provider** — *"AnthropicProvider shall
likewise be a plain concrete class... it shall NOT introduce a virtual base class or dynamic-dispatch
interface."* Unlike the OpenAI cycle (where the "no virtual interface" instruction applied to the
provider class itself, and `ChatController`'s own dispatch mechanism was a Stage-2-only inference from
that spirit), this Spec's §F.9 note explicitly extends the same instruction to *this* provider,
independent of how many providers now exist. Treating "three branches now, not two" as license to
override an explicit, repeated Spec constraint would be scope creep, not architecture improvement.

A `QHash<QString, std::function<...>>`-based registry was seriously considered as a middle path (it
avoids a vtable and an inheritance hierarchy, so it arguably doesn't violate "no virtual base class or
dynamic-dispatch interface" to the letter) — see §10.1 for why it was rejected anyway: it still
resolves provider identity at runtime through an indirection the Spec's plain-language intent ("a
plain concrete class... mirroring OllamaProvider and OpenAIProvider's method shapes") reads as wanting
to avoid, and it would require type-erasing three structurally-similar-but-not-identical `sendChat()`
signatures (`AnthropicProvider` has `setAuthKey()`/`setMaxOutputTokens()` neither sibling has) behind
a common callable shape, adding real complexity to save three lines of `if`/`else`.

**Consequence flagged for Stage 4**: this is a **breaking** constructor change for both `ChatController`
and `ChatViewModel`, exactly as the OpenAI cycle's two-provider change was — every existing call site
(`ChatViewModel::ChatViewModel()`, `tests/application/test_chat_controller.cpp`'s fixture,
`tests/application/test_chat_view_model.cpp`'s fixture if it constructs a `ChatViewModel` directly)
must be updated in the same change. This is required rework, not optional cleanup (§9).

### 5.2 `AnthropicProviderSettingsController` is a new, parallel class — not a template or a further generalization of the existing two

**Decision**: mirrors `openai-provider-adapter/DESIGN.md` §5.2's decision exactly, extended to a third
data point: `AnthropicProviderSettingsController` duplicates ~85% of `OpenAIProviderSettingsController`'s
shape rather than sharing a template or common base.

**Rationale**: the three controllers' surfaces have now diverged in three different directions —
`ProviderSettingsController` has `contextWindow`; `OpenAIProviderSettingsController` has neither
`contextWindow` nor `maxOutputTokens`; `AnthropicProviderSettingsController` has `maxOutputTokens` but
not `contextWindow`, and calls `setAuthKey()` instead of `setAuthToken()` on its provider. A
generalization mechanism now needs to abstract over a Cartesian-ish combination of "does this provider
have a context-window knob," "does it have a max-output-tokens knob," "what's its auth-setter method
name" — more machinery than three ~150-line, well-understood, independently-testable classes. This
strengthens (not weakens) the OpenAI cycle's original call: divergence increased with the third data
point, it didn't converge toward a clean shared shape.

### 5.3 `AnthropicProviderSettingsController` reuses `ProviderSettingsController`'s `CredentialStore`, chained independently of `OpenAIProviderSettingsController`

**Decision**: `AnthropicProviderSettingsController::create()` force-resolves `ProviderSettingsController`
as a QML singleton (same `qmlEngine->singletonInstance<T*>()` pattern) and reuses its `credentialStore()`
accessor directly — it does **not** force-resolve `OpenAIProviderSettingsController` first, even though
that class also happens to hold a reference to the same store.

**Rationale**: `OpenAIProviderSettingsController` does not expose a `credentialStore()` accessor (only
`ProviderSettingsController` does, per `provider-settings-ui/DESIGN.md` §5.3's original design — it was
the first controller constructed, so it was the natural place to add the accessor). Chaining
`AnthropicProviderSettingsController` off `OpenAIProviderSettingsController` instead of directly off
`ProviderSettingsController` would (a) require adding a redundant second `credentialStore()` accessor
to `OpenAIProviderSettingsController` for no benefit, and (b) create an artificial ordering dependency
between two sibling controllers that have no other relationship — QML singleton resolution order
between `OpenAIProviderSettingsController` and `AnthropicProviderSettingsController` becomes
irrelevant either way once both chain independently off the one already-accessor-bearing
`ProviderSettingsController`. One shared `CredentialStore` instance still serves `"ollama"`,
`"openai"`, and `"anthropic"` keys — `isAvailable()`'s sticky-false-per-instance behavior
(`provider-settings-ui/DESIGN.md` §0) stays consistent across all three panels for the same reason
§5.3 of the OpenAI design already established for two.

### 5.4 `x-api-key` + `anthropic-version` via a renamed `requestHeaders()`, not a reused `authHeaders()`

**Decision**: `AnthropicProvider` does not reuse `OllamaProvider`/`OpenAIProvider`'s `authHeaders()`
method name or its "empty map when no token" shape. `requestHeaders()` always returns at least one
entry (`anthropic-version: 2023-06-01`, REQ-F-002 applies to *every* request, auth or not) and
conditionally adds a second (`x-api-key`, only when `auth_key_` is non-empty, REQ-F-038).

**Rationale**: `authHeaders()`'s contract on the other two providers is "empty map ⇒ no Authorization
header at all" — a single conditional entry. Anthropic's requirement is structurally different: one
header is unconditional, the other is conditional, and they're semantically different things
(protocol-version pinning vs. authentication). Reusing the name `authHeaders()` for a method that
always returns a non-empty map when unauthenticated would be misleading to a future reader; the
rename documents the behavioral difference directly at the call site.

### 5.5 System-prompt hoisting lives entirely inside `sendChat()`'s request-construction step — no shared helper, no domain change

**Decision**: the System-message-extraction-and-concatenation logic (§3.2) is inline in
`AnthropicProvider::sendChat()`, operating on the same `std::vector<holonight_domain::Message>
history` every provider already receives. No new domain type, no `Conversation`-level "system prompt"
concept, no shared `hoistSystemMessages()` utility exposed to `OllamaProvider`/`OpenAIProvider`.

**Rationale**: this is exactly what the Spec's own Non-Goals list asks for — *"Shared system-prompt
helper — only Anthropic needs hoisting (Ollama/OpenAI keep sending system inline)."* Ollama's
`/api/chat` and OpenAI's `/v1/responses` both accept `{role: "system", content: "..."}` inline in their
respective message/input arrays and have no top-level `system` field to hoist into — there is nothing
for a shared helper to be shared *with* yet. `Message`/`MessageRole` themselves need no change:
`MessageRole::System` already exists and is already read by `roleToOpenAiString()`'s `case
MessageRole::System: return "system"` branch (never hit in practice for OpenAI today, since nothing
currently sends System-role messages through that path, but the enum value itself was always
general-purpose, not Ollama/OpenAI-specific).

### 5.6 `max_tokens` is always present, sourced from a new required config field — not optional, not hardcoded

**Decision**: `AnthropicProviderConfig` gains `max_output_tokens: int = 1024`, distinct from
`temperature`, with its own Settings UI control (SpinBox, REQ-F-026 item 5) and its own
`ConfigRepository`/JSON round trip (§3.3/§3.4/§2.4).

**Rationale**: REQ-F-007 is explicit that Anthropic's API rejects requests missing `max_tokens`
(unlike OpenAI's Responses API, where `max_output_tokens` is optional and this cycle's OpenAI adapter
deliberately omits it — REQ-C-008 of that Spec). This is the one structural field addition this cycle
makes to the config/settings-controller/QML triad that has no counterpart in either sibling provider —
called out explicitly here because it's the field most likely to be forgotten if an implementer
pattern-matches too literally against `OpenAIProviderConfig`, which has no equivalent.

### 5.7 Denylist substrings: `embedding`, `moderation`, `vision`, `ocr` — fixed, unexported, fail-open (REQ-F-010/011)

See §6 for the implementation. Decision restated here: the four substrings come directly from
REQ-F-010's text, not invented — no additional substrings (e.g., no attempt to also exclude a
hypothetical `claude-instant` legacy family) are added speculatively. Fail-open (§6) means a future
Anthropic chat model not matching any of the four substrings appears automatically (REQ-F-011),
exactly mirroring OpenAI's denylist philosophy.

### 5.8 Async-startup credential-arrival race — confirmed to apply unchanged, same mark-loaded-on-success + retry-on-arrival pattern

**Decision**: `AnthropicProviderSettingsController::load()` does **not** call
`probe_provider_->refresh()` synchronously after seeding `base_url_` from config. It calls
`credential_store_->retrieve("anthropic")` and defers the first `refresh()` call to
`onTokenRetrieved()`, exactly mirroring `OpenAIProviderSettingsController::load()`/`onTokenRetrieved()`
(`openai_provider_settings_controller.cpp:240-256, 368-382`). `ChatViewModel::create()` likewise does
**not** call `anthropic_provider_->refresh()` in `ChatViewModel`'s constructor (§3.6) — the shared
provider's first model fetch happens only after `AnthropicProviderSettingsController::onTokenRetrieved()`
calls `provider_->refresh([this] { model_sync_callback_(std::nullopt); }, ...)`.

**Rationale**: this race is not OpenAI-specific — it is a property of *any* provider whose
authentication is asynchronous (real `SecretServiceCredentialStore::retrieve()` is a D-Bus round
trip). `AnthropicProvider` has exactly the same shape (an HTTP request that needs `auth_key_` populated
*before* it fires, and `auth_key_` is only known after an async credential fetch resolves), so the
same fix applies unchanged. Confirmed applicable rather than assumed: `OllamaProvider`'s equivalent
`refresh()` call in `ChatViewModel`'s constructor is *not* deferred this way, because Ollama's default
config has no auth token requirement in the common case (local Ollama, no token) — Anthropic (like
OpenAI) always requires a key for any real request, so it follows the OpenAI precedent, not the Ollama
one.

### 5.9 `AnthropicProviderSettingsController`'s `authToken` Q_PROPERTY keeps its sibling controllers' name, despite calling `setAuthKey()` underneath

**Decision**: the Q_PROPERTY exposed to QML stays named `authToken` (matching `ProviderSettingsController`
and `OpenAIProviderSettingsController`'s existing property name), even though `AnthropicProvider`'s own
C++ method is `setAuthKey()` (REQ-F-039's method-surface list names it that way explicitly, distinct
from `setAuthToken()`).

**Rationale**: the Q_PROPERTY name is UI-facing internal wiring, private to `AnthropicSettingsPanel.qml`
— no other QML file binds to it by name (`ProviderListDelegate.qml` only reads
`anthropicConnectionStatus`, not the token itself), so there's no cross-panel naming contract to
preserve for correctness. Keeping `authToken` was chosen anyway for two reasons: (1) `AnthropicSettingsPanel.qml`
can be built by literally copy-pasting `OpenAISettingsPanel.qml`'s API-key `TextField`/`Button` block
and only retargeting the singleton name, with zero property-name edits inside that block — smaller,
more mechanically-verifiable diff; (2) a reviewer scanning all three settings panels' QML side-by-side
sees the same property name doing the same job in all three, which is more legible than a
provider-specific rename would be. The C++-level method name difference (`setAuthKey()` vs.
`setAuthToken()`) exists on the *provider* class, one layer below, where REQ-F-039 pins it explicitly
— the controller is free to smooth that back out for its own QML-facing surface, and does.

### 5.10 QML integration: routing only, no new list entry (§0 item 6, detailed in §7)

Restated from §0: `ProviderListPanel.qml`'s model array already contains `"Anthropic"` (added
speculatively when the placeholder-only UI was first built). This cycle's QML changes are therefore
pure *routing* changes — `ProvidersPage.qml`'s `Loader.sourceComponent` switch and
`ProviderListDelegate.qml`'s status-dot binding — plus one new leaf file, `AnthropicSettingsPanel.qml`.
No changes to `ProviderListPanel.qml` itself.

---

## 6. Denylist Implementation and Testing (REQ-F-010/011)

Same pattern and location convention as `OpenAIProvider`'s denylist
(`openai-provider-adapter/DESIGN.md` §6): a `static constexpr std::array<QLatin1StringView, 4>` in an
anonymous namespace inside `anthropic_provider.cpp`, not exposed in the header, not configurable:

```cpp
namespace {
constexpr std::array<QLatin1StringView, 4> kDenylistedSubstrings{
    QLatin1StringView("embedding"),
    QLatin1StringView("moderation"),
    QLatin1StringView("vision"),
    QLatin1StringView("ocr"),
};

bool isDenylistedModel(const QString& modelId) {
  return std::ranges::any_of(kDenylistedSubstrings,
                             [&modelId](QLatin1StringView substring) { return modelId.contains(substring); });
}
}  // namespace
```

Applied inside `fetchModelList()`'s success handler, immediately after parsing the `"data"` array
(Anthropic's `GET /v1/models` response shape is `{"data": [{"id": "claude-...", ...}, ...]}` — the same
top-level `"data"` array shape as OpenAI's, per the current Anthropic API docs) and before appending to
`available_models_`, tagging every surviving entry `provider_id = "anthropic"`.

**Testing**: not unit-tested in isolation (no exposed static/free method for direct unit testing) —
tested exclusively through the public surface, matching both siblings' convention exactly:
`FakeHttpClient::enqueueBufferedSuccess()` with a `/v1/models` JSON body mixing denylisted and allowed
IDs (`"claude-3-opus-20250219"`, `"claude-3-5-sonnet-20241022"`, `"claude-4-ultra-future"` (fail-open
probe, REQ-F-011), `"embedding-001"`, `"moderation-latest"`, plus vision/ocr-substring probes), then
asserting on `provider.availableModels()` after `refresh()`.

---

## 7. QML Integration — exact files

**New file**: `qml/workspace/AnthropicSettingsPanel.qml`. Structurally copies
`qml/workspace/OpenAISettingsPanel.qml` (§0 item 6 confirmed this is the right template, not
`OllamaSettingsPanel.qml`, since Ollama's panel has a `contextWindow` control Anthropic doesn't need)
with these deltas:
- Every `OpenAIProviderSettingsController` reference becomes `AnthropicProviderSettingsController`.
- Temperature `Slider`/`SpinBox` range becomes 0.0–1.0 (not OpenAI's 0.0–2.0), matching REQ-F-006.
- One new control block inserted between Temperature and API key: a "Max output tokens" `SpinBox`
  (`from: 1`, `to: 200000`, `value: AnthropicProviderSettingsController.maxOutputTokens`,
  `onValueModified: AnthropicProviderSettingsController.maxOutputTokens = value`) — REQ-F-026 item 5
  explicitly calls out that this must be "distinct from the temperature control," i.e. its own labeled
  row, not folded into the temperature row's SpinBox/Slider pair the way OpenAI's panel has none.
- `openAiConnectionStatus` binding becomes `anthropicConnectionStatus`.
- Placeholder text `"https://api.openai.com/v1"` becomes `"https://api.anthropic.com"`.

**Modified**: `qml/workspace/ProvidersPage.qml`:

```diff
     Loader {
         Layout.fillWidth: true
         Layout.fillHeight: true
         sourceComponent: {
             switch (providerList.selectedProvider) {
             case "Ollama": return ollamaPanel
             case "OpenAI": return openAiPanel
+            case "Anthropic": return anthropicPanel
             default: return unsupportedPanel
             }
         }
     }

     Component {
         id: openAiPanel

         OpenAISettingsPanel {}
     }

+    Component {
+        id: anthropicPanel
+
+        AnthropicSettingsPanel {}
+    }
+
     Component {
         id: unsupportedPanel

         UnsupportedProviderPanel {
             providerName: providerList.selectedProvider
         }
     }
```

`"Google"` is unaffected — it still falls through to `default: return unsupportedPanel`, matching the
Non-Goals list ("Google provider adapter — separate future cycle").

**Modified**: `qml/workspace/ProviderListDelegate.qml`:

```diff
     readonly property bool isOllama: root.providerName === "Ollama"
     readonly property bool isOpenAi: root.providerName === "OpenAI"
+    readonly property bool isAnthropic: root.providerName === "Anthropic"
     readonly property color statusColor: {
         if (root.isOllama) { ... }
         if (root.isOpenAi) {
             switch (OpenAIProviderSettingsController.openAiConnectionStatus) {
             case "connected": return HoloniightPalette.success
             case "error": return HoloniightPalette.error
             default: return HoloniightPalette.textMuted
             }
         }
+        if (root.isAnthropic) {
+            switch (AnthropicProviderSettingsController.anthropicConnectionStatus) {
+            case "connected": return HoloniightPalette.success
+            case "error": return HoloniightPalette.error
+            default: return HoloniightPalette.textMuted
+            }
+        }
         return HoloniightPalette.textMuted;
     }
```

**Not modified**: `qml/workspace/ProviderListPanel.qml` (§0 item 6 — `"Anthropic"` already present in
the model array), `qml/workspace/SettingsWindow.qml` (routes to `ProvidersPage {}` as a whole, no
per-provider knowledge), `qml/workspace/UnsupportedProviderPanel.qml` (still used for `"Google"`).

---

## 8. CMake Wiring — exact files, confirmed no target-level changes needed

Every file below is a **source-list addition to an existing target** — no new `add_library`, no new
`add_executable`, no new `find_package`, no new `target_link_libraries` entry, matching the OpenAI
cycle's own finding and CLAUDE.md's "no GLOB for C++ sources" convention:

- **`src/providers/CMakeLists.txt`** — `add_library(holonight_providers STATIC ...)`'s source list
  gains `include/holonight_providers/anthropic_provider.h` and `src/anthropic_provider.cpp`.
- **`src/config/CMakeLists.txt`** — **no change**. `provider_config.h` and `config_repository.cpp` are
  already in the source list; `AnthropicProviderConfig` and the two new `ConfigRepository` methods are
  added *inside* those already-listed files, not as new files.
- **`src/application/CMakeLists.txt`** — `add_library(holonight_application STATIC ...)`'s source list
  gains `include/holonight_application/anthropic_provider_settings_controller.h` and
  `src/anthropic_provider_settings_controller.cpp`. `chat_controller.h`/`.cpp` and
  `chat_view_model.h`/`.cpp` are already listed — their in-place edits (§3.5/§3.6) need no new list
  entries.
- **`tests/CMakeLists.txt`** — `add_executable(test_holonight_ai ...)`'s source list gains
  `providers/test_anthropic_provider.cpp` and `application/test_anthropic_provider_settings_controller.cpp`
  (see Corrections to SPEC.md above — this is the **only** test-related CMake change; no new
  `add_executable`, no new `gtest_discover_tests()` call).
- **`apps/chat/CMakeLists.txt`** — **no change**. Two independent reasons, both already true today:
  (1) `qml/*.qml` is picked up via `file(GLOB_RECURSE ... CONFIGURE_DEPENDS "qml/*.qml")` — this is a
  QML-resource glob, not a C++ source glob, and already existed before this cycle; `AnthropicSettingsPanel.qml`
  is automatically included on the next CMake reconfigure (`CONFIGURE_DEPENDS` triggers that
  reconfigure). (2) `AnthropicProviderSettingsController` is a *third* `QML_SINGLETON` type added to
  the already-`STATIC` `holonight_application` library — the `qt6_extract_metatypes()` /
  `combine-metatypes.cmake` / `_qt_internal_qml_type_registration()` machinery this file already runs
  (confirmed present and correctly double-clearing `INTERFACE_SOURCES`, per CLAUDE.md's "QML Singletons
  From Static Libraries" section) handles an arbitrary number of `QML_SINGLETON` types in that one
  library with zero additional plumbing — already proven true for going from one such type
  (`ChatViewModel`) to two (`+OpenAIProviderSettingsController`), and nothing about that mechanism is
  count-limited.
- **`src/domain/CMakeLists.txt`**, **`src/credentials/CMakeLists.txt`** — **no change** (§0 items 2, 5
  — no domain or credentials-interface change is made).

---

## 9. Known Risks

- **Denylist drift**, restated for Anthropic's four substrings (`embedding`, `moderation`, `vision`,
  `ocr`): a future Anthropic chat model family not matching any of the four (e.g., a hypothetical
  `claude-embed-lite` that's actually a chat model, or a genuinely new non-chat family this list
  doesn't anticipate) will either be wrongly excluded or wrongly included; REQ-F-011's fail-open
  default means the likely failure mode is "wrongly included, then fails loudly with a 400 when
  actually used" — annoying, not silently broken, since the error still surfaces via REQ-F-018's path.
  No mitigation proposed this cycle (matches OpenAI's own identical risk entry).
- **The framing/`extractSseDataPayload()` code is now duplicated a third time** (§4.1) — Ollama's
  NDJSON parser is structurally different enough not to count, but OpenAI's and Anthropic's SSE
  block-framing loops are now near-identical copies. This is the concrete "rule of three" trigger for
  extracting a shared `sse_framing.h`/`.cpp` helper (`findSseBlockDelimiter()`, `extractSseDataPayload()`,
  the `onData` accumulation loop) — deliberately *not* done in this cycle (§4.1's YAGNI reasoning), but
  flagged here explicitly as the most likely first refactor a future cycle (a fourth SSE-based provider,
  or general cleanup) should reach for, unlike §5.1's `ChatController` dispatch question, which this
  design argues should stay un-refactored even at three providers because of REQ-F-039's explicit
  constraint (no equivalent constraint blocks extracting shared *parsing* code, which touches no
  provider's public shape).
- **Content-block-index simplification (§4.6) is a real, if currently harmless, gap.** If a future
  cycle enables tool use or extended thinking for Anthropic (both explicit Non-Goals here), multiple
  concurrent content blocks (different `index` values interleaved in the stream) would have their text
  deltas concatenated in arrival order regardless of index, producing garbled output. Not a risk for
  this cycle's actual request shape (§4.6 explains why), but the parser does not defend against it
  structurally — a future implementer extending this adapter for tool use must revisit `routeSseEvent()`
  before doing so, not just add new event-type branches alongside it.
- **`ChatController`'s and `ChatViewModel`'s constructor signature changes are breaking, not additive**
  (§5.1) — every existing call site and test fixture that constructs either class directly needs
  simultaneous updates; there is no default-parameter escape hatch for a third required `shared_ptr`,
  same category of required rework the OpenAI cycle already logged for its own two-provider change.
- **Testing surface for three-way provider routing is entirely `ChatController`-level**, same structure
  as OpenAI's own flagged risk: `tests/application/test_chat_controller.cpp` gains
  `ModelId{provider_id: "anthropic", ...}` cases alongside the existing `"openai"`/(implicit-Ollama)
  ones, verified via each provider's own `FakeHttpClient` recording the call — adequate, but there is
  still no structural (compile-time-enforced) guarantee that a missed branch in a *fourth* future
  provider cycle would fail to compile; a hand-written test is the only safety net, same as it was for
  going from one branch to two.
- **`max_output_tokens` has no upper-bound validation anywhere in this design**, by explicit Spec
  instruction (REQ-C-003: "shall NOT implement... different validation rules or ranges"). A user who
  enters e.g. `500000` will have that value sent verbatim in every request and receive Anthropic's own
  400 error for exceeding the model's actual context/output limit — expected behavior per the Spec, but
  worth restating here since it's the one numeric field in this cycle with no client-side guardrail at
  all (`temperature` at least gets a [0.0, 1.0] range check in `save()`, §5.7/§3.7).

---

## 10. Alternatives Considered

### 10.1 For provider dispatch (§5.1)

- **`QHash<QString, std::function<HttpRequestHandlePtr(...)>>`-style registry**, populated once at
  `ChatController` construction with one entry per provider, replacing the `if`/`else` chain with a
  hash lookup. Seriously considered given this is the third-provider "rule of three" moment. Rejected:
  it does not reduce the amount of code (still three lambda-wrapped call sites, one per provider, just
  relocated from `if` bodies into a hash's value-initializer), it adds a runtime failure mode the
  current design doesn't have (a `provider_id` with no registered entry — currently, an unrecognized
  `provider_id` falls through to the Ollama branch, which is itself arguably a latent bug but at least
  a deterministic one; a hash-based lookup would need its own explicit "unknown provider" error path),
  and — most decisively — it resolves provider identity through a value-level type-erasure mechanism
  at exactly the place REQ-F-039 asks this cycle not to introduce one. Revisit only if a fourth
  SSE-based provider makes three duplicated `if` chains (this one, plus any other `ModelId::provider_id`-keyed
  dispatch site that emerges) feel genuinely unmaintainable — not preemptively.
- **`std::variant<shared_ptr<OllamaProvider>, shared_ptr<OpenAIProvider>, shared_ptr<AnthropicProvider>>`
  member with `std::visit`.** Rejected for the same structural reason the OpenAI design already gave
  for two providers, now sharper with three: `ChatController` needs simultaneous access to *all three*
  providers in several places (`hasModelsFor()` needs the one named by a specific `ModelId`, while
  `ChatViewModel`'s combined `availableModels` independently needs all three's lists) — a `variant<>`
  models "exactly one active provider," which still isn't this shape at three providers any more than
  it was at two.

### 10.2 For `AnthropicProviderSettingsController` (§5.2)

- **Generalize all three settings controllers into one template.** Rejected more strongly than at two
  providers (§5.2) — three real, independently-evolving data points is exactly the sample size where "we
  have three, therefore let's generalize" starts to look tempting but is still premature: the three
  controllers' property sets have diverged in three *different* directions (§5.2), not converged toward
  a common shape a template could cleanly capture without its own escape hatches (optional properties,
  conditional compilation, or a customization-point interface — all of which reintroduce the complexity
  a template was meant to avoid).

### 10.3 For the SSE parser (§4)

- **Extract a shared `sse_framing.h` helper now, since this is the second real copy.** Considered and
  rejected for *this* cycle specifically (not rejected in principle — see §9's explicit flag that this
  is the most likely next refactor): extracting shared parsing infrastructure as a drive-by inside a
  feature cycle risks conflating a pure-refactor change (touching `openai_provider.cpp`, a file this
  Spec has no requirement to modify) with this cycle's actual scope, and makes the diff harder to
  review against SPEC.md's requirements list. A dedicated follow-up change, reviewed purely as a
  refactor with `OpenAIProvider`'s existing test suite as its regression safety net, is the more
  legible way to do this — flagged in §9, deliberately not started here.
- **Reuse `OllamaProvider::processLine()`'s NDJSON loop instead of `OpenAIProvider`'s SSE loop.**
  Rejected for the same reason the OpenAI design already gave (OpenAI DESIGN.md §8.3) — NDJSON's
  per-line, blank-line-is-noise framing and SSE's blank-line-is-the-only-terminator framing are
  different enough in spirit that forcing one to emulate the other would need more conditionals than
  the two independent implementations already require in total.

---

## 11. Test and Build Plan Notes

- `tests/providers/test_anthropic_provider.cpp` is added to the **existing single** `test_holonight_ai`
  CTest binary (Corrections to SPEC.md, above, and §8) — not a new standalone executable. Covers:
  request construction (system-prompt hoisting with 0/1/2 System messages, `max_tokens` presence and
  value, `x-api-key`/`anthropic-version` headers present and correct, `x-api-key` absent when no
  credential per REQ-F-038), SSE parsing (fragmented chunks across five-plus distinct blocks per
  REQ-F-013's acceptance criterion, `ping`/`message_start`/`content_block_start`/`content_block_stop`
  all no-op, `content_block_delta`→`ContentDelta`, `message_stop`→`Completed`, `message_delta` no-op),
  error mapping (401/429/400 bodies → extracted `error.message`, malformed JSON → descriptive `Error`,
  stream-ends-without-`message_stop` → `Error`), model filtering (all four denylist substrings +
  fail-open probe), cancellation (`cancel()` mid-stream halts further delta processing).
- `tests/application/test_anthropic_provider_settings_controller.cpp` mirrors
  `test_openai_provider_settings_controller.cpp`'s structure, plus new cases for `maxOutputTokens`
  round-tripping through `save()`/`load()`/`cancel()`/`resetToDefaults()` and the [0.0, 1.0] temperature
  validation boundary (REQ-F-006) differing from OpenAI's [0.0, 2.0].
- `tests/application/test_chat_controller.cpp`'s existing fixture-building helpers
  (`makeOllamaProvider()`/an OpenAI equivalent) gain a third `makeAnthropicProvider()` helper; every
  test that constructs a `ChatController` directly is updated for the third constructor parameter
  (§5.1's flagged breaking change); new test cases assert `ModelId{provider_id: "anthropic", ...}`
  routes to the Anthropic `FakeHttpClient` recording, not Ollama's or OpenAI's.
- `tests/config/test_config_repository.cpp` gains `AnthropicProviderConfig` round-trip tests
  (including `max_output_tokens`) and a three-provider read-merge-write test: save Ollama, then OpenAI,
  then Anthropic in sequence, asserting all three sections coexist in `config.json` after the third
  save (extending the OpenAI cycle's two-provider version of this same regression test).
- `tests/providers/fake_http_client.h` needs **no changes** — its existing
  `enqueueBufferedSuccess()`/`enqueueBufferedError()`/streaming equivalents already accept arbitrary
  `QByteArray`/`QString` payloads, sufficient for every Anthropic response shape this cycle needs to
  simulate (§4's error-body recovery included).

---

## Document History

- v1.0 (2026-07-24): Initial design for Stage 2, covering all of SPEC.md's REQ-F-001 through
  REQ-C-007, with an explicit correction to REQ-NF-001's test-executable wording.
