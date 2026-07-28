/* ep73 keys, dampers and the sustain pedal over the struck-voice bank.
 * Constants and their sources: docs/ep-constants.md sections 8 and 10. */
#include "epiano.h"

/* sec 13. */
const float EP_DRIVE_SCALE = 2.0f;

void ep_piano_init(ep_piano *p, float sample_rate_hz) {
    *p = (ep_piano){ 0 };
    ep_bank_init(&p->bank, sample_rate_hz);
    tw_drive_init(&p->drive, sample_rate_hz);
    /* sec 13: the odd kernel, not the derived triode curve. This
     * instrument's preamplifier is solid-state [EP-SM 11-1], so symmetric
     * clipping is the honest shape; and the voice already carries a strong
     * asymmetry from its own pickup, which a second asymmetric stage would
     * only double. */
    tw_drive_set_kernel(&p->drive, true);
    ep_tremolo_init(&p->trem, sample_rate_hz);
    ep_cabinet_init(&p->cab, sample_rate_hz);
    /* sec 15: the instrument ships with tolerance, the bank does not. The
     * organ splits the same way — tw_generator_init leaves wear at 0 and
     * tw_organ_init applies the shipped default. */
    ep_bank_set_condition(&p->bank, EP_CONDITION_DEFAULT);
}

void ep_piano_set_condition(ep_piano *p, float v) {
    ep_bank_set_condition(&p->bank, v);
}

void ep_piano_set_drive(ep_piano *p, float v) {
    tw_drive_set(&p->drive, v);
}

/* sec 8, the whole damper rule: off when the key is held or the pedal is
 * down, on otherwise. Catching the pedal late under a decaying note,
 * releasing a key with the pedal already down, and letting the pedal go
 * while keys are still held are all this one line. */
static void refresh_damper(ep_piano *p, int key) {
    int note = key + EP_NOTE_MIN;
    if (p->held[key] || p->sustain) ep_bank_undamp(&p->bank, note);
    else ep_bank_damp(&p->bank, note);
}

void ep_piano_note(ep_piano *p, int midi_note, bool down, int velocity) {
    if (midi_note < EP_NOTE_MIN || midi_note > EP_NOTE_MAX) {
        p->bank.out_of_compass++;
        return;
    }
    int k = midi_note - EP_NOTE_MIN;
    if (down && velocity > 0) {
        p->held[k] = true;
        ep_bank_strike(&p->bank, midi_note, velocity); /* lifts the damper */
    } else {
        p->held[k] = false;
        refresh_damper(p, k);
    }
}

void ep_piano_set_sustain(ep_piano *p, bool down) {
    if (down == p->sustain) return;
    p->sustain = down;
    for (int k = 0; k < EP_KEYS; k++) refresh_damper(p, k);
}

void ep_piano_key_pressure(ep_piano *p, int midi_note, int value) {
    (void)value; /* sec 10.1: no EP meaning — parsed, ignored, counted */
    if (midi_note < EP_NOTE_MIN || midi_note > EP_NOTE_MAX) {
        p->bank.out_of_compass++;
        return;
    }
    p->pressure++;
}

void ep_piano_panic(ep_piano *p) {
    p->sustain = false;
    for (int k = 0; k < EP_KEYS; k++) {
        p->held[k] = false;
        ep_bank_damp(&p->bank, k + EP_NOTE_MIN);
    }
}

/* The gated layout, decision D4 (docs/ep1-evidence.md section C), then the
 * preamp. The scale in and out is exact because EP_DRIVE_SCALE is a power
 * of two, and tw_drive returns its input untouched at drive 0, so the
 * whole path is a bit-exact bypass with the knob down. */
float ep_piano_tick(ep_piano *p) {
    float x = ep_bank_tick_gated(&p->bank) * EP_DRIVE_SCALE;
    return tw_drive_tick(&p->drive, x) * (1.0f / EP_DRIVE_SCALE);
}

/* The generator's Taylor form for 1 - exp(-x), the same one ep_voice.c
 * uses; valid to x <= 0.25, which every corner here sits far under. */
static float one_minus_exp(float x) {
    if (!(x > 0.0f) || x > 0.25f) x = 0.25f;
    return x * (1.0f - x * (0.5f - x * (1.0f / 6.0f - x / 24.0f)));
}

/* --- tremolo (sec 12) ------------------------------------------------- */

static constexpr float TREM_HZ_MIN = 1.0f;
static constexpr float TREM_HZ_MAX = 10.0f;
static constexpr float TREM_HZ_DEFAULT = 5.5f;

void ep_tremolo_init(ep_tremolo *t, float sample_rate_hz) {
    if (!(sample_rate_hz >= 8000.0f)) sample_rate_hz = 48000.0f;
    *t = (ep_tremolo){ 0 };
    ep_tremolo_set_rate(t, sample_rate_hz, TREM_HZ_DEFAULT);
}

