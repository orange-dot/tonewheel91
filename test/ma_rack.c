/* RK1/RK1b gates for the insert rack (docs/ma-rack-plan.md section 4):
 * exact bypass through the pool, the parametric EQ against the analytic
 * SVF for every band shape, structural and band changes without clicks,
 * extreme sweeps, determinism and spec validation. RK2: the chorus and
 * reverb blocks against the MA3-2 kernel bit for bit, the delay's echo
 * times, routing, sync and restart, the saturator's gain, harmonics and
 * tilts, the compressor's curve and timing, the instance caps and the
 * session helpers. Hosted test: libm only for the referees. */
#define _POSIX_C_SOURCE 200809L
#include <complex.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/ma_internal.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static unsigned checks, failures;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint64_t hash_frames(const ma_frame *f, size_t n, uint64_t h) {
    return tw_fnv1a64(f, n * sizeof *f, h);
}

/* Band b of an EQ slot, raising the band count to cover it. */
static void band(ma_rack_slot_spec *s, unsigned b, ma_eq_type type, float hz, float db, float q) {
    if (s->param[MA_EQ_BAND_COUNT] < (float)(b + 1)) s->param[MA_EQ_BAND_COUNT] = (float)(b + 1);
    s->param[MA_EQ_BAND(b, MA_EQ_TYPE)] = type;
    s->param[MA_EQ_BAND(b, MA_EQ_FREQ_HZ)] = hz;
    s->param[MA_EQ_BAND(b, MA_EQ_GAIN_DB)] = db;
    s->param[MA_EQ_BAND(b, MA_EQ_Q)] = q;
    s->param[MA_EQ_BAND(b, MA_EQ_ON)] = 1;
}

/* --- pool-level exact bypass ------------------------------------------- */

enum { POOL_FRAMES = 48000, BLOCK = 128 };
static ma_voice_pool pool;
static ma_rack rack;
static ma_frame frames[POOL_FRAMES];

static bool per_frame;   /* render through ma_voice_pool_tick instead */

static uint64_t render_pool(const ma_patch *patch, ma_rack *r, const ma_rack_spec *spec, bool wet) {
    ma_voice_pool_config config = ma_voice_pool_default_config;
    config.limit = 8;
    if (!ma_voice_pool_init(&pool, 48000, &config, patch)) return 0;
    /* Wet: a chorus and a reverb block after the spec's own (RK2). The wet
     * render without a rack of the caller's uses a second one, so "none"
     * still means none of the blocks under test. */
    static ma_rack wet_only;
    if (wet && !r) r = &wet_only;
    if (r) {
        ma_rack_spec chain = spec ? *spec : (ma_rack_spec){ 0 };
        if (wet && !(ma_rack_spec_set_chorus(&chain, .3f, .35f, .25f)
                     && ma_rack_spec_set_reverb(&chain, .25f, .65f, .55f))) return 0;
        ma_rack_init(r, 48000);
        if (spec || wet) (void)ma_rack_apply(r, &chain);
        ma_voice_pool_attach_rack(&pool, r);
    }
    static const uint8_t chord[] = { 48, 55, 60, 64 };
    for (unsigned i = 0; i < sizeof chord; i++) (void)ma_voice_pool_note_on(&pool, 0, chord[i], 100);
    for (size_t at = 0; at < POOL_FRAMES; at += BLOCK) {
        if (at == POOL_FRAMES / 2)
            for (unsigned i = 0; i < sizeof chord; i++) (void)ma_voice_pool_note_off(&pool, 0, chord[i], 64);
        if (per_frame)
            for (size_t i = 0; i < BLOCK; i++) frames[at + i] = ma_voice_pool_tick(&pool);
        else
            ma_voice_pool_render(&pool, frames + at, BLOCK);
    }
    return hash_frames(frames, POOL_FRAMES, 0);
}

static ma_rack_spec bypassed_spec(void) {
    ma_rack_spec s = { 0 };
    ma_rack_slot_init(&s.slot[0], MA_RACK_EQ, 1);          /* no bands */
    ma_rack_slot_init(&s.slot[1], MA_RACK_UTILITY, 2);     /* neutral utility */
    ma_rack_slot_init(&s.slot[3], MA_RACK_EQ, 3);          /* disabled */
    band(&s.slot[3], 0, MA_EQ_BELL, 900, 9, 1);
    s.slot[3].enabled = false;
    ma_rack_slot_init(&s.slot[2], MA_RACK_DELAY, 6);       /* mix zero (RK2) */
    s.slot[2].mix = 0;
    ma_rack_slot_init(&s.slot[4], MA_RACK_EQ, 5);          /* bands at 0 dB or off */
    band(&s.slot[4], 0, MA_EQ_BELL, 300, 0, 2);
    band(&s.slot[4], 1, MA_EQ_HIGH_SHELF, 5000, 0, .7f);
    band(&s.slot[4], 2, MA_EQ_LOW_CUT, 80, 0, .7071f);
    s.slot[4].param[MA_EQ_BAND(2, MA_EQ_ON)] = 0;
    ma_rack_slot_init(&s.slot[5], MA_RACK_UTILITY, 4);     /* mix zero */
    s.slot[5].param[MA_UTILITY_GAIN_DB] = -6;
    s.slot[5].mix = 0;
    return s;
}

static ma_rack_spec active_spec(void) {
    ma_rack_spec s = { 0 };
    ma_rack_slot_init(&s.slot[0], MA_RACK_EQ, 10);
    band(&s.slot[0], 0, MA_EQ_LOW_CUT, 60, 0, .7071f);
    band(&s.slot[0], 1, MA_EQ_LOW_SHELF, 150, 4, .7071f);
    band(&s.slot[0], 2, MA_EQ_BELL, 1200, -5, 1.5f);
    band(&s.slot[0], 3, MA_EQ_HIGH_SHELF, 7000, 3, .7071f);
    ma_rack_slot_init(&s.slot[1], MA_RACK_UTILITY, 11);
    s.slot[1].param[MA_UTILITY_GAIN_DB] = -2;
    s.slot[1].param[MA_UTILITY_WIDTH] = 1.4f;
    return s;
}

static void test_pool_bypass(void) {
    static const ma_patch *patches[] = { &ma_patch_tepih, &ma_patch_lead, &ma_patch_granica };
    ma_rack_spec empty = { 0 }, bypassed = bypassed_spec(), active = active_spec();
    CHECK(ma_rack_spec_valid(&bypassed) && ma_rack_spec_valid(&active), "test specs invalid");
    for (unsigned p = 0; p < 3; p++)
        for (int wet = 0; wet < 2; wet++) {
            uint64_t none = render_pool(patches[p], nullptr, nullptr, wet);
            uint64_t attached = render_pool(patches[p], &rack, nullptr, wet);
            uint64_t emptied = render_pool(patches[p], &rack, &empty, wet);
            uint64_t bypass = render_pool(patches[p], &rack, &bypassed, wet);
            CHECK(none && none == attached && none == emptied && none == bypass,
                  "patch %u wet %d: bypass changed bits (%016llx %016llx %016llx %016llx)", p, wet,
                  (unsigned long long)none, (unsigned long long)attached,
                  (unsigned long long)emptied, (unsigned long long)bypass);
            uint64_t a1 = render_pool(patches[p], &rack, &active, wet);
            uint64_t a2 = render_pool(patches[p], &rack, &active, wet);
            CHECK(a1 == a2, "patch %u wet %d: active rack not deterministic", p, wet);
            uint64_t runs = rack.lane_runs;
            per_frame = true;
            uint64_t ticked = render_pool(patches[p], &rack, &active, wet);
            per_frame = false;
            CHECK(a1 == ticked, "patch %u wet %d: block render %016llx differs from per-frame %016llx", p, wet,
                  (unsigned long long)a1, (unsigned long long)ticked);
            if (ma_lanes_supported()) CHECK(runs > 0, "patch %u wet %d: the wavefront never ran", p, wet);
            CHECK(a1 != none, "patch %u wet %d: active rack changed nothing", p, wet);
            if (p == 0 && wet == 0)
                printf("rack pool FNV (tepih dry): none %016llx, active %016llx\n",
                       (unsigned long long)none, (unsigned long long)a1);
        }
}

/* --- EQ response against the analytic TPT SVF ---------------------------- */

static double clamp_hz(double hz, double fs) {
    double x = hz / fs;
    if (x < 10.0 / fs) x = 10.0 / fs;
    if (x > .45) x = .45;
    return x * fs;
}

/* H of one band from Simper's forms in double with exact tan and pow: the
 * referee for both the coefficient approximations and the DSP. */
static double complex band_response(int type, double hz, double db, double q, double f, double fs) {
    double a = pow(10, db / 40), t = tan(M_PI * clamp_hz(hz, fs) / fs), k = 1 / q, g = t;
    double m0 = 1, m1 = 0, m2 = 0;
    switch (type) {
    case MA_EQ_BELL: k = 1 / (q * a); m1 = k * (a * a - 1); break;
    case MA_EQ_LOW_SHELF: g = t / sqrt(a); m1 = k * (a - 1); m2 = a * a - 1; break;
    case MA_EQ_HIGH_SHELF: g = t * sqrt(a); m0 = a * a; m1 = k * (1 - a) * a; m2 = 1 - a * a; break;
    case MA_EQ_LOW_CUT: m1 = -k; m2 = -1; break;
    case MA_EQ_HIGH_CUT: m0 = 0; m2 = 1; break;
    default: m1 = -k; break;
    }
    double complex s = I * tan(M_PI * f / fs) / g;
    double complex den = s * s + k * s + 1;
    return m0 + m1 * s / den + m2 / den;
}

static double eq_reference_db(const ma_rack_slot_spec *s, double f, double fs) {
    double complex h = 1;
    unsigned count = (unsigned)s->param[MA_EQ_BAND_COUNT];
    for (unsigned b = 0; b < count; b++) {
        if (!s->param[MA_EQ_BAND(b, MA_EQ_ON)]) continue;
        h *= band_response((int)s->param[MA_EQ_BAND(b, MA_EQ_TYPE)], s->param[MA_EQ_BAND(b, MA_EQ_FREQ_HZ)],
                           s->param[MA_EQ_BAND(b, MA_EQ_GAIN_DB)], s->param[MA_EQ_BAND(b, MA_EQ_Q)], f, fs);
    }
    return 20 * log10(cabs(h)) + s->param[MA_EQ_OUTPUT_DB];
}

