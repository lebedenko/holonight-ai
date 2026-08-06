# Google Gemini Provider Adapter Design

**Document Version**: 1.0
**Date**: 2026-07-25
**Modules**: `holonight_providers` (new: `GoogleProvider`), `holonight_config` (new:
`GoogleProviderConfig`; changed: `ConfigRepository` gains `loadGoogleConfig()`/
`saveGoogleConfig()`), `holonight_application` (new: `GoogleProviderSettingsController`; changed:
`ChatController`, `ChatViewModel`), `qml/workspace/` (new: `GoogleSettingsPanel.qml`; changed:
`ProvidersPage.qml`, `ProviderListDelegate.qml`), `tests/` (new + extended GTest coverage)
**Traces to**: `docs/sdd/google-provider-adapter/SPEC.md` (42 functional, 6 non-functional, 9
constraint requirements)
**Precedent**: `docs/sdd/anthropic-provider-adapter/DESIGN.md` (structurally the closest prior
design — system-prompt hoisting into a top-level field, SSE `StreamContext`/`failStream()` shape,
"rule of three" provider-dispatch discussion — reused directly), `src/providers/src/
anthropic_provider.cpp` (concrete SSE implementation this design's parser mirrors), current
(post-Anthropic-cycle) `src/application/include/holonight_application/
provider_settings_controller_base.h` (the *actual* shared base class the three existing settings
controllers now inherit from — see §0.2, this supersedes what the Anthropic DESIGN.md itself
predicted for its own controller)
**Status**: Ready for Stage 3 (Tasks)

---

## Corrections to SPEC.md

Two corrections were found, both discovered by checking SPEC.md's claims against the real Gemini
API shape and the real current source tree rather than trusting the Spec text or the Anthropic
DESIGN.md's now-stale description of the settings-controller layer.

### Correction 1 — REQ-F-001's literal endpoint path cannot produce SSE framing; the streaming
action suffix and `alt=sse` query parameter are required

REQ-F-001 states the system "shall construct HTTP POST requests to the Gemini API endpoint
`/v1beta/models/{model}:generateContent`". Taken literally, `:generateContent` is Gemini's
**non-streaming** action — it returns one complete JSON object in the response body, not a
sequence of blank-line-delimited `data: {...}` blocks. REQ-F-012 through REQ-F-019 (and their
acceptance criteria: "consecutive blocks separated by blank lines, each prefixed with `data: `,
payload parsed as JSON"; "fragmented byte chunks... five distinct SSE blocks") describe SSE framing
that `:generateContent` cannot produce at all — no framing to fragment.

Gemini's actual streaming action is `:streamGenerateContent`, and it has **two** wire formats
depending on a query parameter:
- Without `?alt=sse`: a single top-level JSON array, streamed incrementally as `[`, then
  comma-separated `GenerateContentResponse` objects, then `]` — not SSE, no blank-line framing, no
  `data:` prefix. A correct parser for this shape would need an incremental JSON-array tokenizer,
  not the blank-line SSE block accumulator REQ-F-012 describes.
- With `?alt=sse`: classic SSE — each `GenerateContentResponse` object arrives as its own
  `data: {...}\n\n` block. This is the **only** one of the two shapes that satisfies REQ-F-012's
  literal framing description, and the only one this design's SSE parser (built to reuse
  `AnthropicProvider`'s/`OpenAIProvider`'s existing blank-line-delimited block accumulator — §4) can
  correctly parse.

**Resolution**: `GoogleProvider::sendChat()` issues its POST to
`{base_url}/v1beta/models/{model}:streamGenerateContent?alt=sse` — not `:generateContent`. Model
discovery's `GET /v1beta/models` (REQ-F-008) is unaffected — that endpoint's shape was correctly
specified and needs no correction. This is read as REQ-F-001 using `:generateContent` as informal
shorthand for "the content-generation action" rather than a deliberate, considered choice of the
non-streaming variant — the Spec's own Executive Summary (point 6: "SSE streaming with
safety-filter blocking") and every REQ-F-012–019 acceptance criterion are unambiguous about
intending SSE streaming, and non-streaming `:generateContent` would make roughly a third of
Section F.3's requirements untestable as written. This is the same category of correction the
Anthropic cycle's own DESIGN.md made against REQ-NF-001's test-executable wording — an imprecise
literal reading of one requirement conflicts with a cluster of much more specific, acceptance-
criteria-backed requirements elsewhere in the same Spec, and the specific ones win.

### Correction 2 — REQ-NF-001's and the Verification Strategy section's "test executable
`test_google_provider`" wording repeats the same known Spec-authoring imprecision already
documented (and corrected) in both the OpenAI and Anthropic cycles' own DESIGN.md documents

`tests/CMakeLists.txt` builds exactly **one** GTest binary, `test_holonight_ai`, via a single
`add_executable(test_holonight_ai ...)` whose `SOURCES` list already contains one `.cpp` file per
feature area (confirmed present: `providers/test_anthropic_provider.cpp`,
`application/test_anthropic_provider_settings_controller.cpp`, and the equivalent Ollama/OpenAI
files, all already in that one list — read directly from the current `tests/CMakeLists.txt`, not
assumed). **What to actually build**: `tests/providers/test_google_provider.cpp` (new) and
`tests/application/test_google_provider_settings_controller.cpp` (new) are added as **source
files** to the existing `add_executable(test_holonight_ai ...)` list (§8 has the exact diff). No
new `add_executable`, no new CTest target, no new `gtest_discover_tests()` call. This is now the
**third** cycle in a row this exact Spec-wording pattern has appeared and been corrected the same
way — worth noting as a stable, recurring authoring habit in how these Specs get generated, not a
one-off typo each time.

No other correction to SPEC.md was found — request/response field paths, header names, the
denylist substring list, and the `supportedGenerationMethods` filtering approach were independently
checked against the real Gemini Developer API shape during this design and match (with the one
endpoint-suffix caveat above).

---

## 0. Ground truth this design was checked against

Seven things were verified by reading the real current source — not the Anthropic cycle's own
DESIGN.md description of it, which in one significant place (§0.2 below) has since been superseded
by a refactor — each shaping a concrete decision below.

### 0.1 `AnthropicProvider` is the closest structural template, confirmed field-for-field

Constructor takes `shared_ptr<HttpClient>` + `QString base_url`; `refresh()` has the same two
overloads; `setBaseUrl`/`baseUrl` mutate/read a plain member, read only at request-construction
time; `sendChat()` returns `HttpRequestHandlePtr` and takes the same four parameters (`ModelId`,
`history`, `on_event`, `idle_timeout = 30s` default). `GoogleProvider` reuses this exact shape
(REQ-F-040) — see §3.1. Google and Anthropic are the two providers in this codebase that hoist
system-role messages out of the per-turn message array into a dedicated top-level request field
(`systemInstruction` vs. `system`) — Ollama and OpenAI both accept `{role: "system", ...}` inline
and have no such field to hoist into.

### 0.2 The settings-controller layer has already been generalized once — `ProviderSettingsControllerBase` is real, current code, not a rejected alternative

This is the one place this design diverges most from what the Anthropic cycle's own DESIGN.md
describes for itself. That document's §5.2/§10.2 explicitly *decided against* generalizing
`OpenAIProviderSettingsController`/`AnthropicProviderSettingsController` into a shared base,
reasoning that the three controllers' property sets had "diverged in three different directions."
**That decision did not hold** — reading the actual current tree shows
`src/application/include/holonight_application/provider_settings_controller_base.h` exists today
and `ProviderSettingsController` (Ollama), `OpenAIProviderSettingsController`, and
`AnthropicProviderSettingsController` all now inherit from `ProviderSettingsControllerBase`
publicly, which owns `baseUrl`/`defaultModel`/`authToken`/`hasStoredToken`/
`credentialStoreAvailable`/`credentialOperationInProgress`/`availableModelNames`/
`modelRefreshInProgress`/`modelRefreshError`/`testConnectionInProgress`/`testConnectionStatus`/
`testConnectionMessage`/`saveNotice` as Q_PROPERTYs, plus `refreshModels()`/`testConnection()` as
shared `Q_INVOKABLE`s, plus the full credential-store async-callback wiring
(`onCredentialRetrieved`/`onCredentialStored`/`onCredentialRemoved`/`onCredentialStoreUnavailable`)
and a `ProviderOperations` struct of five `std::function`s (`configure_probe`, `refresh_probe`,
`probe_models`, `set_shared_credential`, `refresh_shared`) that each concrete subclass supplies as
provider-specific closures over its own two `AnthropicProvider`-shaped instances (shared + probe).
Each subclass now retains only what is genuinely provider-specific: `temperature`/`context_window`/
`max_output_tokens` (whichever apply), its own `save()`/`cancel()`/`resetToDefaults()`/`load()`
(since the field set and validation range per provider differ), and its own `<provider>
ConnectionStatus` Q_PROPERTY (a thin wrapper over the base's protected `connectionStatus()`).

**Consequence for this design**: `GoogleProviderSettingsController` is built as a **fourth
subclass of `ProviderSettingsControllerBase`**, not a from-scratch duplicate of
`AnthropicProviderSettingsController`'s pre-refactor shape and not a fifth ad-hoc reimplementation
of credential-store wiring. This is a smaller class than the Anthropic DESIGN.md's own §3.7
sketched (that sketch predates the base-class extraction) — see §3.7 below for the real, current
shape it must match.

### 0.3 `holonight_domain::StreamEvent` is `std::variant<ContentDelta, Completed, Error,
Cancelled>` — confirmed unchanged, still no `Usage` variant

(`src/domain/include/holonight_domain/stream_event.h:29`.) REQ-F-017 (Gemini's `usageMetadata`) is
therefore unconditionally the no-op branch of its own acceptance criterion, exactly as it was for
Anthropic's `usage`/OpenAI's usage fields. No domain change is made (Non-Goals: "no new
`StreamEvent` variants").

### 0.4 `ChatController`'s constructor already takes three named concrete providers with an
if/else dispatch chain — this is now the fourth such addition, and the Spec re-states the same
no-dynamic-dispatch constraint a third time

Current `src/application/include/holonight_application/chat_controller.h`/`.cpp` (read directly,
not inferred) already has `ollama_provider_`, `openai_provider_`, `anthropic_provider_` as three
named `shared_ptr` members, with `hasModelsFor()`/`dispatchSendChat()` each an `if (provider_id ==
"openai") ...; if (provider_id == "anthropic") ...; return ollama_...` chain — no registry, no
virtual base. Google's own SPEC.md §F.9 "Note on current architecture" re-states, for the third
consecutive provider cycle, that the new provider "shall NOT introduce a virtual base class or
dynamic-dispatch interface, since none exists in the current codebase" — see §5.1 for why this
still holds even as the branch count reaches four.

### 0.5 `CredentialStore` is already provider-ID-agnostic and needs zero changes — and already
names "google" as an example in its own doc comment

`src/credentials/include/holonight_credentials/credential_store.h:15`: `// providerId is a
free-form string ("openai", "anthropic", "google", ...)`. `"google"` was already anticipated by
name in this interface's original design, two provider cycles ago. `store("google", key)` /
`retrieve("google")` / `remove("google")` work completely unmodified (REQ-F-038/041/042 are
satisfied by doing nothing to this module — confirmed, not assumed).

### 0.6 `ProviderListPanel.qml`'s model array already contains `"Google"`

`qml/workspace/ProviderListPanel.qml:11`: `model: ["Ollama", "OpenAI", "Anthropic", "Google"]`.
Confirmed by reading the file directly. It currently falls through `ProvidersPage.qml`'s
`Loader.sourceComponent` switch to `default: return unsupportedPanel`. Only the *routing* — the
`Loader` switch and `ProviderListDelegate.qml`'s status-dot binding — needs to change (§7), exactly
the same "list already anticipated the provider, only routing needs to catch up" situation the
Anthropic cycle found for itself.

### 0.7 `ConfigRepository`'s JSON field-name constants for `base_url`/`default_model`/
`temperature`/`max_output_tokens` already exist and are reused verbatim, unchanged

`src/config/src/config_repository.cpp` already defines `kBaseUrlKey`, `kDefaultModelKey`,
`kTemperatureKey`, `kMaxOutputTokensKey` as anonymous-namespace `constexpr auto` literals (added
during the Anthropic cycle for its own `max_output_tokens` field). `loadGoogleConfig()`/
`saveGoogleConfig()` reuse all four unchanged — only a new `kGoogleKey = "google"` constant is
added (§3.4).

---

## 1. Components

| Component | File(s) | Role |
|---|---|---|
| `holonight_providers::GoogleProvider` | `src/providers/include/holonight_providers/google_provider.h` (new), `src/providers/src/google_provider.cpp` (new) | Concrete adapter mirroring `AnthropicProvider`'s shape: `x-goog-api-key` header, `systemInstruction` hoisting, no body-level `model` field (URL-only), `streamGenerateContent?alt=sse` SSE parsing, model discovery + denylist + `supportedGenerationMethods` double filter, safety/recitation block mapping. |
| `holonight_config::GoogleProviderConfig` | `src/config/include/holonight_config/provider_config.h` (changed — struct added alongside the other three) | Non-secret config: `base_url`, `default_model`, `temperature` (0.0–2.0), `max_output_tokens`. |
| `holonight_config::ConfigRepository` (changed) | `src/config/include/holonight_config/config_repository.h`, `src/config/src/config_repository.cpp` | Gains `loadGoogleConfig()`/`saveGoogleConfig()`, reusing the existing field-name constants (§0.7) plus one new `kGoogleKey`. |
| `holonight_application::ChatController` (changed) | `src/application/include/holonight_application/chat_controller.h`, `.cpp` | Constructor now takes **four** concrete providers; `hasModelsFor()`/`dispatchSendChat()` gain a fourth `provider_id == "google"` branch (§5.1). |
| `holonight_application::ChatViewModel` (changed) | `src/application/include/holonight_application/chat_view_model.h`, `.cpp` | Owns a fourth provider instance; `syncAvailableModels()` unions four providers' model lists; gains `googleProviderForSettings()` and `syncAvailableGoogleModels()`; `create()` factory gains a fourth `QtNetworkHttpClient` + `GoogleProvider`, seeded from `loadGoogleConfig()`. |
| `holonight_application::GoogleProviderSettingsController` | `src/application/include/holonight_application/google_provider_settings_controller.h` (new), `.cpp` (new) | **Fourth subclass of `ProviderSettingsControllerBase`** (§0.2) — not a from-scratch reimplementation. Retains only `temperature` (0.0–2.0 validated in `save()`), `maxOutputTokens`, `googleConnectionStatus`, and its own `load()`/`save()`/`cancel()`/`resetToDefaults()`. |
| `qml/workspace/GoogleSettingsPanel.qml` | new — **replaces the current placeholder routing** | Structurally copies `AnthropicSettingsPanel.qml` (temperature slider/spinbox range widened to 0.0–2.0, max-output-tokens `SpinBox` block kept as-is, every controller reference retargeted). |
| `qml/workspace/ProvidersPage.qml` (changed) | existing | `Loader` gains `case "Google": return googlePanel` — currently falls to `unsupportedPanel`. |
| `qml/workspace/ProviderListDelegate.qml` (changed) | existing | Status dot gains an `isGoogle` branch reading `GoogleProviderSettingsController.googleConnectionStatus` — was hardcoded gray for the Google row until now. |
| `tests/providers/test_google_provider.cpp` | new (source file added to the existing single `test_holonight_ai` binary — see Corrections above and §8) | Request construction (role mapping, `systemInstruction` hoisting, `generationConfig`, URL path/action-suffix), SSE parsing/fragmentation, event routing (including safety/recitation), model filtering (denylist + `supportedGenerationMethods`), cancellation, error mapping. |
| `tests/application/test_chat_controller.cpp` (changed) | existing | Fixture gains a `GoogleProvider`; new tests cover `provider_id == "google"` routing. |
| `tests/application/test_google_provider_settings_controller.cpp` | new | Mirrors `test_anthropic_provider_settings_controller.cpp`'s structure exactly (both are subclasses of the same base — the fixture shape is nearly identical), plus the wider [0.0, 2.0] temperature validation boundary. |
| `tests/config/test_config_repository.cpp` (changed) | existing | New round-trip tests for `GoogleProviderConfig`; a four-provider read-merge-write test (saving Google doesn't clobber Ollama/OpenAI/Anthropic sections and vice versa). |

### 1.1 Class relationship

```
                              ┌─────────────────────────────────────────────────────┐
                              │                    ChatViewModel                      │ (QML_SINGLETON, existing)
                              │  - ollama_provider_, openai_provider_, anthropic_provider_│
                              │  - google_provider_: shared_ptr<GoogleProvider>          │ (new)
                              │  - chat_controller_: unique_ptr<ChatController>          │
                              │  + googleProviderForSettings() const                     │ (new)
                              │  + syncAvailableModels(preferred) — unions all FOUR      │
                              │  + syncAvailableGoogleModels(preferred)                  │ (new)
                              └───────────────┬───────────────────────────────────────────┘
                                              │ owns
                              ┌───────────────▼───────────────────────────────────────────┐
                              │                     ChatController                          │ (changed)
                              │  - ollama_/openai_/anthropic_/google_provider_               │
                              │  routes sendChat()/availableModels() on ModelId::provider_id  │
                              │  — plain if/else chain, four branches, still no vtable (§5.1) │
                              └─────────────────────────────────────────────────────────────┘

  ┌──────────────────────────────┐
  │  ProviderSettingsControllerBase │  (existing — 0.2's real, current shape)
  │  Q_PROPERTY baseUrl, defaultModel, authToken, hasStoredToken, credentialStoreAvailable,
  │             credentialOperationInProgress, availableModelNames, modelRefreshInProgress/Error,
  │             testConnectionInProgress/Status/Message, saveNotice
  │  Q_INVOKABLE refreshModels(), testConnection()
  │  protected: beginLoad(), saveCredential(), clearTransientState(), connectionStatus()
  └───────────────┬────────────────┬────────────────┬────────────────┬─────────────────────┘
                  │ inherits       │ inherits        │ inherits       │ inherits (new)
  ┌───────────────▼──────┐ ┌───────▼──────────────┐ ┌▼──────────────────────┐ ┌▼──────────────────────┐
  │ ProviderSettingsController│ │OpenAIProviderSettings-│ │AnthropicProviderSettings-│ │GoogleProviderSettings-  │
  │ (Ollama)                  │ │Controller             │ │Controller                │ │Controller (new)         │
  │ + contextWindow            │ │ + temperature (0–2)   │ │ + temperature (0–1)      │ │ + temperature (0–2)     │
  │ + temperature (0–0.7 dflt) │ │                        │ │ + maxOutputTokens         │ │ + maxOutputTokens        │
  │ + ollamaConnectionStatus   │ │ + openAiConnectionStatus│ │ + anthropicConnectionStatus│ │ + googleConnectionStatus │
  │ + credentialStore() accessor│ (reused by all three siblings below via chained singleton resolution, §5.3)│
  └────────────────────────────┘ └────────────────────────┘ └───────────────────────────┘ └──────────────────────────┘
```

---

## 2. Data Flow

### 2.1 Chat-send path (REQ-F-001–008, F-013–019, F-024, F-025)

1. User selects a model in the workspace combo box →
   `ChatViewModel.selectedModelId = {provider_id: "google", model_name: "gemini-2.0-flash"}`
   (unchanged QML — the combo box is already provider-agnostic).
2. `ChatViewModel::send(text)` → `chat_controller_->send(*conversation_, selected_model_id_, text,
   onEventCallback)` (unchanged call site).
3. `ChatController::send()` validates via `hasModelsFor(model.provider_id)`, then `startStream()`
   calls `dispatchSendChat()`:
   ```cpp
   if (model.provider_id == QStringLiteral("openai"))    return openai_provider_->sendChat(...);
   if (model.provider_id == QStringLiteral("anthropic")) return anthropic_provider_->sendChat(...);
   if (model.provider_id == QStringLiteral("google"))    return google_provider_->sendChat(...);
   return ollama_provider_->sendChat(...);
   ```
4. `GoogleProvider::sendChat()` partitions `history` into hoisted `systemInstruction` text
   (REQ-F-003/004) and a `contents` array of `user`/`model`-role entries (REQ-F-002), builds a
   `{model}:streamGenerateContent?alt=sse` POST body (§3.2, no `model` field in the body itself —
   §5.1), issues it via `http_client_->sendStreaming()` with `authHeaders()` (`x-goog-api-key` only
   when a credential is present, §5.4), and returns the `HttpRequestHandlePtr` — identical shape to
   every other provider's `sendChat()`, so `ChatController::startStream()`'s handle bookkeeping is
   untouched.
5. Raw bytes arrive via `on_data`; the SSE parser (§4) accumulates them into `StreamContext::buffer`
   (the same struct/algorithm `AnthropicProvider`/`OpenAIProvider` already use), frames complete
   `data: {...}` blocks on blank lines, parses each as a `GenerateContentResponse` JSON object, and
   routes on `candidates[0]`'s content/`finishReason` (§4.3) into the **same**
   `holonight_domain::StreamEvent` variant every provider uses (`ContentDelta`/`Completed`/
   `Error`/`Cancelled`) via the **same** `on_event` callback signature.
6. From here the flow is unchanged and provider-agnostic — `ChatController::handleStreamEvent()`,
   `ChatViewModel::onStreamEvent()`, `MessageListModel`, persistence, and `canSend`/`isStreaming`
   recomputation touch only `holonight_domain::StreamEvent`, never a provider type. **None of this
   code needs to change.**
7. Stop button → `ChatViewModel::stop()` → `ChatController::stop(conversation_id)` → looks up
   `in_flight_[key].handle` (opaque, already provider-agnostic) → `handle->cancel()` (REQ-F-025). No
   routing needed — the handle was already resolved to `GoogleProvider`'s stream at step 4.

### 2.2 Model-discovery / settings path (REQ-F-008–011, REQ-F-033) — shared/probe flow via the base class

Mirrors every existing controller's flow through `ProviderSettingsControllerBase` (§0.2) — the
concrete `GoogleProviderSettingsController` supplies only provider-specific closures, not its own
copy of the async workflow:

1. `GoogleProviderSettingsController::create()` resolves the already-constructed
   `ProviderSettingsController` (Ollama) QML singleton first — purely to reuse its
   `credentialStore()` accessor (§5.3, unchanged reasoning from the Anthropic cycle, now a fourth
   confirmation of the same pattern) — and builds a **probe** `GoogleProvider` over its own fresh
   `QtNetworkHttpClient`, entirely separate from `chatViewModel->googleProviderForSettings()` (the
   shared instance `ChatController` uses for real chat traffic).
2. The constructor passes a `ProviderOperations` struct of five closures over the probe/shared
   `GoogleProvider` pair (§3.7) into `ProviderSettingsControllerBase`'s constructor, which itself
   wires the `CredentialStore` signal connections and calls nothing yet.
3. `load()` (provider-specific — reads `GoogleProviderConfig`, seeds `temperature_`/
   `max_output_tokens_`, calls the base's `beginLoad(preferred_model)`) triggers
   `credential_store_->retrieve("google")` — asynchronous, so the **initial** model fetch happens
   inside the base class's `onCredentialRetrieved()` → `refreshModels()` chain, not synchronously
   here (same async-startup race the OpenAI cycle originally solved and every subsequent provider
   cycle has inherited unchanged, §5.6).
4. `refreshModels()`/`testConnection()` (both entirely base-class code, not overridden) call
   `operations_.configure_probe(base_url_, auth_token_)` → `probe_provider_->setBaseUrl(...)` +
   `probe_provider_->setAuthKey(...)`, then `operations_.refresh_probe(...)` →
   `probe_provider_->refresh(on_success, on_error)`, which issues `GET /v1beta/models` with
   `x-goog-api-key` (REQ-F-008) and, on success, replaces the probe's cached, denylist-**and**-
   `supportedGenerationMethods`-filtered model list (§6).
5. Editing the panel — including repeated Refresh/Test Connection clicks — **never** touches
   `provider_` (the shared instance passed at construction), so live chat traffic is unaffected
   until Save (unchanged rationale, now confirmed for a fourth provider).
6. `save()` (provider-specific, REQ-F-028): validates `temperature` ∈ [0.0, 2.0] (REQ-F-005 — the
   *widest* range of the four providers; Ollama has no explicit range check, OpenAI's is [0.0,
   2.0] too, Anthropic's is [0.0, 1.0]), calls `config_repository_.saveGoogleConfig(config)`,
   applies the new `base_url`/`temperature`/`max_output_tokens` directly to `provider_` (the shared
   instance, bypassing `operations_.configure_probe` since that closure is probe-only), and calls
   the base's `saveCredential(preferred)`, which stores/removes via `credential_store_` and — on
   completion — calls `operations_.refresh_shared(...)`, which is
   `chatViewModel->syncAvailableGoogleModels(preferred)` in production.
7. Token save/remove: entirely base-class code (`saveCredential()`/`onCredentialStored()`/
   `onCredentialRemoved()`), using `credential_store_->store("google", token)` /
   `remove("google")` (REQ-F-029/031/038) — `provider_id_ = "google"` is the only per-call
   difference from the other three subclasses, supplied once at construction.

### 2.3 Config load fallback table (REQ-F-034)

Identical structure to `loadAnthropicConfig()`'s existing table, keyed at `"providers"."google"`:

| Condition | Result |
|---|---|
| File missing / empty / malformed JSON / not an object | `GoogleProviderConfig{}` (defaults: `base_url="https://generativelanguage.googleapis.com"`, `default_model=""`, `temperature=1.0`, `max_output_tokens=1024`) |
| `"providers"` missing or `"providers"."google"` missing/not-object | `GoogleProviderConfig{}` |
| Present, one or more fields missing/wrong-typed | Present fields load; missing/wrong-typed fields individually fall back to that field's default (`QJsonValue::toString(default)`/`toDouble(default)`/`toInt(default)`) |

### 2.4 Persisted JSON shape (REQ-F-037)

```jsonc
{
  "providers": {
    "ollama": { "base_url": "...", "default_model": "...", "context_window": 4096, "temperature": 0.7 },
    "openai": { "base_url": "...", "default_model": "...", "temperature": 1.0 },
    "anthropic": { "base_url": "...", "default_model": "...", "temperature": 1.0, "max_output_tokens": 2048 },
    "google": {
      "base_url": "https://generativelanguage.googleapis.com",
      "default_model": "gemini-2.0-flash",
      "temperature": 1.0,
      "max_output_tokens": 1024
    }
  }
}
```

`saveGoogleConfig()` reads the whole `root` via `readRootObject()`, mutates only
`root["providers"]["google"]`, and writes back via `writeRootObject()` — the `"ollama"`,
`"openai"`, and `"anthropic"` keys are read, kept, and rewritten byte-identical (the read-merge-
write mechanism has now been exercised by four consecutive provider cycles with zero further
change needed to it — only a fourth caller).

---

## 3. Interfaces / APIs

### 3.1 `src/providers/include/holonight_providers/google_provider.h` (new)

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

// Adapter for Google's Gemini Developer API (POST /v1beta/models/{model}:streamGenerateContent
// ?alt=sse — see DESIGN.md's Corrections to SPEC.md for why the action suffix and alt=sse query
// parameter are required, not the literal :generateContent SPEC.md names). Deliberately a plain
// concrete class, structurally parallel to OllamaProvider/OpenAIProvider/AnthropicProvider but
// sharing no base class with any of them (REQ-F-040/SPEC.md §F.9 — no virtual base, no
// dynamic-dispatch interface for this cycle, restated for a fourth time; see DESIGN.md §5.1).
class GoogleProvider {
 public:
  // Model discovery is explicit: callers invoke refresh() once ready to receive the async result.
  explicit GoogleProvider(std::shared_ptr<HttpClient> http_client,
                          QString base_url = QStringLiteral("https://generativelanguage.googleapis.com"));

  [[nodiscard]] const std::vector<holonight_domain::ModelId>& availableModels() const;

  // Re-fetches GET /v1beta/models (denylist + supportedGenerationMethods filtered, REQ-F-009/010)
  // and replaces the cached list. Model IDs are stored WITHOUT the "models/" resource-name prefix
  // Gemini's API returns (§5.2) — availableModels() always yields bare IDs like "gemini-2.0-flash".
  void refresh(const std::function<void()>& on_complete = {});
  void refresh(const std::function<void()>& on_success, const std::function<void(const QString&)>& on_error);

  // Reconfigures the base URL used by the NEXT refresh()/sendChat() call (REQ-F-001).
  void setBaseUrl(QString base_url);
  [[nodiscard]] const QString& baseUrl() const;

  // x-goog-api-key support (REQ-F-001/038). Named setAuthKey() per REQ-F-040's explicit method-
  // surface list, matching AnthropicProvider's naming (a raw API-key header, not a Bearer token).
  // Empty (default) ⇒ no x-goog-api-key header is sent at all — REQ-F-039 explicitly mirrors
  // OllamaProvider's "omit the header entirely" behavior here, NOT Anthropic's "always send one
  // unconditional header" shape (Google has no unconditional second header like anthropic-version).
  // Never logged, never exposed via a getter — write-only by design (REQ-NF-006).
  void setAuthKey(QString auth_key);

  // Applied to every subsequent sendChat()'s generationConfig.temperature field. Range 0.0–2.0
  // (REQ-F-005) — the widest of the four providers. NOT validated here (the provider trusts its
  // caller; range enforcement is the settings controller's job, REQ-C-003 forbids provider-level
  // model-specific validation).
  void setTemperature(double temperature);

  // Applied to every subsequent sendChat()'s generationConfig.maxOutputTokens field (REQ-F-006).
  // Default 1024 matches holonight_config::GoogleProviderConfig{}'s own default.
  void setMaxOutputTokens(int max_output_tokens);

  // Sends `history` to {model}:streamGenerateContent?alt=sse and streams the response as
  // StreamEvents (SSE framing, §4). System-role messages in `history` are hoisted to the top-level
  // "systemInstruction" field, not sent inline (REQ-F-003/004) — the only behavioral divergence
  // from Ollama/OpenAI's sendChat() at the call-site level; the signature itself is identical, and
  // `model.model_name` goes into the URL path, never into the JSON body (§5.1).
  HttpRequestHandlePtr sendChat(const holonight_domain::ModelId& model,
                                const std::vector<holonight_domain::Message>& history,
                                const std::function<void(const holonight_domain::StreamEvent&)>& on_event,
                                std::chrono::milliseconds idle_timeout = std::chrono::seconds{30});

 private:
  void fetchModelList(const std::function<void()>& on_success, const std::function<void(const QString&)>& on_error);

  // Single conditional header (empty map ⇒ no auth header at all, REQ-F-039) — same contract as
  // OllamaProvider's/OpenAIProvider's authHeaders(), unlike AnthropicProvider's requestHeaders()
  // (which always sends one unconditional header). Named authHeaders() to match that contract
  // directly, not requestHeaders() (§5.4).
  [[nodiscard]] QHash<QString, QString> authHeaders() const;

  std::shared_ptr<HttpClient> http_client_;
  QString base_url_;
  std::vector<holonight_domain::ModelId> available_models_;
  QString auth_key_;
  double temperature_ = 1.0;
  int max_output_tokens_ = 1024;
};

}  // namespace holonight_providers
```

### 3.2 Request body construction (REQ-F-001–007)

Full example request, given a conversation with two System messages, a User message, and an
Assistant reply, then a final User message (`temperature = 0.8`, `max_output_tokens = 2048`, model
`gemini-2.0-flash`):

```
POST https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:streamGenerateContent?alt=sse
x-goog-api-key: <key>
Content-Type: application/json
```
```json
{
  "contents": [
    { "role": "user", "parts": [{ "text": "What is a Wayland layer-shell surface?" }] },
    { "role": "model", "parts": [{ "text": "A layer-shell surface is a compositor-managed overlay..." }] },
    { "role": "user", "parts": [{ "text": "Does holonight-chat use one?" }] }
  ],
  "systemInstruction": {
    "parts": [{ "text": "You are a terse, technical assistant.\n\nAlways answer in one paragraph." }]
  },
  "generationConfig": {
    "temperature": 0.8,
    "maxOutputTokens": 2048
  }
}
```

If there are zero System messages, the `"systemInstruction"` key is omitted entirely (REQ-F-004) —
not sent as `"systemInstruction": {"parts": [{"text": ""}]}`. There is deliberately **no top-level
`"model"` field** — unlike Ollama/OpenAI/Anthropic, Gemini's model selection is entirely carried by
the URL path segment (`/models/{model}:streamGenerateContent`), so `ModelId::model_name` is
consumed only when building `request.url`, never written into the JSON body (§5.1). There is also
no top-level `"stream": true` field (unlike OpenAI/Anthropic) — streaming is selected by the
`:streamGenerateContent` action suffix itself, not a body flag.

```cpp
QJsonArray contents;
QStringList systemParts;
for (const Message& message : history) {
  if (message.role() == MessageRole::System) {
    if (!message.text().isEmpty()) {
      systemParts << message.text();
    }
    continue;  // REQ-F-003: System messages never appear in the `contents` array.
  }
  QJsonObject entry;
  entry[QStringLiteral("role")] = roleToGoogleString(message.role());  // "user" | "model"
  entry[QStringLiteral("parts")] = QJsonArray{QJsonObject{{QStringLiteral("text"), message.text()}}};
  contents.append(entry);
}

QJsonObject generationConfig;
generationConfig[QStringLiteral("temperature")] = temperature_;         // REQ-F-005
generationConfig[QStringLiteral("maxOutputTokens")] = max_output_tokens_;  // REQ-F-006

QJsonObject body;
body[QStringLiteral("contents")] = contents;
if (!systemParts.isEmpty()) {
  QJsonObject systemInstruction;
  systemInstruction[QStringLiteral("parts")] =
      QJsonArray{QJsonObject{{QStringLiteral("text"), systemParts.join(QStringLiteral("\n\n"))}}};  // REQ-F-003
  body[QStringLiteral("systemInstruction")] = systemInstruction;
}
body[QStringLiteral("generationConfig")] = generationConfig;
// Deliberately absent: safetySettings, tools, toolConfig, candidateCount, topK, topP,
// stopSequences (REQ-C-001/C-004/C-008/C-009).

const HttpRequest request{
    .method = HttpMethod::Post,
    .url = base_url_ + QStringLiteral("/v1beta/models/") + model.model_name +
           QStringLiteral(":streamGenerateContent?alt=sse"),  // Corrections to SPEC.md #1
    .body = QJsonDocument(body).toJson(QJsonDocument::Compact),
    .content_type = QStringLiteral("application/json"),
    .headers = authHeaders()};
```

`roleToGoogleString()`:

```cpp
QString roleToGoogleString(MessageRole role) {
  switch (role) {
    case MessageRole::Assistant:
      return QStringLiteral("model");  // NOT "assistant" — REQ-F-002
    case MessageRole::User:
    case MessageRole::System:  // unreachable: System is hoisted away before this is called
      return QStringLiteral("user");
  }
  return QStringLiteral("user");
}
```

`authHeaders()`:

```cpp
QHash<QString, QString> GoogleProvider::authHeaders() const {
  QHash<QString, QString> headers;
  if (!auth_key_.isEmpty()) {
    headers.insert(QStringLiteral("x-goog-api-key"), auth_key_);  // REQ-F-001/038/039
  }
  return headers;  // empty map ⇒ no auth header at all, mirroring OllamaProvider's contract
}
```

Example SSE response sequence for the request above (`alt=sse` framing — one complete
`GenerateContentResponse` object per block, not incremental token-level deltas the way OpenAI's
`response.output_text.delta` events are):

```
data: {"candidates":[{"content":{"role":"model","parts":[{"text":"Yes"}]},"index":0}]}

data: {"candidates":[{"content":{"role":"model","parts":[{"text":", the chat process owns"}]},"index":0}]}

data: {"candidates":[{"content":{"role":"model","parts":[{"text":" a wlr-layer-shell surface."}]},"finishReason":"STOP","index":0}],"usageMetadata":{"promptTokenCount":48,"candidatesTokenCount":24,"totalTokenCount":72}}

```

`GoogleProvider` produces exactly three `ContentDelta` events ("Yes", ", the chat process owns",
" a wlr-layer-shell surface."), then one `Completed` event triggered by the third block's
`finishReason: "STOP"` (both the trailing text delta and the `Completed` event fire from the same
block, in that order — §4.3).

Safety-block example (REQ-F-016/024):

```
data: {"candidates":[{"finishReason":"SAFETY","index":0,"safetyRatings":[{"category":"HARM_CATEGORY_DANGEROUS_CONTENT","probability":"HIGH"}]}]}

```

emits a single `Error{message: "Response blocked by Google's safety filters."}` — no `ContentDelta`
before it, since `content`/`parts` is absent on this block.

Error response example (401, non-streaming JSON body — arrives via the same recovery path as §4.5):

```json
{ "error": { "code": 401, "message": "API key not valid. Please pass a valid API key.", "status": "UNAUTHENTICATED" } }
```

### 3.3 `src/config/include/holonight_config/provider_config.h` (changed — struct added)

```cpp
// Non-secret Google configuration persisted to config.json (REQ-F-034/037). No auth key field —
// Secret Service's exclusive responsibility (REQ-F-036/REQ-NF-006). max_output_tokens is present
// (REQ-F-006), same shape as AnthropicProviderConfig; temperature's *range* differs (0.0–2.0, the
// widest of the four providers) but the field type/default match OpenAIProviderConfig's own 1.0
// default.
struct GoogleProviderConfig {
  QString base_url = QStringLiteral("https://generativelanguage.googleapis.com");
  QString default_model;  // empty ⇒ "no default model saved"
  double temperature = 1.0;
  int max_output_tokens = 1024;

  friend bool operator==(const GoogleProviderConfig&, const GoogleProviderConfig&) = default;
};
```

### 3.4 `src/config/include/holonight_config/config_repository.h` / `.cpp` (changed)

```cpp
class ConfigRepository {
 public:
  // ... existing Ollama/OpenAI/Anthropic methods unchanged ...

  // REQ-F-035: never throws, same fallback semantics as the other three load*Config() methods.
  [[nodiscard]] GoogleProviderConfig loadGoogleConfig() const;

  // REQ-F-035: same std::expected<void, QString> convention as the other three save methods.
  [[nodiscard]] std::expected<void, QString> saveGoogleConfig(const GoogleProviderConfig& config) const;

  // readRootObject()/writeRootObject() are reused unchanged — no new private helpers needed.
};
```

`config_repository.cpp` gains exactly **one** new key literal (`kGoogleKey = "google"`) — every
field-name constant it needs (`kBaseUrlKey`, `kDefaultModelKey`, `kTemperatureKey`,
`kMaxOutputTokensKey`) already exists (§0.7). The two new methods follow `loadAnthropicConfig()`/
`saveAnthropicConfig()` byte-for-byte, substituting `GoogleProviderConfig` and `kGoogleKey`:

```cpp
GoogleProviderConfig ConfigRepository::loadGoogleConfig() const {
  GoogleProviderConfig defaults{};
  const QJsonValue providersValue = readRootObject().value(QLatin1String(kProvidersKey));
  if (!providersValue.isObject()) return defaults;
  const QJsonValue googleValue = providersValue.toObject().value(QLatin1String(kGoogleKey));
  if (!googleValue.isObject()) return defaults;

  const QJsonObject google = googleValue.toObject();
  GoogleProviderConfig config;
  config.base_url = google.value(QLatin1String(kBaseUrlKey)).toString(defaults.base_url);
  config.default_model = google.value(QLatin1String(kDefaultModelKey)).toString(defaults.default_model);
  config.temperature = google.value(QLatin1String(kTemperatureKey)).toDouble(defaults.temperature);
  config.max_output_tokens = google.value(QLatin1String(kMaxOutputTokensKey)).toInt(defaults.max_output_tokens);
  return config;
}

std::expected<void, QString> ConfigRepository::saveGoogleConfig(const GoogleProviderConfig& config) const {
  QJsonObject root = readRootObject();
  QJsonObject providers = root.value(QLatin1String(kProvidersKey)).toObject();

  QJsonObject google;
  google[QLatin1String(kBaseUrlKey)] = config.base_url;
  google[QLatin1String(kDefaultModelKey)] = config.default_model;
  google[QLatin1String(kTemperatureKey)] = config.temperature;
  google[QLatin1String(kMaxOutputTokensKey)] = config.max_output_tokens;

  providers[QLatin1String(kGoogleKey)] = google;
  root[QLatin1String(kProvidersKey)] = providers;
  return writeRootObject(root);
}
```

### 3.5 `src/application/include/holonight_application/chat_controller.h` / `.cpp` (changed)

```diff
 class ChatController {
  public:
-  explicit ChatController(std::shared_ptr<holonight_providers::OllamaProvider> ollama_provider,
-                          std::shared_ptr<holonight_providers::OpenAIProvider> openai_provider,
-                          std::shared_ptr<holonight_providers::AnthropicProvider> anthropic_provider);
+  // Takes all FOUR concrete providers explicitly — no virtual base, no registry (§5.1). Breaking
+  // change: every existing call site (ChatViewModel's constructor, test fixtures) must add the
+  // fourth argument in the same change; there is no sensible default for a shared_ptr<GoogleProvider>.
+  explicit ChatController(std::shared_ptr<holonight_providers::OllamaProvider> ollama_provider,
+                          std::shared_ptr<holonight_providers::OpenAIProvider> openai_provider,
+                          std::shared_ptr<holonight_providers::AnthropicProvider> anthropic_provider,
+                          std::shared_ptr<holonight_providers::GoogleProvider> google_provider);

   // send()/regenerate()/stop()/isStreaming() — signatures unchanged.

  private:
   [[nodiscard]] bool hasModelsFor(const QString& provider_id) const;
   holonight_providers::HttpRequestHandlePtr dispatchSendChat(...);  // signature unchanged

   std::shared_ptr<holonight_providers::OllamaProvider> ollama_provider_;
   std::shared_ptr<holonight_providers::OpenAIProvider> openai_provider_;
   std::shared_ptr<holonight_providers::AnthropicProvider> anthropic_provider_;
+  std::shared_ptr<holonight_providers::GoogleProvider> google_provider_;
   QHash<QString, InFlightStream> in_flight_;
 };
```

```cpp
bool ChatController::hasModelsFor(const QString& provider_id) const {
  if (provider_id == QStringLiteral("openai")) return !openai_provider_->availableModels().empty();
  if (provider_id == QStringLiteral("anthropic")) return !anthropic_provider_->availableModels().empty();
  if (provider_id == QStringLiteral("google")) return !google_provider_->availableModels().empty();
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
  if (model.provider_id == QStringLiteral("google")) {
    return google_provider_->sendChat(model, history, on_event);
  }
  return ollama_provider_->sendChat(model, history, on_event);
}
```

### 3.6 `src/application/include/holonight_application/chat_view_model.h` / `.cpp` (changed)

```diff
   explicit ChatViewModel(std::shared_ptr<holonight_providers::OllamaProvider> ollama_provider,
                          std::shared_ptr<holonight_providers::OpenAIProvider> openai_provider,
                          std::shared_ptr<holonight_providers::AnthropicProvider> anthropic_provider,
+                         std::shared_ptr<holonight_providers::GoogleProvider> google_provider,
                          std::unique_ptr<holonight_persistence::ConversationRepository> repository,
                          QString initial_default_model_id = {}, QObject* parent = nullptr);

   [[nodiscard]] std::shared_ptr<holonight_providers::AnthropicProvider> anthropicProviderForSettings() const;
+  [[nodiscard]] std::shared_ptr<holonight_providers::GoogleProvider> googleProviderForSettings() const;

   // syncAvailableModels() now unions FOUR providers' availableModels() lists, not three.
   void syncAvailableModels(const std::optional<holonight_domain::ModelId>& preferredModelId = std::nullopt);
   void syncAvailableAnthropicModels(const std::optional<holonight_domain::ModelId>& preferredModelId = std::nullopt);
+  // Mirrors syncAvailableAnthropicModels(): marks google_models_loaded_ before re-unioning.
+  void syncAvailableGoogleModels(const std::optional<holonight_domain::ModelId>& preferredModelId = std::nullopt);

  private:
   std::shared_ptr<holonight_providers::OllamaProvider> ollama_provider_;
   std::shared_ptr<holonight_providers::OpenAIProvider> openai_provider_;
   std::shared_ptr<holonight_providers::AnthropicProvider> anthropic_provider_;
+  std::shared_ptr<holonight_providers::GoogleProvider> google_provider_;
   ...
   bool anthropic_models_loaded_ = false;
+  bool google_models_loaded_ = false;
```

`create()`'s factory (`chat_view_model.cpp:43-70`, read directly — the real, current form) gains a
fourth block, inserted after the Anthropic block, following the exact same shape:

```cpp
const holonight_config::GoogleProviderConfig googleConfig = configRepository.loadGoogleConfig();
...
auto googleHttpClient = std::make_shared<QtNetworkHttpClient>();
auto googleProvider = std::make_shared<GoogleProvider>(std::move(googleHttpClient), googleConfig.base_url);
googleProvider->setTemperature(googleConfig.temperature);
googleProvider->setMaxOutputTokens(googleConfig.max_output_tokens);
...
return new ChatViewModel(std::move(ollamaProvider), std::move(openAiProvider), std::move(anthropicProvider),
                         std::move(googleProvider), std::move(repository), ollamaConfig.default_model);
```

`ChatViewModel`'s own model-discovery-at-startup behavior for Google is identical to Anthropic's:
**no `google_provider_->refresh()` call in `ChatViewModel`'s constructor** —
`GoogleProviderSettingsController`'s base-class-driven `onCredentialRetrieved()` is what fires the
shared provider's first `refresh()` (§2.2 step 3, §5.6) — avoiding the same async-credential-race
every non-Ollama provider cycle has needed to avoid.

### 3.7 `src/application/include/holonight_application/google_provider_settings_controller.h` (new)

This is the real, current shape — a thin subclass of `ProviderSettingsControllerBase` (§0.2), not
the larger from-scratch class the Anthropic cycle's own (now-superseded) DESIGN.md sketched for
itself:

```cpp
#pragma once

#include "holonight_application/provider_settings_controller_base.h"
#include "holonight_providers/google_provider.h"

#include <QtQml/qqmlregistration.h>

#include <holonight_config/config_repository.h>
#include <memory>

class QQmlEngine;
class QJSEngine;

namespace holonight_application {

// QML-facing Google settings bridge. ProviderSettingsControllerBase owns the workflow shared with
// the Ollama, OpenAI, and Anthropic controllers; this class retains only Google-specific
// persistence, validation, output-token state, and its stable QML singleton API.
class GoogleProviderSettingsController : public ProviderSettingsControllerBase {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

  Q_PROPERTY(double temperature READ temperature WRITE setTemperature NOTIFY temperatureChanged)
  Q_PROPERTY(int maxOutputTokens READ maxOutputTokens WRITE setMaxOutputTokens NOTIFY maxOutputTokensChanged)
  Q_PROPERTY(QString googleConnectionStatus READ googleConnectionStatus NOTIFY googleConnectionStatusChanged)

 public:
  static GoogleProviderSettingsController* create(QQmlEngine* qml_engine, QJSEngine* js_engine);

  explicit GoogleProviderSettingsController(std::shared_ptr<holonight_providers::GoogleProvider> shared_provider,
                                            std::shared_ptr<holonight_providers::GoogleProvider> probe_provider,
                                            holonight_credentials::CredentialStore* credential_store,
                                            holonight_config::ConfigRepository config_repository,
                                            ModelSyncCallback model_sync_callback, QObject* parent = nullptr);

  [[nodiscard]] double temperature() const;
  void setTemperature(double value);
  [[nodiscard]] int maxOutputTokens() const;
  void setMaxOutputTokens(int value);
  [[nodiscard]] QString googleConnectionStatus() const;

  Q_INVOKABLE void load();  // called once at construction
  Q_INVOKABLE void save();
  Q_INVOKABLE void cancel();
  Q_INVOKABLE void resetToDefaults();

 Q_SIGNALS:
  void temperatureChanged();
  void maxOutputTokensChanged();
  void googleConnectionStatusChanged();

 private:
  std::shared_ptr<holonight_providers::GoogleProvider> provider_;  // shared — Save only
  holonight_config::ConfigRepository config_repository_;

  holonight_config::GoogleProviderConfig last_saved_config_;  // Cancel baseline
  double temperature_ = 1.0;
  int max_output_tokens_ = 1024;
};

}  // namespace holonight_application
```

`create()` — same chained-singleton-resolution shape as `AnthropicProviderSettingsController::
create()`, reusing `ProviderSettingsController`'s `CredentialStore` directly (not chained through
`OpenAIProviderSettingsController` or `AnthropicProviderSettingsController` — all three later
controllers independently chain off the one already-accessor-bearing base singleton, §5.3):

```cpp
GoogleProviderSettingsController* GoogleProviderSettingsController::create(QQmlEngine* qml_engine,
                                                                           QJSEngine* js_engine) {
  Q_UNUSED(js_engine)
  auto* chatViewModel = qml_engine->singletonInstance<ChatViewModel*>("HolonightChat", "ChatViewModel");
  Q_ASSERT(chatViewModel != nullptr);

  auto* providerSettingsController =
      qml_engine->singletonInstance<ProviderSettingsController*>("HolonightChat", "ProviderSettingsController");
  Q_ASSERT(providerSettingsController != nullptr);

  auto probeHttpClient = std::make_shared<holonight_providers::QtNetworkHttpClient>();
  auto probeProvider = std::make_shared<holonight_providers::GoogleProvider>(std::move(probeHttpClient));
  holonight_config::ConfigRepository configRepository(holonight_config::resolveConfigFilePath());

  return new GoogleProviderSettingsController(
      chatViewModel->googleProviderForSettings(), std::move(probeProvider),
      providerSettingsController->credentialStore(), std::move(configRepository),
      [chatViewModel](const std::optional<holonight_domain::ModelId>& preferred) {
        chatViewModel->syncAvailableGoogleModels(preferred);
      });
}
```

Constructor — passes the five-closure `ProviderOperations` struct into
`ProviderSettingsControllerBase`, following `AnthropicProviderSettingsController`'s exact current
shape (`setAuthKey()`, not `setAuthToken()`, on both the probe and shared `GoogleProvider`
instances — §5.4):

```cpp
GoogleProviderSettingsController::GoogleProviderSettingsController(
    std::shared_ptr<GoogleProvider> shared_provider, std::shared_ptr<GoogleProvider> probe_provider,
    CredentialStore* credential_store, holonight_config::ConfigRepository config_repository,
    ModelSyncCallback model_sync_callback, QObject* parent)
    : ProviderSettingsControllerBase(
          QStringLiteral("google"), credential_store,
          ProviderOperations{
              .configure_probe =
                  [probe_provider](const QString& base_url, const QString& credential) {
                    probe_provider->setBaseUrl(base_url);
                    probe_provider->setAuthKey(credential);
                  },
              .refresh_probe = [probe_provider](const auto& on_success,
                                                const auto& on_error) { probe_provider->refresh(on_success, on_error); },
              .probe_models = [probe_provider]() -> const std::vector<ModelId>& {
                return probe_provider->availableModels();
              },
              .set_shared_credential =
                  [shared_provider](const QString& credential) { shared_provider->setAuthKey(credential); },
              .refresh_shared = [shared_provider](const auto& on_success,
                                                  const auto& on_error) { shared_provider->refresh(on_success, on_error); },
          },
          std::move(model_sync_callback), true, parent),
      provider_(std::move(shared_provider)),
      config_repository_(std::move(config_repository)) {
  connect(this, &ProviderSettingsControllerBase::connectionStatusChanged, this,
          &GoogleProviderSettingsController::googleConnectionStatusChanged);
  load();
}
```

`save()`'s validation differs from every sibling in exactly one constant:
`if (temperature_ < 0.0 || temperature_ > 2.0)` (REQ-F-005's range — matches OpenAI's [0.0, 2.0],
not Anthropic's [0.0, 1.0]). `max_output_tokens_` is written into
`GoogleProviderConfig::max_output_tokens` with no client-side range validation beyond "is a
positive int" (REQ-C-003 forbids model-specific range enforcement, same as Anthropic's).

### 3.8 `holonight_credentials::CredentialStore` — no changes

Confirmed in §0.5: the interface is already provider-ID-agnostic and its own doc comment already
names `"google"` as an example. `GoogleProviderSettingsController` calls
`credential_store_->store(QStringLiteral("google"), token)` / `retrieve(QStringLiteral("google"))`
/ `remove(QStringLiteral("google"))` — all inherited, unmodified base-class calls parameterized by
the `provider_id_` string passed once at construction.

---

## 4. SSE Parser Design (REQ-F-012–019)

`GoogleProvider::sendChat()` uses the same `StreamContext` + `failStream()` shape as
`AnthropicProvider`'s/`OpenAIProvider`'s (`src/providers/src/anthropic_provider.cpp:29-47`),
copied into `google_provider.cpp`'s own anonymous namespace — this is now the **fourth** SSE-based
provider adapter to make its own copy of this shape (§9 addresses the compounding duplication
directly, since Anthropic's own DESIGN.md already flagged the third copy as the trigger point):

```cpp
struct StreamContext {
  QByteArray buffer;
  HttpRequestHandlePtr handle;
  bool terminal = false;
  bool completed = false;
};
```

### 4.1 Framing — identical blank-line SSE framing, copied a fourth time

Gemini's `?alt=sse` streaming shape is classic SSE (`data: <json>\n\n`, blank line terminates each
block) — the **same wire shape** `AnthropicProvider`'s and `OpenAIProvider`'s parsers already
handle. `GoogleProvider` reuses `findSseBlockDelimiter()`'s exact algorithm (handles both `\n\n`
and `\r\n\r\n`, tolerates fragmented chunk boundaries anywhere including mid-JSON-object and
mid-field-name — REQ-F-012's acceptance criterion) verbatim, copied into `google_provider.cpp`.

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

### 4.2 `processSseBlock()` / `extractSseDataPayload()` — extracting the payload

Reused verbatim from `anthropic_provider.cpp`'s private copy (itself copied from
`openai_provider.cpp`): joins one or more `data:` lines within a block with `\n`, tolerates CRLF
and the optional space-after-colon, ignores any `event:`/`id:`/comment lines (Gemini's `alt=sse`
stream does not emit named `event:` lines the way Anthropic's Messages API does — every block is
implicitly a content update — so this is pure defensive tolerance, not load-bearing here). A block
whose payload fails to parse as JSON calls `failStream()` rather than silently skipping it (same
reasoning as the other three providers).

### 4.3 `routeSseEvent()` — REQ-F-014–019, REQ-C-001

```cpp
void routeSseEvent(const QJsonObject& object, const std::shared_ptr<StreamContext>& context,
                   const std::function<void(const StreamEvent&)>& on_event) {
  // Transport-level error object, distinct from a non-2xx HTTP status (REQ-F-018).
  if (object.contains(QStringLiteral("error"))) {
    failStream(context, on_event, extractGoogleErrorMessage(object));
    return;
  }

  const QJsonArray candidates = object.value(QStringLiteral("candidates")).toArray();
  if (candidates.isEmpty()) {
    // REQ-F-019: partial/intermediate block with no candidate yet (e.g. usage-only) — no-op.
    return;
  }
  const QJsonObject candidate = candidates.at(0).toObject();  // REQ-F-013: candidates[0] only

  // Extract and forward any text content BEFORE checking finishReason — Gemini's final block
  // commonly carries both the last text delta and the terminal finishReason together (§3.2's
  // example's third block), unlike Anthropic's message_stop, which never carries text itself.
  const QJsonArray parts = candidate.value(QStringLiteral("content")).toObject().value(QStringLiteral("parts")).toArray();
  QString deltaText;
  for (const QJsonValue& partValue : parts) {
    const QJsonObject part = partValue.toObject();
    if (part.contains(QStringLiteral("text"))) {  // ignores functionCall/inlineData parts (REQ-C-001/002)
      deltaText += part.value(QStringLiteral("text")).toString();
    }
  }
  if (!deltaText.isEmpty()) {                                          // REQ-F-014
    on_event(StreamEvent{ContentDelta{deltaText}});
  }

  if (!candidate.contains(QStringLiteral("finishReason"))) {
    return;  // REQ-F-019: mid-stream block, nothing terminal yet.
  }

  const QString finishReason = candidate.value(QStringLiteral("finishReason")).toString();
  if (finishReason != QStringLiteral("STOP")) {
    failStream(context, on_event, finishReasonMessage(finishReason));
    return;
  }

  context->terminal = true;
  context->completed = true;
  on_event(StreamEvent{holonight_domain::Completed{}});
}

QString extractGoogleErrorMessage(const QJsonObject& object) {
  const QString message = object.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
  return message.isEmpty() ? QStringLiteral("Google returned an error") : message;
}
```

`usageMetadata` (REQ-F-017) is read nowhere in this function — an unconditional no-op, exactly as
every sibling provider's usage field is (§0.3: no `Usage` variant exists in `StreamEvent`).

### 4.4 `on_finished` — REQ-F-018's "stream ends without completion"

Identical shape to the other three providers': processes any trailing unterminated block, then
calls `failStream(context, on_event, "Stream ended without completion")` if `context->completed`
is still `false`.

### 4.5 `on_error` — recovering the JSON error body from the buffer (REQ-F-018/020/021/022/023)

Identical shape to `AnthropicProvider`'s `onError`, reusing the byte-array-based sibling of §4.3's
helper (`extractGoogleErrorMessage(const QByteArray&, const QString& fallback)`), so a non-2xx
initial-request response (401/429/400, all delivered via `QtNetworkHttpClient`'s already-generic
error-body-preferred-over-errorString() fix — confirmed unchanged by this cycle, same as it was for
Anthropic) surfaces Google's own `error.message` field rather than a generic HTTP status string.

### 4.6 Single-candidate simplification, made explicit

`candidates[0]` is used unconditionally — REQ-F-013's acceptance criterion names this field path
directly, and REQ-C-004 ("no fields not explicitly required... e.g. no candidateCount") guarantees
this request never asks Gemini for more than the default one candidate. If a future cycle adds a
`candidateCount` control, `routeSseEvent()` would need to iterate all of `candidates`, not just
index 0 — documented here as the parser's one scope-bound assumption, the same category of
documented gap as Anthropic's content-block-index simplification (that design's own §4.6).

---

## 5. Key Decisions With Rationale

### 5.1 `ChatController` grows a fourth named provider and a fourth `if` branch — the "rule of
three" moment has now recurred as "rule of four," and is again deliberately not acted on

**Decision**: `ChatController`'s constructor takes all four provider `shared_ptr`s explicitly.
`hasModelsFor()`/`dispatchSendChat()` each grow one more `if (provider_id == "google") ...` branch.
No registry, no `std::variant<shared_ptr<...>>` member, no virtual `ChatProvider` interface.

**Rationale**: identical to the Anthropic cycle's own §5.1 reasoning, now confirmed a second time.
Google's own SPEC.md §F.9 explicitly re-states the same constraint this Spec's predecessors did —
*"GoogleProvider shall likewise be a plain concrete class... it shall NOT introduce a virtual base
class or dynamic-dispatch interface"* — independent of the fact that this is now the fourth such
provider and the fourth near-identical dispatch branch. The Anthropic DESIGN.md predicted this
exact recurrence would eventually put pressure on the "no registry" decision; it has not, because
each Spec keeps re-asserting the same explicit constraint rather than leaving it to Design-stage
inference. Overriding an explicit, repeatedly-restated Spec constraint on the theory that "four
branches is too many" would be scope creep relative to what this cycle's Spec asks for, not
architecture improvement — exactly the same judgment call the Anthropic cycle made at three.

**A registry was reconsidered, and rejected for the same reasons as before, now sharper**: a
`QHash<QString, std::function<...>>`-based dispatch table does not reduce code (four lambda-wrapped
call sites either way), introduces a new "unknown provider_id" failure mode the current design
doesn't have (currently an unrecognized ID falls through to Ollama — arguably a latent bug, but a
deterministic one), and resolves provider identity through exactly the kind of indirection three
consecutive Specs have now explicitly asked this cycle not to introduce. See §10.1.

