# Development

`task build` and `task test` call CMake and CTest directly. Install dependencies first;
CMake reports missing packages and never downloads them. Use `cmake --preset dev
-DCMAKE_PREFIX_PATH=/path/to/prefix` for an additional installed provider prefix.
The development preset enables tests; `release` stages optimized builds.
clangd uses `build/dev/compile_commands.json`.

`task format` uses the pinned image. `task check` is the acceptance gate: fresh provider
builds, build, tests, format, static analysis, applicable QML checks and REUSE.
`task check:integration` adds Release staging and project runtime checks. GitHub invokes
the same launcher with `integration`; no additional validation is hidden in the workflow.
The only host launcher prerequisites are Python 3, Git, tar and Docker, plus Task for aliases.
Builds and clang-tidy use all available CPUs by default (`nproc` inside the container).
Set `JOBS=4 task check` to override the job count. Native component builds use Ninja
defaults; use `CMAKE_BUILD_PARALLEL_LEVEL=4 task build` to limit those.
Image, tool versions and the selected job count appear in the acceptance log. Logs and fresh builds stay under
ignored `build/acceptance/`; downloaded sources are reused under `build/sources/`.

Standalone checks automatically acquire the exact sources in `scripts/ci/providers.json`.
For simultaneous provider/consumer development, explicitly run
`task check -- --siblings /path/to/umbrella`. This uses current sibling files, including
edits. Final umbrella integration uses its gitlinks; never substitute sibling mode for it.
Provider sources are built with ordinary CMake into a disposable prefix. QMarkdown is
an independently installed package in the shared image and is not owned by our installer.
Chat requires QMarkdown >=1.18, packaged with `QMARKDOWN_WITH_KSYNTAXHIGHLIGHTING=ON`.
The shared image must be rebuilt and published before consumers update their digest.
Native builds never fetch, build, or install QMarkdown.

For a separate QMarkdown prefix, include it in `CMAKE_PREFIX_PATH` at configure time.
Its exported `QMarkdown_QML_IMPORT_PATH` is added to build engine and lint lookup
independently of HolonightQt. Installed launches keep relative/system QML imports;
custom external prefixes need `QML_IMPORT_PATH` and the platform shared-library path
configured (for example `LD_LIBRARY_PATH` on Linux). See the
[local migration SDD](docs/sdd/qmarkdown-migration/SPEC.md) and its umbrella contract.

Markdown integration tests use QMarkdown's public API and host-owned copy controls;
the runtime acceptance executable instantiates real Markdown rows in both transcript
hosts and requires highlighting availability. Installed startup checks assert that
the installed QMarkdown module and shared library load without build-directory imports.

Routine fixes need tests and a commit explanation. Substantial designs use one SDD;
cross-project work uses one umbrella contract note. Keep execution logs out of Git.
