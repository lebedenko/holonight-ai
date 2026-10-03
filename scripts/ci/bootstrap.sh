#!/bin/sh
set -eu
python3 /input/scripts/ci/prepare-tools.py
exec setpriv --reuid="$CI_UID" --regid="$CI_GID" --clear-groups \
  /bin/sh /input/scripts/ci/lane.sh "$1"
