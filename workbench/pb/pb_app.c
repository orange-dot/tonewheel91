/* Patchbench: a sound-design desk over the Mamut Analog voice
 * (docs/patchbench-plan.md).  Main loop, document, keys and layout. */
#define _GNU_SOURCE
#include "pb_app.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../desk/wb_hash.h"
#include "../desk/wb_root.h"
#include "pb_bounce.h"

#ifndef WB_REPO
#define WB_REPO "."
#endif

extern char **environ;

enum { FONT_PIXELS = 13, RATE = 48000 };

static pb_undo undo_slot[PB_CHANNELS][2];

/* The session root, as the Workbench resolves it (wb_root.c wb_home); not
 * linked from there, since wb_root.o also carries the session checks and
 * their engine loaders. */
static const char *wb_home_dir(void) {
    const char *home = getenv("WB_HOME");
    return home && *home ? home : WB_HOME_DEFAULT;
}

/* ---- small services --------------------------------------------------------------- */

void pb_say(pb_app *a, bool error, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(a->status, sizeof a->status, format, args);
    va_end(args);
    a->status_error = error;
    if (a->smoke && error) fprintf(stderr, "patchbench: %s\n", a->status);
}

const wb_theme *pb_theme(const pb_app *a) { return wb_theme_get(a->theme); }
ma_patch *pb_authored(pb_app *a) { return &a->slot[a->current]; }
ma_rack_spec *pb_authored_rack(pb_app *a) { return &a->rack[a->current]; }

ma_rack_spec pb_audible_rack(const pb_app *a) {
    ma_rack_spec r = a->auditioning && a->library_open && a->library_selected >= 0 ? a->audition_rack
                   : a->rack[a->current];
    for (unsigned i = 0; i < MA_RACK_SLOTS; i++)
        if (r.slot[i].kind != MA_RACK_EMPTY && a->rack_mute[i]) r.slot[i].enabled = false;
    return r;
}

pb_channel *pb_channel_now(pb_app *a) { return &a->bench.channel[a->bench.selected]; }

ma_patch pb_channel_patch(const pb_app *a, unsigned c) {
    const pb_document *d = &a->parked[c];
    return c == a->bench.selected ? pb_audible(a) : d->slot[d->current];
}

ma_rack_spec pb_channel_rack(const pb_app *a, unsigned c) {
    const pb_document *d = &a->parked[c];
    return c == a->bench.selected ? pb_audible_rack(a) : d->rack[d->current];
}

const char *pb_channel_name(const pb_app *a, unsigned c) { return c == a->bench.selected ? a->name : a->parked[c].name; }
bool pb_channel_dirty(const pb_app *a, unsigned c) { return c == a->bench.selected ? a->dirty : a->parked[c].dirty; }

pb_sequence pb_audible_sequence(const pb_app *a) {
    pb_sequence s = pb_bench_sequence(&a->bench);
    bool solo = false;
    for (unsigned c = 0; c < PB_CHANNELS; c++) solo = solo || a->channel_solo[c];
    for (unsigned c = 0; solo && c < PB_CHANNELS; c++) s.mute[c] = !a->channel_solo[c];
    return s;
}

ma_patch pb_audible(const pb_app *a) {
    ma_patch base = a->slot[a->current];
    if (a->auditioning && a->library_open && a->library_selected >= 0) base = a->audition;
    else if (a->morph > 0 && a->bench.snapshot_used[a->morph_a] && a->bench.snapshot_used[a->morph_b])
        base = pb_morph(&a->bench.snapshot[a->morph_a], &a->bench.snapshot[a->morph_b], a->morph);
    return pb_derive(&base, &a->monitor);
}

void pb_begin_edit(pb_app *a, const char *label) { pb_undo_push(a->undo, pb_authored(a), pb_authored_rack(a), label); }
void pb_edited(pb_app *a) { a->dirty = true; }

static uint64_t now_ms(void) { return SDL_GetTicks(); }

static bool ends_with(const char *s, const char *tail) {
    size_t n = strlen(s), m = strlen(tail);
    return n >= m && !strcmp(s + n - m, tail);
}

static bool mkdirs(const char *path) {
    char p[PB_PATH_MAX];
    snprintf(p, sizeof p, "%s", path);
    for (char *at = p + 1; *at; at++)
        if (*at == '/') {
            *at = 0;
            if (mkdir(p, 0755) && errno != EEXIST) return false;
            *at = '/';
        }
    return !mkdir(p, 0755) || errno == EEXIST;
}

/* The session root's Git, one file per commit; nothing else is staged. */
static void commit_file(pb_app *a, const char *path, const char *message) {
    const char *home = wb_home_dir();
    char git[PB_PATH_MAX];
    snprintf(git, sizeof git, "%s/.git", home);
    struct stat st;
    size_t n = strlen(home);
    if (stat(git, &st) || strncmp(path, home, n) || path[n] != '/') return;
    const char *rel = path + n + 1;
    const char *add[] = { "git", "-C", home, "add", "--", rel, nullptr };
    const char *commit[] = { "git", "-C", home, "commit", "-q", "-m", message, "--", rel, nullptr };
    const char *const *steps[] = { add, commit };
    for (int i = 0; i < 2; i++) {
        pid_t pid;
        if (posix_spawnp(&pid, "git", nullptr, nullptr, (char *const *)steps[i], environ)) return;
        int status = 0;
        waitpid(pid, &status, 0);
        if (!WIFEXITED(status) || WEXITSTATUS(status)) {
            if (i) pb_say(a, false, "saved %s (nothing new to commit)", rel);
            return;
        }
    }
}

/* ---- notes ---------------------------------------------------------------------------- */

void pb_note(pb_app *a, uint8_t note, bool on) {
    if (note > 127) return;
    if (on && !a->bench.probe.pinned) a->bench.probe.note = note;
    if (on && a->latch && a->sounding[note]) on = false;   /* latch: a second press releases */
    a->sounding[note] = on;
    if (a->engine)
        pb_engine_send(a->engine, (pb_event){ .kind = on ? PB_EV_NOTE_ON : PB_EV_NOTE_OFF, .note = note,
                                              .velocity = (uint8_t)a->velocity, .channel = a->bench.selected });
}

static void release_all(pb_app *a) {
    for (int n = 0; n < 128; n++)
        if (a->sounding[n]) {
            a->sounding[n] = false;
            if (a->engine)
                pb_engine_send(a->engine, (pb_event){ .kind = PB_EV_NOTE_OFF, .note = (uint8_t)n,
                                                      .channel = a->bench.selected });
        }
    memset(a->key_down, 0, sizeof a->key_down);
}

/* ---- channels ---------------------------------------------------------------------- */

static pb_document desk_document(const pb_app *a) {
    pb_document d = { .chain = a->chain, .factory = a->factory, .parent = a->parent, .has_parent = a->has_parent,
                      .slot = { a->slot[0], a->slot[1] }, .slot_valid = { a->slot_valid[0], a->slot_valid[1] },
                      .current = a->current, .dirty = a->dirty, .rack = { a->rack[0], a->rack[1] },
                      .parent_rack = a->parent_rack, .rack_next_id = a->rack_next_id };
    memcpy(d.path, a->path, sizeof d.path);
    memcpy(d.name, a->name, sizeof d.name);
    memcpy(d.parent_ref, a->parent_ref, sizeof d.parent_ref);
    return d;
}

/* Channel `c`'s parked document onto the desk; desk-only state clears. */
static void unpark(pb_app *a, unsigned c) {
    const pb_document *d = &a->parked[c];
    memcpy(a->path, d->path, sizeof a->path);
    memcpy(a->name, d->name, sizeof a->name);
    memcpy(a->parent_ref, d->parent_ref, sizeof a->parent_ref);
    a->chain = d->chain;
    a->factory = d->factory;
    a->parent = d->parent;
    a->has_parent = d->has_parent;
    for (int k = 0; k < 2; k++) {
        a->slot[k] = d->slot[k];
        a->slot_valid[k] = d->slot_valid[k];
        a->rack[k] = d->rack[k];
    }
    a->current = d->current;
    a->dirty = d->dirty;
    a->parent_rack = d->parent_rack;
    a->rack_next_id = d->rack_next_id;
    a->bench.selected = (uint8_t)c;
    a->undo = &undo_slot[c][a->current];
    a->monitor = (pb_monitor){ 0 };
    memset(a->rack_mute, 0, sizeof a->rack_mute);
    a->rack_selected = -1;
    a->eq_open = false;
    a->eq_band = a->eq_drag = -1;
    a->morph = 0;
    a->auditioning = false;
    a->ui.editing = -1;
}

