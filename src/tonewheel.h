/* tonewheel91 core — freestanding C23: no OS, no libm, no allocation.
 * Constants and their sources: docs/constants.md. */
#ifndef TONEWHEEL_H
#define TONEWHEEL_H

#include <float.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>

static_assert(CHAR_BIT == 8);
static_assert(sizeof(float) == 4);
static_assert(FLT_RADIX == 2 && FLT_MANT_DIG == 24);
static_assert(FLT_MIN_EXP == -125 && FLT_MAX_EXP == 128);

/* Public stateful APIs require initialized, non-null state pointers. Array
 * parameters require the extent written in their declaration. Setters
 * sanitize documented numeric inputs; tick paths assume state produced by
 * the corresponding initializer and finite signal inputs. */

/* One clock contract for every core component. Invalid and unsupported
 * values, including NaN and infinities, select the 48 kHz default. */
static inline float tw_sample_rate_hz(float sample_rate_hz) {
    return sample_rate_hz >= 44100.0f && sample_rate_hz <= 192000.0f
         ? sample_rate_hz : 48000.0f;
}

/* 1 - exp(-x), used by every core one-pole. The fourth-order alternating
 * expansion is accurate over the supported x <= 0.25 interval. */
static inline float tw_one_pole_coeff(float x) {
    if (!(x > 0.0f) || x > 0.25f) x = 0.25f;
    return x * (1.0f - x * (0.5f - x * (1.0f / 6.0f - x / 24.0f)));
}

enum {
    TW_WHEELS    = 91,
    TW_DRAWBARS  = 9,
    TW_KEYS      = 61,
    TW_WHEEL_MIN = 13, /* manual foldback floor   */
    TW_WHEEL_MAX = 91, /* manual foldback ceiling */
};

/* Drawbar semitone offsets, leftmost (16') first. constants.md section 4. */
extern const int8_t TW_DRAWBAR_OFFSET[TW_DRAWBARS];

/* Drawbar digit 0..8 -> amplitude: transformer taps / 64. constants.md 5. */
extern const float TW_DRAWBAR_GAIN[9];

/* Wheel frequency in Hz at the nominal 20 rev/s shaft; wheel 1..91.
 * Out-of-range wheel returns 0. */
[[nodiscard]] float tw_wheel_freq_hz(int wheel);

/* Busbar tap for (key 1..61, drawbar 0..8), octave-folded into [13, 91].
 * Total: hostile arguments clamp into domain. constants.md section 4. */
[[nodiscard]] int tw_wheel_index(int key, int drawbar);

typedef struct {
    float phase[TW_WHEELS]; /* [0, 1) */
    float step[TW_WHEELS];
    float keyed_gain[TW_WHEELS];
    float keyed_target[TW_WHEELS];
    float perc_gain[TW_WHEELS];
    float perc_target[TW_WHEELS];
    float leak_gain[TW_WHEELS]; /* static bleed-bus weights, sec 13; all
                                 * zero at wear 0 */
    float level[TW_WHEELS];     /* per-wheel level profile: 1 + wear x
                                 * (spread + zone trim), sec 11; exactly
                                 * 1.0 at wear 0 */
    float t2[TW_WHEELS];        /* tooth-profile 2nd/3rd partial depths,
                                 * sec 12: wear x base x 4/teeth; exact
                                 * harmonics of the wheel's own phase, so
                                 * per-wheel and IMD-free; 0 at wear 0 */
    float t3[TW_WHEELS];
    float rev_phase[TW_WHEELS]; /* shaft-rotation phase [0, 1) at
                                 * f_wheel/teeth, sec 12 motion AM;
                                 * random start per wheel, advances
                                 * always (wear-independent state) */
    float rev_step[TW_WHEELS];
    float am_g[TW_WHEELS];      /* motion-AM depth, wear x max x draw;
                                 * exactly 0 at wear 0 */
    float pk2, pk3;             /* pickup nonlinearity, sec 12: cubic
                                 * series of (1 - exp(-alpha x))/alpha
                                 * at alpha = wear x 0.3; exactly 0 at
                                 * wear 0 */
    float hum_phase, hum_step;  /* mains hum on the bleed bus, sec 1:
                                 * 60 Hz; phase advances always */
    float hum_gain;             /* wear x level; exactly 0 at wear 0 */
    float wear;                 /* 0 = idealized reference (bit-identical
                                 * to pre-M7); write via the setter */
    float smooth;               /* one-pole coefficient toward gain targets */
    float perc_smooth;          /* separate one-pole coefficient: perc_target's
                                  * own decay toward 0 (tau fast/slow, sec 8);
                                  * keyed's smooth above is untouched by this */
} tw_generator;

