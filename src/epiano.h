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

/* Contact-transient level and decay per hammer-hardness zone (sec 5.5). */
extern const float EP_HAMMER_LEVEL[EP_ZONES];
extern const float EP_HAMMER_MS[EP_ZONES];
extern const float EP_HAMMER_HZ[EP_ZONES];

/* The tine's swing at the reference note in units of the pickup gap, how
 * fast that grows toward the bass, and where the tine rests in the field
 * (sec 6.1). All three are derived; none is a trim. */
extern const float EP_PICKUP_DRIVE_REF;
extern const float EP_PICKUP_SLOPE;
extern const float EP_PICKUP_OFFSET;

/* The by-ear per-mode trim, the contact time per hammer-hardness zone, and
 * the roll-off corner it implies at velocity 127 (sec 5.2). */
extern const float EP_BASE_W[EP_MODES];
extern const float EP_CONTACT_MS[EP_ZONES];
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

/* How strongly a blow at the striking line excites (key, mode), relative to
 * mode 1 (sec 5.2): the clamped-free mode shape at the strike point, times
 * the shape at the tip the pickup reads. Out-of-range arguments return 0;
 * mode 0 is 1 by construction. */
[[nodiscard]] float ep_mode_shape(int key, int mode);

/* The velocity-to-spectrum weight of (key, mode) at a velocity 1..127
 * (sec 5.2), normalised so mode 0 is always EP_BASE_W[0] — velocity moves
 * loudness through sec 5.1 and timbre through here, and neither leaks into
 * the other. Hostile arguments clamp; out-of-range key or mode returns 0.
 * This is the function the bank strikes with, not a restatement of it. */
[[nodiscard]] float ep_mode_weight(int key, int mode, int velocity);

/* How far a tine swings across its own pickup's field, per key, in units
 * of that pickup's gap (sec 6.1). Swing for a given blow goes as 1/f,
 * which alone would give slope 1; the gap is itself wider in the bass than
 * in the middle and upper ranges [EP-SM 4-8], and that pulls the exponent
 * under 1; 1/4 is the pinned value. Uncapped: the law's maximum is at the
 * bottom of the compass and the strike level is bounded, so the swing is
 * bounded with them. Out-of-range key returns 0. */
[[nodiscard]] float ep_pickup_drive(int key);

/* sec 6: the pickup's field, in units of its own gap. The tine tip sweeps
 * laterally across the pole face [EP-DAFx17 eq 8] and the field over that
 * face reduces, for a point charge, to the inverse-cube law of [EP-DAFx17
 * eq 6], so the flux the coil links goes as (1 + u^2)^(-3/2) with u the
 * tine's lateral offset in gap units.
 *
 * A bell, not a saturator. Two things follow, and both are the point:
 * it has no rail anywhere — it falls as u^-3 and never flattens — and it
 * is what makes the patent's off-centre claim mean something, since at
 * u = 0 the curve is even and passes no fundamental at all. The rest
 * offset is therefore the fundamental-to-overtone control [EP-P61] says
 * it is, not a bias into a monotone curve.
 *
 * No libm, so the inverse square root is exponent halving and negation
 * seeded (~8.9 %) plus three Newton steps, which reaches 7.3e-8 relative
 * over the whole range u takes — f32-exact, and the test checks it
 * against the transcendental. y >= 1 always, so there is no zero, no
 * denormal and no negative case. */
static inline float ep_field(float u) {
    float y = 1.0f + u * u;
    union { float f; uint32_t i; } v = { y };
    v.i = 0x5f400000u - (v.i >> 1);
    float r = v.f;
    r *= 1.5f - 0.5f * y * r * r;
    r *= 1.5f - 0.5f * y * r * r;
    r *= 1.5f - 0.5f * y * r * r;
    return r * r * r;
}