void pb_select_channel(pb_app *a, unsigned c) {
    if (c >= PB_CHANNELS || c == a->bench.selected) return;
    release_all(a);   /* held notes belong to the channel they started on */
    a->parked[a->bench.selected] = desk_document(a);
    unpark(a, c);
    if (a->engine) pb_engine_focus(a->engine, c);
    pb_say(a, false, "channel %u on the desk: %s%s", c + 1, a->name, a->path[0] ? "" : " (not saved)");
}

/* Two octaves on the computer keyboard (the Workbench's rows): Z..M, Q..P. */
static int key_offset(SDL_Scancode k) {
    static const SDL_Scancode LOWER[] = { SDL_SCANCODE_Z, SDL_SCANCODE_S, SDL_SCANCODE_X, SDL_SCANCODE_D,
        SDL_SCANCODE_C, SDL_SCANCODE_V, SDL_SCANCODE_G, SDL_SCANCODE_B, SDL_SCANCODE_H, SDL_SCANCODE_N,
        SDL_SCANCODE_J, SDL_SCANCODE_M, SDL_SCANCODE_COMMA, SDL_SCANCODE_L, SDL_SCANCODE_PERIOD,
        SDL_SCANCODE_SEMICOLON, SDL_SCANCODE_SLASH };
    static const SDL_Scancode UPPER[] = { SDL_SCANCODE_Q, SDL_SCANCODE_2, SDL_SCANCODE_W, SDL_SCANCODE_3,
        SDL_SCANCODE_E, SDL_SCANCODE_R, SDL_SCANCODE_5, SDL_SCANCODE_T, SDL_SCANCODE_6, SDL_SCANCODE_Y,
        SDL_SCANCODE_7, SDL_SCANCODE_U, SDL_SCANCODE_I, SDL_SCANCODE_9, SDL_SCANCODE_O, SDL_SCANCODE_0,
        SDL_SCANCODE_P };
    for (unsigned i = 0; i < sizeof LOWER / sizeof *LOWER; i++) if (LOWER[i] == k) return (int)i;
    for (unsigned i = 0; i < sizeof UPPER / sizeof *UPPER; i++) if (UPPER[i] == k) return (int)i + 12;
    return -1;
}

/* ---- document ---------------------------------------------------------------------- */

static void split(const char *path, char *dir, size_t dsize, const char **base) {
    snprintf(dir, dsize, "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = 0;
        *base = path + (slash - dir) + 1;
    } else {
        snprintf(dir, dsize, ".");
        *base = path;
    }
}

static void stem_of(const char *path, char *out, size_t size) {
    const char *b = strrchr(path, '/');
    snprintf(out, size, "%s", b ? b + 1 : path);
    char *dot = strrchr(out, '.');
    if (dot) *dot = 0;
}

void pb_open(pb_app *a, const char *path) {
    char dir[PB_PATH_MAX];
    const char *base;
    split(path, dir, sizeof dir, &base);
    ma_patch_document doc;
    wb_error e = { 0 };
    bool chain = wb_patch_is_chain(path);
    if (!wb_patch_load(dir, base, &doc, &e)) {
        pb_say(a, true, "%s: %s", path, e.message);
        return;
    }
    a->chain = chain;
    a->has_parent = false;
    a->parent_ref[0] = 0;
    a->parent_rack = (ma_rack_spec){ 0 };
    if (chain) {
        wb_patch_overrides ov;
        if (wb_patch_read_overrides(path, &ov, &e) && wb_patch_parent_rack(dir, &ov, &a->parent, &a->parent_rack, &e)) {
            a->has_parent = true;
            snprintf(a->parent_ref, sizeof a->parent_ref, "%s", ov.parent);
        } else pb_say(a, true, "parent of %s: %s", path, e.message);
    }
    char repo[PB_PATH_MAX];
    snprintf(repo, sizeof repo, "%s/patches/", WB_REPO);
    char *real = realpath(path, nullptr);
    snprintf(a->path, sizeof a->path, "%s", real ? real : path);
    free(real);
    a->factory = !strncmp(a->path, repo, strlen(repo));
    snprintf(a->name, sizeof a->name, "%s", doc.name);
    a->slot[0] = a->slot[1] = doc.value;
    a->rack[0] = a->rack[1] = doc.rack;
    memset(a->rack_mute, 0, sizeof a->rack_mute);
    a->rack_selected = -1;
    a->slot_valid[0] = true;
    a->slot_valid[1] = false;
    a->current = 0;
    a->undo = &undo_slot[a->bench.selected][0];
    pb_undo_clear(&undo_slot[a->bench.selected][0]);
    pb_undo_clear(&undo_slot[a->bench.selected][1]);
    a->dirty = false;
    snprintf(pb_channel_now(a)->patch, sizeof pb_channel_now(a)->patch, "%s", a->path);
    pb_say(a, false, "opened %s%s", a->path, a->factory ? " (factory: saves go to the library as a child)" : "");
}

/* A rack that differs from the parent's is written whole (rack=1). */
static bool write_overrides(pb_app *a, const char *path, const char *parent_ref, const ma_patch *parent,
                            const ma_rack_spec *parent_rack) {
    wb_patch_overrides ov = { 0 };
    snprintf(ov.name, sizeof ov.name, "%s", a->name);
    snprintf(ov.parent, sizeof ov.parent, "%s", parent_ref);
    if (memcmp(pb_authored_rack(a), parent_rack, sizeof *parent_rack)) {
        ov.has_rack = true;
        ov.rack = *pb_authored_rack(a);
    }
    ma_patch_field_info info;
    for (size_t i = 0; ma_patch_field_info_at(i, &info); i++) {
        double x = 0, y = 0;
        (void)ma_patch_field_get(pb_authored(a), i, &x);
        (void)ma_patch_field_get(parent, i, &y);
        if (x != y) wb_patch_override(&ov, (unsigned)i, x, false);
    }
    wb_error e = { 0 };
    if (wb_patch_write_overrides(path, &ov, &e)) return true;
    pb_say(a, true, "%s: %s", path, e.message);
    return false;
}