typedef struct {
    float keyed;
    float percussion;
    float leak;
    float keyed_low; /* running keyed sum captured after wheel 16: the
                      * sub-80 Hz fundamentals that bypass the scanner
                      * line (sec 9 bass bypass). Always a prefix of the
                      * same summation, so keyed itself is untouched. */
} tw_frame;

/* tau_s is the contact-gain smoothing time constant (the click shaper),
 * supported down to ~0.1 ms; sample rates 44.1-192 kHz. */
void tw_generator_init(tw_generator *g, float sample_rate_hz, float tau_s);

/* Atomically replace a whole gain-target bank. Non-finite entries become
 * 0; values clamp to [0, 16]. */
void tw_generator_set_keyed_targets(tw_generator *g, const float t[TW_WHEELS]);
void tw_generator_set_perc_targets(tw_generator *g, const float t[TW_WHEELS]);

/* Sets perc_smooth from a decay time constant (tau fast/slow, sec 8); call
 * whenever the percussion DECAY switch changes. Independent of tau_s above,
 * which shapes keyed_gain's click only. */
void tw_generator_set_perc_tau(tw_generator *g, float sample_rate_hz, float tau_s);

/* The wear knob at the generator: rebuilds every deviation bank (level
 * profile; toothing, motion AM, pickup alpha, leakage, hum as their
 * slices land — constants.md sec 11-13) from the fixed-seed per-wheel
 * character draws, scaled by wear in [0, 1]. Hostile values sanitize
 * (NaN/negative -> 0, cap 1). wear = 0 restores the idealized reference
 * exactly: every bank returns to its bit-exact pre-M7 value, so a
 * mid-render return to 0 rejoins the never-worn render (the scanner-OFF
 * discipline). Phase state is untouched. */
void tw_generator_set_wear(tw_generator *g, float wear);

/* Advance all 91 wheels one sample. Constant cost regardless of targets:
 * behavior differences are gain-gated, never branch-gated. */
tw_frame tw_generator_tick(tw_generator *g);

/* --- vibrato/chorus scanner: dispersive line box + rotary sweep --- */

enum {
    TW_SCAN_SECTIONS   = 18, /* LC lowpass sections, constants.md sec 9  */
    TW_SCAN_NODES      = 19, /* tap nodes v1..v19: input + one/section   */
    TW_SCAN_LOW_WHEELS = 16, /* wheels 1..16: fundamentals below the
                              * sec 9 ~80 Hz line (wheel 16 = 77.8 Hz,
                              * wheel 17 = 82.4 Hz) bypass the line      */
};

/* The vibrato knob: off models the per-manual tablet (the real switch
 * has no off position, sec 9). V modes are swept-only, C modes mix dry
 * with the swept line. */
enum {
    TW_VIB_OFF = 0,
    TW_VIB_V1, TW_VIB_V2, TW_VIB_V3,
    TW_VIB_C1, TW_VIB_C2, TW_VIB_C3,
};

typedef struct {
    /* line state: trapezoidal (bilinear) nodal model of the 18-section
     * LC ladder, sec 9 [DAFx16]. vn[0] is the input node v1. */
    float vn[TW_SCAN_NODES];
    float il[TW_SCAN_SECTIONS];
    /* constant per-rate factors, computed at init (no libm) */
    float a;                       /* trapezoidal 1/(2 L fs)          */
    float gt;                      /* 1/Rt termination conductance    */
    float bc[TW_SCAN_SECTIONS];    /* 2 C fs per shunt cap            */
    float inv_m[TW_SCAN_SECTIONS]; /* tridiagonal solve: 1/pivots     */
    float fw[TW_SCAN_SECTIONS];    /* tridiagonal solve: multipliers  */
    /* rotor */
    float phase; /* [0, 1) turns; one turn = one there-and-back sweep */
    float step;
    int mode;    /* TW_VIB_*; write via tw_organ_set_vibrato          */
} tw_scanner;

