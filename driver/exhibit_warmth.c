/* tonewheel91 warmth harness -- scoring the sec 14.1 drive stage against
 * a circuit-true reference (driver/spice/stage1.cir; `make warmth-ref`).
 *
 * The question (docs/warmth-evidence.md): how much of a real triode
 * stage's warmth signature -- even-harmonic dominance, its bloom with
 * level, gain compression, the H2 onset -- does the behavioral stage
 * already carry, and where does it measurably differ? One analyzer for
 * both subjects, so neither side gets a friendlier ruler:
 *
 *   exhibit_warmth                        C battery: 240 Hz level sweep
 *                                         + 120/480 Hz rows + verdict
 *   exhibit_warmth onset-c A              C onset table at amplitude A
 *   exhibit_warmth spice FILE F0 AMP      one reference sweep row
 *   exhibit_warmth spice-onset FILE F0 T0 reference onset table
 *
 * Sweep window: the last 12000 samples (0.25 s) of each run -- an
 * integer period count at 240/120/480 Hz, so the rectangular DFT is
 * leakage-free (240 Hz = exactly 200 samples/period at 48 kHz). Onset:
 * a per-period H1/H2/bias track from tone start. The spice files are
 * ngspice wrdata exports linearized onto the same 48 kHz grid; both
 * subjects go through amp_at below and nothing else. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/tonewheel.h"
#include "wav.h"

#define RATE 48000
#define N_MEAS 12000
#define N_SETTLE 36000
#define N_FILE 40001
#define ONSET_PERIODS 60

static const double TAU_D = 6.283185307179586;

static double meas[N_MEAS];
static double col_t[N_FILE], col_out[N_FILE], col_k[N_FILE], col_g[N_FILE];

/* Amplitude of the component at an integer cycle count per window;
 * rectangular window, exact for integer cycles. */
static double amp_at(const double *h, int n, int cycles) {
    double re = 0.0, im = 0.0;
    for (int i = 0; i < n; i++) {
        double a = TAU_D * (double)cycles * (double)i / (double)n;
        re += h[i] * cos(a);
        im -= h[i] * sin(a);
    }
    return 2.0 * sqrt(re * re + im * im) / (double)n;
}

static double mean_of(const double *h, int n) {
    double s = 0.0;
    for (int i = 0; i < n; i++) s += h[i];
    return s / (double)n;
}

static double db(double r) { return 20.0 * log10(r + 1e-30); }

/* One sweep row: fundamental gain, H2..H5 relative levels, THD over
 * H2..H5, and two bias readouts the caller labels (env and shift for
 * the C stage; mean cathode and grid volts for the reference). */
static void row_print(double f0, double a, const double *y, int n,
                      double b1, double b2) {
    int c1 = (int)((double)n * f0 / RATE + 0.5);
    double h1 = amp_at(y, n, c1);
    double h2 = amp_at(y, n, 2 * c1), h3 = amp_at(y, n, 3 * c1);
    double h4 = amp_at(y, n, 4 * c1), h5 = amp_at(y, n, 5 * c1);
    double thd = 100.0 * sqrt(h2 * h2 + h3 * h3 + h4 * h4 + h5 * h5) / h1;
    printf("    %5.0f  %4.2f  %8.4f  %6.1f  %7.1f  %7.1f  %7.1f  %7.3f  %+9.4f  %+9.4f\n",
           f0, a, h1 / a, db(h2 / h1), db(h3 / h1), db(h4 / h1), db(h5 / h1),
           thd, b1, b2);
}

static const char *ROW_HDR =
    "       f0   amp      gain   h2/h1    h3/h1    h4/h1    h5/h1     thd%";

/* Run tw_drive at drive 1.0 (pre = 1: amp IS the shaper-input
 * amplitude) and fill the measure window; returns the settled env and
 * the DC image (mean of the coupling-cap tracker over the last 400
 * samples — an integer cycle count at 120/240/480 Hz, so the tracker's
 * fundamental ripple cancels; a single end-sample would alias it). */
