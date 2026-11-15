/* The circuit-derived oscillator shape (docs/ma-vco-plan.md, V2 and V3).
 * - The front end's rise against B1 in SPICE (driver/spice/vco/b1_low.py).
 * - A skewed triangle has the two ramps' spectrum, peaks at +-1 and no
 *   offset, and stays centred while its rise changes note after note.
 * - The saw's bow is the parabola it claims.
 * - The two shapers: the sine's distortion and the saw's halves and steps.
 * - Zero is exact: no shape, no shaper levels, the old bits. The pinned
 *   engine and bounce signatures of `make test` carry that; here the front
 *   end leaves no departure at amount zero.
 * - The pool's vector render and its frame-by-frame tick give the same bits
 *   with every shape on.
 * Hosted test. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../src/ma_internal.h"
#include "../src/ma_voice_impl.h"

#define PI 3.14159265358979323846

static unsigned checks, failures;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

enum { RATE = 48000, SUB = 8 * RATE };

static double db(double x) { return 20.0 * log10(x); }

/* ---- the 8x source of one hand-built voice ----------------------------------------- */

static ma_synth bare(void) {
    ma_synth s = { .sample_rate_hz = RATE };
    s.oscillator1.guard_gain = s.oscillator1.guard_target = 1.0f;
    s.oscillator2.guard_gain = s.oscillator2.guard_target = 1.0f;
    return s;
}

/* VCO1 alone at `hz` for `seconds` at the 8x rate, after a second to settle;
 * `skew`, `swell` and `bow` held, the levels from `v`. */
static float source[SUB * 2];
static double end_phase, sub_step;     /* VCO1's phase after the render, and its step */
static float ramp_bow_up, ramp_bow_down;   /* B1's ramp bows for the next render (V2c) */
static float buffer_amount;                /* B1's buffer bend for the next render (V2d) */
static size_t render(ma_vco_controls v, float hz, float skew, float swell, float bow, double seconds) {
    static ma_synth s;
    s = bare();
    s.vco_skew[0] = skew;
    s.vco_swell[0] = swell;
    s.vco_bow = bow;
    s.vco_ramp_bow[0][0] = ramp_bow_up;
    s.vco_ramp_bow[0][1] = ramp_bow_down;
    s.vco_buffer = buffer_amount;
    ma_render_controls c = { 0 };
    c.vco1 = v;
    c.vco2.pulse_width = .5f;
    ma_source_frame f = ma_voice_prepare_source_frame_guarded(&s, &c, hz, 0.0f, 1.0f, 0.0f);
    size_t n = (size_t)(seconds * SUB);
    for (int k = 0; k < RATE; k++) {
        float out[8];
        ma_voice_render_source_block(&s, &c, &f, hz, 0.0f, 1.0f, 0.0f, out);
    }
    for (size_t i = 0; i < n; i += 8)
        ma_voice_render_source_block(&s, &c, &f, hz, 0.0f, 1.0f, 0.0f, source + i);
    end_phase = (double)s.oscillator1.phase_q48 * 0x1p-48;
    sub_step = (double)f.vco1_step_q48 * 0x1p-48;
    return n;
}

/* The phase sample i was rendered at. */
static double phase_at(size_t i, size_t n) {
    double p = end_phase - (double)(n - i) * sub_step;
    return p - floor(p);
}

/* The jump at phase `at` of a waveform made of straight pieces: lines fitted
 * over a tenth of a turn either side, starting 2 % away or past the edge
 * smoothing (eight steps) and half again, met at `at`. */
static double jump_at(size_t n, double at) {
    double near = fmax(.02, 1.5 * 8 * sub_step), far = near + .10;
    double sx[2] = { 0 }, sy[2] = { 0 }, sxx[2] = { 0 }, sxy[2] = { 0 }, m[2] = { 0 };
    for (size_t i = 0; i < n; i++) {
        double d = phase_at(i, n) - at;
        if (d > .5) d -= 1;
        if (d < -.5) d += 1;
        int side = d > 0;
        double a = fabs(d);
        if (a < near || a > far) continue;
        sx[side] += d; sy[side] += source[i]; sxx[side] += d * d; sxy[side] += d * source[i]; m[side]++;
    }
    double at0[2];
    for (int k = 0; k < 2; k++) {
        double slope = (m[k] * sxy[k] - sx[k] * sy[k]) / (m[k] * sxx[k] - sx[k] * sx[k]);
        at0[k] = (sy[k] - slope * sx[k]) / m[k];
    }
    return at0[1] - at0[0];
}

