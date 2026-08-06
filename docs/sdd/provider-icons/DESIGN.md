# Provider Icons — Design

Companion to `docs/sdd/provider-icons/SPEC.md`. Covers the concrete component/data-flow/interface
design for replacing the placeholder assistant-message icon with per-provider SVG icons.

## 1. Components

### `qml/shared/ProviderIcon.qml` (new)

Thin wrapper around `holonight-qt`'s `HnIcon`, holding the hardcoded provider → icon-policy table
(REQ-C-004) and the fallback branch (REQ-F-007).

```qml
pragma ComponentBehavior: Bound

import QtQuick
import Holonight

Item {
    id: root

    required property string providerId

    readonly property var _table: ({
        "ollama":    { source: "qrc:/HolonightChat/assets/providers/ollama.svg",    tinted: true },
        "openai":    { source: "qrc:/HolonightChat/assets/providers/openai.svg",    tinted: true },
        "anthropic": { source: "qrc:/HolonightChat/assets/providers/anthropic.svg", tinted: true },
        "google":    { source: "qrc:/HolonightChat/assets/providers/google.svg",    tinted: false },
    })
    readonly property var _entry: root._table[root.providerId]
    readonly property bool _known: root._entry !== undefined

    implicitWidth: parent ? parent.width : 0
    implicitHeight: parent ? parent.height : 0

    Rectangle {
        id: frame

        anchors.fill: parent
        radius: HoloniightPalette.radiusControl
        color: "transparent"
        border.color: HoloniightPalette.borderPassive
        border.width: HoloniightPalette.borderWidth

        HnIcon {
            anchors.centerIn: parent
            visible: root._known
            source: root._known ? root._entry.source : ""
            tinted: root._known ? root._entry.tinted : true
            normalColor: HoloniightPalette.textPrimary
            size: Math.round(Math.min(frame.width, frame.height) * 0.6)
        }

        // Fallback placeholder — same circle as today's MessageBubble icon, moved here so
        // MessageBubble stops owning two rendering paths (REQ-F-007).
        Rectangle {
            anchors.centerIn: parent
            visible: !root._known
            width: parent.width * 0.5
            height: width
            radius: width / 2
            color: HoloniightPalette.accentBlue
        }
    }
}
```

Notes on the snippet: `_table` is a plain QML object literal, not a JS `Map` or C++ registry —
adding a fifth provider is a one-line entry (REQ-C-005). `_entry`/`_known` are read-only derived
state, no imperative lookup function needed. `normalColor: HoloniightPalette.textPrimary` is set
unconditionally on the `HnIcon` child regardless of `tinted`, since it's inert when untinted.

### `qml/shared/MessageBubble.qml` (modified)

- `readonly property real iconSize: 32` → `64`.
- New `required property string providerId` alongside the existing `modelName`.
- The current icon-slot `Rectangle` (frame + placeholder circle) moves into `ProviderIcon`, and
  `MessageBubble` replaces it with a single `ProviderIcon` instantiation in the same
  `Layout.alignment: Qt.AlignTop` slot:

```qml
ProviderIcon {
    Layout.alignment: Qt.AlignTop
    visible: root.isAssistant
    Layout.preferredWidth: root.iconSize
    Layout.preferredHeight: root.iconSize
    providerId: root.providerId
}
```

  The frame remains a permanent design element for both resolved icons and the fallback. Only the
  placeholder circle is conditional inside `ProviderIcon.qml` (see §4).
  `MessageBubble.qml` itself ends up with no visual fallback logic of its own; it just always
  shows `ProviderIcon`, which internally decides fallback-vs-real-icon.

### `qml/shared/MessageList.qml` (modified)

The delegate already forwards `modelName` from the model to `MessageBubble`; it gains the same
treatment for `providerId`:

```qml
required property string providerId   // new, alongside existing required properties
...
MessageBubble {
    ...
    providerId: messageDelegate.providerId   // new binding, alongside modelName:
}
```

### `MessageListModel` (modified)

- `holonight_application/message_list_model.h`: new enum value `ProviderIdRole` appended after
  `ModelNameRole` (before `ContentBlocksRole`, or after it — ordering within the enum doesn't
  matter, only that it's a distinct `Qt::UserRole + N`).
- `Row` struct gains `QString provider_id;`.
- `roleNames()` gains `{ProviderIdRole, QByteArrayLiteral("providerId")}`.
- `data()` gains `case ProviderIdRole: return row.provider_id;`.
- `toRow()` gains `.provider_id = message.modelId() ? message.modelId()->provider_id : QString(),`.
- `updateLastMessage()`'s `dataChanged` role list gains `ProviderIdRole` (it already re-lists every
  role it might touch, so this is additive, not a new pattern).

This mirrors the existing `ModelNameRole` end-to-end exactly — same optional-unwrap idiom, same
"empty QString for user/system messages" convention — so no new pattern is introduced into this
class.

## 2. Data Flow

