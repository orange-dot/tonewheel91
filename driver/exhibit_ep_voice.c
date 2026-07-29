/* ep73 EP1 founding exhibit: the struck voice.
 *
 * A: one note's velocity ladder, pp -> ff. The claim is that velocity moves
 *    timbre and not only loudness — the second harmonic the pickup makes
 *    out of the tine's excursion rises about one dB per dB of level, and
 *    the clang partial rises with the hammer's shortening contact. Tabled
 *    against the pinned constants, and rendered twice: once as played
 *    (the dynamic), once with soft and hard normalised to the same peak
 *    so the ear hears the timbre alone.
 * B: per-register decay conformance. Measured from the rendered f1 band,
 *    not from the amplitude state, against the ep-constants.md section 4
 *    table whose two anchors are the founding patent's dwell figures.
 * C: decision D4. Always-advance versus active-gated across a polyphony
 *    sweep, plus the bit-identity that makes the choice a cost question.
 *
 * Constants and their sources: docs/ep-constants.md.
 */
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include "../src/epiano.h"
#include "wav.h"

#define RATE       48000
#define SPEC_N     2048            /* harmonic window, 42.7 ms          */
#define ENV_N      16384           /* decay window, 341 ms              */
#define NOTE_MAX   (13 * RATE)
#define LADDER_MAX (11 * RATE)

static const double TAU = 6.283185307179586;

static float note[NOTE_MAX];
static float ladder[LADDER_MAX];
static float soft[2 * RATE], hard[2 * RATE];

static const int VELS[7] = { 1, 8, 24, 48, 72, 100, 127 };

/* Hann-windowed magnitude of the bin at f. Returns the sinusoid amplitude
 * for a stationary input, so ratios between bins are directly readable. */
static double mag_at(const float *x, int n, double f) {
    double w = TAU * f / RATE, sr = 0.0, si = 0.0, wsum = 0.0;
    for (int i = 0; i < n; i++) {
        double win = 0.5 - 0.5 * cos(TAU * i / (n - 1));
        sr += x[i] * win * cos(w * i);
        si -= x[i] * win * sin(w * i);
        wsum += win;
    }
    return sqrt(sr * sr + si * si) / wsum;
}

static double db(double r) { return 20.0 * log10(r > 1e-30 ? r : 1e-30); }

static uint64_t hash_f32(const float *x, long n) {
    return tw_fnv1a64(x, (size_t)n * sizeof *x, 0);
}

/* One note, struck at sample 0, rendered into dst. */
static void render_note(float *dst, long n, int midi_note, int velocity) {
    ep_bank b;
    ep_bank_init(&b, RATE);
    ep_bank_strike(&b, midi_note, velocity);
    for (long i = 0; i < n; i++) dst[i] = ep_bank_tick(&b);
}

/* The k-th harmonic of the pinned pickup, for a tine swinging `s` gaps
 * about the rest offset. Section 6's field is even about dead centre and
 * the offset is not, so there is no closed form; this is the exact
 * quadrature the model's own kernel is checked against. */
static double field_harmonic(double s, int k) {
    const int N = 4096;
    double ref = pow(1.0 + (double)EP_PICKUP_OFFSET * EP_PICKUP_OFFSET, -1.5);
    double re = 0.0, im = 0.0;
    for (int i = 0; i < N; i++) {
        double th = TAU * i / N;
        double u = EP_PICKUP_OFFSET + s * sin(th);
        double y = ref - pow(1.0 + u * u, -1.5);
        re += y * cos(k * th);
        im += y * sin(k * th);
    }
    return 2.0 * sqrt(re * re + im * im) / N;
}

/* --- A: the velocity ladder ------------------------------------------ */

