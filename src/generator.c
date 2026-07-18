/* tonewheel91 generator. Freestanding: no OS, no libm, no allocation. */
#include "tonewheel.h"

/* Twelve driver/driven gear-tooth pairs, note classes C..B.
 * constants.md section 2. */
static constexpr int16_t GEAR[12][2] = {
    { 85, 104 }, { 71, 82 }, { 67, 73 }, { 105, 108 },
    { 103, 100 }, { 84, 77 }, { 74, 64 }, { 98, 80 },
    { 96, 74 }, { 88, 64 }, { 67, 46 }, { 108, 70 },
};

static constexpr double SHAFT_REV_S = 20.0;

const int8_t TW_DRAWBAR_OFFSET[TW_DRAWBARS] = {
    -12, 7, 0, 12, 19, 24, 28, 31, 36
};

const float TW_DRAWBAR_GAIN[9] = {
    0.0f,        6.0f / 64.0f,  8.0f / 64.0f, 11.0f / 64.0f, 16.0f / 64.0f,
    22.0f / 64.0f, 32.0f / 64.0f, 45.0f / 64.0f, 64.0f / 64.0f,
};

float tw_wheel_freq_hz(int wheel) {
    if (wheel < 1 || wheel > TW_WHEELS) return 0.0f;
    int n = wheel - 1;
    int cls, teeth;
    if (wheel <= 84) {
        cls = n % 12;
        teeth = 2 << (n / 12);
    } else {
        cls = n % 12 + 5; /* top seven: 192 teeth, F..B ratios */
        teeth = 192;
    }
    double ratio = (double)GEAR[cls][0] / (double)GEAR[cls][1];
    return (float)(SHAFT_REV_S * teeth * ratio);
}

int tw_wheel_index(int key, int drawbar) {
    if (key < 1) key = 1;
    else if (key > TW_KEYS) key = TW_KEYS;
    if (drawbar < 0) drawbar = 0;
    else if (drawbar > 8) drawbar = 8;
    int w = TW_WHEEL_MIN + (key - 1) + TW_DRAWBAR_OFFSET[drawbar];
    while (w < TW_WHEEL_MIN) w += 12;
    while (w > TW_WHEEL_MAX) w -= 12;
    return w;
}

/* 1 - exp(-1/(tau*fs)) by the alternating Taylor expansion of exp;
 * valid for x <= 0.25 (tau >= ~0.1 ms at 44.1 kHz), where the truncation
 * error is < 1e-5 of the coefficient. Hostile tau/fs clamp to x = 0.25. */
static float smooth_coeff(float sample_rate_hz, float tau_s) {
    float x = 1.0f / (tau_s * sample_rate_hz);
    if (!(x > 0.0f) || x > 0.25f) x = 0.25f;
    return x * (1.0f - x * (0.5f - x * (1.0f / 6.0f - x / 24.0f)));
}

void tw_generator_init(tw_generator *g, float sample_rate_hz, float tau_s) {
    if (!(sample_rate_hz >= 8000.0f)) sample_rate_hz = 48000.0f;
    *g = (tw_generator){ 0 };
    for (int i = 0; i < TW_WHEELS; i++) {
        float s = tw_wheel_freq_hz(i + 1) / sample_rate_hz;
        g->step[i] = (s < 0.5f) ? s : 0.0f; /* Nyquist sanitize guard only */
        g->level[i] = 1.0f;
    }
    g->smooth = smooth_coeff(sample_rate_hz, tau_s);
    g->perc_smooth = g->smooth; /* harmless default: perc_target starts at 0 */
}

void tw_generator_set_perc_tau(tw_generator *g, float sample_rate_hz, float tau_s) {
    g->perc_smooth = smooth_coeff(sample_rate_hz, tau_s);
}

static void set_targets(float dst[TW_WHEELS], const float src[TW_WHEELS]) {
    for (int i = 0; i < TW_WHEELS; i++) {
        float t = src[i];
        if (!(t >= 0.0f)) t = 0.0f;    /* NaN and negatives */
        else if (t > 16.0f) t = 16.0f; /* +inf and absurd   */
        dst[i] = t;
    }
}

void tw_generator_set_keyed_targets(tw_generator *g, const float t[TW_WHEELS]) {
    set_targets(g->keyed_target, t);
}

void tw_generator_set_perc_targets(tw_generator *g, const float t[TW_WHEELS]) {
    set_targets(g->perc_target, t);
}

static inline void wheel_tick(tw_generator *g, int i, float c,
                              float *keyed, float *perc, float *leak) {
    float p = g->phase[i] + g->step[i];
    p -= (p >= 1.0f) ? 1.0f : 0.0f;
    g->phase[i] = p;
    float raw = tw_sin_turns(p);
    float w = raw * g->level[i];

    /* One-pole gain smoothing shapes the click; snapping to target
     * below -180 dB keeps decaying banks out of denormal range. */
    float kt = g->keyed_target[i];
    float kg = g->keyed_gain[i] + c * (kt - g->keyed_gain[i]);
    kg = (tw_fabsf(kg - kt) < 1e-9f) ? kt : kg;
    g->keyed_gain[i] = kg;

    /* perc_target is its own envelope: it decays toward 0 on its own
     * one-pole (perc_smooth, tau fast/slow), set to peak only at a
     * trigger (organ.c). perc_gain then chases that moving target on
     * the same click tau as keyed, same denormal-safe snap. */
    float pt = g->perc_target[i] - g->perc_smooth * g->perc_target[i];
    pt = (tw_fabsf(pt) < 1e-9f) ? 0.0f : pt;
    g->perc_target[i] = pt;
    float pg = g->perc_gain[i] + c * (pt - g->perc_gain[i]);
    pg = (tw_fabsf(pg - pt) < 1e-9f) ? pt : pg;
    g->perc_gain[i] = pg;

    *keyed += w * kg;
    *perc += w * pg;
    *leak += raw * g->leak_gain[i];
}

tw_frame tw_generator_tick(tw_generator *g) {
    const float c = g->smooth;
    float keyed = 0.0f, perc = 0.0f, leak = 0.0f;
    /* Two ranges, one arithmetic sequence: the running keyed sum is
     * captured after wheel 16 — the sub-80 Hz fundamentals that bypass
     * the scanner line (sec 9) — so the totals stay bit-identical to the
     * single-loop original (same additions in the same order). */
    for (int i = 0; i < TW_SCAN_LOW_WHEELS; i++)
        wheel_tick(g, i, c, &keyed, &perc, &leak);
    float keyed_low = keyed;
    for (int i = TW_SCAN_LOW_WHEELS; i < TW_WHEELS; i++)
        wheel_tick(g, i, c, &keyed, &perc, &leak);
    return (tw_frame){ keyed, perc, leak, keyed_low };
}
