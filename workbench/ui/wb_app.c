/* Workbench UI (W0 frame, W4/W5/W6/W10 views).
 *
 *   workbench [FILE.mws] [--single] [--smoke] [--light]
 *
 * Edits go through the session journal exactly as `wb set/replace` do, the
 * session file is saved after each one, and the derived score is rebuilt.
 * Renders, probes and ledger actions run as `wb` subprocess jobs.  The UI
 * thread never blocks on them and redraws only after events, job output or
 * a status tick, at most 60 times a second (plan §2.3). */
#define _XOPEN_SOURCE 700
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../desk/wb_root.h"
#include "wb_appstate.h"

enum { FONT_PIXELS = 13, FRAME_MS = 16 };

/* ---- console and small API for the views --------------------------------- */

void app_say(app *a, bool error, const char *format, ...) {
    if (a->console_count == CONSOLE_KEEP) {
        memmove(a->console, a->console + 1, sizeof a->console[0] * (CONSOLE_KEEP - 1));
        memmove(a->console_error, a->console_error + 1, sizeof a->console_error[0] * (CONSOLE_KEEP - 1));
        a->console_count--;
    }
    va_list args;
    va_start(args, format);
    vsnprintf(a->console[a->console_count], CONSOLE_LINE, format, args);
    va_end(args);
    a->console_error[a->console_count++] = error;
}

const wb_theme *app_theme(const app *a) { return wb_theme_get(a->theme); }

static void rebuild(app *a) {
    a->version++;
    wb_derived_free(a->derived);
    wb_error e = { 0 };
    a->derived_ok = wb_session_validate(a->session, &e) && wb_orch_build(a->session, a->dir, a->derived, &e);
    snprintf(a->derived_error, sizeof a->derived_error, "%s", a->derived_ok ? "" : e.message);
}

static bool after_edit(app *a, bool ok, const wb_error *e) {
    if (!ok) {
        app_say(a, true, "edit refused: %s", e->message);
        return false;
    }
    wb_error s = { 0 };
    if (!wb_save(a->path, a->session, &s)) app_say(a, true, "save: %s", s.message);
    rebuild(a);
    return true;
}

bool app_replace(app *a, wb_kind kind, unsigned index, const char *block) {
    wb_error e = { 0 };
    return after_edit(a, wb_edit_replace(&a->journal, a->session, kind, index, block, &e), &e);
}

bool app_insert(app *a, wb_kind kind, unsigned index, const char *block) {
    wb_error e = { 0 };
    return after_edit(a, wb_edit_insert(&a->journal, a->session, kind, index, block, &e), &e);
}

bool app_remove(app *a, wb_kind kind, unsigned index) {
    wb_error e = { 0 };
    return after_edit(a, wb_edit_remove(&a->journal, a->session, kind, index, &e), &e);
}

bool app_set_key(app *a, wb_kind kind, unsigned index, const char *key, const char *value) {
    wb_error e = { 0 };
    return after_edit(a, wb_edit_set_key(&a->journal, a->session, kind, index, key, value, &e), &e);
}

void app_inspect(app *a, wb_kind kind, int index) {
    a->inspect_kind = kind;
    a->inspect_index = index;
}

void app_job(app *a, const char *label, const char *const *args) {
    if (!wb_jobs_push(&a->jobs, label, args)) app_say(a, true, "job queue full");
}

static void undo(app *a, bool redo) {
    wb_error e = { 0 };
    bool ok = redo ? wb_journal_redo(&a->journal, a->session, &e) : wb_journal_undo(&a->journal, a->session, &e);
    if (!ok) { app_say(a, false, "%s", e.message); return; }
    after_edit(a, true, &e);
    app_say(a, false, "%s (%zu of %zu edits applied)", redo ? "redo" : "undo", a->journal.cursor, a->journal.count);
}

/* ---- ledger -------------------------------------------------------------- */

void app_reload_ledger(app *a) {
    a->ledger_count = 0;
    char path[4200];
    snprintf(path, sizeof path, "%s/ledger.tsv", a->dir);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[2048];
    bool header = true;
    while (a->ledger_count < LEDGER_ROWS && fgets(line, sizeof line, f)) {
        if (header) { header = false; continue; }
        ledger_row *r = &a->ledger[a->ledger_count];
        *r = (ledger_row){ 0 };
        char *field[7] = { 0 }, *save = nullptr;
        unsigned k = 0;
        for (char *t = strtok_r(line, "\t\n", &save); t && k < 7; t = strtok_r(nullptr, "\t\n", &save)) field[k++] = t;
        if (k < 6) continue;
        snprintf(r->id, sizeof r->id, "%s", field[0]);
        snprintf(r->kind, sizeof r->kind, "%s", field[1]);
        snprintf(r->label, sizeof r->label, "%s", field[2]);
        snprintf(r->fnv, sizeof r->fnv, "%s", field[3]);
        snprintf(r->listening, sizeof r->listening, "%s", field[4]);
        snprintf(r->wall, sizeof r->wall, "%s", field[5]);
        char verdict_path[4600];
        snprintf(verdict_path, sizeof verdict_path, "%s/runs/%s/verdict.txt", a->dir, r->id);
        FILE *v = fopen(verdict_path, "r");
        if (v) {
            if (fgets(r->verdict, sizeof r->verdict, v)) r->verdict[strcspn(r->verdict, "\n")] = 0;
            fclose(v);
        }
        a->ledger_count++;
    }
    fclose(f);
}

/* ---- sources ------------------------------------------------------------- */

typedef struct { uint64_t tick; int delta; } edge;
static int compare_edge(const void *x, const void *y) {
    const edge *p = x, *q = y;
    if (p->tick != q->tick) return p->tick < q->tick ? -1 : 1;
    return p->delta - q->delta;
}
static int compare_u8(const void *x, const void *y) { return *(const uint8_t *)x - *(const uint8_t *)y; }
static int compare_u64(const void *x, const void *y) {
    uint64_t p = *(const uint64_t *)x, q = *(const uint64_t *)y;
    return (p > q) - (p < q);
}

static void free_sources(app *a) {
    for (unsigned i = 0; i < WB_SOURCES; i++) {
        source_view *v = &a->source[i];
        for (unsigned t = 0; t < WB_SMF_TRACKS; t++) free(v->track[t].density);
        if (v->loaded) wb_smf_free(&v->smf);
        memset(v, 0, sizeof *v);
    }
}

