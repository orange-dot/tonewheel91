/* Glas: formant bank, Orbita formants and formant grains for one voice.
 * Constants and contracts: docs/ma-constants.md 10C. The grain step and the
 * formant bank are the Glas kernel (ma_glas_kernel.h) at width one; the
 * vector lanes run the same kernel and call the per-voice parts here. */
#include "ma_voice_impl.h"
#define ML_W 1
#include "ma_lane_ops.h"
#include "ma_glas_kernel.h"

static constexpr float GLAS_VOWEL[5][MA_FORMANTS] = {
    { 300.0f,  870.0f, 2240.0f, 3300.0f },  /* u */
    { 570.0f,  840.0f, 2410.0f, 3300.0f },  /* o */
    { 730.0f, 1090.0f, 2440.0f, 3300.0f },  /* a */
    { 530.0f, 1840.0f, 2480.0f, 3300.0f },  /* e */
    { 270.0f, 2290.0f, 3010.0f, 3300.0f },  /* i */
};
static constexpr float GLAS_BANDWIDTH[MA_FORMANTS] = { 80.0f, 90.0f, 120.0f, 160.0f };
static constexpr float FORMANT_MIN_HZ = 60.0f;
static constexpr float FORMANT_MAX_RATIO = 0.40f;
static constexpr float FORMANT_K_MIN = 0.02f;
static constexpr float FORMANT_K_MAX = 2.0f;
static constexpr float GRAIN_ATTACK_S = 0.002f;
static constexpr float GRAIN_F0_MIN = 20.0f;
static constexpr float GRAIN_F0_MAX = 2000.0f;
static constexpr float PI_F = 3.14159265358979f;
static constexpr uint8_t GLAS_TICK = 32;
static constexpr float ORBITA_RATE = 0.12f;
static constexpr float CAPTURE_TOL = 0.002f;
static constexpr uint8_t CAPTURE_TICKS = 8;
static constexpr float CAPTURED_SLEW = 0.25f;
static constexpr float SNAP_TOL = 1.0e-6f;
static constexpr float RELEASE_MARGIN = 0.65f;

void ma_glas_init(ma_glas *g, float mix, float vowel, float shift, float orbita,
                  float grain_mix) {
    *g = (ma_glas){
        .formant_mix = mix,
        .formant_vowel = vowel,
        .formant_shift = shift,
        .formant_orbita = orbita,
        .grain_mix = grain_mix,
        .mix_smoother = initialized_smoother(mix),
        .vowel_smoother = initialized_smoother(vowel),
        .shift_smoother = initialized_smoother(shift),
        .grain_smoother = initialized_smoother(grain_mix),
        .retrigger = true,
    };
    for (int i = 0; i < MA_FORMANTS; i++)
        g->moon[i] = (ma_formant_moon){ .captured = -1, .candidate = -1 };
}

void ma_glas_set_formant(ma_glas *g, float mix, float vowel, float shift,
                         float orbita, float sample_rate_hz) {
    g->formant_mix = clamp_control(mix, 0.0f, 1.0f, 0.0f);
    g->formant_vowel = clamp_control(vowel, 0.0f, 1.0f, 0.25f);
    g->formant_shift = clamp_control(shift, -1.0f, 1.0f, 0.0f);
    g->formant_orbita = clamp_control(orbita, 0.0f, 1.0f, 0.0f);
    set_bypass_smoother_target(&g->mix_smoother, g->formant_mix, sample_rate_hz);
    set_smoother_target(&g->vowel_smoother, g->formant_vowel, sample_rate_hz);
    set_smoother_target(&g->shift_smoother, g->formant_shift, sample_rate_hz);
}

void ma_glas_set_grain(ma_glas *g, float mix, float sample_rate_hz) {
    g->grain_mix = clamp_control(mix, 0.0f, 1.0f, 0.0f);
    set_bypass_smoother_target(&g->grain_smoother, g->grain_mix, sample_rate_hz);
}

bool ma_glas_active(const ma_glas *g) {
    return g->mix_smoother.current > 0.0f || g->mix_smoother.target > 0.0f
        || g->grain_smoother.current > 0.0f || g->grain_smoother.target > 0.0f;
}

static float advance_smoother(ma_smoother *smoother) {
    if (smoother->remaining == 0) return smoother->current;
    smoother->current += smoother->step;
    smoother->remaining--;
    if (smoother->remaining == 0) smoother->current = smoother->target;
    return smoother->current;
}

