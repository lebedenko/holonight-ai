# SDD Tasks — provider-icons

- [x] T-001: Fix SVG fill attributes on tinted assets
  - REQs: REQ-F-002
  - Check: `grep 'fill="#000000"' assets/providers/ollama.svg assets/providers/openai.svg assets/providers/anthropic.svg` confirms all three have explicit `fill="#000000"` on path elements; REQ-F-002 recolor path can now match hex-literal regex.

- [x] T-002: Wire provider SVG assets in CMakeLists.txt
  - REQs: REQ-F-009, REQ-NF-001
  - Check: `apps/chat/CMakeLists.txt` defines `HOLONIGHT_CHAT_PROVIDER_ICONS` glob matching `assets/providers/*.svg` and passes it as `RESOURCES` argument to `qt_add_qml_module(holonight-chat ...)`; project builds without CMake errors.

- [x] T-003: Add ProviderIdRole to MessageListModel
  - REQs: REQ-F-004, REQ-NF-001
  - Check: `holonight_application/message_list_model.h` contains `ProviderIdRole` enum value; `roleNames()` includes `{ProviderIdRole, "providerId"}`; `data()` returns `row.provider_id`; QML binding `model.providerId` compiles and receives correct value.

- [x] T-004: Create ProviderIcon.qml component with lookup table
  - REQs: REQ-F-001, REQ-F-002, REQ-F-003, REQ-F-007, REQ-F-008, REQ-NF-001, REQ-NF-002, REQ-C-004
  - Check: `qml/shared/ProviderIcon.qml` exists; accepts `required property string providerId`; defines static `_table` object mapping ollama/openai/anthropic to `tinted: true` and google to `tinted: false`; instantiates `HnIcon` with `normalColor: HoloniightPalette.textPrimary`; fallback Rectangle visible when `providerId` is empty/unknown; no animation or conditional tint-color elements present.

- [x] T-005: Update MessageBubble.qml to use ProviderIcon and increase icon size
  - REQs: REQ-F-005, REQ-F-006, REQ-NF-002, REQ-C-001
  - Check: `qml/shared/MessageBubble.qml` has `required property string providerId`; `iconSize` property changed from 32 to 64; icon-slot Rectangle replaced with single `ProviderIcon` instantiation with `providerId: root.providerId` binding; no modifications to `qml/workspace/` files.

- [x] T-006: Update MessageList.qml delegate to forward providerId
  - REQs: REQ-F-004, REQ-F-005
  - Check: `qml/shared/MessageList.qml` ListView delegate gains `required property string providerId`; `MessageBubble` instantiation includes `providerId: messageDelegate.providerId` binding; QML bindings compile without errors.

- [x] T-007: Build and verify resource paths
  - REQs: REQ-F-009, REQ-NF-001
  - Check: `task build` succeeds; inspecting `build/apps/chat/.qt/rcc/qrc_HolonightChat.qrc` (or equivalent generated `.qrc` file) confirms all four provider SVG entries resolve to `qrc:/HolonightChat/assets/providers/{ollama,openai,anthropic,google}.svg`; running the app produces no "missing resource" warnings in console or QML logs.

- [x] T-008: Final visual inspection and acceptance verification
  - REQs: REQ-F-002, REQ-F-003, REQ-F-008, REQ-NF-001, REQ-NF-002, REQ-C-001, REQ-C-002, REQ-C-003, REQ-C-004, REQ-C-005
  - Check: Launch the app and visually inspect: (1) ollama/openai/anthropic message icons render with the UI theme's primary text color in both light and dark themes; (2) google message icon retains multi-color branding without theme recolor; (3) resolved icons and the empty/unknown-provider placeholder circle render inside the same 64×64 design frame; (4) no new C++ code files under `src/`; (5) no modifications to `qml/workspace/` or provider settings panels; (6) grep confirms zero new hardcoded hex color literals in `qml/shared/ProviderIcon.qml` or `qml/shared/MessageBubble.qml`; (7) grep confirms no animation elements (`Behavior`, `PropertyAnimation`, `Transition`) in `ProviderIcon.qml`; (8) grep confirms no new build-time helpers, scripts, or documentation changes to workflows; (9) all items in SPEC.md "Acceptance Verification Checklist" section verified.
