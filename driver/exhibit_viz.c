/* tonewheel91 viz exhibit: three pictures of engine state the WAVs
 * cannot carry. Same contract as the other exhibits — every panel is
 * rendered twice and hashed, every claim is a measured number, and the
 * verdict fails on any drift. The images are presentation; the numbers
 * printed beside them are the evidence, so a panel's verdict never
 * depends on how it was shaded.
 *
 * 1. Wheel roll (docs/viz/wheel-roll.png). The 91-wheel bank against
 *    time through a chromatic walk up the whole 61-key compass at a
 *    full registration, percussion on, at the shipped wear. Green is
 *    the keyed bank, red the percussion envelope, blue the static
 *    bleed-bus weights. Foldback shows as the walk hitting the wheel-91
 *    ceiling and folding back down; the blue stripes run in the
 *    generator's bin order, not the musical one (design.md: leakage is
 *    structured, not uniform).
 *
 * 2. Drive hysteresis (docs/viz/drive-hysteresis.png). Input against
 *    output for the preamp stage at drive 0.8, plotted as a trajectory.
 *    A memoryless waveshaper would trace the grey reference curve and
 *    nothing else; the bias follower and the coupling cap open it into
 *    a loop whose width is frequency-dependent — the picture that
 *    separates this stage from a shaper (design.md model-depth
 *    doctrine).
 *
 * 3. Rotor telemetry (docs/viz/rotor-telemetry.png). Horn and drum
 *    rate through chorale -> tremolo -> chorale -> brake. The rise and
 *    fall times are the constants.md sec 15.1 slip lags made visible:
 *    the drum's sourced ~6.5 s fall against the horn's ~1 s rise.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../src/tonewheel.h"
#include "viz.h"

#define RATE 48000

/* --- 1. wheel roll ------------------------------------------------- */

enum {
    ROLL_W   = 720,
    ROLL_PX  = 4,                     /* pixel rows per wheel          */
    ROLL_H   = TW_WHEELS * ROLL_PX,
    ROLL_KEY = 4320,                  /* 90 ms a key; held for half of
                                       * it, so the 45 ms gap clears
                                       * the percussion re-arm RC      */
    ROLL_COL = 366,                   /* samples a column: 61 x 4320
                                       * = 720 x 366 exactly           */
};
static unsigned char roll_img[(size_t)ROLL_W * ROLL_H * 3];

/* Gain -> 8-bit intensity for one channel of the wheel roll. The banks
 * span the shipped wear's leakage floor (order 1e-4) up to a full
 * registration piling onto one wheel (order 1e1), so a linear map shows
 * the loudest notes and leaves everything else black. */
static unsigned char roll_shade(float gain) {
    if (!(gain > 0.0f)) return 0; /* NaN and the silent banks */
    /* Linear in dB against a reference fixed in the source, not fitted per
     * image: two rolls only mean anything side by side (wear 0 against
     * wear 1, say) if the same gain shades the same grey in both. REF is
     * the loudest a single wheel gets under a full registration once taper
     * and robbing have had their say; the floor clears the shipped wear's
     * quietest bleed weight, which is what makes the blue layer a layer
     * and not a black band. Measured re REF: keyed peak -2.2 dB,
     * percussion peak -11.9, bleed weights -72.5..-82.1. */
    constexpr float REF = 4.0f;
    constexpr float FLOOR_DB = -96.0f;
    float db = 20.0f * log10f(gain / REF);
    if (db <= FLOOR_DB) return 0;
    float t = (db - FLOOR_DB) / -FLOOR_DB;
    if (t > 1.0f) t = 1.0f;
    t *= sqrtf(t); /* gamma 1.5: the bleed layer stays a layer, not a wash */
    return (unsigned char)(255.0f * t + 0.5f);
}

