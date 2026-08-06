# Provider Icons Feature — EARS Specification

**Scope**: Replace the placeholder message-bubble icon with per-provider SVG icons (ollama, openai, anthropic, google), recoloring monochrome assets and preserving google's multi-color branding. New UI component `ProviderIcon.qml`, new `MessageListModel` role `providerId`, icon slot resize from 32 to 64 pixels, and asset resource wiring.

**Source assets**: `assets/providers/{ollama,openai,anthropic,google}.svg` (already checked in).

**Reuse pattern**: Existing `HnIcon` QML component from `holonight-qt` library (already a project dependency), with hardcoded per-provider tint configuration in new `ProviderIcon.qml`.

---

## Functional Requirements

### Provider Icon Configuration

**REQ-F-001** (Ubiquitous)
The system shall provide a `ProviderIcon` QML component that accepts a `providerId` string property and internally maps known provider identifiers to icon source URLs and tinting policy.

*Acceptance criterion*: A QML component file `qml/shared/ProviderIcon.qml` exists, accepts `providerId` as a required property, and exposes properties or bindings that route the `providerId` to a lookup table returning `{source: url, tinted: boolean}` tuples.

---

**REQ-F-002** (Ubiquitous)
The system shall tint the ollama, openai, and anthropic provider icons when rendering them via the `HnIcon` component.

*Acceptance criterion*: `ProviderIcon.qml` lookup table maps `"ollama"`, `"openai"`, `"anthropic"` to entries where `tinted: true`, and `HnIcon` is instantiated with `tinted: true` for these providers; visual inspection confirms the icon color matches the UI theme's primary text color.

---

**REQ-F-003** (Ubiquitous)
The system shall NOT tint the google provider icon and shall render it with its original multi-color branding intact.

*Acceptance criterion*: `ProviderIcon.qml` lookup table maps `"google"` to an entry where `tinted: false`, and `HnIcon` is instantiated with `tinted: false` for google; visual inspection confirms google icon retains its original multi-color appearance without theme color substitution.

---

### Message Provider Identity Exposure

**REQ-F-004** (Ubiquitous)
The `MessageListModel` class shall expose a new `providerId` role that returns the provider identifier string from each message's associated model ID.

*Acceptance criterion*: `MessageListModel` class defines a new role (e.g. `ProviderIdRole`) in its `roleNames()` and `data()` implementation; when `data(index, ProviderIdRole)` is called, it returns `message.modelId()->provider_id` if `modelId` exists, or `QString()` if not; QML bindings using `model.providerId` receive the correct value.

---

### Icon Rendering in Message Bubbles

**REQ-F-005** (Ubiquitous)
The `MessageBubble` component shall render assistant-message icons using the `ProviderIcon` component, bound to the message's `providerId` role.

*Acceptance criterion*: `qml/shared/MessageBubble.qml` contains an instantiation of `ProviderIcon` with `providerId: model.providerId` (or equivalent binding), positioned in the existing icon slot; QML binding chain completes without errors.

---

**REQ-F-006** (Ubiquitous)
The system shall increase the icon slot size from 32 pixels to 64 pixels in `MessageBubble.qml`.

*Acceptance criterion*: The `readonly property real iconSize: 32` property in `MessageBubble.qml` is changed to `64`, and all layout bindings that reference `iconSize` render the icon at the new dimensions; visual inspection confirms the icon occupies a 64×64 frame.

---

### Fallback and Unknown Provider Handling

**REQ-F-007** (Conditional)
Where `providerId` is empty, null, or does not match any configured provider in the `ProviderIcon` lookup table, the system shall render the original solid-color circle placeholder inside the 64×64 icon frame.

*Acceptance criterion*: `ProviderIcon.qml` always renders the icon frame; when `providerId` is `""` or unmapped, the placeholder circle is rendered inside it; when a known `providerId` is supplied, the placeholder circle is hidden and the provider icon is shown inside the same frame.

---

### Icon Tint Color

**REQ-F-008** (Ubiquitous)
The system shall tint monochrome provider icons exclusively with the `HoloniightPalette.textPrimary` color, independent of message status or error state.

*Acceptance criterion*: The `ProviderIcon` component or its `HnIcon` child is configured to use `HoloniightPalette.textPrimary` as the tint color source; visual inspection of rendered icons in the message bubble confirms the tint color matches the primary text color in the current theme (light and dark mode).

---

### Asset Resource Wiring

