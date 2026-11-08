/* Orbita offline spike: resonance capture, hysteretic escape and breakup.
 *
 * Orbita is a post-MA5 candidate (docs/analog-backlog.md, MA5); the operator
 * pulled its offline experiment forward on 2026-11-07. This is hosted code
 * only. No core file changes, and nothing here is reachable from the core.
 *
 * Part A ports the donor's v0.1 primitive (mamut-sint-sw
 * docs/EPM1_BACKLOG_SET5_ORBITA_MOZAIK_KOSAVA.md, SET5-1): K = 5 "moons",
 * each a frequency ratio to the fundamental that drifts toward, is captured
 * by, and escapes from small-integer ratios, plus a staggered breakup into a
 * ring pair. Part B drives the real MA voice: one moon is the VCO2/VCO1
 * ratio, written through the public ma_synth_set_vco2 setter every tick.
 *
 * The primitive lives in ma_orbita.{h,c}. Constants and their tags:
 * docs/ma-orbita-spike-20261107.md.
 *
 *   --check            table-driven checks, exit status 0 on success
 *   --render [-o DIR]  scenario WAVs plus one metrics line per scenario
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "../src/mamutanalog.h"
#include "ma_orbita.h"
#include "wav.h"

enum { RATE = 48000 };

/* --- Part A scenarios ---------------------------------------------------- */

static constexpr uint64_t SEED = UINT64_C(0x4f52424954413031); /* ORBITA01 */
static constexpr float PART_A_F0 = 220.0f;
static constexpr float PLANET_GAIN = 0.5f;
static constexpr float MOON_GAIN = 0.5f;
static constexpr float FADE_S = 0.010f;

typedef enum { SC_CAPTURE_SLOW, SC_CAPTURE_FAST, SC_ESCAPE_KICK,
               SC_KIRKWOOD_SWEEP, SC_ROCHE_SWEEP, SC_COUNT } scenario_id;

typedef struct {
    const char *name;
    float seconds;
    float dissipation;
} scenario;

static const scenario SCENARIOS[SC_COUNT] = {
    [SC_CAPTURE_SLOW] = { "capture_slow", 24.0f, 0.25f },
    [SC_CAPTURE_FAST] = { "capture_fast", 8.0f, 0.90f },
    [SC_ESCAPE_KICK] = { "escape_kick", 20.0f, 0.60f },
    [SC_KIRKWOOD_SWEEP] = { "kirkwood_sweep", 20.0f, 0.60f },
    [SC_ROCHE_SWEEP] = { "roche_sweep", 18.0f, 0.60f },
};

typedef struct {
    uint64_t hash;
    double sum_squares;
    float peak;
    size_t frames;
    double ns_per_sample;
    uint32_t kick_escapes[10];
} metrics;

static double now_ns(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_MONOTONIC);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static float fade(size_t i, size_t n) {
    size_t f = (size_t)(FADE_S * RATE);
    if (i < f) return (float)i / (float)f;
    if (n - 1 - i < f) return (float)(n - 1 - i) / (float)f;
    return 1.0f;
}

