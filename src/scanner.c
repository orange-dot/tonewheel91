/* tonewheel91 vibrato/chorus scanner (M4). Freestanding: no OS, no libm,
 * no allocation. Constants and their sources: docs/constants.md section 9.
 *
 * The line box is a real 18-section LC lowpass ladder, not an ideal delay:
 * passband edge ~7075 Hz, idealized total delay sqrt(sum L x sum C)
 * ~0.85 ms, and the impulse smears progressively along the line
 * (dispersion) — the effect's signature comes from modeling the actual
 * circuit (sec 9 [DAFx16]). The scanner reads 9 of the 19 line nodes
 * through a triangular capacitive crossfade — effectively linear
 * interpolation, whose comb notches between taps are part of the sound.
 *
 * Discretization [decision, sec 9]: trapezoidal (bilinear) nodal model of
 * the ladder, one constant-coefficient tridiagonal solve per sample,
 * factors precomputed at init. No prewarp: plain bilinear keeps low- and
 * mid-band group delay exact, so the emergent V3 depth lands inside the
 * pinned 1.1-1.6% band; the cost is the passband edge warping ~6% low at
 * 48 kHz (~6.7 kHz), converging to ~7.09 kHz at 192 kHz. */
#include "tonewheel.h"

enum {
    SCAN_TERMINALS = 9,  /* scanner terminals t1..t9, sec 9 [P45] */
    SCAN_PLATES    = 16, /* fixed plate stacks around the rotor; t1..t9
                          * wired there-and-back, so one revolution = one
                          * full triangular sweep, sec 9 [DAFx16] */
    SCAN_MODES     = 3,  /* depths 1..3; V and C share tap tables */
};

/* Ladder components in base SI units, sec 9 [DAFx16 Table 1]:
 * series L1..L18 = 500 mH each; shunt C1..C17 = 0.004 uF, C18 = 0.001 uF;
 * termination Rt = 15 kohm. */
static constexpr float SCAN_L_H = 0.5f; /* all 18 sections equal */
static constexpr float SCAN_C_F[TW_SCAN_SECTIONS] = {
    4e-9f, 4e-9f, 4e-9f, 4e-9f, 4e-9f, 4e-9f,
    4e-9f, 4e-9f, 4e-9f, 4e-9f, 4e-9f, 4e-9f,
    4e-9f, 4e-9f, 4e-9f, 4e-9f, 4e-9f, 1e-9f,
};
static constexpr float SCAN_RT_OHM = 15000.0f;

/* Series input resistor: shorted by the switch in V modes, in circuit for
 * C modes, sec 9 [DAFx16]. The value is pinned but not yet consumed: the
 * C-mode mix is behavioral (SCAN_C_MIX below); driving the line through
 * Rc against its ~11.2 kohm image impedance is the named circuit-level
 * upgrade if the ear objects. */
[[maybe_unused]] static constexpr float SCAN_RC_OHM = 22000.0f;

/* C modes mix the dry line feed with the swept output at equal amplitude:
 * out = (dry + wet) / 2 [decision, sec 9] — the [P39] pre-scanner chorus
 * ran its detuned pair at equal power with the solo wheel. */
static constexpr float SCAN_C_MIX = 0.5f;

/* Per-node gain from the stage voltage dividers on v1..v6 (series R+ /
 * shunt R- in kohm: 27/68, 56/150, 39/150, 33/180, 18/180, 12/180),
 * nodes v7..v19 undivided, sec 9 [DAFx16 Table 1]. The linear ratio
 * R- / (R+ + R-) reproduces the measured -2.9, -2.8, -2.0, -1.5, -0.83,
 * -0.56 dB, then 0 dB of [DAFx16 Table 3] — a built-in AM ramp across
 * each scan cycle. */
static constexpr float SCAN_NODE_GAIN[TW_SCAN_NODES] = {
    68.0f / (27.0f + 68.0f),   /* v1: -2.9 dB  */
    150.0f / (56.0f + 150.0f), /* v2: -2.8 dB  */
    150.0f / (39.0f + 150.0f), /* v3: -2.0 dB  */
    180.0f / (33.0f + 180.0f), /* v4: -1.5 dB  */
    180.0f / (18.0f + 180.0f), /* v5: -0.83 dB */
    180.0f / (12.0f + 180.0f), /* v6: -0.56 dB */
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
};

/* Tap tables: the line node (v1..v19) each scanner terminal t1..t9 reads,
 * per depth, sec 9 [DAFx16 Table 2]. Spacing is non-uniform, denser near
 * both ends — supersedes the earlier "V2 ~ half, V1 ~ third" reading of
 * [SM 5-22]. */
static constexpr int8_t SCAN_TAP_NODE[SCAN_MODES][SCAN_TERMINALS] = {
    { 1, 2, 3, 4, 5, 6, 7, 8, 9 },      /* V1/C1 */
    { 1, 2, 3, 5, 7, 9, 11, 12, 13 },   /* V2/C2 */
    { 1, 2, 4, 7, 10, 13, 16, 18, 19 }, /* V3/C3 */
};