void tw_scanner_init(tw_scanner *s, float sample_rate_hz);

/* Advance the rotor and the line one sample. low is the bass-bypass
 * branch (added straight through), high feeds the line. Modes outside
 * V1..C3 (including OFF) return low + high and leave the line untouched;
 * the organ never routes through here while OFF, so pre-M4 renders stay
 * bit-identical. */
float tw_scanner_tick(tw_scanner *s, float low, float high);

/* --- MIDI byte parser: bytes in, channel messages out --- */

typedef struct {
    uint8_t status; /* e.g. 0x90 | channel */
    uint8_t d1, d2;
} tw_midi_msg;

typedef struct {
    uint8_t status; /* running status, 0 = none */
    uint8_t data[2];
    uint8_t have, need;
} tw_midi_parser; /* zero-initialize */

/* Feed one byte; returns 1 when a complete channel message lands in out.
 * Real-time bytes (>= 0xF8) are transparent; system common and SysEx
 * cancel running status and are otherwise ignored. */
int tw_midi_parse(tw_midi_parser *p, uint8_t byte, tw_midi_msg *out);

/* --- organ: key contacts, busbars, swell over the generator --- */

enum {
    TW_NOTE_MIN  = 36, /* 61-key compass, MIDI 36..96 */
    TW_NOTE_MAX  = 96,
    TW_KEY_EVMAX = 64, /* 9 contacts x (1 + 2*3 bounce toggles) = 63 */
    TW_TAP_MAX   = 3,  /* most keys of one bus folding onto one wheel */
};

typedef struct {
    int64_t frame;
    uint8_t bus;
    uint8_t closed;
} tw_contact_ev;

/* Four independent tablets, not one enum (sec 8): on/off, harmonic
 * (false=2nd/4' tap, true=3rd/2-2/3' tap), decay speed (false=fast,
 * true=slow), volume (false=soft, true=normal). The rest is the trigger
 * state machine, not user controls. It follows the 1' contacts, not the
 * keys: that bus is the sensing line, so closing any key's ninth contact
 * grounds terminal K and releases the envelope, and the grid recovers
 * only once every one of them is open again. armed = the grid has
 * recovered; wheel = the last-tapped wheel, 0 = none; sense_n = how many
 * ninth contacts are closed; rearm_at = the frame recovery completes,
 * 0 = not recovering. */
typedef struct {
    bool on, third, slow, normal;
    bool armed;
    int wheel;
    uint8_t sense_n;
    int64_t rearm_at;
} tw_percussion;

typedef struct {
    tw_generator gen;
    int64_t now;
    uint64_t rng; /* fixed-seed; advanced only at note and depth events */
    float rate;
    bool held[TW_KEYS];
    uint8_t made[TW_KEYS];   /* contacts the key travel commands, 0..9
                              * (sec 7.1); a held key stands at 9 until a
                              * depth message moves it. contact[][] below
                              * is where they actually are, bounce and
                              * stagger included. */
    bool contact[TW_KEYS][TW_DRAWBARS];
    tw_percussion perc;
    tw_scanner scan;
    /* Keys (1..61) of each bus that can tap a wheel, 0-terminated; filled
     * once at init from the foldback rule. Which of them are closed is
     * contact[][]; taper makes colliding wires unequal, so a bare
     * multiplicity count cannot describe the state. */
    uint8_t taps[TW_DRAWBARS][TW_WHEELS + 1][TW_TAP_MAX];
    uint8_t registration[TW_DRAWBARS]; /* digits 0..8 */
    float swell_target, swell_gain, swell_coeff;
    tw_contact_ev pend[TW_KEYS][TW_KEY_EVMAX];
    uint8_t pend_n[TW_KEYS], pend_i[TW_KEYS];
    uint32_t out_of_compass;
} tw_organ;