/* Pickup, sec 6: the tine sitting off-centre in that field. One pickup per
 * tine, so this runs per voice before summation — harmonic distortion,
 * never IMD.
 *
 * `g` is the swing per unit strike in gap units (sec 6.1), `x0` the rest
 * offset, and `ref` is ep_field(x0), which the bank precomputes so the
 * curve passes through zero. Subtracted that way round so a positive
 * displacement gives a positive output: the flux falls as the tine leaves
 * the pole.
 *
 * Not monotone, and that is the physics — past dead centre the tine is
 * on the far side of the field and the flux falls again. What replaces
 * monotonicity as the safety property is that the field is bounded,
 * smooth and rail-free, so no operating point can flatten the envelope. */
static inline float ep_pickup(float x, float g, float x0, float ref) {
    return ref - ep_field(g * x + x0);
}

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
    float pk_g[EP_KEYS], pk_x0[EP_KEYS], pk_ref[EP_KEYS]; /* sec 6.1     */
    float dc_lp, dc_c;              /* coupling-cap highpass, sec 6.2     */
    /* condition, sec 15: the per-note deviations behind one knob. Every
     * one is exactly neutral at condition 0, so the idealized instrument
     * is reproduced bit for bit. */
    float cond;
    float trim[EP_MODES][EP_KEYS];  /* per-note voicing, exactly 1 at 0  */
    /* sec 16: the horizontal polarisation. The tine is a round wire and
     * vibrates in two planes; the design suppresses the one the hammer
     * does not drive, and real instruments keep a little of it anyway.
     * Exactly absent at condition 0. */
    float h_phase[EP_KEYS], h_step[EP_KEYS], h_amp[EP_KEYS];
    float h_depth[EP_KEYS];
    float floor_gain, hum_gain;     /* exactly 0 at condition 0          */
    float hum_phase, hum_step;
    uint64_t floor_rng;
    /* contact transient, sec 5.5: a short shaped burst per voice. The
     * seed is reset from the note and the strike ordinal at every blow,
     * so the two tick layouts cannot drift apart even though only one of
     * them advances a silent voice. */
    float hn_amp[EP_KEYS], hn_dec[EP_KEYS];
    float hn_lp[EP_KEYS], hn_c[EP_KEYS];
    uint64_t hn_rng[EP_KEYS];
    uint32_t strikes;
    float rate;
    /* active-gated bookkeeping (sec 9): keys with any live mode, kept
     * ascending so both tick layouts sum in the same order. */
    uint8_t live[EP_KEYS];
    uint8_t live_n;
    bool relist;
    uint32_t out_of_compass;
} ep_bank;

void ep_bank_init(ep_bank *b, float sample_rate_hz);

/* The shipped condition, nonzero because tolerance effects exist on a
 * factory-new instrument (sec 15, the organ's own wear doctrine). */
extern const float EP_CONDITION_DEFAULT;

/* The one condition knob, 0..1 (sec 15). Rebuilds every per-note deviation
 * from fixed-seed draws: tuning spread, clang-ratio spread, voicing
 * spread, pickup-distance spread, and the noise and hum floors. 0 restores
 * the idealized instrument exactly, even mid-note, so every pre-EP7 render
 * stays pinned. Hostile values sanitize. Phase and amplitude state are
 * untouched. */
void ep_bank_set_condition(ep_bank *b, float v);

/* A note-on. Notes outside 28..100 are ignored and counted; velocity
 * clamps to 1..127. Velocity scales loudness (sec 5.1) and timbre
 * (sec 5.2) — the first time in this codebase. A mode standing at exactly
 * zero has its phase reset; a ringing one keeps its phase and the blow
 * adds to it, bounded by the hardest single blow (sec 5.3, 5.4). */
void ep_bank_strike(ep_bank *b, int midi_note, int velocity);

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

/* --- tremolo: one LFO and a gain law (sec 12) --- */

/* Two variants behind one control, as the backlog asks: the mono amplitude
 * wobble, and the stereo opposing pan of the cabinet-equipped instrument.
 * Stereo begins here; everything before it is mono. */
enum {
    EP_TREM_AM = 0, /* both channels together      */
    EP_TREM_PAN,    /* channels in opposition      */
};

typedef struct {
    float phase, step; /* [0, 1) turns                                   */
    float depth;       /* 0 = bit-exact mono bypass; write via the setter */
    int mode;
} ep_tremolo;