/* A free library path for `stem` with `ext`. */
static void library_path(pb_app *a, const char *stem, const char *ext, char *out, size_t size) {
    char clean[96];
    size_t n = 0;
    for (const char *s = stem; *s && n < sizeof clean - 1; s++)
        clean[n++] = (char)(((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || *s == '-') ? *s
                            : (*s >= 'A' && *s <= 'Z') ? *s + 32 : '-');
    clean[n] = 0;
    snprintf(out, size, "%s/%s%s", a->library, clean, ext);
    for (int k = 2; !access(out, F_OK) && k < 1000; k++) snprintf(out, size, "%s/%s-%d%s", a->library, clean, k, ext);
}

static bool in_library(const pb_app *a, const char *path) {
    size_t n = strlen(a->library);
    return !strncmp(path, a->library, n) && path[n] == '/' && !strchr(path + n + 1, '/');
}

void pb_save(pb_app *a) {
    mkdirs(a->library);
    bool ok = false;
    if (a->factory) {
        /* Factory files are read-only parents: the edit becomes a child. */
        char stem[64], path[PB_PATH_MAX], ref[96];
        stem_of(a->path, stem, sizeof stem);
        library_path(a, stem, ".wbpatch", path, sizeof path);
        snprintf(ref, sizeof ref, "factory:%s", stem);
        ma_patch parent = a->slot[a->current];
        ma_patch_document doc;
        wb_error e = { 0 };
        if (wb_patch_load(".", ref, &doc, &e)) parent = doc.value;
        ma_rack_spec none = { 0 };   /* a factory parent has no rack */
        if (!write_overrides(a, path, ref, &parent, &none)) return;
        snprintf(a->path, sizeof a->path, "%s", path);
        snprintf(a->parent_ref, sizeof a->parent_ref, "%s", ref);
        a->parent = parent;
        a->parent_rack = none;
        a->has_parent = a->chain = true;
        a->factory = false;
        ok = true;
    } else if (!a->path[0]) {
        char path[PB_PATH_MAX];
        library_path(a, a->name, ".mapatch", path, sizeof path);
        snprintf(a->path, sizeof a->path, "%s", path);
    }
    if (!ok && a->chain) ok = write_overrides(a, a->path, a->parent_ref, &a->parent, &a->parent_rack);
    else if (!ok) {
        ma_patch_document doc = { .value = *pb_authored(a), .rack = *pb_authored_rack(a) };
        snprintf(doc.name, sizeof doc.name, "%s", a->name);
        ma_patch_error e = { 0 };
        ok = ma_patch_save(a->path, &doc, &e);
        if (!ok) pb_say(a, true, "%s: %s", a->path, e.message);
    }
    if (!ok) return;
    a->dirty = false;
    snprintf(pb_channel_now(a)->patch, sizeof pb_channel_now(a)->patch, "%s", a->path);
    pb_error be = { 0 };
    if (!pb_bench_save(a->bench_path, &a->bench, &be)) pb_say(a, true, "%s", be.message);
    pb_say(a, false, "saved %s", a->path);
    char message[300];
    snprintf(message, sizeof message, "Patchbench: save %s", a->name);
    commit_file(a, a->path, message);
}

void pb_derive_child(pb_app *a) {
    mkdirs(a->library);
    char ref[PB_PATH_MAX], stem[64];
    stem_of(a->path[0] ? a->path : a->name, stem, sizeof stem);
    if (a->factory) snprintf(ref, sizeof ref, "factory:%s", stem);
    else if (a->path[0] && in_library(a, a->path) && !a->dirty) snprintf(ref, sizeof ref, "%s", strrchr(a->path, '/') + 1);
    else {
        /* Anything else first becomes a flat parent in the library. */
        char path[PB_PATH_MAX];
        library_path(a, stem, ".mapatch", path, sizeof path);
        ma_patch_document doc = { .value = *pb_authored(a), .rack = *pb_authored_rack(a) };
        snprintf(doc.name, sizeof doc.name, "%s", a->name);
        ma_patch_error e;
        if (!ma_patch_save(path, &doc, &e)) {
            pb_say(a, true, "%s: %s", path, e.message);
            return;
        }
        snprintf(ref, sizeof ref, "%s", strrchr(path, '/') + 1);
    }
    char child[PB_PATH_MAX], cstem[80];
    snprintf(cstem, sizeof cstem, "%s-child", stem);
    library_path(a, cstem, ".wbpatch", child, sizeof child);
    ma_patch parent = *pb_authored(a);
    ma_rack_spec parent_rack = *pb_authored_rack(a);   /* the child inherits it */
    if (!write_overrides(a, child, ref, &parent, &parent_rack)) return;
    pb_open(a, child);
    pb_say(a, false, "derived %s from %s", child, ref);
}

void pb_flatten(pb_app *a) {
    mkdirs(a->library);
    char stem[64], path[PB_PATH_MAX], flat[80];
    stem_of(a->path[0] ? a->path : a->name, stem, sizeof stem);
    snprintf(flat, sizeof flat, "%s-flat", stem);
    library_path(a, flat, ".mapatch", path, sizeof path);
    ma_patch_document doc = { .value = *pb_authored(a), .rack = *pb_authored_rack(a) };
    snprintf(doc.name, sizeof doc.name, "%s", a->name);
    ma_patch_error e;
    if (ma_patch_save(path, &doc, &e)) pb_say(a, false, "flattened to %s", path);
    else pb_say(a, true, "%s: %s", path, e.message);
}

void pb_bounce_now(pb_app *a) {
    char dir[PB_PATH_MAX + 16], stem[64], wav[PB_PATH_MAX + 200];
    snprintf(dir, sizeof dir, "%s/bounces", a->library);
    mkdirs(dir);
    stem_of(a->path[0] ? a->path : a->name, stem, sizeof stem);
    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    snprintf(wav, sizeof wav, "%s/%s-%04d%02d%02d-%02d%02d%02d.wav", dir, stem, tm.tm_year + 1900, tm.tm_mon + 1,
             tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    /* What the desk plays: every channel's audible patch, rack and voice. */
    ma_patch patch[PB_CHANNELS];
    ma_rack_spec rack[PB_CHANNELS];
    pb_voice voice[PB_CHANNELS];
    for (unsigned c = 0; c < PB_CHANNELS; c++) {
        patch[c] = pb_channel_patch(a, c);
        rack[c] = pb_channel_rack(a, c);
        voice[c] = a->bench.channel[c].voice;
    }
    pb_sequence sequence = pb_audible_sequence(a);
    float *pcm = nullptr;
    size_t frames = 0;
    if (!pb_bounce_render(a->rate, patch, rack, voice, &sequence, 2, 2.0f, &pcm, &frames)) {
        pb_say(a, true, "bounce failed");
        return;
    }
    char message[400];
    bool ok = pb_bounce_write(wav, a->rate, patch, rack, &a->bench, pcm, frames, message, sizeof message);
    pb_say(a, !ok, "bounce %s", message);
    free(pcm);
}

void pb_snapshot_store(pb_app *a, unsigned slot) {
    if (slot >= PB_SNAPSHOTS) return;
    a->bench.snapshot[slot] = *pb_authored(a);
    a->bench.snapshot_used[slot] = true;
    pb_say(a, false, "snapshot %u stored", slot + 1);
}

void pb_snapshot_recall(pb_app *a, unsigned slot) {
    if (slot >= PB_SNAPSHOTS || !a->bench.snapshot_used[slot]) {
        pb_say(a, true, "snapshot %u is empty (Ctrl+%u stores)", slot + 1, slot + 1);
        return;
    }
    pb_begin_edit(a, "snapshot recall");
    *pb_authored(a) = a->bench.snapshot[slot];
    pb_edited(a);
    pb_say(a, false, "snapshot %u recalled", slot + 1);
}

void pb_reset(pb_app *a) {
    pb_begin_edit(a, "reset");
    *pb_authored(a) = pb_init_patch();
    *pb_authored_rack(a) = (ma_rack_spec){ 0 };
    a->rack_selected = -1;
    a->eq_open = false;
    pb_channel_now(a)->voice = pb_voice_default();
    a->monitor = (pb_monitor){ 0 };
    pb_edited(a);
    pb_say(a, false, "reset to the neutral default (undo restores the patch and the rack; the voice settings stay reset)");
}

void pb_swap_ab(pb_app *a) {
    if (!a->slot_valid[1]) {
        a->slot[1] = a->slot[0];
        a->rack[1] = a->rack[0];
        a->slot_valid[1] = true;
    }
    a->current ^= 1u;
    a->undo = &undo_slot[a->bench.selected][a->current];
    pb_say(a, false, "slot %c", a->current ? 'B' : 'A');
}

/* On the audio thread: RealtimeKit directly (it reports the priority it
 * granted), else SDL's request, which goes the same way. */
static bool raise_priority(int *priority) {
    if (pb_rtkit_realtime(gettid(), priority)) return true;
    if (!SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_TIME_CRITICAL)) return false;
    *priority = 0;   /* granted, value unknown */
    return true;
}

/* While audio runs, the UI and the probe stay off the audio thread's core. */
static void place_threads(pb_app *a, bool live) {
    if (!a->live_pin || a->cpus.audio_cpu < 0) return;
    cpu_set_t all, rest;
    CPU_ZERO(&all);
    CPU_OR(&all, &a->cpus.audio_core, &a->cpus.others);
    (void)pb_cpu_worker(&a->cpus, &rest);
    /* With the voice-bank worker on, the UI and probe also leave its CPU. */
    const cpu_set_t *set = !live ? &all : a->live_worker ? &rest : &a->cpus.others;
    (void)pthread_setaffinity_np(pthread_self(), sizeof *set, set);
    (void)pthread_setaffinity_np(a->probe.thread, sizeof *set, set);
}

void pb_audio_toggle(pb_app *a) {
    if (!a->engine) return;
    if (a->audio_on) {
        pb_engine_stop(a->engine);
        a->audio_on = false;
        release_all(a);
        pb_power_hold_end(&a->power);
        place_threads(a, false);
        pb_say(a, false, "audio off");
        return;
    }
    a->engine->raise_priority = raise_priority;
    atomic_store(&a->engine->demoted, false);
    a->demotion_seen = false;
    a->overloads_seen = a->engine->overload_panics;
    a->engine->options.lock_memory = a->live_lock;
    a->engine->options.audio_cpu = a->live_pin ? a->cpus.audio_cpu : -1;
    cpu_set_t rest;
    int worker_cpu = pb_cpu_worker(&a->cpus, &rest);
    a->engine->options.worker = a->live_worker && a->cpus.audio_cpu >= 0;
    a->engine->options.worker_cpu = a->live_pin ? worker_cpu : -1;
    a->engine->options.worker_lead_us = getenv("PB_LIVE_WORKER_LEAD_US")
        ? (unsigned)atoi(getenv("PB_LIVE_WORKER_LEAD_US")) : 0;
    place_threads(a, true);
    bool held = a->live_hold && pb_power_hold_start(&a->power, "performance", "Patchbench live audio");
    const char *periods = getenv("WB_AUDIO_PERIODS");
    unsigned count = periods && atoi(periods) >= 2 ? (unsigned)atoi(periods) : 3;
    if (!pb_engine_start(a->engine, a->device, 128, count)) {
        pb_power_hold_end(&a->power);
        place_threads(a, false);
        pb_say(a, true, "audio: %s", a->engine->error);
        return;
    }
    a->audio_on = true;
    SDL_Delay(20);
    static const char *SCHED[] = { "normal priority (xruns likely)", "nice -10", "SCHED_FIFO", "real time via RealtimeKit" };
    pb_engine *e = a->engine;
    char pin[48] = "", lock[48] = "";
    if (atomic_load(&e->pinned)) snprintf(pin, sizeof pin, ", cpu %d alone", e->options.audio_cpu);
    if (e->options.lock_memory)
        snprintf(lock, sizeof lock, e->lock_failed ? ", memory lock FAILED" : ", %.1f MB locked",
                 (double)e->locked_bytes / (1 << 20));
    char worker[64] = "";
    if (e->parallel)
        snprintf(worker, sizeof worker, ", voices on 2 cores (worker cpu %d, RT %d)", e->options.worker_cpu,
                 atomic_load(&e->worker.priority));
    pb_say(a, atomic_load(&e->scheduling) == 0, "audio on: %s, %u Hz, %u-frame periods x %u, %s %d%s%s%s%s",
           a->device, a->rate, e->period, count, SCHED[atomic_load(&e->scheduling)], atomic_load(&e->priority), pin,
           lock, worker, held ? ", performance profile held" : "");
}

/* ---- library ---------------------------------------------------------------------- */

static int by_label(const void *x, const void *y) {
    return strcmp(((const pb_library_entry *)x)->label, ((const pb_library_entry *)y)->label);
}

static void scan_dir(pb_app *a, const char *dir, const char *prefix) {
    DIR *d = opendir(dir);
    if (!d) return;
    for (struct dirent *e; (e = readdir(d)) && a->library_count < PB_LIBRARY_MAX;) {
        if (e->d_name[0] == '.' || (!ends_with(e->d_name, ".mapatch") && !ends_with(e->d_name, ".wbpatch"))) continue;
        pb_library_entry *x = &a->library_list[a->library_count++];
        snprintf(x->path, sizeof x->path, "%s/%s", dir, e->d_name);
        snprintf(x->label, sizeof x->label, "%s%.120s", prefix, e->d_name);
    }
    closedir(d);
}

void pb_library_scan(pb_app *a) {
    if (!a->library_list) a->library_list = calloc(PB_LIBRARY_MAX, sizeof *a->library_list);
    if (!a->library_list) return;
    a->library_count = 0;
    a->library_selected = -1;
    char dir[PB_PATH_MAX];
    snprintf(dir, sizeof dir, "%s/patches/mamutanalog", WB_REPO);
    scan_dir(a, dir, "factory     ");
    scan_dir(a, a->library, "library     ");
    DIR *d = opendir(wb_home_dir());
    if (d) {
        for (struct dirent *e; (e = readdir(d));) {
            if (e->d_name[0] == '.' || !strcmp(e->d_name, "patchbench")) continue;
            char patches[PB_PATH_MAX], prefix[64];
            snprintf(patches, sizeof patches, "%s/%s/patches", wb_home_dir(), e->d_name);
            snprintf(prefix, sizeof prefix, "%-11.11s ", e->d_name);
            scan_dir(a, patches, prefix);
        }
        closedir(d);
    }
    qsort(a->library_list, a->library_count, sizeof *a->library_list, by_label);
}

/* ---- engine and probe plumbing ---------------------------------------------------- */

static bool audio_busy(void *arg) {
    pb_app *a = arg;
    if (!a->audio_on || !a->engine) return false;
    pb_meters m = pb_engine_read(a->engine);
    return m.period_ms > 0 && m.budget_ms > .8f * m.period_ms;
}

static void sync_engine(pb_app *a) {
    if (!a->engine) return;
    pb_engine_collect(a->engine);
    for (unsigned c = 0; c < PB_CHANNELS; c++) {
        ma_patch audible = pb_channel_patch(a, c);
        if (!a->sent_valid || memcmp(&audible, &a->sent_patch[c], sizeof audible)) {
            pb_engine_set_patch(a->engine, c, &audible);
            a->sent_patch[c] = audible;
        }
        ma_rack_spec rack = pb_channel_rack(a, c), chain;
        const pb_voice *voice = &a->bench.channel[c].voice;
        bool rack_new = !a->sent_valid || memcmp(&rack, &a->sent_rack[c], sizeof rack);
        bool voice_new = !a->sent_valid || memcmp(voice, &a->sent_voice[c], sizeof *voice);
        /* RK2: the voice's chorus and reverb are blocks of this rack. */
        if ((rack_new || voice_new) && !pb_rt_chain(&rack, voice, &chain))
            pb_say(a, true, "channel %u: the rack is full, so the voice's chorus or reverb does not play", c + 1);
        if (rack_new) {
            pb_engine_set_rack(a->engine, c, &rack);
            a->sent_rack[c] = rack;
        }
        if (voice_new && pb_engine_set_voice(a->engine, c, voice, &audible)) a->sent_voice[c] = *voice;
    }
    pb_sequence sequence = pb_audible_sequence(a);
    if (pb_sequence_valid(&sequence)
        && (!a->sent_valid || memcmp(&sequence, &a->sent_sequence, sizeof sequence))) {
        pb_engine_set_sequence(a->engine, &sequence);
        a->sent_sequence = sequence;
    }
    a->sent_valid = true;
    if (a->audio_on) {
        a->meters = pb_engine_read(a->engine);
        if (a->meters.overload_panics > a->overloads_seen) {
            a->overloads_seen = a->meters.overload_panics;
            pb_say(a, true, "CPU overload: the voices did not fit in the %.2f ms period, so every voice was released "
                   "(%llu time(s)). Lower a channel's voice limit or use a lighter patch.",
                   (double)a->meters.period_ms, (unsigned long long)a->overloads_seen);
        }
        if (a->meters.demoted && !a->demotion_seen) {
            a->demotion_seen = true;
            pb_say(a, true, "CPU overload: the audio thread used up its real-time allowance and runs at normal "
                   "priority now; restart audio (F12 twice) once the load is lower.");
        }
        if (!atomic_load(&a->engine->running)) {
            pb_engine_stop(a->engine);
            a->audio_on = false;
            pb_say(a, true, "audio stopped (device lost or write error)");
        }
    }
}

static void sync_probe(pb_app *a) {
    ma_patch audible = pb_audible(a);
    uint8_t note = a->bench.probe.note, velocity = a->bench.probe.velocity;
    if (!a->probe_posted || memcmp(&audible, &a->probed, sizeof audible) || note != a->probed_note
        || velocity != a->probed_velocity || a->compass_sheet != a->probed_compass) {
        pb_probe_request rq = { .patch = audible, .note = note, .velocity = velocity, .compass = a->compass_sheet };
        pb_probe_post(&a->probe, &rq);
        a->probed = audible;
        a->probed_note = note;
        a->probed_velocity = velocity;
        a->probed_compass = a->compass_sheet;
        a->probe_posted = true;
    }
    a->shown = pb_probe_acquire(&a->probe);
}

static void autosave(pb_app *a) {
    uint64_t now = now_ms();
    bool dirty = false;
    for (unsigned c = 0; c < PB_CHANNELS; c++) dirty = dirty || pb_channel_dirty(a, c);
    if (!dirty || now - a->last_autosave_ms < 15000) return;
    a->last_autosave_ms = now;
    char dir[PB_PATH_MAX + 16], path[PB_PATH_MAX + 120];
    snprintf(dir, sizeof dir, "%s/.autosave", a->library);
    mkdirs(dir);
    for (unsigned c = 0; c < PB_CHANNELS; c++) {
        if (!pb_channel_dirty(a, c)) continue;
        pb_document d = c == a->bench.selected ? desk_document(a) : a->parked[c];
        char stem[64];
        stem_of(d.path[0] ? d.path : d.name, stem, sizeof stem);
        snprintf(path, sizeof path, "%s/ch%u-%s.mapatch", dir, c + 1, stem);
        ma_patch_document doc = { .value = d.slot[d.current], .rack = d.rack[d.current] };
        snprintf(doc.name, sizeof doc.name, "%s", d.name);
        ma_patch_error e;
        if (!ma_patch_save(path, &doc, &e)) pb_say(a, true, "autosave: %s", e.message);
    }
    snprintf(path, sizeof path, "%s/bench.pbench", dir);
    pb_error be;
    (void)pb_bench_save(path, &a->bench, &be);
}

/* ---- keys ---------------------------------------------------------------------------- */

static bool text_active(pb_app *a) {
    return SDL_TextInputActive(a->main.window) || a->ui.editing >= 0 || a->find_open;
}

static bool handle_key(pb_app *a, const SDL_KeyboardEvent *k) {
    bool ctrl = k->mod & SDL_KMOD_CTRL, shift = k->mod & SDL_KMOD_SHIFT, alt = k->mod & SDL_KMOD_ALT;
    if (k->key == SDLK_ESCAPE && k->down) {
        if (a->ui.editing >= 0) a->ui.editing = -1;
        else if (a->find_open) a->find_open = false;
        else if (a->library_open) a->library_open = a->auditioning = false;
        else if (!pb_eq_key(a, false, true)) {   /* Esc closes the EQ editor first */
            release_all(a);
            if (a->engine) pb_engine_send(a->engine, (pb_event){ .kind = PB_EV_PANIC });
            pb_say(a, false, "panic");
        }
        return true;
    }
    if (text_active(a)) return false;
    /* The musical keyboard. */
    int offset = key_offset(k->scancode);
    if (offset >= 0 && !ctrl && !alt && k->scancode < 512) {
        if (k->repeat) return true;
        if (k->down) {
            int note = 12 * (a->octave + 1) + offset;
            if (note < 0 || note > 127) return true;
            a->key_note[k->scancode] = (uint8_t)note;
            a->key_down[k->scancode] = true;
            pb_note(a, (uint8_t)note, true);
        } else if (a->key_down[k->scancode]) {
            a->key_down[k->scancode] = false;
            if (!a->latch) pb_note(a, a->key_note[k->scancode], false);
        }
        return true;
    }
    if (!k->down) return false;
    if (k->key == SDLK_DELETE && !ctrl && !alt && pb_eq_key(a, true, false)) return true;
    if (ctrl && k->key >= SDLK_1 && k->key <= SDLK_8) { pb_snapshot_store(a, (unsigned)(k->key - SDLK_1)); return true; }
    if (alt && k->key >= SDLK_1 && k->key <= SDLK_8) { pb_snapshot_recall(a, (unsigned)(k->key - SDLK_1)); return true; }
    if (ctrl && k->key == SDLK_Q) { a->quit = true; return true; }
    if (ctrl && k->key == SDLK_S) { pb_save(a); return true; }
    if (ctrl && k->key == SDLK_O) { a->library_open = !a->library_open; if (a->library_open) pb_library_scan(a); return true; }
    if (ctrl && k->key == SDLK_F) { a->find_open = true; a->find_length = 0; a->find[0] = 0; return true; }
    if (ctrl && k->key == SDLK_B) { pb_bounce_now(a); return true; }
    if (ctrl && shift && k->key == SDLK_T) {
        a->theme = a->theme == WB_THEME_DARK ? WB_THEME_LIGHT : WB_THEME_DARK;
        wb_theme_apply(a->main.ctx, pb_theme(a));
        return true;
    }
    if (ctrl && (k->key == SDLK_Y || (shift && k->key == SDLK_Z))) {
        if (pb_undo_redo(a->undo, pb_authored(a), pb_authored_rack(a))) pb_edited(a);
        return true;
    }
    if (ctrl && k->key == SDLK_Z) {
        if (pb_undo_undo(a->undo, pb_authored(a), pb_authored_rack(a))) pb_edited(a);
        return true;
    }
    if (ctrl || alt) return false;
    switch (k->key) {
    case SDLK_SPACE:
        if (a->engine) pb_engine_send(a->engine, (pb_event){ .kind = a->meters.seq_playing ? PB_EV_SEQ_STOP : PB_EV_SEQ_PLAY });
        if (!a->audio_on) pb_say(a, false, "the sequencer runs when audio is on (audio button)");
        return true;
    case SDLK_TAB: pb_swap_ab(a); return true;
    case SDLK_F1:
    case SDLK_F2:
    case SDLK_F3:
    case SDLK_F4: a->channel_request = (int)(k->key - SDLK_F1); return true;
    case SDLK_F7: if (a->octave > 0) a->octave--; return true;
    case SDLK_F8: if (a->octave < 8) a->octave++; return true;
    case SDLK_F12: pb_audio_toggle(a); return true;
    case SDLK_UP: if (shift) a->ui.key_coarse++; else a->ui.key_steps++; return true;
    case SDLK_DOWN: if (shift) a->ui.key_coarse--; else a->ui.key_steps--; return true;
    case SDLK_LEFT: pb_ui_move_focus(&a->ui, -1, false); return true;
    case SDLK_RIGHT: pb_ui_move_focus(&a->ui, 1, false); return true;
    case SDLK_LEFTBRACKET: pb_ui_move_focus(&a->ui, -1, true); return true;
    case SDLK_RIGHTBRACKET: pb_ui_move_focus(&a->ui, 1, true); return true;
    case SDLK_RETURN: a->ui.key_enter = true; return true;
    case SDLK_BACKSPACE: a->ui.key_reset = true; return true;
    default: return false;
    }
}

/* ---- frame --------------------------------------------------------------------------- */

static void layout(pb_app *a, struct nk_context *ctx, float w, float h) {
    /* RK1: the insert rack takes a row between the macros and the bottom. */
    float bar = 32, status = 24, macros = 90, rack = h >= 1000 ? 150 : h * .14f,
          bottom = h >= 1000 ? 220 : h * .21f;
    float row = (h - bar - status - macros - rack - bottom) / 2;
    pb_draw_bar(a, ctx, nk_rect(0, 0, w, bar));
    if (a->eq_open) pb_draw_eq(a, ctx, nk_rect(0, bar, w, 2 * row));
    if (!a->eq_open) {
        static const pb_module SOURCES[] = { PB_MOD_VCO1, PB_MOD_VCO2, PB_MOD_SYNC, PB_MOD_RASTER, PB_MOD_MOZAIK, PB_MOD_MIXER };
        for (int i = 0; i < 6; i++) pb_draw_module(a, ctx, SOURCES[i], nk_rect(w / 6 * (float)i, bar, w / 6, row));
        /* Shaping: the filter takes two of seven units. */
        static const pb_module SHAPING[] = { PB_MOD_BCS, PB_MOD_FILTER, PB_MOD_GLAS, PB_MOD_AMP, PB_MOD_OUTPUT, PB_MOD_VOICE };
        float unit = w / 7, x = 0;
        for (int i = 0; i < 6; i++) {
            float mw = SHAPING[i] == PB_MOD_FILTER ? 2 * unit : unit;
            pb_draw_module(a, ctx, SHAPING[i], nk_rect(x, bar + row, mw, row));
            x += mw;
        }
    }
    pb_draw_macros(a, ctx, nk_rect(0, bar + 2 * row, w, macros));
    pb_draw_rack(a, ctx, nk_rect(0, bar + 2 * row + macros, w, rack));
    float y = bar + 2 * row + macros + rack;
    /* The channel strip beside the notes of the channel on the desk. */
    float strip = w >= 2200 ? 190 : 170, notes = w >= 2200 ? w * .40f : w * .42f;
    pb_draw_channels(a, ctx, nk_rect(0, y, strip, bottom));
    pb_draw_notes(a, ctx, nk_rect(strip, y, notes - strip, bottom));
    pb_draw_scope(a, ctx, nk_rect(notes, y, w * .62f - notes, bottom));
    pb_draw_spectrum(a, ctx, nk_rect(w * .62f, y, w * .28f, bottom));
    pb_draw_level(a, ctx, nk_rect(w * .90f, y, w * .10f, bottom));
    pb_draw_status(a, ctx, nk_rect(0, h - status, w, status));
    if (a->library_open) pb_draw_library(a, ctx, w, h);
    if (a->find_open) pb_draw_find(a, ctx, w, h);
}

/* Smoke: the next presented frame is also saved here (PB_SHOTS). */
static char shot_path[PB_PATH_MAX];

static void present(pb_app *a, pb_window *win) {
    struct nk_color bg = pb_theme(a)->background;
    SDL_SetRenderDrawColor(win->renderer, bg.r, bg.g, bg.b, 0xff);
    SDL_RenderClear(win->renderer);
    nk_sdl_render(win->ctx, NK_ANTI_ALIASING_ON);
    if (shot_path[0] && win == &a->main) {
        SDL_Surface *s = SDL_RenderReadPixels(win->renderer, nullptr);
        if (s) {
            SDL_SaveBMP(s, shot_path);
            SDL_DestroySurface(s);
        }
        shot_path[0] = 0;
    }
    SDL_RenderPresent(win->renderer);
}

static void frame(pb_app *a) {
    int w = 0, h = 0;
    SDL_GetWindowSize(a->main.window, &w, &h);
    if (a->channel_request >= 0) {
        pb_select_channel(a, (unsigned)a->channel_request);
        a->channel_request = -1;
    }
    pb_ui_begin(&a->ui, pb_theme(a));
    layout(a, a->main.ctx, (float)w, (float)h);
    present(a, &a->main);
}

/* ---- windows ------------------------------------------------------------------------ */

typedef enum { PLACE_PLAIN, PLACE_MAXIMIZED } placement;

static bool open_window(pb_window *w, const char *title, SDL_DisplayID display, int width, int height, placement how) {
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, (Sint64)SDL_WINDOWPOS_CENTERED_DISPLAY(display));
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, (Sint64)SDL_WINDOWPOS_CENTERED_DISPLAY(display));
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_MAXIMIZED_BOOLEAN, how == PLACE_MAXIMIZED);
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

