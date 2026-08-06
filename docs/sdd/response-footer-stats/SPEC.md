# Response Footer Stats UI — EARS Specification

## Scope Revision (2026-08-03)

**Cost/pricing display has been removed from this feature.** T-023 manual verification surfaced
`$ ?` (Unknown cost) for every provider — not just Ollama, whose API genuinely has no billing
concept. Live requests against Ollama, OpenAI, Anthropic, and Google (both chat-completion and
models-list endpoints) confirmed none of the four expose pricing through any API; the only source
was a hand-maintained `price_table.json` (REQ-C-006) that nobody would realistically keep current
across every model. That gap was judged unfixable within the original design, so cost/pricing was
removed entirely rather than patched. **REQ-F-002, REQ-F-006, REQ-F-008, REQ-C-006, REQ-C-009, and
REQ-C-010 below are void** (kept for historical record only); REQ-F-003/004/007 are still accurate
for their token-count and duration portions but their cost-related clauses no longer apply. See
`docs/sdd/usage-cost-tracking/SPEC.md`'s matching note for the backend-half removal (schema,
`PriceTable`, `computeCostBreakdown`). Token counts (T/C/R), duration, and the popup's Token
Breakdown column are unaffected.

## Overview & Context

The `usage-cost-tracking` cycle (completed) built the entire domain, configuration, persistence, and provider-parsing infrastructure for token usage and cost tracking (REQ-C-001 of that cycle explicitly deferred all UI/QML work to a follow-on cycle). This cycle IS that follow-on. We close the UI-deferral gap by implementing:

- Read path from SQLite to QML: fetch persisted usage rows for an entire conversation in one batch query when the conversation loads.
- QML data roles: expose usage/cost fields on `MessageListModel` so QML can display them.
- Workspace window footer: a compact badge row + info popup on fully completed assistant messages showing token counts (input/completion/reasoning/cache), duration, cost breakdown, and a three-state cost display logic (Free/Priced/Unknown).

This specification does NOT re-visit the backend domain types, configuration, persistence, or provider adapters — all built in `usage-cost-tracking` and referenced as given infrastructure. This cycle focuses exclusively on the read path, schema migration for cost breakdown, and all QML/UI surface.

---

## Functional Requirements

### Area: Read Path & Schema

**REQ-F-001: Batch Load Usage on Conversation Open**

*Event-Driven:* When a conversation is opened and loaded into the workspace window, the system shall fetch all persisted usage records (one per assistant message) for that conversation in a single batch query, keyed by `conversation_id`, in message creation order (ascending).

*Acceptance Criterion:*
- `ConversationRepository` shall define a new interface method `usageForConversation(const ConversationId &id) -> QMap<MessageId, UsageRecord>` returning a multi-record map (one record per message that has a usage row; entries absent for messages without usage).
- `SqliteConversationRepository` shall implement this using a single SQL `SELECT * FROM usage WHERE conversation_id = ? ORDER BY message_id ASC` query.
- The application layer (e.g., `ChatViewModel`) shall invoke this during `loadConversation()` and pass the result to `MessageListModel` so QML can access it (see REQ-F-003).
- No per-message lazy fetching shall occur; all usage for a conversation is fetched together.

**REQ-F-002: Extend Usage Schema with Per-Category Cost Breakdown**

*Ubiquitous:* The SQLite `usage` table schema shall be extended with five new nullable REAL columns capturing the per-category cost breakdown: `input_cost_usd`, `output_cost_usd`, `reasoning_cost_usd`, `cache_creation_cost_usd`, `cache_read_cost_usd`, each independently computed and frozen at write time.

*Acceptance Criterion:*
- A new migration file `0005_add_usage_cost_breakdown.sql` shall add the five columns as `REAL DEFAULT NULL` (no NOT NULL constraint; nulls indicate either the category had no tokens or the price-table entry was absent).
- The cost formula per category is: `tokens × price / 1,000,000` only where BOTH token count and price field are present; if either is absent/null, the cost column shall be NULL.
- These five per-category columns shall always sum exactly to the existing `estimated_cost_usd` for any new row persisted after this migration.
- Existing usage rows persisted before this migration shall keep the five new columns NULL (no backfill, no recomputation from stored raw tokens).
- The migration shall be idempotent and shall succeed whether or not the table already exists.

