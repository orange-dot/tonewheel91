/* Live-path soak (operator check, not part of make test: it opens the sound
 * card, silent at gain 0). Holds N Mamut voices through the Patchbench
 * engine and reports, per 128-frame period: DSP time and the interval from
 * one period's start to the next (percentiles, maximum), xruns, CPU
 * migrations of the audio thread and the frequency of its CPU, sampled.
 *
 *   build/pb_live_soak [seconds] [device]
 *   PB_SOAK_VOICES=32     held voices (pool limit the same)
 *   PB_SOAK_CHANNELS=1    spread them over this many channels (1..4), each
 *                         with its own pool; the other channels idle
 *   PB_SOAK_RACK=1        one 8-band EQ in the rack (settled: the wavefront)
 *   PB_SOAK_BUSY=1        busy threads that render private pools, as the
 *                         probe does after an edit
 *   PB_LIVE_LOCK/PIN/HOLD/WORKER as in the Patchbench (1 by default)
 *   PB_SOAK_PERIOD=128, PB_SOAK_PERIODS=3   the ALSA period and buffer
 *   PB_SOAK_CSV=path      the two histograms, 10 us bins */
#define _GNU_SOURCE
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../pb_engine.h"
#include "../pb_host.h"

static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e3 + t.tv_nsec * 1e-6;
}

static bool flag(const char *name, bool fallback) {
    const char *v = getenv(name);
    return v ? strcmp(v, "0") != 0 : fallback;
}

static double percentile(const uint32_t *hist, double q) {
    uint64_t total = 0;
    for (int i = 0; i < PB_HIST_BINS; i++) total += hist[i];
    if (!total) return 0;
    uint64_t want = (uint64_t)ceil(q * (double)total), seen = 0;
    for (int i = 0; i < PB_HIST_BINS; i++) {
        seen += hist[i];
        if (seen >= want) return (i + 1) * .01;   /* upper edge of the bin */
    }
    return PB_HIST_BINS * .01;
}

static uint64_t above(const uint32_t *hist, double ms) {
    uint64_t n = 0;
    for (int i = (int)(ms * 100); i < PB_HIST_BINS; i++) n += hist[i];
    return n;
}

static _Atomic bool busy_run;
static cpu_set_t busy_set;
static bool busy_pinned;

static void *busy_main(void *arg) {
    (void)arg;
    if (busy_pinned) (void)pthread_setaffinity_np(pthread_self(), sizeof busy_set, &busy_set);
    pb_voice v = pb_voice_default();
    v.limit = 8;
    pb_pool *p = pb_pool_build(48000, &ma_patch_granica, &v);
    if (!p) return nullptr;
    for (int n = 0; n < 8; n++) (void)ma_voice_pool_note_on(&p->pool, 0, (uint8_t)(40 + 3 * n), 100);
    static _Thread_local ma_frame block[512];
    while (atomic_load(&busy_run)) ma_voice_pool_render(&p->pool, block, 512);
    free(p);
    return nullptr;
}

static bool raise(int *priority) { return pb_rtkit_realtime(gettid(), priority); }