static void close_window(pb_window *w) {
    if (w->ctx) nk_sdl_shutdown(w->ctx);
    if (w->renderer) SDL_DestroyRenderer(w->renderer);
    if (w->window) SDL_DestroyWindow(w->window);
    *w = (pb_window){ 0 };
}

static void style(pb_app *a, pb_window *w) {
    wb_font_load(w->ctx, FONT_PIXELS, a->font_path, sizeof a->font_path);
    wb_theme_apply(w->ctx, pb_theme(a));
}

static void handle_event(pb_app *a, SDL_Event *e) {
    if (e->type == SDL_EVENT_QUIT) { a->quit = true; return; }
    if (e->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        if (e->window.windowID == a->main.id) a->quit = true;
        return;
    }
    if ((e->type == SDL_EVENT_KEY_DOWN || e->type == SDL_EVENT_KEY_UP) && handle_key(a, &e->key)) return;
    if (e->type == SDL_EVENT_WINDOW_FOCUS_LOST && !a->latch) release_all(a);
    nk_sdl_handle_event(a->main.ctx, e);
}

static void input_begin(pb_app *a) {
    nk_input_begin(a->main.ctx);
}

static void input_end(pb_app *a) {
    nk_input_end(a->main.ctx);
}

/* ---- smoke -------------------------------------------------------------------------- */

