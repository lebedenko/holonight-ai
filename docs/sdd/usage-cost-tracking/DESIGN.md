# DESIGN: Per-Provider, Per-Model Token Usage and Cost Tracking

Status: Stage 2 (Design) of the SDD cycle. Implements `docs/sdd/usage-cost-tracking/SPEC.md`. No
code in this document — file paths, type names, and method signatures are specified to the level
needed to make Stage 4 (Implementation) mechanical, not to pre-write it.

---

## 0. Corrected premise: no `.gen` codegen for `Usage`

The SPEC's REQ-F-001 acceptance criterion mentions "the generated `.gen` domain header file." This
cycle's task brief asked to verify that claim against the real repo before assuming it. It does
not hold: `src/domain/include/holonight_domain/stream_event.h` is a single hand-written header —
`ContentDelta`, `Completed`, `Error`, and `Cancelled` are all plain structs in that file, and
`find src/domain -iname "*.gen*"` returns nothing anywhere in the module. No `.gen.h`/codegen
script exists for any domain type. `Usage` is therefore designed as a fifth hand-written struct
added directly to `stream_event.h`, following the same `friend bool operator==(...) = default`
convention as its siblings. This correction does not change any REQ-F-001/002 acceptance
criterion's intent (Usage optional on the three terminal variants, fields independently optional)
— it only changes *how* the header is authored (by hand, not generated).

---

## 1. Components

### 1.1 `holonight_domain` (modified: `stream_event.h`; no new files)

Add a `Usage` struct and attach `std::optional<Usage>` to `Completed`, `Error`, and `Cancelled`.
`ContentDelta` is untouched (REQ-F-001 scopes Usage to terminal variants only).

```
src/domain/include/holonight_domain/stream_event.h   (modified)
```

No new `.cpp` — the whole file is header-only aggregate structs, matching the existing pattern.

### 1.2 `holonight_providers` (modified: all four adapters + one new shared header)

Each adapter's anonymous-namespace `StreamContext` struct (already holding per-stream mutable
state such as Anthropic's `stop_reason`) gains a `holonight_domain::Usage usage;` accumulator
member, populated field-by-field as SSE/NDJSON events arrive and attached to whichever terminal
`StreamEvent` fires.

```
src/providers/include/holonight_providers/clock.h          (new — REQ-NF-001 time-source seam)
src/providers/src/anthropic_provider.cpp                    (modified — message_start/message_delta parsing)
src/providers/src/openai_provider.cpp                        (modified — response.completed usage parsing)
src/providers/src/google_provider.cpp                         (modified — usageMetadata + modelVersion parsing)
src/providers/src/ollama_provider.cpp                          (modified — done:true chunk parsing)
```

`clock.h` is placed in `holonight_providers` (not `holonight_domain`) because it is a
provider-adapter implementation seam — exactly analogous to `http_client.h` living in the same
directory as the adapters that inject it, not in domain. Each `sendChat()` gains a `Clock&`
(or `std::shared_ptr<Clock>`) constructor/parameter analogous to `http_client_`.

### 1.3 `holonight_config` (new: price table loader; modified: `config_path.h`)

```
src/config/include/holonight_config/price_table.h        (new — PriceTable, ModelPricing types)
src/config/src/price_table.cpp                             (new)
src/config/include/holonight_config/price_table_repository.h  (new — load/lookup API)
src/config/src/price_table_repository.cpp                       (new)
src/config/include/holonight_config/config_path.h          (modified — add resolvePriceTableFilePath())
src/config/src/config_path.cpp                                (modified)
```

`PriceTableRepository` is deliberately a separate class from `ConfigRepository`, and the price
table lives in its own JSON file rather than a new section of `config.json` — see §6.3.

### 1.4 `holonight_persistence` (new migration + repository methods; modified: schema-adjacent files)

```
src/persistence/migrations/0004_add_usage.sql                     (new)
src/persistence/include/holonight_persistence/usage_record.h       (new — UsageRecord struct)
src/persistence/src/usage_record.cpp                                (new)
src/persistence/include/holonight_persistence/conversation_repository.h  (modified — add persistUsage())
src/persistence/include/holonight_persistence/detail/conversation_repository_worker.h  (modified)
src/persistence/src/detail/conversation_repository_worker.cpp                            (modified)
src/persistence/include/holonight_persistence/sqlite_conversation_repository.h  (modified — forward persistUsage())
src/persistence/src/sqlite_conversation_repository.cpp                            (modified)
src/persistence/CMakeLists.txt                                                     (modified — register migration 0004 + new files)
```

