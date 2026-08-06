# SDD Tasks — advanced-markdown-rendering

## Build System & CMake Configuration

- [x] T-001: Create holonight_rendering module structure
  - REQs: REQ-C-003
  - Check: `src/rendering/CMakeLists.txt`, `src/rendering/include/holonight_rendering/`, and `src/rendering/src/` directories exist; CMakeLists.txt declares `add_library(holonight_rendering STATIC)` and `target_include_directories(PUBLIC include)`.

- [x] T-002: Configure MD4C in holonight_rendering CMakeLists
  - REQs: REQ-C-001
  - Check: `src/rendering/CMakeLists.txt` contains `pkg_check_modules(md4c REQUIRED md4c IMPORTED_TARGET)` and `target_link_libraries(holonight_rendering ... PkgConfig::md4c)` with no CMake configuration errors.

- [x] T-003: Configure KF6SyntaxHighlighting in holonight_rendering CMakeLists
  - REQs: REQ-C-002
  - Check: `src/rendering/CMakeLists.txt` contains `find_package(KF6SyntaxHighlighting REQUIRED)` and `target_link_libraries(holonight_rendering ... KF6::SyntaxHighlighting)` with no CMake configuration errors.

- [x] T-004: Add holonight_rendering to root CMakeLists.txt
  - REQs: REQ-C-003, REQ-C-004
  - Check: Root `CMakeLists.txt` contains `add_subdirectory(src/rendering)` placed before `add_subdirectory(src/application)`; holonight_rendering is included in the ENABLE_COVERAGE target loop; `task configure` succeeds.

- [x] T-005: Link holonight_application to holonight_rendering
  - REQs: REQ-C-004, REQ-C-005
  - Check: `src/application/CMakeLists.txt` contains `target_link_libraries(holonight_application PRIVATE holonight_rendering)`; `task configure` and `task build` succeed without dependency errors.

- [x] T-006: Extend metatype-merge machinery in apps/chat/CMakeLists.txt
  - REQs: REQ-C-005, REQ-C-006
  - Check: `apps/chat/CMakeLists.txt` calls `qt6_extract_metatypes()` for both `holonight_application` and `holonight_rendering`, clears `INTERFACE_SOURCES` before and after each extract call, and passes both metatypes JSON paths pipe-joined to `combine-metatypes.cmake`; generated `.qmltypes` contains ContentBlock types without "Multiple C++ types found" warnings.

## Domain Model Types

- [x] T-007: Define ContentBlockTypeNs enum and ContentBlock Q_GADGET
  - REQs: REQ-F-001
  - Check: `src/rendering/include/holonight_rendering/content_block.h` defines `ContentBlockTypeNs` with enum `Markdown` and `Code` values (QML_NAMED_ELEMENT); `ContentBlock` wraps `std::variant<MarkdownPayload, CodePayload>` (QML_VALUE_TYPE), with public accessors `type()`, `text()`, `language()`, and Q_PROPERTY bindings; types compile and register to QML without errors.

- [x] T-008: Implement language alias normalization
  - REQs: REQ-F-003
  - Check: `src/rendering/src/language_alias.cpp` implements `LanguageAlias::normalize(QString)` that maps cpp/c++/cxx→C++, js/javascript→JavaScript, ts/typescript→TypeScript, sh/bash/shell→Bash, py/python→Python, qml→QML, passes through unrecognized trimmed strings, and handles empty input correctly; case-insensitivity verified by unit test or manual check.

## Parser Implementation

- [x] T-009: Implement MessageContentParser with MD4C
  - REQs: REQ-F-002
  - Check: `src/rendering/src/message_content_parser.cpp` implements `parse(QString)` using MD4C's callback API (`md_parse`, `MD_PARSER` vtable) to extract triple-backtick/tilde fenced code blocks into `ContentBlock::Code` instances and group other Markdown into `ContentBlock::Markdown` instances; empty input returns empty vector; malformed input returns single Markdown block with original text; indented code remains in Markdown blocks; parsing completes without crash or memory leak on a typical 5KB message.

