/* Step sequencer (docs/patchbench-plan.md §2.6, §5.5): one pattern per
 * channel, frame math as exact rationals.  The same code runs on the audio
 * thread and offline for the bounce; it allocates nothing and calls nothing. */
#ifndef PB_SEQ_H
#define PB_SEQ_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    PB_SEQ_STEPS = 32,       /* most steps in a pattern */
    PB_SEQ_SPAN = 25,        /* semitones of the grid: root - 12 .. root + 12 */
    PB_SEQ_HELD = 64,
    PB_SEQ_EVENTS = 96,      /* most events at one frame */
    PB_SEQ_BPM_MIN = 2000,   /* centi-BPM */
    PB_SEQ_BPM_MAX = 30000,
    PB_SEQ_SWING_MAX = 75,   /* percent of a step */
    PB_SEQ_OCTAVE_MAX = 4,
    PB_CHANNELS = 4,         /* sequencer channels, each with its own patch */
};

typedef struct {
    uint32_t mask;           /* bit i sounds root - 12 + i (+ 12 * octave) */
    uint8_t velocity;        /* 1..127 */
    uint8_t gate;            /* 1..100 percent of the step */
    int8_t octave;           /* -4..4: lets one grid reach the whole compass */
    bool tie;                /* hold into the next step; no retrigger there */
} pb_step;

typedef struct {
    uint32_t bpm;            /* centi-BPM, 2000..30000 */
    uint8_t steps;           /* 1..32 */
    uint8_t swing;           /* 0..75: odd steps start this share of a step late */
    uint8_t root;            /* 12..115 */
    pb_step step[PB_SEQ_STEPS];
} pb_pattern;

/* The channels of the sequencer.  They start together and share one tempo
 * and swing: every pattern carries the same bpm and swing, so the step
 * boundaries of all channels fall on the same frames.  Step counts may
 * differ. */
typedef struct {
    pb_pattern channel[PB_CHANNELS];
    bool mute[PB_CHANNELS];  /* the channel keeps time but starts no notes */
} pb_sequence;

typedef struct {
    uint32_t offset;         /* frames into the block (set by the caller) */
    uint8_t note, velocity;
    bool on;
} pb_seq_event;

typedef struct {
    uint32_t rate;
    uint64_t frame;              /* frames since play */
    uint64_t step;               /* absolute index of the next boundary */
    uint64_t origin_frame, origin_step;
    uint32_t origin_bpm;
    uint8_t origin_swing;
    uint64_t next_boundary;
    unsigned index;              /* pattern step played at the next boundary */
    unsigned sounding;           /* pattern step now sounding */
    unsigned held_count;
    uint8_t held_note[PB_SEQ_HELD];
    uint64_t held_off[PB_SEQ_HELD];   /* UINT64_MAX: tied into the next step */
    bool playing;
} pb_seq_run;

bool pb_pattern_valid(const pb_pattern *p);
/* A pattern of 16 steps without notes. */
pb_pattern pb_pattern_empty(uint8_t root, uint32_t bpm);
bool pb_pattern_has_notes(const pb_pattern *p);
/* Every pattern valid, one bpm and swing. */
bool pb_sequence_valid(const pb_sequence *s);
/* `p` on channel 1, the other channels without notes, on its tempo. */
pb_sequence pb_sequence_single(const pb_pattern *p);
/* Steps of one pass: the longest pattern among the channels that sound
 * (notes, not muted); channel 1's when none does. */
unsigned pb_sequence_steps(const pb_sequence *s);
/* MIDI note of grid row `row` (0..24) at a step, or -1 outside 0..127. */
int pb_step_note(const pb_pattern *p, unsigned step, unsigned row);

/* Start frame of absolute step k for the run's current tempo origin. */
uint64_t pb_seq_step_frame(const pb_seq_run *r, uint64_t k);

void pb_seq_start(pb_seq_run *r, const pb_pattern *p, uint32_t rate);
/* Frames until the next event, at most `limit`; 0 when one is due now. */
uint32_t pb_seq_until(const pb_seq_run *r, uint32_t limit);
bool pb_seq_at_boundary(const pb_seq_run *r);
/* Adopt a new pattern at a boundary: tempo and swing changes re-origin the
 * frame math there, so no step moves backwards. */
void pb_seq_adopt(pb_seq_run *r, const pb_pattern *next);
/* All events due at the current frame, offs before ons except that a tie's
 * note-offs follow the next step's note-ons (legato, so glide is heard). */
size_t pb_seq_fire(pb_seq_run *r, const pb_pattern *p, pb_seq_event *out, size_t max);
void pb_seq_skip(pb_seq_run *r, uint32_t frames);
/* Note-offs for everything held; the run stops. */
size_t pb_seq_stop(pb_seq_run *r, pb_seq_event *out, size_t max);

/* Built-in patterns (plan §2.6). */
typedef enum : unsigned char {
    PB_PAT_PULSE, PB_PAT_OCTAVES, PB_PAT_FIFTHS, PB_PAT_ARPEGGIO, PB_PAT_BASS, PB_PAT_STABS,
    PB_PAT_HELD_CHORD, PB_PAT_VELOCITY_LADDER, PB_PAT_PITCH_LADDER, PB_PAT_TIED_LINE, PB_PAT_COUNT
} pb_builtin;
const char *pb_builtin_name(pb_builtin b);
pb_pattern pb_builtin_pattern(pb_builtin b, uint8_t root, uint32_t bpm);

#endif
