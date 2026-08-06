# SPEC: Shared window adoption

**Feature:** Adopt HoloNight's shared application-window and semantic-surface components
**Status:** Implemented
**Date:** 2026-07-23

## Scope

Migrate the chat workspace and settings window to the compositor-native window contract exported
by `holonight-qt`. Apply semantic frames only to major internal navigation panels. This cycle does
not add client-side decoration, shell integration, or a quick panel.

## Requirements

- The workspace and settings roots shall use `HnApplicationWindow`.
- Both windows shall retain ordinary compositor decoration and shall not draw an inner perimeter.
- The workspace shall retain its 960x640 initial size and 720x480 minimum size.
- The settings window shall retain its existing size, minimum size, title, visibility, and shared
  QML-engine ownership.
- `ChatApplication` shall create the workspace as a top-level QML window through one `QQmlEngine`.
- Chat shutdown shall still cancel an active stream before QML teardown.
- OpenAI provider settings shall still initialize eagerly on that same QML engine.
- The conversation navigation and settings navigation shall use `HnSurfaceFrame` with the
  `HnSurfaceRole.Panel` role.
- Window and panel spacing shall continue to use shared HoloNight palette metrics.

## Constraints

- No `Qt.FramelessWindowHint`, custom resize edges, window mask, blur, or shell import.
- Do not convert control-level backgrounds or message bubbles in this cycle.
- Do not split settings into a second QML engine.

## Acceptance

The application builds, all automated tests pass, QML lint reports no new errors, both windows use
normal compositor chrome, settings reuses the application singletons, and the two navigation
panels respond to the shared semantic corner configuration.
