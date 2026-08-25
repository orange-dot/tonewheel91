/* Mamut Analog one-voice core. MA1 begins with the pinned pitch/control
 * spine; render state lands behind this concrete public object. */
#include "mamutanalog.h"

static constexpr uint64_t NOISE_SEED = UINT64_C(0x4d414e4f49534531);
static constexpr uint32_t MOZAIK_SLOPE_MIN_Q32 = UINT32_C(0x73333333);
static constexpr uint32_t MOZAIK_SLOPE_MAX_Q32 = UINT32_C(0xc0000000);
static constexpr uint32_t MOZAIK_GOLDEN_Q32 = UINT32_C(0x9e3779b9);
static constexpr float MOZAIK_GOLDEN_CONTRAST = 1.618034f;
static constexpr float Q32_ONE_F = 0x1p32f;

typedef struct {
    float sigma;
    uint32_t q32;
} mozaik_detent;

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

static constexpr float NOTE_HZ[128] = {
    8.175799370e+00f, 8.661956787e+00f, 9.177023888e+00f, 9.722718239e+00f, 1.030086136e+01f, 1.091338253e+01f, 1.156232548e+01f, 1.224985695e+01f,
    1.297827148e+01f, 1.375000000e+01f, 1.456761742e+01f, 1.543385315e+01f, 1.635159874e+01f, 1.732391357e+01f, 1.835404778e+01f, 1.944543648e+01f,
    2.060172272e+01f, 2.182676506e+01f, 2.312465096e+01f, 2.449971390e+01f, 2.595654297e+01f, 2.750000000e+01f, 2.913523483e+01f, 3.086770630e+01f,
    3.270319748e+01f, 3.464782715e+01f, 3.670809555e+01f, 3.889087296e+01f, 4.120344543e+01f, 4.365353012e+01f, 4.624930191e+01f, 4.899942780e+01f,
    5.191308594e+01f, 5.500000000e+01f, 5.827046967e+01f, 6.173541260e+01f, 6.540639496e+01f, 6.929565430e+01f, 7.341619110e+01f, 7.778174591e+01f,
    8.240689087e+01f, 8.730706024e+01f, 9.249860382e+01f, 9.799885559e+01f, 1.038261719e+02f, 1.100000000e+02f, 1.165409393e+02f, 1.234708252e+02f,
    1.308127899e+02f, 1.385913086e+02f, 1.468323822e+02f, 1.555634918e+02f, 1.648137817e+02f, 1.746141205e+02f, 1.849972076e+02f, 1.959977112e+02f,
    2.076523438e+02f, 2.200000000e+02f, 2.330818787e+02f, 2.469416504e+02f, 2.616255798e+02f, 2.771826172e+02f, 2.936647644e+02f, 3.111269836e+02f,
    3.296275635e+02f, 3.492282410e+02f, 3.699944153e+02f, 3.919954224e+02f, 4.153046875e+02f, 4.400000000e+02f, 4.661637573e+02f, 4.938833008e+02f,
    5.232511597e+02f, 5.543652344e+02f, 5.873295288e+02f, 6.222539673e+02f, 6.592551270e+02f, 6.984564819e+02f, 7.399888306e+02f, 7.839908447e+02f,
    8.306093750e+02f, 8.800000000e+02f, 9.323275146e+02f, 9.877666016e+02f, 1.046502319e+03f, 1.108730469e+03f, 1.174659058e+03f, 1.244507935e+03f,
    1.318510254e+03f, 1.396912964e+03f, 1.479977661e+03f, 1.567981689e+03f, 1.661218750e+03f, 1.760000000e+03f, 1.864655029e+03f, 1.975533203e+03f,
    2.093004639e+03f, 2.217460938e+03f, 2.349318115e+03f, 2.489015869e+03f, 2.637020508e+03f, 2.793825928e+03f, 2.959955322e+03f, 3.135963379e+03f,
    3.322437500e+03f, 3.520000000e+03f, 3.729310059e+03f, 3.951066406e+03f, 4.186009277e+03f, 4.434921875e+03f, 4.698636230e+03f, 4.978031738e+03f,
    5.274041016e+03f, 5.587651855e+03f, 5.919910645e+03f, 6.271926758e+03f, 6.644875000e+03f, 7.040000000e+03f, 7.458620117e+03f, 7.902132812e+03f,
    8.372018555e+03f, 8.869843750e+03f, 9.397272461e+03f, 9.956063477e+03f, 1.054808203e+04f, 1.117530371e+04f, 1.183982129e+04f, 1.254385352e+04f,
};

