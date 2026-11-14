/* Two cores on one supply (docs/ma-vco-plan.md, V4).
 * - A voice's phase difference against the coupled VCO-2 pair in SPICE
 *   (driver/spice/vco/vco4.py, build/vco/vco4/fits.json): the sawtooth's
 *   half height and where it jumps, at unison, the octave and the fifth, for
 *   the well-decoupled board and the soft rail.
 * - Within the lock range the pair locks at the jump; outside it it beats.
 * - The front end's lock range: the table, the supply between the two
 *   boards, the amount; no coupling at other intervals or without the law.
 * - The pool's vector render and its frame-by-frame tick give the same bits.
 * Hosted test. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../src/ma_internal.h"

static unsigned checks, failures;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

enum { RATE = 48000 };

/* build/vco/vco4/fits.json (2026-11-14): the sawtooth fitted to each SPICE
 * pair, lock half-range over the lower core's frequency, and the jump in the
 * core's phase convention (0 at the reset; SPICE timed mid-ramp, so octave
 * and fifth move by (q - p) / 2). */
static const struct { const char *name; int interval; float supply; double rel, jump; } SPICE[] = {
    { "unison, good board, A4", 0, 0, 1.075e-4, .000 },
    { "unison, soft rail, A4", 0, 1, 1.044e-3, .010 },
    { "octave, good board, A4", 12, 0, 1.615e-4, .990 },
    { "octave, soft rail, A4", 12, 1, 1.559e-3, .969 },
    { "fifth, good board, A4", 7, 0, 1.320e-4, .000 },
    { "fifth, soft rail, A4", 7, 1, 1.315e-3, .010 },
};

static ma_patch pair_patch(int interval, float fine_cents, float supply) {
    ma_patch p = ma_patch_tepih;
    p.vco1 = (ma_vco_controls){ .saw_level = 1, .pulse_width = .5f };
    p.vco2 = (ma_vco_controls){ .saw_level = 1, .pulse_width = .5f };
    p.vco2_level = 1; p.vco2_interval = interval; p.vco2_fine_cents = fine_cents;
    p.sync_amount = p.sync_softness = p.crossmod_amount = p.noise_level = 0;
    p.raster_mix = p.bcs_amount = p.mozaik_mix = p.formant_mix = p.grain_mix = 0;
    /* the filter envelope bends VCO1 up: let it die at once */
    p.filter_env_amount = 0;
    p.filter_adsr = (ma_adsr){ .attack_ms = 1, .decay_ms = 1, .sustain = 0, .release_ms = 1 };
    p.vco_analog = 1; p.vco_dead_time = 0; p.vco_supply = supply;
    return p;
}

static double turns(uint64_t q48) { return (double)q48 * 0x1p-48; }

/* q phi2 - p phi1 of a voice, frame by frame, and its rate against itself:
 * the sawtooth's half height (Hz) and jump, fitted as in vco4.py. */
