/* RK1/RK1b gates for the insert rack (docs/ma-rack-plan.md section 4):
 * exact bypass through the pool, the parametric EQ against the analytic
 * SVF for every band shape, structural and band changes without clicks,
 * extreme sweeps, determinism and spec validation. Hosted test: libm only
 * for the analytic referee. */
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
    if (wet) {
        ma_effects_set_chorus(&pool.effects, (ma_chorus_controls){ true, .3f, .35f, .25f });
        ma_effects_set_reverb(&pool.effects, (ma_reverb_controls){ true, .25f, .65f, .55f });
    }
    if (r) {
        ma_rack_init(r, 48000);
        if (spec) (void)ma_rack_apply(r, spec);
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

static void random_slot(ma_rack_slot_spec *slot, uint64_t *s, uint32_t id) {
    ma_rack_kind kind = urand(s) < .6f ? MA_RACK_EQ : MA_RACK_UTILITY;
    ma_rack_slot_init(slot, kind, id);
    slot->enabled = urand(s) < .9f;
    slot->mix = urand(s) < .2f ? urand(s) : 1;
    if (kind == MA_RACK_UTILITY) {
        for (unsigned p = 0; p < MA_UTILITY_PARAM_COUNT; p++) slot->param[p] = pick(s, kind, p);
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
                unsigned n = (unsigned)(urand(&seed) * 9);
                for (unsigned j = 0; j < n && j < MA_RACK_SLOTS; j++)
                    random_slot(&spec.slot[j], &seed, 1 + (uint32_t)(urand(&seed) * 3) * 8 + j);
                if (!ma_rack_apply(&rack, &spec)) valid = false;
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
            unsigned n = 1 + (unsigned)(urand(&seed) * 8);
            for (unsigned j = 0; j < n && j < MA_RACK_SLOTS; j++)
                random_slot(&spec.slot[j], &seed, 1 + (uint32_t)(urand(&seed) * 3) * 8 + j);
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
    test_utility();
    test_smoothing();
    test_eq_response();
    test_clicks();
    test_sweeps();
    test_block_parity();
    test_pool_bypass();
    report_levels();
    printf("ma rack: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