/* Harmonic k of x (Blackman-Harris projection at k f0), complex. */
static void harmonic(const float *x, size_t n, double f0, int k, double *re, double *im) {
    double sr = 0, si = 0, sw = 0;
    for (size_t i = 0; i < n; i++) {
        double u = (double)i / (double)n;
        double w = .35875 - .48829 * cos(2 * PI * u) + .14128 * cos(4 * PI * u) - .01168 * cos(6 * PI * u);
        double a = 2 * PI * k * f0 * (double)i / SUB;
        sr += w * x[i] * cos(a);
        si -= w * x[i] * sin(a);
        sw += w;
    }
    *re = 2 * sr / sw;
    *im = 2 * si / sw;
}
static double level(const float *x, size_t n, double f0, int k) {
    double re, im;
    harmonic(x, n, f0, k, &re, &im);
    return hypot(re, im);
}

static void extremes(const float *x, size_t n, float *low, float *high, double *mean) {
    *low = 2, *high = -2, *mean = 0;
    for (size_t i = 0; i < n; i++) {
        if (x[i] < *low) *low = x[i];
        if (x[i] > *high) *high = x[i];
        *mean += x[i];
    }
    *mean /= (double)n;
}

/* ---- the front end's rise against SPICE ---------------------------------------------- */

/* build/vco/b1low/low.json (2026-11-14): frequency and rise fraction of B1's
 * triangle at fourteen CVs. */
static const struct { double hz, rise; } B1[] = {
    { 27.387, .54461 }, { 33.590, .54036 }, { 40.067, .53709 }, { 50.162, .53333 }, { 67.675, .52885 },
    { 93.051, .52454 }, { 130.208, .52038 }, { 167.904, .51746 }, { 224.939, .51434 }, { 416.511, .50876 },
    { 800.555, .50443 }, { 1183.541, .50260 }, { 1564.697, .50159 }, { 1943.399, .50094 },
};

/* build/vco/b1low/bow.json (2026-11-14): each ramp's parabola height at
 * mid-ramp over half the swing, rise and fall. */
static const struct { double hz, up, down; } B1_BOW[] = {
    { 27.387, +0.01493, -0.02148 },
    { 33.590, +0.01301, -0.01977 },
    { 40.067, +0.01162, -0.01842 },
    { 50.162, +0.01013, -0.01682 },
    { 67.675, +0.00850, -0.01485 },
    { 93.051, +0.00710, -0.01288 },
    { 130.208, +0.00587, -0.01094 },
    { 167.904, +0.00507, -0.00954 },
    { 224.939, +0.00424, -0.00804 },
    { 416.511, +0.00280, -0.00533 },
    { 800.555, +0.00170, -0.00322 },
    { 1183.541, +0.00123, -0.00232 },
    { 1564.697, +0.00097, -0.00182 },
    { 1943.399, +0.00080, -0.00151 },
};

static double b1_bow(double hz, bool up) {
    unsigned n = sizeof B1_BOW / sizeof *B1_BOW, i = 0;
    while (i + 2 < n && B1_BOW[i + 1].hz < hz) i++;
    double t = log(hz / B1_BOW[i].hz) / log(B1_BOW[i + 1].hz / B1_BOW[i].hz);
    double a = up ? B1_BOW[i].up : -B1_BOW[i].down, b = up ? B1_BOW[i + 1].up : -B1_BOW[i + 1].down;
    double v = exp(log(a) + t * (log(b) - log(a)));
    return up ? v : -v;
}

/* B1's rise at a pitch, log-log between the simulated points. */
static double b1_skew(double hz) {
    unsigned n = sizeof B1 / sizeof *B1, i = 0;
    while (i + 2 < n && B1[i + 1].hz < hz) i++;
    double t = log(hz / B1[i].hz) / log(B1[i + 1].hz / B1[i].hz);
    return exp(log(B1[i].rise - .5) + t * (log(B1[i + 1].rise - .5) - log(B1[i].rise - .5)));
}

static ma_patch plain_patch(void) {
    ma_patch p = ma_patch_tepih;
    p.vco1 = (ma_vco_controls){ .triangle_level = 1, .pulse_width = .5f };
    p.vco2 = (ma_vco_controls){ .pulse_width = .5f };
    p.vco2_level = 0; p.vco2_interval = 0; p.vco2_fine_cents = 0;
    p.sync_amount = p.sync_softness = p.crossmod_amount = p.noise_level = 0;
    p.raster_mix = p.bcs_amount = p.mozaik_mix = p.formant_mix = p.grain_mix = 0;
    p.filter_env_amount = 0;
    return p;
}