static void wait_probe(pb_app *a, bool full) {
    for (int i = 0; i < 400; i++) {
        sync_probe(a);
        if (a->shown && (!full || (a->shown->note_ready && a->shown->response_ready
                                   && (!a->compass_sheet || a->shown->compass_ready))))
            return;
        SDL_Delay(10);
    }
}

/* The most vertices one frame of the main window converted: the backend's
 * 16-bit indices allow 65535; the smoke wants a margin. */
static unsigned max_vertices;

static void count_vertices(pb_app *a) {
    if (a->main.ctx->draw_list.vertex_count > max_vertices) max_vertices = a->main.ctx->draw_list.vertex_count;
}

static void smoke_frame(pb_app *a) {
    input_begin(a);
    input_end(a);
    sync_engine(a);
    sync_probe(a);
    frame(a);
    count_vertices(a);
}

static int smoke(pb_app *a) {
    char before[WB_SHA256_HEX + 1] = "", after[WB_SHA256_HEX + 1] = "";
    bool hashed = a->path[0] && wb_sha256_file_hex(a->path, before);
    ma_patch start = *pb_authored(a);
    /* RK1: a rack in place with the EQ selected, as if opened from a file,
     * so the rack row and its measured pictures draw in every pass. */
    ma_rack_spec *rack = pb_authored_rack(a);
    ma_rack_slot_init(&rack->slot[0], MA_RACK_EQ, 1);
    static const struct { ma_eq_type type; float hz, db, q; } BANDS[MA_EQ_BANDS] = {
        { MA_EQ_LOW_CUT, 40, 0, .7071f }, { MA_EQ_LOW_SHELF, 120, 3, .7071f }, { MA_EQ_BELL, 400, -4, 1.5f },
        { MA_EQ_NOTCH, 1200, 0, 6 }, { MA_EQ_BELL, 2500, 3, .8f }, { MA_EQ_BELL, 5000, -2, 2 },
        { MA_EQ_HIGH_SHELF, 9000, 2, .7071f }, { MA_EQ_HIGH_CUT, 18000, 0, .7071f },
    };
    rack->slot[0].param[MA_EQ_BAND_COUNT] = MA_EQ_BANDS;
    for (unsigned b = 0; b < MA_EQ_BANDS; b++) {
        rack->slot[0].param[MA_EQ_BAND(b, MA_EQ_TYPE)] = BANDS[b].type;
        rack->slot[0].param[MA_EQ_BAND(b, MA_EQ_FREQ_HZ)] = BANDS[b].hz;
        rack->slot[0].param[MA_EQ_BAND(b, MA_EQ_GAIN_DB)] = BANDS[b].db;
        rack->slot[0].param[MA_EQ_BAND(b, MA_EQ_Q)] = BANDS[b].q;
    }
    a->eq_band = 2;
    ma_rack_slot_init(&rack->slot[2], MA_RACK_UTILITY, 2);
    rack->slot[2].param[MA_UTILITY_GAIN_DB] = -3;
    a->rack_selected = 0;
    ma_rack_spec rack_start = *rack;
    unsigned frames = 0;
    static const int WIDTHS[] = { 1920, 2560 };
    wait_probe(a, true);
    for (int theme = 0; theme < 2; theme++) {
        a->theme = theme ? WB_THEME_LIGHT : WB_THEME_DARK;
        wb_theme_apply(a->main.ctx, pb_theme(a));
        for (int s = 0; s < 2; s++) {
            /* PB_SMOKE_HEIGHT: a maximized window is shorter than its display
             * (a 1080 display leaves about 1000). */
            const char *tall = getenv("PB_SMOKE_HEIGHT");
            int height = tall ? atoi(tall) : 1080;
            SDL_SetWindowSize(a->main.window, WIDTHS[s], height >= 600 && height <= 4320 ? height : 1080);
            SDL_SyncWindow(a->main.window);
            const char *shots = getenv("PB_SHOTS");
            for (int k = -1; k < PB_TAP_COUNT; k++) {
                if (shots && k == PB_TAP_BUS) {
                    snprintf(shot_path, sizeof shot_path, "%s/pb-%s-%d.bmp", shots, theme ? "light" : "dark", WIDTHS[s]);
                    smoke_frame(a);   /* the window size settles over one frame */
                }
                a->scope_tap = k;
                a->note_view = k == PB_TAP_OUTPUT;
                a->spectrum_hold = k == 3;
                smoke_frame(a);
                frames++;
            }
            /* The EQ editor in place of the module rows, on this size and theme. */
            if (shots) snprintf(shot_path, sizeof shot_path, "%s/pb-eq-%s-%d.bmp", shots, theme ? "light" : "dark", WIDTHS[s]);
            a->eq_open = true;
            smoke_frame(a);
            smoke_frame(a);
            a->eq_open = false;
            frames += 2;
            a->compass_sheet = true;
            wait_probe(a, true);
            a->library_open = true;
            pb_library_scan(a);
            smoke_frame(a);
            a->library_open = false;
            a->find_open = true;
            smoke_frame(a);
            a->find_open = false;
            a->compass_sheet = false;
            for (int m = 0; m < PB_SRC_COUNT; m++) {
                a->monitor.solo[m] = true;
                smoke_frame(a);
                a->monitor.solo[m] = false;
            }
            frames += 2 + PB_SRC_COUNT;
        }
    }
    /* RK2: every new kind in the rack, each selected once, so its picture
     * (the block's own output) and its controls draw; then the rack as it
     * was. */
    static const ma_rack_kind RK2_KINDS[] = { MA_RACK_CHORUS, MA_RACK_REVERB, MA_RACK_DELAY, MA_RACK_SATURATOR,
                                              MA_RACK_COMPRESSOR };
    unsigned vertices_before = max_vertices;
    for (unsigned k = 0; k < 5; k++) ma_rack_slot_init(&rack->slot[3 + k], RK2_KINDS[k], 10 + k);
    rack->slot[5].param[MA_DELAY_SYNC] = MA_DELAY_SYNC_8D;
    rack->slot[5].param[MA_DELAY_PINGPONG] = 1;
    bool rk2_ok = ma_rack_spec_valid(rack);
    a->rack_selected = -1;
    a->rack_insert = 2;                  /* the kind buttons for the empty second slot */
    smoke_frame(a);
    a->rack_insert = 0;
    for (unsigned k = 0; k < 5; k++) {
        a->rack_selected = 3 + (int)k;
        const char *shots = getenv("PB_SHOTS");
        if (shots) snprintf(shot_path, sizeof shot_path, "%s/pb-rack-%s.bmp", shots, ma_rack_kind_name(RK2_KINDS[k]));
        smoke_frame(a);
        smoke_frame(a);
        frames += 2;
    }
    *rack = rack_start;
    a->rack_selected = 0;
    smoke_frame(a);
    printf("patchbench smoke: at most %u vertices a frame before the RK2 kinds, %u with seven blocks in the rack\n",
           vertices_before, max_vertices);
    if (!rk2_ok) {
        printf("patchbench smoke: FAILED, the RK2 rack is not a valid spec\n");
        return 1;
    }
    /* Drawing without input must not edit. */
    if (a->dirty || memcmp(&start, pb_authored(a), sizeof start) || memcmp(&rack_start, rack, sizeof rack_start)) {
        printf("patchbench smoke: FAILED, the patch changed without input\n");
        return 1;
    }
    /* Channels: channel 2 goes on the desk, takes a factory patch, a bass
     * pattern and an edit; channel 3 is muted and channel 1 soloed.  Back on
     * channel 1 its patch, rack and history are as they were, the engine
     * has channel 2's patch, and nothing was written. */
    bool channels_ok = a->bench.selected == 0 && !strcmp(pb_channel_name(a, 1), "init");
    a->channel_request = 1;
    smoke_frame(a);
    ma_patch init = pb_init_patch();
    channels_ok = channels_ok && a->bench.selected == 1 && !memcmp(pb_authored(a), &init, sizeof init) && !a->dirty
               && !memcmp(&a->parked[0].slot[0], &start, sizeof start)
               && !memcmp(&a->parked[0].rack[0], &rack_start, sizeof rack_start);
    pb_open(a, WB_REPO "/patches/mamutanalog/dubina.mapatch");
    ma_patch second = *pb_authored(a);
    pb_channel_now(a)->pattern = pb_builtin_pattern(PB_PAT_BASS, 36, pb_channel_now(a)->pattern.bpm);
    pb_channel_now(a)->pattern.steps = 12;
    pb_channel_now(a)->voice.gain = 8;
    a->bench.channel[2].mute = true;
    a->channel_solo[0] = true;
    smoke_frame(a);
    pb_sequence heard = pb_audible_sequence(a);
    channels_ok = channels_ok && a->factory && second.filter_cutoff_hz == ma_patch_dubina.filter_cutoff_hz
               && !heard.mute[0] && heard.mute[1] && heard.mute[2] && heard.mute[3]
               && !memcmp(&a->sent_patch[1], &second, sizeof second) && !memcmp(&a->sent_patch[0], &start, sizeof start)
               && a->sent_voice[1].gain == 8 && a->sent_sequence.channel[1].steps == 12;
    a->channel_solo[0] = false;
    pb_begin_edit(a, "channel edit");
    pb_authored(a)->filter_cutoff_hz *= .5f;
    pb_edited(a);
    smoke_frame(a);
    channels_ok = channels_ok && pb_channel_dirty(a, 1) && !pb_channel_dirty(a, 0)
               && a->sent_patch[1].filter_cutoff_hz == second.filter_cutoff_hz * .5f;
    a->channel_request = 0;
    smoke_frame(a);
    heard = pb_audible_sequence(a);
    channels_ok = channels_ok && a->bench.selected == 0 && !a->dirty && a->parked[1].dirty
               && !memcmp(pb_authored(a), &start, sizeof start) && !memcmp(rack, &rack_start, sizeof rack_start)
               && a->parked[1].slot[0].filter_cutoff_hz == second.filter_cutoff_hz * .5f
               && !heard.mute[0] && !heard.mute[1] && heard.mute[2]
               && !pb_undo_undo(a->undo, pb_authored(a), pb_authored_rack(a));
    /* Channel 2's own history brings its patch back. */
    a->channel_request = 1;
    smoke_frame(a);
    channels_ok = channels_ok && pb_undo_undo(a->undo, pb_authored(a), pb_authored_rack(a))
               && !memcmp(pb_authored(a), &second, sizeof second);
    a->channel_request = 0;
    smoke_frame(a);
    frames += 6;
    /* Scripted drags: the filter picture (cutoff and resonance) and a
     * keyboard step on a focused field; each is one undo step. */
    int w = 0, h = 0;
    SDL_GetWindowSize(a->main.window, &w, &h);
    float bar = 32, row = ((float)h - 32 - 24 - 90 - 150 - 220) / 2, unit = (float)w / 7;
    float fx = unit + 2 * unit * .34f, fy = bar + row + 30 + row * .25f;
    float cutoff = pb_authored(a)->filter_cutoff_hz;
    struct nk_context *ctx = a->main.ctx;
    struct { float x, y; bool down; } script[] = {
        { fx, fy, false }, { fx, fy, true }, { fx + 20, fy + 5, true }, { fx + 60, fy + 10, true }, { fx + 60, fy + 10, false },
    };
    for (size_t i = 0; i < sizeof script / sizeof *script; i++) {
        nk_input_begin(ctx);
        nk_input_motion(ctx, (int)script[i].x, (int)script[i].y);
        nk_input_button(ctx, NK_BUTTON_LEFT, (int)script[i].x, (int)script[i].y, script[i].down);
        nk_input_end(ctx);
        sync_engine(a);
        frame(a);
        count_vertices(a);
        frames++;
    }
    bool dragged = pb_authored(a)->filter_cutoff_hz > cutoff;
    a->ui.focus = pb_field_index("vco1.pulse_width");
    a->ui.key_steps = 3;
    smoke_frame(a);
    bool stepped = fabsf(pb_authored(a)->vco1.pulse_width - (start.vco1.pulse_width + .03f)) < 1e-4f;
    /* A rack edit is one more undo step and must come back with the rest. */
    pb_begin_edit(a, "rack");
    rack->slot[0].param[MA_EQ_BAND(2, MA_EQ_GAIN_DB)] = 9;
    pb_edited(a);
    smoke_frame(a);
    /* Reset: the neutral patch, an empty rack and the default voice in one
     * step; a frame draws it, and one undo brings patch and rack back. */
    ma_patch before_reset = *pb_authored(a);
    ma_rack_spec rack_before_reset = *rack;
    pb_channel_now(a)->voice.chorus_mix = .5f;
    a->monitor.mute[0] = true;
    pb_reset(a);
    smoke_frame(a);
    ma_patch neutral = pb_init_patch();
    pb_voice voice_default = pb_voice_default();
    bool reset_ok = !memcmp(&neutral, pb_authored(a), sizeof neutral) && ma_rack_spec_empty(rack)
                 && !memcmp(&voice_default, &pb_channel_now(a)->voice, sizeof voice_default) && !a->monitor.mute[0]
                 && pb_undo_undo(a->undo, pb_authored(a), pb_authored_rack(a))
                 && !memcmp(&before_reset, pb_authored(a), sizeof before_reset)
                 && !memcmp(&rack_before_reset, rack, sizeof rack_before_reset);
    smoke_frame(a);
    unsigned undos = 0;
    while (pb_undo_undo(a->undo, pb_authored(a), pb_authored_rack(a))) undos++;
    bool restored = !memcmp(&start, pb_authored(a), sizeof start) && !memcmp(&rack_start, rack, sizeof rack_start);
    if (hashed) wb_sha256_file_hex(a->path, after);
    bool untouched = !hashed || !strcmp(before, after);
    printf("patchbench smoke: %u frames, 1 window, font %s; probe A %.1f ms, B %.1f ms; channels %s, drag %s, "
           "key step %s, reset %s, %u undo steps %s, file %s; at most %u vertices a frame (limit 65535, smoke gate "
           "50000)\n",
           frames, a->font_path[0] ? a->font_path : "(built-in)",
           a->shown ? a->shown->pass_ms : -1.0, a->shown ? a->shown->note_ms : -1.0, channels_ok ? "ok" : "FAILED",
           dragged ? "ok" : "FAILED",
           stepped ? "ok" : "FAILED", reset_ok ? "ok" : "FAILED", undos, restored ? "restore the patch" : "FAILED",
           untouched ? "unchanged" : "CHANGED", max_vertices);
    return channels_ok && dragged && stepped && reset_ok && restored && untouched && undos == 3 && max_vertices < 50000
        ? 0 : 1;
}

