/* ep73 keys, dampers and the sustain pedal over the struck-voice bank.
 * Constants and their sources: docs/ep-constants.md sections 8 and 10. */
#include "epiano.h"

void ep_piano_init(ep_piano *p, float sample_rate_hz) {
    *p = (ep_piano){ 0 };
    ep_bank_init(&p->bank, sample_rate_hz);
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

/* The gated layout, decision D4 (docs/ep1-evidence.md section C). */
float ep_piano_tick(ep_piano *p) {
    return ep_bank_tick_gated(&p->bank);
}