static void test_front(void) {
    double worst = 0;
    static ma_synth s;
    for (int note = 29; note <= 94; note++) {      /* F1 .. A#6: inside the simulated range */
        ma_patch p = plain_patch();
        p.vco_analog = 1;
        ma_synth_init_patch(&s, RATE, &p);
        ma_synth_set_lfo(&s, 0, 5);
        ma_synth_note_on(&s, 0, (uint8_t)note, 100);
        (void)ma_synth_tick(&s);
        double hz = 440.0 * pow(2.0, (note - 69) / 12.0);
        double e = fabs((double)s.vco_skew[0] / b1_skew(hz) - 1);
        if (e > worst) worst = e;
    }
    printf("  rise against B1 in SPICE, F1..A#6: within %.1f %% of the asymmetry\n", 100 * worst);
    /* The quartic is within 3 % at the simulated pitches; between them this
     * compares with a log-log line through the two neighbours. */
    CHECK(worst < .06, "rise: %.3f of the asymmetry away from SPICE", worst);
    double worst_bow = 0;
    for (int note = 29; note <= 94; note++) {
        ma_patch p = plain_patch();
        p.vco_analog = 1;
        ma_synth_init_patch(&s, RATE, &p);
        ma_synth_set_lfo(&s, 0, 5);
        ma_synth_note_on(&s, 0, (uint8_t)note, 100);
        (void)ma_synth_tick(&s);
        double hz = 440.0 * pow(2.0, (note - 69) / 12.0);
        double e = fmax(fabs(s.vco_ramp_bow[0][0] / b1_bow(hz, true) - 1),
                        fabs(s.vco_ramp_bow[0][1] / b1_bow(hz, false) - 1));
        if (e > worst_bow) worst_bow = e;
    }
    printf("  ramp bows against B1 in SPICE, F1..A#6: within %.1f %%\n", 100 * worst_bow);
    CHECK(worst_bow < .04, "ramp bow: %.3f away from SPICE", worst_bow);

    /* Half the amount, half the departure; zero, none; the bottom is capped. */
    ma_patch p = plain_patch();
    p.vco_analog = .5f;
    ma_synth_init_patch(&s, RATE, &p);
    ma_synth_note_on(&s, 0, 45, 100);
    (void)ma_synth_tick(&s);
    double half = s.vco_skew[0];
    p.vco_analog = 1;
    ma_synth_init_patch(&s, RATE, &p);
    ma_synth_note_on(&s, 0, 45, 100);
    (void)ma_synth_tick(&s);
    CHECK(fabs(half / s.vco_skew[0] - .5) < 1e-6, "half amount: %g of the full skew", half / s.vco_skew[0]);
    CHECK(s.vco_bow > .012f && s.vco_bow < .0121f, "bow %g", (double)s.vco_bow);
    ma_synth_note_on(&s, 0, 0, 100);
    for (int i = 0; i < 64; i++) (void)ma_synth_tick(&s);
    CHECK(s.vco_skew[0] > .05f && s.vco_skew[0] <= .1f, "C-1: skew %g", (double)s.vco_skew[0]);
    p.vco_analog = 0;
    ma_synth_init_patch(&s, RATE, &p);
    ma_synth_note_on(&s, 0, 45, 100);
    (void)ma_synth_tick(&s);
    CHECK(s.vco_skew[0] == 0 && s.vco_skew[1] == 0 && s.vco_swell[0] == 0 && s.vco_bow == 0,
          "amount zero leaves shape departures");
}

/* ---- the skewed triangle -------------------------------------------------------------- */

