# Multi-Provider Settings Design

Status: Implemented

This design implements `SPEC.md`. Requirement references are included in each section, and the
completed phased implementation work is recorded in `TASKS.md`.

## 1. Configuration model

Addresses: REQ-F-001, REQ-F-002, REQ-F-003, REQ-F-009, REQ-F-012, REQ-F-013, REQ-NF-002

Add a closed `ProviderType` enum (`Ollama`, `OpenAi`, `Anthropic`, `Google`) with explicit stable
string serialization. Keep the existing settings structs and combine them in a
`ProviderSettings = std::variant<OllamaProviderConfig, OpenAiProviderConfig,
AnthropicProviderConfig, GoogleProviderConfig>`.

```cpp
struct ProviderInstanceConfig {
  QString id;
  ProviderType type;
  QString display_name;
  bool enabled = true;
  ProviderSettings settings;
};

struct ProviderTombstone {
  QString instance_id;
  ProviderType type;
  QString last_display_name;
};

struct ProviderState {
  std::vector<ProviderInstanceConfig> instances;
  std::vector<ProviderTombstone> tombstones;
};
```

The variant makes mismatched type/settings combinations unrepresentable after parsing and retains
the validation/defaults already owned by each concrete provider. IDs are opaque UUID strings for
new instances. Display names are presentation metadata and may change; IDs never change.

Tombstones are append/update-on-delete historical identities keyed by instance ID. They deliberately
exclude settings, endpoints, enabled state, model catalogs, and credentials. If a legacy tombstone
already exists for the ID, deletion does not mutate its immutable identity.

## 2. JSON schema and migration

Addresses: REQ-F-002, REQ-F-008, REQ-F-009, REQ-F-013

`config.json` stores one ordered state object:

```json
{
  "provider_instances": {
    "schema_version": 1,
    "instances": [
      {
        "id": "550e8400-e29b-41d4-a716-446655440000",
        "type": "ollama",
        "name": "Ollama LAN",
        "enabled": true,
        "settings": {
          "base_url": "http://192.168.0.20:11434",
          "default_model": "qwen3:14b",
          "context_window": 131072,
          "temperature": 0.7
        }
      }
    ],
    "tombstones": [
      {
        "id": "openai",
        "type": "openai",
        "name": "OpenAI"
      }
    ]
  },
  "utility": {},
  "other_unrelated_key": {}
}
```

Replace fixed `loadOllamaConfig()`/`saveOllamaConfig()`-style provider entry points with
`loadProviderState()` and `saveProviderState(const ProviderState&)`. The repository reads the root
once per operation, changes only `provider_instances` (and, when requested in the same transaction,
the utility default), and writes through `QSaveFile`. A serialization, open, write, or commit error
returns failure without changing runtime state.

When `provider_instances` is absent, inspect the legacy root `providers` object. Iterate the four
known keys in stable provider order, but create an instance only when that exact key contains an
object. The instance uses the key as ID and the template display name, existing typed parsing and
defaults for missing fields, and the legacy enabled behavior. Invalid objects are diagnosed and
skipped. The first successful save writes the new schema; thereafter the legacy object is ignored.
The migration does not invent absent providers and does not remove unrelated root keys.

On new-schema load, validate the version, UUID/legacy ID shape, provider type, non-empty normalized
name, typed settings, and unique IDs/names. Skip malformed entries while retaining the relative
order of valid entries. Keep the first valid occurrence of an ID and diagnose later duplicates.

## 3. Application-owned registry

Addresses: REQ-F-003, REQ-F-004, REQ-F-005, REQ-F-007, REQ-F-008, REQ-F-009, REQ-F-014

Introduce one application-owned `ProviderInstanceRegistry` (with a QML-facing list model/controller
where needed). It owns:

- saved ordered `ProviderState` and historical lookup;
- one runtime record per saved instance: concrete adapter, runtime coordinator status, model cache,
  and active-stream count;
- current chat/provider selection and per-instance session model memory;
- the selected Settings instance and at most one `ProviderDraftSession`.

The registry is constructed before chat, settings, and utility controllers, then injected into them.
It is the sole component allowed to add/remove/reconfigure runtime records. Loading creates records
only for configured instances. Disabled records retain configuration but do not schedule probes.

