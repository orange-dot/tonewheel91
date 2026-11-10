/* Mamut Analog filter for one voice: the ladder prewarp, the filter kernel
 * (ma_filter_kernel.h) at width one, and the output render. */
#include "ma_voice_impl.h"
#define ML_W 1
#include "ma_lane_ops.h"

float ma_filter_prewarp(float cutoff_hz, float sample_rate_hz) {
    return ml_prewarp(cutoff_hz, sample_rate_hz);
}

#if !defined(MA_SOURCE_EVIDENCE)
#include "ma_filter_kernel.h"

float ma_voice_filter_envelope_amount(const ma_synth *s,
                                      const ma_render_controls *controls) {
    return ml_filter_envelope_amount(controls->filter_env_amount,
                                     ma_velocity_filter(s->velocity));
}

float ma_voice_effective_filter_cutoff(const ma_synth *s,
                                       const ma_render_controls *controls,
                                       float envelope_amount,
                                       float note_position) {
    const ml_cutoff_in x = {
        .envelope_level = s->filter_envelope.level,
        .envelope_amount = envelope_amount,
        .note_position = note_position,
        .filter_keytrack = controls->filter_keytrack,
        .filter_cutoff_hz = controls->filter_cutoff_hz,
        .poly_pressure = controls->poly_pressure,
        .character = s->character.smoother.current,
        .cutoff_octaves = s->character.cutoff_octaves,
        .sample_rate_hz = s->sample_rate_hz,
    };
    return ml_effective_cutoff(&x);
}

ma_frame ma_voice_render_output(ma_synth *s,
                                const ma_render_controls *controls,
                                float mono) {
    return ma_output_render(&s->output, mono, 0.0f, controls->body_drive,
                             controls->body_load_ratio, s->master_level);
}

/* Two 2x mix/pressure/ladder steps and the 7-tap output decimator. */
float ma_voice_filter(ma_synth *s, const ma_voice_front *f,
                      const float at_2x[2]) {
    ma_ladder *ladder = &s->ladder;
    const ma_render_controls *c = &f->controls;
    ml_ladder l = {
        .state = { ladder->state[0], ladder->state[1], ladder->state[2],
                   ladder->state[3] },
        .y4_guess = ladder->y4_guess,
        .pressure_input = ladder->pressure_input,
        .pressure_output = ladder->pressure_output,
        .output_history = ladder->output_history,
        .output_pos = &ladder->output_pos,
        .reset_count = &ladder->reset_count,
    };
    const ml_filter_in in = {
        .guard1 = f->guard1,
        .guard2 = f->guard2,
        .vco2_level = c->vco2_level,
        .noise_level = c->noise_level,
        .raster_mix = c->raster_mix,
        .mozaik_mix = c->mozaik_mix,
        .mozaik_gain = { f->mozaik_gain[0], f->mozaik_gain[1] },
        .mozaik = { f->mozaik[0], f->mozaik[1] },
        .raster = { f->raster[0], f->raster[1] },
        .grain_mix = s->glas.frame_grain_mix,
        .grain = { s->glas.grain_out[0], s->glas.grain_out[1] },
        .mixer_pressure = c->mixer_pressure,
        .filter_g = f->filter_g,
        .filter_drive = c->filter_drive,
        .filter_resonance = c->filter_resonance,
    };
    float output = ml_filter_block(&l, &in, at_2x);
    for (int stage = 0; stage < 4; stage++) ladder->state[stage] = l.state[stage];
    ladder->y4_guess = l.y4_guess;
    ladder->pressure_input = l.pressure_input;
    ladder->pressure_output = l.pressure_output;
    return output;
}
#endif
