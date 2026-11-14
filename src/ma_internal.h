/* Shared implementation boundary for the one-card and five-card outputs. */
#ifndef MA_INTERNAL_H
#define MA_INTERNAL_H

#include "mamutanalog.h"

[[nodiscard]] float ma_voice_tick_raw(ma_synth *s);
[[nodiscard]] ma_frame ma_output_render(ma_output_state *output, ma_rack *rack,
    float mid, float side, float drive, float load, float master);
/* ma_output_render in two halves around the rack, for a block-wise host:
 * front = body and DC block, back = master and safety. */
[[nodiscard]] ma_frame ma_output_front(ma_output_state *output, float mid, float side,
    float drive, float load, bool *finite);
[[nodiscard]] ma_frame ma_output_back(ma_output_state *output, ma_frame x, float master,
    bool finite);
/* AVX2 wavefront of a settled EQ block (ma_rack_lanes.c): the active bands
 * of `b` over n frames, in to out, the same float operations per band and
 * sample as the scalar tick. False when the CPU or build has no AVX2. */
[[nodiscard]] bool ma_rack_eq_wavefront(ma_rack_block *b, uint8_t active, const ma_frame *in,
    ma_frame *out, size_t n);

void ma_voice_tick_controls(ma_synth *s);
void ma_voice_snap_controls(ma_synth *s);
void ma_voice_patch_controls(ma_synth *s, const ma_patch *patch);
[[nodiscard]] float ma_voice_tick_detuned(ma_synth *s, float cents);
/* The VCO-2 pitch law alone (docs/ma-vco-plan.md), for its referee: the
 * frequency one trimmed core gives for an ideal frequency `hz`, `octaves`
 * from A4. */
[[nodiscard]] float ma_voice_vco_law_hz(float hz, float octaves, float extra_hz,
                                        float dead_s, float temp_k);

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
