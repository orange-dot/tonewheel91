/* The circuit-derived oscillator pitch (docs/ma-vco-plan.md, V1).
 * - The law against the SPICE study's tables: tracking of the two trimmed
 *   boards over C0..C9 and board A at 10 and 40 C (docs/ma-vco2-spice.md).
 * - Amount zero is an exact bypass.
 * - The pitch a voice actually plays, for VCO1 and for VCO2, equals the law.
 * - The pool's vector render and its frame-by-frame tick give the same bits
 *   with the law on.
 * Hosted test. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../src/ma_internal.h"

static unsigned checks, failures;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static double cents(double f, double reference) { return 1200.0 * log2(f / reference); }
static double note_hz(double note) { return 440.0 * pow(2.0, (note - 69.0) / 12.0); }

static double law_cents(double note, float extra_hz, float dead_us, float temp_k) {
    float hz = (float)note_hz(note);
    return cents(ma_voice_vco_law_hz(hz, (float)((note - 69.0) / 12.0), extra_hz, dead_us * 1e-6f, temp_k), hz);
}

/* ---- the law against SPICE ------------------------------------------------------ */

/* build/vco-step2-20261112 (2026-11-12): track-A.json, track-B.json, cents
 * from equal temperament at C0, F#0, ... C9 after the trim, and the pitch-law
 * fit of each board (shape-A.json: extra current, dead time). */
static const float SPICE_A[19] = { 6.84f, 2.14f, -0.60f, -1.97f, -2.42f, -2.26f, -1.75f, -1.09f, -0.47f, -0.06f,
                                   -0.07f, -0.75f, -2.42f, -5.52f, -10.63f, -18.57f, -30.45f, -47.77f, -72.68f };
static const float SPICE_B[19] = { 12.10f, 5.43f, 1.33f, -0.95f, -1.98f, -2.17f, -1.85f, -1.25f, -0.61f, -0.12f,
                                   -0.00f, -0.51f, -1.98f, -4.83f, -9.65f, -17.25f, -28.72f, -45.55f, -69.85f };
static constexpr float A_EXTRA_HZ = 0.2084f;
static constexpr float A_DEAD_US = 6.603f;
static constexpr float B_EXTRA_HZ = 0.2795f;
static constexpr float B_DEAD_US = 6.490f;

static void test_law(void) {
    double worst_a = 0, worst_b = 0;
    for (int i = 0; i < 19; i++) {
        double note = 12 + 6 * i;
        double a = fabs(law_cents(note, A_EXTRA_HZ, A_DEAD_US, 0) - SPICE_A[i]);
        double b = fabs(law_cents(note, B_EXTRA_HZ, B_DEAD_US, 0) - SPICE_B[i]);
        if (a > worst_a) worst_a = a;
        if (b > worst_b) worst_b = b;
    }
    printf("  law against SPICE tracking, C0..C9: board A within %.2f cents, board B within %.2f cents\n",
           worst_a, worst_b);
    CHECK(worst_a < .5, "board A: %.3f cents from the SPICE tracking", worst_a);
    CHECK(worst_b < .5, "board B: %.3f cents from the SPICE tracking", worst_b);

    /* The trim itself: A4 on pitch, C2 -> C6 four octaves. */
    CHECK(fabs(law_cents(69, A_EXTRA_HZ, A_DEAD_US, 0)) < .02, "A4 %.4f cents", law_cents(69, A_EXTRA_HZ, A_DEAD_US, 0));
    double span = law_cents(84, A_EXTRA_HZ, A_DEAD_US, 0) - law_cents(36, A_EXTRA_HZ, A_DEAD_US, 0);
    CHECK(fabs(span) < .1, "C2 -> C6 is %.4f cents from four octaves", span);

    /* Board A at 10 and 40 C with the +3300 ppm/K resistor (temp-A.json). */
    static const struct { float temp_k; int note; float spice; } T[] = {
        { -15, 36, -25.74f }, { -15, 69, -15.11f }, { -15, 84, -13.58f },
        { 15, 36, 20.46f }, { 15, 69, 14.84f }, { 15, 84, 8.53f },
    };
    double worst_t = 0;
    for (unsigned i = 0; i < sizeof T / sizeof *T; i++) {
        double d = fabs(law_cents(T[i].note, A_EXTRA_HZ, A_DEAD_US, T[i].temp_k) - T[i].spice);
        if (d > worst_t) worst_t = d;
    }
    printf("  law against SPICE at 10 and 40 C: within %.2f cents\n", worst_t);
    CHECK(worst_t < .6, "temperature: %.3f cents from SPICE", worst_t);

    /* With the dead time trimmed out the top is no longer flat. */
    CHECK(fabs(law_cents(108, A_EXTRA_HZ, 0, 0)) < 5, "C8 with the dead time trimmed: %.2f cents", law_cents(108, A_EXTRA_HZ, 0, 0));
    CHECK(law_cents(108, A_EXTRA_HZ, A_DEAD_US, 0) < -25, "C8 untrimmed: %.2f cents", law_cents(108, A_EXTRA_HZ, A_DEAD_US, 0));
}

