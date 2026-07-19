/* tonewheel91 M3 exhibit: taper wired in, robbing on real wires.
 *
 * Robbing sweep: every (bus, wheel) collision site — keys of one bus
 * folding onto one wheel — is measured through the public API: each key's
 * solo target is its taper gain, the group target is the merged
 * contribution, and merge_ratio = group / coherent sum. The delta against
 * the flat placeholder law a(k) = 4k/(k+3) (merge factor a(k)/k) is what
 * M3 changed: constants.md 6.1 sizes it at +0.97..+1.78 dB (mean +1.19)
 * over 53 sites. Targets are the model quantity; the contacts exhibit
 * shows steady RMS tracking them on the anchor site.
 *
 * Taper sweep: a 16'-only chromatic walk up the compass, 150 ms per key,
 * so the -10 dB foldback pad and the +7 dB treble lift are audible.
 * By-ear verification of taper and the new robbing stays open.
 */
#include <math.h>
#include <stdio.h>
#include "../src/tonewheel.h"
#include "wav.h"

#define RATE 48000
#define STEP 7200 /* 150 ms per key */

static float sweep[TW_KEYS * STEP];

/* Press notes at vel 127 (zero stagger), run past the bounce window,
 * read the settled target on one wheel. */
static double settled_target(const int *keys, int n, int bus, int wheel) {
    tw_organ o;
    tw_organ_init(&o, RATE);
    /* M7: pin the idealized reference (wear 0) — this exhibit's recorded
     * signatures predate the wear stage and must stay bit-stable. */
    tw_organ_set_wear(&o, 0.0f);
    uint8_t reg[TW_DRAWBARS] = { 0 };
    reg[bus] = 8;
    tw_organ_set_registration(&o, reg);
    for (int i = 0; i < n; i++) tw_organ_note(&o, keys[i] + 35, true, 127);
    for (int i = 0; i < 480; i++) (void)tw_organ_tick(&o);
    return o.gen.keyed_target[wheel - 1];
}

static void render_sweep(float *dst, long n) {
    tw_organ o;
    tw_organ_init(&o, RATE);
    tw_organ_set_wear(&o, 0.0f); /* M7: idealized reference */
    static const uint8_t reg16[TW_DRAWBARS] = { 8, 0, 0, 0, 0, 0, 0, 0, 0 };
    tw_organ_set_registration(&o, reg16);
    for (long i = 0; i < n; i++) {
        long seg = i % STEP;
        int key = (int)(i / STEP) + 1;
        if (seg == 0) tw_organ_note(&o, key + 35, true, 127);
        if (seg == STEP - STEP / 5) tw_organ_note(&o, key + 35, false, 0);
        dst[i] = tw_organ_tick(&o) * 0.25f;
    }
}

int main(void) {
    printf("tonewheel91 M3 exhibit -- taper and the robbing law\n\n");

    int sites = 0, triples = 0;
    int per_bus[TW_DRAWBARS] = { 0 };
    double min_db = 1e9, max_db = -1e9, sum_db = 0.0;
    for (int b = 0; b < TW_DRAWBARS; b++)
        for (int w = TW_WHEEL_MIN; w <= TW_WHEEL_MAX; w++) {
            int keys[TW_TAP_MAX], n = 0;
            for (int k = 1; k <= TW_KEYS; k++)
                if (tw_wheel_index(k, b) == w && n < TW_TAP_MAX) keys[n++] = k;
            if (n < 2) continue;
            double coherent = 0.0;
            for (int i = 0; i < n; i++)
                coherent += settled_target(&keys[i], 1, b, w);
            double mr = settled_target(keys, n, b, w) / coherent;
            double flat = 4.0 / (double)(n + 3); /* a(n)/n */
            double db = 20.0 * log10(mr / flat);
            if (db < min_db) min_db = db;
            if (db > max_db) max_db = db;
            sum_db += db;
            sites++;
            per_bus[b]++;
            if (n == 3) triples++;
        }
    double mean_db = sum_db / sites;
    printf("  robbing sweep over every same-(bus, wheel) collision site:\n");
    printf("    sites: %d (per bus", sites);
    for (int b = 0; b < TW_DRAWBARS; b++) printf(" %d", per_bus[b]);
    printf("), %d of them three-key\n", triples);
    printf("    placeholder a(k) over-robbed by +%.2f..+%.2f dB"
           " (mean +%.2f)\n", min_db, max_db, mean_db);

    static const int anchor[2] = { 1, 13 }; /* 16' bus, wheel 13 */
    double g1 = settled_target(&anchor[0], 1, 0, 13);
    double g13 = settled_target(&anchor[1], 1, 0, 13);
    double both = settled_target(anchor, 2, 0, 13);
    printf("    anchor 16'/wheel 13: solo %.4f + %.4f, pair %.4f"
           " (merge_ratio %.4f, a(2)-equivalent %.3f)\n",
           g1, g13, both, both / (g1 + g13), 2.0 * both / (g1 + g13));

    render_sweep(sweep, TW_KEYS * STEP);
    uint64_t h1 = tw_fnv1a64(sweep, sizeof sweep, 0);
    render_sweep(sweep, TW_KEYS * STEP);
    uint64_t h2 = tw_fnv1a64(sweep, sizeof sweep, 0);
    printf("\n  taper sweep (16' chromatic, 150 ms/key):"
           " FNV64 %016llx %s\n", (unsigned long long)h1,
           h1 == h2 ? "(two runs identical)" : "MISMATCH");

    int rc = wav_write_f32("build/m3_taper_sweep.wav", sweep,
                           TW_KEYS * STEP, RATE, 1);
    printf("  wav: build/m3_taper_sweep.wav%s\n", rc ? " (WRITE FAILED)" : "");

    int verdict = sites == 53 && triples == 7
               && min_db > 0.92 && min_db < 1.02
               && max_db > 1.73 && max_db < 1.83
               && mean_db > 1.14 && mean_db < 1.24
               && h1 == h2 && rc == 0;
    printf("\n  exhibit verdict: %s\n", verdict ? "PASS" : "FAIL");
    return verdict ? 0 : 1;
}
