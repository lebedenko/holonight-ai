# Provider configuration

HoloNight AI treats Ollama, OpenAI, Anthropic, and Google as provider templates. A configured
provider is an instance of one of those templates, with its own display name, endpoint, credential,
models, enabled state, and runtime adapter. Any number of instances of the same type may coexist.

## Identity and storage

New instances receive an immutable UUID. That instance ID—not the provider type—is used by
credentials, model caches, chat routing, utility routing, and `ModelId.provider_id`. Display names
may change without changing identity. Names must be non-empty and globally unique ignoring case.

Non-secret state is stored in `$XDG_CONFIG_HOME/holonight-ai/config.json`, falling back to
`~/.config/holonight-ai/config.json`. The `provider_instances.instances` array is also the card
order; newly added instances are prepended. Credentials are stored separately in the desktop
Secret Service under the instance ID and never appear in JSON. See
[`config.example.json`](config.example.json) for the complete schema.

The top-level `utility.default_utility_model.provider_id` is likewise an instance ID. If that
instance/model is unavailable, utility work follows the normal fallback policy. Cached model lists
may appear beneath the legacy-named top-level `providers` object, but its keys are instance IDs;
that cache is not authoritative provider configuration.

## Settings drafts

Opening Settings does not select a provider automatically. Add provider creates and selects an
enabled draft using the template defaults and a lowest-free display name such as `Ollama`,
`Ollama 2`, or `Ollama 3`.

Adding, renaming, enabling/disabling, editing provider fields, resetting fields, and changing a
credential affect only the current draft. Save validates and durably writes non-secret state
before changing the live runtime. Reset only restores defaults inside the draft. Cancel restores a
saved instance or removes an unsaved addition. Navigating away from a dirty draft prompts Save,
Discard, or Cancel; a failed save leaves the draft and current view intact.

## Disable and delete

A disabled instance remains saved and editable, but performs no new probes and is excluded from
future chat and utility selection. Disabling does not interrupt an already-running response; it
applies to subsequent work.

Deletion requires confirmation and is blocked while that instance has an active stream. It removes
the saved instance, runtime adapter, model cache, selection memory, and any utility default that
references it. Credential removal is attempted separately; failure is reported but does not
restore the deleted instance.

Deletion never removes conversations or rewrites their stored `ModelId` values. Instead, it adds a
tombstone containing only the instance ID, provider type, and last saved display name. Historical
rendering resolves a live instance first, then its tombstone, then an Unknown provider fallback.
The tombstone contains no endpoint, enabled state, typed settings, models, or credential.

When a disabled/deleted instance was selected by a restored conversation, the stored history stays
unchanged while the next live send falls back to a usable enabled instance/model. If none exists,
live selection clears and sending is disabled.

## Legacy migration

When `provider_instances` is absent, objects present under the old `providers.ollama`,
`providers.openai`, `providers.anthropic`, and `providers.google` keys are migrated in memory. Each
migrated instance retains that fixed legacy ID so existing Secret Service and conversation
references keep working. Missing sections create no instances, invalid sections are skipped, and
valid sections still migrate. Once `provider_instances` exists, old provider settings are neither
merged nor migrated again.

Newly created instances always use UUIDs. The four fixed strings are accepted only as stable IDs
for migrated data.
