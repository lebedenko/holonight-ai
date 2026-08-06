# Shared Controls Adoption — Manual Visual Checklist

Automated verification is complete; no agent-driven visual inspection or screenshots were used.

- [x] Inspect workspace, settings, and quick panel in light and dark themes at 100% and 125%.
- [x] Check normal and minimum window sizes for clipping, wrapping, spacing, and popup placement.
- [x] Check pointer focus, keyboard focus, Tab order, search clear/Escape, combo arrow navigation,
  activation, and disabled states.
- [x] Check conversation selection, search, rename, delete, and quick-panel recent actions.
- [x] Check every provider field, slider/spin box, model refresh, credentials, status, connection
  test, reset, cancel, and save path.
- [x] Check short and long composer text, scrolling, Enter/Shift+Enter, send/stop/retry, compact
  mode, and error/status presentation.
- [x] I approve the shared-control migration visuals.

## Issues

| Screen | Theme | Scale | Action | Expected result | Observed result |
| --- | --- | ---: | --- | --- | --- |
| Workspace conversation list | Dark | Reported scale | Select a conversation | Canonical quiet selected fill and accent edge, matching navigation selection | Verified after replacing the elevated application background with the public `HnNavigationDelegate` selection shell |
| Workspace conversation list | Dark | Reported scale | View conversation rows | Conversation title, icon, and relative time are visible and vertically centered | Verified after explicitly binding the title and centering custom content in the padded content area |
| Workspace conversation list | Light | 125% | Switch conversations | Exactly one row—the active conversation—is selected | Verified after disabling the list's unused implicit current item |
| Provider settings list | Dark | Reported scale | Switch providers | Exactly one row—the provider identified by `selectedProviderId`—is selected | Verified after disabling the list's unused implicit current item |
| Provider default model | Dark | Reported scale | View and select a default model | The model is selected from a non-editable combo and updates the provider setting | Verified after replacing editable-text wiring with selected-index and activation bindings in all four provider panels |
