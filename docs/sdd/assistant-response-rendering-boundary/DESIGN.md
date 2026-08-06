# Assistant Response Rendering Boundary — Design

Status: Stage 2 (Design). This design implements `SPEC.md` without changing C++ interfaces.

## Ownership boundary

Add `qml/shared/AssistantResponseContent.qml` as a `ColumnLayout` with the exact required properties
`contentModel` and `messageStatus`. It owns the block `Repeater`, Markdown/code component dispatch,
the hidden plain-text copy source, response copy button, accessible feedback label, and reset timer
(REQ-F-001, REQ-F-003, REQ-F-004).

`MessageBubble` replaces its inline block and response-copy sections with that component. The parent
continues to decide whether assistant content is visible from the authoritative message text and
retains attribution, timestamp, user text, error border, empty-placeholder suppression, and stats
footer unchanged (REQ-F-002, REQ-C-003). The extracted component binds directly to
`contentModel.rawMarkdown`; it never reconstructs source from rows (REQ-F-004).

## Delegate lifecycle

The extracted `Repeater` keeps the existing persistent model and role bindings. Its loader selects
`MarkdownBlock` or `ChatCodeBlock` by `ContentBlockType` and is not replaced when a matching row
emits `dataChanged`. Stable object names identify the row loader and code block only for focused
tests (REQ-F-003, REQ-NF-001, REQ-NF-002).

Within `ChatCodeBlock`, a `Loader` is active only while `complete` is true. Its component constructs
`CodeHighlighter`, binds the language, and attaches it to the existing `TextEdit` document in
`onLoaded`. Until then the body remains the same literal/selectable `TextEdit` and the header uses
“plain text.” Completion changes the nested loader, not the code-block delegate (REQ-F-005).

The language header null-checks the loaded item. Palette notifications call `refreshTheme()` only
when the item exists and reports active highlighting. Thus completed unknown or missing languages
can resolve safely but remain inactive and plainly labelled (REQ-F-006).

## Verification

Focused QML tests use a real `MessageContentModel` to cover ordered mixed blocks, loader and delegate
identity across completion, absent/present highlighter lifecycle, literal selectable code, unknown
language fallback, terminal action visibility, exact clipboard content, and feedback. Existing
message-bubble tests continue to cover attribution and empty cancelled placeholders. Then run QML
lint, format checking, and the full test suite (REQ-NF-003).

No new block registry, parser/model changes, C++ API, cross-block selection, shared-control work, or
dependency is introduced (REQ-C-001 through REQ-C-003).
