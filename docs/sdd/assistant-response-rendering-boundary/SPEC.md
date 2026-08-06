# Assistant Response Rendering Boundary — Specification

Status: Stage 1 (Specification). This cycle succeeds `incremental-chat-content-rendering` and
preserves that cycle's persistent model and observable behavior.

## Functional requirements

### REQ-F-001: Dedicated assistant renderer

`AssistantResponseContent` shall render one persistent `MessageContentModel` through the QML
contract `required property var contentModel` and `required property string messageStatus`. It shall
dispatch Markdown and code delegates in model order and own terminal full-response copying.

### REQ-F-002: Message chrome ownership

`MessageBubble` shall continue to own role selection, provider attribution, error chrome, timestamp,
waiting/empty-message policy, and response usage statistics. User-message rendering shall not change.

### REQ-F-003: Behavioral compatibility

The extraction shall preserve block order and source fidelity, stable delegate identity during tail
updates, per-block selection, scrolling behavior, terminal visibility, clipboard feedback, and
accessible copy naming.

### REQ-F-004: Terminal response copy

The response-copy action shall be visible only for non-empty `complete`, `error`, or `cancelled`
responses. It shall copy `contentModel.rawMarkdown` exactly, including fences, whitespace, Unicode,
and special characters.

### REQ-F-005: Completion-gated highlighting

`ChatCodeBlock` shall create and attach `CodeHighlighter` only after `complete` becomes true, without
replacing the code-block delegate. Incomplete code shall remain literal, selectable plain text.

### REQ-F-006: Safe language and theme behavior

A completed block may resolve its language through its highlighter. Missing or unknown languages
shall remain inactive and display “plain text.” Palette changes shall refresh only an existing,
active highlighter.

## Non-functional requirements

- **REQ-NF-001:** The ownership refactor shall not reset persistent delegates or introduce visible
  streaming flicker.
- **REQ-NF-002:** Focused QML tests shall assert behavior through stable object names only where
  necessary.
- **REQ-NF-003:** Repository QML lint, formatting, and automated tests shall remain warning-free.

## Constraints

- **REQ-C-001:** Do not add block types, a renderer registry, or cross-block selection.
- **REQ-C-002:** Do not change parsers, content models, model roles, or public C++ APIs.
- **REQ-C-003:** Do not change user-message behavior or `holonight-qt`.

## Acceptance criteria

Mixed Markdown/code responses render in source order; a growing tail retains its delegate; no
highlighter exists before code completion; known completed code can highlight while unknown and
language-less code stays plain; terminal copy is status-gated and byte-for-byte faithful at the
QString level; and `MessageBubble` retains empty-terminal and usage-stat policies.
