# Tasks

- [x] Audit current consumers and preserve unrelated edits.
- [x] Apply the accepted separator contract where needed.
- [x] Build and run relevant regressions against the explicitly staged modified provider.
- [x] Record local verification and remaining integration boundary.

Verified 2026-09-21 against the uncommitted provider working tree, staged from
`holonight-files/build/deps/holonight-qt` into this repository's dependency prefix.

Quick-panel and conversation headers use trailing bottom boundaries. QML lint now uses --bare with
explicit provider and standard Qt import roots, preventing the installed older provider from taking priority.

Full build and 714 CTest entries passed (one existing real-secret-service integration case skipped).
Final QML regression selection against the staged provider: 48/48. Strict QML lint passes.

Native connected-window acceptance belongs to Files and has passed. The user subsequently authorized
publication and pin updates; the umbrella ledger records published revisions and the CI snapshot.