void ep_tremolo_init(ep_tremolo *t, float sample_rate_hz);

/* Rate in Hz, clamped to the sec 12 range; hostile values sanitize. */
void ep_tremolo_set_rate(ep_tremolo *t, float sample_rate_hz, float hz);

/* Depth 0..1. Exactly 0 makes tick return { x, x } bit-identically, so
 * every pre-EP4 render stays pinned (the scanner-OFF discipline). */
void ep_tremolo_set_depth(ep_tremolo *t, float d);

/* EP_TREM_*; hostile ints clamp. */
void ep_tremolo_set_mode(ep_tremolo *t, int mode);

/* One control covering both variants, the way the organ's CC84 covers off
 * and six vibrato positions: 0 is off, 1..63 is the mono wobble with
 * rising depth, 64..127 is the stereo pan with rising depth (sec 10.1). */
void ep_tremolo_set_cc(ep_tremolo *t, int value);

tw_stereo ep_tremolo_tick(ep_tremolo *t, float x);

/* --- drive: the preamp stage (sec 13) --- */

/* The EP feeds tw_drive at twice its bus level, because tw_drive's internal
 * reference is the organ's and this instrument runs lower. Two is chosen
 * over any nearer number because it is a power of two: the scale in and
 * out is then exact in f32, so drive = 0 stays a bit-identical bypass. */
extern const float EP_DRIVE_SCALE;

/* --- cabinet: the loudspeaker's bandwidth (sec 14) --- */

/* A box and a cone, and nothing else. The rolloffs are what any
 * loudspeaker imposes and the model had none; the early-reflection set the
 * backlog also names is deliberately not here — see sec 14 for why. */
extern const float EP_CAB_LOW_HZ;
extern const float EP_CAB_HIGH_HZ;

typedef struct {
    float lp1[2], lp2[2]; /* cascaded one-poles: the cone's top end     */
    float hp[2];          /* the box's bottom end                       */
    float c_hi, c_lo;     /* per-rate coefficients, computed at init    */
    float mix;            /* 0 = bit-exact bypass; write via the setter */
} ep_cabinet;

void ep_cabinet_init(ep_cabinet *c, float sample_rate_hz);

/* 0..1, where 0 is a bit-exact bypass and 1 is the modelled speaker.
 * Between them it is a blend, which is what a "how much box" knob is.
 * Hostile values sanitize. */
void ep_cabinet_set(ep_cabinet *c, float v);

tw_stereo ep_cabinet_tick(ep_cabinet *c, tw_stereo x);

/* --- piano: keys, dampers and the sustain pedal over the bank --- */

/* A damper is off when the key is held or the pedal is down, and on
 * otherwise (sec 8). That one rule covers catching the pedal late under a
 * decaying note, releasing a key with the pedal already down, and letting
 * go of the pedal while keys are still held. */
typedef struct {
    ep_bank bank;
    tw_drive drive;
    ep_tremolo trem;
    ep_cabinet cab;
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

/* The mono chain: bank -> drive. At drive 0 this is bit-identical to the
 * pre-EP5 chain, so every earlier render stays pinned. */
float ep_piano_tick(ep_piano *p);

/* The mono chain, then the tremolo, then the cabinet. With both of those
 * at zero — the defaults — this duplicates ep_piano_tick onto both
 * channels bit-identically. */
tw_stereo ep_piano_tick_stereo(ep_piano *p);

/* CC91, sec 10.1. */
void ep_piano_set_tremolo(ep_piano *p, int cc_value);

/* The drive knob, 0..1 (sec 13). 0 is an exact bypass: the whole
 * instrument renders bit-identically to its pre-EP5 self. CC85, the same
 * number the organ uses, so one controller drives both. */
void ep_piano_set_drive(ep_piano *p, float v);

/* CC92, sec 10.1. 0 is bypass. */
void ep_piano_set_cabinet(ep_piano *p, float v);

/* CC93, sec 10.1 and 15. The instrument initialises to
 * EP_CONDITION_DEFAULT, not to 0. */
void ep_piano_set_condition(ep_piano *p, float v);

#endif
