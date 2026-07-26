/* ep73 core — the tine electric piano. Freestanding C23 under the same
 * doctrine as tonewheel.h, which it includes for the shared kernels
 * (tw_sin_turns, tw_fabsf, tw_splitmix64, tw_fnv1a64), the MIDI parser,
 * and later tw_drive. The organ never includes this header.
 * Constants and their sources: docs/ep-constants.md. */
#ifndef EPIANO_H
#define EPIANO_H

#include "tonewheel.h"

enum {
    EP_KEYS     = 73,  /* E1..E7                                        */
    EP_MODES    = 3,   /* tine flexural modes per voice, sec 3          */
    EP_ZONES    = 5,   /* hammer-tip hardness sections, sec 5.2         */
    EP_NOTE_MIN = 28,  /* E1                                            */
    EP_NOTE_MAX = 100, /* E7                                            */
};

/* Mode frequency ratios (clamped-free cantilever, sec 3) and the extra
 * per-mode damping factors on t60 (sec 4). Exposed for tests and the
 * evidence harness. */
extern const float EP_MODE_RATIO[EP_MODES];
extern const float EP_MODE_T60[EP_MODES];

/* Strike weights before the contact roll-off, and the roll-off corner per
 * hammer-hardness zone at velocity 127 (sec 5.2). */
extern const float EP_BASE_W[EP_MODES];
extern const float EP_CORNER_HZ[EP_ZONES];

/* Fundamental of key 0..72 at the pinned equal temperament, A4 = 440
 * (sec 2). Out-of-range key returns 0. */
[[nodiscard]] float ep_key_freq_hz(int key);

/* Hammer-hardness zone 0..4 of a MIDI note (sec 5.2). Notes outside the
 * compass clamp to the nearest zone. */
[[nodiscard]] int ep_zone(int midi_note);

/* Pinned free-decay t60 of (key, mode) in seconds (sec 4). Out-of-range
 * arguments return 0. */
[[nodiscard]] float ep_t60_s(int key, int mode);

/* Pinned damped t60 of a key, in seconds (sec 8). No mode argument: the
 * felt sets the rate and does not care which mode it is stopping.
 * Out-of-range key returns 0. */
[[nodiscard]] float ep_damp_t60_s(int key);

/* The velocity-to-spectrum weight of (key, mode) at a velocity 1..127
 * (sec 5.2), normalised so mode 0 is always EP_BASE_W[0] — velocity moves
 * loudness through sec 5.1 and timbre through here, and neither leaks into
 * the other. Hostile arguments clamp; out-of-range key or mode returns 0.
 * This is the function the bank strikes with, not a restatement of it. */
[[nodiscard]] float ep_mode_weight(int key, int mode, int velocity);

/* Pickup nonlinearity, sec 6: the cubic series of (1 - exp(-alpha x))/alpha
 * at alpha = 1.1, monotone for every alpha. One pickup per tine, so this
 * runs per voice before summation — harmonic distortion, never IMD.
 * mean_sq is the mean of x^2 over a period, which the caller has for free
 * as half the sum of the squared modal amplitudes; subtracting it is what
 * keeps a decaying voice from dragging a DC thump behind it. */
static inline float ep_pickup(float x, float mean_sq) {
    float x2 = x * x;
    return x - 0.55f * (x2 - mean_sq) + (1.21f / 6.0f) * x2 * x;
}

/* The restrike law, decision D5: what a hammer blow does to a tine that is
 * already ringing. Both axes are audible and idiomatic on fast repeated
 * notes, and neither has an obvious single answer, so EP2 renders the four
 * combinations as an A/B and the loser gets deleted. The defaults are the
 * provisional law EP1 ran, so every EP1 render stays pinned. */
enum {
    EP_AMP_REPLACE = 0, /* the blow sets the amplitude   */
    EP_AMP_ADD,         /* the blow adds to what is there */
};
enum {
    EP_PHASE_CONTINUE = 0, /* the tine keeps moving through the blow */
    EP_PHASE_RESET,        /* the blow restarts it                   */
};

/* The struck-voice bank: 73 keys x 3 modes, fixed state, no allocator and
 * no voice stealing — the resonators exist physically and their state is
 * theirs. Mode-major so each bank is a 73-float run (sec 9). */
