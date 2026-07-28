/* ep73 struck-voice bank (EP1): 73 tines, three flexural modes each, a
 * per-voice pickup nonlinearity, and a passive sum.
 * Constants and their sources: docs/ep-constants.md sections 2-6 and 9. */
#include "epiano.h"

/* sec 3: clamped-free cantilever modes, (beta_m / beta_1)^2 for
 * beta = 1.8751041, 4.6940911, 7.8547574. */
const float EP_MODE_RATIO[EP_MODES] = { 1.0f, 6.2669f, 17.5475f };

/* sec 4: t60 of a mode is the key's t60 times this — the f^-p law at the
 * mode's own frequency, times an extra damping factor [FOLK]. */
const float EP_MODE_T60[EP_MODES] = { 1.0f, 0.18480f, 0.05535f };

/* sec 5.2: the by-ear trim, one scale per mode. The shape across the
 * compass is derived (ep_mode_shape below), so this is what EP3 turns when
 * the ear says there is still too much bell — not a base weight. */
const float EP_BASE_W[EP_MODES] = { 1.0f, 1.0f, 1.0f };

/* sec 5.2: the contact roll-off corner per hammer-hardness zone at
 * velocity 127, fc = 1/(2 t_c), from the contact times a neoprene tip of
 * each durometer plausibly gives [EP-SM 4-3 ladder, times [FOLK]]. The
 * zone boundaries are sourced; the times are EP3's to settle. */
const float EP_CONTACT_MS[EP_ZONES] = { 9.0f, 6.0f, 4.2f, 2.85f, 1.95f };
const float EP_CORNER_HZ[EP_ZONES] = {
    55.556f, 83.333f, 119.048f, 175.439f, 256.410f,
};

/* sec 2: 440 * 2^(-41/12), and the twelve semitone ratios. The octave
 * factor is an exact power of two, so the whole compass comes off one
 * anchor and twelve constants. */
static constexpr float EP_F_E1 = 41.203445f;
static constexpr float EP_SEMI[12] = {
    1.000000000f, 1.059463094f, 1.122462048f, 1.189207115f,
    1.259921050f, 1.334839854f, 1.414213562f, 1.498307077f,
    1.587401052f, 1.681792831f, 1.781797436f, 1.887748625f,
};

/* sec 5.1: the velocity-to-level law, (v/127)^gamma with
 * gamma = 1.042, pinned by ear at EP3 (D3a). Carried as a table
 * because the core has no libm and velocity is an integer 1..127
 * anyway; index 0 is unused, the setter clamps to 1. Re-pinning
 * gamma means regenerating this table, which is the point: the
 * exponent is a documented generator parameter, not live code. */
static constexpr float EP_LEVEL[128] = {
    0.0000000f, 0.0064245f, 0.0132285f, 0.0201835f, 0.0272385f, 0.0343687f, 0.0415595f, 0.0488010f,
    0.0560862f, 0.0634099f, 0.0707679f, 0.0781570f, 0.0855743f, 0.0930177f, 0.1004852f, 0.1079751f,
    0.1154861f, 0.1230168f, 0.1305661f, 0.1381331f, 0.1457169f, 0.1533165f, 0.1609315f, 0.1685609f,
    0.1762044f, 0.1838612f, 0.1915309f, 0.1992129f, 0.2069070f, 0.2146126f, 0.2223294f, 0.2300570f,
    0.2377950f, 0.2455433f, 0.2533014f, 0.2610691f, 0.2688461f, 0.2766322f, 0.2844271f, 0.2922307f,
    0.3000427f, 0.3078629f, 0.3156911f, 0.3235271f, 0.3313708f, 0.3392220f, 0.3470805f, 0.3549462f,
    0.3628189f, 0.3706985f, 0.3785849f, 0.3864779f, 0.3943774f, 0.4022833f, 0.4101954f, 0.4181137f,
    0.4260381f, 0.4339684f, 0.4419046f, 0.4498465f, 0.4577940f, 0.4657472f, 0.4737058f, 0.4816698f,
    0.4896391f, 0.4976136f, 0.5055933f, 0.5135781f, 0.5215678f, 0.5295626f, 0.5375621f, 0.5455665f,
    0.5535757f, 0.5615895f, 0.5696079f, 0.5776308f, 0.5856583f, 0.5936902f, 0.6017264f, 0.6097670f,
    0.6178119f, 0.6258610f, 0.6339143f, 0.6419717f, 0.6500332f, 0.6580987f, 0.6661682f, 0.6742417f,
    0.6823190f, 0.6904002f, 0.6984852f, 0.7065740f, 0.7146665f, 0.7227627f, 0.7308626f, 0.7389661f,
    0.7470732f, 0.7551838f, 0.7632979f, 0.7714156f, 0.7795366f, 0.7876611f, 0.7957889f, 0.8039201f,
    0.8120547f, 0.8201925f, 0.8283335f, 0.8364778f, 0.8446253f, 0.8527759f, 0.8609297f, 0.8690866f,
    0.8772466f, 0.8854097f, 0.8935758f, 0.9017449f, 0.9099169f, 0.9180920f, 0.9262699f, 0.9344508f,
    0.9426346f, 0.9508212f, 0.9590107f, 0.9672030f, 0.9753981f, 0.9835960f, 0.9917966f, 1.0000000f,
};