static float vowel_hz(int formant, float vowel) {
    float p = 4.0f * vowel;
    int i = (int)p;
    if (i > 3) i = 3;
    if (i < 0) i = 0;
    float t = p - (float)i;
    return GLAS_VOWEL[i][formant]
         + t * (GLAS_VOWEL[i + 1][formant] - GLAS_VOWEL[i][formant]);
}

static int nearest_harmonic(float h) {
    if (!(h >= 1.0f)) return 1;
    if (h > 30000.0f) h = 30000.0f;
    return (int)(h + 0.5f);
}

void ma_glas_reset_state(ma_glas *g) {
    for (int i = 0; i < MA_FORMANTS; i++) {
        g->svf[i] = (ma_formant_svf){ 0 };
        for (int j = 0; j < MA_GRAINS_PER_FORMANT; j++)
            g->grain[i][j] = (ma_grain){ 0 };
    }
    g->pulse_phase = 0.0f;
    g->retrigger = true;
    g->resets++;
}

void ma_glas_tick_controls(ma_glas *g) {
    (void)advance_smoother(&g->vowel_smoother);
    (void)advance_smoother(&g->shift_smoother);
    (void)advance_smoother(&g->mix_smoother);
    (void)advance_smoother(&g->grain_smoother);
}

void ma_glas_snap_controls(ma_glas *g) {
    g->vowel_smoother = initialized_smoother(g->formant_vowel);
    g->shift_smoother = initialized_smoother(g->formant_shift);
    g->mix_smoother = initialized_smoother(g->formant_mix);
    g->grain_smoother = initialized_smoother(g->grain_mix);
}

/* One Orbita tick: moon dynamics and the formant coefficients. */
static void glas_tick(ma_glas *g, float f0, float sample_rate_hz) {
    float vowel = g->vowel_smoother.current;
    float shift = g->shift_smoother.current;
    float r = ma_octave_ratio_small(0.25f * shift);
    float scale = r * r;
    float d = g->formant_orbita;
    float rate = ORBITA_RATE * d * d;
    float top = FORMANT_MAX_RATIO * sample_rate_hz;
    if (!(f0 >= GRAIN_F0_MIN)) f0 = GRAIN_F0_MIN;
    for (int i = 0; i < MA_FORMANTS; i++) {
        ma_formant_moon *m = &g->moon[i];
        float home = vowel_hz(i, vowel) * scale / f0;
        int n = nearest_harmonic(home);
        if (!(m->ratio >= -FLT_MAX && m->ratio <= FLT_MAX)) {
            g->resets++;
            g->retrigger = true;
        }
        if (d == 0.0f) {
            m->ratio = home;
            m->captured = m->candidate = -1;
            m->hold = 0;
        } else if (g->retrigger) {
            m->ratio = (float)n;
            m->captured = (int16_t)n;
            m->candidate = -1;
            m->hold = 0;
        } else {
            if (m->captured >= 1) {
                float c = (float)m->captured;
                if (tw_fabsf(home - c) > RELEASE_MARGIN) {
                    m->captured = -1;
                    g->releases++;
                } else {
                    m->ratio += (c - m->ratio) * CAPTURED_SLEW;
                    if (tw_fabsf(m->ratio - c) <= SNAP_TOL * c) m->ratio = c;
                }
            }
            if (m->captured < 1) {
                float target = (float)n;
                m->ratio -= rate * (m->ratio - target);
                if (tw_fabsf(m->ratio - target) < CAPTURE_TOL * target) {
                    if (m->candidate == n) m->hold++;
                    else { m->candidate = (int16_t)n; m->hold = 1; }
                    if (m->hold >= CAPTURE_TICKS) {
                        m->captured = (int16_t)n;
                        m->candidate = -1;
                        m->hold = 0;
                        g->captures++;
                    }
                } else {
                    m->candidate = -1;
                    m->hold = 0;
                }
            }
        }
        float centre = clamp_signal(f0 * m->ratio, FORMANT_MIN_HZ, top);
        g->centre_hz[i] = centre;
        /* ma_filter_prewarp is pinned for the 2x ladder: it maps c to
         * tan(pi c / (2 fs)), so 2F gives the 1x coefficient. */
        float G = ma_filter_prewarp(2.0f * centre, sample_rate_hz);
        g->g[i] = G / (1.0f - G);
        g->k[i] = clamp_signal(GLAS_BANDWIDTH[i] / centre, FORMANT_K_MIN,
                               FORMANT_K_MAX);
    }
    g->retrigger = false;
    g->coefficients_ready = true;
    g->coefficient_epoch++;
}

