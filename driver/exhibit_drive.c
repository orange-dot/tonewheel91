/* tonewheel91 M5 exhibit -- the stateful preamp drive.
 *
 * Bias-excursion A/B: the same organ passage rendered through the
 * stateful stage (sec 14.1: envelope follower shifting the saturator's
 * operating point toward cutoff + coupling-cap highpass) and through
 * the wrong model design.md rules out -- the same tanh-shaped kernel
 * used bare (memoryless, same pregain/makeup law, no follower, no cap).
 * The bare render's harmonic recipe is frozen; the stateful render's
 * even harmonics bloom and duck with playing level, the coupling cap
 * breathes on level changes, and closing the swell cleans the stage up
 * (the design.md ordering argument, audible mid-passage). That
 * difference is the exhibit.
 *
 * Measurements (printed, recorded in docs/m5-evidence.md): saturator
 * kernel deviation vs true tanh, follower attack/release taus, the
 * coupling-cap -3 dB point, even/odd harmonic proxy for stateful vs
 * bare at equal drive (sec 14's odd/even proxy), drive-0 identity
 * against the bare organ, and two-run FNV determinism of the driven
 * render.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../src/tonewheel.h"
#include "wav.h"

#define RATE 48000
#define SECS 8
#define FRAMES (RATE * SECS)

static const double TAU_D = 6.283185307179586;

static float dry_buf[FRAMES], bias_buf[FRAMES], naive_buf[FRAMES];
static float organ_buf[FRAMES];

/* pregain / X_ref for a knob value (sec 14.1): (1 + 7 v^2) / 8. */
static float drive_pre(float v) { return (1.0f + 7.0f * v * v) / 8.0f; }

/* One 8 s passage: percussive chord attack, swell closed mid-way (the
 * stage cleans up), reopened, released. */
static void render_passage(float *dst, int frames, float drive) {
    tw_instrument ins;
    tw_instrument_init(&ins, RATE);
    /* M7: pin the idealized reference (wear 0) — this exhibit's recorded
     * signatures predate the wear stage and must stay bit-stable. */
    tw_organ_set_wear(&ins.organ, 0.0f);
    tw_instrument_set_drive(&ins, drive);
    static const uint8_t reg[TW_DRAWBARS] = { 8, 8, 8, 8, 0, 0, 0, 0, 0 };
    tw_organ_set_registration(&ins.organ, reg);
    tw_organ_set_percussion(&ins.organ, true, false, false, true);
    static const int chord[3] = { 48, 52, 55 }; /* low C-E-G: IMD-rich */
    for (int i = 0; i < frames; i++) {
        if (i == RATE / 2)
            for (int k = 0; k < 3; k++) tw_organ_note(&ins.organ, chord[k], true, 100);
        if (i == 3 * RATE) tw_organ_set_swell(&ins.organ, 0.3f);
        if (i == 5 * RATE) tw_organ_set_swell(&ins.organ, 1.0f);
        if (i == 7 * RATE)
            for (int k = 0; k < 3; k++) tw_organ_note(&ins.organ, chord[k], false, 0);
        dst[i] = tw_instrument_tick(&ins);
    }
}

/* The same passage on a bare organ -- the drive-0 identity oracle. */
static void render_organ_twin(float *dst, int frames) {
    tw_organ o;
    tw_organ_init(&o, RATE);
    tw_organ_set_wear(&o, 0.0f); /* M7: idealized reference */
    static const uint8_t reg[TW_DRAWBARS] = { 8, 8, 8, 8, 0, 0, 0, 0, 0 };
    tw_organ_set_registration(&o, reg);
    tw_organ_set_percussion(&o, true, false, false, true);
    static const int chord[3] = { 48, 52, 55 };
    for (int i = 0; i < frames; i++) {
        if (i == RATE / 2)
            for (int k = 0; k < 3; k++) tw_organ_note(&o, chord[k], true, 100);
        if (i == 3 * RATE) tw_organ_set_swell(&o, 0.3f);
        if (i == 5 * RATE) tw_organ_set_swell(&o, 1.0f);
        if (i == 7 * RATE)
            for (int k = 0; k < 3; k++) tw_organ_note(&o, chord[k], false, 0);
        dst[i] = tw_organ_tick(&o);
    }
}