**REQ-F-009** (Ubiquitous)
The system shall register all four provider SVG assets (ollama, openai, anthropic, google) in the Qt resource system via `apps/chat/CMakeLists.txt` and make them resolvable at runtime by QML.

*Acceptance criterion*: `apps/chat/CMakeLists.txt` contains resource registration (e.g. `qt_add_resources` or equivalent) for `assets/providers/*.svg`; QML can load and display each icon by a stable resource URL (e.g. `qrc:/HolonightChat/assets/providers/openai.svg`); running the application does not report missing resource errors in the console or QML warning log.

---

## Non-Functional Requirements

### Code Reuse and Dependencies

**REQ-NF-001** (Ubiquitous)
The system shall reuse the existing `HnIcon` QML component from the `holonight-qt` library to implement icon rendering and shall NOT introduce new C++ code, new QML modules, or new package dependencies.

*Acceptance criterion*: No new files under `src/` are created; `ProviderIcon.qml` instantiates `HnIcon` directly; `find_package(HolonightQt)` and `import Holonight` are already satisfied by existing project configuration.

---

### Palette Integration

**REQ-NF-002** (Ubiquitous)
All theme-aware color values shall be sourced from `HoloniightPalette` (from the `holonight-qt` library) and shall not contain hardcoded hex values.

*Acceptance criterion*: Grep across `qml/shared/ProviderIcon.qml` and `qml/shared/MessageBubble.qml` diff shows zero new hex color literals; any color reference is a `HoloniightPalette.*` binding.

---

## Constraints

### Out of Scope (Non-Goals)

**REQ-C-001** (Ubiquitous)
Changes to the provider settings UI (`qml/workspace/ProviderListDelegate.qml` and associated settings panels) are explicitly out of scope for this cycle.

*Acceptance criterion*: No modifications are made to files under `qml/workspace/` or files that implement provider configuration or management dialogs; provider settings continue to function and render exactly as they do before this feature is merged.

---

**REQ-C-002** (Ubiquitous)
Error-state color variants (e.g. a different tint color for failed messages or rate-limited provider responses) are out of scope.

*Acceptance criterion*: The tint color is always `HoloniightPalette.textPrimary` regardless of message status; no REQ-NF or REQ-F requirement references message status, error state, or conditional tint color.

---

**REQ-C-003** (Ubiquitous)
Icon animation, transition, or fade-in effects are out of scope; rendering is static.

*Acceptance criterion*: `ProviderIcon.qml` contains no `Behavior`, `PropertyAnimation`, `Sequential`, or `Transition` elements; no animation parameters are exposed or documented.

---

**REQ-C-004** (Ubiquitous)
Automatic detection or heuristic-based recolor-vs-not classification of SVG content is explicitly rejected; all recolor decisions are hardcoded in the per-provider lookup table.

*Acceptance criterion*: `ProviderIcon.qml` contains a static lookup table mapping `providerId` strings to hardcoded `{source, tinted}` tuples; no code inspects SVG content, file paths, or other dynamic signals to infer tinting policy.

---

**REQ-C-005** (Ubiquitous)
Tooling, documentation, or workflow changes for adding new providers are out of scope; adding a new provider remains a multi-file manual process (and will include a one-line entry in the `ProviderIcon.qml` lookup table).

*Acceptance criterion*: No new scripts, build-time helpers, or onboarding documents are created; comments in `ProviderIcon.qml` or elsewhere may briefly document the lookup table, but no changes are made to build processes or contribution guidelines.

---

## Acceptance Verification Checklist

- [ ] `qml/shared/ProviderIcon.qml` exists with per-provider lookup table (ollama, openai, anthropic → tinted; google → not tinted).
- [ ] `MessageListModel::roleNames()` and `data()` include a `providerId` role sourced from `message.modelId()->provider_id`.
- [ ] `qml/shared/MessageBubble.qml` instantiates `ProviderIcon` and increases icon slot size to 64 pixels.
- [ ] All provider icons render inside the icon frame; empty/unknown `providerId` falls back to the original placeholder circle inside that frame.
- [ ] Tint color references `HoloniightPalette.textPrimary`.
- [ ] `apps/chat/CMakeLists.txt` registers all four provider SVG assets and makes them resolvable in QML at runtime.
- [ ] No new C++ code or module dependencies are introduced.
- [ ] No modifications to provider settings UI, error-state handling, animations, or provider-onboarding workflows.
- [ ] Visual inspection: ollama, openai, anthropic icons render with the correct theme color; google icon retains multi-color appearance.
