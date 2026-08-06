# SDD Tasks — usage-cost-tracking

- [x] T-001: Add Usage struct to holonight_domain::stream_event.h
  - REQs: REQ-F-001, REQ-F-002
  - Check: stream_event.h declares Usage struct with six independently-optional token fields plus duration_ms and four Ollama-native timing fields, and Completed/Error/Cancelled each have std::optional<Usage> member.

- [x] T-002: Add domain construction and equality tests for Usage
  - REQs: REQ-F-001, REQ-F-002
  - Check: tests/domain/test_stream_event.cpp contains EXPECT_EQ assertions that default-constructed Completed{} has usage==std::nullopt, and Completed{.usage=Usage{.input_tokens=50}} constructs and compares equal to an identical instance.

- [x] T-003: Add Clock interface to holonight_providers
  - REQs: REQ-NF-001
  - Check: src/providers/include/holonight_providers/clock.h defines Clock base class with virtual now() returning std::chrono::milliseconds, and SteadyClock implementation returning std::chrono::steady_clock::now().

- [x] T-004: Add FakeClock test double for holonight_providers
  - REQs: REQ-NF-001
  - Check: tests/providers/fake_clock.h defines FakeClock with push(qint64) method; FakeClock().push(1000).push(2500) followed by two now() calls returns 1000 then 2500.

- [x] T-005: Add Clock interface tests
  - REQs: REQ-NF-001
  - Check: tests/providers/test_clock.cpp contains EXPECT_GT assertions that SteadyClock::now() returns positive milliseconds, and FakeClock queued-value round-trip test asserts exact values 1000 and 2500 returned in sequence.

- [x] T-006: Add Usage accumulation to Ollama provider adapter
  - REQs: REQ-F-003, REQ-F-012
  - Check: src/providers/src/ollama_provider.cpp StreamContext contains holonight_domain::Usage usage member; done:true chunk parsing populates input_tokens, output_tokens, total_tokens, and four ollama_*_duration_ns fields; Completed event carries populated usage.

- [x] T-007: Add Ollama provider usage-parsing tests
  - REQs: REQ-F-003, REQ-F-002, REQ-F-012
  - Check: tests/providers/test_ollama_provider.cpp contains TEST(Ollama, SendChatPopulatesUsageFromDone) asserting EXPECT_EQ(usage.input_tokens, 42) given mock done:true with prompt_eval_count:42, and a field-omitted test asserting null reasoning_tokens when not present.

- [x] T-008: Add Usage accumulation to OpenAI provider adapter
  - REQs: REQ-F-004
  - Check: src/providers/src/openai_provider.cpp StreamContext contains holonight_domain::Usage usage member; response.completed event parsing extracts usage object nested under response field, populating input_tokens, output_tokens, cache_read_tokens (from input_tokens_details.cached_tokens), reasoning_tokens (from output_tokens_details.reasoning_tokens).

- [x] T-009: Add OpenAI provider usage-parsing tests
  - REQs: REQ-F-004, REQ-F-002
  - Check: tests/providers/test_openai_provider.cpp contains TEST(OpenAI, SendChatPopulatesUsageFromResponseCompleted) asserting EXPECT_EQ(usage.input_tokens, 50) given mock response.completed with usage.input_tokens:50, and a field-omitted test asserting null cache_read_tokens when input_tokens_details.cached_tokens absent.

- [x] T-010: Add Usage accumulation to Anthropic provider adapter
  - REQs: REQ-F-005
  - Check: src/providers/src/anthropic_provider.cpp StreamContext contains holonight_domain::Usage usage member; message_start populates input_tokens and output_tokens; message_delta replaces output_tokens (not sums) and adds cache_creation_tokens, cache_read_tokens; Completed carries final accumulated usage.

- [x] T-011: Add Anthropic provider usage-parsing tests
  - REQs: REQ-F-005, REQ-F-002
  - Check: tests/providers/test_anthropic_provider.cpp contains TEST(Anthropic, MessageDeltaOverwritesNotSums) sending message_start with output_tokens:1 then message_delta with output_tokens:20, asserting final usage.output_tokens==20 (not 21), and a field-omitted test asserting null reasoning_tokens when not present.