### 5.2 `GoogleProviderSettingsController` is built as a subclass of the *real, current*
`ProviderSettingsControllerBase` — not a copy of the Anthropic DESIGN.md's own (superseded) plan

**Decision**: `GoogleProviderSettingsController` inherits `ProviderSettingsControllerBase`
directly, following the actual current shape of `AnthropicProviderSettingsController` (§0.2/§3.7),
not the from-scratch ~150-line class the Anthropic cycle's own DESIGN.md §3.7/§5.2 sketched for
itself before this later refactor happened.

**Rationale**: §0.2 already covers *why* the base class exists now (the Anthropic cycle's own
"divergence increased with the third data point" argument against generalizing did not, in
practice, hold once a real fourth provider's Spec was in view — or, more precisely, the
generalization happened as its own dedicated refactor at some point after that cycle's Design stage
closed, separate from any single provider cycle's Design document). What matters for *this*
design is narrower: a Design document's job is to describe what the codebase *actually does today*
and slot the new feature into it — describing a four-provider settings-controller layer as if it
still duplicated ~85% of its shared workflow four times over, when it in fact does not, would be
actively misleading to whoever picks up Stage 3's task breakdown. This is exactly the kind of
"read the real source, don't trust the prior cycle's own design doc" verification §0 exists to
force.

### 5.3 `GoogleProviderSettingsController` reuses `ProviderSettingsController`'s `CredentialStore`,
chained independently of the other two settings controllers — confirmed unchanged for a fourth data point

**Decision**: `GoogleProviderSettingsController::create()` force-resolves `ProviderSettingsController`
(Ollama) as a QML singleton and reuses its `credentialStore()` accessor directly — it does **not**
force-resolve `OpenAIProviderSettingsController` or `AnthropicProviderSettingsController` first.

**Rationale**: unchanged from the Anthropic cycle's own §5.3 — only `ProviderSettingsController`
exposes a `credentialStore()` accessor (it was the first controller constructed, and remains the
one already-accessor-bearing base singleton every later controller chains off of independently).
One shared `CredentialStore` instance now serves `"ollama"`, `"openai"`, `"anthropic"`, and
`"google"` keys — `isAvailable()`'s sticky-false-per-instance behavior stays consistent across all
four panels for the same reason established at two providers and reconfirmed at three.

### 5.4 `x-goog-api-key` via a plain `authHeaders()`, not `AnthropicProvider`'s two-header
`requestHeaders()`

**Decision**: `GoogleProvider::authHeaders()` returns an **empty map** when no credential is
stored (REQ-F-039) — not a map with one always-present entry the way `AnthropicProvider`'s
`requestHeaders()` always includes `anthropic-version`. The method is named `authHeaders()`,
matching `OllamaProvider`'s/`OpenAIProvider`'s naming and contract, not `AnthropicProvider`'s
`requestHeaders()`.

**Rationale**: Gemini's API has no equivalent to Anthropic's mandatory `anthropic-version` header —
REQ-F-001 names only `x-goog-api-key`, and REQ-F-039 explicitly says to mirror
*"OllamaProvider's existing behavior of omitting the auth header entirely when its token is
empty"* — language the Anthropic Spec never used for its own provider (Anthropic's own REQ-F-038
never asked for header omission, because `anthropic-version` always accompanies the request
regardless of auth state). Reusing `AnthropicProvider`'s `requestHeaders()` name for a method whose
actual contract matches Ollama/OpenAI's `authHeaders()` far more closely would misdescribe the
method at the call site; the name should track the *closer* precedent, and here that's not the
provider (Anthropic) this design otherwise mirrors most.

