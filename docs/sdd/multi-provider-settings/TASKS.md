# Multi-Provider Settings Tasks

Status: Complete

All implementation and acceptance tasks are complete. Requirement IDs refer to `SPEC.md`;
implementation details refer to `DESIGN.md`.

## Phase 1: Configuration types and schema migration

- [x] T-001: Define `ProviderType`, typed `ProviderSettings` variant, `ProviderInstanceConfig`,
  `ProviderTombstone`, and ordered `ProviderState`, including equality and stable serialization
  helpers. (REQ-F-001, REQ-F-009, REQ-F-012, REQ-NF-002; DESIGN §§1–2)
- [x] T-002: Replace fixed per-provider repository load/save entry points with whole-state
  operations that preserve unrelated root keys and atomically write through `QSaveFile`.
  (REQ-F-013; DESIGN §2)
- [x] T-003: Implement the one-time legacy migration: migrate only physically present valid provider
  objects, retain legacy IDs, preserve stable order, and never merge legacy data after the new schema
  exists. (REQ-F-002, REQ-NF-002; DESIGN §2)
- [x] T-004: Parse supported new-schema data defensively, retaining valid relative order while
  diagnosing malformed entries, duplicate IDs/names, and unsupported schema versions.
  (REQ-F-013; DESIGN §§2, 9)
- [x] T-005: Add repository tests for clean install, partial/full legacy migration, migration
  idempotence, ordering, all typed settings variants, tombstones, cached/config-adjacent data,
  unrelated-key preservation, malformed input, duplicates, and atomic write failure.
  (REQ-F-002, REQ-F-009, REQ-F-013, REQ-NF-003; DESIGN §§1–2, 9)

## Phase 2: Instance registry and provider routing

- [x] T-006: Implement the application-owned instance registry and ordered QAbstractListModel
  projection for saved instances, runtime records, statuses, selections, model caches, and
  tombstone lookup. (REQ-F-004, REQ-F-009, REQ-F-014; DESIGN §3)
- [x] T-007: Implement UUID creation, prepend behavior, globally unique trimmed case-insensitive
  names, and lowest-free-number allocation. (REQ-F-003, REQ-NF-002; DESIGN §3)
- [x] T-008: Add the concrete-adapter `std::variant` factory/router and pass immutable instance IDs
  into all four existing adapters so discovered `ModelId`s are instance-scoped.
  (REQ-F-010, REQ-F-012, REQ-F-014, REQ-NF-001; DESIGN §4)
- [x] T-009: Retarget runtime coordination and model caches to instance IDs; stop probes for disabled
  instances, preserve active streams through disable, and track active streams for deletion guards.
  (REQ-F-007, REQ-F-008, REQ-F-014; DESIGN §§3–4, 7)
- [x] T-010: Add registry/router tests for multiple same-type instances, independent routing,
  numbering reuse, duplicate-name rejection, cache isolation, disabled probing, active-stream
  behavior, unavailable providers, and fallback after disable/delete.
  (REQ-F-003, REQ-F-007, REQ-F-008, REQ-F-014, REQ-NF-003; DESIGN §§3–4, 7)

## Phase 3: Settings draft and session support

- [x] T-011: Implement the single `ProviderDraftSession` snapshot/edit model for additions, common
  metadata, typed settings, credentials, dirty state, Reset, and validation.
  (REQ-F-003, REQ-F-005, REQ-NF-002; DESIGN §5)
- [x] T-012: Retarget Ollama, OpenAI, Anthropic, and Google settings controllers to the selected
  instance while preserving their current fields, defaults, validation, probes, and credential
  behavior. (REQ-F-005, REQ-F-012; DESIGN §§4–5)
- [x] T-013: Implement durable Save ordering and error reporting so failed config writes do not
  publish runtime changes and credential failures remain explicit and retryable.
  (REQ-F-005, REQ-F-013; DESIGN §§5, 9)
- [x] T-014: Implement Save/Discard/Cancel guarding for provider changes, Settings navigation, and
  window close, including new-draft removal and failure-stays-in-place behavior.
  (REQ-F-004, REQ-F-005, REQ-F-006; DESIGN §§3, 5)
- [x] T-015: Implement confirmed deletion, active-stream blocking, durable tombstone creation,
  runtime/cache/session cleanup, utility-default clearing, and best-effort credential removal.
  (REQ-F-008, REQ-F-009; DESIGN §§2–5, 7–9)
- [x] T-016: Add draft/controller tests for add, rename, enable/disable, typed field edits,
  Save/Discard/Cancel on every navigation path, write failure, delete confirmation/stream blocking,
  credential cleanup failure, and one-session enforcement.
  (REQ-F-003, REQ-F-005, REQ-F-006, REQ-F-007, REQ-F-008, REQ-F-013, REQ-NF-003; DESIGN §§3, 5, 9)

## Phase 4: QML provider management

- [x] T-017: Replace the fixed provider rows with the ordered instance model, a fresh-install empty
  state, and a no-selection details state with no automatic first-card selection.
  (REQ-F-001, REQ-F-004; DESIGN §6)
- [x] T-018: Add the template-backed Add provider menu that always lists all four supported types,
  prepends the draft card, selects it, and loads the retained provider-specific panel.
  (REQ-F-001, REQ-F-003, REQ-F-012; DESIGN §6)