**REQ-F-003: Expose Usage Fields as QML Roles on MessageListModel**

*Ubiquitous:* `MessageListModel` shall expose the following new data roles for each message row in the QML layer, independently nullable/absent (not zero-filled):

*Acceptance Criterion:*
- Token count roles (each `int`, or absent/undefined in QML if the message has no usage row): `inputTokenCount`, `outputTokenCount`, `reasoningTokenCount`, `cacheCreationTokenCount`, `cacheReadTokenCount`, `totalTokenCount`.
- Duration role: `durationMs` (type `qint64`, or absent in QML if null in the domain).
- Cost roles (each type `double` in QML, or absent if the message has no usage row or the category has no cost data): `inputCostUsd`, `outputCostUsd`, `reasoningCostUsd`, `cacheCreationCostUsd`, `cacheReadCostUsd`, `estimatedCostUsd`.
- For messages with no usage row (pre-feature historical messages, non-assistant messages, or messages that did not complete), all nine roles shall be absent/undefined in QML (not zero or empty string).
- The `MessageListModel` shall be populated with this data during conversation load (via REQ-F-001) and during each new message completion (from the `Completed` terminal `StreamEvent`).

---

### Area: Footer Badge Row

**REQ-F-004: Render Compact Badge Row on Completed Assistant Messages**

*State-Driven:* The assistant bubble in `MessageBubble.qml` (workspace window only) shall render a new footer row ONLY when all of the following conditions hold: (1) the message is an assistant message (`isAssistant === true`), (2) the message has reached `Completed` terminal status, and (3) a usage row exists for the message.

*Acceptance Criterion:*
- The footer row shall appear as a final sibling row within the assistant `MessageBubble`'s content ColumnLayout, after the message text/rich content, but not in the quick-panel surface (never).
- The row shall contain, left to right, the following badges (each omitted entirely if its underlying value is null/absent):
  - **Prompt Token badge** (`T <N>`): input_tokens, right-aligned in the T badge container, formatted per REQ-F-005.
  - **Completion Token badge** (`C <N>`): output_tokens, right-aligned in the C badge container, formatted per REQ-F-005.
  - **Reasoning Token badge** (`R <N>`): reasoning_tokens, right-aligned in the R badge container, formatted per REQ-F-005, omitted if null (common; only Google/Gemini has reasoning data today).
  - **Cost badge**: displaying per-category cost or total, see REQ-F-006 for three-state logic.
  - **Duration badge** (`<N>s` or `<N>ms`): durationMs formatted as seconds with 1 decimal if ≥1000 ms (e.g. `1.2s`), or as milliseconds if <1000 ms (e.g. `340ms`), omitted if null.
  - **Info icon button**: inline SVG reusing the existing icon system, clickable to open the info popup (REQ-F-007).
- Each badge shall be rendered with semantic styling consistent with the existing message-bubble design (muted text color, slightly smaller font than the message body, inline or wrapped horizontally if space is constrained).

**REQ-F-005: Format Token Counts with Abbreviation for Large Values**

*Ubiquitous:* Token count values in badges and popup text shall use the following formatting rule:

*Acceptance Criterion:*
- Token counts ≥1000 shall be abbreviated as `<N.NK>` with exactly 1 decimal place (e.g., `2.4K` for 2400, `10.0K` for 10000).
- Token counts <1000 shall be displayed as exact integers with no decimal point (e.g., `42`, `999`).
- Thousands separators (commas) shall NOT be used in badge text (badges are compact; commas go in popup text only, see REQ-F-007).
- This rule applies to all token displays: prompt, completion, reasoning, cache creation, cache read, and total.

**REQ-F-006: Apply Three-State Cost Display Logic**

*Conditional:* The cost badge in the footer and all cost displays in the info popup shall use a three-state display logic based on the price-table configuration and the persisted cost value:

*Acceptance Criterion:*
- **Free state**: If the model's price-table entry has the relevant price field(s) explicitly set to `0.0` (verified-free, e.g., self-hosted Ollama, Google free tier), display the literal text `Free` (no `$` sign, no numeric value). This state is distinct from and never confused with Unknown.
- **Priced state**: If the relevant price field(s) are real nonzero values and a cost was computed, display as `$ X.XXXX` with exactly 4 decimal places (e.g., `$ 0.0042`, `$ 1.2345`). If the true computed value rounds to exactly `0.0000` at 4 decimals but is genuinely nonzero, display `<$0.0001` instead of `$0.0000`, ensuring a nonzero cost is never visually confused with Free.
- **Unknown state**: If no price-table entry exists at all for this model_identifier for the relevant category, display the distinct marker `$ ?` (with space and question mark), never conflated with Free or with a priced `$0.0000`.
- Each cost category (input, output, reasoning, cache creation, cache read) in the popup shall apply this same three-state logic independently.
- The total cost row shall also apply this logic: sum is Free only if all component costs are Free or 0.0; sum is Unknown if any component is Unknown; sum is Priced otherwise.

---

### Area: Info Popup

**REQ-F-007: Render Info Popup with Token and Cost Breakdown**

*Event-Driven:* When the user clicks the info icon button on the footer badge row, the system shall open a dismissible popup displaying a two-column compact layout with token and cost breakdown for that message.

*Acceptance Criterion:*
- The popup shall be a `Popup` component with `closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside` (standard dismiss behavior, matching `QuickPanelHeader.qml` precedent).
- The popup background shall use `HnSurfaceFrame { surfaceRole: HnSurfaceRole.Menu }` for semantic styling.
- The popup shall contain two columns, left-aligned or grid-laid out:
  - **Left column "Token Breakdown"**: exact integer counts with thousands separators (commas) for each category, and percentage-of-total for that category. Rows in order: Prompt Tokens, Completion Tokens, Reasoning Tokens, Cache Creation Tokens, Cache Read Tokens, followed by a "Total Tokens" summary row. Each row shall be OMITTED entirely (not shown as 0 or a dash) if its underlying token count is null/absent.
  - **Right column "Cost Breakdown (USD)"**: line items for Input Cost, Output Cost, Reasoning Cost, Cache Creation Cost, Cache Read Cost, each formatted per the three-state logic (REQ-F-006), followed by a "Total Cost" summary row. Each row shall be OMITTED if its underlying cost field is null (which is common for historical pre-feature messages that lack per-category cost columns, or for categories where token count or price data was absent at write time).
- For historical pre-feature usage rows that do not have per-category cost columns populated (pre-migration rows, where the five new columns are NULL), the popup shall still display the Token Breakdown left column normally (token counts were always persisted), but the Cost Breakdown right column shall show only the Total Cost row and omit all per-category cost rows.
- The popup text shall be compact and appropriately scaled for the tight presentation, with semantic color/contrast consistent with the workspace theme.

---

### Area: Cost Display Logic

**REQ-F-008: Unify Three-State Cost Display Across Footer and Popup**

*Ubiquitous:* All cost displays — whether in the footer badge, in the popup's per-category line items, or in the popup's total-cost summary — shall apply the same three-state logic (REQ-F-006) uniformly and without exception.

*Acceptance Criterion:*
- A single C++ or QML helper function shall implement the three-state cost formatting (`Free` / `$ X.XXXX` / `$ ?`), and all cost displays shall invoke this function rather than duplicating the logic.
- Per-category costs in the popup shall each apply the logic independently based on their own token count and price-table entry.
- The total cost shall compute the sum of all per-category costs and then apply the three-state logic to the sum, respecting the rule that total is Free only if all components are Free or 0.0, Unknown if any component is Unknown, and Priced otherwise.
- The cost display shall never use hardcoded numeric `0` or `$0.00` to represent Unknown; `$ ?` is the explicit marker for missing price data.

---

## Non-Functional Requirements

**REQ-NF-001: Testability of Batch Read Path**

*Acceptance Criterion:*
- Unit tests shall exist that directly invoke `usageForConversation()` on an in-memory or test SQLite instance with known usage records, verifying that the returned map contains the correct records in the correct order (by message_id, ascending).
- No visual or integration test is required; the read path shall be covered by headless GTest unit tests.

**REQ-NF-002: Testability of Three-State Cost Formatting**

*Acceptance Criterion:*
- A standalone unit test (GTest) shall exist that exercises the cost-formatting helper function with inputs covering all three states: Free (0.0 in price table), Priced (nonzero value), Unknown (null price table entry), and edge cases (rounded-to-0.0000 nonzero value).
- The test shall verify that Free, Priced, and Unknown states are each rendered correctly and never confused with each other.

