/* Glas gate (ma-constants.md 10C): exact bypass, formant-bank response,
 * Orbita formant capture on the harmonic grid, formant-grain spectrum,
 * determinism, hostile controls, and the pool's scalar fallback from the
 * vector lanes. Also prints the measured gains pinned in 10C.6. */
#include "../src/ma_voice_impl.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

enum { RATE = 48000 };
static int failures;
static void expect(bool ok, const char *what) {
    if (!ok) { fprintf(stderr, "ma_glas: FAIL %s\n", what); failures++; }
}

static uint64_t render_hash(ma_synth *s, size_t frames, float *out) {
    uint64_t h = 0;
    for (size_t i = 0; i < frames; i++) {
        ma_frame f = ma_synth_tick(s);
        if (out) out[i] = f.left;
        h = tw_fnv1a64(&f, sizeof f, h);
    }
    return h;
}

static ma_patch open_patch(void) {
    ma_patch p = ma_patch_lead;
    p.vco1 = (ma_vco_controls){ .saw_level = 1, .pulse_width = .5f };
    p.vco2_level = 0; p.sync_amount = 0; p.crossmod_amount = 0;
    p.noise_level = 0; p.mozaik_mix = 0; p.bcs_amount = 0;
    p.filter_cutoff_hz = 12000; p.filter_resonance = 0; p.filter_drive = 0;
    p.mixer_pressure = 0; p.filter_env_amount = 0; p.filter_keytrack = 0;
    p.filter_adsr = (ma_adsr){ 1, 1, 1, 100 };
    p.amp_adsr = (ma_adsr){ 1, 1, 1, 100 };
    for (int m = 0; m < MA_MACRO_COUNT; m++) p.macro[m] = 0;
    p.body_drive = 0;
    return p;
}

/* |H(f)| of the formant bank by sine probe, through the white-box stage. */
static float bank_gain(float vowel, float hz) {
    static ma_synth s;
    ma_synth_init(&s, RATE);
    ma_synth_set_formant(&s, 1, vowel, 0, 0);
    ma_glas_snap_controls(&s.glas);
    double in = 0, out = 0;
    float phase = 0;
    for (int i = 0; i < RATE / 2; i++) {
        (void)ma_glas_prepare(&s, 110.0f);
        float x = tw_sin_turns(phase);
        phase += hz / RATE;
        if (phase >= 1) phase -= 1;
        float y = ma_glas_formant(&s, x);
        if (i > RATE / 4) { in += (double)x * x; out += (double)y * y; }
    }
    return (float)sqrt(out / in);
}

static void check_bypass(void) {
    static ma_synth a, b;
    ma_synth_init(&a, RATE);
    ma_synth_init(&b, RATE);
    ma_synth_note_on(&a, 0, 48, 100);
    ma_synth_note_on(&b, 0, 48, 100);
    /* Zero setters on b: no state may move. */
    ma_synth_set_formant(&b, 0, .9f, .5f, .7f);
    ma_synth_set_grain(&b, 0);
    expect(render_hash(&a, RATE, NULL) == render_hash(&b, RATE, NULL),
           "zero mixes are PCM-identical to an untouched voice");
    expect(!ma_glas_active(&b.glas) && b.glas.grains_launched == 0
           && b.glas.svf[0].s1 == 0, "zero mixes leave Glas state untouched");
}

static void check_bank(void) {
    /* Vowel a: F1 730, F2 1090; 10C.1. */
    float f1 = bank_gain(.5f, 730), f2 = bank_gain(.5f, 1090);
    float low = bank_gain(.5f, 150), high = bank_gain(.5f, 6000);
    printf("ma_glas: bank |H| vowel a: 150 Hz %.3f, F1 %.3f, F2 %.3f, 6 kHz %.4f\n",
           (double)low, (double)f1, (double)f2, (double)high);
    expect(f1 > 4 * low && f1 > 20 * high, "bank peaks at F1 over low and high stopbands");
    expect(f2 > 4 * high, "bank passes F2");
}