static void load_source(app *a, unsigned index) {
    source_view *v = &a->source[index];
    char path[4400];
    snprintf(path, sizeof path, "%s/%s", a->dir, a->session->source[index].path);
    wb_error e = { 0 };
    if (!wb_smf_load(path, &v->smf, &e)) {
        snprintf(v->error, sizeof v->error, "%s", e.message);
        return;
    }
    v->loaded = true;
    v->ticks_per_bar = 4 * v->smf.division;
    wb_smf_meter meter;
    if (wb_smf_meters(&v->smf, &meter, 1) && meter.tick == 0 && meter.numerator)
        v->ticks_per_bar = meter.numerator * 4 * v->smf.division / (1u << meter.denominator_log2);
    uint64_t last = 0;
    for (unsigned t = 0; t < v->smf.track_count; t++)
        if (v->smf.track[t].end_tick > last) last = v->smf.track[t].end_tick;
    v->bars = (unsigned)((last + v->ticks_per_bar - 1) / v->ticks_per_bar);
    double tpb = v->ticks_per_bar;
    for (unsigned t = 0; t < v->smf.track_count; t++) {
        track_stats *x = &v->track[t];
        *x = (track_stats){ .low = 127, .velocity_min = 127, .program = -1, .channel = -1 };
        const wb_smf_track *track = &v->smf.track[t];
        for (size_t i = 0; i < track->count; i++) {
            const wb_smf_event *ev = &v->smf.event[track->first + i];
            unsigned kind = ev->status & 0xf0u;
            if (ev->status != 0xffu && kind != 0x80u && kind != 0x90u && kind != 0xc0u) x->controllers++;
        }
        wb_smf_notes n;
        if (!wb_smf_pair_notes(&v->smf, t, &n) || !n.count) { wb_smf_notes_free(&n); continue; }
        x->notes = (unsigned)n.count;
        x->density = calloc(v->bars + 1, sizeof *x->density);
        uint8_t *vel = malloc(n.count);
        uint64_t *dur = malloc(n.count * sizeof *dur);
        edge *edges = malloc(2 * n.count * sizeof *edges);
        x->first_bar = INFINITY;
        for (size_t i = 0; i < n.count; i++) {
            const wb_smf_note *note = &n.note[i];
            if (note->note < x->low) x->low = note->note;
            if (note->note > x->high) x->high = note->note;
            if (vel) vel[i] = note->velocity;
            if (dur) dur[i] = note->end - note->start;
            if (edges) {
                edges[2 * i] = (edge){ note->start, 1 };
                edges[2 * i + 1] = (edge){ note->end, -1 };
            }
            double b0 = note->start / tpb, b1 = note->end / tpb;
            if (b0 < x->first_bar) x->first_bar = b0;
            if (b1 > x->last_bar) x->last_bar = b1;
            if (b1 - b0 > 2) x->long_notes++;
            x->pitch_class[note->note % 12]++;
            unsigned bar = (unsigned)b0;
            if (x->density && bar <= v->bars) x->density[bar]++;
            if (x->program < 0) { x->program = note->program; x->channel = note->channel; }
        }
        if (vel) {
            qsort(vel, n.count, 1, compare_u8);
            x->velocity_min = vel[0];
            x->velocity_median = vel[n.count / 2];
            x->velocity_max = vel[n.count - 1];
        }
        if (dur) {
            qsort(dur, n.count, sizeof *dur, compare_u64);
            x->duration_median_bars = dur[n.count / 2] / tpb;
            x->duration_max_bars = dur[n.count - 1] / tpb;
        }
        if (edges) {
            qsort(edges, 2 * n.count, sizeof *edges, compare_edge);
            int active = 0;
            for (size_t i = 0; i < 2 * n.count; i++) {
                active += edges[i].delta;
                if (active > (int)x->held_peak) x->held_peak = (unsigned)active;
            }
        }
        free(vel);
        free(dur);
        free(edges);
        wb_smf_notes_free(&n);
    }
}

bool app_load_session(app *a, const char *path) {
    wb_session *s = malloc(sizeof *s);
    wb_error e = { 0 };
    if (!s || !wb_load(path, s, &e)) {
        app_say(a, true, "%s:%u: %s", path, e.line, e.message);
        free(s);
        return false;
    }
    free_sources(a);
    free(a->session);
    a->session = s;
    snprintf(a->path, sizeof a->path, "%s", path);
    wb_dir_of(path, a->dir, sizeof a->dir);
    char journal[1100];
    snprintf(journal, sizeof journal, "%s.journal", path);
    bool stale = false;
    if (!wb_journal_open(&a->journal, journal, s, &stale, &e)) app_say(a, true, "%s", e.message);
    if (stale) app_say(a, true, "%s did not match the session; set aside as .stale", journal);
    for (unsigned i = 0; i < s->source_count; i++) load_source(a, i);
    a->part = s->part_count ? 0 : -1;
    a->patch_part = -1;
    a->roll_bars = 0;
    a->listen_columns = 0;
    rebuild(a);
    app_reload_ledger(a);
    if (!a->derived_ok) app_say(a, true, "derived score: %s", a->derived_error);
    snprintf(a->probe_from, sizeof a->probe_from, "%d", s->form.start_bar);
    snprintf(a->probe_to, sizeof a->probe_to, "%d",
             s->form.start_bar + 8 < s->form.end_bar ? s->form.start_bar + 8 : s->form.end_bar);
    app_say(a, false, "%s: %u parts, %u stems, %u rules, %zu journal edits", path, s->part_count, s->stem_count,
            s->rule_count, a->journal.count);
    if (a->main.window) {
        char title[1400];
        snprintf(title, sizeof title, "tonewheel91 Workbench - %s - %s", s->head.name, wb_home());
        SDL_SetWindowTitle(a->main.window, title);
    }
    return true;
}

/* ---- live audio ---------------------------------------------------------- */

static void record(app *a, const wb_live_cmd *cmd) {
    uint8_t status, d1, d2;
    if (cmd->kind == WB_LIVE_NOTE_ON) { status = 0x90; d1 = cmd->d0; d2 = cmd->d1; }
    else if (cmd->kind == WB_LIVE_NOTE_OFF) { status = 0x80; d1 = cmd->d0; d2 = 0; }
    else if (cmd->kind == WB_LIVE_MIDI) { status = cmd->d0; d1 = cmd->d1; d2 = cmd->d2; }
    else return;
    if (a->take_count == a->take_capacity) {
        size_t capacity = a->take_capacity ? 2 * a->take_capacity : 1024;
        wb_take_event *grown = realloc(a->take_events, capacity * sizeof *grown);
        if (!grown) return;
        a->take_events = grown;
        a->take_capacity = capacity;
    }
    a->take_events[a->take_count++] = (wb_take_event){ SDL_GetTicksNS() / 1000 - a->record_start_us, cmd->slot,
                                                       status, d1, d2 };
}

void app_audio_send(app *a, wb_live_cmd cmd) {
    if (a->live && a->audio_on && !wb_live_send(a->live, cmd)) app_say(a, true, "audio command ring full");
    else if (a->recording) record(a, &cmd);
}

void app_record_toggle(app *a) {
    if (a->recording) {
        a->recording = false;
        a->take_pending = a->take_count != 0;
        app_say(a, false, "take stopped: %zu events; keep or discard", a->take_count);
        return;
    }
    if (!a->audio_on) app_audio_toggle(a);
    a->take_count = 0;
    a->take_pending = false;
    a->record_start_us = SDL_GetTicksNS() / 1000;
    a->recording = true;
    app_say(a, false, "recording a take (MIDI events, not audio)");
}

void app_take_keep(app *a, bool keep) {
    a->take_pending = false;
    if (!keep || !a->session) { a->take_count = 0; return; }
    char dir[1100], path[1200], when[32], id[40];
    snprintf(dir, sizeof dir, "%s/takes", a->dir);
    time_t now = time(nullptr);
    struct tm local;
    localtime_r(&now, &local);
    strftime(when, sizeof when, "%Y%m%d-%H%M%S", &local);
    snprintf(path, sizeof path, "%s/take-%s.mid", dir, when);
    snprintf(id, sizeof id, "take-%s", when);
    const char *names[4];
    for (unsigned s = 0; s < 4; s++)
        names[s] = a->rack_part[s] >= 0 ? a->session->part[a->rack_part[s]].id
                 : a->part >= 0 && s == 0 ? a->session->part[a->part].id : "take";
    wb_error e = { 0 };
    if (!wb_take_write(path, a->take_events, a->take_count, names, a->session->head.name, &e)) {
        app_say(a, true, "%s", e.message);
        return;
    }
    app_say(a, false, "take saved: %s; registering it as source '%s'", path, id);
    const char *args[] = { "add-source", a->path, id, path, nullptr };
    app_job(a, "add take as source", args);
    a->take_count = 0;
}