/* Renders scenario `id` into `out` (mono, `frames` long). */
static void run_scenario(scenario_id id, orbit_system *s, float *out,
                         size_t frames, metrics *mx) {
    const scenario *sc = &SCENARIOS[id];
    orbit_init(s, ORBIT_MAX_MOONS, SEED, RATE);
    orbit_set_controls(s, sc->dissipation, 0.0f, 0.0f);
    float planet = 0.0f;
    *mx = (metrics){ .frames = frames };
    uint32_t escapes_before = 0;
    int kick = -1;
    double t0 = now_ns();
    for (size_t i = 0; i < frames; i++) {
        float t = (float)i / RATE;
        if (i % ORBIT_TICK == 0) {
            switch (id) {
            case SC_ESCAPE_KICK: {
                /* Ten kicks of rising size, one every 1.5 s from 4 s. */
                int n = (int)((t - 4.0f) / 1.5f);
                if (t >= 4.0f && n < 10 && n != kick) {
                    if (kick >= 0)
                        mx->kick_escapes[kick] = s->escapes - escapes_before;
                    escapes_before = s->escapes;
                    kick = n;
                    orbit_kick(s, 0.1f * (float)(n + 1));
                }
                break;
            }
            case SC_KIRKWOOD_SWEEP: {
                float k = (t - 2.0f) / 14.0f;
                orbit_set_controls(s, sc->dissipation,
                                   k < 0.0f ? 0.0f : k > 1.0f ? 1.0f : k,
                                   0.0f);
                break;
            }
            case SC_ROCHE_SWEEP: {
                /* Capture for 3 s, rise to 1 over 5 s, hold 2 s, fall over
                 * 5 s, hold 3 s. */
                float p = t < 3.0f ? 0.0f : t < 8.0f ? (t - 3.0f) / 5.0f
                        : t < 10.0f ? 1.0f : t < 15.0f ? 1.0f - (t - 10.0f) / 5.0f
                        : 0.0f;
                orbit_set_controls(s, sc->dissipation, 0.0f, p);
                break;
            }
            default:
                break;
            }
        }
        float moons = orbit_render(s, PART_A_F0);
        float y = PLANET_GAIN * orbit_advance(&planet, PART_A_F0, RATE)
                + MOON_GAIN * moons;
        out[i] = y * fade(i, frames);
    }
    if (kick >= 0) mx->kick_escapes[kick] = s->escapes - escapes_before;
    mx->ns_per_sample = (now_ns() - t0) / (double)frames;
    for (size_t i = 0; i < frames; i++) {
        mx->sum_squares += (double)out[i] * out[i];
        if (fabsf(out[i]) > mx->peak) mx->peak = fabsf(out[i]);
    }
    mx->hash = tw_fnv1a64(out, frames * sizeof *out, 0);
}

static void print_moons(const orbit_system *s) {
    for (uint8_t k = 0; k < s->count; k++) {
        const orbit_moon *m = &s->moon[k];
        printf("    moon %u home %.5f final %.6f ", (unsigned)k,
               (double)m->home, (double)m->ratio);
        if (m->captured >= 0)
            printf("captured %u/%u", ORBIT_TABLE[m->captured].p,
                   ORBIT_TABLE[m->captured].q);
        else
            printf("free");
        if (m->first_capture_tick)
            printf(" first-capture %.3f s",
                   (double)m->first_capture_tick * ORBIT_TICK / RATE);
        printf(" ring-peak %.4f\n", (double)m->ring_peak);
    }
}

/* --- Part B: VCO2 interval as one moon on the MA voice -------------------- */

typedef struct {
    const char *name;
    const ma_patch *patch;
    float seconds;
    float dissipation;
    float start_cents;   /* VCO2 above VCO1 at start               */
    bool use_orbita;     /* false: one setter call, no Orbita      */
    bool kicks;
} vco2_scenario;

static constexpr uint8_t PART_B_NOTE = 48;
static constexpr uint8_t PART_B_VELOCITY = 100;

typedef struct {
    uint64_t hash;
    float peak;
    double sum_squares;
    uint32_t fine_clamps;
    float final_cents;
    float first_capture_s;
    uint32_t captures, escapes;
    size_t frames;
} vco2_metrics;

static ma_patch vco2_patch(const ma_patch *base) {
    ma_patch p = *base;
    /* Hard sync would force the VCO2 period onto VCO1 and cross-mod would
     * smear the beat; both are off so the capture is audible as beating
     * that slows and stops [DECISION, spike only]. */
    p.sync_amount = 0.0f;
    p.crossmod_amount = 0.0f;
    p.vco2_level = 0.80f;
    p.amp_adsr.sustain = 0.80f;
    return p;
}

static void vco2_split(float cents, int *interval, float *fine,
                       uint32_t *clamps) {
    int i = (int)lroundf(cents / 100.0f);
    i = i < -24 ? -24 : i > 24 ? 24 : i;
    float f = cents - 100.0f * (float)i;
    if (f > 50.0f || f < -50.0f) {
        (*clamps)++;
        f = f > 50.0f ? 50.0f : -50.0f;
    }
    *interval = i;
    *fine = f;
}

