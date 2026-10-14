# External dependency management — holonight-ai

Work package: EDM-017
Baseline: `777affc646b44bfb757bd2616f5cd7a218897cba`
Umbrella audit: `38146d8f474317f3f8d72699f60e6d16a71dd165`
Status: Done locally; publication and umbrella pinning remain separate.
Implementation baseline: `0d8c54e166fb4e34ee1e496421c4bd98339a95c1`.
Canonical declaration checkpoint: `f6621567ddef06e14403a387c4bf99a5a6b679d6`.
Providers: Config `0ccd25f323662f50a47bb048be636160f2b7c135`, then Qt
`7c3ce074a8c2e02e4ee8cad8cd19eb8627aa4f68` (Controls and Wayland enabled).

## Requirements

- Configuration and normal build commands shall never acquire dependencies or prepare source providers.
- `task deps` shall explicitly prepare supplied local providers and check installed external prerequisites.
- Internal providers shall resolve only from approved prefixes; external packages shall resolve from an explicit
  external prefix or normal system locations.
- Enabled dependency-dependent features shall require their dependencies; minimal profiles shall disable them explicitly.
- Where production components use Qt, discovery shall require >=6.11; private Qt components shall match
  the selected public version exactly.
- Where tests use GTest, consumers shall require installed GTest >=1.14 CONFIG and any GMock targets they use.
- Existing public APIs, component switches, install-tree consumers and acceptance coverage shall be preserved.

## Design

Adopt canonical `tooling/modules/holonight-ai.json` and `.cmake`, then synchronize bundle 2.0.0 into this repository.
The copied metadata is owned by this standalone checkout. CMake performs authoritative discovery; shared probes
provide prerequisite diagnostics and record resolved versions/paths. Incompatible caches and local provider install
trees are archived before reuse. Package acquisition occurs only in explicit environment preparation. CI switches
build/runtime/release lanes together to qualified immutable images and runs acceptance offline after preparation.

Remove the GTest download fallback; require GMock. Preserve required md4c and syntax-highlighting discovery.

## Implementation and verification tasks

- [x] Confirm the exact baseline and canonical handoff before implementation.
- [x] Adopt declarations, prefix helpers, explicit preparation and compatibility signatures.
- [x] Apply the repository-specific discovery changes above.
- [x] Update Task, CI preparation/entrypoints, Dockerfiles, release lanes and contributor instructions.
- [x] Run declaration/CMake/profile consistency checks and offline missing-package regressions.
- [x] Run narrow regressions, clean acceptance, installed-package consumers and required quality checks.
- [x] Inspect complete logs; record commands, versions, features, warnings and manual checks.
- [x] Make a repository-local commit only after local verification; publish only when authorized.

Structured evidence is recorded below and in `verification.json`.

## Implementation decisions

- Bundle 2.0.0 and the canonical module declaration are copied without local bundle forks.
  Internal Config/Qt discovery remains restricted to approved prefixes; system/external discovery
  is used for Qt, GTest, libsecret, md4c and KF6SyntaxHighlighting.
- `task deps` prepares supplied local providers in declaration order. Both historical dependency
  commands delegate to it. Ordinary commands never invoke source preparation.
- CI preparation clones exact committed provider inputs (including when cloning local checkouts),
  records source revisions, toolchain/options and installed-content hashes, and archives incompatible
  evidence. Compatible provider builds are retained after launcher-only changes.
- Acceptance receives the installed prefix and revision records read-only, without provider sources
  or build trees. Every container uses the caller UID/GID and offline/pull-never options. The full
  qualified Arch image and separate REUSE image are declared in `scripts/ci/images.json`.
- The mutable project-image publisher and package bootstrap are retired. The compatibility Dockerfile
  only selects the qualified image; historical package audit data is preserved.
- Release has its own clean tests-OFF build and staged installation. Existing metatype extraction,
  singleton registration, test-module isolation, CTest registrations and style acceptance are retained.
  No application API or runtime behavior changes are intended.

## Compatibility correction

The first qualified acceptance passed all 714 CTest registrations, Release staging/startup,
strict static checks and licensing, then exposed the retained extra Holonight composer assertion:
the icon-only attachment action was 32 pixels high while native text buttons needed 34.
`ChatComposerActions.qml` now derives the square attachment height from its adjacent text actions.
The original equality, geometry, accessibility, icon and compact-mode assertions remain intact.
A focused real-QML check fails on the unchanged baseline (32/34/34), and passes after correction
under Holonight (34/34/34) and Fusion (25/25/25), each at scales 1 and 1.25.
This narrow composition correction is committed separately from dependency adoption.

