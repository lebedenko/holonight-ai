# Shared Controls Adoption — Tasks

Status: Implemented

Upstream baseline: `holonight-qt@7a74276`

- [x] Bundle 1 — require and link Controls; extend focused QML construction/source-policy coverage.
- [x] Bundle 2 — adopt shared search, navigation/list/action delegates, empty states, status
  indicators, and shared-styled local SVG action adapters.
- [x] Bundle 3 — adopt icon combos, panel/action composition, and semantic separators.
- [x] Bundle 4 — recompose the four provider panels with shared form and settings controls while
  preserving bindings and operations.
- [x] Bundle 5 — adopt `HnTextArea`, streaming status/action composition, and remove superseded
  local controls/imports.
- [x] Visual correction — use the public canonical navigation selection shell for conversation
  rows instead of the application-painted elevated background.
- [x] Visual correction — explicitly bind conversation titles and center custom row content
  within the shared delegate's padded content area.
- [x] Visual correction — disable the conversation list's unused implicit current item so only
  the active conversation receives selected styling.
- [x] Visual correction — disable the provider list's unused implicit current item so only the
  provider identified by `selectedProviderId` receives selected styling.
- [x] Visual correction — make provider default-model combos selection-only while preserving the
  configured model display and model-selection controller bindings.
- [x] Obtain user approval using `MANUAL_VISUAL_CHECKLIST.md`.

Reported visual issues must be added here as focused correction tasks, followed by focused
automated re-verification and a refreshed checklist.
