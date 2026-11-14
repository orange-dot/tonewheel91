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

/* The two smoothed edges of a half period take 1.25 * edge of the 50% pulse's
 * area, so the triangle integrator runs 1 / (1 - 1.25 * edge) faster
 * (ma-constants.md 3.1). */
static constexpr float TRIANGLE_EDGE_LOSS = 1.25f;

/* A skewed triangle's pull back to its steady trajectory, per frame
 * (ma_osc_kernel.h, ml_seed_triangle; docs/ma-vco-plan.md V2). */
static constexpr float TRIANGLE_PULL = 1.0f / 64.0f;

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
    float vco1_skew, vco2_skew;     /* V2: the triangles' rise less 0.5 */
    float vco1_swell, vco2_swell;   /* V3: the triangle the shapers see, less 1 */
    float saw_bow;                  /* V2: VCO-2's saw bow, 0 = straight */
    float couple_hz, couple_p, couple_q, couple_jump;   /* V4: lock range, p:q, jump */
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

enum { WAVE_SAW = 1, WAVE_PULSE = 2, WAVE_TRIANGLE = 4, WAVE_TRI_SINE = 8, WAVE_TRI_SAW = 16,
       WAVE_ALL = WAVE_SAW | WAVE_PULSE | WAVE_TRIANGLE | WAVE_TRI_SINE | WAVE_TRI_SAW };

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

static inline float wrap_phase(float phase) {
    if (phase >= 1.0f) phase -= 1.0f;
    if (phase < 0.0f) phase += 1.0f;
    return phase;
}