**REQ-NF-003: Null-Safety and Absence Discipline**

*Acceptance Criterion:*
- All new QML roles shall be designed to be absent/undefined when no data is available (not zero-filled, not empty string, not a placeholder).
- The MessageListModel shall propagate absence through the role layer: if the domain `Usage` struct field is null, the corresponding QML role shall be absent/undefined, not a default value.
- C++ code shall use `std::optional` and QML shall use undefined/absent semantics (e.g., `if (model.inputTokenCount !== undefined)` patterns) rather than null-checking against zero.

**REQ-NF-004: Performance of Batch Load**

*Acceptance Criterion:*
- A conversation with N assistant messages and full usage data shall fetch all N usage records in a single SQL query, not N queries (verified by checking that only one `SELECT` statement is issued against the usage table per conversation load).
- The batch load shall complete without perceptible lag (no explicit performance target, but shall not block the UI; async pattern matching the existing `ConversationRepository` threading model).

---

## Constraints & Non-Goals

**REQ-C-001: No Footer on Non-Completed Messages**

*Conditional Unwanted Behavior:* The footer badge row shall NOT render on messages that have not reached `Completed` terminal status, even if they carry partial usage data (e.g., cancelled or error terminal events with partial token counts).

*Acceptance Criterion:*
- Acceptance: The badge footer is not rendered on any message with status `Cancelled`, `Error`, or any non-terminal state.
- Rejection: If a `Cancelled` or `Error` message displays a footer badge, the requirement is not met.

**REQ-C-002: No Footer on Pre-Feature Historical Messages**

*Ubiquitous Unwanted Behavior:* Assistant messages persisted before this feature's implementation (no usage row in the SQLite table) shall not display a footer badge row.

*Acceptance Criterion:*
- Acceptance: Pre-feature messages load without a footer badge, even though they are assistant messages in `Completed` status.
- Rejection: If a pre-feature message displays a placeholder footer (e.g., "No usage data available"), the requirement is not met.

**REQ-C-003: No Footer in Quick-Panel Surface**

*Ubiquitous Unwanted Behavior:* The response footer badge row and info popup shall not be rendered or accessible in the quick-panel surface (`qml/quickpanel/`); the quick panel remains text-only and minimal.

*Acceptance Criterion:*
- Acceptance: The quick-panel conversation view is not modified; no badges, no popup button, no cost display.
- Rejection: If the quick panel displays usage badges or an info button, the requirement is not met.

**REQ-C-004: No Backfill of Per-Category Cost Columns**

*Ubiquitous Unwanted Behavior:* Historical usage rows persisted before the schema migration `0005_add_usage_cost_breakdown.sql` shall retain NULL values in the five new per-category cost columns; no retroactive computation or backfill from stored token counts shall occur.

*Acceptance Criterion:*
- Acceptance: After the migration, querying a pre-migration usage row returns NULL for all five new cost columns (input_cost_usd, output_cost_usd, etc.), and the popup displays only the total cost, not per-category breakdowns.
- Rejection: If the system recomputes or fills these columns with new cost values for old rows, the requirement is not met.

**REQ-C-005: No Conversation-Level Cost Rollup**

*Ubiquitous Unwanted Behavior:* The system shall not display a running total of token usage or cost at the conversation or window level (e.g., in a header, sidebar, or summary row above the message list).

*Acceptance Criterion:*
- Acceptance: Cost and token totals are shown only per-message (footer badge + popup), never aggregated at the conversation level.
- Rejection: If a conversation header, sidebar, or any non-message-level UI displays an aggregate cost or token total, the requirement is not met.

**REQ-C-006: No Settings UI for Price Table**

*Ubiquitous Unwanted Behavior:* This cycle shall not implement a settings panel or UI for editing `price_table.json`; price-table configuration remains manual JSON editing only, unchanged from the `usage-cost-tracking` cycle constraint.

*Acceptance Criterion:*
- Acceptance: No new settings UI is added; users edit price_table.json by hand (out of scope).
- Rejection: If a settings panel appears for price-table editing, the requirement is not met.

**REQ-C-007: No Live-Updating Footer During Stream**

*State-Driven Unwanted Behavior:* The footer badge row and info popup shall not appear or update while a message is in-flight (streaming, non-terminal); they shall only appear once the message reaches `Completed` terminal status.

