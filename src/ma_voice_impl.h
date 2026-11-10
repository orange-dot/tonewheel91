/* Private to the Mamut Analog voice modules, their white-box tests and
 * probes: constants, internal types and the per-sample primitives that
 * scalar and lane code share. ma_internal.h stays the boundary for the
 * pool, bank and output. */
#ifndef MA_VOICE_IMPL_H
#define MA_VOICE_IMPL_H

#include "ma_internal.h"

/* Explicit fused multiply-add (operator decision 2026-11-09, docs/ma3-fma-20261109.md).
 * fma is exactly specified by IEEE 754 (one rounding), so scalar code and
 * the lanes (_mm256_fmadd_ps) still agree bit for bit, on any host with
 * hardware FMA. -ffp-contract=off stays: nothing fuses unless written here.
 * The core has no libm, so hardware FMA is required. */
#if !defined(__FMA__) && !defined(__ARM_FEATURE_FMA) && !defined(__FP_FAST_FMAF)
#error "Mamut Analog needs hardware FMA (x86-64: -march=x86-64-v3)"
#endif
static inline float ma_fmadd(float a, float b, float c) { return __builtin_fmaf(a, b, c); }  /* a b + c */
static inline float ma_fnmadd(float a, float b, float c) { return __builtin_fmaf(-a, b, c); } /* c - a b */

static constexpr uint64_t NOISE_SEED = UINT64_C(0x4d414e4f49534531);
#if !defined(MA_SOURCE_EVIDENCE)
static constexpr float LN1000 = 6.907755278982137f;
#endif
static constexpr uint32_t MOZAIK_SLOPE_MIN_Q32 = UINT32_C(0x73333333);
static constexpr uint32_t MOZAIK_SLOPE_MAX_Q32 = UINT32_C(0xc0000000);
static constexpr float MOZAIK_GOLDEN_CONTRAST = 1.618034f;
static constexpr float Q32_ONE_F = 0x1p32f;
static constexpr float DC_BLOCK_10_HZ = 62.83185307179586f;
static constexpr float SAFETY_KNEE = 0.98f;
static constexpr float SAFETY_RANGE = 0.02f;
static constexpr float LFO_RATE_DEFAULT_HZ = 1.0f;
static constexpr float BCS_TAU = 6.283185307179586f;
static constexpr float BCS_STATE_CEILING = 32.0f;
static constexpr int BCS_INTEGRATION_STEPS = 4;

typedef struct {
    float sigma;
    uint32_t q32;
} mozaik_detent;

typedef struct {
    ma_vco_controls vco1;
    ma_vco_controls vco2;
    float vco2_level;
    float vco2_fine_cents;
    float sync_amount;
    float sync_softness;
    float crossmod_amount;
    float noise_level;
    float raster_mix;
    float raster_position;
    float raster_warp;
    float bcs_amount;
    float bcs_regime;
    float mozaik_mix;
    uint32_t mozaik_slope_q32;
    float mozaik_contrast;
    float mozaik_drift;
    float mixer_pressure;
    float filter_cutoff_hz;
    float filter_resonance;
    float filter_drive;
    float filter_env_amount;
    float filter_keytrack;
    float pitch_bend_semitones;
    float poly_pressure;
    float lfo_depth;
    float lfo_rate_hz;
    float body_drive;
    float body_load_ratio;
    float width;
    float crossfeed;
} ma_render_controls;

static constexpr mozaik_detent MOZAIK_DETENT[5] = {
    { .sigma = 0x1.3c6ef4p-1f, .q32 = UINT32_C(0x9e3779b9) },
    { .sigma = 0.5f,          .q32 = UINT32_C(0x80000000) },
    { .sigma = 0.6f,          .q32 = UINT32_C(0x9999999a) },
    { .sigma = 0.625f,        .q32 = UINT32_C(0xa0000000) },
    { .sigma = 2.0f / 3.0f,   .q32 = UINT32_C(0xaaaaaaab) },
};