static constexpr float T60_E1 = 17.0f;         /* sec 4 [EP-P61]         */
static constexpr float T60_RATIO = 0.980099f;  /* per semitone, sec 4    */
static constexpr float DAMP_T60_E1 = 0.35f;    /* sec 8 [FOLK]           */
static constexpr float DAMP_T60_RATIO = 0.981119f; /* per semitone, sec 8 */
static constexpr float NYQUIST_GUARD = 0.45f;  /* sec 3.1                */
static constexpr float LN1000 = 6.907755f;     /* t60 -> amplitude tau   */
static constexpr float AMP_EPS = 1e-9f;        /* denormal-safe snap      */

/* sec 5.5: the tip meeting the tine. Level is relative to the strike and
 * rises as the square root of it, so a soft blow is proportionally more
 * knock than tone — which is how a struck instrument behaves and what
 * makes pianissimo read as percussion rather than as a quiet sine. */
const float EP_HAMMER_LEVEL[EP_ZONES] = { 0.160f, 0.150f, 0.130f, 0.110f, 0.100f };
const float EP_HAMMER_MS[EP_ZONES] = { 12.0f, 10.0f, 8.0f, 6.0f, 5.0f };

/* The burst's own bandwidth, which is a different question from the mode
 * roll-off of sec 5.2 and must not reuse its corner: contact *duration*
 * decides which modes can ring, contact *hardness* decides how bright the
 * knock itself is. Harder tips toward the treble click higher. Reusing the
 * mode corner here put a 113 Hz filter, time constant 1.4 ms, in front of a
 * burst that is over in about 1 ms — the filter never left its ramp and
 * threw away 40 dB. [FOLK], EP3 owns. */
const float EP_HAMMER_HZ[EP_ZONES] = { 1200.0f, 1500.0f, 1900.0f, 2400.0f, 3000.0f };

/* sec 6.1: alpha at the reference note (E4), and the exponent on
 * (f_ref / f) that carries it across the compass. Pure momentum would give
 * slope 1, since swing goes as 1/f for a given blow; the instrument is
 * voiced for evenness across the compass, which pulls it back, and how far
 * is [FOLK] — EP3 owns it. */
const float EP_PICKUP_DRIVE_REF = 1.6f;
const float EP_PICKUP_SLOPE = 0.5f;
const float EP_PICKUP_OFFSET = 0.45f;
static constexpr float PICKUP_F_REF = 329.6276f;  /* E4, mid compass */
static constexpr float PICKUP_DRIVE_MAX = 3.5f;
static constexpr float DC_BLOCK_HZ = 10.0f;       /* the coupling cap, sec 6.2 */

/* "ep73" "hammer" — fixed, per sec 5.5; every draw is reproducible from
 * the note and the strike ordinal alone. */
static constexpr uint64_t HAMMER_SEED = 0x6570373368616d6du;

/* sec 15: the condition set. Each depth is the full swing at condition 1
 * and exactly neutral at 0. [FOLK] throughout except where noted. */