## Syntax Highlighting Foundation

- [x] T-010: Implement CodeHighlighter class with KSyntaxHighlighting
  - REQs: REQ-F-009, REQ-NF-003
  - Check: `src/rendering/src/code_highlighter.cpp` defines `CodeHighlighter` as QObject (QML_ELEMENT) subclassing `KSyntaxHighlighting::SyntaxHighlighter`, with Q_PROPERTY `language` (READ/WRITE/NOTIFY) and `highlightingActive` (READ/NOTIFY), Q_INVOKABLE `attachTo(QQuickTextDocument*)`, and gracefully applies highlighting for known languages or no highlighting for unknown languages without crash or error logs.

- [x] T-011: Implement Repository initialization and theme loading in CodeHighlighter
  - REQs: REQ-C-009, REQ-F-011
  - Check: `CodeHighlighter::CodeHighlighter()` initializes `KSyntaxHighlighting::Repository` member and calls `repository.addCustomSearchPath()` with holonight-qt theme directory path (set via compile-time HOLONIGHT_SYNTAX_THEME_DIR CMake variable); falls back gracefully (no crash, no error log, just no highlighting) if theme directory or specific theme is not found at runtime.

## MessageListModel Integration

- [x] T-012: Add ContentBlocksRole enum to MessageListModel
  - REQs: REQ-F-008
  - Check: `src/application/src/message_list_model.cpp` defines `ContentBlocksRole` enum value; adds it to `roleNames()` return map with key `"contentBlocks"`; includes it in `dataChanged()` role list emitted by `updateLastMessage()`.

- [x] T-013: Implement ContentBlocksRole data() with lazy parsing and caching
  - REQs: REQ-F-008
  - Check: `MessageListModel::data(index, ContentBlocksRole)` returns empty `QVariantList{}` for messages with status `pending` or `streaming`; for `complete` or `error` status, parses text via `MessageContentParser::parse()` on first read, caches result in `Row::cached_content_blocks` member (std::optional<QVariantList>), returns cached result on subsequent reads without re-parsing; `updateLastMessage()` does not trigger re-parsing.

## QML Components for Content Blocks

- [x] T-014: Create MarkdownBlock.qml component
  - REQs: REQ-F-010
  - Check: `qml/shared/MarkdownBlock.qml` exists with `required property contentBlock block`; renders block text via single `Text { textFormat: Text.MarkdownText }` element bound to `block.text`.

- [x] T-015: Create HnCodeBlock.qml with header, copy button, and full text rendering
  - REQs: REQ-F-005, REQ-F-006, REQ-NF-002
  - Check: `qml/shared/HnCodeBlock.qml` exists with `required property contentBlock codeBlock`; header shows normalized language label (via `highlighterInstance.highlightingActive ? codeBlock.language : qsTr("plain text")`); copy button copies code to clipboard via `body.selectAll()/body.copy()/body.deselect()` with visual feedback (1500ms "Copied" timer); body is `Flickable` wrapping `Rectangle` wrapping `TextEdit { textFormat: TextEdit.PlainText, wrapMode: TextEdit.NoWrap, readOnly: true, selectByMouse: true, font.family: "monospace" }`; owns `CodeHighlighter` instance (attached via `Component.onCompleted: highlighterInstance.attachTo(body.textDocument)`); includes `Connections { target: HoloniightPalette; function onPaletteChanged() { highlighterInstance.refreshTheme() } }` for live theme switching.

## Message Rendering Switch on Completion

- [x] T-016: Implement MessageBubble rendering switch from streaming to block-based
  - REQs: REQ-F-010, REQ-F-007
  - Check: `qml/shared/MessageBubble.qml` retains existing single `Text { textFormat: Text.MarkdownText }` and adds new `ColumnLayout` with `Repeater` over `contentBlocks`; both are permanently instantiated, toggled only via `visible` property based on message status; when status becomes `complete` or `error`, single-text renderer becomes invisible and block renderer becomes visible; when status is `pending` or `streaming`, single-text renderer is visible; transition shows no flicker or blank state.

