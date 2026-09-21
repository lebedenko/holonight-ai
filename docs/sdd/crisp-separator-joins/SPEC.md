# Crisp separator joins — holonight-ai

Status: Accepted for implementation, 2026-09-21. Work package CS-004.
Baseline: `7e25e78cc7fb33aa63ed480bc3f328a65e04ca0c`. The approved user plan authorizes this scope.

- REQ-001: Adopt HnSeparator integer physical thickness, inherited opacity, borderPassive default,
  and Leading/Center/Trailing boundary alignment without caller DPR calculations.
- REQ-002: Bottom/right boundaries shall be trailing aligned; top/left boundaries leading aligned.
  Preserve unrelated worktree edits, application architecture and existing style overrides.
- REQ-003: Verify imports and relevant application regressions against explicitly staged modified Qt.