### 5.5 System-prompt hoisting into `systemInstruction` mirrors Anthropic's `system`-hoisting
approach exactly — inline in `sendChat()`, no shared helper, no domain change

**Decision**: the System-message-extraction-and-concatenation logic (§3.2) is inline in
`GoogleProvider::sendChat()`, operating on the same `std::vector<holonight_domain::Message>
history` every provider already receives. No new domain type, no `Conversation`-level "system
prompt" concept, no helper shared with `AnthropicProvider` despite both providers needing
structurally identical hoisting logic (extract System-role messages in order, join with `\n\n`,
omit the field entirely if empty).

**Rationale**: this restates the Anthropic DESIGN.md's own §5.5 argument, now for a genuine second
data point rather than a hypothetical one — and the conclusion is the same, for the same reason:
Ollama's `/api/chat` and OpenAI's `/v1/responses` both accept `{role: "system", ...}` inline and
have no top-level field to hoist into, so a "shared hoisting helper" would only ever have two
callers (`AnthropicProvider`, `GoogleProvider`) and would need to abstract over two structurally
different target shapes anyway — Anthropic's is a bare string (`"system": "..."`), Google's is a
nested object (`"systemInstruction": {"parts": [{"text": "..."}]}"`). The shared part (which
messages to hoist, in what order, joined by what separator) is about five lines; the divergent part
(what shape to wrap the joined string in) is provider-specific by definition. Extracting a helper
for five shared lines wrapping two genuinely different output shapes is not a clear win — noted
here explicitly since the SSE-framing duplication (§9) crossed exactly this kind of threshold and
this one deliberately has not, for a documented reason rather than by omission.

