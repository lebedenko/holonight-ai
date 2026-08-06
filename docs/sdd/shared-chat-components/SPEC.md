# SPEC: Shared chat components

**Feature:** Extract reusable chat presentation for workspace and compact surfaces
**Status:** Implemented
**Date:** 2026-07-23

## Scope

Move the provider/model picker, notices, message list and bubbles, and composer actions out of the
workspace window into reusable components under `qml/shared/`. The workspace remains the canonical
surface and adopts the shared composition without changing chat behavior. This cycle prepares the
presentation boundary for a later quick-panel cycle; it does not create that panel.

## Requirements

- `WorkspaceWindow.qml` shall retain workspace-only ownership of conversation navigation, settings,
  and the top-level application window.
- A reusable `ChatPanel` shall compose model selection, notices, messages, and message composition.
- Shared components shall use the existing `ChatViewModel` singleton as the only chat state and
  action source.
- The model picker shall preserve provider-qualified labels and selected-model matching.
- The message list shall preserve user/assistant alignment, streaming status text, and wrapping.
  Loaded transcripts shall start at the top. An active response shall follow the bottom once it
  overflows the viewport, until the user scrolls away.
- The composer shall preserve draft synchronization, Enter-to-send, Shift+Enter, Send, Stop, and
  Retry behavior.
- Persistence and request errors shall retain their current presentation and dismissal behavior.
- Compact consumers shall be able to control status/action visibility and message width without
  duplicating chat components.
- The full workspace presentation shall remain behaviorally and visually equivalent.

## Constraints

- Do not add a quick-panel window, layer-shell integration, D-Bus API, or single-instance handling.
- Do not move presentation state into C++ or introduce another chat view model.
- Do not change provider, persistence, conversation, or streaming behavior.
- Do not add a general-purpose component framework; keep each shared component feature-specific.

## Acceptance

The project builds, all automated tests pass, QML lint introduces no new diagnostics, the workspace
can send/stop/retry and select models as before, and compact presentation properties can be changed
without creating a second chat implementation.
