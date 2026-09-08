# UQC-104 design

See [specification](SPEC.md) and [implementation record](IMPLEMENTATION.md).

## Migration inventory

`qml/workspace`, `qml/shared` and `qml/quickpanel` mix direct Holonight,
unqualified Basic and QQC2/H aliases. Include CredentialTextField and
ProviderActionButton inheritance, dialog/menu/popup instances, enums and
attached scrollbars. Preserve Hn composite size roles; conditionally bind any
standard-control style-only property using the provider pattern.

`apps/chat/CMakeLists.txt` owns the sorted production QML and icon inventory.
Share that inventory with a dedicated acceptance executable without sharing
production singleton registration. Reuse existing test doubles and rendering
types. New checks use properties/signals rather than pointer/focus automation.

`ChatApplication` currently injects the configured dependency import root
unconditionally. Restrict it to the exact build executable, otherwise discover
relative to the executable's installed prefix. Embed the root style config.
Keep existing metatype extraction, clear-twice merge and registration unchanged.

Extend the canonical shell checker rather than replacing adoption/selection
checks. Keep semantic styling policy independent. Taskfile and both CI jobs
must agree on exact prerequisite revisions and stage under AI build directories,
with provider tests/examples off and Wayland on. Derive lint/test paths from CMake.

## Publication sequence

Publish this design first, then umbrella In Progress checkpoint linked here.
Implement and verify AI, publish and confirm canonical availability before pinning.
Only passing local acceptance makes UQC-104 Done. Prepare package-manager UQC-105
Ready at its rechecked published baseline; preserve both untracked mockups.
Keep the initiative Accepted and UQC-102/UQC-106/UQC-201 Planned.
