# Chat rendering with QMarkdown 1.18

## Intent and ownership

Workspace and quick-panel user and assistant messages render through one shared
`MarkdownBlock.qml`, using installed QMarkdown >=1.18. System text and tool cards
retain their existing presentation. No persistence or provider protocol changes occur.

The [umbrella contract](../../../../docs/initiatives/qmarkdown-chat-migration.md)
coordinates the package recipe, shared image prerequisite, and consumer qualification.
QMarkdown stays package-owned: native builds and the umbrella installer never fetch,
build, or install it. The package enables `QMARKDOWN_WITH_KSYNTAXHIGHLIGHTING=ON`.

## Interfaces and decisions

`MarkdownBlock` is a QMarkdown `MarkdownView`. It accepts the complete `markdown`
source, `streaming`, and an optional `selectionViewport`, and forwards
`selectionScrollRequested(deltaY)` to its transcript host. There is no application
parser, fence extraction, content-block model, publication timer, or HTML escaping.
Assistant source binds to the model's `text` role. Streaming is enabled only for
`streaming` status; complete/error/cancelled turns disable it and flush pending
snapshots even when the source has not changed. User and restored messages render
synchronously. QMarkdown parses full snapshots; no incremental throughput guarantee
is made.

GFM tables, tasks, strikethrough, and extended autolinks are enabled. Math, footnotes,
Find, outline, and custom fence renderers remain disabled. Links open through
`Qt.openUrlExternally`; QMarkdown's deny-all image resource policy remains the default.

Shared wrapper style and syntax-theme objects bind live to Holonight palette tokens
and the monospace font, over PolishedDark/PolishedLight presets selected from the
effective surface lightness. Presets own spacing, padding, borders and decoration.
This replaces KDE theme-file styling. Code stays unwrapped and without line numbers,
with QMarkdown height limits and scrolling on both axes for overflowing blocks; highlighting stays
on during streaming and unknown languages remain readable. The action header shows
QMarkdown's `languageName` or “plain text” and Copy/Copied feedback.

A small hidden `TextEdit` clipboard helper copies selection and exact parser-provided
code payloads, including trailing newlines. Terminal assistant full-response copy
uses untouched message source. Selection spans the document within one message.
Edge-scroll requests clamp against the host's actual range and suspend following
latest; existing bottom anchoring and positive-width delegate guards remain.

The app and QML test executables link `QMarkdown::QMarkdown`; build engine and lint
paths include its exported import path independently of HolonightQt. Installed
execution retains executable-relative lookup and Qt system imports. Separate custom
prefixes require `CMAKE_PREFIX_PATH` at configure time and `QML_IMPORT_PATH` plus the
platform library search path at installed runtime. Application metatype extraction,
merging, singleton registration, and isolated runtime fixture resources remain intact.

## Acceptance

Public API integration tests cover mixed prose/code, nested lists/quotes, GFM,
literal HTML, Unicode, unknown languages, incomplete streaming fences, terminal
flushes, replacement/restoration, document selection, exact code and raw copy, action
feedback, zero/narrow/wide layouts, and transcript edge scrolling. Runtime acceptance
must instantiate real user and assistant Markdown in both hosts and require the
highlighting backend. Installed staging tests must load package-owned plugins without
build imports. Run focused tests, task test, qml-lint, qmltypes-check, format-check,
then container check and check:integration with the rebuilt dependency image.
