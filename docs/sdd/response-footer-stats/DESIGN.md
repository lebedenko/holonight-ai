# DESIGN: Response Footer Stats UI

Follow-on to `docs/sdd/usage-cost-tracking/DESIGN.md`, which built and froze all domain/config/
persistence/provider infrastructure for token usage and cost and explicitly deferred every UI
concern (its REQ-C-001). This cycle implements only the read path (SQLite → QML) and the QML
surface itself: no new domain types, no new provider parsing, no config schema changes beyond one
additive migration.

---

## 1. Components

### 1.1 `holonight_persistence` (modified: schema, repository interface/impl/worker; new migration file)

- **`src/persistence/migrations/0005_add_usage_cost_breakdown.sql`** (new) — five `ALTER TABLE usage
  ADD COLUMN ... REAL DEFAULT NULL` statements (REQ-F-002).
- **`src/persistence/src/migration_runner.cpp`** (modified) — `builtInMigrations()` gets a 5th
  hardcoded `Migration{.version = 5, .name = "0005_add_usage_cost_breakdown", ...}` entry. See
  §6.5/§8.1 — this hand-maintained list is the known gotcha flagged in CLAUDE.md and prior-cycle
  memory; nothing about it changes structurally this cycle, it just needs a 5th entry like every
  prior migration did.
- **`src/persistence/CMakeLists.txt`** (modified) — `qt_add_resources(... FILES ...)` gets a 5th
  line, `migrations/0005_add_usage_cost_breakdown.sql`, or the resource is invisible to
  `readResource()` and the migration silently no-ops (empty SQL string, no error — `executeStatements`
  splits an empty string into zero statements and just records the schema_version bump). This is a
  second, distinct place the same "new migration" checklist touches — call it out in Stage 3 tasks
  as its own line item, not folded into the migration_runner.cpp edit.
- **`src/persistence/include/holonight_persistence/usage_record.h`** (modified) — `UsageRecord` gets
  five new `std::optional<double>` fields: `input_cost_usd`, `output_cost_usd`,
  `reasoning_cost_usd`, `cache_creation_cost_usd`, `cache_read_cost_usd`. `toUsageRecord()` does NOT
  set these (it has no access to a `PriceTable`) — they are populated by the worker after calling
  the (renamed/extended) cost-breakdown function, same division of labor `estimated_cost_usd`
  already has today (`toUsageRecord()` takes `estimatedCostUsd` as a caller-supplied parameter, not
  something it computes itself — see current signature in usage_record.h:37-38). `UsageRecord` also
  gains `Q_DECLARE_METATYPE` (it has never crossed a real Qt signal boundary before this cycle — see
  §3.5/§8.4).
- **`src/persistence/src/detail/conversation_repository_worker.cpp`** (modified) — the anonymous-
  namespace `computeEstimatedCost()` (lines 24-50 today) is replaced by `computeCostBreakdown()`,
  which returns all six values (five per-category + total) in one pass, evaluating each category's
  three-state status explicitly rather than silently folding "no price for this category" into a
  0-contribution sum. See §3.2 — this is the trickiest single piece of this cycle and gets its own
  worked-through section. `persistUsage()`'s SQL `INSERT` gets five more bound columns; `persistUsage()`
  also gains a **new slot behavior**: on successful `INSERT`, emit a new `usagePersisted(...)` signal
  carrying the fully-populated `UsageRecord` (see §2a, §6.2) — this replaces the current "fire-and-
  forget, no success signal" contract documented in `conversation_repository.h:46-48` and
  `conversation_repository_worker.cpp`'s doc comments, which predates this cycle's UI consumer.
- **New worker slot**: `usageForConversation(const QString& conversationId)` — one `SELECT * FROM
  usage WHERE conversation_id = ? ORDER BY message_id ASC` query (REQ-F-001's literal acceptance
  criterion), building a `QMap<QString, UsageRecord>` keyed by `message_id`, emitted via a new
  `usageForConversationLoaded(QString conversationId, QMap<QString, UsageRecord> usageByMessageId)`
  signal.
- **`ConversationRepository`/`SqliteConversationRepository`** (modified) — one new pure-virtual
  method `usageForConversation(QString conversationId)` (void, fire-and-forget, matching every other
  method on this interface — see §3.1 for why this deviates from the spec's literal
  `-> QMap<MessageId, UsageRecord>` notation) and two new signals (`usageForConversationLoaded`,
  `usagePersisted`).

### 1.2 `holonight_application` (modified: `ChatViewModel`, `MessageListModel`; new formatting helper)

- **`ChatViewModel`** (modified) — `onConversationLoaded()` now also calls
  `repository_->usageForConversation(loaded.summary.id)` immediately after adopting the conversation
  (REQ-F-001). Two new private slots: `onUsageForConversationLoaded(QString, QMap<QString,
  UsageRecord>)` and `onUsagePersisted(QString, QString, UsageRecord)`, both guarded by `conversationId
  == active_conversation_id_` (matching the stale-result-discard pattern every other
  conversation-scoped slot already needs — see §8.3).
- **`MessageListModel`** (modified) — `Row` gains one new field, `std::optional<
  holonight_persistence::UsageRecord> usage` (a single field, not 13 flattened members — see §6.3).
  13 new `Roles` enum entries appended after `ProviderNameRole` (§3.3). One new public method:
  `applyUsageRecords(const QMap<QString, holonight_persistence::UsageRecord>& usageByMessageId)`
  (§3.3, used for both the batch-load and the single-message-live-completion path — see §2b/§2c).
