# Advanced Markdown Rendering for Chat Messages — SPEC.md

## Overview

Today, assistant chat messages render via a single QML `Text { textFormat: Text.MarkdownText }` element, providing limited affordances for fenced code blocks. This specification defines the replacement architecture: a block-based renderer that splits finished messages into a sequence of typed content blocks (`Markdown` and `Code`) and renders each with dedicated QML components. Fenced code blocks gain syntax highlighting via KSyntaxHighlighting, thematically integrated with the app's active HoloNight color scheme, and live re-highlight when schemes switch. Streaming messages retain today's single-text rendering until completion.

---

## Non-Goals

The following capabilities are **explicitly out of scope** for this cycle and must not be implemented:

- `Table`, `Image`, and `ToolCall` content block types — only `Markdown` and `Code` blocks exist this cycle.
- Streaming incremental re-parsing: messages continue using single-text rendering while streaming (`pending` or `streaming` status); parsed blocks appear only when the message reaches `complete` or `error` status.
- Line-number gutters, wrap-toggle controls, and "capped preview for very long blocks" behavior.
- Save-to-file, "Open in editor", "Insert into project", and "Run" actions on code blocks.
- Cross-block text selection (selecting from one block through another). Per-block selection and per-block copy actions are sufficient.
- User messages (`qml/shared/UserMessageCard.qml`) — they remain unchanged, using `Text.MarkdownText` throughout.
- Rendering `Table` markdown syntax, images, or non-code block types — inherited behavior from Qt's native `Text.MarkdownText` is acceptable as-is.

---

## Functional Requirements

### REQ-F-001: Message content block model

**Statement:** The system shall define a `ContentBlock` model with exactly two block types: `Markdown` and `Code`.

**Acceptance criteria:**
- `ContentBlock` is a C++ type with a discriminated union representing either a `Markdown` block (containing raw Markdown prose) or a `Code` block (containing the code content and fence info-string language).
- A `Markdown` block's content is retrievable as a QString.
- A `Code` block's content is retrievable as a QString (the raw code).
- A `Code` block's language info-string is retrievable as a QString.

---

### REQ-F-002: MD4C-based parsing of assistant messages

**Statement:** The system shall parse a finished (non-streaming) assistant message's Markdown text into an ordered list of `ContentBlock`s using the MD4C C library.

**Acceptance criteria:**
- Parsing invokes `md4c` (version 0.5.3 or compatible).
- Consecutive non-code Markdown (prose, lists, headings, blockquotes, etc.) is kept together in a single `Markdown` block.
- Each fenced code block (delimited by triple-backticks or triple-tildes) is extracted into its own `Code` block, preserving the fence's info-string language and the raw code content.
- Parsing does not occur during streaming (status `pending` or `streaming`); it occurs only after the message reaches status `complete` or `error`.
- An empty message produces an empty block list (not a single empty block).
- Parsing errors (malformed MD4C invocation) do not crash the application; the message falls back to single-text rendering (REQ-F-007).

---

### REQ-F-003: Language alias normalization

**Statement:** The system shall normalize common fence-language aliases to canonical display names before syntax highlighting and label generation.

**Acceptance criteria:**
- The following aliases are normalized: `cpp`, `c++`, `cxx` → `C++`; `js`, `javascript` → `JavaScript`; `ts`, `typescript` → `TypeScript`; `sh`, `bash`, `shell` → `Bash`; `py`, `python` → `Python`; `qml` → `QML`.
- Languages not in the normalization list are passed through as-is (e.g., `rust`, `go`, `java` remain unchanged).
- An empty or missing language info-string is treated as an unknown language (see REQ-F-004).
- The normalization is applied before passing the language to the `CodeHighlighter` (REQ-F-009).

---

### REQ-F-004: Unknown language fallback

**Statement:** If a `Code` block's language is unknown (not in KSyntaxHighlighting's definition list and not normalizable via REQ-F-003), then the system shall render the code block with a neutral "plain text" label and no syntax highlighting applied.

**Acceptance criteria:**
- A code block with language `"unknownlang"` displays a header showing "plain text" or equivalent neutral label.
- No highlighting is applied to that block's text.
- The application does not crash, log an error, or degrade performance when encountering an unknown language.
- A code block with no language info-string (empty string) is treated as unknown and displays the neutral label.

