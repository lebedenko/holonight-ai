# SPEC: Per-Provider, Per-Model Token Usage and Cost Tracking

## Scope Revision (2026-08-03)

**The cost/pricing half of this cycle has been removed** (`response-footer-stats`, the UI follow-on
cycle this SPEC deferred to, is where the gap surfaced: every provider showed Unknown cost, not just
Ollama). Live requests against all four providers' chat-completion and models-list endpoints
confirmed none expose pricing through any API — `holonight_config::PriceTable`/`PriceTableRepository`
and `price_table.json` (REQ-F-007/REQ-F-008/REQ-C-006 below) were the only source, and a
hand-maintained JSON file was judged an unacceptable UX rather than a gap worth patching further.
`PriceTable`, `PriceTableRepository`, `computeCostBreakdown()`/`evaluateCategory()`, `estimated_cost_usd`
and all per-category cost columns have been deleted (migration `0006_drop_usage_cost_columns.sql`
drops what migrations `0004`/`0005` added, forward-only — those historical migrations are unedited).
**REQ-F-007, REQ-F-008, REQ-F-009's cost-computation clause, and REQ-C-005/REQ-C-006 below no longer
apply** (kept for historical record). Usage/token tracking itself (REQ-F-001 through REQ-F-006,
REQ-F-010 through REQ-F-012) is unaffected — token counts, durations, and their persistence remain
exactly as specified.

## Overview & Context

This cycle closes the long-standing deferred requirement **REQ-F-017** (token usage tracking, deferred across three prior SDD cycles: anthropic-provider-adapter, google-provider-adapter, openai-provider-adapter). All four implemented provider adapters—Ollama, OpenAI (Responses API), Anthropic (Messages API), and Google (Gemini)—parse usage data from their streaming responses but currently discard it due to a domain model gap: `holonight_domain::StreamEvent` has no place to carry usage information.

This cycle spans a horizontal cut across:
- `holonight_domain` — introducing the `Usage` type and attaching usage fields to terminal stream events
- All four provider adapters (`holonight_providers`) — accumulating and reporting usage data
- `holonight_persistence` — storing usage per message (via SQLite migration)
- `holonight_config` — maintaining a price table and computing estimated costs

**Scope decision:** This cycle covers the backend data model, provider parsing, and persistence layers. QML/UI surfacing (any visible running-cost display, per-provider/per-model rollup views, or budget-threshold warnings) is explicitly deferred to a follow-on cycle, consistent with this project's established precedent of splitting provider/data backend cycles from later UI cycles (e.g., Ollama chat backend → chat-window-qml).

---

## Functional Requirements

### Domain Model: Usage Structure

**REQ-F-001 — Ubiquitous: Usage optional on terminal variants**

The system SHALL define a `holonight_domain::Usage` type carrying token counts and timing data. The system SHALL attach an `std::optional<Usage>` field to each of the three terminal `StreamEvent` variants (`Completed`, `Cancelled`, and `Error`) rather than introducing a new Usage variant. Each adapter SHALL accumulate usage internally as its stream progresses and attach whatever usage it has—zero, partial, or final—to whichever terminal event actually fires.

*Acceptance criterion:* The generated `.gen` domain header file declares `Usage` with the fields specified in REQ-F-002, and the three terminal variants each have an `std::optional<Usage>` member. A provider adapter that accumulates partial usage and then cancels the request can produce a `Cancelled` event with non-null usage fields populated.

---

**REQ-F-002 — Ubiquitous: Usage field coverage**

The system SHALL define the following fields in the `Usage` type, each independently optional (`std::optional<int>` or equivalent), to accommodate per-provider reporting differences:
- `input_tokens` — count of tokens consumed from the input/prompt
- `output_tokens` — count of tokens generated in the response
- `reasoning_tokens` — count of tokens used for extended reasoning/thinking (null/empty if not present)
- `cache_creation_tokens` — count of tokens written to a provider's cache (null/empty if not present)
- `cache_read_tokens` — count of tokens satisfied from a provider's cache (null/empty if not present)
- `total_tokens` — sum of input + output + reasoning (provider-reported or computed; null if provider does not report or compute it)

Each field SHALL tolerate "provider does not report this field" (null/absent) as distinct from "provider reported zero." The system SHALL NOT substitute missing prices or token counts with `$0` or `0`—absence must remain distinguishable from zero.