/* Steady-state gain of the rack at f: settle, then fit sin/cos over a whole
 * number of cycles. */
static double measure_db(const ma_rack_spec *spec, double f, double fs) {
    ma_rack_init(&rack, (float)fs);
    (void)ma_rack_apply(&rack, spec);
    const double amp = .25;
    size_t settle = (size_t)(fs * .5), cycles = (size_t)ceil(f * .5);
    if (cycles < 20) cycles = 20;
    size_t n = (size_t)llround((double)cycles * fs / f);
    double ws = 0, wc = 0, w = 2 * M_PI * f / fs;
    for (size_t i = 0; i < settle + n; i++) {
        float x = (float)(amp * sin(w * (double)i));
        ma_frame y = ma_rack_tick(&rack, (ma_frame){ x, x });
        if (i >= settle) {
            ws += y.left * sin(w * (double)i);
            wc += y.left * cos(w * (double)i);
        }
    }
    double a = 2 * hypot(ws, wc) / (double)n;
    return 20 * log10(a / amp);
}

static void test_eq_response(void) {
    static const double rates[] = { 44100, 48000, 96000, 192000 };
    static const double freqs[] = { 25, 50, 80, 120, 300, 1000, 2500, 6000, 10000, 16000 };
    enum { CONFIGS = 8 };
    ma_rack_slot_spec c[CONFIGS];
    for (int i = 0; i < CONFIGS; i++) ma_rack_slot_init(&c[i], MA_RACK_EQ, 1);
    band(&c[0], 0, MA_EQ_BELL, 1000, -9, 2);
    band(&c[1], 0, MA_EQ_LOW_SHELF, 120, 12, .7071f);
    band(&c[2], 0, MA_EQ_HIGH_SHELF, 6000, 6, .7071f);
    band(&c[3], 0, MA_EQ_LOW_CUT, 80, 0, .7071f);
    band(&c[4], 0, MA_EQ_HIGH_CUT, 5000, 0, 1.2f);
    band(&c[5], 0, MA_EQ_NOTCH, 2500, 0, 4);
    /* All eight bands, every shape, plus output gain. */
    band(&c[6], 0, MA_EQ_LOW_CUT, 30, 0, .7071f);
    band(&c[6], 1, MA_EQ_LOW_SHELF, 100, -4, .9f);
    band(&c[6], 2, MA_EQ_BELL, 300, 5, .7f);
    band(&c[6], 3, MA_EQ_BELL, 900, -7, 3);
    band(&c[6], 4, MA_EQ_NOTCH, 3000, 0, 8);
    band(&c[6], 5, MA_EQ_BELL, 5000, 4, 1);
    band(&c[6], 6, MA_EQ_HIGH_SHELF, 9000, -6, .7071f);
    band(&c[6], 7, MA_EQ_HIGH_CUT, 15000, 0, .7071f);
    c[6].param[MA_EQ_OUTPUT_DB] = 2.5f;
    /* Very wide and very narrow. */
    band(&c[7], 0, MA_EQ_BELL, 400, 10, .1f);
    band(&c[7], 1, MA_EQ_BELL, 4000, -12, 40);
    double worst = 0;
    for (unsigned ri = 0; ri < 4; ri++)
        for (int k = 0; k < CONFIGS; k++) {
            ma_rack_spec spec = { 0 };
            spec.slot[0] = c[k];
            for (unsigned fi = 0; fi < sizeof freqs / sizeof *freqs; fi++) {
                double f = freqs[fi], fs = rates[ri];
                if (f > .45 * fs) continue;
                double got = measure_db(&spec, f, fs), want = eq_reference_db(&c[k], f, fs);
                /* Deep stops (notch, cut skirts) are compared in absolute
                 * terms: below -60 dB both are "gone". */
                double err = want < -60 && got < -60 ? 0 : fabs(got - want);
                if (err > worst) worst = err;
                CHECK(err <= .1, "fs %.0f config %d f %.0f: %.3f dB, analytic %.3f dB", fs, k, f, got, want);
            }
        }
    printf("rack EQ: worst deviation from the analytic SVF %.4f dB (8 configurations, 4 rates)\n", worst);

    /* The intent the formulas must carry, measured on the block. */
    ma_rack_spec spec = { 0 };
    spec.slot[0] = c[0];
    double v = measure_db(&spec, 1000, 48000);
    CHECK(fabs(v + 9) < .1, "bell at its centre %.3f dB, want -9", v);
    spec.slot[0] = c[3];
    v = measure_db(&spec, 80, 48000);
    CHECK(fabs(v + 3.0103) < .1, "low cut at its corner %.3f dB, want -3.01", v);
    spec.slot[0] = c[1];
    v = measure_db(&spec, 12, 48000);
    CHECK(fabs(v - 12) < .3, "low shelf plateau %.3f dB, want +12", v);
    spec.slot[0] = c[2];
    v = measure_db(&spec, 20000, 48000);
    CHECK(fabs(v - 6) < .3, "high shelf plateau %.3f dB, want +6", v);
    spec.slot[0] = c[5];
    v = measure_db(&spec, 2500, 48000);
    CHECK(v < -40, "notch at its centre %.1f dB, want below -40", v);
}

/* --- utility -------------------------------------------------------------- */

/* A new block enters through its 10 ms fade (RKD6); read it once settled. */
static ma_frame settled_tick(ma_frame x) {
    for (int i = 0; i < 2000; i++) (void)ma_rack_tick(&rack, x);
    return ma_rack_tick(&rack, x);
}

static void test_utility(void) {
    ma_rack_spec spec = { 0 };
    ma_rack_slot_init(&spec.slot[0], MA_RACK_UTILITY, 1);
    spec.slot[0].param[MA_UTILITY_GAIN_DB] = -6;
    ma_rack_init(&rack, 48000);
    (void)ma_rack_apply(&rack, &spec);
    ma_frame y = settled_tick((ma_frame){ .5f, -.25f });
    CHECK(fabs(y.left / .5 - pow(10, -6 / 20.0)) < 1e-6 && fabs(y.right / -.25 - pow(10, -6 / 20.0)) < 1e-6,
          "utility -6 dB: %g %g", y.left, y.right);
    spec.slot[0].param[MA_UTILITY_GAIN_DB] = 0;
    spec.slot[0].param[MA_UTILITY_WIDTH] = 0;
    ma_rack_init(&rack, 48000);
    (void)ma_rack_apply(&rack, &spec);
    y = settled_tick((ma_frame){ .5f, -.25f });
    CHECK(y.left == y.right && fabsf(y.left - .125f) < 1e-7f, "utility width 0 is mono: %g %g", y.left, y.right);
    spec.slot[0].param[MA_UTILITY_WIDTH] = 1;
    spec.slot[0].param[MA_UTILITY_PAN] = 1;
    ma_rack_init(&rack, 48000);
    (void)ma_rack_apply(&rack, &spec);
    y = settled_tick((ma_frame){ .5f, -.25f });
    CHECK(y.left == 0 && y.right == -.25f, "utility pan right: %g %g", y.left, y.right);
}

/* --- clicks: structure, band shape, band count ----------------------------- */

static double largest_step(ma_frame *prev, ma_frame y, double worst) {
    double d = fmax(fabs(y.left - prev->left), fabs(y.right - prev->right));
    *prev = y;
    return d > worst ? d : worst;
}

/* Feed a 220 Hz sine, apply one spec per second, and compare the largest
 * frame step with the largest steady step of every spec alone (+10 %). */
static void click_run(const char *what, const ma_rack_spec *const *steps, unsigned count,
                      uint64_t want_structural) {
    const double fs = 48000, amp = .25, w = 2 * M_PI * 220 / fs;
    double steady = amp * w;
    static ma_rack probe;
    for (unsigned k = 0; k < count; k++) {
        ma_rack_init(&probe, (float)fs);
        (void)ma_rack_apply(&probe, steps[k]);
        ma_frame prev = { 0 };
        double s = 0;
        for (size_t i = 0; i < 48000; i++) {
            float x = (float)(amp * sin(w * (double)i));
            ma_frame y = ma_rack_tick(&probe, (ma_frame){ x, x });
            if (i > 24000) s = largest_step(&prev, y, s);
            else prev = y;
        }
        if (s > steady) steady = s;
    }
    ma_rack_init(&rack, (float)fs);
    ma_frame prev = { 0 };
    double worst = 0;
    bool finite_all = true;
    for (size_t i = 0; i < (size_t)((count + 1) * fs); i++) {
        size_t k = i / (size_t)fs;
        if (i % (size_t)fs == 0 && k < count) CHECK(ma_rack_apply(&rack, steps[k]), "%s: step %zu rejected", what, k);
        float x = (float)(amp * sin(w * (double)i));
        ma_frame y = ma_rack_tick(&rack, (ma_frame){ x, x });
        if (!isfinite(y.left) || !isfinite(y.right)) finite_all = false;
        if (i) worst = largest_step(&prev, y, worst);
        else prev = y;
    }
    printf("rack %s: largest step %.5f against steady %.5f (%llu structural, %llu adoptions)\n", what, worst,
           steady, (unsigned long long)rack.structural_changes, (unsigned long long)rack.adoptions);
    CHECK(finite_all, "%s: non-finite output", what);
    CHECK(worst <= steady * 1.10, "%s clicked: step %.5f, steady %.5f", what, worst, steady);
    CHECK(rack.structural_changes == want_structural, "%s: %llu structural changes, want %llu", what,
          (unsigned long long)rack.structural_changes, (unsigned long long)want_structural);
    CHECK(!ma_rack_moving(&rack), "%s: rack still moving after the last change", what);
}

