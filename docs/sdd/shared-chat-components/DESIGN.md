# DESIGN: Shared chat components

**Spec:** `docs/sdd/shared-chat-components/SPEC.md`
**Status:** Implemented
**Date:** 2026-07-23

## Component boundary

`qml/shared/ChatPanel.qml` is the reusable composition root. It binds directly to the process-wide
`ChatViewModel`, ensuring every future surface observes the same active conversation, draft, model,
and running request. It composes four focused presentation areas:

- `ModelPicker.qml`: provider-qualified model selection.
- `ChatNoticeStack.qml`: persistence and request feedback.
- `MessageList.qml` with `MessageBubble.qml`: transcript layout and scroll policy.
- `ChatComposer.qml`: draft editing and Send/Stop/Retry actions.

The components remain inside the existing `HolonightChat` QML module. The current recursive QML
resource registration discovers them without an explicit source list update.

## Presentation policy

`ChatPanel` exposes a small policy surface rather than separate desktop and compact variants:

- `compact` tightens spacing and selects a wider bubble ratio.
- `showModelPicker` controls whether constrained surfaces include model selection.
- `showMessageStatus` controls per-message lifecycle labels.
- `showRetryAction` controls the optional retry button.
- `messageWidthRatio` permits a surface-specific bubble-width policy and defaults from `compact`.

These properties affect presentation only. They do not cache or mirror backend state. A later quick
panel can set them while preserving the same message model, draft, selected model, and stream.

`ChatNoticeStack` collapses when neither notice exists and is capped at its implicit content height
when populated. This prevents its layout wrapper from competing with `MessageList` for the panel's
remaining vertical space.

`MessageList` places a loaded transcript at the beginning, regardless of its total height. When a
new response starts streaming, it enables bottom-following; short content remains top-aligned and
overflowing content follows the latest delta. Once the user scrolls away from the bottom during
streaming, automatic following remains suspended until the next response begins.

## Workspace ownership

`WorkspaceWindow.qml` continues to own `HnApplicationWindow`, `SettingsWindow`, the settings
launcher, and `ConversationListPanel`. Its main content column is replaced with `ChatPanel`.
Settings remains eager so provider-controller initialization and shared-engine ownership are
unchanged.

## Verification

QML compilation during the application build validates module registration and component type
resolution. `task qml-lint` checks bindings and QML types; Qt 6.8 retains the documented false
warning for the imported `Window`-derived `HnApplicationWindow`, plus a downstream unresolved
`SettingsWindow` property warning. The shared components introduce no lint diagnostics. `task test`
protects the unchanged `ChatViewModel` behavior. Manual verification covers model selection,
scrolling, notices, Enter/Shift+Enter, Send/Stop/Retry, and the unchanged workspace layout in both
themes.