static constexpr float COND_DETUNE_CENTS = 6.0f;  /* tuning drift, +/-    */
static constexpr float COND_RATIO_DEV = 0.04f;    /* clang ratio, +/-     */
static constexpr float COND_VOICE_DEV = 0.25f;    /* per-note voicing, +/-*/
static constexpr float COND_ALPHA_DEV = 0.30f;    /* pickup gap, +/-      */
/* The pickup coil is wound in two sections in opposite phase for hum
 * cancelling [EP-DAFx17 sec 3.1], so this instrument's hum floor is
 * structurally below the organ's -60 dB rather than equal to it. */
static constexpr float COND_HUM_LEVEL = 1.3e-4f;  /* -78 dB              */
static constexpr float COND_FLOOR_LEVEL = 2.5e-4f;/* -72 dB broadband    */
static constexpr float COND_HUM_HZ = 60.0f;
/* sec 16: the horizontal polarisation's split from the vertical, as a
 * frequency ratio — the asymmetry is in the wire and its mounting, so it
 * scales with pitch and the beat rate rises with it. Its excitation and
 * how strongly it reaches the pickup are separate depths. [FOLK] */
static constexpr float COND_POLAR_SPLIT = 0.004f;
static constexpr float COND_POLAR_DEPTH = 0.60f;
static constexpr uint64_t COND_SEED = 0x65703733636f6e64u; /* "ep73" "cond" */

const float EP_CONDITION_DEFAULT = 0.5f;

/* A draw's 13-bit field mapped to [-1, 1). */
static float field(uint64_t d, int shift) {
    return (float)(int32_t)((d >> shift) & 0x1FFFu) * (1.0f / 4096.0f) - 1.0f;
}

/* Square root, strike-time only — never on a per-sample path. Exponent
 * halving seeds it to ~3.5 %, and three Newton steps take that to f32
 * exactness over the whole positive range the callers use (contact
 * corners in (0, 1], burst normalisation up to a few hundred). */
static float ep_sqrtf(float x) {
    union { float f; uint32_t u; } v = { x };
    v.u = (v.u >> 1) + 0x1fc00000u;
    float r = v.f;
    r = 0.5f * (r + x / r);
    r = 0.5f * (r + x / r);
    return 0.5f * (r + x / r);
}

/* 1 - exp(-ln(1000)/(t60*fs)) by the alternating Taylor expansion of exp —
 * the generator's smooth_coeff form (sec 4.1). x is at most ~7e-4 here, so
 * the truncation sits far below f32 resolution. */
static float one_minus_exp(float x) {
    if (!(x > 0.0f) || x > 0.25f) x = 0.25f;
    return x * (1.0f - x * (0.5f - x * (1.0f / 6.0f - x / 24.0f)));
}

static float decay_coeff(float sample_rate_hz, float t60_s) {
    return one_minus_exp(LN1000 / (t60_s * sample_rate_hz));
}

/* One-pole lowpass coefficient for a corner at fc: tau = 1/(2 pi fc). */
static float lowpass_coeff(float sample_rate_hz, float fc_hz) {
    return one_minus_exp(6.2831853f * fc_hz / sample_rate_hz);
}

/* sec 16: one sample of the horizontal polarisation, returned as the
 * factor it applies to the voice. The pickup's chisel edge stands
 * perpendicular to the plane the hammer drives [EP-P61], so horizontal
 * motion barely induces anything by itself — what it does is move the tine
 * across the field and modulate how well the vertical motion couples. So
 * it multiplies rather than adds, and what it produces is the slow breath
 * of a real note rather than a second pitch. Exactly 1 at condition 0. */
static float polar_factor(ep_bank *b, int key) {
    float p = b->h_phase[key] + b->h_step[key];
    p -= (p >= 1.0f) ? 1.0f : 0.0f;
    b->h_phase[key] = p;
    float a = b->h_amp[key];
    float f = 1.0f + a * tw_sin_turns(p);
    a -= a * b->dec[0][key];
    b->h_amp[key] = (a < AMP_EPS) ? 0.0f : a;
    return f;
}

/* sec 6.2: the coupling capacitor at the preamplifier's input. A
 * saturating asymmetric pickup leaves a level-dependent DC behind it, and
 * unlike the old cubic form that DC has no closed expression to subtract,
 * so it is filtered off where the instrument filters it off — once, on the
 * summed bus, not per voice. At 10 Hz it costs E1 a quarter of a dB. */
