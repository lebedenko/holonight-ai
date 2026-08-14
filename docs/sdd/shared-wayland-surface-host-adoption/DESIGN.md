# Shared Wayland Surface Host Adoption Design

Status: Accepted

## Ownership and structure

`PanelSurface` remains the narrow adapter consumed by `ChatApplication`. It resolves the requested
output and builds a complete `LayerSurfaceSpec`; one `LayerSurfaceHost` owns the view, native
surface, layer role, configure acknowledgement, presentation, and role-before-window teardown.
AI no longer generates protocols or accesses Wayland and Qt private APIs.

The spec builder is a deterministic policy seam. A small internal host interface lets lifecycle
tests substitute callbacks without a compositor; the production implementation contains exactly
one shared `LayerSurfaceHost` and forwards `open`, `close`, diagnostics, and lifecycle signals.

## Lifecycle

An accepted open stores the resolved output name and live host identity. The first configure marks
the adapter configured and emits `opened`; later configures are ignored at the application layer.
Explicit close detaches the host before asking it to close, then emits one `closed` event. Shared
host close/failure callbacks are queued and capture the originating identity. When delivered, they
act only if that identity is still current, preventing late callbacks from an earlier generation
from changing a reopened panel.

Output lookup failure occurs before host construction. Shared-host open failure clears adapter
state and propagates its diagnostic once. In every terminal path the adapter clears configuration
and output state while shared code retains native teardown ordering.

## Dependencies

CMake requires `HolonightQt` components `Controls` and `Wayland`, and `holonight_platform` links
`HolonightQt::Wayland`. Local tasks and both CI jobs build/install the exact config provider before
the exact Qt provider. The AI-owned XML, scanner setup, generated include paths, direct Wayland
client dependency, and Qt GUI-private bridge are removed.

## Compatibility and verification

No D-Bus, QML URL, or `ChatApplication` changes are made. Policy tests inspect every spec field;
lifecycle tests cover first configure, duplicates, both close paths, failure, invalid output,
repeated open, synchronous failure, and stale terminal callbacks. Live compositor checks remain in
SWS-201.
