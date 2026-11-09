#define _POSIX_C_SOURCE 200809L
#include "wb_jobs.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

void wb_jobs_init(wb_jobs *j, const char *wb_path) {
    memset(j, 0, sizeof *j);
    j->pid = -1;
    j->out_fd = -1;
    snprintf(j->wb_path, sizeof j->wb_path, "%s", wb_path);
}

static void job_free(wb_job *job) {
    for (int i = 0; i < job->argc; i++) free(job->argv[i]);
    memset(job, 0, sizeof *job);
}

bool wb_jobs_push(wb_jobs *j, const char *label, const char *const *args) {
    if (j->count == WB_JOB_QUEUE) return false;
    wb_job *job = &j->queue[j->count];
    memset(job, 0, sizeof *job);
    snprintf(job->label, sizeof job->label, "%s", label);
    job->argv[job->argc++] = strdup(j->wb_path);
    for (unsigned i = 0; args[i] && job->argc < WB_JOB_ARGS - 1; i++) job->argv[job->argc++] = strdup(args[i]);
    job->argv[job->argc] = nullptr;
    j->count++;
    return true;
}

bool wb_jobs_running(const wb_jobs *j) { return j->pid > 0; }

static void add_line(wb_jobs *j, const char *text, size_t n) {
    if (j->line_count == WB_JOB_LINES) {
        memmove(j->line, j->line + 1, sizeof j->line[0] * (WB_JOB_LINES - 1));
        j->line_count--;
    }
    if (n >= WB_JOB_LINE) n = WB_JOB_LINE - 1;
    memcpy(j->line[j->line_count], text, n);
    j->line[j->line_count++][n] = 0;
}

static bool start(wb_jobs *j) {
    int pipe_fd[2];
    if (pipe(pipe_fd)) return false;
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pipe_fd[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, pipe_fd[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipe_fd[0]);
    pid_t pid;
    int rc = posix_spawn(&pid, j->queue[0].argv[0], &actions, nullptr, j->queue[0].argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(pipe_fd[1]);
    if (rc) {
        close(pipe_fd[0]);
        char text[160];
        int n = snprintf(text, sizeof text, "%s: cannot start %s", j->queue[0].label, j->queue[0].argv[0]);
        add_line(j, text, (size_t)n);
        return false;
    }
    fcntl(pipe_fd[0], F_SETFL, fcntl(pipe_fd[0], F_GETFL) | O_NONBLOCK);
    j->pid = pid;
    j->out_fd = pipe_fd[0];
    j->partial_length = 0;
    char text[160];
    int n = snprintf(text, sizeof text, "> %s", j->queue[0].label);
    add_line(j, text, (size_t)n);
    return true;
}

static void pop(wb_jobs *j) {
    job_free(&j->queue[0]);
    memmove(j->queue, j->queue + 1, sizeof j->queue[0] * (j->count - 1));
    j->count--;
    memset(&j->queue[j->count], 0, sizeof j->queue[0]);
}

bool wb_jobs_poll(wb_jobs *j) {
    bool changed = false;
    if (j->pid <= 0 && j->count) {
        if (!start(j)) pop(j);
        changed = true;
    }
    if (j->out_fd >= 0) {
        char buffer[4096];
        ssize_t got;
        while ((got = read(j->out_fd, buffer, sizeof buffer)) > 0) {
            for (ssize_t i = 0; i < got; i++) {
                if (buffer[i] == '\n' || j->partial_length == WB_JOB_LINE - 1) {
                    add_line(j, j->partial, j->partial_length);
                    j->partial_length = 0;
                } else j->partial[j->partial_length++] = buffer[i];
            }
            changed = true;
        }
        if (got == 0) {
            if (j->partial_length) add_line(j, j->partial, j->partial_length);
            close(j->out_fd);
            j->out_fd = -1;
        }
    }
    if (j->pid > 0 && j->out_fd < 0) {
        int status;
        if (waitpid(j->pid, &status, 0) == j->pid) {
            j->last_status = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
            snprintf(j->finished_label, sizeof j->finished_label, "%s", j->queue[0].label);
            j->finished = true;
            char text[160];
            int n = snprintf(text, sizeof text, "< %s: %s", j->queue[0].label,
                             j->last_status ? "failed" : "done");
            add_line(j, text, (size_t)n);
            j->pid = -1;
            pop(j);
            changed = true;
        }
    }
    return changed;
}

void wb_jobs_cancel(wb_jobs *j) {
    if (j->pid > 0) kill(j->pid, SIGTERM);
}

void wb_jobs_shutdown(wb_jobs *j) {
    wb_jobs_cancel(j);
    if (j->out_fd >= 0) close(j->out_fd);
    if (j->pid > 0) waitpid(j->pid, nullptr, 0);
    while (j->count) pop(j);
}