static constexpr float EXP2_SMALL[6] = {
    1.000000000e+00f, 6.931471825e-01f, 2.402265072e-01f,
    5.550410971e-02f, 9.618128650e-03f, 1.333355787e-03f,
};

static constexpr float SEMITONE_RATIO[12] = {
    1.000000000e+00f, 1.059463143e+00f, 1.122462034e+00f,
    1.189207077e+00f, 1.259921074e+00f, 1.334839821e+00f,
    1.414213538e+00f, 1.498307109e+00f, 1.587401032e+00f,
    1.681792855e+00f, 1.781797409e+00f, 1.887748599e+00f,
};

static constexpr float OSC_HALFBAND_31[31] = {
    0.0f, 0.0f, 4.103266983e-04f, 0.0f,
   -2.230306389e-03f, 0.0f, 7.100922987e-03f, 0.0f,
   -1.791719720e-02f, 0.0f, 4.010779038e-02f, 0.0f,
   -9.010776132e-02f, 0.0f, 3.126362264e-01f, 5.0e-01f,
    3.126362264e-01f, 0.0f, -9.010776132e-02f, 0.0f,
    4.010779038e-02f, 0.0f, -1.791719720e-02f, 0.0f,
    7.100922987e-03f, 0.0f, -2.230306389e-03f, 0.0f,
    4.103266983e-04f, 0.0f, 0.0f,
};
/* Symmetric, h[t] = h[30 - t], with zero odd taps: the decimators sum the
 * seven nonzero pairs t, 30 - t (smallest coefficient first) and then the
 * centre tap 15 (docs/ma3-fir-fold-20261109.md). */
enum { OSC_HALFBAND_31_PAIRS = 7, OSC_HALFBAND_31_CENTRE = 15 };
static constexpr uint8_t OSC_HALFBAND_31_PAIR[OSC_HALFBAND_31_PAIRS] = {
    2, 4, 6, 8, 10, 12, 14,
};

#if !defined(MA_SOURCE_EVIDENCE)
static constexpr float FILTER_HALFBAND_7[7] = {
   -4.674123600e-02f, 0.0f, 2.967412472e-01f, 5.0e-01f,
    2.967412472e-01f, 0.0f, -4.674123600e-02f,
};
/* Pairs 0/6 and 2/4, then the centre tap 3. */
enum { FILTER_HALFBAND_7_PAIRS = 2, FILTER_HALFBAND_7_CENTRE = 3 };
static constexpr uint8_t FILTER_HALFBAND_7_PAIR[FILTER_HALFBAND_7_PAIRS] = { 0, 2 };
#endif

static constexpr float PREWARP_CHEB[7] = {
    2.287037611e+00f, -5.843005180e-01f, 2.182650715e-01f,
   -3.681783006e-02f,  1.203418057e-02f, -2.262040973e-03f,
    6.386489258e-04f,
};

static constexpr uint64_t PHASE_ONE_Q48 = UINT64_C(1) << 48;

static constexpr uint64_t PHASE_MASK_Q48 = (UINT64_C(1) << 48) - 1;

/* A sync reset is tracked until VCO2 has run one edge zone (eight steps)
 * past it, and never beyond half a turn: no zone is wider than 0.49. Past
 * that run every edge within a zone behind the phase was crossed after the
 * reset. */
static constexpr uint64_t SYNC_RUN_LIMIT_Q48 = UINT64_C(1) << 47;
static inline uint64_t sync_run_limit(uint64_t step_q48) {
    uint64_t limit = 8 * step_q48;
    return limit < SYNC_RUN_LIMIT_Q48 ? limit : SYNC_RUN_LIMIT_Q48;
}

/* The two smoothed edges of a half period take 1.25 * edge of the 50% pulse's
 * area, so the triangle integrator runs 1 / (1 - 1.25 * edge) faster
 * (ma-constants.md 3.1). */
static constexpr float TRIANGLE_EDGE_LOSS = 1.25f;

