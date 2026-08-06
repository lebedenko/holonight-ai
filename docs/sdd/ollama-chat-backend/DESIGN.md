# Ollama Chat Backend Design

**Document Version**: 1.0
**Date**: 2026-07-21
**Modules**: `holonight_providers` (Ollama adapter), `holonight_application` (orchestration), plus a
small additive change to the already-implemented `holonight_domain`
**Traces to**: `docs/sdd/ollama-chat-backend/SPEC.md` (13 functional, 1 non-functional, 8 constraint
requirements)
**Status**: Implemented

---

## 1. Components

### 1.1 `holonight_domain` (additive change to an already-"done" module)

Two new methods are added to existing classes — **no new files, no new types**. This is called out
up front because `holonight_domain` was completed in a prior SDD cycle and is otherwise not in this
cycle's scope (see [Key Decision 4.4](#44-conversation--message-need-two-small-additive-methods)):

- `Message::setText(QString text)` — replaces the message's text content in place.
- `Conversation::replaceLastMessage(Message replacement)` — replaces `messages_.back()` in place.

### 1.2 `holonight_providers`

- **`HttpRequest` / `HttpMethod` / `HttpRequestHandle`** (`http_client.h`) — plain value types and a
  cancellable-handle interface shared by every HTTP call this module makes. No Qt-specific or
  Ollama-specific knowledge lives here.
- **`HttpClient`** (`http_client.h`) — the abstract interface REQ-C-001 requires: one buffered
  `send()` method (GET /api/tags) and one `sendStreaming()` method (POST /api/chat) with an
  idle-timeout parameter. `OllamaProvider` depends only on this interface, never on
  `QNetworkAccessManager` directly.
- **`QtNetworkHttpClient`** (`qt_network_http_client.h`) — the concrete, production `HttpClient`
  implementation backed by `QNetworkAccessManager`/`QNetworkReply`. Owns the idle-timeout `QTimer`
  and the byte-level plumbing between Qt's signal-based network API and this module's
  callback-based `HttpClient` contract.
- **`OllamaProvider`** (`ollama_provider.h`) — the Ollama-specific adapter. Fetches/caches the model
  list from `/api/tags`, builds Ollama's `/api/chat` JSON request body from a message history,
  parses NDJSON response chunks into `holonight_domain::StreamEvent`s, and exposes `refresh()`.
  Holds no per-request mutable state as instance members (see §4.5) so that multiple concurrent
  `sendChat()` calls (REQ-F-012) cannot corrupt one another's parse state.
- **`FakeHttpClient`** (test-only, `tests/providers/fake_http_client.h`) — the REQ-C-001 test double.
  Not part of the production `holonight_providers` public API surface, but designed alongside it
  because `HttpClient`'s shape is chosen specifically to make this fake trivial to hand-write.

### 1.3 `holonight_application`

- **`ChatController`** (`chat_controller.h`) — the orchestrator. Owns a `std::shared_ptr<OllamaProvider>`,
  drives `Message`/`Conversation` state transitions from the `StreamEvent`s `OllamaProvider` emits,
  tracks one in-flight stream per conversation (REQ-F-012/013), and exposes `send()`, `regenerate()`,
  `stop()`, `isStreaming()`. `ChatController` never touches `Qt6::Network` or JSON directly — that
  is entirely `holonight_providers`' concern, per the module boundary already declared in
  `CLAUDE.md` ("it should depend on `holonight_providers` for network access, not talk to the
  network directly").
- **`SendRejected`/`SendRejectReason`** (`chat_controller.h`) — the synchronous-rejection error type
  for REQ-F-009/013's "reject before any HTTP call" paths.

---

## 2. Data Flow

All flows assume a single `ChatController` instance shared across all open conversations, and a
`Conversation&` supplied by the caller (a future UI layer) at each call site — `ChatController`
does not own or store `Conversation` objects, only per-conversation *streaming* bookkeeping keyed
by `ConversationId` (see §4.3).

### 2.1 Happy-path send (REQ-F-001, REQ-F-002)

1. Caller invokes `chatController.send(conversation, model, userText, onEvent)`.
2. `ChatController` validates synchronously, before touching the network:
   - `model.model_name` is empty, or `provider->availableModels()` is empty → return
     `std::unexpected(SendRejected{NoModelSelected})` (REQ-F-009). No HTTP call is made.
   - `conversation.id()` already has an entry in `in_flight_` → return
     `std::unexpected(SendRejected{AlreadyStreaming})` (REQ-F-013). No HTTP call is made.
3. `ChatController` constructs the user's `Message` (`MessageId::generate()`, `User`, `userText`,
   `MessageStatus::Complete` — user-authored text is never "streamed", so it is complete the
   instant it is submitted) and appends it via `conversation.appendMessage(...)`.