---

### REQ-F-005: HnCodeBlock QML component — header and copy button

**Statement:** The system shall provide an `HnCodeBlock` QML component that renders a `Code` block with a header displaying the normalized language label and a copy button.

**Acceptance criteria:**
- `HnCodeBlock` accepts a `codeBlock` property (a `ContentBlock` of type `Code`).
- The header displays a human-readable language name (from REQ-F-003 or the neutral label from REQ-F-004).
- A copy button is present in the header; clicking it copies only the code content (not surrounding Markdown or HTML) to the system clipboard.
- The copy action succeeds (does not crash or silently fail) for code containing special characters (`<`, `>`, `&`, quotes, etc.).
- After a successful copy, the button briefly indicates success (visual feedback; behavior is optional but encouraged).

---

### REQ-F-006: HnCodeBlock QML component — text rendering and scrolling

**Statement:** The system shall render the code body inside `HnCodeBlock` using plain-text rendering with horizontal scrolling for long lines and no line wrapping by default.

**Acceptance criteria:**
- The code text is rendered in a monospace font.
- HTML, markup, and special characters (`<`, `>`, `&`, etc.) inside the code are displayed literally, never interpreted as rich text or HTML entities.
- Lines that exceed the component's width trigger horizontal scrolling; no line wrapping occurs.
- Text inside the code block is selectable and can be copied using standard text selection.
- The code body's background color is slightly darker than the surrounding assistant message bubble.

---

### REQ-F-007: Streaming messages retain single-text rendering

**Statement:** While a message's status is `pending` or `streaming`, the system shall render it using a single QML `Text { textFormat: Text.MarkdownText }` element with no code blocks or syntax highlighting.

**Acceptance criteria:**
- A message with status `pending` or `streaming` displays the full accumulated text (including incomplete code blocks) as plain Markdown text.
- No `ContentBlocksRole` blocks are instantiated during streaming.
- When the message status transitions to `complete` or `error`, the message rendering switches to block-based rendering (REQ-F-010).
- The transition does not show a visible flicker or an intermediate blank/empty state.

---

### REQ-F-008: MessageListModel ContentBlocksRole and caching

**Statement:** The existing `MessageListModel` shall gain a `ContentBlocksRole` that provides the parsed block list only after a message reaches `complete` or `error` status.

**Acceptance criteria:**
- `ContentBlocksRole` is a new role on `MessageListModel` that returns a list of `ContentBlock` objects.
- The block list is computed by invoking `MessageContentParser` (the MD4C-backed parser defined in the `holonight_rendering` module) on the message's text only after the message's status becomes `complete` or `error`.
- The parsed block list is cached; calling `data(index, ContentBlocksRole)` multiple times for the same message does not recompute the list.
- Calling `updateLastMessage()` (the streaming hot path) does not recompute the block list.
- For a message with status `pending` or `streaming`, `ContentBlocksRole` returns an empty list (not an error).

---

### REQ-F-009: Syntax highlighting activation

**Statement:** The system shall apply syntax highlighting to each `Code` block according to its normalized language using KSyntaxHighlighting.

**Acceptance criteria:**
- Each `HnCodeBlock` instance instantiates a `CodeHighlighter` C++ object (subclass of `KSyntaxHighlighting::SyntaxHighlighter`).
- The `CodeHighlighter` is attached to the `QTextDocument` backing the `HnCodeBlock`'s text element.
- The highlighter applies highlighting rules for the code block's normalized language (from REQ-F-003).
- Highlighting updates synchronously after the text is loaded — no visible uncolored flash before colors apply.
- For unknown languages (REQ-F-004), the highlighter applies no highlighting (the text remains unstyled).

---

### REQ-F-010: MessageBubble rendering switch on completion

**Statement:** When a message's status transitions to `complete` or `error`, the system shall switch `MessageBubble.qml` from single-text rendering to block-based rendering.