static double c_fill(double amp, double f0, double *dc) {
    tw_drive d;
    tw_drive_init(&d, RATE);
    tw_drive_set(&d, 1.0f);
    double ph = 0.0, lp = 0.0;
    for (int i = 0; i < N_SETTLE + N_MEAS; i++) {
        float y = tw_drive_tick(&d, (float)(amp * sin(TAU_D * ph)));
        if (i >= N_SETTLE) meas[i - N_SETTLE] = (double)y;
        if (i >= N_SETTLE + N_MEAS - 400) lp += (double)d.hp_lp;
        ph += f0 / RATE;
        if (ph >= 1.0) ph -= 1.0;
    }
    if (dc) *dc = lp / 400.0;
    return (double)d.env;
}

static int battery(void) {
    printf("tonewheel91 warmth harness -- C side (tw_drive, drive 1.0, 48 kHz)\n\n");

    /* analyzer floor: the float-quantized input sine through the same
     * ruler -- the H2 reading of a signal that has none. */
    double ph = 0.0;
    for (int i = 0; i < N_MEAS; i++) {
        meas[i] = (double)(float)(0.3 * sin(TAU_D * ph));
        ph += 240.0 / RATE;
        if (ph >= 1.0) ph -= 1.0;
    }
    double floor_db = db(amp_at(meas, N_MEAS, 120) / amp_at(meas, N_MEAS, 60));
    printf("  analyzer H2 floor on a pure sine: %.1f dB\n\n", floor_db);

    static const double AMPS[12] = { 0.02, 0.05, 0.1, 0.15, 0.2, 0.3,
                                     0.5,  0.7,  1.0, 1.5,  2.0, 3.0 };
    printf("  240 Hz level sweep (amp = shaper-input amplitude, unit gain law):\n");
    printf("%s       env    dc_img\n", ROW_HDR);
    for (int i = 0; i < 12; i++) {
        double dc, env = c_fill(AMPS[i], 240.0, &dc);
        row_print(240.0, AMPS[i], meas, N_MEAS, env, dc);
    }

    printf("\n  frequency rows at amp 0.30:\n");
    printf("%s       env    dc_img\n", ROW_HDR);
    double dc120, e120 = c_fill(0.3, 120.0, &dc120);
    row_print(120.0, 0.3, meas, N_MEAS, e120, dc120);
    double dc480, e480 = c_fill(0.3, 480.0, &dc480);
    row_print(480.0, 0.3, meas, N_MEAS, e480, dc480);

    /* two-run determinism of the amp-1.0 window */
    (void)c_fill(1.0, 240.0, nullptr);
    uint64_t h1 = tw_fnv1a64(meas, sizeof meas, 0);
    (void)c_fill(1.0, 240.0, nullptr);
    uint64_t h2 = tw_fnv1a64(meas, sizeof meas, 0);
    printf("\n  measure-window determinism (amp 1.0): FNV64 %016llx %s\n",
           (unsigned long long)h1, h1 == h2 ? "(two runs identical)" : "MISMATCH");

    int verdict = floor_db < -120.0 && h1 == h2;
    printf("\n  harness verdict: %s\n", verdict ? "PASS" : "FAIL");
    return verdict ? 0 : 1;
}

/* Per-period H1/H2/bias track from tone start; shared by both onset
 * modes. bias[] is the per-period mean of the caller's bias signal. */
static void onset_table(const double *y, const double *bias, int period,
                        double f0) {
    static double h1p[ONSET_PERIODS], h2p[ONSET_PERIODS];
    for (int p = 0; p < ONSET_PERIODS; p++) {
        h1p[p] = amp_at(y + p * period, period, 1);
        h2p[p] = amp_at(y + p * period, period, 2);
    }
    double steady = 0.0, steady1 = 0.0;
    for (int p = 40; p < 60; p++) { steady += h2p[p]; steady1 += h1p[p]; }
    steady /= 20.0; steady1 /= 20.0;

    double pms = 1000.0 * (double)period / RATE;
    printf("     t_ms       h1   h2/h1      bias\n");
    for (int p = 0; p < ONSET_PERIODS; p++) {
        if (p >= 15 && (p + 1) % 10 != 0) continue;
        printf("    %5.1f  %7.4f  %6.1f  %+9.4f\n", (double)p * pms,
               h1p[p], db(h2p[p] / h1p[p]), bias[p]);
    }
    printf("    steady h2/h1 (periods 41..60): %.1f dB\n", db(steady / steady1));
    int p50 = -1, p90 = -1;
    for (int p = 0; p < ONSET_PERIODS; p++) {
        if (p50 < 0 && h2p[p] >= 0.5 * steady) p50 = p;
        if (p90 < 0 && h2p[p] >= 0.9 * steady) p90 = p;
    }
    printf("    h2 reaches 50%% of steady within %.1f ms, 90%% within %.1f ms\n",
           (double)(p50 + 1) * pms, (double)(p90 + 1) * pms);
    (void)f0;
}

