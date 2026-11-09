/* Orchestration engine (docs/workbench-plan.md §5.3, W3): session rules and
 * grooves applied to the source files, giving one note list per part and
 * the derived score MIDI.  The derived MIDI is a build artifact named by its
 * own hash; identical sessions give byte-identical files.
 *
 * Per part, in this order: rule notes (session order), groove notes, the
 * end cap at form.end_bar (later attacks dropped, notes cut), the part's
 * dedupe policy, zero-length notes dropped, attacks before form.start_bar
 * dropped and the rest shifted so tick 0 is the form start, then sorted by
 * (start, pitch).  This is make_hurt_orchestration.py's order. */
#ifndef WB_ORCH_H
#define WB_ORCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wb_format.h"
#include "wb_session.h"
#include "wb_smf.h"

typedef struct {
    uint64_t start, end;           /* derived ticks; 0 = form start */
    uint8_t note, velocity;
} wb_onote;

/* A forwarded controller event: status kind (0xa0, 0xb0, 0xd0, 0xe0, no
 * channel), data bytes, derived tick. */
typedef struct {
    uint64_t tick;
    uint32_t order;
    uint8_t kind, d1, d2;
} wb_octl;

typedef struct {
    wb_onote *note;
    size_t count, capacity;
    wb_octl *ctl;
    size_t ctl_count, ctl_capacity;
    unsigned taken;                /* notes delivered by rules and grooves */
    unsigned merged;               /* exact same-start duplicates merged */
    unsigned cut;                  /* notes shortened by a re-strike */
    unsigned held_peak;
} wb_opart;

enum { WB_ORCH_TEMPOS = 512, WB_ORCH_METERS = 64 };

typedef struct {
    unsigned ppq;
    uint64_t start_tick, end_tick; /* form in master source ticks */
    unsigned ticks_per_bar;        /* of the master's first meter */
    unsigned tempo_count, meter_count;
    wb_smf_tempo tempo[WB_ORCH_TEMPOS];   /* derived ticks */
    wb_smf_meter meter[WB_ORCH_METERS];
    wb_opart part[WB_PARTS];
    unsigned part_count;
    wb_buf midi;
    char sha256[65];
    /* What the sources needed repairing. */
    unsigned unmatched_offs, stuck_notes;
} wb_derived;

void wb_derived_free(wb_derived *derived);

/* Load the sources (checking their SHA-256), apply rules and grooves, and
 * write the derived MIDI into derived->midi. */
bool wb_orch_build(const wb_session *session, const char *session_dir,
                   wb_derived *derived, wb_error *error);

/* <session dir>/derived/<name>-<sha12>.mid, written once (an existing file
 * with that name already holds the same bytes). */
bool wb_orch_write(const wb_derived *derived, const wb_session *session,
                   const char *session_dir, char *path, size_t path_size,
                   wb_error *error);

#endif
