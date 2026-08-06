# DESIGN: Canonical HoloNight module adoption

**Spec:** `docs/sdd/canonical-module-adoption/SPEC.md`
**Status:** Implemented
**Date:** 2026-07-28

## Repository findings

The application owns one QML module, `HolonightChat`, whose sources are embedded by
`apps/chat/CMakeLists.txt`. `ChatApplication` adds the configured HoloNight QML import root to its
engine, while `task run` prepends the same dependency installation to `QML_IMPORT_PATH`.

The source tree contains `import Holonight` and one already-aliased
`import Holonight.Controls as HnControls`; it contains no lowercase compatibility imports and no
dynamically constructed QML import statements. The sibling dependency is at the required
canonical-module migration commit.

## Import migration

Each QML file is classified from the types it actually consumes:

- Add `import Holonight.Core` for palette/theme/appearance/shape enums and services, including
  `HnIcon`.
- Add `import Holonight.Controls` for `HnSurfaceFrame` and `HnApplicationWindow`.
- Keep unaliased `import Holonight` only in files that instantiate Qt Quick Controls types whose
  HoloNight style implementation remains in that module.
- Remove an unaliased compatibility import when Core and Controls fully cover the file.
- Preserve `MessageList.qml`'s `HnControls` alias because it intentionally disambiguates an
  explicitly qualified Controls component.

Imports remain unversioned, matching the repository and upstream convention. Core precedes
Controls, and the compatibility/style module remains adjacent to them where retained.

## Build and runtime resolution

No new application QML module or copied component is introduced. The existing
`HOLONIGHT_QML_IMPORT_PATH` points at the dependency's QML root and therefore covers sibling
`Holonight/Core` and `Holonight/Controls` directories as well as `Holonight`.

The implementation shall verify the canonical module artifacts during configuration or focused
testing. Explicit application linkage to `HolonightQt::Core` or `HolonightQt::Controls` will be
added only if the imported plugins are not already runtime-resolvable through the installed
package. Package names, project versions, install destinations, and the `HolonightChat` URI remain
unchanged.

## Verification design

A focused QML smoke test uses a real `QQmlEngine` with the configured import root and the
application's embedded QML module. It verifies direct canonical imports and representative
components, including windows/surfaces and singleton-backed palette or appearance bindings.
Tests assert observable construction success and QML error output, not generated implementation
details.

A focused source-policy check scans application QML imports and migrated type usage. It rejects
missing canonical imports, lowercase canonical modules, and compatibility imports that have no
remaining style-owned consumer. A packaging check validates the resolved dependency installation
contains Core and Controls `qmldir`, plugin, and type metadata artifacts.

Verification proceeds from focused tests to `task qml-lint`, full `task test`, formatting and
whitespace checks, then offscreen/headless launches with `QT_SCALE_FACTOR=1.0` and `1.25`.
Launch logs are checked for import, binding, and component-creation diagnostics. A visual
redesign is outside this cycle, so no screenshot baseline is changed.

## Risks and mitigations

- **False removal of `Holonight`:** classify style-owned control use per file and let QML lint and
  component smoke tests catch unresolved controls.
- **Older dependency shadowing:** retain the existing configured import-root precedence and test
  against `/tmp/holonight-qt-prefix`.
- **Singleton duplication:** import canonical and compatibility modules in the same engine during
  focused verification and assert behavior through the existing singleton-backed components.
- **Platform-only quick panel behavior:** instantiate what can run offscreen and separately inspect
  launch logs; do not alter Wayland surface policy in this adoption.

## Traceability

- REQ-F-001 through REQ-F-005: per-file import classification and source-policy check.
- REQ-F-006 and REQ-F-007: focused QML component smoke test.
- REQ-F-008 and REQ-P-001 through REQ-P-004: existing runtime path plus packaging check.
- REQ-Q-001 through REQ-Q-004: focused tests, full headless verification, and scale-factor
  launches.
