#define _POSIX_C_SOURCE 200809L
#include "wb_ledger.h"

#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "wb_format.h"
#include "wb_hash.h"

#ifndef WB_REPO
#define WB_REPO "."
#endif

[[gnu::format(printf, 2, 3)]]
static bool fail(wb_error *error, const char *format, ...) {
    if (error) {
        error->line = 0;
        va_list args;
        va_start(args, format);
        vsnprintf(error->message, sizeof error->message, format, args);
        va_end(args);
    }
    return false;
}

double wb_dbfs(double x) { return x > 0 ? 20.0 * log10(x) : -INFINITY; }

static void stamp(char *out, size_t size, const char *format) {
    time_t t = time(nullptr);
    struct tm local;
    localtime_r(&t, &local);
    strftime(out, size, format, &local);
}

bool wb_run_begin(wb_run_info *run, const char *dir, const char *kind, const char *label,
                  wb_error *error) {
    run->kind = kind;
    run->label = label;
    stamp(run->started, sizeof run->started, "%Y-%m-%dT%H:%M:%S%z");
    char when[32], clean[40];
    stamp(when, sizeof when, "%Y%m%d-%H%M%S");
    size_t n = 0;
    for (const char *p = label; *p && n + 1 < sizeof clean; p++) {
        char c = *p;
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
               || c == '.' || c == '_' || c == '-';
        clean[n++] = ok ? c : '-';
    }
    clean[n] = 0;
    snprintf(run->id, sizeof run->id, "%s-%s", when, n ? clean : kind);
    char runs[4200];
    snprintf(runs, sizeof runs, "%s/runs", dir);
    if (mkdir(runs, 0755) && errno != EEXIST) return fail(error, "cannot create %s", runs);
    if (snprintf(run->run_dir, sizeof run->run_dir, "%.4000s/%s", runs, run->id)
        >= (int)sizeof run->run_dir)
        return fail(error, "run directory path too long");
    if (mkdir(run->run_dir, 0755)) return fail(error, "cannot create %s", run->run_dir);
    return true;
}

static void read_line(const char *path, char *out, size_t size) {
    out[0] = 0;
    FILE *f = fopen(path, "r");
    if (!f) return;
    if (fgets(out, (int)size, f)) out[strcspn(out, "\n")] = 0;
    fclose(f);
}

void wb_host_probe(wb_host_info *h) {
    *h = (wb_host_info){ 0 };
    FILE *f = fopen("/proc/cpuinfo", "r");
    char line[512];
    while (f && fgets(line, sizeof line, f))
        if (!strncmp(line, "model name", 10)) {
            const char *colon = strchr(line, ':');
            if (colon) {
                snprintf(h->cpu, sizeof h->cpu, "%s", colon + 2);
                h->cpu[strcspn(h->cpu, "\n")] = 0;
            }
            break;
        }
    if (f) fclose(f);
    read_line("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", h->governor, sizeof h->governor);
    /* Browsers and VS Code moved timings by up to 66% on this host. */
    static const char *const NAMES[] = { "code", "chrome", "firefox", "chromium", "electron" };
    bool seen[sizeof NAMES / sizeof *NAMES] = { 0 };
    DIR *proc = opendir("/proc");
    struct dirent *e;
    while (proc && (e = readdir(proc))) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        char path[300], comm[64];
        snprintf(path, sizeof path, "/proc/%s/comm", e->d_name);
        read_line(path, comm, sizeof comm);
        for (size_t i = 0; i < sizeof NAMES / sizeof *NAMES; i++)
            if (!strcmp(comm, NAMES[i])) seen[i] = true;
    }
    if (proc) closedir(proc);
    for (size_t i = 0; i < sizeof NAMES / sizeof *NAMES; i++)
        if (seen[i]) {
            size_t used = strlen(h->contamination);
            snprintf(h->contamination + used, sizeof h->contamination - used, "%s%s",
                     used ? "," : "", NAMES[i]);
        }
    if (!h->contamination[0]) snprintf(h->contamination, sizeof h->contamination, "none");
}

static void git_describe(char *out, size_t size) {
    snprintf(out, size, "unknown");
    FILE *p = popen("git -C '" WB_REPO "' describe --always --dirty 2>/dev/null", "r");
    if (!p) return;
    if (fgets(out, (int)size, p)) out[strcspn(out, "\n")] = 0;
    pclose(p);
}