static void test_clicks(void) {
    /* Structure: insert, reorder, remove, kind change on one id. */
    ma_rack_spec a = { 0 }, b = { 0 }, c = { 0 }, d = { 0 };
    ma_rack_slot_init(&a.slot[0], MA_RACK_EQ, 1);
    band(&a.slot[0], 0, MA_EQ_LOW_SHELF, 300, 12, .7071f);
    ma_rack_slot_init(&a.slot[1], MA_RACK_UTILITY, 2);
    a.slot[1].param[MA_UTILITY_GAIN_DB] = -12;
    b.slot[0] = a.slot[1];
    b.slot[1] = a.slot[0];
    c.slot[2] = a.slot[1];
    ma_rack_slot_init(&d.slot[0], MA_RACK_EQ, 2);
    band(&d.slot[0], 0, MA_EQ_BELL, 220, 18, 1);
    const ma_rack_spec *structure[] = { &a, &b, &c, &d, &a };
    click_run("structure", structure, 5, 5);

    /* Bands inside one EQ: shape change, band on/off, count up and down,
     * a big gain jump. None of these is structural. */
    ma_rack_spec e[6] = { 0 };
    for (int i = 0; i < 6; i++) ma_rack_slot_init(&e[i].slot[0], MA_RACK_EQ, 7);
    band(&e[0].slot[0], 0, MA_EQ_BELL, 220, 9, 1);
    e[1] = e[0];
    e[1].slot[0].param[MA_EQ_BAND(0, MA_EQ_TYPE)] = MA_EQ_HIGH_CUT;   /* shape swap at 220 Hz */
    e[2] = e[1];
    e[2].slot[0].param[MA_EQ_BAND(0, MA_EQ_ON)] = 0;
    e[3] = e[0];
    band(&e[3].slot[0], 1, MA_EQ_NOTCH, 230, 0, 2);
    band(&e[3].slot[0], 2, MA_EQ_LOW_SHELF, 400, -12, .7071f);
    e[4] = e[0];                                                       /* count 3 -> 1 */
    e[5] = e[0];
    e[5].slot[0].param[MA_EQ_BAND(0, MA_EQ_GAIN_DB)] = -24;
    const ma_rack_spec *bands[] = { &e[0], &e[1], &e[2], &e[3], &e[4], &e[5] };
    click_run("bands", bands, 6, 1);   /* only the first insert is structural */

    /* RK2 kinds: insert, reorder, replace, remove. A buffered block starts
     * from an empty line and takes its input through the gate, so its first
     * delayed copy does not begin with a step. */
    ma_rack_spec k[5] = { 0 };
    ma_rack_slot_init(&k[0].slot[0], MA_RACK_SATURATOR, 31);
    k[0].slot[0].param[MA_SATURATOR_DRIVE] = .6f;
    ma_rack_slot_init(&k[0].slot[1], MA_RACK_COMPRESSOR, 32);
    k[0].slot[1].param[MA_COMPRESSOR_THRESHOLD_DB] = -40;
    k[1].slot[0] = k[0].slot[1];
    k[1].slot[1] = k[0].slot[0];
    ma_rack_slot_init(&k[2].slot[0], MA_RACK_DELAY, 33);
    k[2].slot[0].param[MA_DELAY_TIME_S] = .11f;
    ma_rack_slot_init(&k[2].slot[1], MA_RACK_CHORUS, 34);
    ma_rack_slot_init(&k[3].slot[0], MA_RACK_REVERB, 35);
    k[4].slot[0] = k[0].slot[1];
    k[4].slot[1] = k[2].slot[0];
    k[4].slot[2] = k[0].slot[0];
    const ma_rack_spec *kinds[] = { &k[0], &k[1], &k[2], &k[3], &k[4] };
    click_run("RK2 kinds", kinds, 5, 5);
}

/* --- sweeps ---------------------------------------------------------------- */

static float urand(uint64_t *s) { return (float)(tw_splitmix64(s) >> 40) / 16777216.0f; }

static float pick(uint64_t *s, ma_rack_kind kind, unsigned p) {
    ma_rack_param_info info;
    (void)ma_rack_param_info_at(kind, p, &info);
    float u = urand(s);
    if (info.curve == MA_RACK_TOGGLE) return u < .8f ? 1 : 0;
    if (info.curve == MA_RACK_STEPPED) return (float)(int)(u * (info.maximum - info.minimum + 1) + info.minimum);
    if (urand(s) < .15f) return u < .5f ? info.minimum : info.maximum;
    if (info.curve == MA_RACK_LOG) return info.minimum * powf(info.maximum / info.minimum, u);
    return info.minimum + u * (info.maximum - info.minimum);
}

/* `held` counts the capped kinds of the spec under construction (RKD8); a
 * kind at its cap becomes an EQ. */
static void random_slot(ma_rack_slot_spec *slot, uint64_t *s, uint32_t id, unsigned held[MA_RACK_KIND_COUNT]) {
    float u = urand(s);
    ma_rack_kind kind = u < .35f ? MA_RACK_EQ : u < .50f ? MA_RACK_UTILITY : u < .60f ? MA_RACK_CHORUS
                      : u < .70f ? MA_RACK_REVERB : u < .82f ? MA_RACK_DELAY : u < .91f ? MA_RACK_SATURATOR
                      : MA_RACK_COMPRESSOR;
    static const unsigned cap[MA_RACK_KIND_COUNT] = {
        [MA_RACK_CHORUS] = MA_RACK_CHORUSES, [MA_RACK_REVERB] = MA_RACK_REVERBS, [MA_RACK_DELAY] = MA_RACK_DELAYS,
    };
    if (cap[kind] && held[kind] >= cap[kind]) kind = MA_RACK_EQ;
    held[kind]++;
    ma_rack_slot_init(slot, kind, id);
    slot->enabled = urand(s) < .9f;
    slot->mix = urand(s) < .2f ? urand(s) : 1;
    if (kind != MA_RACK_EQ) {
        for (unsigned p = 0; p < ma_rack_param_count(kind); p++) slot->param[p] = pick(s, kind, p);
        return;
    }
    unsigned count = (unsigned)pick(s, kind, MA_EQ_BAND_COUNT);
    slot->param[MA_EQ_BAND_COUNT] = (float)count;
    slot->param[MA_EQ_OUTPUT_DB] = pick(s, kind, MA_EQ_OUTPUT_DB);
    for (unsigned b = 0; b < count; b++)
        for (unsigned f = 0; f < MA_EQ_BAND_FIELDS; f++)
            slot->param[MA_EQ_BAND(b, f)] = pick(s, kind, MA_EQ_BAND(b, f));
}

static void test_sweeps(void) {
    static const float rates[] = { 44100, 192000 };
    for (int ri = 0; ri < 2; ri++) {
        uint64_t seed = 0x52414b31u + (uint64_t)ri;
        ma_rack_init(&rack, rates[ri]);
        double peak = 0;
        bool finite_all = true, valid = true;
        for (size_t i = 0; i < 400000; i++) {
            if (i % 64 == 0) {
                ma_rack_spec spec = { 0 };
                unsigned n = (unsigned)(urand(&seed) * 9), held[MA_RACK_KIND_COUNT] = { 0 };
                for (unsigned j = 0; j < n && j < MA_RACK_SLOTS; j++)
                    random_slot(&spec.slot[j], &seed, 1 + (uint32_t)(urand(&seed) * 3) * 8 + j, held);
                if (!ma_rack_apply(&rack, &spec)) valid = false;
                if (urand(&seed) < .05f) ma_rack_set_tempo(&rack, 20 + 380 * urand(&seed));
            }
            float x = 2 * urand(&seed) - 1, z = 2 * urand(&seed) - 1;
            ma_frame y = ma_rack_tick(&rack, (ma_frame){ x, z });
            if (!isfinite(y.left) || !isfinite(y.right)) finite_all = false;
            double m = fmax(fabs(y.left), fabs(y.right));
            if (m > peak) peak = m;
        }
        CHECK(valid, "sweep at %.0f Hz: a random spec was rejected", rates[ri]);
        CHECK(finite_all, "sweep at %.0f Hz: non-finite output", rates[ri]);
        CHECK(rack.sanitizations == 0, "sweep at %.0f Hz: %llu sanitizations", rates[ri],
              (unsigned long long)rack.sanitizations);
        CHECK(peak < 1e9, "sweep at %.0f Hz: peak %g", rates[ri], peak);
        printf("rack sweep %.0f Hz: peak %.1f, %llu applies\n", rates[ri], peak, (unsigned long long)rack.applies);
    }
    /* Eight EQs, eight bands each, all active, held: bounded and finite. */
    ma_rack_spec full = { 0 };
    uint64_t seed = 0x4551384bu;
    for (unsigned j = 0; j < MA_RACK_SLOTS; j++) {
        ma_rack_slot_init(&full.slot[j], MA_RACK_EQ, j + 1);
        for (unsigned b = 0; b < MA_EQ_BANDS; b++)
            band(&full.slot[j], b, (ma_eq_type)(b % MA_EQ_TYPE_COUNT), 40 * powf(400, urand(&seed)),
                 -6 + 12 * urand(&seed), .5f + 2 * urand(&seed));
    }
    CHECK(ma_rack_spec_valid(&full), "full spec invalid");
    ma_rack_init(&rack, 48000);
    (void)ma_rack_apply(&rack, &full);
    double peak = 0;
    bool finite_all = true;
    for (size_t i = 0; i < 96000; i++) {
        float x = 2 * urand(&seed) - 1;
        ma_frame y = ma_rack_tick(&rack, (ma_frame){ x, -x });
        if (!isfinite(y.left) || !isfinite(y.right)) finite_all = false;
        peak = fmax(peak, fmax(fabs(y.left), fabs(y.right)));
    }
    CHECK(finite_all && peak < 1e6, "64 active bands: finite %d, peak %g", finite_all, peak);
    printf("rack 8 EQs x %d bands: peak %.2f on full-scale noise\n", MA_EQ_BANDS, peak);
}

/* --- block-wise processing against the per-frame tick ------------------------ */

/* Two racks get the same specs and input; one ticks frame by frame, the
 * other runs ma_rack_process over random run lengths (the AVX2 wavefront
 * where an EQ is settled). Every output bit must agree, including through
 * edits, ramps, fades, skipped frames and huge gains. */