void ep_tremolo_set_rate(ep_tremolo *t, float sample_rate_hz, float hz) {
    if (!(sample_rate_hz >= 8000.0f)) sample_rate_hz = 48000.0f;
    if (!(hz >= TREM_HZ_MIN)) hz = TREM_HZ_MIN; /* NaN and below-range */
    else if (hz > TREM_HZ_MAX) hz = TREM_HZ_MAX;
    t->step = hz / sample_rate_hz;
}

void ep_tremolo_set_depth(ep_tremolo *t, float d) {
    if (!(d > 0.0f)) d = 0.0f; /* NaN and negatives */
    else if (d > 1.0f) d = 1.0f;
    t->depth = d;
}

void ep_tremolo_set_mode(ep_tremolo *t, int mode) {
    t->mode = (mode == EP_TREM_PAN) ? EP_TREM_PAN : EP_TREM_AM;
}

void ep_tremolo_set_cc(ep_tremolo *t, int value) {
    if (value < 0) value = 0;
    else if (value > 127) value = 127;
    if (value == 0) {
        ep_tremolo_set_depth(t, 0.0f);
    } else if (value < 64) {
        ep_tremolo_set_mode(t, EP_TREM_AM);
        ep_tremolo_set_depth(t, (float)value * (1.0f / 63.0f));
    } else {
        ep_tremolo_set_mode(t, EP_TREM_PAN);
        ep_tremolo_set_depth(t, (float)(value - 63) * (1.0f / 64.0f));
    }
}

/* The LFO runs whatever the depth is — it is an oscillator in the
 * amplifier, not a thing the panel switches on — but at depth 0 both gains
 * are exactly 1 and the stage is a bit-exact bypass. */
tw_stereo ep_tremolo_tick(ep_tremolo *t, float x) {
    float p = t->phase + t->step;
    p -= (p >= 1.0f) ? 1.0f : 0.0f;
    t->phase = p;
    float lfo = tw_sin_turns(p);
    float half = 0.5f * t->depth;
    if (t->mode == EP_TREM_PAN) {
        return (tw_stereo){ x * (1.0f - half * (1.0f - lfo)),
                            x * (1.0f - half * (1.0f + lfo)) };
    }
    float g = 1.0f - half * (1.0f - lfo);
    return (tw_stereo){ x * g, x * g };
}

/* --- cabinet (sec 14) ------------------------------------------------ */

const float EP_CAB_LOW_HZ = 80.0f;
const float EP_CAB_HIGH_HZ = 4000.0f;

void ep_cabinet_init(ep_cabinet *c, float sample_rate_hz) {
    if (!(sample_rate_hz >= 8000.0f)) sample_rate_hz = 48000.0f;
    *c = (ep_cabinet){ 0 };
    c->c_lo = one_minus_exp(6.2831853f * EP_CAB_LOW_HZ / sample_rate_hz);
    c->c_hi = one_minus_exp(6.2831853f * EP_CAB_HIGH_HZ / sample_rate_hz);
}

void ep_cabinet_set(ep_cabinet *c, float v) {
    if (!(v > 0.0f)) v = 0.0f; /* NaN and negatives */
    else if (v > 1.0f) v = 1.0f;
    c->mix = v;
}

/* Two one-poles for the cone's top, one for the box's bottom, and a blend
 * back to dry.
 *
 * At mix 0 the stage returns its input and touches no state, the way the
 * organ's rotary bypass does. Blending with a zero coefficient is not good
 * enough: `a + 0*(wet-a)` turns -0.0 into +0.0, which is a real hole in a
 * bit-exactness claim even though it is inaudible. A cabinet that is not in
 * the signal path has no state to keep. */
tw_stereo ep_cabinet_tick(ep_cabinet *c, tw_stereo x) {
    if (c->mix == 0.0f) return x;
    float in[2] = { x.l, x.r }, out[2];
    for (int ch = 0; ch < 2; ch++) {
        float a = in[ch];
        c->lp1[ch] += c->c_hi * (a - c->lp1[ch]);
        c->lp2[ch] += c->c_hi * (c->lp1[ch] - c->lp2[ch]);
        c->hp[ch] += c->c_lo * (c->lp2[ch] - c->hp[ch]);
        float wet = c->lp2[ch] - c->hp[ch];
        out[ch] = a + c->mix * (wet - a);
    }
    return (tw_stereo){ out[0], out[1] };
}

tw_stereo ep_piano_tick_stereo(ep_piano *p) {
    return ep_cabinet_tick(&p->cab, ep_tremolo_tick(&p->trem, ep_piano_tick(p)));
}

void ep_piano_set_cabinet(ep_piano *p, float v) {
    ep_cabinet_set(&p->cab, v);
}

void ep_piano_set_tremolo(ep_piano *p, int cc_value) {
    ep_tremolo_set_cc(&p->trem, cc_value);
}
