# SPEC: Canonical HoloNight module adoption

**Feature:** Adopt canonical HoloNight Core and Controls QML modules
**Status:** Implemented
**Date:** 2026-07-28
**Upstream baseline:** `holonight-qt` commit `25939dd`

## Goal

Migrate the application's use of shared HoloNight types from compatibility exports to their
canonical `Holonight.Core` and `Holonight.Controls` modules without changing behavior, appearance,
packaging, or launchability.

## Functional requirements

- **REQ-F-001:** Every QML file that uses `HoloniightPalette`, `HolonightTheme`, `HnAppearance`,
  `HnShapeProfile`, `HnSurfaceRole`, `HnCornerStyle`, `HnShapeKind`, `HnCornerMask`,
  `HnIconProvider`, or `HnIcon` shall import `Holonight.Core`.
- **REQ-F-002:** Every QML file that uses `HnSurfaceFrame` or `HnApplicationWindow` shall import
  `Holonight.Controls`.
- **REQ-F-003:** A QML file shall retain `import Holonight` when it uses standard HoloNight Qt
  Quick Controls style components that remain owned by that module.
- **REQ-F-004:** A legacy `Holonight` import shall be removed when the file no longer consumes
  anything exported exclusively by that compatibility/style module.
- **REQ-F-005:** Existing import aliases shall be preserved unless an explicit alias is required
  to resolve ambiguity.
- **REQ-F-006:** The workspace window, settings window, quick panel, and important shared
  components shall continue to instantiate through the existing `HolonightChat` QML module.
- **REQ-F-007:** Palette, theme, appearance, semantic surface roles, corner masks, and icon
  behavior shall retain their current bindings and values.
- **REQ-F-008:** Build-tree, development-run, and installed execution shall continue to resolve
  the canonical HoloNight QML plugins.

## Packaging and dependency requirements

- **REQ-P-001:** Preserve the project version, QML module URI/version, executable name, install
  destinations, and `HolonightQt` package name.
- **REQ-P-002:** Use only the established `HolonightQt::Core` and `HolonightQt::Controls` CMake
  component targets if explicit target linkage is required.
- **REQ-P-003:** The configured HoloNight QML import root shall contain the canonical
  `Holonight/Core` and `Holonight/Controls` module artifacts needed at runtime.
- **REQ-P-004:** No lowercase `holonight.core` or `holonight.controls` import shall be introduced.

## Quality requirements

- **REQ-Q-001:** Focused automated checks shall prove canonical imports resolve and representative
  application components instantiate headlessly.
- **REQ-Q-002:** Automated checks shall guard newly migrated application QML from unnecessary
  compatibility imports.
- **REQ-Q-003:** Packaging/install verification shall check that the resolved dependency import
  tree includes canonical Core and Controls runtime artifacts.
- **REQ-Q-004:** Full headless tests, QML lint, formatting/whitespace checks, and launches at scale
  factors 1.0 and 1.25 shall report no new QML import, binding, or component-creation errors.

## Constraints and non-goals

- Preserve current visuals, geometry, behavior, public versions, and singleton identity.
- Do not remove or deprecate upstream compatibility exports.
- Do not duplicate upstream controls or introduce new dependencies.
- Do not perform density, semantic-size, appearance, or unrelated cleanup.
- Do not modify generated dependency lockfiles, credentials, or secrets.

## Acceptance

The application consumes migrated shared types from `Holonight.Core` and `Holonight.Controls`,
retains `Holonight` only for its style-owned controls, passes focused and full verification, and
launches from the configured upstream installation at scale factors 1.0 and 1.25 without new QML
diagnostics or visual changes.