/* The wrong model (design.md/sec 14.1): the same kernel and the same
 * pregain/makeup law, memoryless -- no follower, no coupling cap. */
static void naive_drive(float *dst, const float *src, int frames, float drive) {
    float pre = drive_pre(drive), post = 1.0f / pre;
    for (int i = 0; i < frames; i++)
        dst[i] = post * tw_sat(pre * src[i]);
}

static double mag_at(const float *h, int n, double f) {
    double re = 0.0, im = 0.0;
    for (int i = 0; i < n; i++) {
        double a = TAU_D * f * (double)i / RATE;
        re += (double)h[i] * cos(a);
        im -= (double)h[i] * sin(a);
    }
    return sqrt(re * re + im * im);
}

static double measure_kernel(void) {
    double worst = 0.0;
    for (int i = -30000; i <= 30000; i++) {
        double x = (double)i * 1e-4;
        double err = fabs((double)tw_sat((float)x) - tanh(x));
        if (err > worst) worst = err;
    }
    return worst;
}

static void measure_taus(int *atk, int *rel) {
    tw_drive d;
    tw_drive_init(&d, RATE);
    tw_drive_set(&d, 1.0f); /* pre = 1: a unit step is |in| = 1 */
    double knee = 1.0 - exp(-1.0);
    int n = 0;
    while ((double)d.env < knee && n < RATE) {
        (void)tw_drive_tick(&d, 1.0f);
        n++;
    }
    *atk = n;
    for (int i = 0; i < RATE; i++) (void)tw_drive_tick(&d, 1.0f);
    n = 0;
    while ((double)d.env > exp(-1.0) && n < 10 * RATE) {
        (void)tw_drive_tick(&d, 0.0f);
        n++;
    }
    *rel = n;
}

/* Small-signal stage gain at one frequency by DFT on a 2 s window
 * (integer cycles for half-integer f), settle 2 s. */
static float tone_buf[2 * RATE];

static double tone_gain(double f_hz) {
    tw_drive d;
    tw_drive_init(&d, RATE);
    tw_drive_set(&d, 0.1f);
    const double amp = 0.002;
    double ph = 0.0;
    for (int i = 0; i < 4 * RATE; i++) {
        float y = tw_drive_tick(&d, (float)(amp * sin(TAU_D * ph)));
        if (i >= 2 * RATE) tone_buf[i - 2 * RATE] = y;
        ph += f_hz / RATE;
        if (ph >= 1.0) ph -= 1.0;
    }
    return mag_at(tone_buf, 2 * RATE, f_hz) / (amp * 2.0 * RATE / 2.0);
}

/* Scan 6..14 Hz in 0.5 Hz steps for the -3 dB crossing. */
static double measure_hp_edge(void) {
    double edge = 0.0;
    for (double f = 6.0; f <= 14.0; f += 0.5)
        if (tone_gain(f) < 0.70710678) edge = f + 0.5;
    return edge;
}

/* Even/odd proxy at 220 Hz, drive 0.8, organ-scale amplitude 2. */
static void measure_harmonics(double *h2_db, double *n2_db, double *n3_db) {
    tw_drive d;
    tw_drive_init(&d, RATE);
    tw_drive_set(&d, 0.8f);
    float pre = drive_pre(0.8f), post = 1.0f / pre;
    static float sb[RATE], nb[RATE];
    double ph = 0.0;
    for (int i = 0; i < 2 * RATE; i++) {
        float x = 2.0f * tw_sin_turns((float)ph);
        float y = tw_drive_tick(&d, x);
        if (i >= RATE) {
            sb[i - RATE] = y;
            nb[i - RATE] = post * tw_sat(pre * x);
        }
        ph += 220.0 / RATE;
        if (ph >= 1.0) ph -= 1.0;
    }
    double s1 = mag_at(sb, RATE, 220.0), s2 = mag_at(sb, RATE, 440.0);
    double m1 = mag_at(nb, RATE, 220.0), m2 = mag_at(nb, RATE, 440.0);
    double m3 = mag_at(nb, RATE, 660.0);
    *h2_db = 20.0 * log10(s2 / s1);
    *n2_db = 20.0 * log10(m2 / m1 + 1e-30);
    *n3_db = 20.0 * log10(m3 / m1);
}

