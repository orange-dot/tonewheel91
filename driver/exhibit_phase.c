/* tonewheel91 M1 founding exhibit: shared-wheel phase coherence.
 *
 * A: key 25 (8') and key 37 (16') both tap wheel 37 through the shared
 *    generator — the taps reinforce and the envelope stays flat.
 * B: the same two notes as independent oscillators detuned +/-1.5 cents
 *    (the per-voice additive idiom) — the pair beats.
 *
 * Bonus render: a C-major triad at registration 888000000 (its own
 * shared-wheel collision: C's 5-1/3' and G's 8' are both wheel 44).
 */
#include <stdio.h>
#include <math.h>
#include <time.h>
#include "../src/tonewheel.h"
#include "wav.h"

#define RATE     48000
#define SECS     8
#define FRAMES   (RATE * SECS)
#define CHFRAMES (4 * RATE)

static const double TAU = 6.283185307179586;
static const double DETUNE_CENTS = 1.5;

static float render_a[FRAMES], render_b[FRAMES], chord[CHFRAMES];

static void run_shared(float *dst) {
    tw_generator g;
    tw_generator_init(&g, RATE, 0.001f);
    float t[TW_WHEELS] = { 0 };
    t[tw_wheel_index(25, 2) - 1] += TW_DRAWBAR_GAIN[8];
    t[tw_wheel_index(37, 0) - 1] += TW_DRAWBAR_GAIN[8];
    tw_generator_set_keyed_targets(&g, t);
    for (long i = 0; i < FRAMES; i++)
        dst[i] = tw_generator_tick(&g).keyed * 0.25f;
}

static void run_detuned(float *dst) {
    double f = tw_wheel_freq_hz(37);
    double f1 = f * pow(2.0, +DETUNE_CENTS / 1200.0);
    double f2 = f * pow(2.0, -DETUNE_CENTS / 1200.0);
    double p1 = 0.0, p2 = 0.0;
    for (long i = 0; i < FRAMES; i++) {
        dst[i] = 0.25f * (float)(sin(TAU * p1) + sin(TAU * p2));
        p1 += f1 / RATE;
        p2 += f2 / RATE;
        if (p1 >= 1.0) p1 -= 1.0;
        if (p2 >= 1.0) p2 -= 1.0;
    }
}

static void run_chord(float *dst) {
    static const int keys[3] = { 25, 29, 32 };
    static const int reg[TW_DRAWBARS] = { 8, 8, 8, 0, 0, 0, 0, 0, 0 };
    tw_generator g;
    tw_generator_init(&g, RATE, 0.001f);
    float t[TW_WHEELS] = { 0 };
    for (int k = 0; k < 3; k++)
        for (int d = 0; d < TW_DRAWBARS; d++)
            t[tw_wheel_index(keys[k], d) - 1] += TW_DRAWBAR_GAIN[reg[d]];
    tw_generator_set_keyed_targets(&g, t);
    float peak = 0.0f;
    for (long i = 0; i < CHFRAMES; i++) {
        dst[i] = tw_generator_tick(&g).keyed;
        if (tw_fabsf(dst[i]) > peak) peak = tw_fabsf(dst[i]);
    }
    float s = (peak > 0.0f) ? 0.9f / peak : 1.0f;
    for (long i = 0; i < CHFRAMES; i++) dst[i] *= s;
}

static double window_rms(const float *x, long from, long n) {
    double acc = 0.0;
    for (long i = from; i < from + n; i++) acc += (double)x[i] * x[i];
    return sqrt(acc / (double)n);
}

/* Envelope min/max over windows of `win` frames, skipping the first
 * window (attack). */
static void env_minmax(const float *x, long win, double *lo, double *hi) {
    *lo = 1e9;
    *hi = 0.0;
    for (long w = 1; w < FRAMES / win; w++) {
        double r = window_rms(x, w * win, win);
        if (r < *lo) *lo = r;
        if (r > *hi) *hi = r;
    }
}

static double env_depth_db(const float *x, long win) {
    double lo, hi;
    env_minmax(x, win, &lo, &hi);
    return 20.0 * log10(hi / (lo > 1e-9 ? lo : 1e-9));
}

static void envelope_report(const char *name, const float *x) {
    const long win = RATE / 4;
    double lo, hi;
    printf("  %s window RMS (0.25 s):", name);
    for (long w = 1; w < FRAMES / win; w++) {
        printf(w % 8 == 1 ? "\n    " : "  ");
        printf("%.4f", window_rms(x, w * win, win));
    }
    env_minmax(x, win, &lo, &hi);
    printf("\n  %s envelope: min %.5f  max %.5f  depth %.1f dB"
           " (fine 50 ms windows: %.1f dB)\n",
           name, lo, hi, 20.0 * log10(hi / (lo > 1e-9 ? lo : 1e-9)),
           env_depth_db(x, RATE / 20));
}

