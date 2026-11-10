/* Mamut Analog filter stage, written once for both lane widths: the 2x
 * source mix, mixer pressure, the ladder and the 7-tap output decimator.
 * ma_filter.c instantiates it for one voice (ML_W 1), ma_lanes.c for eight
 * pool voices per AVX2 vector (ML_W 8). Include after ma_voice_impl.h and
 * ma_lane_ops.h. */
#ifndef MA_FILTER_KERNEL_H
#define MA_FILTER_KERNEL_H

/* ---- the filter's coefficient ---- */

static inline ml_f ml_filter_envelope_amount(ml_f env_amount, ml_f velocity_filter) {
    return ml_clamp(env_amount + 0.25f * velocity_filter, 0.0f, 1.0f);
}

/* What the effective cutoff reads from the voice and the controls. */
typedef struct {
    ml_f envelope_level, envelope_amount;
    ml_f note_position, filter_keytrack, filter_cutoff_hz, poly_pressure;
    ml_f character, cutoff_octaves;     /* card character and its cutoff bias */
    ml_f sample_rate_hz;
} ml_cutoff_in;

/* The cutoff after the filter envelope, key tracking, poly pressure and card
 * character, inside 20 Hz .. min(0.42 fs, 20 kHz). */
static inline ml_f ml_effective_cutoff(const ml_cutoff_in *x) {
    ml_f envelope = 1.0f + 0.78f * x->envelope_level * x->envelope_amount;
    ml_f keytrack = 1.0f + (x->note_position - 60.0f) * (1.0f / 48.0f)
                         * x->filter_keytrack * 0.42f;
    keytrack = ml_clamp(keytrack, 0.55f, 1.35f);
    ml_f cutoff = x->filter_cutoff_hz * envelope * keytrack;
    ml_m poly = x->poly_pressure > 0.0f;
    if (ml_any(poly))
        cutoff = ml_if(poly, cutoff * ml_octave_ratio_small(0.25f * x->poly_pressure), cutoff);
    ml_m character = x->character > 0.0f;
    if (ml_any(character))
        cutoff = ml_if(character,
                       cutoff * ml_octave_ratio_small(x->character * x->cutoff_octaves), cutoff);
    ml_f cutoff_max = 0.42f * x->sample_rate_hz;
    cutoff_max = ml_if(cutoff_max > 20000.0f, 20000.0f, cutoff_max);
    return ml_clamp(cutoff, 20.0f, cutoff_max);
}

/* ---- the 2x stage ---- */

/* The ladder's state, and where its output ring and reset counts live. */
typedef struct {
    ml_f state[4];
    ml_f y4_guess;
    ml_f pressure_input;
    ml_f pressure_output;
    float *output_history;          /* 7 rows */
    uint8_t *output_pos;
    uint32_t *reset_count;          /* one per lane */
} ml_ladder;

/* The frame's inputs to the filter stage: front end, Glas grains, controls. */
typedef struct {
    ml_f guard1, guard2, vco2_level, noise_level;
    ml_f raster_mix, mozaik_mix;
    ml_f mozaik_gain[2], mozaik[2], raster[2];
    ml_f grain_mix, grain[2];
    ml_f mixer_pressure;
    ml_f filter_g, filter_drive, filter_resonance;
} ml_filter_in;

