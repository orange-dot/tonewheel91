/* MA3-PERF SIMD spike (2026-11-06): do the per-voice 2x ladder and the
 * 31-tap oversampling decimator stay bit-exact, and get faster, when eight
 * voices run as the eight lanes of one AVX2 vector?
 *
 * White-box: includes ma_voice.c and ma_filter.c and uses their scalar
 * functions as the reference. Not shipping code. The AVX2 kernels are
 * target-attributed (AVX2 only, no FMA) so every other function keeps the
 * production flags, including -ffp-contract=off. Every lane performs the
 * scalar code's float operations in the scalar code's order; the --check
 * mode compares bits.
 *
 *   spike_ma_simd --check   exact-bit equivalence, adversarial inputs
 *   spike_ma_simd --bench   cycles per voice-call, scalar vs 8 lanes
 *                           (pin with taskset; counters via perf_event_open)
 *
 * Production caveat this spike does not solve: the decimator kernel assumes
 * one ring position shared by all lanes; real voices wake at different
 * times, so a pooled layout must align or rotate rings on wake. */
#define _GNU_SOURCE
#include "../src/ma_voice.c"
#include "../src/ma_filter.c"
#include <immintrin.h>
#include <linux/perf_event.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

enum { LANES = 8, BENCH_CALLS = 4096, BENCH_TRIALS = 31 };
#define AVX2 __attribute__((target("avx2")))

static uint32_t bits_of(float value) {
    uint32_t b; memcpy(&b, &value, sizeof b); return b;
}

/* xorshift32: deterministic test data */
static uint32_t rng_state = UINT32_C(0x4d414d55);
static uint32_t rng(void) {
    rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5;
    return rng_state;
}
static float uniform(float lo, float hi) {
    return lo + (hi - lo) * (float)(rng() >> 8) * 0x1p-24f;
}
/* Mostly ordinary values; sometimes the edges a ladder or FIR can meet. */
static float adversarial(float ordinary) {
    static const float edge[] = {
        0.0f, -0.0f, 3.0f, -3.0f, 3.0000002f, -3.0000002f, FLT_MAX, -FLT_MAX,
        FLT_TRUE_MIN, -FLT_TRUE_MIN, FLT_MIN, 1e30f, -1e30f, 1e19f,
        __builtin_inff(), -__builtin_inff(), __builtin_nanf(""),
    };
    uint32_t r = rng();
    if (r % 64 == 0) return edge[(r >> 8) % (sizeof edge / sizeof *edge)];
    if (r % 64 == 1) { uint32_t b = rng(); float f; memcpy(&f, &b, sizeof f); return f; }
    return ordinary;
}

/* ---- 8-lane ladder: render_ladder_2x per lane ---- */
typedef struct { __m256 state[4], y4_guess; uint32_t reset_count[LANES]; } ladder8;

AVX2 static inline __m256 sat8(__m256 x) {
    const __m256 hi = _mm256_set1_ps(3.0f), lo = _mm256_set1_ps(-3.0f);
    __m256 above = _mm256_cmp_ps(x, hi, _CMP_GT_OQ);   /* (x > 3) ? 3 :      */
    __m256 below = _mm256_cmp_ps(x, lo, _CMP_LT_OQ);   /* (x < -3) ? -3 : x  */
    x = _mm256_blendv_ps(_mm256_blendv_ps(x, lo, below), hi, above);
    /* x * (27 + x * x) / (27 + 9 * x * x), with 9 * x * x == (9 * x) * x */
    __m256 num = _mm256_mul_ps(x, _mm256_add_ps(_mm256_set1_ps(27.0f), _mm256_mul_ps(x, x)));
    __m256 den = _mm256_add_ps(_mm256_set1_ps(27.0f),
                               _mm256_mul_ps(_mm256_mul_ps(_mm256_set1_ps(9.0f), x), x));
    return _mm256_div_ps(num, den);
}

AVX2 static inline __m256 in_range8(__m256 a) {   /* a >= -FLT_MAX && a <= FLT_MAX */
    return _mm256_and_ps(_mm256_cmp_ps(a, _mm256_set1_ps(-FLT_MAX), _CMP_GE_OQ),
                         _mm256_cmp_ps(a, _mm256_set1_ps(FLT_MAX), _CMP_LE_OQ));
}