static void run_vco2(const vco2_scenario *sc, float *out, size_t frames,
                     vco2_metrics *mx) {
    *mx = (vco2_metrics){ .frames = frames, .first_capture_s = -1.0f };
    ma_patch patch = vco2_patch(sc->patch);
    static ma_synth synth;
    ma_synth_init_patch(&synth, RATE, &patch);
    orbit_system s;
    orbit_init(&s, 1, SEED, RATE);
    float start_ratio = exp2f(sc->start_cents / 1200.0f);
    orbit_set_home(&s, 0, start_ratio);
    orbit_set_controls(&s, sc->dissipation, 0.0f, 0.0f);

    /* The interval is applied unsmoothed by the core, so it is fixed per
     * scenario at the semitone nearest the start; only the smoothed fine
     * tune moves. Scenarios are chosen so the fine never clamps. */
    int interval;
    float fine;
    vco2_split(1200.0f * log2f(start_ratio), &interval, &fine,
               &mx->fine_clamps);
    ma_synth_set_vco2(&synth, patch.vco2, patch.vco2_level, interval, fine);
    ma_synth_note_on(&synth, 0, PART_B_NOTE, PART_B_VELOCITY);
    size_t release_at = frames - (size_t)(1.0f * RATE);
    int kick_done = 0;
    for (size_t i = 0; i < frames; i++) {
        float t = (float)i / RATE;
        if (i == release_at) ma_synth_note_off(&synth, 0, PART_B_NOTE, 64);
        if (sc->use_orbita && i % ORBIT_TICK == 0) {
            if (sc->kicks) {
                /* +1.0 % at 5 s (inside the exit band), +2.5 % at 9 s
                 * (escapes), -2.5 % at 14 s. */
                static constexpr float at[3] = { 5.0f, 9.0f, 14.0f };
                static constexpr float rel[3] = { 0.010f, 0.025f, -0.025f };
                if (kick_done < 3 && t >= at[kick_done]) {
                    orbit_kick_moon(&s, 0, rel[kick_done]);
                    kick_done++;
                }
            }
            orbit_tick(&s);
            float cents = 1200.0f * log2f(s.moon[0].ratio) - 100.0f * (float)interval;
            if (cents > 50.0f || cents < -50.0f) {
                mx->fine_clamps++;
                cents = cents > 50.0f ? 50.0f : -50.0f;
            }
            ma_synth_set_vco2(&synth, patch.vco2, patch.vco2_level, interval,
                              cents);
        }
        ma_frame f = ma_synth_tick(&synth);
        out[2 * i] = f.left;
        out[2 * i + 1] = f.right;
    }
    for (size_t i = 0; i < 2 * frames; i++) {
        mx->sum_squares += (double)out[i] * out[i];
        if (fabsf(out[i]) > mx->peak) mx->peak = fabsf(out[i]);
    }
    mx->hash = tw_fnv1a64(out, 2 * frames * sizeof *out, 0);
    mx->final_cents = 1200.0f * log2f(s.moon[0].ratio);
    if (s.moon[0].first_capture_tick)
        mx->first_capture_s = (float)s.moon[0].first_capture_tick * ORBIT_TICK / RATE;
    mx->captures = s.captures;
    mx->escapes = s.escapes;
}

/* 3/2 is 701.955 cents; starting 30 cents above keeps the fine at +31.96
 * on interval 7. Tepih's own +7 cents unison detune is 0.40 % from 1/1. */
static const vco2_scenario VCO2_SCENARIOS[] = {
    { "vco2_fifth_static", &ma_patch_lead, 12.0f, 0.00f, 731.955f, true, false },
    { "vco2_fifth_capture_slow", &ma_patch_lead, 12.0f, 0.20f, 731.955f, true, false },
    { "vco2_fifth_capture_fast", &ma_patch_lead, 6.0f, 0.60f, 731.955f, true, false },
    { "vco2_fifth_kicks", &ma_patch_lead, 20.0f, 0.35f, 731.955f, true, true },
    { "vco2_unison_tepih", &ma_patch_tepih, 10.0f, 0.10f, 7.0f, true, false },
};
enum { VCO2_COUNT = sizeof VCO2_SCENARIOS / sizeof *VCO2_SCENARIOS };

/* --- checks -------------------------------------------------------------- */

static int failures;

static void expect(bool ok, const char *what) {
    printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}

static uint32_t ticks_to_capture(float home, float dissipation) {
    orbit_system s;
    orbit_init(&s, 1, SEED, RATE);
    orbit_set_home(&s, 0, home);
    orbit_set_controls(&s, dissipation, 0.0f, 0.0f);
    for (uint32_t n = 0; n < 2000000; n++) {
        orbit_tick(&s);
        if (s.moon[0].captured >= 0) return s.moon[0].first_capture_tick;
    }
    return 0;
}