- **New file `src/application/include/holonight_application/cost_formatting.h` /
  `src/application/src/cost_formatting.cpp`** — two free C++ functions,
  `formatCost(const QVariant&)` and `formatTokenCount(const QVariant&)`, directly GTest-testable
  (REQ-NF-002), no `QObject`/meta-object machinery needed for the logic itself.
- **New file `src/application/include/holonight_application/token_cost_formatter.h` /
  `.cpp`** — a thin `QML_ELEMENT QML_SINGLETON QML_UNCREATABLE` `QObject` wrapper,
  `TokenCostFormatter`, exposing the two functions above as `Q_INVOKABLE` so QML can call them
  (§6.1 justifies the split).

### 1.3 `qml/shared/` (modified: `MessageBubble.qml`, `MessageList.qml`; new popup file)

- **`qml/shared/MessageList.qml`** (modified) — `messageDelegate`'s `required property` list grows by
  13 entries (one per new role), forwarded into `messageBubbleComponent`'s `MessageBubble { ... }`
  instantiation, mirroring exactly how `contentBlocks` is already threaded through today
  (MessageList.qml:46/80-86).
- **`qml/shared/MessageBubble.qml`** (modified) — 13 new `required property var` declarations (nullable
  — QML `var`, not `int`/`real`, is mandatory here, see §6.4/§8.5) plus a new footer `RowLayout` as
  the last child of the existing content `ColumnLayout` (MessageBubble.qml:107-143 today), visible
  only when `isAssistant && messageStatus === "complete" && usage row present` (REQ-F-004,
  REQ-C-001/002/007).
- **New file `qml/shared/ResponseStatsPopup.qml`** — the info popup (REQ-F-007), a separate
  component rather than an inline `Popup {}` block (§6.2/§7.2 covers the alternative and why it
  loses here, in contrast to `QuickPanelHeader.qml`'s inline `dropdownPopup`).

No changes anywhere under `qml/quickpanel/` — confirmed by inspection that quickpanel has no
`MessageBubble`/`MessageList` usage at all today (it renders conversation content some other,
simpler way); REQ-C-003 is therefore satisfied structurally, by omission, not by an added guard.

---

## 2. Data flow

### 2a. Live completion → persisted row → live footer update

```
ChatController streams StreamEvent::Completed{usage, model_identifier}
  → ChatViewModel::onStreamEvent()                                    [chat_view_model.cpp:724]
      lastMessage terminal + Assistant + usage.has_value() + modelIdentifier.has_value()
      → repository_->persistUsage(conversationId, messageId, *modelIdentifier, *usage)  [existing call, unchanged signature]
          → SqliteConversationRepository::persistUsage() packages a functor,
            QMetaObject::invokeMethod(worker_, ..., Qt::QueuedConnection)               [existing pattern, unchanged]
              → ConversationRepositoryWorker::persistUsage() (worker thread)
                  breakdown = computeCostBreakdown(price_table_, modelIdentifier, usage)  [NEW — see §3.2]
                  record = toUsageRecord(..., breakdown.total_cost_usd) with 5 new fields
                           copied from `breakdown` onto `record` after toUsageRecord() returns
                  INSERT INTO usage (..., input_cost_usd, output_cost_usd, reasoning_cost_usd,
                                     cache_creation_cost_usd, cache_read_cost_usd) VALUES (...)
                  on success: emit usagePersisted(conversationId, messageId, record)        [NEW signal]
                      (queued back to GUI thread; UsageRecord now Q_DECLARE_METATYPE'd — §3.5)
→ ChatViewModel::onUsagePersisted(conversationId, messageId, record)                  [NEW slot]
    if conversationId != active_conversation_id_: return   (stale — user switched conversations
                                                              mid-flight; discard, matches existing
                                                              stale-result convention elsewhere)
    message_model_->applyUsageRecords({{messageId, record}})                          [NEW method]
→ MessageListModel::applyUsageRecords(): finds the one row with id == messageId (linear scan;
    conversation-sized lists are small — see §8.6), sets row.usage = record, emits dataChanged()
    for exactly the 13 new roles
→ QML: MessageBubble's usage-derived required properties re-bind; footer row's `visible` binding
    flips true (status was already "complete" from the earlier updateNewestMessage() call in the
    same onStreamEvent() tick; usage arrives a queued tick later — REQ-C-007 is still satisfied,
    since the footer never appears *before* Completed, only slightly *after* it, which the spec's
    "no live-updating footer during stream" wording does not forbid — see §8.7)
```

### 2b. Conversation open → batch usage load → MessageListModel populated

```
User switches/opens a conversation
  → ChatViewModel::switchConversation()/startup → repository_->loadConversation(conversationId)  [existing, unchanged]
      → ... → ConversationRepositoryWorker::loadConversation() → emit conversationLoaded(LoadedConversation)
→ ChatViewModel::onConversationLoaded(loaded)                                          [existing slot, modified]
    adoptConversation(loaded.summary, loaded.messages)
      → message_model_->resetFromChronological(conversation_->messages())  [existing — Row.usage
                                                                             starts nullopt for every
                                                                             row, since toRow() never
                                                                             touches usage — §6.3]
    repository_->usageForConversation(loaded.summary.id)                               [NEW call]
        → SqliteConversationRepository facade → functor → worker thread
          → ConversationRepositoryWorker::usageForConversation(conversationId)         [NEW slot]
              SELECT * FROM usage WHERE conversation_id = ? ORDER BY message_id ASC    [REQ-F-001 literal SQL]
              build QMap<QString, UsageRecord> keyed by message_id (positional query.value(N),
              mirroring summaryFromRecord()/messageFromRecord()'s existing style)
              emit usageForConversationLoaded(conversationId, usageByMessageId)
→ ChatViewModel::onUsageForConversationLoaded(conversationId, usageByMessageId)        [NEW slot]
    if conversationId != active_conversation_id_: return   (user already switched again — discard)
    message_model_->applyUsageRecords(usageByMessageId)                               [NEW method,
                                                                                        same method
                                                                                        as §2a — see
                                                                                        §6.6 for why
                                                                                        one method
                                                                                        covers both]
→ MessageListModel::applyUsageRecords(): for every row whose id is a key in the map, sets
    row.usage = usageByMessageId[row.id], emits dataChanged for the 13 roles; rows with no match
    (pre-feature historical messages, user/system messages, non-terminal messages) are left
    untouched — i.e. still nullopt from the resetFromChronological() that just ran, i.e. the roles
    stay absent/undefined (REQ-C-002, REQ-NF-003) with zero extra code needed to "clear" them.
→ QML: MessageList's ListView is already bound to ChatViewModel.messages; the dataChanged() emission
    updates only the rows that changed, footer rows become visible for any assistant/complete/
    usage-present row.
```

### 2c. Popup open (REQ-F-007)

```
User clicks the info icon button in the footer row (MessageBubble.qml)
  → onClicked: statsPopup.open()   (ResponseStatsPopup.qml instance, a direct child of the footer
                                    row, `parent: root` per the QuickPanelHeader precedent so it's
                                    not clipped by the message bubble's own frame)
→ ResponseStatsPopup reads the same 13 required properties already bound onto MessageBubble
    (passed straight through as its own required properties — no second model lookup, no signal,
    pure property binding) and renders the two-column Token/Cost breakdown, applying
    TokenCostFormatter.formatCost()/.formatTokenCount() per line item (REQ-F-008).
```

---

## 3. Interfaces / APIs

### 3.1 `ConversationRepository` (interface) — new method and signals

```cpp
// conversation_repository.h
virtual void usageForConversation(QString conversationId) = 0;

Q_SIGNALS:
  void usageForConversationLoaded(QString conversationId,
                                  QMap<QString, holonight_persistence::UsageRecord> usageByMessageId);
  void usagePersisted(QString conversationId, QString messageId, holonight_persistence::UsageRecord record);
```

**Deviation from the spec's literal notation, and why.** REQ-F-001 writes this as
`usageForConversation(const ConversationId &id) -> QMap<MessageId, UsageRecord>`. Two things about
that don't match this codebase as it exists:

1. **Every existing method on this interface is `void`-returning and fire-and-forget**, with results
   delivered later via a signal (`loadConversation(QString) → conversationLoaded(LoadedConversation)`
   is the closest precedent — see `conversation_repository.h:30-32`'s own comment: "Every method
   below returns void immediately... results/errors arrive later via the signals below"). A
   synchronously-returning `usageForConversation()` would require either blocking the GUI thread on
   the worker thread's SQL query (defeats the entire point of `ConversationRepositoryWorker` living
   on its own `QThread`) or a hidden synchronization primitive nowhere else in this class uses. The
   spec's arrow notation is read as *describing the logical shape of the data*, not mandating a
   synchronous C++ return type — the async void-method-plus-signal translation is the same one
   every other spec'd "returns X" acceptance criterion in this codebase already gets (e.g.
   `loadConversation` "returns" a `LoadedConversation` in exactly this sense).