/* Install the rack (or, with an empty rack, the selected part) and the takes,
 * then (re)start the thread.  Instruments change only while audio is stopped. */
static bool raise_priority(void) {
    /* SDL asks RealtimeKit over D-Bus on Linux, as PipeWire clients do. */
    return SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_TIME_CRITICAL);
}

void app_audio_reload(app *a) {
    bool was_on = a->audio_on;
    if (a->live) wb_live_stop(a->live);
    a->audio_on = false;
    if (!was_on || !a->session) return;
    if (!a->live && !(a->live = wb_live_create(a->session->head.rate))) return;
    a->live->raise_priority = raise_priority;
    bool any = false;
    for (unsigned s = 0; s < WB_LIVE_SLOTS; s++) {
        wb_live_clear_slot(a->live, s);
        int part = a->rack_part[s];
        if (part < 0 || (unsigned)part >= a->session->part_count) continue;
        wb_error e = { 0 };
        if (wb_live_load_part(a->live, s, a->session, a->dir, (unsigned)part, &e)) any = true;
        else app_say(a, true, "rack slot %u: %s", s + 1, e.message);
    }
    if (!any && a->part >= 0) {
        wb_error e = { 0 };
        if (!wb_live_load_part(a->live, 0, a->session, a->dir, (unsigned)a->part, &e))
            app_say(a, true, "audition: %s", e.message);
    }
    for (unsigned t = 0; t < 2; t++)
        wb_live_set_take(a->live, t, a->take[t], a->take_frames[t], (float)a->take_gain[t]);
    memset(a->key_note, 0, sizeof a->key_note);
    memset(a->key_slots, 0, sizeof a->key_slots);
    const char *periods = getenv("WB_AUDIO_PERIODS");
    unsigned count = periods && atoi(periods) >= 2 ? (unsigned)atoi(periods) : 3;
    if (wb_live_start(a->live, a->audio_device, 128, count)) a->audio_on = true;
    else app_say(a, true, "audio: %s", a->live->error);
}

void app_audio_toggle(app *a) {
    if (a->audio_on) {
        wb_live_stop(a->live);
        a->audio_on = false;
        app_say(a, false, "audio off");
        return;
    }
    a->audio_on = true;
    app_audio_reload(a);
    if (a->audio_on) {
        static const char *const SCHED[] = { "normal priority (no rtprio: xruns likely under load)", "nice -10",
                                             "SCHED_FIFO", "real time via RealtimeKit" };
        SDL_Delay(20);
        app_say(a, atomic_load(&a->live->scheduling) == 0, "audio on: %s, 48 kHz, %u-frame periods, %s",
                a->audio_device, a->live->period, SCHED[atomic_load(&a->live->scheduling)]);
    }
}

bool app_load_take(app *a, unsigned index, const char *run) {
    char path[4600];
    snprintf(path, sizeof path, "%s/runs/%s/%s_mix.wav", a->dir, run, run);
    wb_audio audio;
    wb_error e = { 0 };
    if (!wb_audio_load(path, &audio, &e)) {
        app_say(a, true, "%s", e.message);
        return false;
    }
    bool was_on = a->audio_on;
    if (a->live) wb_live_stop(a->live);
    free(a->take[index]);
    a->take[index] = audio.sample;
    a->take_frames[index] = audio.frames;
    snprintf(a->take_run[index], sizeof a->take_run[index], "%s", run);
    /* A/B at matched loudness (prepare_hurt_listening.py --reference); one
     * take alone plays at its listening-copy gain. */
    wb_audio x = { .sample = a->take[0], .frames = a->take_frames[0], .rate = 48000, .channels = 2 };
    wb_audio y = { .sample = a->take[1], .frames = a->take_frames[1], .rate = 48000, .channels = 2 };
    if (a->take[0] && a->take[1] && wb_ab_gains(&x, &y, &a->take_gain[0], &a->take_gain[1])) {
        app_say(a, false, "A/B at matched RMS: A %+.2f dB, B %+.2f dB", 20 * log10(a->take_gain[0]),
                20 * log10(a->take_gain[1]));
    } else {
        wb_measure m;
        wb_measure_audio(audio.sample, audio.frames, audio.rate, &m);
        a->take_gain[index] = m.peak > 0 ? 0.70794576 / m.peak : 1;
    }
    a->audio_on = was_on;
    app_audio_reload(a);
    return true;
}

void app_refresh_cache_state(app *a) {
    a->cache_version = a->version;
    a->cache_state_valid = true;
    for (unsigned p = 0; p < WB_PARTS; p++) a->part_cached[p] = false;
    if (!a->session || !a->derived_ok) return;
    for (unsigned p = 0; p < a->session->part_count; p++) {
        char key[65];
        a->part_cached[p] = wb_cache_key(a->session, a->dir, a->derived, p, key) && wb_cache_valid(a->dir, key);
    }
}

/* Frozen parts summed into one preview take; live parts (at most four) go
 * on the rack and follow the derived score from the playhead. */
void app_play_arrangement(app *a) {
    if (!a->session || !a->derived_ok) { app_say(a, true, "no derived score"); return; }
    bool was_on = a->audio_on;
    if (a->live) wb_live_stop(a->live);
    wb_score_free(&a->arrangement);
    char midi[4400];
    wb_error e = { 0 };
    if (!wb_orch_write(a->derived, a->session, a->dir, midi, sizeof midi, &e)
        || !wb_score_load_midi(a->session, midi, &a->arrangement, &e)) {
        app_say(a, true, "arrangement: %s", e.message);
        return;
    }
    uint64_t frames = wb_render_length(a->session, &a->arrangement);
    free(a->preview);
    a->preview = calloc(2 * frames, sizeof *a->preview);
    a->preview_frames = frames;
    unsigned frozen = 0, stale = 0, live = 0;
    int8_t part_slot[WB_PARTS];
    for (unsigned p = 0; p < WB_PARTS; p++) part_slot[p] = -1;
    for (unsigned s = 0; s < WB_LIVE_SLOTS; s++) a->rack_part[s] = -1;
    for (unsigned p = 0; p < a->session->part_count && a->preview; p++) {
        if (a->part_live[p] && live < WB_LIVE_SLOTS) {
            a->rack_part[live] = (int)p;
            part_slot[p] = (int8_t)live++;
            continue;
        }
        char key[65];
        float *x;
        uint64_t n;
        if (!wb_cache_key(a->session, a->dir, a->derived, p, key) || !wb_cache_load(a->dir, key, &x, &n)) {
            stale++;
            continue;
        }
        for (uint64_t i = 0; i < 2 * (n < frames ? n : frames); i++) a->preview[i] += x[i];
        free(x);
        frozen++;
    }
    a->take[0] = nullptr;   /* the preview replaces take A while it plays */
    free(a->take[1]);
    a->take[1] = nullptr;
    a->take_run[0][0] = a->take_run[1][0] = 0;
    if (!a->live && !(a->live = wb_live_create(a->session->head.rate))) return;
    a->audio_on = true;
    app_audio_reload(a);
    if (a->live) {
        wb_live_stop(a->live);
        wb_live_set_take(a->live, 0, a->preview, a->preview_frames, 1.0f);
        wb_live_set_score(a->live, a->arrangement.event, a->arrangement.count, part_slot);
        const char *periods = getenv("WB_AUDIO_PERIODS");
        if (!wb_live_start(a->live, a->audio_device, 128, periods && atoi(periods) >= 2 ? (unsigned)atoi(periods) : 3)) {
            app_say(a, true, "audio: %s", a->live->error);
            a->audio_on = was_on && false;
            return;
        }
        double bar = atof(a->play_from);
        uint64_t at = wb_timemap_bar_frame(&a->arrangement.map, bar);
        app_audio_send(a, (wb_live_cmd){ .kind = WB_LIVE_LOCATE, .frame = at });
        app_audio_send(a, (wb_live_cmd){ .kind = WB_LIVE_PLAY });
    }
    app_say(a, stale != 0, "arrangement preview from bar %s: %u frozen, %u live, %u stale (muted; F freezes)",
            a->play_from, frozen, live, stale);
}

