/* Desk renderer (docs/workbench-plan.md §2, W2): one session plus its
 * derived score rendered in a single pass that writes every stem and the mix
 * as it goes, with the manifest and ledger line written at the end.
 *
 * Bit-exactness contract.  For a session that restates an MA3 show desk
 * exhibit, the render reproduces ma3_render's PCM: the same pool setup, the
 * same event order (frame, releases before attacks, file order), controls
 * every control_frames frames before that frame's events, stems summed in
 * part order, the same fade and the same FNV-64 per 128-frame block. */
#ifndef WB_RENDER_H
#define WB_RENDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wb_session.h"

/* ---- time --------------------------------------------------------------- */

enum { WB_TEMPO_SEGMENTS = 512 };

typedef struct {
    uint64_t tick;
    uint32_t us_per_quarter;
    uint64_t us_ticks_before;      /* sum of dtick * us before this segment */
} wb_tempo_segment;

/* Tick 0 is form.start_bar.  Ticks map to frames exactly:
 * frame = floor((sum(dtick * us) * rate + D / 2) / D), D = ppq * 10^6. */
typedef struct {
    unsigned ppq, rate, ticks_per_bar;
    int32_t start_bar;
    unsigned count;
    wb_tempo_segment segment[WB_TEMPO_SEGMENTS];
} wb_timemap;

bool wb_timemap_init(wb_timemap *map, unsigned ppq, unsigned rate,
                     unsigned ticks_per_bar, int32_t start_bar,
                     const uint64_t *ticks, const uint32_t *us, unsigned count);
uint64_t wb_timemap_frame(const wb_timemap *map, uint64_t tick);
/* Bar position of a frame in f32, with the operation order of the MA3
 * exhibits: start_bar + (float)frame * K1 / (float(us) * float(ticks/bar)). */
float wb_timemap_bar(const wb_timemap *map, uint64_t frame);
/* First frame at or after a (possibly fractional) bar. */
uint64_t wb_timemap_bar_frame(const wb_timemap *map, double bar);

/* ---- score -------------------------------------------------------------- */

/* A score event: a note (on/off) or a forwarded controller.  kind is 0 for
 * notes, else the channel status kind 0xa0/0xb0/0xd0/0xe0 with d1/d2. */
typedef struct {
    uint64_t frame;
    uint32_t order;
    uint8_t part, note, velocity;
    bool on;
    uint8_t kind, d1, d2;
    uint8_t status;                /* the file's status byte, channel included */
} wb_note_event;

typedef struct {
    wb_note_event *event;
    size_t count, capacity;
    wb_timemap map;
    uint64_t last_tick;
    uint64_t last_frame;           /* render_midi compat: last event frame */
    bool compat;
    char sha256[65];               /* of the score's MIDI bytes */
} wb_score;

void wb_score_free(wb_score *score);
/* Read a derived score MIDI: format 0/1, PPQ, one channel per part in
 * session part order (channel i -> part i), notes only.  Tempo from the
 * file; 4/4 bars.  Events keep file order for equal times. */
bool wb_score_load_midi(const wb_session *session, const char *path,
                        wb_score *score, wb_error *error);
/* Sort by (frame, releases first, file order), as ma3_check does. */
void wb_score_sort(wb_score *score);

/* ---- render ------------------------------------------------------------- */

typedef struct {
    uint64_t hash;
    double square;
    float peak;
} wb_level;

typedef struct {
    unsigned notes, held_peak;
    unsigned slot_peak;
    uint64_t steals_released, steals_sustained, steals_held, sleeps;
    uint64_t fx_clips, fx_sanitizations;
    unsigned active_at_end;
    uint64_t lane_clamps;
    uint64_t controllers, controllers_ignored;
    uint32_t out_of_compass;       /* organ and ep73 */
} wb_part_report;

/* One loud moment of the mix: the 100 ms window's peak and what each stem
 * contributed on that sample and channel. */
enum { WB_PEAKS = 8 };
typedef struct {
    uint64_t frame;
    float mix;
    float stem[WB_STEMS];
} wb_peak;

typedef struct {
    uint64_t frames, total_frames;
    uint64_t measure_from, measure_to;  /* measured window [from, to) */
    unsigned outputs;              /* stems + mix */
    wb_level level[WB_STEMS + 1];
    /* Post-gain part outputs over the measured window. */
    double part_square[WB_PARTS];
    float part_peak[WB_PARTS];
    unsigned peak_count;
    wb_peak peak[WB_PEAKS];        /* loudest windows first */
    wb_part_report part[WB_PARTS];
    unsigned aggregate_peak;
    uint64_t overs;                /* samples above 0 dBFS (compat mode only) */
    /* Per form section, per output: sum of squares and frames. */
    unsigned section_count;
    double section_square[WB_SECTIONS][WB_STEMS + 1];
    uint64_t section_frames[WB_SECTIONS];
    double wall_seconds;
} wb_render_report;

typedef struct {
    const char *prefix;            /* WAV paths "<prefix>_<stem>.wav"; null: measure only */
    uint64_t frames;               /* end frame; 0: the whole form */
    /* Probe: rendering starts at preroll_from with every note held there
     * re-struck and the last controller values applied; output before
     * measure_from is rendered but neither measured nor written. */
    uint64_t preroll_from, measure_from;
    bool engine_scalar;            /* override the session's engine */
    void (*progress)(void *user, uint64_t done, uint64_t total);
    void *user;
    /* Set from a signal handler or another thread: the render stops at the
     * next block and removes its incomplete WAVs. */
    volatile int *cancel;
    /* Part cache: only `only_part` ticks and receives events, and its
     * post-gain stereo output goes to `part_out` as raw f32 frames. */
    bool solo_part;
    unsigned only_part;
    const char *part_out;
} wb_render_options;

/* Length of the session in frames: length_seconds, else the score's last
 * tick plus tail_seconds, rounded up to whole seconds. */
uint64_t wb_render_length(const wb_session *session, const wb_score *score);

/* ma3_check: held counts, unmatched releases, events inside the form,
 * held peak x unison within each limit, valid pool configuration. */
bool wb_render_check(const wb_session *session, const char *session_dir,
                     const wb_score *score, wb_render_report *report,
                     wb_error *error);

bool wb_render(const wb_session *session, const char *session_dir,
               const wb_score *score, const wb_render_options *options,
               wb_render_report *report, wb_error *error);

#endif
