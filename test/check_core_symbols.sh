#!/bin/sh
set -eu

compiler=$1
output=$2
shift 2

"$compiler" -r "$@" -o "$output"
unexpected=$(nm -u "$output" | awk '{ print $NF }' | grep -Ev '^memset$' || true)
if [ -n "$unexpected" ]; then
    echo "unexpected unresolved core symbols:" >&2
    echo "$unexpected" >&2
    exit 1
fi
