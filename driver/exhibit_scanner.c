/* tonewheel91 M4 exhibit -- the vibrato/chorus scanner.
 *
 * Dispersion A/B: the same organ chord rendered through the real line-box
 * scanner (C3: dry + swept dispersive ladder) and through the wrong model
 * constants.md section 9 warns about -- a plain modulated-delay chorus
 * (ideal delay line, linear-interpolated read, same 6.867 Hz triangular
 * sweep, same 0.85 ms span, same equal mix). The ladder's lowpass edge,
 * moving passband ripple, per-tap AM ramp and dispersion are all absent
 * from the naive render; that difference is the exhibit.
 *
 * Measurements (printed, recorded in docs/m4-evidence.md): line impulse
 * timing at v19, passband edge, V3 depth via zero-crossing instantaneous
 * frequency of a 1 kHz tone (sec 9 acceptance band 1.1-1.6%), scan rate,
 * and two-run FNV determinism of the C3 render.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../src/tonewheel.h"
#include "wav.h"

#define RATE 48000
#define SECS 6
#define FRAMES (RATE * SECS)

static const double TAU_D = 6.283185307179586;

static float dry_buf[FRAMES], v3_buf[FRAMES], c3_buf[FRAMES], naive_buf[FRAMES];

static void render_organ(float *dst, int frames, int vib_mode) {
    tw_organ o;
    tw_organ_init(&o, RATE);
    static const uint8_t reg[TW_DRAWBARS] = { 8, 8, 8, 8, 0, 0, 0, 0, 0 };
    tw_organ_set_registration(&o, reg);
    tw_organ_set_vibrato(&o, vib_mode);
    static const int chord[3] = { 60, 64, 67 };
    for (int i = 0; i < frames; i++) {
        if (i == RATE / 2)
            for (int k = 0; k < 3; k++) tw_organ_note(&o, chord[k], true, 100);
        if (i == frames - RATE)
            for (int k = 0; k < 3; k++) tw_organ_note(&o, chord[k], false, 0);
        dst[i] = tw_organ_tick(&o) * 0.125f;
    }
}

/* The wrong model (sec 9): an ideal, dispersionless delay line read with
 * linear interpolation, swept 0..0.85 ms triangularly at 6.867 Hz, mixed
 * dry+wet at the same 1:1 the C modes use. No lowpass edge, no moving
 * ripple, no AM ramp, no per-section dispersion. */
