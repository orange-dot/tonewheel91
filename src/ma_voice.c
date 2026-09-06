/* Mamut Analog one-voice core. MA1 begins with the pinned pitch/control
 * spine; render state lands behind this concrete public object. */
#include "mamutanalog.h"
#include "ma_raster_table.h"

static constexpr uint64_t NOISE_SEED = UINT64_C(0x4d414e4f49534531);
#if !defined(MA_SOURCE_EVIDENCE)
static constexpr float LN1000 = 6.907755278982137f;
#endif
static constexpr uint32_t MOZAIK_SLOPE_MIN_Q32 = UINT32_C(0x73333333);
static constexpr uint32_t MOZAIK_SLOPE_MAX_Q32 = UINT32_C(0xc0000000);
static constexpr float MOZAIK_GOLDEN_CONTRAST = 1.618034f;
static constexpr float Q32_ONE_F = 0x1p32f;
static constexpr float DC_BLOCK_10_HZ = 62.83185307179586f;
#if !defined(MA_SOURCE_EVIDENCE)
static constexpr float OUTPUT_TINY = 1.0e-9f;
#endif
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

static float filter_prewarp(float cutoff_hz, float sample_rate_hz);

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

const ma_patch ma_patch_tepih = {
    .vco1 = { .saw_level = 0.70f, .pulse_level = 0.25f,
              .triangle_level = 0.15f, .sine_level = 0.20f,
              .pulse_width = 0.50f },
    .vco2 = { .saw_level = 0.35f, .pulse_level = 0.20f,
              .triangle_level = 0.55f, .sine_level = 0.0f,
              .pulse_width = 0.50f },
    .vco2_level = 0.62f, .vco2_interval = 0, .vco2_fine_cents = 7.0f,
    .noise_level = 0.02f,
    .raster_mix = 0.0f,
    .mozaik_mix = 0.20f, .mozaik_slope = 0x1.1ec72ep-1f,
    .mozaik_contrast = 0x1.07b1cap-1f, .mozaik_drift = 0.05f,
    .mixer_pressure = 0.15f,
    .filter_cutoff_hz = 900.0f, .filter_resonance = 0.18f,
    .filter_drive = 0.12f, .filter_env_amount = 0.30f,
    .filter_keytrack = 0.45f,
    .amp_adsr = { 600.0f, 1600.0f, 0.82f, 3000.0f },
    .filter_adsr = { 350.0f, 1800.0f, 0.50f, 2600.0f },
    .body_drive = 0.10f, .width = 0.70f, .master_level = 0.18f,
};

const ma_patch ma_patch_lead = {
    .vco1 = { .saw_level = 0.75f, .pulse_level = 0.30f,
              .triangle_level = 0.05f, .sine_level = 0.10f,
              .pulse_width = 0.43f },
    .vco2 = { .saw_level = 0.55f, .pulse_level = 0.25f,
              .triangle_level = 0.10f, .sine_level = 0.0f,
              .pulse_width = 0.57f },
    .vco2_level = 0.55f, .vco2_interval = 0, .vco2_fine_cents = 7.0f,
    .sync_amount = 0.22f, .sync_softness = 0.18f,
    .crossmod_amount = 0.12f, .noise_level = 0.01f,
    .raster_mix = 0.0f,
    .mozaik_mix = 0.08f, .mozaik_slope = 0x1.1ec72ep-1f,
    .mozaik_contrast = 0.60f, .mozaik_drift = 0.03f,
    .mixer_pressure = 0.32f,
    .filter_cutoff_hz = 1900.0f, .filter_resonance = 0.30f,
    .filter_drive = 0.35f, .filter_env_amount = 0.62f,
    .filter_keytrack = 0.62f,
    .amp_adsr = { 12.0f, 160.0f, 0.68f, 240.0f },
    .filter_adsr = { 6.0f, 260.0f, 0.18f, 220.0f },
    .macro = { 0.12f, 0.04f, 0.25f, 0.22f, 0.03f },
    .body_drive = 0.15f, .width = 0.45f, .crossfeed = 0.08f,
    .master_level = 0.18f,
};

const ma_patch ma_patch_dubina = {
    .vco1 = { .saw_level = 0.10f, .triangle_level = 0.10f,
              .sine_level = 0.15f, .pulse_width = 0.50f },
    .vco2 = { .saw_level = 0.05f, .triangle_level = 0.10f,
              .sine_level = 0.85f, .pulse_width = 0.50f },
    .vco2_level = 0.90f, .vco2_interval = -12,
    .vco2_fine_cents = 0.0f,
    .crossmod_amount = 0.04f, .noise_level = 0.01f,
    .raster_mix = 0.0f,
    .mozaik_mix = 0.05f, .mozaik_slope = 0x1.1ec72ep-1f,
    .mozaik_contrast = 0x1.07b1cap-1f, .mozaik_drift = 0.02f,
    .mixer_pressure = 0.18f,
    .filter_cutoff_hz = 750.0f, .filter_resonance = 0.20f,
    .filter_drive = 0.20f, .filter_env_amount = 0.38f,
    .filter_keytrack = 0.35f,
    .amp_adsr = { 30.0f, 300.0f, 0.80f, 700.0f },
    .filter_adsr = { 20.0f, 450.0f, 0.45f, 600.0f },
    .macro = { 0.20f, 0.0f, 0.18f, 0.04f, 0.0f },
    .body_drive = 0.18f, .width = 0.35f, .crossfeed = 0.12f,
    .master_level = 0.18f,
};

const ma_patch ma_patch_raster = {
    .vco1 = { .pulse_width = 0.50f },
    .vco2 = { .pulse_width = 0.50f },
    .raster_mix = 1.0f, .raster_position = 0.78f, .raster_warp = 0.42f,
    .mozaik_slope = 0x1.1ec72ep-1f,
    .mozaik_contrast = 0x1.07b1cap-1f,
    .filter_cutoff_hz = 5200.0f, .filter_resonance = 0.12f,
    .filter_drive = 0.04f, .filter_env_amount = 0.20f,
    .filter_keytrack = 0.62f,
    .amp_adsr = { 18.0f, 420.0f, 0.72f, 1700.0f },
    .filter_adsr = { 12.0f, 680.0f, 0.34f, 1500.0f },
    .macro = { 0.0f, 0.08f, 0.0f, 0.02f, 0.04f },
    .width = 0.78f, .master_level = 0.90f,
};

const ma_patch ma_patch_prizma = {
    .vco1 = { .saw_level = 0.18f, .triangle_level = 0.24f,
              .sine_level = 0.38f, .pulse_width = 0.50f },
    .vco2 = { .triangle_level = 0.18f, .sine_level = 0.46f,
              .pulse_width = 0.50f },
    .vco2_level = 0.44f, .vco2_fine_cents = 5.0f,
    .noise_level = 0.005f,
    .raster_mix = 0.48f, .raster_position = 0.36f, .raster_warp = 0.18f,
    .mozaik_mix = 0.08f, .mozaik_slope = 0x1.1ec72ep-1f,
    .mozaik_contrast = 0x1.07b1cap-1f, .mozaik_drift = 0.025f,
    .mixer_pressure = 0.08f,
    .filter_cutoff_hz = 1450.0f, .filter_resonance = 0.17f,
    .filter_drive = 0.08f, .filter_env_amount = 0.34f,
    .filter_keytrack = 0.48f,
    .amp_adsr = { 420.0f, 1500.0f, 0.80f, 4300.0f },
    .filter_adsr = { 260.0f, 1900.0f, 0.46f, 3600.0f },
    .macro = { 0.04f, 0.14f, 0.02f, 0.01f, 0.08f },
    .body_drive = 0.04f, .width = 0.76f, .master_level = 0.80f,
};

