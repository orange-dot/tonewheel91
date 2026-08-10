/* tonewheel91 organ layer: key contacts, busbar folding, swell.
 * Model and sources: docs/constants.md sections 4-7. Freestanding. */
#include "tonewheel.h"

/* Contact-gain smoothing (the click shaper). Pinned range 0.2-1.5 ms;
 * default chosen bright, tuned by ear. constants.md section 7. */
static constexpr float CLICK_TAU_S = 0.00025f;
static constexpr float SWELL_TAU_S = 0.010f;
static constexpr float STAGGER_MAX_S = 0.015f; /* slow press, nine buses */
static constexpr float RELEASE_STAGGER_S = 0.003f;
static constexpr float BOUNCE_WINDOW_S = 0.002f; /* <= 3 toggles inside */

/* Key depth (sec 7.1): the nine make points sit evenly over the 0..127
 * travel, each carrying a +-band. A wiping spring contact breaks lower
 * than it makes, so the band is the mechanism as well as the guard that
 * keeps a finger parked on a boundary from chattering its contact. */
static constexpr int DEPTH_HYST = 4;

static int make_point(int i) { return (i + 1) * 128 / (TW_DRAWBARS + 1); }

/* Walk the made-contact count to the one the travel commands: rising
 * needs the make point plus the band, falling the one below it minus
 * the band, so a position inside a band holds whatever it reached. */
static int depth_made(int made, int depth) {
    while (made < TW_DRAWBARS && depth >= make_point(made) + DEPTH_HYST) made++;
    while (made > 0 && depth < make_point(made - 1) - DEPTH_HYST) made--;
    return made;
}

static uint64_t rng_next(uint64_t *s) { return tw_splitmix64(s); }

/* Taper: six resistance-wire classes per (key, bus), dB-ladder order
 * +7, +3.5, 0, -3.5, -7, -10 dB / 10, 15, 24, 34, 50, 100 ohm. Gains are
 * precomputed dB values (no libm in the core); the dB column is the level
 * authority, the ohm column the robbing authority. constants.md 6.1. */
static constexpr float TAPER_GAIN[6] = {
    2.2387f, 1.4962f, 1.0f, 0.6683f, 0.4467f, 0.3162f,
};
static constexpr float TAPER_OHM[6] = {
    10.0f, 15.0f, 24.0f, 34.0f, 50.0f, 100.0f,
};
/* Rg + Rb = 4 + 1 ohm, the patent's worked network. constants.md 6. */
static constexpr float R_SRC = 5.0f;

/* Per-bus wire class over keys, run-length {last key, class}; rows padded
 * by repeating the final run. constants.md 6.1 full table. */
static constexpr uint8_t TAPER_RUNS[TW_DRAWBARS][6][2] = {
    { {10,5}, {16,4}, {24,3}, {36,2}, {48,1}, {61,0} }, /* 16'    */
    { {14,3}, {38,2}, {50,1}, {61,0}, {61,0}, {61,0} }, /* 5-1/3' */
    { {15,4}, {23,3}, {37,2}, {49,1}, {61,0}, {61,0} }, /* 8'     */
    { {13,3}, {39,2}, {61,3}, {61,3}, {61,3}, {61,3} }, /* 4'     */
    { {12,0}, {20,1}, {40,2}, {52,3}, {61,4}, {61,4} }, /* 2-2/3' */
    { {11,0}, {20,1}, {41,2}, {55,3}, {61,4}, {61,4} }, /* 2'     */
    { {18,1}, {42,2}, {51,3}, {61,4}, {61,4}, {61,4} }, /* 1-3/5' */
    { {43,2}, {48,3}, {61,4}, {61,4}, {61,4}, {61,4} }, /* 1-1/3' */
    { {43,2}, {61,4}, {61,4}, {61,4}, {61,4}, {61,4} }, /* 1'     */
};

static int taper_class(int key, int bus) { /* key 0-based */
    const uint8_t (*run)[2] = TAPER_RUNS[bus];
    int i = 0;
    while (i < 5 && key + 1 > run[i][0]) i++;
    return run[i][1];
}