/* Computer keyboard as two octaves (tracker layout): Z..M lower, Q..P upper. */
static int key_offset(SDL_Scancode k, int *row) {
    static const SDL_Scancode LOWER[] = { SDL_SCANCODE_Z, SDL_SCANCODE_S, SDL_SCANCODE_X, SDL_SCANCODE_D,
        SDL_SCANCODE_C, SDL_SCANCODE_V, SDL_SCANCODE_G, SDL_SCANCODE_B, SDL_SCANCODE_H, SDL_SCANCODE_N,
        SDL_SCANCODE_J, SDL_SCANCODE_M, SDL_SCANCODE_COMMA, SDL_SCANCODE_L, SDL_SCANCODE_PERIOD,
        SDL_SCANCODE_SEMICOLON, SDL_SCANCODE_SLASH };
    static const SDL_Scancode UPPER[] = { SDL_SCANCODE_Q, SDL_SCANCODE_2, SDL_SCANCODE_W, SDL_SCANCODE_3,
        SDL_SCANCODE_E, SDL_SCANCODE_R, SDL_SCANCODE_5, SDL_SCANCODE_T, SDL_SCANCODE_6, SDL_SCANCODE_Y,
        SDL_SCANCODE_7, SDL_SCANCODE_U, SDL_SCANCODE_I, SDL_SCANCODE_9, SDL_SCANCODE_O, SDL_SCANCODE_0,
        SDL_SCANCODE_P };
    for (unsigned i = 0; i < sizeof LOWER / sizeof *LOWER; i++) if (LOWER[i] == k) { *row = 1; return (int)i; }
    for (unsigned i = 0; i < sizeof UPPER / sizeof *UPPER; i++) if (UPPER[i] == k) { *row = 2; return (int)i + 12; }
    return -1;
}

static bool playing_view(const app *a) {
    wb_view v = a->active[a->preset];
    return v == WB_VIEW_PLAY || v == WB_VIEW_PATCH;
}

static void release_all_keys(app *a) {
    for (unsigned k = 0; k < 512; k++) {
        for (unsigned s = 0; s < WB_LIVE_SLOTS; s++)
            if (a->key_slots[k] & (1u << s))
                app_audio_send(a, (wb_live_cmd){ .kind = WB_LIVE_NOTE_OFF, .slot = (uint8_t)s, .d0 = a->key_note[k] });
        a->key_slots[k] = 0;
    }
}

/* Key down/up of the musical keyboard; true when consumed. */
static bool play_key(app *a, const SDL_KeyboardEvent *k) {
    if (!a->audio_on || !playing_view(a) || k->repeat || (k->mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT)))
        return false;
    int row = 0, offset = key_offset(k->scancode, &row);
    if (offset < 0 || k->scancode >= 512) return false;
    if (!k->down) {
        for (unsigned s = 0; s < WB_LIVE_SLOTS; s++)
            if (a->key_slots[k->scancode] & (1u << s))
                app_audio_send(a, (wb_live_cmd){ .kind = WB_LIVE_NOTE_OFF, .slot = (uint8_t)s, .d0 = a->key_note[k->scancode] });
        a->key_slots[k->scancode] = 0;
        return true;
    }
    int note = 12 * (a->octave + 1) + offset;
    if (note < 0 || note > 127) return true;
    uint8_t slots = 0;
    bool rack = false;
    for (unsigned s = 0; s < WB_LIVE_SLOTS; s++) rack = rack || a->rack_part[s] >= 0;
    for (unsigned s = 0; s < WB_LIVE_SLOTS; s++) {
        bool zone = !rack ? s == 0 : a->rack_part[s] >= 0 && (a->rack_zone[s] == 0 || a->rack_zone[s] == row);
        if (!zone) continue;
        app_audio_send(a, (wb_live_cmd){ .kind = WB_LIVE_NOTE_ON, .slot = (uint8_t)s, .d0 = (uint8_t)note,
                                         .d1 = (uint8_t)a->velocity });
        slots |= (uint8_t)(1u << s);
    }
    a->key_note[k->scancode] = (uint8_t)note;
    a->key_slots[k->scancode] = slots;
    return true;
}

/* ---- frame --------------------------------------------------------------- */

static void place(struct nk_context *ctx, const char *name, wb_rect r) {
    nk_window_set_bounds(ctx, name, nk_rect(r.x, r.y, r.w, r.h));
}