static int exhibit_velocity(void) {
    const int midi = 64; /* E4, the middle of the compass */
    const int key = midi - EP_NOTE_MIN;
    const double f1 = ep_key_freq_hz(key);

    printf("A. velocity ladder, MIDI %d (E4, %.2f Hz), hammer zone %d,\n"
           "   %.1f ms window from the strike\n\n", midi, f1, ep_zone(midi),
           1000.0 * SPEC_N / RATE);
    printf("   vel    peak     H2/H1 dB           H3/H1 dB      clang/H1 dB\n");
    printf("                 meas   pinned      meas   pinned    meas  pinned\n");

    /* The H2 err column is reported, not asserted, and the reason is that
     * the contact transient lands in *both* bins and which one it favours
     * changes with velocity. Its corner falls as the fourth root of level
     * (sec 5.5), so at a soft blow it sits below f1 and inflates the
     * denominator, pushing the measured ratio *under* the prediction; at a
     * hard blow it sits above f1 and inflates the numerator instead. An
     * earlier form of this check assumed it could only ever add to the
     * numerator and failed the moment the slope ballot moved the operating
     * point. The kernel-versus-transcendental claim it was really trying to
     * make now lives in test.c, where there is no burst in the way.
     * What is asserted here is what a rendered note can honestly show:
     * that velocity moves timbre monotonically and across a wide range. */
    double first_h2 = 0.0, last_h2 = 0.0;
    int monotone = 1, judged = 0;
    double prev = -1e9, worst_deficit = 0.0, worst_excess = 0.0;

    for (int i = 0; i < 7; i++) {
        int v = VELS[i];
        render_note(note, SPEC_N, midi, v);

        double peak = 0.0;
        for (int j = 0; j < SPEC_N; j++)
            if (fabs(note[j]) > peak) peak = fabs(note[j]);

        double h1 = mag_at(note, SPEC_N, f1);
        double h2 = mag_at(note, SPEC_N, 2.0 * f1);
        double h3 = mag_at(note, SPEC_N, 3.0 * f1);
        double cl = mag_at(note, SPEC_N, EP_MODE_RATIO[1] * f1);

        /* Pinned prediction, ep-constants.md sections 5 and 6. The field
         * has no closed Fourier form, so the prediction is the exact
         * integral of the pinned kernel over one cycle of a sine at the
         * strike's own amplitude — computed here, where libm exists, and
         * compared against what the bank actually produced. */
        double g = ep_pickup_drive(key);
        double a = pow((double)v / 127.0, 1.042) * ep_mode_weight(key, 0, v);
        double p_h1 = field_harmonic(g * a, 1);
        double p_h2 = field_harmonic(g * a, 2) / p_h1;
        double p_h3 = field_harmonic(g * a, 3) / p_h1;
        double p_cl = ep_mode_weight(key, 1, v) / ep_mode_weight(key, 0, v);

        double err = db(h2 / h1) - db(p_h2);
        printf("   %3d  %6.3f  %7.2f %7.2f   %8.2f %7.2f  %6.2f %6.2f  %+6.2f\n",
               v, peak, db(h2 / h1), db(p_h2), db(h3 / h1), db(p_h3),
               db(cl / h1), db(p_cl), err);

        if (err < -worst_deficit) worst_deficit = -err;
        if (err > worst_excess) worst_excess = err;
        if (judged++ == 0) first_h2 = db(h2 / h1);
        last_h2 = db(h2 / h1);
        if (db(h2 / h1) <= prev) monotone = 0;
        prev = db(h2 / h1);
    }

    double swing = last_h2 - first_h2;
    printf("\n   second-harmonic swing pp -> ff: %.1f dB, monotone: %s\n"
           "   spread against the pinned quadrature: %.2f dB under to"
           " %.2f dB over\n",
           swing, monotone ? "yes" : "NO", worst_deficit, worst_excess);
    printf("   (`pinned` is the field's exact quadrature at the strike's own\n"
           "    swing; the err column is the contact transient moving the\n"
           "    ratio, either way, and test.c asserts the kernel itself)\n");

    /* As played: seven strikes, one per 1.5 s. */
    long n = 0;
    ep_bank b;
    ep_bank_init(&b, RATE);
    for (int i = 0; i < 7; i++) {
        ep_bank_strike(&b, midi, VELS[i]);
        for (long j = 0; j < (long)(1.5 * RATE) && n < LADDER_MAX; j++)
            ladder[n++] = 0.2f * ep_bank_tick(&b);
    }

    /* Timbre alone: soft and hard, each normalised to the same peak. */
    render_note(soft, 2 * RATE, midi, 8);
    render_note(hard, 2 * RATE, midi, 127);
    for (int pass = 0; pass < 2; pass++) {
        float *p = pass ? hard : soft;
        double pk = 0.0;
        for (int j = 0; j < 2 * RATE; j++) if (fabs(p[j]) > pk) pk = fabs(p[j]);
        float g = (float)(0.6 / (pk > 0 ? pk : 1.0));
        for (int j = 0; j < 2 * RATE; j++) p[j] *= g;
    }

    int rc = 0;
    rc |= wav_write_f32("build/ep1_a_velocity_ladder.wav", ladder, n, RATE, 1);
    rc |= wav_write_f32("build/ep1_a_soft_normalised.wav", soft, 2 * RATE, RATE, 1);
    rc |= wav_write_f32("build/ep1_a_hard_normalised.wav", hard, 2 * RATE, RATE, 1);
    printf("   wavs: build/ep1_a_velocity_ladder.wav,"
           " build/ep1_a_{soft,hard}_normalised.wav%s\n",
           rc ? " (WRITE FAILED)" : "");

    return rc == 0 && monotone && swing > 30.0;
}

