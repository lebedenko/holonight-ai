# UQC-104 implementation and local acceptance

Date: 2026-09-08
Status: Local acceptance passed; implementation published

## Publication and baselines

The [specification](SPEC.md) records the exact assignment/dependency baselines.
AI design `accae4ed9afd3288fb68beeb5ad8fc8ed3643370` was published and confirmed
on canonical origin/main before umbrella checkpoint `791244c` started implementation.
Only AI implementation and umbrella coordination are in scope. Package-manager's
`docs/mockups/explore.png` and `docs/mockups/history.png` remain untouched.

## Implementation

- Application QML uses runtime `Controls` for standard types, enums and attached
  properties, including local button/text-field wrappers, dialogs, menus, popups
  and scrollbars. Core and composites retain their canonical imports and painting.
  All existing size roles belong to composites; no standard-control size binding
  or provider contract change was needed.
- The executable embeds root `:/qtquickcontrols2.conf` with `Style=Holonight`.
  Configured QML discovery is restricted to the exact build executable; installed
  discovery uses the executable-relative configured libdir. CMake re-resolves the
  dependency path rather than retaining a stale cached path across provider changes.
  Configure-time D-Bus activation paths and application/rendering metatype merge,
  clear-twice extraction and singleton registration remain intact.
- `cmake/ChatQmlInventory.cmake` supplies the same sorted QML/icon inventory to
  production and acceptance. The acceptance executable/module live in a separate
  build directory to avoid influencing existing source-based tests. It links
  rendering and Qt, never production application/network/credential registrations.
- Existing composer, provider-management and utility-settings doubles are shared
  through `tests/qml/fakes/`. New acceptance uses properties/signals, including
  all four provider forms' masked draft clearing, wrapper reveal/clear signaling,
  dirty navigation, bidirectional composer drafts/submission, model selection and
  overflow with forty models and three visible rows, transcript scrolling, menus,
  QuickPanel's conversation popup, statistics popup, and tool detail disclosure.
  Tool results are inline in this application; there is no separate tool popup.
- Canonical policy retains Core/composite adoption and semantic-selection checks,
  adds runtime namespace enforcement, and independently rejects eight import/type/
  enum/attached-property mutations plus composite and selection regressions. The
  semantic painting checker remains separate.
- Taskfile and both CI jobs pin the exact provider/config revisions from SPEC,
  stage dependencies in AI build directories, turn provider tests/demo/gallery off,
  and retain Wayland. Lint/tests use configured discovery. The container task
  checks revisions on the host and builds dependencies inside its own build tree.
  README and the intentionally ignored local AGENTS.md/CLAUDE.md explain the policy.
- `ContentBlockTypeNs` now explicitly defaults its existing virtual destructor to
  satisfy the checked-in special-member tidy rule; its registration/API behavior
  is unchanged. Tidy worker count is configurable for constrained machines.

## Isolation and evidence

The acceptance executable uses temporary XDG storage and fake controllers. It
asserts actual control-background implementation URLs and loaded Core/composite/
style libraries, in fresh Holonight and Fusion processes with a 45-second CTest bound.

`scripts/check-runtime-launches.py` runs each actual binary in four fresh modes:
embedded default, environment Fusion, command-line Fusion over environment
Holonight, and external Fusion configuration. It uses a private D-Bus daemon with
no service activation directories, temporary HOME/XDG config/data/cache/runtime,
explicit disabled Ollama/OpenAI/Anthropic/Google instances and background generation
off, and disposable SQLite. Existing startup credential metadata discovery fails
against the private bus with ServiceUnknown; no keyring service can activate and
no credential read/write operation completes. No provider request or tool action
is invoked. The real desktop bus/configuration/storage is never supplied.

Each launch must remain alive for the bounded three-second observation, provide
selected Button/TextArea implementation and loaded plugin-path evidence, and emit
no unexpected QML/configuration diagnostics. Successful processes are explicitly
terminated and reaped; crashes, early exits, missing evidence and timeouts fail.
Installed runs reject build paths in both discovery traces and loaded mappings,
except their explicitly allowed installed prefix. Logs/maps remain under the
ignored `build/uqc104/{build-launch,install-launch}` directories.