AVX2 static __m256 ladder8_step(ladder8 *l, __m256 input, __m256 g, __m256 drive, __m256 resonance) {
    /* 1 + 3 * drive * drive == 1 + ((3 * drive) * drive) */
    __m256 input_gain = _mm256_add_ps(_mm256_set1_ps(1.0f),
        _mm256_mul_ps(_mm256_mul_ps(_mm256_set1_ps(3.0f), drive), drive));
    __m256 feedback = _mm256_mul_ps(_mm256_set1_ps(4.65f), resonance);
    __m256 y[4], v[4], next[4], y4 = l->y4_guess;
    for (int iteration = 0; iteration < 2; iteration++) {
        __m256 u = sat8(_mm256_sub_ps(_mm256_mul_ps(input_gain, input),
                                      _mm256_mul_ps(feedback, y4)));
        for (int stage = 0; stage < 4; stage++) {
            v[stage] = _mm256_mul_ps(_mm256_sub_ps(sat8(u), l->state[stage]), g);
            y[stage] = _mm256_add_ps(v[stage], l->state[stage]);
            u = y[stage];
        }
        y4 = y[3];
    }
    __m256 finite = in_range8(y4);
    for (int stage = 0; stage < 4; stage++) {
        next[stage] = _mm256_add_ps(y[stage], v[stage]);
        finite = _mm256_and_ps(finite, _mm256_and_ps(in_range8(y[stage]),
                     _mm256_and_ps(in_range8(v[stage]), in_range8(next[stage]))));
    }
    const __m256 zero = _mm256_setzero_ps();
    for (int stage = 0; stage < 4; stage++)
        l->state[stage] = _mm256_blendv_ps(zero, next[stage], finite);
    l->y4_guess = _mm256_blendv_ps(zero, y4, finite);
    unsigned reset = ~(unsigned)_mm256_movemask_ps(finite) & 0xffu;
    while (reset) { l->reset_count[__builtin_ctz(reset)]++; reset &= reset - 1; }
    return _mm256_blendv_ps(zero, y[3], finite);
}

/* ---- 8-lane 31-tap decimator: push_oversample_history + decimate_oversample ---- */
typedef struct { alignas(32) float history[31][LANES]; uint8_t position; } fir8;

AVX2 static void fir8_push(fir8 *f, __m256 sample) {
    _mm256_store_ps(f->history[f->position], sample);
    if (++f->position == 31) f->position = 0;
}
AVX2 static __m256 fir8_decimate(const fir8 *f) {
    __m256 output = _mm256_setzero_ps();
    int index = f->position;
    for (int tap = 0; tap < 31; tap++) {          /* every tap, zeros included */
        if (--index < 0) index = 30;
        output = _mm256_add_ps(output, _mm256_mul_ps(_mm256_set1_ps(OSC_HALFBAND_31[tap]),
                                                     _mm256_load_ps(f->history[index])));
    }
    return output;
}

/* ---- scalar side: eight separate voices, as the pool holds them ---- */
static ma_synth voice[LANES];
static ma_render_controls controls[LANES];

/* Everything that touches __m256 is AVX2-attributed; the scalar reference
 * may be inlined there, which keeps its IEEE results (no FMA is enabled). */
AVX2 static int check_ladder(unsigned calls) {
    memset(voice, 0, sizeof voice);
    ladder8 l = {};
    alignas(32) float in[LANES], g[LANES], drive[LANES], res[LANES], simd[LANES],
        sv[4][LANES], sy[LANES];
    uint64_t resets = 0;
    for (unsigned n = 0; n < calls; n++) {
        for (int lane = 0; lane < LANES; lane++) {
            if (n % 97 == 0) {   /* controls are smoothed; they move occasionally */
                controls[lane].filter_drive = adversarial(uniform(0.0f, 1.0f));
                controls[lane].filter_resonance = adversarial(uniform(0.0f, 1.0f));
            }
            drive[lane] = controls[lane].filter_drive;
            res[lane] = controls[lane].filter_resonance;
            in[lane] = adversarial(uniform(-1.5f, 1.5f));
            g[lane] = adversarial(filter_prewarp(uniform(20.0f, 20000.0f), 48000.0f));
        }
        _mm256_store_ps(simd, ladder8_step(&l, _mm256_load_ps(in), _mm256_load_ps(g),
                                           _mm256_load_ps(drive), _mm256_load_ps(res)));
        for (int stage = 0; stage < 4; stage++) _mm256_store_ps(sv[stage], l.state[stage]);
        _mm256_store_ps(sy, l.y4_guess);
        for (int lane = 0; lane < LANES; lane++) {
            float scalar = render_ladder_2x(&voice[lane], &controls[lane], in[lane], g[lane]);
            bool same = bits_of(scalar) == bits_of(simd[lane])
                && bits_of(voice[lane].ladder.y4_guess) == bits_of(sy[lane])
                && voice[lane].ladder.reset_count == l.reset_count[lane];
            for (int stage = 0; stage < 4; stage++)
                same = same && bits_of(voice[lane].ladder.state[stage]) == bits_of(sv[stage][lane]);
            if (!same) {
                fprintf(stderr, "ladder mismatch at call %u lane %d: scalar %08x simd %08x\n",
                        n, lane, bits_of(scalar), bits_of(simd[lane]));
                return 1;
            }
        }
    }
    for (int lane = 0; lane < LANES; lane++) resets += l.reset_count[lane];
    printf("ladder: %u calls x %d lanes, output/state/y4/reset bits identical; %llu resets exercised\n",
           calls, LANES, (unsigned long long)resets);
    return 0;
}