2. **`ConversationId`/`MessageId` never appear as parameter or key types anywhere in this interface
   today** — `loadConversation(QString conversationId)`, `persistUsage(QString conversationId,
   QString messageId, ...)`, etc. all take plain `QString`, with `ConversationId::fromString()`/
   `MessageId::fromString()` reconstruction happening only where domain code needs identity
   semantics (worker-internal `messageFromRecord()`, or `ChatViewModel::adoptConversation()`
   building a `Conversation` from a `LoadedConversation`). `MessageId` also has **no `operator<`**
   (only `operator==`, `message.h:33`), so it cannot be a `QMap` key at all without adding ordering
   to a domain type purely to satisfy this one call site. `usageForConversation` keeps this
   interface's established `QString`-at-the-boundary convention: `QMap<QString, UsageRecord>` keyed
   by `MessageId::toString()`'s string form — exactly the type `UsageRecord::message_id` already is
   (`usage_record.h:17`).

### 3.2 Cost computation — `computeCostBreakdown()` (the tri-state resolution, worked through)

This is the part flagged as trickiest in the brief: `UsageRecord` only stores `std::optional<double>`
per cost field, not a tri-state enum, yet REQ-F-006 needs Free/Priced/Unknown to round-trip through
that single nullable-double column without ever confusing Unknown with Free (both "look like" the
absence of a definite positive number if you're not careful).

**The resolution: a category's persisted cost is genuinely ambiguous in isolation (null could mean
"no tokens in this category" or "tokens present but no price configured"), but that ambiguity is
already broken by an independent, always-reliable signal: the category's token-count column,
which is never null when the category actually applies.** Per REQ-F-007, the popup (and by
construction the footer) never even considers a category's cost unless that category's token count
is non-null — the row is omitted outright otherwise. So by the time any cost-formatting code looks
at a persisted cost value, "no tokens" has already been ruled out by the caller; a null cost value
at that point can only mean "tokens present, no matching price" = Unknown. The `{null, 0.0, >0}`
convention the brief proposes therefore does round-trip correctly, but *only* because token-count
absence is checked first, at the call site, before the cost tri-state logic ever runs. This is
worth stating explicitly because it means **the display-time tri-state check and the write-time
cost computation are not the same function and must not be conflated** — see the total-cost
subtlety below, which is where a naive implementation breaks this invariant for the *aggregate*.