static void test_triangle(void) {
    static const struct { float hz, skew; } C[] = {
        { 27.5f, .0446f }, { 110, .0200f }, { 440, .0080f }, { 1760, .0012f }, { 3520, .0040f },
    };
    double worst_h2 = 0, worst_peak = 0, worst_mean = 0;
    for (unsigned i = 0; i < sizeof C / sizeof *C; i++) {
        ma_vco_controls v = { .triangle_level = 1, .pulse_width = .5f };
        double seconds = C[i].hz < 100 ? 1.0 : .25;
        size_t n = render(v, C[i].hz, C[i].skew, 0, 0, seconds);
        double r = .5 + C[i].skew;
        /* Two straight ramps: |c_k| goes as |sin(pi k r)| / k^2. */
        double want = fabs(cos(PI * r)) / 2;
        double got = level(source, n, C[i].hz, 2) / level(source, n, C[i].hz, 1);
        /* at 3.5 kHz the edge smoothing, not the skew, sets H2 */
        double e = fabs(db(got / want));
        if (C[i].hz < 2000 && e > worst_h2) worst_h2 = e;
        float low, high;
        double mean;
        extremes(source, n, &low, &high, &mean);
        /* the mean over whole cycles (Blackman-Harris DC) */
        double dc, unused;
        harmonic(source, n, C[i].hz, 0, &dc, &unused);
        mean = dc / 2;
        double peak = fmax(fabs(high - 1), fabs(low + 1));
        if (C[i].hz < 2000 && peak > worst_peak) worst_peak = peak;
        if (fabs(mean) > worst_mean) worst_mean = fabs(mean);
        printf("    %.1f Hz skew %.4f: H2 %.3f dB off, peaks %+.4f %+.4f, mean %+.5f\n", (double)C[i].hz,
               (double)C[i].skew, db(got / want), (double)low, (double)high, mean);
    }
    printf("  skewed triangle: H2 within %.2f dB of two straight ramps, peaks within %.4f of +-1,"
           " mean within %.5f\n", worst_h2, worst_peak, worst_mean);
    CHECK(worst_h2 < .25, "H2 %.3f dB from two straight ramps", worst_h2);
    CHECK(worst_peak < .01, "peaks %.4f from +-1", worst_peak);
    CHECK(worst_mean < .002, "mean %.5f", worst_mean);

    /* A minute at 27.5 Hz with B1's skew there: the f32 rounding of unequal
     * steps would walk the state 0.3 off; the pull holds it. */
    ma_vco_controls v = { .triangle_level = 1, .pulse_width = .5f };
    float worst = 0;
    static ma_synth s;
    s = bare();
    s.vco_skew[0] = .0446f;
    ma_render_controls c = { 0 };
    c.vco1 = v;
    c.vco2.pulse_width = .5f;
    ma_source_frame f = ma_voice_prepare_source_frame_guarded(&s, &c, 27.5f, 0.0f, 1.0f, 0.0f);
    float low = 2, high = -2, out[8];
    for (int k = 0; k < 60 * RATE; k++) {
        ma_voice_render_source_block(&s, &c, &f, 27.5f, 0.0f, 1.0f, 0.0f, out);
        if (k < 59 * RATE) continue;
        for (int j = 0; j < 8; j++) {
            if (out[j] < low) low = out[j];
            if (out[j] > high) high = out[j];
        }
    }
    worst = fmaxf(worst, fmaxf(fabsf(high - 1), fabsf(low + 1)));
    printf("  a minute at 27.5 Hz, skew 0.0446: peaks within %.5f of +-1 in the last second\n", (double)worst);
    CHECK(worst < .001f, "the skewed triangle walked: peaks %.5f off", (double)worst);
}

/* A new rise on every note (and glides between them): without the move in
 * ml_seed_triangle the integrator walks off centre. */
static double centred(double skew_scale, bool new_hz) {
    static ma_synth s;
    s = bare();
    ma_render_controls c = { 0 };
    c.vco1 = (ma_vco_controls){ .triangle_level = 1, .pulse_width = .5f };
    c.vco2.pulse_width = .5f;
    uint64_t seed = 0x5eed;
    double worst = 0;
    float out[8];
    for (int note = 0; note < 400; note++) {
        float hz = 40.0f + (float)(tw_splitmix64(&seed) % 1000);
        if (!new_hz) hz = 220.0f;
        float skew = (float)skew_scale * (.002f + .05f * (float)(tw_splitmix64(&seed) % 1000) / 1000.0f);
        int frames = 200 + (int)(tw_splitmix64(&seed) % 2000);
        for (int k = 0; k < frames; k++) {
            /* glide the rise over the note's first 100 frames */
            float t = k < 100 ? (float)k / 100.0f : 1.0f;
            s.vco_skew[0] = s.vco_skew[0] + t * (skew - s.vco_skew[0]);
            ma_source_frame f = ma_voice_prepare_source_frame_guarded(&s, &c, hz, 0.0f, 1.0f, 0.0f);
            ma_voice_render_source_block(&s, &c, &f, hz, 0.0f, 1.0f, 0.0f, out);
        }
        /* one settled period: its extremes */
        ma_source_frame f = ma_voice_prepare_source_frame_guarded(&s, &c, hz, 0.0f, 1.0f, 0.0f);
        float low = 2, high = -2;
        for (int k = 0; k < (int)(RATE / hz) + 2; k++) {
            ma_voice_render_source_block(&s, &c, &f, hz, 0.0f, 1.0f, 0.0f, out);
            for (int j = 0; j < 8; j++) {
                if (out[j] < low) low = out[j];
                if (out[j] > high) high = out[j];
            }
        }
        double off = fabs(.5 * ((double)high + low));
        if (off > worst) worst = off;
    }
    return worst;
}