### 5.6 Async-startup credential-arrival race — confirmed to apply unchanged for a fourth provider

**Decision**: `GoogleProviderSettingsController::load()` does not call `probe_provider_->refresh()`
synchronously. It seeds provider-specific state, then calls the base class's `beginLoad(preferred)`,
which calls `credential_store_->retrieve("google")` — asynchronous — and defers the first
`refresh()` to the base class's `onCredentialRetrieved()` → `refreshModels()` chain.
`ChatViewModel::create()` likewise does not call `google_provider_->refresh()` in its constructor.

**Rationale**: unchanged from every prior non-Ollama provider cycle's identical finding — real
`SecretServiceCredentialStore::retrieve()` is a D-Bus round trip, so any provider whose real
requests need a credential before firing must defer its first `refresh()` past that async
resolution or risk a guaranteed-first-attempt 401. This is now the base class's problem to solve
once (§0.2), not each subclass's — `GoogleProviderSettingsController` gets this behavior for free by
being a `ProviderSettingsControllerBase` subclass, rather than needing to re-derive or re-implement
it, which is itself a concrete benefit of the base-class extraction that happened between the
Anthropic and Google cycles.

### 5.7 Denylist substrings unchanged (`embedding`, `moderation`, `vision`, `ocr`) **plus** a
`supportedGenerationMethods` filter — a genuinely new second filter dimension, not present in any
prior provider cycle