**Acceptance criteria:**
- While status is `pending` or `streaming`, `MessageBubble` displays a single `Text { textFormat: Text.MarkdownText }`.
- When status becomes `complete` or `error`, `MessageBubble` instantiates a `Repeater` over the message's `ContentBlocksRole` blocks.
- The `Repeater` uses a delegate that renders `Markdown`-type blocks via a `MarkdownBlock` component (plain `Text { textFormat: Text.MarkdownText }`).
- The `Repeater` uses a delegate that renders `Code`-type blocks via an `HnCodeBlock` component (REQ-F-005 through REQ-F-006).
- Blocks are rendered in order.
- No rendering artifacts (layout jumps, overlaps, invisible text) occur during the switch.

---

### REQ-F-011: Syntax highlighting theme integration and live switching

**Statement:** The system shall load and apply syntax highlighting themes that match the app's active HoloNight color scheme, and re-apply the correct theme whenever the scheme changes.

**Acceptance criteria:**
- On startup, `CodeHighlighter` loads the KSyntaxHighlighting theme corresponding to the app's currently active color scheme (e.g., `HoloNightDark`, `HoloNightLight`, etc.).
- When `HoloniightPalette::paletteChanged` fires (the existing Qt signal from `holonight-qt`), all active `CodeHighlighter` instances are notified of the scheme change.
- All active `CodeHighlighter` instances re-apply the highlighting for the new scheme without re-parsing the code text.
- The theme change feels instant to the user — no visible delay, loading state, or unstyled intermediate frame.
- Switching schemes while viewing a message with code blocks updates the code block colors live, with no flash, reset, or re-rendering of other message elements.

---

## Non-Functional Requirements

### REQ-NF-001: Parsing must not block UI thread

**Statement:** The system shall not block the UI thread noticeably when parsing a typical chat response's Markdown text.

**Acceptance criteria:**
- Parsing a typical multi-paragraph chat response (with zero to a few code blocks) does not cause a perceptible UI stall or dropped frame when it runs on the main thread after a message completes.
- No numeric SLA is required; the qualitative check is that a developer manually completing a response of ordinary length observes no visible hitch.
- If profiling later shows parsing is a real bottleneck for unusually large responses, that is out of scope for this cycle (optimize incrementally only if a problem is observed, per the original design brief).

---

### REQ-NF-002: Code block text must never interpret HTML or markup

**Statement:** If a code block's text content includes HTML-like sequences or Markdown-like syntax, then the system shall render it as literal text, never interpreting or rendering it as rich text or markup.

**Acceptance criteria:**
- Code containing `<div>`, `&nbsp;`, `**bold**`, `[link](url)`, or other HTML/Markdown sequences displays exactly as written, without rendering or interpretation.
- No HTML entity decoding occurs (e.g., `&lt;` remains `&lt;`, not `<`).
- This is verifiable by a visual test: paste a code snippet containing HTML into a message, complete the message, and verify the code block displays the HTML literally.

---

### REQ-NF-003: Syntax highlighting graceful degradation

**Statement:** If a language's KSyntaxHighlighting definition is unavailable, then the system shall render the code block without highlighting (no crash, no error log, no performance degradation).

**Acceptance criteria:**
- Requesting a highlighter for a language with no KSyntaxHighlighting definition (e.g., a fictional `"mylang"`) does not crash the application.
- The code block renders correctly with no highlighting applied (plain unstyled text).
- No error messages are logged to the console (optional: a debug-level trace is acceptable).
- Syntax highlighting performance does not degrade when encountering unknown languages (i.e., the fallback is fast).

---

## Constraints

### REQ-C-001: MD4C library linkage via pkg-config

**Statement:** The system shall link the `md4c` C library (version 0.5.3 or compatible) via CMake's `pkg_check_modules()` mechanism.

**Acceptance criteria:**
- `holonight_rendering` module's `CMakeLists.txt` invokes `pkg_check_modules(md4c REQUIRED md4c)`.
- The CMake configuration succeeds if `md4c` is installed and discoverable via pkg-config.
- The CMake configuration fails with a clear error message if `md4c` is not found.
- The generated build system passes the correct include paths and linker flags for `md4c`.

---

### REQ-C-002: KF6SyntaxHighlighting linkage via CMake find_package

**Statement:** The system shall link the `KF6SyntaxHighlighting` library via CMake's `find_package()` mechanism (not pkg-config, as KF6SyntaxHighlighting provides no pkg-config file).

