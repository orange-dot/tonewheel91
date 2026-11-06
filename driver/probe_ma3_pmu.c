/* MA3-PERF PMU probe (2026-11-06): where do the remaining cycles go?
 * Per 128-frame render block, for the ten 8-slot wet cases and both pool
 * engines: branches and mispredictions, L1D/L2/L3 misses, floating-point
 * assists (denormal handling) and divider uops. Haswell raw events, user
 * space, this thread; one event group at a time on the same deterministic
 * workload, so nothing is multiplexed (checked per block).
 *
 *   probe_ma3_pmu --calibrate   each raw event against a known trigger
 *   probe_ma3_pmu --run         the measurement (pin with taskset)
 *
 * Not shipping code; not part of `make test`. */
#define _GNU_SOURCE
#include "../src/mamutanalog.h"
#include <inttypes.h>
#include <linux/perf_event.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

enum { RATE = 48000, FRAMES = 128, WARMUP = 256, BLOCKS = 1024, MAXEV = 4 };

typedef struct { const char *name; uint32_t type; uint64_t config; } event;
/* Haswell (SDM vol. 3B, table 19-2x): event | umask << 8 */
static const event BRANCHES = { "branches", PERF_TYPE_RAW, 0x00c4 };      /* BR_INST_RETIRED.ALL_BRANCHES */
static const event BR_MISSES = { "branch_misses", PERF_TYPE_RAW, 0x00c5 };/* BR_MISP_RETIRED.ALL_BRANCHES */
static const event INSTR = { "instructions", PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS };
static const event L1D_REPL = { "l1d_replacements", PERF_TYPE_RAW, 0x0151 };/* L1D.REPLACEMENT */
static const event L2_MISS = { "l2_misses", PERF_TYPE_RAW, 0x3f24 };      /* L2_RQSTS.MISS */
static const event L3_MISS = { "l3_load_misses", PERF_TYPE_RAW, 0x20d1 }; /* MEM_LOAD_UOPS_RETIRED.L3_MISS */
static const event LLC_MISS = { "llc_misses", PERF_TYPE_RAW, 0x412e };    /* LONGEST_LAT_CACHE.MISS */
static const event FP_ASSIST = { "fp_assists", PERF_TYPE_RAW, 0x1eca };   /* FP_ASSIST.ANY */
static const event DIVIDER = { "divider_uops", PERF_TYPE_RAW, 0x0214 };   /* ARITH.DIVIDER_UOPS */

typedef struct { const char *name; event ev[MAXEV]; unsigned count; } group_def;
static const group_def GROUPS[] = {
    { "branch", { INSTR, BRANCHES, BR_MISSES }, 3 },
    { "cache", { L1D_REPL, L2_MISS, LLC_MISS }, 3 },
    { "assist", { FP_ASSIST, DIVIDER }, 2 },
};

typedef struct { int fd[MAXEV + 1]; unsigned n; } group;
static int open_event(uint32_t type, uint64_t config, int leader) {
    struct perf_event_attr a; memset(&a, 0, sizeof a);
    a.type = type; a.size = sizeof a; a.config = config;
    a.disabled = leader < 0; a.exclude_kernel = 1; a.exclude_hv = 1;
    a.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
    return (int)syscall(SYS_perf_event_open, &a, 0, -1, leader, 0UL);
}
static void group_open(group *g, const group_def *d) {
    g->n = d->count + 1;
    g->fd[0] = open_event(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES, -1);
    if (g->fd[0] < 0) { perror("perf_event_open cycles"); exit(2); }
    for (unsigned i = 0; i < d->count; i++)
        if ((g->fd[i + 1] = open_event(d->ev[i].type, d->ev[i].config, g->fd[0])) < 0) {
            fprintf(stderr, "perf_event_open %s: ", d->ev[i].name); perror(""); exit(2);
        }
    if (ioctl(g->fd[0], PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP)) { perror("enable"); exit(2); }
}
static void group_close(group *g) { for (unsigned i = 0; i < g->n; i++) close(g->fd[i]); }
/* values[0] = cycles, values[1..] = members; returns false if multiplexed */
static bool group_read(const group *g, uint64_t values[MAXEV + 1], uint64_t *enabled, uint64_t *running) {
    uint64_t v[3 + MAXEV + 1];
    ssize_t want = (ssize_t)((3 + g->n) * sizeof(uint64_t));
    if (read(g->fd[0], v, sizeof v) != want || v[0] != g->n) { perror("pmu read"); exit(1); }
    *enabled = v[1]; *running = v[2];
    for (unsigned i = 0; i < g->n; i++) values[i] = v[3 + i];
    return true;
}