static constexpr ma_adsr AMP_ADSR_DEFAULT = {
    .attack_ms = 600.0f,
    .decay_ms = 1600.0f,
    .sustain = 0.82f,
    .release_ms = 3000.0f,
};

static constexpr ma_adsr FILTER_ADSR_DEFAULT = {
    .attack_ms = 350.0f,
    .decay_ms = 1800.0f,
    .sustain = 0.50f,
    .release_ms = 2600.0f,
};

/* These destinations are held over all eight source substeps. */
typedef struct {
    ma_vco_controls vco1;
    ma_vco_controls vco2;
    unsigned vco1_active;
    unsigned vco2_active;
    float vco1_divisor;
    float vco2_divisor;
    float mixer_divisor;
    float rate_hz;
    float vco1_hz;
    float vco1_step;
    uint64_t vco1_step_q48;
    float vco1_seed_step;           /* the unmodulated step, cross-mod or not */
    float vco1_triangle_gain;
    float vco1_triangle_step;
    float vco2_hz;
    float vco2_step;
    uint64_t vco2_step_q48;
    float vco2_triangle_gain;
    float vco2_triangle_step;
    float envelope_ratio;
    float sync;
} ma_source_frame;

typedef struct {
    float hopf_mu;
    float hopf_nonlinearity;
    float duffing_frequency_ratio;
    float duffing_damping;
    float duffing_edge;
    float duffing_cubic_stiffness;
    float duffing_cubic_damping;
    float duffing_drive;
    float duffing_feedback;
    float hopf_mix;
    float duffing_mix;
    float output_gain;
} ma_bcs_parameters;

typedef struct {
    float hopf_re;
    float hopf_im;
    float duffing_x;
    float duffing_v;
} ma_bcs_vector;

static constexpr ma_bcs_parameters BCS_LANDMARK[4] = {
    {
        .hopf_mu = 18.0f, .hopf_nonlinearity = 32.0f,
        .duffing_frequency_ratio = 1.0f, .duffing_damping = 0.18f,
        .duffing_edge = 0.10f, .duffing_cubic_stiffness = 0.35f,
        .duffing_cubic_damping = 1.15f, .duffing_drive = 0.030f,
        .duffing_feedback = 0.004f, .hopf_mix = 1.0f,
        .duffing_mix = 0.16f, .output_gain = 0.58f,
    },
    {
        .hopf_mu = 18.0f, .hopf_nonlinearity = 32.0f,
        .duffing_frequency_ratio = 1.0f, .duffing_damping = 0.18f,
        .duffing_edge = 0.42f, .duffing_cubic_stiffness = 0.35f,
        .duffing_cubic_damping = 1.15f, .duffing_drive = 0.105f,
        .duffing_feedback = 0.010f, .hopf_mix = 1.0f,
        .duffing_mix = 0.40f, .output_gain = 0.54f,
    },
    {
        .hopf_mu = 16.0f, .hopf_nonlinearity = 32.0f,
        .duffing_frequency_ratio = 0.42f, .duffing_damping = 0.10f,
        .duffing_edge = 0.40f, .duffing_cubic_stiffness = 0.48f,
        .duffing_cubic_damping = 1.40f, .duffing_drive = 0.050f,
        .duffing_feedback = 0.002f, .hopf_mix = 0.05f,
        .duffing_mix = 1.35f, .output_gain = 0.60f,
    },
    {
        .hopf_mu = 18.0f, .hopf_nonlinearity = 32.0f,
        .duffing_frequency_ratio = 1.0f, .duffing_damping = 0.18f,
        .duffing_edge = 0.36f, .duffing_cubic_stiffness = 0.35f,
        .duffing_cubic_damping = 1.15f, .duffing_drive = 0.090f,
        .duffing_feedback = 0.008f, .hopf_mix = 1.0f,
        .duffing_mix = 0.36f, .output_gain = 0.56f,
    },
};

