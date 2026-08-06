# Chat Header Selection Specification

Status: Complete

## Requirements

- The chat header presents provider first and model second, making the selector dependency read
  from left to right while provider selection controls the
  model catalog.
- Only providers with cached or discovered models appear, in Ollama, OpenAI, Anthropic, Google
  order. Settings continues to show every supported provider.
- A provider change selects its session-remembered model, then its configured default, then its
  first available model. Invalid assignments are ignored.
- Conversation restoration remains authoritative while that conversation's provider is loading and
  seeds the session memory.
- Authoritative refresh removes empty providers and falls back to an available provider/model. With
  no models, selection is cleared and sending is disabled.
- The header reports Idle, Checking, LoadingModels, Connected, SetupRequired, Unavailable, or Error
  from runtime readiness and refresh state; detailed notices remain visible in the chat.
- The mockup is authoritative for the workspace header: one quiet, divider-backed surface contains
  an accent, inline `provider · model` selectors, flexible space, textual connection status, a
  sliders Settings action, and a down-chevron collapse action. There is no title label or outlined
  form treatment.
- Status is always represented by a semantic dot and one of `Idle`, `Checking…`, `Loading models…`,
  `Connected`, `Setup required`, `Unavailable`, or `Error`. At constrained widths only the label
  hides; the dot keeps an accessible description.
- Quick panel retains its top bar and uses the same borderless selector language in a compact second
  row without workspace-only Settings or collapse actions.
- Dropdowns support keyboard navigation, bounded scrolling, focus/hover feedback, elision, and
  accessible names. Empty and loading states must remain truthful.

## Acceptance

Automated application, QML lint, build, test, and formatting checks pass. Manual comparison against
the mockup confirmed the inline selector cluster and subtle chevrons, right-aligned status, matching
square actions, divider, spacing, typography hierarchy, and interaction states. Manual acceptance
also covered normal/minimum workspace and quick-panel sizes, selection restoration, Settings
navigation, collapse/expand behavior, and a clean runtime console.