static void measure(int interval, float fine_cents, float supply, double seconds, double *half,
                    double *jump, double *mean_rate, double *f_low, double *final_psi) {
    static ma_synth s;
    ma_patch p = pair_patch(interval, fine_cents, supply);
    ma_synth_init_patch(&s, RATE, &p);
    ma_synth_set_lfo(&s, 0, 5);
    ma_synth_set_glide(&s, false, 0);
    ma_synth_note_on(&s, 0, 69, 100);
    for (int i = 0; i < RATE / 4; i++) (void)ma_synth_tick(&s);
    int pp = interval == 12 ? 2 : interval == 7 ? 3 : 1, qq = interval == 7 ? 2 : 1;
    enum { BINS = 48 };
    double sum[BINS] = { 0 };
    int count[BINS] = { 0 };
    long n = (long)(seconds * RATE);
    double previous = 0, total = 0;
    for (long i = 0; i <= n; i++) {
        (void)ma_synth_tick(&s);
        double psi = qq * turns(s.oscillator2.phase_q48) - pp * turns(s.oscillator1.phase_q48);
        psi -= floor(psi);
        if (i) {
            double d = psi - previous;
            d -= floor(d + .5);                       /* the step, unwrapped */
            double mid = previous + .5 * d;
            mid -= floor(mid);
            int b = (int)(mid * BINS);
            sum[b] += d * RATE;
            count[b]++;
            total += d;
        }
        previous = psi;
    }
    *final_psi = previous;
    *mean_rate = total * RATE / n;
    *f_low = interval < 0 ? 0 : 440;
    /* sawtooth fit: rate = s (wrap(psi - jump) - 1/2) + c, bins next to the jump left out */
    double best = 1e30;
    *half = 0;
    *jump = 0;
    for (int k = 0; k < 480; k++) {
        double j = k / 480.0, sx = 0, sy = 0, sxx = 0, sxy = 0, m = 0;
        double u[BINS], y[BINS];
        int used = 0;
        for (int b = 0; b < BINS; b++) {
            if (!count[b]) continue;
            double x = (b + .5) / BINS - j;
            x -= floor(x);
            if (x < .05 || x > .95) continue;
            u[used] = x - .5; y[used] = sum[b] / count[b]; used++;
        }
        for (int i = 0; i < used; i++) { sx += u[i]; sy += y[i]; sxx += u[i] * u[i]; sxy += u[i] * y[i]; m++; }
        double slope = (m * sxy - sx * sy) / (m * sxx - sx * sx), c = (sy - slope * sx) / m, r = 0;
        for (int i = 0; i < used; i++) r += pow(y[i] - slope * u[i] - c, 2);
        if (r < best) { best = r; *half = slope / 2; }
    }
    /* the jump itself: the largest drop between neighbouring bins (the fit
     * above cannot place it closer than its left-out zone) */
    double drop = -1e30;
    for (int b = 0; b < BINS; b++) {
        int c = (b + 1) % BINS;
        if (!count[b] || !count[c]) continue;
        double d = sum[b] / count[b] - sum[c] / count[c];
        if (d > drop) { drop = d; *jump = (double)c / BINS; }
    }
}

static double jump_distance(double a, double b) {
    double d = fabs(a - b);
    return d > .5 ? 1 - d : d;
}

static void test_spice(void) {
    for (unsigned i = 0; i < sizeof SPICE / sizeof *SPICE; i++) {
        double half, jump, mean, f_low, last;
        /* B detuned 8 cents, as in SPICE: a few beats */
        measure(SPICE[i].interval, 8, SPICE[i].supply, 3.0, &half, &jump, &mean, &f_low, &last);
        double rel = half / 440;
        printf("  %-24s lock %.3e of f (SPICE %.3e), jump %.3f (%.3f)\n", SPICE[i].name, rel, SPICE[i].rel, jump,
               SPICE[i].jump);
        CHECK(fabs(rel / SPICE[i].rel - 1) < .06, "%s: lock %.4e, SPICE %.4e", SPICE[i].name, rel, SPICE[i].rel);
        CHECK(jump_distance(jump, SPICE[i].jump) < .045, "%s: jump %.3f, SPICE %.3f", SPICE[i].name, jump,
              SPICE[i].jump);
    }
}

static void test_lock(void) {
    /* Soft rail, unison: lock range 1.04e-3 of 440 Hz = 1.8 cents. One cent
     * of detune locks, three beat. */
    double half, jump, mean, f_low, last;
    measure(0, 1, 1, 3.0, &half, &jump, &mean, &f_low, &last);
    printf("  soft rail, 1 cent apart: %.4f Hz mean beat, held at %.3f\n", mean, last);
    CHECK(fabs(mean) < .002, "1 cent inside the lock range still beats at %.4f Hz", mean);
    CHECK(jump_distance(last, 0) < .05, "locked away from the jump: %.3f", last);
    measure(0, 3, 1, 3.0, &half, &jump, &mean, &f_low, &last);
    double free_beat = 440 * (pow(2, 3 / 1200.0) - 1);
    printf("  soft rail, 3 cents apart: %.4f Hz mean beat (free %.4f)\n", mean, free_beat);
    CHECK(mean > .1 && mean < free_beat, "3 cents: mean beat %.4f Hz", mean);
}

