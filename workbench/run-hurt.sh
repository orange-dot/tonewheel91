#!/bin/sh
# Build the Workbench if needed and open it on the Hurt session in the
# session root (falls back to the repository fixture, which has no sources).
#
#   workbench/run-hurt.sh [--single] [--light]
#
# Works from any directory. Extra arguments go to the Workbench.
set -eu

REPO=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$REPO"
make -s workbench
SESSION=${WB_HOME:-/data/tonewheel91-sessions}/hurt/hurt.mws
[ -f "$SESSION" ] || SESSION=workbench/test/fixtures/hurt-v2.mws
exec build/workbench "$SESSION" "$@"