*Acceptance Criterion:*
- Acceptance: A streaming assistant message (before its terminal event is received) has no footer badge or info button visible.
- Rejection: If the footer or popup appears mid-stream or updates with partial usage data during streaming, the requirement is not met.

**REQ-C-008: No Lazy Per-Message Usage Fetching**

*Ubiquitous Unwanted Behavior:* The system shall not fetch individual message usage records on-demand (e.g., only when the user scrolls a message into view or clicks the info button); all usage for a conversation shall be fetched in one batch load during conversation open.

*Acceptance Criterion:*
- Acceptance: The batch `usageForConversation()` query (REQ-F-001) fetches all records once per conversation load, and QML accesses them from the model without triggering additional SQL queries.
- Rejection: If a SQL query is issued when the user clicks the info button or scrolls to a message, the requirement is not met.

**REQ-C-009: No Multi-Currency Support**

*Ubiquitous Unwanted Behavior:* Cost displays shall use USD only (`$` sign); no currency conversion, no multi-currency selection, no locale-specific currency symbols.

*Acceptance Criterion:*
- Acceptance: All cost values are displayed with `$` and USD formatting (4 decimal places), regardless of system locale.
- Rejection: If cost values are displayed in any currency other than USD, or if a currency selection control is provided, the requirement is not met.

**REQ-C-010: No Provider-Side Promotional Billing Detection**

*Ubiquitous Unwanted Behavior:* The system shall not attempt to detect, track, or reconcile provider-side promotional billing, free-quota allowances, or trial-period subsidies (e.g., OpenAI's daily free token allowance, Google's free-tier quotas); cost estimates shall always reflect the configured price-table list price against the reported token counts.

*Acceptance Criterion:*
- Acceptance: Cost is computed solely as `tokens × price_table_entry / 1,000,000`; no special-case logic for free quotas or promotions.
- Rejection: If the system skips cost calculation or shows `$0` for a request that would normally have a cost, claiming a promotion or free quota, the requirement is not met.

---

## Acceptance Criteria Summary

