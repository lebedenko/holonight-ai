# ADR 0001: Standalone repository and process

## Status

Accepted

## Context

HoloNight AI Chat needed a repository and process boundary decision relative to
`holonight-shell`. See `docs/high-level-project-idea.md` for the full comparison (release
lifecycle, reliability, dependency footprint, portability, security, testing, and future
agent/MCP extensibility).

## Decision

- HoloNight AI Chat is a separate repository (`holonight-ai`) and a separate process, not code
  inside `holonight-shell`.
- The executable is `holonight-chat`. A standalone workspace window is the canonical UI; a
  compact left-side quick panel is added later as another surface owned by the same process, not
  a second implementation.
- `holonight-shell` only activates, toggles, and queries coarse state over D-Bus — it never
  receives provider credentials, conversation databases, or raw tool-execution requests.
- Visual styling is shared via `holonight-qt` (`import Holonight`); `holonight-ai` never imports
  anything from `holonight-shell`.
- One running `holonight-chat` process owns both the workspace window and the quick panel
  initially. A separate background daemon (`holonight-ai-service`) is deferred until responses
  must continue after the UI closes, multiple frontends need simultaneous access, or
  scheduled/background agents are introduced.

## Consequences

- Packaging and IPC (D-Bus activation, panel toggling) are extra work compared to embedding chat
  in the shell, but the shell stays focused and lightweight, and chat/provider failures stay
  isolated from the compositor process.
- `holonight-ai` can be developed and tested (including headless provider/core tests) without a
  live Wayland/Hyprland session, and remains portable to other desktop environments.
