# Ecosystem Maintainability Standardization

Status: Draft

Umbrella work package: see `docs/initiatives/ecosystem-maintainability-standardization/TASKS.md` in the pinned
umbrella checkout.

## Profile and review result

Profile: Qt application.

The executable is under apps/chat, but application QML remains at top-level qml and is split only into shared/workspace/quickpanel. Installation includes the binary and D-Bus activation metadata.

The review covered target boundaries, CMake and presets, Task commands, CI/release workflows, tests, packaging,
documentation, installation behavior, and (where applicable) QML module/import/resource metadata. Product changes
remain backlog work; this document does not change runtime behavior.

## Accepted constraints

- Production installation must configure prefix `/usr` and support a temporary `DESTDIR` stage consumed by downstream builds.
- Task names are capability-based; inapplicable local or removal tasks are omitted, not implemented as no-ops.
- Package-manager changes, service enablement, active-session changes, and user/admin data mutation are out of scope.
- CI-image changes belong to the existing Shared CI Build Infrastructure initiative.

## Completion criteria

The local work package is complete only when the tasks in [TASKS.md](TASKS.md) pass repository-local verification,
the commit is published to the canonical remote, and the umbrella coordinator accepts the handoff.