```
Message::modelId()                              (C++, std::optional<ModelId>)
  → ModelId::provider_id                          (C++, QString, e.g. "openai")
    → MessageListModel::toRow()                   (C++, Row::provider_id)
      → MessageListModel::data(ProviderIdRole)     (C++, QVariant)
        → QML delegate `model.providerId`          (MessageList.qml ListView delegate)
          → messageDelegate.providerId             (required property, forwarded)
            → MessageBubble.providerId              (required property)
              → ProviderIcon.providerId              (required property)
                → _table[providerId] lookup          (QML, static object literal)
                  → HnIcon.source / .tinted / .normalColor
                    → (tinted path) HnIconProvider.sourceUrl(...) → image://icon/... → C++ SVG
                      recolor (iconrenderer.cpp) → themed QImage
                    → (untinted path, google only) raw qrc:/ source used directly by the child
                      Image, unmodified
```

For user/system messages, `provider_id` is always `""`, which is simply not a key in `_table`,
so `ProviderIcon` renders its fallback — but this is moot in practice because `MessageBubble`'s
icon slot is already gated by `visible: root.isAssistant` upstream of `ProviderIcon`.

## 3. Interfaces / Contracts

**`ProviderIcon.qml` public surface:**
- `required property string providerId` — provider identifier string, e.g. `"openai"`. Case-
  sensitive, must match `ModelId::provider_id` values exactly as produced by the provider adapters
  (`holonight_providers`), which are already lowercase (`"ollama"`, `"openai"`, `"anthropic"`,
  `"google"`).
- No other public properties. Sizing is via the item's own `width`/`height` (set by the parent
  `Layout.preferredWidth/Height`), matching how the current placeholder Rectangle is sized today.

**`MessageListModel::Roles` addition:**
```cpp
ProviderIdRole,  // provider id for assistant messages (e.g. "openai"), "" for user/system
```
placed alongside `ModelNameRole` in the existing enum.

**CMake resource registration (`apps/chat/CMakeLists.txt`):**

Add alongside the existing QML-file globbing, before the `qt_add_qml_module()` call:

```cmake
file(GLOB HOLONIGHT_CHAT_PROVIDER_ICONS
    LIST_DIRECTORIES false
    CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/assets/providers/*.svg"
)
```

and add a `RESOURCES` argument to the existing `qt_add_qml_module(holonight-chat ...)` call:

```cmake
qt_add_qml_module(holonight-chat
    URI HolonightChat
    VERSION 1.0
    RESOURCE_PREFIX "/"
    TYPEINFO HolonightChat.qmltypes
    NO_GENERATE_QMLTYPES
    NO_IMPORT_SCAN
    QML_FILES ${HOLONIGHT_CHAT_QML_FILES}
    RESOURCES ${HOLONIGHT_CHAT_PROVIDER_ICONS}
)
```

With `RESOURCE_PREFIX "/"` already in effect and no per-file `QT_RESOURCE_ALIAS` override on these
new sources (unlike the QML files, which get aliases stripped of their `qml/` prefix), Qt's
QML-module resource nesting rule (documented in this project's `CLAUDE.md` "QML Structure"
section) still applies: everything registered through `qt_add_qml_module` on a QML-module target
lands under `/<URI>/`, so the real runtime path is
**`qrc:/HolonightChat/assets/providers/<name>.svg`** — matching the `_table` sources in
`ProviderIcon.qml` and REQ-F-009's example URL verbatim. This must be confirmed against the
generated `.qrc`/rcc listing during Implement, exactly as the module docs already instruct for
QML files.

## 4. Key Decisions and Rationale