static void draw_ribbon(app *a, struct nk_context *ctx, wb_rect r) {
    const wb_theme *t = app_theme(a);
    place(ctx, "ribbon", r);
    if (nk_begin(ctx, "ribbon", nk_rect(r.x, r.y, r.w, r.h), NK_WINDOW_NO_SCROLLBAR)) {
        char text[1400];
        nk_layout_row_begin(ctx, NK_STATIC, WB_ROW_HEIGHT, 40);
        nk_layout_row_push(ctx, 380);
        if (a->session && a->derived_ok)
            snprintf(text, sizeof text, "%s  bars %d..%d  %.1f BPM  derived %.8s", a->session->head.name,
                     a->session->form.start_bar, a->session->form.end_bar,
                     60e6 / a->derived->tempo[0].us_per_quarter, a->derived->sha256);
        else snprintf(text, sizeof text, "%s", a->session ? a->session->head.name : "no session");
        nk_label(ctx, text, NK_TEXT_LEFT);
        if (a->session) {
            const wb_form *f = &a->session->form;
            for (unsigned i = 0; i < f->section_count && i < 30; i++) {
                nk_layout_row_push(ctx, 8.0f * (float)strlen(f->section[i].name) + 12);
                if (nk_button_label(ctx, f->section[i].name)) {
                    snprintf(a->probe_from, sizeof a->probe_from, "%d", f->section[i].start_bar);
                    snprintf(a->probe_to, sizeof a->probe_to, "%d", f->section[i].end_bar);
                    a->roll_from_bar = (float)f->section[i].start_bar;
                    a->roll_bars = (float)(f->section[i].end_bar - f->section[i].start_bar);
                }
            }
        }
        nk_layout_row_end(ctx);
        nk_layout_row_begin(ctx, NK_STATIC, WB_ROW_HEIGHT, 16);
        for (unsigned p = 0; p < WB_PRESET_COUNT; p++) {
            const wb_preset *preset = wb_preset_at(p);
            snprintf(text, sizeof text, "F%u %s", p + 1, preset->name);
            nk_layout_row_push(ctx, 8.0f * (float)strlen(text) + 12);
            if (select_clicked(ctx, text, NK_TEXT_CENTERED, p == a->preset)) a->preset = p;
        }
        nk_layout_row_push(ctx, 90);
        if (select_clicked(ctx, a->audio_on ? "audio on" : "audio off", NK_TEXT_CENTERED, a->audio_on))
            app_audio_toggle(a);
        if (a->audio_on) {
            const wb_live_meters *m = &a->meters;
            float peak = m->peak_left > m->peak_right ? m->peak_left : m->peak_right;
            nk_layout_row_push(ctx, 430);
            snprintf(text, sizeof text, " %s peak %5.1f dB  DSP %.2f/%.2f ms (%2.0f%%, worst %.2f)  xruns %llu  oct %d vel %d",
                     m->playing ? "play" : "    ", peak > 0 ? 20 * log10f(peak) : -99.0f, m->budget_ms, m->period_ms,
                     m->period_ms > 0 ? 100 * m->budget_ms / m->period_ms : 0, m->budget_peak_ms,
                     (unsigned long long)m->xruns, a->octave, a->velocity);
            nk_label_colored(ctx, text, NK_TEXT_LEFT,
                             m->budget_ms > 0.8f * m->period_ms ? t->clip : t->dim);
            if (m->muted) {
                nk_layout_row_push(ctx, 150);
                if (nk_button_label(ctx, "MUTED - reset")) {
                    app_audio_send(a, (wb_live_cmd){ .kind = WB_LIVE_RESET_SAFETY });
                }
            }
        }
        nk_layout_row_push(ctx, 560);
        snprintf(text, sizeof text, "   %s  WB_HOME=%s", a->path[0] ? a->path : "(no session)", wb_home());
        nk_label_colored(ctx, text, NK_TEXT_LEFT, t->dim);
        nk_layout_row_push(ctx, 300);
        snprintf(text, sizeof text, "%s CPU %3.0f%%  RSS %5.1f MiB", wb_jobs_running(&a->jobs) ? "job running |" : "",
                 a->proc.cpu_percent, a->proc.rss_mib);
        nk_label_colored(ctx, text, NK_TEXT_RIGHT, a->proc.rss_mib > 512 ? t->steal_released : t->dim);
        nk_layout_row_end(ctx);
    }
    nk_end(ctx);
}

static struct nk_color swatch(unsigned i) {
    static const struct nk_color C[] = {
        { 0x6f, 0xa8, 0xdc, 0xff }, { 0xe6, 0x91, 0x38, 0xff }, { 0x93, 0xc4, 0x7d, 0xff },
        { 0xc2, 0x7b, 0xa0, 0xff }, { 0x8e, 0x7c, 0xc3, 0xff }, { 0xd5, 0xa6, 0xbd, 0xff },
        { 0x76, 0xa5, 0xaf, 0xff }, { 0xff, 0xd9, 0x66, 0xff }, { 0xb4, 0x5f, 0x06, 0xff },
        { 0x45, 0x81, 0x8e, 0xff }, { 0xa6, 0x4d, 0x79, 0xff }, { 0x6a, 0xa8, 0x4f, 0xff },
        { 0x3d, 0x85, 0xc6, 0xff }, { 0xcc, 0x41, 0x25, 0xff }, { 0x99, 0x99, 0x99, 0xff },
        { 0xf6, 0xb2, 0x6b, 0xff },
    };
    return C[i % (sizeof C / sizeof *C)];
}

static void add_part(app *a) {
    char id[32], path[4400], block[512];
    snprintf(id, sizeof id, "part%u", a->session->part_count + 1);
    ma_patch_document doc = { .value = ma_patch_tepih };
    snprintf(doc.name, sizeof doc.name, "%s", id);
    snprintf(path, sizeof path, "%s/patches/%s.mapatch", a->dir, id);
    ma_patch_error pe;
    if (!ma_patch_save(path, &doc, &pe)) {
        app_say(a, true, "%s: %s", path, pe.message);
        return;
    }
    snprintf(block, sizeof block, "[part]\nid=%s\npatch=patches/%s.mapatch\nstem=%s\n", id, id,
             a->session->stem_count ? a->session->stem[0].id : "mix");
    if (app_insert(a, WB_KIND_PART, a->session->part_count, block)) {
        a->part = (int)a->session->part_count - 1;
        app_inspect(a, WB_KIND_PART, a->part);
    }
}

