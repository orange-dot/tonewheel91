/* tonewheel91 drive — the stateful preamp bias stage (M5). Freestanding:
 * no OS, no libm, no allocation. Constants: docs/constants.md sec 14.1.
 *
 * Not a bare waveshaper, by design (design.md model-depth doctrine): a
 * full-wave envelope follower shifts the saturator's operating point
 * toward cutoff (bias excursion) and a coupling-cap highpass blocks the
 * DC that shift creates. The audible signature is dynamic — even
 * harmonics bloom and duck with playing level, and the cap "breathes" on
 * level changes — none of which a memoryless shaper has. The shared
 * preamp is also where intermodulation first enters (sec 14 [AS16]); the
 * per-wheel pickup stage stays IMD-free and separate. The named upgrade,
 * if the ear demands it, is a wave-digital triode stage. */
#include "tonewheel.h"

/* Follower time constants: RC-order working values for a small-tube
 * stage's grid/cathode network; by-ear verdict overrides. sec 14.1. */
static constexpr float BIAS_ATK_S = 0.005f;
static constexpr float BIAS_REL_S = 0.050f;
/* Operating-point shift per unit of follower level, toward cutoff. */
static constexpr float BIAS_DEPTH = 0.5f;
/* Coupling-cap pole: 2.7 octaves under the manual floor. sec 14.1. */
static constexpr float HP_HZ = 10.0f;
/* Knob reach into the shaper: pregain 1..8 (+18 dB) on drive^2, against
 * the X_ref = 8 nominal full-organ level. sec 14.1. */
static constexpr float PREGAIN_MAX = 8.0f;
static constexpr float X_REF = 8.0f;

/* 1 - exp(-x) by the alternating Taylor expansion of exp; valid for
 * x <= 0.25 (generator.c's smooth_coeff discipline); hostile args clamp. */
static float one_pole_coeff(float x) {
    if (!(x > 0.0f) || x > 0.25f) x = 0.25f;
    return x * (1.0f - x * (0.5f - x * (1.0f / 6.0f - x / 24.0f)));
}

void tw_drive_init(tw_drive *d, float sample_rate_hz) {
    if (!(sample_rate_hz >= 8000.0f)) sample_rate_hz = 48000.0f;
    *d = (tw_drive){ 0 };
    d->atk_c = one_pole_coeff(1.0f / (BIAS_ATK_S * sample_rate_hz));
    d->rel_c = one_pole_coeff(1.0f / (BIAS_REL_S * sample_rate_hz));
    d->hp_c = one_pole_coeff(6.28318531f * HP_HZ / sample_rate_hz);
    tw_drive_set(d, 0.0f); /* bypass: the pre-M5 chain, bit-identical */
}

void tw_drive_set(tw_drive *d, float v) {
    if (!(v >= 0.0f)) v = 0.0f; /* NaN and negatives */
    else if (v > 1.0f) v = 1.0f;
    d->drive = v;
    float pregain = 1.0f + (PREGAIN_MAX - 1.0f) * v * v;
    d->pre = pregain / X_REF;
    d->post = X_REF / pregain;
}

float tw_drive_tick(tw_drive *d, float x) {
    if (d->drive == 0.0f) return x; /* exact bypass, state untouched */

    float in = x * d->pre;

    /* Bias excursion: the follower rides |in| (attack fast, release
     * slow) and drags the operating point toward cutoff. Snapping to
     * target below -180 dB keeps a rung-out stage denormal-free. */
    float mag = tw_fabsf(in);
    float c = (mag > d->env) ? d->atk_c : d->rel_c;
    float e = d->env + c * (mag - d->env);
    e = (tw_fabsf(e - mag) < 1e-9f) ? mag : e;
    d->env = e;

    float y = tw_sat(in - BIAS_DEPTH * e);

    /* Coupling cap: a one-pole tracker holds the shaper's DC image; the
     * output is the remainder (AC coupling). Same snap discipline. */
    float lp = d->hp_lp + d->hp_c * (y - d->hp_lp);
    lp = (tw_fabsf(lp - y) < 1e-9f) ? y : lp;
    d->hp_lp = lp;

    return (y - lp) * d->post;
}
