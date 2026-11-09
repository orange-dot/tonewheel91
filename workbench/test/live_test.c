/* Live engine tests without ALSA: the real-time process function driven
 * offline (plan §6, W7). */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "../../driver/ma_patch_file.h"
#include "../audio/wb_live.h"
#include "../desk/wb_format.h"
#include "../desk/wb_render.h"
#include "../desk/wb_take.h"
#include "../desk/wb_cache.h"
#include "../desk/wb_orch.h"
#include "../desk/wb_root.h"
/* RealtimeKit through SDL when it is installed; the soak runs without it
 * otherwise (and reports the scheduling it got). */
#if __has_include(<SDL3/SDL.h>)
#include <SDL3/SDL.h>
static bool raise_priority(void) { return SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_TIME_CRITICAL); }
#else
static bool raise_priority(void) { return false; }
#endif

static unsigned checks, failures;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); } } while (0)

static float peak(const float *x, size_t n) {
    float p = 0;
    for (size_t i = 0; i < n; i++) if (fabsf(x[i]) > p) p = fabsf(x[i]);
    return p;
}

static wb_live_cmd note(wb_live_kind kind, unsigned slot, unsigned n, unsigned v) {
    return (wb_live_cmd){ .kind = kind, .slot = (uint8_t)slot, .d0 = (uint8_t)n, .d1 = (uint8_t)v };
}

/* W12 live-play parity: one recorded take through the desk renderer
 * (render_midi compat, proven equal to render_midi for organ and ep73) and
 * through the live engine run in non-real time must give the same bits. */
static void parity(const char *dir, const char *engine, const char *patch, const char *gain, const char *tail,
                   const wb_take_event *events, size_t count) {
    char take[600], text[2048];
    snprintf(take, sizeof take, "%s/take-%s.mid", dir, engine);
    wb_take_event copy[64];
    memcpy(copy, events, count * sizeof *copy);
    wb_error e = { 0 };
    const char *names[4] = { "take" };
    CHECK(wb_take_write(take, copy, count, names, "parity", &e));
    snprintf(text, sizeof text,
             "mws=1\n[session]\nname=parity\ncompat=render_midi\n[source]\nid=s\npath=sources/s.mid\n"
             "sha256=e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\n"
             "[form]\nstart_bar=0\nend_bar=1\ntail_seconds=%s\nfade_in_ms=0\nfade_out_ms=0\n[stem]\nid=mix\n"
             "[part]\nid=x\nengine=%s\npatch=%s\nstem=mix\ngain=%s\nlimit=16\n", tail, engine, patch, gain);
    wb_session *s = malloc(sizeof *s);
    CHECK(wb_read(text, strlen(text), s, &e));
    wb_score score;
    bool loaded = wb_score_load_midi(s, take, &score, &e);
    CHECK(loaded);
    if (!loaded) { fprintf(stderr, "  %s\n", e.message); free(s); return; }
    wb_render_report *report = calloc(1, sizeof *report);
    wb_render_options options = { 0 };
    bool rendered = wb_render(s, dir, &score, &options, report, &e);
    CHECK(rendered);
    if (!rendered) fprintf(stderr, "  %s: %s\n", engine, e.message);
    /* The live engine, fed the same events at the same frames. */
    wb_live *live = wb_live_create(48000);
    CHECK(wb_live_load_part(live, 0, s, dir, 0, &e));
    int8_t map[WB_PARTS];
    for (unsigned p = 0; p < WB_PARTS; p++) map[p] = -1;
    map[0] = 0;
    wb_live_set_score(live, score.event, score.count, map);
    wb_live_send(live, (wb_live_cmd){ .kind = WB_LIVE_PLAY });
    uint64_t total = report->total_frames, hash = 0;
    float buf[2 * 128];
    for (uint64_t at = 0; at < total;) {
        size_t n = total - at < 128 ? (size_t)(total - at) : 128;
        wb_live_process(live, buf, n);
        hash = tw_fnv1a64(buf, 2 * n * sizeof(float), hash);
        at += n;
    }
    bool same = rendered && hash == report->level[1].hash;
    CHECK(same);
    printf("live parity %-6s %zu events, %llu frames: desk %016llx live %016llx %s\n", engine, score.count,
           (unsigned long long)total, (unsigned long long)(rendered ? report->level[1].hash : 0),
           (unsigned long long)hash, same ? "identical" : "DIFFERENT");
    wb_live_destroy(live);
    wb_score_free(&score);
    free(report);
    free(s);
}

