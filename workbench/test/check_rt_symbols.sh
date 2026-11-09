#!/bin/sh
# Audio-thread symbol audit (plan §9): the real-time object may only call
# the engines (ma_, tw_, ep_, the driver MIDI maps through wb_engine_) and a
# few pure libc/libm helpers. No allocation, locks, I/O or logging.
set -eu
object=$1
unexpected=$(nm -u "$object" | awk '{ print $NF }' \
    | grep -Ev '^(ma_|tw_|ep_|wb_engine_(midi|tick)$|memset$|memcpy$|sqrt$|sqrtf$|fabsf$|__stack_chk_fail$)' || true)
if [ -n "$unexpected" ]; then
    echo "unexpected symbols in the audio-thread object $object:" >&2
    echo "$unexpected" >&2
    exit 1
fi
echo "audio-thread symbols ok: $(nm -u "$object" | wc -l) references, all engine or pure libc/libm"