- [x] T-012: Add Usage accumulation to Google provider adapter
  - REQs: REQ-F-006
  - Check: src/providers/src/google_provider.cpp StreamContext contains holonight_domain::Usage usage member; every chunk's usageMetadata overwrites prior values; final chunk's values survive terminal event; fields map to input_tokens, output_tokens, reasoning_tokens (thoughtsTokenCount), cache_read_tokens (cachedContentTokenCount), total_tokens.

- [x] T-013: Add Google provider usage-parsing tests
  - REQs: REQ-F-006, REQ-F-002
  - Check: tests/providers/test_google_provider.cpp contains TEST(Google, FinalChunkWins) sending two chunks with differing usageMetadata, asserting final usage reflects only the last chunk's values, and a field-omitted test asserting null reasoning_tokens when thoughtsTokenCount absent.

- [x] T-014: Add PriceTable and TokenPricing types to holonight_config
  - REQs: REQ-F-007
  - Check: src/config/include/holonight_config/price_table.h declares TokenPricing struct with six independently-optional double fields (usd_per_million), and PriceTable struct with QHash<QString, TokenPricing> keyed by exact model identifiers.

- [x] T-015: Add PriceTableRepository to holonight_config
  - REQs: REQ-F-007, REQ-F-008
  - Check: src/config/include/holonight_config/price_table_repository.h declares PriceTableRepository class with load() returning PriceTable and pricingFor(table, model_identifier) returning std::optional<TokenPricing> (nullopt for unlisted models).

- [x] T-016: Add resolvePriceTableFilePath() to holonight_config
  - REQs: REQ-F-007
  - Check: src/config/include/holonight_config/config_path.h declares resolvePriceTableFilePath() function returning path to price_table.json in $XDG_CONFIG_HOME/holonight-ai/, mirroring existing resolveConfigFilePath() pattern.

- [x] T-017: Implement PriceTableRepository load and pricingFor methods
  - REQs: REQ-F-007
  - Check: src/config/src/price_table_repository.cpp load() returns empty PriceTable without throwing on missing/malformed file; pricingFor() returns std::nullopt for unlisted model, never zero-valued TokenPricing.

- [x] T-018: Add PriceTableRepository tests
  - REQs: REQ-F-007, REQ-F-008
  - Check: tests/config/test_price_table_repository.cpp contains TEST(PriceTableRepository, ListedModelReturnsPrice) and TEST(PriceTableRepository, UnlistedModelReturnsNullopt); missing-file and malformed-JSON tests both assert load() succeeds returning empty table.

- [x] T-019: Add database migration 0004_add_usage.sql
  - REQs: REQ-F-009, REQ-F-010
  - Check: src/persistence/migrations/0004_add_usage.sql creates usage table with message_id/conversation_id foreign keys, sixteen token/timing columns (all nullable INTEGER or REAL), unique index on message_id, index on conversation_id.

- [x] T-020: Add UsageRecord struct to holonight_persistence
  - REQs: REQ-F-009, REQ-F-010, REQ-NF-002
  - Check: src/persistence/include/holonight_persistence/usage_record.h declares UsageRecord struct holding conversationId, messageId, modelIdentifier, six token counts, four Ollama fields, duration_ms, and estimated_cost_usd (all std::optional except modelIdentifier).

- [x] T-021: Add persistUsage() method to ConversationRepository interface
  - REQs: REQ-F-009, REQ-F-010
  - Check: src/persistence/include/holonight_persistence/conversation_repository.h adds pure virtual persistUsage(conversationId, messageId, modelIdentifier, usage) method; implementation skeleton added to sqlite_conversation_repository.h.

- [x] T-022: Implement persistUsage() in ConversationRepositoryWorker
  - REQs: REQ-F-008, REQ-F-009, REQ-F-010
  - Check: src/persistence/src/detail/conversation_repository_worker.cpp persistUsage() resolves model pricing, computes estimated_cost as Σ(tokens_i × price_i), inserts one row into usage table with frozen cost; no recomputation on read.

- [x] T-023: Add persistUsage() implementation to SqliteConversationRepository
  - REQs: REQ-F-009, REQ-F-010
  - Check: src/persistence/src/sqlite_conversation_repository.cpp forwards persistUsage() call to worker thread via existing QueuedConnection signal/slot pattern.

- [x] T-024: Register new migration and files in persistence CMakeLists.txt
  - REQs: REQ-F-009
  - Check: src/persistence/CMakeLists.txt includes 0004_add_usage.sql in migrations registration; UsageRecord source files listed in target_sources.