enum { WAVE_SAW = 1, WAVE_PULSE = 2, WAVE_TRIANGLE = 4,
       WAVE_ALL = WAVE_SAW | WAVE_PULSE | WAVE_TRIANGLE };

#if !defined(MA_SOURCE_EVIDENCE)
typedef struct {
    ma_render_controls controls;
    ma_source_frame source;
    float character;
    float note_position;
    float base_hz;
    float filter_g;
    float guard1;
    float guard2;
    float noise;
    float mozaik_gain[2];
    float mozaik[2];
    float raster[2];
} ma_voice_front;
#endif

static inline float finite_or(float value, float fallback) {
    return value >= -FLT_MAX && value <= FLT_MAX ? value : fallback;
}

static inline float clamp_control(float value, float low, float high, float fallback) {
    value = finite_or(value, fallback);
    return value < low ? low : value > high ? high : value;
}

static inline float clamp_signal(float value, float low, float high) {
    return value < low ? low : value > high ? high : value;
}

static inline float raw_triangle(float phase) {
    return 1.0f - 4.0f * tw_fabsf(phase - 0.5f);
}

/* The triangle integrator's gain for a phase step, and its increment for a
 * unit pulse. */
static inline float triangle_gain_for(float step) {
    float edge = 8.0f * step;
    if (edge > 0.49f) edge = 0.49f;
    return 1.0f / ma_fnmadd(TRIANGLE_EDGE_LOSS, edge, 1.0f);
}

static inline float triangle_step_for(float step, float gain) {
    return 4.0f * step * gain;
}

/* Where the integrator's steady trajectory is at a phase, outside the edges.
 * The state at a substep holds the pulse summed up to that substep's start,
 * which trails the triangle by half a step. */
static inline float triangle_seed(float phase, float step, float gain) {
    return clamp_signal(gain * raw_triangle(phase - 0.5f * step), -1.0f, 1.0f);
}

static inline float wrap_phase(float phase) {
    if (phase >= 1.0f) phase -= 1.0f;
    if (phase < 0.0f) phase += 1.0f;
    return phase;
}

static inline float phase_turns(uint64_t phase_q48) {
    return (float)(uint32_t)(phase_q48 >> 16) * 0x1p-32f;
}

static inline float smoothstep5(float x) {
    return x * x * x * ma_fmadd(x, ma_fmadd(6.0f, x, -15.0f), 10.0f);
}

/* tw_sin_turns and tw_sat with FMA. The organ and EP keep the tw_ forms. */
static inline float ma_sin_turns(float p) {
    float t = (p < 0.25f) ? p : (p < 0.75f) ? 0.5f - p : p - 1.0f;
    float s = t * t;
    float poly = ma_fmadd(s, 42.0586939f, -76.7058597f);
    poly = ma_fmadd(s, poly, 81.6052493f);
    poly = ma_fmadd(s, poly, -41.3417022f);
    poly = ma_fmadd(s, poly, 6.28318531f);
    return t * poly;
}
static inline float ma_sat(float x) {
    x = (x > 3.0f) ? 3.0f : (x < -3.0f) ? -3.0f : x;
    return x * ma_fmadd(x, x, 27.0f) / ma_fmadd(9.0f * x, x, 27.0f);
}

static inline float octave_ratio_small(float octaves) {
    octaves = clamp_signal(octaves, -0.25f, 0.25f);
    float value = EXP2_SMALL[5];
    for (int i = 4; i >= 0; i--) value = value * octaves + EXP2_SMALL[i];
    return value;
}

static inline float phase_step(float frequency_hz, float sample_rate_hz) {
    float step = frequency_hz / sample_rate_hz;
    return clamp_signal(step, 0.0f, 0.45f);
}

static inline uint64_t phase_step_q48(float step) {
    return (uint64_t)(step * 0x1p48f);
}

static inline uint16_t smoothing_frames(float sample_rate_hz) {
    float frames = sample_rate_hz * 0.006f + 0.5f;
    if (frames < 8.0f) frames = 8.0f;
    if (frames > 512.0f) frames = 512.0f;
    return (uint16_t)frames;
}