static void test_centred(void) {
    double plain = centred(0, true), fixed = centred(1, false), both = centred(1, true);
    printf("  400 notes: off centre by %.4f with new pitches only (the old triangle), %.4f with new rises"
           " only, %.4f with both\n", plain, fixed, both);
    CHECK(fixed < .01, "the triangle walked %.4f off centre", fixed);
    CHECK(both < plain + .01, "new rises add %.4f to the old walk", both - plain);
}

/* B1's triangle at its buffer (TBUF) in SPICE (shapers bench, 2026-11-14):
 * its second harmonic, where the skew alone leaves less and less. */
static const struct { double hz, h2; } TBUF_SPICE[] = {
    { 225.08, -32.7 }, { 440.09, -37.0 }, { 801.03, -41.3 }, { 1184.23, -43.9 }, { 1944.50, -45.9 },
};

static void test_buffer(void) {
    for (unsigned i = 0; i < sizeof TBUF_SPICE / sizeof *TBUF_SPICE; i++) {
        double hz = TBUF_SPICE[i].hz;
        ma_vco_controls v = { .triangle_level = 1, .pulse_width = .5f };
        ramp_bow_up = (float)b1_bow(hz, true);
        ramp_bow_down = (float)b1_bow(hz, false);
        buffer_amount = 1;
        size_t n = render(v, (float)hz, (float)b1_skew(hz), 0, 0, .25);
        double with = db(level(source, n, hz, 2) / level(source, n, hz, 1));
        buffer_amount = 0;
        n = render(v, (float)hz, (float)b1_skew(hz), 0, 0, .25);
        double without = db(level(source, n, hz, 2) / level(source, n, hz, 1));
        ramp_bow_up = ramp_bow_down = 0;
        printf("  triangle H2 at %6.1f Hz: %.1f dB with B1's buffer, %.1f without (SPICE TBUF %.1f)\n", hz, with,
               without, TBUF_SPICE[i].h2);
        CHECK(fabs(with - TBUF_SPICE[i].h2) < 1.0, "triangle H2 at %g Hz: %.2f dB, SPICE %.2f", hz, with,
              TBUF_SPICE[i].h2);
    }
}

/* ---- the saw's bow --------------------------------------------------------------------- */

static void test_bow(void) {
    ma_vco_controls v = { .saw_level = 1, .pulse_width = .5f };
    float hz = 220, bow = .01205f;
    size_t n = render(v, hz, 0, 0, 0, .1);
    static float straight[SUB];
    memcpy(straight, source, n * sizeof *source);
    (void)render(v, hz, 0, 0, bow, .1);
    /* the same phases, so the difference is bow * (4p(1-p) - 2/3) exactly */
    double worst = 0;
    for (size_t i = 0; i < n; i++) {
        double p = phase_at(i, n);
        double e = fabs((double)(source[i] - straight[i]) - bow * (4 * p * (1 - p) - 2.0 / 3));
        if (e > worst) worst = e;
    }
    printf("  saw bow %.5f: the difference is its parabola within %.2g\n", (double)bow, worst);
    CHECK(worst < 2e-6, "bow differs from its parabola by %.3g", worst);

    /* B1's ramp bows (V2c) on a skewed triangle: the difference to the
     * straight one is the two parabolas, the state untouched */
    ma_vco_controls t = { .triangle_level = 1, .pulse_width = .5f };
    float skew = .0446f;
    n = render(t, 27.5f, skew, 0, 0, .5);
    static float plain[SUB];
    memcpy(plain, source, n * sizeof *source);
    ramp_bow_up = .0149f; ramp_bow_down = -.0215f;
    (void)render(t, 27.5f, skew, 0, 0, .5);
    ramp_bow_up = ramp_bow_down = 0;
    double r = .5 + skew, worst_t = 0;
    for (size_t i = 0; i < n; i++) {
        double ph = phase_at(i, n), u = ph < r ? ph / r : (ph - r) / (1 - r);
        double want = (ph < r ? .0149 : -.0215) * 4 * u * (1 - u);
        double e = fabs((double)(source[i] - plain[i]) - want);
        if (e > worst_t) worst_t = e;
    }
    printf("  ramp bows +0.0149 / -0.0215 at 27.5 Hz: the difference is the two parabolas within %.2g\n", worst_t);
    CHECK(worst_t < 1e-5, "ramp bow differs from its parabolas by %.3g", worst_t);
}

/* ---- the shapers ------------------------------------------------------------------------- */


/* build/vco/shapers/shapers.json (2026-11-14): B1 with both shapers in
 * SPICE, trimmed at A4. Sine: THD, H2, H3 in dB. Saw: the step at the top
 * vertex over half the saw's span. */