AVX2 static int check_fir(unsigned pushes) {
    static fir8 f;
    memset(&f, 0, sizeof f);
    float history[LANES][31] = {};
    alignas(32) float in[LANES], simd[LANES];
    uint8_t position[LANES] = {};
    for (unsigned n = 0; n < pushes; n++) {
        for (int lane = 0; lane < LANES; lane++) in[lane] = adversarial(uniform(-1.0f, 1.0f));
        fir8_push(&f, _mm256_load_ps(in));
        _mm256_store_ps(simd, fir8_decimate(&f));
        for (int lane = 0; lane < LANES; lane++) {
            push_oversample_history(history[lane], &position[lane], in[lane]);
            float scalar = decimate_oversample(history[lane], position[lane]);
            if (bits_of(scalar) != bits_of(simd[lane]) || position[lane] != f.position) {
                fprintf(stderr, "fir mismatch at push %u lane %d: scalar %08x simd %08x\n",
                        n, lane, bits_of(scalar), bits_of(simd[lane]));
                return 1;
            }
        }
    }
    printf("fir31: %u pushes x %d lanes, every decimated output bit-identical\n", pushes, LANES);
    return 0;
}

/* ---- cycles: one group read (cycles, instructions), user space, this thread ---- */
static int pmu_fd = -1;
static int pmu_event(uint64_t config, int group) {
    struct perf_event_attr a; memset(&a, 0, sizeof a);
    a.type = PERF_TYPE_HARDWARE; a.size = sizeof a; a.config = config;
    a.disabled = group < 0; a.exclude_kernel = 1; a.exclude_hv = 1;
    a.read_format = PERF_FORMAT_GROUP;
    return (int)syscall(SYS_perf_event_open, &a, 0, -1, group, 0UL);
}
static void pmu_open(void) {
    int leader = pmu_event(PERF_COUNT_HW_CPU_CYCLES, -1);
    if (leader < 0 || pmu_event(PERF_COUNT_HW_INSTRUCTIONS, leader) < 0
        || ioctl(leader, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP)) {
        perror("perf_event_open"); exit(2);
    }
    pmu_fd = leader;
}
typedef struct { uint64_t cycles, instructions; } counts;
static counts pmu_now(void) {
    uint64_t v[3];
    if (read(pmu_fd, v, sizeof v) != (ssize_t)sizeof v || v[0] != 2) { perror("pmu read"); exit(1); }
    return (counts){ v[1], v[2] };
}
static counts since(counts a) {
    counts b = pmu_now();
    return (counts){ b.cycles - a.cycles, b.instructions - a.instructions };
}
static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return (x > y) - (x < y);
}

static alignas(32) float bench_in[BENCH_CALLS][LANES], bench_g[BENCH_CALLS][LANES],
    out_scalar[BENCH_CALLS][LANES], out_simd[BENCH_CALLS][LANES];
static float bench_drive[LANES], bench_res[LANES];

static void bench_data(void) {
    float x[LANES] = {};
    for (unsigned n = 0; n < BENCH_CALLS; n++)
        for (int lane = 0; lane < LANES; lane++) {
            x[lane] = 0.98f * x[lane] + uniform(-0.2f, 0.2f);   /* audio-like walk */
            bench_in[n][lane] = x[lane];
            bench_g[n][lane] = filter_prewarp(uniform(60.0f, 12000.0f), 48000.0f);
        }
    for (int lane = 0; lane < LANES; lane++) {
        bench_drive[lane] = controls[lane].filter_drive = uniform(0.0f, 0.6f);
        bench_res[lane] = controls[lane].filter_resonance = uniform(0.0f, 0.8f);
    }
}

