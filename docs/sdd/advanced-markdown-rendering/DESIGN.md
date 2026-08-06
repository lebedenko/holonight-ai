# Advanced Markdown Rendering for Chat Messages — DESIGN.md

Status: Stage 2 (Design/Architecture) of the SDD cycle. Implements every requirement in `SPEC.md`
(REQ-F-001…011, REQ-NF-001…003, REQ-C-001…010). Read `SPEC.md` first; this document does not
restate acceptance criteria, it explains how they are satisfied.

---

## 1. Overview

Today `MessageBubble.qml` renders every assistant message — streaming or finished — through one
`Text { textFormat: Text.MarkdownText }` element. This design introduces a second rendering path
that only exists for *finished* messages: a synchronous MD4C parse splits the message into an
ordered list of `Markdown`/`Code` blocks, and a `Repeater` renders each block with a dedicated
delegate — plain `Text.MarkdownText` for prose, a new `HnCodeBlock` component (KSyntaxHighlighting
syntax colors, header, copy button) for fenced code. Streaming messages are untouched.

The new logic lives in a new bottom-of-the-stack static library, `holonight_rendering`, so that
parsing/highlighting concerns never leak into `holonight_application`'s orchestration code, and so
that a future headless test target can exercise `MessageContentParser`/`CodeHighlighter` without
pulling in the whole application module.

A second, smaller piece of work happens in the sibling `holonight-qt` repository: a build-time code
generator that turns each of the 10 `ColorTokens` schemes into a KSyntaxHighlighting Kate-format
`.theme` JSON file, installed alongside the `HolonightQt` CMake package.

---

## 2. Components

| Component | Module | Responsibility | Requirements |
|---|---|---|---|
| `ContentBlockType`, `ContentBlock` | `holonight_rendering` | QML-visible discriminated union: a block is either `Markdown` (prose text) or `Code` (code + normalized language). | REQ-F-001 |
| `LanguageAlias::normalize()` | `holonight_rendering` | Pure string function: fence info-string → canonical display name or pass-through. | REQ-F-003 |
| `MessageContentParser` | `holonight_rendering` | MD4C-backed: raw message text → `std::vector<ContentBlock>`. Stateless, synchronous, called only for `complete`/`error` messages. | REQ-F-002, REQ-NF-001 |
| `CodeHighlighter` | `holonight_rendering` | `KSyntaxHighlighting::SyntaxHighlighter` subclass. QML-instantiable. Owns a `Repository`, resolves a `Definition` for a normalized language, resolves a `Theme` for the active HoloNight scheme, re-applies the theme on scheme changes. | REQ-F-004, REQ-F-009, REQ-F-011, REQ-NF-003, REQ-C-002, REQ-C-009, REQ-C-010 |
| `MessageListModel::ContentBlocksRole` | `holonight_application` | New model role: lazily parses + caches blocks for `complete`/`error` rows; empty list otherwise; never touched by `updateLastMessage()`'s hot path. | REQ-F-007, REQ-F-008 |
| `MessageBubble.qml` | `apps/chat` (qml/shared) | Dual-branch layout: streaming `Text.MarkdownText` vs. block `Repeater`, switched by `visible`, never by `Loader.sourceComponent` swap (flicker risk — see §7). | REQ-F-007, REQ-F-010 |
| `MarkdownBlock.qml` (new) | `apps/chat` (qml/shared) | One `Text { textFormat: Text.MarkdownText }`, used as the `Repeater` delegate for `Markdown` blocks. | REQ-F-010 |
| `HnCodeBlock.qml` (new) | `apps/chat` (qml/shared) | Header (language label + copy button) + monospace, horizontally-scrolling, plain-text, selectable code body; owns one `CodeHighlighter`. | REQ-F-005, REQ-F-006, REQ-NF-002 |
| holonight-qt theme generator | `holonight-qt/tools/theme-generator` | Emits 10 Kate-format `.theme` JSON files from `tokensForScheme()`, installed with the `HolonightQt` package. | REQ-C-007, REQ-C-008 |

Module dependency graph (new edges in bold):

```
holonight_domain
    ^
    |
holonight_rendering  --(also links: HolonightQt::Config, KF6::SyntaxHighlighting, PkgConfig::MD4C,
    ^                    Qt6::Gui, Qt6::Qml)
    |  **PRIVATE**
holonight_application  ──> holonight_providers, holonight_persistence, holonight_config, holonight_credentials
    ^
    |
holonight-chat (executable)
```

`holonight_rendering` depends on exactly one sibling `src/*` module (`holonight_domain`), per
REQ-C-004. Its other dependencies (`HolonightQt::Config`, `KF6::SyntaxHighlighting`,
`PkgConfig::MD4C`) are external packages, not sibling modules, so they don't violate that
constraint. `holonight_application` links it `PRIVATE` (REQ-C-005) — §5.3 explains how the header
boundary is kept clean enough that `PRIVATE` actually means something here, not just a CMake
formality.

---

## 3. Data Flow

### 3.1 Streaming (unchanged behavior, now explicit)

```
ChatController --token--> ChatViewModel --> MessageListModel::updateLastMessage(msg)
                                                 |
                                                 status ∈ {pending, streaming}
                                                 |
                                        Row = toRow(msg)   // cached_content_blocks defaults empty
                                                 |
                                        dataChanged(TextRole, StatusRole, ContentBlocksRole, ...)
                                                 |
                                        MessageBubble.qml: streaming Text branch visible,
                                        block Repeater branch (0 items) invisible
```

`ContentBlocksRole` is included in every `dataChanged()` role list (not just on completion) so QML
re-queries it the instant status flips — see §4.5. While streaming it always evaluates to `[]`
without invoking the parser, satisfying REQ-F-008's "no recompute on the hot path" and REQ-F-007's
"no blocks instantiated during streaming".

### 3.2 Completion

```
ChatController: message reaches Complete/Error --> ChatViewModel --> MessageListModel::updateLastMessage(msg)
                                                        |
                                        Row = toRow(msg)   // brand-new Row value; cached_content_blocks
                                                             starts empty again (see §7, "why a full Row
                                                             replacement makes explicit cache invalidation
                                                             unnecessary")
                                                        |
                                        dataChanged(..., ContentBlocksRole)
                                                        |
                                        QML re-reads model.contentBlocks for that row
                                                        |
                                        MessageListModel::data(index, ContentBlocksRole):
                                          status ∈ {complete, error} AND cache empty
                                                        |
                                          MessageContentParser::parse(row.text)   // MD4C, main thread
                                                        |
                                          cache QVariantList of ContentBlock, return it
                                                        |
                                        MessageBubble.qml: streaming Text branch hidden,
                                        block Repeater branch visible, one delegate per block:
                                          - Markdown block -> MarkdownBlock.qml (Text.MarkdownText)
                                          - Code block     -> HnCodeBlock.qml
                                                                |
                                                        CodeHighlighter attached to the code
                                                        TextEdit's QTextDocument; language resolved
                                                        against the Repository; theme resolved against
                                                        the active HoloNight scheme
```