- [x] T-017: Add contentBlocks binding in MessageList delegate
  - REQs: REQ-F-010
  - Check: `qml/workspace/MessageList.qml` message delegate binds `contentBlocks: messageDelegate.contentBlocks` from MessageListModel's ContentBlocksRole and passes it to `MessageBubble` component; binding propagates correctly.

## Cross-Repository Theme Generation (holonight-qt)

- [x] T-018: Create theme generator scaffold in holonight-qt repository
  - REQs: REQ-C-007
  - Check: `holonight-qt/tools/theme-generator/` directory exists with `main.cpp`, `CMakeLists.txt`, and private header files; CMakeLists.txt defines `add_executable(holonight-theme-generator main.cpp ...)` linked against holonight_palette, holonight_config, Qt6::Gui; executable builds without errors.

- [x] T-019: Implement text style to color token mapping function
  - REQs: REQ-C-008
  - Check: `holonight-qt/tools/theme-generator/text_style_mapping.cpp` implements single shared function `HolonightThemeGen::colorForTextStyle(KSyntaxHighlighting::TextStyle, const Holonight::ColorTokens&) → QColor` with documented 31-case switch statement mapping all KSyntaxHighlighting TextStyle enum values to ColorToken fields (e.g., Keyword→accentViolet, String→success, Comment→textMuted, Error→error); all 10 theme files generated by calling this same function, not per-scheme variants.

- [x] T-020: Implement Kate theme JSON writer
  - REQs: REQ-C-007
  - Check: `holonight-qt/tools/theme-generator/kate_theme_writer.cpp` implements `HolonightThemeGen::writeThemeJson(const QString& schemeId, const Holonight::ColorTokens&, const QString& outputPath)` that outputs valid Kate-format theme JSON file with correct `"metadata": {"name": ...}` field matching scheme ID and valid `"text-styles"` object mapping TextStyle names to color and style properties; output matches KSyntaxHighlighting schema.

