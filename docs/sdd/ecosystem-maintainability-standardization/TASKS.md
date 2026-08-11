# Ecosystem Maintainability Standardization — holonight-ai

| ID | Task | State | Verification |
|---|---|---|---|
| HOLONIGHT_AI-01 | Move product QML to apps/chat/qml with feature-oriented directories, one URI, and stable aliases. | Planned | — |
| HOLONIGHT_AI-02 | Remove local install; keep system staging and administrator-neutral D-Bus metadata installation. | Planned | — |
| HOLONIGHT_AI-03 | Audit Holonight module imports, duplicated controls, raw controls, and relative imports; add qmllint/qmltypes checks. | Planned | — |
| HOLONIGHT_AI-04 | Replace fixed `/tmp` dependency assumptions with an explicit temporary staged provider prefix. | Planned | — |
| HOLONIGHT_AI-05 | Replace the obsolete ThemeConfig dependency with the exported Appearance contract. | Done | 2026-08-12: staged Release build and all 703 tests passed; Secret Service integration test skipped by its environment guard. |

Allowed states are `Planned`, `Ready`, `In Progress`, `Done`, `Blocked`, and `Superseded`. Record exact commands
and results before marking a task `Done`.