/* ---------------- calibration ---------------- */
static uint64_t xs = UINT64_C(0x9e3779b97f4a7c15);
static uint64_t next_rand(void) { xs ^= xs << 13; xs ^= xs >> 7; xs ^= xs << 17; return xs; }

static void calibrate(void) {
    puts("event,trigger,trigger_count,measured,per_trigger");
    struct { const event *e; const char *trigger; } cases[] = {
        { &BRANCHES, "1e6 data-dependent branches" }, { &BR_MISSES, "1e6 random branches" },
        { &L1D_REPL, "1e6 dependent loads to random lines in 64 MiB" },
        { &L2_MISS, "1e6 dependent loads to random lines in 64 MiB" },
        { &L3_MISS, "1e6 dependent loads to random lines in 64 MiB" },
        { &LLC_MISS, "1e6 dependent loads to random lines in 64 MiB" },
        { &FP_ASSIST, "1e6 multiplies of a subnormal" }, { &DIVIDER, "1e6 divss" },
    };
    enum { N = 1000000, LINES = (64 << 20) / 64 };
    uint32_t *chain = malloc((size_t)LINES * 16 * sizeof *chain);   /* one uint32 per 64-byte line slot */
    if (!chain) exit(1);
    /* random cyclic permutation over the lines (Sattolo) */
    uint32_t *order = malloc((size_t)LINES * sizeof *order);
    for (uint32_t i = 0; i < LINES; i++) order[i] = i;
    for (uint32_t i = LINES - 1; i > 0; i--) { uint32_t j = (uint32_t)(next_rand() % i); uint32_t t = order[i]; order[i] = order[j]; order[j] = t; }
    for (uint32_t i = 0; i < LINES; i++) chain[(size_t)order[i] * 16] = order[(i + 1) % LINES];
    free(order);
    for (unsigned c = 0; c < sizeof cases / sizeof *cases; c++) {
        group_def d = { cases[c].e->name, { *cases[c].e }, 1 };
        group g; group_open(&g, &d);
        uint64_t a[MAXEV + 1], b[MAXEV + 1], e0, r0, e1, r1;
        volatile float sink = 0.0f;
        volatile uint64_t isink = 0;
        group_read(&g, a, &e0, &r0);
        if (cases[c].e == &BRANCHES || cases[c].e == &BR_MISSES) {
            /* stores to two distinct volatiles: the compiler cannot if-convert
             * this into cmov, so each iteration carries a 50/50 branch */
            static volatile unsigned taken, skipped;
            for (unsigned i = 0; i < N; i++) { if (next_rand() & 1) taken = i; else skipped = i; }
            isink = taken + skipped;
        } else if (cases[c].e == &FP_ASSIST) {
            volatile float tiny = 0x1p-140f;                 /* subnormal input */
            for (unsigned i = 0; i < N; i++) sink = tiny * 0.75f;
        } else if (cases[c].e == &DIVIDER) {
            volatile float x = 3.0f;
            for (unsigned i = 0; i < N; i++) sink = x / 7.0f;
        } else {
            uint32_t at = 0;
            for (unsigned i = 0; i < N; i++) at = chain[(size_t)at * 16];
            isink = at;
        }
        group_read(&g, b, &e1, &r1);
        group_close(&g);
        (void)sink; (void)isink;
        uint64_t measured = b[1] - a[1];
        printf("%s,%s,%d,%" PRIu64 ",%.3f%s\n", cases[c].e->name, cases[c].trigger, N, measured,
               (double)measured / N, (r1 - r0) != (e1 - e0) ? " MULTIPLEXED" : "");
    }
    free(chain);
}

