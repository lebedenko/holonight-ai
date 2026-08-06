# OpenAI Provider Adapter Design

**Document Version**: 1.0
**Date**: 2026-07-23
**Modules**: `holonight_providers` (new: `OpenAIProvider`; changed: `QtNetworkHttpClient` error-body
propagation), `holonight_config` (new: `OpenAIProviderConfig`; changed: `ConfigRepository` gains
`loadOpenAiConfig()`/`saveOpenAiConfig()` and a read-merge-write fix shared with
`saveOllamaConfig()`), `holonight_application` (new: `OpenAIProviderSettingsController`; changed:
`ChatController`, `ChatViewModel`), `qml/workspace/` (new: `OpenAISettingsPanel.qml`; changed:
`ProvidersPage.qml`, `ProviderListDelegate.qml`), `tests/` (new + extended GTest coverage)
**Traces to**: `docs/sdd/openai-provider-adapter/SPEC.md` (38 functional, 6 non-functional, 8
constraint requirements)
**Precedent**: `docs/sdd/provider-settings-ui/DESIGN.md` (shared/probe-provider pattern, QML
singleton wiring, `ConfigRepository` shape — reused directly), `src/providers/src/ollama_provider.cpp`
(NDJSON `StreamContext`/`failStream()` shape — reused structurally, reframed for SSE)
**Status**: Ready for Stage 3 (Tasks)

---

## 0. Ground truth this design was checked against

Four things were verified by reading the real source rather than assumed, and each shapes a
concrete decision below:

1. **The domain layer already has the provider-selection hook.** `holonight_domain::ModelId` is
   `{provider_id: QString, model_name: QString}` (`src/domain/include/holonight_domain/model_id.h`),
   and `holonight_persistence::ConversationRecord`/`ConversationSummary` already persist an
   `std::optional<ModelId> last_model_id` per conversation
   (`src/persistence/include/holonight_persistence/conversation_record.h:20`). **No domain or
   persistence schema change is needed** — "which provider backs this conversation" is already a
   solved, shipped concept; this cycle only needs to populate `provider_id = "openai"` for OpenAI
   models, exactly as `OllamaProvider::fetchModelList` already tags Ollama entries.

2. **The chat-window model picker is already provider-agnostic.** `qml/workspace/WorkspaceWindow.qml`
   (lines ~104–149) iterates `ChatViewModel.availableModels`, an opaque list of `{provider_id,
   model_name}` maps, and displays each entry as `"<provider_id>/<model_name>"`; selecting one calls
   `ChatViewModel.selectedModelId = ChatViewModel.availableModels[index]`. **This QML file needs zero
   changes.** Once `ChatViewModel.availableModels` includes OpenAI-tagged entries alongside Ollama's,
   the existing combo box shows and routes both without modification. This answers Step 3's "how does
   the UI let a user pick a provider" question for free.

3. **`ConfigRepository::saveOllamaConfig()` currently overwrites the whole `"providers"` JSON object
   from scratch** (`src/config/src/config_repository.cpp:73-102` builds `root` with only an
   `"ollama"` key and writes it unconditionally). Today this is invisible because only one provider
   section exists. The moment `saveOpenAiConfig()` is added with the same pattern, saving one
   provider's settings would silently delete the other's `providers.*` section on disk. This is a
   **latent bug exposed by this cycle**, not a hypothetical — §5.4 fixes it with a shared
   read-merge-write helper.