int main(int argc, char **argv) {
    double seconds = argc > 1 ? atof(argv[1]) : 60;
    const char *device = argc > 2 ? argv[2] : "default";
    unsigned voices = getenv("PB_SOAK_VOICES") ? (unsigned)atoi(getenv("PB_SOAK_VOICES")) : 32;
    if (voices < 1 || voices > 64) voices = 32;
    bool rack = flag("PB_SOAK_RACK", false), busy = flag("PB_SOAK_BUSY", false);
    bool lock = flag("PB_LIVE_LOCK", true), pin = flag("PB_LIVE_PIN", true), hold = flag("PB_LIVE_HOLD", true);
    bool worker = flag("PB_LIVE_WORKER", true);

    pb_cpu_layout layout;
    bool split = pb_cpu_layout_read(&layout);
    unsigned channels = getenv("PB_SOAK_CHANNELS") ? (unsigned)atoi(getenv("PB_SOAK_CHANNELS")) : 1;
    if (channels < 1 || channels > PB_CHANNELS) channels = 1;
    pb_voice voice = pb_voice_default();
    voice.limit = (uint8_t)((voices + channels - 1) / channels);
    voice.gain = 0;   /* silent */
    pb_pattern pattern = pb_builtin_pattern(PB_PAT_ARPEGGIO, 48, 12000);
    const char *patch_name = getenv("PB_SOAK_PATCH") ? getenv("PB_SOAK_PATCH") : "Granica";
    static const struct { const char *name; const ma_patch *patch; } P[] = {
        { "Tepih", &ma_patch_tepih }, { "Lead", &ma_patch_lead }, { "Dubina", &ma_patch_dubina },
        { "Raster", &ma_patch_raster }, { "Prizma", &ma_patch_prizma }, { "Granica", &ma_patch_granica },
    };
    ma_patch patch = ma_patch_granica;
    for (unsigned i = 0; i < sizeof P / sizeof *P; i++)
        if (!strcmp(patch_name, P[i].name)) patch = *P[i].patch;
    ma_patch patches[PB_CHANNELS];
    pb_voice voice_setup[PB_CHANNELS];
    for (unsigned c = 0; c < PB_CHANNELS; c++) patches[c] = patch, voice_setup[c] = voice;
    pb_sequence sequence = pb_sequence_single(&pattern);
    pb_engine *e = pb_engine_create(48000, patches, voice_setup, &sequence);
    if (!e) return 1;
    e->raise_priority = raise;
    e->options.lock_memory = lock;
    e->options.audio_cpu = pin && split ? layout.audio_cpu : -1;
    cpu_set_t rest;
    int worker_cpu = pb_cpu_worker(&layout, &rest);
    e->options.worker = worker && split;
    e->options.worker_cpu = pin && split ? worker_cpu : -1;
    e->options.worker_lead_us = getenv("PB_LIVE_WORKER_LEAD_US") ? (unsigned)atoi(getenv("PB_LIVE_WORKER_LEAD_US")) : 0;
    if (rack) {
        ma_rack_spec spec = { 0 };
        ma_rack_slot_init(&spec.slot[0], MA_RACK_EQ, 1);
        spec.slot[0].param[MA_EQ_BAND_COUNT] = MA_EQ_BANDS;
        for (unsigned b = 0; b < MA_EQ_BANDS; b++) {
            spec.slot[0].param[MA_EQ_BAND(b, MA_EQ_TYPE)] = (float)(b % MA_EQ_TYPE_COUNT);
            spec.slot[0].param[MA_EQ_BAND(b, MA_EQ_FREQ_HZ)] = 60.0f * powf(2, (float)b);
            spec.slot[0].param[MA_EQ_BAND(b, MA_EQ_GAIN_DB)] = b % 2 ? -2 : 2;
        }
        for (unsigned c = 0; c < channels; c++) (void)pb_engine_set_rack_now(e, c, &spec);
    }
    pb_power_hold power = { 0 };
    bool held = hold && pb_power_hold_start(&power, "performance", "pb_live_soak");
    char profile[32];
    pb_power_profile(profile, sizeof profile);
    if (pin && split) (void)pthread_setaffinity_np(pthread_self(), sizeof rest, worker ? &rest : &layout.others);

    pthread_t workers[8];
    unsigned nworkers = 0;
    if (busy) {
        busy_pinned = pin && split;
        busy_set = worker ? rest : layout.others;
        atomic_store(&busy_run, true);
        unsigned want = (unsigned)(layout.cpus > 1 ? layout.cpus - 1 : 1);
        for (; nworkers < want && nworkers < 8; nworkers++)
            if (pthread_create(&workers[nworkers], nullptr, busy_main, nullptr)) break;
    }
    unsigned period_frames = getenv("PB_SOAK_PERIOD") ? (unsigned)atoi(getenv("PB_SOAK_PERIOD")) : 128;
    unsigned period_count = getenv("PB_SOAK_PERIODS") ? (unsigned)atoi(getenv("PB_SOAK_PERIODS")) : 3;
    if (period_frames < 16 || period_frames > PB_BLOCK_MAX) period_frames = 128;
    if (period_count < 2 || period_count > 16) period_count = 3;
    if (!pb_engine_start(e, device, period_frames, period_count)) {
        fprintf(stderr, "pb_live_soak: %s\n", e->error);
        return 1;
    }
    for (unsigned n = 0; n < voices; n++)
        pb_engine_send(e, (pb_event){ .kind = PB_EV_NOTE_ON, .note = (uint8_t)(36 + n), .velocity = 100,
                                      .channel = (uint8_t)(n % channels) });
    if (rack) pb_engine_send(e, (pb_event){ .kind = PB_EV_SEQ_PLAY });

    /* Sample the audio CPU's frequency every 10 ms. */
    static unsigned mhz[1 << 20];
    size_t samples = 0;
    double start = now_ms();
    while (now_ms() - start < seconds * 1000) {
        int cpu = e->timing.cpu;
        if (cpu >= 0 && samples < sizeof mhz / sizeof *mhz) {
            unsigned f = pb_cpu_mhz(cpu);
            if (f) mhz[samples++] = f;
        }
        pb_engine_collect(e);
        struct timespec ts = { 0, 10000000 };
        nanosleep(&ts, nullptr);
    }
    pb_meters m = pb_engine_read(e);
    pb_engine_stop(e);
    atomic_store(&busy_run, false);
    for (unsigned i = 0; i < nworkers; i++) pthread_join(workers[i], nullptr);
    pb_power_hold_end(&power);

    const pb_timing *t = &e->timing;
    unsigned lo = 0, med = 0;
    if (samples) {
        for (size_t i = 1; i < samples; i++)
            for (size_t j = i; j > 0 && mhz[j - 1] > mhz[j]; j--) {
                unsigned x = mhz[j];
                mhz[j] = mhz[j - 1];
                mhz[j - 1] = x;
            }
        lo = mhz[0];
        med = mhz[samples / 2];
    }
    static const char *SCHED[] = { "normal", "nice -10", "SCHED_FIFO", "RealtimeKit" };
    double period = m.period_ms;
    printf("soak %.0f s '%s' %u %s voices on %u channel(s)%s%s: %s %d, lock %s, pin %s, profile %s%s\n", seconds,
           device, voices, patch_name, channels,
           rack ? " + 8-band EQ" : "", busy ? " + busy" : "", SCHED[atomic_load(&e->scheduling)],
           atomic_load(&e->priority),
           !lock ? "off" : e->lock_failed ? "FAILED" : "on", atomic_load(&e->pinned) ? "on" : "off",
           profile[0] ? profile : "?", held ? " (held)" : "");
    printf("  worker %s (cpu %d, RT %d, lead %u us, %llu runs)\n", m.worker_runs ? "on" : "off",
           e->options.worker_cpu, atomic_load(&e->worker.priority), e->options.worker_lead_us,
           (unsigned long long)m.worker_runs);
    printf("  periods %llu, xruns %llu, overload panics %llu%s, migrations %llu, audio CPU %u..%u MHz "
           "(median %u, %zu samples), %.2f MB locked\n",
           (unsigned long long)t->periods, (unsigned long long)m.xruns, (unsigned long long)m.overload_panics,
           m.demoted ? ", REAL TIME GIVEN UP (SIGXCPU)" : "", (unsigned long long)t->migrations, lo,
           samples ? mhz[samples - 1] : 0, med, samples, (double)e->locked_bytes / (1 << 20));
    printf("  DSP ms   p50 %.2f  p99 %.2f  p99.9 %.2f  max %.3f  (period %.3f)\n", percentile(t->dsp, .5),
           percentile(t->dsp, .99), percentile(t->dsp, .999), (double)t->dsp_max_ms, period);
    printf("  wake ms  p50 %.2f  p99 %.2f  p99.9 %.2f  max %.3f  above 1.5 periods %llu, above 2 periods %llu\n",
           percentile(t->wake, .5), percentile(t->wake, .99), percentile(t->wake, .999), (double)t->wake_max_ms,
           (unsigned long long)above(t->wake, 1.5 * period), (unsigned long long)above(t->wake, 2 * period));
    const char *csv = getenv("PB_SOAK_CSV");
    if (csv) {
        FILE *f = fopen(csv, "w");
        if (f) {
            fprintf(f, "bin_ms,dsp,wake\n");
            for (int i = 0; i < PB_HIST_BINS; i++) fprintf(f, "%.2f,%u,%u\n", i * .01, t->dsp[i], t->wake[i]);
            fclose(f);
        }
    }
    pb_engine_destroy(e);
    return 0;
}
