#!/bin/sh
# Link the core objects into one relocatable object and allow only memset
# as an unresolved symbol. $1 is the compiler command, flags included (under
# LTO it carries the flags that make `-r` run the LTO link).
set -eu

compiler=$1
output=$2
shift 2

$compiler -r "$@" -o "$output"
if ! nm --defined-only "$output" | grep -q ' T tw_organ_init$'; then
    echo "core-symbol audit: $output defines no core code" >&2
    exit 1
fi
unexpected=$(nm -u "$output" | awk '{ print $NF }' | grep -Ev '^memset$' || true)
if [ -n "$unexpected" ]; then
    echo "unexpected unresolved core symbols:" >&2
    echo "$unexpected" >&2
    exit 1
fi
