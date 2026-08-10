/* tonewheel91 rotary speaker (M6). Freestanding: no OS, no libm, no
 * allocation. Constants and their sources: docs/constants.md section 15
 * (the M6 working set is pinned in 15.1; rotor speeds are [FOLK] —
 * folklore working defaults, the by-ear call overrides).
 *
 * Model (sec 15 [HX]/[RS]/[LB]): one 40 W amp ceiling (the M5 stage
 * reused) feeds a passive 800 Hz 12 dB/octave crossover; the high side
 * fires through the rotating horn (FM as a fractional delay from horn
 * geometry, plus AM from the directivity lobe — one acoustic branch per
 * revolution, the dummy side radiating nothing); the low side fires into
 * the rotating drum, an AM-only device band-limited to ~200-800 Hz — no
 * Doppler on the drum, the wavelength being far larger than the scoop.
 * Two synchronous motors per rotor couple by rim drive, so speed changes
 * are slip-limited: a first-order lag per rotor per direction is the
 * honest model, not a motor spin-up. Brake parks the rotors facing
 * forward. Two virtual mics turn the two paths into a stereo field. */
#include "tonewheel.h"

/* Crossover: 800 Hz, 12 dB/oct, both sides [HX, corroborated by RS].
 * Discretized as a TPT state-variable filter with Butterworth damping
 * (k = sqrt 2); g = tan(pi fc / fs) puts the -3 dB points exactly on
 * the pinned frequency. sec 15.1 [derived]. */
static constexpr float XO_HZ = 800.0f;
static constexpr float XO_K = 1.41421356f;

/* The horn branch recombines inverted: a 2nd-order crossover's two
 * outputs are antiphase at fc, and the non-inverted sum has a null
 * there; lp - hp is flat within +3 dB. sec 15.1 [derived]. */
static constexpr float HORN_SIGN = -1.0f;

/* Drum AM floor: below ~200 Hz the scoop does essentially nothing
 * [HX, wavelength argument]; a one-pole split is the cheapest honest
 * reading of "roughly the upper two octaves". sec 15.1 [decision]. */
static constexpr float DRUM_AM_FLOOR_HZ = 200.0f;

/* Rotor speeds. Tremolo [FOLK]: horn ~400 rpm, drum ~340 rpm — a
 * designed pulley ratio, not a beat. Chorale: both ~40-50 rpm [FOLK];
 * the split inside that band keeps the pulley ordering [decision]. */
static constexpr float HORN_TREM_HZ = 400.0f / 60.0f;
static constexpr float DRUM_TREM_HZ = 340.0f / 60.0f;
static constexpr float HORN_CHOR_HZ = 48.0f / 60.0f;
static constexpr float DRUM_CHOR_HZ = 40.0f / 60.0f;

/* Transition times, read as ~3 tau of the first-order slip lag
 * [decision]: horn rise ~1 s [FOLK]; drum fall 5-8 s [RS, service
 * acceptance figure — the one sourced inertia number], midpoint taken;
 * horn fall and drum rise are unsourced working values [decision]. */
static constexpr float HORN_RISE_TAU_S = 1.0f / 3.0f;
static constexpr float HORN_FALL_TAU_S = 2.0f / 3.0f;
static constexpr float DRUM_RISE_TAU_S = 2.5f / 3.0f;
static constexpr float DRUM_FALL_TAU_S = 6.5f / 3.0f;

/* Brake parks facing forward [LB]: below the engage rate the phase
 * pulls to the front stop and snaps exact. sec 15.1 [decision]. */
static constexpr float PARK_TAU_S = 0.3f;
static constexpr float PARK_ENGAGE_HZ = 0.5f;

/* Horn Doppler: per-mic delay = base - amp * cos(horn - mic). Swing
 * 2*amp = 0.6 ms, inside the [FOLK] 0.3-0.9 ms window; the base is an
 * inaudible static path delay. sec 15.1 [decision]. */
static constexpr float DOP_BASE_S = 0.0010f;
static constexpr float DOP_AMP_S = 0.0003f;

/* AM depths: horn > drum is the only pinned ordering [FOLK]; the
 * values are working defaults [decision], by-ear overrides. */
static constexpr float HORN_AM_DEPTH = 0.40f;
static constexpr float DRUM_AM_DEPTH = 0.15f;

/* Two virtual mics at +-1/8 turn about the front [decision]: parked
 * forward the field is exactly symmetric (L == R), spinning it
 * decorrelates — the lobe and delay swings sit in quadrature. */
static constexpr float MIC_TURNS = 0.125f;