static void test_block_parity(void) {
    static ma_rack a, b;
    bool lanes = ma_lanes_supported();
    uint64_t seed = 0x57415645u;
    ma_rack_init(&a, 48000);
    ma_rack_init(&b, 48000);
    static ma_frame x[4096], y[4096];
    static bool skip[4096];
    size_t mismatches = 0, frames_done = 0;
    for (int round = 0; round < 400; round++) {
        if (round % 5 == 0) {
            ma_rack_spec spec = { 0 };
            unsigned n = 1 + (unsigned)(urand(&seed) * 8), held[MA_RACK_KIND_COUNT] = { 0 };
            for (unsigned j = 0; j < n && j < MA_RACK_SLOTS; j++)
                random_slot(&spec.slot[j], &seed, 1 + (uint32_t)(urand(&seed) * 3) * 8 + j, held);
            (void)ma_rack_apply(&a, &spec);
            (void)ma_rack_apply(&b, &spec);
        }
        size_t n = 1 + (size_t)(urand(&seed) * 700);
        bool skipping = urand(&seed) < .05f;
        for (size_t i = 0; i < n; i++) {
            x[i] = (ma_frame){ .3f * (2 * urand(&seed) - 1), .3f * (2 * urand(&seed) - 1) };
            skip[i] = skipping && urand(&seed) < .1f;
        }
        for (size_t i = 0; i < n; i++) y[i] = skip[i] ? x[i] : ma_rack_tick(&a, x[i]);
        ma_rack_process(&b, x, skipping ? skip : nullptr, n, lanes);
        for (size_t i = 0; i < n; i++) mismatches += memcmp(&x[i], &y[i], sizeof x[i]) != 0;
        frames_done += n;
    }
    printf("rack block parity: %zu frames, %llu wavefront runs, %zu mismatches\n", frames_done,
           (unsigned long long)b.lane_runs, mismatches);
    CHECK(!mismatches, "block processing differs from the per-frame tick in %zu frames", mismatches);
    if (lanes) CHECK(b.lane_runs > 100, "the wavefront ran only %llu times", (unsigned long long)b.lane_runs);
}

/* --- smoothing and validation --------------------------------------------- */

static void test_smoothing(void) {
    ma_rack_spec spec = { 0 };
    ma_rack_slot_init(&spec.slot[0], MA_RACK_EQ, 7);
    band(&spec.slot[0], 0, MA_EQ_BELL, 1000, 6, 1);
    ma_rack_init(&rack, 48000);
    (void)ma_rack_apply(&rack, &spec);
    for (int i = 0; i < 1000; i++) (void)ma_rack_tick(&rack, (ma_frame){ .1f, .1f });
    CHECK(!ma_rack_moving(&rack), "settled before the edit");
    spec.slot[0].param[MA_EQ_BAND(0, MA_EQ_GAIN_DB)] = -6;
    (void)ma_rack_apply(&rack, &spec);
    CHECK(ma_rack_moving(&rack) && rack.structural_changes == 1, "a parameter edit is not structural");
    int frames_moving = 0;
    while (ma_rack_moving(&rack) && frames_moving < 10000) {
        (void)ma_rack_tick(&rack, (ma_frame){ .1f, .1f });
        frames_moving++;
    }
    CHECK(frames_moving == 288, "6 ms ramp at 48 kHz took %d frames", frames_moving);
    /* A shape change fades the band out and back in: twice the ramp. */
    spec.slot[0].param[MA_EQ_BAND(0, MA_EQ_TYPE)] = MA_EQ_LOW_SHELF;
    (void)ma_rack_apply(&rack, &spec);
    frames_moving = 0;
    while (ma_rack_moving(&rack) && frames_moving < 10000) {
        (void)ma_rack_tick(&rack, (ma_frame){ .1f, .1f });
        frames_moving++;
    }
    CHECK(frames_moving >= 2 * 288 - 2 && frames_moving <= 2 * 288 + 2, "shape change took %d frames", frames_moving);
    /* Settle jumps to the end at once. */
    spec.slot[0].param[MA_EQ_BAND(0, MA_EQ_GAIN_DB)] = 3;
    (void)ma_rack_apply(&rack, &spec);
    ma_rack_settle(&rack);
    CHECK(!ma_rack_moving(&rack), "settle left the rack moving");
}

static void test_validation(void) {
    ma_rack_spec good = active_spec();
    CHECK(ma_rack_spec_valid(&good), "active spec invalid");
    ma_rack_spec s = good;
    s.slot[1].id = s.slot[0].id;
    CHECK(!ma_rack_spec_valid(&s), "duplicate ids accepted");
    s = good; s.slot[0].id = 0;
    CHECK(!ma_rack_spec_valid(&s), "id 0 accepted");
    s = good; s.slot[0].param[MA_EQ_BAND(2, MA_EQ_GAIN_DB)] = 25;
    CHECK(!ma_rack_spec_valid(&s), "out-of-range gain accepted");
    s = good; s.slot[0].param[MA_EQ_BAND(0, MA_EQ_ON)] = .5f;
    CHECK(!ma_rack_spec_valid(&s), "half toggle accepted");
    s = good; s.slot[0].param[MA_EQ_BAND(0, MA_EQ_TYPE)] = 1.5f;
    CHECK(!ma_rack_spec_valid(&s), "fractional band type accepted");
    s = good; s.slot[0].param[MA_EQ_BAND(0, MA_EQ_TYPE)] = MA_EQ_TYPE_COUNT;
    CHECK(!ma_rack_spec_valid(&s), "unknown band type accepted");
    s = good; s.slot[0].param[MA_EQ_BAND_COUNT] = MA_EQ_BANDS + 1;
    CHECK(!ma_rack_spec_valid(&s), "band count past the maximum accepted");
    s = good; s.slot[0].param[MA_EQ_BAND(6, MA_EQ_FREQ_HZ)] = 500;
    CHECK(!ma_rack_spec_valid(&s), "a band past the count off its initial values accepted");
    s = good; s.slot[0].param[MA_EQ_BAND(0, MA_EQ_Q)] = .05f;
    CHECK(!ma_rack_spec_valid(&s), "Q below its range accepted");
    s = good; s.slot[0].mix = NAN;
    CHECK(!ma_rack_spec_valid(&s), "NaN mix accepted");
    s = good; s.slot[1].param[MA_UTILITY_PARAM_COUNT] = 1;
    CHECK(!ma_rack_spec_valid(&s), "unused parameter accepted");
    s = good; s.slot[4].mix = .5f;
    CHECK(!ma_rack_spec_valid(&s), "garbage in an empty slot accepted");
    s = good; s.slot[2].kind = MA_RACK_KIND_COUNT; s.slot[2].id = 99;
    CHECK(!ma_rack_spec_valid(&s), "unknown kind accepted");
    ma_rack_init(&rack, 48000);
    CHECK(ma_rack_apply(&rack, &good), "good spec rejected");
    s = good; s.slot[0].param[MA_EQ_BAND(0, MA_EQ_GAIN_DB)] = 40;
    uint64_t applies = rack.applies;
    CHECK(!ma_rack_apply(&rack, &s) && rack.applies == applies, "invalid spec applied");
    CHECK(ma_rack_param_count(MA_RACK_EQ) == MA_EQ_PARAM_COUNT
          && ma_rack_param_count(MA_RACK_UTILITY) == MA_UTILITY_PARAM_COUNT
          && ma_rack_param_count(MA_RACK_EMPTY) == 0, "parameter counts");
    CHECK(ma_rack_param_count(MA_RACK_CHORUS) == MA_CHORUS_PARAM_COUNT
          && ma_rack_param_count(MA_RACK_REVERB) == MA_REVERB_PARAM_COUNT
          && ma_rack_param_count(MA_RACK_DELAY) == MA_DELAY_PARAM_COUNT
          && ma_rack_param_count(MA_RACK_SATURATOR) == MA_SATURATOR_PARAM_COUNT
          && ma_rack_param_count(MA_RACK_COMPRESSOR) == MA_COMPRESSOR_PARAM_COUNT
          && ma_rack_param_count(MA_RACK_KIND_COUNT) == 0, "RK2 parameter counts");
    /* RKD8: one chorus, one reverb, two delays. */
    ma_rack_spec caps = { 0 };
    ma_rack_slot_init(&caps.slot[0], MA_RACK_DELAY, 1);
    ma_rack_slot_init(&caps.slot[1], MA_RACK_DELAY, 2);
    ma_rack_slot_init(&caps.slot[2], MA_RACK_CHORUS, 3);
    ma_rack_slot_init(&caps.slot[3], MA_RACK_REVERB, 4);
    ma_rack_slot_init(&caps.slot[4], MA_RACK_SATURATOR, 5);
    ma_rack_slot_init(&caps.slot[5], MA_RACK_SATURATOR, 6);
    ma_rack_slot_init(&caps.slot[6], MA_RACK_COMPRESSOR, 7);
    ma_rack_slot_init(&caps.slot[7], MA_RACK_COMPRESSOR, 8);
    CHECK(ma_rack_spec_valid(&caps), "a chain at the instance caps rejected");
    s = caps; ma_rack_slot_init(&s.slot[4], MA_RACK_DELAY, 5);
    CHECK(!ma_rack_spec_valid(&s), "a third delay accepted");
    s = caps; ma_rack_slot_init(&s.slot[4], MA_RACK_CHORUS, 5);
    CHECK(!ma_rack_spec_valid(&s), "a second chorus accepted");
    s = caps; ma_rack_slot_init(&s.slot[4], MA_RACK_REVERB, 5);
    CHECK(!ma_rack_spec_valid(&s), "a second reverb accepted");
    s = caps; s.slot[0].param[MA_DELAY_SYNC] = MA_DELAY_SYNC_COUNT;
    CHECK(!ma_rack_spec_valid(&s), "an unknown delay sync accepted");
    s = caps; s.slot[0].param[MA_DELAY_TIME_S] = 3;
    CHECK(!ma_rack_spec_valid(&s), "a delay time past the line accepted");
    s = caps; s.slot[6].param[MA_COMPRESSOR_RATIO] = .5f;
    CHECK(!ma_rack_spec_valid(&s), "a ratio below 1 accepted");
    CHECK(caps.slot[0].mix == .30f && caps.slot[2].mix == .30f && caps.slot[3].mix == .25f
          && caps.slot[4].mix == 1 && caps.slot[6].mix == 1
          && ma_rack_mix_initial(MA_RACK_REVERB) == .25f && ma_rack_mix_initial(MA_RACK_EQ) == 1,
          "initial mixes");
    CHECK(ma_delay_sync_beats(MA_DELAY_SYNC_4) == 1 && ma_delay_sync_beats(MA_DELAY_SYNC_8D) == .75f
          && ma_delay_sync_beats(MA_DELAY_SYNC_FREE) == 0 && ma_delay_sync_beats(MA_DELAY_SYNC_COUNT) == 0,
          "sync note values");
    ma_rack_init(&rack, 48000);
    CHECK(rack.tempo_bpm == 120, "tempo after init %g", rack.tempo_bpm);
    ma_rack_set_tempo(&rack, 10);
    CHECK(rack.tempo_bpm == 20, "tempo floor %g", rack.tempo_bpm);
    ma_rack_set_tempo(&rack, 1000);
    CHECK(rack.tempo_bpm == 400, "tempo ceiling %g", rack.tempo_bpm);
    ma_rack_set_tempo(&rack, NAN);
    CHECK(rack.tempo_bpm == 120, "tempo for a non-finite value %g", rack.tempo_bpm);
}

