#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
build=build/dev
prefix="$PWD/build/deps/prefix"
stage="$PWD/build/installed"
mkdir -p /work/output
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
    python3 scripts/check-runtime-launches.py "$build/holonight-chat" "$prefix" --logs /work/output/build-launch
