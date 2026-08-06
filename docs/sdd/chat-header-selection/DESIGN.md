# Chat Header Selection Design

Status: Complete

## State and data flow

`holonight_domain::ModelId` remains the canonical internal selection used for send, regenerate, and
persistence. `ChatViewModel` projects it into `selectedProviderId`, `selectedModelName`,
`availableProviders`, and provider-filtered `availableModelNames` for QML. A session-only hash stores
the last valid model for each provider. Configured defaults for all four providers are injected at
construction.

Catalog refresh rebuilds the ordered provider projection, then validates the canonical selection.
A restored selection is retained until its own provider reports authoritatively. Once authoritative,
fallback is remembered model, configured default, then first provider-supplied model. Empty providers
are omitted and a globally empty catalog clears selection.

`selectedProviderStatus` maps `ProviderRuntimeCoordinator` readiness and refresh state. Existing
status messages remain the detailed actionable text.

## Components

`HeaderComboBox` presents inline text with a subtle chevron, transparent resting border and surface,
bounded popup, elision, keyboard behavior, and semantic interaction colors. `HeaderAction` provides
the shared square surface for header icons. `ChatHeader` binds directly to the view model and owns
the complete mockup-driven row: accent, provider, dot separator, model, flexible space, semantic
status dot and label, sliders Settings action, down-chevron collapse action, vertical padding, and
full-width divider. At constrained widths the visible status label yields before the dot.

Workspace uses the full composition. Quick panel keeps its independent top bar and chooses the
compact composition, which retains the inline selectors and status dot but omits workspace-only
actions. Neither host stores duplicate selection state.

Settings accepts a provider ID, selects Providers, and forwards the ID to `ProvidersPage`.

## Rejected alternatives

- A combined provider/model picker obscures the dependency and was removed without a shim.
- QML-owned indices are fragile across asynchronous catalog replacement.
- Persisting per-provider memory would require a migration and exceeds the session-only requirement.
- A new UI framework is unnecessary because existing Qt controls and Holoniight palette/surfaces
  provide the required behavior.