static void check_orbita(void) {
    static ma_synth s;
    ma_patch p = open_patch();
    p.formant_mix = 1; p.formant_vowel = .25f; p.formant_orbita = .8f;
    ma_synth_init_patch(&s, RATE, &p);
    ma_synth_note_on(&s, 0, 48, 100);
    (void)render_hash(&s, 64, NULL);
    float f0 = ma_note_frequency_hz(48);
    bool grid = true;
    for (int i = 0; i < MA_FORMANTS; i++) {
        float n = s.glas.centre_hz[i] / f0;
        if (fabsf(n - roundf(n)) > 1e-3f) grid = false;
    }
    expect(grid, "note-on places every centre on a harmonic");
    /* Sweep the vowel across the line: releases and recaptures. */
    for (int k = 0; k <= 200; k++) {
        ma_synth_set_formant(&s, 1, (float)k / 200, 0, .8f);
        (void)render_hash(&s, RATE / 100, NULL);
    }
    printf("ma_glas: orbita sweep: %llu captures, %llu releases, %llu resets\n",
           (unsigned long long)s.glas.captures, (unsigned long long)s.glas.releases,
           (unsigned long long)s.glas.resets);
    expect(s.glas.captures > 0 && s.glas.releases > 0 && s.glas.resets == 0,
           "a vowel sweep releases and recaptures without resets");
    bool locked = true;
    (void)render_hash(&s, RATE, NULL);
    for (int i = 0; i < MA_FORMANTS; i++) {
        float n = s.glas.centre_hz[i] / f0;
        if (s.glas.moon[i].captured >= 1 && n != (float)s.glas.moon[i].captured)
            locked = false;
    }
    expect(locked, "captured centres sit exactly on their harmonic");

    ma_synth_set_formant(&s, 1, .5f, 0, 0);
    (void)render_hash(&s, RATE / 10, NULL);
    expect(fabsf(s.glas.centre_hz[0] - 730) < .5f && fabsf(s.glas.centre_hz[1] - 1090) < .5f,
           "orbita 0 keeps the vowel's fixed centres");
}

static float goertzel(const float *x, size_t n, float hz) {
    double w = 2 * 3.14159265358979 * hz / RATE, c = 2 * cos(w), s1 = 0, s2 = 0;
    for (size_t i = 0; i < n; i++) { double s0 = x[i] + c * s1 - s2; s2 = s1; s1 = s0; }
    return (float)sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) / (float)n;
}

static void check_grains(void) {
    static ma_synth s;
    static float out[RATE];
    ma_patch p = open_patch();
    p.grain_mix = 1; p.formant_vowel = .5f;   /* a: F1 730 */
    ma_synth_init_patch(&s, RATE, &p);
    ma_synth_note_on(&s, 0, 43, 100);          /* G2, 98 Hz */
    (void)render_hash(&s, RATE / 4, NULL);
    (void)render_hash(&s, RATE, out);
    float f0 = ma_note_frequency_hz(43), best = 0, peak = 0;
    int best_k = 0;
    for (int k = 1; k <= 30; k++) {
        float a = goertzel(out, RATE, k * f0);
        if (a > best) { best = a; best_k = k; }
    }
    for (int i = 0; i < RATE; i++) if (fabsf(out[i]) > peak) peak = fabsf(out[i]);
    printf("ma_glas: grains at %.1f Hz: strongest harmonic %d (%.0f Hz), "
           "%llu launched, %llu stolen, voice peak %.3f\n", (double)f0, best_k,
           (double)(best_k * f0), (unsigned long long)s.glas.grains_launched,
           (unsigned long long)s.glas.grains_stolen, (double)peak);
    expect(fabsf(best_k * f0 - 730) < f0, "grain spectrum peaks at the harmonic nearest F1");
    expect(s.glas.grains_launched > 4 * 100, "grains launch every period");
}

static void check_determinism_and_hostile(void) {
    static ma_synth a, b;
    ma_patch p = open_patch();
    p.formant_mix = .8f; p.formant_orbita = .5f; p.grain_mix = .6f;
    ma_synth_init_patch(&a, RATE, &p);
    ma_synth_init_patch(&b, RATE, &p);
    ma_synth_note_on(&a, 0, 50, 90);
    ma_synth_note_on(&b, 0, 50, 90);
    expect(render_hash(&a, RATE, NULL) == render_hash(&b, RATE, NULL),
           "identical voices render identical PCM");
    ma_synth_set_formant(&a, NAN, INFINITY, -INFINITY, NAN);
    ma_synth_set_grain(&a, NAN);
    expect(a.glas.formant_mix == 0 && a.glas.formant_vowel == .25f
           && a.glas.formant_shift == 0 && a.glas.formant_orbita == 0
           && a.glas.grain_mix == 0, "non-finite controls sanitize");
    ma_synth_set_formant(&a, 1, 1, 1, 1);
    ma_synth_set_grain(&a, 1);
    bool finite = true;
    for (int n = 0; n < 128; n += 7) {
        ma_synth_note_on(&a, 0, (uint8_t)n, 127);
        for (int i = 0; i < RATE / 20; i++) {
            ma_frame f = ma_synth_tick(&a);
            if (!isfinite(f.left) || !isfinite(f.right)) finite = false;
        }
    }
    expect(finite && a.glas.resets == 0, "full compass at maximum controls stays finite");
}