- [x] T-021: Integrate theme generator into holonight-qt build system
  - REQs: REQ-C-007
  - Note: verified end-to-end at runtime (`KSyntaxHighlighting::Repository::theme(schemeId).isValid()` checked against a standalone probe binary for all 10 generated `.theme` files, not just "the JSON is well-formed"). This caught a real bug: `Repository::addCustomSearchPath()` does not scan the given directory directly for `*.theme` files — it scans a `themes/` subdirectory beneath it (mirrors the XDG `org.kde.syntax-highlighting/themes` convention, undocumented in the header comment's plain reading). holonight-qt already installs to `share/holonight/themes/*.theme`, so the fix was entirely on the holonight-ai side: `src/rendering/CMakeLists.txt`'s `find_path()` now searches `NAMES themes/holonight-dark.theme` with `PATH_SUFFIXES share/holonight` (the parent of `themes/`), not `share/holonight/themes` itself.
  - Check: `holonight-qt/CMakeLists.txt` defines `add_custom_target(generate-syntax-themes ALL ...)` that runs `holonight-theme-generator` for all 10 ThemeSchemeKind values, writes 10 theme JSON files to build directory with filenames matching `Holonight::schemeIdForKind()` IDs (holonight-dark.theme, holonight-light.theme, holonight-mocha.theme, holonight-latte.theme, tokyonight-storm.theme, tokyonight-day.theme, holonight-ember.theme, holonight-sol.theme, holonight-cyber-d.theme, holonight-cyber-l.theme); `install(DIRECTORY ...)` copies themes to `${CMAKE_INSTALL_DATADIR}/holonight/themes/`; CMake target runs at every build and themes are regenerated on color token changes.

## Live Theme Switching Integration

- [x] T-022: Wire up HoloniightPalette::paletteChanged signal in CodeHighlighter
  - REQs: REQ-C-010, REQ-F-011
  - Check: `CodeHighlighter::CodeHighlighter()` connects `HoloniightPalette::paletteChanged` global signal to private `onPaletteChanged()` slot; signal is received and slot is invoked when palette changes.
  - Note: per DESIGN.md §6.4/§7 Decision 5, a direct C++ connection to `HoloniightPalette::paletteChanged` isn't buildable — that signal lives only inside the `holonight_qml` plugin shared object, with no exported header. Satisfied instead via the QML-mediated path: `HnCodeBlock.qml`'s `Connections` block (T-024) detects the change and calls `CodeHighlighter::refreshTheme()` (T-023), which independently resolves the new scheme through the exported `HolonightQt::Config` API — the same source of truth `HoloniightPalette` itself reads.

- [x] T-023: Implement CodeHighlighter::refreshTheme() with theme reload
  - REQs: REQ-F-011
  - Check: `CodeHighlighter::refreshTheme()` queries current active color scheme via `Holonight::ThemeConfig::load()` or equivalent, maps it to corresponding KSyntaxHighlighting theme ID (e.g., HoloNightDark → holonight-dark), loads theme from Repository via `repository.theme(themeId)`, and re-applies highlighting to attached QTextDocument without re-parsing code; `highlightingActive` property updates if scheme changed.

- [x] T-024: Add palette change handling in HnCodeBlock.qml
  - REQs: REQ-F-011
  - Check: `HnCodeBlock.qml` includes `Connections { target: HoloniightPalette; function onPaletteChanged() { highlighterInstance.refreshTheme() } }` block; when user switches color scheme in app settings, code block colors update instantly with no delay, flicker, text reflow, or scroll-position loss.

## End-to-End Verification & Testing

- [x] T-025: End-to-end markdown parsing and block rendering smoke test
  - REQs: REQ-F-001, REQ-F-002, REQ-NF-001, REQ-NF-002, REQ-NF-003
  - Check: Manually send a complete assistant message containing prose, lists, headings, and multiple fenced code blocks with varied languages (cpp, python, javascript, bash); verify message parses without perceptible UI stall, all block types render correctly without layout jumps, HTML/Markdown sequences inside code blocks display literally (not rendered), and no crash or error logs appear.

- [x] T-026: Verify streaming-to-complete transition without flicker
  - REQs: REQ-F-007, REQ-F-010
  - Check: Manually stream a multi-paragraph response with code blocks from start to finish (use a long-running Ollama model or simulated slow stream); observe message uses single-text rendering during streaming, switches to block-based rendering on completion, and transition is seamless with no visible flicker, blank state, or layout jump.

- [x] T-027: Verify code copy functionality with special characters
  - REQs: REQ-F-005
  - Check: Manually click copy button on code blocks containing special characters (`<div>`, `&nbsp;`, `**bold**`, `[link](url)`, quotes, backslashes, non-ASCII UTF-8); paste copied text into text editor and verify it matches code exactly with no HTML entity encoding, Markdown interpretation, or character corruption.

- [x] T-028: Verify syntax highlighting with live theme switching
  - REQs: REQ-F-009, REQ-F-011, REQ-C-010
  - Check: Manually display message with syntax-highlighted code blocks in current color scheme, then switch active scheme via Settings (e.g., HoloNightDark → HoloNightLight); verify all code blocks update their highlight colors instantly (within 1 frame), with no visible delay, flicker, text reflow, scroll-position loss, or loss of code selection.

- [x] T-029: Verify unknown language fallback and graceful degradation
  - REQs: REQ-F-004, REQ-NF-003
  - Check: Manually create code block with language "unknownlang" or no language (empty backtick fence); verify block displays "plain text" label, renders code literally with no highlighting or attempt to highlight, application does not crash or error-log, and performance is not degraded compared to known-language blocks.