4. `ChatController` constructs the assistant placeholder `Message` (`MessageId::generate()`,
   `Assistant`, empty text, `MessageStatus::Pending`) and appends it via `appendMessage(...)`. This
   is the "message with `MessageStatus::Pending`" REQ-F-001 refers to.
5. `ChatController` keeps a local working copy of that placeholder, calls
   `workingMessage.transitionTo(MessageStatus::Streaming)` (validated by `Message`'s own state
   machine), then `conversation.replaceLastMessage(workingMessage)` — satisfying "transitioned to
   `Streaming` before the first network call."
6. `ChatController` calls `provider->sendChat(model, conversation.messages(), onStreamEvent, idleTimeout)`.
   `OllamaProvider` serializes the full history to Ollama's `/api/chat` JSON shape (§3.5) and calls
   `httpClient->sendStreaming(request, idleTimeout, onData, onFinished, onError)`.
7. `HttpClient` (concretely `QtNetworkHttpClient`) issues the POST and, per received chunk of bytes,
   invokes `onData(chunk)`.
8. `OllamaProvider`'s per-call context (§4.5) appends the chunk to a line buffer, splits on `\n`,
   and for each complete, non-blank line: parses it as JSON, extracts `message.content`, and invokes
   the caller's `onStreamEvent(ContentDelta{content})` (REQ-F-002). Malformed JSON on a line is a
   fatal stream error (§2.4/REQ-F-011), not silently skipped; only empty/whitespace lines are
   skipped (REQ-F-002).
9. Back in `ChatController`'s `onStreamEvent` handler, a `ContentDelta` appends to a local
   `QString` accumulator, then `workingMessage.setText(accumulator)` and
   `conversation.replaceLastMessage(workingMessage)` — the in-place update pattern used for every
   delta, not just for retry.
10. When Ollama's final NDJSON line (`done: true`) arrives, `OllamaProvider` invokes
    `onStreamEvent(Completed{})` after emitting the last `ContentDelta` (if the final line carries
    trailing content) — matching REQ-F-001's ordering.
11. `ChatController` receives `Completed`, calls `workingMessage.transitionTo(MessageStatus::Complete)`,
    `conversation.replaceLastMessage(workingMessage)`, forwards `Completed` to the caller's `onEvent`,
    and removes the conversation's `in_flight_` entry — a new `send()`/`regenerate()` for this
    conversation is now accepted again (REQ-F-013).

### 2.2 Stop / cancel mid-stream (REQ-F-003, REQ-F-004)

1. Caller invokes `chatController.stop(conversationId)`.
2. `ChatController` looks up `in_flight_[conversationId.toString()]`. If absent, `stop()` is a
   silent no-op (idempotent — stopping a conversation with nothing in flight is not an error).
3. If present, `stop()` acts **synchronously and authoritatively**, before the network layer confirms
   anything: it takes the stored working `Message` copy, calls
   `workingMessage.transitionTo(MessageStatus::Cancelled)` (the accumulated partial text is already
   present from prior `setText` calls in §2.1 step 9 — REQ-F-004's "partial text retained" falls out
   for free), calls `conversation.replaceLastMessage(workingMessage)`, invokes the caller's
   `onEvent(Cancelled{})` exactly once, and **removes the `in_flight_` entry immediately**.
4. Only after that local state is settled does `stop()` call `handle->cancel()` on the stored
   `HttpRequestHandlePtr`, telling `QtNetworkHttpClient` to abort the underlying `QNetworkReply`.
5. `QtNetworkHttpClient`'s abort will still asynchronously fire either `onFinished` or `onError` on
   the same callbacks passed to `sendStreaming()` at some later point. `OllamaProvider`'s per-call
   context and `ChatController`'s handler both guard against acting on a callback for a stream that
   is no longer the current `in_flight_` entry for that conversation (see §5 risk on races) — so no
   further `ContentDelta`/`Error` reaches the caller after the `Cancelled{}` already delivered in
   step 3, satisfying "no further `ContentDelta` chunks are emitted after stop."

### 2.3 Retry / regenerate (REQ-F-005)

1. Caller invokes `chatController.regenerate(conversation, model, onEvent)`.
2. `ChatController` performs the same REQ-F-009/013 synchronous checks as `send()`.
3. `ChatController` scans `conversation.messages()` from the back for the last `Message` with
   `role() == MessageRole::Assistant`. If none exists, `regenerate()` returns
   `std::unexpected(SendRejected{NoAssistantMessageToRegenerate})` — an explicit, total handling of
   an edge case the spec's acceptance criteria don't enumerate but that a real API must not leave
   as undefined behavior.
4. `ChatController` constructs a **new** `Message` value reusing the found message's existing
   `MessageId` (`Message(existing.id(), MessageRole::Assistant, QString{}, MessageStatus::Pending)`)
   — same id, reset text and status — and calls `conversation.replaceLastMessage(...)` with it.
   "The entire message object is updated in-place... same `MessageId` is retained" (REQ-F-005) is
   satisfied by construction: no new element is appended, `messages_.size()` is unchanged.