**Per-category evaluation** (worker-side, replaces `computeEstimatedCost()`):

```cpp
enum class CategoryCostState : std::uint8_t { NotApplicable, Unknown, Free, Priced };

struct CategoryCost {
  CategoryCostState state = CategoryCostState::NotApplicable;
  double value = 0.0;  // meaningful only when state is Free or Priced
};

// tokens absent           -> NotApplicable (category didn't occur in this response at all)
// tokens present, no price -> Unknown       (REQ-F-006's Unknown state)
// tokens present, price == 0.0 -> Free      (verified-free rate)
// tokens present, price > 0.0  -> Priced, value = tokens * price / 1'000'000
CategoryCost evaluateCategory(std::optional<int> tokens, std::optional<double> pricePerMillion);
```

```cpp
struct CostBreakdown {
  std::optional<double> input_cost_usd;
  std::optional<double> output_cost_usd;
  std::optional<double> reasoning_cost_usd;
  std::optional<double> cache_creation_cost_usd;
  std::optional<double> cache_read_cost_usd;
  std::optional<double> total_cost_usd;  // becomes estimated_cost_usd
};

CostBreakdown computeCostBreakdown(const holonight_config::PriceTable& priceTable,
                                   const QString& modelIdentifier, const holonight_domain::Usage& usage);
```

Per-category persisted fields collapse `CategoryCost` to the `{null, 0.0, >0}` convention:
`NotApplicable` or `Unknown` → `std::nullopt`; `Free` or `Priced` → `std::optional(value)` (0.0 for
Free). This is the exact "one bit of information deliberately thrown away, safely, because a
different always-present column already carries it" move described above.

