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

/* sec 5.2: strike weights before the contact roll-off [FOLK], and the
 * roll-off corner per hammer-hardness zone at velocity 127. The zone
 * boundaries come from the manual's hammer-tip durometer ladder; the
 * corner values are a working geometric set. */
const float EP_BASE_W[EP_MODES] = { 1.0f, 0.55f, 0.18f };
const float EP_CORNER_HZ[EP_ZONES] = { 1800.0f, 2600.0f, 3600.0f, 5000.0f, 7000.0f };

/* sec 2: 440 * 2^(-41/12), and the twelve semitone ratios. The octave
 * factor is an exact power of two, so the whole compass comes off one
 * anchor and twelve constants. */
static constexpr float EP_F_E1 = 41.203445f;
static constexpr float EP_SEMI[12] = {
    1.000000000f, 1.059463094f, 1.122462048f, 1.189207115f,
    1.259921050f, 1.334839854f, 1.414213562f, 1.498307077f,
    1.587401052f, 1.681792831f, 1.781797436f, 1.887748625f,
};

static constexpr float T60_E1 = 17.0f;         /* sec 4 [EP-P61]         */
static constexpr float T60_RATIO = 0.980099f;  /* per semitone, sec 4    */
static constexpr float DAMP_T60_E1 = 0.35f;    /* sec 8 [FOLK]           */
static constexpr float DAMP_T60_RATIO = 0.981119f; /* per semitone, sec 8 */
static constexpr float NYQUIST_GUARD = 0.45f;  /* sec 3.1                */
static constexpr float LN1000 = 6.907755f;     /* t60 -> amplitude tau   */
static constexpr float AMP_EPS = 1e-9f;        /* denormal-safe snap      */