static void settle(orbit_system *s, uint32_t ticks) {
    for (uint32_t n = 0; n < ticks; n++) orbit_tick(s);
}

static void check_capture_monotone(void) {
    static constexpr float d[] = { 0.2f, 0.4f, 0.6f, 0.8f, 1.0f };
    uint32_t prev = UINT32_MAX;
    bool ok = true;
    printf("      capture ticks from 3/2 +3 %%:");
    for (size_t i = 0; i < sizeof d / sizeof *d; i++) {
        uint32_t t = ticks_to_capture(1.5f * 1.03f, d[i]);
        printf(" d=%.1f:%u", (double)d[i], t);
        if (t == 0 || t >= prev) ok = false;
        prev = t;
    }
    printf("\n");
    expect(ok, "capture time strictly decreases with dissipation");

    orbit_system s;
    orbit_init(&s, 1, SEED, RATE);
    orbit_set_home(&s, 0, 1.5f * 1.03f);
    orbit_set_controls(&s, 0.5f, 0.0f, 0.0f);
    settle(&s, 20000);
    expect(s.moon[0].captured == 3 && s.moon[0].ratio == 1.5f,
           "captured ratio equals the table value bit for bit");
    expect(ticks_to_capture(1.5f * 1.03f, 0.0f) == 0,
           "dissipation 0 never moves or captures");
}

static void captured_at_two(orbit_system *s) {
    orbit_init(s, 1, SEED, RATE);
    orbit_set_home(s, 0, 2.0f * 1.02f);
    orbit_set_controls(s, 0.8f, 0.0f, 0.0f);
    settle(s, 20000);
}

static void check_hysteresis(void) {
    orbit_system s;
    captured_at_two(&s);
    expect(s.moon[0].captured == 7 && s.moon[0].ratio == 2.0f,
           "moon captured exactly at 2/1");
    orbit_kick_moon(&s, 0, 0.014f);
    orbit_tick(&s);
    bool held = s.moon[0].captured == 7;
    settle(&s, 100);
    expect(held && s.moon[0].ratio == 2.0f && s.escapes == 0,
           "+1.4 % kick stays captured and snaps back to exactly 2/1");

    captured_at_two(&s);
    orbit_kick_moon(&s, 0, 0.016f);
    orbit_tick(&s);
    expect(s.moon[0].captured == -1 && s.escapes == 1,
           "+1.6 % kick escapes");
    uint32_t back = 0;
    for (uint32_t n = 1; n < 200000 && back == 0; n++) {
        orbit_tick(&s);
        if (s.moon[0].captured == 7) back = n;
    }
    printf("      recapture after escape: %u ticks (%.3f s)\n", back,
           (double)back * ORBIT_TICK / RATE);
    expect(back > 3 * ORBIT_CAPTURE_TICKS, "escaped moon is recaptured, not instantly");
}

static void check_kirkwood(void) {
    float home = 1.25f * 1.03f;
    orbit_system s;
    for (int k = 0; k <= 1; k++) {
        orbit_init(&s, 1, SEED, RATE);
        orbit_set_home(&s, 0, home);
        orbit_set_controls(&s, 0.8f, (float)k, 0.0f);
        settle(&s, 50);
        float d0 = orbit_rel_distance(home, 1.25f);
        float d1 = orbit_rel_distance(s.moon[0].ratio, 1.25f);
        expect(k == 0 ? d1 < d0 : d1 > d0,
               k == 0 ? "kirkwood 0: 5/4 attracts"
                      : "kirkwood 1: 5/4 repels");
    }
    orbit_init(&s, 1, SEED, RATE);
    orbit_set_home(&s, 0, home);
    orbit_set_controls(&s, 0.8f, 0.0f, 0.0f);
    settle(&s, 40000);
    bool captured = s.moon[0].captured == 1;
    orbit_set_controls(&s, 0.8f, 1.0f, 0.0f);
    settle(&s, 4000);
    expect(captured && s.moon[0].captured == -1 && s.releases == 1
           && orbit_rel_distance(s.moon[0].ratio, 1.25f) > 0.01f,
           "capture on an entry that turns repulsive is released and ejected");
}

