# Chat Composer Actions Boundary — Specification

Status: Complete. This cycle is a behavior-preserving ownership refactor of the existing composer footer.

## Functional requirements

### REQ-F-001: Dedicated action row

`ChatComposerActions` shall own the attachment, context, tools, retry, hint, spacer, and send controls.

### REQ-F-002: Explicit input and signal contract

The component shall require `compact`, `showRetryAction`, `canRegenerate`, and `canSubmit` Boolean inputs and shall
emit `retryRequested()` and `submitRequested()` without binding directly to `ChatViewModel`.

### REQ-F-003: Behavioral compatibility

Button text, icons, visibility, enabled states, sizing, spacing, accessibility metadata, and existing object names
shall remain unchanged. Stable seam names `composerActions` and `retryButton` shall be available for focused tests.

## Non-functional requirements

- **REQ-NF-001:** `ChatComposer` retains draft synchronization, validation, keyboard handling, editor sizing/focus,
  and action-to-view-model translation.
- **REQ-NF-002:** `ChatComposer`, `ChatPanel`, and `ChatViewModel` public contracts shall not change.
- **REQ-NF-003:** Focused QML coverage shall exercise layout policy, placeholders, accessibility, and signals.
- **REQ-NF-004:** QML lint, formatting checks, and automated tests shall remain clean.

## Constraints

- **REQ-C-001:** No visual redesign or draft-entry behavior changes.
- **REQ-C-002:** Attachment, context, and tools remain disabled placeholders.
- **REQ-C-003:** No C++ API, model role, persistence, QML registration strategy, or generated dependency changes.