void tw_organ_init(tw_organ *o, float sample_rate_hz);

/* Notes outside 36..96 are ignored and counted. Velocity never scales
 * loudness (contact closure is binary); it maps to contact stagger:
 * vel 127 ~ 0 ms, vel 1 ~ 15 ms across the nine buses. down with
 * velocity 0 is note-off. */
void tw_organ_note(tw_organ *o, int midi_note, bool down, int velocity);

/* How far one held key is down, 0..127 over the travel (section 7.1):
 * the position decides how many of that key's nine contacts are made,
 * in bus order 0..8 — the same spring-stack order the velocity stagger
 * walks. The nine make points are evenly spaced and carry a
 * break-below-make band, so a finger parked on one cannot chatter its
 * contact. Depth 0 is a held key with every contact open, not a
 * release: note-off stays authoritative, and a key that is not held
 * ignores depth entirely (out-of-compass notes still count). A key
 * presses full-depth on note-on, so an instrument that never sends
 * depth behaves exactly as before. */
void tw_organ_note_depth(tw_organ *o, int midi_note, int depth);

void tw_organ_set_registration(tw_organ *o, const uint8_t digits[TW_DRAWBARS]);
void tw_organ_set_drawbar(tw_organ *o, int drawbar, int digit);

/* Percussion tablets, all four at once (sec 8). Hostile int arguments
 * clamp to their two-valued domain via the bool parameter conversion. */
void tw_organ_set_percussion(tw_organ *o, bool on, bool third, bool slow, bool normal);

/* Vibrato knob, TW_VIB_*; hostile ints clamp to [OFF, C3]. OFF keeps the
 * render bit-identical to a scannerless organ (sec 9). */
void tw_organ_set_vibrato(tw_organ *o, int mode);

/* Swell pedal position 0..1; the audio-taper curve lives inside. */
void tw_organ_set_swell(tw_organ *o, float v);

/* The shipped wear default (design.md: nonzero, because tolerance
 * effects exist on a factory-new unit). constants.md sec 12.1 [FOLK]. */
extern const float TW_WEAR_DEFAULT;

/* The one wear knob, 0..1 (design.md control surface): scales every
 * sec 11-13 deviation — level spread, toothing, motion AM, pickup
 * alpha, leakage, hum. 0 restores the idealized pre-M7 render
 * bit-exactly, even mid-note (the scanner-OFF discipline); the organ
 * initializes to TW_WEAR_DEFAULT. Hostile values sanitize. */
void tw_organ_set_wear(tw_organ *o, float v);

/* Electrical all-off: clears pending contact events and opens every
 * contact immediately (no bounce); gains glide out in the click tau. */
void tw_organ_panic(tw_organ *o);

/* Process due contact events, advance the generator, apply swell. */
float tw_organ_tick(tw_organ *o);

/* --- drive: the stateful preamp bias stage (sec 14.1) --- */

/* One knob (drive 0..1). drive = 0 is an exact bypass: tick returns its
 * input bit-identically and leaves all state untouched (the scanner-OFF
 * discipline), so pre-M5 renders stay stable. */
typedef struct {
    float env;   /* bias-excursion follower state, >= 0            */
    float hp_lp; /* coupling-cap highpass: DC-image tracker state  */
    /* constant per-rate factors, computed at init (no libm) */
    float atk_c, rel_c; /* follower one-pole coefficients          */
    float hp_c;         /* highpass tracker coefficient            */
    /* kernel select, set at init / tw_drive_set_kernel */
    float depth; /* operating-point shift per unit env             */
    bool odd;    /* true: the M5 odd tw_sat kernel (rotary amp)    */
    /* control; write via tw_drive_set */
    float drive; /* 0..1                                           */
    float pre;   /* pregain / X_ref = (1 + 7 drive^2) / 8          */
    float post;  /* X_ref / pregain: unity small-signal through-gain */
} tw_drive;

void tw_drive_init(tw_drive *d, float sample_rate_hz);

/* Drive knob 0..1; hostile values sanitize (NaN/negative -> 0, cap 1). */
void tw_drive_set(tw_drive *d, float v);

