/* Live-path step 7: ma_voice_pool_render_parallel gives the bits of
 * ma_voice_pool_render for any executor. Two executors: one runs the parts
 * in reverse order on the calling thread (any hidden order dependence shows),
 * one runs them on real threads. Notes come and go every few blocks, so
 * voices wake, release and fall asleep inside runs; unison, character,
 * several limits and block sizes, and a rack. Hosted test. */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/ma_internal.h"

static unsigned checks, failures;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---- executors ------------------------------------------------------------------ */

static void reverse_run(void *context, void (*job)(void *, unsigned), void *arg, unsigned parts) {
    (void)context;
    for (unsigned k = parts; k-- > 0;) job(arg, k);
}

/* Real threads: parts 1.. on workers, part 0 on the caller; spin until done. */
enum { WORKERS = 3 };
typedef struct {
    pthread_t thread[WORKERS];
    _Atomic unsigned generation, done;
    _Atomic bool quit;
    void (*job)(void *, unsigned);
    void *arg;
    unsigned parts;
    unsigned index[WORKERS];
} pool_threads;
static pool_threads threads;

static void *worker(void *self) {
    unsigned me = *(unsigned *)self, seen = 0;
    for (;;) {
        unsigned g;
        while ((g = atomic_load_explicit(&threads.generation, memory_order_acquire)) == seen)
            if (atomic_load(&threads.quit)) return nullptr;
        seen = g;
        if (me < threads.parts) threads.job(threads.arg, me);
        atomic_fetch_add_explicit(&threads.done, 1, memory_order_acq_rel);
    }
}

static void threaded_run(void *context, void (*job)(void *, unsigned), void *arg, unsigned parts) {
    (void)context;
    threads.job = job;
    threads.arg = arg;
    threads.parts = parts;
    atomic_store(&threads.done, 0);
    atomic_fetch_add_explicit(&threads.generation, 1, memory_order_acq_rel);
    job(arg, 0);
    while (atomic_load_explicit(&threads.done, memory_order_acquire) < WORKERS) {}
}

/* ---- renders -------------------------------------------------------------------- */

enum { FRAMES = 48000 };
static ma_voice_pool pool;
static ma_frame out[FRAMES];
static ma_rack rack;

static uint64_t render(const ma_patch *patch, unsigned limit, unsigned unison, size_t block,
                       const ma_pool_executor *x, bool with_rack, uint64_t *runs_split) {
    ma_voice_pool_config c = ma_voice_pool_default_config;
    c.limit = (uint8_t)limit;
    c.unison_count = (uint8_t)unison;
    /* Short releases, so voices fall asleep inside runs. */
    ma_patch quick = *patch;
    quick.amp_adsr.release_ms = 120;
    quick.filter_adsr.release_ms = 120;
    if (!ma_voice_pool_init(&pool, 48000, &c, &quick)) return 0;
    ma_voice_pool_set_character(&pool, .3f);
    if (with_rack) {
        ma_rack_spec spec = { 0 };
        ma_rack_slot_init(&spec.slot[0], MA_RACK_EQ, 1);
        spec.slot[0].param[MA_EQ_BAND_COUNT] = 2;
        spec.slot[0].param[MA_EQ_BAND(0, MA_EQ_GAIN_DB)] = 4;
        spec.slot[0].param[MA_EQ_BAND(1, MA_EQ_TYPE)] = MA_EQ_HIGH_SHELF;
        spec.slot[0].param[MA_EQ_BAND(1, MA_EQ_FREQ_HZ)] = 5000;
        spec.slot[0].param[MA_EQ_BAND(1, MA_EQ_GAIN_DB)] = -3;
        ma_rack_init(&rack, 48000);
        (void)ma_rack_apply(&rack, &spec);
        ma_voice_pool_attach_rack(&pool, &rack);
    }
    uint64_t seed = 0x50415231u + limit * 131 + unison;
    uint8_t held[128] = { 0 };
    size_t step = 0;
    for (size_t at = 0; at < FRAMES; at += block, step++) {
        /* Every few blocks: a few note-ons, a few note-offs (some voices then
         * fall asleep inside a later run). */
        if (step % 3 == 0) {
            for (int k = 0; k < 3; k++) {
                uint8_t note = (uint8_t)(36 + tw_splitmix64(&seed) % 48);
                if (tw_splitmix64(&seed) & 1) {
                    (void)ma_voice_pool_note_on(&pool, 0, note, (uint8_t)(40 + tw_splitmix64(&seed) % 80));
                    held[note] = 1;
                } else if (held[note]) {
                    (void)ma_voice_pool_note_off(&pool, 0, note, 64);
                    held[note] = 0;
                }
            }
        }
        if (step % 40 == 39)   /* now and then let everything go */
            for (unsigned note = 0; note < 128; note++)
                if (held[note]) {
                    (void)ma_voice_pool_note_off(&pool, 0, (uint8_t)note, 64);
                    held[note] = 0;
                }
        size_t n = FRAMES - at < block ? FRAMES - at : block;
        ma_voice_pool_render_parallel(&pool, out + at, n, x);
    }
    if (runs_split) *runs_split = pool.diagnostics.sleeps;
    return tw_fnv1a64(out, sizeof out, 0);
}

