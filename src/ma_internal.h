/* Shared implementation boundary for the one-card and five-card outputs. */
#ifndef MA_INTERNAL_H
#define MA_INTERNAL_H

#include "mamutanalog.h"

[[nodiscard]] float ma_voice_tick_raw(ma_synth *s);
[[nodiscard]] ma_frame ma_output_render(ma_output_state *output,
    float mid, float side, float drive, float load, float master);

void ma_voice_tick_controls(ma_synth *s);
void ma_voice_snap_controls(ma_synth *s);
void ma_voice_patch_controls(ma_synth *s, const ma_patch *patch);
[[nodiscard]] float ma_voice_tick_detuned(ma_synth *s, float cents);

/* Vector lanes (ma_lanes.c): eight pool voices per AVX2 vector. Gather the
 * awake slots after begin, tick once per frame, scatter before the slots'
 * ma_synth state is read again. Lanes without a gathered voice output 0. */
[[nodiscard]] bool ma_lanes_supported(void);
void ma_lanes_begin(ma_voice_lanes *b);
void ma_lanes_gather(ma_voice_lanes *b, unsigned lane, const ma_synth *s);
void ma_lanes_scatter(const ma_voice_lanes *b, unsigned lane, ma_synth *s);
void ma_lanes_tick(ma_voice_lanes *b, ma_synth *const voice[MA_LANES],
                   const float detune_cents[MA_LANES], float out[MA_LANES]);
#endif