4. **`QtNetworkHttpClient`'s non-2xx error path never surfaces the HTTP response body — for either
   `send()` or `sendStreaming()`** (`src/providers/src/qt_network_http_client.cpp:53-119`). Both
   `on_error` branches use either `reply->errorString()` (Qt's own generic wording, e.g. "Error
   downloading ... server replied: Unauthorized" — never the JSON body) or a hardcoded
   `"Ollama returned HTTP status %1"` string that is wrong on its face for an OpenAI request. Since
   REQ-F-013/020/021/022/032 all require the *actual* `error.message` text from OpenAI's JSON body to
   reach the UI, this file needs a real fix, detailed in §5.5 — not a paper cut, a functional gap that
   would make every OpenAI error path show the wrong text in production (invisible to the existing
   `FakeHttpClient`-based test suite, since tests never exercise real `QtNetworkHttpClient` code).

---

## 1. Components

| Component | File(s) | Role |
|---|---|---|
| `holonight_providers::OpenAIProvider` | `src/providers/include/holonight_providers/openai_provider.h` (new), `src/providers/src/openai_provider.cpp` (new) | Concrete adapter mirroring `OllamaProvider`'s shape (REQ-F-038): request construction, SSE parsing, model discovery + denylist filtering. |
| `holonight_providers::QtNetworkHttpClient` (changed) | `src/providers/src/qt_network_http_client.cpp` | Non-2xx branches now surface the response body text when present, and the streaming fallback string is no longer Ollama-specific (§5.5). |
| `holonight_config::OpenAIProviderConfig` | `src/config/include/holonight_config/provider_config.h` (changed — struct added alongside `OllamaProviderConfig`) | Non-secret config: `base_url`, `default_model`, `temperature`. |
| `holonight_config::ConfigRepository` (changed) | `src/config/include/holonight_config/config_repository.h`, `src/config/src/config_repository.cpp` | Gains `loadOpenAiConfig()`/`saveOpenAiConfig()`; both save methods switch to read-merge-write (§5.4, fixes item 3 above). |
| `holonight_application::ChatController` (changed) | `src/application/include/holonight_application/chat_controller.h`, `.cpp` | Constructor now takes **both** concrete providers; routes `sendChat()`/model-availability checks on `ModelId::provider_id` — no virtual dispatch (§2 / Step 3 decision). |
| `holonight_application::ChatViewModel` (changed) | `src/application/include/holonight_application/chat_view_model.h`, `.cpp` | Owns both provider instances; `syncAvailableModelsFromProvider()` → `syncAvailableModels()`, now unions both providers' model lists; gains `openAiProviderForSettings()`. |
| `holonight_application::OpenAIProviderSettingsController` | `src/application/include/holonight_application/openai_provider_settings_controller.h` (new), `.cpp` (new) | New `QML_SINGLETON`, structurally parallel to `ProviderSettingsController`, reusing the **same** `CredentialStore` instance (§5.3). |
| `qml/workspace/OpenAISettingsPanel.qml` | new | Mirrors `OllamaSettingsPanel.qml`; no context-window control (REQ-C-008); binds to `OpenAIProviderSettingsController`. |
| `qml/workspace/ProvidersPage.qml` (changed) | existing | Loader routes `"OpenAI"` to the new panel instead of `UnsupportedProviderPanel.qml`. |
| `qml/workspace/ProviderListDelegate.qml` (changed) | existing | Status dot now also reads `OpenAIProviderSettingsController.openAiConnectionStatus` for the OpenAI row (was hardcoded gray). |
| `tests/providers/test_openai_provider.cpp` | new (added to the existing single `test_holonight_ai` CTest binary — see §9) | Request construction, SSE parsing/fragmentation, event routing, denylist filtering, cancellation, error mapping. |
| `tests/application/test_chat_controller.cpp` (changed) | existing | Fixture gains an `OpenAIProvider`; new tests cover provider routing by `model.provider_id`. |
| `tests/application/test_openai_provider_settings_controller.cpp` | new | Mirrors `test_provider_settings_controller.cpp`. |
| `tests/config/test_config_repository.cpp` (changed) | existing | New round-trip tests for `OpenAIProviderConfig`; a new test asserting Ollama-then-OpenAI (and reverse) saves don't clobber each other (§5.4). |

### 1.1 Class relationship

```
                              ┌───────────────────────────────────────┐
                              │              ChatViewModel               │ (QML_SINGLETON, existing)
                              │  - ollama_provider_: shared_ptr<OllamaProvider>│
                              │  - openai_provider_: shared_ptr<OpenAIProvider>│ (new)
                              │  - chat_controller_: unique_ptr<ChatController>│
                              │  + providerForSettings() const                 │
                              │  + openAiProviderForSettings() const           │ (new)
                              │  + syncAvailableModels(preferred)              │ (renamed, now unions both)
                              └───────────────┬─────────────────────────────────┘
                                              │ owns
                              ┌───────────────▼─────────────────────────────────┐
                              │                 ChatController                    │ (changed)
                              │  - ollama_provider_: shared_ptr<OllamaProvider>   │
                              │  - openai_provider_: shared_ptr<OpenAIProvider>   │
                              │  routes sendChat()/availableModels() checks on    │
                              │  ModelId::provider_id — plain if/else, no vtable  │
                              └────────────────────────────────────────────────────┘

  ┌──────────────────────────┐ shared_ptr (Save only)   ┌──────────────────────────────┐
  │  ProviderSettingsController │◄─────────────────────────┤   ChatViewModel (Ollama half)  │
  │  (existing, unchanged shape)│                          └──────────────────────────────┘
  │  - provider_/probe_provider_: OllamaProvider ×2         │
  │  + credentialStore() const  │ (new plain accessor — lets OpenAI's controller reuse the same store)
  └───────────────┬──────────────┘
                  │ engine forces this singleton to exist first (same pattern ChatViewModel already uses)
  ┌───────────────▼──────────────────────────┐ shared_ptr (Save only)  ┌──────────────────────────────┐
  │      OpenAIProviderSettingsController       │◄─────────────────────────┤  ChatViewModel (OpenAI half)   │
  │      (new, structurally parallel)           │                          └──────────────────────────────┘
  │  - provider_/probe_provider_: OpenAIProvider ×2                          │
  │  - credential_store_: SAME CredentialStore* as ProviderSettingsController │
  │  Q_PROPERTY baseUrl, defaultModel, temperature (no contextWindow),        │
  │             authToken, hasStoredToken, credentialStoreAvailable,          │
  │             credentialOperationInProgress, availableModelNames,           │
  │             modelRefreshInProgress/Error, testConnectionInProgress/       │
  │             Status/Message, openAiConnectionStatus, saveNotice            │
  └────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Data Flow

### 2.1 Chat-send path (REQ-F-001–007, F-010–015, F-024, F-025)

1. User selects a model in `WorkspaceWindow.qml`'s combo box → `ChatViewModel.selectedModelId =
   {provider_id: "openai", model_name: "gpt-4o"}` (unchanged QML — §0 item 2).
2. `ChatViewModel::send(text)` → `chat_controller_->send(*conversation_, selected_model_id_, text,
   onEventCallback)` (unchanged call site — `ChatController::send()`'s signature does not change,
   only its constructor and internal routing do).
3. `ChatController::send()` validates via a new private `hasModelsFor(model.provider_id)` (replaces
   the old direct `provider_->availableModels().empty()` check), then calls `startStream()`, which
   calls a new private `dispatchSendChat(model, history, on_event, idle_timeout)`:
   ```cpp
   if (model.provider_id == QStringLiteral("openai")) {
     return openai_provider_->sendChat(model, history, on_event, idle_timeout);
   }
   return ollama_provider_->sendChat(model, history, on_event, idle_timeout);
   ```
4. `OpenAIProvider::sendChat()` builds the `/v1/responses` POST body (§3.1), issues it via
   `http_client_->sendStreaming()`, and returns the `HttpRequestHandlePtr` — identical shape to
   `OllamaProvider::sendChat()`, so `ChatController::startStream()`'s handle bookkeeping
   (`in_flight_[key].handle = handle`) is untouched.
5. Raw bytes arrive via `on_data`; the SSE parser (§4) accumulates them into `StreamContext::buffer`,
   frames complete `data: {...}` blocks on blank lines, parses each as JSON, and routes on `type`
   (§4) into the **same** `holonight_domain::StreamEvent` variant Ollama uses
   (`ContentDelta`/`Completed`/`Error`/`Cancelled`) via the **same** `on_event` callback signature.
6. From this point on, the flow is unchanged and provider-agnostic: `ChatController::handleStreamEvent()`
   updates the conversation's working message and forwards the event to `ChatViewModel::onStreamEvent()`,
   which updates `MessageListModel`, persists via `ConversationRepository` if enabled, and refreshes
   `canSend`/`isStreaming`. **None of this code needs to change** — it already only touches
   `holonight_domain::StreamEvent`, never a provider type.
7. Stop button → `ChatViewModel::stop()` → `ChatController::stop(conversation_id)` → looks up
   `in_flight_[key].handle` (opaque `HttpRequestHandlePtr`, provider-agnostic already) → `handle->cancel()`.
   No routing needed here — the handle was already resolved to the right provider at step 4.

### 2.2 Model-discovery path (REQ-F-016–019, REQ-F-032) — shared/probe flow

Mirrors `ProviderSettingsController`'s existing Ollama flow (`provider-settings-ui/DESIGN.md` §2.5)
exactly, on a second, independent pair of provider instances:

1. `OpenAIProviderSettingsController::create()` builds a **probe** `OpenAIProvider` over its own
   fresh `QtNetworkHttpClient`, entirely separate from `chatViewModel->openAiProviderForSettings()`
   (the shared instance `ChatController` uses for real chat traffic).
2. `refreshModels()`/`testConnection()`/`load()` all point `probe_provider_->setBaseUrl(...)` and
   `probe_provider_->setAuthToken(...)` at the **in-panel** (possibly unsaved) values and call
   `probe_provider_->refresh(on_success, on_error)`, which issues `GET /v1/models` with a Bearer
   header (REQ-F-016) and, on success, replaces `probe_provider_`'s cached model list after denylist
   filtering (§4.5/§5.2).
3. Editing the panel — including clicking Refresh/Test Connection repeatedly — **never** touches
   `provider_` (the shared instance), so live chat traffic is unaffected until Save, exactly matching
   the rationale already recorded for Ollama in `provider-settings-ui/DESIGN.md` §5.3. This property
   falls out for free by giving `OpenAIProviderSettingsController` its own probe instance rather than
   trying to generalize `ProviderSettingsController` to share one probe across two provider types.
4. `save()` (REQ-F-027/028): validates temperature range, calls
   `config_repository_.saveOpenAiConfig(config)` (§5.4's read-merge-write), then applies the new
   `base_url`/`temperature` to `provider_` (the shared instance) and calls `provider_->refresh([this,
   preferred] { model_sync_callback_(preferred); })` — `model_sync_callback_` is
   `chatViewModel->syncAvailableModels(preferred)` in production, which re-unions both providers'
   lists so the chat window's model picker updates immediately, matching the existing Ollama
   precedent's "Save updates the chat window without restart" behavior.
5. Token save/remove: identical shape to Ollama's `save()`/`onTokenStored()`/`onTokenRemoved()`, using
   `credential_store_->store("openai", token)` / `remove("openai")` (REQ-F-028/030/036/037) — the
   **only** difference from Ollama's implementation is the provider-ID string literal.

### 2.3 Config load fallback table (REQ-F-034)

Identical structure to `loadOllamaConfig()`'s existing table (`provider-settings-ui/DESIGN.md` §2.1),
keyed at `"providers"."openai"` instead of `"providers"."ollama"`:

| Condition | Result |
|---|---|
| File missing / empty / malformed JSON / not an object | `OpenAIProviderConfig{}` (defaults) |
| `"providers"` missing or `"providers"."openai"` missing/not-object | `OpenAIProviderConfig{}` |
| Present, one or more fields missing/wrong-typed | Present fields load; missing/wrong-typed fields individually fall back to that field's default (via `QJsonValue::toString(default)`/`toDouble(default)`, same mechanism as Ollama) |

---

## 3. Interfaces / APIs

### 3.1 `src/providers/include/holonight_providers/openai_provider.h` (new)

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

// Mirrors OllamaProvider's method surface exactly (REQ-F-038) — no setContextWindow() (REQ-C-008
// forbids max_output_tokens; OpenAI has no comparable request-time context-window knob in this
// cycle's scope). Deliberately a plain concrete class: no virtual base, no shared interface with
// OllamaProvider (see DESIGN.md §5.1 for why).
class OpenAIProvider {
 public:
  explicit OpenAIProvider(std::shared_ptr<HttpClient> http_client,
                          QString base_url = QStringLiteral("https://api.openai.com/v1"));

  [[nodiscard]] const std::vector<holonight_domain::ModelId>& availableModels() const;

  // GET /v1/models, denylist-filtered (REQ-F-016-018), tagging every entry provider_id="openai".
  void refresh(const std::function<void()>& on_complete = {});
  void refresh(const std::function<void()>& on_success, const std::function<void(const QString&)>& on_error);

  void setBaseUrl(QString base_url);
  [[nodiscard]] const QString& baseUrl() const;

  // Bearer-token support. Empty (default) ⇒ no Authorization header (REQ-F-037). Write-only, never
  // logged (REQ-NF-006).
  void setAuthToken(QString auth_token);

  // Applied to every subsequent sendChat()'s "temperature" field. Default 1.0 matches
  // holonight_config::OpenAIProviderConfig{}'s own default (REQ-F-005).
  void setTemperature(double temperature);

  // POST /v1/responses, SSE-streamed (REQ-F-001-015). idle_timeout mirrors OllamaProvider's
  // default (30s) — no per-requirement timeout distinction; REQ-NF-005's 10s timeout applies only
  // to refresh()'s GET /v1/models, via idle_timeout at the fetchModelList() call site (§3.5).
  HttpRequestHandlePtr sendChat(const holonight_domain::ModelId& model,
                                const std::vector<holonight_domain::Message>& history,
                                const std::function<void(const holonight_domain::StreamEvent&)>& on_event,
                                std::chrono::milliseconds idle_timeout = std::chrono::seconds{30});

 private:
  void fetchModelList(const std::function<void()>& on_success, const std::function<void(const QString&)>& on_error);
  [[nodiscard]] QHash<QString, QString> authHeaders() const;

  std::shared_ptr<HttpClient> http_client_;
  QString base_url_;
  std::vector<holonight_domain::ModelId> available_models_;
  QString auth_token_;
  double temperature_ = 1.0;
};

}  // namespace holonight_providers
```

Notes on deliberate parameter/behavior deltas from `OllamaProvider`, each traced to a REQ:

- No `context_window_` field/setter (REQ-C-008).
- `fetchModelList()`'s idle timeout for `GET /v1/models` is fixed at `std::chrono::seconds{10}`
  (REQ-NF-005), hardcoded at the call site inside `fetchModelList()` — not exposed as a constructor
  parameter, since nothing in the Spec asks it to be configurable (YAGNI).
- `sendChat()`'s request construction (§3.2) is the only place `MessageRole` → OpenAI's
  `{role, content}` shape is produced; it reuses the same `roleToOpenAiString()`-style free function
  pattern `ollama_provider.cpp` already uses (`"system"`/`"user"`/`"assistant"` — identical strings,
  so this could theoretically be shared, but a 6-line switch statement duplicated once is simpler
  and less coupling than extracting a cross-provider helper for three string literals; revisit only
  if a third provider needs the same mapping).

### 3.2 Request body construction (REQ-F-001–007)

```cpp
QJsonArray input;
for (const Message& message : history) {
  QJsonObject entry;
  entry[QStringLiteral("role")] = roleToOpenAiString(message.role());
  entry[QStringLiteral("content")] = message.text();
  input.append(entry);
}

QJsonObject body;
body[QStringLiteral("model")] = model.model_name;
body[QStringLiteral("input")] = input;
body[QStringLiteral("store")] = false;                 // REQ-F-003, always present
body[QStringLiteral("temperature")] = temperature_;     // REQ-F-005
// Deliberately absent: previous_response_id (REQ-F-004/C-005), max_output_tokens (REQ-F-006/C-008),
// reasoning.effort (REQ-C-004).

const HttpRequest request{.method = HttpMethod::Post,
                          .url = base_url_ + QStringLiteral("/responses"),
                          .body = QJsonDocument(body).toJson(QJsonDocument::Compact),
                          .content_type = QStringLiteral("application/json"),
                          .headers = authHeaders()};
```

`authHeaders()` is byte-for-byte the same implementation as `OllamaProvider::authHeaders()` (empty
map when `auth_token_.isEmpty()`, else one `Authorization: Bearer <token>` entry) — REQ-F-037's "omit
the header entirely" behavior is identical, so this is copied rather than shared for the same
three-line-duplication reasoning as §3.1's role mapping.

### 3.3 `src/config/include/holonight_config/provider_config.h` (changed — struct added)

```cpp
// Non-secret OpenAI configuration persisted to config.json (REQ-F-033). No token field — Secret
// Service's exclusive responsibility (REQ-F-035). Default member initializers double as REQ-F-034's
// missing-file/-field fallback values.
struct OpenAIProviderConfig {
  QString base_url = QStringLiteral("https://api.openai.com/v1");
  QString default_model;
  double temperature = 1.0;

  friend bool operator==(const OpenAIProviderConfig&, const OpenAIProviderConfig&) = default;
};
```

### 3.4 `src/config/include/holonight_config/config_repository.h` (changed)

```cpp
class ConfigRepository {
 public:
  explicit ConfigRepository(QString file_path);

  [[nodiscard]] OllamaProviderConfig loadOllamaConfig() const;
  [[nodiscard]] std::expected<void, QString> saveOllamaConfig(const OllamaProviderConfig& config) const;

  // REQ-F-034: same never-throws / plain-return-value / std::expected<void, QString> conventions as
  // the Ollama pair above, exactly.
  [[nodiscard]] OpenAIProviderConfig loadOpenAiConfig() const;
  [[nodiscard]] std::expected<void, QString> saveOpenAiConfig(const OpenAIProviderConfig& config) const;

 private:
  // New (§5.4): both save methods now funnel through these instead of building `root` from scratch,
  // so saving one provider's section preserves whatever the other provider's section already holds.
  [[nodiscard]] QJsonObject readRootObject() const;
  [[nodiscard]] std::expected<void, QString> writeRootObject(const QJsonObject& root) const;

  QString file_path_;
};
```

`config_repository.cpp` restructures around the two new private helpers (§5.4 has the exact bodies);
`loadOllamaConfig()`/`loadOpenAiConfig()` both call `readRootObject()` and read their own
`"providers"."<id>"` sub-object; `saveOllamaConfig()`/`saveOpenAiConfig()` both call
`readRootObject()`, mutate only their own key inside `"providers"`, and call `writeRootObject()`.

### 3.5 `src/application/include/holonight_application/chat_controller.h` (changed)

```diff
 class ChatController {
  public:
-  explicit ChatController(std::shared_ptr<holonight_providers::OllamaProvider> provider);
+  // Takes both concrete providers explicitly — no virtual base, no registry (Step 3 decision,
+  // DESIGN.md §5.1). Either shared_ptr may be null only in tests that exercise a single provider's
+  // codepath in isolation; production always supplies both (ChatViewModel::ChatViewModel()).
+  explicit ChatController(std::shared_ptr<holonight_providers::OllamaProvider> ollama_provider,
+                          std::shared_ptr<holonight_providers::OpenAIProvider> openai_provider);

   std::expected<void, SendRejected> send(...);       // unchanged signature
   std::expected<void, SendRejected> regenerate(...);  // unchanged signature
   void stop(const holonight_domain::ConversationId& conversation_id);           // unchanged
   [[nodiscard]] bool isStreaming(const holonight_domain::ConversationId&) const; // unchanged

  private:
   ...                                                  // InFlightStream, startStream() unchanged shape
+  [[nodiscard]] bool hasModelsFor(const QString& provider_id) const;
+  [[nodiscard]] holonight_providers::HttpRequestHandlePtr dispatchSendChat(
+      const holonight_domain::ModelId& model, const std::vector<holonight_domain::Message>& history,
+      const std::function<void(const holonight_domain::StreamEvent&)>& on_event,
+      std::chrono::milliseconds idle_timeout);

-  std::shared_ptr<holonight_providers::OllamaProvider> provider_;
+  std::shared_ptr<holonight_providers::OllamaProvider> ollama_provider_;
+  std::shared_ptr<holonight_providers::OpenAIProvider> openai_provider_;
   QHash<QString, InFlightStream> in_flight_;
 };
```

`send()`/`regenerate()`'s existing guard `if (model.model_name.isEmpty() || provider_->availableModels().empty())`
becomes `if (model.model_name.isEmpty() || model.provider_id.isEmpty() || !hasModelsFor(model.provider_id))`.
`startStream()`'s `provider_->sendChat(...)` call becomes `dispatchSendChat(model, history, ..., idle_timeout)`.

### 3.6 `src/application/include/holonight_application/chat_view_model.h` (changed)

```diff
   explicit ChatViewModel(std::shared_ptr<holonight_providers::OllamaProvider> provider,
+                         std::shared_ptr<holonight_providers::OpenAIProvider> openai_provider,
                          std::unique_ptr<holonight_persistence::ConversationRepository> repository,
                          QString initial_default_model_id = {}, QObject* parent = nullptr);

   [[nodiscard]] std::shared_ptr<holonight_providers::OllamaProvider> providerForSettings() const;
+  [[nodiscard]] std::shared_ptr<holonight_providers::OpenAIProvider> openAiProviderForSettings() const;

-  void syncAvailableModelsFromProvider(const std::optional<holonight_domain::ModelId>& preferredModelId = std::nullopt);
+  // Renamed (was FromProvider, singular) — now unions ollama_provider_ and openai_provider_'s
+  // availableModels() lists into one combined ChatViewModel::availableModels (DESIGN.md §2.1 step 5).
+  void syncAvailableModels(const std::optional<holonight_domain::ModelId>& preferredModelId = std::nullopt);
```

`ollama_provider_`/`openai_provider_` replace the single `provider_` member. `initial_default_model_id_`
keeps its existing meaning **unchanged** — it seeds a brand-new conversation's default model from
`OllamaProviderConfig::default_model` only (§5.6 explains why this is a deliberate, minimal choice,
not an oversight). `create()`'s factory now also loads `OpenAIProviderConfig` and constructs a second
`QtNetworkHttpClient`-backed `OpenAIProvider`, both passed into the constructor.

### 3.7 `src/application/include/holonight_application/openai_provider_settings_controller.h` (new)

Structurally identical to `ProviderSettingsController` (`src/application/include/holonight_application/provider_settings_controller.h`)
with these deltas:

```diff
 class OpenAIProviderSettingsController : public QObject {
   Q_OBJECT
   QML_ELEMENT
   QML_SINGLETON

   Q_PROPERTY(QString baseUrl READ baseUrl WRITE setBaseUrl NOTIFY baseUrlChanged)
   Q_PROPERTY(QString defaultModel READ defaultModel WRITE setDefaultModel NOTIFY defaultModelChanged)
-  Q_PROPERTY(int contextWindow READ contextWindow WRITE setContextWindow NOTIFY contextWindowChanged)
   Q_PROPERTY(double temperature READ temperature WRITE setTemperature NOTIFY temperatureChanged)
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
-  Q_PROPERTY(QString ollamaConnectionStatus READ ollamaConnectionStatus NOTIFY ollamaConnectionStatusChanged)
+  Q_PROPERTY(QString openAiConnectionStatus READ openAiConnectionStatus NOTIFY openAiConnectionStatusChanged)
   Q_PROPERTY(QString saveNotice READ saveNotice NOTIFY saveNoticeChanged)

  public:
   using ModelSyncCallback = std::function<void(const std::optional<holonight_domain::ModelId>&)>;   // reused as-is

   static OpenAIProviderSettingsController* create(QQmlEngine* qml_engine, QJSEngine* js_engine);

   explicit OpenAIProviderSettingsController(std::shared_ptr<holonight_providers::OpenAIProvider> shared_provider,
                                             std::shared_ptr<holonight_providers::OpenAIProvider> probe_provider,
                                             holonight_credentials::CredentialStore* credential_store,
                                             holonight_config::ConfigRepository config_repository,
                                             ModelSyncCallback model_sync_callback, QObject* parent = nullptr);

   // ... contextWindow accessor pair removed; every other accessor/Q_INVOKABLE identical in name and
   // shape to ProviderSettingsController's (load/refreshModels/testConnection/save/cancel/resetToDefaults) ...
 };
```

`ProviderSettingsController` also gains one new plain accessor so `OpenAIProviderSettingsController::create()`
can reuse the same `CredentialStore` instance (§5.3) instead of constructing a second one:

```diff
 class ProviderSettingsController : public QObject {
  public:
   ...
+  // Plain C++ accessor (not a Q_PROPERTY — only OpenAIProviderSettingsController::create() needs the
+  // raw pointer, to share one CredentialStore instance across both settings singletons; DESIGN.md
+  // §5.3). Ownership is unchanged — still parented to this controller, still destroyed with it.
+  [[nodiscard]] holonight_credentials::CredentialStore* credentialStore() const { return credential_store_; }
 };
```

`create()`:

```cpp
OpenAIProviderSettingsController* OpenAIProviderSettingsController::create(QQmlEngine* qml_engine, QJSEngine* js_engine) {
  Q_UNUSED(js_engine)
  auto* chatViewModel = qml_engine->singletonInstance<ChatViewModel*>("HolonightChat", "ChatViewModel");
  Q_ASSERT(chatViewModel != nullptr);
  // Forces ProviderSettingsController to exist first, exactly as ProviderSettingsController::create()
  // already forces ChatViewModel to exist first — same established pattern, one more link in the chain.
  auto* ollamaSettings = qml_engine->singletonInstance<ProviderSettingsController*>("HolonightChat", "ProviderSettingsController");
  Q_ASSERT(ollamaSettings != nullptr);

  auto probeHttpClient = std::make_shared<holonight_providers::QtNetworkHttpClient>();
  auto probeProvider = std::make_shared<holonight_providers::OpenAIProvider>(std::move(probeHttpClient));

  holonight_config::ConfigRepository configRepository(holonight_config::resolveConfigFilePath());

  return new OpenAIProviderSettingsController(
      chatViewModel->openAiProviderForSettings(), std::move(probeProvider),
      ollamaSettings->credentialStore(),  // reused, not owned by this controller
      std::move(configRepository),
      [chatViewModel](const std::optional<holonight_domain::ModelId>& preferred) {
        chatViewModel->syncAvailableModels(preferred);
      });
}
```

### 3.8 `src/providers/src/qt_network_http_client.cpp` (changed — §5.5)

```diff
   QObject::connect(reply, &QNetworkReply::finished, reply, [reply, on_success, on_error]() {
-    if (reply->error() != QNetworkReply::NoError) {
-      on_error(reply->errorString());
-    } else if (!isSuccessfulHttpStatus(reply)) {
-      on_error(QStringLiteral("Ollama returned HTTP status %1")
-                   .arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()));
-    } else {
+    if (isSuccessfulHttpStatus(reply)) {
       on_success(reply->readAll());
+    } else {
+      const QByteArray body = reply->readAll();
+      on_error(body.isEmpty() ? reply->errorString()
+                              : QString::fromUtf8(body));
     }
     reply->deleteLater();
   });
```

Same restructuring applied to `sendStreaming()`'s `finished` handler's error branch (body there is
almost always already drained by prior `readyRead`/`on_data` calls by the time `finished` fires, so
`reply->readAll()` typically returns empty and falls back to `errorString()` — this is expected and
fine; the streaming path's *real* error-body recovery happens inside `OpenAIProvider` itself from its
own `StreamContext::buffer`, §4.6, not here). The one-line fallback string this replaces is also
generalized: `"Ollama returned HTTP status %1"` → `"Request failed with HTTP status %1"`, since this
file is shared HTTP infrastructure, not Ollama-specific, and a second real caller now exists.

---

## 4. SSE Parser Design (REQ-F-008–015)

`OpenAIProvider::sendChat()` uses the same `StreamContext` + `failStream()` shape as
`OllamaProvider::sendChat()` (`src/providers/src/ollama_provider.cpp:19-37`), reframed for
block-at-a-time SSE framing instead of line-at-a-time NDJSON:

```cpp
struct StreamContext {
  QByteArray buffer;
  HttpRequestHandlePtr handle;
  bool terminal = false;
  bool completed = false;
};
```

### 4.1 Framing — where a "block" ends

OpenAI's Responses API streams events as classic SSE:

```
event: response.output_text.delta
data: {"type":"response.output_text.delta","delta":"Hello"}

event: response.completed
data: {"type":"response.completed", ...}

```

Each event is one or more lines, terminated by a **blank line** (`\n\n`). REQ-F-008 only requires
buffering raw bytes and reassembling on that blank-line boundary, so the parser does not need to
understand the `event:` line at all — the payload's own `"type"` field (REQ-F-009) is authoritative
and redundant with `event:`, exactly as OpenAI's API is documented to guarantee. Algorithm, run
inside `on_data`:

```cpp
auto onData = [context, on_event](const QByteArray& chunk) {
  if (context->terminal) return;
  context->buffer += chunk;

  qsizetype blockEnd = context->buffer.indexOf("\n\n");
  while (blockEnd != -1) {
    const QByteArray block = context->buffer.left(blockEnd);
    context->buffer.remove(0, blockEnd + 2);
    processSseBlock(block, context, on_event);
    if (context->terminal) return;
    blockEnd = context->buffer.indexOf("\n\n");
  }
};
```

- `context->buffer += chunk` handles arbitrary chunk boundaries: a chunk that splits mid-JSON-object,
  mid-field-name, or even mid-`\n\n` sequence (one `\n` in this chunk, the second in the next) is
  simply not yet found by `indexOf("\n\n")` and stays buffered until enough bytes arrive — no special
  partial-match state is needed, `indexOf` re-scans the whole (small) buffer each call, which is
  cheap at chat-message scale (REQ-NF-004's "non-blocking" requirement is satisfied by this being
  O(buffer size) synchronous work per callback, not by any threading).
- `QByteArray::indexOf("\n\n")` operates on raw bytes, not decoded text, so it is correct across
  multi-byte UTF-8 codepoints split across chunks: `\n` (0x0A) never appears as a continuation byte
  in UTF-8, so a chunk boundary landing inside a multi-byte character cannot falsely trigger or hide
  a `\n\n` match. `QJsonDocument::fromJson()` is only ever called on a **complete** block (after the
  blank-line boundary is found), by which point any split codepoint has been fully reassembled into
  the buffer — the codepoint-splitting risk that superficially looks like an edge case for this
  design turns out not to be one, precisely because framing happens on ASCII newlines, not on decoded
  text (see §7 for the residual risk this does *not* eliminate).

### 4.2 `processSseBlock()` — extracting the payload

```cpp
void processSseBlock(const QByteArray& block, const std::shared_ptr<StreamContext>& context,
                     const std::function<void(const StreamEvent&)>& on_event) {
  QByteArray payload;
  for (const QByteArray& rawLine : block.split('\n')) {
    const QByteArray line = rawLine.endsWith('\r') ? rawLine.chopped(1) : rawLine;  // tolerate CRLF
    if (line.startsWith("data:")) {
      QByteArray value = line.mid(5);
      if (value.startsWith(' ')) value = value.mid(1);   // SSE's one-optional-space-after-colon rule
      payload += value;
    }
    // event:, id:, retry:, and comment lines (starting with ':') are intentionally ignored — the
    // JSON payload's own "type" field is authoritative (REQ-F-009).
  }
  if (payload.isEmpty()) return;  // e.g. a bare ": keep-alive" comment block — REQ-F-015-adjacent no-op

  QJsonParseError parseError{};
  const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    failStream(context, on_event, QStringLiteral("Malformed response from OpenAI: %1").arg(parseError.errorString()));
    return;
  }
  routeSseEvent(doc.object(), context, on_event);
}
```

### 4.3 `routeSseEvent()` — REQ-F-009 through REQ-F-015, REQ-C-001

```cpp
void routeSseEvent(const QJsonObject& object, const std::shared_ptr<StreamContext>& context,
                   const std::function<void(const StreamEvent&)>& on_event) {
  const QString type = object.value(QStringLiteral("type")).toString();

  if (type == QStringLiteral("response.output_text.delta") || type == QStringLiteral("response.refusal.delta")) {
    on_event(StreamEvent{ContentDelta{object.value(QStringLiteral("delta")).toString()}});   // REQ-F-010/011
    return;
  }
  if (type == QStringLiteral("response.completed")) {                                          // REQ-F-012
    context->terminal = true;
    context->completed = true;
    on_event(StreamEvent{holonight_domain::Completed{}});
    return;
  }
  if (type == QStringLiteral("response.failed") || type == QStringLiteral("response.incomplete")) {  // REQ-F-014
    failStream(context, on_event, extractErrorMessage(object, type));
    return;
  }
  if (type == QStringLiteral("error") || (type.isEmpty() && object.contains(QStringLiteral("error")))) {  // REQ-F-013
    failStream(context, on_event, extractErrorMessage(object, type));
    return;
  }
  // Everything else — response.created, response.in_progress, response.output_item.*,
  // response.content_part.*, response.function_call_arguments.* (REQ-C-001), code-interpreter and
  // annotation events — is a deliberate no-op (REQ-F-015). No default branch needed; falling off the
  // end of this function *is* the no-op.
}

QString extractErrorMessage(const QJsonObject& object, const QString& type) {
  const QString message = object.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
  return message.isEmpty() ? QStringLiteral("OpenAI reported an error (%1)").arg(type) : message;
}
```

### 4.4 Malformed JSON within a completed block — fail, don't skip

A block whose `data:` payload fails to parse as JSON calls `failStream()` (§4.2), matching
`OllamaProvider::processLine()`'s existing precedent for malformed NDJSON lines, rather than silently
skipping it. Rationale: a malformed payload from a *supposedly complete* block (the blank-line
terminator already arrived, so this is not a buffering artifact) indicates something is genuinely
wrong with the connection or OpenAI's response — surfacing it as an `Error` event is more honest to
the user than silently dropping content and leaving them staring at an incomplete reply with no
explanation (REQ-NF-001 explicitly calls out "malformed JSON" as a test case to cover, without
prescribing skip-vs-fail; failing is the choice that keeps this adapter's error-handling philosophy
consistent with Ollama's, which was already reviewed and shipped).

### 4.5 `on_finished` — REQ-F-013's "stream ends without completion"

```cpp
auto onFinished = [context, on_event]() {
  if (context->terminal) return;
  const QByteArray remaining = context->buffer.trimmed();
  if (!remaining.isEmpty()) {
    processSseBlock(remaining, context, on_event);   // tolerate a final block with no trailing blank line
  }
  if (!context->completed) {
    failStream(context, on_event, QStringLiteral("Stream ended without completion"));
  }
};
```

### 4.6 `on_error` — recovering the JSON error body from the SSE buffer (REQ-F-013/020/021/022/023)

This is the piece that makes REQ-F-013's 401/429 acceptance criteria actually work end-to-end, given
§0 item 4's finding that `HttpClient`'s own error string is not reliable for HTTP-status errors on
the streaming path:

```cpp
auto onError = [context, on_event](const QString& transportMessage) {
  if (context->terminal) return;
  context->terminal = true;
  if (context->handle) context->handle->cancel();

  // A non-2xx response body (e.g. `{"error":{"message":"Invalid API key"}}`) is NOT SSE-framed — it
  // arrives via the same on_data callback as normal chunks (Qt delivers body bytes for HTTP-error
  // statuses too — see DESIGN.md §0 item 4) but never completes a blank-line-terminated block, so it
  // sits unparsed in context->buffer when the transport ultimately reports failure. Recover it here.
  const QByteArray candidate = context->buffer.trimmed();
  const QJsonDocument doc = QJsonDocument::fromJson(candidate);
  const QString bodyMessage =
      doc.isObject() ? doc.object().value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString()
                     : QString();

  on_event(StreamEvent{holonight_domain::Error{bodyMessage.isEmpty() ? transportMessage : bodyMessage}});
};
```

A genuine transport-level failure (DNS failure, connection refused, idle timeout — REQ-F-023) leaves
`context->buffer` empty or non-JSON, so `bodyMessage` stays empty and `transportMessage` (post-§3.8
fix, either the real body text or Qt's `errorString()`) is used verbatim.

---

## 5. Key Decisions With Rationale

### 5.1 Step 3 decision: `ChatController` holds both concrete providers and routes on `ModelId::provider_id` — no virtual interface

**Decision**: `ChatController`'s constructor takes `shared_ptr<OllamaProvider>` **and**
`shared_ptr<OpenAIProvider>` explicitly. Internally, two small private helpers
(`hasModelsFor(provider_id)`, `dispatchSendChat(model, ...)`) branch on
`model.provider_id == "openai"` vs. the Ollama fallback. `ChatViewModel` owns both provider instances
directly and unions their model lists for the QML-facing `availableModels` property.

**Rationale**:
- REQ-F-038's own text is explicit and load-bearing here: *"it shall NOT introduce a virtual base
  class or dynamic-dispatch interface for this cycle, since none exists to extend."* This isn't
  merely CLAUDE.md's general anti-abstraction stance being applied by inference — the Spec states it
  directly as a constraint on this cycle's `OpenAIProvider`. A design that introduces a
  `ChatProvider` virtual interface (even one used only by `ChatController`, not by the providers
  themselves) would be in tension with that instruction's spirit, even if it technically leaves
  `OpenAIProvider` itself concrete.
- The "textbook moment to introduce an abstraction" argument (this being the second real
  implementation) is real and was weighed — but the Spec pre-empts it explicitly for this cycle, and
  the codebase's own convention (`StreamEvent = std::variant<...>`) already favors closed, enumerable
  sum types over open, virtual interfaces where the member set is small and known. Two named
  providers with two named branches is that same idiom applied at the `ChatController` level: no
  vtable, no heap-allocated interface object, compile-time-checked exhaustiveness is achievable if a
  `switch`-like construct is ever wanted later, and every call site is `grep`-able by provider name.
- **Rule of three, deliberately not applied at two.** Hardcoding exactly two known providers inside
  `ChatController` means a *third* provider (Anthropic, Google — both already placeholder rows in
  `ProviderListPanel.qml`) would require editing `ChatController` again. That is an accepted,
  explicit trade-off: per CLAUDE.md's "no speculative future-proofing," building a registry/interface
  now for providers that don't exist yet would be exactly the premature generalization the project
  disallows. If/when a third provider adapter cycle starts, *that* is the point at which "two
  duplicated branches becoming three" is the concrete signal to introduce a real dispatch mechanism —
  not before.
- This design keeps `ProviderSettingsController`'s existing shared/probe pattern **completely
  unchanged** — it never touches `ChatController` or any shared interface, it only hands its
  `provider_` (shared) instance to `ChatViewModel` via `providerForSettings()`, exactly as today.

### 5.2 `OpenAIProviderSettingsController` is a new, parallel class — `ProviderSettingsController` is not generalized

**Decision**: rather than templating or otherwise generalizing `ProviderSettingsController` over a
provider type, a structurally parallel `OpenAIProviderSettingsController` is added (§3.7), duplicating
~90% of `ProviderSettingsController`'s property/method shape.

**Rationale**: the two controllers' surfaces have already diverged in a way a shared base would have
to abstract over awkwardly — `ProviderSettingsController` has `contextWindow`/`ollamaConnectionStatus`;
`OpenAIProviderSettingsController` has neither, and (in a future cycle) might grow OpenAI-specific
fields Ollama never will. A template (`ProviderSettingsController<Config, Provider>`) would need
either a second virtual-dispatch-avoidance trick or compile-time customization points for the
property set itself, which is a heavier and less legible solution than ~150 lines of near-duplicate,
well-understood code that already has a working precedent to copy from. This mirrors §5.1's reasoning
exactly: two known, small, already-diverging shapes are better served by duplication than by a
generalization mechanism invented for exactly two data points.

### 5.3 `OpenAIProviderSettingsController` reuses the *same* `CredentialStore` instance as `ProviderSettingsController`, not a second one

**Decision**: `ProviderSettingsController` gains a plain `credentialStore()` accessor;
`OpenAIProviderSettingsController::create()` force-resolves `ProviderSettingsController` as a QML
singleton first (same `qmlEngine->singletonInstance<T*>()` pattern `ProviderSettingsController::create()`
already uses to force-resolve `ChatViewModel`) and reuses its `CredentialStore*`.

**Rationale**: `CredentialStore` is already provider-ID-generic (`store(providerId, secret)`) — one
instance legitimately serves both `"ollama"` and `"openai"` keys. Constructing a second
`SecretServiceCredentialStore` would open a second D-Bus/Secret-Service session for no benefit, and
worse, would let the two settings panels disagree about `credentialStoreAvailable` (REQ-F-031) if the
underlying service fails after only one instance has probed it — `isAvailable()` is sticky-false
*per instance* (per `provider-settings-ui/DESIGN.md` §0), so two instances could show two different
answers to the same underlying failure. Sharing one instance makes "Secret storage unavailable"
consistent across both provider panels, which is what a user would expect from one desktop app.

### 5.4 `ConfigRepository`'s save methods switch to read-merge-write

**Decision**: `saveOllamaConfig()` and `saveOpenAiConfig()` both route through new private
`readRootObject()`/`writeRootObject()` helpers; each save only mutates its own key inside the shared
`"providers"` object instead of constructing `root` from scratch.

```cpp
QJsonObject ConfigRepository::readRootObject() const {
  QFile file(file_path_);
  if (!file.open(QIODevice::ReadOnly)) return {};
  const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
  return doc.isObject() ? doc.object() : QJsonObject{};
}

std::expected<void, QString> ConfigRepository::saveOpenAiConfig(const OpenAIProviderConfig& config) const {
  QJsonObject root = readRootObject();
  QJsonObject providers = root.value(QLatin1String(kProvidersKey)).toObject();

  QJsonObject openai;
  openai[QLatin1String(kBaseUrlKey)] = config.base_url;
  openai[QLatin1String(kDefaultModelKey)] = config.default_model;
  openai[QLatin1String(kTemperatureKey)] = config.temperature;
  providers[QLatin1String(kOpenAiKey)] = openai;

  root[QLatin1String(kProvidersKey)] = providers;
  return writeRootObject(root);
}
```

**Rationale**: this is a correctness fix, not a style preference (§0 item 3) — without it, the first
time a user saves OpenAI settings after already having Ollama settings configured (the overwhelmingly
common case, since Ollama was the only provider before this cycle), their Ollama `base_url`/
`context_window`/`temperature`/`default_model` would silently vanish from `config.json` on next load,
reverting to defaults. `writeRootObject()` keeps `saveOllamaConfig()`'s existing directory-creation
and `QJsonDocument::Indented` write behavior unchanged, just fed a merged `root` instead of a
single-provider one. `saveOllamaConfig()` is edited alongside `saveOpenAiConfig()` in the same
change, since fixing only the new method while leaving the old one's overwrite bug in place would
just move the data-loss direction (OpenAI-then-Ollama would still clobber) rather than fix it.

### 5.5 `QtNetworkHttpClient`'s non-2xx branches now surface the response body

Covered in depth in §0 item 4 and §3.8. Restated briefly: without this fix, none of REQ-F-020
(401), REQ-F-021 (429), REQ-F-022 (400 reasoning-model error), or REQ-F-032 (Test Connection error
display) can show the actual OpenAI error text in the real, non-test build — they would all show
either Qt's generic `errorString()` or the old literal `"Ollama returned HTTP status %1"` string,
regardless of how correct `OpenAIProvider`'s own code is. This is invisible to the GTest suite (which
only exercises `FakeHttpClient`, never real `QtNetworkHttpClient`), so it would ship a plausible-but-
wrong implementation without any test catching it — flagged explicitly in §7's risks, with the manual
testing checklist (SPEC.md items 2–4) as the only verification surface for this specific change.

### 5.6 A brand-new conversation's default-model fallback stays Ollama-only; OpenAI's `default_model` config is honored only in the Settings panel and via explicit model-picker selection

**Decision**: `ChatViewModel::adoptConversation()`'s "no persisted `last_model_id`" branch is left
exactly as it is today — it seeds `selected_model_id_` from `initial_default_model_id_` (Ollama's
config, tagged `provider_id: "ollama"`), falling back to `models.front()` of the combined list only
if that specific Ollama model isn't available. `OpenAIProviderConfig::default_model` is read by
`OpenAIProviderSettingsController` (to prefill and persist the Settings panel's own "Default model"
combo box, REQ-F-019/027) but is **not** wired into `ChatViewModel`'s brand-new-conversation seeding
logic at all.

**Rationale**: nothing in the Spec's acceptance criteria requires a global "which provider is my
default backend" setting — REQ-F-019 only requires OpenAI's own default-model combo box to behave
correctly *within the OpenAI settings panel* (remember the user's last OpenAI model choice across a
Refresh). Inventing an app-wide "default provider" concept (e.g., "OpenAI over Ollama when both have
a configured default") is exactly the kind of speculative generalization CLAUDE.md disallows — no
acceptance criterion asks for it, and the existing, already-shipped model picker (§0 item 2) already
lets a user manually select any OpenAI model for any conversation regardless of this fallback. If a
future cycle wants a genuine "preferred provider" setting, that is new, explicit scope, not something
this cycle should guess at.

---

## 6. Denylist Implementation and Testing (REQ-F-017/018)

The denylist lives as a `static constexpr std::array<QLatin1StringView, 10>` in an anonymous
namespace inside `openai_provider.cpp` (not exposed in the header, not configurable — REQ-F-017 gives
an exact, fixed list; no requirement asks for it to be user-editable or externally stored):

```cpp
namespace {
constexpr std::array<QLatin1StringView, 10> kDenylistedSubstrings{
    QLatin1StringView("embedding-"), QLatin1StringView("tts-"),      QLatin1StringView("whisper-"),
    QLatin1StringView("dall-e"),     QLatin1StringView("gpt-image"), QLatin1StringView("omni-moderation"),
    QLatin1StringView("text-moderation"), QLatin1StringView("davinci-002"), QLatin1StringView("babbage-002"),
    QLatin1StringView("sora-"),
};

bool isDenylistedModel(const QString& modelId) {
  return std::ranges::any_of(kDenylistedSubstrings,
                             [&modelId](QLatin1StringView substring) { return modelId.contains(substring); });
}
}  // namespace
```

Applied inside `fetchModelList()`'s success handler, immediately after parsing the `"data"` array and
before appending to `available_models_` — a model whose `id` matches any substring is skipped
entirely (fail-open: no match ⇒ included, REQ-F-018).

**Testing**: not unit-tested in isolation (no `openai_provider_denylist_test.cpp`, no exposed public
static method) — tested exclusively through the same public surface every other `OpenAIProvider`
behavior is tested through: `FakeHttpClient::enqueueBufferedSuccess()` with a `/v1/models` JSON body
containing a mix of denylisted and allowed IDs, then asserting on `provider.availableModels()` after
`refresh()`. This matches `OllamaProvider`'s existing testing convention exactly (no provider ever
exposes a private helper for direct unit testing) and is sufficient: REQ-F-017/018's acceptance
criteria are both phrased in terms of "the filtered list," not "the filter function."

---

## 7. Known Risks

- **Denylist drift.** REQ-F-017's list is a snapshot of OpenAI's non-chat model families as of this
  writing. OpenAI periodically ships new non-chat families (e.g., a future `realtime-` or `codex-`
  family) that this fixed list won't catch, and REQ-F-018's fail-open design means an uncaught family
  would appear in the chat model picker and fail loudly with a 400 when actually used — annoying but
  not silently broken (the error still surfaces per REQ-F-013). No mitigation is proposed for this
  cycle; revisiting the denylist is an operational/content task, not an architectural one.
- **`QtNetworkHttpClient`'s error-body fix (§3.8/§5.5) has no direct automated test coverage,** as
  noted in §5.5 — it can only be verified by the manual testing checklist against a real OpenAI
  endpoint. If this fix is skipped or regresses silently in a future change, REQ-F-020/021/022/032
  would degrade to showing generic Qt error text instead of OpenAI's actual message, without any
  `ctest` failure signaling it.
- **SSE parsing edge cases beyond what §4 already handles:**
  - An extremely long single-line JSON payload (e.g., a very long `delta` string, or a large
    `response.completed` object with extensive `output` echoing) is buffered in full before parsing —
    there is no line-length cap. This is consistent with `OllamaProvider`'s existing NDJSON handling
    (also uncapped) and is not expected to matter at chat-message scale, but a pathological or
    malicious server response could grow `StreamContext::buffer` unboundedly before a `\n\n` is ever
    found. No cap is added this cycle (YAGNI, matches existing Ollama behavior) but is worth
    remembering if OpenAI ever streams much larger structured payloads per event.
  - §4.1 shows why *codepoint*-splitting across chunk boundaries is not actually a risk (framing is
    byte-level, on ASCII newlines) — but a chunk boundary landing **inside the `data: ` prefix itself**
    (e.g., `...\ndat` | `a: {...}\n\n`) is handled correctly only because the whole block is
    accumulated before any line-prefix check happens (§4.2 operates on the complete, already-framed
    block) — this is correct by construction, not by luck, but is worth stating explicitly since it's
    the kind of thing a less careful reimplementation could break.
- **Reasoning-model 400 errors remain unexplained to the user by design (REQ-C-003/REQ-C-004).**
  Selecting `o1-preview` and sending a message will show OpenAI's raw
  `"temperature is not supported for this model"` (or similar) with no client-side hint that this is
  a reasoning-model limitation — this is explicitly in-scope-to-not-fix per the Spec, but is worth
  flagging as a real, known rough edge a user will hit if they pick a reasoning model from the
  denylist-filtered (but not reasoning-model-aware) list.
- **Testing surface for the provider-selection mechanism (§5.1) is entirely `ChatController`-level.**
  There is no dedicated "routing" test file — routing correctness is verified by
  `tests/application/test_chat_controller.cpp` sending/regenerating with `ModelId{provider_id:
  "openai", ...}` and `ModelId{provider_id: "ollama", ...}` against a fixture with both fake-backed
  providers, and asserting the right one's `FakeHttpClient` recorded the call. This is adequate
  coverage but means a future third provider added the same way (§5.1's "rule of three" trigger) will
  need its own new branch *and* new routing tests added by hand — there is no structural guarantee
  (e.g., an exhaustive `std::variant` visit) that a missed branch would fail to compile.
- **`ChatController`'s and `ChatViewModel`'s constructor signature changes are breaking, not
  additive.** Unlike `provider-settings-ui`'s changes (which kept every existing call site compiling
  via a default parameter), `ChatController(shared_ptr<OllamaProvider>)` →
  `ChatController(shared_ptr<OllamaProvider>, shared_ptr<OpenAIProvider>)` has no sensible default for
  the second parameter — every existing test fixture and production call site
  (`tests/application/test_chat_controller.cpp`, `ChatViewModel::ChatViewModel()`) must be updated in
  the same change, which Stage 4 (Tasks/Implementation) needs to account for as required rework, not
  optional cleanup.

---

## 8. Alternatives Considered

### 8.1 For Step 3 (provider dispatch)

- **Virtual `ChatProvider` interface with `OllamaProvider`/`OpenAIProvider` both implementing it.**
  Rejected: directly contradicts REQ-F-038's explicit "shall NOT introduce a virtual base class or
  dynamic-dispatch interface for this cycle" instruction, and — per the "textbook moment" framing in
  the task brief — was weighed seriously rather than reflexively dismissed; but the Spec itself
  already made this call for this cycle, and §5.1's `std::variant`-flavored, exactly-two-provider
  reasoning gives a design that gets the same *practical* benefit (`ChatController` stays
  provider-agnostic from `ChatViewModel`'s perspective — it exposes the exact same public API either
  way) without the vtable, without a heap-allocated interface object per provider, and without
  contradicting the Spec's explicit constraint.
- **`ChatController` templated on provider type (`template <typename Provider> class ChatController`),
  instantiated twice, with `ChatViewModel` owning two `ChatController` instances and routing
  `send()`/`stop()`/`isStreaming()` calls itself based on `selected_model_id_.provider_id`.**
  Rejected: pushes the routing responsibility (and its own repeated-logic risk) up into
  `ChatViewModel`, which already has enough responsibilities (conversation lifecycle, persistence,
  QML property bridging); doesn't reduce total code versus §5.1's approach (still two near-identical
  code paths, just organized differently); and complicates `in_flight_` bookkeeping — a single
  conversation could theoretically have `ChatController<OllamaProvider>::isStreaming(id)` true while
  `ChatController<OpenAIProvider>::isStreaming(id)` is also queried, requiring `ChatViewModel` to OR
  two calls together everywhere `ChatController::isStreaming()` is used today. Keeping one
  `ChatController` instance with internal routing (§5.1) keeps `in_flight_` a single source of truth
  per conversation, which is a real correctness simplification, not just a style preference.
- **A tiny compile-time "provider registry" via `std::variant<shared_ptr<OllamaProvider>,
  shared_ptr<OpenAIProvider>>` member with `std::visit`-based dispatch, instead of two named
  `shared_ptr` members and if/else.** Considered as a middle ground — genuinely closer to the
  codebase's `StreamEvent` idiom. Rejected only because `ChatController` needs to reach *both*
  providers simultaneously in several places (checking `hasModelsFor()` against whichever provider a
  *specific* `ModelId` names, while the *other* provider's model list is independently relevant to
  `ChatViewModel`'s combined `availableModels`) — a single `variant<>` member models "exactly one
  active provider at a time," which isn't this shape; two independent providers, each reachable
  directly, is simpler here than shoehorning both into one variant slot.

### 8.2 For `ProviderSettingsController` (§5.2)

- **Generalize `ProviderSettingsController` into a template parameterized on
  `{Config, Provider, providerId}`.** Rejected per §5.2's rationale — the two panels' property
  surfaces have already diverged (`contextWindow` exists only for Ollama), and templating a
  `Q_OBJECT`-derived class over a data shape that already differs is more machinery than the ~150
  lines of duplication it would save, for exactly two data points.

### 8.3 For the SSE parser (§4)

- **Reuse `OllamaProvider::processLine()`'s exact NDJSON line-splitting loop, redefining "line" as
  "text between `\n\n`."** Rejected: NDJSON's line-splitting deliberately trims and skips *every*
  blank line as a no-op separator with no semantic meaning; SSE's blank line is the **only** framing
  signal and must be consumed exactly once per event, and SSE lines carry a `data:`/`event:` prefix
  NDJSON doesn't have — reusing the literal loop would need enough conditionals bolted on to stop
  being the same code in spirit, so a parallel, purpose-built `processSseBlock()`/`routeSseEvent()`
  pair (§4.2/4.3) was written fresh instead, matching the SPEC's own framing of this as "genuinely new
  code, no existing helper."

---

## 9. Test and Build Plan Notes

- `tests/providers/test_openai_provider.cpp` is added to the **existing single** `test_holonight_ai`
  CTest binary (`tests/CMakeLists.txt`'s `add_executable(test_holonight_ai ...)` source list gains one
  line), not a new standalone test executable — SPEC.md's REQ-NF-001 acceptance-criterion wording ("A
  test executable `test_openai_provider` is added") reads more literally than the codebase's actual,
  established pattern (`test_ollama_provider.cpp` is a source file in the one shared binary, not its
  own target); this design follows the real precedent, consistent with how `provider-settings-ui`'s
  own test file was added.
- `tests/providers/fake_http_client.h` needs **no changes** — `enqueueBufferedError(QString)`/
  `emitError(index, QString)` already accept an arbitrary string, letting tests simulate either a
  plain transport failure message or a raw JSON error-body string (§4.6) without any fixture changes.
- `src/providers/CMakeLists.txt`/`src/config/CMakeLists.txt`/`src/application/CMakeLists.txt` each
  gain the new header/source file pairs in their `add_library(... STATIC ...)` source lists — no
  target-level changes (no new `find_package`, no new linked library) beyond that, since
  `OpenAIProvider` uses the same `Qt6::Core`/`Qt6::Network` `holonight_providers` already links, and
  `OpenAIProviderSettingsController` uses the same `holonight_config`/`holonight_credentials` links
  `holonight_application` already has via `ProviderSettingsController`.
- `apps/chat/CMakeLists.txt` needs **no changes**: `OpenAIProviderSettingsController` is a second
  `QML_SINGLETON` type added to the already-`STATIC` `holonight_application` library, and per
  `provider-settings-ui/DESIGN.md` §0's own finding (confirmed still true by reading the current file,
  §0 above), the metatype-combining CMake machinery already handles arbitrary additional
  `QML_SINGLETON` types in that library with zero new plumbing.
- `qml/workspace/OpenAISettingsPanel.qml` needs no `CMakeLists.txt` registration —
  `apps/chat/CMakeLists.txt`'s `GLOB_RECURSE ... CONFIGURE_DEPENDS "qml/*.qml"` already picks up any
  new file under `qml/`.

---

## Document History

- v1.0 (2026-07-23): Initial design for Stage 2, covering all of SPEC.md's REQ-F-001 through
  REQ-C-008.