static float dc_block(ep_bank *b, float x) {
    float lp = b->dc_lp + b->dc_c * (x - b->dc_lp);
    /* the same denormal-safe snap every envelope here uses: without it a
     * silenced instrument keeps leaking a decaying tail forever and never
     * reaches exact zero */
    b->dc_lp = (tw_fabsf(lp) < AMP_EPS) ? 0.0f : lp;
    return x - b->dc_lp;
}

/* sec 15: the instrument's own floor — mains hum on the pickup wire and a
 * broadband noise floor. Bank-level, not per voice, because a floor is
 * there whether anything is playing or not; that also means both tick
 * layouts advance it identically. Exactly 0 at condition 0. */
static float floor_tick(ep_bank *b) {
    float p = b->hum_phase + b->hum_step;
    p -= (p >= 1.0f) ? 1.0f : 0.0f;
    b->hum_phase = p;
    uint64_t r = tw_splitmix64(&b->floor_rng);
    float w = (float)(int32_t)(uint32_t)(r >> 32) * (1.0f / 2147483648.0f);
    return b->hum_gain * tw_sin_turns(p) + b->floor_gain * w;
}

/* sec 5.5: one sample of the contact transient. Added after the
 * pickup, not before: the magnet's field geometry acts on tine
 * displacement, and folding a noise burst into that would also feed the
 * quadratic term a nonzero mean square and put a DC step under the
 * attack. */
static float hammer_tick(ep_bank *b, int key) {
    uint64_t r = tw_splitmix64(&b->hn_rng[key]);
    float w = (float)(int32_t)(uint32_t)(r >> 32) * (1.0f / 2147483648.0f);
    float lp = b->hn_lp[key] + b->hn_c[key] * (w - b->hn_lp[key]);
    b->hn_lp[key] = lp;
    float a = b->hn_amp[key];
    float out = lp * a;
    a -= a * b->hn_dec[key];
    b->hn_amp[key] = (a < AMP_EPS) ? 0.0f : a;
    return out;
}

float ep_key_freq_hz(int key) {
    if (key < 0 || key >= EP_KEYS) return 0.0f;
    return EP_F_E1 * EP_SEMI[key % 12] * (float)(1u << (key / 12));
}

int ep_zone(int midi_note) {
    if (midi_note <= 50) return 0;
    if (midi_note <= 60) return 1;
    if (midi_note <= 70) return 2;
    if (midi_note <= 84) return 3;
    return 4;
}

float ep_t60_s(int key, int mode) {
    if (key < 0 || key >= EP_KEYS || mode < 0 || mode >= EP_MODES) return 0.0f;
    float t = T60_E1;
    for (int i = 0; i < key; i++) t *= T60_RATIO;
    return t * EP_MODE_T60[mode];
}

float ep_damp_t60_s(int key) {
    if (key < 0 || key >= EP_KEYS) return 0.0f;
    float t = DAMP_T60_E1;
    for (int i = 0; i < key; i++) t *= DAMP_T60_RATIO;
    return t;
}

/* sec 5.2: how strongly a blow at the striking line excites each mode, per
 * key, relative to mode 1. A clamped-free tine struck at xi = x0/L couples
 * to mode m as phi_m(xi), and the pickup reads the tip, so the weight goes
 * as phi_m(xi) * phi_m(1).
 *
 * Pinned as a table rather than fitted: the length law has a corner in it.
 * A tine cannot be longer than the harp is deep, and the manual's longest
 * replacement blank is 4-3/8 inch [EP-SM 5-1], so below about MIDI 45 the
 * length stops growing and the pitch comes from the counterweight instead —
 * which is exactly what the founding patent describes, heavier springs on
 * the lower-pitched generators [EP-P61]. That corner defeats a polynomial;
 * every entry here is checked against the exact transcendental shape by the
 * test. */