5. From here the flow is identical to §2.1 steps 5–11: transition to `Streaming`, call
   `provider->sendChat(...)` with the conversation history **up to but excluding** the regenerated
   message (the history sent to Ollama must not include the stale placeholder being replaced),
   stream deltas via `setText`/`replaceLastMessage`, finish or error exactly as a normal send.

### 2.4 Errors — connection failure, HTTP error, malformed JSON (REQ-F-010, REQ-F-011, REQ-NF-001)

All three error sources funnel through the same `HttpClient::sendStreaming`'s `onError` callback (or,
for malformed mid-stream JSON, through `OllamaProvider` itself detecting a parse failure and
short-circuiting to the equivalent of `onError`) and are handled identically by `ChatController`:

1. `workingMessage.transitionTo(MessageStatus::Error)`, `setText(humanReadableMessage)` — the same
   placeholder message already in the conversation becomes the inline error message; REQ-F-010/011's
   "appended to the conversation as an inline assistant message" is satisfied because that message
   is already part of `conversation.messages()` from step 4/5 of §2.1 — no second message is appended.
2. `conversation.replaceLastMessage(workingMessage)`.
3. `onEvent(Error{humanReadableMessage})` forwarded to the caller.
4. The conversation's `in_flight_` entry is removed — "subsequent send requests can proceed"
   (REQ-F-010) falls out automatically since REQ-F-013's rejection only triggers while an entry
   exists.

Idle timeout (REQ-NF-001) is a special case of "connection failure": `QtNetworkHttpClient` itself
detects the no-data-for-N-seconds condition (its own `QTimer`, reset on every `onData` call) and
aborts the `QNetworkReply`, invoking `onError("Ollama server did not respond within 30 seconds")` —
from `OllamaProvider`/`ChatController`'s point of view this is indistinguishable from any other
transport failure, which is why no separate timeout-handling code path exists above the `HttpClient`
layer.

### 2.5 Model list fetch and refresh (REQ-F-006, REQ-F-007, REQ-F-008)

