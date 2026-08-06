# HoloNight AI Quick Panel — Specification

## Objective

Provide a compact AI chat panel anchored to the left edge of a Wayland output while keeping
`holonight-chat` isolated from `holonight-shell`.

The chat process owns and renders both its regular workspace window and its quick-panel surface.
The shell may request lifecycle changes over the session D-Bus, but it does not host chat QML or
native chat code.

## Constraints

- The panel must use the `zwlr_layer_shell_v1` protocol. A normal `xdg_toplevel`, window flags,
  and client-side geometry are not substitutes for a layer-surface role.
- The panel and workspace use the same `QQmlEngine` and application singletons.
- The shell and chat remain separate processes. No cross-process `QQuickItem` or surface embedding
  is attempted.
- The compositor configures final panel geometry. The client requests an output, anchors, width,
  margins, layer, exclusive zone, and keyboard interactivity.
- Only one `holonight-chat` instance owns `org.holonight.Chat` on the session bus.

## D-Bus API

Service: `org.holonight.Chat`

Object: `/org/holonight/Chat`

Interface: `org.holonight.Chat1`

Methods:

- `ShowWorkspace()`
- `ShowPanel(string outputName)`
- `CollapseToPanel(string outputName)`
- `ClosePanel(bool showWorkspace)`
- `TogglePanel(string outputName)` collapses the workspace into the panel, or closes the panel and
  restores the workspace.

Signals:

- `WorkspaceVisibilityChanged(bool visible)`
- `PanelVisibilityChanged(bool visible, string outputName)`

An empty `outputName` selects the primary screen. Unknown non-empty output names are rejected
without changing visible state.

## Requirements

### REQ-F-001 — Startup

Starting `holonight-chat` normally creates one visible workspace. The quick panel is not created
or mapped until requested.

### REQ-F-002 — Layer surface

Opening the panel creates a `QQuickView`, obtains its uncommitted Wayland `wl_surface`, and assigns
the `zwlr_layer_surface_v1` role before attaching a buffer.

The panel requests:

- top layer;
- top, bottom, and left anchors;
- width `clamp(output width × 0.30, 440, 560)` logical pixels;
- compositor-selected height;
- zero exclusive zone;
- a 12-pixel top, bottom, and left margin;
- exclusive keyboard interactivity while mapped;
- namespace `holonight-ai-panel`.

The view becomes visible only after the first layer-surface configure is acknowledged.

### REQ-F-003 — Collapse

`CollapseToPanel` opens the panel first. The workspace is hidden only after the panel receives its
first configure event. If panel creation fails, the workspace remains visible.

### REQ-F-004 — Expand

The panel expand action unmaps the panel, shows the workspace, raises it, and requests activation.
Conversation, draft, model, and streaming state remain intact because both presentations share
the same engine and singletons. Scroll position is presentation-local and need not transfer.

### REQ-F-005 — Closing

Escape and the panel close action call `ClosePanel(true)`. A shell keybind may call the same D-Bus
method. Closing with `showWorkspace=false` leaves the chat process running without a visible
window.

### REQ-F-006 — External activation

The installed D-Bus service file can launch `holonight-chat`. If another process already owns the
service name, a new invocation requests `ShowWorkspace()` from the owner and exits.

### REQ-F-007 — Shell isolation

A chat crash must not terminate the shell. The shell must interact only through the documented
D-Bus contract and must not load chat QML or link chat implementation libraries.

### REQ-F-008 — Shutdown

An in-flight generation is stopped once during application shutdown, not when either presentation
is merely hidden.

## Non-goals

- Cross-process visual embedding.
- A separate chat daemon in this cycle.
- Persisting panel visibility across login.
- Supporting compositors without `wlr-layer-shell`.
- Shell-side implementation of the chat UI.
- Reserving desktop work area for the temporary panel.

## Acceptance checks

- Normal startup shows only the workspace.
- Collapse maps a left-anchored layer surface and then hides the workspace.
- Expand and Escape restore the workspace without interrupting a stream.
- A D-Bus request can open the panel on a named output.
- An invalid output does not hide the workspace.
- A second launch activates the existing process.
- Closing the panel through either process follows the same lifecycle.
