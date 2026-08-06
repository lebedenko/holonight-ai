# SDD Tasks — response-footer-stats

## Scope Revision (2026-08-03)

Cost/pricing display was removed from this feature after T-023 manual verification surfaced that
`$ ?` (Unknown) rendered for every provider, not just Ollama. Investigation (real API calls against
Ollama, OpenAI, Anthropic, Google — both chat-completion and models-list endpoints) confirmed none
of the four providers expose pricing data through any API; the only source was a hand-maintained
`price_table.json` nobody would realistically keep current for every model. Rather than patch that
gap, cost/pricing was removed entirely, cycle-wide (see `usage-cost-tracking/SPEC.md`'s matching
scope-revision note for the backend-half of this decision). Token counts (T/C/R), duration, and the
info popup's Token Breakdown column are unaffected — those are real, unambiguous provider data with
no external dependency.

Tasks below (T-001 through T-022) are left as an accurate historical record of what was built and
then removed; they are **not** representative of the current codebase. T-024 records the removal
itself. T-023's Check criterion has been revised to drop cost-specific assertions.

## Persistence Layer (Schema & Migration)

- [x] T-001: Create migration file 0005_add_usage_cost_breakdown.sql
  - REQs: REQ-F-002
  - Check: Migration file contains exactly five ALTER TABLE usage ADD COLUMN statements, each adding a REAL DEFAULT NULL column (input_cost_usd, output_cost_usd, reasoning_cost_usd, cache_creation_cost_usd, cache_read_cost_usd).

- [x] T-002: Register migration file in CMakeLists.txt
  - REQs: REQ-F-002
  - Check: CMakeLists.txt qt_add_resources(... FILES ...) includes migrations/0005_add_usage_cost_breakdown.sql as a new line entry.

- [x] T-003: Register migration entry in migration_runner.cpp builtInMigrations()
  - REQs: REQ-F-002
  - Check: migration_runner.cpp builtInMigrations() returns exactly 5 Migration struct entries (version 1–5), with version 5 pointing to "0005_add_usage_cost_breakdown".

## Persistence Layer (UsageRecord & Cost Computation)

- [x] T-004: Extend UsageRecord struct with per-category cost fields and add Q_DECLARE_METATYPE
  - REQs: REQ-F-002, REQ-F-003, REQ-NF-003
  - Check: UsageRecord has five new std::optional<double> fields (input_cost_usd, output_cost_usd, reasoning_cost_usd, cache_creation_cost_usd, cache_read_cost_usd); Q_DECLARE_METATYPE and qRegisterMetaType are defined.

- [x] T-005: Implement computeCostBreakdown() and evaluateCategory() functions
  - REQs: REQ-F-002, REQ-F-006, REQ-F-008
  - Check: computeCostBreakdown() returns CostBreakdown struct with six fields; evaluateCategory() correctly classifies each category as Free (0.0)/Priced (>0)/Unknown (null price); total is Unknown if any component is Unknown, Free if all applicable are Free, summed value otherwise.

- [x] T-006: Modify persistUsage() to compute per-category costs and bind five new INSERT columns
  - REQs: REQ-F-001, REQ-F-002, REQ-F-006
  - Check: persistUsage() calls computeCostBreakdown(), binds five per-category cost columns in the INSERT statement, and sets estimated_cost_usd to the computed total.

- [x] T-007: Add usagePersisted signal to persistUsage() and emit after successful INSERT
  - REQs: REQ-F-001
  - Check: persistUsage() emits usagePersisted(QString conversationId, QString messageId, UsageRecord record) signal after successful database INSERT with fully-populated UsageRecord.

## Persistence Layer (Repository & Batch Load)

- [x] T-008: Implement usageForConversation() worker method and emit usageForConversationLoaded signal
  - REQs: REQ-F-001, REQ-NF-001
  - Check: ConversationRepositoryWorker::usageForConversation() executes SELECT * FROM usage WHERE conversation_id = ? ORDER BY message_id ASC, builds QMap<QString messageId, UsageRecord>, and emits usageForConversationLoaded(QString conversationId, QMap<QString, UsageRecord>).