static void draw_parts(app *a, struct nk_context *ctx, wb_rect r) {
    const wb_theme *t = app_theme(a);
    place(ctx, "parts", r);
    if (nk_begin(ctx, "parts", nk_rect(r.x, r.y, r.w, r.h), NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
        unsigned count = a->session ? a->session->part_count : 0;
        for (unsigned i = 0; i < count; i++) {
            const wb_part *p = &a->session->part[i];
            bool over = a->derived_ok && a->derived->part[i].held_peak * p->unison > p->limit;
            nk_layout_row_begin(ctx, NK_STATIC, WB_ROW_HEIGHT, 5);
            nk_layout_row_push(ctx, 10);
            struct nk_rect s;
            if (nk_widget(&s, ctx)) {
                s.y += 4;
                s.h -= 8;
                nk_fill_rect(nk_window_get_canvas(ctx), s, 0, swatch(i));
            }
            nk_layout_row_push(ctx, 96);
            if (select_clicked(ctx, p->id, NK_TEXT_LEFT, (int)i == a->part)) {
                a->part = (int)i;
                app_inspect(a, WB_KIND_PART, (int)i);
            }
            nk_layout_row_push(ctx, 46);
            nk_label_colored(ctx, p->engine == WB_PART_ORGAN ? "organ" : p->engine == WB_PART_EP73 ? "ep73" : "mamut",
                             NK_TEXT_LEFT, t->dim);
            nk_layout_row_push(ctx, 56);
            nk_label_colored(ctx, p->stem, NK_TEXT_LEFT, t->dim);
            nk_layout_row_push(ctx, 60);
            char text[32];
            if (!a->cache_state_valid || a->cache_version != a->version) app_refresh_cache_state(a);
            const char *state = a->part_live[i] ? "live" : a->part_cached[i] ? "frozen" : "stale";
            snprintf(text, sizeof text, "%s", over ? "over" : state);
            nk_label_colored(ctx, text, NK_TEXT_RIGHT,
                             over ? t->clip : a->part_live[i] ? t->live : a->part_cached[i] ? t->cached : t->stale);
            nk_layout_row_end(ctx);
        }
        if (a->session) {
            nk_layout_row_dynamic(ctx, WB_ROW_HEIGHT, 2);
            if (nk_button_label(ctx, "+ part")) add_part(a);
            if (nk_button_label(ctx, "inspect form")) app_inspect(a, WB_KIND_FORM, 0);
        }
    }
    nk_end(ctx);
}

static void draw_workspace(app *a, struct nk_context *ctx, wb_rect r) {
    const wb_preset *preset = wb_preset_at(a->preset);
    place(ctx, "workspace", r);
    if (nk_begin(ctx, "workspace", nk_rect(r.x, r.y, r.w, r.h), NK_WINDOW_BORDER)) {
        nk_layout_row_begin(ctx, NK_STATIC, WB_ROW_HEIGHT, WB_PRESET_TABS);
        for (unsigned i = 0; i < preset->tab_count; i++) {
            const char *name = wb_view_name(preset->tab[i]);
            nk_layout_row_push(ctx, 8.0f * (float)strlen(name) + 16);
            if (select_clicked(ctx, name, NK_TEXT_CENTERED, preset->tab[i] == a->active[a->preset]))
                a->active[a->preset] = preset->tab[i];
        }
        nk_layout_row_end(ctx);
        view_draw(a, ctx, a->active[a->preset], r.w - 16, r.h - 2 * WB_ROW_HEIGHT);
    }
    nk_end(ctx);
}

static void draw_inspector(app *a, struct nk_context *ctx, wb_rect r) {
    place(ctx, "inspector", r);
    if (nk_begin(ctx, "inspector", nk_rect(r.x, r.y, r.w, r.h), NK_WINDOW_BORDER | NK_WINDOW_TITLE))
        view_inspector(a, ctx);
    nk_end(ctx);
}

static void draw_console(app *a, struct nk_context *ctx, wb_rect r) {
    const wb_theme *t = app_theme(a);
    place(ctx, "console", r);
    nk_flags flags = NK_WINDOW_BORDER | (a->console_open ? 0 : NK_WINDOW_NO_SCROLLBAR);
    if (nk_begin(ctx, "console", nk_rect(r.x, r.y, r.w, r.h), flags)) {
        if (a->show_keys && a->console_open) {
            static const char *const KEYS[] = {
                "F1..F6 layouts  Ctrl+Tab next tab  J/K part  Ctrl+Z / Ctrl+Y undo / redo  Ctrl+R final render",
                "Ctrl+Shift+P probe the range  Tab A/B in Patch  0 whole form in Roll  `  console  ?  help  "
                "Ctrl+Shift+T theme  Ctrl+Q quit",
            };
            for (unsigned i = 0; i < 2; i++) {
                nk_layout_row_dynamic(ctx, WB_ROW_HEIGHT, 1);
                nk_label_colored(ctx, KEYS[i], NK_TEXT_LEFT, t->dim);
            }
        }
        unsigned first = a->console_open ? 0 : (a->console_count ? a->console_count - 1 : 0);
        for (unsigned i = first; i < a->console_count; i++) {
            nk_layout_row_dynamic(ctx, WB_ROW_HEIGHT, 1);
            nk_label_colored(ctx, a->console[i], NK_TEXT_LEFT, a->console_error[i] ? t->clip : t->text);
        }
    }
    nk_end(ctx);
}

static void draw_main(app *a) {
    struct nk_context *ctx = a->main.ctx;
    int w = 0, h = 0;
    SDL_GetWindowSize(a->main.window, &w, &h);
    wb_frame f = wb_frame_layout((float)w, (float)h, a->console_open);
    draw_ribbon(a, ctx, f.ribbon);
    draw_parts(a, ctx, f.parts);
    draw_workspace(a, ctx, f.workspace);
    draw_inspector(a, ctx, f.inspector);
    draw_console(a, ctx, f.console);
}

static void draw_second(app *a) {
    struct nk_context *ctx = a->second.ctx;
    int w = 0, h = 0;
    SDL_GetWindowSize(a->second.window, &w, &h);
    wb_view view = wb_preset_at(a->preset)->pinned;
    char title[64];
    snprintf(title, sizeof title, "pinned: %s", wb_view_name(view));
    place(ctx, "pinned", (wb_rect){ 0, 0, (float)w, (float)h });
    if (nk_begin_titled(ctx, "pinned", title, nk_rect(0, 0, (float)w, (float)h), NK_WINDOW_BORDER | NK_WINDOW_TITLE))
        view_draw(a, ctx, view, (float)w - 16, (float)h - 2 * WB_ROW_HEIGHT);
    nk_end(ctx);
}

static void present(app *a, ui_window *w) {
    struct nk_color bg = app_theme(a)->background;
    SDL_SetRenderDrawColor(w->renderer, bg.r, bg.g, bg.b, 0xff);
    SDL_RenderClear(w->renderer);
    nk_sdl_render(w->ctx, NK_ANTI_ALIASING_ON);
    SDL_RenderPresent(w->renderer);
}

static void frame(app *a) {
    draw_main(a);
    present(a, &a->main);
    if (a->has_second) {
        draw_second(a);
        present(a, &a->second);
    }
}

/* ---- windows ------------------------------------------------------------- */

typedef enum { PLACE_PLAIN, PLACE_MAXIMIZED, PLACE_FULLSCREEN } placement;

/* Windows are placed by display, not by coordinates: Wayland ignores client
 * positions but honours the display of a fullscreen window. */
static bool open_window(ui_window *w, const char *title, SDL_DisplayID display, SDL_Rect bounds, placement how) {
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, (Sint64)SDL_WINDOWPOS_CENTERED_DISPLAY(display));
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, (Sint64)SDL_WINDOWPOS_CENTERED_DISPLAY(display));
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, bounds.w);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, bounds.h);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_MAXIMIZED_BOOLEAN, how == PLACE_MAXIMIZED);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN, how == PLACE_FULLSCREEN);
    w->window = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    if (!w->window) return false;
    w->renderer = SDL_CreateRenderer(w->window, nullptr);
    if (!w->renderer) return false;
    SDL_SetRenderVSync(w->renderer, 1);
    w->ctx = nk_sdl_init(w->window, w->renderer, nk_sdl_allocator());
    w->id = SDL_GetWindowID(w->window);
    return w->ctx != nullptr;
}

static void close_window(ui_window *w) {
    if (w->ctx) nk_sdl_shutdown(w->ctx);
    if (w->renderer) SDL_DestroyRenderer(w->renderer);
    if (w->window) SDL_DestroyWindow(w->window);
    *w = (ui_window){ 0 };
}

static void style_window(app *a, ui_window *w) {
    wb_font_load(w->ctx, FONT_PIXELS, a->font_path, sizeof a->font_path);
    wb_theme_apply(w->ctx, app_theme(a));
}

/* ---- events -------------------------------------------------------------- */

static void cycle_tab(app *a, int step) {
    const wb_preset *p = wb_preset_at(a->preset);
    unsigned at = 0;
    while (at < p->tab_count && p->tab[at] != a->active[a->preset]) at++;
    at = (at + (unsigned)(step < 0 ? p->tab_count - 1 : 1)) % p->tab_count;
    a->active[a->preset] = p->tab[at];
}

static bool text_input_active(app *a) {
    return SDL_TextInputActive(a->main.window) || (a->has_second && SDL_TextInputActive(a->second.window));
}

static void swap_ab(app *a) {
    ma_patch_document x = a->patch;
    a->patch = a->patch_b;
    a->patch_b = x;
    char path[4400];
    snprintf(path, sizeof path, "%s/%s", a->dir, a->session->part[a->patch_part].patch);
    ma_patch_error pe;
    if (!ma_patch_save(path, &a->patch, &pe)) app_say(a, true, "%s: %s", path, pe.message);
}

