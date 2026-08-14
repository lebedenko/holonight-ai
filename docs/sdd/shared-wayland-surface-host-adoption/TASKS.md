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
- [ ] Commit and publish the SWS-103 handoff; record its commit and CI results below.

## Local verification — 2026-08-14

- `task test`: passed, 711 tests; the Secret Service integration test was skipped by its existing
  environment gate.
- `task build:release`: passed.
- `task format-check`: passed after applying the repository formatter.
- `task tidy`: executed; the SWS-103 production and test files pass focused clang-tidy. The full
  target remains red on pre-existing findings in `conversation.cpp`, three QML tests, and one test
  identifier under the local rolling clang toolchain; none are in this work package.
- `task qml-lint`: passed with the repository's existing missing-property warnings.
- `task qmltypes-check`: passed.
- Source and regenerated Ninja/compilation configuration contain no AI-owned protocol XML,
  `zwlr_layer_*` calls, raw Wayland handles, Qt Wayland private headers, or AI protocol scanner.

Initial handoff commit: `3e75aa6a35e4a7756dc85fe6c4dc3293bc434f65`, published to canonical
`origin/main`. CI run `31794389862` exposed a missing `tomlplusplus` package in the AI CI image;
run `31794521163` then exposed missing `ripgrep` and a root-only permission-test assumption. The
follow-up environment repairs are part of this handoff. Final CI results are pending.