- [x] T-009: Add usageForConversation() interface and two new signals to ConversationRepository/SqliteConversationRepository
  - REQs: REQ-F-001
  - Check: ConversationRepository declares pure-virtual void usageForConversation(QString conversationId), two new Q_SIGNALS (usageForConversationLoaded, usagePersisted); SqliteConversationRepository forwards the call to worker thread.

## Application Layer (Formatting Helpers)

- [x] T-010: Implement cost_formatting.h/cpp with formatCost() and formatTokenCount() free functions
  - REQs: REQ-F-005, REQ-F-006, REQ-F-008, REQ-NF-002
  - Check: formatCost(QVariant) returns "Free" for 0.0, "$ X.XXXX" (4 decimals) for nonzero, "<$0.0001" for rounded-to-zero nonzero, "$ ?" for invalid/null; formatTokenCount(QVariant) returns "N.NK" (1 decimal) for ≥1000 or exact integer for <1000.

- [x] T-011: Implement TokenCostFormatter QML singleton wrapper
  - REQs: REQ-F-005, REQ-F-006
  - Check: TokenCostFormatter declared QML_ELEMENT, QML_SINGLETON, QML_UNCREATABLE; exposes Q_INVOKABLE static formatCost(QVariant) and formatTokenCount(QVariant) methods callable from QML.

## Application Layer (MessageListModel)

- [x] T-012: Extend MessageListModel::Roles enum and implement applyUsageRecords() method
  - REQs: REQ-F-003, REQ-NF-003
  - Check: Roles enum has 13 new entries appended after ProviderNameRole (inputTokenCount, outputTokenCount, reasoningTokenCount, cacheCreationTokenCount, cacheReadTokenCount, totalTokenCount, durationMs, inputCostUsd, outputCostUsd, reasoningCostUsd, cacheCreationCostUsd, cacheReadCostUsd, estimatedCostUsd); Row struct has std::optional<UsageRecord> usage field; applyUsageRecords(QMap<QString, UsageRecord>) sets row.usage for matching message IDs and emits dataChanged(index, index, {13 role constants}).

- [x] T-013: Update MessageListModel::roleNames() and data() for 13 new roles
  - REQs: REQ-F-003, REQ-NF-003
  - Check: roleNames() returns correct string names for all 13 new roles; data() returns invalid QVariant (undefined in QML) when row.usage is absent, and returns token/cost/duration values when present (not zero-filled).

## Application Layer (ChatViewModel)

- [x] T-014: Update ChatViewModel::onConversationLoaded() to batch-load usage and add signal slots
  - REQs: REQ-F-001
  - Check: onConversationLoaded() calls repository_->usageForConversation(loaded.summary.id) immediately after adoptConversation(); two new private slots onUsageForConversationLoaded and onUsagePersisted guard against stale conversations (conversationId == active_conversation_id_).

## QML Layer (MessageList & MessageBubble)

- [x] T-015: Update MessageList.qml to thread 13 usage properties into MessageBubble
  - REQs: REQ-F-003
  - Check: messageDelegate required properties include all 13 usage-related roles; each property is forwarded to MessageBubble instantiation; qmllint reports no undefined-property errors.

- [x] T-016: Add footer badge row to MessageBubble.qml
  - REQs: REQ-F-004, REQ-F-005
  - Check: Footer RowLayout renders as the last child of content ColumnLayout, visible only when (isAssistant && messageStatus === "complete" && usage !== undefined); row contains badges for prompt tokens (T), completion tokens (C), reasoning tokens (R, if defined), duration (Xs/Xms if defined), cost (three-state), and clickable info icon; footer is absent in quick-panel surface.

## QML Layer (Popup)

- [x] T-017: Implement ResponseStatsPopup.qml
  - REQs: REQ-F-007, REQ-F-006, REQ-C-003
  - Check: Popup displays two-column layout (left: Token Breakdown with comma-separated integers and percentages; right: Cost Breakdown USD with three-state formatting); omits rows for absent token counts; applies three-state cost logic to total; uses HnSurfaceFrame for styling; responds to Escape and outside click.