static bool handle_key(app *a, const SDL_KeyboardEvent *k) {
    if (!text_input_active(a) && play_key(a, k)) return true;
    if (!k->down || k->repeat) return false;
    if (k->key == SDLK_ESCAPE && a->audio_on) {
        release_all_keys(a);
        app_audio_send(a, (wb_live_cmd){ .kind = WB_LIVE_PANIC });
        app_audio_send(a, (wb_live_cmd){ .kind = WB_LIVE_STOP });
        app_say(a, false, "panic");
        return true;
    }
    if (k->key == SDLK_F7 || k->key == SDLK_F8) {
        a->octave += k->key == SDLK_F7 ? -1 : 1;
        if (a->octave < 0) a->octave = 0;
        if (a->octave > 8) a->octave = 8;
        return true;
    }
    bool ctrl = k->mod & SDL_KMOD_CTRL, shift = k->mod & SDL_KMOD_SHIFT;
    if (k->key >= SDLK_F1 && k->key <= SDLK_F6) { a->preset = (unsigned)(k->key - SDLK_F1); return true; }
    if (ctrl && k->key == SDLK_Q) { a->quit = true; return true; }
    if (ctrl && k->key == SDLK_TAB) { cycle_tab(a, shift ? -1 : 1); return true; }
    if (ctrl && shift && k->key == SDLK_T) {
        a->theme = a->theme == WB_THEME_DARK ? WB_THEME_LIGHT : WB_THEME_DARK;
        wb_theme_apply(a->main.ctx, app_theme(a));
        if (a->has_second) wb_theme_apply(a->second.ctx, app_theme(a));
        return true;
    }
    if (a->session && ctrl && !shift && k->key == SDLK_Z) { undo(a, false); return true; }
    if (a->session && ctrl && (k->key == SDLK_Y || (shift && k->key == SDLK_Z))) { undo(a, true); return true; }
    if (a->session && ctrl && !shift && k->key == SDLK_R) {
        const char *args[] = { "render", a->path, "--label", a->render_label[0] ? a->render_label : "render", nullptr };
        app_job(a, "final render", args);
        return true;
    }
    if (a->session && ctrl && shift && k->key == SDLK_P) {
        const char *args[] = { "probe", a->path, a->probe_from, a->probe_to, "--preroll",
                               a->probe_preroll[0] ? a->probe_preroll : "4", nullptr };
        app_job(a, "probe", args);
        return true;
    }
    if (ctrl || (k->mod & SDL_KMOD_ALT) || text_input_active(a)) return false;
    if (k->key == SDLK_GRAVE) { a->console_open = !a->console_open; return true; }
    if (k->key == SDLK_SLASH && shift) {
        a->show_keys = !a->show_keys;
        a->console_open = a->show_keys || a->console_open;
        return true;
    }
    if (k->key == SDLK_TAB && a->active[a->preset] == WB_VIEW_LISTEN && a->take[0] && a->take[1]) {
        a->take_selected ^= 1u;
        app_audio_send(a, (wb_live_cmd){ .kind = WB_LIVE_SELECT_TAKE, .d0 = (uint8_t)a->take_selected });
        return true;
    }
    if (k->key == SDLK_TAB && a->active[a->preset] == WB_VIEW_PATCH && a->patch_b_valid && a->patch_part >= 0) {
        swap_ab(a);
        return true;
    }
    if (k->key == SDLK_0 && a->session) { a->roll_bars = 0; return true; }
    if (k->key == SDLK_DELETE && a->session && a->roll_selected && a->active[a->preset] == WB_VIEW_ROLL) {
        char block[256];
        snprintf(block, sizeof block, "[overlay]\npart=%s\nop=delete\nbar=%.17g\nnote=%u\n",
                 a->session->part[a->roll_part].id, a->roll_bar, a->roll_note);
        if (app_insert(a, WB_KIND_OVERLAY, a->session->overlay_count, block)) a->roll_selected = false;
        return true;
    }
    if (a->session && a->part >= 0 && (k->key == SDLK_F || k->key == SDLK_U) && !playing_view(a)) {
        if (k->key == SDLK_F) {
            a->part_live[a->part] = false;
            const char *args[] = { "cache", a->path, a->session->part[a->part].id, nullptr };
            app_job(a, "freeze part", args);
        } else a->part_live[a->part] = true;
        return true;
    }
    unsigned parts = a->session ? a->session->part_count : 0;
    if (parts && (k->key == SDLK_J || k->key == SDLK_K)) {
        int step = k->key == SDLK_J ? 1 : -1;
        a->part = (a->part + step + (int)parts) % (int)parts;
        app_inspect(a, WB_KIND_PART, a->part);
        return true;
    }
    return false;
}

static void handle_event(app *a, SDL_Event *e) {
    if (e->type == SDL_EVENT_QUIT) { a->quit = true; return; }
    if (e->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        if (e->window.windowID == a->main.id) a->quit = true;
        else if (a->has_second && e->window.windowID == a->second.id) {
            close_window(&a->second);
            a->has_second = false;
        }
        return;
    }
    if ((e->type == SDL_EVENT_KEY_DOWN || e->type == SDL_EVENT_KEY_UP) && handle_key(a, &e->key)) return;
    if (e->type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        /* No stuck notes when the window loses focus. */
        release_all_keys(a);
    }
    SDL_Window *from = SDL_GetWindowFromEvent(e);
    SDL_WindowID target = from ? SDL_GetWindowID(from) : a->main.id;
    if (a->has_second && target == a->second.id) nk_sdl_handle_event(a->second.ctx, e);
    else nk_sdl_handle_event(a->main.ctx, e);
}

static void input_begin(app *a) {
    nk_input_begin(a->main.ctx);
    if (a->has_second) nk_input_begin(a->second.ctx);
}

static void input_end(app *a) {
    if (a->has_second) nk_input_end(a->second.ctx);
    nk_input_end(a->main.ctx);
}

/* Finished jobs change files the UI shows. */
static void after_jobs(app *a) {
    if (!a->jobs.finished) return;
    a->jobs.finished = false;
    for (unsigned i = 0; i < a->jobs.line_count; i++) {
        const char *l = a->jobs.line[i];
        if (!strncmp(l, "< ", 2) || strstr(l, "peak driver") || strstr(l, "suggestion") || strstr(l, "identical")
            || strstr(l, "listening copy"))
            app_say(a, strstr(l, "failed") || strstr(l, "DIFFERENT"), "%s", l);
    }
    app_reload_ledger(a);
    a->cache_state_valid = false;
    if (!strcmp(a->jobs.finished_label, "import into session") || !strcmp(a->jobs.finished_label, "add take as source"))
        app_load_session(a, a->path);
    else if (!strcmp(a->jobs.finished_label, "import as new session")) {
        /* The new session's path is in the job's output. */
        for (unsigned i = a->jobs.line_count; i-- > 0;) {
            const char *l = a->jobs.line[i];
            const char *at = strstr(l, ".mws: imported");
            if (at) {
                char path[1024];
                snprintf(path, sizeof path, "%.*s.mws", (int)(at - l), l);
                app_load_session(a, path);
                break;
            }
        }
    }
}

/* ---- smoke --------------------------------------------------------------- */