/* The analog source with raster and mozaik, each weighted by its mix. */
static inline ml_f ml_mix_2x(const ml_filter_in *in, ml_f analog, int step) {
    ml_f vco1_weight = ml_if(in->guard1 > 0.0f, 1.0f, 0.0f);
    ml_f vco2_weight = ml_if(in->guard2 > 0.0f, in->vco2_level, 0.0f);
    ml_f analog_weight = vco1_weight + vco2_weight + in->noise_level;
    analog_weight = ml_if(analog_weight < 1.0f, 1.0f, analog_weight);
    ml_m mozaik_on = (in->mozaik_mix > 0.0f) & (in->mozaik_gain[step] > 0.0f);
    ml_f mozaik = in->mozaik_mix * in->mozaik_gain[step] * in->mozaik[step];
    ml_f without_raster = ml_if(mozaik_on,
        (analog_weight * analog + mozaik) / (analog_weight + in->mozaik_mix), analog);
    ml_f weighted = analog_weight * analog + in->raster_mix * in->raster[step];
    ml_f weight = analog_weight + in->raster_mix;
    weighted = ml_if(mozaik_on, weighted + mozaik, weighted);
    weight = ml_if(mozaik_on, weight + in->mozaik_mix, weight);
    return ml_if(in->raster_mix == 0.0f, without_raster, weighted / weight);
}

static inline ml_f ml_pressure(ml_f source, ml_f pressure) {
    ml_m off = pressure == 0.0f;
    if (ml_all(off)) return source;
    ml_f gain = 1.0f + 5.0f * pressure;
    ml_f shaped = ml_sat(gain * source) / ml_sat(gain);
    return ml_if(off, source, source + pressure * (shaped - source));
}

[[gnu::const]] static inline ml_m ml_finite(ml_f x) {
    return (x >= -FLT_MAX) & (x <= FLT_MAX);
}

/* One 2x step: two passes of the solve around the four saturating stages. A
 * voice whose state leaves the finite range restarts from silence and is
 * counted. */
static inline ml_f ml_ladder_2x(ml_ladder *l, const ml_filter_in *in, ml_f input) {
    ml_f g = in->filter_g;
    ml_f input_gain = 1.0f + 3.0f * in->filter_drive * in->filter_drive;
    ml_f feedback = 4.65f * in->filter_resonance;
    ml_f y[4] = {}, v[4] = {}, y4 = l->y4_guess;
    for (int iteration = 0; iteration < 2; iteration++) {
        ml_f u = ml_sat(ml_fnmadd(feedback, y4, input_gain * input));
        for (int stage = 0; stage < 4; stage++) {
            ml_f d = ml_sat(u) - l->state[stage];
            v[stage] = d * g;
            y[stage] = ml_fmadd(d, g, l->state[stage]);
            u = y[stage];
        }
        y4 = y[3];
    }
    ml_m finite = ml_finite(y4);
    ml_f next[4] = {};
    for (int stage = 0; stage < 4; stage++) {
        next[stage] = y[stage] + v[stage];
        finite = finite & ml_finite(y[stage]) & ml_finite(v[stage]) & ml_finite(next[stage]);
    }
    for (int stage = 0; stage < 4; stage++) l->state[stage] = ml_keep(finite, next[stage]);
    l->y4_guess = ml_keep(finite, y4);
    ml_tally(l->reset_count, ml_not(finite));
    return ml_keep(finite, y[3]);
}

/* Two 2x mix/pressure/ladder steps, then the 7-tap output decimator. The
 * grains blend in only where their mix is positive, so other voices keep
 * their bits, -0 included. */
static inline ml_f ml_filter_block(ml_ladder *l, const ml_filter_in *in, const ml_f at_2x[2]) {
    ml_m grain = in->grain_mix > 0.0f;
    for (int step = 0; step < 2; step++) {
        ml_f source = ml_mix_2x(in, at_2x[step], step);
        if (ml_any(grain))
            source = ml_if(grain, source + in->grain_mix * (in->grain[step] - source), source);
        ml_f pressured = ml_pressure(source, in->mixer_pressure);
        l->pressure_input = source;
        l->pressure_output = pressured;
        ml_push(l->output_history, 7, l->output_pos, ml_ladder_2x(l, in, pressured));
    }
    return ml_halfband(l->output_history, 7, *l->output_pos, FILTER_HALFBAND_7,
                       FILTER_HALFBAND_7_PAIR, FILTER_HALFBAND_7_PAIRS,
                       FILTER_HALFBAND_7_CENTRE);
}

#endif