static int onset_c(double amp) {
    const int period = RATE / 240;
    static double y[ONSET_PERIODS * (RATE / 240)];
    static double eb[ONSET_PERIODS];
    tw_drive d;
    tw_drive_init(&d, RATE);
    tw_drive_set(&d, 1.0f);
    double ph = 0.0;
    for (int p = 0; p < ONSET_PERIODS; p++) {
        double es = 0.0;
        for (int i = 0; i < period; i++) {
            y[p * period + i] =
                (double)tw_drive_tick(&d, (float)(amp * sin(TAU_D * ph)));
            es += (double)d.env;
            ph += 240.0 / RATE;
            if (ph >= 1.0) ph -= 1.0;
        }
        eb[p] = es / (double)period; /* per-period mean follower level */
    }
    printf("  C onset at amp %.2f, 240 Hz, from zeroed state (bias = env mean):\n",
           amp);
    onset_table(y, eb, period, 240.0);
    return 0;
}

/* ngspice wrdata import: one header line (wr_vecnames), then rows of
 * time v(out) v(k) v(g) on the 48 kHz linearize grid (wr_singlescale). */
static int parse_wrdata(const char *path, int *n) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return -1; }
    char hdr[256];
    if (!fgets(hdr, sizeof hdr, f)) { fclose(f); return -1; }
    int i = 0;
    while (i < N_FILE && fscanf(f, "%lf %lf %lf %lf", &col_t[i], &col_out[i],
                                &col_k[i], &col_g[i]) == 4)
        i++;
    fclose(f);
    *n = i;
    return (i >= 4096) ? 0 : -1; /* each mode checks its own coverage */
}

static int spice_row(const char *path, double f0, double amp) {
    int n;
    if (parse_wrdata(path, &n) != 0 || n < N_MEAS) {
        fprintf(stderr, "sweep window not covered by %s\n", path);
        return 1;
    }
    int base = n - N_MEAS;
    row_print(f0, amp, col_out + base, N_MEAS, mean_of(col_k + base, N_MEAS),
              mean_of(col_g + base, N_MEAS));
    return 0;
}

static int spice_onset(const char *path, double f0, double t0) {
    int n;
    if (parse_wrdata(path, &n) != 0) return 1;
    int start = 0;
    while (start < n && col_t[start] < t0 - 1e-9) start++;
    int period = (int)((double)RATE / f0 + 0.5);
    if (start == 0 || n - start < ONSET_PERIODS * period) {
        fprintf(stderr, "onset window not covered by %s\n", path);
        return 1;
    }
    double vk0 = mean_of(col_k, start);
    static double eb[ONSET_PERIODS];
    for (int p = 0; p < ONSET_PERIODS; p++)
        eb[p] = mean_of(col_k + start + p * period, period) - vk0;
    printf("  reference onset (tone at t0, bias = cathode-volt walk from quiescent %.4f V):\n",
           vk0);
    onset_table(col_out + start, eb, period, f0);
    return 0;
}

/* --- fit: derive the drive kernel table from the static curve ---
 *
 * Input: the curve.cir DC sweep (columns vin, v(p), v(g), v(k)).
 * Normalization [derived]: input axis S = 0.72 V per shaper-input unit
 * (the 1 % THD anchor of warmth-evidence: 0.155 V vs 0.215 units);
 * output normalized to unit small-signal slope by G0 (central
 * difference at vin = 0). The fit is a monotone C1 cubic Hermite on
 * uniform knots over [-8, 8], h = 0.25: slopes are central differences
 * limited to 3x the adjacent secants (Fritsch-Carlson), end slopes 0
 * (flat C1 rails), y(0) forced to exactly 0 (silence discipline).
 * Prints the residual against the sweep and emits the C arrays for
 * src/drive.c. */
