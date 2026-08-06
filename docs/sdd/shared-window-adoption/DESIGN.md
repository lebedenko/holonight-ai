# DESIGN: Shared window adoption

**Spec:** `docs/sdd/shared-window-adoption/SPEC.md`
**Status:** Implemented
**Date:** 2026-07-23

## Window ownership

`HnApplicationWindow` derives from QML `Window`, so it cannot be used as the root item of the
existing `QQuickView`. `ChatApplication` therefore owns one `QQmlEngine`, creates
`WorkspaceWindow.qml` through `QQmlComponent`, validates that the result is a `QQuickWindow`, and
owns that window explicitly. Member order destroys the root window before its engine. The
workspace owns its eagerly declared settings window.

This keeps `ChatViewModel`, `ProviderSettingsController`, and
`OpenAIProviderSettingsController` in one QML engine. The existing close handler remains attached
to the workspace window and calls `ChatViewModel::stop()` before engine teardown.

## QML structure

`WorkspaceWindow.qml` and `SettingsWindow.qml` derive from `HnApplicationWindow`. Their shared
component headers remain hidden, so the compositor supplies the only visible window title bar and
outer border. `contentPadding` replaces duplicate root-layout margins.

`ConversationListPanel.qml` and `SettingsSidebar.qml` derive from `HnSurfaceFrame` with the
`Panel` role. Each owns an inset layout for its prior content. Semantic geometry is resolved by
`HnAppearance`; the AI application does not contain radius or chamfer constants.

## Deliberate boundaries

Message bubbles, banners, picker delegates, and provider controls remain unchanged. They have
control/card semantics that should be handled in a later, separately reviewable pass. The future
quick panel will use a complete `Panel` surface because it is a shell-managed panel rather than an
ordinary application window.

## Verification

Build and full CTest cover C++ compatibility. `task qml-lint` validates the migrated QML types and
bindings. Qt 6.8's `qmllint` currently reports the imported `Window`-derived composite as unknown,
even though the same installed module resolves it at runtime; this known warning is inspected
rather than suppressed. Manual verification covers compositor decoration, settings-window reuse,
theme changes, and semantic panel corners.

The development `task run` prepends `/tmp/holonight-qt-prefix` to `QML_IMPORT_PATH`. This prevents
an older user-local HoloNight installation from shadowing the dependency that the task has just
built and installed.