/* --- RK2: the session's chorus and reverb as blocks ------------------------------ */

static void test_session_blocks(void) {
    ma_rack_spec s = { 0 }, before;
    CHECK(ma_rack_spec_set_chorus(&s, 0, .35f, .25f) && ma_rack_spec_empty(&s), "a chorus at mix 0 added a block");
    CHECK(ma_rack_spec_set_reverb(&s, .25f, .65f, .55f) && s.slot[0].kind == MA_RACK_REVERB
          && s.slot[0].id == MA_RACK_ID_REVERB && s.slot[0].enabled && s.slot[0].mix == .25f
          && s.slot[0].param[MA_REVERB_SIZE] == .65f && s.slot[0].param[MA_REVERB_DAMPING] == .55f,
          "session reverb");
    CHECK(ma_rack_spec_set_chorus(&s, .3f, .35f, .25f) && s.slot[0].kind == MA_RACK_CHORUS
          && s.slot[0].id == MA_RACK_ID_CHORUS && s.slot[1].kind == MA_RACK_REVERB && s.slot[1].mix == .25f
          && s.slot[2].kind == MA_RACK_EMPTY && ma_rack_spec_valid(&s), "the chorus goes before the session reverb");
    CHECK(ma_rack_spec_set_chorus(&s, 0, .5f, 2) && s.slot[0].kind == MA_RACK_CHORUS && s.slot[0].mix == 0
          && s.slot[0].param[MA_CHORUS_DEPTH] == .5f && s.slot[0].param[MA_CHORUS_RATE_HZ] == 2,
          "an existing block takes mix 0 and the values");
    CHECK(ma_rack_spec_set_chorus(&s, 2, -1, 100) && s.slot[0].mix == 1 && s.slot[0].param[MA_CHORUS_DEPTH] == 0
          && s.slot[0].param[MA_CHORUS_RATE_HZ] == 20 && ma_rack_spec_valid(&s), "values clamp to their ranges");
    CHECK(ma_rack_spec_set_reverb(&s, NAN, NAN, 5) && s.slot[1].mix == 0 && s.slot[1].param[MA_REVERB_SIZE] == 0
          && s.slot[1].param[MA_REVERB_DAMPING] == .99f && ma_rack_spec_valid(&s), "non-finite values take the low bound");

    /* The patch's own block takes the values; no second block. */
    s = (ma_rack_spec){ 0 };
    ma_rack_slot_init(&s.slot[2], MA_RACK_CHORUS, 7);
    s.slot[2].enabled = false;
    s.slot[2].mix = .9f;
    ma_rack_slot_init(&s.slot[5], MA_RACK_EQ, 8);
    CHECK(ma_rack_spec_set_chorus(&s, .4f, .35f, .25f) && s.slot[2].id == 7 && s.slot[2].enabled
          && s.slot[2].mix == .4f && s.slot[6].kind == MA_RACK_EMPTY && s.slot[5].kind == MA_RACK_EQ,
          "an authored chorus takes the session's values");
    /* Not enabled: left alone. */
    before = s;
    CHECK(ma_rack_spec_set_effects(&s, (ma_chorus_controls){ false, .8f, 1, 1 }, (ma_reverb_controls){ false, .8f, 1, .5f })
          && !memcmp(&s, &before, sizeof s), "a stage that is not enabled changed the spec");
    CHECK(ma_rack_spec_set_effects(&s, (ma_chorus_controls){ true, .8f, 1, 1 }, (ma_reverb_controls){ true, .5f, 1, .5f })
          && s.slot[2].mix == .8f && s.slot[6].kind == MA_RACK_REVERB && s.slot[6].mix == .5f && ma_rack_spec_valid(&s),
          "both stages at once");

    /* The end taken, a gap before it: the blocks close up, order kept. */
    s = (ma_rack_spec){ 0 };
    ma_rack_slot_init(&s.slot[0], MA_RACK_EQ, 1);
    ma_rack_slot_init(&s.slot[3], MA_RACK_UTILITY, 2);
    ma_rack_slot_init(&s.slot[7], MA_RACK_SATURATOR, 3);
    CHECK(ma_rack_spec_set_reverb(&s, .2f, .65f, .55f) && s.slot[0].id == 1 && s.slot[1].id == 2 && s.slot[2].id == 3
          && s.slot[3].kind == MA_RACK_REVERB && s.slot[4].kind == MA_RACK_EMPTY && s.slot[7].kind == MA_RACK_EMPTY
          && ma_rack_spec_valid(&s), "a full tail did not close up");
    /* Eight blocks: no room, nothing changed. */
    s = (ma_rack_spec){ 0 };
    for (unsigned i = 0; i < MA_RACK_SLOTS; i++) ma_rack_slot_init(&s.slot[i], MA_RACK_UTILITY, i + 1);
    before = s;
    CHECK(!ma_rack_spec_set_chorus(&s, .3f, .35f, .25f) && !memcmp(&s, &before, sizeof s), "a full rack took a chorus");
    CHECK(ma_rack_spec_set_chorus(&s, 0, .35f, .25f) && !memcmp(&s, &before, sizeof s), "mix 0 on a full rack failed");
}

/* --- RK2: chorus and reverb blocks against the MA3-2 kernel ---------------------- */

/* "Bit-identical at equal position": the two blocks, in place, against the
 * standalone ma_effects pair on the same input, at every rate. The input
 * stays inside the kernel's +-1 bound; past it the two differ on purpose
 * (the rack's blocks sit before master and bound at MA_RACK_FX_CEILING). */
static void test_kernel_parity(void) {
    static const float rates[] = { 44100, 48000, 96000, 192000 };
    static const struct { float cm, cd, cr, rm, rs, rd; } set[] = {
        { .3f, .35f, .25f, .25f, .65f, .55f }, { 1, 1, 20, 1, 1, 0 }, { .5f, 0, .03f, .7f, 0, .99f },
    };
    static ma_effects fx;
    size_t total = 0, mismatches = 0;
    for (unsigned ri = 0; ri < 4; ri++)
        for (unsigned k = 0; k < sizeof set / sizeof *set; k++) {
            ma_effects_init(&fx, rates[ri]);
            ma_effects_set_chorus(&fx, (ma_chorus_controls){ true, set[k].cm, set[k].cd, set[k].cr });
            ma_effects_set_reverb(&fx, (ma_reverb_controls){ true, set[k].rm, set[k].rs, set[k].rd });
            ma_rack_spec spec = { 0 };
            CHECK(ma_rack_spec_set_chorus(&spec, set[k].cm, set[k].cd, set[k].cr)
                  && ma_rack_spec_set_reverb(&spec, set[k].rm, set[k].rs, set[k].rd), "kernel parity spec");
            ma_rack_init(&rack, rates[ri]);
            (void)ma_rack_apply(&rack, &spec);
            ma_rack_settle(&rack);
            uint64_t seed = 0x4b45524eu + k;
            size_t n = (size_t)rates[ri] * 2;
            for (size_t i = 0; i < n; i++) {
                bool burst = i % 9000 < 3000;
                ma_frame x = burst ? (ma_frame){ 1.6f * urand(&seed) - .8f, 1.6f * urand(&seed) - .8f } : (ma_frame){ 0 };
                ma_frame a = ma_effects_tick(&fx, x), b = ma_rack_tick(&rack, x);
                mismatches += memcmp(&a, &b, sizeof a) != 0;
            }
            total += n;
            CHECK(fx.meter.clips == rack.fx.clips && fx.meter.sanitizations == rack.fx.sanitizations
                  && fx.meter.pre_peak == rack.fx.pre_peak && fx.meter.post_peak == rack.fx.post_peak,
                  "rate %.0f set %u: the meters differ", rates[ri], k);
            CHECK(fx.meter.clips == 0, "rate %.0f set %u: the parity input reached the bound", rates[ri], k);
        }
    /* Past +-1: ma_effects clips at its ceiling, the rack's blocks do not. */
    ma_rack_spec loud = { 0 };
    (void)ma_rack_spec_set_chorus(&loud, .3f, .35f, .25f);
    (void)ma_rack_spec_set_reverb(&loud, .25f, .65f, .55f);
    ma_effects_init(&fx, 48000);
    ma_effects_set_chorus(&fx, (ma_chorus_controls){ true, .3f, .35f, .25f });
    ma_effects_set_reverb(&fx, (ma_reverb_controls){ true, .25f, .65f, .55f });
    ma_rack_init(&rack, 48000);
    (void)ma_rack_apply(&rack, &loud);
    ma_rack_settle(&rack);
    float peak = 0;
    for (size_t i = 0; i < 48000; i++) {
        float v = 6 * sinf((float)i * .05f);
        ma_frame y = ma_rack_tick(&rack, (ma_frame){ v, -v });
        (void)ma_effects_tick(&fx, (ma_frame){ v, -v });
        peak = fmaxf(peak, fmaxf(fabsf(y.left), fabsf(y.right)));
    }
    CHECK(fx.meter.clips > 0 && rack.fx.clips == 0 && peak > 4 && peak < MA_RACK_FX_CEILING,
          "at +15 dBFS: ma_effects %llu clips, rack %llu clips, rack peak %g", (unsigned long long)fx.meter.clips,
          (unsigned long long)rack.fx.clips, (double)peak);
    printf("rack chorus and reverb blocks against ma_effects: %zu frames, %zu mismatches\n", total, mismatches);
    CHECK(!mismatches, "the blocks differ from the kernel in %zu frames", mismatches);
}

/* --- RK2: bypass ---------------------------------------------------------------- */

/* Mix zero, disabled, and the neutral settings of the saturator and the
 * compressor: the frame's bits, -0 included, through tick and process. */