/* ---- a voice's pitch -------------------------------------------------------------- */

enum { RATE = 48000, MEASURE = 2 * RATE, WARM = RATE / 4 };

/* A patch whose output is one sine: VCO1's, or VCO2's when `second`. */
static ma_patch sine_patch(bool second, int interval) {
    ma_patch p = ma_patch_tepih;
    p.vco1 = (ma_vco_controls){ .sine_level = second ? 0.0f : 1.0f, .pulse_width = .5f };
    p.vco2 = (ma_vco_controls){ .sine_level = second ? 1.0f : 0.0f, .pulse_width = .5f };
    p.vco2_level = second ? 1.0f : 0.0f;
    p.vco2_interval = interval;
    p.vco2_fine_cents = 0;
    p.sync_amount = p.sync_softness = p.crossmod_amount = p.noise_level = 0;
    p.raster_mix = p.bcs_amount = p.mozaik_mix = p.formant_mix = p.grain_mix = 0;
    p.filter_cutoff_hz = 20000; p.filter_resonance = 0; p.filter_drive = 0;
    p.filter_env_amount = 0; p.filter_keytrack = 0; p.mixer_pressure = 0;
    p.amp_adsr = (ma_adsr){ .attack_ms = 1, .decay_ms = 100, .sustain = 1, .release_ms = 50 };
    p.body_drive = 0;
    for (int m = 0; m < MA_MACRO_COUNT; m++) p.macro[m] = 0;
    return p;
}

/* The frequency of a held note, from rising zero crossings over two seconds. */
static double played_hz(const ma_patch *patch, uint8_t note) {
    static ma_synth s;
    ma_synth_init_patch(&s, RATE, patch);
    ma_synth_set_lfo(&s, 0, 5);
    ma_synth_set_glide(&s, false, 0);
    ma_synth_note_on(&s, 0, note, 100);
    for (int i = 0; i < WARM; i++) (void)ma_synth_tick(&s);
    double first = -1, last = -1, previous = ma_synth_tick(&s).left;
    long count = 0;
    for (int i = 1; i < MEASURE; i++) {
        double x = ma_synth_tick(&s).left;
        if (previous < 0 && x >= 0) {
            double at = i - 1 + previous / (previous - x);
            if (first < 0) first = at;
            last = at;
            count++;
        }
        previous = x;
    }
    return count > 1 ? (count - 1) * (double)RATE / (last - first) : 0;
}

