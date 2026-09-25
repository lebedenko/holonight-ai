# Explicit provider asset rendering in AI

Baseline: `c014946899b560d1ce23c57970cd209d8ba0a80d`.

Provider icons are bundled QRC SVG assets. Set `HnIcon` rendering explicitly so the existing semantic foreground colors remain palette-aware. Run QML lint and focused application tests against the accepted `holonight-qt` revision.

Implementation: simplify the provider icon source table and migrate the affected QML callers. Local verification (2026-09-25): `task qml-lint` and `task test` passed (714 tests) against the local provider build.