static void test_rk2_bypass(void) {
    ma_rack_spec spec = { 0 };
    ma_rack_slot_init(&spec.slot[0], MA_RACK_CHORUS, 1);
    spec.slot[0].mix = 0;
    ma_rack_slot_init(&spec.slot[1], MA_RACK_REVERB, 2);
    spec.slot[1].enabled = false;
    ma_rack_slot_init(&spec.slot[2], MA_RACK_DELAY, 3);
    spec.slot[2].mix = 0;
    ma_rack_slot_init(&spec.slot[3], MA_RACK_SATURATOR, 4);
    spec.slot[3].param[MA_SATURATOR_DRIVE] = 0;
    spec.slot[3].param[MA_SATURATOR_SHAPE] = .5f;
    spec.slot[3].param[MA_SATURATOR_INPUT_DB] = 40;
    ma_rack_slot_init(&spec.slot[4], MA_RACK_COMPRESSOR, 5);
    spec.slot[4].param[MA_COMPRESSOR_RATIO] = 1;
    ma_rack_slot_init(&spec.slot[5], MA_RACK_COMPRESSOR, 6);   /* far below its threshold */
    spec.slot[5].param[MA_COMPRESSOR_THRESHOLD_DB] = 0;
    spec.slot[5].param[MA_COMPRESSOR_KNEE_DB] = 0;
    ma_rack_slot_init(&spec.slot[6], MA_RACK_DELAY, 7);
    spec.slot[6].enabled = false;
    CHECK(ma_rack_spec_valid(&spec), "RK2 bypass spec invalid");
    static ma_frame x[4096], y[4096];
    uint64_t seed = 0x42595032u;
    size_t differ = 0;
    for (int pass = 0; pass < 2; pass++) {
        ma_rack_init(&rack, 48000);
        (void)ma_rack_apply(&rack, &spec);           /* not settled: the fades run too */
        for (int round = 0; round < 12; round++) {
            for (size_t i = 0; i < 4096; i++) {
                x[i] = (ma_frame){ .5f * urand(&seed) - .25f, .5f * urand(&seed) - .25f };
                if (i % 97 == 0) x[i] = (ma_frame){ -0.0f, 0.0f };
                y[i] = x[i];
            }
            if (pass) ma_rack_process(&rack, y, nullptr, 4096, ma_lanes_supported());
            else for (size_t i = 0; i < 4096; i++) y[i] = ma_rack_tick(&rack, x[i]);
            for (size_t i = 0; i < 4096; i++) differ += memcmp(&x[i], &y[i], sizeof x[i]) != 0;
        }
        CHECK(rack.chorus.written == 0 && rack.reverb.written == 0 && !rack.block[2].delay.primed
              && !rack.block[3].saturator.primed && !rack.block[4].compressor.primed,
              "a bypassed block ran its DSP");
    }
    CHECK(!differ, "bypassed RK2 blocks changed %zu frames", differ);
}

/* --- RK2: delay ------------------------------------------------------------------- */

static ma_rack_spec delay_spec(float seconds, float feedback, bool pingpong) {
    ma_rack_spec spec = { 0 };
    ma_rack_slot_init(&spec.slot[0], MA_RACK_DELAY, 1);
    spec.slot[0].mix = 1;                                    /* the echoes alone */
    spec.slot[0].param[MA_DELAY_TIME_S] = seconds;
    spec.slot[0].param[MA_DELAY_FEEDBACK] = feedback;
    spec.slot[0].param[MA_DELAY_PINGPONG] = pingpong;
    spec.slot[0].param[MA_DELAY_LOWCUT_HZ] = 20;
    spec.slot[0].param[MA_DELAY_HIGHCUT_HZ] = 20000;
    return spec;
}

static void start(const ma_rack_spec *spec, float rate) {
    ma_rack_init(&rack, rate);
    (void)ma_rack_apply(&rack, spec);
    ma_rack_settle(&rack);
}

static void test_delay(void) {
    /* Stereo: an impulse comes back after exactly time x rate frames, whole
     * and unfiltered; each later echo has passed the feedback and its cuts. */
    ma_rack_spec spec = delay_spec(.25f, .5f, false);
    start(&spec, 48000);
    static ma_frame out[60000];
    for (size_t i = 0; i < 60000; i++) out[i] = ma_rack_tick(&rack, i ? (ma_frame){ 0 } : (ma_frame){ 1, -.5f });
    size_t early = 0;
    for (size_t i = 0; i < 12000; i++) early += out[i].left != 0 || out[i].right != 0;
    CHECK(!early, "delay: %zu nonzero frames before the first echo", early);
    CHECK(out[12000].left == 1 && out[12000].right == -.5f, "delay: first echo %g %g at 12000", out[12000].left,
          out[12000].right);
    size_t between = 0;
    for (size_t i = 12001; i < 24000; i++) between += out[i].left != 0 || out[i].right != 0;
    CHECK(!between, "delay: %zu nonzero frames between the first and the second echo", between);
    double energy[4] = { 0 };
    for (int k = 0; k < 4; k++)
        for (size_t i = 0; i < 12000; i++) energy[k] += (double)out[12000 * (size_t)(k + 1) + i].left * out[12000 * (size_t)(k + 1) + i].left;
    CHECK(out[24000].left > .3f && out[24000].left < .5f, "delay: second echo starts at %g", out[24000].left);
    CHECK(energy[1] < .25 * energy[0] && energy[1] > .1 * energy[0] && energy[2] < .25 * energy[1]
          && energy[3] < .25 * energy[2] && energy[3] > 0,
          "delay: echo energies %g %g %g %g do not fall by the feedback", energy[0], energy[1], energy[2], energy[3]);

    /* Ping-pong: the mono sum on the left, then right, then left. */
    spec = delay_spec(.25f, .5f, true);
    start(&spec, 48000);
    for (size_t i = 0; i < 60000; i++) out[i] = ma_rack_tick(&rack, i ? (ma_frame){ 0 } : (ma_frame){ 1, 0 });
    size_t wrong = 0;
    for (size_t i = 0; i < 36000; i++) {
        if (i == 12000) continue;
        wrong += out[i].left != 0;
        if (i < 24000) wrong += out[i].right != 0;
    }
    CHECK(out[12000].left == .5f && out[12000].right == 0 && !wrong && out[24000].right > .15f
          && out[24000].right < .25f && out[36000].left > .03f && out[36000].left < .125f,
          "ping-pong: L %g, R %g, L %g, %zu stray frames", out[12000].left, out[24000].right, out[36000].left, wrong);

    /* Sync: a dotted eighth at 120 is 18000 frames at 48 kHz; at 90, after
     * the read distance has followed, 24000. Free time is then ignored. */
    spec = delay_spec(.01f, 0, false);
    spec.slot[0].param[MA_DELAY_SYNC] = MA_DELAY_SYNC_8D;
    start(&spec, 48000);
    size_t first = 0;
    for (size_t i = 0; i < 30000 && !first; i++)
        if (ma_rack_tick(&rack, i ? (ma_frame){ 0 } : (ma_frame){ 1, 1 }).left != 0) first = i;
    CHECK(first == 18000, "synced delay: echo at %zu, want 18000", first);
    ma_rack_set_tempo(&rack, 90);
    for (size_t i = 0; i < 96000; i++) (void)ma_rack_tick(&rack, (ma_frame){ 0 });
    CHECK(rack.block[0].delay.frames == 24000, "after a tempo change the read distance is %g, want 24000",
          (double)rack.block[0].delay.frames);
    first = 0;
    for (size_t i = 0; i < 30000 && !first; i++)
        if (ma_rack_tick(&rack, i ? (ma_frame){ 0 } : (ma_frame){ 1, 1 }).left != 0) first = i;
    CHECK(first == 24000, "synced delay at 90: echo at %zu, want 24000", first);

    /* A time the line cannot hold clamps to the line: a whole note at 20. */
    spec.slot[0].param[MA_DELAY_SYNC] = MA_DELAY_SYNC_1;
    ma_rack_init(&rack, 48000);
    ma_rack_set_tempo(&rack, 20);
    (void)ma_rack_apply(&rack, &spec);
    ma_rack_settle(&rack);
    first = 0;
    for (size_t i = 0; i < MA_DELAY_CAPACITY + 10 && !first; i++)
        if (ma_rack_tick(&rack, i ? (ma_frame){ 0 } : (ma_frame){ 1, 1 }).left != 0) first = i;
    CHECK(first == MA_DELAY_CAPACITY - 2, "clamped delay: echo at %zu, want %d", first, MA_DELAY_CAPACITY - 2);

    /* A bypass empties the line without clearing it (RKD7): after mix 0
     * and back, silence in gives exact silence out. */
    spec = delay_spec(.05f, .9f, false);
    start(&spec, 48000);
    uint64_t seed = 0x44454c59u;
    for (size_t i = 0; i < 48000; i++) (void)ma_rack_tick(&rack, (ma_frame){ urand(&seed) - .5f, urand(&seed) - .5f });
    spec.slot[0].mix = 0;
    (void)ma_rack_apply(&rack, &spec);
    size_t changed = 0;
    for (size_t i = 0; i < 2000; i++) {
        ma_frame x = { urand(&seed) - .5f, urand(&seed) - .5f }, y = ma_rack_tick(&rack, x);
        if (i > 400) changed += memcmp(&x, &y, sizeof x) != 0;   /* past the 6 ms mix ramp */
    }
    spec.slot[0].mix = 1;
    (void)ma_rack_apply(&rack, &spec);
    size_t leaked = 0;
    for (size_t i = 0; i < 30000; i++) {
        ma_frame y = ma_rack_tick(&rack, (ma_frame){ 0 });
        leaked += y.left != 0 || y.right != 0;
    }
    CHECK(!changed && !leaked, "delay restart: %zu frames changed at mix 0, %zu frames of old echoes", changed, leaked);

    /* Two delays hold two lines: .1 s into .2 s, both echoes only. */
    spec = delay_spec(.1f, 0, false);
    ma_rack_slot_init(&spec.slot[1], MA_RACK_DELAY, 2);
    spec.slot[1].mix = 1;
    spec.slot[1].param[MA_DELAY_TIME_S] = .2f;
    spec.slot[1].param[MA_DELAY_FEEDBACK] = 0;
    start(&spec, 48000);
    first = 0;
    float value = 0;
    for (size_t i = 0; i < 30000 && !first; i++) {
        value = ma_rack_tick(&rack, i ? (ma_frame){ 0 } : (ma_frame){ 1, 1 }).left;
        if (value != 0) first = i;
    }
    CHECK(first == 14400 && value == 1 && rack.block[0].delay.unit != rack.block[1].delay.unit,
          "two delays: echo %g at %zu, lines %u and %u", value, first, rack.block[0].delay.unit, rack.block[1].delay.unit);
    /* Removing the first keeps the second on its line, with its echoes. */
    for (size_t i = 0; i < 100; i++) (void)ma_rack_tick(&rack, (ma_frame){ .25f, .25f });
    unsigned line = rack.block[1].delay.unit;
    ma_rack_spec second = { 0 };
    second.slot[0] = spec.slot[1];
    (void)ma_rack_apply(&rack, &second);
    for (size_t i = 0; i < 2000; i++) (void)ma_rack_tick(&rack, (ma_frame){ 0 });
    CHECK(rack.block[0].kind == MA_RACK_DELAY && rack.block[0].id == 2 && rack.block[0].delay.unit == line
          && rack.block[0].delay.primed && rack.block[0].delay.written > 2000, "the kept delay lost its line");

    /* A time change is followed, not jumped to, and arrives. */
    spec = delay_spec(.1f, .3f, false);
    start(&spec, 48000);
    for (size_t i = 0; i < 4800; i++) (void)ma_rack_tick(&rack, (ma_frame){ .1f, .1f });
    spec.slot[0].param[MA_DELAY_TIME_S] = .2f;
    (void)ma_rack_apply(&rack, &spec);
    (void)ma_rack_tick(&rack, (ma_frame){ .1f, .1f });
    float soon = rack.block[0].delay.frames;
    for (size_t i = 0; i < 96000; i++) (void)ma_rack_tick(&rack, (ma_frame){ .1f, .1f });
    CHECK(soon > 4800 && soon < 4810 && rack.block[0].delay.frames == 9600, "time change: %g after one frame, %g at rest",
          (double)soon, (double)rack.block[0].delay.frames);
}