- **QML-only lookup table, not a C++ registry.** REQ-NF-001 explicitly forbids new C++ code/
  modules for this feature. A `QMap`/enum-based provider-icon registry in `holonight_application`
  or a new module would need new headers, new metatype registration wiring (the project's
  `qt6_extract_metatypes`/`INTERFACE_SOURCES` dance — real cost, not boilerplate, per the OpenAI
  and Google provider-adapter cycles' CMake gotchas), and a new QML-visible type. A plain QML
  object literal costs nothing and satisfies REQ-C-004 (hardcoded, no content-sniffing) exactly as
  well. This also matches the project's established "rule of three/four" precedent: provider-
  specific branching stays as flat, unshared lookup logic until a fourth or fifth structural axis
  demands an abstraction — here there's no repeated *behavior* per provider, just data, so even
  that threshold doesn't apply.
- **Explicit `normalColor: HoloniightPalette.textPrimary` override.** `HnIcon`'s own default
  (`textSecondary`) is a deliberate choice for its other call sites (muted icon buttons, etc.).
  REQ-F-008 pins this feature's tint to `textPrimary` specifically, so `ProviderIcon` must override
  the default rather than rely on it — an incidental palette change to `HnIcon`'s default would
  silently change ProviderIcon's behavior if this weren't explicit.
- **Persistent frame with a conditional child inside `ProviderIcon`.** The frame is a design
  element shared by resolved provider icons and the fallback; it is not itself a placeholder.
  REQ-F-007 requires the pre-existing circle to remain available for empty/unknown `providerId`.
  Moving the frame and conditional child into `ProviderIcon` keeps `MessageBubble.qml`'s icon slot
  down to one unconditional component and ensures every caller gets the same framing. Two
  co-located, mutually-`visible`-gated children inside the frame are simpler than a `Loader` here
  since both branches are cheap, static QML.

## 5. Alternatives Considered

- **C++-side provider metadata registry** (e.g. a `ProviderIconRegistry` singleton in
  `holonight_application` mapping provider id → icon resource + tint policy). Rejected: violates
  REQ-NF-001 outright (new C++ code, new QML-registered type, new metatype-extraction wiring), and
  provides no benefit over a QML table since the data is static and QML-only consumers (just
  `ProviderIcon.qml`) already exist.
- **Tint flag as a filename/path convention** (e.g. `assets/providers/tinted/openai.svg` vs.
  `assets/providers/untinted/google.svg`). Rejected: still requires a `providerId` → path lookup
  table (so no complexity is actually removed), and additionally couples the tint decision to
  directory placement — moving or renaming an asset would silently change its recolor behavior.
  Explicitly the kind of "automatic ... heuristic-based classification" REQ-C-004 rules out, just
  shifted from SVG content to file path.
- **`QtQuick.Effects.MultiEffect`/`ColorOverlay` for tinting** instead of routing through
  `HnIcon`/`HnIconProvider`. Rejected: introduces a new QML module dependency (`QtQuick.Effects`)
  that the feature doesn't currently have, duplicates recoloring logic that `HnIconProvider`
  already implements and is already linked into this project, and would not reuse the existing
  KDE-style `ColorScheme-*`/literal-hex dual-path recoloring `IconRenderer::applySemanticColors`
  provides. `HnIcon` is the established, spec-mandated reuse target (REQ-NF-001).

## 6. Known Risks

- **The three "tinted" SVGs likely will not actually recolor as written.**
  `IconRenderer::applySemanticColors` (in `holonight-qt/src/icons/iconrenderer.cpp`) only rewrites
  paint colors it can find as literal **hex** values (`#[0-9A-Fa-f]{3,8}`) following
  `fill=`/`stroke=`/`stop-color=`/`color:`, when no KDE `ColorScheme-*` CSS classes are present.
  Inspecting the actual checked-in assets:
  - `ollama.svg` and `anthropic.svg` have **no `fill` attribute at all** on their `<path>`
    elements (they rely on the SVG default fill, which is black).
  - `openai.svg`'s single path uses `fill="black"` (a CSS/SVG **named** color, not hex).
  None of these match the literal-hex regex, so as committed today, all three would render as
  flat black regardless of theme or `resolvedColor` — the recolor path silently no-ops. This is a
  real gap, not a hypothetical: confirmed by grepping the actual asset files during this design's
  research (`grep -o 'fill="[^"]*"' assets/providers/*.svg`). **Action for Implement stage**: this
  needs a quick verification render early (before considering REQ-F-002 done), and if confirmed,
  a minimal *asset* edit — adding an explicit `fill="#000000"` attribute to the path element(s) in
  `ollama.svg`, `openai.svg`, and `anthropic.svg` — is the fix. This is an SVG content edit, not
  new code, so it doesn't conflict with REQ-NF-001; it should be called out explicitly as in-scope
  asset cleanup if hit.
- **Resource path nesting.** As documented in this project's own `CLAUDE.md` ("QML Structure"),
  Qt nests *all* resources registered via `qt_add_qml_module` under `/<URI>/` regardless of
  `RESOURCE_PREFIX`. This was previously verified for `.qml` files by inspecting the generated
  `.qrc` under `build/apps/chat/.qt/rcc/`; the same check needs to be repeated for the new SVG
  `RESOURCES` entries specifically, since non-QML-file resources may follow a slightly different
  aliasing rule than QML sources (which get an explicit per-file `QT_RESOURCE_ALIAS` today, while
  the SVGs as proposed do not). If the resulting path doesn't match
  `qrc:/HolonightChat/assets/providers/<name>.svg`, `ProviderIcon.qml`'s `_table` sources need a
  one-line update — low risk, but worth a build-time check before declaring REQ-F-009 done.
- **Regex recoloring is inherently naive.** Even once the hex-literal issue above is fixed, the
  literal-paint-color regex will recolor *every* hex fill/stroke it finds, indiscriminately. This
  is fine for the current three single-path monochrome assets (one color each) but would misbehave
  on a future "tinted" asset with multiple literal hex colors that aren't meant to unify to one
  tint. Not a blocker for this cycle's four assets, just a constraint to remember if a fifth
  provider's icon is added later via a differently-authored SVG.
- **Layout reflow from 32→64 icon size.** `MessageBubble.qml`'s `messageFrame.Layout.preferredWidth`
  binding subtracts `iconSize + iconSpacing` from the available row width when `isAssistant` is
  true:
  `Math.min(row.width * maximumWidthRatio - (isAssistant ? iconSize + iconSpacing : 0), ...)`.
  Doubling `iconSize` from 32 to 64 doubles that subtraction (an extra 32px removed from the
  message bubble's max width budget). On narrow windows this could visibly shrink the assistant
  bubble further than before; worth a quick visual check at small window widths during Implement,
  though no formula change is needed — the existing expression already accounts for `iconSize`
  symbolically.