Adding creates an in-memory draft only, prepends its projected card, and selects it. The UUID and
default settings are generated once and survive validation retries. With no explicit selection, the
details projection is empty. Canceling a new draft removes its card; canceling an existing draft
restores its snapshot. No automatic first-card selection occurs on load or after deletion.

Names are normalized by trimming for storage and Unicode-aware case folding for comparison. The
lowest-free-number algorithm tests the normalized global name set, beginning with the template name
and then suffixes 2, 3, and upward. It does not depend on count or provider type alone.

## 4. Concrete adapter router

Addresses: REQ-F-007, REQ-F-010, REQ-F-011, REQ-F-012, REQ-F-014, REQ-NF-001

Do not introduce a provider base framework. Use a concrete variant such as:

```cpp
using ProviderAdapter = std::variant<std::shared_ptr<OllamaProvider>,
                                     std::shared_ptr<OpenAiProvider>,
                                     std::shared_ptr<AnthropicProvider>,
                                     std::shared_ptr<GoogleProvider>>;
```

A small factory switches on `ProviderType`, constructs the existing adapter with its typed settings,
and assigns the instance ID. A router looks up the runtime record by instance ID and uses
`std::visit` for refresh, send, cancellation, and settings reconfiguration. Provider-specific HTTP
and validation behavior remains in the concrete adapters.

Each adapter emits discovered `ModelId` values with `provider_id = instance_id`. Provider type is
metadata obtained from the registry, never inferred from that field. This permits two adapters of
the same type to expose identical model names without collision.

Disabling immediately prevents new routing and cancels scheduled probes, then publishes neutral
status. Existing stream handles retain their adapter until completion. Deletion is rejected while
the runtime record's active-stream count is nonzero.

## 5. Draft sessions and settings controllers

Addresses: REQ-F-003, REQ-F-005, REQ-F-006, REQ-F-008, REQ-F-012, REQ-F-013

`ProviderDraftSession` stores the original optional config, editable config, credential edit intent,
dirty state, and validation errors. Provider-specific settings controllers are retargeted from a
fixed provider to the selected instance/draft and continue exposing the same fields, connection
tests, refresh actions, defaults, and validation rules to their existing QML panels.

Save proceeds in this order:

1. Validate trimmed global name uniqueness and provider-specific fields.
2. Build the complete candidate `ProviderState` (and any utility-default clearing for deletion).
3. Atomically persist non-secret state.
4. Apply the committed config to the registry/runtime and clear the draft.
5. Perform the requested credential store/update/removal and report its result.

Normal Save must not publish configuration/runtime changes when step 3 fails. Credential operations
cannot be part of the JSON transaction; a credential failure leaves the non-secret save committed,
keeps the submitted credential value out of JSON, and exposes a retryable error. For deletion,
configuration/runtime/cache removal proceeds after the durable state write even when subsequent
credential cleanup fails.

All navigation requests pass through a draft guard. Save continues navigation only after durable
commit; Discard restores/removes the draft; Cancel aborts navigation. The same guard handles provider
selection, Settings section navigation, and window close. The application continues to enforce the
existing single Settings window, which guarantees one draft session.

## 6. QML provider management

Addresses: REQ-F-001, REQ-F-003, REQ-F-004, REQ-F-005, REQ-F-006, REQ-F-007, REQ-F-008, REQ-F-012

`ProviderListPanel` binds to the registry's ordered list model rather than a static four-row model.
Its Add provider menu is backed by the static template catalog and therefore always lists all four
types. New cards appear first and become selected. The details loader shows an empty-state component
when there is no selection, otherwise it chooses the existing provider form by `ProviderType`.

`ProviderSettingsScaffold` adds inline display-name editing, enabled state, and confirmed deletion.
Validation is displayed near the name and action bar. Disabled cards always render the neutral
Disabled state. Delete is disabled with an explanation while the instance streams. Dialogs for
deletion and dirty navigation are application-modal to the Settings window.

If a new application Q_OBJECT/list model/controller becomes QML-visible, add its headers and sources
to `holonight_application`, retain the existing static-library metatype registration helper, and add
the type to the executable's `qt_add_qml_module`/registration path described in `CLAUDE.md`. Add new
QML files to `apps/chat/CMakeLists.txt` when they are not already covered by its source collection.