typedef struct {
    float phase[EP_MODES][EP_KEYS]; /* [0, 1) turns                      */
    float step[EP_MODES][EP_KEYS];  /* 0 for a Nyquist-muted mode        */
    float amp[EP_MODES][EP_KEYS];   /* >= 0; exactly 0 means silent      */
    float dec[EP_MODES][EP_KEYS];   /* the live decrement: free or damped */
    float dec_free[EP_MODES][EP_KEYS];
    float dec_damp[EP_MODES][EP_KEYS];
    float gate[EP_MODES][EP_KEYS];  /* 1 or 0: the Nyquist rule, sec 3.1 */
    float ceiling[EP_MODES][EP_KEYS]; /* the hardest single blow, sec 5.4 */
    float f1[EP_KEYS];              /* fundamentals in Hz                */
    uint8_t amp_law, phase_law;     /* D5; write via ep_bank_set_restrike */
    /* active-gated bookkeeping (sec 9): keys with any live mode, kept
     * ascending so both tick layouts sum in the same order. */
    uint8_t live[EP_KEYS];
    uint8_t live_n;
    bool relist;
    uint32_t out_of_compass;
} ep_bank;

void ep_bank_init(ep_bank *b, float sample_rate_hz);

/* A note-on. Notes outside 28..100 are ignored and counted; velocity
 * clamps to 1..127. Velocity scales loudness (sec 5.1) and timbre
 * (sec 5.2) — the first time in this codebase. A mode standing at exactly
 * zero has its phase reset, a ringing one keeps it (sec 5.3); the restrike
 * law itself is decision D5 and closes at EP2. */
void ep_bank_strike(ep_bank *b, int midi_note, int velocity);

/* The restrike law, D5. Hostile values clamp to the defaults. */
void ep_bank_set_restrike(ep_bank *b, int amp_law, int phase_law);

/* Damper on and off for one note (sec 8). A strike lifts the damper by
 * itself — the hammer does it through the bridle strap — so ep_bank_damp
 * is what a release calls, and only when the pedal is up. Notes outside
 * the compass are ignored without counting: the caller that resolved the
 * note has already counted it. */
void ep_bank_damp(ep_bank *b, int midi_note);
void ep_bank_undamp(ep_bank *b, int midi_note);

/* Everything to exact silence, phases included — a hard mute, not a
 * damper. The instrument's panic drops dampers instead (ep_piano_panic). */
void ep_bank_silence(ep_bank *b);

/* One sample, always-advance: every mode of every key ticks, amplitudes
 * zero when silent. Constant cost, branch-free (sec 9). */
float ep_bank_tick(ep_bank *b);

/* One sample, active-gated: keys with no live mode are skipped entirely
 * (sec 9). Bit-identical to ep_bank_tick by construction — see sec 5.3 —
 * so decision D4 rests on cost alone. D4 closed on this one; ep_bank_tick
 * stays as the reference the identity test asserts against. */
float ep_bank_tick_gated(ep_bank *b);

/* --- piano: keys, dampers and the sustain pedal over the bank --- */

/* A damper is off when the key is held or the pedal is down, and on
 * otherwise (sec 8). That one rule covers catching the pedal late under a
 * decaying note, releasing a key with the pedal already down, and letting
 * go of the pedal while keys are still held. */
typedef struct {
    ep_bank bank;
    bool held[EP_KEYS];
    bool sustain;
    uint32_t pressure; /* poly key pressure: parsed, ignored, counted */
} ep_piano;

void ep_piano_init(ep_piano *p, float sample_rate_hz);

/* Notes outside 28..100 are ignored and counted in bank.out_of_compass.
 * down with velocity 0 is a note-off, per the MIDI convention. */
void ep_piano_note(ep_piano *p, int midi_note, bool down, int velocity);

/* CC64, sec 10.1. Down defers damper engagement; up applies the damper to
 * every key that is not held. */
void ep_piano_set_sustain(ep_piano *p, bool down);

/* Poly key pressure has no meaning on this instrument — a key that is down
 * has already thrown its hammer. Counted so the driver can report it. */
void ep_piano_key_pressure(ep_piano *p, int midi_note, int value);

/* CC120/CC123: drops all dampers and lets go of the pedal, so the
 * instrument silences in the damper tau rather than by a hard mute. */
void ep_piano_panic(ep_piano *p);

float ep_piano_tick(ep_piano *p);

#endif
