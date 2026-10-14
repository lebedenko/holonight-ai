#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
lane=$1
mkdir /work/source
cp -a /input/. /work/source/
cd /work/source
export HOME=/work/build/home LC_ALL=C.UTF-8 TZ=UTC
export JOBS=${JOBS:-2} CMAKE_BUILD_PARALLEL_LEVEL=${JOBS:-2}
export QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_FORCE_STDERR_LOGGING=1
export QT_QPA_PLATFORMTHEME= QT_STYLE_OVERRIDE= XDG_RUNTIME_DIR=/work/runtime
mkdir -p "$HOME"
mkdir -m 700 "$XDG_RUNTIME_DIR"
if [ "$lane" = licensing ]; then
  reuse --version
  reuse lint
  exit
fi
mkdir -p build/ci-provenance
pacman -Q > build/ci-provenance/packages.txt
c++ --version > build/ci-provenance/compiler.txt
cmake --version > build/ci-provenance/cmake.txt
{ python3 --version; task --version; ninja --version; clang-format --version; clang-tidy --version; qmake6 --version; wayland-scanner --version; } > build/ci-provenance/versions.txt 2>&1
capture() {
  status=$?
  cp -a build/ci-provenance /output/
  if [ -d build/test/Testing ]; then cp -a build/test/Testing /output/; fi
  if [ -f build/test/compile_commands.json ]; then cp build/test/compile_commands.json /output/; fi
  if [ -d .cache/tooling ]; then cp -a .cache/tooling /output/tooling; fi
  exit "$status"
}
trap capture 0
if [ "$lane" = prepare ]; then
  export HOLONIGHT_CONFIG_SOURCE=/prepared/sources/holonight-config
  export HOLONIGHT_QT_SOURCE=/prepared/sources/holonight-qt
  if [ -d /prepared/deps ]; then cp -a /prepared/deps build/deps; fi
  task deps PRESET=test
  mkdir -p /prepared/deps
  cp -a build/deps/. /prepared/deps/
  cp -a build/ci-provenance /prepared/environment
  exit
fi
cp -a /prepared/deps build/deps
cp /prepared/provider-revisions.json build/ci-provenance/
cp build/deps/provider-revisions.tsv build/ci-provenance/
prefix=$PWD/build/deps/prefix
export LD_LIBRARY_PATH="$prefix/lib"
build=build/test
stage=/work/install
case "$lane" in
  build-test)
    python3 scripts/ci/test_launcher.py
    python3 scripts/ci/test_dependencies.py
    task build PRESET=test
    dbus-run-session -- ctest --test-dir "$build" --output-on-failure --no-tests=error
    cat "$build/Testing/Temporary/LastTest.log"
    for style in Holonight Fusion; do
      for scale in 1 1.25; do
        printf "Composer acceptance: style=%s scale=%s\n" "$style" "$scale"
        QT_QUICK_CONTROLS_STYLE="$style" QT_SCALE_FACTOR="$scale" \
          ctest --test-dir "$build" -R ChatComposerActionsQml --output-on-failure --no-tests=error
      done
      printf "QML acceptance: style=%s\n" "$style"
      QT_QUICK_CONTROLS_STYLE="$style" ctest --test-dir "$build" \
        -R 'Qml|CanonicalQmlModules|BottomAnchoredListView|MarkdownBlock' --output-on-failure --no-tests=error
    done
    python3 scripts/check-runtime-launches.py "$build/holonight-chat" "$prefix" --logs /output/build-launch
    ;;
  static-checks)
    task build PRESET=test
    task format-check
    cmake -S . -B "$build" -DTIDY_JOBS="$JOBS"
    cmake --build "$build" --target tidy
    cmake --build "$build" --target qml-lint
    task qml-import-check
    scripts/check-qmltypes.sh "$build"
    ;;
  release)
    task configure PRESET=release
    cmake -S . -B build/release -DCMAKE_INSTALL_PREFIX="$stage"
    cmake --build build/release --parallel "$JOBS"
    mkdir -p "$stage"
    cp -a "$prefix/." "$stage/"
    cmake --install build/release --prefix "$stage"
    test -x "$stage/bin/holonight-chat"
    if find "$stage" -type f \( -name 'test_*' -o -name '*acceptance*' -o -path '*/fixtures/*' \) | grep .; then exit 1; fi
    python3 scripts/check-runtime-launches.py "$stage/bin/holonight-chat" "$stage" \
      --forbid-path "$PWD/build" --logs /output/install-launch
    python3 - "$prefix" /output/install-launch <<'PY_ASSERT'
from pathlib import Path
import sys
for mode in ('default', 'environment', 'command-line', 'external-config'):
    for extension in ('log', 'maps'):
        evidence = Path(sys.argv[2]) / f'{mode}.{extension}'
        assert sys.argv[1] not in evidence.read_text(), f'{mode}: installed provider build path discovered'
        assert '/prepared/' not in evidence.read_text(), f'{mode}: preparation path discovered'
PY_ASSERT
    service="$stage/share/dbus-1/services/org.holonight.Chat.service"
    grep -Fx "Exec=$stage/bin/holonight-chat" "$service"
    test -x "$(sed -n 's/^Exec=//p' "$service")"
    cp -a "$stage" /output/stage
    ;;
  *) echo "Unknown lane: $lane" >&2; exit 2 ;;
esac