static constexpr float VELOCITY_LEVEL[128] = {
    0.000000000e+00f, 2.285772935e-02f, 3.924971446e-02f, 5.385024473e-02f, 6.739689410e-02f, 8.021021634e-02f, 9.246791899e-02f, 1.042820513e-01f,
    1.157292873e-01f, 1.268651187e-01f, 1.377314478e-01f, 1.483608782e-01f, 1.587795168e-01f, 1.690086424e-01f, 1.790659279e-01f, 1.889662594e-01f,
    1.987223327e-01f, 2.083450854e-01f, 2.178440243e-01f, 2.272275090e-01f, 2.365029156e-01f, 2.456768006e-01f, 2.547550499e-01f, 2.637429237e-01f,
    2.726452053e-01f, 2.814662457e-01f, 2.902099490e-01f, 2.988799810e-01f, 3.074796498e-01f, 3.160119653e-01f, 3.244798183e-01f, 3.328857720e-01f,
    3.412322700e-01f, 3.495215476e-01f, 3.577557802e-01f, 3.659368753e-01f, 3.740667105e-01f, 3.821469843e-01f, 3.901793659e-01f, 3.981653750e-01f,
    4.061064422e-01f, 4.140039682e-01f, 4.218592048e-01f, 4.296734333e-01f, 4.374477565e-01f, 4.451833069e-01f, 4.528810978e-01f, 4.605422020e-01f,
    4.681675136e-01f, 4.757579267e-01f, 4.833143651e-01f, 4.908376038e-01f, 4.983284771e-01f, 5.057877302e-01f, 5.132160783e-01f, 5.206142068e-01f,
    5.279827714e-01f, 5.353224874e-01f, 5.426338911e-01f, 5.499176383e-01f, 5.571743250e-01f, 5.644043684e-01f, 5.716084242e-01f, 5.787869692e-01f,
    5.859404206e-01f, 5.930693746e-01f, 6.001742482e-01f, 6.072554588e-01f, 6.143134832e-01f, 6.213486791e-01f, 6.283615232e-01f, 6.353523135e-01f,
    6.423214674e-01f, 6.492694020e-01f, 6.561964154e-01f, 6.631028056e-01f, 6.699890494e-01f, 6.768553257e-01f, 6.837020516e-01f, 6.905294657e-01f,
    6.973379254e-01f, 7.041276693e-01f, 7.108989954e-01f, 7.176522017e-01f, 7.243874669e-01f, 7.311051488e-01f, 7.378054857e-01f, 7.444887161e-01f,
    7.511550188e-01f, 7.578046918e-01f, 7.644379735e-01f, 7.710550427e-01f, 7.776561379e-01f, 7.842414379e-01f, 7.908112407e-01f, 7.973656058e-01f,
    8.039048910e-01f, 8.104291558e-01f, 8.169386387e-01f, 8.234335184e-01f, 8.299140334e-01f, 8.363802433e-01f, 8.428324461e-01f, 8.492707014e-01f,
    8.556951880e-01f, 8.621061444e-01f, 8.685036898e-01f, 8.748879433e-01f, 8.812591434e-01f, 8.876172900e-01f, 8.939626813e-01f, 9.002953768e-01f,
    9.066155553e-01f, 9.129232764e-01f, 9.192187786e-01f, 9.255021214e-01f, 9.317734241e-01f, 9.380329251e-01f, 9.442805648e-01f, 9.505166411e-01f,
    9.567412138e-01f, 9.629543424e-01f, 9.691561460e-01f, 9.753468633e-01f, 9.815264344e-01f, 9.876950979e-01f, 9.938529134e-01f, 1.000000000e+00f,
};

static constexpr float VELOCITY_FILTER[128] = {
    0.000000000e+00f, 5.344314035e-03f, 1.129807346e-02f, 1.750583947e-02f, 2.388453484e-02f, 3.039342165e-02f, 3.700797632e-02f, 4.371171817e-02f,
    5.049276724e-02f, 5.734213814e-02f, 6.425278634e-02f, 7.121903449e-02f, 7.823619992e-02f, 8.530034870e-02f, 9.240814298e-02f, 9.955671430e-02f,
    1.067435294e-01f, 1.139663979e-01f, 1.212233528e-01f, 1.285126507e-01f, 1.358327121e-01f, 1.431821287e-01f, 1.505596042e-01f, 1.579639763e-01f,
    1.653941423e-01f, 1.728491336e-01f, 1.803280115e-01f, 1.878299564e-01f, 1.953541487e-01f, 2.028998882e-01f, 2.104664743e-01f, 2.180532664e-01f,
    2.256596684e-01f, 2.332851142e-01f, 2.409290671e-01f, 2.485910356e-01f, 2.562705278e-01f, 2.639671266e-01f, 2.716803849e-01f, 2.794098854e-01f,
    2.871552706e-01f, 2.949161530e-01f, 3.026921749e-01f, 3.104830682e-01f, 3.182884455e-01f, 3.261080384e-01f, 3.339415491e-01f, 3.417886794e-01f,
    3.496491909e-01f, 3.575228155e-01f, 3.654092848e-01f, 3.733084202e-01f, 3.812199235e-01f, 3.891436160e-01f, 3.970792890e-01f, 4.050267339e-01f,
    4.129857421e-01f, 4.209561050e-01f, 4.289377034e-01f, 4.369302988e-01f, 4.449337423e-01f, 4.529478550e-01f, 4.609724879e-01f, 4.690074921e-01f,
    4.770526886e-01f, 4.851079583e-01f, 4.931731522e-01f, 5.012481213e-01f, 5.093327761e-01f, 5.174269080e-01f, 5.255303979e-01f, 5.336432457e-01f,
    5.417651534e-01f, 5.498961210e-01f, 5.580360293e-01f, 5.661846995e-01f, 5.743421316e-01f, 5.825080872e-01f, 5.906825662e-01f, 5.988654494e-01f,
    6.070565581e-01f, 6.152558923e-01f, 6.234633923e-01f, 6.316788197e-01f, 6.399022341e-01f, 6.481334567e-01f, 6.563723683e-01f, 6.646190286e-01f,
    6.728732586e-01f, 6.811349988e-01f, 6.894041300e-01f, 6.976806521e-01f, 7.059644461e-01f, 7.142554522e-01f, 7.225536108e-01f, 7.308588028e-01f,
    7.391709685e-01f, 7.474901080e-01f, 7.558161020e-01f, 7.641488910e-01f, 7.724884152e-01f, 7.808346152e-01f, 7.891874313e-01f, 7.975468040e-01f,
    8.059126735e-01f, 8.142849803e-01f, 8.226636648e-01f, 8.310486674e-01f, 8.394399285e-01f, 8.478374481e-01f, 8.562411070e-01f, 8.646509051e-01f,
    8.730667233e-01f, 8.814886212e-01f, 8.899164200e-01f, 8.983501792e-01f, 9.067897797e-01f, 9.152352214e-01f, 9.236863852e-01f, 9.321433306e-01f,
    9.406059384e-01f, 9.490742087e-01f, 9.575480819e-01f, 9.660274982e-01f, 9.745124578e-01f, 9.830029011e-01f, 9.914987683e-01f, 1.000000000e+00f,
};