/* ---------------- the pool workload (as bench_ma3_perf, limit 8, held, wet) ---------------- */
static ma_voice_pool pool;
static uint64_t fnv(const void *p, size_t n, uint64_t h) {
    const unsigned char *b = p;
    if (!h) h = UINT64_C(0xcbf29ce484222325);
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= UINT64_C(0x100000001b3); }
    return h;
}
static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return (x > y) - (x < y);
}
static void run(void) {
    const ma_patch *patches[] = { &ma_patch_tepih, &ma_patch_lead, &ma_patch_raster, &ma_patch_prizma, &ma_patch_granica };
    const char *names[] = { "Tepih", "Lead", "Raster", "Prizma", "Granica" };
    static uint64_t per_block[BLOCKS][MAXEV + 1];
    puts("patch,bcs,engine,group,event,median_per_block,mean_per_block,multiplexed_blocks,pcm_hash");
    for (unsigned p = 0; p < 5; p++)
        for (unsigned bcs = 0; bcs < 2; bcs++)
            for (unsigned engine = 0; engine < 2; engine++) {
                ma_voice_pool_config c = ma_voice_pool_default_config;
                c.limit = 8; c.unison_count = 1;
                c.engine = engine ? MA_POOL_ENGINE_SCALAR : MA_POOL_ENGINE_AUTO;
                ma_patch patch = *patches[p];
                patch.bcs_amount = bcs ? ma_patch_granica.bcs_amount : 0;
                patch.bcs_regime = ma_patch_granica.bcs_regime;
                if (!ma_voice_pool_init(&pool, RATE, &c, &patch)) exit(1);
                for (unsigned n = 0; n < 8; n++)
                    if (!ma_voice_pool_note_on(&pool, 0, (uint8_t)(36 + n), 100)) exit(1);
                ma_effects_set_chorus(&pool.effects, (ma_chorus_controls){ true, .30f, .35f, .25f });
                ma_effects_set_reverb(&pool.effects, (ma_reverb_controls){ true, .25f, .65f, .55f });
                ma_frame out[FRAMES];
                uint64_t hash = 0;
                for (unsigned b = 0; b < WARMUP; b++) {
                    ma_voice_pool_render(&pool, out, FRAMES);
                    hash = fnv(out, sizeof out, hash);
                }
                char lines[sizeof GROUPS / sizeof *GROUPS][MAXEV + 1][256];
                for (unsigned gi = 0; gi < sizeof GROUPS / sizeof *GROUPS; gi++) {
                    group g; group_open(&g, &GROUPS[gi]);
                    unsigned mux = 0;
                    for (unsigned b = 0; b < BLOCKS; b++) {
                        uint64_t a[MAXEV + 1], z[MAXEV + 1], e0, r0, e1, r1;
                        group_read(&g, a, &e0, &r0);
                        ma_voice_pool_render(&pool, out, FRAMES);
                        group_read(&g, z, &e1, &r1);
                        mux += (r1 - r0) != (e1 - e0);
                        for (unsigned i = 0; i < g.n; i++) per_block[b][i] = z[i] - a[i];
                        hash = fnv(out, sizeof out, hash);
                    }
                    group_close(&g);
                    for (unsigned i = 0; i < g.n; i++) {
                        static uint64_t column[BLOCKS];
                        double sum = 0;
                        for (unsigned b = 0; b < BLOCKS; b++) { column[b] = per_block[b][i]; sum += (double)column[b]; }
                        qsort(column, BLOCKS, sizeof *column, cmp_u64);
                        snprintf(lines[gi][i], sizeof lines[gi][i], "%s,%u,%s,%s,%s,%" PRIu64 ",%.1f,%u",
                                 names[p], bcs, engine ? "scalar" : "vector", GROUPS[gi].name,
                                 i ? GROUPS[gi].ev[i - 1].name : "cycles", column[BLOCKS / 2], sum / BLOCKS, mux);
                    }
                }
                for (unsigned gi = 0; gi < sizeof GROUPS / sizeof *GROUPS; gi++)
                    for (unsigned i = 0; i <= GROUPS[gi].count; i++)
                        printf("%s,%016" PRIx64 "\n", lines[gi][i], hash);
                fflush(stdout);
            }
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--calibrate")) { calibrate(); return 0; }
    if (argc == 2 && !strcmp(argv[1], "--run")) { run(); return 0; }
    puts("Usage: probe_ma3_pmu --calibrate|--run");
    return argc == 1 ? 0 : 1;
}
