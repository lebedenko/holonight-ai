# Shared Wayland Surface Host Adoption Specification

Status: Accepted

## Baselines

- Work package: SWS-103
- `holonight-ai`: `6868ac8775ac997a9fadd1cd95fce159174a2d1d`
- `holonight-qt`: `a45f7552054abbc6cbd66609e802b43b9b8ee894`
- `holonight-config`: `5cd36ec9986801c4b831527a38e7b7414b9a1312`

## Requirements

AI shall replace its private wlr-layer-shell protocol and lifecycle implementation with
`Holonight::Wayland::LayerSurfaceHost`. `PanelSurface` remains the application-facing adapter and
keeps its operations, signals, output lookup, idempotent open/close semantics, and observable
`isOpen`, `isConfigured`, and `outputName` behavior.

The surface policy shall use the existing `QQmlEngine`, the selected `QScreen`, namespace
`holonight-ai-panel`, top layer, top/bottom/left anchors, a width of
`clamp(outputWidth * 0.30, 440, 560)`, compositor-selected height, margins `12/0/12/12`, zero
exclusive zone, default input region, and exclusive keyboard interactivity. The view shall load
`qrc:/HolonightChat/quickpanel/QuickPanel.qml`, start with the selected output name, and be
transparent, frameless, and compositor-managed.

Only the first configure shall emit application-level `opened`. Explicit close, compositor close,
and failure shall each produce one corresponding terminal application event. Queued terminal
callbacks shall be identity guarded so callbacks from an old host cannot terminate a replacement.

The `org.holonight.Chat1` contract, QML URLs, panel geometry, presentation state, and
`ChatApplication` collapse/restore sequencing shall remain unchanged. Missing layer-shell support
shall continue to fail panel opening without hiding the workspace.

Live compositor visual, hotplug, and close/reopen validation is deferred to umbrella SWS-201.
