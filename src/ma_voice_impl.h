/* Private to the Mamut Analog voice modules, their white-box tests and
 * probes: constants, internal types and the per-sample primitives that
 * scalar and lane code share. ma_internal.h stays the boundary for the
 * pool, bank and output. */
#ifndef MA_VOICE_IMPL_H
#define MA_VOICE_IMPL_H

#include "ma_internal.h"

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

#if !defined(MA_SOURCE_EVIDENCE)
static constexpr float FILTER_HALFBAND_7[7] = {
   -4.674123600e-02f, 0.0f, 2.967412472e-01f, 5.0e-01f,
    2.967412472e-01f, 0.0f, -4.674123600e-02f,
};
#endif

static constexpr float PREWARP_CHEB[7] = {
    2.287037611e+00f, -5.843005180e-01f, 2.182650715e-01f,
   -3.681783006e-02f,  1.203418057e-02f, -2.262040973e-03f,
    6.386489258e-04f,
};

static constexpr uint64_t PHASE_ONE_Q48 = UINT64_C(1) << 48;

static constexpr uint64_t PHASE_MASK_Q48 = (UINT64_C(1) << 48) - 1;

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
    float vco2_hz;
    float vco2_step;
    uint64_t vco2_step_q48;
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

static inline float wrap_phase(float phase) {
    if (phase >= 1.0f) phase -= 1.0f;
    if (phase < 0.0f) phase += 1.0f;
    return phase;
}

static inline float phase_turns(uint64_t phase_q48) {
    return (float)(uint32_t)(phase_q48 >> 16) * 0x1p-32f;
}

static inline float smoothstep5(float x) {
    return x * x * x * (10.0f + x * (-15.0f + 6.0f * x));
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

#endif
