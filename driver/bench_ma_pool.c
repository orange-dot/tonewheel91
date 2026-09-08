/* Opt-in host cost matrix. Keep hashing, event dispatch and I/O outside timing. */
#define _GNU_SOURCE
#include "../src/mamutanalog.h"
#include <inttypes.h>
#include <math.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#include <sys/utsname.h>

enum { RATE = 48000, DEFAULT_WARMUP = 16, DEFAULT_BLOCKS = 32 };
static unsigned warmup_blocks(void) {
    const char *value = getenv("MA_POOL_WARMUP");
    return value ? (unsigned)strtoul(value, NULL, 10) : DEFAULT_WARMUP;
}
static unsigned measured_blocks(void) {
    const char *value = getenv("MA_POOL_BLOCKS");
    return value ? (unsigned)strtoul(value, NULL, 10) : DEFAULT_BLOCKS;
}
static ma_voice_pool pool;
static struct timespec now(clockid_t clock) {
    struct timespec t = {0};
    if (clock_gettime(clock, &t)) { perror("clock_gettime"); exit(1); }
    return t;
}
static double us(struct timespec a, struct timespec b) {
    return (double)(b.tv_sec - a.tv_sec) * 1e6 + (double)(b.tv_nsec - a.tv_nsec) * .001;
}
static int compare(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
static uint64_t measure(const char *name, const ma_patch *patch, unsigned bcs,
    unsigned limit, unsigned frames, unsigned scenario, unsigned pass) {
    unsigned unison = scenario >= 5 ? 1u << (scenario - 4) : 1;
    unsigned slots = scenario == 0 ? 0 : scenario == 1 ? 1 : scenario == 2 ? 8 : limit;
    ma_voice_pool_config c = ma_voice_pool_default_config;
    c.limit = (uint8_t)limit; c.unison_count = (uint8_t)unison;
    if (!ma_voice_pool_init(&pool, RATE, &c, patch)) exit(1);
    for (unsigned n = 0; n < slots / unison; n++)
        if (!ma_voice_pool_note_on(&pool, 0, (uint8_t)(36 + n % 48), 100)) exit(1);
    ma_frame output[512];
    unsigned warmup = warmup_blocks(), blocks = measured_blocks();
    double *cpu = calloc(blocks, sizeof *cpu), *wall = calloc(blocks, sizeof *wall);
    if (!cpu || !wall) exit(1);
    double cpu_sum = 0, wall_sum = 0;
    unsigned minimum = limit, maximum = 0;
    uint64_t hash = 0;
    struct rusage before = {0}, after = {0};
    if (getrusage(RUSAGE_SELF, &before)) exit(1);
    for (unsigned block = 0; block < warmup + blocks; block++) {
        if (scenario == 4) {
            if (block == warmup) {
                ma_voice_pool_set_sustain(&pool, 0, true);
                for (unsigned n = 0; n < slots; n++)
                    (void)ma_voice_pool_note_off(&pool, 0, (uint8_t)(36 + n % 48), 0);
            }
            if (block == warmup + blocks / 2) ma_voice_pool_set_sustain(&pool, 0, false);
        }
        struct timespec w = now(CLOCK_MONOTONIC), t = now(CLOCK_THREAD_CPUTIME_ID);
        ma_voice_pool_render(&pool, output, frames);
        struct timespec te = now(CLOCK_THREAD_CPUTIME_ID), we = now(CLOCK_MONOTONIC);
        if (block >= warmup) {
            unsigned i = block - warmup;
            cpu[i] = us(t, te); wall[i] = us(w, we);
            cpu_sum += cpu[i]; wall_sum += wall[i];
            unsigned active = pool.diagnostics.active_slots;
            if (active < minimum) minimum = active;
            if (active > maximum) maximum = active;
        }
        for (unsigned i = 0; i < frames; i++) {
            if (!isfinite(output[i].left) || !isfinite(output[i].right)
                || fabsf(output[i].left) > 1 || fabsf(output[i].right) > 1) exit(1);
            hash = tw_fnv1a64(&output[i], sizeof output[i], hash);
        }
    }
    if (getrusage(RUSAGE_SELF, &after)) exit(1);
    qsort(cpu, blocks, sizeof *cpu, compare);
    qsort(wall, blocks, sizeof *wall, compare);
    static const char *names[] = {"idle", "one", "eight", "full", "sustain-release", "unison2", "unison4", "unison8"};
    printf("%s,%u,%u,%u,%s,%u,%u,%u,%u,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%ld,%ld,%ld,%ld,%016" PRIx64 ",%.7g,%.7g,%" PRIu64 "\n",
        name, bcs, limit, frames, names[scenario], pass, unison, minimum, maximum,
        cpu_sum / blocks, cpu[blocks/2], cpu[blocks*99/100], cpu[blocks-1],
        wall_sum / blocks, wall[blocks/2], wall[blocks*99/100], wall[blocks-1],
        cpu_sum / ((double)blocks * frames / RATE * 1e6),
        before.ru_maxrss, after.ru_maxrss, after.ru_minflt-before.ru_minflt,
        after.ru_majflt-before.ru_majflt, hash, pool.output.diagnostics.pre_peak,
        pool.output.diagnostics.post_peak, pool.output.diagnostics.knee_hit_count);
    fflush(stdout);
    return hash;
}
int main(int argc, char **argv) {
    if (argc != 2 || strcmp(argv[1], "--run")) {
        puts("Usage: bench_ma_pool --run > results.csv\nRun alone under taskset; records affinity, CPU/wall block timing, RSS high-water, page faults and repeated PCM hashes.");
        return argc == 1 || (argc == 2 && !strcmp(argv[1], "--help")) ? 0 : 1;
    }
    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    if (sched_getaffinity(0, sizeof cpus, &cpus)) { perror("sched_getaffinity"); return 1; }
    struct utsname host;
    if (uname(&host)) return 1;
    printf("# host=%s kernel=%s arch=%s compiler=%s rate=%u warmup=%u blocks=%u pool_bytes=%zu affinity=",
        host.nodename, host.release, host.machine, __VERSION__, RATE,
        warmup_blocks(), measured_blocks(), sizeof pool);
    for (unsigned i = 0; i < CPU_SETSIZE; i++) if (CPU_ISSET(i, &cpus)) printf("%u;", i);
    puts("\npatch,bcs,limit,frames,scenario,pass,unison,min_active,max_active,cpu_mean_us,cpu_p50_us,cpu_p99_us,cpu_max_us,wall_mean_us,wall_p50_us,wall_p99_us,wall_max_us,cpu_audio_ratio,rss_before_kib,rss_after_kib,minor_faults,major_faults,hash,pre_peak,post_peak,safety_hits");
    const ma_patch *patches[] = {&ma_patch_tepih, &ma_patch_lead, &ma_patch_raster, &ma_patch_prizma, &ma_patch_granica};
    const char *names[] = {"Tepih", "Lead", "Raster", "Prizma", "Granica"};
    const char *patch_filter = getenv("MA_POOL_PATCH");
    const char *bcs_filter = getenv("MA_POOL_BCS");
    const char *limit_filter = getenv("MA_POOL_LIMIT");
    const char *frames_filter = getenv("MA_POOL_FRAMES");
    const char *scenario_filter = getenv("MA_POOL_SCENARIO");
    for (unsigned p = 0; p < 5; p++)
        for (unsigned bcs = 0; bcs < 2; bcs++) {
            if (patch_filter && strcmp(patch_filter, names[p])) continue;
            if (bcs_filter && (unsigned)strtoul(bcs_filter, NULL, 10) != bcs) continue;
            ma_patch patch = *patches[p];
            patch.bcs_amount = bcs ? ma_patch_granica.bcs_amount : 0;
            patch.bcs_regime = ma_patch_granica.bcs_regime;
            for (unsigned limit = 16; limit <= 64; limit *= 2)
                for (unsigned frames = 128; frames <= 512; frames *= 2)
                    for (unsigned scenario = 0; scenario < 8; scenario++) {
                        if (limit_filter && limit != (unsigned)strtoul(limit_filter, NULL, 10)) continue;
                        if (frames_filter && frames != (unsigned)strtoul(frames_filter, NULL, 10)) continue;
                        if (scenario_filter && scenario != (unsigned)strtoul(scenario_filter, NULL, 10)) continue;
                        uint64_t first = measure(names[p], &patch, bcs, limit, frames, scenario, 1);
                        uint64_t second = measure(names[p], &patch, bcs, limit, frames, scenario, 2);
                        if (first != second) { fputs("PCM mismatch\n", stderr); return 1; }
                    }
        }
}