/* cos(2*pi*p) for p in [0, 1) via the quarter-turn shift. */
static inline float cos_turns(float p) {
    p += 0.25f;
    p -= (p >= 1.0f) ? 1.0f : 0.0f;
    return tw_sin_turns(p);
}

/* States decaying toward zero snap below -400 dB so a rung-out stage
 * never sits in denormal territory (the scanner's discipline). */
static inline float snap0(float x) {
    return (tw_fabsf(x) < 1e-20f) ? 0.0f : x;
}

void tw_rotary_init(tw_rotary *r, float sample_rate_hz) {
    sample_rate_hz = tw_sample_rate_hz(sample_rate_hz);
    *r = (tw_rotary){ 0 };
    r->inv_rate = 1.0f / sample_rate_hz;

    /* TPT companions: g = tan(pi fc / fs) from the repo sine kernel */
    float p = XO_HZ / (2.0f * sample_rate_hz);
    float g = tw_sin_turns(p) / cos_turns(p);
    r->xo_a1 = 1.0f / (1.0f + g * (g + XO_K));
    r->xo_a2 = g * r->xo_a1;
    r->xo_a3 = g * r->xo_a2;

    r->dm_c = tw_one_pole_coeff(6.28318531f * DRUM_AM_FLOOR_HZ / sample_rate_hz);

    /* per-direction decay factors of the rate deviation */
    r->horn_up_k = 1.0f - tw_one_pole_coeff(1.0f / (HORN_RISE_TAU_S * sample_rate_hz));
    r->horn_dn_k = 1.0f - tw_one_pole_coeff(1.0f / (HORN_FALL_TAU_S * sample_rate_hz));
    r->drum_up_k = 1.0f - tw_one_pole_coeff(1.0f / (DRUM_RISE_TAU_S * sample_rate_hz));
    r->drum_dn_k = 1.0f - tw_one_pole_coeff(1.0f / (DRUM_FALL_TAU_S * sample_rate_hz));
    r->park_c = tw_one_pole_coeff(1.0f / (PARK_TAU_S * sample_rate_hz));

    r->d_base = DOP_BASE_S * sample_rate_hz;
    r->d_amp = DOP_AMP_S * sample_rate_hz;

    tw_drive_init(&r->amp, sample_rate_hz); /* drive 0: exact pass */
    /* The 40 W ceiling keeps the M5 odd kernel and its 0.5 depth: a
     * power stage wants a hard bound, not the preamp's derived triode
     * warmth (docs/warmth-evidence.md; M6 signatures stay intact). */
    tw_drive_set_kernel(&r->amp, true);

    r->mode = TW_ROT_BYPASS; /* the M6 identity default */
    r->balance = 0.5f;
    r->width = 1.0f;
    /* phases start 0 = parked forward; targets 0, devs 0 */
}

void tw_rotary_set_mode(tw_rotary *r, int mode) {
    if (mode < TW_ROT_BYPASS) mode = TW_ROT_BYPASS;
    if (mode > TW_ROT_BRAKE) mode = TW_ROT_BRAKE;
    r->mode = mode;
    /* retarget with a continuous rate: dev carries the difference */
    float ht = r->horn_target, dt = r->drum_target;
    if (mode == TW_ROT_CHORALE) {
        ht = HORN_CHOR_HZ;
        dt = DRUM_CHOR_HZ;
    } else if (mode == TW_ROT_TREMOLO) {
        ht = HORN_TREM_HZ;
        dt = DRUM_TREM_HZ;
    } else if (mode == TW_ROT_BRAKE) {
        ht = 0.0f;
        dt = 0.0f;
    } /* bypass: targets stay; all state freezes in tick */
    r->horn_dev += r->horn_target - ht;
    r->drum_dev += r->drum_target - dt;
    r->horn_target = ht;
    r->drum_target = dt;
}

static float knob01(float v) {
    if (!(v >= 0.0f)) return 0.0f; /* NaN and negatives */
    return (v > 1.0f) ? 1.0f : v;
}

void tw_rotary_set_balance(tw_rotary *r, float v) { r->balance = knob01(v); }
void tw_rotary_set_width(tw_rotary *r, float v) { r->width = knob01(v); }
void tw_rotary_set_drive(tw_rotary *r, float v) { tw_drive_set(&r->amp, v); }

/* One rotor step: decay the rate deviation with the direction's factor,
 * advance the phase, and under brake pull the phase to the front stop
 * once slow enough, snapping both exact at the end. */
