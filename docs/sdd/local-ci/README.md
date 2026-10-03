# Local CI rehearsal

Baseline: 1d8854b0eefacda1d84b3260a13ca1591b455239. Umbrella package CI-011.

Preserve independent build-test and static-checks jobs, push/PR main triggers,
and separate licensing push/PR job. Each calls shared repository scripts with
immutable build/REUSE 6.2.0 image identities. Keep Config
fe69a59e6b73167fd5349223a4d265d75386c139 and Qt
8d11e3e91fea5ad0d20a34f2ed27e5e5f485124a. The previous Qt pin
863af41 lacks the rendering/normalColor icon API already consumed by AI;
8d11e3e is the first published implementation of that API. Fresh Release providers disable tests,
demo and gallery, and enable Wayland. Fresh Debug application enables tests and
compile commands. Keep full CTest, composer Holonight/Fusion at scales 1/1.25,
existing QML suites in both styles, isolated build/installed four-mode startup,
activation Exec assertions, QML lint/types and independent format/full tidy.

Use the accepted immutable Qt build image with checksum-pinned syntax-highlighting
and ripgrep supplements installed only in the disposable container before dropping
to the invoking host UID/GID. Matching system paths let isolated runtime acceptance
retain its sanitized environment. No host dependency installation.

Snapshot tracked edits/non-ignored new files preserving modes/symlinks; report new
inputs. Keep input read-only, fresh trees and host-owned logs/results/evidence in
ignored build/ci. Preserve development tasks. Required unavailable checks fail.
No push/pin updates or publication. Locally verified on 2026-10-03.

Host diagnostics: retain the existing static-check families and anchor header
selection to owned paths. Small private helpers reduce streaming, projection,
configuration and provider-initialization complexity. Reflected Qt role constants
and exported D-Bus names retain their contracts; narrow annotations explain those
exceptions and intentionally visible test-fixture state.

Clang-tidy 23's new trailing-comma check incorrectly treats delimiters following
empty inline initializers as commas inside those initializers. Configure only
`readability-trailing-comma.SingleLineCommaPolicy=Ignore`, retaining multiline
validation. This follows the documented policy option in the [LLVM check
documentation](https://clang.llvm.org/extra/clang-tidy/checks/readability/trailing-comma.html).
Restore all affected delimiters, compile and run behavioral acceptance after fixes.
The policy does not disable any static-check family.


Acceptance evidence: `task ci` passed all three lanes in
`build/ci/20261003T201002Z-gflc90t4/`: 714 CTest cases, four composer runs
(4 cases each), both 59-case QML style runs, all eight build/installed startup
modes, QML lint/types, format/full tidy and REUSE 6.2.0. All 23 evidence files
are host-owned. Hash/mode/mtime/symlink comparison confirmed 1,932 source and
normal development-build files unchanged. Four launcher regressions pass,
covering snapshots, missing runtimes, failed lanes and rootless Podman mapping.
Real Podman execution is unverified because it is not installed.

Host verification uses exact archived Config/Qt revisions in
`build/ci/native-providers-rendering/`, Release providers and a fresh Debug
`build/ci/native-acceptance/`. Complete build, CMake `tidy-src` with clang-tidy
23.1.1, `task format-check`, native REUSE, 179 focused behavioral cases and
57 affected projection/runtime cases pass. Native evidence is in ignored
`build/ci/native-*.log`; toolchain GCC 16.2.1 / Qt 6.11.2. Container acceptance
uses GCC 16.1.1 / Qt 6.11.1 / clang 22.1.6. Complete logs were reviewed; only
Qt's expected private-Gui version-coupling notices remain. Logging-only matrix
labels were added after acceptance and checked with shell syntax/regressions.

The earlier run (`20261003T190020Z-6oi303it`) demonstrated real runtime/static
failures and nonzero task propagation. Its logs remain available. The provider
correction and owned diagnostics were resolved before the successful clean run.