static constexpr float MODE2_SHAPE[EP_KEYS] = {
    1.98054f, 2.15284f, 2.32012f, 2.48217f, 2.63889f, 2.79018f,
    2.93603f, 3.07645f, 3.21149f, 3.34124f, 3.46579f, 3.58527f,
    3.69981f, 3.80955f, 3.91464f, 4.01525f, 4.11153f, 4.19266f,
    4.21791f, 4.24285f, 4.26748f, 4.29180f, 4.31582f, 4.33954f,
    4.36297f, 4.38610f, 4.40894f, 4.43150f, 4.45378f, 4.47577f,
    4.49750f, 4.51895f, 4.54013f, 4.56104f, 4.58170f, 4.60209f,
    4.62223f, 4.64212f, 4.66176f, 4.68115f, 4.70029f, 4.71920f,
    4.73787f, 4.75631f, 4.77451f, 4.79249f, 4.81024f, 4.82776f,
    4.84507f, 4.86216f, 4.87904f, 4.89571f, 4.91216f, 4.92842f,
    4.94446f, 4.96031f, 4.97596f, 4.99141f, 5.00667f, 5.02174f,
    5.03662f, 5.05132f, 5.06583f, 5.08016f, 5.09432f, 5.10829f,
    5.12210f, 5.13573f, 5.14919f, 5.16249f, 5.17561f, 5.18858f,
    5.20139f,
};

static constexpr float MODE3_SHAPE[EP_KEYS] = {
    0.16655f, 0.15855f, 0.51587f, 0.90048f, 1.30762f, 1.73277f,
    2.17173f, 2.62063f, 3.07598f, 3.53464f, 3.99384f, 4.45116f,
    4.90451f, 5.35211f, 5.79245f, 6.22429f, 6.64662f, 7.00911f,
    7.12314f, 7.23629f, 7.34857f, 7.45996f, 7.57045f, 7.68003f,
    7.78870f, 7.89645f, 8.00327f, 8.10916f, 8.21411f, 8.31813f,
    8.42120f, 8.52333f, 8.62451f, 8.72474f, 8.82403f, 8.92236f,
    9.01975f, 9.11619f, 9.21168f, 9.30623f, 9.39984f, 9.49251f,
    9.58423f, 9.67503f, 9.76489f, 9.85383f, 9.94184f, 10.02893f,
    10.11511f, 10.20038f, 10.28474f, 10.36820f, 10.45077f, 10.53245f,
    10.61325f, 10.69317f, 10.77222f, 10.85040f, 10.92773f, 11.00420f,
    11.07983f, 11.15462f, 11.22858f, 11.30171f, 11.37402f, 11.44552f,
    11.51622f, 11.58612f, 11.65523f, 11.72355f, 11.79110f, 11.85788f,
    11.92390f,
};

float ep_pickup_drive(int key) {
    if (key < 0 || key >= EP_KEYS) return 0.0f;
    /* sec 6.1: the slope is pinned at 1/2, so the ratio needs one square
     * root and no general power — the same trick section 5.2 uses. */
    float a = EP_PICKUP_DRIVE_REF * ep_sqrtf(PICKUP_F_REF / ep_key_freq_hz(key));
    return a > PICKUP_DRIVE_MAX ? PICKUP_DRIVE_MAX : a;
}

float ep_mode_shape(int key, int mode) {
    if (key < 0 || key >= EP_KEYS || mode < 0 || mode >= EP_MODES) return 0.0f;
    if (mode == 0) return 1.0f;
    return (mode == 1) ? MODE2_SHAPE[key] : MODE3_SHAPE[key];
}

float ep_mode_weight(int key, int mode, int velocity) {
    if (key < 0 || key >= EP_KEYS || mode < 0 || mode >= EP_MODES) return 0.0f;
    if (velocity < 1) velocity = 1;
    else if (velocity > 127) velocity = 127;
    float f1 = ep_key_freq_hz(key);
    float fm = f1 * EP_MODE_RATIO[mode];
    /* sec 5.2: the corner squared is fc0^2 * sqrt(v/127), so the weight is
     * a ratio of squares — no division by the corner, and one square root
     * instead of two. The ratio falls as 1/f^2 above the corner, which is
     * the envelope of a finite-duration contact pulse; a one-pole shape
     * would be too gentle to be a hammer at all. */
    float c0 = EP_CORNER_HZ[ep_zone(key + EP_NOTE_MIN)];
    float fc2 = c0 * c0 * ep_sqrtf((float)velocity * (1.0f / 127.0f));
    return EP_BASE_W[mode] * ep_mode_shape(key, mode)
         * (fc2 + f1 * f1) / (fc2 + fm * fm);
}