/* --- B: decay conformance -------------------------------------------- */

/* t60 from the rendered f1 band, measured between two windows placed late
 * enough that the pickup's own level-dependent boost of the fundamental is
 * spent. The start was a t60/8 at EP1 and had to move to t60/4 at EP3: the
 * pickup curve is per register now (ep-constants.md sec 6.1) and runs about
 * three times stronger in the bass, so its boost — which goes as alpha
 * squared — takes proportionally longer to leave the window. Measuring the
 * decay of a nonlinear stage means waiting for the nonlinearity. */
static double measured_t60(int midi, double *pinned_out) {
    int key = midi - EP_NOTE_MIN;
    double f1 = ep_key_freq_hz(key);
    double pinned = ep_t60_s(key, 0);
    *pinned_out = pinned;

    long start = (long)(pinned / 4.0 * RATE);
    long span = (long)(pinned / 3.0 * RATE);
    long total = start + span + ENV_N;
    if (total > NOTE_MAX) total = NOTE_MAX;

    render_note(note, total, midi, 127);
    double m0 = mag_at(note + start, ENV_N, f1);
    double m1 = mag_at(note + start + span, ENV_N, f1);
    double drop = db(m0 / m1);
    return drop > 0.0 ? 60.0 * (span / (double)RATE) / drop : 0.0;
}

static int exhibit_decay(void) {
    static const int notes[4] = { 28, 52, 76, 100 }; /* E1 E3 E5 E7 */
    printf("\nB. free-decay conformance, f1 band of the render\n\n");
    printf("   MIDI   f1 Hz     t60 pinned   t60 measured   error\n");
    int ok = 1;
    for (int i = 0; i < 4; i++) {
        double pinned = 0.0;
        double meas = measured_t60(notes[i], &pinned);
        double err = 100.0 * (meas - pinned) / pinned;
        printf("   %4d  %8.2f    %8.2f s     %8.2f s   %+5.1f %%\n",
               notes[i], ep_key_freq_hz(notes[i] - EP_NOTE_MIN), pinned, meas, err);
        if (fabs(err) > 5.0) ok = 0;
    }
    printf("\n   anchors: 17 s at the bottom and 3-5 s at the top are the\n"
           "   founding patent's own dwell figures; everything between is\n"
           "   the f^-p interpolation they fix.\n");
    return ok;
}