static inline float next_noise(uint64_t *state) {
    uint32_t top = (uint32_t)(tw_splitmix64(state) >> 40);
    return (float)top * (1.0f / 8388608.0f) - 1.0f;
}

static inline float interval_ratio(int interval) {
    if (interval < -24) interval = -24;
    if (interval > 24) interval = 24;
    int octave = interval / 12;
    int semitone = interval % 12;
    if (semitone < 0) {
        semitone += 12;
        octave--;
    }
    float ratio = SEMITONE_RATIO[semitone];
    while (octave < 0) {
        ratio *= 0.5f;
        octave++;
    }
    while (octave > 0) {
        ratio *= 2.0f;
        octave--;
    }
    return ratio;
}

static inline void start_envelope_stage(ma_envelope *envelope,
                                        ma_envelope_stage stage, float target) {
    float error = 0.001f * tw_fabsf(target - envelope->level);
    envelope->completion_error = error > 1.0e-9f ? error : 1.0e-9f;
    envelope->stage = stage;
}

static inline float next_mozaik_guard_gain(ma_mozaik *mozaik) {
    if (mozaik->guard_frames == 0) return mozaik->guard_gain;
    mozaik->guard_gain += mozaik->guard_step;
    mozaik->guard_frames--;
    if (mozaik->guard_frames == 0)
        mozaik->guard_gain = mozaik->guard_target;
    return mozaik->guard_gain;
}

static inline void reset_bcs(ma_bcs *bcs) {
    bcs->hopf_re = 0.025f;
    bcs->hopf_im = 0.0f;
    bcs->duffing_x = 0.001f;
    bcs->duffing_v = 0.0f;
    bcs->output = 0.0f;
    bcs->reset_count++;
}

static inline void set_mozaik_guard_target(ma_mozaik *mozaik, float target,
                                           float sample_rate_hz) {
    if (target == mozaik->guard_target) return;
    mozaik->guard_target = target;
    mozaik->guard_frames = smoothing_frames(sample_rate_hz);
    mozaik->guard_step = (target - mozaik->guard_gain)
                       / (float)mozaik->guard_frames;
}

static inline void shift_mozaik_phason(ma_mozaik *mozaik, uint32_t delta_q32) {
    uint32_t base = mozaik->has_pending_phason
                  ? mozaik->pending_phason_q32
                  : mozaik->applied_phason_q32;
    mozaik->pending_phason_q32 = base + delta_q32;
    mozaik->has_pending_phason = true;
}

static inline void start_mozaik_tile(ma_synth *s,
                                     const ma_render_controls *controls,
                                     float f0_hz, float render_rate_hz) {
    ma_mozaik *mozaik = &s->mozaik;
    float leftover = mozaik->tile_pos > mozaik->tile_len
                   ? mozaik->tile_pos - mozaik->tile_len : 0.0f;
    if (mozaik->has_pending_phason) {
        mozaik->frac_q32 += mozaik->pending_phason_q32
                          - mozaik->applied_phason_q32;
        mozaik->applied_phason_q32 = mozaik->pending_phason_q32;
        mozaik->has_pending_phason = false;
    }
    uint32_t previous = mozaik->frac_q32;
    mozaik->frac_q32 += controls->mozaik_slope_q32;
    bool is_long = mozaik->frac_q32 < previous;
    float sigma = (float)controls->mozaik_slope_q32 / Q32_ONE_F;
    float mean_samples = render_rate_hz / (2.0f * f0_hz);
    float mean_factor = (1.0f - sigma)
                      + sigma * controls->mozaik_contrast;
    float kind_factor = is_long ? controls->mozaik_contrast : 1.0f;
    mozaik->tile_len = mean_samples * kind_factor / mean_factor;
    if (mozaik->tile_len < 4.0f) mozaik->tile_len = 4.0f;
    mozaik->tile_pos = leftover;
    mozaik->tile_sign = is_long ? 1.0f : -1.0f;
    mozaik->tiles_emitted++;
}