## 7. Chat, utility, and cache integration

Addresses: REQ-F-007, REQ-F-008, REQ-F-010, REQ-F-011, REQ-F-014

`ChatViewModel`, `ChatController`, `ProviderRuntimeCoordinator`, model caches, configured defaults,
and per-provider session memory are changed from fixed provider-type keys to instance IDs. The chat
provider projection contains enabled instances with models, ordered by saved card order and labeled
with current display names. The selected instance exclusively determines the model projection.

Selection validation remains authoritative during conversation restoration. If a restored instance
is disabled, deleted, unavailable, or lacks the attributed model, preserve the stored `ModelId` but
choose remembered model, configured default, or first model from another enabled instance for the
next send. If no usable instance exists, clear live selection and disable sending.

`UtilityConfig.default_utility_model` continues using `ModelId`, now with an instance ID. The utility
runner asks the registry for a fresh isolated adapter derived from the saved instance configuration.
It rejects disabled/deleted/unavailable records, clears deleted references during the deletion
state write, and otherwise retains the existing fallback chain.

Model caches are partitioned by instance ID and removed on deletion. Renaming does not invalidate a
cache; changing connection/auth settings does.

## 8. Historical rendering and icons

Addresses: REQ-F-008, REQ-F-009, REQ-F-012

Conversation and message persistence schemas keep their existing `ModelId`; no history migration or
rewrite is required because migrated legacy IDs remain stable. Rendering resolves attribution with
`registry.historicalIdentity(instance_id)`: a live instance wins, followed by its tombstone, followed
by an explicit Unknown provider fallback for corrupt/foreign IDs.

Provider icon lookup accepts `ProviderType`. Current rows obtain it from live configuration and
historical messages from live identity or tombstone. Display-name changes affect live attribution;
after deletion the tombstone freezes the last saved display name.

## 9. Failure behavior

Addresses: REQ-F-002, REQ-F-006, REQ-F-007, REQ-F-008, REQ-F-009, REQ-F-013, REQ-F-014

- Config read errors or unsupported schema versions expose a diagnostic and do not overwrite the
  file. Valid entries from a partially malformed supported schema may still load.
- Config write failure leaves the previous registry/runtime and the draft unchanged.
- Credential cleanup failure after deletion reports orphaned-secret cleanup as retryable but never
  restores configuration or runtime state.
- An unavailable provider remains configurable and visible, with no background work when disabled.
- Dirty-navigation Save failure stays on the current draft; Discard and Cancel remain available.
- Disable/delete fallback affects only live future selection. Persisted conversation and message
  attribution is never rewritten.

## 10. Design choices and trade-offs

- A typed variant is preferred over a generic map: adding fields remains compiler-checked and the
  existing adapters/forms keep ownership of provider-specific rules.
- A registry centralizes instance lifetime because chat, Settings, utility tasks, caches, and history
  need one identity authority. It is not a new provider framework; dispatch remains closed and
  concrete.
- Tombstones intentionally retain minimal identity. This makes historical rendering stable without
  retaining operational configuration or secrets.
- JSON state commits before credential operations because the credential backend cannot participate
  in an atomic file transaction. Errors are surfaced explicitly rather than pretending the two
  stores are transactional.

## 11. Requirement traceability

| Requirement | Primary design sections |
| --- | --- |
| REQ-F-001 | 1, 3, 6 |
| REQ-F-002 | 2, 9 |
| REQ-F-003 | 1, 3, 6 |
| REQ-F-004 | 3, 6 |
| REQ-F-005 | 3, 5, 6 |
| REQ-F-006 | 5, 6, 9 |
| REQ-F-007 | 3, 4, 6, 7, 9 |
| REQ-F-008 | 2, 3, 5, 6, 7, 8, 9 |
| REQ-F-009 | 1, 2, 3, 8, 9 |
| REQ-F-010 | 4, 7 |
| REQ-F-011 | 4, 7 |
| REQ-F-012 | 1, 4, 5, 6, 8 |
| REQ-F-013 | 1, 2, 5, 9 |
| REQ-F-014 | 3, 4, 7, 9 |
| REQ-NF-001 | 4, 6 |
| REQ-NF-002 | 1, 2 |
| REQ-NF-003 | `TASKS.md`, Phase 8 |