static float finite_or(float value, float fallback) {
    return value >= -FLT_MAX && value <= FLT_MAX ? value : fallback;
}

static float clamp_control(float value, float low, float high, float fallback) {
    value = finite_or(value, fallback);
    return value < low ? low : value > high ? high : value;
}

static float clamp_signal(float value, float low, float high) {
    return value < low ? low : value > high ? high : value;
}

static float raw_triangle(float phase) {
    return 1.0f - 4.0f * tw_fabsf(phase - 0.5f);
}

static float wrap_phase(float phase) {
    if (phase >= 1.0f) phase -= 1.0f;
    if (phase < 0.0f) phase += 1.0f;
    return phase;
}

static float phase_turns(uint64_t phase_q48) {
    return (float)(uint32_t)(phase_q48 >> 16) * 0x1p-32f;
}

static float smoothstep5(float x) {
    return x * x * x * (10.0f + x * (-15.0f + 6.0f * x));
}

static float poly_blep(float phase, float step) {
    if (!(step > FLT_EPSILON)) return 0.0f;
    float edge = 8.0f * step;
    if (edge > 0.49f) edge = 0.49f;
    if (phase < edge) {
        float x = 0.5f * (phase / edge + 1.0f);
        return 2.0f * (smoothstep5(x) - 1.0f);
    }
    if (phase > 1.0f - edge) {
        float x = 0.5f * ((phase - 1.0f) / edge + 1.0f);
        return 2.0f * smoothstep5(x);
    }
    return 0.0f;
}

static float saw_sample(float phase, float step) {
    return clamp_signal(2.0f * phase - 1.0f - poly_blep(phase, step),
                        -1.25f, 1.25f);
}

static float pulse_sample(float phase, float width, float step) {
    float sample = phase < width ? 1.0f : -1.0f;
    sample += poly_blep(phase, step);
    sample -= poly_blep(wrap_phase(phase - width), step);
    return clamp_signal(sample, -1.25f, 1.25f);
}

static float waveform_divisor(ma_vco_controls controls) {
    float sum = controls.saw_level + controls.pulse_level
              + controls.triangle_level;
    return sum > 1.0f ? sum : 1.0f;
}

static float raw_wave_mix(float phase, ma_vco_controls controls) {
    float saw = 2.0f * phase - 1.0f;
    float pulse = phase < controls.pulse_width ? 1.0f : -1.0f;
    float triangle = raw_triangle(phase);
    return (controls.saw_level * saw + controls.pulse_level * pulse
            + controls.triangle_level * triangle)
         / waveform_divisor(controls);
}

static float preview_oscillator(const ma_oscillator *oscillator,
                                ma_vco_controls controls, float step) {
    float phase = phase_turns(oscillator->phase_q48);
    float triangle = oscillator->triangle_initialized
                   ? clamp_signal(oscillator->triangle, -1.0f, 1.0f)
                   : raw_triangle(phase);
    return (controls.saw_level * saw_sample(phase, step)
            + controls.pulse_level * pulse_sample(phase, controls.pulse_width,
                                                   step)
            + controls.triangle_level * triangle)
         / waveform_divisor(controls);
}

static float render_oscillator(ma_oscillator *oscillator,
                               ma_vco_controls controls, float step,
                               float sync_correction) {
    if (!oscillator->triangle_initialized) {
        oscillator->triangle = raw_triangle(
            phase_turns(oscillator->phase_q48));
        oscillator->triangle_initialized = true;
    }
    float sample = preview_oscillator(oscillator, controls, step)
                 + oscillator->sync_residual + sync_correction;
    oscillator->sync_residual = 0.0f;
    oscillator->triangle = clamp_signal(
        oscillator->triangle
            + pulse_sample(phase_turns(oscillator->phase_q48), 0.5f, step)
                * 4.0f * step,
        -1.25f, 1.25f);
    return clamp_signal(sample, -1.25f, 1.25f);
}