static const struct { double hz, thd, h2, h3, static_h3; } SINE_SPICE[] = {
    { 27.40, -25.34, -25.76, -36.57, -44.24 },
    { 40.09, -27.01, -27.36, -39.39, -46.38 },
    { 67.72, -29.28, -29.57, -43.19, -48.63 },
    { 130.29, -32.32, -32.63, -47.44, -53.73 },
    { 225.08, -35.14, -35.54, -49.83, -55.27 },
    { 440.09, -39.51, -40.37, -53.16, -58.63 },
    { 801.03, -43.45, -45.36, -57.22, -57.96 },
    { 1184.23, -45.35, -48.22, -62.68, -55.15 },
    { 1944.50, -46.40, -49.94, -64.06, -50.87 },
};
static const struct { double hz, step; } SAW_SPICE[] = {
    { 27.40, +0.00373 },
    { 40.09, +0.00349 },
    { 67.72, +0.00308 },
    { 130.29, +0.00238 },
    { 225.08, +0.00154 },
    { 440.09, -0.00000 },
    { 801.03, -0.00228 },
    { 1184.23, -0.00452 },
    { 1944.50, -0.00872 },
};

static double thd(const float *x, size_t n, double f0, double *h2, double *h3) {
    double h1 = level(x, n, f0, 1), sum = 0;
    for (int k = 2; k <= 12; k++) {
        double a = level(x, n, f0, k) / h1;
        if (k == 2) *h2 = db(a);
        if (k == 3) *h3 = db(a);
        sum += a * a;
    }
    return 10 * log10(sum);
}

/* build/vco/shapers/high.json (2026-11-14, V2b): the same board with its
 * range switch (C3 / 8, C3 / 16), the A4 trim unchanged. The saw's step, the
 * sine's H2 and H3. */
static const struct { double hz, step, h2, h3; } HIGH_SPICE[] = {
    { 1769.18, -0.01038, -34.63, -49.40 },
    { 3844.24, -0.01852, -38.74, -48.65 },
    { 6174.97, -0.02779, -40.93, -44.12 },
    { 9017.18, -0.03921, -42.11, -40.31 },
    { 11779.44, -0.05045, -42.56, -37.68 },
    { 14461.06, -0.06159, -42.71, -35.69 },
    { 18741.69, -0.07704, -42.18, -33.35 },
};

/* What the switched pair saw in SPICE at a pitch: the swell its step implies
 * (step = -2 w / (2 + w)), log-linear between the cases. */
static double swell_of_step(double step) { return -2 * step / (2 + step); }
static float swell_spice(double hz) {
    if (hz > SAW_SPICE[sizeof SAW_SPICE / sizeof *SAW_SPICE - 1].hz) {
        unsigned n = sizeof HIGH_SPICE / sizeof *HIGH_SPICE, i = 0;
        while (i + 2 < n && HIGH_SPICE[i + 1].hz < hz) i++;
        double t = (hz - HIGH_SPICE[i].hz) / (HIGH_SPICE[i + 1].hz - HIGH_SPICE[i].hz);
        double a = swell_of_step(HIGH_SPICE[i].step), b = swell_of_step(HIGH_SPICE[i + 1].step);
        /* the range switch's own offset at the join (its turn-around is
         * slower at its lower current): taken out, the slope kept */
        double join = swell_of_step(SAW_SPICE[sizeof SAW_SPICE / sizeof *SAW_SPICE - 1].step)
                    - swell_of_step(HIGH_SPICE[0].step)
                    - (SAW_SPICE[sizeof SAW_SPICE / sizeof *SAW_SPICE - 1].hz - HIGH_SPICE[0].hz) * 4.1438e-6;
        return (float)(a + t * (b - a) + join);
    }
    unsigned n = sizeof SAW_SPICE / sizeof *SAW_SPICE, i = 0;
    while (i + 2 < n && SAW_SPICE[i + 1].hz < hz) i++;
    double t = log(hz / SAW_SPICE[i].hz) / log(SAW_SPICE[i + 1].hz / SAW_SPICE[i].hz);
    double a = swell_of_step(SAW_SPICE[i].step), b = swell_of_step(SAW_SPICE[i + 1].step);
    return (float)(a + t * (b - a));
}