See §6 for the implementation. REQ-F-009/010 name the same four substrings every prior provider
cycle's denylist has used, but Google's Spec is the first of the four to *also* require inspecting
a structured field (`supportedGenerationMethods`) rather than relying on substring matching alone.
This is a genuinely new pattern, not a mechanical copy — documented in its own section (§6) rather
than folded into "same as before."

### 5.8 Model IDs are stored without their `"models/"` resource-name prefix — a concrete
resolution of an ambiguity SPEC.md's own mock data left implicit

**Decision**: `fetchModelList()` parses the raw `name` field from Gemini's `GET /v1beta/models`
response (e.g. `"models/gemini-2.0-flash"`), applies denylist + `supportedGenerationMethods`
filtering against that raw string, then **strips the `"models/"` prefix** before constructing
`ModelId{.provider_id = "google", .model_name = "gemini-2.0-flash"}`. `sendChat()`'s URL
construction then re-adds the literal `"models/"` segment itself (§3.2): `base_url_ +
"/v1beta/models/" + model.model_name + ":streamGenerateContent?alt=sse"`.

**Rationale**: REQ-F-009's own mock model list example uses IDs *with* the `"models/"` prefix
(`"models/gemini-2.0-flash"`), matching Gemini's real API response shape, but REQ-F-001's URL-path
template writes `"models/"` as a **literal** segment in the path (`/v1beta/models/{model}
:generateContent`) — if `{model}` already carried its own `"models/"` prefix, the resulting URL
would double up (`/v1beta/models/models/gemini-2.0-flash:...`), which is not a valid Gemini
endpoint. Stripping the prefix once, at model-list-parse time, and treating `ModelId::model_name`
as the bare suffix everywhere downstream (matching every sibling provider's convention, where
`model_name` is always the bare, URL-path-ready identifier) resolves this cleanly and keeps
`ModelId`'s cross-provider contract uniform — no provider-specific "sometimes has a prefix"
special-casing leaks into `ChatController`, `ChatViewModel`, or the QML combo box.

