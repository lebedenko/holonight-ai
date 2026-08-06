# Multi-Provider Settings Specification

Status: Accepted

## Purpose

Replace the four fixed, single-instance provider configurations with an ordered collection of
user-configured provider instances. The feature preserves the existing Ollama, OpenAI, Anthropic,
and Google adapters and their settings forms while allowing any number of configurations of each
supported type.

`DESIGN.md` maps each requirement to the implementation. `TASKS.md` records the completed,
requirement-linked implementation sequence and verification results.

## Terminology

- A **provider template** is one of the supported provider types: Ollama, OpenAI, Anthropic, or
  Google. Templates describe available adapters, defaults, icons, and settings forms; they are not
  saved configurations.
- A **provider instance** is a user-configured provider with an immutable instance ID, provider
  type, display name, enabled state, typed settings, runtime adapter, and model catalog.
- A **draft** is the single Settings editing session containing changes that have not been saved.
- A **tombstone** is immutable, non-secret historical identity retained after an instance is
  deleted.

## Functional requirements

### REQ-F-001: Templates and configured instances are separate

- A fresh installation has zero provider instances and shows the Providers empty state.
- Add provider always offers all four supported templates, including types already configured.
- A provider template is never inferred to be a configured instance merely because its adapter is
  supported.

### REQ-F-002: Legacy configurations migrate conservatively

- Migration runs only when the new provider-state schema is absent.
- Only provider objects physically present beneath the legacy `providers` object are migrated;
  missing provider sections do not produce instances.
- Each migrated instance retains its legacy ID (`ollama`, `openai`, `anthropic`, or `google`) so
  existing credential and conversation references remain valid.
- Partial legacy configuration migrates only its present sections. Invalid sections are skipped
  with a diagnostic and do not prevent valid sections from migrating.
- Once a new schema is present, legacy data is not migrated or merged again.

### REQ-F-003: Instances can be added and named predictably

- Adding a template creates a draft instance with a new opaque UUID, prepends it to the ordered
  card list, selects it, and opens its existing provider-specific form.
- New draft instances start enabled and use the template's existing settings defaults.
- The initial name uses the lowest free positive slot: `Ollama`, `Ollama 2`, `Ollama 3`, and so on.
  Renaming or deleting an instance makes its previous slot reusable.
- Names are trimmed, non-empty, and globally unique across all provider types using a
  case-insensitive comparison. Invalid names show inline validation and prevent Save.

### REQ-F-004: Selection is explicit

- Loading Settings never selects the first card automatically.
- With no selected instance, including after opening a fresh installation, the details pane shows
  a neutral empty state.
- Adding an instance selects that draft. Otherwise, selection changes only through explicit user
  action or the defined fallback after disabling or deleting the selected instance.

### REQ-F-005: All edits are staged

- Additions, display-name edits, enabled-state changes, provider-specific form fields, and
  credential edits remain draft-only until Save.
- Save validates the entire selected draft and commits common metadata, typed provider settings,
  and its credential operation as one user action.
- Cancel restores an existing instance to its last saved state; for an unsaved addition, Cancel
  removes the draft and returns to the no-selection details state.
- Reset continues to restore provider-type defaults within the draft and does not save them.
- Only one Settings draft session may exist at a time.

### REQ-F-006: Dirty navigation is guarded

- Navigating to another provider, another Settings section, or closing the Settings window while
  the draft is dirty prompts Save, Discard, or Cancel.
- Save validates and commits, then completes the requested navigation only on success. Discard
  restores/removes the draft and completes navigation. Cancel leaves both draft and selection
  unchanged.
- A failed save keeps the draft and current view intact and reports an actionable error.

### REQ-F-007: Disabled instances are inert for future work

- A saved disabled instance performs no background connection or model probes and is excluded from
  chat and utility-task selection.
- Future chat sends, regenerations, and utility tasks cannot route to it.
- Its card uses a neutral `Disabled` status rather than stale connected/error state.
- Disabling does not interrupt a response already streaming through that instance; the new state
  applies to subsequent work.

### REQ-F-008: Deletion is confirmed and complete

- Delete requires explicit confirmation and cannot proceed while that instance is actively
  streaming a response.