int main(void) {
    if (!ma_lanes_supported()) {
        printf("ma pool parallel: no AVX2 lanes on this CPU; nothing to compare\n");
        return 0;
    }
    for (unsigned w = 0; w < WORKERS; w++) {
        threads.index[w] = w + 1;
        pthread_create(&threads.thread[w], nullptr, worker, &threads.index[w]);
    }
    static const struct { const char *name; const ma_patch *patch; } P[] = {
        { "Granica", &ma_patch_granica }, { "Tepih", &ma_patch_tepih },
    };
    static const struct { unsigned limit, unison; } L[] = { { 8, 1 }, { 20, 1 }, { 32, 4 }, { 64, 2 } };
    static const size_t BLOCKS[] = { 128, 37 };
    const ma_pool_executor reverse2 = { reverse_run, nullptr, 2 }, reverse4 = { reverse_run, nullptr, 4 };
    const ma_pool_executor real2 = { threaded_run, nullptr, 2 }, real4 = { threaded_run, nullptr, 4 };
    unsigned cases = 0;
    for (unsigned pi = 0; pi < sizeof P / sizeof *P; pi++)
        for (unsigned li = 0; li < sizeof L / sizeof *L; li++)
            for (unsigned bi = 0; bi < sizeof BLOCKS / sizeof *BLOCKS; bi++) {
                bool with_rack = (pi + li + bi) % 2 == 0;
                uint64_t sleeps = 0;
                uint64_t serial = render(P[pi].patch, L[li].limit, L[li].unison, BLOCKS[bi], nullptr, with_rack, &sleeps);
                uint64_t r2 = render(P[pi].patch, L[li].limit, L[li].unison, BLOCKS[bi], &reverse2, with_rack, nullptr);
                uint64_t r4 = render(P[pi].patch, L[li].limit, L[li].unison, BLOCKS[bi], &reverse4, with_rack, nullptr);
                uint64_t t2 = render(P[pi].patch, L[li].limit, L[li].unison, BLOCKS[bi], &real2, with_rack, nullptr);
                uint64_t t4 = render(P[pi].patch, L[li].limit, L[li].unison, BLOCKS[bi], &real4, with_rack, nullptr);
                CHECK(serial && serial == r2 && serial == r4 && serial == t2 && serial == t4,
                      "%s limit %u unison %u block %zu rack %d: serial %016llx reverse %016llx/%016llx "
                      "threads %016llx/%016llx", P[pi].name, L[li].limit, L[li].unison, BLOCKS[bi], with_rack,
                      (unsigned long long)serial, (unsigned long long)r2, (unsigned long long)r4,
                      (unsigned long long)t2, (unsigned long long)t4);
                CHECK(sleeps > 0, "%s limit %u: no voice fell asleep, the sleep path is untested", P[pi].name,
                      L[li].limit);
                cases++;
            }
    atomic_store(&threads.quit, true);
    atomic_fetch_add(&threads.generation, 1);
    for (unsigned w = 0; w < WORKERS; w++) pthread_join(threads.thread[w], nullptr);
    printf("ma pool parallel: %u cases x 4 executors, %u checks, %u failures\n", cases, checks, failures);
    return failures ? 1 : 0;
}