static void roll_render(uint64_t *hash, int *active, int *top, int *bottom,
                        int *perc_wheels, int *leak_wheels) {
    memset(roll_img, 0, sizeof roll_img);

    tw_organ o;
    tw_organ_init(&o, RATE);
    static const uint8_t full[TW_DRAWBARS] = { 8, 8, 8, 8, 8, 8, 8, 8, 8 };
    tw_organ_set_registration(&o, full);
    tw_organ_set_percussion(&o, true, false, false, true); /* on, 2nd, fast */

    float seen_k[TW_WHEELS] = { 0 }, seen_p[TW_WHEELS] = { 0 };
    float col_k[TW_WHEELS] = { 0 }, col_p[TW_WHEELS] = { 0 };

    for (int x = 0; x < ROLL_W; x++) {
        for (int i = 0; i < TW_WHEELS; i++) col_k[i] = col_p[i] = 0.0f;

        for (int s = 0; s < ROLL_COL; s++) {
            long n = (long)x * ROLL_COL + s;
            long seg = n % ROLL_KEY;
            int key = (int)(n / ROLL_KEY) + 1;
            if (seg == 0) tw_organ_note(&o, key + 35, true, 127);
            if (seg == ROLL_KEY / 2) tw_organ_note(&o, key + 35, false, 0);
            (void)tw_organ_tick(&o);

            /* Peak-hold inside the column: a percussion transient is
             * shorter than the 7.6 ms a column spans. */
            for (int i = 0; i < TW_WHEELS; i++) {
                float k = o.gen.keyed_gain[i] * o.gen.level[i];
                float p = o.gen.perc_gain[i] * o.gen.level[i];
                if (k > col_k[i]) col_k[i] = k;
                if (p > col_p[i]) col_p[i] = p;
            }
        }

        for (int i = 0; i < TW_WHEELS; i++) {
            if (col_k[i] > seen_k[i]) seen_k[i] = col_k[i];
            if (col_p[i] > seen_p[i]) seen_p[i] = col_p[i];
            uint32_t c = ((uint32_t)roll_shade(col_p[i]) << 16)
                       | ((uint32_t)roll_shade(col_k[i]) << 8)
                       | (uint32_t)roll_shade(o.gen.leak_gain[i] * o.gen.level[i]);
            int y0 = (TW_WHEELS - 1 - i) * ROLL_PX; /* wheel 91 on top */
            for (int dy = 0; dy < ROLL_PX; dy++)
                viz_px(roll_img, ROLL_W, ROLL_H, x, y0 + dy, c);
        }
    }

    *active = *top = *perc_wheels = *leak_wheels = 0;
    *bottom = TW_WHEELS + 1;
    for (int i = 0; i < TW_WHEELS; i++) {
        if (seen_k[i] > 1e-3f) {
            (*active)++;
            if (i + 1 > *top) *top = i + 1;
            if (i + 1 < *bottom) *bottom = i + 1;
        }
        if (seen_p[i] > 1e-3f) (*perc_wheels)++;
        if (o.gen.leak_gain[i] != 0.0f) (*leak_wheels)++;
    }
    *hash = tw_fnv1a64(roll_img, sizeof roll_img, 0);
}

/* --- 2. drive hysteresis ------------------------------------------- */

enum { DRV_W = 512 };
static unsigned char drv_img[(size_t)DRV_W * DRV_W * 3];

static constexpr float DRV_KNOB = 0.8f;
static constexpr float DRV_AMP  = 10.0f;
static constexpr float DRV_XMAX = 12.0f;
static constexpr float DRV_YMAX = 6.0f;

static int drv_x(float x) {
    return (int)((0.5f + 0.5f * x / DRV_XMAX) * (DRV_W - 1) + 0.5f);
}

static int drv_y(float y) {
    return (int)((0.5f - 0.5f * y / DRV_YMAX) * (DRV_W - 1) + 0.5f);
}

/* One frequency through the stage: settle past the 50 ms follower
 * release and the 10 Hz coupling cap, then plot a full cycle as an
 * (input, output) trajectory. Returns the largest vertical departure
 * from the memoryless reference over that cycle. */
static float drv_trace(float f_hz, uint32_t colour) {
    tw_drive d;
    tw_drive_init(&d, RATE);
    tw_drive_set(&d, DRV_KNOB);

    float step = f_hz / RATE, ph = 0.0f;
    for (long i = 0; i < RATE / 2; i++) {
        (void)tw_drive_tick(&d, DRV_AMP * tw_sin_turns(ph));
        ph += step;
        ph -= (ph >= 1.0f) ? 1.0f : 0.0f;
    }

    long n = (long)(1.0f / step);
    int px = -1, py = -1;
    float gap = 0.0f;
    for (long i = 0; i <= n; i++) {
        float x = DRV_AMP * tw_sin_turns(ph);
        float y = tw_drive_tick(&d, x);
        float g = tw_fabsf(y - d.post * tw_drive_curve(d.pre * x));
        if (g > gap) gap = g;
        int qx = drv_x(x), qy = drv_y(y);
        if (px >= 0) viz_line(drv_img, DRV_W, DRV_W, px, py, qx, qy, colour);
        px = qx;
        py = qy;
        ph += step;
        ph -= (ph >= 1.0f) ? 1.0f : 0.0f;
    }
    return gap;
}