static inline float render_mozaik(ma_synth *s,
                                  const ma_render_controls *controls,
                                  float f0_hz, float render_rate_hz) {
    float upper_hz = 0.125f * s->sample_rate_hz;
    if (upper_hz > 8000.0f) upper_hz = 8000.0f;
    set_mozaik_guard_target(&s->mozaik,
                            f0_hz >= 20.0f && f0_hz <= upper_hz ? 1.0f : 0.0f,
                            render_rate_hz);
    uint32_t drift_q32 = (uint32_t)(0.5f * controls->mozaik_drift
                                   * controls->mozaik_drift * Q32_ONE_F
                                   / render_rate_hz + 0.5f);
    if (drift_q32) shift_mozaik_phason(&s->mozaik, drift_q32);
    if (s->mozaik.tile_pos >= s->mozaik.tile_len)
        start_mozaik_tile(s, controls, f0_hz, render_rate_hz);
    float unit_pos = s->mozaik.tile_pos / s->mozaik.tile_len;
    float hann = 0.5f * (1.0f - tw_sin_turns(wrap_phase(unit_pos + 0.25f)));
    s->mozaik.tile_pos += 1.0f;
    return s->mozaik.tile_sign * hann;
}

#if !defined(MA_SOURCE_EVIDENCE)
static inline float envelope_coefficient(float time_ms, float sample_rate_hz) {
    float x = LN1000 / (0.001f * time_ms * sample_rate_hz);
    return tw_one_pole_coeff(x);
}

static inline float render_envelope(ma_envelope *envelope, ma_adsr controls,
                                    float sample_rate_hz) {
    float target = 0.0f;
    float time_ms = controls.release_ms;
    switch (envelope->stage) {
    case MA_ENVELOPE_IDLE:
        envelope->level = 0.0f;
        return 0.0f;
    case MA_ENVELOPE_ATTACK:
        target = 1.0f;
        time_ms = controls.attack_ms;
        break;
    case MA_ENVELOPE_DECAY:
        target = controls.sustain;
        time_ms = controls.decay_ms;
        break;
    case MA_ENVELOPE_SUSTAIN:
        envelope->level = controls.sustain;
        return envelope->level;
    case MA_ENVELOPE_RELEASE:
        break;
    }

    float coefficient = envelope_coefficient(time_ms, sample_rate_hz);
    envelope->level += coefficient * (target - envelope->level);
    if (tw_fabsf(target - envelope->level) > envelope->completion_error)
        return envelope->level;

    envelope->level = target;
    switch (envelope->stage) {
    case MA_ENVELOPE_ATTACK:
        start_envelope_stage(envelope, MA_ENVELOPE_DECAY, controls.sustain);
        break;
    case MA_ENVELOPE_DECAY:
        envelope->completion_error = 0.0f;
        envelope->stage = MA_ENVELOPE_SUSTAIN;
        break;
    case MA_ENVELOPE_RELEASE:
        envelope->completion_error = 0.0f;
        envelope->stage = MA_ENVELOPE_IDLE;
        break;
    case MA_ENVELOPE_IDLE:
    case MA_ENVELOPE_SUSTAIN:
        break;
    }
    return envelope->level;
}

static inline ma_adsr character_adsr(ma_adsr authored, ma_envelope_time_bias bias,
                                     float amount) {
    if (amount > 0.0f) {
        authored.attack_ms *= 1.0f + amount * bias.attack;
        authored.decay_ms *= 1.0f + amount * bias.decay;
        authored.release_ms *= 1.0f + amount * bias.release;
    }
    return authored;
}

static inline float voice_back_end(const ma_synth *s, const ma_voice_front *f,
                                   float filtered) {
    float velocity_gain = 0.25f + 0.75f * ma_velocity_level(s->velocity);
    float output = filtered * s->amp_envelope.level * velocity_gain;
    if (f->controls.poly_pressure > 0.0f)
        output *= 1.0f + 0.10f * f->controls.poly_pressure;
    if (f->character > 0.0f)
        output *= 1.0f + f->character * s->character.vca_bias;
    return output;
}
#endif