static double rms(double square, uint64_t samples) {
    return samples ? sqrt(square / (double)samples) : 0;
}

bool wb_manifest_write(const wb_run_info *run, const wb_session *s, const char *dir,
                       const wb_score *score, const wb_render_report *r,
                       const wb_host_info *host, wb_error *error) {
    wb_buf m = { 0 };
    char finished[32], binary[65] = "unknown", describe[96], sha[65];
    stamp(finished, sizeof finished, "%Y-%m-%dT%H:%M:%S%z");
    wb_sha256_file_hex("/proc/self/exe", binary);
    git_describe(describe, sizeof describe);
    wb_buf_printf(&m, "mwr=1\nid=%s\nkind=%s\nlabel=%s\nstarted=%s\nfinished=%s\nwall_seconds=%.1f\n",
                  run->id, run->kind, run->label, run->started, finished, r->wall_seconds);
    wb_buf_printf(&m, "session.name=%s\nsession.sha256=%s\nscore.path=%s\nscore.sha256=%s\n",
                  s->head.name, run->session_sha256, run->score_path ? run->score_path : "",
                  score->sha256);
    for (unsigned i = 0; i < s->source_count; i++)
        wb_buf_printf(&m, "source.%s.sha256=%s\n", s->source[i].id, s->source[i].sha256);
    for (unsigned i = 0; i < s->part_count; i++) {
        char path[4400];
        snprintf(path, sizeof path, "%s/%s", dir, s->part[i].patch);
        if (!wb_sha256_file_hex(path, sha)) snprintf(sha, sizeof sha, "unreadable");
        wb_buf_printf(&m, "patch.%s.sha256=%s\n", s->part[i].id, sha);
    }
    wb_buf_printf(&m, "binary.sha256=%s\ngit.describe=%s\ncompiler=%s\nengine=%s\n", binary,
                  describe, __VERSION__,
                  run->engine_scalar || s->head.engine == WB_ENGINE_SCALAR ? "scalar" : "auto");
    wb_buf_printf(&m, "host.cpu=%s\nhost.governor=%s\nhost.contamination=%s\nhost.sleep_inhibited=%s\n",
                  host->cpu, host->governor, host->contamination, run->inhibited ? "yes" : "no");
    wb_buf_printf(&m, "rate=%u\nframes=%llu\ntotal_frames=%llu\nmeasure_from=%llu\naggregate_active_peak=%u\n",
                  s->head.rate, (unsigned long long)r->frames, (unsigned long long)r->total_frames,
                  (unsigned long long)r->measure_from, r->aggregate_peak);
    if (!strcmp(run->kind, "probe"))
        wb_buf_printf(&m, "probe.from_bar=%g\nprobe.to_bar=%g\nprobe.preroll_bars=%g\n",
                      run->probe_from_bar, run->probe_to_bar, run->probe_preroll_bars);
    if (run->listening)
        wb_buf_printf(&m, "listening.sha256=%s\nlistening.gain_db=%.3f\nlistening.peak_dbfs=%.3f\n"
                      "listening.rms_dbfs=%.3f\n", run->listening_sha256, run->listening_gain_db,
                      run->listening_peak_dbfs, run->listening_rms_dbfs);
    for (unsigned o = 0; o < r->outputs; o++) {
        const char *name = o + 1 == r->outputs ? "mix" : s->stem[o].id;
        const wb_level *l = &r->level[o];
        wb_buf_printf(&m, "out.%s.fnv64=%016llx\nout.%s.peak_dbfs=%.3f\nout.%s.rms_dbfs=%.3f\n",
                      name, (unsigned long long)l->hash, name, wb_dbfs(l->peak), name,
                      wb_dbfs(rms(l->square, 2 * r->frames)));
    }
    for (unsigned p = 0; p < s->part_count; p++)
        wb_buf_printf(&m, "level.%s.rms_dbfs=%.3f\nlevel.%s.peak_dbfs=%.3f\n", s->part[p].id,
                      wb_dbfs(rms(r->part_square[p], 2 * r->frames)), s->part[p].id,
                      wb_dbfs(r->part_peak[p]));
    for (unsigned i = 0; i < r->peak_count; i++) {
        const wb_peak *x = &r->peak[i];
        wb_buf_printf(&m, "peak.%u=frame %llu mix %.6f", i + 1, (unsigned long long)x->frame, (double)x->mix);
        for (unsigned st = 0; st + 1 < r->outputs; st++)
            wb_buf_printf(&m, " %s %.6f", s->stem[st].id, (double)x->stem[st]);
        wb_buf_printf(&m, "\n");
    }
    for (unsigned p = 0; p < s->part_count; p++) {
        const wb_part_report *x = &r->part[p];
        wb_buf_printf(&m, "part.%s=notes %u held_peak %u slot_peak %u steals_rsh %llu/%llu/%llu "
                      "sleeps %llu fx_clips %llu sanitizations %llu active_at_end %u lane_clamps %llu\n",
                      s->part[p].id, x->notes, x->held_peak, x->slot_peak,
                      (unsigned long long)x->steals_released, (unsigned long long)x->steals_sustained,
                      (unsigned long long)x->steals_held, (unsigned long long)x->sleeps,
                      (unsigned long long)x->fx_clips, (unsigned long long)x->fx_sanitizations,
                      x->active_at_end, (unsigned long long)x->lane_clamps);
    }
    for (unsigned i = 0; i < r->section_count; i++) {
        if (!r->section_frames[i]) continue;
        wb_buf_printf(&m, "section.%s=bars %d..%d", s->form.section[i].name,
                      s->form.section[i].start_bar, s->form.section[i].end_bar);
        for (unsigned o = 0; o < r->outputs; o++)
            wb_buf_printf(&m, " %s %.2f", o + 1 == r->outputs ? "mix" : s->stem[o].id,
                          wb_dbfs(rms(r->section_square[i][o], 2 * r->section_frames[i])));
        wb_buf_printf(&m, "\n");
    }
    char path[4200];
    snprintf(path, sizeof path, "%s/manifest.mwr", run->run_dir);
    bool ok = !m.failed && wb_write_file_atomic(path, m.data, m.length);
    wb_buf_free(&m);
    return ok || fail(error, "cannot write %s", path);
}

