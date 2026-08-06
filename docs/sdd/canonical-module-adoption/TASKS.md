# TASKS: Canonical HoloNight module adoption

**Spec:** `docs/sdd/canonical-module-adoption/SPEC.md`
**Design:** `docs/sdd/canonical-module-adoption/DESIGN.md`

- [x] **T-001:** Confirm a clean downstream worktree and upstream baseline commit `25939dd`.
- [x] **T-002:** Inventory QML imports, migrated type usage, dynamic QML loading, dependency
  declarations, runtime import paths, packaging, and existing tests.
- [x] **T-003:** Record requirements, design boundaries, verification strategy, and traceability.
- [x] **T-004:** Add canonical Core imports to every migrated Core-type consumer.
- [x] **T-005:** Add canonical Controls imports to every migrated Controls-type consumer.
- [x] **T-006:** Retain `Holonight` only for style-owned controls and preserve required aliases.
- [x] **T-007:** Add focused canonical-import, representative-instantiation, source-policy, and
  dependency-artifact coverage.
- [x] **T-008:** Run the narrowest focused checks and `task qml-lint`.
- [x] **T-009:** Run the repository's full headless test workflow (421/421 passing).
- [x] **T-010:** Launch offscreen at scale factors 1.0 and 1.25; neither launch emitted QML
  diagnostics.
- [x] **T-011:** Review the final diff, run formatting/whitespace checks, and document any
  intentional compatibility imports.
