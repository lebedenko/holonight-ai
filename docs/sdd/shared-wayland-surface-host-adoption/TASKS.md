# SWS-103 Tasks

Status: Accepted

- [x] Record exact AI, Qt, and configuration-provider baselines.
- [x] Supersede AI-private Wayland lifecycle ownership in the earlier quick-panel design.
- [x] Adopt `HolonightQt::Wayland` and remove AI-owned protocol/private integration.
- [x] Preserve `PanelSurface` policy and application contract with exclusive keyboard focus.
- [x] Add deterministic surface-policy and lifecycle-adapter coverage.
- [x] Pin local and CI dependency setup to the accepted provider revisions.
- [x] Run `task test`, `task build:release`, `task format-check`, `task tidy`, `task qml-lint`, and
  `task qmltypes-check` (see verification note for the repository-wide tidy baseline).
- [x] Confirm no private layer-shell implementation remains in source/build configuration.
- [x] Commit and publish the SWS-103 handoff; record its commit and CI results below.

## Local verification — 2026-08-14

- `task test`: passed, 711 tests; the Secret Service integration test was skipped by its existing
  environment gate.
- `task build:release`: passed.
- `task format-check`: passed after applying the repository formatter.
- `task tidy`: passed after generating the build artifacts required by QML test translation units
  and applying the current toolchain's mechanical style updates.
- `task qml-lint`: passed with the repository's existing missing-property warnings.
- `task qmltypes-check`: passed.
- Source and regenerated Ninja/compilation configuration contain no AI-owned protocol XML,
  `zwlr_layer_*` calls, raw Wayland handles, Qt Wayland private headers, or AI protocol scanner.

Published implementation handoff: `5e96acc06a6a5b9ae342cfd677e9350d6ba01784`. Canonical CI run
`31800288242` passed both `Build and test` and `Static checks`, including build, 711 tests, QML
lint, QML type metadata validation, format check, and full clang-tidy. Earlier runs
`31794389862`, `31794521163`, `31795909578`, and `31797190263` exposed the missing pinned-provider
packages, root-container test assumption, absent generated MOC artifacts, and current-toolchain
formatting that were repaired as part of the published handoff.
