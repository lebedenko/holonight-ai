# Multi-Provider Settings Checklist

- [x] Phase 1 — Configuration types and schema migration (T-001–T-005)
- [x] Phase 2 — Instance registry and provider routing (T-006–T-010)
- [x] Phase 3 — Settings drafts and sessions (T-011–T-016)
- [x] Phase 4 — QML provider management (T-017–T-022)
- [x] Phase 5 — Chat and utility integration (T-023–T-026)
- [x] Phase 6 — Historical rendering (T-027–T-029)
- [x] Phase 7 — Remove fixed-ID assumptions (T-030–T-032)
- [x] T-033 — Focused tests pass
- [x] T-034 — Full verification recorded
  - [x] Tests: 450/450
  - [x] Format check
  - [x] QML lint
  - [x] QML type check
  - [x] Clang-tidy run; findings recorded in `TASKS.md`
- [x] T-035 — Graphical manual acceptance pass
  - [x] Fresh-install empty state
  - [x] Complete Add menu
  - [x] Multiple instances of the same provider type
  - [x] New-instance prepend and selection
  - [x] Instance-name validation
  - [x] All provider-specific forms
  - [x] Save/Discard/Cancel prompts
  - [x] Disable during streaming
  - [x] Delete confirmation and active-stream blocking
  - [x] Dependent chat provider/model dropdowns
  - [x] Utility-provider fallback
  - [x] Historical rendering after provider deletion

Overall: **35 of 35 tasks complete**. The feature is accepted with the recorded clang-tidy debt.

See [TASKS.md](TASKS.md) for detailed requirements and verification notes.