/* Percussion decay time constants: SLOW = R58 alone (4.70 Mohm), FAST =
 * R57||R58 (1.137 Mohm), ratio 4.133:1. constants.md section 8. */
static constexpr float PERC_TAU_SLOW_S = 1.551f;
static constexpr float PERC_TAU_FAST_S = 0.375f;

/* Re-arm: C31 recharges through R55 + R56 = 104 kohm, tau ~= 34 ms
 * (sec 8, derived). Reading recovery as one tau is [decision]. What the
 * constant has to do is separate a detached attack from the 2 ms of
 * contact bounce on the same sensing line -- it sits two orders below a
 * playable staccato gap and one order above the bounce window. */
static constexpr float PERC_REARM_S = 0.034f;

/* Shipped wear default (design.md: nonzero — tolerance effects exist on
 * a factory-new unit; wear = 0 stays the idealized test reference).
 * constants.md sec 12.1 [FOLK]; by-ear verdict overrides. */
const float TW_WEAR_DEFAULT = 0.2f;

/* Peak percussion amplitude at trigger: [decision], matches the 0 dB
 * taper reference class (TAPER_GAIN[2] above); tune by ear once played
 * against reference recordings. constants.md section 8. */
static constexpr float PERC_PEAK_GAIN = 1.0f;

/* SOFT pad on the percussion channel: [decision] placeholder. The R46/
 * R59/R51 divider that would derive this from the schematic is not
 * resolved from prose alone (constants.md section 8, still open) --
 * halves the peak until someone with the schematic can pin the real
 * ratio. */
static constexpr float PERC_SOFT_PAD = 0.5f;

/* NORMAL-mode sustained-tone attenuation [derived]: R50 (22 ohm, the
 * percussion return leg, constants.md section 8) folded as a zero-gain
 * shunt against this network's R_SRC (5 ohm) -- one fixed ratio, a
 * simplification of the full per-taper-class merge law in refold_wheel;
 * caveat in docs/m3-evidence.md. */
static constexpr float PERC_NORMAL_ATTEN = R_SRC / (R_SRC + 22.0f);

/* Per (wheel, bus), the closed contacts i tapping it merge as
 *     Rpar         = 1 / sum(1/Rw_i)
 *     merge_ratio  = (1/(R_SRC + Rpar)) / sum(1/(R_SRC + Rw_i))
 *     contribution = merge_ratio * sum(g_i)
 * A single contact passes its taper gain exactly (merge_ratio == 1);
 * equal wires collapse to the old a(k) = 4k/(k+3), which is this law's
 * special case, not a rival. constants.md sections 6 and 6.1. */
static void refold_wheel(tw_organ *o, int w) {
    float t = 0.0f;
    for (int b = 0; b < TW_DRAWBARS; b++) {
        /* Ninth-drawbar theft (sec 8): the 1' bus is the trigger-sensing
         * line while percussion is enabled, so it is out of service for
         * the sustained tone -- unconditional on trigger/decay state. */
        if (b == 8 && o->perc.on) continue;
        const uint8_t *tap = o->taps[b][w];
        float g = 0.0f, inv_rw = 0.0f, inv_rtot = 0.0f;
        int n = 0;
        for (int i = 0; i < TW_TAP_MAX && tap[i]; i++) {
            int key = tap[i] - 1;
            if (!o->contact[key][b]) continue;
            int c = taper_class(key, b);
            g += TAPER_GAIN[c];
            inv_rw += 1.0f / TAPER_OHM[c];
            inv_rtot += 1.0f / (R_SRC + TAPER_OHM[c]);
            n++;
        }
        if (n == 0) continue;
        float ratio = (n == 1)
            ? 1.0f : 1.0f / ((R_SRC + 1.0f / inv_rw) * inv_rtot);
        t += ratio * g * TW_DRAWBAR_GAIN[o->registration[b]];
    }
    /* NORMAL-mode sustained-tone attenuation (sec 8): only the wheel the
     * percussion channel is currently tapping is affected. */
    if (w == o->perc.wheel && o->perc.on && o->perc.normal) t *= PERC_NORMAL_ATTEN;
    o->gen.keyed_target[w - 1] = t;
}