/* --- C: decision D4 --------------------------------------------------- */

static double now_s(void) {
    struct timespec t;
    int base = timespec_get(&t, TIME_MONOTONIC) ? TIME_MONOTONIC : TIME_UTC;
    timespec_get(&t, base);
    return (double)t.tv_sec + 1e-9 * (double)t.tv_nsec;
}

/* A short script both layouts have to reproduce bit for bit. */
static uint64_t run_script(int gated, long n) {
    ep_bank b;
    ep_bank_init(&b, RATE);
    uint64_t h = 0;
    for (long i = 0; i < n; i++) {
        if (i % (RATE / 2) == 0) {
            int step = (int)(i / (RATE / 2));
            ep_bank_strike(&b, 40 + 5 * step, 20 + 15 * step);
        }
        float x = gated ? ep_bank_tick_gated(&b) : ep_bank_tick(&b);
        h = tw_fnv1a64(&x, sizeof x, h);
    }
    return h;
}

static double cost_ns(int gated, int voices) {
    const long n = 2L * RATE;
    ep_bank b;
    ep_bank_init(&b, RATE);
    for (int i = 0; i < voices; i++) ep_bank_strike(&b, 28 + i, 127);
    /* warm the caches, then measure */
    for (long i = 0; i < RATE / 10; i++)
        (void)(gated ? ep_bank_tick_gated(&b) : ep_bank_tick(&b));
    double t0 = now_s();
    volatile float sink = 0.0f;
    for (long i = 0; i < n; i++)
        sink += gated ? ep_bank_tick_gated(&b) : ep_bank_tick(&b);
    double t1 = now_s();
    (void)sink;
    return 1e9 * (t1 - t0) / n;
}

static int exhibit_layout(void) {
    static const int poly[6] = { 1, 3, 6, 12, 24, 73 };
    printf("\nC. bank layout, decision D4\n\n");

    uint64_t ha = run_script(0, 4L * RATE);
    uint64_t hg = run_script(1, 4L * RATE);
    printf("   four-second script, FNV64: always-advance %016llx\n",
           (unsigned long long)ha);
    printf("                              active-gated   %016llx  %s\n",
           (unsigned long long)hg, ha == hg ? "identical" : "MISMATCH");

    printf("\n   voices   always-advance   active-gated    one 48k core\n");
    printf("            ns/sample        ns/sample       adv / gated\n");
    for (int i = 0; i < 6; i++) {
        double a = cost_ns(0, poly[i]);
        double g = cost_ns(1, poly[i]);
        printf("   %5d   %10.0f       %10.0f      %5.1f%% / %4.1f%%\n",
               poly[i], a, g, a / 208.33, g / 208.33);
    }
    printf("\n   worst case is the full compass ringing (pedal down,\n"
           "   glissando); the typical case is a chord. Neither layout\n"
           "   vectorises at the project's -O2 — the sine kernel's folding\n"
           "   branches block it, in this bank exactly as in the organ's —\n"
           "   so the two are measured on equal terms.\n");

    return ha == hg;
}

int main(void) {
    printf("ep73 EP1 exhibit: the struck voice (docs/ep-constants.md)\n\n");

    int a = exhibit_velocity();
    int b = exhibit_decay();
    int c = exhibit_layout();

    /* Two-run determinism on the same binary. */
    render_note(note, 4L * RATE, 64, 100);
    uint64_t h1 = hash_f32(note, 4L * RATE);
    render_note(note, 4L * RATE, 64, 100);
    uint64_t h2 = hash_f32(note, 4L * RATE);
    printf("\n   FNV64 E4 v100 run1 %016llx run2 %016llx  %s\n",
           (unsigned long long)h1, (unsigned long long)h2,
           h1 == h2 ? "identical" : "MISMATCH");

    int verdict = a && b && c && h1 == h2;
    printf("\n  exhibit verdict: %s\n", verdict ? "PASS" : "FAIL");
    return verdict ? 0 : 1;
}