/* Everything derived from the note map and the condition draws. Called at
 * init and again on every condition change, exactly as the organ's wear
 * knob rebuilds its deviation banks. */
static void rebuild(ep_bank *b, float sample_rate_hz) {
    const float guard = NYQUIST_GUARD * sample_rate_hz;
    const float c = b->cond;
    uint64_t seed = COND_SEED;
    for (int k = 0; k < EP_KEYS; k++) {
        uint64_t d1 = tw_splitmix64(&seed), d2 = tw_splitmix64(&seed);
        /* sec 15: tuning drift, in cents, as a frequency multiplier. The
         * cent-to-ratio conversion is linearised — 6 cents is 0.347 %, and
         * the error of treating that as linear is under 6e-6. */
        float detune = 1.0f + c * COND_DETUNE_CENTS * field(d1, 0)
                     * (0.0005777895f);
        float f1 = ep_key_freq_hz(k) * detune;
        b->f1[k] = f1;
        /* sec 6.1: the gap is a per-note setup adjustment, so both how
         * hard the tine drives the field and where it sits in it vary. */
        b->pk_g[k] = ep_pickup_drive(k)
                   * (1.0f + c * COND_ALPHA_DEV * field(d1, 13));
        b->pk_x0[k] = EP_PICKUP_OFFSET
                    * (1.0f + c * COND_ALPHA_DEV * field(d2, 52));
        b->pk_ref[k] = tw_sat(b->pk_x0[k]);
        for (int m = 0; m < EP_MODES; m++) {
            /* sec 15: the clang ratio drifts per note, because the tuning
             * spring moves the fundamental and not the harmonics [EP-P61].
             * Mode 1 is the fundamental and carries no extra deviation. */
            float ratio = EP_MODE_RATIO[m]
                        * (m == 0 ? 1.0f
                           : 1.0f + c * COND_RATIO_DEV * field(d2, 13 * m));
            b->trim[m][k] = (m == 0) ? 1.0f
                          : 1.0f + c * COND_VOICE_DEV * field(d2, 26 + 13 * m);
            /* sec 3.1: a mode inside the guard band renders at gain zero —
             * silence, not foldback. A piano borrows nothing. */
            float f = f1 * ratio;
            bool voiced = f < guard;
            b->gate[m][k] = voiced ? 1.0f : 0.0f;
            b->step[m][k] = voiced ? f / sample_rate_hz : 0.0f;
            b->dec_free[m][k] = decay_coeff(sample_rate_hz, ep_t60_s(k, m));
            b->dec_damp[m][k] = decay_coeff(sample_rate_hz, ep_damp_t60_s(k));
            b->dec[m][k] = b->dec_damp[m][k]; /* at rest the dampers are on */
            /* sec 5.4: the hardest single blow this mode can take, which is
             * also the ceiling a restrike may not push it past. */
            b->ceiling[m][k] = ep_mode_weight(k, m, 127) * b->trim[m][k]
                             * b->gate[m][k];
        }
        /* sec 16: a twin of the fundamental, split by a per-note fraction
         * and running on the same tine's losses. Both depths are exactly
         * 0 at condition 0, which makes the whole stage vanish. */
        float split = 1.0f + c * COND_POLAR_SPLIT * field(d2, 39);
        float fh = f1 * split;
        b->h_step[k] = (fh < guard) ? fh / sample_rate_hz : 0.0f;
        /* One depth, not two. Excitation and pickup sensitivity were
         * separate factors at first, each a small number, and their
         * product came out at a couple of percent — inaudible. What the
         * ear cares about is the modulation index that reaches the bus, so
         * that is what is pinned. */
        b->h_depth[k] = c * COND_POLAR_DEPTH
                      * (0.5f + 0.5f * field(d1, 26));
        b->hn_dec[k] = decay_coeff(sample_rate_hz,
                                   EP_HAMMER_MS[ep_zone(k + EP_NOTE_MIN)] * 1e-3f);
        b->hn_c[k] = lowpass_coeff(sample_rate_hz,
                                   EP_HAMMER_HZ[ep_zone(k + EP_NOTE_MIN)]);
    }
    b->dc_c = one_minus_exp(6.2831853f * DC_BLOCK_HZ / sample_rate_hz);
    b->hum_step = COND_HUM_HZ / sample_rate_hz;
    b->hum_gain = c * COND_HUM_LEVEL;
    b->floor_gain = c * COND_FLOOR_LEVEL;
    b->rate = sample_rate_hz;
}