### 1.5 `holonight_application` (modified: orchestration hook, not a new module concern)

```
src/application/include/holonight_application/chat_controller.h   (modified — inject Clock, start/stop timer per stream)
src/application/src/chat_controller.cpp                              (modified)
src/application/src/chat_view_model.cpp                               (modified — call repository_->persistUsage() alongside persistMessageSettled())
```

No new QML-visible type is added (REQ-C-001) — `ChatViewModel`/`MessageListModel` gain no new
Q_PROPERTY. The only application-layer change is: (a) `ChatController` owns the wall-clock timer
per in-flight stream (§2, §6.3) since it is the one place that knows both "when `sendChat()` was
dispatched" and "which terminal event fired," and (b) `ChatViewModel::onStreamEvent()`'s existing
`isTerminal` branch (chat_view_model.cpp:731-738, right where `persistMessageSettled()` is already
called) also extracts `Usage` from the terminal event and calls the new
`ConversationRepository::persistUsage(...)`.

---

## 2. Data flow

```
 ChatController::startStream()
   │  clock_->now() → dispatch_time  (REQ-NF-001)
   ▼
 <Provider>::sendChat()
   │  StreamContext.usage accumulates as SSE/NDJSON events arrive
   │  (Anthropic: message_start partial → message_delta authoritative, §6.1
   │   Google: usageMetadata on every chunk, last chunk wins, §6.2
   │   OpenAI: single usage object on response.completed
   │   Ollama: single usage block on done:true, plus native duration fields)
   ▼
 routeSseEvent() / processLine() reaches a terminal condition
   │  builds StreamEvent{Completed{.usage = context->usage}}   (or Error{...}/Cancelled{...})
   ▼
 ChatController::handleStreamEvent()  [chat_controller.cpp std::visit]
   │  clock_->now() → completion_time
   │  duration_ms = completion_time - dispatch_time  (REQ-F-011, REQ-NF-001)
   │  stamps duration_ms into the Usage carried by the terminal event before forwarding it
   │  (ChatController::stop() does the same for the synthetic Cancelled it emits)
   ▼
 ChatViewModel::onStreamEvent()  [chat_view_model.cpp:719]
   │  existing isTerminal branch already fires persistMessageSettled(); now also:
   │  extracts std::optional<Usage> from the terminal StreamEvent
   │  extracts provider-reported model identifier (response.model / message.model /
   │    modelVersion / Ollama's echoed "model" field) captured earlier by the adapter
   │  if the settled message is Assistant and Usage is present (even partial):
   ▼
 ConversationRepository::persistUsage(conversationId, messageId, modelIdentifier, usage)
   │  (worker thread, same QueuedConnection pattern as persistNewMessage/persistMessageSettled)
   ▼
 ConversationRepositoryWorker::persistUsage()
   │  looks up PriceTableRepository (injected at ChatViewModel construction, same lifetime as
   │    ConfigRepository) for modelIdentifier's per-category USD/1M prices
   │  computes estimated_cost = Σ(tokens_i × price_i) over present (non-null) fields only (REQ-F-008)
   │  INSERT INTO usage (...) — one row, cost frozen at this moment (REQ-F-009)
```

Key point: **no new signal/slot pair is introduced.** `persistUsage()` piggybacks on the exact
call site that already exists for `persistMessageSettled()`, so a user message never reaches it
(REQ-F-010) — that branch already only fires for terminal transitions.

---

## 3. Interfaces / APIs

### 3.1 `holonight_domain::Usage` (in `stream_event.h`)