static void test_shapers(void) {
    /* The front end's swell against what the pair saw in SPICE. */
    double worst_swell = 0;
    static ma_synth fs;
    for (int note = 29; note <= 127; note++) {
        ma_patch p = plain_patch();
        p.vco_analog = 1;
        p.vco_dead_time = 0;
        ma_synth_init_patch(&fs, RATE, &p);
        ma_synth_set_lfo(&fs, 0, 5);
        ma_synth_note_on(&fs, 0, (uint8_t)note, 100);
        (void)ma_synth_tick(&fs);
        double e = fabs(fs.vco_swell[0] - swell_spice(440.0 * pow(2.0, (note - 69) / 12.0)));
        if (e > worst_swell) worst_swell = e;
    }
    printf("  swell against the switched pair in SPICE, F1..G9: within %.5f\n", worst_swell);
    CHECK(worst_swell < 1e-3, "swell %.5f from SPICE", worst_swell);

    /* The sine on B1's triangle (its rise and swing at each pitch). */
    for (unsigned i = 0; i < sizeof SINE_SPICE / sizeof *SINE_SPICE; i++) {
        ma_vco_controls v = { .tri_sine_level = 1, .pulse_width = .5f };
        float hz = (float)SINE_SPICE[i].hz;
        ramp_bow_up = (float)b1_bow(hz, true);
        ramp_bow_down = (float)b1_bow(hz, false);
        buffer_amount = 1;
        size_t n = render(v, hz, (float)b1_skew(hz), swell_spice(hz), 0, hz < 100 ? 1.0 : .25);
        ramp_bow_up = ramp_bow_down = 0;
        buffer_amount = 0;
        double h2, h3, t = thd(source, n, hz, &h2, &h3);
        printf("  shaper sine at %6.1f Hz: THD %5.1f dB (SPICE %5.1f), H2 %5.1f (%5.1f), H3 %5.1f (%5.1f,"
               " static curve %5.1f)\n", (double)hz, t, SINE_SPICE[i].thd, h2, SINE_SPICE[i].h2, h3,
               SINE_SPICE[i].h3, SINE_SPICE[i].static_h3);
        /* H2 follows B1's skew and, above 1 kHz, its buffer (V2d): within
         * 1.5 dB to 440 Hz. Above it the core has more: the pair's static
         * curve on the board's own TBUF already gives 2.5 to 3 dB more than
         * the board in transient at 0.8 to 1.2 kHz, and at 1.9 kHz the
         * core's own triangle (its edge zone) adds about 4 dB. H3 follows
         * the bowed ramps (V2c) and the sine's input coupling (V3b). */
        CHECK(fabs(h2 - SINE_SPICE[i].h2) < (hz < 500 ? 1.5 : 4.0), "sine H2 at %g Hz: %.2f dB, SPICE %.2f",
              (double)hz, h2, SINE_SPICE[i].h2);
        if (hz > 1200) continue;
        CHECK(fabs(h3 - SINE_SPICE[i].h3) < 1.5, "sine H3 at %g Hz: %.2f dB, SPICE %.2f", (double)hz, h3,
              SINE_SPICE[i].h3);
        CHECK(fabs(t - SINE_SPICE[i].thd) < 2.5, "sine THD at %g Hz: %.2f dB, SPICE %.2f", (double)hz, t,
              SINE_SPICE[i].thd);
        (void)0;
    }
    /* The saw: halves of rise and 1 - rise, centred on -1/2 and +1/2; its
     * step at the top vertex against the switched pair in SPICE (relative to
     * half the saw's span). */
    for (unsigned i = 0; i < sizeof SAW_SPICE / sizeof *SAW_SPICE; i++) {
        ma_vco_controls v = { .tri_saw_level = 1, .pulse_width = .5f };
        float hz = (float)SAW_SPICE[i].hz;
        double skew = b1_skew(hz);
        size_t n = render(v, hz, (float)skew, swell_spice(hz), 0, hz < 100 ? 1.0 : .25);
        double r = .5 + skew, q1 = 0, q3 = 0;
        int m1 = 0, m3 = 0;
        for (size_t k = 0; k < n; k++) {
            double p = phase_at(k, n);
            if (fabs(p - r / 2) < sub_step) { q1 += source[k]; m1++; }
            if (fabs(p - (1 + r) / 2) < sub_step) { q3 += source[k]; m3++; }
        }
        q1 /= m1; q3 /= m3;
        float low, high;
        double mean;
        extremes(source, n, &low, &high, &mean);
        double step = jump_at(n, r) / (.5 * (high - low));
        printf("  shaper saw at %.1f Hz: quarter points %+.4f %+.4f, vertex step %+.5f (SPICE %+.5f)\n",
               (double)hz, q1, q3, step, SAW_SPICE[i].step);
        CHECK(fabs(q1 + .5) < .01 && fabs(q3 - .5) < .01, "saw quarter points at %g Hz: %.4f %.4f",
              (double)hz, q1, q3);
        /* the rounded corners above 1 kHz leave the fitted lines a little */
        CHECK(fabs(step - SAW_SPICE[i].step) < fmax(3e-4, .1 * fabs(SAW_SPICE[i].step)),
              "saw step at %g Hz: %.5f, SPICE %.5f",
              (double)hz, step, SAW_SPICE[i].step);
    }
    /* Above 2 kHz (V2b): the saw's step and the sine's H2 and H3 against the
     * range-switched board, where they are below 20 kHz. */
    for (unsigned i = 1; i < 3; i++) {
        float hz = (float)HIGH_SPICE[i].hz;
        ma_vco_controls v = { .tri_saw_level = 1, .pulse_width = .5f };
        size_t n = render(v, hz, 0, swell_spice(hz), 0, .25);
        float low, high;
        double mean;
        extremes(source, n, &low, &high, &mean);
        double step = jump_at(n, .5) / (.5 * (high - low));
        v = (ma_vco_controls){ .tri_sine_level = 1, .pulse_width = .5f };
        n = render(v, hz, 0, swell_spice(hz), 0, .25);
        double h2, h3;
        (void)thd(source, n, hz, &h2, &h3);
        printf("  at %.0f Hz: saw step %+.4f (SPICE %+.4f), sine H3 %.1f dB (%.1f), H2 %.1f (%.1f; the core's"
               " triangle is not skewed here)\n", (double)hz, step, HIGH_SPICE[i].step, h3, HIGH_SPICE[i].h3, h2,
               HIGH_SPICE[i].h2);
        CHECK(fabs(step - HIGH_SPICE[i].step) < .15 * fabs(HIGH_SPICE[i].step), "saw step at %g Hz: %.4f, SPICE %.4f",
              (double)hz, step, HIGH_SPICE[i].step);
        /* The core's triangle loses top harmonics at high notes (its edge
         * zone), so the pair, which cancels a true triangle's H3, leaves
         * more: 4 dB at 3.8 kHz, 8 dB at 6.2 kHz (H3 at 18.5 kHz). Checked
         * where it is inside 5 dB. */
        if (i == 1)
            CHECK(fabs(h3 - HIGH_SPICE[i].h3) < 5, "sine H3 at %g Hz: %.1f dB, SPICE %.1f", (double)hz, h3,
                  HIGH_SPICE[i].h3);
    }
}

