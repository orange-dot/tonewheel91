/* tonewheel91 M2 exhibit: contacts, click, and the merge law.
 *
 * Click: full registration, one key pressed hard (vel 127, ~0 ms stagger)
 * vs soft (vel 25, ~12 ms stagger). The click metric is the peak of the
 * second difference (12 dB/oct high-pass proxy) in the attack window
 * against the sustained tone.
 *
 * Merge law: keys 36 and 48 collide on wheel 13 on the 16' bus by
 * foldback (100- and 50-ohm taper wires) -> the pair merges to
 * merge_ratio ~0.9416 of the coherent sum of the two solo levels, while
 * a non-colliding pair power-sums. constants.md sections 6 and 6.1.
 */
#include <math.h>
#include <stdio.h>
#include <time.h>
#include "../src/tonewheel.h"
#include "wav.h"

#define RATE 48000

static float click127[120000], click25[120000], merged[96000];

/* Click is measured on a dark registration and a low key, where it
 * musically stands out: high sustained partials would swamp the
 * second-difference metric. */
static void render_click(float *dst, long n, int vel) {
    tw_organ o;
    tw_organ_init(&o, RATE);
    /* M7: pin the idealized reference (wear 0) — this exhibit's recorded
     * signatures predate the wear stage and must stay bit-stable. */
    tw_organ_set_wear(&o, 0.0f);
    static const uint8_t dark[TW_DRAWBARS] = { 8, 8, 8, 8, 0, 0, 0, 0, 0 };
    tw_organ_set_registration(&o, dark);
    for (long i = 0; i < n; i++) {
        if (i == RATE / 2) tw_organ_note(&o, 36, true, vel);
        if (i == 3 * RATE / 2) tw_organ_note(&o, 36, false, 0);
        dst[i] = tw_organ_tick(&o) * 0.15f;
    }
}

static double peak_d2(const float *x, long from, long to) {
    double peak = 0.0;
    for (long i = from + 2; i < to; i++) {
        double d2 = fabs((double)x[i] - 2.0 * x[i - 1] + x[i - 2]);
        if (d2 > peak) peak = d2;
    }
    return peak;
}

static double steady_rms(tw_organ *o, long settle, long window) {
    for (long i = 0; i < settle; i++) (void)tw_organ_tick(o);
    double acc = 0.0;
    for (long i = 0; i < window; i++) {
        double s = tw_organ_tick(o);
        acc += s * s;
    }
    return sqrt(acc / (double)window);
}

static double pair_rms(const int *notes, int n, float *wav, long frames) {
    tw_organ o;
    tw_organ_init(&o, RATE);
    tw_organ_set_wear(&o, 0.0f); /* M7: idealized reference */
    static const uint8_t reg16[TW_DRAWBARS] = { 8, 0, 0, 0, 0, 0, 0, 0, 0 };
    tw_organ_set_registration(&o, reg16);
    for (int i = 0; i < n; i++) tw_organ_note(&o, notes[i], true, 127);
    if (wav) {
        for (long i = 0; i < frames; i++) wav[i] = tw_organ_tick(&o) * 0.3f;
        double acc = 0.0;
        for (long i = frames / 2; i < frames; i++) acc += (double)wav[i] * wav[i];
        return sqrt(acc / (double)(frames - frames / 2)) / 0.3;
    }
    return steady_rms(&o, RATE / 2, RATE);
}

static void run_script(float *dst, long n) {
    tw_organ o;
    tw_organ_init(&o, RATE);
    tw_organ_set_wear(&o, 0.0f); /* M7: idealized reference */
    for (long i = 0; i < n; i++) {
        if (i == 4800) tw_organ_note(&o, 57, true, 96);
        if (i == 6000) tw_organ_note(&o, 64, true, 40);
        if (i == 20000) tw_organ_set_drawbar(&o, 8, 7);
        if (i == 30000) tw_organ_set_swell(&o, 0.4f);
        if (i == 40000) tw_organ_note(&o, 57, false, 0);
        if (i == 52000) tw_organ_note(&o, 64, false, 0);
        dst[i] = tw_organ_tick(&o);
    }
}