static void check_hostile(void) {
    orbit_system s;
    orbit_init(&s, ORBIT_MAX_MOONS, SEED, RATE);
    orbit_set_controls(&s, NAN, INFINITY, -INFINITY);
    expect(s.dissipation == 0.0f && s.kirkwood == 0.0f && s.proximity == 0.0f
           && s.sanitized == 3, "non-finite controls sanitize to zero");
    orbit_set_controls(&s, -5.0f, 5.0f, 0.5f);
    expect(s.dissipation == 0.0f && s.kirkwood == 1.0f && s.proximity == 0.5f,
           "out-of-range controls clamp to [0, 1]");
    s.moon[2].ratio = NAN;
    orbit_tick(&s);
    expect(s.resets == 1 && s.moon[2].ratio == s.moon[2].home,
           "non-finite moon state resets to its seeded home");

    orbit_init(&s, ORBIT_MAX_MOONS, SEED, RATE);
    uint64_t rng = 0x5354524553533031u;
    bool bounded = true;
    for (uint32_t n = 0; n < 200000 && bounded; n++) {
        uint64_t z = tw_splitmix64(&rng);
        float c[3];
        for (int j = 0; j < 3; j++)
            c[j] = (float)((z >> (16 * j)) & 0xffff) / 32768.0f - 0.5f;
        if ((z >> 60) == 0) c[z >> 56 & 1] = NAN;
        orbit_set_controls(&s, c[0], c[1], c[2]);
        if ((z >> 48 & 0x3f) == 0) orbit_kick(&s, c[2] + 0.5f);
        for (int j = 0; j < ORBIT_TICK; j++) {
            float y = orbit_render(&s, 220.0f);
            if (!isfinite(y) || fabsf(y) > 1.0f) bounded = false;
        }
        for (uint8_t k = 0; k < s.count; k++) {
            const orbit_moon *m = &s.moon[k];
            if (!(m->ratio >= ORBIT_R_MIN && m->ratio <= ORBIT_R_MAX)
                || !(m->ring >= 0.0f && m->ring <= 1.0f))
                bounded = false;
        }
    }
    printf("      stress: %u captures, %u escapes, %u releases, %u sanitized\n",
           s.captures, s.escapes, s.releases, s.sanitized);
    expect(bounded, "200k-tick hostile stress stays finite and bounded");

    orbit_init(&s, ORBIT_MAX_MOONS, SEED, RATE);
    float peak = 0.0f;
    for (int i = 0; i < RATE; i++) {
        float y = orbit_render(&s, 6000.0f);
        if (fabsf(y) > peak) peak = fabsf(y);
    }
    expect(s.guard_samples > 0 && isfinite(peak) && peak <= 1.0f,
           "partials near Nyquist fade in the guard band");
}

static void check_determinism(void) {
    size_t frames = (size_t)(6.0f * RATE);
    float *a = malloc(frames * sizeof *a), *b = malloc(frames * sizeof *b);
    if (!a || !b) { expect(false, "allocation"); free(a); free(b); return; }
    orbit_system sa, sb;
    metrics ma, mb;
    run_scenario(SC_ESCAPE_KICK, &sa, a, frames, &ma);
    run_scenario(SC_ESCAPE_KICK, &sb, b, frames, &mb);
    expect(ma.hash == mb.hash && memcmp(&sa, &sb, sizeof sa) == 0,
           "same seed and script: identical PCM and state");
    free(a);
    free(b);
}

static void check_vco2_bypass(void) {
    size_t frames = (size_t)(3.0f * RATE);
    float *a = malloc(2 * frames * sizeof *a), *b = malloc(2 * frames * sizeof *b);
    if (!a || !b) { expect(false, "allocation"); free(a); free(b); return; }
    vco2_scenario with = VCO2_SCENARIOS[0];
    vco2_scenario without = with;
    with.seconds = without.seconds = 3.0f;
    without.use_orbita = false;
    vco2_metrics mw, mo;
    run_vco2(&with, a, frames, &mw);
    run_vco2(&without, b, frames, &mo);
    printf("      vco2 dissipation-0 %016llx, no Orbita %016llx\n",
           (unsigned long long)mw.hash, (unsigned long long)mo.hash);
    expect(mw.hash == mo.hash,
           "VCO2 moon at dissipation 0 is PCM-identical to no Orbita");
    free(a);
    free(b);
}

static int run_checks(void) {
    check_capture_monotone();
    check_hysteresis();
    check_kirkwood();
    check_hostile();
    check_determinism();
    check_vco2_bypass();
    printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}