bool wb_ledger_append(const char *dir, const wb_run_info *run, const wb_render_report *r,
                      wb_error *error) {
    char path[4200];
    snprintf(path, sizeof path, "%s/ledger.tsv", dir);
    bool fresh = access(path, F_OK) != 0;
    FILE *f = fopen(path, "a");
    if (!f) return fail(error, "cannot open %s", path);
    if (fresh) fputs("id\tkind\tlabel\tmix_fnv64\tlistening_sha256\twall_s\tverdict\n", f);
    fprintf(f, "%s\t%s\t%s\t%016llx\t%s\t%.1f\t-\n", run->id, run->kind, run->label,
            (unsigned long long)r->level[r->outputs - 1].hash,
            run->listening ? run->listening_sha256 : "-", r->wall_seconds);
    bool ok = !ferror(f);
    if (fclose(f)) ok = false;
    return ok || fail(error, "cannot write %s", path);
}

bool wb_manifest_get(const char *path, const char *key, char *value, size_t size) {
    value[0] = 0;
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char line[2048];
    size_t n = strlen(key);
    bool found = false;
    while (!found && fgets(line, sizeof line, f))
        if (!strncmp(line, key, n) && line[n] == '=') {
            line[strcspn(line, "\n")] = 0;
            snprintf(value, size, "%s", line + n + 1);
            found = true;
        }
    fclose(f);
    return found;
}

bool wb_previous_probe(const char *dir, const char *exclude, double from, double to, char *out,
                       size_t size) {
    char runs[4200];
    snprintf(runs, sizeof runs, "%s/runs", dir);
    DIR *d = opendir(runs);
    if (!d) return false;
    char best[256] = "";
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.' || !strcmp(e->d_name, exclude)) continue;
        char path[4600], kind[16], a[32], b[32];
        snprintf(path, sizeof path, "%s/%s/manifest.mwr", runs, e->d_name);
        if (!wb_manifest_get(path, "kind", kind, sizeof kind) || strcmp(kind, "probe")) continue;
        if (!wb_manifest_get(path, "probe.from_bar", a, sizeof a)
            || !wb_manifest_get(path, "probe.to_bar", b, sizeof b))
            continue;
        if (atof(a) != from || atof(b) != to) continue;
        /* Run ids start with the time, so the larger name is newer. */
        if (strcmp(e->d_name, best) > 0) snprintf(best, sizeof best, "%s", e->d_name);
    }
    closedir(d);
    if (!best[0]) return false;
    snprintf(out, size, "%s/%s", runs, best);
    return true;
}