static inline ma_smoother initialized_smoother(float value) {
    return (ma_smoother){ .current = value, .target = value };
}

static inline void set_smoother_target(ma_smoother *smoother, float target,
                                       float sample_rate_hz) {
    if (target == smoother->target) return;
    smoother->target = target;
    smoother->remaining = smoothing_frames(sample_rate_hz);
    smoother->step = (target - smoother->current)
                   / (float)smoother->remaining;
}

static inline void set_bypass_smoother_target(ma_smoother *smoother, float target,
                                              float sample_rate_hz) {
    if (target == 0.0f) {
        *smoother = initialized_smoother(0.0f);
        return;
    }
    set_smoother_target(smoother, target, sample_rate_hz);
}

static inline float filter_prewarp(float cutoff_hz, float sample_rate_hz) {
    float x = cutoff_hz / (2.0f * sample_rate_hz);
    float z = 2.0f * x * (1.0f / 0.42f) - 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    for (int k = 6; k >= 1; k--) {
        float b = 2.0f * z * b1 - b2 + PREWARP_CHEB[k];
        b2 = b1;
        b1 = b;
    }
    return x * (z * b1 - b2 + PREWARP_CHEB[0]);
}

static inline float mix_source_2x(const ma_render_controls *controls, float analog,
                                  float guard1, float guard2,
                                  float mozaik_gain, float mozaik, float raster) {
    if (controls->raster_mix == 0.0f) {
        if (!(controls->mozaik_mix > 0.0f && mozaik_gain > 0.0f))
            return analog;
        float vco1_weight = guard1 > 0.0f ? 1.0f : 0.0f;
        float vco2_weight = guard2 > 0.0f ? controls->vco2_level : 0.0f;
        float analog_weight = vco1_weight + vco2_weight
                            + controls->noise_level;
        if (analog_weight < 1.0f) analog_weight = 1.0f;
        return (analog_weight * analog
                + controls->mozaik_mix * mozaik_gain * mozaik)
             / (analog_weight + controls->mozaik_mix);
    }

    float vco1_weight = guard1 > 0.0f ? 1.0f : 0.0f;
    float vco2_weight = guard2 > 0.0f ? controls->vco2_level : 0.0f;
    float analog_weight = vco1_weight + vco2_weight
                        + controls->noise_level;
    if (analog_weight < 1.0f) analog_weight = 1.0f;
    float weighted = analog_weight * analog
                   + controls->raster_mix * raster;
    float weight = analog_weight + controls->raster_mix;
    if (controls->mozaik_mix > 0.0f && mozaik_gain > 0.0f) {
        weighted += controls->mozaik_mix * mozaik_gain * mozaik;
        weight += controls->mozaik_mix;
    }
    return weighted / weight;
}

/* Out-of-line functions defined in ma_voice.c. The control code
 * (ma_controls.c) snaps the mozaik slope; the vector lanes (ma_lanes.c) run
 * front end part A; the front-lane gate checks the filter envelope and
 * cutoff against the lane math. */
uint32_t ma_voice_mozaik_slope_q32(float control);
#if !defined(MA_SOURCE_EVIDENCE)
void ma_voice_front_a(ma_synth *restrict s, float detune_cents,
                      ma_voice_front *restrict f);
void ma_voice_front_b_shared(ma_synth *restrict s, float bcs, float guard1,
                             float guard2, ma_voice_front *restrict f);
float ma_voice_filter_envelope_amount(const ma_synth *s,
                                      const ma_render_controls *controls);
float ma_voice_effective_filter_cutoff(const ma_synth *s,
                                       const ma_render_controls *controls,
                                       float envelope_amount,
                                       float note_position);
#endif