```cpp
struct Usage {
  std::optional<int> input_tokens{};
  std::optional<int> output_tokens{};
  std::optional<int> reasoning_tokens{};
  std::optional<int> cache_creation_tokens{};
  std::optional<int> cache_read_tokens{};
  std::optional<int> total_tokens{};

  // Canonical cross-provider metric (REQ-F-011). Populated by ChatController, not the adapters —
  // no adapter has meaningful access to "when did ChatController::sendChat() get invoked."
  std::optional<qint64> duration_ms{};

  // Ollama-only supplementary fields (REQ-F-012). Left unset by the other three adapters.
  std::optional<qint64> ollama_total_duration_ns{};
  std::optional<qint64> ollama_load_duration_ns{};
  std::optional<qint64> ollama_prompt_eval_duration_ns{};
  std::optional<qint64> ollama_eval_duration_ns{};

  friend bool operator==(const Usage&, const Usage&) = default;
};

struct Completed {
  std::optional<Usage> usage{};
  friend bool operator==(const Completed&, const Completed&) = default;
};

struct Error {
  QString message{};
  std::optional<Usage> usage{};
  friend bool operator==(const Error&, const Error&) = default;
};

struct Cancelled {
  std::optional<Usage> usage{};
  friend bool operator==(const Cancelled&, const Cancelled&) = default;
};
```

`ContentDelta` unchanged. Placing the four Ollama-native fields directly on `Usage` (rather than a
nested `OllamaTiming` sub-struct) mirrors the flat, non-nested style already used by every existing
domain struct in this file — nesting would be the only nested struct in `stream_event.h` for four
fields that only one of four providers ever populates.

### 3.2 Injectable clock (`holonight_providers::Clock`, `clock.h`)

Mirrors `HttpClient`'s shape exactly (pure interface, `send`-style single responsibility, a real
implementation + an injectable seam):

```cpp
class Clock {
 public:
  Clock() = default;
  virtual ~Clock() = default;
  Clock(const Clock&) = delete;
  Clock& operator=(const Clock&) = delete;
  Clock(Clock&&) = delete;
  Clock& operator=(Clock&&) = delete;

  virtual std::chrono::milliseconds now() const = 0;
};

class SteadyClock : public Clock {
 public:
  std::chrono::milliseconds now() const override;  // std::chrono::steady_clock::now(), production default
};
```