#define FIT_N 65
#define FIT_H 0.25

static double fit_sample(double u) {
    /* linear interp of the sweep at vin = u * S (sweep step 2 mV) */
    double s = 0.72;
    double vin = u * s;
    double pos = (vin + 6.0) / 0.002;
    int i = (int)pos;
    if (i < 0) i = 0;
    if (i > 6000 - 1) i = 6000 - 1;
    double t = pos - (double)i;
    return col_out[i] * (1.0 - t) + col_out[i + 1] * t;
}

static int fit(const char *path) {
    int n;
    if (parse_wrdata(path, &n) != 0 || n < 6001) {
        fprintf(stderr, "curve sweep incomplete (%d rows)\n", n);
        return 1;
    }
    double s = 0.72;
    /* op point and small-signal gain from the sweep itself */
    int z = 3000; /* vin = 0 at 2 mV steps from -6 V */
    double vp0 = col_out[z];
    double g0 = -(col_out[z + 1] - col_out[z - 1]) / (2.0 * 0.002);
    printf("  fit: vp0 = %.4f V, G0 = %.4f, S = %.2f V/unit\n", vp0, g0, s);

    static double yk[FIT_N], mk[FIT_N], dl[FIT_N];
    for (int i = 0; i < FIT_N; i++) {
        double u = -8.0 + FIT_H * (double)i;
        yk[i] = (vp0 - fit_sample(u)) / (g0 * s);
    }
    yk[FIT_N / 2] = 0.0; /* silence discipline: curve(0) exactly 0 */
    for (int i = 0; i < FIT_N - 1; i++) dl[i] = (yk[i + 1] - yk[i]) / FIT_H;
    mk[0] = 0.0;
    mk[FIT_N - 1] = 0.0; /* flat C1 rails */
    for (int i = 1; i < FIT_N - 1; i++) {
        double m = 0.5 * (dl[i - 1] + dl[i]);
        double lim = 3.0 * (dl[i - 1] < dl[i] ? dl[i - 1] : dl[i]);
        if (m > lim) m = lim; /* monotone limiter; the curve is rising */
        if (m < 0.0) m = 0.0;
        mk[i] = m;
    }

    /* residual of the Hermite against the raw sweep, over |u| <= 8 */
    double worst = 0.0, worst_u = 0.0;
    for (int j = 0; j <= 3200; j++) {
        double u = -8.0 + (double)j * 0.005;
        double t = (u + 8.0) / FIT_H;
        int i = (int)t;
        if (i > FIT_N - 2) i = FIT_N - 2;
        t -= (double)i;
        double t2 = t * t, t3 = t2 * t;
        double fitv = yk[i] * (2 * t3 - 3 * t2 + 1) +
                      mk[i] * FIT_H * (t3 - 2 * t2 + t) +
                      yk[i + 1] * (-2 * t3 + 3 * t2) + mk[i + 1] * FIT_H * (t3 - t2);
        double ref = (vp0 - fit_sample(u)) / (g0 * s);
        double e = fitv - ref;
        if (e < 0) e = -e;
        if (e > worst) { worst = e; worst_u = u; }
    }
    printf("  fit residual vs sweep: worst %.5f at u = %+.3f (curve spans %.3f..%+.3f)\n\n",
           worst, worst_u, yk[0], yk[FIT_N - 1]);

    /* emit the drive.c tables: slopes pre-multiplied by h */
    printf("static const float CURVE_Y[%d] = {", FIT_N);
    for (int i = 0; i < FIT_N; i++)
        printf("%s%.8ff,", (i % 5) ? " " : "\n    ", yk[i]);
    printf("\n};\n");
    printf("static const float CURVE_MH[%d] = {", FIT_N);
    for (int i = 0; i < FIT_N; i++)
        printf("%s%.8ff,", (i % 5) ? " " : "\n    ", mk[i] * FIT_H);
    printf("\n};\n");
    return 0;
}

