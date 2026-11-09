/* Import of an existing MIDI arrangement (docs/workbench-plan.md §5.2, W3b).
 *
 * A plan splits the source into proposed parts; nothing is applied until
 * the plan is committed into a session.  Each proposed part becomes one
 * part, one rule linking it to the source, and a session-local copy of its
 * patch template, so the parts stay linked and a re-import can diff them. */
#ifndef WB_IMPORT_H
#define WB_IMPORT_H

#include <stdbool.h>
#include <stddef.h>

#include "wb_session.h"
#include "wb_smf.h"

enum { WB_IMPORT_PARTS = 64, WB_ASSIGNS = 64, WB_TEMPLATE_MAX = 31 };

/* Manual split: a source stream (track, optional channel) into a named part.
 * Streams that share a part id merge into one part with several rules. */
typedef struct {
    int32_t track, channel;        /* channel 1..16, or -1 for every channel */
    wb_id part;
} wb_assign;

typedef struct {
    wb_split split;
    int32_t drums;                 /* drum channel 1..16, 0 off; default 10 */
    bool program_split;
    unsigned pitch_split_count;
    wb_pitch_split pitch_split[WB_PITCH_SPLITS];
    unsigned assign_count;
    wb_assign assign[WB_ASSIGNS];
    wb_ctlset controllers;         /* forwarded controller streams */
    bool keep_leading_silence;
    const char *map_path;          /* engine/template table; null: built-in */
} wb_import_options;

void wb_import_options_default(wb_import_options *options);

typedef struct {
    wb_id id;
    unsigned rule_count;
    wb_rule rule[8];               /* source field left empty until applied */
    /* Suggestion (marked as such until committed). */
    char engine[8];                /* mamut | organ | ep73 */
    char template_name[WB_TEMPLATE_MAX + 1];
    char stem[WB_ID_MAX + 1];
    unsigned limit, unison;
    /* Statistics over the part's notes, before orchestration. */
    unsigned notes, held_peak, low, high, velocity_min, velocity_max;
    double first_bar, last_bar;
    unsigned controllers;
    bool compass_ok;               /* inside the suggested engine's compass */
} wb_import_part;

typedef struct {
    unsigned part_count;
    wb_import_part part[WB_IMPORT_PARTS];
    char title[WB_TEXT_MAX + 1];
    int32_t start_bar, end_bar;
    unsigned section_count;
    wb_section section[WB_SECTIONS];
    unsigned empty_streams;        /* streams without notes: listed, no part */
    unsigned unmatched_offs, stuck_notes, running_status, zero_velocity_offs;
} wb_import_plan;

bool wb_import_plan_build(const wb_smf *smf, const wb_import_options *options,
                          wb_import_plan *plan, wb_error *error);

/* Commit a plan into a session: copy the MIDI file into sources/, add the
 * source, an [import] block, the stems the parts need, the parts with
 * patches copied from <templates_dir>/<template>.mapatch (or a factory
 * preset when the template file is absent), and their rules.  When the
 * session has no form yet (end_bar <= start_bar + 1 and no sections) the
 * form comes from the plan.  The session is unchanged on failure. */
bool wb_import_apply(wb_session *session, const char *session_dir,
                     const char *source_id, const char *midi_path,
                     const wb_import_plan *plan, const wb_import_options *options,
                     const char *templates_dir, wb_error *error);

#endif