static inline float phase_turns(uint64_t phase_q48) {
    return (float)(uint32_t)(phase_q48 >> 16) * 0x1p-32f;
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

/* ---- circuit-derived oscillator pitch (docs/ma-vco-plan.md) ----
 * One VCO-2 core (driver/spice/vco/vco2.cir): an exponential current plus a
 * constant extra current charges the capacitor, and every cycle loses a dead
 * time to the reset:  1/f = 1/(f_exp + extra) + dead.
 * The board is trimmed the way a technician trims it: scale until C2 -> C6
 * is four octaves, pitch until A4 is 440 Hz (to first order in the two small
 * terms; the SPICE trim loop lands within 0.4 cents of this over C0..C9).
 * Temperature moves pitch and scale together, as measured with the
 * +3300 ppm/K scale resistor fitted. `hz` is the ideal frequency and
 * `octaves` its distance from A4. */
static constexpr float VCO_INV_LN2 = 1.44269502f;
static constexpr float VCO_TEMP_OCTAVES_PER_K = 0.99f / 1200.0f;          /* at A4 */
static constexpr float VCO_TEMP_SCALE_PER_K = -0.20f / 1200.0f;           /* per octave from A4 */
static constexpr float VCO_NOMINAL_EXTRA_HZ = 0.244f;
static constexpr float VCO_NOMINAL_DEAD_US = 6.546f;
static constexpr float VCO_WALK_K_PER_CENT = 0.5f / 3.0f;   /* the card's walk, read as kelvin */

/* ---- circuit-derived oscillator shape (docs/ma-vco-plan.md, V2) ----
 * B1's triangle (driver/spice/vco/b1_low.py, 27 Hz to 1.94 kHz): the rise
 * takes 0.5 + exp(P(x)) of the turn, x octaves from A4, P the quartic below
 * (within 3 % of the asymmetry at every simulated pitch). Outside the
 * simulated range P runs on along its end slopes; the rise is capped. */
static constexpr float VCO_RISE_POLY[5] = {          /* x^0 .. x^4 */
    -4.76207198f, -0.620919801f, -0.114165139f, -0.0267012744f, -0.00284620846f,
};
static constexpr float VCO_RISE_X_LOW = -4.00592f;
static constexpr float VCO_RISE_X_HIGH = 2.14301f;
static constexpr float VCO_RISE_SLOPE_LOW = -0.259835f;
static constexpr float VCO_RISE_SLOPE_HIGH = -1.59015f;
static constexpr float VCO_RISE_MAX = 0.6f;
/* Its swing grows with pitch (each reversal overshoots by the comparator's
 * delay). The shapers see that growth relative to A4, where they are
 * trimmed, a little less than the capacitor has it: the saw's pair swaps
 * just after the vertex. `swell` is what the switched pair sees, fitted to
 * its step at the vertex in SPICE (driver/spice/vco/shapers.py): to 1.94 kHz
 * a cubic in octaves from A4 through zero (within 2.3e-4), held below 27 Hz;
 * above it, measured with B1's range switch to 18.7 kHz (V2b), straight in
 * hertz (within 8e-4), held above 20 kHz. The sine sees the same swell; its
 * drive is 0.3 % off at most for it. */
static constexpr float VCO_SWELL_POLY[3] = {           /* x^1 .. x^3 */
    0.00224345534f, 0.000646784953f, 0.0000813179675f,
};
static constexpr float VCO_SWELL_X_LOW = -4.00504f;
static constexpr float VCO_SWELL_X_HIGH = 2.14382f;    /* 1944.5 Hz */
static constexpr float VCO_SWELL_JOIN_HZ = 1944.5f;
static constexpr float VCO_SWELL_PER_HZ = 4.1438e-6f;
static constexpr float VCO_SWELL_TOP_HZ = 20000.0f;
/* VCO-2's saw bow: 30.5 mV in the middle of a 5.06 V ramp at C4, 29.3 to
 * 32.1 mV from C2 to C8 (docs/ma-vco2-spice.md); as a share of the span 2. */
static constexpr float VCO_SAW_BOW = 0.01205f;

/* ---- two cores on one supply (docs/ma-vco-plan.md, V4) ----
 * Two VCO-2 cores on a shared 12 V / 8 V supply in SPICE
 * (driver/spice/vco/vco4.py): near p:q (VCO2 over VCO1) the phase
 * difference q phi2 - p phi1 runs at its detune plus a sawtooth of half
 * height L that locks it at `jump`. L over the lower core's frequency is the
 * same from C2 to C6; it is `good` for a board with 1 ohm and 10 uF on its
 * rails and `soft` for 10 ohm and 100 nF. Every pair locks with its resets
 * together: the jump is at psi = 0 in the core's phase (0 at the reset)
 * within 0.03 in every SPICE case. The intervals below VCO1 mirror the ones
 * above it. Other intervals do not couple. */
typedef struct { int8_t interval, p, q; float jump, good, soft_octaves; } vco_couple_row;
static constexpr vco_couple_row VCO_COUPLE[5] = {     /* soft = good * 2^soft_octaves */
    { 0, 1, 1, 0.0f, 0.000106f, 3.2944f },   /* soft 0.00104 */
    { 12, 2, 1, 0.0f, 0.00016f, 3.2854f },   /* soft 0.00156 */
    { -12, 1, 2, 0.0f, 0.00016f, 3.2854f },   /* soft 0.00156 */
    { 7, 3, 2, 0.0f, 0.000132f, 3.3219f },   /* soft 0.00132 */
    { -7, 2, 3, 0.0f, 0.000132f, 3.3219f },   /* soft 0.00132 */
};

/* ---- the triangle core's shapers (docs/ma-vco-plan.md, V3) ----
 * The sine pair's static curve (driver/spice/vco/shapers.py fit), trimmed at
 * A4, on the triangle normalized to +-1 at A4's vertices and scaled to peaks
 * of +-1: x * odd(x^2) + even(x^2), within 5e-5 over +-1 and 1.3e-4 over
 * +-1.1. The even part is the trimmed offset. */
enum { SHAPER_SINE_TERMS = 6 };
static constexpr float SHAPER_SINE_ODD[SHAPER_SINE_TERMS] = {
    1.54028599f, -0.561557056f, -0.192092654f, 0.307457722f, -0.0962403955f, 0.00214095014f,
};
static constexpr float SHAPER_SINE_EVEN[SHAPER_SINE_TERMS] = {
    -0.00273633822f, 0.00344021426f, 0.00217042488f, -0.00509633677f, 0.0025910752f, -0.000369745654f,
};

float ma_octave_ratio_small(float octaves);

static inline float vco_law_hz(float hz, float octaves, float extra_hz,
                               float dead_s, float temp_k) {
    float e_c2 = extra_hz * (1.0f / 65.40639f) - 65.40639f * dead_s;
    float e_c6 = extra_hz * (1.0f / 1046.5023f) - 1046.5023f * dead_s;
    float e_a4 = extra_hz * (1.0f / 440.0f) - 440.0f * dead_s;
    float scale = -0.25f * VCO_INV_LN2 * (e_c6 - e_c2) + temp_k * VCO_TEMP_SCALE_PER_K;
    float offset = -VCO_INV_LN2 * e_a4 + temp_k * VCO_TEMP_OCTAVES_PER_K;
    float exponential = hz * ma_octave_ratio_small(offset + scale * octaves);
    return 1.0f / (1.0f / (exponential + extra_hz) + dead_s);
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

/* 2^octaves for |octaves| <= 1/4 (ma_osc.c), and the ladder's g for a
 * cutoff (ma_filter.c): the kernels' forms at width one. */
float ma_octave_ratio_small(float octaves);
float ma_filter_prewarp(float cutoff_hz, float sample_rate_hz);

/* The oscillators (ma_osc.c): the 8x source frame, the oscillator kernel
 * (ma_osc_kernel.h) for one voice, and the 8x-to-2x halfband.
 * ma_voice_prepare_source_frame reads the guards (the evidence tick). */
ma_source_frame ma_voice_prepare_source_frame_guarded(
    const ma_synth *s, const ma_render_controls *controls, float base_hz,
    float filter_modulation, float guard1_gain, float guard2_gain);
ma_source_frame ma_voice_prepare_source_frame(const ma_synth *s,
                                              const ma_render_controls *controls,
                                              float base_hz,
                                              float filter_modulation);
/* One frame's eight 8x source samples: VCO2's guard target, the triangle
 * seeds, the substeps and both guard steps. */
void ma_voice_render_source_block(ma_synth *s, const ma_render_controls *controls,
                                  const ma_source_frame *source, float base_hz,
                                  float noise, float guard1, float guard2,
                                  float out[8]);
/* The block's parts, for white-box gates: the seeds of triangles with no
 * state yet, and one substep. */
void ma_voice_seed_triangles(ma_synth *s, const ma_source_frame *source);
float ma_voice_render_source_substep(ma_synth *s,
                                     const ma_render_controls *controls,
                                     const ma_source_frame *source,
                                     float base_hz, float noise,
                                     float guard1, float guard2);
/* 8x -> 4x -> 2x through the voice's two halfband rings. */
void ma_voice_decimate_source(ma_synth *s, const float source[8], float at_2x[2]);
/* One halfband ring, for the evidence tick and the FIR gate. */
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