void ep_bank_init(ep_bank *b, float sample_rate_hz) {
    if (!(sample_rate_hz >= 8000.0f)) sample_rate_hz = 48000.0f;
    *b = (ep_bank){ 0 };
    b->floor_rng = COND_SEED;
    rebuild(b, sample_rate_hz); /* condition 0: the idealized instrument */
    b->relist = true;
}

void ep_bank_set_condition(ep_bank *b, float v) {
    if (!(v > 0.0f)) v = 0.0f; /* NaN and negatives */
    else if (v > 1.0f) v = 1.0f;
    b->cond = v;
    rebuild(b, b->rate);
}

static void set_decrements(ep_bank *b, int key, bool damped) {
    for (int m = 0; m < EP_MODES; m++)
        b->dec[m][key] = damped ? b->dec_damp[m][key] : b->dec_free[m][key];
}

void ep_bank_damp(ep_bank *b, int midi_note) {
    if (midi_note < EP_NOTE_MIN || midi_note > EP_NOTE_MAX) return;
    set_decrements(b, midi_note - EP_NOTE_MIN, true);
}

void ep_bank_undamp(ep_bank *b, int midi_note) {
    if (midi_note < EP_NOTE_MIN || midi_note > EP_NOTE_MAX) return;
    set_decrements(b, midi_note - EP_NOTE_MIN, false);
}

void ep_bank_strike(ep_bank *b, int midi_note, int velocity) {
    if (midi_note < EP_NOTE_MIN || midi_note > EP_NOTE_MAX) {
        b->out_of_compass++;
        return;
    }
    if (velocity < 1) velocity = 1;
    else if (velocity > 127) velocity = 127;
    int k = midi_note - EP_NOTE_MIN;
    float level = EP_LEVEL[velocity]; /* sec 5.1, gamma = 1.042 */
    for (int m = 0; m < EP_MODES; m++) {
        float have = b->amp[m][k];
        /* sec 5.3: a mode at rest has no phase worth keeping; a ringing one
         * keeps its phase, which is what the layout identity rests on. */
        if (have == 0.0f) b->phase[m][k] = 0.0f;
        /* sec 5.4, D5 closed by ear at EP3: the blow adds to what is there,
         * bounded by what its own hardest single blow would give. A hammer
         * puts energy into a tine and has no way to take any out. */
        float a = have + level * ep_mode_weight(k, m, velocity)
               * b->trim[m][k] * b->gate[m][k];
        b->amp[m][k] = (a > b->ceiling[m][k]) ? b->ceiling[m][k] : a;
    }
    /* sec 5.5: the tip meeting the tine. Seed and filter state are both
     * reset here, from the note and the strike ordinal alone, so the two
     * tick layouts enter every blow identically however long the voice sat
     * silent — which is what keeps decision D4's identity intact. */
    int z = ep_zone(midi_note);
    float c = lowpass_coeff(b->rate, EP_HAMMER_HZ[z] * ep_sqrtf(ep_sqrtf(level)));
    b->hn_c[k] = c;
    /* EP_HAMMER_LEVEL is the burst's rms against the strike level, so the
     * two things the raw source costs are divided back out: uniform noise
     * on [-1, 1) carries rms 1/sqrt(3), and a one-pole at coefficient c
     * passes variance c/(2 - c) of what it is given. Without this the
     * constant would silently mean something 20-odd dB quieter than it
     * says, which is exactly the sort of number nobody can calibrate. */
    b->hn_amp[k] = EP_HAMMER_LEVEL[z] * ep_sqrtf(level)
                 * ep_sqrtf(3.0f * (2.0f - c) / c);
    b->hn_lp[k] = 0.0f;
    b->hn_rng[k] = HAMMER_SEED ^ ((uint64_t)midi_note * 0x9e3779b97f4a7c15u)
                 ^ ((uint64_t)(b->strikes++) * 0xbf58476d1ce4e5b9u);
    /* sec 16: the blow spills a little into the plane it is not aimed at.
     * Same add-and-ceiling law as the modes, same phase rule. */
    if (b->h_amp[k] == 0.0f) b->h_phase[k] = 0.0f;
    float hcap = b->h_depth[k];
    float ha = b->h_amp[k] + level * b->h_depth[k];
    b->h_amp[k] = (ha > hcap) ? hcap : ha;
    set_decrements(b, k, false); /* the hammer lifts the damper, sec 8 */
    b->relist = true;
}