## Verification record — 2026-10-14

Structured identities and log/content hashes are retained in `verification.json`. Full local logs,
CTest output, startup logs/maps, compiler database, package versions and staged Release payload
remain under ignored `build/ci/`. No hosted CI result or publication is claimed.

Commands run from AI unless noted:

- `python3 scripts/ci/test_launcher.py`: 12 launcher regressions pass.
- `python3 scripts/ci/test_dependencies.py` in the qualified offline container: 15 real-CMake
  regressions pass, including missing/old Qt/GTest, missing GMock, tests OFF, required external
  libraries, missing Controls/Wayland, approved-prefix isolation, spaces, compatible cache reuse,
  changed external prefix/features/toolchain, and coverage configuration/instrumented compilation.
- `python3 scripts/ci/run.py --prepare --provider-source-root ..`: exact Config then Qt preparation
  passes; subsequent launcher-contract refreshes reuse compatible provider builds. Sources,
  installed files, modes, symlinks, options, environment and content signatures are retained.
- `JOBS=8 python3 scripts/ci/run.py`: final clean four-lane acceptance; the final result is recorded
  in `verification.json` (run `20261014T151030Z-0d98xrcy`). No application cache is reused.
- At the umbrella: `python3 tooling/sync.py --check --module holonight-ai`,
  `python3 tooling/generate-install-profile.py --check`, declaration/snippet equality,
  `python3 -m unittest discover -s tooling -p test_external.py` (21), and
  `python3 -m unittest discover -s tooling -p test_tooling.py` (11) pass.
- Arch/Debian declaration mapping checks verify coverage-only gcovr and production exclusion of
  test-only GTest/GMock, D-Bus tools and gcovr. Bundle remains exactly 2.0.0.
- Python AST, Task/workflow YAML parsing, shell syntax and `git diff --check` pass.

The environment uses Qt 6.11.2, GTest 1.18.0, GCC 16.2.1, Clang 22.1.8, CMake 4.4.3,
Ninja 1.13.2, Task 3.53.1, Python 3.14.7, gcovr 8.6 and REUSE 6.2.0. Provider builds use
Debug, Config tests OFF, Qt tests/demo/gallery OFF and Wayland ON; Controls remain enabled.
Application acceptance builds Debug/tests ON and a separate Release/tests OFF installation.

Warning review: provider configuration emits Qt's private-module version-coupling notices;
optional Vulkan headers are absent in the qualified software-rendering environment. Source-only
QML unit fixtures report missing application image resources, which they intentionally do not
embed. Production/runtime fixture resources remain separately registered. These diagnostics are
reviewed rather than suppressed; real startup requires clean QML diagnostics. Compiler/static
warnings, startup results and remaining limitations are recorded with the final lane results.

Boundaries: no host installation, application restart, live model-service calls, Debian runtime
qualification, desktop automation or fresh manual desktop acceptance. Coverage instrumentation
is checked without a report threshold. Publication, canonical availability/current-CI query and
umbrella pinning remain separate handoffs. EDM-022 and the other four adoptions remain unchanged;
the umbrella initiative remains Accepted.

Final result: all four lanes pass in `20261014T151030Z-0d98xrcy`. CTest 714/714, composer
4/4 in every style/scale combination, and both style-specific QML subsets 59/59 pass.
Build-tree and installed startup pass all four selectors. Release fixture exclusion, path isolation
and configured D-Bus activation pass. Strict tidy checks the existing 127-unit target (the original
`tests/main.cpp` exclusion is retained), with warnings-as-errors; format, QML lint/types/policy and
REUSE pass. Complete application build/static logs contain no compiler or CMake warnings.
Final non-documentation input hashes match the accepted snapshot. Documentation completion is
validated separately without repeating unchanged application builds.

Compatibility correction commit: `549ab344cac6f989e56b2404e49a69a1b4fede52`. Final documentation snapshot
REUSE passes for 508/508 files. A direct submodule-mount attempt was rejected as invalid evidence
because its external Git metadata was unavailable and ignored local artifacts were scanned.
The corrected check uses the canonical independent snapshot; no license rules were relaxed.
