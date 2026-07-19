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

/* --- M7 wear: the structured-deviation banks (constants.md 11-13) ---
 * Every deviation scales linearly with the one wear knob; wear = 0 must
 * restore each bank to its bit-exact idealized value. Character values
 * come from one fixed-seed splitmix64 draw per wheel (sec 12 [decision]),
 * re-derived on every set_wear call so no base banks need storing. */

/* Fixed seed for the per-wheel character draws [decision]. */
static constexpr uint64_t WEAR_SEED = 0x7765617274773931u; /* "wear" "tw91" */

/* Level profile (sec 11): per-unit spread +-0.12 (~+-1 dB) at wear = 1
 * around the factory equal-loudness intent [FOLK], plus a per-zone trim
 * over the three conditioning zones (<= 43 shunt / 44..48 reactor /
 * 49..91 tuned LC) [FOLK]; by-ear verdict overrides both. */
static constexpr float LEVEL_SPREAD = 0.12f;
static constexpr float ZONE_TRIM[3] = { 0.0f, -0.02f, 0.02f };

static int cond_zone(int wheel) { /* sec 11 conditioning zones, 1-based */
    return (wheel <= 43) ? 0 : (wheel <= 48) ? 1 : 2;
}

/* Tooth profile (sec 12): iron-nonlinearity "toothing" is stronger for
 * the cornered low-register geometries, so depths follow 4/teeth,
 * anchored at the 4-tooth lowest manual octave [FOLK anchors; the
 * 1/teeth shape is the [ISMA19] low-register observation]. Wheels 1..12
 * take the same law as a placeholder — their true squarish profile
 * (sec 3) stays out of scope while they sit below the manual foldback
 * floor. */
static constexpr float TOOTH2_LOW = 0.015f;
static constexpr float TOOTH3_LOW = 0.03f;

/* Motion AM (sec 12 [ISMA19]/[DAFx11]): shaft wobble modulates each
 * wheel at its own rotation rate f_wheel/teeth. Max depth at wear = 1;
 * the per-wheel depth is the draw's 21..41 field times this [FOLK]. */
static constexpr float MOTION_AM_MAX = 0.05f;

/* Pickup nonlinearity (sec 12 [AS16]): y = (1 - exp(-alpha x))/alpha,
 * alpha ~ 0.3 measured; here alpha = wear x 0.3 so the idealized pickup
 * is exactly linear. Truncated to its cubic series (x - a x^2/2
 * + a^2 x^3/6): the dropped quartic is ~-59 dB at full wear and the
 * bandwidth stays bounded at 3 x wheel 91 < any supported Nyquist. */
static constexpr float PICKUP_ALPHA = 0.3f;

/* Structured leakage (sec 13): each wheel's weight on the static bleed
 * bus is the sum of its couplings to its physical neighbours' wires —
 * same-shaft partner strongest, then same-bin wheels, all other wheels
 * ~0. Coupling strengths at wear = 1 [FOLK]: chosen so the idle-organ
 * floor lands near -30 dB, below the [AS16] -24 dB clearly-audible
 * band, tuned upward by ear only. */
static constexpr float LEAK_SHAFT = 3e-3f;
static constexpr float LEAK_BIN = 8e-4f;

/* Mains hum (sec 1): 60 Hz — the reference-era machines and canonical
 * recordings are 60 Hz units; the 50 Hz variant stays a documented
 * constant switch [decision]. Level at wear = 1: -60 dB vs a unit
 * wheel [FOLK], injected on the bleed bus. */
static constexpr float HUM_HZ = 60.0f;
static constexpr float HUM_LEVEL = 1e-3f;

static int wheel_teeth(int wheel) { /* sec 3, 1-based */
    return (wheel <= 84) ? 2 << ((wheel - 1) / 12) : 192;
}

/* Shaft partner per sec 13: n <-> n+48 for 1..36, the 192-tooth top
 * pairs as (42..48) <-> (85..91) (offset 43), wheels 37..41 carry blank
 * partners. 0 = blank. */
static int shaft_partner(int wheel) {
    if (wheel <= 36) return wheel + 48;
    if (wheel <= 41) return 0;
    if (wheel <= 48) return wheel + 43;
    if (wheel <= 84) return wheel - 48;
    return wheel - 43;
}

/* Same-bin wheels beyond the shaft partner (sec 13 compartment sets):
 * the r < 5 second bins hold three wheels {r+13, r+37, r+61}, so their
 * shaft-pair members see one extra neighbour; every other wheel sits in
 * a full four-wheel bin and sees two. */
static int bin_mates(int wheel) {
    return ((wheel >= 13 && wheel <= 17) || (wheel >= 61 && wheel <= 65))
        ? 1 : 2;
}