/* Square root of x in (0, 1], for the contact corner only (sec 5.2) — a
 * strike-time cost, never a per-sample one. Exponent-halving seed (~3.5%)
 * plus three Newton steps, which is exact to f32 over this range. */
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
static float decay_coeff(float sample_rate_hz, float t60_s) {
    float x = LN1000 / (t60_s * sample_rate_hz);
    if (!(x > 0.0f) || x > 0.25f) x = 0.25f;
    return x * (1.0f - x * (0.5f - x * (1.0f / 6.0f - x / 24.0f)));
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

float ep_mode_weight(int key, int mode, int velocity) {
    if (key < 0 || key >= EP_KEYS || mode < 0 || mode >= EP_MODES) return 0.0f;
    if (velocity < 1) velocity = 1;
    else if (velocity > 127) velocity = 127;
    float f1 = ep_key_freq_hz(key);
    float fm = f1 * EP_MODE_RATIO[mode];
    /* sec 5.2: the corner squared is fc0^2 * sqrt(v/127), so the weight is
     * a ratio of squares — no division by the corner, and one square root
     * instead of two. */
    float c0 = EP_CORNER_HZ[ep_zone(key + EP_NOTE_MIN)];
    float fc2 = c0 * c0 * ep_sqrtf((float)velocity * (1.0f / 127.0f));
    return EP_BASE_W[mode] * (fc2 + f1 * f1) / (fc2 + fm * fm);
}

void ep_bank_init(ep_bank *b, float sample_rate_hz) {
    if (!(sample_rate_hz >= 8000.0f)) sample_rate_hz = 48000.0f;
    *b = (ep_bank){ 0 };
    const float guard = NYQUIST_GUARD * sample_rate_hz;
    for (int k = 0; k < EP_KEYS; k++) {
        float f1 = ep_key_freq_hz(k);
        b->f1[k] = f1;
        for (int m = 0; m < EP_MODES; m++) {
            /* sec 3.1: a mode inside the guard band renders at gain zero —
             * silence, not foldback. A piano borrows nothing. */
            float f = f1 * EP_MODE_RATIO[m];
            bool voiced = f < guard;
            b->gate[m][k] = voiced ? 1.0f : 0.0f;
            b->step[m][k] = voiced ? f / sample_rate_hz : 0.0f;
            b->dec_free[m][k] = decay_coeff(sample_rate_hz, ep_t60_s(k, m));
            b->dec_damp[m][k] = decay_coeff(sample_rate_hz, ep_damp_t60_s(k));
            b->dec[m][k] = b->dec_damp[m][k]; /* at rest the dampers are on */
            /* sec 5.4: the hardest single blow this mode can take, which is
             * also the ceiling a restrike may not push it past. */
            b->ceiling[m][k] = ep_mode_weight(k, m, 127) * b->gate[m][k];
        }
    }
    b->relist = true;
}

void ep_bank_set_restrike(ep_bank *b, int amp_law, int phase_law) {
    b->amp_law = (amp_law == EP_AMP_ADD) ? EP_AMP_ADD : EP_AMP_REPLACE;
    b->phase_law = (phase_law == EP_PHASE_RESET) ? EP_PHASE_RESET
                                                 : EP_PHASE_CONTINUE;
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
    float level = (float)velocity * (1.0f / 127.0f); /* sec 5.1, gamma = 1 */
    for (int m = 0; m < EP_MODES; m++) {
        float have = b->amp[m][k];
        /* sec 5.3: a mode at rest has no phase worth keeping, whatever D5
         * settles for a ringing one — the layout identity rests on it. */
        if (have == 0.0f || b->phase_law == EP_PHASE_RESET) b->phase[m][k] = 0.0f;
        float want = level * ep_mode_weight(k, m, velocity) * b->gate[m][k];
        float a = (b->amp_law == EP_AMP_ADD) ? have + want : want;
        b->amp[m][k] = (a > b->ceiling[m][k]) ? b->ceiling[m][k] : a;
    }
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
    b->live_n = 0;
    b->relist = false;
}

/* Keys carrying any live mode, ascending — the order matters: it is what
 * makes the gated sum bit-identical to the always-advance one. */
static void rebuild_live(ep_bank *b) {
    int n = 0;
    for (int k = 0; k < EP_KEYS; k++) {
        float s = 0.0f;
        for (int m = 0; m < EP_MODES; m++) s += b->amp[m][k];
        if (s > 0.0f) b->live[n++] = (uint8_t)k;
    }
    b->live_n = (uint8_t)n;
    b->relist = false;
}

float ep_bank_tick(ep_bank *b) {
    float voice[EP_KEYS] = { 0 };
    float sq[EP_KEYS] = { 0 }; /* sum of squared amplitudes, sec 6 */

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
            sq[k] += a * a;
            /* one-pole toward zero, the generator's smoother with a zero
             * target; the snap keeps a decayed voice out of denormals */
            a -= a * dc[k];
            am[k] = (a < AMP_EPS) ? 0.0f : a;
        }
    }

    float sum = 0.0f;
    for (int k = 0; k < EP_KEYS; k++) sum += ep_pickup(voice[k], 0.5f * sq[k]);
    return sum;
}

float ep_bank_tick_gated(ep_bank *b) {
    if (b->relist) rebuild_live(b);

    float sum = 0.0f;
    for (int i = 0; i < b->live_n; i++) {
        int k = b->live[i];
        float x = 0.0f, sq = 0.0f, alive = 0.0f;
        for (int m = 0; m < EP_MODES; m++) {
            float p = b->phase[m][k] + b->step[m][k];
            p -= (p >= 1.0f) ? 1.0f : 0.0f;
            b->phase[m][k] = p;
            float a = b->amp[m][k];
            x += a * tw_sin_turns(p);
            sq += a * a;
            a -= a * b->dec[m][k];
            a = (a < AMP_EPS) ? 0.0f : a;
            b->amp[m][k] = a;
            alive += a;
        }
        if (alive == 0.0f) b->relist = true;
        sum += ep_pickup(x, 0.5f * sq);
    }
    return sum;
}
