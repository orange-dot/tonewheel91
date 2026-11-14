#!/bin/sh
# Audio-thread symbol audit (docs/patchbench-plan.md §5.1): pb_rt.o and the
# sequencer it runs (pb_seq.o) may call the Mamut engine (ma_) and pure libm
# only.  No
# allocation, locks, I/O or logging.  An LTO object is linked through the
# plugin first, so its references are real.
set -eu
object="$*"
tmp=$(mktemp)
# One relocatable link of all the objects: references between them resolve,
# what is left is what the audio thread reaches outside its own code.
# PB_AUDIT_LINK is the compiler with the Makefile's AUDIT_LDFLAGS, so LTO
# objects link through the plugin (as test/check_core_symbols.sh does).
${PB_AUDIT_LINK:-cc} -r -nostdlib "$@" -o "$tmp"
refs=$(nm -u "$tmp" | awk '{ print $NF }')
rm -f "$tmp"
unexpected=$(printf '%s\n' "$refs" | grep -v '^$' \
    | grep -Ev '^(ma_|memset$|memcpy$|sqrt$|sqrtf$|fabsf$|__stack_chk_fail$)' || true)
if [ -n "$unexpected" ]; then
    echo "unexpected symbols in the audio-thread object $object:" >&2
    echo "$unexpected" >&2
    exit 1
fi
echo "patchbench audio-thread symbols ok: $(printf '%s\n' "$refs" | grep -vc '^$') references, all engine or pure libm"