static void naive_chorus(float *dst, const float *src, int frames) {
    static float dl[4096];
    memset(dl, 0, sizeof dl);
    int w = 0;
    double ph = 0.0;
    for (int i = 0; i < frames; i++) {
        dl[w] = src[i];
        double tri = 1.0 - 2.0 * fabs(ph - 0.5);
        double rp = (double)w - tri * 0.85e-3 * RATE;
        int r0 = (int)floor(rp);
        double fr = rp - (double)r0;
        float s0 = dl[r0 & 4095], s1 = dl[(r0 + 1) & 4095];
        float wet = (float)((1.0 - fr) * (double)s0 + fr * (double)s1);
        dst[i] = 0.5f * (src[i] + wet);
        w = (w + 1) & 4095;
        ph += 412.0 / 60.0 / RATE;
        if (ph >= 1.0) ph -= 1.0;
    }
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

static float line_h[8192];

static void measure_line(double *peak_ms, double *edge_hz) {
    tw_scanner s;
    tw_scanner_init(&s, RATE);
    s.mode = TW_VIB_V1;
    for (int i = 0; i < 8192; i++) {
        (void)tw_scanner_tick(&s, 0.0f, i == 0 ? 1.0f : 0.0f);
        line_h[i] = s.vn[18];
    }
    int peak = 0;
    for (int i = 1; i < 8192; i++)
        if (fabsf(line_h[i]) > fabsf(line_h[peak])) peak = i;
    *peak_ms = 1000.0 * (double)peak / RATE;
    double ref = mag_at(line_h, 8192, 500.0);
    double edge = 0.0;
    for (double f = 4000.0; f <= 9000.0; f += 25.0)
        if (mag_at(line_h, 8192, f) > 0.5 * ref) edge = f;
    *edge_hz = edge;
}

/* V3 depth: 1 kHz tone through the swept line only; instantaneous
 * frequency from upward zero crossings (linear interp). Two figures:
 *
 * - cyclical depth, smoothed over 32 periods (32 ms ~ 3.5 plate legs,
 *   still under the 72.8 ms sweep half-cycle): the [P45]-comparable
 *   "cyclical shift ~1.5%" -- the sec 9 acceptance band 1.1-1.6%.
 * - peak deviation, smoothed over 8 periods (under one 9.1 ms leg):
 *   the cyclical sweep plus the moving standing-wave ripple the
 *   low-impedance V-mode drive sets up in the mismatched line -- the
 *   [DAFx16] "moving ripple" signature, reported informationally.
 */
static void measure_depth(double *cyc, double *peak) {
    tw_scanner s;
    tw_scanner_init(&s, RATE);
    s.mode = TW_VIB_V3;
    double ph = 0.0;
    float prev = 0.0f;
    for (int i = 0; i < RATE; i++) { /* settle line + a few sweeps */
        prev = tw_scanner_tick(&s, 0.0f, (float)sin(TAU_D * ph));
        ph += 1000.0 / RATE;
        if (ph >= 1.0) ph -= 1.0;
    }
    static double tz[8192];
    int nz = 0;
    for (int i = 0; i < 4 * RATE && nz < 8192; i++) {
        float y = tw_scanner_tick(&s, 0.0f, (float)sin(TAU_D * ph));
        ph += 1000.0 / RATE;
        if (ph >= 1.0) ph -= 1.0;
        if (prev <= 0.0f && y > 0.0f)
            tz[nz++] = (double)i + (double)prev / ((double)prev - (double)y);
        prev = y;
    }
    double dev8 = 0.0, dev32 = 0.0;
    for (int k = 0; k + 8 < nz; k++) {
        double f = 8.0 * RATE / (tz[k + 8] - tz[k]);
        if (fabs(f - 1000.0) > dev8) dev8 = fabs(f - 1000.0);
    }
    for (int k = 0; k + 32 < nz; k++) {
        double f = 32.0 * RATE / (tz[k + 32] - tz[k]);
        if (fabs(f - 1000.0) > dev32) dev32 = fabs(f - 1000.0);
    }
    *peak = 100.0 * dev8 / 1000.0;
    *cyc = 100.0 * dev32 / 1000.0;
}

static double measure_rate(void) {
    tw_scanner s;
    tw_scanner_init(&s, RATE);
    s.mode = TW_VIB_V1;
    int wraps = 0;
    float prev = s.phase;
    for (int i = 0; i < 10 * RATE; i++) {
        (void)tw_scanner_tick(&s, 0.0f, 0.0f);
        if (s.phase < prev) wraps++;
        prev = s.phase;
    }
    return ((double)wraps + (double)s.phase) / 10.0;
}

int main(void) {
    printf("tonewheel91 M4 exhibit -- vibrato/chorus scanner\n\n");

    double peak_ms, edge_hz;
    measure_line(&peak_ms, &edge_hz);
    printf("  line box (18-section ladder, v19, 48 kHz):\n");
    printf("    impulse peak: %.2f ms (idealized total delay ~0.85 ms)\n", peak_ms);
    printf("    passband edge (last -6 dB): %.0f Hz (analog ~7075 Hz;"
           " bilinear warp puts ~6.7 kHz at 48 kHz)\n", edge_hz);

    double cyc, peakdev;
    measure_depth(&cyc, &peakdev);
    printf("    V3 cyclical depth at 1 kHz: +/-%.2f%% (sec 9 acceptance"
           " 1.1-1.6%%; [P45] ~1.5%%)\n", cyc);
    printf("    V3 peak deviation (with moving ripple): +/-%.2f%%\n", peakdev);

    double rate = measure_rate();
    printf("    scan rate: %.3f Hz (412 rpm = 6.867 Hz)\n", rate);

    render_organ(dry_buf, FRAMES, TW_VIB_OFF);
    render_organ(v3_buf, FRAMES, TW_VIB_V3);
    render_organ(c3_buf, FRAMES, TW_VIB_C3);
    naive_chorus(naive_buf, dry_buf, FRAMES);

    /* two-run determinism on the C3 render */
    static float r2[FRAMES];
    render_organ(r2, FRAMES, TW_VIB_C3);
    uint64_t h1 = tw_fnv1a64(c3_buf, sizeof c3_buf, 0);
    uint64_t h2 = tw_fnv1a64(r2, sizeof r2, 0);
    printf("\n  scripted determinism (C3 chord): FNV64 %016llx %s\n",
           (unsigned long long)h1, h1 == h2 ? "(two runs identical)" : "MISMATCH");

    int rc = 0;
    rc |= wav_write_f32("build/m4_dry.wav", dry_buf, FRAMES, RATE, 1);
    rc |= wav_write_f32("build/m4_scanner_v3.wav", v3_buf, FRAMES, RATE, 1);
    rc |= wav_write_f32("build/m4_scanner_c3.wav", c3_buf, FRAMES, RATE, 1);
    rc |= wav_write_f32("build/m4_chorus_naive.wav", naive_buf, FRAMES, RATE, 1);
    printf("\n  wavs: build/m4_dry.wav, m4_scanner_v3.wav, m4_scanner_c3.wav,\n"
           "        m4_chorus_naive.wav (A/B: c3 vs naive)%s\n",
           rc ? " (WRITE FAILED)" : "");

    int verdict = peak_ms > 0.6 && peak_ms < 1.3
               && edge_hz > 6200.0 && edge_hz < 7200.0
               && cyc > 1.1 && cyc < 1.6
               && peakdev > cyc && peakdev < 2.3
               && rate > 6.82 && rate < 6.92
               && h1 == h2 && rc == 0;
    printf("\n  exhibit verdict: %s\n", verdict ? "PASS" : "FAIL");
    return verdict ? 0 : 1;
}