static void refold_all(tw_organ *o) {
    for (int w = TW_WHEEL_MIN; w <= TW_WHEEL_MAX; w++) refold_wheel(o, w);
}

/* Percussion tap (sec 4/8): 2nd harmonic = 4' bus, 3rd = 2-2/3' bus,
 * pre-drawbar -- the wheel choice and its peak amplitude never depend on
 * that bus's registration digit. Refolds the old and new tapped wheel so
 * NORMAL attenuation (refold_wheel) follows the trigger. */
static void percussion_trigger(tw_organ *o, int key) {
    int bus = o->perc.third ? 4 : 3;
    int w = tw_wheel_index(key + 1, bus);
    int old = o->perc.wheel;
    o->perc.wheel = w;
    o->gen.perc_target[w - 1] =
        PERC_PEAK_GAIN * (o->perc.normal ? 1.0f : PERC_SOFT_PAD);
    if (old && old != w) refold_wheel(o, old);
    refold_wheel(o, w);
}

/* The trigger-sensing line (sec 8): the 1' bus sits at about -25 V, and
 * closing any key's ninth contact grounds it through the generator
 * filters, which isolates the control-tube grid and lets C31 start
 * drifting -- the envelope. So percussion follows that one contact, not
 * the key event: a press that stops short of it never fires, and while
 * any of them is closed the grid stays clamped, which is the
 * single-trigger rule. Opening the last one starts the R55/R56 recovery,
 * whose 34 ms is what makes a detached attack detached; a re-close
 * inside that window (the next legato key, or this contact's own bounce)
 * abandons it. */
static void sense_contact(tw_organ *o, int key, bool closed) {
    if (closed) {
        o->perc.sense_n++;
        o->perc.rearm_at = 0;
        if (o->perc.armed) {
            if (o->perc.on) percussion_trigger(o, key);
            o->perc.armed = false;
        }
    } else if (--o->perc.sense_n == 0) {
        o->perc.rearm_at = o->now + (int64_t)(o->rate * PERC_REARM_S);
    }
}

static void apply_contact(tw_organ *o, int key, int bus, bool closed) {
    if (o->contact[key][bus] == closed) return;
    o->contact[key][bus] = closed;
    if (bus == TW_DRAWBARS - 1) sense_contact(o, key, closed);
    refold_wheel(o, tw_wheel_index(key + 1, bus));
}

void tw_organ_init(tw_organ *o, float sample_rate_hz) {
    sample_rate_hz = tw_sample_rate_hz(sample_rate_hz);
    *o = (tw_organ){ 0 };
    o->rate = sample_rate_hz;
    o->rng = 0x7477393174773931u; /* fixed seed: determinism contract */
    for (int b = 0; b < TW_DRAWBARS; b++)
        for (int k = 1; k <= TW_KEYS; k++) {
            uint8_t *tap = o->taps[b][tw_wheel_index(k, b)];
            for (int i = 0; i < TW_TAP_MAX; i++)
                if (!tap[i]) { tap[i] = (uint8_t)k; break; }
        }
    tw_generator_init(&o->gen, sample_rate_hz, CLICK_TAU_S);
    tw_scanner_init(&o->scan, sample_rate_hz); /* mode starts OFF */
    static const uint8_t default_reg[TW_DRAWBARS] = { 8, 8, 8, 0, 0, 0, 0, 0, 0 };
    for (int b = 0; b < TW_DRAWBARS; b++) o->registration[b] = default_reg[b];
    o->swell_target = 1.0f;
    o->swell_gain = 1.0f;
    o->swell_coeff = 1.0f / (SWELL_TAU_S * sample_rate_hz);
    o->perc = (tw_percussion){ .armed = true, .normal = true };
    tw_generator_set_perc_tau(&o->gen, sample_rate_hz, PERC_TAU_FAST_S);
    tw_organ_set_wear(o, TW_WEAR_DEFAULT);
}

