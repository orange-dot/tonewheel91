/* Full SMF reading and writing for the desk (plan §5.2): format 0 and 1 with
 * PPQ division; per-track names, every channel event, meta events (tempo,
 * time and key signatures, text, markers, lyrics, instrument names) and the
 * note pairing every orchestration and import step builds on.  SMPTE
 * division and format 2 are refused. */
#ifndef WB_SMF_H
#define WB_SMF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wb_format.h"
#include "wb_session.h"

enum { WB_SMF_TRACKS = 256, WB_SMF_NAME_MAX = 127 };

typedef struct {
    uint64_t tick;
    uint32_t seq;                  /* file order across all tracks */
    uint16_t track;
    uint8_t status;                /* channel status byte, or 0xff for meta */
    uint8_t d1, d2;                /* channel data; meta: d1 = meta type */
    uint32_t offset, length;       /* meta payload inside wb_smf.data */
} wb_smf_event;

typedef struct {
    char name[WB_SMF_NAME_MAX + 1];  /* last track-name meta, Latin-1 bytes */
    size_t first, count;             /* its events, in file order */
    uint64_t end_tick;
} wb_smf_track;

typedef struct {
    unsigned format, division, track_count;
    wb_smf_track track[WB_SMF_TRACKS];
    wb_smf_event *event;
    size_t event_count;
    uint8_t *data;                 /* copy of the file bytes */
    size_t size;
    /* What the reader repaired or saw, reported to the operator. */
    unsigned running_status, zero_velocity_offs, sysex;
} wb_smf;

bool wb_smf_read(const uint8_t *data, size_t size, wb_smf *smf, wb_error *error);
bool wb_smf_load(const char *path, wb_smf *smf, wb_error *error);
void wb_smf_free(wb_smf *smf);

/* Paired notes of one track: FIFO per (channel, pitch).  A release with no
 * held note is counted and dropped; a note still held at the end of the
 * track is closed at the track's end tick and counted. */
typedef struct {
    uint64_t start, end;
    uint8_t channel, note, velocity, program;
    uint32_t seq;                  /* of the note-on */
} wb_smf_note;

typedef struct {
    wb_smf_note *note;
    size_t count;
    unsigned unmatched_offs, stuck;
} wb_smf_notes;

/* Program in effect is tracked per channel across all tracks (format 1
 * shares channel state), from program changes earlier in file time. */
bool wb_smf_pair_notes(const wb_smf *smf, unsigned track, wb_smf_notes *notes);
void wb_smf_notes_free(wb_smf_notes *notes);

/* Tempo and time-signature maps, sorted by (tick, file order). */
typedef struct { uint64_t tick; uint32_t us_per_quarter; } wb_smf_tempo;
typedef struct { uint64_t tick; uint8_t numerator, denominator_log2, clocks, thirty_seconds; } wb_smf_meter;
size_t wb_smf_tempos(const wb_smf *smf, wb_smf_tempo *out, size_t max);
size_t wb_smf_meters(const wb_smf *smf, wb_smf_meter *out, size_t max);

/* ---- writing ------------------------------------------------------------- */

typedef struct {
    wb_buf body;
    uint64_t last_tick;
} wb_smf_track_writer;

void wb_smf_put_vlq(wb_buf *out, uint64_t value);
void wb_smf_track_begin(wb_smf_track_writer *w);
/* Raw event bytes at an absolute tick (non-decreasing). */
void wb_smf_track_event(wb_smf_track_writer *w, uint64_t tick, const uint8_t *bytes, size_t size);
void wb_smf_track_meta(wb_smf_track_writer *w, uint64_t tick, uint8_t type, const void *payload,
                       size_t size);
/* End of track at the last event's tick; appends the MTrk chunk to out. */
void wb_smf_track_end(wb_smf_track_writer *w, wb_buf *out);
void wb_smf_header(wb_buf *out, unsigned format, unsigned tracks, unsigned division);

#endif