/* --- render -------------------------------------------------------------- */

static int render_all(const char *dir) {
    mkdir(dir, 0755);
    char path[512];
    size_t max_frames = (size_t)(24.0f * RATE);
    float *out = malloc(2 * max_frames * sizeof *out);
    if (!out) return 1;
    int status = 0;
    printf("Part A: moon bank, f0 %.0f Hz, seed %016llx, %d moons, "
           "planet %.2f + moons %.2f\n", (double)PART_A_F0,
           (unsigned long long)SEED, ORBIT_MAX_MOONS, (double)PLANET_GAIN,
           (double)MOON_GAIN);
    for (int id = 0; id < SC_COUNT; id++) {
        const scenario *sc = &SCENARIOS[id];
        size_t frames = (size_t)(sc->seconds * RATE);
        orbit_system s;
        metrics mx;
        run_scenario((scenario_id)id, &s, out, frames, &mx);
        snprintf(path, sizeof path, "%s/a_%s.wav", dir, sc->name);
        if (wav_write_f32(path, out, frames, RATE, 1) != 0) status = 1;
        printf("%s: %.1f s d=%.2f fnv %016llx rms %.4f peak %.4f "
               "captures %u escapes %u releases %u resets %u guard %u "
               "%.1f ns/sample\n", sc->name, (double)sc->seconds,
               (double)sc->dissipation, (unsigned long long)mx.hash,
               sqrt(mx.sum_squares / (double)frames), (double)mx.peak,
               s.captures, s.escapes, s.releases, s.resets, s.guard_samples,
               mx.ns_per_sample);
        print_moons(&s);
        if (id == SC_ESCAPE_KICK) {
            printf("    escapes per kick (amount .1 .. 1.0):");
            for (int k = 0; k < 10; k++) printf(" %u", mx.kick_escapes[k]);
            printf("\n");
        }
    }
    {
        size_t frames = (size_t)(SCENARIOS[SC_CAPTURE_SLOW].seconds * RATE);
        orbit_system s;
        metrics m1, m2;
        run_scenario(SC_CAPTURE_SLOW, &s, out, frames, &m1);
        run_scenario(SC_CAPTURE_SLOW, &s, out, frames, &m2);
        printf("determinism: capture_slow twice %016llx %016llx %s\n",
               (unsigned long long)m1.hash, (unsigned long long)m2.hash,
               m1.hash == m2.hash ? "equal" : "DIFFERENT");
        if (m1.hash != m2.hash) status = 1;
    }
    printf("Part B: MA voice, note %u velocity %u, sync and cross-mod off\n",
           PART_B_NOTE, PART_B_VELOCITY);
    for (int id = 0; id < VCO2_COUNT; id++) {
        const vco2_scenario *sc = &VCO2_SCENARIOS[id];
        size_t frames = (size_t)(sc->seconds * RATE);
        vco2_metrics mx, twin;
        run_vco2(sc, out, frames, &mx);
        snprintf(path, sizeof path, "%s/b_%s.wav", dir, sc->name);
        if (wav_write_f32(path, out, frames, RATE, 2) != 0) status = 1;
        run_vco2(sc, out, frames, &twin);
        printf("%s: %.1f s d=%.2f start %+.3f c fnv %016llx twin %s "
               "rms %.4f peak %.4f final %.3f c captures %u escapes %u "
               "first-capture %.3f s fine-clamps %u\n", sc->name,
               (double)sc->seconds, (double)sc->dissipation,
               (double)sc->start_cents, (unsigned long long)mx.hash,
               twin.hash == mx.hash ? "equal" : "DIFFERENT",
               sqrt(mx.sum_squares / (double)(2 * frames)), (double)mx.peak,
               (double)mx.final_cents, mx.captures, mx.escapes,
               (double)mx.first_capture_s, mx.fine_clamps);
        if (twin.hash != mx.hash || mx.fine_clamps) status = 1;
    }
    free(out);
    return status;
}

int main(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], "--check") == 0) return run_checks();
    if (argc >= 2 && strcmp(argv[1], "--render") == 0) {
        const char *dir = "build/ma-orbita-spike";
        if (argc >= 4 && strcmp(argv[2], "-o") == 0) dir = argv[3];
        return render_all(dir);
    }
    fprintf(stderr, "usage: %s --check | --render [-o DIR]\n", argv[0]);
    return 2;
}