*Acceptance criterion:* A test case loads a provider response that omits a field (e.g., Google Gemini without thinking tokens) and verifies that the corresponding Usage field is null/unset, distinct from a response that explicitly reports zero.

---

### Per-Provider Usage Parsing

**REQ-F-003 — Event-driven: Ollama usage parsing**

WHEN an Ollama provider adapter receives the final `done:true` chunk in a streaming response, the system SHALL extract `prompt_eval_count` (input tokens), `eval_count` (output tokens), `total_duration`, `load_duration`, `prompt_eval_duration`, and `eval_duration` (all nanoseconds, server-measured) from the response. The system SHALL populate `input_tokens`, `output_tokens`, and `total_tokens` fields. The system SHALL preserve the server-measured timing fields as supplementary/bonus data (see REQ-NF-003 for canonical timing methodology).

*Acceptance criterion:* A mock Ollama response with `"done": true, "prompt_eval_count": 42, "eval_count": 18, "total_duration": 5000000000` populates Usage with `input_tokens=42`, `output_tokens=18`, and the final Completed event carries this Usage. A test asserts the exact values.

---

**REQ-F-004 — Event-driven: OpenAI usage parsing**

WHEN an OpenAI Responses API adapter receives the `response.completed` event in a streaming response (`openai_provider.cpp`'s existing `routeSseEvent` handler, which already branches on `type == "response.completed"`), the system SHALL extract the `usage` object nested under the event's `response` field (matching the same `response.<field>` nesting already used by the adjacent `response.failed`/`response.incomplete` handling for `response.error`), containing `input_tokens`, `output_tokens`, `total_tokens`, `input_tokens_details.cached_tokens`, `input_tokens_details.cache_write_tokens`, and `output_tokens_details.reasoning_tokens`. The system SHALL populate the corresponding `Usage` fields, mapping cache/reasoning details into `cache_read_tokens` and `reasoning_tokens` respectively.

*Acceptance criterion:* A mock OpenAI SSE stream ending with `{"type": "response.completed", "response": {"usage": {"input_tokens": 50, "output_tokens": 25, "total_tokens": 75, "input_tokens_details": {"cached_tokens": 10}, "output_tokens_details": {"reasoning_tokens": 5}}}}` produces a Completed event with Usage containing `input_tokens=50`, `output_tokens=25`, `cache_read_tokens=10`, `reasoning_tokens=5`. A test asserts all fields.

---

**REQ-F-005 — Event-driven: Anthropic usage parsing**

WHEN an Anthropic Messages API adapter receives SSE `message_start` and `message_delta` events in a streaming response, the system SHALL handle the two-part usage reporting: `message_start` reports initial `input_tokens` and partial `output_tokens`; `message_delta` (before `message_stop`) reports the final cumulative `output_tokens`, `cache_creation_input_tokens`, `cache_read_input_tokens`, and output-token details. The adapter SHALL accumulate usage internally and use the `message_delta` value as authoritative/final. The system SHALL map Anthropic's cache fields to `cache_creation_tokens` and `cache_read_tokens` respectively.

*Acceptance criterion:* A mock Anthropic SSE stream with `message_start` reporting `input_tokens=40, output_tokens=1` followed by `message_delta` reporting `output_tokens=20, cache_read_input_tokens=5` produces a Completed event with final Usage `input_tokens=40, output_tokens=20, cache_read_tokens=5`. A test verifies that the message_start value is overridden by message_delta, not summed.

---

**REQ-F-006 — Event-driven: Google Gemini usage parsing**

WHEN a Google Gemini adapter receives SSE chunks with `usageMetadata` in a streaming response, the system SHALL extract `promptTokenCount` (input), `candidatesTokenCount` (output), `thoughtsTokenCount` (reasoning/thinking), `cachedContentTokenCount`, and `totalTokenCount` from the final chunk (values are cumulative-final per chunk; the last chunk's value is authoritative). The system SHALL map these to `input_tokens`, `output_tokens`, `reasoning_tokens`, `cache_read_tokens`, and `total_tokens` respectively.

*Acceptance criterion:* A mock Google Gemini SSE stream with multiple chunks, the last carrying `{"usageMetadata": {"promptTokenCount": 30, "candidatesTokenCount": 15, "thoughtsTokenCount": 8, "cachedContentTokenCount": 2, "totalTokenCount": 55}}` produces a Completed event with Usage `input_tokens=30, output_tokens=15, reasoning_tokens=8, cache_read_tokens=2, total_tokens=55`. A test verifies that only the final chunk's values are used.

---

### Cost Computation & Persistence

**REQ-F-007 — Ubiquitous: Price table definition and loading**

The system SHALL maintain a user-editable price table as a JSON file at a location determined by `holonight_config` (consistent with existing provider-instance config conventions). The file SHALL define USD-denominated prices per token category (input, output, reasoning, cache-creation, cache-read) for each model, at a granularity of price-per-1M-tokens. The system SHALL load this price table on application startup and make it available to the cost-computation subsystem. If a model has no entry in the price table, the system SHALL treat its cost as unknown (null), not as `$0`.

*Acceptance criterion:* A sample price table JSON with entries for `gpt-4-turbo` (input: $0.01, output: $0.03), `claude-3-opus` (input: $0.015, output: $0.075), and `llama-2-7b` (zero price, Ollama self-hosted) loads without error. A lookup for `claude-3-opus.input_tokens` returns the correct price; a lookup for an unlisted model returns null, not zero.

---

**REQ-F-008 — Ubiquitous: Per-turn cost computation (no deduplication)**

The system SHALL compute the estimated cost for each chat turn as the sum of (`input_tokens × input_price` + `output_tokens × output_price` + `reasoning_tokens × reasoning_price` + `cache_creation_tokens × cache_creation_price` + `cache_read_tokens × cache_read_price`), using the price table. The system SHALL NOT deduplicate tokens across turns—since every provider resends the full conversation as input on every turn, summing per-turn cost is the number that matches the provider's invoice. The total cost for a chat is the sum of all per-turn costs.

*Acceptance criterion:* A chat with two turns, both using the same model and price table, computes cost1 = (input1_tokens + input2_tokens) × input_price + (output1_tokens + output2_tokens) × output_price (not deduplicated). A second test verifies that the total chat cost equals cost1 + cost2, not a deduplicated per-unique-token value.

---

**REQ-F-009 — Ubiquitous: Cost freezing at write time**

The system SHALL compute and persist `estimated_cost` at the moment the usage row is written to the database, using the price-table entry active at that moment. The system SHALL NOT recompute historical costs if the price table is edited afterward. Raw token counts (`input_tokens`, `output_tokens`, etc.) SHALL also be persisted alongside `estimated_cost` so the cost calculation remains auditable and re-derivable if pricing changes in the future.

*Acceptance criterion:* A usage row is written with `input_tokens=50, estimated_cost=$0.50` using price table version 1. The price table is then edited to double input prices. A historical cost report re-reads the same row and confirms `estimated_cost=$0.50` (frozen, not recomputed). A separate re-derivation function uses the stored token counts and an updated price table to verify the calculation is reproducible but not auto-applied.

---

**REQ-F-010 — State-driven: One usage row per assistant message**

IF a message is an assistant response (i.e., generated by a provider model in response to user input), THEN the system SHALL create exactly one usage row linked to that message. IF a message is a user message, THEN no usage row SHALL be created (usage is only reported on the assistant's response). The usage row SHALL store the exact model identifier/version string the provider actually returned (not the user-configured alias or `model_id`), so historical accuracy is preserved across model rebranding and version updates.

*Acceptance criterion:* A conversation with alternating user and assistant messages produces exactly N assistant messages and exactly N usage rows (1:1 mapping). The usage row's `model_identifier` field contains the exact string from the provider response body (e.g., `gpt-4-turbo-2024-04-09` from OpenAI's `response.model` field, or `claude-3-5-sonnet-20241022` from Anthropic's `message.model` field), not the user's alias (e.g., `gpt-4-turbo`). A test verifies this distinction.

---

### Timing Measurement

**REQ-F-011 — Ubiquitous: Client-side wall-clock timing across all providers**

The system SHALL measure "time to generate" (duration from request dispatch to terminal event) using a client-side wall-clock timer, uniformly across all four providers for comparability. The timer SHALL start when `sendChat()` is invoked (or the request is dispatched) and stop at whichever terminal event fires (`Completed`, `Cancelled`, or `Error`). This duration includes network/TLS/queueing overhead and is the canonical metric for cross-provider timing comparisons.

*Acceptance criterion:* A test sends a request via each of the four providers and confirms that the Completed (or Cancelled/Error) event carries a `duration_ms` field with a positive integer value greater than zero. Two identically-configured requests to the same provider produce durations within a reasonable variance (not exact, due to network variance, but both > 0).

---

**REQ-F-012 — Ubiquitous: Supplementary provider-native timing for Ollama**

The system SHALL preserve Ollama's server-measured timing fields (`total_duration`, `load_duration`, `prompt_eval_duration`, `eval_duration`, all nanoseconds) as supplementary/bonus data in the Usage type, distinct from the canonical wall-clock duration. The system SHALL NOT use Ollama's server-measured duration as the canonical "time to generate" metric (which remains wall-clock), as this would not be comparable to the other three providers' network-inclusive measurements.

*Acceptance criterion:* An Ollama usage row contains both `duration_ms` (wall-clock, from Completed event timestamp) and `ollama_prompt_eval_duration_ns` (server-measured, from the `done:true` chunk). A cross-provider comparison report explicitly labels Ollama's times as "server-measured (compute-only)" and others as "client-measured (network-inclusive)," preventing silent conflation.

---

## Non-Functional Requirements

**REQ-NF-001 — Testability: Injectable time source**

Duration measurement SHALL go through an injectable time-source seam (a small abstraction, mirroring the existing `HttpClient` dependency-injection pattern already used throughout the provider adapters), defaulting to real wall-clock time in production but swappable in tests for two fixed timestamps (start and end) to enable exact duration assertions without flakiness.

*Acceptance criterion:* A unit test injects a mock clock with `MockClock::start(1000)` and `MockClock::end(2500)`, verifies that a Completed event carries exactly `duration_ms=1500`, and confirms that this exact value can be asserted without fuzzy "> 0" checks. The production code defaults to real `std::chrono::steady_clock`.

---

**REQ-NF-002 — Auditability: Token counts and cost both persisted**

The system SHALL persist both raw token counts (input_tokens, output_tokens, reasoning_tokens, cache_creation_tokens, cache_read_tokens, total_tokens) and computed estimated_cost to the database. This enables historical auditing and re-derivation of costs if the price table changes, without loss of precision.

*Acceptance criterion:* A SQL query on the usage table returns columns for each token field and estimated_cost. A separate utility function recomputes cost from stored tokens and a provided price table, confirming the original cost is reproducible.

---

**REQ-NF-003 — Consistency: No token deduplication across turns**

The system design SHALL acknowledge that token counts are NOT deduplicated across conversation turns (since providers resend the full conversation on each request). This is the intentional, correct behavior for invoice matching. Any future refactoring SHALL preserve this property and document it explicitly to prevent accidental "optimization" that would diverge from actual billing.

*Acceptance criterion:* A regression test computes total-chat cost by summing per-turn costs (with full prompt resending) and compares it against a provider's documented billing model to confirm alignment. Documentation states: "Total cost = sum of per-turn costs, not deduplicated against unique tokens."

---

## Constraints & Non-Goals

**REQ-C-001 — No UI/QML this cycle**

This cycle SHALL NOT include any QML/UI components, settings panels, cost-display widgets, or per-provider/per-model rollup views. User-facing cost visualization is explicitly deferred to a follow-on cycle, consistent with prior precedent (e.g., Ollama backend → chat-window-qml as separate cycles).

*Acceptance criterion:* The cycle closes without adding or modifying any `.qml` files. Code review confirms no QML-dependent changes in domain, providers, persistence, or config modules.

---

**REQ-C-002 — No reasoning/thinking mode enablement**

This cycle SHALL NOT add adapter parameters to enable Anthropic `thinking` mode or OpenAI `reasoning.effort` mode. Current adapters intentionally omit these params (documented in inline comments). Reasoning/thinking tokens will be structurally supported in the Usage type (REQ-F-002) but will remain empty/null in practice until a separate cycle explicitly enables reasoning generation.

*Acceptance criterion:* A code review confirms that `anthropic_provider.cpp` and `openai_provider.cpp` remain unchanged in their request-building logic (no new thinking/reasoning params). A test sends a real request to each provider and verifies that `reasoning_tokens` in the Usage is null/zero (not because reasoning was filtered, but because it was never requested).

---

**REQ-C-003 — No Anthropic cache_control opt-in**

This cycle SHALL NOT add adapter changes to opt into Anthropic prompt caching (`cache_control` breakpoints). The usage row persists whatever cache fields the provider returns unprompted—for Anthropic, this will effectively always be null/zero since no adapter opts into caching. OpenAI and Google cache automatically (and may show real cache values), but this asymmetry is expected and not a defect to be fixed.

*Acceptance criterion:* The Anthropic adapter code remains unchanged; no `cache_control` additions are made. If an Anthropic response unprompted-ly reports cache fields in the future, the Usage row correctly stores them; if it never does, the fields remain null. A comment in the code documents this asymmetry as intentional.

---

**REQ-C-004 — No backfill of historical messages**

Messages sent before this feature ships SHALL NOT receive retroactive usage rows. This is expected and correct, not a data gap requiring migration. The persistence migration SHALL be additive only (new usage table/columns), not backfill-based.

*Acceptance criterion:* A conversation with 10 messages before and 5 messages after feature deployment shows 5 usage rows (post-deployment only). No migration or backfill script is provided or required.

---

**REQ-C-005 — No live re-pricing of historical costs**

If the price table is edited after a usage row is persisted, the `estimated_cost` field of that row SHALL remain frozen (unchanged). Pricing edits SHALL affect only new messages going forward.

*Acceptance criterion:* A historical usage row with `estimated_cost=$0.50` persists unchanged after the price table is edited. A new message sent after the edit uses the new prices. A test verifies both behaviors.

---

**REQ-C-006 — No settings-UI pricing editor this cycle**

The price table SHALL be edited only via manual JSON file editing (by advanced users). No QML settings dialog or UI-based pricing editor SHALL be added this cycle.

*Acceptance criterion:* No new settings QML files are added. The price table is loaded from a well-documented JSON file path. Documentation explains how to edit the file manually.

---

## Multi-Module Scope Justification

This cycle intentionally spans four provider adapters (`holonight_providers`), plus three supporting modules (`holonight_domain`, `holonight_persistence`, `holonight_config`), closing the same long-standing `REQ-F-017` deferred requirement that appears identically across three of the four provider cycles (Anthropic, Google, OpenAI) and adding equivalent support for Ollama. This is a deliberate architectural choice to close a cross-cutting concern uniformly, not scope creep.

---

## Summary of Acceptance Criteria

| Requirement | Acceptance Criterion |
|-------------|----------------------|
| REQ-F-001 | Usage is optional on terminal variants; partial usage carries through to event that fires (e.g., Cancelled with partial usage) |
| REQ-F-002 | Each Usage field is independently optional; missing field ≠ zero |
| REQ-F-003 | Ollama `done:true` chunk parsing extracts input/output/duration tokens; exact values in test |
| REQ-F-004 | OpenAI response.completed event parsing extracts all fields; cache/reasoning mapping verified |
| REQ-F-005 | Anthropic message_delta overrides message_start; final value used, not summed |
| REQ-F-006 | Google Gemini final chunk values used (not intermediate chunks); all fields mapped correctly |
| REQ-F-007 | Price table loads, unlisted model returns null (not $0) |
| REQ-F-008 | Per-turn cost computed without deduplication; total = sum of turns |
| REQ-F-009 | Cost frozen at write; token counts persisted; cost remains unchanged if price table edited |
| REQ-F-010 | One usage row per assistant message (zero for user messages); exact model identifier stored |
| REQ-F-011 | Wall-clock duration measured consistently across all providers; positive integer value |
| REQ-F-012 | Ollama server-measured timing preserved; not confused with canonical wall-clock metric |
| REQ-NF-001 | Exact duration assertions possible via injectable mock clock; production uses steady_clock |
| REQ-NF-002 | Token counts and cost both persisted; cost re-derivable from stored tokens |
| REQ-NF-003 | Total cost = sum of per-turn (documented as intentional, not deduplicated) |
| REQ-C-001 | No .qml files added; UI deferred to follow-on cycle |
| REQ-C-002 | No thinking/reasoning mode enablement; fields remain null in practice |
| REQ-C-003 | No Anthropic cache_control opt-in; asymmetry documented as intentional |
| REQ-C-004 | No backfill of pre-deployment messages; migration is additive only |
| REQ-C-005 | Historical costs frozen; new messages use updated price table |
| REQ-C-006 | Price table edited manually (JSON); no settings-UI editor added |