ma_grain ma_glas_new_grain(const ma_glas *g, int i, float render_rate_hz) {
    float x = PI_F * GLAS_BANDWIDTH[i] / render_rate_hz;
    return (ma_grain){
        .phase = 0.0f,
        .step = clamp_signal(g->centre_hz[i] / render_rate_hz, 0.0f, 0.45f),
        .envelope = 1.0f,
        .decay = 1.0f - tw_one_pole_coeff(x),
        .attack_pos = 0.0f,
        .attack_step = 1.0f / (GRAIN_ATTACK_S * render_rate_hz),
        .gain = GLAS_GAIN[i],
        .active = true,
    };
}

static void launch_grains(ma_glas *g, float render_rate_hz) {
    for (int i = 0; i < MA_FORMANTS; i++) {
        uint8_t slot = g->grain_cursor[i];
        g->grain_cursor[i] = (uint8_t)((slot + 1u) % MA_GRAINS_PER_FORMANT);
        if (g->grain[i][slot].active) g->grains_stolen++;
        g->grain[i][slot] = ma_glas_new_grain(g, i, render_rate_hz);
        g->grains_launched++;
    }
}

/* The pitch-synchronous pulse: true when this 2x sample launches grains. */
bool ma_glas_pulse(ma_glas *g, float f0, float render_rate_hz) {
    g->pulse_phase += f0 / render_rate_hz;
    if (g->pulse_phase >= 1.0f) {
        g->pulse_phase -= (float)(int)g->pulse_phase;
        return f0 >= GRAIN_F0_MIN && f0 <= GRAIN_F0_MAX;
    }
    return false;
}

static float render_grains(ma_glas *g, float f0, float render_rate_hz) {
    if (ma_glas_pulse(g, f0, render_rate_hz)) launch_grains(g, render_rate_hz);
    float y = 0.0f;
    for (int i = 0; i < MA_FORMANTS; i++) {
        for (int j = 0; j < MA_GRAINS_PER_FORMANT; j++) {
            ma_grain *q = &g->grain[i][j];
            ml_grain grain = {
                .phase = q->phase, .step = q->step, .envelope = q->envelope,
                .decay = q->decay, .attack_pos = q->attack_pos,
                .attack_step = q->attack_step, .gain = q->gain, .active = q->active,
            };
            y = ml_grain_step(&grain, true, y);
            q->phase = grain.phase;
            q->envelope = grain.envelope;
            q->attack_pos = grain.attack_pos;
            q->active = grain.active;
        }
    }
    return GRAIN_NORM * y;
}

/* Once per frame while Glas is active, before the 2x source/ladder steps:
 * smoothers, the Orbita tick, and two 2x grain samples. Returns the
 * smoothed grain mix; zero leaves the grain state untouched. */
float ma_glas_prepare_controls(ma_synth *s, float f0) {
    ma_glas *g = &s->glas;
    (void)advance_smoother(&g->vowel_smoother);
    (void)advance_smoother(&g->shift_smoother);
    (void)advance_smoother(&g->mix_smoother);
    float grain_mix = advance_smoother(&g->grain_smoother);
    if (g->tick_pos == 0 || !g->coefficients_ready || g->retrigger) {
        glas_tick(g, f0, s->sample_rate_hz);
        g->tick_pos = 0;
    }
    if (++g->tick_pos >= GLAS_TICK) g->tick_pos = 0;
    return grain_mix;
}

float ma_glas_prepare(ma_synth *s, float f0) {
    ma_glas *g = &s->glas;
    float grain_mix = ma_glas_prepare_controls(s, f0);
    if (grain_mix > 0.0f) {
        float rate = 2.0f * s->sample_rate_hz;
        for (int k = 0; k < 2; k++) {
            float y = render_grains(g, f0, rate);
            if (!(y >= -FLT_MAX && y <= FLT_MAX)) {
                ma_glas_reset_state(g);
                y = 0.0f;
            }
            g->grain_out[k] = y;
        }
    }
    return grain_mix;
}

/* The formant bank on the ladder's 1x output. */
float ma_glas_formant(ma_synth *s, float x) {
    ma_glas *g = &s->glas;
    float s1[MA_FORMANTS] = {}, s2[MA_FORMANTS] = {};
    for (int i = 0; i < MA_FORMANTS; i++) {
        s1[i] = g->svf[i].s1;
        s2[i] = g->svf[i].s2;
    }
    bool bad = false;
    float y = ml_formant_bank(s1, s2, g->g, g->k, x, g->mix_smoother.current, true, &bad);
    for (int i = 0; i < MA_FORMANTS; i++) {
        g->svf[i].s1 = s1[i];
        g->svf[i].s2 = s2[i];
    }
    if (bad) ma_glas_reset_state(g);
    return y;
}
