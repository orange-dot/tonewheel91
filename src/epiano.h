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

/* The struck-voice bank: 73 keys x 3 modes, fixed state, no allocator and
 * no voice stealing — the resonators exist physically and their state is
 * theirs. Mode-major so each bank is a 73-float run (sec 9). */
typedef struct {
    float phase[EP_MODES][EP_KEYS]; /* [0, 1) turns                      */
    float step[EP_MODES][EP_KEYS];  /* 0 for a Nyquist-muted mode        */
    float amp[EP_MODES][EP_KEYS];   /* >= 0; exactly 0 means silent      */
    float dec[EP_MODES][EP_KEYS];   /* free-decay decrement per sample   */
    float gate[EP_MODES][EP_KEYS];  /* 1 or 0: the Nyquist rule, sec 3.1 */
    float f1[EP_KEYS];              /* fundamentals in Hz                */
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

/* Everything to exact silence, phases included. There is no damper here —
 * dampers and the sustain pedal land at EP2. */
void ep_bank_silence(ep_bank *b);

/* One sample, always-advance: every mode of every key ticks, amplitudes
 * zero when silent. Constant cost, branch-free (sec 9). */
float ep_bank_tick(ep_bank *b);

/* One sample, active-gated: keys with no live mode are skipped entirely
 * (sec 9). Bit-identical to ep_bank_tick by construction — see sec 5.3 —
 * so decision D4 rests on cost alone. */
float ep_bank_tick_gated(ep_bank *b);

#endif