/* Terminal under each plate stack: t1..t9 wired there-and-back around the
 * 16 stacks, so adjacent-plate crossfade sweeps 1..9..1 per revolution. */
static constexpr int8_t SCAN_PLATE_TERM[SCAN_PLATES] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 7, 6, 5, 4, 3, 2, 1,
};

/* Scan rate: run motor at 412 rpm = 6.867 Hz, sec 9 [SM 5-26]; the patent
 * claims pin the window at 5-8 cps [P45]. */
static constexpr float SCAN_RATE_HZ = 412.0f / 60.0f;

/* States decaying toward zero snap below -400 dB so a rung-out line never
 * sits in denormal territory (same discipline as the generator gains). */
static inline float snap0(float x) {
    return (tw_fabsf(x) < 1e-20f) ? 0.0f : x;
}

void tw_scanner_init(tw_scanner *s, float sample_rate_hz) {
    if (!(sample_rate_hz >= 8000.0f)) sample_rate_hz = 48000.0f;
    *s = (tw_scanner){ 0 };
    s->step = SCAN_RATE_HZ / sample_rate_hz;

    /* Trapezoidal companions: a = T/(2L), bc = 2C/T, gt = 1/Rt. The
     * unknowns are the 18 shunt-cap nodes v2..v19 (v1 is the input);
     * eliminating the inductor updates leaves a constant tridiagonal
     * system [-a, d_j, -a] with d_j = bc_j + 2a and, at the terminated
     * node 19, d = bc + a + gt. Factor it once (Thomas). */
    s->a = 1.0f / (2.0f * SCAN_L_H * sample_rate_hz);
    s->gt = 1.0f / SCAN_RT_OHM;
    float m_prev = 0.0f;
    for (int j = 0; j < TW_SCAN_SECTIONS; j++) {
        s->bc[j] = 2.0f * SCAN_C_F[j] * sample_rate_hz;
        float d = (j < TW_SCAN_SECTIONS - 1)
            ? s->bc[j] + 2.0f * s->a
            : s->bc[j] + s->a + s->gt;
        s->fw[j] = (j == 0) ? 0.0f : s->a / m_prev;
        float m = (j == 0) ? d : d - s->a * s->fw[j];
        s->inv_m[j] = 1.0f / m;
        m_prev = m;
    }
}

float tw_scanner_tick(tw_scanner *s, float low, float high) {
    int mode = s->mode;
    if (mode <= TW_VIB_OFF || mode > TW_VIB_C3) return low + high;

    /* --- one trapezoidal step of the ladder, new input v1 = high --- */
    const float a = s->a;
    float r[TW_SCAN_SECTIONS];
    r[0] = s->bc[0] * s->vn[1] + 2.0f * (s->il[0] - s->il[1])
         + a * (s->vn[0] - 2.0f * s->vn[1] + s->vn[2]) + a * high;
    for (int j = 1; j < TW_SCAN_SECTIONS - 1; j++)
        r[j] = s->bc[j] * s->vn[j + 1] + 2.0f * (s->il[j] - s->il[j + 1])
             + a * (s->vn[j] - 2.0f * s->vn[j + 1] + s->vn[j + 2]);
    r[17] = s->bc[17] * s->vn[18] + 2.0f * s->il[17]
          + a * (s->vn[17] - s->vn[18]) - s->gt * s->vn[18];

    float x[TW_SCAN_SECTIONS];
    for (int j = 1; j < TW_SCAN_SECTIONS; j++) r[j] += s->fw[j] * r[j - 1];
    x[17] = r[17] * s->inv_m[17];
    for (int j = TW_SCAN_SECTIONS - 2; j >= 0; j--)
        x[j] = (r[j] + a * x[j + 1]) * s->inv_m[j];

    /* inductor update reads old vn and new x; then commit node voltages */
    float up = high; /* new voltage on the inductor's source side */
    for (int k = 0; k < TW_SCAN_SECTIONS; k++) {
        s->il[k] = snap0(s->il[k]
                         + a * (up + s->vn[k] - x[k] - s->vn[k + 1]));
        up = x[k];
    }
    s->vn[0] = high;
    for (int j = 0; j < TW_SCAN_SECTIONS; j++) s->vn[j + 1] = snap0(x[j]);

    /* --- rotor: crossfade the two plates under the pickup --- */
    float p = s->phase * (float)SCAN_PLATES;
    int pl = (int)p;
    float fr = p - (float)pl;
    s->phase += s->step;
    s->phase -= (s->phase >= 1.0f) ? 1.0f : 0.0f;

    int depth = (mode >= TW_VIB_C1) ? mode - TW_VIB_C1 : mode - TW_VIB_V1;
    int na = SCAN_TAP_NODE[depth][SCAN_PLATE_TERM[pl]] - 1;
    int nb = SCAN_TAP_NODE[depth][SCAN_PLATE_TERM[(pl + 1) & (SCAN_PLATES - 1)]] - 1;
    float wet = (1.0f - fr) * SCAN_NODE_GAIN[na] * s->vn[na]
              + fr * SCAN_NODE_GAIN[nb] * s->vn[nb];

    if (mode >= TW_VIB_C1) return low + SCAN_C_MIX * (high + wet);
    return low + wet;
}