static void drv_render(uint64_t *hash, float *gap_lo, float *gap_hi) {
    memset(drv_img, 0x0d, sizeof drv_img);

    viz_line(drv_img, DRV_W, DRV_W, 0, drv_y(0.0f), DRV_W - 1, drv_y(0.0f), 0x303030);
    viz_line(drv_img, DRV_W, DRV_W, drv_x(0.0f), 0, drv_x(0.0f), DRV_W - 1, 0x303030);

    /* The memoryless reference: what a bare waveshaper at this drive
     * would trace. Read pre/post off the stage rather than recomputing
     * the knob law here. */
    tw_drive ref;
    tw_drive_init(&ref, RATE);
    tw_drive_set(&ref, DRV_KNOB);
    for (int x = 1; x < DRV_W; x++) {
        float x0 = DRV_XMAX * (2.0f * (float)(x - 1) / (DRV_W - 1) - 1.0f);
        float x1 = DRV_XMAX * (2.0f * (float)x / (DRV_W - 1) - 1.0f);
        viz_line(drv_img, DRV_W, DRV_W,
                 drv_x(x0), drv_y(ref.post * tw_drive_curve(ref.pre * x0)),
                 drv_x(x1), drv_y(ref.post * tw_drive_curve(ref.pre * x1)),
                 0x9a9a9a);
    }

    *gap_lo = drv_trace(55.0f, 0x46c8ff);
    *gap_hi = drv_trace(440.0f, 0xff9a3c);
    *hash = tw_fnv1a64(drv_img, sizeof drv_img, 0);
}

/* --- 3. rotor telemetry -------------------------------------------- */

enum {
    ROT_W   = 720,
    ROT_H   = 320,
    ROT_COL = 2400, /* 720 x 2400 = 36 s at 48 kHz */
};
static unsigned char rot_img[(size_t)ROT_W * ROT_H * 3];

static constexpr float ROT_MAXHZ = 7.0f;

static const struct { long sec; int mode; } ROT_SCRIPT[] = {
    {  0, TW_ROT_CHORALE },
    {  3, TW_ROT_TREMOLO },
    { 13, TW_ROT_CHORALE },
    { 26, TW_ROT_BRAKE   },
};

static int rot_y(float hz) {
    return (ROT_H - 1) - (int)(hz / ROT_MAXHZ * (ROT_H - 1) + 0.5f);
}

static void rot_render(uint64_t *hash, double *horn_rise, double *drum_rise,
                       double *drum_fall) {
    memset(rot_img, 0x0d, sizeof rot_img);
    for (int hz = 1; hz < (int)ROT_MAXHZ; hz++)
        viz_line(rot_img, ROT_W, ROT_H, 0, rot_y((float)hz),
                 ROT_W - 1, rot_y((float)hz), 0x242424);
    for (size_t e = 1; e < sizeof ROT_SCRIPT / sizeof *ROT_SCRIPT; e++) {
        int x = (int)(ROT_SCRIPT[e].sec * RATE / ROT_COL);
        viz_line(rot_img, ROT_W, ROT_H, x, 0, x, ROT_H - 1, 0x3c3c3c);
    }

    tw_rotary r;
    tw_rotary_init(&r, RATE);
    tw_rotary_set_drive(&r, 0.3f);

    /* Something to spin: the rate curves do not depend on it, but a
     * silent cabinet is not the state the picture claims to show. */
    float ph = 0.0f, step = 220.0f / RATE;

    long hr_at = -1, dr_at = -1, df_at = -1;
    int px_h = -1, py_h = -1, px_d = -1, py_d = -1;
    size_t next = 0;

    for (int x = 0; x < ROT_W; x++) {
        for (int s = 0; s < ROT_COL; s++) {
            long n = (long)x * ROT_COL + s;
            if (next < sizeof ROT_SCRIPT / sizeof *ROT_SCRIPT
                && n == ROT_SCRIPT[next].sec * RATE)
                tw_rotary_set_mode(&r, ROT_SCRIPT[next++].mode);

            (void)tw_rotary_tick(&r, 4.0f * tw_sin_turns(ph));
            ph += step;
            ph -= (ph >= 1.0f) ? 1.0f : 0.0f;

            /* 95 % of each step is three time constants of the slip lag. */
            float hz_h = r.horn_target + r.horn_dev;
            float hz_d = r.drum_target + r.drum_dev;
            if (hr_at < 0 && n > 3 * RATE && hz_h >= 6.3733f) hr_at = n - 3 * RATE;
            if (dr_at < 0 && n > 3 * RATE && hz_d >= 5.4167f) dr_at = n - 3 * RATE;
            if (df_at < 0 && n > 13 * RATE && hz_d <= 0.9167f) df_at = n - 13 * RATE;
        }
        int qy_h = rot_y(r.horn_target + r.horn_dev);
        int qy_d = rot_y(r.drum_target + r.drum_dev);
        if (px_h >= 0) {
            viz_line(rot_img, ROT_W, ROT_H, px_d, py_d, x, qy_d, 0xff6464);
            viz_line(rot_img, ROT_W, ROT_H, px_h, py_h, x, qy_h, 0x50ff96);
        }
        px_h = x; py_h = qy_h;
        px_d = x; py_d = qy_d;
    }

    *horn_rise = (double)hr_at / RATE;
    *drum_rise = (double)dr_at / RATE;
    *drum_fall = (double)df_at / RATE;
    *hash = tw_fnv1a64(rot_img, sizeof rot_img, 0);
}