int main(void) {
    printf("tonewheel91 M2 exhibit -- contacts, click, merge law\n\n");

    render_click(click127, 120000, 127);
    render_click(click25, 120000, 25);
    double sus = peak_d2(click127, (long)(0.8 * RATE), (long)(1.4 * RATE));
    double atk127 = peak_d2(click127, (long)(0.5 * RATE), (long)(0.506 * RATE));
    double sus25 = peak_d2(click25, (long)(0.8 * RATE), (long)(1.4 * RATE));
    double atk25 = peak_d2(click25, (long)(0.5 * RATE), (long)(0.52 * RATE));
    double click_db_127 = 20.0 * log10(atk127 / sus);
    double click_db_25 = 20.0 * log10(atk25 / sus25);
    printf("  click (2nd-difference peak, attack vs sustain):\n");
    printf("    vel 127 (stagger ~0 ms):  +%.1f dB\n", click_db_127);
    printf("    vel 25  (stagger ~12 ms): +%.1f dB\n", click_db_25);

    static const int k1[1] = { 36 }, k13[1] = { 48 }, k25[1] = { 60 };
    static const int same[2] = { 36, 48 };  /* both wheel 13, 16' bus */
    static const int dist[2] = { 36, 60 };  /* distinct wheels        */
    double r1 = pair_rms(k1, 1, NULL, 0);
    double r13 = pair_rms(k13, 1, NULL, 0);
    double r25 = pair_rms(k25, 1, NULL, 0);
    double r2 = pair_rms(same, 2, merged, 96000);
    double r3 = pair_rms(dist, 2, NULL, 0);
    double mr = r2 / (r1 + r13);            /* colliding: coherent sum robs */
    double ps = r3 / sqrt(r1 * r1 + r25 * r25); /* distinct: power sum      */
    printf("\n  merge law (16' bus, steady RMS ratios):\n");
    printf("    same wheel pair / coherent sum: %.4f (law merge_ratio 0.9416,"
           " a(2)-equivalent %.3f)\n", mr, 2.0 * mr);
    printf("    distinct pair / power sum:      %.4f (independent sources"
           " = 1.000)\n", ps);

    static float s1[60000], s2[60000];
    run_script(s1, 60000);
    run_script(s2, 60000);
    uint64_t h1 = tw_fnv1a64(s1, sizeof s1, 0);
    uint64_t h2 = tw_fnv1a64(s2, sizeof s2, 0);
    printf("\n  scripted determinism: FNV64 %016llx %s\n",
           (unsigned long long)h1, h1 == h2 ? "(two runs identical)" : "MISMATCH");

    tw_organ o;
    tw_organ_init(&o, RATE);
    tw_organ_set_wear(&o, 0.0f); /* M7: idealized reference */
    tw_organ_note(&o, 60, true, 127);
    tw_organ_note(&o, 64, true, 127);
    tw_organ_note(&o, 67, true, 127);
    volatile float sink = 0.0f;
    struct timespec t0, t1;
    int base = timespec_get(&t0, TIME_MONOTONIC) ? TIME_MONOTONIC : TIME_UTC;
    timespec_get(&t0, base);
    for (long i = 0; i < 10L * RATE; i++) sink += tw_organ_tick(&o);
    timespec_get(&t1, base);
    (void)sink;
    double ns = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / (10.0 * RATE);
    printf("  cost: %.0f ns/frame, %.2f%% of one core at %d Hz\n",
           ns, ns * RATE / 1e7, RATE);

    int rc = 0;
    rc |= wav_write_f32("build/m2_click_vel127.wav", click127, 120000, RATE, 1);
    rc |= wav_write_f32("build/m2_click_vel25.wav", click25, 120000, RATE, 1);
    rc |= wav_write_f32("build/m2_merge_pair.wav", merged, 96000, RATE, 1);
    printf("\n  wavs: build/m2_click_vel127.wav, m2_click_vel25.wav,"
           " m2_merge_pair.wav%s\n", rc ? " (WRITE FAILED)" : "");

    int verdict = click_db_127 > 10.0 && fabs(mr - 0.9416) < 0.01
               && fabs(ps - 1.0) < 0.02 && h1 == h2 && rc == 0;
    printf("\n  exhibit verdict: %s\n", verdict ? "PASS" : "FAIL");
    return verdict ? 0 : 1;
}