## Verification commands

Commands below run from the AI repository. Dependency builds/staging require no
system installation. The launch harness requires local socket permission; the
sandbox denied private D-Bus socket binding, so those checks ran with permission.

```sh
task configure-tests
cmake --build build -j8
ctest --test-dir build -R 'runtime_controls_|canonical_qml_import_policy|semantic_qml_styling_policy' --output-on-failure
ctest --test-dir build --output-on-failure -j8
QT_QUICK_CONTROLS_STYLE=Holonight ctest --test-dir build -R 'Qml|CanonicalQmlModules|BottomAnchoredListView|MarkdownBlock' --output-on-failure -j4
QT_QUICK_CONTROLS_STYLE=Fusion ctest --test-dir build -R 'Qml|CanonicalQmlModules|BottomAnchoredListView|MarkdownBlock' --output-on-failure -j4
cmake --build build --target format-check qml-lint
scripts/check-qmltypes.sh build
cmake -S . -B build -DTIDY_JOBS=4
cmake --build build --target tidy
cmake -S . -B build -DCMAKE_INSTALL_PREFIX="$PWD/build/uqc104/stage"
cmake --install build/dependencies/config --prefix "$PWD/build/uqc104/stage"
cmake --install build/dependencies/qt --prefix "$PWD/build/uqc104/stage"
cmake --install build
python3 scripts/check-runtime-launches.py build/holonight-chat build/dependencies/prefix --logs build/uqc104/build-launch
python3 scripts/check-runtime-launches.py build/uqc104/stage/bin/holonight-chat build/uqc104/stage --forbid-path build --logs build/uqc104/install-launch
bash -n scripts/check-canonical-qml-imports.sh scripts/check-semantic-qml-styling.sh scripts/check-qmltypes.sh
task --list
git diff --check
```

Python AST parsing validates both new scripts. Local Markdown link validation
covers README and all UQC-104 documents. Activation validation compares the staged
service's Exec value to the configure-time staged executable path. CI workflow and
Taskfile shell blocks are syntax-checked after template expansion where needed.

## Results and limitations

Final verification passed with Qt 6.11.2, LLVM/clang-tidy 22.1.8 and CMake 4.4.3:

| Check | Result |
|---|---|
| Focused runtime/policies | Both styles and both policies plus independent negative fixtures pass |
| Complete CTest suite | 714 reported, 713 executed passes, one opt-in real Secret Service test skipped |
| Existing QML-related selection | 59/59 Holonight and 59/59 Fusion (includes the QML-facing model-role check) |
| Format, qmllint, qmltypes | Pass |
| Full tidy | Pass with `TIDY_JOBS=4` after fixes |
| Build launches | 4/4 pass with implementation/plugin evidence |
| Staged-install launches | 4/4 pass; no build discovery/library paths |
| Activation prefix | Staged service Exec equals the configured staged executable |
| Syntax, docs, whitespace | Python AST, shell syntax, 15 CI run blocks, Taskfile YAML/shell blocks, local links and diff check pass |

No container CI run is claimed by local verification; both workflows contain the
pinned dependency staging and the build job repeats dual-style and launch acceptance.

Earlier verification
caught a stale dependency cache, fixture module discovery leaking into source tests,
and a test pointer to a deferred-deleted provider form; all were fixed and retested.
An unrestricted-worker tidy attempt exited without diagnostics; limited workers
produced actionable checks. New test brace diagnostics and the rendering enum
holder's missing explicit destructor were corrected.

Live layer-shell, human-operated Hyprland/Sway, real third-party application and
ecosystem activation acceptance remain UQC-201. No system installation or other
consumer implementation is included. The real Secret Service integration test is
opt-in and deliberately skipped; no live credential test was requested.

## Published handoff

Implementation `286df4791c651ab8842f0d8f278eac9d4f803b81` was pushed to canonical
origin/main and confirmed by `git ls-remote origin refs/heads/main` on 2026-09-08.
The AI working tree was clean after publication. This documentation checkpoint
completes the repository-local handoff; the umbrella coordinator may pin its
published revision and mark UQC-104 Done. UQC-201 remains the ecosystem gate.