void ep_bank_silence(ep_bank *b) {
    for (int m = 0; m < EP_MODES; m++)
        for (int k = 0; k < EP_KEYS; k++) {
            b->amp[m][k] = 0.0f;
            b->phase[m][k] = 0.0f;
            b->dec[m][k] = b->dec_damp[m][k]; /* back to rest */
        }
    for (int k = 0; k < EP_KEYS; k++) {
        b->hn_amp[k] = 0.0f;
        b->hn_lp[k] = 0.0f;
        b->h_amp[k] = 0.0f;
        b->h_phase[k] = 0.0f;
    }
    b->live_n = 0;
    b->relist = false;
}

/* Keys carrying any live mode, ascending — the order matters: it is what
 * makes the gated sum bit-identical to the always-advance one. */
static void rebuild_live(ep_bank *b) {
    int n = 0;
    for (int k = 0; k < EP_KEYS; k++) {
        float s = b->hn_amp[k] + b->h_amp[k];
        for (int m = 0; m < EP_MODES; m++) s += b->amp[m][k];
        if (s > 0.0f) b->live[n++] = (uint8_t)k;
    }
    b->live_n = (uint8_t)n;
    b->relist = false;
}

float ep_bank_tick(ep_bank *b) {
    float voice[EP_KEYS] = { 0 };

    /* restrict is what lets the 73-wide runs vectorize: the four bank rows
     * and the two scratch banks are provably disjoint here, which the
     * compiler cannot see through the struct on its own. */
    for (int m = 0; m < EP_MODES; m++) {
        float *restrict ph = b->phase[m];
        float *restrict am = b->amp[m];
        const float *restrict st = b->step[m];
        const float *restrict dc = b->dec[m];
        for (int k = 0; k < EP_KEYS; k++) {
            float p = ph[k] + st[k];
            p -= (p >= 1.0f) ? 1.0f : 0.0f;
            ph[k] = p;
            float a = am[k];
            voice[k] += a * tw_sin_turns(p);
            /* one-pole toward zero, the generator's smoother with a zero
             * target; the snap keeps a decayed voice out of denormals */
            a -= a * dc[k];
            am[k] = (a < AMP_EPS) ? 0.0f : a;
        }
    }

    float sum = floor_tick(b);
    for (int k = 0; k < EP_KEYS; k++) {
        float g = polar_factor(b, k);
        sum += ep_pickup(voice[k] * g, b->pk_g[k], b->pk_x0[k], b->pk_ref[k])
             + hammer_tick(b, k);
    }
    return dc_block(b, sum);
}

float ep_bank_tick_gated(ep_bank *b) {
    if (b->relist) rebuild_live(b);

    float sum = floor_tick(b);
    for (int i = 0; i < b->live_n; i++) {
        int k = b->live[i];
        float x = 0.0f, alive = 0.0f;
        for (int m = 0; m < EP_MODES; m++) {
            float p = b->phase[m][k] + b->step[m][k];
            p -= (p >= 1.0f) ? 1.0f : 0.0f;
            b->phase[m][k] = p;
            float a = b->amp[m][k];
            x += a * tw_sin_turns(p);
            a -= a * b->dec[m][k];
            a = (a < AMP_EPS) ? 0.0f : a;
            b->amp[m][k] = a;
            alive += a;
        }
        float g = polar_factor(b, k);
        alive += b->hn_amp[k] + b->h_amp[k];
        if (alive == 0.0f) b->relist = true;
        sum += ep_pickup(x * g, b->pk_g[k], b->pk_x0[k], b->pk_ref[k])
             + hammer_tick(b, k);
    }
    return dc_block(b, sum);
}