int main(void) {
    char dir[] = "/tmp/wb-live-XXXXXX";
    CHECK(mkdtemp(dir) != nullptr);
    char path[512];
    snprintf(path, sizeof path, "%s/patches", dir);
    mkdir(path, 0755);
    ma_patch_document doc = { .value = ma_patch_tepih };
    snprintf(doc.name, sizeof doc.name, "p");
    snprintf(path, sizeof path, "%s/patches/p.mapatch", dir);
    ma_patch_error pe;
    CHECK(ma_patch_save(path, &doc, &pe));
    wb_twreg reg;
    wb_twreg_default(&reg);
    snprintf(reg.name, sizeof reg.name, "o");
    snprintf(path, sizeof path, "%s/patches/o.twreg", dir);
    wb_error e = { 0 };
    CHECK(wb_twreg_save(path, &reg, &e));
    static const char SESSION[] =
        "mws=1\n[session]\nname=live\n[source]\nid=s\npath=sources/s.mid\n"
        "sha256=e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\n"
        "[form]\nstart_bar=0\nend_bar=4\n[stem]\nid=mix\n"
        "[part]\nid=p\npatch=patches/p.mapatch\nstem=mix\nlimit=8\n"
        "[part]\nid=o\nengine=organ\npatch=patches/o.twreg\nstem=mix\ngain=0.125\n";
    wb_session *s = malloc(sizeof *s);
    CHECK(wb_read(SESSION, sizeof SESSION - 1, s, &e));

    wb_live *live = wb_live_create(48000);
    CHECK(live && wb_live_load_part(live, 0, s, dir, 0, &e) && wb_live_load_part(live, 1, s, dir, 1, &e));
    float out[2 * 4800];

    /* Silence until a note, sound while held, back toward silence after. */
    wb_live_process(live, out, 4800);
    CHECK(peak(out, sizeof out / sizeof *out) == 0);
    CHECK(wb_live_send(live, note(WB_LIVE_NOTE_ON, 0, 60, 100)));
    wb_live_process(live, out, 4800);
    float held = peak(out, sizeof out / sizeof *out);
    CHECK(held > 1e-4f);
    CHECK(wb_live_send(live, note(WB_LIVE_NOTE_OFF, 0, 60, 0)));
    for (unsigned i = 0; i < 100; i++) wb_live_process(live, out, 4800);
    CHECK(peak(out, sizeof out / sizeof *out) < held * 0.01f);

    /* The organ slot through its MIDI map, and panic silences it. */
    CHECK(wb_live_send(live, note(WB_LIVE_NOTE_ON, 1, 64, 100)));
    wb_live_process(live, out, 4800);
    CHECK(peak(out, sizeof out / sizeof *out) > 1e-4f);
    CHECK(wb_live_send(live, (wb_live_cmd){ .kind = WB_LIVE_PANIC }));
    for (unsigned i = 0; i < 20; i++) wb_live_process(live, out, 4800);
    CHECK(peak(out, sizeof out / sizeof *out) < 1e-3f);

    /* The live tick equals the desk renderer's for the same Mamut part. */
    wb_live *a = wb_live_create(48000), *b = wb_live_create(48000);
    CHECK(wb_live_load_part(a, 0, s, dir, 0, &e) && wb_live_load_part(b, 0, s, dir, 0, &e));
    float x[2 * 512], y[2 * 512];
    wb_live_send(a, note(WB_LIVE_NOTE_ON, 0, 48, 90));
    wb_live_send(b, note(WB_LIVE_NOTE_ON, 0, 48, 90));
    wb_live_process(a, x, 512);
    wb_live_process(b, y, 512);
    CHECK(!memcmp(x, y, sizeof x));
    wb_live_destroy(a);
    wb_live_destroy(b);

    /* Score playback: a live part's notes at their frames, from the playhead. */
    {
        wb_live *sc = wb_live_create(48000);
        CHECK(wb_live_load_part(sc, 2, s, dir, 0, &e));
        static wb_note_event ev[2] = {
            { .frame = 4800, .part = 0, .note = 60, .velocity = 100, .on = true, .status = 0x90 },
            { .frame = 9600, .part = 0, .note = 60, .velocity = 0, .on = false, .status = 0x80 },
        };
        int8_t map[WB_PARTS];
        for (unsigned p = 0; p < WB_PARTS; p++) map[p] = -1;
        map[0] = 2;
        wb_live_set_score(sc, ev, 2, map);
        wb_live_send(sc, (wb_live_cmd){ .kind = WB_LIVE_PLAY });
        float buf[2 * 4800];
        wb_live_process(sc, buf, 4800);
        CHECK(peak(buf, 2 * 4800) == 0 && sc->score_next == 0);
        wb_live_process(sc, buf, 4800);
        CHECK(peak(buf, 2 * 4800) > 1e-4f && sc->score_next == 1);
        wb_live_send(sc, (wb_live_cmd){ .kind = WB_LIVE_LOCATE, .frame = 9000 });
        wb_live_process(sc, buf, 10);
        CHECK(sc->score_next == 1 && sc->position == 9010);
        wb_live_send(sc, (wb_live_cmd){ .kind = WB_LIVE_LOCATE, .frame = 0 });
        wb_live_process(sc, buf, 1);
        CHECK(sc->score_next == 0);
        wb_live_destroy(sc);
    }

    /* Playback: plays, stops at the end, A/B switches at the same position. */
    enum { TAKE = 9600 };
    float *t0 = calloc(2 * TAKE, sizeof *t0), *t1 = calloc(2 * TAKE, sizeof *t1);
    for (unsigned i = 0; i < 2 * TAKE; i++) { t0[i] = 0.25f; t1[i] = -0.5f; }
    wb_live_clear_slot(live, 0);
    wb_live_clear_slot(live, 1);
    wb_live_set_take(live, 0, t0, TAKE, 1.0f);
    wb_live_set_take(live, 1, t1, TAKE, 0.5f);
    wb_live_send(live, (wb_live_cmd){ .kind = WB_LIVE_PLAY });
    wb_live_process(live, out, 4800);
    CHECK(out[0] == 0.25f && out[9599] == 0.25f);
    wb_live_send(live, (wb_live_cmd){ .kind = WB_LIVE_SELECT_TAKE, .d0 = 1 });
    wb_live_process(live, out, 4800);
    CHECK(out[0] == -0.25f);
    wb_live_meters m = wb_live_read(live);
    (void)m;
    wb_live_process(live, out, 4800);   /* past the end */
    CHECK(out[0] == 0 && !live->playing);
    wb_live_send(live, (wb_live_cmd){ .kind = WB_LIVE_LOCATE, .frame = 100 });
    wb_live_send(live, (wb_live_cmd){ .kind = WB_LIVE_PLAY });
    wb_live_process(live, out, 10);
    CHECK(live->position == 110);

    /* Safety: more than 10 ms above 0 dBFS mutes, and stays muted. */
    for (unsigned i = 0; i < 2 * TAKE; i++) t0[i] = 2.0f;
    wb_live_send(live, (wb_live_cmd){ .kind = WB_LIVE_SELECT_TAKE, .d0 = 0 });
    wb_live_send(live, (wb_live_cmd){ .kind = WB_LIVE_LOCATE, .frame = 0 });
    wb_live_process(live, out, 1000);
    CHECK(live->muted && out[2 * 999] == 0 && out[0] == 2.0f);
    wb_live_send(live, (wb_live_cmd){ .kind = WB_LIVE_RESET_SAFETY });
    t0[2 * 2000] = NAN;
    wb_live_send(live, (wb_live_cmd){ .kind = WB_LIVE_LOCATE, .frame = 2000 });
    wb_live_process(live, out, 1);
    CHECK(live->muted && out[0] == 0);

    /* The command ring holds WB_LIVE_RING commands and refuses the next. */
    unsigned sent = 0;
    while (wb_live_send(live, (wb_live_cmd){ .kind = WB_LIVE_STOP })) sent++;
    CHECK(sent == WB_LIVE_RING);
    wb_live_process(live, out, 1);
    CHECK(wb_live_send(live, (wb_live_cmd){ .kind = WB_LIVE_STOP }));

    /* Meters publish consistently. */
    wb_live_publish(live, 0.5f, 2.667f, 3);
    m = wb_live_read(live);
    CHECK(m.budget_ms == 0.5f && m.xruns == 3 && m.muted);

    /* W12 live-play parity for the three engines. */
    {
        char ep_path[600];
        wb_epset set;
        wb_epset_default(&set);
        snprintf(set.name, sizeof set.name, "e");
        snprintf(ep_path, sizeof ep_path, "%s/patches/e.epset", dir);
        CHECK(wb_epset_save(ep_path, &set, &e));
        static const wb_take_event NOTES[] = {
            { 0, 0, 0x90, 60, 100 }, { 120000, 0, 0x90, 64, 90 }, { 250000, 0, 0x90, 67, 80 },
            { 600000, 0, 0x80, 60, 0 }, { 700000, 0, 0x80, 64, 0 }, { 900000, 0, 0x80, 67, 0 },
            { 1000000, 0, 0x90, 72, 110 }, { 1500000, 0, 0x80, 72, 0 },
        };
        wb_take_event organ[16], ep[16];
        size_t n = sizeof NOTES / sizeof *NOTES;
        memcpy(organ, NOTES, sizeof NOTES);
        memcpy(ep, NOTES, sizeof NOTES);
        /* Console controls on the organ: a drawbar, the rotary switch, swell. */
        organ[n] = (wb_take_event){ 300000, 0, 0xb0, 72, 100 };
        organ[n + 1] = (wb_take_event){ 400000, 0, 0xb0, 87, 127 };
        organ[n + 2] = (wb_take_event){ 800000, 0, 0xb0, 11, 64 };
        /* Sustain on the ep73. */
        ep[n] = (wb_take_event){ 550000, 0, 0xb0, 64, 127 };
        ep[n + 1] = (wb_take_event){ 950000, 0, 0xb0, 64, 0 };
        parity(dir, "organ", "patches/o.twreg", "0.125", "1", organ, n + 3);
        parity(dir, "ep73", "patches/e.epset", "0.125", "1", ep, n + 2);
        parity(dir, "mamut", "patches/p.mapatch", "1", "12", NOTES, n);
    }

    /* Optional: the real ALSA thread for one second of silence. */
    const char *device = getenv("WB_LIVE_DEVICE");
    if (device && *device) {
        wb_live *real = wb_live_create(48000);
        bool started = wb_live_start(real, device, 128, 3);
        CHECK(started);
        if (!started) fprintf(stderr, "  %s\n", real->error);
        struct timespec second = { 1, 0 };
        nanosleep(&second, nullptr);
        wb_live_meters rm = wb_live_read(real);
        wb_live_stop(real);
        CHECK(rm.periods > 300);
        printf("ALSA %s: %llu periods of %.3f ms in 1 s, DSP %.4f ms, xruns %llu\n", device,
               (unsigned long long)rm.periods, (double)rm.period_ms, (double)rm.budget_peak_ms,
               (unsigned long long)rm.xruns);
        wb_live_destroy(real);

        /* Soak: eight held Mamut voices at gain 0 (full DSP, no sound). */
        const char *soak = getenv("WB_LIVE_SOAK");
        if (soak && atoi(soak) > 0) {
            wb_live *busy = wb_live_create(48000);
            busy->raise_priority = raise_priority;
            CHECK(wb_live_load_part(busy, 0, s, dir, 0, &e));
            busy->slot[0].gain = 0;
            const char *periods = getenv("WB_AUDIO_PERIODS");
            CHECK(wb_live_start(busy, device, 128, periods ? (unsigned)atoi(periods) : 3));
            for (unsigned n = 0; n < 8; n++) wb_live_send(busy, note(WB_LIVE_NOTE_ON, 0, 48 + 3 * n, 100));
            for (int sec = 0; sec < atoi(soak); sec++) nanosleep(&second, nullptr);
            wb_live_meters sm = wb_live_read(busy);
            wb_live_stop(busy);
            static const char *const SCHED[] = { "normal priority", "nice -10", "SCHED_FIFO 70", "RealtimeKit" };
            printf("soak %s s, %s, %u voices: %llu periods, worst DSP %.3f ms of %.3f, xruns %llu\n", soak,
                   SCHED[atomic_load(&busy->scheduling)],
                   sm.active_voices, (unsigned long long)sm.periods, (double)sm.budget_peak_ms,
                   (double)sm.period_ms, (unsigned long long)sm.xruns);
            CHECK(sm.xruns == 0 && sm.active_voices == 8);
            wb_live_destroy(busy);
        }
    }

    /* W8 gate (optional): a session's whole arrangement from the part caches
     * with one live part following the score, silenced by gain 0. */
    const char *play = getenv("WB_PLAY_SESSION");
    if (device && *device && play && *play) {
        wb_session *ps = malloc(sizeof *ps);
        wb_derived *d = calloc(1, sizeof *d);
        char pdir[4096], midi[4400];
        wb_score score;
        bool ok = wb_load(play, ps, &e) && wb_dir_of(play, pdir, sizeof pdir) && wb_orch_build(ps, pdir, d, &e)
               && wb_orch_write(d, ps, pdir, midi, sizeof midi, &e) && wb_score_load_midi(ps, midi, &score, &e);
        CHECK(ok);
        if (ok) {
            const char *live_id = getenv("WB_PLAY_LIVE");
            int live_part = wb_find_part(ps, live_id && *live_id ? live_id : "vocal");
            uint64_t frames = wb_render_length(ps, &score);
            float *preview = calloc(2 * frames, sizeof *preview);
            unsigned frozen = 0;
            for (unsigned p = 0; p < ps->part_count; p++) {
                if ((int)p == live_part) continue;
                char key[65];
                float *x;
                uint64_t n;
                if (!wb_cache_key(ps, pdir, d, p, key) || !wb_cache_load(pdir, key, &x, &n)) continue;
                for (uint64_t i = 0; i < 2 * (n < frames ? n : frames); i++) preview[i] += x[i];
                free(x);
                frozen++;
            }
            wb_live *arr = wb_live_create(48000);
            arr->raise_priority = raise_priority;
            CHECK(live_part >= 0 && wb_live_load_part(arr, 0, ps, pdir, (unsigned)live_part, &e));
            arr->slot[0].gain = 0;
            wb_live_set_take(arr, 0, preview, frames, 0.0f);
            int8_t map[WB_PARTS];
            for (unsigned p = 0; p < WB_PARTS; p++) map[p] = -1;
            if (live_part >= 0) map[live_part] = 0;
            wb_live_set_score(arr, score.event, score.count, map);
            CHECK(wb_live_start(arr, device, 128, 3));
            wb_live_send(arr, (wb_live_cmd){ .kind = WB_LIVE_PLAY });
            const char *secs = getenv("WB_PLAY_SECONDS");
            unsigned seconds = secs ? (unsigned)atoi(secs) : (unsigned)(frames / 48000 + 1);
            struct timespec one = { 1, 0 };
            unsigned peak_voices = 0;
            for (unsigned t = 0; t < seconds; t++) {
                nanosleep(&one, nullptr);
                wb_live_meters pm = wb_live_read(arr);
                if (pm.active_voices > peak_voices) peak_voices = pm.active_voices;
            }
            wb_live_meters pm = wb_live_read(arr);
            wb_live_stop(arr);
            printf("arrangement %s: %u frozen parts + live '%s', %u s, position %.1f s, peak %u live voices, "
                   "worst DSP %.3f ms, xruns %llu\n", play, frozen, live_part >= 0 ? ps->part[live_part].id : "-",
                   seconds, (double)pm.position / 48000, peak_voices, (double)pm.budget_peak_ms,
                   (unsigned long long)pm.xruns);
            CHECK(pm.xruns == 0 && frozen + 1 == ps->part_count);
            wb_live_destroy(arr);
            free(preview);
            wb_score_free(&score);
        } else fprintf(stderr, "  %s: %s\n", play, e.message);
        wb_derived_free(d);
        free(d);
        free(ps);
    }

    wb_live_destroy(live);
    free(t0);
    free(t1);
    free(s);
    char cmd[600];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
    CHECK(system(cmd) == 0);
    printf("workbench live: %u checks, %u failed\n", checks, failures);
    return failures ? 1 : 0;
}