const ma_patch ma_patch_granica = {
    .vco1 = { .saw_level = 0.10f, .triangle_level = 0.34f,
              .sine_level = 0.48f, .pulse_width = 0.50f },
    .vco2 = { .triangle_level = 0.24f, .sine_level = 0.72f,
              .pulse_width = 0.50f },
    .vco2_level = 0.68f, .vco2_interval = -12, .vco2_fine_cents = 3.0f,
    .sync_amount = 0.03f, .sync_softness = 0.70f,
    .crossmod_amount = 0.05f, .noise_level = 0.004f,
    .raster_mix = 0.10f, .raster_position = 0.68f, .raster_warp = 0.12f,
    .bcs_amount = 0.72f, .bcs_regime = 0.62f,
    .mozaik_mix = 0.04f, .mozaik_slope = 0x1.1ec72ep-1f,
    .mozaik_contrast = 0x1.07b1cap-1f, .mozaik_drift = 0.018f,
    .mixer_pressure = 0.16f,
    .filter_cutoff_hz = 820.0f, .filter_resonance = 0.26f,
    .filter_drive = 0.15f, .filter_env_amount = 0.36f,
    .filter_keytrack = 0.38f,
    .amp_adsr = { 70.0f, 520.0f, 0.78f, 1900.0f },
    .filter_adsr = { 40.0f, 760.0f, 0.38f, 1500.0f },
    .macro = { 0.24f, 0.02f, 0.15f, 0.08f, 0.01f },
    .body_drive = 0.10f, .width = 0.42f, .crossfeed = 0.10f,
    .master_level = 0.54f,
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

static float mamut_sine(float phase) {
    static constexpr float DRIVE = 1.45f;
    static constexpr float SECOND_LEVEL = 0.07f;
    static constexpr float PEAK_NORMALIZATION = 0.95591217f;
    float fundamental = tw_sin_turns(phase);
    float warm = tw_sat(DRIVE * fundamental) / tw_sat(DRIVE);
    float second = tw_sin_turns(wrap_phase(2.0f * phase + 0.07f));
    return PEAK_NORMALIZATION * (warm + SECOND_LEVEL * second);
}

static float waveform_divisor(ma_vco_controls controls) {
    float sum = controls.saw_level + controls.pulse_level
              + controls.triangle_level + controls.sine_level;
    return sum > 1.0f ? sum : 1.0f;
}

static float raw_wave_mix(float phase, ma_vco_controls controls) {
    float saw = 2.0f * phase - 1.0f;
    float pulse = phase < controls.pulse_width ? 1.0f : -1.0f;
    float triangle = raw_triangle(phase);
    float sample = controls.saw_level * saw + controls.pulse_level * pulse
                 + controls.triangle_level * triangle;
    if (controls.sine_level > 0.0f)
        sample += controls.sine_level * mamut_sine(phase);
    return sample / waveform_divisor(controls);
}

static float preview_oscillator(const ma_oscillator *oscillator,
                                ma_vco_controls controls, float step) {
    float phase = phase_turns(oscillator->phase_q48);
    float triangle = oscillator->triangle_initialized
                   ? clamp_signal(oscillator->triangle, -1.0f, 1.0f)
                   : raw_triangle(phase);
    float sample = controls.saw_level * saw_sample(phase, step)
                 + controls.pulse_level * pulse_sample(
                     phase, controls.pulse_width, step)
                 + controls.triangle_level * triangle;
    if (controls.sine_level > 0.0f)
        sample += controls.sine_level * mamut_sine(phase);
    return sample / waveform_divisor(controls);
}

static float render_oscillator(ma_oscillator *oscillator,
                               float preview, float step,
                               float sync_correction) {
    if (!oscillator->triangle_initialized) {
        oscillator->triangle = raw_triangle(
            phase_turns(oscillator->phase_q48));
        oscillator->triangle_initialized = true;
    }
    float sample = preview + oscillator->sync_residual + sync_correction;
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

static float note_position_hz(float note) {
    note = clamp_signal(note, 0.0f, 127.0f);
    uint8_t floor_note = (uint8_t)note;
    float fraction = note - floor_note;
    return NOTE_HZ[floor_note] * octave_ratio_small(fraction / 12.0f);
}

static uint32_t glide_frames(const ma_synth *s) {
    float frames = s->glide.seconds * s->sample_rate_hz + 0.5f;
    if (frames < 1.0f) return 1;
    return (uint32_t)frames;
}

static void glide_to_note(ma_synth *s, uint8_t note) {
    float target = note;
    if (!s->glide.initialized || !s->glide.enabled
        || s->glide.seconds == 0.0f) {
        s->glide.current_note = target;
        s->glide.target_note = target;
        s->glide.step = 0.0f;
        s->glide.remaining = 0;
    } else {
        s->glide.target_note = target;
        s->glide.remaining = glide_frames(s);
        s->glide.step = (target - s->glide.current_note)
                      / (float)s->glide.remaining;
    }
    s->glide.initialized = true;
}

static float next_glide_note(ma_synth *s) {
    float note = s->glide.current_note;
    if (s->glide.remaining) {
        s->glide.current_note += s->glide.step;
        s->glide.remaining--;
        if (!s->glide.remaining)
            s->glide.current_note = s->glide.target_note;
    }
    return note;
}

static float next_lfo(ma_synth *s, const ma_render_controls *controls) {
    if (controls->lfo_depth == 0.0f) return 0.0f;
    float value = controls->lfo_depth * tw_sin_turns(s->lfo.phase);
    s->lfo.phase += controls->lfo_rate_hz / s->sample_rate_hz;
    if (s->lfo.phase >= 1.0f) s->lfo.phase -= 1.0f;
    return value;
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

static ma_smoother initialized_smoother(float value) {
    return (ma_smoother){ .current = value, .target = value };
}

static void set_smoother_target(ma_smoother *smoother, float target,
                                float sample_rate_hz) {
    if (target == smoother->target) return;
    smoother->target = target;
    smoother->remaining = smoothing_frames(sample_rate_hz);
    smoother->step = (target - smoother->current)
                   / (float)smoother->remaining;
}

static void set_bypass_smoother_target(ma_smoother *smoother, float target,
                                       float sample_rate_hz) {
    if (target == 0.0f) {
        *smoother = initialized_smoother(0.0f);
        return;
    }
    set_smoother_target(smoother, target, sample_rate_hz);
}

static float next_smoother(ma_smoother *smoother) {
    if (smoother->remaining == 0) return smoother->current;
    smoother->current += smoother->step;
    smoother->remaining--;
    if (smoother->remaining == 0) smoother->current = smoother->target;
    return smoother->current;
}

static ma_vco_smoothers initialized_vco_smoothers(ma_vco_controls value) {
    return (ma_vco_smoothers){
        .saw_level = initialized_smoother(value.saw_level),
        .pulse_level = initialized_smoother(value.pulse_level),
        .triangle_level = initialized_smoother(value.triangle_level),
        .sine_level = initialized_smoother(value.sine_level),
        .pulse_width = initialized_smoother(value.pulse_width),
    };
}

static void set_vco_targets(ma_vco_smoothers *smoothers,
                            ma_vco_controls value, float sample_rate_hz) {
    set_smoother_target(&smoothers->saw_level, value.saw_level,
                        sample_rate_hz);
    set_smoother_target(&smoothers->pulse_level, value.pulse_level,
                        sample_rate_hz);
    set_smoother_target(&smoothers->triangle_level, value.triangle_level,
                        sample_rate_hz);
    set_smoother_target(&smoothers->sine_level, value.sine_level,
                        sample_rate_hz);
    set_smoother_target(&smoothers->pulse_width, value.pulse_width,
                        sample_rate_hz);
}

static ma_vco_controls next_vco_controls(ma_vco_smoothers *smoothers) {
    return (ma_vco_controls){
        .saw_level = next_smoother(&smoothers->saw_level),
        .pulse_level = next_smoother(&smoothers->pulse_level),
        .triangle_level = next_smoother(&smoothers->triangle_level),
        .sine_level = next_smoother(&smoothers->sine_level),
        .pulse_width = next_smoother(&smoothers->pulse_width),
    };
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

static float next_character(ma_synth *s) {
    ma_character *c = &s->character;
    if (!c->assigned) return 0.0f;
    set_bypass_smoother_target(&c->smoother,
        clamp_control(c->amount, 0.0f, 1.0f, 0.0f), s->sample_rate_hz);
    c->walk_cents = clamp_signal(c->walk_cents + c->walk_step, -3.0f, 3.0f);
    if (++c->walk_phase == 32) {
        c->walk_phase = 0;
        c->walk_cents = c->walk_target;
        c->walk_target = clamp_signal(
            c->walk_target + .03f * next_noise(&c->walk_state), -3.0f, 3.0f);
        c->walk_step = (c->walk_target - c->walk_cents) * (1.0f / 32.0f);
    }
    return next_smoother(&c->smoother);
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

static float late_macro(float value, float threshold) {
    if (value <= threshold) return 0.0f;
    return clamp_signal((value - threshold) / (1.0f - threshold),
                        0.0f, 1.0f);
}

static ma_identity resolve_identity(float gravitacija, float bloom,
                                    float heat, float ruin, float swarm) {
    float late_gravitacija = late_macro(gravitacija, 0.68f);
    float extreme_swarm = late_macro(swarm, 0.82f);
    ma_identity identity = {
        .gravitacija = gravitacija,
        .bloom = bloom,
        .heat = heat,
        .ruin = ruin,
        .swarm = swarm,
    };
    identity.horizont_open = clamp_signal(
        0.70f * bloom + 0.25f * (1.0f - gravitacija)
        - 0.10f * ruin - 0.05f * heat, 0.0f, 1.0f);
    identity.horizont_air = clamp_signal(
        0.78f * bloom + 0.08f * (1.0f - heat)
        - 0.18f * late_gravitacija, 0.0f, 1.0f);
    identity.horizont_span = clamp_signal(
        0.58f * bloom + 0.32f * swarm - 0.16f * gravitacija,
        0.0f, 1.0f);
    identity.pec_mass = clamp_signal(
        0.60f * heat + 0.25f * gravitacija + 0.05f * swarm
        - 0.05f * bloom, 0.0f, 1.0f);
    identity.pec_heat = clamp_signal(
        0.80f * heat + 0.12f * gravitacija, 0.0f, 1.0f);
    identity.pec_pressure = clamp_signal(
        0.58f * gravitacija + 0.24f * heat + 0.08f * ruin,
        0.0f, 1.0f);
    identity.baklja_ready = clamp_signal(
        0.56f * ruin + 0.28f * gravitacija + 0.08f * extreme_swarm,
        0.0f, 1.0f);
    identity.baklja_edge = clamp_signal(
        0.68f * ruin + 0.40f * late_gravitacija, 0.0f, 1.0f);
    identity.baklja_sync_bias = clamp_signal(
        0.72f * ruin + 0.06f * heat, 0.0f, 1.0f);
    identity.grav_pull = gravitacija;
    identity.mass = clamp_signal(
        0.70f * identity.pec_mass + 0.20f * identity.pec_heat
        + 0.10f * identity.grav_pull, 0.0f, 1.0f);
    identity.strain = clamp_signal(
        0.40f * identity.baklja_edge + 0.38f * identity.pec_pressure
        + 0.22f * identity.grav_pull, 0.0f, 1.0f);
    identity.headroom = clamp_signal(
        0.78f + 0.18f * identity.horizont_air + 0.08f * bloom
        - 0.46f * identity.grav_pull - 0.14f * heat
        - 0.08f * identity.baklja_ready, 0.0f, 1.0f);
    identity.body_focus = clamp_signal(
        0.50f * identity.pec_mass + 0.28f * identity.grav_pull
        - 0.18f * bloom, 0.0f, 1.0f);
    identity.rupture_threshold = clamp_signal(
        0.78f - 0.22f * identity.baklja_ready
        - 0.16f * identity.grav_pull, 0.0f, 1.0f);
    identity.rupture_response = clamp_signal(
        0.55f * identity.baklja_edge + 0.25f * identity.strain
        + 0.20f * ruin, 0.0f, 1.0f);
    identity.spatial_dispersion = clamp_signal(
        0.55f * identity.horizont_span + 0.33f * swarm
        - 0.10f * identity.grav_pull, 0.0f, 1.0f);
    return identity;
}

static void resolve_effective_identity(ma_synth *s) {
    float gravitacija = clamp_signal(
        s->macro[MA_MACRO_GRAVITACIJA] + 0.45f * s->channel_pressure,
        0.0f, 1.0f);
    float bloom = clamp_signal(
        s->macro[MA_MACRO_BLOOM] + 0.35f * s->mod_wheel,
        0.0f, 1.0f);
    float heat = s->macro[MA_MACRO_HEAT];
    float ruin = clamp_signal(
        s->macro[MA_MACRO_RUIN] + 0.35f * s->channel_pressure,
        0.0f, 1.0f);
    float swarm = clamp_signal(
        s->macro[MA_MACRO_SWARM] + 0.50f * s->mod_wheel,
        0.0f, 1.0f);
    s->identity = resolve_identity(gravitacija, bloom, heat, ruin, swarm);
}

static ma_render_controls target_render_controls(const ma_synth *s) {
    ma_identity const *identity = &s->identity;
    ma_identity const *zero = &s->identity_zero;
    float cutoff_ratio = 1.0f
                       + (0.62f * identity->bloom
                          + 0.18f * (identity->horizont_air
                                     - zero->horizont_air)
                          - 0.40f * identity->gravitacija) / 0.9144f;
    float cutoff_max = 0.42f * s->sample_rate_hz;
    if (cutoff_max > 20000.0f) cutoff_max = 20000.0f;
    float resonance_delta = 0.14f * identity->baklja_edge
                          + 0.06f * identity->ruin
                          - 0.04f * identity->mass;
    float filter_drive_delta = 0.18f * identity->pec_heat
                             + 0.10f * identity->strain;
    float filter_env_delta = 0.12f * (identity->horizont_open
                                     - zero->horizont_open);
    float keytrack_ratio = 1.0f - 0.12f * identity->mass / 0.92f;
    float sync_delta = 0.30f * identity->baklja_sync_bias;
    float crossmod_delta = 0.24f * identity->baklja_ready;
    float width_delta = 0.18f * identity->horizont_span
                      - 0.20f * identity->body_focus;
    float crossfeed_delta = 0.28f * identity->body_focus
                          + 0.10f * identity->grav_pull
                          - 0.10f * identity->spatial_dispersion;
    float body_drive_delta = 0.25f * identity->body_focus;
    float body_load_ratio = 1.0f + 0.14f * identity->mass
                          + 0.08f * identity->strain
                          + 0.25f * (zero->headroom - identity->headroom);
    return (ma_render_controls){
        .vco1 = s->vco1,
        .vco2 = s->vco2,
        .vco2_level = s->vco2_level,
        .vco2_fine_cents = s->vco2_fine_cents,
        .sync_amount = clamp_signal(s->sync_amount + sync_delta,
                                    0.0f, 1.0f),
        .sync_softness = s->sync_softness,
        .crossmod_amount = clamp_signal(s->crossmod_amount + crossmod_delta,
                                        0.0f, 1.0f),
        .noise_level = s->noise_level,
        .raster_mix = s->raster_mix,
        .raster_position = s->raster_position,
        .raster_warp = s->raster_warp,
        .bcs_amount = s->bcs_amount,
        .bcs_regime = s->bcs_regime,
        .mozaik_mix = clamp_signal(s->mozaik_mix
                                   + 0.10f * identity->bloom, 0.0f, 1.0f),
        .mozaik_slope_q32 = mozaik_slope_q32(s->mozaik_slope),
        .mozaik_contrast = 1.0f + 1.2f * clamp_signal(
            s->mozaik_contrast_control + 0.20f * identity->heat,
            0.0f, 1.0f),
        .mozaik_drift = clamp_signal(s->mozaik_drift
                                     + 0.35f * identity->swarm,
                                     0.0f, 1.0f),
        .mixer_pressure = s->mixer_pressure,
        .filter_cutoff_hz = clamp_signal(s->filter_cutoff_hz * cutoff_ratio,
                                         20.0f, cutoff_max),
        .filter_resonance = clamp_signal(s->filter_resonance
                                         + resonance_delta, 0.0f, 1.0f),
        .filter_drive = clamp_signal(s->filter_drive + filter_drive_delta,
                                     0.0f, 1.0f),
        .filter_env_amount = clamp_signal(s->filter_env_amount
                                          + filter_env_delta, 0.0f, 1.0f),
        .filter_keytrack = clamp_signal(s->filter_keytrack * keytrack_ratio,
                                        0.0f, 1.0f),
        .pitch_bend_semitones = s->pitch_bend_semitones,
        .poly_pressure = s->poly_pressure,
        .lfo_depth = s->lfo.depth,
        .lfo_rate_hz = s->lfo.rate_hz,
        .body_drive = clamp_signal(s->body_drive + body_drive_delta,
                                   0.0f, 1.0f),
        .body_load_ratio = clamp_signal(body_load_ratio, 0.75f, 1.50f),
        .width = clamp_signal(s->width + width_delta, 0.0f, 1.0f),
        .crossfeed = clamp_signal(s->crossfeed + crossfeed_delta,
                                  0.0f, 1.0f),
    };
}

static void initialize_control_smoothers(ma_synth *s) {
    ma_render_controls target = target_render_controls(s);
    s->smoothers = (ma_control_smoothers){
        .vco1 = initialized_vco_smoothers(target.vco1),
        .vco2 = initialized_vco_smoothers(target.vco2),
        .vco2_level = initialized_smoother(target.vco2_level),
        .vco2_fine_cents = initialized_smoother(target.vco2_fine_cents),
        .sync_amount = initialized_smoother(target.sync_amount),
        .sync_softness = initialized_smoother(target.sync_softness),
        .crossmod_amount = initialized_smoother(target.crossmod_amount),
        .noise_level = initialized_smoother(target.noise_level),
        .raster_mix = initialized_smoother(target.raster_mix),
        .raster_position = initialized_smoother(target.raster_position),
        .raster_warp = initialized_smoother(target.raster_warp),
        .bcs_amount = initialized_smoother(target.bcs_amount),
        .bcs_regime = initialized_smoother(target.bcs_regime),
        .mozaik_mix = initialized_smoother(target.mozaik_mix),
        .mozaik_slope = initialized_smoother(s->mozaik_slope),
        .mozaik_contrast = initialized_smoother(target.mozaik_contrast),
        .mozaik_drift = initialized_smoother(target.mozaik_drift),
        .mixer_pressure = initialized_smoother(target.mixer_pressure),
        .filter_cutoff_hz = initialized_smoother(target.filter_cutoff_hz),
        .filter_resonance = initialized_smoother(target.filter_resonance),
        .filter_drive = initialized_smoother(target.filter_drive),
        .filter_env_amount = initialized_smoother(target.filter_env_amount),
        .filter_keytrack = initialized_smoother(target.filter_keytrack),
        .pitch_bend_semitones = initialized_smoother(
            target.pitch_bend_semitones),
        .poly_pressure = initialized_smoother(target.poly_pressure),
        .lfo_depth = initialized_smoother(target.lfo_depth),
        .lfo_rate_hz = initialized_smoother(target.lfo_rate_hz),
        .body_drive = initialized_smoother(target.body_drive),
        .body_load_ratio = initialized_smoother(target.body_load_ratio),
        .width = initialized_smoother(target.width),
        .crossfeed = initialized_smoother(target.crossfeed),
    };
}

static void update_control_targets(ma_synth *s) {
    ma_render_controls target = target_render_controls(s);
    set_vco_targets(&s->smoothers.vco1, target.vco1, s->sample_rate_hz);
    set_vco_targets(&s->smoothers.vco2, target.vco2, s->sample_rate_hz);
    set_smoother_target(&s->smoothers.vco2_level, target.vco2_level,
                        s->sample_rate_hz);
    set_smoother_target(&s->smoothers.vco2_fine_cents,
                        target.vco2_fine_cents, s->sample_rate_hz);
    set_smoother_target(&s->smoothers.sync_amount, target.sync_amount,
                        s->sample_rate_hz);
    set_smoother_target(&s->smoothers.sync_softness, target.sync_softness,
                        s->sample_rate_hz);
    set_smoother_target(&s->smoothers.crossmod_amount,
                        target.crossmod_amount, s->sample_rate_hz);
    set_smoother_target(&s->smoothers.noise_level, target.noise_level,
                        s->sample_rate_hz);
    set_bypass_smoother_target(&s->smoothers.raster_mix, target.raster_mix,
                               s->sample_rate_hz);
    set_smoother_target(&s->smoothers.raster_position,
                        target.raster_position, s->sample_rate_hz);
    set_smoother_target(&s->smoothers.raster_warp, target.raster_warp,
                        s->sample_rate_hz);
    set_bypass_smoother_target(&s->smoothers.bcs_amount, target.bcs_amount,
                               s->sample_rate_hz);
    set_smoother_target(&s->smoothers.bcs_regime, target.bcs_regime,
                        s->sample_rate_hz);
    set_bypass_smoother_target(&s->smoothers.mozaik_mix, target.mozaik_mix,
                               s->sample_rate_hz);
    set_smoother_target(&s->smoothers.mozaik_slope, s->mozaik_slope,
                        s->sample_rate_hz);
    set_smoother_target(&s->smoothers.mozaik_contrast,
                        target.mozaik_contrast, s->sample_rate_hz);
    set_smoother_target(&s->smoothers.mozaik_drift, target.mozaik_drift,
                        s->sample_rate_hz);
    set_bypass_smoother_target(&s->smoothers.mixer_pressure,
                               target.mixer_pressure, s->sample_rate_hz);
    set_smoother_target(&s->smoothers.filter_cutoff_hz,
                        target.filter_cutoff_hz, s->sample_rate_hz);
    set_smoother_target(&s->smoothers.filter_resonance,
                        target.filter_resonance, s->sample_rate_hz);
    set_smoother_target(&s->smoothers.filter_drive, target.filter_drive,
                        s->sample_rate_hz);
    set_smoother_target(&s->smoothers.filter_env_amount,
                        target.filter_env_amount, s->sample_rate_hz);
    set_smoother_target(&s->smoothers.filter_keytrack,
                        target.filter_keytrack, s->sample_rate_hz);
    set_smoother_target(&s->smoothers.pitch_bend_semitones,
                        target.pitch_bend_semitones, s->sample_rate_hz);
    set_smoother_target(&s->smoothers.poly_pressure, target.poly_pressure,
                        s->sample_rate_hz);
    set_bypass_smoother_target(&s->smoothers.lfo_depth, target.lfo_depth,
                               s->sample_rate_hz);
    set_smoother_target(&s->smoothers.lfo_rate_hz, target.lfo_rate_hz,
                        s->sample_rate_hz);
    set_smoother_target(&s->smoothers.body_drive, target.body_drive,
                        s->sample_rate_hz);
    set_smoother_target(&s->smoothers.body_load_ratio,
                        target.body_load_ratio, s->sample_rate_hz);
    set_smoother_target(&s->smoothers.width, target.width,
                        s->sample_rate_hz);
    set_smoother_target(&s->smoothers.crossfeed, target.crossfeed,
                        s->sample_rate_hz);
}

static ma_render_controls next_render_controls(ma_synth *s) {
    float slope = next_smoother(&s->smoothers.mozaik_slope);
    return (ma_render_controls){
        .vco1 = next_vco_controls(&s->smoothers.vco1),
        .vco2 = next_vco_controls(&s->smoothers.vco2),
        .vco2_level = next_smoother(&s->smoothers.vco2_level),
        .vco2_fine_cents = next_smoother(&s->smoothers.vco2_fine_cents),
        .sync_amount = next_smoother(&s->smoothers.sync_amount),
        .sync_softness = next_smoother(&s->smoothers.sync_softness),
        .crossmod_amount = next_smoother(&s->smoothers.crossmod_amount),
        .noise_level = next_smoother(&s->smoothers.noise_level),
        .raster_mix = next_smoother(&s->smoothers.raster_mix),
        .raster_position = next_smoother(&s->smoothers.raster_position),
        .raster_warp = next_smoother(&s->smoothers.raster_warp),
        .bcs_amount = next_smoother(&s->smoothers.bcs_amount),
        .bcs_regime = next_smoother(&s->smoothers.bcs_regime),
        .mozaik_mix = next_smoother(&s->smoothers.mozaik_mix),
        .mozaik_slope_q32 = mozaik_slope_q32(slope),
        .mozaik_contrast = next_smoother(&s->smoothers.mozaik_contrast),
        .mozaik_drift = next_smoother(&s->smoothers.mozaik_drift),
        .mixer_pressure = next_smoother(&s->smoothers.mixer_pressure),
        .filter_cutoff_hz = next_smoother(&s->smoothers.filter_cutoff_hz),
        .filter_resonance = next_smoother(&s->smoothers.filter_resonance),
        .filter_drive = next_smoother(&s->smoothers.filter_drive),
        .filter_env_amount = next_smoother(
            &s->smoothers.filter_env_amount),
        .filter_keytrack = next_smoother(&s->smoothers.filter_keytrack),
        .pitch_bend_semitones = next_smoother(
            &s->smoothers.pitch_bend_semitones),
        .poly_pressure = next_smoother(&s->smoothers.poly_pressure),
        .lfo_depth = next_smoother(&s->smoothers.lfo_depth),
        .lfo_rate_hz = next_smoother(&s->smoothers.lfo_rate_hz),
        .body_drive = next_smoother(&s->smoothers.body_drive),
        .body_load_ratio = next_smoother(&s->smoothers.body_load_ratio),
        .width = next_smoother(&s->smoothers.width),
        .crossfeed = next_smoother(&s->smoothers.crossfeed),
    };
}

static uint32_t effective_phason_q32(const ma_synth *s) {
    uint32_t identity_delta = (uint32_t)(uint64_t)(
        0.15f * s->identity.ruin * Q32_ONE_F);
    return s->mozaik_phason_q32 + identity_delta;
}

static void queue_effective_phason(ma_synth *s) {
    uint32_t target = effective_phason_q32(s);
    uint32_t current_target = s->mozaik.has_pending_phason
                            ? s->mozaik.pending_phason_q32
                            : s->mozaik.applied_phason_q32;
    if (target == current_target) return;
    s->mozaik.pending_phason_q32 = target;
    s->mozaik.has_pending_phason = true;
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

static void start_mozaik_tile(ma_synth *s,
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

static float render_mozaik(ma_synth *s,
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

static ma_vco_controls sanitize_vco(ma_vco_controls controls,
                                    ma_vco_controls fallback) {
    return (ma_vco_controls){
        .saw_level = clamp_control(controls.saw_level, 0.0f, 1.0f,
                                   fallback.saw_level),
        .pulse_level = clamp_control(controls.pulse_level, 0.0f, 1.0f,
                                     fallback.pulse_level),
        .triangle_level = clamp_control(controls.triangle_level, 0.0f, 1.0f,
                                        fallback.triangle_level),
        .sine_level = clamp_control(controls.sine_level, 0.0f, 1.0f,
                                    fallback.sine_level),
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

static ma_patch sanitize_patch(const ma_patch *value, float sample_rate_hz) {
    const ma_patch *source = value ? value : &ma_patch_tepih;
    float cutoff_max = 0.42f * sample_rate_hz;
    if (cutoff_max > 20000.0f) cutoff_max = 20000.0f;
    ma_patch result = {
        .vco1 = sanitize_vco(source->vco1, ma_patch_tepih.vco1),
        .vco2 = sanitize_vco(source->vco2, ma_patch_tepih.vco2),
        .vco2_level = clamp_control(source->vco2_level, 0.0f, 1.0f,
                                    ma_patch_tepih.vco2_level),
        .vco2_interval = source->vco2_interval < -24 ? -24
                       : source->vco2_interval > 24 ? 24
                       : source->vco2_interval,
        .vco2_fine_cents = clamp_control(
            source->vco2_fine_cents, -50.0f, 50.0f,
            ma_patch_tepih.vco2_fine_cents),
        .sync_amount = clamp_control(source->sync_amount, 0.0f, 1.0f, 0.0f),
        .sync_softness = clamp_control(source->sync_softness,
                                       0.0f, 1.0f, 0.0f),
        .crossmod_amount = clamp_control(source->crossmod_amount,
                                         0.0f, 1.0f, 0.0f),
        .noise_level = clamp_control(source->noise_level, 0.0f, 1.0f, 0.02f),
        .raster_mix = clamp_control(source->raster_mix, 0.0f, 1.0f, 0.0f),
        .raster_position = clamp_control(source->raster_position,
                                         0.0f, 1.0f, 0.0f),
        .raster_warp = clamp_control(source->raster_warp,
                                     0.0f, 1.0f, 0.0f),
        .bcs_amount = clamp_control(source->bcs_amount, 0.0f, 1.0f, 0.0f),
        .bcs_regime = clamp_control(source->bcs_regime, 0.0f, 1.0f, 0.0f),
        .mozaik_mix = clamp_control(source->mozaik_mix, 0.0f, 1.0f, 0.20f),
        .mozaik_slope = clamp_control(source->mozaik_slope,
                                      0.0f, 1.0f, 0.5601133f),
        .mozaik_contrast = clamp_control(source->mozaik_contrast,
                                         0.0f, 1.0f, 0.5150284f),
        .mozaik_phason = finite_or(source->mozaik_phason, 0.0f),
        .mozaik_drift = clamp_control(source->mozaik_drift,
                                      0.0f, 1.0f, 0.05f),
        .mixer_pressure = clamp_control(source->mixer_pressure,
                                        0.0f, 1.0f, 0.15f),
        .filter_cutoff_hz = clamp_control(source->filter_cutoff_hz,
                                          20.0f, cutoff_max, 900.0f),
        .filter_resonance = clamp_control(source->filter_resonance,
                                           0.0f, 1.0f, 0.18f),
        .filter_drive = clamp_control(source->filter_drive,
                                      0.0f, 1.0f, 0.12f),
        .filter_env_amount = clamp_control(source->filter_env_amount,
                                            0.0f, 1.0f, 0.30f),
        .filter_keytrack = clamp_control(source->filter_keytrack,
                                          0.0f, 1.0f, 0.45f),
        .amp_adsr = sanitize_adsr(source->amp_adsr, AMP_ADSR_DEFAULT),
        .filter_adsr = sanitize_adsr(source->filter_adsr,
                                     FILTER_ADSR_DEFAULT),
        .body_drive = clamp_control(source->body_drive, 0.0f, 1.0f, 0.10f),
        .width = clamp_control(source->width, 0.0f, 1.0f, 0.70f),
        .crossfeed = clamp_control(source->crossfeed, 0.0f, 1.0f, 0.0f),
        .master_level = clamp_control(source->master_level,
                                      0.0f, 1.0f, 0.18f),
    };
    for (int macro = 0; macro < MA_MACRO_COUNT; macro++)
        result.macro[macro] = clamp_control(source->macro[macro],
                                            0.0f, 1.0f, 0.0f);
    return result;
}

static void start_envelope_stage(ma_envelope *envelope,
                                 ma_envelope_stage stage, float target) {
    float error = 0.001f * tw_fabsf(target - envelope->level);
    envelope->completion_error = error > 1.0e-9f ? error : 1.0e-9f;
    envelope->stage = stage;
}

#if !defined(MA_SOURCE_EVIDENCE)
static float envelope_coefficient(float time_ms, float sample_rate_hz) {
    float x = LN1000 / (0.001f * time_ms * sample_rate_hz);
    return tw_one_pole_coeff(x);
}

static float render_envelope(ma_envelope *envelope, ma_adsr controls,
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

static float filter_envelope_amount(const ma_synth *s,
                                    const ma_render_controls *controls) {
    return clamp_signal(controls->filter_env_amount
                        + 0.25f * ma_velocity_filter(s->velocity),
                        0.0f, 1.0f);
}

static float effective_filter_cutoff(const ma_synth *s,
                                     const ma_render_controls *controls,
                                     float envelope_amount,
                                     float note_position) {
    float envelope = 1.0f + 0.78f * s->filter_envelope.level
                                  * envelope_amount;
    float keytrack = 1.0f
                   + (note_position - 60.0f) * (1.0f / 48.0f)
                   * controls->filter_keytrack * 0.42f;
    keytrack = clamp_signal(keytrack, 0.55f, 1.35f);
    float cutoff = controls->filter_cutoff_hz * envelope * keytrack;
    if (controls->poly_pressure > 0.0f)
        cutoff *= octave_ratio_small(0.25f * controls->poly_pressure);
    if (s->character.smoother.current > 0.0f)
        cutoff *= octave_ratio_small(s->character.smoother.current
                                     * s->character.cutoff_octaves);
    float cutoff_max = 0.42f * s->sample_rate_hz;
    if (cutoff_max > 20000.0f) cutoff_max = 20000.0f;
    return clamp_signal(cutoff, 20.0f, cutoff_max);
}
#endif

float ma_note_frequency_hz(uint8_t note) {
    return note < 128 ? NOTE_HZ[note] : 0.0f;
}

float ma_velocity_level(uint8_t velocity) {
    return velocity < 128 ? VELOCITY_LEVEL[velocity] : 0.0f;
}

float ma_velocity_filter(uint8_t velocity) {
    return velocity < 128 ? VELOCITY_FILTER[velocity] : 0.0f;
}

float ma_safety_curve(float sample) {
    if (!(sample >= -FLT_MAX && sample <= FLT_MAX)) return 0.0f;
    float magnitude = tw_fabsf(sample);
    if (magnitude <= SAFETY_KNEE) return sample;
    float t = (magnitude - SAFETY_KNEE) / SAFETY_RANGE;
    t = clamp_signal(t, 0.0f, 1.0f);
    float limited = SAFETY_KNEE
                  + SAFETY_RANGE * (t + t * t - t * t * t);
    return sample < 0.0f ? -limited : limited;
}

void ma_synth_init_patch(ma_synth *s, float sample_rate_hz,
                         const ma_patch *patch) {
    sample_rate_hz = tw_sample_rate_hz(sample_rate_hz);
    ma_patch value = sanitize_patch(patch, sample_rate_hz);
    *s = (ma_synth){
        .sample_rate_hz = sample_rate_hz,
        .vco1 = value.vco1,
        .vco2 = value.vco2,
        .vco2_level = value.vco2_level,
        .vco2_interval = value.vco2_interval,
        .vco2_fine_cents = value.vco2_fine_cents,
        .sync_amount = value.sync_amount,
        .sync_softness = value.sync_softness,
        .crossmod_amount = value.crossmod_amount,
        .noise_level = value.noise_level,
        .raster_mix = value.raster_mix,
        .raster_position = value.raster_position,
        .raster_warp = value.raster_warp,
        .bcs_amount = value.bcs_amount,
        .bcs_regime = value.bcs_regime,
        .mozaik_mix = value.mozaik_mix,
        .mozaik_slope = value.mozaik_slope,
        .mozaik_contrast_control = value.mozaik_contrast,
        .mozaik_contrast = 1.0f + 1.2f * value.mozaik_contrast,
        .mozaik_slope_q32 = mozaik_slope_q32(value.mozaik_slope),
        .mozaik_phason_q32 = mozaik_phason_q32(value.mozaik_phason),
        .mozaik_drift = value.mozaik_drift,
        .mixer_pressure = value.mixer_pressure,
        .filter_cutoff_hz = value.filter_cutoff_hz,
        .filter_cutoff_effective_hz = value.filter_cutoff_hz,
        .filter_resonance = value.filter_resonance,
        .filter_drive = value.filter_drive,
        .filter_env_amount = value.filter_env_amount,
        .filter_keytrack = value.filter_keytrack,
        .amp_adsr = value.amp_adsr,
        .filter_adsr = value.filter_adsr,
        .pitch_bend_semitones = 0.0f,
        .glide = {
            .current_note = 69.0f,
            .target_note = 69.0f,
        },
        .lfo = { .rate_hz = LFO_RATE_DEFAULT_HZ },
        .channel_pressure = 0.0f,
        .poly_pressure = 0.0f,
        .mod_wheel = 0.0f,
        .body_drive = value.body_drive,
        .body_load_ratio = 1.0f,
        .width = value.width,
        .crossfeed = value.crossfeed,
        .master_level = value.master_level,
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
        .bcs = {
            .hopf_re = 0.025f,
            .duffing_x = 0.001f,
        },
        .note = 69,
        .velocity = 0,
        .channel = 0,
        .note_active = false,
    };
    for (int macro = 0; macro < MA_MACRO_COUNT; macro++)
        s->macro[macro] = value.macro[macro];
    s->identity_zero = resolve_identity(0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    resolve_effective_identity(s);
    initialize_control_smoothers(s);
    reset_mozaik(&s->mozaik, effective_phason_q32(s));
    s->filter_g = filter_prewarp(s->filter_cutoff_hz, s->sample_rate_hz);
    tw_drive_init(&s->output.body, s->sample_rate_hz);
    s->output.dc_coefficient = tw_one_pole_coeff(
        DC_BLOCK_10_HZ / s->sample_rate_hz);
}

void ma_synth_init(ma_synth *s, float sample_rate_hz) {
    ma_synth_init_patch(s, sample_rate_hz, &ma_patch_tepih);
}

void ma_synth_apply_patch(ma_synth *s, const ma_patch *patch) {
    ma_character character = s->character;
    ma_synth_init_patch(s, s->sample_rate_hz, patch);
    s->character = character;
}

void ma_synth_note_on(ma_synth *s, uint8_t channel, uint8_t note,
                      uint8_t velocity) {
    if (channel >= 16 || note >= 128) return;
    if (velocity == 0) {
        ma_synth_note_off(s, channel, note, 0);
        return;
    }
    s->channel = channel;
    s->note = note;
    glide_to_note(s, note);
    s->velocity = velocity < 128 ? velocity : 127;
    s->note_active = true;
    s->poly_pressure = 0.0f;
    s->smoothers.poly_pressure = initialized_smoother(0.0f);
    start_envelope_stage(&s->amp_envelope, MA_ENVELOPE_ATTACK, 1.0f);
    start_envelope_stage(&s->filter_envelope, MA_ENVELOPE_ATTACK, 1.0f);
    reset_mozaik(&s->mozaik, effective_phason_q32(s));
}

void ma_synth_note_off(ma_synth *s, uint8_t channel, uint8_t note,
                       uint8_t release_velocity) {
    if (channel >= 16 || note >= 128) return;
    (void)release_velocity;
    s->ignored_release_velocities++;
    if (s->note_active && s->channel == channel && s->note == note) {
        s->note_active = false;
        if (s->amp_envelope.stage != MA_ENVELOPE_IDLE)
            start_envelope_stage(&s->amp_envelope, MA_ENVELOPE_RELEASE, 0.0f);
        if (s->filter_envelope.stage != MA_ENVELOPE_IDLE)
            start_envelope_stage(&s->filter_envelope,
                                 MA_ENVELOPE_RELEASE, 0.0f);
    }
}

/* These destinations are held over all eight source substeps. */
typedef struct {
    ma_vco_controls vco1;
    ma_vco_controls vco2;
    float rate_hz;
    float vco2_hz;
    float vco2_step;
    uint64_t vco2_step_q48;
    float envelope_ratio;
    float sync;
} ma_source_frame;

static ma_source_frame prepare_source_frame(const ma_synth *s,
                                             const ma_render_controls *controls,
                                             float base_hz,
                                             float filter_modulation) {
    ma_source_frame source = {
        .vco1 = controls->vco1,
        .vco2 = controls->vco2,
        .rate_hz = 8.0f * s->sample_rate_hz,
        .envelope_ratio = 1.0f,
        .sync = controls->sync_amount
              * clamp_signal(1.0f - 0.75f * controls->sync_softness,
                             0.0f, 1.0f),
    };
#if !defined(MA_SOURCE_EVIDENCE)
    source.vco1.pulse_width = clamp_signal(
        source.vco1.pulse_width + 0.120f * filter_modulation,
        0.05f, 0.95f);
    source.vco2.pulse_width = clamp_signal(
        source.vco2.pulse_width - 0.080f * filter_modulation,
        0.05f, 0.95f);
    source.envelope_ratio = octave_ratio_small(0.125f * filter_modulation);
#else
    (void)filter_modulation;
#endif
    float fine_ratio = octave_ratio_small(
        controls->vco2_fine_cents / 1200.0f);
    source.vco2_hz = base_hz * interval_ratio(s->vco2_interval) * fine_ratio;
    if (s->character.smoother.current > 0.0f)
        source.vco2_hz *= octave_ratio_small(s->character.smoother.current
                                            * s->character.vco2_cents / 1200.0f);
    float step = phase_step(source.vco2_hz, source.rate_hz);
    source.vco2_step_q48 = phase_step_q48(step);
    source.vco2_step = phase_turns(source.vco2_step_q48);
    return source;
}

static float render_source_substep(ma_synth *s,
                                   const ma_render_controls *controls,
                                   const ma_source_frame *source,
                                   float base_hz, float noise,
                                   float guard1, float guard2) {
    ma_vco_controls vco1_controls = source->vco1;
    ma_vco_controls vco2_controls = source->vco2;
    float vco2_step = source->vco2_step;
    uint64_t vco2_step_q48 = source->vco2_step_q48;
    float vco2_preview = preview_oscillator(&s->oscillator2, vco2_controls,
                                            vco2_step);
    float cross_ratio = clamp_signal(
        1.0f + vco2_preview * controls->crossmod_amount * 0.25f,
        0.25f, 4.0f);
    float vco1_hz = base_hz * cross_ratio;
#if !defined(MA_SOURCE_EVIDENCE)
    vco1_hz *= source->envelope_ratio;
#endif
    float vco1_step = phase_step(vco1_hz, source->rate_hz);
    uint64_t vco1_step_q48 = phase_step_q48(vco1_step);
    vco1_step = phase_turns(vco1_step_q48);

    float guard_hz = 0.45f * s->sample_rate_hz;
    set_guard_target(&s->oscillator1, vco1_hz < guard_hz ? 1.0f : 0.0f,
                     s->sample_rate_hz);
    set_guard_target(&s->oscillator2, source->vco2_hz < guard_hz ? 1.0f : 0.0f,
                     s->sample_rate_hz);
    bool master_wrap = phase_will_wrap(&s->oscillator1, vco1_step_q48);
    float sync = source->sync;
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
        float jump = raw_wave_mix(sync_reset_phase, vco2_controls)
                   - raw_wave_mix(sync_event_phase, vco2_controls);
        float before = 0.5f * (1.0f - event_fraction);
        float after = 1.0f - 0.5f * event_fraction;
        sync_now = jump * smoothstep5(before);
        sync_next = jump * (smoothstep5(after) - 1.0f);
    }

    float vco1_preview = preview_oscillator(&s->oscillator1, vco1_controls,
                                            vco1_step);
    float vco1 = render_oscillator(&s->oscillator1, vco1_preview,
                                   vco1_step, 0.0f);
    /* Cross-mod already read VCO2 at this phase, before sync correction. */
    float vco2 = render_oscillator(&s->oscillator2, vco2_preview,
                                   vco2_step, sync_now);
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
    float vco2_weight = guard2 > 0.0f ? controls->vco2_level : 0.0f;
    float weights = vco1_weight + vco2_weight + controls->noise_level;
    if (weights < 1.0f) weights = 1.0f;
    return (guard1 * vco1 + guard2 * controls->vco2_level * vco2
            + controls->noise_level * noise) / weights;
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

static uint8_t raster_mip(float phase_step, float warp, float *blend) {
    static constexpr uint8_t harmonic_limit[MA_RASTER_MIPS] = {
        64, 32, 16, 8, 4, 2, 1,
    };
    float effective_step = phase_step * (1.0f + 0.8f * warp);
    uint8_t mip = 0;
    while (mip + 1 < MA_RASTER_MIPS
           && effective_step * harmonic_limit[mip] > 0.45f)
        mip++;
    float load = effective_step * harmonic_limit[mip];
    *blend = mip + 1 < MA_RASTER_MIPS
           ? clamp_signal((load - 0.225f) * (1.0f / 0.225f), 0.0f, 1.0f)
           : 0.0f;
    return mip;
}

static float raster_table_sample(uint8_t family, uint8_t mip, float phase) {
    float table_phase = phase * (float)MA_RASTER_SAMPLES;
    uint16_t index = (uint16_t)table_phase & (MA_RASTER_SAMPLES - 1);
    uint16_t next = (index + 1) & (MA_RASTER_SAMPLES - 1);
    float fraction = table_phase - (float)(uint16_t)table_phase;
    float first = (float)MA_RASTER_TABLE[family][mip][index]
                * (1.0f / 32768.0f);
    float second = (float)MA_RASTER_TABLE[family][mip][next]
                 * (1.0f / 32768.0f);
    return first + fraction * (second - first);
}

static float render_raster(ma_synth *s, const ma_render_controls *controls,
                           float frequency_hz, float render_rate_hz) {
    float step = phase_step(frequency_hz, render_rate_hz);
    uint64_t step_q48 = phase_step_q48(step);
    float phase = phase_turns(s->raster.phase_q48);
    float warped = phase
                 + 0.12f * controls->raster_warp * tw_sin_turns(phase);
    if (warped < 0.0f) warped += 1.0f;
    if (warped >= 1.0f) warped -= 1.0f;

    float family_position = controls->raster_position
                          * (float)(MA_RASTER_FAMILIES - 1);
    uint8_t family = (uint8_t)family_position;
    float morph = family_position - (float)family;
    uint8_t next_family = family;
    if (family + 1 < MA_RASTER_FAMILIES) next_family++;
    float mip_blend = 0.0f;
    s->raster.mip = raster_mip(step, controls->raster_warp, &mip_blend);
    float first = raster_table_sample(family, s->raster.mip, warped);
    float second = raster_table_sample(next_family, s->raster.mip, warped);
    float sample = first + morph * (second - first);
    if (mip_blend > 0.0f) {
        uint8_t coarse_mip = s->raster.mip + 1;
        first = raster_table_sample(family, coarse_mip, warped);
        second = raster_table_sample(next_family, coarse_mip, warped);
        float coarse = first + morph * (second - first);
        sample += mip_blend * (coarse - sample);
    }
    s->raster.phase_q48 = (s->raster.phase_q48 + step_q48)
                        & PHASE_MASK_Q48;
    return sample;
}

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

static float bcs_lerp(float first, float second, float fraction) {
    return first + fraction * (second - first);
}

static ma_bcs_parameters bcs_parameters(float regime) {
    float position = regime * 3.0f;
    int landmark = (int)position;
    if (landmark > 2) landmark = 2;
    float fraction = position - (float)landmark;
    ma_bcs_parameters const *first = &BCS_LANDMARK[landmark];
    ma_bcs_parameters const *second = &BCS_LANDMARK[landmark + 1];
#define BCS_INTERPOLATE(member) \
    .member = bcs_lerp(first->member, second->member, fraction)
    return (ma_bcs_parameters){
        BCS_INTERPOLATE(hopf_mu),
        BCS_INTERPOLATE(hopf_nonlinearity),
        BCS_INTERPOLATE(duffing_frequency_ratio),
        BCS_INTERPOLATE(duffing_damping),
        BCS_INTERPOLATE(duffing_edge),
        BCS_INTERPOLATE(duffing_cubic_stiffness),
        BCS_INTERPOLATE(duffing_cubic_damping),
        BCS_INTERPOLATE(duffing_drive),
        BCS_INTERPOLATE(duffing_feedback),
        BCS_INTERPOLATE(hopf_mix),
        BCS_INTERPOLATE(duffing_mix),
        BCS_INTERPOLATE(output_gain),
    };
#undef BCS_INTERPOLATE
}

static ma_bcs_vector bcs_derivative(ma_bcs_vector state,
                                    ma_bcs_parameters parameters,
                                    float omega, float external_drive) {
    float radius2 = state.hopf_re * state.hopf_re
                  + state.hopf_im * state.hopf_im;
    float radial = parameters.hopf_mu
                 - parameters.hopf_nonlinearity * radius2;
    float duffing_omega = omega * parameters.duffing_frequency_ratio;
    float square = state.duffing_x * state.duffing_x;
    float edge_damping = (parameters.duffing_edge
                          - parameters.duffing_damping) * duffing_omega;
    float nonlinear_damping = parameters.duffing_cubic_damping
                            * duffing_omega * square;
    float cubic = parameters.duffing_cubic_stiffness * duffing_omega
                * square * state.duffing_x;
    float drive = parameters.duffing_drive * state.hopf_re + external_drive;
    return (ma_bcs_vector){
        .hopf_re = radial * state.hopf_re - omega * state.hopf_im
                 + parameters.duffing_feedback * omega * state.duffing_x,
        .hopf_im = omega * state.hopf_re + radial * state.hopf_im,
        .duffing_x = duffing_omega * state.duffing_v,
        .duffing_v = (edge_damping - nonlinear_damping) * state.duffing_v
                   - duffing_omega * state.duffing_x - cubic
                   + duffing_omega * drive,
    };
}

static ma_bcs_vector bcs_advance(ma_bcs_vector state,
                                 ma_bcs_vector derivative, float scale) {
    return (ma_bcs_vector){
        .hopf_re = state.hopf_re + scale * derivative.hopf_re,
        .hopf_im = state.hopf_im + scale * derivative.hopf_im,
        .duffing_x = state.duffing_x + scale * derivative.duffing_x,
        .duffing_v = state.duffing_v + scale * derivative.duffing_v,
    };
}

static void reset_bcs(ma_bcs *bcs) {
    bcs->hopf_re = 0.025f;
    bcs->hopf_im = 0.0f;
    bcs->duffing_x = 0.001f;
    bcs->duffing_v = 0.0f;
    bcs->output = 0.0f;
    bcs->reset_count++;
}

static bool bcs_state_is_safe(ma_bcs_vector state, float *maximum) {
    float values[4] = {
        state.hopf_re, state.hopf_im, state.duffing_x, state.duffing_v,
    };
    float peak = 0.0f;
    for (int i = 0; i < 4; i++) {
        if (!(values[i] >= -FLT_MAX && values[i] <= FLT_MAX)
            || tw_fabsf(values[i]) > BCS_STATE_CEILING)
            return false;
        if (tw_fabsf(values[i]) > peak) peak = tw_fabsf(values[i]);
    }
    *maximum = peak;
    return true;
}

static float render_bcs(ma_synth *s, float frequency_hz, float amount,
                        float regime, float feedback_sample) {
    ma_bcs_parameters parameters = bcs_parameters(regime);
    float upper_hz = 0.125f * s->sample_rate_hz;
    if (upper_hz > 8000.0f) upper_hz = 8000.0f;
    frequency_hz = clamp_signal(frequency_hz, 20.0f, upper_hz);
    float omega = BCS_TAU * frequency_hz;
    float step = 1.0f / (s->sample_rate_hz * BCS_INTEGRATION_STEPS);
    float external_drive = 0.018f * amount
                         * clamp_signal(feedback_sample, -1.0f, 1.0f);
    ma_bcs_vector state = {
        s->bcs.hopf_re, s->bcs.hopf_im, s->bcs.duffing_x, s->bcs.duffing_v,
    };
    for (int substep = 0; substep < BCS_INTEGRATION_STEPS; substep++) {
        ma_bcs_vector k1 = bcs_derivative(state, parameters, omega,
                                          external_drive);
        ma_bcs_vector k2 = bcs_derivative(
            bcs_advance(state, k1, 0.5f * step), parameters, omega,
            external_drive);
        ma_bcs_vector k3 = bcs_derivative(
            bcs_advance(state, k2, 0.5f * step), parameters, omega,
            external_drive);
        ma_bcs_vector k4 = bcs_derivative(
            bcs_advance(state, k3, step), parameters, omega,
            external_drive);
        state.hopf_re += (step / 6.0f)
                       * (k1.hopf_re + 2.0f * k2.hopf_re
                          + 2.0f * k3.hopf_re + k4.hopf_re);
        state.hopf_im += (step / 6.0f)
                       * (k1.hopf_im + 2.0f * k2.hopf_im
                          + 2.0f * k3.hopf_im + k4.hopf_im);
        state.duffing_x += (step / 6.0f)
                         * (k1.duffing_x + 2.0f * k2.duffing_x
                            + 2.0f * k3.duffing_x + k4.duffing_x);
        state.duffing_v += (step / 6.0f)
                         * (k1.duffing_v + 2.0f * k2.duffing_v
                            + 2.0f * k3.duffing_v + k4.duffing_v);
    }
    float maximum = 0.0f;
    if (!bcs_state_is_safe(state, &maximum)) {
        reset_bcs(&s->bcs);
        return 0.0f;
    }
    s->bcs.hopf_re = state.hopf_re;
    s->bcs.hopf_im = state.hopf_im;
    s->bcs.duffing_x = state.duffing_x;
    s->bcs.duffing_v = state.duffing_v;
    if (maximum > s->bcs.maximum_state) s->bcs.maximum_state = maximum;
    s->bcs.output = tw_sat((parameters.hopf_mix * state.hopf_re
                            + parameters.duffing_mix * state.duffing_x)
                           * parameters.output_gain);
    return s->bcs.output;
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

static float mix_source_2x(const ma_render_controls *controls, float analog,
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
        float u = tw_sat(input_gain * input - feedback * y4_guess);
        for (int stage = 0; stage < 4; stage++) {
            v[stage] = (tw_sat(u) - ladder->state[stage]) * filter_g;
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

static float output_dc_tick(ma_output_state *output, float input, float *lp) {
    if (!(input >= -FLT_MAX && input <= FLT_MAX)) return input;
    float next = *lp + output->dc_coefficient * (input - *lp);
    if (next != 0.0f && tw_fabsf(next) < OUTPUT_TINY) {
        next = 0.0f;
        output->diagnostics.tiny_flush_count++;
    }
    *lp = next;
    return input - next;
}

static float output_safety_tick(ma_output_state *output, float input) {
    if (!(input >= -FLT_MAX && input <= FLT_MAX)) {
        output->diagnostics.sanitization_count++;
        return 0.0f;
    }
    float magnitude = tw_fabsf(input);
    if (magnitude > output->diagnostics.pre_peak)
        output->diagnostics.pre_peak = magnitude;
    if (magnitude > SAFETY_KNEE) output->diagnostics.knee_hit_count++;
    float sample = ma_safety_curve(input);
    float limited = tw_fabsf(sample);
    if (limited > output->diagnostics.post_peak)
        output->diagnostics.post_peak = limited;
    float reduction = magnitude > limited ? magnitude - limited : 0.0f;
    if (reduction > output->diagnostics.maximum_reduction)
        output->diagnostics.maximum_reduction = reduction;
    return sample;
}

static ma_frame render_output(ma_synth *s,
                              const ma_render_controls *controls,
                              float mono) {
    ma_output_state *output = &s->output;
    output->pre_body = mono;
    if (!(mono >= -FLT_MAX && mono <= FLT_MAX)) {
        output->post_body = mono;
        return (ma_frame){
            .left = output_safety_tick(output, mono),
            .right = output_safety_tick(output, mono),
        };
    }

    float body = mono;
    if (controls->body_drive > 0.0f) {
        tw_drive_set(&output->body, controls->body_drive);
        body = 0.25f * tw_drive_tick(
            &output->body, 4.0f * controls->body_load_ratio * mono);
    }
    output->post_body = body;

    float left = output_dc_tick(output, body, &output->dc_lp_left)
               * s->master_level;
    float right = output_dc_tick(output, body, &output->dc_lp_right)
                * s->master_level;
    return (ma_frame){
        .left = output_safety_tick(output, left),
        .right = output_safety_tick(output, right),
    };
}
#endif

#if !defined(MA_SOURCE_EVIDENCE)
static ma_adsr character_adsr(ma_adsr authored, ma_envelope_time_bias bias,
                              float amount) {
    if (amount > 0.0f) {
        authored.attack_ms *= 1.0f + amount * bias.attack;
        authored.decay_ms *= 1.0f + amount * bias.decay;
        authored.release_ms *= 1.0f + amount * bias.release;
    }
    return authored;
}
#endif

ma_frame ma_synth_tick(ma_synth *s) {
    ma_render_controls controls = next_render_controls(s);
    float character = next_character(s);
    float note_position = next_glide_note(s);
    float pitch_delta = controls.pitch_bend_semitones + next_lfo(s, &controls);
    float base_hz = note_position_hz(note_position);
    if (pitch_delta != 0.0f)
        base_hz *= octave_ratio_small(pitch_delta / 12.0f);
    if (character > 0.0f)
        base_hz *= octave_ratio_small(character
            * (s->character.vco1_cents + s->character.walk_cents) / 1200.0f);
    if (controls.bcs_amount > 0.0f) {
        float bcs = render_bcs(s, base_hz, controls.bcs_amount,
                               controls.bcs_regime, s->ladder.y4_guess);
        float crossmod_depth = 0.10f + 0.20f * controls.bcs_regime;
        float resonance_depth = 0.08f + 0.08f * controls.bcs_regime;
        controls.crossmod_amount = clamp_signal(
            controls.crossmod_amount
                + controls.bcs_amount * crossmod_depth * bcs,
            0.0f, 1.0f);
        controls.filter_resonance = clamp_signal(
            controls.filter_resonance
                + controls.bcs_amount * resonance_depth * bcs,
            0.0f, 1.0f);
    }
#if !defined(MA_SOURCE_EVIDENCE)
    ma_adsr filter_adsr = character_adsr(s->filter_adsr,
        s->character.filter_time_bias, character);
    ma_adsr amp_adsr = character_adsr(s->amp_adsr,
        s->character.amp_time_bias, character);
    (void)render_envelope(&s->filter_envelope, filter_adsr,
                          s->sample_rate_hz);
    (void)render_envelope(&s->amp_envelope, amp_adsr,
                          s->sample_rate_hz);
    float envelope_amount = filter_envelope_amount(s, &controls);
    float filter_modulation = s->filter_envelope.level * envelope_amount;
    s->filter_cutoff_effective_hz = effective_filter_cutoff(
        s, &controls, envelope_amount, note_position);
    float filter_g = filter_prewarp(s->filter_cutoff_effective_hz,
                                    s->sample_rate_hz);
#else
    float filter_modulation = 0.0f;
#endif
    float guard1 = s->oscillator1.guard_gain;
    float guard2 = s->oscillator2.guard_gain;
    float noise = next_noise(&s->noise_state);
    ma_source_frame source_frame = prepare_source_frame(
        s, &controls, base_hz, filter_modulation);
    for (int at_2x = 0; at_2x < 2; at_2x++) {
        for (int at_4x = 0; at_4x < 2; at_4x++) {
            push_oversample_history(
                s->oversample_history[0], &s->oversample_history_pos[0],
                render_source_substep(s, &controls, &source_frame,
                                      base_hz, noise, guard1, guard2));
            push_oversample_history(
                s->oversample_history[0], &s->oversample_history_pos[0],
                render_source_substep(s, &controls, &source_frame,
                                      base_hz, noise, guard1, guard2));
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
        float mozaik = render_mozaik(s, &controls, base_hz,
                                     2.0f * s->sample_rate_hz);
        (void)next_mozaik_guard_gain(&s->mozaik);
        float raster = controls.raster_mix > 0.0f
                     ? render_raster(s, &controls, base_hz,
                                     2.0f * s->sample_rate_hz)
                     : 0.0f;
        float source = mix_source_2x(&controls, sample_2x, guard1, guard2,
                                     mozaik_gain, mozaik, raster);
        float pressured = apply_mixer_pressure(source,
                                                controls.mixer_pressure);
        s->ladder.pressure_input = source;
        s->ladder.pressure_output = pressured;
        push_filter_history(&s->ladder,
                            render_ladder_2x(s, &controls, pressured,
                                             filter_g));
#endif
    }
    (void)next_guard_gain(&s->oscillator1);
    (void)next_guard_gain(&s->oscillator2);

#if defined(MA_SOURCE_EVIDENCE)
    float level = s->note_active ? ma_velocity_level(s->velocity) : 0.0f;
    float analog = decimate_oversample(
        s->oversample_history[2], s->oversample_history_pos[2]);
    float mozaik_gain = s->mozaik.guard_gain;
    float mozaik = render_mozaik(s, &controls, base_hz,
                                 s->sample_rate_hz);
    (void)next_mozaik_guard_gain(&s->mozaik);
    float raster = controls.raster_mix > 0.0f
                 ? render_raster(s, &controls, base_hz, s->sample_rate_hz)
                 : 0.0f;
    float output = mix_source_2x(&controls, analog, guard1, guard2,
                                 mozaik_gain, mozaik, raster) * level;
#else
    float velocity_gain = 0.25f + 0.75f * ma_velocity_level(s->velocity);
    float output = decimate_filter(&s->ladder)
                 * s->amp_envelope.level * velocity_gain;
#endif
    if (controls.poly_pressure > 0.0f)
        output *= 1.0f + 0.10f * controls.poly_pressure;
    if (character > 0.0f)
        output *= 1.0f + character * s->character.vca_bias;
#if defined(MA_SOURCE_EVIDENCE)
    return (ma_frame){ .left = output, .right = output };
#else
    return render_output(s, &controls, output);
#endif
}

void ma_synth_set_vco1(ma_synth *s, ma_vco_controls controls) {
    static constexpr ma_vco_controls fallback = {
        .saw_level = 0.70f, .pulse_level = 0.25f,
        .triangle_level = 0.15f, .sine_level = 0.20f,
        .pulse_width = 0.50f,
    };
    s->vco1 = sanitize_vco(controls, fallback);
    update_control_targets(s);
}

void ma_synth_set_vco2(ma_synth *s, ma_vco_controls controls, float level,
                       int interval, float fine_cents) {
    static constexpr ma_vco_controls fallback = {
        .saw_level = 0.35f, .pulse_level = 0.20f,
        .triangle_level = 0.55f, .sine_level = 0.0f,
        .pulse_width = 0.50f,
    };
    s->vco2 = sanitize_vco(controls, fallback);
    s->vco2_level = clamp_control(level, 0.0f, 1.0f, 0.62f);
    s->vco2_interval = interval < -24 ? -24 : interval > 24 ? 24 : interval;
    s->vco2_fine_cents = clamp_control(fine_cents, -50.0f, 50.0f, 7.0f);
    update_control_targets(s);
}

void ma_synth_set_oscillator_modulation(ma_synth *s, float sync_amount,
                                        float sync_softness,
                                        float crossmod_amount,
                                        float noise_level) {
    s->sync_amount = clamp_control(sync_amount, 0.0f, 1.0f, 0.0f);
    s->sync_softness = clamp_control(sync_softness, 0.0f, 1.0f, 0.0f);
    s->crossmod_amount = clamp_control(crossmod_amount, 0.0f, 1.0f, 0.0f);
    s->noise_level = clamp_control(noise_level, 0.0f, 1.0f, 0.02f);
    update_control_targets(s);
}

void ma_synth_set_raster(ma_synth *s, float mix, float position, float warp) {
    s->raster_mix = clamp_control(mix, 0.0f, 1.0f, 0.0f);
    s->raster_position = clamp_control(position, 0.0f, 1.0f, 0.0f);
    s->raster_warp = clamp_control(warp, 0.0f, 1.0f, 0.0f);
    update_control_targets(s);
}

void ma_synth_set_bcs(ma_synth *s, float amount, float regime) {
    s->bcs_amount = clamp_control(amount, 0.0f, 1.0f, 0.0f);
    s->bcs_regime = clamp_control(regime, 0.0f, 1.0f, 0.0f);
    update_control_targets(s);
}

void ma_synth_set_mozaik(ma_synth *s, float mix, float slope,
                         float contrast, float phason, float drift) {
    s->mozaik_mix = clamp_control(mix, 0.0f, 1.0f, 0.20f);
    slope = clamp_control(slope, 0.0f, 1.0f,
                          (MOZAIK_DETENT[0].sigma - 0.45f) / 0.30f);
    s->mozaik_slope = slope;
    s->mozaik_slope_q32 = mozaik_slope_q32(slope);
    contrast = clamp_control(contrast, 0.0f, 1.0f,
                             (MOZAIK_GOLDEN_CONTRAST - 1.0f) / 1.2f);
    s->mozaik_contrast_control = contrast;
    s->mozaik_contrast = 1.0f + 1.2f * contrast;
    s->mozaik_phason_q32 = mozaik_phason_q32(phason);
    s->mozaik_drift = clamp_control(drift, 0.0f, 1.0f, 0.05f);
    update_control_targets(s);
    queue_effective_phason(s);
}

void ma_synth_set_filter(ma_synth *s, float cutoff_hz, float resonance,
                         float drive, float mixer_pressure) {
    float cutoff_max = 0.42f * s->sample_rate_hz;
    if (cutoff_max > 20000.0f) cutoff_max = 20000.0f;
    s->filter_cutoff_hz = clamp_control(cutoff_hz, 20.0f, cutoff_max, 900.0f);
    s->filter_g = filter_prewarp(s->filter_cutoff_hz, s->sample_rate_hz);
    s->filter_cutoff_effective_hz = s->filter_cutoff_hz;
    s->filter_resonance = clamp_control(resonance, 0.0f, 1.0f, 0.18f);
    s->filter_drive = clamp_control(drive, 0.0f, 1.0f, 0.12f);
    s->mixer_pressure = clamp_control(mixer_pressure, 0.0f, 1.0f, 0.15f);
    update_control_targets(s);
}

void ma_synth_set_filter_modulation(ma_synth *s, float envelope_amount,
                                    float keytrack) {
    s->filter_env_amount = clamp_control(envelope_amount, 0.0f, 1.0f, 0.30f);
    s->filter_keytrack = clamp_control(keytrack, 0.0f, 1.0f, 0.45f);
    update_control_targets(s);
}

void ma_synth_set_amp_adsr(ma_synth *s, ma_adsr adsr) {
    s->amp_adsr = sanitize_adsr(adsr, AMP_ADSR_DEFAULT);
}

void ma_synth_set_filter_adsr(ma_synth *s, ma_adsr adsr) {
    s->filter_adsr = sanitize_adsr(adsr, FILTER_ADSR_DEFAULT);
}

void ma_synth_set_glide(ma_synth *s, bool enabled, float seconds) {
    s->glide.enabled = enabled;
    s->glide.seconds = clamp_control(seconds, 0.0f, 10.0f, 0.0f);
    if (!enabled || s->glide.seconds == 0.0f) {
        s->glide.current_note = s->glide.target_note;
        s->glide.step = 0.0f;
        s->glide.remaining = 0;
    } else if (s->glide.remaining) {
        s->glide.remaining = glide_frames(s);
        s->glide.step = (s->glide.target_note - s->glide.current_note)
                      / (float)s->glide.remaining;
    }
}

void ma_synth_set_lfo(ma_synth *s, float depth, float rate_hz) {
    s->lfo.depth = clamp_control(depth, 0.0f, 1.0f, 0.0f);
    s->lfo.rate_hz = clamp_control(rate_hz, 0.03f, 20.0f,
                                   LFO_RATE_DEFAULT_HZ);
    update_control_targets(s);
}

void ma_synth_set_macro(ma_synth *s, ma_macro_id macro, float value) {
    if ((unsigned)macro >= MA_MACRO_COUNT) return;
    s->macro[macro] = clamp_control(value, 0.0f, 1.0f, 0.0f);
    resolve_effective_identity(s);
    update_control_targets(s);
    queue_effective_phason(s);
}

void ma_synth_set_output(ma_synth *s, float body_drive, float width,
                         float crossfeed, float master_level) {
    s->body_drive = clamp_control(body_drive, 0.0f, 1.0f, 0.10f);
    s->width = clamp_control(width, 0.0f, 1.0f, 0.70f);
    s->crossfeed = clamp_control(crossfeed, 0.0f, 1.0f, 0.0f);
    s->master_level = clamp_control(master_level, 0.0f, 1.0f, 0.18f);
    update_control_targets(s);
}

void ma_synth_set_pitch_bend(ma_synth *s, float semitones) {
    s->pitch_bend_semitones = clamp_control(semitones, -2.0f, 2.0f, 0.0f);
    update_control_targets(s);
}

void ma_synth_set_channel_pressure(ma_synth *s, float pressure) {
    s->channel_pressure = clamp_control(pressure, 0.0f, 1.0f, 0.0f);
    resolve_effective_identity(s);
    update_control_targets(s);
    queue_effective_phason(s);
}

void ma_synth_set_poly_pressure(ma_synth *s, uint8_t channel, uint8_t note,
                                float pressure) {
    if (channel >= 16 || note >= 128 || s->channel != channel
        || s->note != note || s->amp_envelope.stage == MA_ENVELOPE_IDLE)
        return;
    s->poly_pressure = clamp_control(pressure, 0.0f, 1.0f, 0.0f);
    set_smoother_target(&s->smoothers.poly_pressure, s->poly_pressure,
                        s->sample_rate_hz);
}

void ma_synth_set_mod_wheel(ma_synth *s, float amount) {
    s->mod_wheel = clamp_control(amount, 0.0f, 1.0f, 0.0f);
    resolve_effective_identity(s);
    update_control_targets(s);
}