**Why the total needs its own, more careful rule (REQ-F-006's last bullet: "Unknown if any component
is Unknown").** A naive total = "sum whatever categories computed a value, treat everything else as
0 contribution" is what the *old* `computeEstimatedCost()` effectively did (loop over five `if
(tokens && price) cost += ...`, otherwise silently add nothing) — and it is wrong for this cycle's
requirement: if `reasoning_tokens` is present (the category applies) but no `reasoning_
usd_per_million` is configured, the total must show `$ ?` (Unknown), not a partial sum that quietly
drops the unpriced category. The corrected total logic, built from the same `CategoryCost` values
already computed above (no duplicate work):

```
anyUnknown     = any of the five CategoryCost.state == Unknown
anyApplicable  = any of the five CategoryCost.state != NotApplicable
total_cost_usd = anyUnknown || !anyApplicable
                   ? std::nullopt                                    // Unknown, or nothing applied at all
                   : sum of value for every category whose state != NotApplicable   // Free contributes 0.0
```

This is a **deliberate, intentional behavior change** from the pre-this-cycle `computeEstimatedCost()`
for one specific edge case: a price-table entry that exists but has a gap for a category the
response actually used. Previously that silently produced a partial (too-low, not-obviously-wrong)
dollar figure; now it correctly surfaces as Unknown. REQ-F-002's own acceptance criterion ("these
five per-category columns shall always sum exactly to the existing estimated_cost_usd for any new
row") is satisfied by construction, since `estimated_cost_usd` is now *derived* from the same five
values rather than computed by a second, independent formula — the two are the same computation, not
two computations that happen to agree. See §8.2 for the one pre-existing test whose semantics this
does not change (no existing test exercises the "entry exists, one applicable category unpriced"
case — checked `tests/persistence/test_usage_persistence.cpp`), and §7.1 for the alternative
(persisting an explicit tri-state flag column) that was rejected.

**Free-total edge case**: if every applicable category is Free (price fields explicitly 0.0), `total_
cost_usd` = `0.0` (not null), which the display layer correctly renders as the literal `Free` text,
not `$0.0000` — same `{null, 0.0, >0}` convention, same reasoning as the per-category case, and here
"applicable" is unambiguous because `anyApplicable` was computed from real per-category
`NotApplicable` checks, not re-derived from a possibly-stale column later.

### 3.3 `MessageListModel::Roles` — appended entries

```cpp
enum Roles : std::uint16_t {
  IdRole = Qt::UserRole + 1,
  RoleRole,            // 258
  TextRole,            // 259
  StatusRole,          // 260
  CreatedAtRole,       // 261
  ModelNameRole,       // 262
  ContentBlocksRole,   // 263
  ProviderIdRole,      // 264
  ProviderTypeRole,    // 265
  ProviderNameRole,    // 266  <- existing last entry, unchanged value
  InputTokenCountRole,          // 267 — REQ-F-003
  OutputTokenCountRole,         // 268
  ReasoningTokenCountRole,      // 269
  CacheCreationTokenCountRole,  // 270
  CacheReadTokenCountRole,      // 271
  TotalTokenCountRole,          // 272
  DurationMsRole,               // 273
  InputCostUsdRole,             // 274
  OutputCostUsdRole,            // 275
  ReasoningCostUsdRole,         // 276
  CacheCreationCostUsdRole,     // 277
  CacheReadCostUsdRole,         // 278
  EstimatedCostUsdRole,         // 279
};
```

(Values shown assuming `Qt::UserRole == 256`, the standard Qt value — the point is they are
appended after `ProviderNameRole` with no renumbering of any existing entry, per the explicit
instruction; do not insert in the middle even for "logical grouping.")

`roleNames()` additions (exact QML-facing names, matching REQ-F-003's acceptance criterion verbatim):

```cpp
{InputTokenCountRole, QByteArrayLiteral("inputTokenCount")},
{OutputTokenCountRole, QByteArrayLiteral("outputTokenCount")},
{ReasoningTokenCountRole, QByteArrayLiteral("reasoningTokenCount")},
{CacheCreationTokenCountRole, QByteArrayLiteral("cacheCreationTokenCount")},
{CacheReadTokenCountRole, QByteArrayLiteral("cacheReadTokenCount")},
{TotalTokenCountRole, QByteArrayLiteral("totalTokenCount")},
{DurationMsRole, QByteArrayLiteral("durationMs")},
{InputCostUsdRole, QByteArrayLiteral("inputCostUsd")},
{OutputCostUsdRole, QByteArrayLiteral("outputCostUsd")},
{ReasoningCostUsdRole, QByteArrayLiteral("reasoningCostUsd")},
{CacheCreationCostUsdRole, QByteArrayLiteral("cacheCreationCostUsd")},
{CacheReadCostUsdRole, QByteArrayLiteral("cacheReadCostUsd")},
{EstimatedCostUsdRole, QByteArrayLiteral("estimatedCostUsd")},
```

`data()` pattern for every one of the 13 (illustrated for two; the rest are identical in shape):

```cpp
case InputTokenCountRole:
  return row.usage && row.usage->input_tokens ? QVariant(*row.usage->input_tokens) : QVariant();
case InputCostUsdRole:
  return row.usage && row.usage->input_cost_usd ? QVariant(*row.usage->input_cost_usd) : QVariant();
```

Returning a default-constructed `QVariant{}` (invalid) is what already happens for every
unmatched role in the existing `default: return {};` branch (message_list_model.cpp:100-101) — Qt's
QML list-model bridge exposes an invalid `QVariant` role value as `undefined` in QML, which is the
mechanism REQ-NF-003's "absent/undefined, not zero" requirement rides on. No new plumbing needed
here, just consistent use of the pattern already in the file.

`Row` gets exactly one new field:

```cpp
struct Row {
  // ...existing fields unchanged...
  std::optional<holonight_persistence::UsageRecord> usage;  // NEW — see §6.3 for why one field, not 13
};
```

`toRow()` does not touch `usage` (it has no usage data — `Message` doesn't carry it); every row
starts with `usage = std::nullopt`, which is exactly the "pre-feature historical message" /
"non-assistant message" / "non-terminal message" absent state REQ-C-002/REQ-C-001 need, for free.

New public method:

```cpp
// Sets row.usage for every row whose id is a key in usageByMessageId; rows with no matching key are
// left untouched (not cleared) — see §6.6 for why this single method serves both the batch-load
// path (REQ-F-001, called right after resetFromChronological(), where every row already starts
// nullopt) and the single-message live-completion path (REQ-F-003's "during each new message
// completion", called with a one-entry map). Emits dataChanged() with exactly the 13 usage roles
// for each row actually touched.
void applyUsageRecords(const QMap<QString, holonight_persistence::UsageRecord>& usageByMessageId);
```

### 3.4 SQL — `usageForConversation`

```sql
SELECT * FROM usage WHERE conversation_id = ? ORDER BY message_id ASC
```

Literal `SELECT *`, matching REQ-F-001's acceptance criterion exactly, and matching this file's
existing style of positional `query.value(N)` reads (`summaryFromRecord()`, `messageFromRecord()`)
rather than named-column access — column order must match `0001_init`'s original `CREATE TABLE
usage` column order followed by the two migrations' appended columns, in the exact order they were
added (input_cost_usd..cache_read_cost_usd last, per 0005's column order). **Note**: `ORDER BY
message_id ASC` sorts by the UUID string lexicographically, which is *not* actually chronological
message-creation order (UUIDs are not time-ordered) — this satisfies the acceptance criterion's
literal SQL text, but readers should not assume the returned row order is creation-order (REQ-F-001's
prose says "message creation order (ascending)"; the acceptance criterion's SQL does not achieve
that). This is harmless for this cycle because the consumer (`MessageListModel::applyUsageRecords`)
looks up by key in a `QMap`, never iterates in result order — see §8.8.

### 3.5 Cost/token formatting — the REQ-NF-002-driven C++/QML split

```cpp
// src/application/include/holonight_application/cost_formatting.h
namespace holonight_application {

// REQ-F-006/REQ-F-008. `cost`: an invalid QVariant means Unknown (no matching price — the caller
// must have already established the category/total is "applicable," e.g. by checking the
// corresponding token-count role is not undefined, before calling this — see §3.2's invariant);
// 0.0 means Free; any other finite value means Priced.
[[nodiscard]] QString formatCost(const QVariant& cost);

// REQ-F-005. `tokens`: an invalid QVariant returns an empty string (caller is expected to omit the
// badge/row entirely in that case, per REQ-F-004/007 — this function never fabricates a placeholder).
[[nodiscard]] QString formatTokenCount(const QVariant& tokens);

}  // namespace holonight_application
```

```cpp
// formatCost body sketch
if (!cost.isValid()) return QStringLiteral("$ ?");
const double value = cost.toDouble();
if (value == 0.0) return QStringLiteral("Free");
if (value > 0.0 && value < 0.0001) return QStringLiteral("<$0.0001");
return QStringLiteral("$ %1").arg(value, 0, 'f', 4);
```

```cpp
// src/application/include/holonight_application/token_cost_formatter.h
class TokenCostFormatter : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  QML_UNCREATABLE("Stateless formatting helper; call its methods directly, do not instantiate")

 public:
  Q_INVOKABLE static QString formatCost(const QVariant& cost);
  Q_INVOKABLE static QString formatTokenCount(const QVariant& tokens);
};
```

`TokenCostFormatter`'s two methods are one-line forwards to the free functions above. QML call
sites: `TokenCostFormatter.formatCost(model.estimatedCostUsd)`,
`TokenCostFormatter.formatTokenCount(model.inputTokenCount)`.

---

## 4. Key decisions with rationale

**4.1 Cost/token formatting logic lives in C++, exposed to QML via a thin singleton wrapper, not as
QML JS.** REQ-NF-002 mandates a "standalone GTest unit test" for the three-state cost formatter —
GTest cannot directly exercise a `.qml` file's inline JavaScript function. That single testability
requirement decides the split: the actual formatting logic (`formatCost`/`formatTokenCount`) is a
plain, `QObject`-free C++ function pair (`cost_formatting.h/.cpp`), unit-testable with zero Qt
meta-object overhead; a `QML_SINGLETON` wrapper (`TokenCostFormatter`) exists purely to make those
functions callable from QML, forwarding with no logic of its own. Token-count formatting (REQ-F-005)
has no equivalent NFR mandate, but is grouped into the same C++ pair for one reason: both formatters
are consumed by exactly the same call sites (the footer row and the popup), and splitting them across
C++ and QML would mean two different invocation styles at every one of those call sites for no
benefit.

**4.2 `TokenCostFormatter`'s methods take `QVariant`, not `std::optional<double>`/`std::optional
<int>`.** QML has no notion of `std::optional`; the natural QML-side "no value" is `undefined`,
which crosses into C++ as an invalid `QVariant` when passed through a `Q_INVOKABLE` call — not as an
empty `std::optional`. Accepting `QVariant` directly avoids an extra conversion layer at the QML/
C++ boundary and matches a pattern already used in this exact module for the analogous problem
(`decodeModelId(const QVariant&)` in `conversation_repository_worker.cpp:58-71`, which treats
`value.isNull()` as the "absent" signal).

**4.3 Per-category cost columns replace, rather than sit alongside, the previous
`computeEstimatedCost()` total-cost formula.** See §3.2 in full — the two are the same computation
now (`estimated_cost_usd` = the sum step of `computeCostBreakdown()`), which is the only way to
satisfy REQ-F-002's "these five columns shall always sum exactly to the existing estimated_cost_usd"
by construction rather than by two formulas that happen to agree today and could silently drift
apart under a future edit to either one.

**4.4 Live per-message usage data reaches `MessageListModel` via a new `usagePersisted` signal from
the write path, not by re-fetching or by duplicating cost computation in `ChatViewModel`.** Two
alternatives were available: (a) give `ChatViewModel` its own copy of `PriceTable` and recompute the
cost breakdown on the GUI thread the moment a `Completed` event arrives, synchronously populating
`MessageListModel`; or (b) have the worker emit the exact `UsageRecord` it just computed and wrote,
after the `INSERT` succeeds, and have `ChatViewModel` forward that into `MessageListModel`. (b) was
chosen: `PriceTable` is currently owned exactly once, by the worker thread, loaded at
`SqliteConversationRepository` construction with an explicit no-live-reload contract (REQ-C-006,
documented at `sqlite_conversation_repository.h:20-22`); giving `ChatViewModel` its own second copy
would either require plumbing the same `PriceTable` value to two owners (a duplication with no
enforced consistency mechanism) or re-reading `price_table.json` a second time from a different
thread, and either way would run the cost formula in two places in the codebase, which is exactly
the kind of duplication rejected in §4.3 for the same formula. (b) keeps `computeCostBreakdown()`
the single site the formula is ever evaluated at, and reuses the write path as its own delivery
mechanism instead of adding a second read.

**4.5 `applyUsageRecords()` is one method serving both the batch-load path and the single-message
live path**, taking a `QMap<QString, UsageRecord>` in both cases (a one-entry map for the live case).
See §6.6.

**4.6 `Row.usage` is a single `std::optional<UsageRecord>` field, not 13 flattened primitive
members.** `UsageRecord` already has exactly the shape needed (13 of its fields map 1:1 onto the 13
new roles); flattening would mean copying field names in two places (`UsageRecord` and `Row`) that
must be kept in sync by hand, with no compiler check that a field was copied correctly in either
direction. A single optional member means `applyUsageRecords()` is one assignment
(`row.usage = usageByMessageId.value(row.id)` — wait, must check presence first, see the actual
implementation note below) instead of 13, and `data()`'s 13 cases are all a one-line "does the
option and its inner optional field have a value" check with no risk of typo'd field-name drift.

**4.7 The five new SQL columns are `REAL DEFAULT NULL` `ALTER TABLE ADD COLUMN` statements, not a
table rebuild.** SQLite supports adding nullable columns to an existing table cheaply and without
touching existing rows (REQ-C-004's "no backfill" falls out of this for free — existing rows simply
never get values in the new columns, which is exactly the desired historical-row behavior).

---

## 5. Alternatives considered

**5.1 Free/Priced/Unknown persistence — considered and rejected: a fourth, explicit tri-state
column (or a bitmask of five tri-state flags) alongside the numeric cost.** E.g. `input_cost_state
TEXT CHECK(input_cost_state IN ('unknown','free','priced'))` per category, five extra columns beyond
the five cost columns already added. This would remove any need for the "token-count-column-as-tie-
breaker" reasoning in §3.2 and make the per-category state directly readable without cross-
referencing another column. It was rejected for three reasons: (a) REQ-F-002 explicitly specifies
only five new nullable REAL columns, not ten; adding a parallel state column per category would be
scope creep against an approved spec. (b) The `{null, 0.0, >0}` convention this design uses instead
is not a new invention — it is the exact convention `TokenPricing` itself already uses at the
config layer (`price_table.h:10-15`'s own doc comment: "an absent field means unknown... a model
asserting a verified-free rate sets the field to 0.0 explicitly"); reusing the same convention one
layer down, at the persisted-cost layer, keeps the "null vs. 0.0 vs. real value" mental model
identical across the whole cost-tracking feature rather than introducing a second, different
convention only for the UI-facing columns. (c) It was verified (§3.2) that the convention round-trips
correctly for every case the spec actually requires, given the token-count gate already mandated by
REQ-F-007 — the extra columns would carry no information the existing columns don't already
determine once that gate is applied.

**5.2 Info popup — considered and rejected: an inline anonymous `Popup { ... }` block directly inside
`MessageBubble.qml`, mirroring `QuickPanelHeader.qml`'s `dropdownPopup`.** `QuickPanelHeader.qml`'s
popup is a reasonable precedent for *a* popup living inline, but the two cases differ in an important
way: `QuickPanelHeader` is instantiated exactly once per application window, so its popup is a
one-off piece of that header's own state. `MessageBubble` is instantiated once *per assistant
message* — every visible bubble in a long conversation gets its own footer and, on this design,
its own popup instance. Inlining a two-column grid layout (Token Breakdown + Cost Breakdown, up to
six rows each) directly inside `MessageBubble.qml`'s body would roughly double that file's size and
make `MessageBubble.qml` responsible for two visually and logically distinct pieces of UI (the
message content itself, and the stats breakdown), where today it is responsible for one. A separate
`ResponseStatsPopup.qml` file keeps `MessageBubble.qml`'s growth to the footer *trigger* row only (a
handful of badges plus one button), keeps the popup independently reviewable/lintable
(`task qml-lint`), and matches this codebase's existing convention of extracting anything with its
own internal layout into its own file even when only used from one place today (`UserMessageCard.qml`,
`ProviderIcon.qml`, `MarkdownBlock.qml`, `HnCodeBlock.qml` are all separate files, each instantiated
from exactly one call site in `MessageBubble.qml`/`MessageList.qml`).

**5.3 Batch usage load — considered and rejected: fold the usage query into `loadConversation()`
itself** (extend `LoadedConversation` with a `QMap<QString, UsageRecord> usage` field, populated by
the same worker method, delivered on the existing `conversationLoaded` signal, no new interface
method at all). This would avoid a second round-trip and second signal wiring entirely, and would
guarantee the conversation and its usage data always arrive atomically as one unit. It was rejected
because REQ-F-001's acceptance criterion explicitly requires `ConversationRepository` to "define a
new interface method `usageForConversation`" — the spec calls this out as its own named, testable
seam (REQ-NF-001 wants a unit test that "directly invokes `usageForConversation()`"), which an
implicit fold into `loadConversation()` would not provide as cleanly (the test would need to invoke
`loadConversation()` and assert on a nested field, coupling the two tests together). The two-signal
approach costs one extra async round-trip per conversation open, which is imperceptible against the
existing multi-query `loadConversation()` implementation already doing three sequential queries
per open (`conversationQuery`, `settleAbandoned`, `messagesQuery` — `conversation_repository_worker.
cpp:373-409`).

---

## 6. Known risks

**6.1 `migration_runner.cpp`'s hardcoded `builtInMigrations()` list (flagged in CLAUDE.md and prior-
cycle memory).** Adding `0005_add_usage_cost_breakdown.sql` to `src/persistence/migrations/` and to
`CMakeLists.txt`'s `qt_add_resources` call is not sufficient by itself — `MigrationRunner::
builtInMigrations()` is a hand-written `std::vector<Migration>` literal (migration_runner.cpp:28-51)
that must get a 5th `Migration{.version = 5, ...}` entry, or the new SQL file is compiled into the
resource bundle but never applied to any real database. This is a three-file change (`.sql` file +
`CMakeLists.txt` + `migration_runner.cpp`), and it is easy to do only the first two and ship a
migration that silently never runs (no error at startup — `MigrationRunner::apply()` just sees no
migration with `version > currentVersion()` beyond whatever the hardcoded list already had).

**6.2 `MessageListModel::Roles` numbering fragility.** The 13 new roles must be appended strictly
after `ProviderNameRole` with no renumbering. If a future change inserts a role in the middle of the
existing block (e.g. for "logical grouping" with the fields it's related to), every role value after
the insertion point shifts, silently breaking any QML/C++ code that captured a numeric role value
(there is none today that this design is aware of, but `Q_ENUM(Roles)` exposes these to QML by name,
not by number, which is the existing mitigation — as long as QML always refers to roles by their
`roleNames()` string, not the numeric enum value, a shift is harmless; this only matters for C++
code, if any is ever written, that hardcodes a numeric role value instead of the enum symbol).

**6.3 QML absence-vs-`undefined` footguns.** `required property var xTokenCount` bound from a role
that returns an invalid `QVariant` becomes `undefined` in QML, not `null` and not `0` — code must
test with `!== undefined` (as REQ-NF-003 explicitly calls out), never a truthiness check
(`if (!model.inputTokenCount)` incorrectly treats a genuine `0` token count the same as "absent",
though in practice a `0`-token category is unlikely to occur; still, `formatTokenCount`/`formatCost`
above are written to check `QVariant::isValid()`, not truthiness, specifically to avoid this class of
bug). A second, QML-specific footgun: `required property var` fields that are never assigned a value
at all (rather than explicitly bound to `undefined`) can trip QML's "required property was not
provided" runtime error in some Qt versions if the delegate's model doesn't expose that role name at
all — this cannot happen here because `MessageListModel::roleNames()` always registers all 13 role
names for every row (the role name is always present in the model; only its *value* is sometimes
`undefined`), which is the correct side of that distinction.

**6.4 Threading: the batch query, plus the live single-row push, are two separate signals that must
both land before the UI is fully consistent.** Since `usageForConversation()`'s result and a
subsequent `usagePersisted()` (if a new message completes while the batch load is still in flight —
unlikely in practice but not impossible if a user switches into a conversation and immediately sends
a message before the batch query's queued signal has been delivered) both ultimately call
`applyUsageRecords()`, and that method never clears rows absent from its argument, the two can
interleave in either order without data loss or corruption — whichever arrives first populates its
rows, the other adds/overwrites its own rows on top. No additional synchronization is needed, but
this should be called out explicitly in Stage 4 as the reason `applyUsageRecords()` must never be
implemented as "clear everything, then populate from the map" (which would make the interleaving
order-dependent and lossy).

**6.5 `estimated_cost_usd` semantics change for one previously-untested edge case.** As detailed in
§3.2/§4.3, a price-table entry that exists but is missing a price for a category the response
actually used now yields `estimated_cost_usd = NULL` (Unknown) instead of the old code's silent
partial sum. No existing test in `tests/persistence/test_usage_persistence.cpp` exercises this exact
case (checked: `SumPerTurnCostsNoDedup` and `CostFrozenAtWriteNotRecomputedWhenPriceTableChanges`
both use price tables with prices for every category the test's `Usage` values populate), so no
existing test needs to change, but Stage 4 should add a new test asserting the corrected behavior
explicitly (`ReqF002PartialPriceTableEntryYieldsUnknownTotal` or similar), since this is exactly the
kind of behavior a future maintainer could "fix" back to the old, wrong, silently-partial-sum
behavior without a regression test pinning it down.

**6.6 `UsageRecord` crossing a real Qt signal boundary for the first time.** Every existing
cross-thread delivery in `ConversationRepositoryWorker`'s signals already required `Q_DECLARE_
METATYPE` + `qRegisterMetaType()` for its custom payload types (`ConversationSummary`,
`LoadedConversation`, `ModelId` — see `conversation_record.h:40-42`/`conversation_record.cpp`'s
`registerMetaTypes()`). `UsageRecord` has never needed this before (`persistUsage()`'s existing
worker-thread dispatch uses a functor-based `QMetaObject::invokeMethod()`, which captures arguments
by value in the lambda closure and does not need `QMetaType` registration at all — see the
docstring at `sqlite_conversation_repository.h:14-15`). The *new* `usagePersisted`/
`usageForConversationLoaded` signals are genuine Qt signal/slot connections crossing the worker-
thread boundary, which do go through the `QMetaType` system for cross-thread queuing — forgetting
to add `Q_DECLARE_METATYPE(holonight_persistence::UsageRecord)` to `usage_record.h` and
`qRegisterMetaType<holonight_persistence::UsageRecord>()` to `registerMetaTypes()` in
`conversation_record.cpp` produces a runtime warning ("Cannot queue arguments of type
'UsageRecord'...") and the connection silently drops the call — no compile error, easy to miss until
manually testing the live-completion path end to end.

---

## 7. Testing strategy (pointers for Stage 3/4, not exhaustive)

- **`computeCostBreakdown()` / `evaluateCategory()`** — new GTest cases in (or alongside)
  `tests/persistence/test_usage_persistence.cpp`: Free (price 0.0), Priced (nonzero), Unknown (no
  price-table entry at all), Unknown (entry exists, category token present, that category's price
  field absent — the corrected-behavior case from §6.5), and the sub-$0.0001-but-nonzero edge case.
- **`usageForConversation()`** — new GTest against `ConversationRepositoryWorker` directly (same
  pattern `UsagePersistenceTest` already uses: in-memory `:memory:` SQLite, `QSignalSpy` on the new
  `usageForConversationLoaded` signal), asserting the returned map's contents and its message_id-
  ascending order (REQ-NF-001), and asserting exactly one `SELECT` is issued (REQ-NF-004 — countable
  via a `QSqlQuery`-wrapping spy or by asserting on call count if the worker is instrumented, or more
  simply by construction/code-review of the implementation having a single `query.exec()` call).
- **`formatCost()`/`formatTokenCount()`** — new standalone GTest file (e.g.
  `tests/application/test_cost_formatting.cpp`), covering every REQ-NF-002 case directly with no Qt
  event loop or QML engine needed at all, since these are plain functions.
- **`MessageListModel::applyUsageRecords()`** — new GTest cases verifying: absent roles for
  unmatched rows, correct role values and `dataChanged` role list for matched rows, and that calling
  it twice (batch then live-style single-entry) does not clear previously-applied rows (§6.4).
- No QML/visual test is required or expected (REQ-NF-001 explicitly waives this for the read path;
  per this project's established policy — see prior-cycle memory on "no visual verification" — QML
  footer/popup rendering is verified by the user manually, not by an automated screenshot/Xvfb check).
