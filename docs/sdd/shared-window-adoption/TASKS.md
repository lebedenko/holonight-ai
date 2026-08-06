# TASKS: Shared window adoption

**Spec:** `docs/sdd/shared-window-adoption/SPEC.md`
**Design:** `docs/sdd/shared-window-adoption/DESIGN.md`

- [x] T-001: Record the compositor-native window and semantic-panel requirements.
- [x] T-002: Replace the `QQuickView` host with one `QQmlEngine` and an explicitly owned root window.
- [x] T-003: Preserve eager OpenAI initialization and stream cancellation on close.
- [x] T-004: Migrate `WorkspaceWindow.qml` to `HnApplicationWindow`.
- [x] T-005: Migrate `SettingsWindow.qml` to `HnApplicationWindow` on the shared engine.
- [x] T-006: Apply `HnSurfaceRole.Panel` to conversation and settings navigation.
- [x] T-007: Update repository theming/status guidance.
- [x] T-008: Run build and automated tests (254/254 passing).
- [x] T-009: Run QML lint and inspect the Qt 6.8 composite-window type warning.
- [x] T-010: Manually verify compositor chrome, both themes, settings reuse, and panel corners.