static bool phase_will_wrap(const ma_oscillator *oscillator,
                            uint64_t step_q48) {
    return oscillator->phase_q48 + step_q48 >= PHASE_ONE_Q48;
}

static void advance_phase(ma_oscillator *oscillator, uint64_t step_q48) {
    oscillator->phase_q48 = (oscillator->phase_q48 + step_q48)
                          & PHASE_MASK_Q48;
}

static float octave_ratio_small(float octaves) {
    octaves = clamp_signal(octaves, -0.25f, 0.25f);
    float value = EXP2_SMALL[5];
    for (int i = 4; i >= 0; i--) value = value * octaves + EXP2_SMALL[i];
    return value;
}

static float interval_ratio(int interval) {
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

static float phase_step(float frequency_hz, float sample_rate_hz) {
    float step = frequency_hz / sample_rate_hz;
    return clamp_signal(step, 0.0f, 0.45f);
}

static uint64_t phase_step_q48(float step) {
    return (uint64_t)(step * 0x1p48f);
}

static uint16_t smoothing_frames(float sample_rate_hz) {
    float frames = sample_rate_hz * 0.006f + 0.5f;
    if (frames < 8.0f) frames = 8.0f;
    if (frames > 512.0f) frames = 512.0f;
    return (uint16_t)frames;
}

static void set_mozaik_guard_target(ma_mozaik *mozaik, float target,
                                    float sample_rate_hz) {
    if (target == mozaik->guard_target) return;
    mozaik->guard_target = target;
    mozaik->guard_frames = smoothing_frames(sample_rate_hz);
    mozaik->guard_step = (target - mozaik->guard_gain)
                       / (float)mozaik->guard_frames;
}

static float next_mozaik_guard_gain(ma_mozaik *mozaik) {
    if (mozaik->guard_frames == 0) return mozaik->guard_gain;
    mozaik->guard_gain += mozaik->guard_step;
    mozaik->guard_frames--;
    if (mozaik->guard_frames == 0)
        mozaik->guard_gain = mozaik->guard_target;
    return mozaik->guard_gain;
}

static void set_guard_target(ma_oscillator *oscillator, float target,
                             float sample_rate_hz) {
    if (target == oscillator->guard_target) return;
    oscillator->guard_target = target;
    oscillator->guard_frames = smoothing_frames(sample_rate_hz);
    oscillator->guard_step = (target - oscillator->guard_gain)
                           / (float)oscillator->guard_frames;
}

static float next_guard_gain(ma_oscillator *oscillator) {
    if (oscillator->guard_frames == 0) return oscillator->guard_gain;
    oscillator->guard_gain += oscillator->guard_step;
    oscillator->guard_frames--;
    if (oscillator->guard_frames == 0)
        oscillator->guard_gain = oscillator->guard_target;
    return oscillator->guard_gain;
}

static float next_noise(uint64_t *state) {
    uint32_t top = (uint32_t)(tw_splitmix64(state) >> 40);
    return (float)top * (1.0f / 8388608.0f) - 1.0f;
}

static uint32_t mozaik_slope_q32(float control) {
    float sigma = 0.45f + 0.30f * control;
    float best_distance = FLT_MAX;
    uint32_t snapped = 0;
    for (size_t i = 0; i < sizeof MOZAIK_DETENT / sizeof *MOZAIK_DETENT; i++) {
        float distance = tw_fabsf(sigma - MOZAIK_DETENT[i].sigma);
        if (distance <= 0.004f && distance < best_distance) {
            best_distance = distance;
            snapped = MOZAIK_DETENT[i].q32;
        }
    }
    if (snapped) return snapped;
    uint64_t q32 = (uint64_t)(sigma * Q32_ONE_F + 0.5f);
    if (q32 < MOZAIK_SLOPE_MIN_Q32) return MOZAIK_SLOPE_MIN_Q32;
    if (q32 > MOZAIK_SLOPE_MAX_Q32) return MOZAIK_SLOPE_MAX_Q32;
    return (uint32_t)q32;
}

static uint32_t mozaik_phason_q32(float phason) {
    phason = finite_or(phason, 0.0f);
    if (phason < -2147483520.0f || phason > 2147483520.0f) phason = 0.0f;
    phason -= (float)(int32_t)phason;
    if (phason < 0.0f) phason += 1.0f;
    return (uint32_t)(uint64_t)(phason * Q32_ONE_F);
}

static void reset_mozaik(ma_mozaik *mozaik, uint32_t phason_q32) {
    *mozaik = (ma_mozaik){
        .frac_q32 = phason_q32,
        .applied_phason_q32 = phason_q32,
        .tile_sign = 1.0f,
        .guard_gain = 1.0f,
        .guard_target = 1.0f,
    };
}

static void shift_mozaik_phason(ma_mozaik *mozaik, uint32_t delta_q32) {
    uint32_t base = mozaik->has_pending_phason
                  ? mozaik->pending_phason_q32
                  : mozaik->applied_phason_q32;
    mozaik->pending_phason_q32 = base + delta_q32;
    mozaik->has_pending_phason = true;
}

static void start_mozaik_tile(ma_synth *s, float f0_hz,
                              float render_rate_hz) {
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
    mozaik->frac_q32 += s->mozaik_slope_q32;
    bool is_long = mozaik->frac_q32 < previous;
    float sigma = (float)s->mozaik_slope_q32 / Q32_ONE_F;
    float mean_samples = render_rate_hz / (2.0f * f0_hz);
    float mean_factor = (1.0f - sigma) + sigma * s->mozaik_contrast;
    float kind_factor = is_long ? s->mozaik_contrast : 1.0f;
    mozaik->tile_len = mean_samples * kind_factor / mean_factor;
    if (mozaik->tile_len < 4.0f) mozaik->tile_len = 4.0f;
    mozaik->tile_pos = leftover;
    mozaik->tile_sign = is_long ? 1.0f : -1.0f;
    mozaik->tiles_emitted++;
}

static float render_mozaik(ma_synth *s, float f0_hz, float render_rate_hz) {
    float upper_hz = 0.125f * s->sample_rate_hz;
    if (upper_hz > 8000.0f) upper_hz = 8000.0f;
    set_mozaik_guard_target(&s->mozaik,
                            f0_hz >= 20.0f && f0_hz <= upper_hz ? 1.0f : 0.0f,
                            render_rate_hz);
    uint32_t drift_q32 = (uint32_t)(0.5f * s->mozaik_drift
                                   * s->mozaik_drift * Q32_ONE_F
                                   / render_rate_hz + 0.5f);
    if (drift_q32) shift_mozaik_phason(&s->mozaik, drift_q32);
    if (s->mozaik.tile_pos >= s->mozaik.tile_len)
        start_mozaik_tile(s, f0_hz, render_rate_hz);
    float unit_pos = s->mozaik.tile_pos / s->mozaik.tile_len;
    float hann = 0.5f * (1.0f - tw_sin_turns(wrap_phase(unit_pos + 0.25f)));
    s->mozaik.tile_pos += 1.0f;
    return s->mozaik.tile_sign * hann;
}

static ma_vco_controls sanitize_vco(ma_vco_controls controls,
                                    ma_vco_controls fallback) {
    return (ma_vco_controls){
        .saw_level = clamp_control(controls.saw_level, 0.0f, 1.0f,
                                   fallback.saw_level),
        .pulse_level = clamp_control(controls.pulse_level, 0.0f, 1.0f,
                                     fallback.pulse_level),
        .triangle_level = clamp_control(controls.triangle_level, 0.0f, 1.0f,
                                        fallback.triangle_level),
        .pulse_width = clamp_control(controls.pulse_width, 0.05f, 0.95f,
                                     fallback.pulse_width),
    };
}

static ma_adsr sanitize_adsr(ma_adsr value, ma_adsr fallback) {
    return (ma_adsr){
        .attack_ms = clamp_control(value.attack_ms, 1.0f, 20000.0f,
                                   fallback.attack_ms),
        .decay_ms = clamp_control(value.decay_ms, 1.0f, 20000.0f,
                                  fallback.decay_ms),
        .sustain = clamp_control(value.sustain, 0.0f, 1.0f,
                                 fallback.sustain),
        .release_ms = clamp_control(value.release_ms, 1.0f, 20000.0f,
                                    fallback.release_ms),
    };
}

float ma_note_frequency_hz(uint8_t note) {
    return note < 128 ? NOTE_HZ[note] : 0.0f;
}

float ma_velocity_level(uint8_t velocity) {
    return velocity < 128 ? VELOCITY_LEVEL[velocity] : 0.0f;
}

float ma_velocity_filter(uint8_t velocity) {
    return velocity < 128 ? VELOCITY_FILTER[velocity] : 0.0f;
}

void ma_synth_init(ma_synth *s, float sample_rate_hz) {
    *s = (ma_synth){
        .sample_rate_hz = tw_sample_rate_hz(sample_rate_hz),
        .vco1 = { .saw_level = 0.70f, .pulse_level = 0.25f,
                  .triangle_level = 0.15f, .pulse_width = 0.50f },
        .vco2 = { .saw_level = 0.35f, .pulse_level = 0.20f,
                  .triangle_level = 0.55f, .pulse_width = 0.50f },
        .vco2_level = 0.62f,
        .vco2_interval = 0,
        .vco2_fine_cents = 7.0f,
        .sync_amount = 0.0f,
        .sync_softness = 0.0f,
        .crossmod_amount = 0.0f,
        .noise_level = 0.02f,
        .mozaik_mix = 0.20f,
        .mozaik_contrast = MOZAIK_GOLDEN_CONTRAST,
        .mozaik_slope_q32 = MOZAIK_GOLDEN_Q32,
        .mozaik_phason_q32 = 0,
        .mozaik_drift = 0.05f,
        .mixer_pressure = 0.15f,
        .filter_cutoff_hz = 900.0f,
        .filter_resonance = 0.18f,
        .filter_drive = 0.12f,
        .filter_env_amount = 0.30f,
        .filter_keytrack = 0.45f,
        .amp_adsr = AMP_ADSR_DEFAULT,
        .filter_adsr = FILTER_ADSR_DEFAULT,
        .macro = { 0 },
        .body_drive = 0.10f,
        .width = 0.70f,
        .master_level = 0.18f,
        .noise_state = NOISE_SEED,
        .oscillator1 = {
            .phase_q48 = 0,
            .triangle = -1.0f,
            .guard_gain = 1.0f,
            .guard_target = 1.0f,
            .triangle_initialized = true,
        },
        .oscillator2 = {
            .phase_q48 = 0,
            .triangle = -1.0f,
            .guard_gain = 1.0f,
            .guard_target = 1.0f,
            .triangle_initialized = true,
        },
        .note = 69,
        .velocity = 0,
        .note_active = false,
    };
    reset_mozaik(&s->mozaik, s->mozaik_phason_q32);
    ma_synth_set_filter(s, s->filter_cutoff_hz, s->filter_resonance,
                        s->filter_drive, s->mixer_pressure);
}

void ma_synth_note_on(ma_synth *s, uint8_t note, uint8_t velocity) {
    if (note >= 128) return;
    if (velocity == 0) {
        ma_synth_note_off(s, note);
        return;
    }
    s->note = note;
    s->velocity = velocity < 128 ? velocity : 127;
    s->note_active = true;
    reset_mozaik(&s->mozaik, s->mozaik_phason_q32);
}

void ma_synth_note_off(ma_synth *s, uint8_t note) {
    if (note < 128 && s->note_active && s->note == note)
        s->note_active = false;
}

static float render_source_substep(ma_synth *s, float noise,
                                   float guard1, float guard2) {
    float oversampled_rate_hz = 8.0f * s->sample_rate_hz;
    float base_hz = ma_note_frequency_hz(s->note);
    float fine_ratio = octave_ratio_small(s->vco2_fine_cents / 1200.0f);
    float vco2_hz = base_hz * interval_ratio(s->vco2_interval) * fine_ratio;
    float vco2_step = phase_step(vco2_hz, oversampled_rate_hz);
    uint64_t vco2_step_q48 = phase_step_q48(vco2_step);
    vco2_step = phase_turns(vco2_step_q48);
    float vco2_preview = preview_oscillator(&s->oscillator2, s->vco2,
                                            vco2_step);
    float cross_ratio = clamp_signal(
        1.0f + vco2_preview * s->crossmod_amount * 0.25f, 0.25f, 4.0f);
    float vco1_hz = base_hz * cross_ratio;
    float vco1_step = phase_step(vco1_hz, oversampled_rate_hz);
    uint64_t vco1_step_q48 = phase_step_q48(vco1_step);
    vco1_step = phase_turns(vco1_step_q48);

    float guard_hz = 0.45f * s->sample_rate_hz;
    set_guard_target(&s->oscillator1, vco1_hz < guard_hz ? 1.0f : 0.0f,
                     s->sample_rate_hz);
    set_guard_target(&s->oscillator2, vco2_hz < guard_hz ? 1.0f : 0.0f,
                     s->sample_rate_hz);
    bool master_wrap = phase_will_wrap(&s->oscillator1, vco1_step_q48);
    float sync = s->sync_amount
               * clamp_signal(1.0f - 0.75f * s->sync_softness, 0.0f, 1.0f);
    float sync_now = 0.0f;
    float sync_next = 0.0f;
    float event_fraction = 1.0f;
    uint64_t sync_reset_phase_q48 = 0;
    if (master_wrap && sync > 0.0f && vco1_step > FLT_EPSILON) {
        uint64_t until_wrap = PHASE_ONE_Q48 - s->oscillator1.phase_q48;
        event_fraction = (float)until_wrap / (float)vco1_step_q48;
        event_fraction = clamp_signal(event_fraction, 0.0f, 1.0f);
        uint64_t event_advance = (uint64_t)((float)vco2_step_q48
                                            * event_fraction);
        uint64_t sync_event_phase_q48 =
            (s->oscillator2.phase_q48 + event_advance) & PHASE_MASK_Q48;
        sync_reset_phase_q48 =
            (uint64_t)((float)sync_event_phase_q48 * (1.0f - sync))
            & PHASE_MASK_Q48;
        float sync_event_phase = phase_turns(sync_event_phase_q48);
        float sync_reset_phase = phase_turns(sync_reset_phase_q48);
        float jump = raw_wave_mix(sync_reset_phase, s->vco2)
                   - raw_wave_mix(sync_event_phase, s->vco2);
        float before = 0.5f * (1.0f - event_fraction);
        float after = 1.0f - 0.5f * event_fraction;
        sync_now = jump * smoothstep5(before);
        sync_next = jump * (smoothstep5(after) - 1.0f);
    }

    float vco1 = render_oscillator(&s->oscillator1, s->vco1, vco1_step, 0.0f);
    float vco2 = render_oscillator(&s->oscillator2, s->vco2, vco2_step,
                                   sync_now);
    s->oscillator2.sync_residual += sync_next;
    advance_phase(&s->oscillator1, vco1_step_q48);
    if (master_wrap && sync > 0.0f) {
        float remaining = 1.0f - event_fraction;
        uint64_t remaining_advance = (uint64_t)((float)vco2_step_q48
                                                * remaining);
        s->oscillator2.phase_q48 =
            (sync_reset_phase_q48 + remaining_advance) & PHASE_MASK_Q48;
        float sync_reset_phase = phase_turns(sync_reset_phase_q48);
        s->oscillator2.triangle = raw_triangle(sync_reset_phase);
        s->oscillator2.triangle = clamp_signal(
            s->oscillator2.triangle
                + pulse_sample(sync_reset_phase, 0.5f,
                               vco2_step * remaining)
                    * 4.0f * vco2_step * remaining,
            -1.25f, 1.25f);
        s->oscillator2.triangle_initialized = true;
    } else {
        advance_phase(&s->oscillator2, vco2_step_q48);
    }

    float vco1_weight = guard1 > 0.0f ? 1.0f : 0.0f;
    float vco2_weight = guard2 > 0.0f ? s->vco2_level : 0.0f;
    float weights = vco1_weight + vco2_weight + s->noise_level;
    if (weights < 1.0f) weights = 1.0f;
    return (guard1 * vco1 + guard2 * s->vco2_level * vco2
            + s->noise_level * noise) / weights;
}

static void push_oversample_history(float history[31], uint8_t *position,
                                    float sample) {
    history[*position] = sample;
    (*position)++;
    if (*position == 31) *position = 0;
}

static float decimate_oversample(const float history[31], uint8_t position) {
    float output = 0.0f;
    int index = position;
    for (int tap = 0; tap < 31; tap++) {
        if (--index < 0) index = 30;
        output += OSC_HALFBAND_31[tap] * history[index];
    }
    return output;
}

static float filter_prewarp(float cutoff_hz, float sample_rate_hz) {
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

static float mix_source_2x(const ma_synth *s, float analog,
                           float guard1, float guard2,
                           float mozaik_gain, float mozaik) {
    if (!(s->mozaik_mix > 0.0f && mozaik_gain > 0.0f)) return analog;
    float vco1_weight = guard1 > 0.0f ? 1.0f : 0.0f;
    float vco2_weight = guard2 > 0.0f ? s->vco2_level : 0.0f;
    float analog_weight = vco1_weight + vco2_weight + s->noise_level;
    if (analog_weight < 1.0f) analog_weight = 1.0f;
    return (analog_weight * analog
            + s->mozaik_mix * mozaik_gain * mozaik)
         / (analog_weight + s->mozaik_mix);
}

#if !defined(MA_SOURCE_EVIDENCE)
static float apply_mixer_pressure(float source, float pressure) {
    if (pressure == 0.0f) return source;
    float gain = 1.0f + 5.0f * pressure;
    float shaped = tw_sat(gain * source) / tw_sat(gain);
    return source + pressure * (shaped - source);
}

static void reset_ladder(ma_ladder *ladder) {
    for (int stage = 0; stage < 4; stage++) ladder->state[stage] = 0.0f;
    ladder->y4_guess = 0.0f;
    ladder->reset_count++;
}

static float render_ladder_2x(ma_synth *s, float input) {
    ma_ladder *ladder = &s->ladder;
    float y[4] = { 0 };
    float v[4] = { 0 };
    float next_state[4] = { 0 };
    float y4_guess = ladder->y4_guess;
    float input_gain = 1.0f + 3.0f * s->filter_drive * s->filter_drive;
    float feedback = 4.65f * s->filter_resonance;
    for (int iteration = 0; iteration < 2; iteration++) {
        float u = tw_sat(input_gain * input - feedback * y4_guess);
        for (int stage = 0; stage < 4; stage++) {
            v[stage] = (tw_sat(u) - ladder->state[stage]) * s->filter_g;
            y[stage] = v[stage] + ladder->state[stage];
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

static float decimate_filter(const ma_ladder *ladder) {
    float output = 0.0f;
    int index = ladder->output_pos;
    for (int tap = 0; tap < 7; tap++) {
        if (--index < 0) index = 6;
        output += FILTER_HALFBAND_7[tap] * ladder->output_history[index];
    }
    return output;
}
#endif

ma_frame ma_synth_tick(ma_synth *s) {
    float guard1 = s->oscillator1.guard_gain;
    float guard2 = s->oscillator2.guard_gain;
    float noise = next_noise(&s->noise_state);
    for (int at_2x = 0; at_2x < 2; at_2x++) {
        for (int at_4x = 0; at_4x < 2; at_4x++) {
            push_oversample_history(
                s->oversample_history[0], &s->oversample_history_pos[0],
                render_source_substep(s, noise, guard1, guard2));
            push_oversample_history(
                s->oversample_history[0], &s->oversample_history_pos[0],
                render_source_substep(s, noise, guard1, guard2));
            float sample_4x = decimate_oversample(
                s->oversample_history[0], s->oversample_history_pos[0]);
            push_oversample_history(
                s->oversample_history[1], &s->oversample_history_pos[1],
                sample_4x);
        }
        float sample_2x = decimate_oversample(
            s->oversample_history[1], s->oversample_history_pos[1]);
#if defined(MA_SOURCE_EVIDENCE)
        push_oversample_history(
            s->oversample_history[2], &s->oversample_history_pos[2],
            sample_2x);
#else
        float mozaik_gain = s->mozaik.guard_gain;
        float mozaik = render_mozaik(s, ma_note_frequency_hz(s->note),
                                     2.0f * s->sample_rate_hz);
        (void)next_mozaik_guard_gain(&s->mozaik);
        float source = mix_source_2x(s, sample_2x, guard1, guard2,
                                     mozaik_gain, mozaik);
        float pressured = apply_mixer_pressure(source, s->mixer_pressure);
        s->ladder.pressure_input = source;
        s->ladder.pressure_output = pressured;
        push_filter_history(&s->ladder, render_ladder_2x(s, pressured));
#endif
    }
    (void)next_guard_gain(&s->oscillator1);
    (void)next_guard_gain(&s->oscillator2);

    float level = s->note_active ? ma_velocity_level(s->velocity) : 0.0f;
#if defined(MA_SOURCE_EVIDENCE)
    float analog = decimate_oversample(
        s->oversample_history[2], s->oversample_history_pos[2]);
    float mozaik_gain = s->mozaik.guard_gain;
    float mozaik = render_mozaik(s, ma_note_frequency_hz(s->note),
                                 s->sample_rate_hz);
    (void)next_mozaik_guard_gain(&s->mozaik);
    float output = mix_source_2x(s, analog, guard1, guard2,
                                 mozaik_gain, mozaik) * level;
#else
    float output = decimate_filter(&s->ladder) * level;
#endif
    return (ma_frame){ .left = output, .right = output };
}

void ma_synth_set_vco1(ma_synth *s, ma_vco_controls controls) {
    static constexpr ma_vco_controls fallback = {
        .saw_level = 0.70f, .pulse_level = 0.25f,
        .triangle_level = 0.15f, .pulse_width = 0.50f,
    };
    s->vco1 = sanitize_vco(controls, fallback);
}

void ma_synth_set_vco2(ma_synth *s, ma_vco_controls controls, float level,
                       int interval, float fine_cents) {
    static constexpr ma_vco_controls fallback = {
        .saw_level = 0.35f, .pulse_level = 0.20f,
        .triangle_level = 0.55f, .pulse_width = 0.50f,
    };
    s->vco2 = sanitize_vco(controls, fallback);
    s->vco2_level = clamp_control(level, 0.0f, 1.0f, 0.62f);
    s->vco2_interval = interval < -24 ? -24 : interval > 24 ? 24 : interval;
    s->vco2_fine_cents = clamp_control(fine_cents, -50.0f, 50.0f, 7.0f);
}

void ma_synth_set_oscillator_modulation(ma_synth *s, float sync_amount,
                                        float sync_softness,
                                        float crossmod_amount,
                                        float noise_level) {
    s->sync_amount = clamp_control(sync_amount, 0.0f, 1.0f, 0.0f);
    s->sync_softness = clamp_control(sync_softness, 0.0f, 1.0f, 0.0f);
    s->crossmod_amount = clamp_control(crossmod_amount, 0.0f, 1.0f, 0.0f);
    s->noise_level = clamp_control(noise_level, 0.0f, 1.0f, 0.02f);
}

void ma_synth_set_mozaik(ma_synth *s, float mix, float slope,
                         float contrast, float phason, float drift) {
    s->mozaik_mix = clamp_control(mix, 0.0f, 1.0f, 0.20f);
    slope = clamp_control(slope, 0.0f, 1.0f,
                          (MOZAIK_DETENT[0].sigma - 0.45f) / 0.30f);
    s->mozaik_slope_q32 = mozaik_slope_q32(slope);
    contrast = clamp_control(contrast, 0.0f, 1.0f,
                             (MOZAIK_GOLDEN_CONTRAST - 1.0f) / 1.2f);
    s->mozaik_contrast = 1.0f + 1.2f * contrast;
    s->mozaik_phason_q32 = mozaik_phason_q32(phason);
    s->mozaik.pending_phason_q32 = s->mozaik_phason_q32;
    s->mozaik.has_pending_phason = true;
    s->mozaik_drift = clamp_control(drift, 0.0f, 1.0f, 0.05f);
}

void ma_synth_set_filter(ma_synth *s, float cutoff_hz, float resonance,
                         float drive, float mixer_pressure) {
    float cutoff_max = 0.42f * s->sample_rate_hz;
    if (cutoff_max > 20000.0f) cutoff_max = 20000.0f;
    s->filter_cutoff_hz = clamp_control(cutoff_hz, 20.0f, cutoff_max, 900.0f);
    s->filter_g = filter_prewarp(s->filter_cutoff_hz, s->sample_rate_hz);
    s->filter_resonance = clamp_control(resonance, 0.0f, 1.0f, 0.18f);
    s->filter_drive = clamp_control(drive, 0.0f, 1.0f, 0.12f);
    s->mixer_pressure = clamp_control(mixer_pressure, 0.0f, 1.0f, 0.15f);
}

void ma_synth_set_amp_adsr(ma_synth *s, ma_adsr adsr) {
    s->amp_adsr = sanitize_adsr(adsr, AMP_ADSR_DEFAULT);
}

void ma_synth_set_filter_adsr(ma_synth *s, ma_adsr adsr) {
    s->filter_adsr = sanitize_adsr(adsr, FILTER_ADSR_DEFAULT);
}

void ma_synth_set_macro(ma_synth *s, ma_macro_id macro, float value) {
    if ((unsigned)macro >= MA_MACRO_COUNT) return;
    s->macro[macro] = clamp_control(value, 0.0f, 1.0f, 0.0f);
}