/* --- RK2: saturator --------------------------------------------------------------- */

/* Amplitude of harmonic h of a settled sine through the rack (left). */
static double harmonic(const ma_rack_spec *spec, double f, double amp, int h, double fs) {
    start(spec, (float)fs);
    size_t settle = (size_t)(fs * .5), cycles = (size_t)ceil(f * .5);
    size_t n = (size_t)llround((double)cycles * fs / f);
    double ws = 0, wc = 0, w = 2 * M_PI * f / fs;
    for (size_t i = 0; i < settle + n; i++) {
        float x = (float)(amp * sin(w * (double)i));
        ma_frame y = ma_rack_tick(&rack, (ma_frame){ x, x });
        if (i >= settle) {
            ws += y.left * sin(h * w * (double)i);
            wc += y.left * cos(h * w * (double)i);
        }
    }
    return 2 * hypot(ws, wc) / (double)n;
}

static void test_saturator(void) {
    ma_rack_spec spec = { 0 };
    ma_rack_slot_init(&spec.slot[0], MA_RACK_SATURATOR, 1);
    ma_rack_slot_spec *s = &spec.slot[0];
    /* Unit small-signal gain at full drive, both curves. */
    for (int shape = 0; shape < 2; shape++) {
        s->param[MA_SATURATOR_DRIVE] = 1;
        s->param[MA_SATURATOR_SHAPE] = (float)shape;
        double amp = pow(10, -90 / 20.0), g = 20 * log10(harmonic(&spec, 440, amp, 1, 48000) / amp);
        CHECK(fabs(g) < .05, "saturator shape %d: small-signal gain %.3f dB", shape, g);
    }
    /* Harmonics of a 220 Hz sine at the rack's measured levels (a chord's
     * peaks near -20 dBFS, a note's near -28), at the initial input gain. */
    printf("rack saturator, 220 Hz sine, input gain %+.0f dB: level, drive, shape -> H1 change, H2, H3 (dB re H1)\n",
           (double)s->param[MA_SATURATOR_INPUT_DB]);
    double h2_triode = 0, h3_triode = 0, h2_odd = 0, h3_odd = 0, thd_low = 0, thd_high = 0;
    for (int level = -28; level <= -12; level += 8)
        for (int shape = 0; shape < 2; shape++)
            for (int d = 0; d < 3; d++) {
                static const float drives[] = { .25f, .5f, 1 };
                s->param[MA_SATURATOR_DRIVE] = drives[d];
                s->param[MA_SATURATOR_SHAPE] = (float)shape;
                double amp = pow(10, level / 20.0);
                double h1 = harmonic(&spec, 220, amp, 1, 48000), h2 = harmonic(&spec, 220, amp, 2, 48000),
                       h3 = harmonic(&spec, 220, amp, 3, 48000);
                printf("  %4d dBFS  drive %.2f  %-6s  %+6.2f  %7.1f  %7.1f\n", level, (double)drives[d],
                       shape ? "odd" : "triode", 20 * log10(h1 / amp), 20 * log10(h2 / h1 + 1e-12), 20 * log10(h3 / h1 + 1e-12));
                if (level == -20 && d == 1) {
                    if (shape) { h2_odd = h2 / h1; h3_odd = h3 / h1; }
                    else { h2_triode = h2 / h1; h3_triode = h3 / h1; }
                }
                if (level == -20 && !shape && d == 0) thd_low = hypot(h2, h3) / h1;
                if (level == -20 && !shape && d == 2) thd_high = hypot(h2, h3) / h1;
            }
    CHECK(h2_triode > 2 * h3_triode, "the triode curve is not led by its second harmonic (%g, %g)", h2_triode, h3_triode);
    /* The odd kernel rides its bias follower, so it has a second harmonic
     * too; what sets it apart is far more third. */
    CHECK(h3_odd > 4 * h3_triode && h2_odd > 0, "the odd kernel does not add third harmonic (%g against %g)", h3_odd,
          h3_triode);
    CHECK(thd_high > 2 * thd_low && thd_low > 0, "more drive did not give more harmonics (%g, %g)", thd_low, thd_high);

    /* A tilt of t dB: -t/2 far below the pivot, +t/2 far above, 0 at it;
     * the opposite tilt after the (idle) stage undoes it. */
    s->param[MA_SATURATOR_DRIVE] = 0;
    s->param[MA_SATURATOR_SHAPE] = 0;
    s->param[MA_SATURATOR_TILT_PRE_DB] = 8;
    double lo = 20 * log10(harmonic(&spec, 20, .1, 1, 48000) / .1), mid = 20 * log10(harmonic(&spec, 650, .1, 1, 48000) / .1),
           hi = 20 * log10(harmonic(&spec, 20000, .1, 1, 48000) / .1);
    CHECK(fabs(lo + 4) < .1 && fabs(mid) < .02 && fabs(hi - 4) < .1, "tilt +8 dB: %.3f at 20 Hz, %.3f at the pivot, %.3f at 20 kHz",
          lo, mid, hi);
    s->param[MA_SATURATOR_TILT_POST_DB] = -8;
    double worst = 0;
    static const double freqs[] = { 30, 200, 650, 2000, 9000, 18000 };
    for (unsigned i = 0; i < 6; i++) worst = fmax(worst, fabs(20 * log10(harmonic(&spec, freqs[i], .1, 1, 48000) / .1)));
    CHECK(worst < .001, "opposite tilts leave %.5f dB", worst);

    /* Left alone stays left alone: the channels share nothing. */
    s->param[MA_SATURATOR_DRIVE] = 1;
    s->param[MA_SATURATOR_TILT_PRE_DB] = 3;
    start(&spec, 48000);
    size_t leak = 0;
    for (size_t i = 0; i < 20000; i++) leak += ma_rack_tick(&rack, (ma_frame){ .3f * sinf((float)i * .03f), 0 }).right != 0;
    CHECK(!leak, "the saturator leaked into the silent channel in %zu frames", leak);
    /* Bounded on any input. */
    uint64_t seed = 0x53415455u;
    double peak = 0;
    for (size_t i = 0; i < 50000; i++) {
        ma_frame y = ma_rack_tick(&rack, (ma_frame){ 200 * urand(&seed) - 100, 200 * urand(&seed) - 100 });
        peak = fmax(peak, fmax(fabs(y.left), fabs(y.right)));
    }
    CHECK(isfinite(peak) && peak < 1000, "saturator peak %g on +40 dBFS noise", peak);
}

/* --- RK2: compressor ---------------------------------------------------------------- */

/* The static curve in dB (Giannoulis, Massberg and Reiss): gain change for a
 * level, threshold, ratio and knee width. */
static double curve_db(double level, double threshold, double ratio, double knee) {
    double over = level - threshold, slope = 1 / ratio - 1;
    if (2 * over <= -knee) return 0;
    if (2 * over < knee) return slope * (over + knee / 2) * (over + knee / 2) / (2 * knee);
    return slope * over;
}

