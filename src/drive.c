/* tonewheel91 drive — the stateful preamp bias stage (M5, kernel
 * re-derived at the warmth pass). Freestanding: no OS, no libm, no
 * allocation. Constants: docs/constants.md sec 14.1; derivation and
 * measured evidence: docs/warmth-evidence.md.
 *
 * Not a bare waveshaper, by design (design.md model-depth doctrine),
 * and since the warmth pass not an odd one either: the kernel is the
 * normalized static transfer of a circuit-true triode reference
 * (driver/spice/curve.cir), so the even harmonics that make the stage
 * warm come from the curve's own asymmetry — instantly, proportional
 * to level, with H3 held far below H2 — the way the reference does it.
 * The envelope follower survives with a small derived depth as the
 * slow operating-point walk (the reference's cathode drift), and the
 * coupling-cap highpass blocks the DC image the asymmetry creates —
 * that DC now swings positive (rectified plate current rising), and
 * its "breathing" on level changes is physical. The shared preamp is
 * also where intermodulation first enters (sec 14 [AS16]); the
 * per-wheel pickup stage stays IMD-free and separate. The named
 * upgrade, if the ear demands blocking/sag (the regimes the static
 * curve cannot carry), is a wave-digital triode stage. */
#include "tonewheel.h"

/* Follower time constants: RC-order working values for a small-tube
 * stage's grid/cathode network; by-ear verdict overrides. sec 14.1. */
static constexpr float BIAS_ATK_S = 0.005f;
static constexpr float BIAS_REL_S = 0.050f;
/* Operating-point shift per unit of follower level, toward cutoff.
 * Triode kernel [derived, warmth pass]: the reference's cathode walk is
 * ~0.037 units per unit of envelope at the 1.0-1.5 V anchor (and
 * negligible below — the curve, not the walk, now owns the even
 * harmonics). The M5 odd kernel keeps its 0.5: there the walk is what
 * fakes the asymmetry, and the rotary's 40 W ceiling still runs it. */
static constexpr float TRIODE_DEPTH = 0.037f;
static constexpr float M5_ODD_DEPTH = 0.5f;
/* Coupling-cap pole: 2.7 octaves under the manual floor. sec 14.1. */
static constexpr float HP_HZ = 10.0f;
/* Knob reach into the shaper: pregain 1..8 (+18 dB) on drive^2, against
 * the X_ref = 8 nominal full-organ level. sec 14.1. */
static constexpr float PREGAIN_MAX = 8.0f;
static constexpr float X_REF = 8.0f;

/* The drive kernel [derived, warmth pass — docs/warmth-evidence.md]:
 * the normalized static (fast-manifold) transfer of the reference
 * stage — Koren 12AX7 family, fixed-bias cathode, 1k source — sampled
 * by driver/spice/curve.cir and fitted by `exhibit_warmth fit` as a
 * monotone C1 cubic Hermite on uniform knots over [-8, 8], h = 0.25
 * (worst residual 0.0009 against the sweep; axis S = 0.72 V/unit from
 * the 1 % THD anchor, output slope-normalized by G0 = 60.56). Exact 0
 * and unit slope at 0; flat C1 rails at -1.831 (cutoff floor) and
 * +3.722 (grid-conduction ceiling); the ~+15 % slope rise before the
 * positive knee is the triode's 3/2-power law. Asymmetric on purpose:
 * H2 rides ~18-28 dB above H3 through the warmth window, which the
 * old odd rational could not do (its cubic was ~17x the reference's).
 * CURVE_MH holds knot slopes pre-multiplied by h. */
