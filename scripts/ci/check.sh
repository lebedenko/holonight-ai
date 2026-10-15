#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
task test
task format-check
task tidy
task qml-lint
task qml-import-check
task qmltypes-check
reuse lint
bash scripts/ci/product-checks.sh