Parsing is triggered by the *first* `data(index, ContentBlocksRole)` call after completion, not
eagerly inside `updateLastMessage()` — see §7 for why.

### 3.3 Live theme switch (independent of the above)

```
User changes appearance -> Holonight::ThemeConfig file on disk changes
        |                                     |
        |                          HoloniightPalette (QML singleton, holonight-qt)
        |                          detects file change, reloads tok_, emits paletteChanged()
        |
        +-- MessageBubble's already-materialized HnCodeBlock instances are untouched --+
                                                |
                        HnCodeBlock.qml: Connections { target: HoloniightPalette
                                                        function onPaletteChanged() }
                                                |
                                    codeHighlighter.refreshTheme()  // Q_INVOKABLE
                                                |
                        CodeHighlighter re-reads Holonight::ThemeConfig::load()
                        (same file HoloniightPalette itself watches), resolves the
                        scheme id, calls repository_.theme(schemeId), setTheme() —
                        no re-parse of the code text, no document rebuild.
```

No conversation re-render, no re-parse, no MD4C invocation — only the highlighter's format table
is swapped. See §6.4 for why this is a QML-mediated connection rather than a direct C++ signal/slot,
which the SPEC's phrasing suggests but the actual holonight-qt build does not expose.

---

## 4. Interfaces / APIs

### 4.1 `ContentBlockType` / `ContentBlock` — `include/holonight_rendering/content_block.h`

```cpp
#pragma once

#include <QMetaType>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <variant>

namespace holonight_rendering {

// Namespace-style holder purely so the enum gets a QML-visible name (`ContentBlockType.Code`)
// distinct from the `ContentBlock` value type itself.
class ContentBlockTypeNs {
  Q_GADGET
  QML_NAMED_ELEMENT(ContentBlockType)

 public:
  enum Type : std::uint8_t { Markdown, Code };
  Q_ENUM(Type)
};

using ContentBlockType = ContentBlockTypeNs::Type;

// Discriminated union (REQ-F-001). Deliberately NOT a QObject: blocks are produced in batches of
// unpredictable size on every message completion, and QObject-per-block would mean parent/lifetime
// bookkeeping for a value that is conceptually immutable data. A Q_GADGET registered as a QML
// value type carries no such baggage and is copy/compare/assign like any other value.
class ContentBlock {
  Q_GADGET
  QML_VALUE_TYPE(contentBlock)

  Q_PROPERTY(holonight_rendering::ContentBlockType type READ type CONSTANT)
  Q_PROPERTY(QString text READ text CONSTANT)      // Markdown prose, or the raw code — see text()
  Q_PROPERTY(QString language READ language CONSTANT)  // "" for Markdown blocks

 public:
  ContentBlock() = default;  // required by QML_VALUE_TYPE; produces an empty Markdown block

  [[nodiscard]] static ContentBlock markdown(QString text);
  [[nodiscard]] static ContentBlock code(QString code, QString normalizedLanguage);

  [[nodiscard]] ContentBlockType type() const noexcept { return type_; }
  // Markdown block: the raw markdown source slice (REQ-F-001, "retrievable as QString").
  // Code block: the raw code content (REQ-F-001, "retrievable as QString").
  [[nodiscard]] QString text() const;
  // Code block: the language after REQ-F-003 normalization. Markdown block: "".
  [[nodiscard]] QString language() const;

  [[nodiscard]] bool operator==(const ContentBlock&) const noexcept = default;

 private:
  struct MarkdownPayload {
    QString text;
    bool operator==(const MarkdownPayload&) const = default;
  };
  struct CodePayload {
    QString code;
    QString language;
    bool operator==(const CodePayload&) const = default;
  };

  ContentBlockType type_ = ContentBlockType::Markdown;
  std::variant<MarkdownPayload, CodePayload> payload_{MarkdownPayload{}};
};

}  // namespace holonight_rendering

Q_DECLARE_METATYPE(holonight_rendering::ContentBlock)
```

`MessageListModel::data(ContentBlocksRole)` returns a `QVariantList` where each element is
`QVariant::fromValue<ContentBlock>(...)`. A `Repeater { model: message.contentBlocks }` binds each
delegate's `modelData` directly to a `ContentBlock` value — no adapter model needed.

### 4.2 `LanguageAlias` — `include/holonight_rendering/language_alias.h`

```cpp
#pragma once

#include <QString>

namespace holonight_rendering::LanguageAlias {

// Pure function, no KSyntaxHighlighting dependency (REQ-F-003 is a string transform; deciding
// whether the *result* is a language KSyntaxHighlighting actually knows about is REQ-F-004's job,
// done later by CodeHighlighter — see §7, "why normalization and unknown-language detection are
// two different steps run at two different times").
//
// cpp/c++/cxx -> "C++"; js/javascript -> "JavaScript"; ts/typescript -> "TypeScript";
// sh/bash/shell -> "Bash"; py/python -> "Python"; qml -> "QML". Anything else (including "",
// "rust", "go") is returned unchanged (trimmed).
[[nodiscard]] QString normalize(const QString& fenceInfoStringLanguage);

}  // namespace holonight_rendering::LanguageAlias
```

Matching is case-insensitive on the alias keys (`Cpp`, `CPP`, `cpp` all normalize identically);
comparison against the alias table is done on a lower-cased, trimmed copy of the input, but the
canonical output ("C++", "JavaScript", ...) is always the exact casing shown above regardless of
input casing.

### 4.3 `MessageContentParser` — `include/holonight_rendering/message_content_parser.h`

```cpp
#pragma once

#include <holonight_rendering/content_block.h>

#include <QString>
#include <vector>

namespace holonight_rendering {

// Stateless, synchronous, MD4C-backed. Safe to default-construct per call site (MessageListModel
// keeps one instance as a member; there is no per-parse setup expensive enough to amortize further
// — see REQ-NF-001 and §9's note on MD4C's own performance characteristics).
class MessageContentParser {
 public:
  // Empty input -> empty vector (REQ-F-002: "an empty message produces an empty block list, not a
  // single empty block"). Malformed input is handled internally by falling back to a single
  // Markdown block containing the original text verbatim (REQ-F-002: parsing errors never crash
  // the app and never lose the message content — this is the fallback that satisfies REQ-F-007's
  // sibling requirement, "falls back to single-text rendering").
  [[nodiscard]] std::vector<ContentBlock> parse(const QString& markdownText) const;
};

}  // namespace holonight_rendering
```

**Parsing strategy.** MD4C's callback-based API (`md_parse(text, size, MD_PARSER*, userdata)`)
delivers block/span/text events whose text pointers, per MD4C's own zero-copy design, alias
directly into the input buffer for ordinary prose (this is MD4C's core performance property, not
an incidental implementation detail — it's why byte-offset recovery via pointer arithmetic is a
supported technique, not a hack). `MessageContentParser::parse()`:

1. Converts `markdownText` to a `QByteArray` (UTF-8) and keeps it alive for the duration of the
   parse — this buffer, not MD4C's decoded event text, is what Markdown blocks are sliced from.