| ID | Title | Type | Acceptance Criterion | Verifiable |
|---|---|---|---|---|
| REQ-F-001 | Batch Load Usage on Conversation Open | Event | `usageForConversation()` method implemented; single SQL query per conversation; called during `loadConversation()`; data passed to MessageListModel. | Yes (unit test; SQL spy) |
| REQ-F-002 | Extend Usage Schema with Per-Category Cost Breakdown | Ubiquitous | Migration `0005_add_usage_cost_breakdown.sql` adds 5 nullable REAL columns; cost formula follows (tokens × price / 1M where both present); pre-migration rows retain NULL; formula: sum of 5 categories = total. | Yes (migration inspection; integration test with pre/post rows) |
| REQ-F-003 | Expose Usage Fields as QML Roles | Ubiquitous | 9 new roles on MessageListModel (6 token counts, 1 duration, 2 costs); each independently absent/undefined if null in domain; populated during load and on message completion. | Yes (QML binding test; inspect role presence) |
| REQ-F-004 | Render Compact Badge Row on Completed Assistant Messages | State | Footer appears only on completed assistant messages with usage row; appears in workspace window only (not quick panel); contains T/C/R/cost/duration/info-icon badges; badges omitted if value is null. | Yes (visual inspection; QML condition test) |
| REQ-F-005 | Format Token Counts with Abbreviation | Ubiquitous | ≥1000 tokens: `<N.NK>` (1 decimal); <1000: exact integer; no commas in badges; rule applies uniformly. | Yes (unit test on formatter function) |
| REQ-F-006 | Apply Three-State Cost Display Logic | Conditional | Free (price=0.0): text "Free"; Priced (price>0): "$ X.XXXX" (4 decimals, or "<$0.0001" if rounds to 0.0000 but nonzero); Unknown (no price entry): "$ ?". Applied to all cost displays. | Yes (unit test; inspect popup/badge rendering) |
| REQ-F-007 | Render Info Popup with Token and Cost Breakdown | Event | Popup opens on info-icon click; Popup with Escape/outside-click dismiss; HnSurfaceFrame Menu role; 2-column layout (Tokens left, Costs right); commas in popup text; rows omitted if null; pre-migration rows show only total cost. | Yes (visual; QML state test) |
| REQ-F-008 | Unify Three-State Cost Display Across Footer and Popup | Ubiquitous | Single cost-formatter function used by footer and popup; per-category and total costs use same logic; total is Free only if all components Free/0.0, Unknown if any Unknown, Priced otherwise. | Yes (unit test; inspect rendering code) |
| REQ-NF-001 | Testability of Batch Read Path | Non-Func | GTest unit tests exist for `usageForConversation()` with known test data; verifies correct records and order. | Yes (ctest output) |
| REQ-NF-002 | Testability of Three-State Cost Formatting | Non-Func | GTest unit test exists covering Free, Priced, Unknown states and edge cases (rounded-to-0.0000). | Yes (ctest output) |
| REQ-NF-003 | Null-Safety and Absence Discipline | Non-Func | QML roles are undefined/absent if null; C++ uses `std::optional`; no zero-filling; QML checks `!== undefined`. | Yes (code inspection; binding behavior test) |
| REQ-NF-004 | Performance of Batch Load | Non-Func | Single SQL SELECT per conversation load; no per-message queries; UI not blocked; async pattern matches existing repo threading. | Yes (SQL query profiling; UI responsiveness observation) |
| REQ-C-001 | No Footer on Non-Completed Messages | Unwanted | Footer not rendered on Cancelled, Error, or non-terminal messages. | Yes (visual inspection; unit test on message status) |
| REQ-C-002 | No Footer on Pre-Feature Historical Messages | Unwanted | Pre-feature messages (no usage row) do not display footer. | Yes (load pre-feature conversation; inspect rendering) |
| REQ-C-003 | No Footer in Quick-Panel Surface | Unwanted | Quick panel QML untouched; no badges, popup, or cost display in quick panel. | Yes (code inspection; quick-panel rendering test) |
| REQ-C-004 | No Backfill of Per-Category Cost Columns | Unwanted | Pre-migration usage rows retain NULL in 5 new columns after migration. | Yes (query pre-migration row; inspect columns are NULL) |
| REQ-C-005 | No Conversation-Level Cost Rollup | Unwanted | No window-header, sidebar, or conversation-level total cost/tokens display. | Yes (visual inspection; code inspection for rollup logic) |
| REQ-C-006 | No Settings UI for Price Table | Unwanted | No settings panel for price-table editing; manual JSON only. | Yes (code inspection; visual inspection of settings UI) |
| REQ-C-007 | No Live-Updating Footer During Stream | Unwanted | Footer and popup do not appear or update while message is in-flight; only appear at Completed. | Yes (observe streaming message; verify footer appears only after completion) |
| REQ-C-008 | No Lazy Per-Message Usage Fetching | Unwanted | No SQL query issued during info-button click or scroll; all data fetched in batch at load. | Yes (SQL query logging; QML binding test) |
| REQ-C-009 | No Multi-Currency Support | Unwanted | USD only (`$` sign); no currency conversion or selection. | Yes (code inspection; visual inspection of cost displays) |
| REQ-C-010 | No Provider-Side Promotional Billing Detection | Unwanted | Cost = tokens × price_table / 1M; no special-case logic for free quotas or promotions. | Yes (inspect cost-computation function; verify no quota-skipping logic) |

---

## References

- **Prior Cycle (Usage-Cost-Tracking)**: `docs/sdd/usage-cost-tracking/SPEC.md` (closes UI deferral constraint REQ-C-001).
- **Domain Types**: `src/domain/include/holonight_domain/stream_event.h` — `Usage` struct fields.
- **Persistence Schema**: `src/persistence/migrations/0004_add_usage.sql` — existing `usage` table; `0005_add_usage_cost_breakdown.sql` (new, this cycle).
- **Price Configuration**: `src/config/include/holonight_config/price_table.h` — `PriceTable` and `TokenPricing`.
- **MessageListModel**: `src/application/include/holonight_application/message_list_model.h` — QML roles to be extended.
- **Message Bubble**: `qml/shared/MessageBubble.qml` — footer row target.
- **QML Popup Precedent**: `qml/quickpanel/QuickPanelHeader.qml` — Popup dismiss and styling pattern.
- **Provider Field Coverage**: Ollama (no R/cache tokens, ns-duration fields); OpenAI (R tokens if reasoning.effort enabled, cache tokens automatic); Anthropic (no R/cache tokens); Google/Gemini (R and cache-read tokens present).