`ChatViewModel` invokes `OllamaProvider::refresh()`, which calls
`httpClient->send({Get, base_url + "/api/tags"}, onSuccess, onError)`
before returning. `onSuccess` parses the `models` array and populates `available_models_`; `onError`
leaves `available_models_` empty (REQ-F-006's graceful-degradation clause). **Important**: with the
real `QtNetworkHttpClient`, this fetch is asynchronous — `available_models_` may still be empty for
some time after the constructor returns, until the reply completes. With `FakeHttpClient` in tests,
`send()` invokes its callback synchronously and inline, so the model list is populated before the
constructor call even returns — this asymmetry is intentional and documented (§5) rather than
papered over with a fake blocking event loop. `refresh()` repeats the same GET and, on success,
entirely replaces `available_models_` (not merges); on failure, the previous cache is left untouched.

---

## 3. Interfaces / APIs

### 3.1 `http_client.h` (`holonight_providers`)

```cpp
#pragma once

#include <QByteArray>
#include <QString>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

namespace holonight_providers {

enum class HttpMethod : std::uint8_t { Get, Post };

struct HttpRequest {
  HttpMethod method = HttpMethod::Get;
  QString url;
  QByteArray body;                                          // empty for Get
  QString content_type = QStringLiteral("application/json");  // ignored for Get
};

class HttpRequestHandle {
 public:
  virtual ~HttpRequestHandle() = default;

  virtual void cancel() = 0;
};

using HttpRequestHandlePtr = std::shared_ptr<HttpRequestHandle>;

using HttpDataCallback = std::function<void(const QByteArray&)>;
using HttpFinishedCallback = std::function<void()>;
using HttpErrorCallback = std::function<void(const QString&)>;
using HttpBufferedSuccessCallback = std::function<void(const QByteArray&)>;

class HttpClient {
 public:
  virtual ~HttpClient() = default;

  // Single round-trip request (used for GET /api/tags). on_success fires exactly once with the
  // full response body, or on_error fires instead. No idle timeout applies.
  virtual HttpRequestHandlePtr send(const HttpRequest& request, HttpBufferedSuccessCallback on_success,
                                     HttpErrorCallback on_error) = 0;

  // Streaming request (used for POST /api/chat). on_data fires once per chunk of raw bytes
  // received — not necessarily line-aligned. on_finished fires once when the connection closes
  // normally; on_error fires instead of on_finished on failure, non-2xx HTTP status, or idle
  // timeout. idle_timeout resets on every on_data invocation.
  virtual HttpRequestHandlePtr sendStreaming(const HttpRequest& request, std::chrono::milliseconds idle_timeout,
                                              HttpDataCallback on_data, HttpFinishedCallback on_finished,
                                              HttpErrorCallback on_error) = 0;
};

}  // namespace holonight_providers
```

### 3.2 `qt_network_http_client.h` (`holonight_providers`)

```cpp
#pragma once

#include "holonight_providers/http_client.h"

#include <QNetworkAccessManager>
#include <QObject>

namespace holonight_providers {

// Production HttpClient backed by Qt6::Network. QObject-derived only because
// QNetworkAccessManager/QNetworkReply are themselves signal-based; this is the sole seam in
// holonight_providers where Qt's signal/slot machinery is used — HttpClient's own public
// contract stays plain-callback (see Key Decision 4.1).
class QtNetworkHttpClient : public QObject, public HttpClient {
  Q_OBJECT

 public:
  explicit QtNetworkHttpClient(QObject* parent = nullptr);

  HttpRequestHandlePtr send(const HttpRequest& request, HttpBufferedSuccessCallback on_success,
                             HttpErrorCallback on_error) override;

  HttpRequestHandlePtr sendStreaming(const HttpRequest& request, std::chrono::milliseconds idle_timeout,
                                      HttpDataCallback on_data, HttpFinishedCallback on_finished,
                                      HttpErrorCallback on_error) override;

 private:
  QNetworkAccessManager network_manager_;
};

}  // namespace holonight_providers
```

Internally, `sendStreaming` connects `QNetworkReply::readyRead` to a lambda that reads available
bytes, calls `on_data`, and restarts a per-reply `QTimer` (single-shot, `idle_timeout` interval)
whose `timeout()` slot aborts the reply and calls `on_error("Ollama server did not respond within "
+ ... + " seconds")`. `QNetworkReply::finished` (when not preceded by the timer firing) calls
`on_finished` on success or `on_error` with `reply->errorString()` on a Qt network-layer error, and
inspects the HTTP status code for non-2xx before treating a reply as successful (REQ-F-011's "HTTP
5xx or other error status"). The returned `HttpRequestHandle::cancel()` calls `reply->abort()`.

### 3.3 `ollama_provider.h` (`holonight_providers`)

```cpp
#pragma once

#include "holonight_providers/http_client.h"

#include <holonight_domain/holonight_domain.h>

#include <QString>

#include <chrono>
#include <functional>
#include <memory>
#include <vector>

namespace holonight_providers {

class OllamaProvider {
 public:
  explicit OllamaProvider(std::shared_ptr<HttpClient> http_client,
                           QString base_url = QStringLiteral("http://localhost:11434"));

  [[nodiscard]] const std::vector<holonight_domain::ModelId>& availableModels() const;

  // Re-fetches /api/tags and replaces the cached list. on_complete (optional) fires once the
  // fetch settles, success or failure, so a future UI layer can react without polling.
  void refresh(std::function<void()> on_complete = {});

  // Sends `history` (already including the newly-appended user/placeholder messages) to
  // /api/chat and streams the response. Returns a handle the caller can cancel(); the handle is
  // also threaded through as the in-flight token ChatController stores per conversation.
  HttpRequestHandlePtr sendChat(const holonight_domain::ModelId& model,
                                 const std::vector<holonight_domain::Message>& history,
                                 std::function<void(const holonight_domain::StreamEvent&)> on_event,
                                 std::chrono::milliseconds idle_timeout = std::chrono::seconds{30});

 private:
  std::shared_ptr<HttpClient> http_client_;
  QString base_url_;
  std::vector<holonight_domain::ModelId> available_models_;
};

}  // namespace holonight_providers
```

Note what is deliberately **absent**: no member field buffers a partial NDJSON line or an
in-progress response across calls. That state is local to each `sendChat()` invocation (see §4.5),
so `available_models_`/`http_client_`/`base_url_` are the only instance state, all of which are
either read-only after construction or fully replaced (never partially mutated) by `refresh()`.

### 3.4 `chat_controller.h` (`holonight_application`)

```cpp
#pragma once

#include "holonight_providers/ollama_provider.h"

#include <holonight_domain/holonight_domain.h>

#include <QHash>
#include <QString>

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>

namespace holonight_application {

enum class SendRejectReason : std::uint8_t {
  NoModelSelected,
  AlreadyStreaming,
  NoAssistantMessageToRegenerate,
};

struct SendRejected {
  SendRejectReason reason;

  friend bool operator==(const SendRejected&, const SendRejected&) = default;
};

class ChatController {
 public:
  explicit ChatController(std::shared_ptr<holonight_providers::OllamaProvider> provider);

  std::expected<void, SendRejected> send(
      holonight_domain::Conversation& conversation, const holonight_domain::ModelId& model, QString user_text,
      std::function<void(const holonight_domain::StreamEvent&)> on_event = {});

  std::expected<void, SendRejected> regenerate(
      holonight_domain::Conversation& conversation, const holonight_domain::ModelId& model,
      std::function<void(const holonight_domain::StreamEvent&)> on_event = {});

  // Idempotent: stopping a conversation with nothing in flight is a no-op, not an error.
  void stop(const holonight_domain::ConversationId& conversation_id);

  [[nodiscard]] bool isStreaming(const holonight_domain::ConversationId& conversation_id) const;

 private:
  struct InFlightStream {
    holonight_providers::HttpRequestHandlePtr handle;
    holonight_domain::Message working_message;
    QString accumulated_text;
  };

  std::shared_ptr<holonight_providers::OllamaProvider> provider_;
  QHash<QString, InFlightStream> in_flight_;  // keyed by ConversationId::toString(), see §4.3
};

}  // namespace holonight_application
```

### 3.5 Ollama wire shapes targeted

**`POST {base_url}/api/chat`** request body (REQ-C-005/006):

```json
{
  "model": "llama3",
  "stream": true,
  "messages": [
    { "role": "user", "content": "Hello" },
    { "role": "assistant", "content": "Hi there" }
  ]
}
```

`MessageRole` maps to Ollama's role strings via a small private helper inside `OllamaProvider`:
`System → "system"`, `User → "user"`, `Assistant → "assistant"`.

NDJSON response, one JSON object per line, non-final:

```json
{"model":"llama3","message":{"role":"assistant","content":"Hi"},"done":false}
```

final line:

```json
{"model":"llama3","message":{"role":"assistant","content":""},"done":true}
```

`message.content` on each line (including the final one, if non-empty) becomes a `ContentDelta`;
`done: true` triggers `Completed` after any trailing delta. An `error` field at the top level of a
line (Ollama's shape for in-stream errors, e.g. `{"error":"model runner exited"}`) or a non-2xx HTTP
status on the initial response maps to `StreamEvent::Error` (REQ-F-011).

**`GET {base_url}/api/tags`** response:

```json
{
  "models": [
    { "name": "llama3:latest", "modified_at": "...", "size": 123, "digest": "..." }
  ]
}
```

Each `models[].name` maps to `ModelId{ .provider_id = "ollama", .model_name = name }` (REQ-F-006).

### 3.6 `holonight_domain` additions

```cpp
// message.h — one new method on the existing class
class Message {
  ...
  void setText(QString text);  // replaces text_ only; status is unaffected and still governed
                                // exclusively by transitionTo()
  ...
};
```

```cpp
// conversation.h — one new method on the existing class
class Conversation {
  ...
  // Replaces messages_.back() with `replacement` and stamps updated_at_. Returns false (no-op)
  // if the conversation currently has no messages; true otherwise.
  bool replaceLastMessage(Message replacement);
  ...
};
```

---

## 4. Key Decisions With Rationale

### 4.1 Streaming delivery mechanism → plain `std::function` callbacks, not `QObject` signals

`HttpClient`, `OllamaProvider::sendChat`, and `ChatController::send`/`regenerate` all deliver events
via `std::function` parameters, not Qt signals. `holonight_providers`/`holonight_application` have
no `QObject`-derived types today (`CLAUDE.md` confirms both are still empty `INTERFACE` targets),
and REQ-C-001 explicitly asks for a fake that is easy to substitute in GTest without network I/O. A
`QObject`-based interface would force every consumer — including the GTest fake — through moc
generation, `connect()` boilerplate, and (for the abstract base itself) `QObject`'s single-parent
ownership model, none of which is needed for what is, from the caller's point of view, a synchronous
"give me a callback, I'll call it" contract. Callbacks compose more directly with `std::expected`
(already the project's established idiom, per memory `project_domain_core_types`) and let
`FakeHttpClient` be a small hand-written subclass with no `Q_OBJECT` macro at all. The one place Qt
signals remain load-bearing is *inside* `QtNetworkHttpClient` (§3.2), which is unavoidable since
`QNetworkReply` itself is signal-based — that internal wiring is fully hidden behind the plain
`HttpClient` interface.

### 4.2 Idle timeout → owned by the concrete `HttpClient`, not by `OllamaProvider`

`sendStreaming`'s `idle_timeout` parameter is implemented with a `QTimer` inside
`QtNetworkHttpClient`, reset on every `onData` invocation. `OllamaProvider` and `ChatController` treat
a timeout as just another flavor of `onError`. This keeps the "no data received for N seconds"
concept scoped to the layer that actually owns wall-clock/event-loop timing primitives
(`Qt6::Network`/`Qt6::Core`'s `QTimer`), and keeps `OllamaProvider` free of any Qt event-loop
dependency beyond what it already needs for JSON/`QByteArray` handling — `OllamaProvider`'s own
logic is then testable with a `FakeHttpClient` that never starts a real timer at all (see §4.6/§5).

### 4.3 Per-conversation in-flight tracking → `QHash<QString, InFlightStream>` keyed by `toString()`

`ConversationId` (`src/domain/include/holonight_domain/conversation.h`) has no `qHash` overload
today — only a defaulted `operator==`. Adding one purely to support this module's internal bookkeeping
would be scope creep into a module this cycle is meant to leave untouched except for the two additive
methods in §3.6. `ConversationId::toString()` already returns a UUID-formatted `QString` (per the
prior cycle's design), which is exactly the shape `QHash<QString, ...>` wants, at the cost of one
string copy per lookup — negligible next to a network round trip. This is the same pattern the prior
cycle's own design doc anticipated ("If `Conversation` lookup by ID is later needed, that is an
`application`/`persistence` concern... e.g. a `QHash` keyed by `toString()`").

### 4.4 `Conversation`/`Message` need two small additive methods

Neither `replaceLastMessage` nor `setText` existed before this cycle; both are required because
`Conversation` currently exposes only `appendMessage` (no removal/replacement) and `Message` exposes
only `transitionTo` (status only, no content mutation) — there is today no way to update an
already-appended message's text as streaming chunks arrive, nor to satisfy REQ-F-005's "replace in
place, same `MessageId`, no new entry appended" without one. Both methods are deliberately narrow:
`setText` never touches `status_` (that remains `transitionTo`'s exclusive responsibility, preserving
the prior cycle's "transitions are the sole validated mutator" invariant), and `replaceLastMessage`
never touches anything but the final vector element — neither method opens a general-purpose mutation
API that would let a caller violate `Conversation`'s ordering or `Message`'s status-machine
invariants. This is flagged explicitly, per this task's instructions, as a change to a module a prior
SDD cycle marked complete — it is additive-only (no existing signature changes), so it does not
invalidate any of that cycle's 33 passing tests.

### 4.5 `OllamaProvider` keeps zero per-request state as instance members

Each `sendChat()` call captures its own line-buffer/accumulator state (e.g. a
`std::make_shared<QByteArray>()` line buffer) inside the lambdas passed to
`httpClient->sendStreaming(...)`, rather than storing it on `OllamaProvider` itself. This is required
by REQ-F-012 ("two distinct Conversation objects can each initiate a send request without blocking
one another... data from Conversation A does not affect Conversation B"): if `OllamaProvider` stored
"the current line buffer" as a single member, two concurrent `sendChat()` calls for different
conversations would corrupt each other's partial-line state. Per-call heap-held closures make each
stream's parsing state independent by construction, with no locking needed since Qt's network I/O
callbacks all run on the same event-loop thread.

### 4.6 `stop()` is synchronous and authoritative, not "wait for the network layer to confirm"

`ChatController::stop()` performs the `Cancelled` transition and clears `in_flight_` immediately,
*before* asking the `HttpRequestHandle` to cancel the underlying transport. The alternative — waiting
for `QNetworkReply::abort()` to actually fire a callback before transitioning to `Cancelled` — would
leave a window in which the reply could still deliver one more `readyRead` (and thus one more
`ContentDelta`) between the user's stop request and the eventual abort taking effect, which would
violate REQ-F-003's "no further `ContentDelta` chunks are emitted after stop is called." Treating the
application-level cancel as authoritative and the transport-level abort as a best-effort cleanup
detail (whose late callback is explicitly ignored, §5) is the only way to make that guarantee
synchronous from the caller's perspective.

---

## 5. Alternatives Considered

- **`QObject` signals for streaming delivery** (rejected, §4.1): would require `HttpClient`,
  `OllamaProvider`, and `ChatController` to all be `QObject`-derived, forcing moc generation onto
  types that otherwise need none, and forcing GTest fakes to either derive from `QObject` too or
  awkwardly wrap a non-`QObject` fake behind a signal-emitting adapter just for the test. Plain
  callbacks satisfy REQ-C-001's "easy to substitute a fake" requirement more directly.
- **A generic multi-provider `ChatProvider` interface this cycle** (rejected): SPEC.md's Non-Goals
  explicitly limit this cycle to "a small extensible provider interface... but only the Ollama
  adapter is implemented." Designing a full abstract chat-provider interface now (beyond
  `HttpClient`, which is transport-level and genuinely provider-agnostic) risks guessing wrong about
  what OpenAI/Anthropic/Google adapters will need before any of them exist, and is explicitly out of
  scope. `HttpClient` is the only abstraction introduced beyond the concrete `OllamaProvider`.
- **`Message::appendText(QString delta)` instead of `setText(QString text)`** (rejected, §3.6):
  append-only would need a *second* method for regenerate's "reset text to empty" case anyway (REQ-F-005
  starts a fresh stream against the same message), so it doesn't actually save an API surface; a
  single `setText` with the caller (`ChatController`) owning the accumulation buffer covers both
  "grow during streaming" and "reset for regenerate" with one method and is simpler to reason about
  (`ChatController`'s `accumulated_text` is always the single source of truth for what the message's
  text *should* be at any instant, not derived from repeated in-place string appends on two separate
  objects).
- **`QHash<ConversationId, InFlightStream>` with a hand-written `qHash(const ConversationId&)`
  overload** (rejected, §4.3): would require touching `holonight_domain::ConversationId` beyond the
  two additive methods already justified in §4.4, for a benefit (avoiding one `QString` copy per
  lookup) that is immaterial next to network I/O latency.
- **Waiting for transport-level confirmation before transitioning to `Cancelled`** (rejected, §4.6):
  correctness risk (a late `ContentDelta` after stop) outweighs the marginal benefit of only
  transitioning once the socket is verifiably closed.

---

## 6. `CMakeLists.txt` Changes

### `src/providers/CMakeLists.txt` — `INTERFACE` → `STATIC`

```diff
-add_library(holonight_providers INTERFACE)
+add_library(holonight_providers STATIC
+    src/http_client.cpp
+    src/qt_network_http_client.cpp
+    src/ollama_provider.cpp
+)

 target_include_directories(holonight_providers
-    INTERFACE
+    PUBLIC
     ${CMAKE_CURRENT_SOURCE_DIR}/include
 )

 target_link_libraries(holonight_providers
-    INTERFACE
+    PUBLIC
     holonight_domain
     Qt6::Core
     Qt6::Network
 )

-target_compile_features(holonight_providers INTERFACE cxx_std_23)
+target_compile_features(holonight_providers PUBLIC cxx_std_23)
```

`QtNetworkHttpClient` is `QObject`-derived (§3.2), so `set_target_properties(holonight_providers
PROPERTIES AUTOMOC ON)` must also be added — the first module in this project to need AUTOMOC on a
`src/*` static library rather than only on the `test_holonight_ai`/`holonight-chat` executables.

### `src/application/CMakeLists.txt` — `INTERFACE` → `STATIC`

```diff
-add_library(holonight_application INTERFACE)
+add_library(holonight_application STATIC
+    src/chat_controller.cpp
+)

 target_include_directories(holonight_application
-    INTERFACE
+    PUBLIC
     ${CMAKE_CURRENT_SOURCE_DIR}/include
 )

 target_link_libraries(holonight_application
-    INTERFACE
+    PUBLIC
     holonight_domain
     Qt6::Core
+    holonight_providers
 )

-target_compile_features(holonight_application INTERFACE cxx_std_23)
+target_compile_features(holonight_application PUBLIC cxx_std_23)
```

`holonight_providers` is added as a new link dependency — `ChatController` holds a
`std::shared_ptr<OllamaProvider>` by header inclusion, so this must be `PUBLIC` (consumers of
`holonight_application` need `holonight_providers`' headers transitively). `Qt6::Network` is *not*
added to `holonight_application` — it reaches it only transitively through `holonight_providers`,
preserving the module-boundary rule in `CLAUDE.md` that application code never talks to the network
directly.

### `tests/CMakeLists.txt`

```diff
 add_executable(test_holonight_ai
   main.cpp
   test_placeholder.cpp
   domain/test_message.cpp
   domain/test_conversation.cpp
   domain/test_model_id.cpp
   domain/test_stream_event.cpp
+  domain/test_conversation_replace_last_message.cpp
+  providers/test_http_client_fake.cpp
+  providers/test_ollama_provider.cpp
+  application/test_chat_controller.cpp
 )
 set_target_properties(test_holonight_ai PROPERTIES AUTOMOC ON)
 target_compile_features(test_holonight_ai PRIVATE cxx_std_23)
 target_link_libraries(test_holonight_ai PRIVATE
   GTest::gtest
   GTest::gmock
   Qt6::Gui
   holonight_domain
+  holonight_providers
+  holonight_application
 )
```

(`domain/test_conversation.cpp` may instead simply gain new `TEST()` cases for `replaceLastMessage`
rather than a new file — a Development-phase call; both are listed here as the two reasonable
options, not a hard requirement of this design.)

---

## 7. Test File Organization

```
tests/
├── domain/
│   └── test_conversation.cpp          # + new cases: replaceLastMessage on empty/non-empty
│                                       #   conversation, updated_at stamping, Message::setText
├── providers/
│   ├── fake_http_client.h             # FakeHttpClient test double (see §3, REQ-C-001)
│   ├── test_ollama_provider.cpp        # NDJSON parsing, model list fetch/cache/refresh,
│   │                                   # connection-error → Error mapping, chunk-boundary
│   │                                   # splitting across multiple onData calls
│   └── test_qt_network_http_client.cpp # optional, thin: constructs the real client, asserts
│                                        # it can be constructed/destroyed safely; not a primary
│                                        # coverage vehicle (see §8 risk on timer testing)
└── application/
    └── test_chat_controller.cpp        # full send→stream→complete via FakeHttpClient,
                                         # stop-mid-stream with partial content retained,
                                         # regenerate preserving MessageId, concurrent
                                         # per-conversation sends, reject-no-model,
                                         # reject-already-streaming
```

`FakeHttpClient` (§1.2, §3) is the single shared test double used by both `providers` and
`application` test files — `application` tests construct an `OllamaProvider` wrapping a
`FakeHttpClient` exactly as `providers` tests do, so that `ChatController`-level tests exercise the
real `OllamaProvider` parsing logic end-to-end rather than mocking `OllamaProvider` itself. Each new
`.cpp` uses plain `TEST()`/`TEST_F()` GTest bodies, following the existing
`TEST(Placeholder, AlwaysPasses)` naming convention (e.g.
`TEST(ChatController, StopMidStreamRetainsPartialText)`).

---

## 8. Known Risks

- **`QNetworkReply` streaming + idle-timeout interaction**: real risk. `QNetworkReply` delivers
  `readyRead` at arbitrary chunk boundaries (not one call per NDJSON line, sometimes multiple lines
  per call, sometimes a line split across two calls) — `OllamaProvider`'s line-buffering (§4.5) must
  handle both. Aborting a reply from inside its own `timeout()`-triggered slot must not re-enter
  `readyRead`/`finished` in a way that double-fires `on_error`; `QtNetworkHttpClient` needs a
  single "already resolved" guard per request (e.g. a bool captured alongside the timer) so that an
  abort-triggered `finished`/`errorOccurred` signal that fires after the timeout already called
  `on_error` does not call it a second time.
- **Testing the idle timeout without real wall-clock waits**: `QtNetworkHttpClient`'s actual `QTimer`
  wiring is not the primary target of fast unit tests — it is thin, reviewable glue. The
  *behavior* REQ-NF-001 actually specifies (timeout → `Error`-status message, inline error, stream
  usable afterward) is instead verified at the `OllamaProvider`/`ChatController` layer using
  `FakeHttpClient`, which exposes a synchronous `simulateIdleTimeout(handle)` hook that invokes
  `on_error(...)` directly with no real timer involved at all. This means the real `QTimer` logic
  inside `QtNetworkHttpClient` itself has comparatively thin automated coverage (`test_qt_network_http_client.cpp`
  is intentionally described as "optional, thin" in §7) — accepted as a residual risk for this
  cycle, mitigated by keeping that logic small and isolated for manual/code-review verification
  against a real local Ollama instance.
- **In-place message replacement racing with a concurrent stop**: addressed by design (§4.6) via
  `stop()` being synchronous/authoritative and clearing `in_flight_` before the transport-level abort
  is even requested, but the *implementation* must be careful that `OllamaProvider`'s per-call
  callbacks (§4.5), once invoked after a `stop()` has already fired, are complete no-ops — not merely
  "harmless" but actively checked, e.g. by having `ChatController`'s stored `HttpRequestHandlePtr`
  double as an identity token: a callback fires with a captured copy of the handle it was registered
  against, and the handler checks that handle is still the one currently in `in_flight_` for that
  conversation before doing anything. Getting this guard wrong would let a late network error
  silently resurrect a `Cancelled` message back to `Error`, or vice versa.
- **`Conversation&` lifetime across an async stream**: `ChatController` captures a reference to the
  caller's `Conversation` inside the lambdas passed down to `OllamaProvider`/`HttpClient`. Since
  `holonight_persistence` is out of scope this cycle, nothing owns conversations centrally — if a
  caller destroys a `Conversation` while `isStreaming(conversation.id())` is true, the in-flight
  callback will dereference a dangling reference. This is an accepted constraint of an in-memory,
  no-persistence cycle (REQ-C-004) and should be revisited once a `holonight_persistence`-backed
  conversation store exists with stable ownership; until then, callers (the future UI layer) must
  call `stop()` before releasing a `Conversation` with an in-flight stream.
- **Moving `holonight_providers`/`holonight_application` from `INTERFACE` to `STATIC`**: mechanical
  but easy to get subtly wrong — in particular remembering `AUTOMOC ON` for `holonight_providers`
  (new requirement, since `QtNetworkHttpClient` is this project's first `src/*`-library `QObject`
  type) and keeping `Qt6::Network` `PUBLIC` only on `holonight_providers`, not leaking it into
  `holonight_application`'s link interface, to preserve the module-boundary rule in `CLAUDE.md`.
- **Ollama error-shape variability**: Ollama can signal failure three different ways — a non-2xx
  HTTP status on the initial response, a top-level `"error"` field inside an otherwise
  `done:false`-shaped NDJSON line, or a line that simply fails to parse as JSON at all. All three
  must map to `StreamEvent::Error` (REQ-F-011), but they are detected at different points in
  `QtNetworkHttpClient`/`OllamaProvider`'s pipeline (HTTP status at the `HttpClient` layer; the
  other two inside `OllamaProvider`'s line-processing loop) — a review pass during implementation
  should specifically check all three paths produce the same `StreamEvent::Error`/`MessageStatus::Error`
  outcome, not just the one exercised by the most obvious test case.

---

## Document History

| Version | Date       | Author | Changes                                     |
|---------|------------|--------|----------------------------------------------|
| 1.0     | 2026-07-21 | Claude | Initial design from SPEC.md v1.0             |