### 5.9 No top-level `"model"` field in the request body — model selection lives entirely in the
URL path, unlike every sibling provider

**Decision**: `sendChat()`'s JSON body has no `"model"` key at all (§3.2) — `model.model_name` is
consumed only when constructing `request.url`.

**Rationale**: this is a genuine, Gemini-specific structural difference from Ollama/OpenAI/
Anthropic, all three of which put `"model": "..."` in the body alongside a fixed, model-independent
URL. Gemini's API design instead encodes the model into the URL's resource path
(`/v1beta/models/{model}:streamGenerateContent`) and has no body-level equivalent — sending an
extraneous `"model"` field in the body would violate REQ-C-004's "no fields not explicitly
required... or documented" constraint for no benefit, since the URL path is already authoritative.
Flagged explicitly (rather than left as an implicit omission) because it's the one place an
implementer pattern-matching too literally against `AnthropicProvider::sendChat()`'s body-
construction code (§3.2 there always includes `body["model"] = model.model_name`) would introduce
an incorrect extra field.

### 5.10 QML integration: routing only, no new list entry (§0.6, detailed in §7)

Restated from §0.6: `ProviderListPanel.qml`'s model array already contains `"Google"` (added
speculatively when the placeholder-only UI was first built, alongside `"Anthropic"`). This cycle's
QML changes are therefore pure *routing* changes — `ProvidersPage.qml`'s `Loader.sourceComponent`
switch and `ProviderListDelegate.qml`'s status-dot binding — plus one new leaf file,
`GoogleSettingsPanel.qml`. No changes to `ProviderListPanel.qml` itself. This is now the **fourth**
row in that list to go from placeholder to functional, and the array itself needs no further
changes for any subsequent provider cycle unless a fifth provider name is added to it later.