static void test_compressor(void) {
    ma_rack_spec spec = { 0 };
    ma_rack_slot_init(&spec.slot[0], MA_RACK_COMPRESSOR, 1);
    ma_rack_slot_spec *s = &spec.slot[0];
    s->param[MA_COMPRESSOR_DETECTOR] = MA_COMPRESSOR_RMS;
    s->param[MA_COMPRESSOR_THRESHOLD_DB] = -30;
    s->param[MA_COMPRESSOR_RATIO] = 4;
    s->param[MA_COMPRESSOR_ATTACK_MS] = 5;
    s->param[MA_COMPRESSOR_RELEASE_MS] = 500;
    /* Steady sines against the static curve; an RMS detector reads a sine
     * 3.01 dB under its peak. */
    double worst = 0;
    for (int knee = 0; knee <= 12; knee += 12) {
        s->param[MA_COMPRESSOR_KNEE_DB] = (float)knee;
        for (double level = -50; level <= 0; level += 5) {
            double amp = pow(10, level / 20), got = 20 * log10(harmonic(&spec, 1000, amp, 1, 48000) / amp);
            double want = curve_db(level - 3.0103, -30, 4, knee), err = fabs(got - want);
            worst = fmax(worst, err);
            CHECK(err < .15, "compressor knee %d, sine at %.0f dBFS: gain %.3f dB, curve %.3f dB", knee, level, got, want);
        }
    }
    printf("rack compressor: worst deviation from the static curve %.4f dB (RMS detector, knees 0 and 12 dB)\n", worst);

    /* Under the threshold (and its knee) the frames pass untouched. */
    s->param[MA_COMPRESSOR_KNEE_DB] = 6;
    start(&spec, 48000);
    size_t changed = 0;
    for (size_t i = 0; i < 48000; i++) {
        float v = .01f * sinf((float)i * .13f);
        ma_frame x = { v, -.5f * v }, y = ma_rack_tick(&rack, x);
        changed += memcmp(&x, &y, sizeof x) != 0;
    }
    CHECK(!changed, "a compressor under its threshold changed %zu frames", changed);

    /* Timing: a 1 kHz sine steps from -40 to -10 dBFS and back. The gain
     * change covers 63 % of its way in about the attack (plus the RMS
     * window), and falls to 37 % in about the release. */
    s->param[MA_COMPRESSOR_KNEE_DB] = 0;
    s->param[MA_COMPRESSOR_ATTACK_MS] = 10;
    s->param[MA_COMPRESSOR_RELEASE_MS] = 150;
    start(&spec, 48000);
    double final_db = curve_db(-13.0103, -30, 4, 0), w = 2 * M_PI * 1000 / 48000;
    double attack_ms = 0, release_ms = 0;
    for (size_t i = 0; i < 24000; i++) (void)ma_rack_tick(&rack, (ma_frame){ (float)(.01 * sin(w * (double)i)), 0 });
    for (size_t i = 0; i < 48000; i++) {
        float v = (float)(pow(10, -10 / 20.0) * sin(w * (double)i));
        (void)ma_rack_tick(&rack, (ma_frame){ v, v });
        if (!attack_ms && rack.block[0].compressor.reduction_db <= .632 * final_db) attack_ms = (double)i / 48;
    }
    double held = rack.block[0].compressor.reduction_db;
    for (size_t i = 0; i < 48000; i++) {
        float v = (float)(.01 * sin(w * (double)i));
        (void)ma_rack_tick(&rack, (ma_frame){ v, v });
        if (!release_ms && rack.block[0].compressor.reduction_db >= .368 * held) release_ms = (double)i / 48;
    }
    printf("rack compressor timing: attack 10 ms -> 63 %% in %.1f ms, release 150 ms -> 37 %% in %.1f ms, "
           "held %.2f dB (curve %.2f)\n", attack_ms, release_ms, held, final_db);
    CHECK(fabs(held - final_db) < .1, "held gain change %.3f dB, curve %.3f", held, final_db);
    CHECK(attack_ms > 8 && attack_ms < 30, "attack took %.1f ms", attack_ms);
    CHECK(release_ms > 120 && release_ms < 220, "release took %.1f ms", release_ms);
    for (size_t i = 0; i < 96000; i++) (void)ma_rack_tick(&rack, (ma_frame){ (float)(.01 * sin(w * (double)i)), 0 });
    CHECK(rack.block[0].compressor.reduction_db == 0, "the gain change did not return to exactly 0 dB (%g)",
          (double)rack.block[0].compressor.reduction_db);

    /* Peak detector as a limiter: ratio 20 holds a -6 dBFS sine near the threshold. */
    s->param[MA_COMPRESSOR_DETECTOR] = MA_COMPRESSOR_PEAK;
    s->param[MA_COMPRESSOR_THRESHOLD_DB] = -20;
    s->param[MA_COMPRESSOR_RATIO] = 20;
    s->param[MA_COMPRESSOR_ATTACK_MS] = 1;
    s->param[MA_COMPRESSOR_RELEASE_MS] = 200;
    double out = 20 * log10(harmonic(&spec, 1000, .5, 1, 48000));
    CHECK(out > -20.5 && out < -17, "peak limiter: a -6 dBFS sine comes out at %.2f dBFS", out);

    /* Makeup alone is a gain; both channels share one gain. */
    s->param[MA_COMPRESSOR_RATIO] = 1;
    s->param[MA_COMPRESSOR_MAKEUP_DB] = 6;
    start(&spec, 48000);
    ma_frame y = ma_rack_tick(&rack, (ma_frame){ .25f, -.125f });
    CHECK(fabs(y.left / .25 - pow(10, 6 / 20.0)) < 1e-5 && y.right == -.5f * y.left, "makeup +6 dB: %g %g", y.left, y.right);
    /* Bounded and finite on anything. */
    s->param[MA_COMPRESSOR_RATIO] = 20;
    s->param[MA_COMPRESSOR_MAKEUP_DB] = 24;
    start(&spec, 48000);
    uint64_t seed = 0x434f4d50u;
    bool finite_all = true;
    for (size_t i = 0; i < 50000; i++) {
        float big = i % 500 == 0 ? 1e20f : 1;
        ma_frame z = ma_rack_tick(&rack, (ma_frame){ big * (urand(&seed) - .5f), big * (urand(&seed) - .5f) });
        finite_all &= isfinite(z.left) && isfinite(z.right);
    }
    CHECK(finite_all && rack.sanitizations == 0, "compressor on hostile input: finite %d, %llu sanitizations", finite_all,
          (unsigned long long)rack.sanitizations);
}

/* RK2 through the pool: the chain sits before master and safety (RKD9), the
 * block render equals the per-frame tick, and both engines agree. */
static void test_pool_rk2(void) {
    ma_rack_spec spec = { 0 };
    ma_rack_slot_init(&spec.slot[0], MA_RACK_SATURATOR, 1);
    ma_rack_slot_init(&spec.slot[1], MA_RACK_COMPRESSOR, 2);
    ma_rack_slot_init(&spec.slot[2], MA_RACK_DELAY, 3);
    spec.slot[2].param[MA_DELAY_SYNC] = MA_DELAY_SYNC_8;
    spec.slot[2].param[MA_DELAY_PINGPONG] = 1;
    ma_rack_slot_init(&spec.slot[3], MA_RACK_CHORUS, 4);
    ma_rack_slot_init(&spec.slot[4], MA_RACK_REVERB, 5);
    CHECK(ma_rack_spec_valid(&spec), "pool RK2 spec invalid");
    uint64_t dry = render_pool(&ma_patch_tepih, nullptr, nullptr, false);
    uint64_t a = render_pool(&ma_patch_tepih, &rack, &spec, false);
    float peak = 0;
    for (size_t i = 0; i < POOL_FRAMES; i++) peak = fmaxf(peak, fmaxf(fabsf(frames[i].left), fabsf(frames[i].right)));
    uint64_t b = render_pool(&ma_patch_tepih, &rack, &spec, false);
    per_frame = true;
    uint64_t ticked = render_pool(&ma_patch_tepih, &rack, &spec, false);
    per_frame = false;
    CHECK(a && a != dry && a == b && a == ticked, "pool with the RK2 chain: %016llx %016llx, per frame %016llx, dry %016llx",
          (unsigned long long)a, (unsigned long long)b, (unsigned long long)ticked, (unsigned long long)dry);
    CHECK(peak < 1 && rack.sanitizations == 0 && rack.fx.sanitizations == 0, "pool with the RK2 chain: peak %g", peak);
    printf("rack pool FNV (tepih, saturator-compressor-delay-chorus-reverb): %016llx, peak %.3f, chorus/reverb clips %llu\n",
           (unsigned long long)a, peak, (unsigned long long)rack.fx.clips);
}


/* RK1 section 3: the level the rack receives (after the DC block, before
 * master) for each factory patch, as a reference for RK2's nonlinear
 * blocks. An empty rack is attached; the pool output divided by master is
 * the rack input wherever the safety knee (.98) is not touched. A single
 * note (C3, velocity 100) and a four-note chord (C3 E3 G3 C4), 2 s held. */
static void report_levels(void) {
    static const struct { const char *name; const ma_patch *patch; } P[] = {
        { "Tepih", &ma_patch_tepih }, { "Lead", &ma_patch_lead }, { "Dubina", &ma_patch_dubina },
        { "Raster", &ma_patch_raster }, { "Prizma", &ma_patch_prizma }, { "Granica", &ma_patch_granica },
    };
    printf("rack input level (dBFS, pre-master): patch, master, note peak/rms, chord peak/rms\n");
    for (unsigned i = 0; i < sizeof P / sizeof *P; i++) {
        double peak[2] = { 0 }, sum[2] = { 0 };
        for (int chord = 0; chord < 2; chord++) {
            ma_voice_pool_config config = ma_voice_pool_default_config;
            config.limit = 8;
            if (!ma_voice_pool_init(&pool, 48000, &config, P[i].patch)) return;
            ma_rack_init(&rack, 48000);
            ma_voice_pool_attach_rack(&pool, &rack);
            static const uint8_t notes[] = { 48, 52, 55, 60 };
            for (unsigned k = 0; k < (chord ? 4u : 1u); k++) (void)ma_voice_pool_note_on(&pool, 0, notes[k], 100);
            for (size_t at = 0; at < POOL_FRAMES * 2; at += BLOCK) {
                ma_voice_pool_render(&pool, frames, BLOCK);
                for (unsigned f = 0; f < BLOCK; f++) {
                    double l = frames[f].left / P[i].patch->master_level, r = frames[f].right / P[i].patch->master_level;
                    peak[chord] = fmax(peak[chord], fmax(fabs(l), fabs(r)));
                    sum[chord] += .5 * (l * l + r * r);
                }
            }
        }
        double n = POOL_FRAMES * 2;
        printf("  %-8s master %.2f  note %6.1f / %6.1f  chord %6.1f / %6.1f\n", P[i].name,
               (double)P[i].patch->master_level, 20 * log10(peak[0]), 10 * log10(sum[0] / n),
               20 * log10(peak[1]), 10 * log10(sum[1] / n));
    }
}

int main(void) {
    test_validation();
    test_session_blocks();
    test_utility();
    test_smoothing();
    test_eq_response();
    test_kernel_parity();
    test_rk2_bypass();
    test_delay();
    test_saturator();
    test_compressor();
    test_clicks();
    test_sweeps();
    test_block_parity();
    test_pool_bypass();
    test_pool_rk2();
    report_levels();
    printf("ma rack: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
