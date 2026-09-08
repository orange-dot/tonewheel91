/* Shared implementation boundary for the one-card and five-card outputs. */
#ifndef MA_INTERNAL_H
#define MA_INTERNAL_H

#include "mamutanalog.h"

[[nodiscard]] float ma_voice_tick_raw(ma_synth *s);
[[nodiscard]] ma_frame ma_output_render(ma_output_state *output,
    float mid, float side, float drive, float load, float master);

#endif