`ChatController` is constructed with a `std::shared_ptr<Clock>` (defaulting to `SteadyClock` at
the `apps/chat` composition root, exactly where `HttpClient` implementations are already wired
up), storing `dispatch_time_` per in-flight stream inside the existing `InFlightStream` struct and
computing `duration_ms` at whichever terminal branch of `handleStreamEvent()`'s `std::visit` fires
(including the synthetic `Cancelled` built in `ChatController::stop()`). A test-only `FakeClock`
(mirroring `tests/providers/fake_http_client.h`'s naming) returns queued or fixed millisecond
values.

### 3.3 `holonight_config::PriceTableRepository`

```cpp
struct TokenPricing {
  std::optional<double> input_usd_per_million{};
  std::optional<double> output_usd_per_million{};
  std::optional<double> reasoning_usd_per_million{};
  std::optional<double> cache_creation_usd_per_million{};
  std::optional<double> cache_read_usd_per_million{};
};

struct PriceTable {
  QHash<QString, TokenPricing> models;  // keyed by exact model identifier string
};

class PriceTableRepository {
 public:
  explicit PriceTableRepository(QString file_path);  // same DI-for-tests shape as ConfigRepository

  [[nodiscard]] PriceTable load() const;  // never throws; missing/malformed file ⇒ empty PriceTable
  [[nodiscard]] std::optional<TokenPricing> pricingFor(const PriceTable& table, const QString& model_identifier) const;

 private:
  QString file_path_;
};
```

Synchronous GUI-thread JSON I/O, loaded once at startup (REQ-F-007) — same rationale
`ConfigRepository`'s header already documents for why it does *not* follow the worker-thread
precedent: this file is small, read-mostly, and not on any user-facing latency path.
`pricingFor()` returning `std::nullopt` for an unlisted model (not a zero-valued `TokenPricing`)
is what lets the cost-computation step distinguish "no data" from "verified zero" (REQ-F-007's
acceptance criterion).

### 3.4 Persistence: `ConversationRepository::persistUsage()`

Added to the existing abstract interface (`conversation_repository.h`), alongside
`persistMessageSettled`:

```cpp
virtual void persistUsage(QString conversationId, QString messageId, QString modelIdentifier,
                          holonight_domain::Usage usage) = 0;
```

Fire-and-forget, matching every other repository method's "void now, signal later or silently
no-op on non-fatal error" convention — no new success signal is added since nothing in the UI
observes usage rows this cycle (REQ-C-001). A `usageForConversation(QString conversationId)` /
`usageForMessage(QString messageId)` *read* pair is **not** added in this cycle — no consumer
exists yet (QML rollups are explicitly deferred, REQ-C-001), and REQ-NF-002's "auditability"
acceptance criterion is satisfiable by direct SQL/sqlite3 inspection plus the unit-level repository
round-trip test in §9. Stage 4 may still add a minimal internal getter if the persistence test
needs one to assert the round trip — see §9.4.

`ConversationRepositoryWorker::persistUsage()` (worker thread) resolves cost via an injected
`holonight_config::PriceTable` snapshot (handed to the worker once at construction, same as the
`databasePath` constructor argument — a config reload requires reconstructing the repository, which
is an acceptable limitation since REQ-C-006 already restricts price-table edits to manual restarts
of the "advanced user" workflow, not live-reload).

---

## 4. Database schema

New migration, additive-only (REQ-C-004), following the exact numbering/naming/embedding
convention of `0001_init.sql` → `0003_add_title_source.sql`:

```
src/persistence/migrations/0004_add_usage.sql
```

```sql
CREATE TABLE usage (
    id TEXT PRIMARY KEY,
    message_id TEXT NOT NULL,
    conversation_id TEXT NOT NULL,
    model_identifier TEXT NOT NULL,
    input_tokens INTEGER,
    output_tokens INTEGER,
    reasoning_tokens INTEGER,
    cache_creation_tokens INTEGER,
    cache_read_tokens INTEGER,
    total_tokens INTEGER,
    duration_ms INTEGER,
    ollama_total_duration_ns INTEGER,
    ollama_load_duration_ns INTEGER,
    ollama_prompt_eval_duration_ns INTEGER,
    ollama_eval_duration_ns INTEGER,
    estimated_cost_usd REAL,
    created_at TIMESTAMP NOT NULL,
    FOREIGN KEY (message_id) REFERENCES messages(id) ON DELETE CASCADE
);

CREATE UNIQUE INDEX idx_usage_message_id ON usage(message_id);
CREATE INDEX idx_usage_conversation_id ON usage(conversation_id);
```

Notes:

- All `INTEGER`/`REAL` token/cost columns are nullable (no `NOT NULL`, no `DEFAULT 0`) — SQLite
  `NULL` is the direct storage of `std::optional`'s empty state (REQ-F-002, REQ-NF-002). Binding an
  unset `std::optional<int>` as `QVariant()` (the same idiom `persistNewMessage()` already uses for
  `model_id`) round-trips correctly.
- `id` is a fresh generated identifier (same `QUuid`/`MessageId`-style opaque string convention as
  every other primary key in this schema), not reused from `message_id`, so a future
  re-derivation/audit tool (REQ-F-009's acceptance criterion) can append a second historical
  cost-recompute row without violating a natural key — though this cycle only ever writes one row
  per message (REQ-F-010's 1:1 mapping is enforced at the call site, not by a DB constraint, mirroring
  how `persistNewMessage`/`persistMessageSettled` already rely on call-site discipline rather than
  DB triggers for message lifecycle invariants).
- `conversation_id` is denormalized onto the row (recoverable via a `messages` join, but every
  historical/rollup query this table will ever serve is "usage for conversation X," so avoiding
  that join on the hot read path is worth the redundancy — same trade-off `messages.conversation_id`
  itself already makes relative to being derivable from `conversations`).
- `model_identifier` is `NOT NULL` — REQ-F-010 requires it always be captured; a `Usage` with zero
  populated token fields can still legitimately arrive (e.g., a request that fails before any usage
  is reported), but the row is only written at all when a model identifier is known.
- No backfill statement — migration 0004 only creates the table (REQ-C-004).

---

## 5. Price table JSON schema

New file, resolved by `holonight_config::resolvePriceTableFilePath()`
(`$XDG_CONFIG_HOME/holonight-ai/price_table.json`, built with the identical
`QStandardPaths::GenericConfigLocation` + `mkpath` idiom as `resolveConfigFilePath()`):

```json
{
  "version": 1,
  "models": {
    "gpt-4-turbo-2024-04-09": {
      "input_usd_per_million": 10.0,
      "output_usd_per_million": 30.0
    },
    "claude-3-5-sonnet-20241022": {
      "input_usd_per_million": 3.0,
      "output_usd_per_million": 15.0,
      "cache_creation_usd_per_million": 3.75,
      "cache_read_usd_per_million": 0.3
    },
    "gemini-2.5-pro": {
      "input_usd_per_million": 1.25,
      "output_usd_per_million": 10.0,
      "reasoning_usd_per_million": 10.0,
      "cache_read_usd_per_million": 0.3125
    },
    "llama3.1:8b": {
      "input_usd_per_million": 0.0,
      "output_usd_per_million": 0.0
    }
  }
}
```

- Top-level `"version"` is reserved for a future non-additive schema change (not consulted by this
  cycle's loader, which tolerates its absence) — same forward-compatibility placeholder style as
  `schema_version` in the SQLite migrations, but a plain JSON field here since this file has no
  migration runner of its own.
- Keys under `"models"` are exact provider-returned model identifiers (REQ-F-010) — e.g.
  `gpt-4-turbo-2024-04-09`, not the user-facing alias `gpt-4-turbo` — since that is the same string
  `persistUsage()` receives and looks up with. This is called out prominently in an inline JSON
  comment... except JSON has no comments, so it is documented in this DESIGN.md and (Stage 4) in a
  short header comment on `price_table.h` and the file's own sibling
  `docs/provider-configuration.md`-style note, since REQ-C-006 makes this file hand-edited by
  advanced users who need to know which string to key on.
- Every price field is optional; an absent field means "unknown for this category," not
  "$0/token" — `llama3.1:8b`'s explicit `0.0` values are how a user asserts a verified free/local
  model, distinct from simply omitting the model entirely (REQ-F-007's null-vs-zero distinction
  applies symmetrically to prices, not just token counts).
- No `total_usd_per_million` field — `total_tokens` in `Usage` is a token-count sum only; SPEC's
  REQ-F-008 cost formula sums `tokens × price` per category, so a redundant total-price field would
  invite double-counting.

---

## 6. Key decisions with rationale

**6.1 — Usage as an optional field on terminal variants, not a new `StreamEvent` variant
(REQ-F-001).** A new `Usage` variant would force every consumer's `std::visit` (currently
exhaustive over 4 arms in `ChatController::handleStreamEvent` and 2 arms in
`ChatViewModel::onStreamEvent`) to add a 5th arm whose only job is "attach this data to whatever
terminal event I already have in flight," reintroducing state-tracking machinery the SPEC's own
acceptance criterion explicitly wants to avoid (a `Cancelled` with partial usage attached directly,
not a `Cancelled` followed by a separate `Usage` event racing against the UI already having
transitioned the message to `Cancelled` status). An optional field composes with the existing
terminal-only trio with zero new `std::visit` arms.

**6.2 — Anthropic accumulator: `message_delta` overwrites, never sums, `message_start`'s
`output_tokens` (REQ-F-005).** Anthropic's own API contract defines `message_delta.usage` as the
cumulative final count, not a delta-of-a-delta despite the event's name — summing would double
count. The `StreamContext.usage` accumulator therefore *replaces* `output_tokens` (and adds
`cache_creation_tokens`/`cache_read_tokens`, which only ever appear in `message_delta`) rather than
adding to whatever `message_start` set. This mirrors the file's existing `stop_reason` field, which
is also last-write-wins across multiple `message_delta` events, not accumulated.

**6.3 — Google: last chunk's `usageMetadata` wins, not a running sum (REQ-F-006).** Confirmed via
Gemini API docs (`ai.google.dev/gemini-api/docs/generate-content/text-generation`): every streamed
chunk carries a *cumulative* `usageMetadata` snapshot (and a `modelVersion` field used for REQ-F-010's
model-identifier capture), not a per-chunk delta — so `StreamContext.usage` is simply overwritten
on every chunk that contains `usageMetadata`, and whatever value survived until the terminal
`finishReason: STOP` chunk is authoritative, matching REQ-F-006's acceptance criterion.

**6.4 — Wall-clock timing lives in `ChatController`, not per-provider adapters
(REQ-F-011/NF-001).** Only `ChatController::startStream()` know both endpoints of "time to
generate": it calls `dispatchSendChat()` (the moment of dispatch) and its own
`handleStreamEvent()` sees every terminal event first, before forwarding to the caller-supplied
`on_event`. Putting the timer in each of the four adapters would require duplicating
start/stop bookkeeping four times and would still need the same dispatch-to-terminal window
`ChatController` already owns end-to-end; putting it there once is strictly less code and is the
one existing call site every provider already funnels through (REQ-C-001 in the SPEC's own module
list already documents `ChatController` as the shared orchestration seam other cycles route
through).

**6.5 — Ollama's server timing fields preserved as separate, clearly-named `ollama_*` fields, never
substituted for `duration_ms` (REQ-F-012).** Naming them `ollama_prompt_eval_duration_ns` etc.
(rather than generic `server_duration_ns`) makes misuse (e.g. accidentally comparing an Ollama
compute-only duration against another provider's network-inclusive `duration_ms` in a future
rollup view) a naming-level tell, not just a comment-level warning — satisfies the SPEC's explicit
"preventing silent conflation" acceptance criterion.

**6.6 — Cost frozen at write time; `estimated_cost_usd` computed once inside
`ConversationRepositoryWorker::persistUsage()`, never recomputed on read (REQ-F-009/C-005).** The
worker resolves pricing from the `PriceTable` snapshot it was constructed with and writes the
result directly into the row. No `estimated_cost` is ever recalculated by a read path — the only
way a row's cost changes is a full re-derivation utility (out of scope this cycle) that would
explicitly `UPDATE` using stored raw token counts, matching REQ-F-009's "re-derivable but not
auto-applied" acceptance criterion.

**6.7 — No cross-turn deduplication anywhere in the design (REQ-F-008/NF-003).** There is
deliberately no aggregation table, no distinct-token cache, and no "total conversation cost" column
on `conversations` — total cost is left as a pure `SUM(estimated_cost_usd) WHERE conversation_id =
?` query against the per-turn `usage` rows, computed by a future UI/reporting layer, not persisted
redundantly. This keeps the invariant machine-enforced by construction (there is nothing to
accidentally "optimize" into a deduplicated form) rather than relying on a comment warning against
it.

**6.8 — Price table is a separate JSON file, not a new section of `config.json`
(REQ-F-007/C-006).** `ConfigRepository::writeRootObject()` does a full read-modify-write of the
shared `config.json` root object on every provider/utility settings save. A hand-edited pricing
section living in that same file would be at risk of being silently reformatted (re-indented,
key-order-normalized) every time the user changes an unrelated provider setting, and — more
importantly — advanced users editing prices need a file they can find, back up, and diff
independently of the app's own frequently-rewritten settings state. A dedicated
`price_table.json`, loaded once at startup by `PriceTableRepository` (not `ConfigRepository`),
avoids all of that.

---

## 7. Alternatives considered

- **New `Usage` `StreamEvent` variant instead of an optional field on the terminal three.**
  Rejected — see §6.1. Would also break `REQ-F-001`'s literal wording ("the system SHALL attach
  ... rather than introducing a new Usage variant").
- **Live re-pricing (recompute `estimated_cost` on every read against the current price table).**
  Rejected by REQ-C-005/REQ-F-009 directly — historical invoices would silently drift if a user
  edited prices, defeating the entire point of an auditable cost log. Frozen-at-write is the only
  option that satisfies "matches the provider's invoice for that turn."
  - **Server-native timing (Ollama's `total_duration`, or a hypothetical per-provider
  "time-to-first-token") as the canonical duration metric**, instead of client-side wall clock.
  Rejected because only Ollama exposes server-side timing at all — OpenAI/Anthropic/Google's
  streaming responses carry no server-measured duration field, so a "canonical = server-native"
  design would have three providers with no canonical value and Ollama with an
  incomparable-to-the-others one. Uniform client wall-clock (REQ-F-011) is the only metric all four
  can report on equal terms; Ollama's richer server data is kept as supplementary detail (§6.5),
  not discarded.
- **Deduplicating tokens across turns for a "total unique tokens" metric.** Rejected by
  REQ-NF-003 — explicitly the wrong number for invoice-matching, since every provider re-bills the
  full resent history on every turn.
- **Storing the price table inside `config.json`'s existing root object (new `"pricing"` key).**
  Rejected — see §6.8.
- **A `usage` row keyed by `message.id` directly as its primary key** (instead of a fresh
  generated `id` with a unique index on `message_id`). Considered simpler, but rejected because it
  forecloses a future re-derivation/audit feature (REQ-F-009's acceptance criterion literally
  describes "a separate re-derivation function... to verify the calculation is reproducible") ever
  appending a second row per message without a schema change; a synthetic PK plus a `UNIQUE INDEX`
  enforces today's 1:1 invariant at effectively the same cost while leaving that door open.

---

## 8. Known risks

- **Anthropic's two-phase usage reporting is easy to get backwards.** A naive accumulator that
  *adds* `message_delta.output_tokens` to `message_start.output_tokens` instead of replacing it
  would silently double the true count on every response — REQ-F-005's acceptance criterion exists
  specifically to catch this. Mitigation: the GTest in §9.1 asserts the non-summed value explicitly,
  not just "some positive number."
- **Google's per-chunk `usageMetadata` requires "last chunk wins," and an implementer who assumes
  the *first* chunk with `usageMetadata` is authoritative (as one might reasonably assume for a
  field named like a running total) will under-report every response that streams more than one
  chunk.** Mitigation: §9.2's test explicitly sends ≥2 chunks with differing `usageMetadata` values
  and asserts only the last one is used.
- **Price table drift on model rename.** If a provider renames a model string (e.g. a dated
  suffix rotates, `gpt-4-turbo-2024-04-09` → a new dated variant), `pricingFor()` returns
  `std::nullopt` for every new response until a human updates `price_table.json` — by design
  (REQ-F-007 forbids silently substituting $0), but it means cost silently stops being computed
  with no in-app signal this cycle (no UI exists yet to surface "N models unpriced" — that is
  natural follow-on-cycle scope, consistent with REQ-C-001/006).
- **Floating-point cost precision.** `estimated_cost_usd REAL` (SQLite's only floating type) can
  accumulate representation error across a long conversation's many small per-turn costs if a
  future feature ever sums stored costs rather than recomputing from raw tokens. Mitigation: raw
  token counts are always persisted alongside the derived cost (REQ-NF-002), so any
  precision-sensitive future rollup can re-derive from integers rather than trusting accumulated
  `REAL` sums; this cycle does not attempt any such rollup itself.
- **Gemini's `modelVersion` field (used for REQ-F-010's model-identifier capture) was verified
  against current Gemini API documentation during this design (see docs citation in §6.3) rather
  than against a live response capture** — Stage 4's mock-response fixtures should still be built
  from a real captured Gemini stream where possible, since documentation and wire behavior can
  drift.
- **`PriceTableRepository`'s snapshot is loaded once per `ConversationRepositoryWorker`
  construction, not live-reloaded.** A user who hand-edits `price_table.json` mid-session (REQ-C-006's
  only supported editing path) will not see new prices apply until the app restarts. This is
  consistent with REQ-C-006 not requiring a live-reload UX, but is worth flagging as a
  discoverability gap if a future support request surfaces it.

---

## 9. Testing strategy

All new tests are GTest, run via `task test` / `ctest -R <pattern>`, placed alongside each
touched module's existing test directory (`tests/domain`, `tests/providers`, `tests/config`,
`tests/persistence`, `tests/application`), matching the project's existing one-directory-per-module
convention.

**9.1 Provider usage-parsing tests** (`tests/providers/test_{ollama,openai,anthropic,google}_provider.cpp`,
extending the existing `FakeHttpClient`-based suites):
- One `TEST(<Provider>, SendChatPopulatesUsageFromXxx)` per adapter, feeding the exact mock
  payloads from the SPEC's own acceptance criteria (e.g. Anthropic's `message_start` +
  `message_delta` sequence from REQ-F-005) through `sendChat()`, asserting the terminal event's
  `Usage` fields with exact `EXPECT_EQ`, mirroring the existing `SendChatMapsErrorFieldToStreamError`-style
  tests already in each file.