/* Scalar trials: production flags, no AVX2 in scope. */
static counts trial_ladder_scalar(void) {
    memset(voice, 0, sizeof voice);
    counts a = pmu_now();
    for (unsigned n = 0; n < BENCH_CALLS; n++)
        for (int lane = 0; lane < LANES; lane++)
            out_scalar[n][lane] = render_ladder_2x(&voice[lane], &controls[lane],
                                                   bench_in[n][lane], bench_g[n][lane]);
    return since(a);
}
static counts trial_fir_scalar(void) {
    static float history[LANES][31];
    static uint8_t position[LANES];
    memset(history, 0, sizeof history); memset(position, 0, sizeof position);
    counts a = pmu_now();
    for (unsigned n = 0; n < BENCH_CALLS; n++)
        for (int lane = 0; lane < LANES; lane++) {
            push_oversample_history(history[lane], &position[lane], bench_in[n][lane]);
            out_scalar[n][lane] = decimate_oversample(history[lane], position[lane]);
        }
    return since(a);
}
AVX2 static counts trial_ladder_simd(void) {
    ladder8 l = {};
    __m256 drive = _mm256_loadu_ps(bench_drive), res = _mm256_loadu_ps(bench_res);
    counts a = pmu_now();
    for (unsigned n = 0; n < BENCH_CALLS; n++)
        _mm256_store_ps(out_simd[n], ladder8_step(&l, _mm256_load_ps(bench_in[n]),
                                                  _mm256_load_ps(bench_g[n]), drive, res));
    return since(a);
}
AVX2 static counts trial_fir_simd(void) {
    static fir8 f;
    memset(&f, 0, sizeof f);
    counts a = pmu_now();
    for (unsigned n = 0; n < BENCH_CALLS; n++) {
        fir8_push(&f, _mm256_load_ps(bench_in[n]));
        _mm256_store_ps(out_simd[n], fir8_decimate(&f));
    }
    return since(a);
}

static double median_cycles_per_voice_call(counts (*trial)(void), double *instructions) {
    uint64_t cycles[BENCH_TRIALS], ins[BENCH_TRIALS];
    (void)trial();                                        /* warm caches */
    for (int t = 0; t < BENCH_TRIALS; t++) {
        counts c = trial(); cycles[t] = c.cycles; ins[t] = c.instructions;
    }
    qsort(cycles, BENCH_TRIALS, sizeof *cycles, cmp_u64);
    qsort(ins, BENCH_TRIALS, sizeof *ins, cmp_u64);
    *instructions = (double)ins[BENCH_TRIALS / 2] / (BENCH_CALLS * LANES);
    return (double)cycles[BENCH_TRIALS / 2] / (BENCH_CALLS * LANES);
}

static int bench(void) {
    pmu_open();
    bench_data();
    struct { const char *name; counts (*scalar)(void), (*simd)(void); } pair[] = {
        { "ladder_2x", trial_ladder_scalar, trial_ladder_simd },
        { "fir31", trial_fir_scalar, trial_fir_simd },
    };
    printf("# %d calls x %d lanes per trial, %d trials, median; cycles/instructions per voice-call\n",
           BENCH_CALLS, LANES, BENCH_TRIALS);
    puts("kernel,scalar_cycles,simd_cycles,speedup,scalar_instructions,simd_instructions,outputs_identical");
    for (unsigned i = 0; i < sizeof pair / sizeof *pair; i++) {
        double si, vi;
        double sc = median_cycles_per_voice_call(pair[i].scalar, &si);
        double vc = median_cycles_per_voice_call(pair[i].simd, &vi);
        bool same = true;
        for (unsigned n = 0; n < BENCH_CALLS && same; n++)
            for (int lane = 0; lane < LANES; lane++)
                same = same && bits_of(out_scalar[n][lane]) == bits_of(out_simd[n][lane]);
        printf("%s,%.1f,%.1f,%.2f,%.1f,%.1f,%s\n", pair[i].name, sc, vc, sc / vc, si, vi,
               same ? "yes" : "NO");
        if (!same) return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2 || (strcmp(argv[1], "--check") && strcmp(argv[1], "--bench"))) {
        puts("Usage: spike_ma_simd --check|--bench");
        return argc == 1 ? 0 : 1;
    }
    if (!__builtin_cpu_supports("avx2")) {
        puts("spike_ma_simd: CPU has no AVX2; skipped");
        return 0;
    }
    if (!strcmp(argv[1], "--bench")) return bench();
    return check_ladder(2000000) || check_fir(2000000);
}