2. Registers an `MD_PARSER` vtable (`enter_block`, `leave_block`, `text`) with a small parse-context
   struct as `userdata`, holding: the buffer's base pointer, a `lastFlushOffset`, an in-progress
   markdown segment, and (while inside a fenced code block) an accumulating code buffer + the fence's
   `MD_ATTRIBUTE lang` field.
3. Outside a code block: nothing is done per-event; the *original source slice* from
   `lastFlushOffset` to the fenced code block's start offset (recovered via pointer arithmetic on
   the first `text` callback fired once inside the code block, then walking backward to the
   enclosing line's start to exclude the fence line itself) becomes a `Markdown` block, trimmed of
   leading/trailing blank lines but otherwise byte-identical to the source — this is what makes it
   safe to hand to `Text.MarkdownText` a second time downstream: it never was decoded, so no
   markdown syntax was lost.
4. Inside `MD_BLOCK_CODE` (fenced or indented — the SPEC scope is fenced only, per REQ-F-002's
   "delimited by triple-backticks or triple-tildes"; indented code blocks are intentionally left in
   the Markdown segment and continue rendering via `Text.MarkdownText`'s own native handling): `text`
   callbacks of type `MD_TEXT_CODE` are concatenated verbatim into a code buffer (MD4C's contract for
   this text type explicitly preserves indentation and newlines, so no reconstruction is needed);
   `detail->lang` from the `MD_BLOCK_CODE_DETAIL` supplied to `enter_block` becomes the raw language
   string, passed through `LanguageAlias::normalize()`.
5. `leave_block(MD_BLOCK_CODE)` emits the accumulated `Code` block and advances `lastFlushOffset`
   past the closing fence line.
6. After `md_parse()` returns, any trailing markdown segment is flushed.

This is the trickiest part of the module — precise fence-boundary recovery has edge cases (fences
at document start/end, back-to-back fences with no prose between them, fences inside blockquotes).
It is called out again in §9 as the primary implementation risk, with a concrete test-case list.

### 4.4 `CodeHighlighter` — `include/holonight_rendering/code_highlighter.h`

```cpp
#pragma once

#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/SyntaxHighlighter>

#include <QObject>
#include <QQuickTextDocument>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace holonight_rendering {

// QML-instantiable: each HnCodeBlock.qml creates exactly one of these and attaches it to its code
// TextEdit's document. Subclasses KSyntaxHighlighting::SyntaxHighlighter per REQ-C-003/REQ-F-009.
class CodeHighlighter : public KSyntaxHighlighting::SyntaxHighlighter {
  Q_OBJECT
  QML_ELEMENT

  // The language after REQ-F-003 normalization (or "" / an unknown string). Setting it resolves a
  // KSyntaxHighlighting::Definition and calls the base class's setDefinition(); an unresolved
  // language clears the definition (REQ-F-004/REQ-NF-003: no highlighting, no crash, no log spam).
  Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
  // True once a valid Definition is applied — HnCodeBlock's header uses this (rather than
  // re-deriving the same lookup a second time in QML) to decide between the normalized language
  // label and the neutral "plain text" label.
  Q_PROPERTY(bool highlightingActive READ highlightingActive NOTIFY highlightingActiveChanged)

 public:
  explicit CodeHighlighter(QObject* parent = nullptr);
  ~CodeHighlighter() override = default;

  // Attaches this highlighter to the QTextDocument backing a QML TextEdit/TextArea. Must be called
  // once, before or after setLanguage() (order doesn't matter — both paths call rehighlight() only
  // once state is complete, avoiding the "visible uncolored flash" REQ-F-009 forbids).
  Q_INVOKABLE void attachTo(QQuickTextDocument* document);

  [[nodiscard]] QString language() const { return language_; }
  void setLanguage(const QString& normalizedLanguage);
  [[nodiscard]] bool highlightingActive() const { return highlighting_active_; }

  // Re-reads the active HoloNight scheme and re-applies the matching KSyntaxHighlighting theme.
  // Called from QML on HoloniightPalette::paletteChanged (see §6.4) — never re-parses or
  // re-derives the language, only swaps the Theme, so it never re-triggers highlightingActive's
  // notify unless the definition itself changes (it doesn't).
  Q_INVOKABLE void refreshTheme();

 signals:
  void languageChanged();
  void highlightingActiveChanged();

 private:
  void applyThemeForActiveScheme();  // shared by the constructor and refreshTheme()

  KSyntaxHighlighting::Repository repository_;
  QString language_;
  bool highlighting_active_ = false;
};

}  // namespace holonight_rendering
```

Construction: `CodeHighlighter()` calls `repository_.addCustomSearchPath(...)` only if the
compile-time theme directory (`HOLONIGHT_SYNTAX_THEME_DIR`, see §5.1) exists on disk at runtime
(`QFileInfo::exists()`), then calls `applyThemeForActiveScheme()`, which resolves the active scheme
via `Holonight::ThemeConfig::load().resolvedThemeScheme()` and `Holonight::schemeIdForKind()` (both
from the already-public `HolonightQt::Config` package — see §6.4 for why this, not a direct
`HoloniightPalette` connection, is what REQ-C-009/REQ-C-010 actually resolve to in this codebase),
looks up `repository_.theme(schemeId)`, and calls `SyntaxHighlighter::setTheme()` only if the
returned `Theme::isValid()`. If the theme directory is missing or the specific theme file can't be
found, the Repository has no custom themes loaded and `theme(schemeId)` returns an invalid `Theme`
— `setTheme()` is simply skipped, leaving KSyntaxHighlighting's own bundled default theme in place
(REQ-C-009: "falls back to an empty/default Repository", "no crash, no error log required").

### 4.5 `MessageListModel` — role addition

```cpp
// message_list_model.h — additive changes only
enum Roles : std::uint16_t {
  IdRole = Qt::UserRole + 1,
  RoleRole,
  TextRole,
  StatusRole,
  CreatedAtRole,
  ModelNameRole,
  ContentBlocksRole,  // NEW: QVariantList of holonight_rendering::ContentBlock
};
```

```cpp
struct Row {
  QString id;
  QString role;
  QString text;
  QString status;
  QDateTime created_at;
  QString model_name;
  // Deliberately QVariantList, not std::vector<holonight_rendering::ContentBlock> — see §7,
  // "why Row's cache field is typed QVariantList". `has_value` distinguishes "not computed yet"
  // from "computed, happens to be empty" (an empty message legitimately produces []).
  mutable std::optional<QVariantList> cached_content_blocks;
};
```

```cpp
// message_list_model.cpp
QVariant MessageListModel::data(const QModelIndex& index, int role) const {
  ...
  case ContentBlocksRole: {
    if (row.status != QStringLiteral("complete") && row.status != QStringLiteral("error")) {
      return QVariantList{};  // REQ-F-008: no blocks while pending/streaming, not an error
    }
    if (!row.cached_content_blocks.has_value()) {
      const std::vector<holonight_rendering::ContentBlock> blocks = parser_.parse(row.text);
      QVariantList variants;
      variants.reserve(static_cast<int>(blocks.size()));
      for (const auto& block : blocks) variants.append(QVariant::fromValue(block));
      row.cached_content_blocks = std::move(variants);  // row must be non-const-ref; see below
    }
    return *row.cached_content_blocks;
  }
  ...
}
```

Two additions to member data: `holonight_rendering::MessageContentParser parser_;` (private,
default-constructed, header-invisible — see §7), and `ContentBlocksRole` added to the role list
passed to `roleNames()` (`"contentBlocks"`) and to the `dataChanged()` call inside
`updateLastMessage()`.

Cache invalidation is free: `updateLastMessage()` already does `rows_[lastRow] = toRow(message)`
— a *whole-Row* value replacement, not a field-by-field mutation — so `cached_content_blocks`
reverts to `std::nullopt` on every call automatically, including the transition into `complete`
and any hypothetical future retry that resets a completed row back to `pending`. No explicit
invalidation code is needed; this falls out of code that already exists.

### 4.6 `MessageBubble.qml` — dual-branch restructure

```qml
Item {
    id: root
    // ... existing properties ...
    required property var contentBlocks: []   // NEW — bound to model.contentBlocks

    readonly property bool isBlockRendering: messageStatus === "complete" || messageStatus === "error"

    // ... existing RowLayout / HnSurfaceFrame / header unchanged ...

    ColumnLayout {
        // replaces the single `Text { textFormat: Text.MarkdownText }` from the old file

        Text {
            Layout.fillWidth: true
            visible: !root.isBlockRendering
            text: root.messageText
            textFormat: Text.MarkdownText
            wrapMode: Text.Wrap
            color: HoloniightPalette.textPrimary
            onLinkActivated: link => Qt.openUrlExternally(link)
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: root.isBlockRendering
            spacing: HoloniightPalette.controlPadding

            Repeater {
                model: root.contentBlocks
                delegate: Loader {
                    Layout.fillWidth: true
                    required property var modelData
                    sourceComponent: modelData.type === ContentBlockType.Code ? codeDelegate : markdownDelegate

                    Component { id: markdownDelegate; MarkdownBlock { block: modelData } }
                    Component { id: codeDelegate; HnCodeBlock { codeBlock: modelData } }
                }
            }
        }
    }
}
```

`MessageList.qml`'s delegate gains one line, mirroring the existing role-binding pattern exactly:

```qml
required property var contentBlocks
...
MessageBubble {
    ...
    contentBlocks: messageDelegate.contentBlocks
}
```

### 4.7 `MarkdownBlock.qml` (new, `qml/shared/`)

```qml
import QtQuick
import Holonight
import HolonightChat

Text {
    id: root
    required property contentBlock block

    text: block.text
    textFormat: Text.MarkdownText
    wrapMode: Text.Wrap
    color: HoloniightPalette.textPrimary
    onLinkActivated: link => Qt.openUrlExternally(link)
}
```

### 4.8 `HnCodeBlock.qml` (new, `qml/shared/`)

```qml
import QtQuick
import QtQuick.Layouts
import Holonight
import HolonightChat

ColumnLayout {
    id: root
    required property contentBlock codeBlock

    spacing: 0

    property CodeHighlighter highlighter: CodeHighlighter {
        id: highlighterInstance
        language: root.codeBlock.language
        Component.onCompleted: attachTo(body.textDocument)
    }

    Connections {
        target: HoloniightPalette
        function onPaletteChanged(): void { highlighterInstance.refreshTheme() }
    }

    // --- header: language label + copy button ---
    RowLayout {
        Layout.fillWidth: true
        Text {
            text: highlighterInstance.highlightingActive ? root.codeBlock.language : qsTr("plain text")
            color: HoloniightPalette.textSecondary
            font.family: "monospace"
        }
        Item { Layout.fillWidth: true }
        Button {  // Holonight's Button.qml
            id: copyButton
            text: copied ? qsTr("Copied") : qsTr("Copy")
            property bool copied: false
            onClicked: {
                body.selectAll()
                body.copy()
                body.deselect()
                copied = true
                resetTimer.restart()
            }
            Timer { id: resetTimer; interval: 1500; onTriggered: copyButton.copied = false }
        }
    }

    // --- body: plain-text, monospace, horizontally scrolling, selectable ---
    Flickable {
        Layout.fillWidth: true
        Layout.preferredHeight: body.implicitHeight
        contentWidth: body.implicitWidth
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Rectangle {
            width: Math.max(parent.width, body.implicitWidth)
            height: body.implicitHeight
            color: Qt.darker(HoloniightPalette.surface, 1.15)  // "slightly darker" — REQ-F-006

            TextEdit {
                id: body
                text: root.codeBlock.text
                textFormat: TextEdit.PlainText   // REQ-F-006/REQ-NF-002: never rich text
                wrapMode: TextEdit.NoWrap
                readOnly: true
                selectByMouse: true
                font.family: "monospace"
                color: HoloniightPalette.textPrimary
            }
        }
    }
}
```

Notes:
- `textFormat: TextEdit.PlainText` is what makes REQ-NF-002 hold structurally, not incidentally:
  Qt's `TextEdit` never interprets `<`, `&`, or Markdown syntax as anything but literal characters
  in this mode, regardless of content.
- `body.copy()` copies exactly the code text (no header, no surrounding markdown) because `body`'s
  own `text` property is bound only to `codeBlock.text` — selecting-all-then-copying never risks
  picking up sibling QML items' text.
- `highlighter` is declared as a property holding a `CodeHighlighter` instance (rather than an
  anonymous inline child) purely so `Connections.onPaletteChanged` and `Component.onCompleted` can
  both address it by a stable id; this has no behavioral significance.

---

## 5. CMake / Module Wiring

### 5.1 `src/rendering/CMakeLists.txt` (new)

```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(MD4C REQUIRED IMPORTED_TARGET md4c)   # REQ-C-001
find_package(KF6SyntaxHighlighting REQUIRED)             # REQ-C-002

add_library(holonight_rendering STATIC
    include/holonight_rendering/content_block.h
    include/holonight_rendering/language_alias.h
    include/holonight_rendering/message_content_parser.h
    include/holonight_rendering/code_highlighter.h
    src/language_alias.cpp
    src/message_content_parser.cpp
    src/code_highlighter.cpp
)

set_target_properties(holonight_rendering PROPERTIES AUTOMOC ON)

target_include_directories(holonight_rendering PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Same qmltyperegistrar bare-filename #if __has_include(<code_highlighter.h>) gotcha documented in
# CLAUDE.md for holonight_application — the consuming target's generated registration .cpp needs
# this module's own include/ subdirectory on its path, not just the parent include/ root.
target_include_directories(holonight_rendering PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include/holonight_rendering
)

target_link_libraries(holonight_rendering PUBLIC
    holonight_domain
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    PkgConfig::MD4C
    KF6::SyntaxHighlighting
    HolonightQt::Config
)

# Runtime location of the installed .theme JSON files generated by holonight-qt's
# tools/theme-generator (REQ-C-007/REQ-C-009). Mirrors the existing HOLONIGHT_QML_IMPORT_PATH
# find_path() pattern in the root CMakeLists.txt.
find_path(HOLONIGHT_SYNTAX_THEME_DIR
    NAMES holonight-dark.theme
    HINTS "${HolonightQt_DIR}/../../.."
    PATH_SUFFIXES share/holonight/themes
    NO_DEFAULT_PATH
)
if(HOLONIGHT_SYNTAX_THEME_DIR)
    target_compile_definitions(holonight_rendering PRIVATE
        HOLONIGHT_SYNTAX_THEME_DIR="${HOLONIGHT_SYNTAX_THEME_DIR}"
    )
endif()
# Deliberately NOT REQUIRED, unlike HOLONIGHT_QML_IMPORT_PATH: an older installed holonight-qt
# without the theme generator must still let holonight-ai build (REQ-C-009's fallback path) —
# CodeHighlighter checks the #define's presence and existence on disk at runtime, not at configure
# time.

target_compile_features(holonight_rendering PUBLIC cxx_std_23)
```

### 5.2 Root `CMakeLists.txt`

```cmake
add_subdirectory(src/domain)
add_subdirectory(src/config)
add_subdirectory(src/rendering)     # NEW — before application, which links it
add_subdirectory(src/application)
add_subdirectory(src/providers)
add_subdirectory(src/persistence)
add_subdirectory(src/credentials)
add_subdirectory(src/platform)
add_subdirectory(apps/chat)
```

Also add `holonight_rendering` to the `ENABLE_COVERAGE` target loop, matching every other `src/*`
static library.

### 5.3 `src/application/CMakeLists.txt`

```cmake
target_link_libraries(holonight_application PUBLIC
    holonight_domain
    holonight_providers
    holonight_persistence
    holonight_config
    holonight_credentials
    Qt6::Core
    Qt6::Qml
)
target_link_libraries(holonight_application PRIVATE
    holonight_rendering   # NEW — REQ-C-005
)
```

**Why `PRIVATE` is not just a formality here:** `message_list_model.h` (the only holonight_application
header that touches rendering output) declares its cache field as `std::optional<QVariantList>`,
*not* `std::optional<std::vector<holonight_rendering::ContentBlock>>` — see §4.5. Only
`message_list_model.cpp` includes `<holonight_rendering/message_content_parser.h>` and
`<holonight_rendering/content_block.h>`. This means no public header of `holonight_application`
transitively requires `holonight_rendering`'s include directory, so `PRIVATE` linkage actually
achieves what it's meant to: `apps/chat`'s own `.cpp` files (which never include
`message_list_model.h`'s internals beyond the public role/enum surface) never need
`holonight_rendering` on their compile include path. CMake still propagates the *linked archive*
(not the *usage requirements*) to the final executable automatically for static-library
dependencies, so `CodeHighlighter`'s and `ContentBlock`'s object code still ends up inside
`holonight-chat`'s binary — `PRIVATE` only suppresses include-dir/compile-definition propagation,
never archive propagation for statically-linked targets.

### 5.4 `apps/chat/CMakeLists.txt` — metatype-merge extension

The existing extraction is duplicated for `holonight_rendering`, and both JSON outputs are merged:

```cmake
set_property(TARGET holonight_application PROPERTY INTERFACE_SOURCES "")
qt6_extract_metatypes(holonight_application OUTPUT_FILES _HOLONIGHT_APPLICATION_METATYPES)
set_property(TARGET holonight_application PROPERTY INTERFACE_SOURCES "")
set(_HOLONIGHT_APPLICATION_METATYPES_DEP "${_HOLONIGHT_APPLICATION_METATYPES}.gen")

# NEW: same extraction, same clear-twice dance, for holonight_rendering (ContentBlockTypeNs,
# ContentBlock, CodeHighlighter are all QML-visible types declared there — REQ-C-005/REQ-C-006).
set_property(TARGET holonight_rendering PROPERTY INTERFACE_SOURCES "")
qt6_extract_metatypes(holonight_rendering OUTPUT_FILES _HOLONIGHT_RENDERING_METATYPES)
set_property(TARGET holonight_rendering PROPERTY INTERFACE_SOURCES "")
set(_HOLONIGHT_RENDERING_METATYPES_DEP "${_HOLONIGHT_RENDERING_METATYPES}.gen")

set(_HOLONIGHT_CHAT_METATYPES "${CMAKE_CURRENT_BINARY_DIR}/meta_types/qt6holonight-chat_metatypes.json")
add_custom_command(
    OUTPUT "${_HOLONIGHT_CHAT_METATYPES}"
    DEPENDS
        ${_HOLONIGHT_APPLICATION_METATYPES_DEP}
        ${_HOLONIGHT_RENDERING_METATYPES_DEP}
        "${PROJECT_SOURCE_DIR}/cmake/combine-metatypes.cmake"
    COMMAND ${CMAKE_COMMAND}
        "-DINPUT_FILES=${_HOLONIGHT_APPLICATION_METATYPES}|${_HOLONIGHT_RENDERING_METATYPES}"
        "-DOUTPUT=${_HOLONIGHT_CHAT_METATYPES}"
        -P "${PROJECT_SOURCE_DIR}/cmake/combine-metatypes.cmake"
    COMMENT "Collecting HolonightChat C++ metatypes"
    VERBATIM
)
# ... rest (_qt_internal_assign_build_metatypes_files_and_properties, _qt_internal_qml_type_registration) unchanged
```

`cmake/combine-metatypes.cmake` already splits `INPUT_FILES` on `|` and concatenates each file's
JSON array contents (see its existing `string(REPLACE "|" ";" ...)` logic) — no change needed
there; REQ-C-006's "invoked for all relevant static-library targets" is satisfied by passing both
paths in the existing pipe-joined string.

**Why `holonight_rendering` needs its own clear-twice, not just `holonight_application`'s:**
`_qt_internal_qml_type_registration(holonight-chat)` walks the *entire* transitive link graph of
`holonight-chat` looking for foreign QML types via each linked target's `INTERFACE_SOURCES`
property — `holonight_rendering` is in that graph (via `holonight_application`) whether or not
anything links it `PUBLIC`. Leaving its `INTERFACE_SOURCES` populated after its own
`qt6_extract_metatypes()` call would let that automatic walk re-discover `CodeHighlighter`/
`ContentBlock` a second time on top of the manual merge above — the exact "Multiple C++ types
found" ODR class of warning CLAUDE.md's gotcha describes for `holonight_application`, now also
possible for `holonight_rendering`.

### 5.5 Dependency graph (link-time)

```
holonight-chat (exe)
 ├─ holonight_application (PRIVATE)
 │   ├─ holonight_rendering (PRIVATE)  ─┬─ holonight_domain
 │   │                                   ├─ HolonightQt::Config  (external)
 │   │                                   ├─ KF6::SyntaxHighlighting (external)
 │   │                                   ├─ PkgConfig::MD4C (external)
 │   │                                   └─ Qt6::Gui, Qt6::Qml (external)
 │   ├─ holonight_providers
 │   ├─ holonight_persistence
 │   ├─ holonight_config
 │   └─ holonight_credentials
 ├─ holonight_domain
 ├─ holonight_providers
 ├─ holonight_persistence
 ├─ holonight_credentials
 └─ holonight_platform
```

Acyclic; `holonight_rendering` has no outgoing edge to any of `holonight_application`,
`holonight_providers`, `holonight_persistence`, `holonight_credentials`, `holonight_platform`
(REQ-C-004).

---

## 6. Cross-repo work: `holonight-qt` theme generator

### 6.1 Location and build integration

New directory `holonight-qt/tools/theme-generator/`, added to `holonight-qt`'s existing
`add_subdirectory(tools)`:

```
tools/theme-generator/
  CMakeLists.txt
  main.cpp                  # loops the 10 ThemeSchemeKind values, writes one .theme file each
  text_style_mapping.h       # the single shared mapping function (REQ-C-008)
  text_style_mapping.cpp
  kate_theme_writer.h        # serializes {tokens, mapping} -> Kate theme.json text
  kate_theme_writer.cpp
```

```cmake
# tools/theme-generator/CMakeLists.txt
add_executable(holonight-theme-generator main.cpp text_style_mapping.cpp kate_theme_writer.cpp)
target_link_libraries(holonight-theme-generator PRIVATE holonight_palette holonight_config Qt6::Gui)
target_compile_features(holonight-theme-generator PRIVATE cxx_std_23)

set(_THEME_OUTPUT_DIR "${CMAKE_BINARY_DIR}/generated-themes")
add_custom_command(
    OUTPUT "${_THEME_OUTPUT_DIR}/.stamp"
    COMMAND holonight-theme-generator "${_THEME_OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E touch "${_THEME_OUTPUT_DIR}/.stamp"
    DEPENDS holonight-theme-generator
    COMMENT "Generating KSyntaxHighlighting theme JSON files from ColorTokens"
    VERBATIM
)
add_custom_target(generate-syntax-themes ALL DEPENDS "${_THEME_OUTPUT_DIR}/.stamp")

install(DIRECTORY "${_THEME_OUTPUT_DIR}/" DESTINATION "${CMAKE_INSTALL_DATADIR}/holonight/themes"
        FILES_MATCHING PATTERN "*.theme")
```

Generated at every build (`ALL`), not committed to version control: this keeps the 10 files
mechanically in sync with `palette.cpp`'s `tokensForScheme()` with zero chance of drift for anyone
building from source, at the cost of the one residual risk noted in §9 (a *prebuilt/packaged*
`holonight-qt` that predates a `ColorTokens` change).

### 6.2 Shared mapping function (REQ-C-008)

```cpp
// text_style_mapping.h
#pragma once
#include <KSyntaxHighlighting/Theme>
#include <holonight/palette.h>

namespace HolonightThemeGen {

// The ONE place TextStyle -> ColorTokens correspondence is defined. Called once per (style,
// scheme) pair for each of the 10 schemes — never duplicated per-scheme (REQ-C-008).
[[nodiscard]] QColor colorForTextStyle(KSyntaxHighlighting::Theme::TextStyle style,
                                        const Holonight::ColorTokens& tokens);

}  // namespace HolonightThemeGen
```

```cpp
// text_style_mapping.cpp — rationale documented per row, per REQ-C-008
QColor HolonightThemeGen::colorForTextStyle(KSyntaxHighlighting::Theme::TextStyle style,
                                             const Holonight::ColorTokens& t) {
  using TS = KSyntaxHighlighting::Theme;
  switch (style) {
    case TS::Normal:        return t.textPrimary;    // default code text
    case TS::Keyword:       return t.accentViolet;    // language keywords, distinct from flow
    case TS::Function:      return t.accentBlue;      // calls/definitions
    case TS::Variable:      return t.textSecondary;   // identifiers, slightly de-emphasized
    case TS::ControlFlow:   return t.primary;         // if/else/return — most prominent accent
    case TS::Operator:      return t.textSecondary;
    case TS::BuiltIn:       return t.accentCyan;
    case TS::Extension:     return t.accentCyan;       // Qt/boost-style extensions, group with BuiltIn
    case TS::Preprocessor:  return t.warning;          // directives, attention-grabbing
    case TS::Attribute:     return t.accentYellow;
    case TS::Char:          return t.success;          // group with String
    case TS::SpecialChar:   return t.accentYellow;      // escapes inside strings, stand out
    case TS::String:        return t.success;
    case TS::VerbatimString:return t.success;
    case TS::SpecialString: return t.warning;           // regex/etc., attention
    case TS::Import:        return t.accentViolet;      // group with Keyword
    case TS::DataType:      return t.accentBlue;        // types, group with Function
    case TS::DecVal:        return t.accentYellow;
    case TS::BaseN:         return t.accentYellow;
    case TS::Float:         return t.accentYellow;
    case TS::Constant:      return t.accentCyan;        // true/false/null, group with BuiltIn
    case TS::Comment:       return t.textMuted;
    case TS::Documentation: return t.textSecondary;     // doc comments, one step above plain
    case TS::Annotation:    return t.accentViolet;
    case TS::CommentVar:    return t.textSecondary;
    case TS::RegionMarker:  return t.textMuted;
    case TS::Information:   return t.primary;
    case TS::Warning:       return t.warning;
    case TS::Alert:         return t.warning;
    case TS::Error:         return t.error;
    case TS::Others:        return t.textPrimary;
  }
  return t.textPrimary;
}
```

`kate_theme_writer.cpp` calls this once per `TextStyle` enumerator (all 31 values) per scheme,
writes `"text-styles"` entries with `"text-color"` set to the returned `QColor`'s hex, and a
minimal `"editor-colors"` block (`BackgroundColor`, `TextSelection`, etc. mapped straightforwardly
to `surfaceElevated`/`primary`/etc.) present only because the Kate theme schema requires the key —
`SyntaxHighlighter` driving a `QTextDocument` from QML never actually reads `editor-colors`; the
QML side (`HnCodeBlock.qml`) draws its own background with `Qt.darker(HoloniightPalette.surface,
...)` (§4.8), independent of the theme file.

`main.cpp`:

```cpp
for (Holonight::ThemeSchemeKind scheme : kAllTenSchemes) {
  const Holonight::ColorTokens tokens = Holonight::tokensForScheme(scheme);
  const QString schemeId = Holonight::schemeIdForKind(scheme);  // e.g. "holonight-dark"
  writeKateTheme(outputDir + "/" + schemeId + ".theme", schemeId, tokens);
}
```

The 10 output file names (matching `Holonight::schemeIdForKind()`'s existing ids from
`theme_catalog.cpp`): `holonight-dark.theme`, `holonight-light.theme`, `holonight-mocha.theme`,
`holonight-latte.theme`, `tokyonight-storm.theme`, `tokyonight-day.theme`, `holonight-ember.theme`,
`holonight-sol.theme`, `holonight-cyber-d.theme`, `holonight-cyber-l.theme`. Each file's internal
`"metadata": {"name": "<scheme-id>"}` field matches its filename stem, since
`Repository::theme(name)` looks up by that internal name field, not by filename.

### 6.3 Installation path and discovery

Installed to `${CMAKE_INSTALL_DATADIR}/holonight/themes/` (i.e. `share/holonight/themes/` under
the `HolonightQt` install prefix) — a plain data directory, deliberately not nested under the QML
plugin's `lib/qt6/qml/HolonightQt/` tree, since these files are consumed by C++
(`KSyntaxHighlighting::Repository::addCustomSearchPath()`), not by the QML engine's module loader.
`holonight-ai`'s root `CMakeLists.txt` locates it via `find_path()` (§5.1), the same pattern
already used for `HOLONIGHT_QML_IMPORT_PATH`.

### 6.4 `CodeHighlighter` ↔ `HoloniightPalette` live-switch bridging

The SPEC's REQ-C-010 wording ("`CodeHighlighter` connects to the global
`HoloniightPalette::paletteChanged` signal") reads as a direct C++ connection. **Confirmed against
the actual `holonight-qt` source, this isn't possible as a same-binary C++ signal/slot connection**:
`qml/holoniightpalette.h`/`.cpp` are listed as `SOURCES` inside `qt_add_qml_module(holonight_qml
...)` — they compile into the `holonight_qml` QML plugin shared object, not into any target that
`install(EXPORT HolonightQtTargets ...)` exports. Only `holonight_palette` (`palette.h`,
`ColorTokens`/`tokensForScheme()`) and `holonight_config` (`config.h`, `theme_catalog.h`,
`ThemeConfig`/`schemeIdForKind()`) are exported as linkable `HolonightQt::Palette`/
`HolonightQt::Config` C++ targets with installed headers. There is no `holoniightpalette.h` for
`holonight-ai`'s C++ code to `#include`, and no way to `connect()` to its signal from outside the
QML engine that owns the singleton instance.

The design therefore splits the requirement across the QML/C++ boundary, using what the codebase
actually exposes on each side:

- **Detecting the change**: done in QML, where `HoloniightPalette` *is* reachable as an imported
  singleton. `HnCodeBlock.qml` (§4.8) holds `Connections { target: HoloniightPalette;
  function onPaletteChanged() { highlighterInstance.refreshTheme() } }`.
- **Resolving the new theme**: done in C++, using the one part of the puzzle that IS exported —
  `HolonightQt::Config`'s `Holonight::ThemeConfig::load()` re-reads the exact same on-disk config
  file `HoloniightPalette::reload()` itself reads to repopulate `tok_` (confirmed from
  `holoniightpalette.cpp`'s constructor: `theme_config_path_{Holonight::ThemeConfig::configFilePath()}`).
  `CodeHighlighter::refreshTheme()` calls `Holonight::ThemeConfig::load().resolvedThemeScheme()`
  then `Holonight::schemeIdForKind(scheme)` to get the exact string `HoloniightPalette` is
  simultaneously deriving from the same source of truth — the two never disagree.

This satisfies the requirement's actual behavior (every active `CodeHighlighter` re-applies its
theme, immediately, when the scheme changes, without re-parsing) using a slightly different wiring
path than the SPEC's literal phrasing implied, because the literal reading isn't buildable against
the real `holonight-qt` package surface. This is flagged again in §7 as a key decision.

---

## 7. Key Decisions and Rationale

1. **Parsing is lazy (on first `data(ContentBlocksRole)` read), not eager (inside
   `updateLastMessage()`).** Keeps `MessageContentParser` entirely out of
   `MessageListModel`'s mutation methods — `appendMessage()`/`updateLastMessage()`/`resetFrom()`
   stay parser-agnostic, and a scrolled-off-screen historical message that QML never re-queries
   never gets parsed at all. The cost is identical either way for the common case (QML asks for the
   role in the same event-loop turn the completion fires), so there's no latency downside.

2. **`Row.cached_content_blocks` is `std::optional<QVariantList>`, not
   `std::optional<std::vector<ContentBlock>>`.** This is what makes `PRIVATE` linkage of
   `holonight_rendering` into `holonight_application` (REQ-C-005) mean something rather than being
   a no-op CMake flag: `message_list_model.h` — a *public* header of `holonight_application` — never
   needs `#include <holonight_rendering/...>`. Only `message_list_model.cpp` does. No consumer of
   `holonight_application`'s public headers (including `apps/chat`, per REQ-C-004's "not the
   executable") needs `holonight_rendering`'s include directory on its compile line.

3. **`ContentBlock` is a `Q_GADGET`/`QML_VALUE_TYPE`, not a `QObject`.** Blocks are produced in
   batches on every message completion and are conceptually immutable, comparable values — the
   `QObject` alternative (parent-owned, one allocation + moc vtable per block, explicit lifetime
   tied to the cache) buys nothing here and complicates the "just return a `QVariantList`" model role
   contract. See §8 for the QObject-wrapper alternative and why it's kept as a fallback, not the
   primary design.

4. **Markdown-block content is a byte-slice of the original source, not a re-serialization of
   MD4C's decoded event stream.** MD4C's non-code callbacks (`enter_span`, `text` for prose) strip
   markdown syntax (bold markers, link brackets, etc.) as part of parsing — feeding *that* back into
   a second `Text.MarkdownText` pass would double-render or lose formatting entirely. Byte-offset
   slicing (§4.3) is the only approach that preserves exact markdown syntax through the round trip.

5. **`CodeHighlighter` resolves the active scheme via `HolonightQt::Config`, not via a direct
   `HoloniightPalette` C++ connection.** Not a preference — the latter isn't possible against the
   actual `holonight-qt` build (§6.4). The QML-mediated `Connections` block is the only viable
   trigger path; `ThemeConfig::load()` is the only viable *source-of-truth* read, since it happens
   to be the same file `HoloniightPalette` itself watches.

6. **`MessageBubble.qml`'s streaming/block switch uses `visible` toggling on two permanently
   -instantiated branches, not `Loader.sourceComponent` swapping.** A `Loader` tears down its old
   `item` and constructs the new one on `sourceComponent` change — guaranteed at least one frame
   where the delegate's content is a completely empty `Item`, which is exactly the "visible flicker
   or blank state" REQ-F-007 forbids. Keeping both the `Text` and the block `ColumnLayout`
   permanently alive (the block branch's `Repeater` simply has 0 delegates while `contentBlocks` is
   `[]`) means the swap is a single `visible` flip on already-laid-out items — no teardown, no gap.

7. **Synchronous, main-thread parsing — no worker thread.** REQ-NF-001 explicitly scopes the
   non-functional requirement to "no perceptible UI stall for typical message sizes" and explicitly
   defers any worker-thread optimization to a future cycle "only if a problem is observed." Adding a
   thread here would also reopen a real correctness question (marshaling `ContentBlock` values and
   the resulting cache write back onto the model's thread, with the attendant risk of a stale write
   landing after a newer `updateLastMessage()` has already replaced the row) that the SPEC doesn't
   ask this cycle to solve. Simplicity wins by explicit SPEC instruction, not by default.

8. **Theme JSON files are generated at every `holonight-qt` build (`ALL` target), not committed to
   version control.** Guarantees the 10 files can never drift from `tokensForScheme()` for anyone
   building from source, which is the drift risk REQ-C-007 exists to prevent in the first place.
   The residual risk (a stale *installed/packaged* `holonight-qt`) is unavoidable either way and is
   listed in §9.

---

## 8. Alternatives Considered and Rejected

- **`ContentBlock` as a `QObject`-derived, per-block wrapper (`QML_ELEMENT`, `QML_UNCREATABLE`),
  returned as `QList<QObject*>` from the role.** Rejected: adds one heap allocation + moc overhead
  per block on every completed message, needs explicit parenting/lifetime decisions for the cached
  list (who deletes the old list when the cache is replaced?), and buys nothing a `Q_GADGET` value
  type doesn't already give for free. Kept as the documented fallback if `QML_VALUE_TYPE`
  registration for a `std::variant`-backed gadget proves awkward during implementation — swapping
  it in only touches `content_block.h`/`.cpp` and the `data(ContentBlocksRole)` case, not the QML
  delegate code (which already treats blocks as `modelData` with `.type`/`.text`/`.language`
  properties regardless of which C++ representation backs them).

- **Eager block computation inside `toRow()`/`updateLastMessage()`.** Rejected: forces
  `MessageListModel`'s mutation path to depend on `MessageContentParser` even when nothing has
  asked for the blocks yet (e.g., a long conversation scrolled away from), and doesn't change when
  the "no visible hitch" moment actually happens in practice — QML re-reads the role in the same
  frame regardless.

- **Reconstructing Markdown blocks from MD4C's decoded event stream (spans/text) instead of raw
  source-byte slicing.** Rejected outright, not just as a style preference: this loses the original
  markdown syntax (bold/italic markers, link brackets, list markers), which `Text.MarkdownText`
  needs verbatim to render the block correctly the second time. See Key Decision 4.

- **`CodeHighlighter` obtaining the active scheme via a `QQmlEngine::singletonInstance<QObject*>()`
  lookup at `componentComplete()` time, connecting to it directly in C++.** Considered as the
  "more literal" reading of REQ-C-010. Rejected: `HoloniightPalette`'s type isn't registered under
  any C++-visible name outside the `holonight_qml` plugin binary itself (no public header), so even
  though the *singleton instance* is technically reachable at runtime via the QML engine's internal
  registry, there is no public C++ type to `qobject_cast<>` it to, and connecting to a signal by
  runtime string lookup (`QMetaObject::connect` by signal index) is exactly the kind of fragile,
  header-avoiding trick this design otherwise tries not to need. The QML-`Connections` bridge (§6.4)
  achieves the same behavior with a type-safe, one-line QML binding instead.

- **Adding a new `currentThemeId`/`currentScheme` `Q_PROPERTY` to `HoloniightPalette` in
  `holonight-qt`, purely to give `CodeHighlighter` something to query.** Considered, since it would
  make REQ-C-010's literal wording buildable. Rejected for this cycle: it's a `holonight-qt` public
  API change to a shared, cross-consumer singleton, motivated by a single downstream consumer's
  convenience, when the already-exported `HolonightQt::Config` package gives the identical
  information (same source file, same resolution logic) with zero API surface change. Worth
  revisiting if a second consumer ever needs the same thing.

- **Worker-thread parsing (`QtConcurrent::run` + a `Future`-based model update).** Rejected per Key
  Decision 7 — explicitly out of scope per REQ-NF-001's own wording.

---

## 9. Known Risks

1. **MD4C fence-boundary byte-offset recovery (§4.3) is the single hardest piece of new code in
   this cycle and is under-specified by the SPEC's black-box acceptance criteria.** Concrete edge
   cases that need dedicated unit tests before this is trusted: a fenced block at the very start or
   end of the message text; two fenced blocks with zero prose between them; a fence using `~~~`
   instead of backticks; a fence nested inside a blockquote (MD4C reports blockquote content as
   normal block events — verify the offset math doesn't get confused by the `>` prefix characters);
   a message that is *only* a code fence with no surrounding prose (must not spuriously emit an
   empty leading/trailing `Markdown` block per REQ-F-002's "empty message -> empty list" spirit).

2. **Re-parsing cost scales with conversation length only in the sense that every individual
   completed message re-parses in full the first time it's queried after completion** — there is no
   caching across message boundaries and no incremental re-parse, which is correct per REQ-F-002/
   REQ-NF-001's scope, but means a very long single message (large generated code dump) pays MD4C's
   full parse cost once. REQ-NF-001 explicitly defers optimizing this until it's observed as a real
   problem; flagged here so it isn't rediscovered as a surprise.

3. **KSyntaxHighlighting Kate theme JSON schema fragility.** The `.theme` file format is
   KSyntaxHighlighting/Kate-internal and not covered by a stable public spec beyond its own source
   and example files; a malformed field name (e.g. wrong casing on `"text-color"` vs. `"textColor"`)
   fails silently — `Repository::theme()` simply won't find/parse the file correctly, and
   `CodeHighlighter` degrades to "no custom theme" (per its own designed fallback), which could mask
   a generator bug as "themes just don't look distinct from each other" rather than a hard failure.
   Mitigation: the generator should be validated against at least one real KSyntaxHighlighting theme
   file's exact JSON shape (e.g. its own bundled `default.theme`) byte-for-byte structurally before
   trusting the 10 generated files.

4. **holonight-qt/holonight-ai repo version skew if the theme generator isn't rebuilt.** Since
   themes are generated at `holonight-qt` build time and not committed, an `holonight-ai` build
   linked against a *pre-existing, already-installed* `holonight-qt` prefix that predates this
   cycle's generator addition will simply not find `HOLONIGHT_SYNTAX_THEME_DIR` (the `find_path()`
   in §5.1 is intentionally non-`REQUIRED`) — `CodeHighlighter` falls back to no custom theme, which
   is a silent, low-severity degradation (code blocks render, just with KSyntaxHighlighting's
   built-in default theme instead of a HoloNight-matched one) rather than a build failure. Anyone
   hitting this needs to rebuild/reinstall the sibling `holonight-qt` checkout via
   `task build:qt-dependency`.

5. **`QML_VALUE_TYPE` registration for a `Q_GADGET` wrapping a `std::variant` member is a less
   common Qt6 QML pattern than `QObject`-based registration** and its exact interaction with
   `qt6_extract_metatypes()` / `_qt_internal_qml_type_registration()` (REQ-C-006) hasn't been
   proven inside this specific repo's build (unlike `QML_ELEMENT` on `QObject` subclasses, which
   this codebase already does five times over per CLAUDE.md's module history). If it doesn't
   extract/register cleanly, the `QObject`-wrapper alternative in §8 is the documented fallback —
   budget time in the task-breakdown stage for this to need one iteration.