/* The oscillators (ma_osc.c): the guard smoothers, the 8x source frame, one
 * source substep and the 8x-to-2x halfband. The scalar tick calls them per
 * frame and per substep; ma_voice_prepare_source_frame reads the guards
 * (the evidence tick). */
void ma_voice_set_guard_target(ma_oscillator *oscillator, float target,
                               float sample_rate_hz);
float ma_voice_next_guard_gain(ma_oscillator *oscillator);
ma_source_frame ma_voice_prepare_source_frame_guarded(
    const ma_synth *s, const ma_render_controls *controls, float base_hz,
    float filter_modulation, float guard1_gain, float guard2_gain);
ma_source_frame ma_voice_prepare_source_frame(const ma_synth *s,
                                              const ma_render_controls *controls,
                                              float base_hz,
                                              float filter_modulation);
/* Gives a triangle with no state yet its seed; once per frame, before the
 * frame's substeps. */
void ma_voice_seed_triangles(ma_synth *s, const ma_source_frame *source);
float ma_voice_render_source_substep(ma_synth *s,
                                     const ma_render_controls *controls,
                                     const ma_source_frame *source,
                                     float base_hz, float noise,
                                     float guard1, float guard2);
void ma_voice_push_oversample_history(float history[31], uint8_t *position,
                                      float sample);
float ma_voice_decimate_oversample(const float history[31], uint8_t position);

/* The raster and BCS sources (ma_sources.c), rendered at 2x by the scalar
 * front end. ma_voice_raster_mip is the band-limit choice both share with
 * the vector raster's exactness gate. */
uint8_t ma_voice_raster_mip(float phase_step, float warp, float *blend);
float ma_voice_render_raster(ma_synth *s, const ma_render_controls *controls,
                             float frequency_hz, float render_rate_hz);
float ma_voice_render_bcs(ma_synth *s, float frequency_hz, float amount,
                          float regime, float feedback_sample);

/* Glas (ma_glas.c, ma-constants.md 10C). The scalar tick calls
 * ma_glas_prepare once per frame while ma_glas_active, then the bank. The
 * vector lanes call ma_glas_prepare_controls, then pulse/launch per lane and
 * render grains and the bank lane-major with the same arithmetic. */
static constexpr float GLAS_GAIN[MA_FORMANTS] = { 1.0f, 0.63f, 0.25f, 0.14f };
static constexpr float FORMANT_MAKEUP = 4.0f;      /* 10C.6 */
static constexpr float GRAIN_NORM = 0.30f;         /* 10C.6 */
static constexpr float GRAIN_FLOOR = 1.0e-4f;
float ma_glas_prepare_controls(ma_synth *s, float f0);
bool ma_glas_pulse(ma_glas *g, float f0, float render_rate_hz);
ma_grain ma_glas_new_grain(const ma_glas *g, int formant, float render_rate_hz);
void ma_glas_reset_state(ma_glas *g);
void ma_glas_init(ma_glas *g, float mix, float vowel, float shift, float orbita,
                  float grain_mix);
void ma_glas_set_formant(ma_glas *g, float mix, float vowel, float shift,
                         float orbita, float sample_rate_hz);
void ma_glas_set_grain(ma_glas *g, float mix, float sample_rate_hz);
bool ma_glas_active(const ma_glas *g);
/* Control-only voices (the pool's control synth) advance and snap the Glas
 * smoothers so woken slots inherit settled values. */
void ma_glas_tick_controls(ma_glas *g);
void ma_glas_snap_controls(ma_glas *g);
float ma_glas_prepare(ma_synth *s, float f0);
float ma_glas_formant(ma_synth *s, float x);

/* The scalar filter stage (ma_filter.c), called once per frame by the
 * scalar tick. */
#if !defined(MA_SOURCE_EVIDENCE)
float ma_voice_filter(ma_synth *s, const ma_voice_front *f,
                      const float at_2x[2]);
ma_frame ma_voice_render_output(ma_synth *s,
                                const ma_render_controls *controls,
                                float mono);
#endif

#endif