/* ---- the two render paths ---------------------------------------------------------------- */

static ma_voice_pool pool_block, pool_tick;

static void test_pool(void) {
    ma_patch p = ma_patch_tepih;
    p.vco_analog = 1; p.vco_dead_time = .5f;
    p.vco1.triangle_level = .4f; p.vco1.tri_sine_level = .5f; p.vco1.tri_saw_level = .3f;
    p.vco2.tri_saw_level = .6f; p.vco2.saw_level = .4f;
    p.sync_amount = .6f;
    ma_voice_pool_config c = ma_voice_pool_default_config;
    c.limit = 12;
    CHECK(ma_voice_pool_init(&pool_block, RATE, &c, &p) && ma_voice_pool_init(&pool_tick, RATE, &c, &p), "pool init");
    ma_voice_pool_set_character(&pool_block, .5f);
    ma_voice_pool_set_character(&pool_tick, .5f);
    static const uint8_t CHORD[] = { 21, 33, 45, 52, 57, 60, 64, 72, 79, 88, 96 };
    for (unsigned i = 0; i < sizeof CHORD; i++) {
        (void)ma_voice_pool_note_on(&pool_block, 0, CHORD[i], 90);
        (void)ma_voice_pool_note_on(&pool_tick, 0, CHORD[i], 90);
    }
    static ma_frame block[8192];
    unsigned different = 0;
    for (size_t at = 0; at < 8192; at += 128) {
        if (at == 4096)
            for (unsigned i = 0; i < 4; i++) {         /* new notes: new rises on running triangles */
                (void)ma_voice_pool_note_on(&pool_block, 0, (uint8_t)(CHORD[i] + 7), 90);
            }
        ma_voice_pool_render(&pool_block, block + at, 128);
    }
    for (size_t i = 0; i < 8192; i++) {
        if (i == 4096)
            for (unsigned k = 0; k < 4; k++) (void)ma_voice_pool_note_on(&pool_tick, 0, (uint8_t)(CHORD[k] + 7), 90);
        ma_frame x = ma_voice_pool_tick(&pool_tick);
        different += memcmp(&x, &block[i], sizeof x) != 0;
    }
    CHECK(!different, "block render and frame-by-frame tick: %u of 8192 frames differ with every shape on",
          different);
}

int main(void) {
    printf("ma vco shape:\n");
    test_front();
    test_triangle();
    test_centred();
    test_bow();
    test_buffer();
    test_shapers();
    test_pool();
    printf("ma vco shape: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
