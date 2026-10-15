#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
build=build/dev
prefix="$PWD/build/deps/prefix"
stage="$PWD/build/installed"
mkdir -p /work/output
    task configure PRESET=release
    cmake -S . -B build/release -DCMAKE_INSTALL_PREFIX="$stage"
    cmake --build build/release --parallel "$JOBS"
    mkdir -p "$stage"
    cp -a "$prefix/." "$stage/"
    cmake --install build/release --prefix "$stage"
    test -x "$stage/bin/holonight-chat"
    if find "$stage" -type f \( -name 'test_*' -o -name '*acceptance*' -o -path '*/fixtures/*' \) | grep .; then exit 1; fi
    python3 scripts/check-runtime-launches.py "$stage/bin/holonight-chat" "$stage" \
      --forbid-path "$PWD/build" --logs /work/output/install-launch
    python3 - "$prefix" /work/output/install-launch <<'PY_ASSERT'
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
    cp -a "$stage" /work/output/stage
