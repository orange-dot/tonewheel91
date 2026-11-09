/* Mamut Analog scalar filter stage: mixer pressure, the 2x ladder, the
 * 7-tap output decimator and the output render. The vector lanes run
 * their own copy (ml_kernel in ma_lanes.c). */
#include "ma_voice_impl.h"

#if !defined(MA_SOURCE_EVIDENCE)
static float apply_mixer_pressure(float source, float pressure) {
    if (pressure == 0.0f) return source;
    float gain = 1.0f + 5.0f * pressure;
    float shaped = ma_sat(gain * source) / ma_sat(gain);
    return source + pressure * (shaped - source);
}

static void reset_ladder(ma_ladder *ladder) {
    for (int stage = 0; stage < 4; stage++) ladder->state[stage] = 0.0f;
    ladder->y4_guess = 0.0f;
    ladder->reset_count++;
}

static float render_ladder_2x(ma_synth *s,
                              const ma_render_controls *controls,
                              float input, float filter_g) {
    ma_ladder *ladder = &s->ladder;
    float y[4] = { 0 };
    float v[4] = { 0 };
    float next_state[4] = { 0 };
    float y4_guess = ladder->y4_guess;
    float input_gain = 1.0f + 3.0f * controls->filter_drive
                     * controls->filter_drive;
    float feedback = 4.65f * controls->filter_resonance;
    for (int iteration = 0; iteration < 2; iteration++) {
        float u = ma_sat(ma_fnmadd(feedback, y4_guess, input_gain * input));
        for (int stage = 0; stage < 4; stage++) {
            float d = ma_sat(u) - ladder->state[stage];
            v[stage] = d * filter_g;
            y[stage] = ma_fmadd(d, filter_g, ladder->state[stage]);
            u = y[stage];
        }
        y4_guess = y[3];
    }
    bool finite = y4_guess >= -FLT_MAX && y4_guess <= FLT_MAX;
    for (int stage = 0; stage < 4; stage++) {
        next_state[stage] = y[stage] + v[stage];
        finite = finite
              && y[stage] >= -FLT_MAX && y[stage] <= FLT_MAX
              && v[stage] >= -FLT_MAX && v[stage] <= FLT_MAX
              && next_state[stage] >= -FLT_MAX
              && next_state[stage] <= FLT_MAX;
    }
    if (!finite) {
        reset_ladder(ladder);
        return 0.0f;
    }
    for (int stage = 0; stage < 4; stage++)
        ladder->state[stage] = next_state[stage];
    ladder->y4_guess = y4_guess;
    return y[3];
}

static void push_filter_history(ma_ladder *ladder, float sample) {
    ladder->output_history[ladder->output_pos] = sample;
    ladder->output_pos++;
    if (ladder->output_pos == 7) ladder->output_pos = 0;
}

/* Pairs 0/6 and 2/4 added first and fused, then the centre tap 3. */
static float decimate_filter(const ma_ladder *ladder) {
    const float *x = ladder->output_history;
    int position = ladder->output_pos;
    float output = 0.0f;
    for (int k = 0; k < FILTER_HALFBAND_7_PAIRS; k++) {
        int tap = FILTER_HALFBAND_7_PAIR[k];
        int near = position - 1 - tap, far = position - 7 + tap;
        if (near < 0) near += 7;
        if (far < 0) far += 7;
        output = ma_fmadd(FILTER_HALFBAND_7[tap], x[near] + x[far], output);
    }
    int centre = position - 1 - FILTER_HALFBAND_7_CENTRE;
    if (centre < 0) centre += 7;
    return ma_fmadd(FILTER_HALFBAND_7[FILTER_HALFBAND_7_CENTRE], x[centre], output);
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
    for (int step = 0; step < 2; step++) {
        float source = mix_source_2x(&f->controls, at_2x[step], f->guard1,
                                     f->guard2, f->mozaik_gain[step],
                                     f->mozaik[step], f->raster[step]);
        float grain_mix = s->glas.frame_grain_mix;
        if (grain_mix > 0.0f)
            source += grain_mix * (s->glas.grain_out[step] - source);
        float pressured = apply_mixer_pressure(source,
                                                f->controls.mixer_pressure);
        s->ladder.pressure_input = source;
        s->ladder.pressure_output = pressured;
        push_filter_history(&s->ladder,
                            render_ladder_2x(s, &f->controls, pressured,
                                             f->filter_g));
    }
    return decimate_filter(&s->ladder);
}
#endif
