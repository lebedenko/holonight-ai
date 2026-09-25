# Repository Guidelines

Use Conventional Commits for every new commit: `type(scope): imperative summary`, or `type: imperative summary` when a scope adds no clarity.

## Project Structure & Module Organization

`apps/chat/` contains the executable-owned code for the `holonight-chat` binary — entry point,
app wiring (`app/ChatApplication.{h,cpp}`), and QML module registration. Reusable C++ targets live
under `src/`: `src/domain/`, `src/application/`, `src/providers/`, `src/persistence/`,
`src/credentials/`, `src/platform/` — see `CLAUDE.md` for what each owns. Implemented modules use static libraries. QML sources live at the top level
under `qml/shared/`, `qml/workspace/`, `qml/quickpanel/`. Design/architecture notes are in `docs/`
(`docs/high-level-project-idea.md`, `docs/adr/`). Unit tests live in `tests/`.

## Build, Test, and Development Commands

Use `task` as the primary workflow:

- `task configure` configures a Debug CMake/Ninja build and writes `compile_commands.json`.
- `task build` builds `build/holonight-chat`.
- `task run` builds and launches the window.
- `task test` configures tests, builds them, and runs `ctest --output-on-failure`.
- `task coverage` generates an HTML coverage report in `build/coverage/index.html`.
- `task format`, `task format-check`, `task tidy`, and `task qml-lint` run clang-format,
  clang-tidy, and qmllint checks.
- `task clean` removes `build/` after confirmation.

All of the above depend on `task build:qt-dependency`, which builds and installs the sibling
`../holonight-qt` checkout to `build/dependencies/prefix`.

## Coding Style & Naming Conventions

Follow the checked-in `.clang-format` and `.clang-tidy` rules (copied from `holonight-shell`):
Google-based formatting, 2-space indentation, 120-column limit, and C++23. Include order is local
headers first, then Qt headers, then system headers. Naming conventions are `CamelCase` for
classes/types, `camelBack` for functions/methods, `lower_case` for members, and `lower_case_` for
private members. QML files should be feature-scoped, per-directory under `qml/`, registered in
`apps/chat/CMakeLists.txt`.

## Testing Guidelines

Tests use GTest for C++, driven through CTest, with QML behavior tests under `tests/qml/` and isolated application acceptance under
`tests/runtime/`. Run `task test` before submitting behavior changes; run
`task qml-lint` for QML changes.

## Commit & Pull Request Guidelines

Follow Conventional Commit-style prefixes such as `feat:`, `docs:`, and `chore:`, matching
`holonight-shell`. Keep subjects imperative and scoped to the change. Pull requests should
describe the user-visible change, list validation commands run, and link relevant issues or ADRs.

## Agent-Specific Notes

Preserve the application/rendering metatype extraction, merge and singleton registration in
`apps/chat/CMakeLists.txt`. The acceptance executable shares the QML/assets inventory but
registers only test doubles and rendering types. Keep its executable and generated QML module
in their separate build directory so source-based tests cannot discover fixture resources.

Runtime standard controls use `import QtQuick.Controls as Controls`; palette/primitives and
composites remain explicit `Holonight.Core` and `Holonight.Controls` APIs. The executable embeds
`:/qtquickcontrols2.conf` with `Style=Holonight`. Environment, `-style Fusion` and
`QT_QUICK_CONTROLS_CONF` overrides remain supported. The build executable uses its configured
dependency path; installed execution discovers `../lib/qt6/qml` relative to the executable
(using the configured install libdir). Activation paths are fixed at CMake configure time.
See [UQC-104 acceptance](docs/sdd/unified-qtquick-controls/SPEC.md).
