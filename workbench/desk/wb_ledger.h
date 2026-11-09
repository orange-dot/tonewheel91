/* Run manifests and the session ledger (docs/workbench-plan.md §3.4).
 * A manifest is written once when a run ends and never edited afterwards;
 * the verdict lives in verdict.txt next to it. */
#ifndef WB_LEDGER_H
#define WB_LEDGER_H

#include <stdbool.h>
#include <stddef.h>

#include "wb_render.h"
#include "wb_session.h"

typedef struct {
    char id[64];                   /* yyyymmdd-hhmmss-label */
    const char *kind;              /* final | probe */
    const char *label;
    char started[32], finished[32];
    char run_dir[4096];
    char session_sha256[65];
    const char *score_path;
    bool engine_scalar;
    bool inhibited;
    /* Probe window in bars (kind probe), for comparisons between probes. */
    double probe_from_bar, probe_to_bar, probe_preroll_bars;
    /* Listening copy (final renders). */
    bool listening;
    char listening_sha256[65];
    double listening_gain_db, listening_peak_dbfs, listening_rms_dbfs;
} wb_run_info;

/* Fill id, run_dir (created) and the start time.  Labels are reduced to
 * [A-Za-z0-9._-]. */
bool wb_run_begin(wb_run_info *run, const char *session_dir, const char *kind,
                  const char *label, wb_error *error);

/* Host facts for the manifest: CPU model, governor, and running processes
 * known to disturb timing (browsers, VS Code). */
typedef struct {
    char cpu[128], governor[32], contamination[128];
} wb_host_info;
void wb_host_probe(wb_host_info *host);

bool wb_manifest_write(const wb_run_info *run, const wb_session *session,
                       const char *session_dir, const wb_score *score,
                       const wb_render_report *report, const wb_host_info *host,
                       wb_error *error);

/* Append "id kind label mix_fnv listening_sha wall verdict" to ledger.tsv. */
bool wb_ledger_append(const char *session_dir, const wb_run_info *run,
                      const wb_render_report *report, wb_error *error);

double wb_dbfs(double linear);

/* Read one key of an earlier manifest ("" when absent). */
bool wb_manifest_get(const char *manifest_path, const char *key, char *value, size_t size);

/* The newest earlier probe of the same bar window, if any: its run dir. */
bool wb_previous_probe(const char *session_dir, const char *exclude_id, double from_bar,
                       double to_bar, char *run_dir, size_t size);

#endif
