# Quick Panel Design

## Ownership

`holonight-chat` owns all chat state and rendering. `holonight-shell` remains an optional control
peer. This preserves crash isolation while allowing the panel to use the same `QQmlEngine` and
singletons as the workspace.

```text
holonight-shell ── session D-Bus ──> holonight-chat
                                      ├─ WorkspaceWindow (xdg_toplevel)
                                      └─ QuickPanel (wlr layer_surface)
```

## Components

### `holonight_platform::PanelSurface`

Owns the Wayland-specific panel lifecycle:

1. Select a `QScreen`.
2. Create a hidden `QQuickView` on the application's existing engine.
3. Set `Qt::BypassWindowManagerHint` and create its native `wl_surface`.
4. Assign the layer-surface role before any buffer is committed.
5. Configure anchors, width, margins, layer, exclusive zone, and keyboard interactivity.
6. Make the required initial empty commit.
7. Acknowledge the compositor configure, resize, and show the view.
8. Destroy the role and view when closed.

Protocol setup failures are reported without hiding the workspace.

### `ChatApplication`

Owns the workspace and `PanelSurface`, exports the D-Bus control API, and sequences transitions.
For collapse, it records the pending intent and waits for `PanelSurface::opened` before hiding the
workspace. For expand, it closes the panel before showing and activating the workspace.

### QML

`QuickPanel.qml` is an `Item`, not a `Window`. It fills the `QQuickView` supplied by
`PanelSurface`, draws `HnSurfaceFrame`, and hosts `ChatPanel { compact: true }`.

The workspace and panel call `ChatApplication` methods through one root-context property.

## Single-instance behavior

The first process registers `org.holonight.Chat`. Registration happens before creating visible
windows. A later process calls `ShowWorkspace()` on the existing owner and exits.

The D-Bus activation file uses the same executable and service name, so shell calls work whether
or not chat is already running.

## Failure behavior

- No Wayland platform: panel requests fail and the workspace remains visible.
- No layer-shell global: same behavior, with a diagnostic.
- Unknown output: request is rejected without changing state.
- Compositor closes the layer surface: panel resources are released and the workspace is shown
  when the close policy requests it.
- Shell absent: workspace and in-panel controls continue to work.

## Testing

Pure panel width and output-selection policy are unit-tested without a compositor. Build and QML
lint cover integration. Live layer-shell placement and D-Bus activation require a manual test
under the HoloNight Wayland session.