---

## 6. Denylist + `supportedGenerationMethods` Filter Implementation and Testing (REQ-F-009/010/011)

Two independent filters, both applied inside `fetchModelList()`'s success handler, both required
for a model to survive (REQ-F-009: "exclude... AND exclude..."):

```cpp
namespace {

// Same four substrings every prior provider cycle's denylist has used (REQ-F-009's own text names
// them explicitly) — not extended speculatively with Gemini-specific guesses.
constexpr std::array<QLatin1StringView, 4> kDenylistedSubstrings{
    QLatin1StringView("embedding"),
    QLatin1StringView("moderation"),
    QLatin1StringView("vision"),
    QLatin1StringView("ocr"),
};

bool isDenylistedModel(const QString& raw_name) {
  return std::ranges::any_of(kDenylistedSubstrings,
                             [&raw_name](QLatin1StringView substring) { return raw_name.contains(substring); });
}

// New for this provider (§5.7): a model must also declare "generateContent" support. Missing or
// empty supportedGenerationMethods is treated as NOT supporting it (fail-closed on this one check,
// deliberately asymmetric with the denylist's fail-open philosophy — see §9's risk entry on why
// these two filters have opposite failure directions).
bool supportsGenerateContent(const QJsonObject& entry) {
  const QJsonArray methods = entry.value(QStringLiteral("supportedGenerationMethods")).toArray();
  return std::ranges::any_of(methods, [](const QJsonValue& method) {
    return method.toString() == QStringLiteral("generateContent");
  });
}

QString stripModelsPrefix(QString name) {  // §5.8
  constexpr auto kPrefix = QLatin1StringView("models/");
  if (name.startsWith(kPrefix)) {
    name.remove(0, kPrefix.size());
  }
  return name;
}

}  // namespace
```

Applied inside `fetchModelList()`'s success handler, immediately after parsing the `"models"`
array (Gemini's `GET /v1beta/models` response shape is `{"models": [{"name": "models/...",
"supportedGenerationMethods": [...], ...}]}`) and before appending to `available_models_`:

```cpp
const QJsonArray modelsArray = doc.object().value(QStringLiteral("models")).toArray();
std::vector<ModelId> models;
for (const auto& entry : modelsArray) {
  const QJsonObject modelObject = entry.toObject();
  const QString rawName = modelObject.value(QStringLiteral("name")).toString();
  if (rawName.isEmpty() || isDenylistedModel(rawName) || !supportsGenerateContent(modelObject)) {
    continue;
  }
  models.push_back(ModelId{.provider_id = QStringLiteral("google"), .model_name = stripModelsPrefix(rawName)});
}
available_models_ = std::move(models);
```

Fail-open on the denylist half (REQ-F-010/011): a hypothetical future model
`"models/gemini-3-ultra"` not matching any of the four substrings, with `supportedGenerationMethods`
containing `"generateContent"`, is included automatically. Fail-*closed* on the
`supportedGenerationMethods` half is a deliberate, separate decision — see §9.

**Testing**: not unit-tested in isolation (no exposed static/free method) — tested exclusively
through the public surface, matching every sibling provider's convention:
`FakeHttpClient::enqueueBufferedSuccess()` with a `/v1beta/models` JSON body mixing denylisted IDs,
allowed IDs with `supportedGenerationMethods: ["generateContent"]`, IDs with only
`["embedContent"]`/`["imageAnalysis"]` (excluded by the second filter despite not matching any
denylist substring), an ID with no `supportedGenerationMethods` field at all (excluded, fail-
closed), and a fail-open future-model probe (`"models/gemini-4-ultra"`, allowed substrings, present
`generateContent` support) — asserting on `provider.availableModels()` after `refresh()`, including
that surviving entries have their `"models/"` prefix stripped.

---

## 7. QML Integration — exact files

**New file**: `qml/workspace/GoogleSettingsPanel.qml`. Structurally copies the current, real
`qml/workspace/AnthropicSettingsPanel.qml` (read directly — §1) with these deltas:
- Every `AnthropicProviderSettingsController` reference becomes `GoogleProviderSettingsController`.
- Temperature `SpinBox`/`Slider` range becomes 0.0–2.0 (not Anthropic's 0.0–1.0): the `SpinBox`'s
  `to:` becomes `200` (still `value / 100` scaling, so `to: 200` represents `2.00`), the `Slider`'s
  `to:` becomes `2.0`, and the trailing bound `Text { text: "1" }` becomes `Text { text: "2" }`
  (REQ-F-005/REQ-F-027's acceptance criterion: "the temperature control supports the wider 0.0–2.0
  range").
- The "Max output tokens" `SpinBox` block (`from: 1`, `to: 200000`, bound to `maxOutputTokens`) is
  kept exactly as-is — same control shape, only the controller reference changes.
- `anthropicConnectionStatus`-equivalent binding becomes `googleConnectionStatus` (the panel itself
  has no direct binding to the connection-status property — that's `ProviderListDelegate.qml`'s
  job, see below — but any panel-local "connected"/"error" text coloring, if present, is retargeted
  the same way).
- Placeholder text `"https://api.anthropic.com"` becomes
  `"https://generativelanguage.googleapis.com"`.

**Modified**: `qml/workspace/ProvidersPage.qml`:

```diff
     Loader {
         Layout.fillWidth: true
         Layout.fillHeight: true
         sourceComponent: {
             switch (providerList.selectedProvider) {
             case "Ollama": return ollamaPanel
             case "OpenAI": return openAiPanel
             case "Anthropic": return anthropicPanel
+            case "Google": return googlePanel
             default: return unsupportedPanel
             }
         }
     }

     Component {
         id: anthropicPanel

         AnthropicSettingsPanel {}
     }

+    Component {
+        id: googlePanel
+
+        GoogleSettingsPanel {}
+    }
+
     Component {
         id: unsupportedPanel

         UnsupportedProviderPanel {
             providerName: providerList.selectedProvider
         }
     }
```

No provider name falls through to `unsupportedPanel` any longer after this change — all four
entries in `ProviderListPanel.qml`'s model array now route to a real panel.

**Modified**: `qml/workspace/ProviderListDelegate.qml`:

```diff
     readonly property bool isAnthropic: root.providerName === "Anthropic"
+    readonly property bool isGoogle: root.providerName === "Google"
     readonly property color statusColor: {
         ...
         if (root.isAnthropic) {
             switch (AnthropicProviderSettingsController.anthropicConnectionStatus) {
             case "connected": return HoloniightPalette.success
             case "error": return HoloniightPalette.error
             default: return HoloniightPalette.textMuted
             }
         }
+        if (root.isGoogle) {
+            switch (GoogleProviderSettingsController.googleConnectionStatus) {
+            case "connected": return HoloniightPalette.success
+            case "error": return HoloniightPalette.error
+            default: return HoloniightPalette.textMuted
+            }
+        }
         return HoloniightPalette.textMuted;
     }
