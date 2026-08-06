# Development Setup

This project is developed with C++23, Qt 6, CMake, Ninja, and Task. Unlike `holonight-shell`, it
has no Wayland/Hyprland dependency — `holonight-chat` is a normal desktop Qt Quick window, so
development and tests work under any Qt platform plugin, including `offscreen`.

## CI Baseline (Arch, via Dockerfile.ci)

CI runs inside the `ghcr.io/lebedenko/holonight-ai-ci` container, built from `Dockerfile.ci`:

```text
base-devel cmake git ninja pkgconf qt6-base qt6-declarative qt6-svg md4c syntax-highlighting
wayland-protocols gtest libsecret clang dbus
```

Rebuild and push it (maintainers only) with `task build-ci-image` or by pushing changes to
`Dockerfile.ci` on `main` (see `.github/workflows/publish-ci-image.yml`).

## Configure Presets

`CMakePresets.json` mirrors the main local workflows:

```bash
cmake --preset debug
cmake --build --preset debug

cmake --preset debug-tests
cmake --build --preset debug-tests
ctest --preset debug-tests

cmake --preset coverage
cmake --build --preset coverage
```

`task` remains the preferred day-to-day interface — it also builds and installs the sibling
`../holonight-qt` checkout that these presets don't handle on their own.

## holonight-qt Dependency

This project depends on `../holonight-qt` (a sibling checkout) for the `HolonightQt` CMake
package and the `Holonight` QML module. `task build:qt-dependency` builds it and installs it to
`/tmp/holonight-qt-prefix`; every other `task` target depends on this. In CI, the workflow
checks out `lebedenko/holonight-qt` and installs it to `/usr` instead.