void tw_generator_set_wear(tw_generator *g, float wear) {
    if (!(wear >= 0.0f)) wear = 0.0f; /* NaN and negatives */
    else if (wear > 1.0f) wear = 1.0f;
    g->wear = wear;
    float alpha = wear * PICKUP_ALPHA;
    g->pk2 = 0.5f * alpha;
    g->pk3 = alpha * alpha * (1.0f / 6.0f);
    g->hum_gain = wear * HUM_LEVEL;
    uint64_t s = WEAR_SEED;
    for (int i = 0; i < TW_WHEELS; i++) {
        uint64_t d = tw_splitmix64(&s);
        /* one draw per wheel, split into 21-bit fields (sec 12): bits
         * 0..20 level spread, 21..41 motion-AM depth; 42..62 is the AM
         * start angle, taken at init (phase is state) */
        float ul = (float)(d & 0x1fffffu) * (1.0f / 2097152.0f);
        float ua = (float)((d >> 21) & 0x1fffffu) * (1.0f / 2097152.0f);
        g->level[i] = 1.0f + wear * (LEVEL_SPREAD * (2.0f * ul - 1.0f)
                                     + ZONE_TRIM[cond_zone(i + 1)]);
        float inv_teeth4 = 4.0f / (float)wheel_teeth(i + 1);
        g->t2[i] = wear * TOOTH2_LOW * inv_teeth4;
        g->t3[i] = wear * TOOTH3_LOW * inv_teeth4;
        g->am_g[i] = wear * MOTION_AM_MAX * ua;
        g->leak_gain[i] = wear * (LEAK_SHAFT * (shaft_partner(i + 1) ? 1.0f : 0.0f)
                                  + LEAK_BIN * (float)bin_mates(i + 1));
    }
}

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
    uint64_t seed = WEAR_SEED;
    for (int i = 0; i < TW_WHEELS; i++) {
        float s = tw_wheel_freq_hz(i + 1) / sample_rate_hz;
        g->step[i] = (s < 0.5f) ? s : 0.0f; /* Nyquist sanitize guard only */
        /* shaft-rotation accumulator (sec 12 motion AM): rate is
         * f_wheel/teeth; the starting angle is the same per-wheel draw
         * set_wear re-derives (bits 42..62), taken here once because
         * phase is state, not a wear-scaled bank */
        g->rev_step[i] = g->step[i] / (float)wheel_teeth(i + 1);
        uint64_t d = tw_splitmix64(&seed);
        g->rev_phase[i] = (float)((d >> 42) & 0x1fffffu) * (1.0f / 2097152.0f);
    }
    g->hum_step = HUM_HZ / sample_rate_hz;
    tw_generator_set_wear(g, 0.0f); /* idealized reference; organ sets the
                                     * shipped default */
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

    /* Tooth profile (sec 12): 2nd/3rd partials on the wheel's own phase
     * — exact harmonics, so per-wheel and IMD-free. Both depths are
     * exactly 0 at wear 0, leaving raw bit-untouched. */
    float p2 = p + p;
    p2 -= (p2 >= 1.0f) ? 1.0f : 0.0f;
    float p3 = p2 + p;
    p3 -= (p3 >= 1.0f) ? 1.0f : 0.0f;
    float emf = raw + g->t2[i] * tw_sin_turns(p2) + g->t3[i] * tw_sin_turns(p3);

    /* Motion AM (sec 12): the shaft wobble modulates the whole induced
     * EMF at the wheel's own rotation rate. The accumulator always
     * advances (constant cost, wear-independent state); the depth is
     * exactly 0 at wear 0, making the factor exactly 1. */
    float rp = g->rev_phase[i] + g->rev_step[i];
    rp -= (rp >= 1.0f) ? 1.0f : 0.0f;
    g->rev_phase[i] = rp;
    emf *= 1.0f + g->am_g[i] * tw_sin_turns(rp);

    /* Pickup nonlinearity (sec 12): asymmetric and memoryless, one
     * pickup per wheel, so harmonic distortion only — never IMD. The
     * series' static term is removed at the source ([decision] sec
     * 12.1: the matching transformer passes no DC), hence e2 - 1/2.
     * Both coefficients are exactly 0 at wear 0. */
    float e2 = emf * emf;
    emf = emf - g->pk2 * (e2 - 0.5f) + g->pk3 * e2 * emf;

    float w = emf * g->level[i];

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
    /* The bleed bus taps the conditioned wire signal (sec 13 [ISMA19]:
     * the per-pickup filters shape what leaks), so the leak carries the
     * wheel's wear colour too. Exactly +0 while every weight is 0. */
    *leak += w * g->leak_gain[i];
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

    /* Mains hum (sec 1) rides the bleed bus; the phase always advances,
     * the gain is exactly 0 at wear 0 (leak stays +0). */
    float hp = g->hum_phase + g->hum_step;
    hp -= (hp >= 1.0f) ? 1.0f : 0.0f;
    g->hum_phase = hp;
    leak += g->hum_gain * tw_sin_turns(hp);

    return (tw_frame){ keyed, perc, leak, keyed_low };
}