static int smoke(app *a) {
    unsigned frames = 0;
    size_t edits = a->journal.count;
    for (unsigned theme_pass = 0; theme_pass < 2; theme_pass++) {
        a->theme = theme_pass ? WB_THEME_LIGHT : WB_THEME_DARK;
        wb_theme_apply(a->main.ctx, app_theme(a));
        if (a->has_second) wb_theme_apply(a->second.ctx, app_theme(a));
        for (unsigned p = 0; p < WB_PRESET_COUNT; p++) {
            a->preset = p;
            const wb_preset *preset = wb_preset_at(p);
            for (unsigned i = 0; i < preset->tab_count; i++) {
                a->active[p] = preset->tab[i];
                a->console_open = i & 1u;
                a->show_keys = i == 1;
                /* Exercise the inspector on several object kinds. */
                static const wb_kind KINDS[] = { WB_KIND_PART, WB_KIND_RULE, WB_KIND_FORM, WB_KIND_LANE };
                if (a->session) app_inspect(a, KINDS[i % 4], 0);
                input_begin(a);
                input_end(a);
                frame(a);
                frames++;
            }
        }
    }
    /* Drawing without input must never edit the session. */
    if (a->journal.count != edits) {
        printf("workbench smoke: FAILED, %zu edits made without input\n", a->journal.count - edits);
        return 1;
    }
    printf("workbench smoke: %u frames, %s window(s), font %s, %s\n", frames, a->has_second ? "2" : "1",
           a->font_path[0] ? a->font_path : "(built-in)",
           a->session ? (a->derived_ok ? "derived score built" : a->derived_error) : "no session");
    return 0;
}

/* ---- main ---------------------------------------------------------------- */

int main(int argc, char **argv) {
    app *a = calloc(1, sizeof *a);
    if (!a) return 1;
    a->derived = calloc(1, sizeof *a->derived);
    a->plan = calloc(1, sizeof *a->plan);
    if (!a->derived || !a->plan) return 1;
    const char *path = nullptr;
    bool single = false, smoke_mode = false;
    a->part = -1;
    a->inspect_index = -1;
    a->inspected_index = -2;
    a->ledger_selected = a->ledger_compare = -1;
    a->patch_part = -1;
    a->import_drums = 10;
    snprintf(a->import_id, sizeof a->import_id, "src");
    snprintf(a->probe_preroll, sizeof a->probe_preroll, "4");
    snprintf(a->render_label, sizeof a->render_label, "render");
    snprintf(a->play_from, sizeof a->play_from, "0");
    const char *device = getenv("WB_AUDIO_DEVICE");
    snprintf(a->audio_device, sizeof a->audio_device, "%s", device && *device ? device : "default");
    a->velocity = 96;
    a->octave = 3;
    for (unsigned s = 0; s < WB_LIVE_SLOTS; s++) a->rack_part[s] = -1;
    wb_journal_init(&a->journal);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--single")) single = true;
        else if (!strcmp(argv[i], "--smoke")) smoke_mode = true;
        else if (!strcmp(argv[i], "--light")) a->theme = WB_THEME_LIGHT;
        else if (argv[i][0] != '-' && !path) path = argv[i];
        else {
            fprintf(stderr, "usage: workbench [FILE.mws] [--single] [--smoke] [--light]\n");
            return 2;
        }
    }
    /* The wb binary next to this one runs every job. */
    char self[PATH_MAX], wb_path[PATH_MAX + 8];
    ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
    self[n > 0 ? n : 0] = 0;
    char *slash = strrchr(self, '/');
    if (slash) *slash = 0;
    snprintf(wb_path, sizeof wb_path, "%s/wb", self);
    wb_jobs_init(&a->jobs, wb_path);
    for (unsigned p = 0; p < WB_PRESET_COUNT; p++) a->active[p] = wb_preset_at(p)->active;
    if (path) {
        /* Absolute, so jobs and the window title are unambiguous. */
        char *real = realpath(path, nullptr);
        app_load_session(a, real ? real : path);
        free(real);
    }
    app_say(a, false, "session root %s", wb_home());

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "workbench: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    int display_count = 0;
    SDL_DisplayID *displays = SDL_GetDisplays(&display_count);
    SDL_Rect primary = { 0, 0, 2560, 1080 }, secondary = { 0, 0, 1920, 1080 };
    SDL_DisplayID primary_id = SDL_GetPrimaryDisplay(), secondary_id = 0;
    if (primary_id) SDL_GetDisplayBounds(primary_id, &primary);
    for (int i = 0; displays && i < display_count && !single; i++)
        if (displays[i] != primary_id) {
            secondary_id = displays[i];
            SDL_GetDisplayBounds(secondary_id, &secondary);
            break;
        }
    SDL_free(displays);
    char title[1400];
    snprintf(title, sizeof title, "tonewheel91 Workbench - %s - %s", a->session ? a->session->head.name : "no session",
             wb_home());
    int rc = 1;
    if (!open_window(&a->main, title, primary_id, primary, smoke_mode ? PLACE_PLAIN : PLACE_MAXIMIZED)) {
        fprintf(stderr, "workbench: window: %s\n", SDL_GetError());
        goto done;
    }
    style_window(a, &a->main);
    if (secondary_id || smoke_mode) {
        a->has_second = open_window(&a->second, "tonewheel91 Workbench - pinned",
                                    secondary_id ? secondary_id : primary_id, secondary,
                                    smoke_mode ? PLACE_PLAIN : PLACE_FULLSCREEN);
        if (a->has_second) style_window(a, &a->second);
    }
    if (smoke_mode) {
        rc = smoke(a);
        goto done;
    }
    Uint64 last_frame = 0, last_status = 0;
    int audition_part = -2;
    unsigned audition_generation = 0;
    frame(a);
    while (!a->quit) {
        SDL_Event e;
        bool got = SDL_WaitEventTimeout(&e, a->audio_on ? 33 : wb_jobs_running(&a->jobs) || a->jobs.count ? 100 : 500);
        input_begin(a);
        if (got) {
            handle_event(a, &e);
            while (SDL_PollEvent(&e)) handle_event(a, &e);
        }
        input_end(a);
        bool jobs = wb_jobs_poll(&a->jobs);
        if (a->audio_on) {
            /* The audition instrument follows the selected part and its file. */
            bool rack = false;
            for (unsigned s = 0; s < WB_LIVE_SLOTS; s++) rack = rack || a->rack_part[s] >= 0;
            if (!rack && (audition_part != a->part || audition_generation != a->audio_generation)) {
                audition_part = a->part;
                audition_generation = a->audio_generation;
                app_audio_reload(a);
            }
        }
        if (a->audio_on) {
            a->meters = wb_live_read(a->live);
            if (!wb_live_running(a->live) || !atomic_load(&a->live->running)) {
                app_say(a, true, "audio stopped (device lost or write error)");
                wb_live_stop(a->live);
                a->audio_on = false;
            }
            jobs = true;   /* meters redraw while audio runs */
        }
        after_jobs(a);
        Uint64 now = SDL_GetTicks();
        bool status = now - last_status >= 1000;
        if (status) {
            a->proc = wb_proc_sample();
            last_status = now;
        }
        if (!got && !status && !jobs) continue;
        if (now - last_frame < FRAME_MS) SDL_Delay((Uint32)(FRAME_MS - (now - last_frame)));
        frame(a);
        last_frame = SDL_GetTicks();
    }
    rc = 0;
done:
    wb_live_destroy(a->live);
    wb_score_free(&a->arrangement);
    free(a->preview);
    free(a->take_events);
    if (a->take[0] != a->preview) free(a->take[0]);
    free(a->take[1]);
    wb_jobs_shutdown(&a->jobs);
    if (a->has_second) close_window(&a->second);
    close_window(&a->main);
    SDL_Quit();
    free_sources(a);
    wb_derived_free(a->derived);
    free(a->derived);
    free(a->plan);
    wb_journal_free(&a->journal);
    free(a->session);
    free(a);
    return rc;
}