static void check_pool_fallback(void) {
    static ma_voice_pool lanes, scalar;
    ma_voice_pool_config c = ma_voice_pool_default_config;
    c.limit = 8;
    ma_patch p = open_patch();
    p.formant_mix = .8f; p.formant_orbita = .6f; p.grain_mix = .5f;
    expect(ma_voice_pool_init(&lanes, RATE, &c, &p), "pool init (auto)");
    c.engine = MA_POOL_ENGINE_SCALAR;
    expect(ma_voice_pool_init(&scalar, RATE, &c, &p), "pool init (scalar)");
    static const uint8_t notes[] = { 45, 52, 57, 60 };
    for (int i = 0; i < 4; i++) {
        (void)ma_voice_pool_note_on(&lanes, 0, notes[i], 100);
        (void)ma_voice_pool_note_on(&scalar, 0, notes[i], 100);
    }
    enum { BLOCKS = 187, FRAMES = BLOCKS * 128 };
    static ma_frame fa[FRAMES], fb[FRAMES];
    for (size_t at = 0; at < FRAMES; at += 128) {
        ma_voice_pool_render(&lanes, fa + at, 128);
        ma_voice_pool_render(&scalar, fb + at, 128);
    }
    expect(memcmp(fa, fb, sizeof fa) == 0,
           "a Glas pool renders the same PCM on the auto and scalar engines");
}

/* The fat-vocal case: unison 4, 16 slots, overlapping notes, patch edits
 * and grains, AUTO (vector lanes with per-lane Glas) against SCALAR. */
static void check_pool_lanes_heavy(void) {
    static ma_voice_pool lanes, scalar;
    ma_voice_pool_config c = ma_voice_pool_default_config;
    c.limit = 16; c.unison_count = 4; c.detune_cents = 10;
    ma_patch p = open_patch();
    p.vco2_interval = -12; p.vco2_level = .85f;
    p.formant_mix = .25f; p.formant_shift = -.30f; p.formant_orbita = .55f; p.grain_mix = .10f;
    p.filter_cutoff_hz = 900; p.filter_resonance = .20f; p.filter_drive = .45f; p.mixer_pressure = .40f;
    expect(ma_voice_pool_init(&lanes, RATE, &c, &p), "heavy pool init (auto)");
    c.engine = MA_POOL_ENGINE_SCALAR;
    expect(ma_voice_pool_init(&scalar, RATE, &c, &p), "heavy pool init (scalar)");
    static const uint8_t notes[] = { 45, 50, 52, 57, 54, 47, 62, 69 };
    enum { BLOCK = 128, BLOCKS = 600 };
    static ma_frame fa[BLOCK], fb[BLOCK];
    bool same = true;
    for (unsigned k = 0; k < BLOCKS && same; k++) {
        if (k % 40 == 0) {
            uint8_t n = notes[(k / 40) % 8];
            (void)ma_voice_pool_note_on(&lanes, 0, n, (uint8_t)(70 + k % 50));
            (void)ma_voice_pool_note_on(&scalar, 0, n, (uint8_t)(70 + k % 50));
        }
        if (k % 40 == 30 && k >= 80) {
            uint8_t n = notes[(k / 40 - 2) % 8];
            (void)ma_voice_pool_note_off(&lanes, 0, n, 0);
            (void)ma_voice_pool_note_off(&scalar, 0, n, 0);
        }
        if (k % 25 == 0) {
            p.formant_vowel = (float)(k % 100) / 100.0f;
            p.grain_mix = k % 50 < 25 ? .10f : 0.0f;
            ma_voice_pool_set_patch(&lanes, &p);
            ma_voice_pool_set_patch(&scalar, &p);
        }
        ma_voice_pool_render(&lanes, fa, BLOCK);
        ma_voice_pool_render(&scalar, fb, BLOCK);
        same = memcmp(fa, fb, sizeof fa) == 0;
    }
    expect(lanes.lanes_enabled && !scalar.lanes_enabled, "heavy twin really runs both engines");
    expect(same, "fat Glas pool: vector lanes bit-identical to scalar over 76800 frames");
}

static void measure_gains(void) {
    /* Saw at 130.8 Hz through vowel o: bank output RMS over input RMS. */
    static ma_synth s;
    ma_synth_init(&s, RATE);
    ma_synth_set_formant(&s, 1, .25f, 0, 0);
    ma_glas_snap_controls(&s.glas);
    double in = 0, out = 0;
    float phase = 0, f0 = 130.81f;
    for (int i = 0; i < RATE; i++) {
        (void)ma_glas_prepare(&s, f0);
        float x = 2 * phase - 1;
        phase += f0 / RATE;
        if (phase >= 1) phase -= 1;
        float y = ma_glas_formant(&s, x);
        if (i > RATE / 4) { in += (double)x * x; out += (double)y * y; }
    }
    printf("ma_glas: makeup check: saw 130.8 Hz through vowel o, out/in RMS %.3f\n",
           sqrt(out / in));
}

int main(void) {
    check_bypass();
    check_bank();
    check_orbita();
    check_grains();
    check_determinism_and_hostile();
    check_pool_fallback();
    check_pool_lanes_heavy();
    measure_gains();
    if (failures) return 1;
    puts("ma_glas: all checks passed");
    return 0;
}