/* Beat rate from envelope minima on 50 ms windows. */
static double measured_beat_hz(const float *x) {
    const long win = RATE / 20;
    const int nwin = (int)(FRAMES / win);
    double rms[FRAMES / (RATE / 20)];
    double hi = 0.0;
    for (int w = 0; w < nwin; w++) {
        rms[w] = window_rms(x, (long)w * win, win);
        if (rms[w] > hi) hi = rms[w];
    }
    int first = -1, last = -1, count = 0;
    for (int w = 1; w + 1 < nwin; w++) {
        if (rms[w] < rms[w - 1] && rms[w] <= rms[w + 1] && rms[w] < 0.25 * hi) {
            if (first < 0) first = w;
            last = w;
            count++;
        }
    }
    if (count < 2) return 0.0;
    return (double)(count - 1) / ((double)(last - first) * win / RATE);
}

static uint64_t hash_f32(const float *x, long n) {
    return tw_fnv1a64(x, (size_t)n * sizeof *x, 0);
}

/* Steady-state cost of the constant-cost render, measured, not asserted. */
static void cost_report(void) {
    tw_generator g;
    tw_generator_init(&g, RATE, 0.001f);
    float t[TW_WHEELS] = { 0 };
    for (int d = 0; d < 3; d++)
        t[tw_wheel_index(25, d) - 1] += TW_DRAWBAR_GAIN[8];
    tw_generator_set_keyed_targets(&g, t);
    volatile float sink = 0.0f;
    const long n = 10L * RATE;
    struct timespec t0, t1;
    int base = timespec_get(&t0, TIME_MONOTONIC) ? TIME_MONOTONIC : TIME_UTC;
    timespec_get(&t0, base);
    for (long i = 0; i < n; i++) sink += tw_generator_tick(&g).keyed;
    timespec_get(&t1, base);
    (void)sink;
    double ns = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / (double)n;
    printf("\n  cost: %.0f ns/frame, %.2f%% of one core at %d Hz"
           " (10 s of audio, this host)\n", ns, ns * RATE / 1e7, RATE);
}

int main(void) {
    printf("tonewheel91 M1 exhibit -- shared-wheel phase coherence\n");
    printf("render: %d Hz, %d s, f32; wheel under test: 37 (%.4f Hz)\n",
           RATE, SECS, (double)tw_wheel_freq_hz(37));
    printf("taps: key 25 drawbar 8' and key 37 drawbar 16' -> wheel %d, %d\n\n",
           tw_wheel_index(25, 2), tw_wheel_index(37, 0));

    run_shared(render_a);
    run_detuned(render_b);
    run_chord(chord);

    envelope_report("A(shared)", render_a);
    printf("\n");
    envelope_report("B(detuned)", render_b);
    double depth_a = env_depth_db(render_a, RATE / 20);
    double depth_b = env_depth_db(render_b, RATE / 20);

    double f = tw_wheel_freq_hz(37);
    double predicted =
        f * (pow(2.0, DETUNE_CENTS / 1200.0) - pow(2.0, -DETUNE_CENTS / 1200.0));
    printf("\n  B beat: predicted %.4f Hz, measured %.4f Hz\n",
           predicted, measured_beat_hz(render_b));

    /* Two-run determinism: fresh generator, same script, same bits. */
    uint64_t h1 = hash_f32(render_a, FRAMES);
    run_shared(render_a);
    uint64_t h2 = hash_f32(render_a, FRAMES);
    printf("\n  FNV64 A run1 %016llx run2 %016llx  %s\n",
           (unsigned long long)h1, (unsigned long long)h2,
           h1 == h2 ? "identical" : "MISMATCH");
    printf("  FNV64 B      %016llx\n", (unsigned long long)hash_f32(render_b, FRAMES));
    printf("  FNV64 chord  %016llx\n", (unsigned long long)hash_f32(chord, CHFRAMES));

    cost_report();

    int rc = 0;
    rc |= wav_write_f32("build/m1_a_shared_generator.wav", render_a, FRAMES, RATE, 1);
    rc |= wav_write_f32("build/m1_b_detuned_pair.wav", render_b, FRAMES, RATE, 1);
    rc |= wav_write_f32("build/m1_chord_888000000.wav", chord, CHFRAMES, RATE, 1);
    printf("\n  wavs: build/m1_a_shared_generator.wav, build/m1_b_detuned_pair.wav,"
           " build/m1_chord_888000000.wav%s\n", rc ? " (WRITE FAILED)" : "");

    int verdict = (depth_a < 0.5 && depth_b > 20.0 && h1 == h2 && rc == 0);
    printf("\n  exhibit verdict: %s\n", verdict ? "PASS" : "FAIL");
    return verdict ? 0 : 1;
}
