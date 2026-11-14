/* Operator check, not part of make test (it opens the sound card): the cost
 * of ma_voice_pool_set_patch per period with 8 and 32 held voices (plan
 * PB0), then a live run through ALSA at gain 0, silent, with the sequencer,
 * held notes, patch mailbox traffic at 60 Hz and a pool swap, reporting DSP
 * time and xruns (plan PB2).
 *   build/pb_live_check [seconds] [device] */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "../pb_engine.h"

static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e3 + t.tv_nsec * 1e-6;
}

static void set_patch_cost(unsigned limit) {
    pb_voice v = pb_voice_default();
    v.limit = (uint8_t)limit;
    ma_patch p = ma_patch_granica;
    pb_pool *pool = pb_pool_build(48000, &p, &v);
    if (!pool) return;
    for (unsigned n = 0; n < limit; n++) (void)ma_voice_pool_note_on(&pool->pool, 0, (uint8_t)(36 + n), 100);
    ma_frame block[128];
    ma_voice_pool_render(&pool->pool, block, 128);
    enum { N = 2000 };
    double worst = 0, total = 0, render = 0;
    for (int i = 0; i < N; i++) {
        p.filter_cutoff_hz = 400.0f + (float)(i % 200) * 20.0f;
        double t0 = now_ms();
        ma_voice_pool_set_patch(&pool->pool, &p);
        double t1 = now_ms();
        ma_voice_pool_render(&pool->pool, block, 128);
        double t2 = now_ms();
        total += t1 - t0;
        render += t2 - t1;
        if (t1 - t0 > worst) worst = t1 - t0;
    }
    printf("set_patch, %2u held voices: mean %.4f ms, worst %.4f ms per call; render mean %.3f ms per 128 frames "
           "(period 2.667 ms)\n", limit, total / N, worst, render / N);
    free(pool);
}

int main(int argc, char **argv) {
    double seconds = argc > 1 ? atof(argv[1]) : 20;
    const char *device = argc > 2 ? argv[2] : "default";
    set_patch_cost(8);
    set_patch_cost(32);

    ma_patch patch = ma_patch_lead;
    pb_voice voice = pb_voice_default();
    voice.gain = 0;   /* silent */
    pb_pattern pattern = pb_builtin_pattern(PB_PAT_ARPEGGIO, 48, 14000);
    /* Channel 1 plays; the other channels idle on the same patch. */
    ma_patch patches[PB_CHANNELS];
    pb_voice voices[PB_CHANNELS];
    for (unsigned c = 0; c < PB_CHANNELS; c++) patches[c] = patch, voices[c] = voice;
    pb_sequence sequence = pb_sequence_single(&pattern);
    pb_engine *e = pb_engine_create(48000, patches, voices, &sequence);
    if (!e || !pb_engine_start(e, device, 128, 3)) {
        fprintf(stderr, "pb_live_check: %s\n", e ? e->error : "engine");
        return 1;
    }
    for (int n = 0; n < 6; n++)
        pb_engine_send(e, (pb_event){ .kind = PB_EV_NOTE_ON, .note = (uint8_t)(55 + 3 * n), .velocity = 90 });
    pb_engine_send(e, (pb_event){ .kind = PB_EV_SEQ_PLAY });
    double start = now_ms(), worst = 0;
    bool swapped = false;
    unsigned updates = 0;
    while (now_ms() - start < seconds * 1000) {
        double t = (now_ms() - start) / 1000;
        patch.filter_cutoff_hz = 800 + 3000 * (float)(.5 + .5 * sin(t * 2));
        pb_engine_set_patch(e, 0, &patch);
        updates++;
        if (!swapped && t > seconds / 2) {
            voice.limit = 16;
            swapped = pb_engine_set_voice(e, 0, &voice, &patch);
        }
        pb_engine_collect(e);
        pb_meters m = pb_engine_read(e);
        if (m.budget_ms > worst) worst = m.budget_ms;
        struct timespec ts = { 0, 16666667 };
        nanosleep(&ts, nullptr);
    }
    pb_meters m = pb_engine_read(e);
    static const char *SCHED[] = { "normal", "nice -10", "SCHED_FIFO", "RealtimeKit" };
    printf("live %.0f s on '%s' (%s): %llu periods, %u patch updates, %llu pool swap(s), %u voices, "
           "DSP worst %.3f ms of %.3f, xruns %llu\n", seconds, device, SCHED[atomic_load(&e->scheduling)],
           (unsigned long long)m.periods, updates, (unsigned long long)m.pool_swaps, m.active_voices, worst,
           (double)m.period_ms, (unsigned long long)m.xruns);
    pb_engine_destroy(e);
    return 0;
}