int main(void) {
    printf("tonewheel91 M5 exhibit -- the stateful preamp drive\n\n");

    double kerr = measure_kernel();
    printf("  saturator kernel (sec 14.1 [derived]):\n");
    printf("    worst deviation vs true tanh on |x| <= 3: %.4f (~x = 1.5)\n", kerr);

    int atk, rel;
    measure_taus(&atk, &rel);
    printf("  bias-excursion follower:\n");
    printf("    attack: %d samples = %.2f ms (pinned 5 ms)\n",
           atk, 1000.0 * atk / RATE);
    printf("    release: %d samples = %.2f ms (pinned 50 ms)\n",
           rel, 1000.0 * rel / RATE);

    double edge = measure_hp_edge();
    double g200 = tone_gain(200.0);
    printf("  coupling-cap highpass:\n");
    printf("    -3 dB crossing: %.1f Hz (pinned 10 Hz; 0.5 Hz scan)\n", edge);
    printf("    |H(200 Hz)|: %.4f (passband transparency)\n", g200);

    double h2_db, n2_db, n3_db;
    measure_harmonics(&h2_db, &n2_db, &n3_db);
    printf("  even/odd proxy (220 Hz, amp 2, drive 0.8):\n");
    printf("    stateful stage H2/H1: %.1f dB (the bias bloom)\n", h2_db);
    printf("    bare shaper   H2/H1: %.1f dB (odd symmetry: none)\n", n2_db);
    printf("    bare shaper   H3/H1: %.1f dB (odd content present)\n", n3_db);

    /* drive 0 identity: the instrument render must equal the organ's */
    render_passage(dry_buf, FRAMES, 0.0f);
    render_organ_twin(organ_buf, FRAMES);
    uint64_t hd = tw_fnv1a64(dry_buf, sizeof dry_buf, 0);
    uint64_t ho = tw_fnv1a64(organ_buf, sizeof organ_buf, 0);
    printf("\n  drive-0 identity: FNV64 %016llx %s organ %016llx\n",
           (unsigned long long)hd, hd == ho ? "==" : "!=",
           (unsigned long long)ho);

    /* the A/B and two-run determinism of the driven render */
    render_passage(bias_buf, FRAMES, 0.7f);
    naive_drive(naive_buf, dry_buf, FRAMES, 0.7f);
    static float r2[FRAMES];
    render_passage(r2, FRAMES, 0.7f);
    uint64_t h1 = tw_fnv1a64(bias_buf, sizeof bias_buf, 0);
    uint64_t h2 = tw_fnv1a64(r2, sizeof r2, 0);
    printf("  scripted determinism (driven passage): FNV64 %016llx %s\n",
           (unsigned long long)h1, h1 == h2 ? "(two runs identical)" : "MISMATCH");

    /* exhibit scale: the M1..M4 1/8 headroom convention, all three files */
    for (int i = 0; i < FRAMES; i++) {
        dry_buf[i] *= 0.125f;
        bias_buf[i] *= 0.125f;
        naive_buf[i] *= 0.125f;
    }
    int rc = 0;
    rc |= wav_write_f32("build/m5_dry.wav", dry_buf, FRAMES, RATE, 1);
    rc |= wav_write_f32("build/m5_drive_bias.wav", bias_buf, FRAMES, RATE, 1);
    rc |= wav_write_f32("build/m5_drive_naive.wav", naive_buf, FRAMES, RATE, 1);
    printf("\n  wavs: build/m5_dry.wav, m5_drive_bias.wav, m5_drive_naive.wav\n"
           "        (A/B: bias vs naive; same kernel, same drive 0.7)%s\n",
           rc ? " (WRITE FAILED)" : "");

    int verdict = kerr < 0.03
               && atk >= 228 && atk <= 252
               && rel >= 2280 && rel <= 2520
               && edge >= 9.0 && edge <= 11.0
               && g200 > 0.985 && g200 < 1.005
               && h2_db > -34.0 && n2_db < -80.0 && n3_db > -40.0
               && hd == ho && h1 == h2 && rc == 0;
    printf("\n  exhibit verdict: %s\n", verdict ? "PASS" : "FAIL");
    return verdict ? 0 : 1;
}