```

**Not modified**: `qml/workspace/ProviderListPanel.qml` (§0.6 — `"Google"` already present in the
model array), `qml/workspace/SettingsWindow.qml` (routes to `ProvidersPage {}` as a whole, no
per-provider knowledge), `qml/workspace/UnsupportedProviderPanel.qml` (no longer referenced by any
row in the current provider list after this change, but left in place — a future fifth provider
would need it again, and nothing about this cycle requires deleting it).

---

## 8. CMake Wiring — exact files, confirmed no target-level changes needed

Every file below is a **source-list addition to an existing target** — no new `add_library`, no
new `add_executable`, no new `find_package`, no new `target_link_libraries` entry, confirmed by
reading the real, current `CMakeLists.txt` files directly (not assumed from the Anthropic cycle's
description of them):

- **`src/providers/CMakeLists.txt`** — `add_library(holonight_providers STATIC ...)`'s source list
  (currently: `http_client.h`, `ollama_provider.h`, `openai_provider.h`, `anthropic_provider.h`,
  `qt_network_http_client.h`, and their four/five matching `.cpp` files) gains
  `include/holonight_providers/google_provider.h` and `src/google_provider.cpp`.
- **`src/config/CMakeLists.txt`** — **no change**. `provider_config.h` and `config_repository.cpp`
  are already in the source list; `GoogleProviderConfig` and the two new `ConfigRepository` methods
  are added *inside* those already-listed files, not as new files.
- **`src/application/CMakeLists.txt`** — `add_library(holonight_application STATIC ...)`'s source
  list (currently includes `provider_settings_controller_base.h`/`.cpp`,
  `provider_settings_controller.h`/`.cpp`, `openai_provider_settings_controller.h`/`.cpp`,
  `anthropic_provider_settings_controller.h`/`.cpp` — confirmed by reading the file directly) gains
  `include/holonight_application/google_provider_settings_controller.h` and
  `src/google_provider_settings_controller.cpp`. `chat_controller.h`/`.cpp` and
  `chat_view_model.h`/`.cpp` are already listed — their in-place edits (§3.5/§3.6) need no new list
  entries.
- **`tests/CMakeLists.txt`** — `add_executable(test_holonight_ai ...)`'s source list (confirmed by
  reading the file directly: already contains `providers/test_anthropic_provider.cpp` and
  `application/test_anthropic_provider_settings_controller.cpp` as plain entries in one list) gains
  `providers/test_google_provider.cpp` and `application/test_google_provider_settings_controller.cpp`
  (see Corrections to SPEC.md above — this is the **only** test-related CMake change; no new
  `add_executable`, no new `gtest_discover_tests()` call).
- **`apps/chat/CMakeLists.txt`** — **no change**. Confirmed by reading the file directly (§ near
  line 55-88): `qml/*.qml` is picked up via a `CONFIGURE_DEPENDS` glob (`GoogleSettingsPanel.qml`
  is automatically included on the next CMake reconfigure), and `GoogleProviderSettingsController`
  is the **fourth** `QML_SINGLETON` type added to the already-`STATIC` `holonight_application`
  library. The `set_property(TARGET holonight_application PROPERTY INTERFACE_SOURCES "")` /
  `qt6_extract_metatypes(holonight_application ...)` / second `set_property(... INTERFACE_SOURCES
  "")` / `combine-metatypes.cmake` / `_qt_internal_qml_type_registration(holonight-chat)` sequence
  this file already runs is read directly (not assumed) to confirm it makes no assumption about how
  many `QML_SINGLETON` types live in `holonight_application` — it extracts and merges whatever
  metatypes the target currently has, unconditionally. This has now been proven true going from one
  such type (`ChatViewModel`) to two (`+OpenAIProviderSettingsController`) to three
  (`+AnthropicProviderSettingsController`) to four (`+GoogleProviderSettingsController`) with zero
  additional plumbing at each step — the CLAUDE.md-documented double-`INTERFACE_SOURCES`-clear
  gotcha is the only thing that mechanism depends on getting right, and it is already correct in
  the current file.
- **`src/domain/CMakeLists.txt`**, **`src/credentials/CMakeLists.txt`** — **no change** (§0.3, §0.5
  — no domain or credentials-interface change is made).

---

## 9. Known Risks

- **The SSE framing/`extractSseDataPayload()`/`StreamContext` code is now duplicated a FOURTH
  time** (§4.1). Anthropic's own DESIGN.md §9 explicitly named this exact scenario — "a fourth
  SSE-based provider... should reach for [extracting `sse_framing.h`]" — as the concrete trigger it
  was watching for, and deliberately declined to act on it preemptively at three copies. That
  trigger condition has now unambiguously occurred. This design still does **not** perform the
  extraction in-cycle, for the same reason the Anthropic design gave for not doing it at three: the
  extraction would touch `openai_provider.cpp` and `anthropic_provider.cpp`, both files outside
  SPEC.md's own "Dependencies and Integration Points" file list, conflating a pure-refactor change
  (with its own regression risk against two already-passing, unrelated test suites) with this
  cycle's actual scope. **This is now flagged for a second consecutive cycle and should be treated
  as near-mandatory follow-up work, not a standing "maybe someday" item** — a fifth SSE-based
  provider (unlikely soon, but not impossible) would make a fifth copy, at which point declining to
  extract stops being a defensible YAGNI call and starts being simple neglect.
- **`supportedGenerationMethods`-missing fail-closed behavior (§6) is asymmetric with the
  denylist's fail-open philosophy, and could silently exclude a valid future chat model.** If
  Google ships a new Gemini chat model whose `GET /v1beta/models` entry omits
  `supportedGenerationMethods` entirely (a field-reliability assumption this design takes on faith,
  not verified against a service-level guarantee), that model is excluded from the ComboBox with no
  error surfaced to the user — it simply never appears, unlike the denylist's fail-open design
  (REQ-F-010/011), which explicitly guards against exactly this kind of "the code doesn't know
  about a legitimate new model" gap for its own filter. No mitigation implemented this cycle
  (matches REQ-F-009's literal text, which requires this field to be inspected, but the *direction*
  of failure on a missing field was this design's own call, not the Spec's).
- **`v1beta` API-version stability.** Every endpoint in this design (`GET /v1beta/models`, `POST
  /v1beta/models/{model}:streamGenerateContent?alt=sse`) uses Google's `v1beta` path segment, not a
  stable `v1`. Google could introduce a `v1` Gemini Developer API surface with different field
  names or behavior in the future; this design has no version-negotiation or fallback logic
  (REQ-C-007 explicitly forbids "alternate... routing" beyond the standard endpoint anyway, so this
  is accepted as-is, not treated as a gap to close this cycle).
- **Safety-filter false positives are Google's, not this adapter's, but land as user-facing
  friction regardless.** REQ-F-016/024's distinct "Response blocked by Google's safety filters."
  message is more informative than a generic error, but the underlying blocking decision is opaque
  and non-configurable (REQ-C-008 explicitly forbids exposing `safetySettings` controls this
  cycle) — a user who hits a false-positive safety block has no in-app recourse beyond rephrasing
  and retrying. Documented as a known UX limitation, not a defect in this design.
- **Model discovery pagination is handled transactionally.** Gemini's `GET /v1beta/models`
  response can include a `nextPageToken`; the adapter follows it until exhausted and publishes the
  accumulated model list only after every page succeeds. A later-page failure therefore preserves
  the previously cached list rather than exposing partial discovery results.
- **`ChatController`'s if/else provider-dispatch chain is now four branches deep, with the same
  structural gap flagged at three:** no compile-time guarantee a missed branch in a hypothetical
  fifth future provider cycle would fail to compile — a hand-written `test_chat_controller.cpp` case
  is the only safety net, unchanged from the prior cycle's identical risk entry, now simply one
  branch larger.
- **`max_output_tokens` has no upper-bound validation anywhere in this design**, by explicit Spec
  instruction (REQ-C-003). A user who enters an unreasonably large value will have it sent verbatim
  and receive Gemini's own 400 error for exceeding the selected model's actual output-token limit —
  expected behavior per the Spec, restated here since `temperature` at least gets a client-side
  [0.0, 2.0] range check in `save()` and this field gets none.
- **The `"models/"`-prefix-stripping convention (§5.8) is a Design-stage resolution of an ambiguity
  SPEC.md itself did not resolve explicitly.** If a future Gemini API revision changes the resource-
  name format returned by `GET /v1beta/models` (e.g., drops the prefix, or nests models under a
  different path segment), `stripModelsPrefix()`'s `startsWith("models/")` check degrades
  gracefully (a name without the prefix passes through unchanged), but the corresponding change to
  `sendChat()`'s URL-construction literal (`"/v1beta/models/" + model.model_name`) would need
  re-verification against whatever the new format turns out to be.

---

## 10. Alternatives Considered

### 10.1 For provider dispatch (§5.1)

- **`QHash<QString, std::function<...>>`-style registry**, now considered for a second cycle in a
  row. Rejected for the same reasons as the Anthropic cycle's own §10.1, restated: no net code
  reduction (four lambda-wrapped call sites either way), a new "unknown provider" failure mode the
  `if`/`else` chain's Ollama-fallback behavior doesn't have, and — decisively — it resolves provider
  identity through a value-level indirection at exactly the place three consecutive Specs (OpenAI,
  Anthropic, Google) have each explicitly asked this cycle not to introduce one.
- **`std::variant<shared_ptr<Ollama...>, shared_ptr<OpenAI...>, shared_ptr<Anthropic...>,
  shared_ptr<Google...>>` member with `std::visit`.** Rejected for the same structural reason as
  before, sharper at four: `ChatController` needs simultaneous access to *all four* providers in
  several places (`hasModelsFor()` needs the one named by a specific `ModelId`, while
  `ChatViewModel`'s combined `availableModels` independently needs all four's lists) — a `variant<>`
  models "exactly one active provider," which still isn't this shape at four providers.

### 10.2 For `GoogleProviderSettingsController` (§5.2)

- **Reimplement the pre-refactor, per-controller-duplicated shape the Anthropic DESIGN.md's own
  §3.7 sketched**, rather than subclassing the real, current `ProviderSettingsControllerBase`.
  Rejected outright — the base class already exists in the tree today (§0.2), and describing (or
  worse, building) a fourth independent ~150-line duplicate of workflow the codebase has already
  consolidated once would be actively regressive, not merely redundant.
- **Further generalize `ProviderSettingsControllerBase` itself to also own `temperature`** (present
  on three of four subclasses with three different ranges) or `maxOutputTokens` (present on two of
  four). Considered briefly, rejected: the base class's current design already drew this line at
  "identical shape and identical validation-free semantics across all subclasses" (base-owned) vs.
  "same field name, different range/presence per subclass" (subclass-owned) — `temperature`'s range
  varies per provider (0.7 Ollama-default-no-hard-limit, 0.0–2.0 OpenAI, 0.0–1.0 Anthropic, 0.0–2.0
  Google) and would need either a constructor-supplied range pair (adding a parameter to every
  existing subclass's constructor call for a property that's arguably still "mostly the same shape
  four times" today) or a virtual range-check hook (reintroducing exactly the kind of per-subclass
  override machinery the base class currently avoids). Not clearly a win over the current four
  three-to-five-line `save()` validation checks.

### 10.3 For the SSE parser (§4)

- **Extract a shared `sse_framing.h` helper now, since this is explicitly the previously-flagged
  trigger point (§9).** Seriously reconsidered given Anthropic's own DESIGN.md named this exact
  moment. Still not done *within this design's file list*, for the reason given in §9: SPEC.md's
  own file-dependency list does not include `openai_provider.cpp`/`anthropic_provider.cpp`, and
  bundling a cross-file refactor into a single-provider feature cycle's diff makes the change harder
  to review against SPEC.md's 42 functional requirements specifically. The recommendation is an
  immediate, small, dedicated follow-up change — not "someday" — with `OpenAIProvider`'s and
  `AnthropicProvider`'s existing test suites as its regression safety net, done as its own
  reviewable unit separate from this cycle's Stage 4 implementation.
- **Use Gemini's non-`alt=sse` streaming shape (incremental JSON array) instead of correcting
  toward `alt=sse` (Corrections to SPEC.md #1).** Rejected: would require an entirely different,
  bespoke incremental-JSON-array parser with no code to reuse from any of the other three providers,
  contradicts REQ-F-012's literal SSE-framing acceptance criteria, and gains nothing — `alt=sse` is
  a same-endpoint query parameter, not a different action or a less-capable mode.

---

## 11. Test and Build Plan Notes

- `tests/providers/test_google_provider.cpp` is added to the **existing single** `test_holonight_ai`
  CTest binary (Corrections to SPEC.md, above, and §8) — not a new standalone executable. Covers:
  request construction (`contents` role mapping user→"user"/assistant→"model", `systemInstruction`
  hoisting with 0/1/2 System messages, `generationConfig.temperature`/`maxOutputTokens` presence and
  values, URL path includes the literal `models/` segment and the selected model ID and
  `:streamGenerateContent?alt=sse` suffix — not `:generateContent`, no top-level `"model"` body
  field, `x-goog-api-key` header present/absent per REQ-F-039), SSE parsing (fragmented chunks
  across five-plus distinct blocks per REQ-F-012's acceptance criterion, `candidates[0].content.
  parts[0].text` → `ContentDelta`, `finishReason: "STOP"` → `Completed`, token exhaustion and
  filtering/failure finish reasons → distinct `Error` messages, prompt-level blocks reported via
  `promptFeedback.blockReason`, a block carrying both trailing
  text and a terminal `finishReason` together emits both events in order), error mapping
  (401/429/400 bodies → extracted `error.message`, malformed JSON → descriptive `Error`, stream-ends-
  without-`finishReason` → `Error`), model filtering (all four denylist substrings, the
  `supportedGenerationMethods` fail-closed check independently, the fail-open future-model probe,
  `"models/"` prefix stripping), cancellation (`cancel()` mid-stream halts further delta processing).
- `tests/application/test_google_provider_settings_controller.cpp` mirrors
  `test_anthropic_provider_settings_controller.cpp`'s structure closely (both subclass the same
  base — §0.2 — so the fixture shape, credential-store mocking, and base-class-inherited-behavior
  assertions are nearly identical), plus new cases for the [0.0, 2.0] temperature validation
  boundary (REQ-F-005) and `maxOutputTokens` round-tripping through `save()`/`load()`/`cancel()`/
  `resetToDefaults()`.
- `tests/application/test_chat_controller.cpp`'s existing fixture-building helpers gain a fourth
  `makeGoogleProvider()` helper; every test that constructs a `ChatController` directly is updated
  for the fourth constructor parameter (§5.1's flagged breaking change); new test cases assert
  `ModelId{provider_id: "google", ...}` routes to the Google `FakeHttpClient` recording, not any of
  the other three providers'.
- `tests/config/test_config_repository.cpp` gains `GoogleProviderConfig` round-trip tests (including
  `max_output_tokens`) and extends the existing multi-provider read-merge-write regression test to
  four providers: save Ollama, then OpenAI, then Anthropic, then Google in sequence, asserting all
  four sections coexist in `config.json` after the fourth save.
- `tests/providers/fake_http_client.h` needs **no changes** — its existing
  `enqueueBufferedSuccess()`/`enqueueBufferedError()`/streaming equivalents already accept arbitrary
  `QByteArray`/`QString` payloads, sufficient for every Gemini response shape this cycle needs to
  simulate (confirmed by reading the file directly — §-level structure identical to what
  `AnthropicProvider`'s tests already exercise).

---

## Document History

- v1.0 (2026-07-25): Initial design for Stage 2, covering all of SPEC.md's REQ-F-001 through
  REQ-C-009, with two explicit corrections to SPEC.md (streaming endpoint action/query-parameter,
  and the recurring single-test-binary wording), and an explicit note that this design targets the
  real, current `ProviderSettingsControllerBase`-based settings-controller architecture rather than
  the pre-refactor shape the Anthropic cycle's own DESIGN.md described for itself.