static const float CURVE_Y[65] = {
    -1.83117718f, -1.83117686f, -1.83117628f, -1.83117523f, -1.83117330f,
    -1.83116975f, -1.83116326f, -1.83115133f, -1.83112952f, -1.83108959f,
    -1.83101646f, -1.83088264f, -1.83063804f, -1.83019157f, -1.82937878f,
    -1.82790531f, -1.82525228f, -1.82052744f, -1.81225570f, -1.79814322f,
    -1.77493380f, -1.73856573f, -1.68478192f, -1.61003709f, -1.51222469f,
    -1.39084589f, -1.24667692f, -1.08125568f, -0.89644463f, -0.69415571f,
    -0.47621058f, -0.24430191f, 0.00000000f, 0.25521777f, 0.51991899f,
    0.79267015f, 1.07199119f, 1.35631098f, 1.64390723f, 1.93275676f,
    2.21145404f, 2.42929576f, 2.62298018f, 2.80369688f, 2.96894186f,
    3.11371244f, 3.23401880f, 3.32955532f, 3.40363520f, 3.46099037f,
    3.50595812f, 3.54186930f, 3.57110937f, 3.59535284f, 3.61578152f,
    3.63324061f, 3.64834588f, 3.66155453f, 3.67321225f, 3.68358498f,
    3.69288052f, 3.70126356f, 3.70886638f, 3.71579644f, 3.72214200f,
};
static const float CURVE_MH[65] = {
    0.00000000f, 0.00000045f, 0.00000081f, 0.00000149f, 0.00000274f,
    0.00000502f, 0.00000921f, 0.00001687f, 0.00003087f, 0.00005653f,
    0.00010348f, 0.00018921f, 0.00034554f, 0.00062963f, 0.00114313f,
    0.00206325f, 0.00368893f, 0.00649829f, 0.01119211f, 0.01866095f,
    0.02978874f, 0.04507594f, 0.06426432f, 0.08627862f, 0.10959560f,
    0.13277388f, 0.15479511f, 0.17511615f, 0.19354998f, 0.21011703f,
    0.22492690f, 0.23810529f, 0.24975984f, 0.25995950f, 0.26872619f,
    0.27603610f, 0.28182041f, 0.28595802f, 0.28822289f, 0.28377341f,
    0.24826950f, 0.20576307f, 0.18720056f, 0.17298084f, 0.15500778f,
    0.13253847f, 0.10792144f, 0.08480820f, 0.06571752f, 0.05116146f,
    0.04043947f, 0.03257562f, 0.02674177f, 0.02233607f, 0.01894388f,
    0.01628218f, 0.01415696f, 0.01243318f, 0.01101523f, 0.00983413f,
    0.00883929f, 0.00799293f, 0.00726644f, 0.00663781f, 0.00000000f,
};

float tw_drive_curve(float x) {
    if (x <= -8.0f) return CURVE_Y[0];
    if (x >= 8.0f) return CURVE_Y[64];
    float t = (x + 8.0f) * 4.0f;
    int i = (int)t;
    t -= (float)i;
    float t2 = t * t, t3 = t2 * t;
    return CURVE_Y[i] * (2.0f * t3 - 3.0f * t2 + 1.0f)
         + CURVE_MH[i] * (t3 - 2.0f * t2 + t)
         + CURVE_Y[i + 1] * (3.0f * t2 - 2.0f * t3)
         + CURVE_MH[i + 1] * (t3 - t2);
}

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
    tw_drive_set_kernel(d, false); /* preamp default: the triode curve */
    tw_drive_set(d, 0.0f); /* bypass: the pre-M5 chain, bit-identical */
}

void tw_drive_set_kernel(tw_drive *d, bool odd) {
    d->odd = odd;
    d->depth = odd ? M5_ODD_DEPTH : TRIODE_DEPTH;
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

    float u = in - d->depth * e;
    float y = d->odd ? tw_sat(u) : tw_drive_curve(u);

    /* Coupling cap: a one-pole tracker holds the shaper's DC image; the
     * output is the remainder (AC coupling). Same snap discipline. */
    float lp = d->hp_lp + d->hp_c * (y - d->hp_lp);
    lp = (tw_fabsf(lp - y) < 1e-9f) ? y : lp;
    d->hp_lp = lp;

    return (y - lp) * d->post;
}