/* ---- main ---------------------------------------------------------------------------- */

int main(int argc, char **argv) {
    pb_app *a = calloc(1, sizeof *a);
    if (!a) return 1;
    const char *path = nullptr;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--smoke")) a->smoke = true;
        else if (!strcmp(argv[i], "--light")) a->theme = WB_THEME_LIGHT;
        else if (argv[i][0] != '-' && !path) path = argv[i];
        else {
            fprintf(stderr, "usage: patchbench [PATCH.mapatch|PATCH.wbpatch] [--smoke] [--light]\n");
            return 2;
        }
    }
    a->rate = RATE;
    a->octave = 3;
    a->velocity = 100;
    a->scope_tap = PB_TAP_VCO1;
    a->library_selected = -1;
    a->rack_selected = -1;
    a->eq_band = -1;
    a->eq_drag = -1;
    a->eq_range = 12;
    a->eq_analyzer = true;
    const char *v;
    a->live_lock = !(v = getenv("PB_LIVE_LOCK")) || strcmp(v, "0");
    a->live_pin = !(v = getenv("PB_LIVE_PIN")) || strcmp(v, "0");
    a->live_hold = !(v = getenv("PB_LIVE_HOLD")) || strcmp(v, "0");
    a->live_worker = !(v = getenv("PB_LIVE_WORKER")) || strcmp(v, "0");
    (void)pb_cpu_layout_read(&a->cpus);
    pb_ui_init(&a->ui);
    a->undo = &undo_slot[0][0];
    a->channel_request = -1;
    for (size_t i = 0; i <= PB_SPECTRUM / 2; i++) a->spectrum_peak[i] = -180;
    const char *device = getenv("WB_AUDIO_DEVICE");
    snprintf(a->device, sizeof a->device, "%s", device && *device ? device : "default");
    snprintf(a->library, sizeof a->library, "%s/patchbench", wb_home_dir());
    snprintf(a->bench_path, sizeof a->bench_path, "%s/patchbench.pbench", a->library);
    mkdirs(a->library);
    pb_bench_default(&a->bench);
    pb_error be = { 0 };
    if (!access(a->bench_path, R_OK) && !pb_bench_load(a->bench_path, &a->bench, &be)) {
        pb_say(a, true, "%s line %u: %s (defaults used)", a->bench_path, be.line, be.message);
        pb_bench_default(&a->bench);
    }
    if (a->smoke) pb_bench_default(&a->bench);
    /* Every channel starts on the init patch and opens the file the bench
     * names for it; a patch on the command line goes to the channel on the
     * desk. */
    unsigned desk = a->bench.selected;
    for (unsigned c = 0; c < PB_CHANNELS; c++) {
        pb_document *d = &a->parked[c];
        snprintf(d->name, sizeof d->name, "init");
        d->slot[0] = d->slot[1] = pb_init_patch();
        d->slot_valid[0] = true;
    }
    for (unsigned c = 0; c < PB_CHANNELS; c++) {
        unpark(a, c);
        const char *file = a->bench.channel[c].patch;
        if (path && c == desk) pb_open(a, path);
        else if (file[0] && !access(file, R_OK)) pb_open(a, file);
        a->parked[c] = desk_document(a);
    }
    unpark(a, desk);

    ma_patch patches[PB_CHANNELS];
    pb_voice voices[PB_CHANNELS];
    for (unsigned c = 0; c < PB_CHANNELS; c++) {
        patches[c] = pb_channel_patch(a, c);
        voices[c] = a->bench.channel[c].voice;
    }
    pb_sequence sequence = pb_audible_sequence(a);
    a->engine = pb_engine_create(a->rate, patches, voices, &sequence);
    if (!a->engine) pb_say(a, true, "engine: pool init failed");
    else pb_engine_focus(a->engine, desk);
    if (!pb_probe_start(&a->probe, a->rate)) pb_say(a, true, "probe thread failed");
    a->probe.busy = audio_busy;
    a->probe.busy_arg = a;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "patchbench: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    /* One window, on the largest display (by area); the primary display is
     * only the fallback, since it need not be the large one. */
    int count = 0;
    SDL_DisplayID *displays = SDL_GetDisplays(&count);
    SDL_DisplayID target = SDL_GetPrimaryDisplay();
    SDL_Rect pb = { 0, 0, 2560, 1080 };
    if (target) SDL_GetDisplayBounds(target, &pb);
    for (int i = 0; displays && i < count; i++) {
        SDL_Rect r;
        if (SDL_GetDisplayBounds(displays[i], &r) && (int64_t)r.w * r.h > (int64_t)pb.w * pb.h) {
            target = displays[i];
            pb = r;
        }
    }
    SDL_free(displays);
    int rc = 1;
    if (!open_window(&a->main, "tonewheel91 Patchbench", target, a->smoke ? 1920 : pb.w, a->smoke ? 1080 : pb.h,
                     a->smoke ? PLACE_PLAIN : PLACE_MAXIMIZED)) {
        fprintf(stderr, "patchbench: window: %s\n", SDL_GetError());
        goto done;
    }
    style(a, &a->main);
    if (a->smoke) {
        rc = smoke(a);
        goto done;
    }
    a->last_autosave_ms = now_ms();
    while (!a->quit) {
        SDL_Event e;
        bool busy = a->audio_on || (a->shown && !a->shown->components_ready) || a->ui.active >= 0;
        bool got = SDL_WaitEventTimeout(&e, busy ? 16 : 100);
        input_begin(a);
        if (got) {
            handle_event(a, &e);
            while (SDL_PollEvent(&e)) handle_event(a, &e);
        }
        input_end(a);
        sync_engine(a);
        sync_probe(a);
        autosave(a);
        frame(a);
    }
    rc = 0;
    if (!a->smoke) {
        pb_error se;
        if (!pb_bench_save(a->bench_path, &a->bench, &se)) fprintf(stderr, "patchbench: %s\n", se.message);
    }
done:
    pb_probe_stop(&a->probe);
    pb_engine_destroy(a->engine);
    close_window(&a->main);
    SDL_Quit();
    free(a->library_list);
    free(a);
    return rc;
}