- A "field omitted ⇒ null, not zero" test per provider (REQ-F-002's acceptance criterion), e.g. a
  Google response with no `thoughtsTokenCount` key asserts `usage.reasoning_tokens ==
  std::nullopt`, distinct from a response with `"thoughtsTokenCount": 0`.
- Anthropic-specific: a test asserting `message_delta`'s value *replaces* rather than adds to
  `message_start`'s partial `output_tokens` (directly encodes REQ-F-005's acceptance criterion).
- Google-specific: a multi-chunk test where an earlier chunk's `usageMetadata` differs from the
  final chunk's, asserting only the final chunk's values survive (REQ-F-006).
- A partial-usage-on-cancel test: start a stream, let one usage-bearing chunk arrive, then invoke
  cancellation before the terminal event — assert the resulting `Cancelled` (or, for adapters where
  cancellation is only synthesized by `ChatController`, verify the adapter's accumulator itself
  holds the partial values ready to be read) carries non-null partial fields (REQ-F-001's
  acceptance criterion).

**9.2 Injectable clock tests** (`tests/providers/fake_clock.h`, new, alongside the existing
`fake_http_client.h`, plus assertions in `tests/application/test_chat_controller.cpp` — new file
if one doesn't already exist, else extending the existing controller test):
- `FakeClock` returns a queued sequence of fixed millisecond values (`FakeClock::push(1000);
  FakeClock::push(2500);`). A `ChatController` constructed with it, sending one message through a
  `FakeHttpClient` that completes synchronously, asserts the terminal event's `Usage.duration_ms ==
  1500` exactly — no `> 0` fuzz, directly satisfying REQ-NF-001's acceptance criterion.
- A default-construction test confirms `ChatController`'s no-clock-supplied constructor path uses
  `SteadyClock`, and a real (non-mocked) send produces `duration_ms > 0` (loose bound is
  appropriate only for this one production-default smoke test, not the exact-value tests above).

**9.3 Price table / cost computation tests** (`tests/config/test_price_table_repository.cpp`, new):
- Loads a temp-dir JSON fixture matching REQ-F-007's acceptance criterion (three models,
  including an intentionally-unlisted one) and asserts `pricingFor()` returns populated data for
  listed models and `std::nullopt` for the unlisted one — never a zero-valued `TokenPricing`.
- A missing-file and a malformed-JSON case both assert `load()` returns an empty `PriceTable`
  without throwing (mirrors `ConfigRepository::readRootObject()`'s existing "never throws, empty
  object on failure" tests).
- A cost-computation unit test (likely co-located with the persistence round-trip test in §9.4,
  since REQ-F-008's formula is exercised at the point it's actually invoked — inside
  `ConversationRepositoryWorker::persistUsage()`) verifies the no-deduplication formula: two
  sequential turns with the same model sum to `cost1 + cost2`, not a deduplicated value
  (REQ-F-008's second acceptance criterion).

**9.4 Persistence round-trip tests** (`tests/persistence/test_conversation_repository.cpp`,
extending the existing suite that already exercises `persistNewMessage`/`persistMessageSettled`
against a real temp-file SQLite database):
- `persistUsage()` on an assistant message, then a direct `QSqlQuery` against the `usage` table
  (the existing test file's established pattern for asserting persisted state, since no public
  read API is added per §3.4) confirms every column round-trips, including `NULL` for every unset
  `std::optional` field (REQ-NF-002's acceptance criterion).
- Migration test: applying migrations 0001-0004 in order against a fresh in-memory/temp database
  succeeds and `usage` exists with the expected columns — same style as any existing
  `MigrationRunner::apply` test.
- Cost-freezing test (REQ-F-009/C-005's acceptance criterion): persist a usage row through a
  `PriceTableRepository` snapshot with price X, assert `estimated_cost_usd`; construct a second
  worker/repository with a price table snapshot at price 2X; assert the original row's
  `estimated_cost_usd` is unchanged after re-reading it, while a newly persisted row under the
  second snapshot uses the new price.
- One-row-per-assistant-message test: drive a conversation through several
  user/assistant exchanges via the fixture used elsewhere in this file, assert exactly N usage rows
  for N assistant messages and zero rows attributable to user messages (REQ-F-010).

**9.5 Domain tests** (`tests/domain/test_stream_event.cpp`, extended):
- New cases mirroring the existing `HoldsCompleted`/`CompletedInstancesAlwaysEqual`-style tests,
  but asserting `Completed{.usage = Usage{...}}` construction, equality, and that a
  default-constructed `Completed{}` still has `usage == std::nullopt` (backward-compatible default,
  since existing call sites across the codebase construct `Completed{}` with no arguments and must
  keep compiling unchanged).