- [x] T-025: Add persistence round-trip tests for Usage
  - REQs: REQ-F-009, REQ-F-010, REQ-NF-002
  - Check: tests/persistence/test_conversation_repository.cpp contains TEST(ConversationRepository, PersistUsageRoundTrip) asserting all fields round-trip via QSqlQuery, including NULL for unset optionals, and TEST(ConversationRepository, OneUsageRowPerAssistantMessage) verifies N assistant messages produce exactly N usage rows.

- [x] T-026: Add migration-order test for usage table
  - REQs: REQ-F-009
  - Check: tests/persistence/test_conversation_repository.cpp contains TEST(MigrationRunner, CreateUsageTableInOrder) applying migrations 0001-0004 in sequence against temp database and verifying usage table exists with expected columns.

- [x] T-027: Add cost-freezing test for usage persistence
  - REQs: REQ-F-009, REQ-C-005
  - Check: tests/persistence/test_conversation_repository.cpp contains TEST(ConversationRepository, CostFrozenAtWrite) persisting usage with price X, then persisting new usage with price 2X, re-reading first row and asserting estimated_cost unchanged at original value.

- [x] T-028: Inject Clock into ChatController
  - REQs: REQ-F-011, REQ-NF-001
  - Check: src/application/include/holonight_application/chat_controller.h ChatController constructor accepts std::shared_ptr<holonight_providers::Clock> parameter; stores as clock_ member; default constructed parameter uses SteadyClock.

- [x] T-029: Add dispatch_time_ field to InFlightStream
  - REQs: REQ-F-011
  - Check: src/application/include/holonight_application/chat_controller.h InFlightStream struct adds std::chrono::milliseconds dispatch_time_ member; ChatController::startStream() initializes it via clock_->now().

- [x] T-030: Stamp duration_ms into Usage at terminal event in ChatController
  - REQs: REQ-F-011, REQ-NF-001
  - Check: src/application/src/chat_controller.cpp handleStreamEvent() std::visit branch for each terminal variant (Completed, Error, Cancelled) calls clock_->now(), computes duration_ms, stamps it into terminal event's Usage before forwarding to on_event callback; synthetic Cancelled in stop() does same.

- [x] T-031: Wire PriceTableRepository into ChatViewModel composition
  - REQs: REQ-F-008, REQ-F-009
  - Check: src/application/src/chat_view_model.cpp ChatViewModel construction accepts PriceTableRepository instance and stores as member; passed to repository worker thread at construction.

- [x] T-032: Wire persistUsage() call into ChatViewModel's isTerminal branch
  - REQs: REQ-F-010
  - Check: src/application/src/chat_view_model.cpp onStreamEvent() isTerminal branch extracts modelIdentifier from terminal event (via provider adapter's captured response.model/message.model/modelVersion), extracts std::optional<Usage>, calls repository_->persistUsage(conversationId, messageId, modelIdentifier, usage) for assistant messages with non-null usage.

- [x] T-033: Wire Clock injection at apps/chat composition root
  - REQs: REQ-F-011, REQ-NF-001
  - Check: apps/chat/src/main.cpp or composition entrypoint constructs SteadyClock instance and passes to ChatController constructor; tests can inject FakeClock via same path.

- [x] T-034: Add ChatController timing tests with FakeClock
  - REQs: REQ-F-011, REQ-NF-001
  - Check: tests/application/test_chat_controller.cpp (new or extended) contains TEST(ChatController, DurationMsComputedExactly) constructing controller with FakeClock pushing 1000 then 2500, sending message through FakeHttpClient, asserting terminal event's usage.duration_ms==1500 exactly.

- [x] T-035: Add no-dedup cost-sum integration test
  - REQs: REQ-F-008, REQ-NF-003
  - Check: tests/persistence/test_conversation_repository.cpp or tests/application/ contains TEST(CostIntegration, SumPerTurnCostsNoDedup) driving two conversation turns via same model, computing cost1 and cost2 from stored tokens, asserting total == cost1 + cost2 (not deduplicated).

- [x] T-036: Full test suite pass and backward-compat verification
  - REQs: All
  - Check: `task test` passes all tests including new suite; `task build` compiles without warnings; existing code that constructs Completed{} without .usage argument still compiles unchanged.