/* Carry a key to `target` contacts made (bus order 0..8), the transitions
 * spread over `span` frames with per-contact bounce. target = 9 is a full
 * press, 0 a full release, in between is key depth. Only buses that must
 * actually move are scheduled, and the pending list is rewritten from the
 * contacts as they stand, so a target arriving mid-flight takes over from
 * the one before it without leaving a contact behind. */
static void schedule_key(tw_organ *o, int key, int target, int span) {
    int bounce_w = (int)(o->rate * BOUNCE_WINDOW_S);
    tw_contact_ev *ev = o->pend[key];
    int n = 0;
    for (int b = 0; b < TW_DRAWBARS; b++) {
        bool down = b < target;
        if (o->contact[key][b] == down) continue;
        int64_t t = o->now + 1 + (int64_t)span * b / (TW_DRAWBARS - 1);
        ev[n++] = (tw_contact_ev){ t, (uint8_t)b, down };
        int nb = (int)(rng_next(&o->rng) % 4);
        int64_t tt = t;
        for (int j = 0; j < nb; j++) {
            uint64_t lim = (uint64_t)(bounce_w / (2 * nb)) + 1;
            tt += 1 + (int64_t)(rng_next(&o->rng) % lim);
            ev[n++] = (tw_contact_ev){ tt, (uint8_t)b, !down };
            tt += 1 + (int64_t)(rng_next(&o->rng) % lim);
            ev[n++] = (tw_contact_ev){ tt, (uint8_t)b, down };
        }
    }
    for (int i = 1; i < n; i++) { /* insertion sort; n <= 63, mostly ordered */
        tw_contact_ev e = ev[i];
        int j = i;
        while (j > 0 && ev[j - 1].frame > e.frame) {
            ev[j] = ev[j - 1];
            j--;
        }
        ev[j] = e;
    }
    o->pend_n[key] = (uint8_t)n;
    o->pend_i[key] = 0;
}

void tw_organ_note(tw_organ *o, int midi_note, bool down, int velocity) {
    if (down && velocity == 0) down = false;
    if (midi_note < TW_NOTE_MIN || midi_note > TW_NOTE_MAX) {
        o->out_of_compass++;
        return;
    }
    int key = midi_note - TW_NOTE_MIN;
    if (o->held[key] == down) return;
    o->held[key] = down;
    int span;
    if (down) {
        int v = velocity < 1 ? 1 : velocity > 127 ? 127 : velocity;
        span = (int)(o->rate * STAGGER_MAX_S * (float)(127 - v) / 126.0f);
    } else {
        span = (int)(o->rate * RELEASE_STAGGER_S);
    }
    o->made[key] = down ? TW_DRAWBARS : 0; /* a press bottoms out (sec 7.1) */
    schedule_key(o, key, o->made[key], span);
    /* Percussion is not touched here: its trigger is the 1' contact
     * (sec 8, sense_contact), which this press has only scheduled. */
}

/* The travel of one held key (sec 7.1). The position stream is itself
 * the stagger — each message moves the contacts it has to and no
 * others — so the transitions go out with zero span and keep only their
 * bounce. A position that lands on the count already made changes
 * nothing at all, RNG included: a surface may stream at any rate. */
void tw_organ_note_depth(tw_organ *o, int midi_note, int depth) {
    if (midi_note < TW_NOTE_MIN || midi_note > TW_NOTE_MAX) {
        o->out_of_compass++;
        return;
    }
    int key = midi_note - TW_NOTE_MIN;
    if (!o->held[key]) return; /* note-off stays authoritative */
    int d = depth < 0 ? 0 : depth > 127 ? 127 : depth;
    int made = depth_made(o->made[key], d);
    if (made == o->made[key]) return;
    o->made[key] = (uint8_t)made;
    schedule_key(o, key, made, 0);
}

void tw_organ_set_registration(tw_organ *o, const uint8_t digits[TW_DRAWBARS]) {
    for (int b = 0; b < TW_DRAWBARS; b++)
        o->registration[b] = digits[b] > 8 ? 8 : digits[b];
    refold_all(o);
}