static void test_front(void) {
    static ma_synth s;
    static const int INTERVALS[] = { 0, 12, -12, 7, -7, 5, 24 };
    static const double GOOD[] = { 1.06e-4, 1.60e-4, 1.60e-4, 1.32e-4, 1.32e-4, 0, 0 };
    static const double SOFT[] = { 1.04e-3, 1.56e-3, 1.56e-3, 1.32e-3, 1.32e-3, 0, 0 };
    double worst = 0;
    for (unsigned k = 0; k < sizeof INTERVALS / sizeof *INTERVALS; k++)
        for (int supply = 0; supply <= 2; supply++)
            for (int note = 36; note <= 84; note += 24) {
                ma_patch p = pair_patch(INTERVALS[k], 0, (float)supply / 2);
                ma_synth_init_patch(&s, RATE, &p);
                ma_synth_note_on(&s, 0, (uint8_t)note, 100);
                (void)ma_synth_tick(&s);
                double low = 440 * pow(2, (note - 69 + (INTERVALS[k] < 0 ? INTERVALS[k] : 0)) / 12.0);
                double want = GOOD[k] * pow(SOFT[k] / (GOOD[k] ? GOOD[k] : 1), supply / 2.0) * low;
                if (!GOOD[k]) want = 0;
                double e = want ? fabs(s.vco_couple_hz / want - 1) : s.vco_couple_hz;
                if (e > worst) worst = e;
            }
    printf("  front end's lock range against the table: within %.2g\n", worst);
    CHECK(worst < .005, "lock range %.4g from the table", worst);
    ma_patch p = pair_patch(0, 0, 1);
    p.vco_analog = 0;
    ma_synth_init_patch(&s, RATE, &p);
    ma_synth_note_on(&s, 0, 60, 100);
    (void)ma_synth_tick(&s);
    CHECK(s.vco_couple_hz == 0, "the supply couples without the law: %g", (double)s.vco_couple_hz);
}

static ma_voice_pool pool_block, pool_tick;

static void test_pool(void) {
    ma_patch p = ma_patch_tepih;
    p.vco_analog = 1; p.vco_supply = 1;
    p.vco2_interval = 0; p.vco2_fine_cents = 1; p.vco2_level = .8f;
    p.vco1.triangle_level = .5f; p.vco2.tri_saw_level = .5f;
    ma_voice_pool_config c = ma_voice_pool_default_config;
    c.limit = 12;
    CHECK(ma_voice_pool_init(&pool_block, RATE, &c, &p) && ma_voice_pool_init(&pool_tick, RATE, &c, &p), "pool init");
    ma_voice_pool_set_character(&pool_block, .5f);
    ma_voice_pool_set_character(&pool_tick, .5f);
    static const uint8_t CHORD[] = { 33, 45, 52, 57, 60, 64, 72, 79, 88, 96 };
    for (unsigned i = 0; i < sizeof CHORD; i++) {
        (void)ma_voice_pool_note_on(&pool_block, 0, CHORD[i], 90);
        (void)ma_voice_pool_note_on(&pool_tick, 0, CHORD[i], 90);
    }
    static ma_frame block[8192];
    unsigned different = 0;
    for (size_t at = 0; at < 8192; at += 128) ma_voice_pool_render(&pool_block, block + at, 128);
    for (size_t i = 0; i < 8192; i++) {
        ma_frame x = ma_voice_pool_tick(&pool_tick);
        different += memcmp(&x, &block[i], sizeof x) != 0;
    }
    CHECK(!different, "block render and frame-by-frame tick: %u of 8192 frames differ with the coupling on",
          different);
}

int main(void) {
    printf("ma vco couple:\n");
    test_spice();
    test_lock();
    test_front();
    test_pool();
    printf("ma vco couple: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