static void test_voice(void) {
    /* Without a card the voice is the nominal board (0.244 Hz, 6.546 us). */
    static const uint8_t NOTES[] = { 36, 69, 96, 108 };
    for (unsigned i = 0; i < sizeof NOTES / sizeof *NOTES; i++) {
        uint8_t note = NOTES[i];
        ma_patch p = sine_patch(false, 0);
        /* VCO1 is also bent up by the filter envelope (17 cents with this
         * patch), so the law is measured against the same voice without it. */
        double plain = played_hz(&p, note);
        CHECK(plain > 0, "note %u: no pitch measured", note);
        p.vco_analog = 1; p.vco_dead_time = 1;
        double full = cents(played_hz(&p, note), plain), want = law_cents(note, .244f, 6.546f, 0);
        CHECK(fabs(full - want) < .05, "note %u, VCO1: plays %.3f cents, the law says %.3f", note, full, want);
        p.vco_dead_time = 0;
        double trimmed = cents(played_hz(&p, note), plain), want0 = law_cents(note, .244f, 0, 0);
        CHECK(fabs(trimmed - want0) < .05, "note %u, dead time trimmed: plays %.3f cents, the law says %.3f", note, trimmed, want0);
        p.vco_analog = .5f; p.vco_dead_time = 1;
        double half = cents(played_hz(&p, note), plain), want_half = law_cents(note, .122f, 3.273f, 0);
        CHECK(fabs(half - want_half) < .05, "note %u at half amount: plays %.3f cents, expected %.3f", note, half, want_half);
        if (note == 108)
            printf("  C8 plays %.2f cents untrimmed, %.2f at half amount, %.2f with the dead time trimmed\n",
                   full, half, trimmed);
    }
    /* VCO2 has its own core: an octave and a fifth above a C3 it follows the
     * law at its own pitch, not VCO1's. */
    for (int interval = -12; interval <= 19; interval += 31) {
        ma_patch p = sine_patch(true, interval);
        double plain = played_hz(&p, 48);
        CHECK(fabs(cents(plain, note_hz(48 + interval))) < .05, "VCO2 at %+d without the law: %.4f cents", interval,
              cents(plain, note_hz(48 + interval)));
        p.vco_analog = 1; p.vco_dead_time = 1;
        double got = cents(played_hz(&p, 48), plain), want = law_cents(48 + interval, .244f, 6.546f, 0);
        CHECK(fabs(got - want) < .05, "VCO2 at %+d: plays %.3f cents, the law says %.3f", interval, got, want);
    }
}

/* ---- exact bypass and the two render paths ---------------------------------------- */

static void test_bypass(void) {
    /* The dead-time control alone does nothing: amount zero is the old voice. */
    static ma_synth a, b;
    ma_patch p = ma_patch_lead, q = ma_patch_lead;
    q.vco_dead_time = 1;
    ma_synth_init_patch(&a, RATE, &p);
    ma_synth_init_patch(&b, RATE, &q);
    ma_synth_note_on(&a, 0, 57, 100);
    ma_synth_note_on(&b, 0, 57, 100);
    unsigned different = 0;
    for (int i = 0; i < RATE; i++) {
        ma_frame x = ma_synth_tick(&a), y = ma_synth_tick(&b);
        different += memcmp(&x, &y, sizeof x) != 0;
    }
    CHECK(!different, "amount zero: %u frames differ", different);
    /* Switched on and off again, the voice returns to the old pitch path. */
    ma_synth_set_vco_analog(&b, 1, 1);
    for (int i = 0; i < 4800; i++) (void)ma_synth_tick(&b);
    ma_synth_set_vco_analog(&b, 0, 1);
    CHECK(b.vco_analog == 0, "amount is %g after switching off", (double)b.vco_analog);
    (void)ma_synth_tick(&b);
    CHECK(b.vco2_bend == 0, "VCO2's bend is %g with the law off", (double)b.vco2_bend);
}

static ma_voice_pool pool_block, pool_tick;

static void test_pool(void) {
    ma_patch p = ma_patch_tepih;
    p.vco_analog = 1; p.vco_dead_time = .5f;
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
    static ma_frame block[4096];
    unsigned different = 0;
    for (size_t at = 0; at < 4096; at += 128) ma_voice_pool_render(&pool_block, block + at, 128);
    for (size_t i = 0; i < 4096; i++) {
        ma_frame x = ma_voice_pool_tick(&pool_tick);
        different += memcmp(&x, &block[i], sizeof x) != 0;
    }
    CHECK(!different, "block render and frame-by-frame tick: %u of 4096 frames differ with the law on", different);

    /* Every card's cores are inside the study's spread. */
    for (unsigned slot = 0; slot < c.limit; slot++) {
        const ma_character *k = &pool_block.slot[slot].voice.character;
        if (!k->assigned) continue;
        CHECK(fabsf(k->temp_k) <= 2 && fabsf(k->vco2_temp_k) <= .5f, "slot %u temperatures %g %g", slot,
              (double)k->temp_k, (double)k->vco2_temp_k);
        for (int v = 0; v < 2; v++)
            CHECK(k->vco_extra_hz[v] >= .208f && k->vco_extra_hz[v] <= .280f
                  && k->vco_dead_us[v] >= 6.489f && k->vco_dead_us[v] <= 6.603f,
                  "slot %u core %d: %g Hz, %g us", slot, v, (double)k->vco_extra_hz[v], (double)k->vco_dead_us[v]);
    }
}

int main(void) {
    printf("ma vco analog:\n");
    test_law();
    test_voice();
    test_bypass();
    test_pool();
    printf("ma vco analog: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