void tw_organ_set_drawbar(tw_organ *o, int drawbar, int digit) {
    if (drawbar < 0 || drawbar >= TW_DRAWBARS) return;
    o->registration[drawbar] = (uint8_t)(digit < 0 ? 0 : digit > 8 ? 8 : digit);
    refold_all(o);
}

void tw_organ_set_percussion(tw_organ *o, bool on, bool third, bool slow, bool normal) {
    o->perc.on = on;
    o->perc.third = third;
    o->perc.slow = slow;
    o->perc.normal = normal;
    tw_generator_set_perc_tau(&o->gen, o->rate, slow ? PERC_TAU_SLOW_S : PERC_TAU_FAST_S);
    refold_all(o);
}

void tw_organ_set_vibrato(tw_organ *o, int mode) {
    if (mode < TW_VIB_OFF) mode = TW_VIB_OFF;
    else if (mode > TW_VIB_C3) mode = TW_VIB_C3;
    o->scan.mode = mode;
}

void tw_organ_set_wear(tw_organ *o, float v) {
    tw_generator_set_wear(&o->gen, v); /* sanitize lives in the setter */
}

void tw_organ_set_swell(tw_organ *o, float v) {
    if (!(v >= 0.0f)) v = 0.0f;
    else if (v > 1.0f) v = 1.0f;
    o->swell_target = v * v; /* audio taper; curve open, tuned by ear */
}

void tw_organ_panic(tw_organ *o) {
    for (int k = 0; k < TW_KEYS; k++) {
        o->pend_n[k] = 0;
        o->pend_i[k] = 0;
        o->held[k] = false;
        o->made[k] = 0;
        for (int b = 0; b < TW_DRAWBARS; b++) o->contact[k][b] = false;
    }
    for (int w = 0; w < TW_WHEELS; w++) o->gen.perc_target[w] = 0.0f;
    o->perc.wheel = 0;
    o->perc.armed = true; /* the contacts went with the keys, so does K */
    o->perc.sense_n = 0;
    o->perc.rearm_at = 0;
    refold_all(o);
}

float tw_organ_tick(tw_organ *o) {
    /* The grid finishing its R55/R56 recovery (sec 8). One scalar test
     * against a bank loop that already runs 61 wide. */
    if (o->perc.rearm_at && o->now >= o->perc.rearm_at) {
        o->perc.armed = true;
        o->perc.rearm_at = 0;
    }
    for (int k = 0; k < TW_KEYS; k++) {
        while (o->pend_i[k] < o->pend_n[k]
               && o->pend[k][o->pend_i[k]].frame <= o->now) {
            tw_contact_ev e = o->pend[k][o->pend_i[k]++];
            apply_contact(o, k, e.bus, e.closed);
        }
    }
    o->now++;
    tw_frame f = tw_generator_tick(&o->gen);
    float sw = o->swell_gain + o->swell_coeff * (o->swell_target - o->swell_gain);
    o->swell_gain = (tw_fabsf(sw - o->swell_target) < 1e-9f) ? o->swell_target : sw;

    /* Scanner sits between the keyed+perc sum and swell (sec 9). OFF
     * takes the original expression untouched — bit-identical to pre-M4.
     * The line feed is the above-80 Hz remainder plus percussion (its
     * lowest tap is wheel 25, so all of it is line-side). The M7 bleed
     * bus joins the same generator-side sum (sec 13: crosstalk couples
     * at the wheel wiring, ahead of the preamp/line), riding the line
     * whole — its sub-80 Hz share is far below the bass-split's
     * audibility [decision]. frame.leak is exactly +0 at wear 0, so
     * every pre-M7 render is bit-stable. */
    float mix;
    if (o->scan.mode == TW_VIB_OFF) {
        mix = f.keyed + f.percussion + f.leak;
    } else {
        float high = (f.keyed - f.keyed_low) + f.percussion + f.leak;
        mix = tw_scanner_tick(&o->scan, f.keyed_low, high);
    }
    return mix * o->swell_gain;
}