/* Kernel select. Default (odd = false) is the derived triode curve —
 * the preamp's warmth voice. odd = true keeps the M5 odd kernel with
 * its 0.5 bias depth: the rotary's 40 W ceiling, where a power stage
 * wants a hard bound, not preamp warmth (docs/warmth-evidence.md). */
void tw_drive_set_kernel(tw_drive *d, bool odd);

/* One sample: pregain -> bias-shifted saturator -> coupling-cap
 * highpass -> makeup. constants.md section 14.1. */
float tw_drive_tick(tw_drive *d, float x);

/* The drive kernel [derived, warmth pass]: normalized static transfer
 * of the circuit-true triode reference, monotone C1 Hermite table over
 * [-8, 8]; exact 0 and unit slope at 0, flat rails at -1.831 / +3.722.
 * Exposed for tests and the warmth harness (docs/warmth-evidence.md).
 * The odd tanh-shaped tw_sat below survives as the M5-pinned kernel
 * (exhibit A/B twin and non-drive uses). */
float tw_drive_curve(float x);

/* --- rotary speaker: crossover, two rotors, Doppler, AM, stereo
 * (constants.md section 15) --- */

/* Rotor states, sec 15 [LB]: Fast (tremolo), Slow (chorale), Off
 * (brake, parks facing forward). Bypass models the rotary cabinet
 * absent entirely (the stationary tone cabinet of the console sources);
 * it is the M6 identity default. */
enum {
    TW_ROT_BYPASS = 0,
    TW_ROT_CHORALE,
    TW_ROT_TREMOLO,
    TW_ROT_BRAKE,
};

enum {
    /* Horn delay capacity: (base 1.0 + swing amp 0.3) ms at 192 kHz is
     * 250 samples; power of two for mask wrap. constants.md sec 15.1. */
    TW_ROTARY_DMAX = 512,
};

typedef struct {
    float l, r;
} tw_stereo;

typedef struct {
    /* crossover: TPT state-variable filter, 800 Hz Butterworth split
     * (sec 15 [HX]); lp = drum branch, hp = horn branch */
    float xo_ic1, xo_ic2; /* integrator states                        */
    float xo_lp, xo_hp;   /* last split pair (test/exhibit taps)      */
    float xo_a1, xo_a2, xo_a3; /* per-rate factors, computed at init  */
    /* drum AM floor: one-pole 200 Hz tracker inside the drum branch
     * (sec 15 [HX]: AM only over ~200-800 Hz) */
    float dm_lp, dm_c;
    /* rotors: phase in turns [0, 1), rate = target + dev in Hz; dev
     * decays multiplicatively per direction (up vs down time constants,
     * sec 15 [RS]/[FOLK]) — exact landing on target, no f32 stall */
    float horn_phase, horn_dev, horn_target;
    float drum_phase, drum_dev, drum_target;
    float horn_up_k, horn_dn_k, drum_up_k, drum_dn_k;
    float park_c;    /* brake: phase pull toward the front stop       */
    float inv_rate;  /* 1 / sample rate                               */
    /* horn Doppler: fractional delay line, linear read per mic       */
    float dline[TW_ROTARY_DMAX];
    int32_t widx;
    float d_base, d_amp; /* delay base and swing amplitude, samples   */
    /* the 40 W amp ceiling ahead of the rotors: the M5 stage reused
     * (sec 15 [LB]); its knob is the rotary drive control */
    tw_drive amp;
    /* control; write via the setters */
    int mode;      /* TW_ROT_*                                        */
    float balance; /* 0 = drum only .. 1 = horn only, 0.5 neutral     */
    float width;   /* 0 = mono .. 1 = full stereo field               */
} tw_rotary;

void tw_rotary_init(tw_rotary *r, float sample_rate_hz);

/* Mode TW_ROT_*; hostile ints clamp to [BYPASS, BRAKE]. BYPASS keeps
 * the render bit-identical to the mono pre-M6 chain and freezes all
 * rotary state (the scanner-OFF discipline). */
void tw_rotary_set_mode(tw_rotary *r, int mode);