static inline void rotor_step(float *phase, float *dev, float target,
                              float up_k, float dn_k, float inv_rate,
                              int braking, float park_c) {
    *dev = snap0(*dev * ((*dev < 0.0f) ? up_k : dn_k));
    float rate = target + *dev;
    float ph = *phase + rate * inv_rate;
    ph -= (ph >= 1.0f) ? 1.0f : 0.0f;
    if (braking && rate < PARK_ENGAGE_HZ) {
        float stop = (ph < 0.5f) ? 0.0f : 1.0f;
        ph += park_c * (stop - ph);
        if (tw_fabsf(ph - stop) < 2e-3f && rate < 1e-3f) {
            ph = 0.0f;
            *dev = 0.0f;
        }
    }
    *phase = ph;
}

tw_stereo tw_rotary_tick(tw_rotary *r, float x) {
    if (r->mode == TW_ROT_BYPASS)
        return (tw_stereo){ x, x }; /* exact bypass, state untouched */

    /* the 40 W ceiling ahead of the rotors (rotary drive 0: exact pass) */
    float in = tw_drive_tick(&r->amp, x);

    /* --- crossover: one TPT SVF step; lp = drum, hp = horn --- */
    float v3 = in - r->xo_ic2;
    float v1 = r->xo_a1 * r->xo_ic1 + r->xo_a2 * v3;
    float v2 = r->xo_ic2 + r->xo_a2 * r->xo_ic1 + r->xo_a3 * v3;
    r->xo_ic1 = snap0(2.0f * v1 - r->xo_ic1);
    r->xo_ic2 = snap0(2.0f * v2 - r->xo_ic2);
    float lp = v2;
    float hp = in - XO_K * v1 - v2;
    r->xo_lp = lp;
    r->xo_hp = hp;

    /* --- drum AM floor: split the drum branch at 200 Hz --- */
    float dlow = r->dm_lp + r->dm_c * (lp - r->dm_lp);
    dlow = (tw_fabsf(dlow - lp) < 1e-9f) ? lp : dlow;
    r->dm_lp = dlow;
    float dmid = lp - dlow;

    /* --- pickups: two virtual mics read both rotors at the current
     * angles; the horn contributes FM (fractional delay) + AM, the drum
     * AM on its 200-800 Hz band only --- */
    r->dline[r->widx] = hp;
    float horn_m[2], drum_m[2];
    static constexpr float mic[2] = { -MIC_TURNS, +MIC_TURNS };
    for (int m = 0; m < 2; m++) {
        float hph = r->horn_phase - mic[m];
        hph += (hph < 0.0f) ? 1.0f : 0.0f;
        hph -= (hph >= 1.0f) ? 1.0f : 0.0f;
        float hc = cos_turns(hph);
        float delay = r->d_base - r->d_amp * hc;
        int32_t di = (int32_t)delay;
        float fr = delay - (float)di;
        int32_t i0 = (r->widx - di) & (TW_ROTARY_DMAX - 1);
        int32_t i1 = (i0 - 1) & (TW_ROTARY_DMAX - 1);
        float hs = r->dline[i0] + fr * (r->dline[i1] - r->dline[i0]);
        horn_m[m] = (1.0f - HORN_AM_DEPTH * (0.5f - 0.5f * hc)) * hs;

        float dph = r->drum_phase - mic[m];
        dph += (dph < 0.0f) ? 1.0f : 0.0f;
        dph -= (dph >= 1.0f) ? 1.0f : 0.0f;
        float dc = cos_turns(dph);
        drum_m[m] = dlow + (1.0f - DRUM_AM_DEPTH * (0.5f - 0.5f * dc)) * dmid;
    }
    r->widx = (r->widx + 1) & (TW_ROTARY_DMAX - 1);

    /* --- rotors advance after the reads (the scanner's ordering) --- */
    int braking = r->mode == TW_ROT_BRAKE;
    rotor_step(&r->horn_phase, &r->horn_dev, r->horn_target,
               r->horn_up_k, r->horn_dn_k, r->inv_rate, braking, r->park_c);
    rotor_step(&r->drum_phase, &r->drum_dev, r->drum_target,
               r->drum_up_k, r->drum_dn_k, r->inv_rate, braking, r->park_c);

    /* --- balance tilt, then mid/side width --- */
    float hg = 2.0f * r->balance * HORN_SIGN;
    float dg = 2.0f * (1.0f - r->balance);
    float left = dg * drum_m[0] + hg * horn_m[0];
    float right = dg * drum_m[1] + hg * horn_m[1];
    float mid = 0.5f * (left + right);
    float side = 0.5f * (left - right);
    return (tw_stereo){ mid + r->width * side, mid - r->width * side };
}