- [x] T-019: Add inline instance-name editing and validation, enabled/disabled controls and neutral
  status, draft-aware action buttons, and confirmed deletion with streaming explanation.
  (REQ-F-003, REQ-F-005, REQ-F-007, REQ-F-008; DESIGN §6)
- [x] T-020: Add the shared Save/Discard/Cancel dirty-navigation prompt to card selection, Settings
  section navigation, and Settings window close. (REQ-F-006; DESIGN §§5–6)
- [x] T-021: Register each new QML-visible application type through the existing
  `holonight_application` static-library metatype plumbing and executable QML module path documented
  in `CLAUDE.md`; add new QML sources to `apps/chat/CMakeLists.txt` as required.
  (REQ-NF-001; DESIGN §6)
- [x] T-022: Add QML tests for empty state, complete Add menu, prepend/selection, inline validation,
  no automatic selection, dirty prompts, disabled status, and deletion confirmation/blocking.
  (REQ-F-001, REQ-F-003, REQ-F-004, REQ-F-006, REQ-F-007, REQ-F-008, REQ-NF-003; DESIGN §6)

## Phase 5: Chat and utility integration

- [x] T-023: Change chat provider/model projections, configured defaults, per-provider memory,
  send/regenerate routing, and runtime readiness from fixed provider IDs to instance IDs.
  (REQ-F-007, REQ-F-010, REQ-F-014; DESIGN §7)
- [x] T-024: Make the chat dropdowns dependent: enabled modeled instances by custom name first,
  models scoped to the selected instance second, with remembered/default/first fallback.
  (REQ-F-010; DESIGN §7)
- [x] T-025: Retarget utility defaults and isolated utility-provider creation to saved enabled
  instances, and implement fallback/clearing for disabled, deleted, unavailable, or model-less
  instances. (REQ-F-007, REQ-F-008, REQ-F-011; DESIGN §7)
- [x] T-026: Add application tests for multiple same-type catalogs, dynamic routing, dependent
  dropdowns, enable/disable during a stream, delete fallback, utility isolation/default cleanup,
  and restoration with an invalid selection. (REQ-F-007, REQ-F-008, REQ-F-010, REQ-F-011,
  REQ-F-014, REQ-NF-003; DESIGN §§4, 7)

## Phase 6: Historical rendering

- [x] T-027: Add live-instance/tombstone/unknown historical identity resolution without changing
  conversation or message persistence. (REQ-F-009; DESIGN §8)
- [x] T-028: Resolve provider icons from `ProviderType` for live instances and tombstone-backed
  messages rather than interpreting an instance ID as a provider type.
  (REQ-F-009, REQ-F-012; DESIGN §8)
- [x] T-029: Add persistence/application/QML tests proving deletion preserves conversations and
  per-message `ModelId`, tombstones contain only historical identity, renamed/deleted attribution
  renders correctly, and invalid restored selections fall back without rewriting history.
  (REQ-F-008, REQ-F-009, REQ-NF-003; DESIGN §§7–8)

## Phase 7: Remove fixed-ID assumptions

- [x] T-030: Audit C++, QML, tests, configuration, credential keys, icon lookup, logs, and status
  mapping for assumptions that `ollama`, `openai`, `anthropic`, or `google` is both type and instance
  ID; replace each operational key with instance ID and each visual/type decision with
  `ProviderType`. (REQ-F-010, REQ-F-011, REQ-F-012, REQ-F-014; DESIGN §§4, 7–8)
- [x] T-031: Remove superseded fixed repository APIs, static provider-row wiring, and compatibility
  shims after all callers and tests use provider state and the registry.
  (REQ-F-001, REQ-F-013, REQ-NF-001; DESIGN §§2–3, 6)
- [x] T-032: Update architecture/user documentation and sample configuration to describe instances,
  migration, UUID identity, draft semantics, disable/delete behavior, and tombstones.
  (REQ-F-002, REQ-F-005, REQ-F-007, REQ-F-008, REQ-F-009; DESIGN §§1–9)

## Phase 8: Final verification and acceptance

- [x] T-033: Run focused CTest cases for configuration migration/repository, registry/router,
  settings drafts, chat/utility routing, and historical rendering; resolve all failures.
  (REQ-NF-003)
- [x] T-034: Run `task test`, `task format-check`, `task tidy`, `task qml-lint`, and
  `task qmltypes-check`; record successful results or actionable blockers. (REQ-NF-003)
  Verification: `task test` (450/450), `task format-check`, `task qml-lint`, and
  `task qmltypes-check` pass. `task tidy` is blocked by feature-branch clang-tidy findings:
  undersized enum backing types, const-qualified provider instance ID members, short/unnamed
  identifiers, missing braces, a reverse iterator loop, and `ChatViewModel` constructor complexity.
- [x] T-035: Complete a graphical manual pass covering fresh empty state, Add menu, multiple
  same-type instances, prepend/selection, name validation, provider forms, Save/Discard/Cancel
  prompts, disable during streaming, delete confirmation/blocking, dependent chat dropdowns,
  utility fallback, and historical rendering after deletion. (REQ-F-001 through REQ-F-014,
  REQ-NF-003)