**Acceptance criteria:**
- `holonight_rendering` module's `CMakeLists.txt` invokes `find_package(KF6SyntaxHighlighting REQUIRED)`.
- The CMake configuration succeeds if `KF6SyntaxHighlighting` is installed and discoverable via CMake's `find_package` search paths.
- The CMake configuration fails with a clear error message if `KF6SyntaxHighlighting` is not found.
- The generated build system links against `KF6::SyntaxHighlighting`.

---

### REQ-C-003: New holonight_rendering static library module

**Statement:** The system shall introduce a new `holonight_rendering` static library module under `src/rendering/`, exporting `MessageContentParser` (MD4C-backed) and `CodeHighlighter` (KSyntaxHighlighting-backed) classes.

**Acceptance criteria:**
- A new directory `/src/rendering/` exists with `CMakeLists.txt`, `include/holonight_rendering/`, and `src/` subdirectories.
- `holonight_rendering` is declared as a CMake `add_library(holonight_rendering STATIC)` target.
- The module exports public headers via `target_include_directories(holonight_rendering PUBLIC include)`.
- `MessageContentParser` is declared in `include/holonight_rendering/message_content_parser.h` and implemented in `src/message_content_parser.cpp`.
- `CodeHighlighter` is declared in `include/holonight_rendering/code_highlighter.h` and implemented in `src/code_highlighter.cpp`.
- `holonight_rendering` links `md4c` (via pkg-config) and `KF6::SyntaxHighlighting` (via CMake find_package).
- The module's CMakeLists.txt is added to the root `CMakeLists.txt` via `add_subdirectory(src/rendering)`.

---

### REQ-C-004: Module dependency graph

**Statement:** The `holonight_rendering` module shall depend on `holonight_domain` (for type definitions) and shall not depend on `holonight_application`, `holonight_providers`, `holonight_persistence`, `holonight_credentials`, or `holonight_platform`.

**Acceptance criteria:**
- `holonight_rendering`'s `CMakeLists.txt` links `holonight_domain` as a private or public dependency.
- No other `src/*` modules are linked as dependencies of `holonight_rendering`.
- `holonight_application` (and only `holonight_application`, not the `holonight-chat` executable) links `holonight_rendering` as a dependency to enable `MessageListModel`'s `ContentBlocksRole`.
- The dependency graph is acyclic (no circular dependencies).

---

### REQ-C-005: MessageListModel export and link requirement

**Statement:** `holonight_application` shall add `holonight_rendering` as a dependency and re-export `ContentBlock` types for QML use via the existing metatype-merge machinery.

**Acceptance criteria:**
- `src/application/CMakeLists.txt` invokes `target_link_libraries(holonight_application PRIVATE holonight_rendering)`.
- `holonight_rendering`'s types (`ContentBlock` enum, `Code`/`Markdown` variants) are made available to QML via `Q_ENUM` or equivalent macros.
- The `apps/chat/CMakeLists.txt` metatype-extraction step (already present, per CLAUDE.md) is extended to also extract metatypes from `holonight_rendering` (in addition to the existing extraction from `holonight_application`).
- The generated `.qmltypes` file correctly lists `ContentBlock` types without "Multiple C++ types found" ODR warnings.

---

### REQ-C-006: Qt metatype-merge machinery extension

**Statement:** The `apps/chat/CMakeLists.txt` shall extend the existing metatype-merge and type-registration machinery to include types from `holonight_rendering`.

**Acceptance criteria:**
- The existing `qt6_extract_metatypes()` call (or its `_qt_internal_qml_type_registration()` equivalent) is extended to cover both `holonight_application` and `holonight_rendering` targets.
- The `cmake/combine-metatypes.cmake` helper (if used) is invoked for all relevant static-library targets.
- `INTERFACE_SOURCES` is cleared appropriately to prevent ODR violations (per the "QML Singletons From Static Libraries" gotcha in CLAUDE.md).
- The build system does not emit "Multiple C++ types found" warnings.

---

### REQ-C-007: holonight-qt KSyntaxHighlighting theme JSON generation

**Statement:** The sibling `holonight-qt` repository shall gain a small code generator that emits one Kate-format theme JSON file per active HoloNight color scheme (10 schemes total).