/* Knobs 0..1; hostile values sanitize (NaN/negative -> 0, cap 1). */
void tw_rotary_set_balance(tw_rotary *r, float v);
void tw_rotary_set_width(tw_rotary *r, float v);
void tw_rotary_set_drive(tw_rotary *r, float v);

/* One sample: amp ceiling -> crossover -> horn FM+AM / drum AM (no FM)
 * -> two virtual mics -> balance/width. BYPASS returns { x, x } and
 * touches no state. */
tw_stereo tw_rotary_tick(tw_rotary *r, float x);

/* --- instrument: the top-level chain (sec 14.1 [decision]) --- */

/* organ -> drive -> rotary. tw_instrument_tick is the mono pre-rotary
 * chain (organ -> drive), unchanged since M5 so pre-M6 exhibits and
 * signatures stay bit-stable; tw_instrument_tick_stereo appends the
 * rotary, whose BYPASS default duplicates the mono chain onto both
 * channels bit-identically (design.md signal chain; swell lives inside
 * the organ, before drive, on purpose). */
typedef struct {
    tw_organ organ;
    tw_drive drive;
    tw_rotary rotary;
} tw_instrument;

static inline void tw_instrument_init(tw_instrument *ins, float sample_rate_hz) {
    sample_rate_hz = tw_sample_rate_hz(sample_rate_hz);
    tw_organ_init(&ins->organ, sample_rate_hz);
    tw_drive_init(&ins->drive, sample_rate_hz);
    tw_rotary_init(&ins->rotary, sample_rate_hz);
}

/* The one drive knob: 0 (the default) = the pre-M5 chain, bit-identical. */
static inline void tw_instrument_set_drive(tw_instrument *ins, float v) {
    tw_drive_set(&ins->drive, v);
}

static inline float tw_instrument_tick(tw_instrument *ins) {
    return tw_drive_tick(&ins->drive, tw_organ_tick(&ins->organ));
}

static inline tw_stereo tw_instrument_tick_stereo(tw_instrument *ins) {
    return tw_rotary_tick(&ins->rotary, tw_instrument_tick(ins));
}

/* --- freestanding-pure helpers (shared by core, drivers, tests) --- */

static inline float tw_fabsf(float x) {
    union { float f; uint32_t u; } v = { x };
    v.u &= 0x7fffffffu;
    return v.f;
}

/* sin(2*pi*p) for p in [0, 1): fold to |t| <= 1/4, odd Taylor to t^9.
 * Max abs error ~4e-6 at the quadrant edge (harmonics < -100 dB). */
static inline float tw_sin_turns(float p) {
    float t = (p < 0.25f) ? p : (p < 0.75f) ? 0.5f - p : p - 1.0f;
    float s = t * t;
    return t * (6.28318531f + s * (-41.3417022f + s * (81.6052493f
             + s * (-76.7058597f + s * 42.0586939f))));
}

/* tanh-shaped saturator: the odd rational x(27 + x^2)/(27 + 9 x^2) with
 * the input clamped to |x| <= 3, where it reaches +-1 tangentially (zero
 * slope: C1-continuous clamp). Monotone everywhere, unit slope at 0,
 * within ~0.024 of true tanh. constants.md section 14.1 [derived]. */
static inline float tw_sat(float x) {
    x = (x > 3.0f) ? 3.0f : (x < -3.0f) ? -3.0f : x;
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

/* splitmix64 — the repo's deterministic RNG (contact bounce, per-wheel
 * wear character). Fixed seeds per use site; see design.md determinism. */
static inline uint64_t tw_splitmix64(uint64_t *s) {
    uint64_t z = (*s += 0x9e3779b97f4a7c15u);
    z = (z ^ z >> 30) * 0xbf58476d1ce4e5b9u;
    z = (z ^ z >> 27) * 0x94d049bb133111ebu;
    return z ^ z >> 31;
}

/* FNV-1a 64 over raw bytes — the repo's determinism signature.
 * Pass h = 0 to start a fresh hash; pass a previous result to chain. */
static inline uint64_t tw_fnv1a64(const void *data, size_t n, uint64_t h) {
    const unsigned char *p = data;
    if (h == 0) h = 0xcbf29ce484222325u;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 0x100000001b3u;
    }
    return h;
}

#endif