/* --- panels -------------------------------------------------------- */

static int panel_roll(void) {
    uint64_t h1 = 0, h2 = 0;
    int active = 0, top = 0, bottom = 0, perc = 0, leak = 0;
    roll_render(&h1, &active, &top, &bottom, &perc, &leak);
    roll_render(&h2, &active, &top, &bottom, &perc, &leak);

    printf("  wheel roll -- 61-key chromatic walk, registration 888888888,"
           " percussion on, wear %.2f\n", (double)TW_WEAR_DEFAULT);
    printf("    keyed wheels %d, spanning %d..%d (foldback ceiling %d);"
           " percussion touched %d\n", active, bottom, top, TW_WHEEL_MAX, perc);
    printf("    bleed-bus weights nonzero on %d of %d wheels\n", leak, TW_WHEELS);
    printf("    %dx%d, FNV64 %016llx %s\n", ROLL_W, ROLL_H,
           (unsigned long long)h1, h1 == h2 ? "(two runs identical)" : "MISMATCH");

    int rc = viz_write_png("docs/viz/wheel-roll.png", roll_img, ROLL_W, ROLL_H);
    printf("    png: docs/viz/wheel-roll.png%s\n", rc ? " (WRITE FAILED)" : "");

    return h1 == h2 && rc == 0
        && active == 79 && bottom == TW_WHEEL_MIN && top == TW_WHEEL_MAX
        && perc == 61 && leak == TW_WHEELS;
}

static int panel_drive(void) {
    uint64_t h1 = 0, h2 = 0;
    float lo = 0.0f, hi = 0.0f;
    drv_render(&h1, &lo, &hi);
    drv_render(&h2, &lo, &hi);

    printf("\n  drive hysteresis -- drive %.2f, input amplitude %.1f"
           " (X_ref = 8)\n", (double)DRV_KNOB, (double)DRV_AMP);
    printf("    departure from the memoryless curve: %.3f at 55 Hz,"
           " %.3f at 440 Hz\n", (double)lo, (double)hi);
    printf("    %dx%d, FNV64 %016llx %s\n", DRV_W, DRV_W,
           (unsigned long long)h1, h1 == h2 ? "(two runs identical)" : "MISMATCH");

    int rc = viz_write_png("docs/viz/drive-hysteresis.png", drv_img, DRV_W, DRV_W);
    printf("    png: docs/viz/drive-hysteresis.png%s\n", rc ? " (WRITE FAILED)" : "");

    /* A memoryless stage would score 0 at both; the loop widths are what
     * the bias follower and the coupling cap are worth. */
    return h1 == h2 && rc == 0
        && lo > 2.20f && lo < 2.31f && hi > 1.36f && hi < 1.46f;
}

static int panel_rotor(void) {
    uint64_t h1 = 0, h2 = 0;
    double hr = 0.0, dr = 0.0, df = 0.0;
    rot_render(&h1, &hr, &dr, &df);
    rot_render(&h2, &hr, &dr, &df);

    printf("\n  rotor telemetry -- chorale, tremolo at 3 s, chorale at 13 s,"
           " brake at 26 s\n");
    printf("    to 95%% of the step: horn rise %.2f s (3 tau = 1.00),"
           " drum rise %.2f s (2.50), drum fall %.2f s (6.50)\n", hr, dr, df);
    printf("    %dx%d, FNV64 %016llx %s\n", ROT_W, ROT_H,
           (unsigned long long)h1, h1 == h2 ? "(two runs identical)" : "MISMATCH");

    int rc = viz_write_png("docs/viz/rotor-telemetry.png", rot_img, ROT_W, ROT_H);
    printf("    png: docs/viz/rotor-telemetry.png%s\n", rc ? " (WRITE FAILED)" : "");

    return h1 == h2 && rc == 0
        && hr > 0.98 && hr < 1.02 && dr > 2.48 && dr < 2.52
        && df > 6.47 && df < 6.53;
}

int main(void) {
    printf("tonewheel91 viz exhibit -- engine state as pictures\n\n");
    int ok = panel_roll();
    ok &= panel_drive();
    ok &= panel_rotor();
    printf("\n  exhibit verdict: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