**Acceptance criteria:**
- The generator is implemented in the `holonight-qt` repository (location TBD; a new `tools/theme-generator/` directory is acceptable).
- For each of the 10 schemes (`HoloNightDark`, `HoloNightLight`, `HoloNightMocha`, `HoloNightLatte`, `TokyoNightStorm`, `TokyoNightDay`, `HoloNightEmber`, `HoloNightSol`, `HoloNightCyberD`, `HoloNightCyberL`), the generator calls `Holonight::ColorTokens::tokensForScheme()` and outputs a valid KSyntaxHighlighting theme JSON file.
- Generated theme files follow the KSyntaxHighlighting `theme.json` format (with `"text-styles"` object mapping `TextStyle` enum names to color and style properties).
- Themes are installed alongside the `HolonightQt` CMake package (e.g., under `lib/qt6/qml/HolonightQt/themes/` or a dedicated `share/holonight/themes/` directory).
- The generator is re-run as part of the `holonight-qt` build process (via a custom CMake target or build step) whenever color tokens change, and the generated files are committed to version control (or generated at install time; either approach is acceptable).

---

### REQ-C-008: Shared color mapping function

**Statement:** The color mapping from KSyntaxHighlighting's `TextStyle` roles onto each scheme's `ColorTokens` fields shall be defined once in a single shared function, not duplicated per-scheme.

**Acceptance criteria:**
- A single mapping function (e.g., `std::map<KSyntaxHighlighting::TextStyle, ColorToken> mapTextStylesToColorTokens()` or equivalent) is defined in the theme generator.
- All 10 generated theme JSON files use the output of this single function (invoked once per scheme).
- No per-scheme-variant of the mapping function exists; if future themes are added, the same mapping is reused.
- The mapping is documented or commented to explain the rationale for each KSyntaxHighlighting `TextStyle` → `ColorToken` correspondence.

---

### REQ-C-009: CodeHighlighter theme loading and repository initialization

**Statement:** The `CodeHighlighter` class shall load KSyntaxHighlighting themes from the `holonight-qt` package's theme directory using a `KSyntaxHighlighting::Repository` with a custom search path.

**Acceptance criteria:**
- `CodeHighlighter::CodeHighlighter()` (constructor) initializes a `KSyntaxHighlighting::Repository` and calls `repository.addCustomSearchPath()` with the path to the installed theme directory (determined at runtime, e.g., via CMake variable substitution or Qt resource path traversal).
- The search path is set up before any theme lookup occurs.
- If the theme directory is not found at runtime, `CodeHighlighter` falls back to an empty/default `Repository` (no crash, no error log required, just no highlighting).

---

### REQ-C-010: Live theme switching integration

**Statement:** All active `CodeHighlighter` instances shall re-apply syntax highlighting when `HoloniightPalette::paletteChanged` fires.

**Acceptance criteria:**
- `CodeHighlighter` connects to the global `HoloniightPalette::paletteChanged` signal (from the imported `holonight-qt` library).
- On receiving the signal, `CodeHighlighter::onPaletteChanged()` queries the current active color scheme (e.g., `HoloniightPalette::currentScheme()` or equivalent), maps it to the corresponding theme name, loads that theme from the `Repository`, and re-applies highlighting to the attached `QTextDocument`.
- All code blocks in the UI update their colors immediately, with no perceptible delay after the palette change.
- No text reflow, re-parsing, or loss of scroll position occurs during theme switching.

---

## Summary of Acceptance Criteria Coverage

This specification ensures that:

1. **Parsing** (REQ-F-002, REQ-NF-001) is correct, efficient, and doesn't block the UI.
2. **Language normalization** (REQ-F-003, REQ-F-004) handles known aliases and gracefully falls back for unknown languages.
3. **Code block rendering** (REQ-F-005, REQ-F-006, REQ-NF-002) is safe (no HTML interpretation) and user-friendly (copy, selection, scrolling).
4. **Message lifecycle** (REQ-F-007, REQ-F-008, REQ-F-010) transitions correctly from streaming to complete.
5. **Syntax highlighting** (REQ-F-009, REQ-F-011, REQ-NF-003) activates, integrates with theming, and degrades gracefully.
6. **Modularity** (REQ-C-001 through REQ-C-010) is maintained via a new `holonight_rendering` module with clear dependencies, and KSyntaxHighlighting theme integration is handled in the sibling `holonight-qt` repository.

Every requirement is independently falsifiable and can be verified during development or by automated testing.