## Testing (Cost Computation & Edge Cases)

- [x] T-018: Write GTest for computeCostBreakdown() and evaluateCategory() logic
  - REQs: REQ-NF-002
  - Check: GTest covers Free (0.0 price), Priced (nonzero value), Unknown (null price), sub-$0.0001 rounding, and correct total logic (Unknown if any component Unknown, Free if all Free, summed value otherwise).

- [x] T-019: Write regression test for partial price-table entry edge case
  - REQs: REQ-F-002, REQ-F-006
  - Check: GTest verifies that a price-table entry missing one applicable category's price correctly yields Unknown total ($ ?) and does not produce a partial/low-ball cost value.

## Testing (Batch Load & Persistence)

- [x] T-020: Write GTest for usageForConversation() batch query
  - REQs: REQ-F-001, REQ-NF-001
  - Check: GTest uses :memory: SQLite, QSignalSpy captures usageForConversationLoaded signal, returned QMap contains all and only usage records for the specified conversation, in any order (key lookup used, not iteration order).

## Testing (Formatting)

- [x] T-021: Write GTest for formatCost() and formatTokenCount() functions
  - REQs: REQ-F-005, REQ-F-006, REQ-NF-002
  - Check: GTest exercises all three cost states (Free/$X.XXXX/$ ?), edge case (<$0.0001), and token formatting (K abbreviation, no comma in badge context, plain integers <1000).

## Testing (MessageListModel Behavior)

- [x] T-022: Write GTest for MessageListModel::applyUsageRecords() behavior
  - REQs: REQ-F-003, REQ-NF-003
  - Check: GTest verifies applyUsageRecords() sets row.usage for matching message IDs, emits dataChanged for correct 13 role constants, does not clear non-matching rows, and handles repeated calls without data loss.

## Manual Verification

- [x] T-023: Manual verification of footer stats rendering in running app
  - REQs: REQ-F-004, REQ-F-007, REQ-C-001, REQ-C-002, REQ-C-003, REQ-C-007
  - Check (revised, cost assertions removed per Scope Revision above): Completed assistant messages in workspace window show footer with badges (tokens/duration rendered per formatting rules, omitted if absent); non-completed messages lack footer; pre-feature historical messages lack footer; quick-panel surface has no footer row; clicking info icon opens popup with Token Breakdown; popup closes on Escape or outside click.
  - Outcome: surfaced the universal-Unknown-cost bug during manual testing, which led to the Scope Revision above; footer/token/duration/popup behavior otherwise confirmed working (including a real popup-visibility bug fixed the same session: Popup wasn't reparented to Overlay.overlay, so it rendered underneath the opaque message bubble).

## Removal (Scope Revision)

- [x] T-024: Remove cost/pricing entirely, keep tokens/duration
  - REQs: supersedes REQ-F-002, REQ-F-006, REQ-F-008, REQ-C-006, REQ-C-009, REQ-C-010 (all voided); trims REQ-F-003/004/007 to their token/duration portions
  - Check: migration 0006_drop_usage_cost_columns.sql drops all 6 cost columns from `usage` (forward-only, does not edit migrations 0004/0005); `UsageRecord`/`toUsageRecord()` carry no cost fields; `computeCostBreakdown`/`evaluateCategory`/`CostBreakdown` deleted from conversation_repository_worker.cpp; `holonight_config::PriceTable`/`PriceTableRepository`/`resolvePriceTableFilePath()` deleted entirely; `cost_formatting.h/cpp` and `TokenCostFormatter` replaced by `token_formatting.h/cpp` and `TokenFormatter` (formatTokenCount only); `MessageListModel` keeps only the 7 token/duration roles; `MessageBubble.qml` cost badge and `ResponseStatsPopup.qml` Cost Breakdown column removed; all cost-only tests deleted, remaining tests updated; full suite green (518/518), qmllint clean.