- Confirmed deletion removes saved configuration, runtime adapter/coordinator state, cached models,
  and session selection memory for the instance.
- Credential removal is attempted using the instance ID. Failure is reported but does not recreate
  the deleted configuration.
- Utility defaults that reference the instance are cleared.
- After deletion, Settings has no selected provider; it does not automatically select another card.

### REQ-F-009: Historical attribution survives deletion

- Deleting an instance never deletes conversations or rewrites per-message `ModelId` attribution.
- Deletion stores an immutable tombstone containing only instance ID, provider type, and last
  display name. It contains no endpoint, typed settings, enabled state, model cache, or secret.
- Historical rendering resolves provider name and icon through the live instance first and then
  its tombstone.
- Restoring a conversation whose selected instance or model is no longer valid uses normal
  enabled-instance fallback for future sends without rewriting the conversation or its messages.

### REQ-F-010: Chat selectors remain dependent

- The provider dropdown contains enabled instances that have available models and labels them by
  custom display name.
- The model dropdown contains only models discovered or cached for the selected instance.
- Selecting a provider resolves its remembered model, configured default, or first available model
  in that order. If none exist, selection clears and sending remains disabled.
- `ModelId.provider_id` identifies the provider instance, not merely the provider type.

### REQ-F-011: Utility routing is instance-scoped

- Utility defaults and isolated utility-task providers use instance IDs.
- A missing, disabled, deleted, or model-less utility instance falls back through the existing
  utility/chat fallback policy without mutating historical selections.
- Utility execution does not share mutable streaming state with chat, while both derive settings
  from the same saved instance configuration.

### REQ-F-012: Provider-specific behavior is preserved

- The four concrete adapters, existing provider forms, validation rules, connection testing, model
  refresh behavior, and credential semantics remain provider-specific.
- Each form edits the selected instance rather than a fixed provider ID.
- Icons are resolved from provider type for configured cards and tombstone-backed messages.

### REQ-F-013: Persistence is ordered, atomic, and non-destructive

- Provider instances and tombstones persist under the new `provider_instances` state in card order.
- Whole-state load/save operations preserve unrelated root keys in `config.json`.
- Writes are atomic. A failed write leaves the previous file valid, keeps the draft dirty, and does
  not reconfigure live runtime state.
- Malformed new-schema entries are skipped with diagnostics; valid entries retain relative order.
  Duplicate IDs are rejected deterministically rather than silently merged.

### REQ-F-014: Runtime ownership and fallback are deterministic

- Application-owned state is the authority for ordered configuration, runtime adapters, statuses,
  selections, drafts, model caches, and tombstone lookup.
- Multiple instances of one provider type route independently using their instance IDs.
- Disabling or deleting the active chat selection falls back to another enabled instance/model for
  subsequent sends; if none is usable, selection clears and sending is disabled.
- An unavailable configured provider remains visible in Settings with actionable status but is not
  treated as a usable chat or utility selection.

## Non-functional requirements

### REQ-NF-001: Incremental architecture

Use the existing modules, concrete provider adapters, QML panels, and static-library QML metatype
registration. Do not introduce a general provider plugin/framework abstraction.

### REQ-NF-002: Identity and secrets

New IDs are UUIDs, remain immutable, and are globally unique. Secrets continue to live only in the
credential store, keyed by instance ID; neither instances nor tombstones serialize secret values.

### REQ-NF-003: Verification

Repository, application, and QML behavior is covered by deterministic tests. Focused CTest cases
run before the full project checks and a graphical manual pass.

## Out of scope

- Drag or manual reordering beyond prepending newly added instances.
- Deleting conversations or historical message attribution.
- Auto-save.
- New provider protocols or replacement adapters.
- Multiple simultaneous Settings drafts or Settings windows.

## Acceptance summary

The feature is accepted: clean and migrated installations behave as specified; any number of
same-type instances can be drafted, saved, independently routed, disabled, and deleted; historical
attribution remains renderable; dependent chat and utility selection uses instance IDs; failure
paths preserve valid persisted and runtime state; and the verification results and known lint debt
are recorded in `TASKS.md`.