/* --- render: the ears' A/B ---
 *
 * The M5 exhibit passage (low C-E-G with percussion, swell closing and
 * reopening mid-way) rendered through the SAME stage twice per drive
 * setting: once with the M5 odd kernel + 0.5 depth (the pre-warmth
 * voice, exact via tw_drive_set_kernel(_, true) — not a replica), once
 * with the derived triode kernel. Warmth lives at drive ~0.35; grit at
 * 0.7. Files at the exhibits' 1/8 headroom scale, f32 (peaks above
 * full-scale survive; the printout reports them). */
#define R_SECS 8
#define R_FRAMES (RATE * R_SECS)

static float rbuf[R_FRAMES];

static void render_passage(float drive, bool odd) {
    tw_instrument ins;
    tw_instrument_init(&ins, RATE);
    tw_organ_set_wear(&ins.organ, 0.0f); /* idealized reference */
    tw_drive_set_kernel(&ins.drive, odd);
    tw_instrument_set_drive(&ins, drive);
    static const uint8_t reg[TW_DRAWBARS] = { 8, 8, 8, 8, 0, 0, 0, 0, 0 };
    tw_organ_set_registration(&ins.organ, reg);
    tw_organ_set_percussion(&ins.organ, true, false, false, true);
    static const int chord[3] = { 48, 52, 55 };
    for (int i = 0; i < R_FRAMES; i++) {
        if (i == RATE / 2)
            for (int k = 0; k < 3; k++) tw_organ_note(&ins.organ, chord[k], true, 100);
        if (i == 3 * RATE) tw_organ_set_swell(&ins.organ, 0.3f);
        if (i == 5 * RATE) tw_organ_set_swell(&ins.organ, 1.0f);
        if (i == 7 * RATE)
            for (int k = 0; k < 3; k++) tw_organ_note(&ins.organ, chord[k], false, 0);
        rbuf[i] = tw_instrument_tick(&ins);
    }
}

static int render_one(const char *path, float drive, bool odd) {
    render_passage(drive, odd);
    float peak = 0.0f;
    for (int i = 0; i < R_FRAMES; i++) {
        rbuf[i] *= 0.125f;
        float a = tw_fabsf(rbuf[i]);
        if (a > peak) peak = a;
    }
    uint64_t h = tw_fnv1a64(rbuf, sizeof rbuf, 0);
    int rc = wav_write_f32(path, rbuf, R_FRAMES, RATE, 1);
    printf("    %-28s peak %5.3f  FNV64 %016llx%s\n", path, (double)peak,
           (unsigned long long)h, rc ? "  (WRITE FAILED)" : "");
    return rc;
}

static int render(void) {
    printf("  warmth A/B renders (M5 passage; old = M5 odd kernel, new = derived triode):\n");
    int rc = 0;
    rc |= render_one("build/warmth_ab_dry.wav", 0.0f, false);
    rc |= render_one("build/warmth_ab_old_035.wav", 0.35f, true);
    rc |= render_one("build/warmth_ab_new_035.wav", 0.35f, false);
    rc |= render_one("build/warmth_ab_old_070.wav", 0.7f, true);
    rc |= render_one("build/warmth_ab_new_070.wav", 0.7f, false);
    return rc;
}

int main(int argc, char **argv) {
    if (argc == 1) return battery();
    if (argc == 2 && strcmp(argv[1], "render") == 0) return render();
    if (argc == 3 && strcmp(argv[1], "onset-c") == 0)
        return onset_c(strtod(argv[2], nullptr));
    if (argc == 5 && strcmp(argv[1], "spice") == 0)
        return spice_row(argv[2], strtod(argv[3], nullptr), strtod(argv[4], nullptr));
    if (argc == 5 && strcmp(argv[1], "spice-onset") == 0)
        return spice_onset(argv[2], strtod(argv[3], nullptr), strtod(argv[4], nullptr));
    if (argc == 3 && strcmp(argv[1], "fit") == 0)
        return fit(argv[2]);
    fprintf(stderr,
            "usage: exhibit_warmth [onset-c A | spice FILE F0 AMP | spice-onset FILE F0 T0 | fit FILE]\n");
    return 2;
}
