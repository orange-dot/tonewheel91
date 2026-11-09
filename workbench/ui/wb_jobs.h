/* Background jobs of the UI: every render, probe, reproduce and export runs
 * as the headless `wb` command on the same session file, so the UI and the
 * CLI share one code path (plan §2.1).  One job runs at a time; its output
 * lines stream into the console without blocking the UI thread. */
#ifndef WB_JOBS_H
#define WB_JOBS_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

enum { WB_JOB_ARGS = 24, WB_JOB_QUEUE = 16, WB_JOB_LINE = 256, WB_JOB_LINES = 48 };

typedef struct {
    char label[64];
    char *argv[WB_JOB_ARGS];       /* owned copies */
    int argc;
} wb_job;

typedef struct {
    wb_job queue[WB_JOB_QUEUE];
    unsigned count;                /* queued, the running one is queue[0] */
    pid_t pid;
    int out_fd;
    char partial[WB_JOB_LINE];
    size_t partial_length;
    /* Recent output of the running job, newest last. */
    char line[WB_JOB_LINES][WB_JOB_LINE];
    unsigned line_count;
    int last_status;               /* exit status of the last finished job */
    char finished_label[64];
    bool finished;                 /* a job ended since the flag was cleared */
    char wb_path[1024];
} wb_jobs;

void wb_jobs_init(wb_jobs *jobs, const char *wb_path);
/* Queue `wb <args...>`; the args are copied. */
bool wb_jobs_push(wb_jobs *jobs, const char *label, const char *const *args);
/* Start the next job if idle, read available output; never blocks.
 * Returns true when anything changed (for redraw). */
bool wb_jobs_poll(wb_jobs *jobs);
bool wb_jobs_running(const wb_jobs *jobs);
/* SIGTERM to the running job (wb removes its incomplete files). */
void wb_jobs_cancel(wb_jobs *jobs);
void wb_jobs_shutdown(wb_jobs *jobs);

#endif
