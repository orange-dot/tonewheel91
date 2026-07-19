/* tonewheel91 M7 exhibit -- wear, the structured deviations.
 *
 * One A/B/C on one sustained chord: the idealized reference (wear 0),
 * the shipped default (0.2), and full character (1.0) -- per-wheel
 * level spread, tooth-profile harmonics, motion AM ("shimmer"), pickup
 * asymmetry, the bin/shaft bleed floor, and the 60 Hz line, all behind
 * the one knob (constants.md sec 11.1/12.1/13.1). Plus the sec 13
 * evidence render: the idle-organ noise floor itself.
 *
 * The identity anchor: the M6 transition passage re-rendered at wear 0
 * must reproduce the FNV-64 recorded in docs/m6-evidence.md
 * (00eb4c9bf80cb408) bit-for-bit -- wear = 0 IS the pre-M7 instrument.
 *
 * Caveat carried into the evidence doc: every depth here is a [FOLK]
 * or [decision] working value (only the pickup's alpha = 0.3 has a
 * measured source [AS16]); the by-ear verdict against reference
 * recordings overrides all of them, and the shipped default 0.2 most
 * of all.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../src/tonewheel.h"
#include "wav.h"

#define RATE 48000
#define AB_SECS 6
#define TR_SECS 16
#define AB_FRAMES (RATE * AB_SECS)
#define TR_FRAMES (RATE * TR_SECS)

static const double TAU_D = 6.283185307179586;

static float ab_off[AB_FRAMES], ab_def[AB_FRAMES], ab_full[AB_FRAMES];
static float idle_buf[2 * AB_FRAMES];
static float tr_buf[2 * TR_FRAMES];

static double mag_at(const float *h, long n, double f) {
    double re = 0.0, im = 0.0;
    for (long i = 0; i < n; i++) {
        double a = TAU_D * f * (double)i / RATE;
        re += (double)h[i] * cos(a);
        im -= (double)h[i] * sin(a);
    }
    return sqrt(re * re + im * im);
}

/* The M6 exhibit's transition passage, verbatim (chorale -> tremolo at
 * 5 s -> chorale at 10 s), at an explicit wear. */
static void render_transition(float *dst, int frames, float wear) {
    tw_instrument ins;
    tw_instrument_init(&ins, RATE);
    tw_organ_set_wear(&ins.organ, wear);
    static const uint8_t reg[TW_DRAWBARS] = { 8, 8, 8, 8, 0, 0, 0, 0, 0 };
    tw_organ_set_registration(&ins.organ, reg);
    tw_instrument_set_drive(&ins, 0.4f);
    tw_rotary_set_mode(&ins.rotary, TW_ROT_CHORALE);
    static const int chord[3] = { 60, 64, 67 };
    for (int i = 0; i < frames; i++) {
        if (i == RATE / 2)
            for (int k = 0; k < 3; k++) tw_organ_note(&ins.organ, chord[k], true, 100);
        if (i == 5 * RATE) tw_rotary_set_mode(&ins.rotary, TW_ROT_TREMOLO);
        if (i == 10 * RATE) tw_rotary_set_mode(&ins.rotary, TW_ROT_CHORALE);
        if (i == frames - RATE)
            for (int k = 0; k < 3; k++) tw_organ_note(&ins.organ, chord[k], false, 0);
        tw_stereo y = tw_instrument_tick_stereo(&ins);
        dst[2 * i] = y.l;
        dst[2 * i + 1] = y.r;
    }
}

/* The A/B chord: bare organ (no drive, no rotary) so the wear colour
 * is the only variable; a 1.5 s tail leaves the floor audible. */
static void render_chord(float *dst, int frames, float wear) {
    tw_organ o;
    tw_organ_init(&o, RATE);
    tw_organ_set_wear(&o, wear);
    static const uint8_t reg[TW_DRAWBARS] = { 8, 8, 8, 8, 0, 0, 0, 0, 0 };
    tw_organ_set_registration(&o, reg);
    static const int chord[3] = { 60, 64, 67 };
    for (int i = 0; i < frames; i++) {
        if (i == RATE / 2)
            for (int k = 0; k < 3; k++) tw_organ_note(&o, chord[k], true, 100);
        if (i == frames - 3 * RATE / 2)
            for (int k = 0; k < 3; k++) tw_organ_note(&o, chord[k], false, 0);
        dst[i] = tw_organ_tick(&o);
    }
}

/* One wheel alone through a worn generator; returns the keyed capture. */
static float wheel_buf[48000];
static void render_wheel(int wheel, float wear) {
    tw_generator g;
    tw_generator_init(&g, RATE, 0.001f);
    tw_generator_set_wear(&g, wear);
    float t[TW_WHEELS] = { 0 };
    t[wheel - 1] = 1.0f;
    tw_generator_set_keyed_targets(&g, t);
    for (int i = 0; i < 4800; i++) (void)tw_generator_tick(&g);
    for (int i = 0; i < 48000; i++) wheel_buf[i] = tw_generator_tick(&g).keyed;
}

int main(void) {
    printf("tonewheel91 M7 exhibit -- wear, the structured deviations\n\n");

    /* the identity anchor: the M6 transition passage at wear 0 must
     * carry the docs/m6-evidence.md signature bit-for-bit */
    render_transition(tr_buf, TR_FRAMES, 0.0f);
    uint64_t h_id = tw_fnv1a64(tr_buf, sizeof tr_buf, 0);
    const uint64_t pre_m7 = 0x00eb4c9bf80cb408u;
    printf("  identity: M6 transition at wear 0: FNV64 %016llx %s\n",
           (unsigned long long)h_id,
           h_id == pre_m7 ? "== the m6-evidence baseline" : "MISMATCH");

    /* ...and the shipped default is its own render, deterministically */
    render_transition(tr_buf, TR_FRAMES, TW_WEAR_DEFAULT);
    uint64_t h_d1 = tw_fnv1a64(tr_buf, sizeof tr_buf, 0);
    render_transition(tr_buf, TR_FRAMES, TW_WEAR_DEFAULT);
    uint64_t h_d2 = tw_fnv1a64(tr_buf, sizeof tr_buf, 0);
    printf("  default (%.2f) transition: FNV64 %016llx %s\n",
           (double)TW_WEAR_DEFAULT, (unsigned long long)h_d1,
           h_d1 == h_d2 ? "(two runs identical)" : "MISMATCH");

    /* the sec 12.1 wheel character, measured at wear 1 */
    double f13 = tw_wheel_freq_hz(13), f73 = tw_wheel_freq_hz(73);
    render_wheel(13, 1.0f);
    double t_h1 = mag_at(wheel_buf, 48000, f13);
    double t_h2 = mag_at(wheel_buf, 48000, 2.0 * f13);
    double t_h3 = mag_at(wheel_buf, 48000, 3.0 * f13);
    render_wheel(73, 1.0f);
    double p_h1 = mag_at(wheel_buf, 48000, f73);
    double p_h2 = mag_at(wheel_buf, 48000, 2.0 * f73);
    printf("  wheel 13 (4 teeth): H2/H1 %.4f (alpha/4 + the 0.015 tooth\n"
           "    term in quadrature), H3/H1 %.4f (the 0.03 tooth term net\n"
           "    of the pickup cubic)\n", t_h2 / t_h1, t_h3 / t_h1);
    printf("  pickup alpha, wheel 73 (toothing ~1e-3 there): H2/H1 %.4f"
           " (alpha/4 says ~0.074)\n", p_h2 / p_h1);

    /* motion AM, wheel 46: envelope ripple at its 27.5 Hz rotation */
    render_wheel(46, 1.0f);
    static double env[48000];
    int win = 218; /* ~2 cycles of 440 Hz */
    double si = 0.0, sq = 0.0;
    static double di[48000], dq[48000];
    for (int i = 0; i < 48000; i++) {
        double w = TAU_D * 440.0 * (double)i / RATE;
        di[i] = (double)wheel_buf[i] * cos(w);
        dq[i] = -(double)wheel_buf[i] * sin(w);
    }
    for (int i = 0; i < win; i++) { si += di[i]; sq += dq[i]; }
    int n_env = 0;
    for (int j = 0; j + win + 1 < 48000; j++) {
        env[n_env++] = 2.0 * sqrt(si * si + sq * sq) / win;
        si += di[j + win] - di[j];
        sq += dq[j + win] - dq[j];
    }
    double mean = 0.0;
    int j0 = 500, j1 = n_env - 500;
    for (int j = j0; j < j1; j++) mean += env[j];
    mean /= (double)(j1 - j0);
    double re = 0.0, im = 0.0;
    for (int j = j0; j < j1; j++) {
        double w = TAU_D * 27.5 * (double)(j - j0) / RATE;
        re += (env[j] - mean) * cos(w);
        im -= (env[j] - mean) * sin(w);
    }
    double am_d = 2.0 * sqrt(re * re + im * im) / (mean * (double)(j1 - j0));
    printf("  motion AM, wheel 46: depth %.4f at 27.5 rev/s"
           " (draw x 0.05 max)\n", am_d);

    /* the sec 13.1 floor: an idle organ at wear 1 -- bleed classes,
     * the 60 Hz line, and the aggregate rms */
    tw_organ o;
    tw_organ_init(&o, RATE);
    tw_organ_set_wear(&o, 1.0f);
    for (int i = 0; i < 4800; i++) (void)tw_organ_tick(&o);
    static float floor1[4 * RATE];
    double frms = 0.0;
    for (int i = 0; i < 4 * RATE; i++) {
        floor1[i] = tw_organ_tick(&o);
        frms += (double)floor1[i] * floor1[i];
    }
    frms = sqrt(frms / (4.0 * RATE));
    double l20 = mag_at(floor1, 4 * RATE, tw_wheel_freq_hz(20)) / (2.0 * RATE);
    double l39 = mag_at(floor1, 4 * RATE, tw_wheel_freq_hz(39)) / (2.0 * RATE);
    double l60 = mag_at(floor1, 4 * RATE, 60.0) / (2.0 * RATE);
    printf("  idle floor at wear 1: rms %.4f (%.1f dB; sec 13.1 aims"
           " ~-30)\n", frms, 20.0 * log10(frms));
    printf("    bleed lines: wheel 20 (full bin) %.2e vs wheel 39"
           " (blank partner) %.2e -- the bin layout, audible\n", l20, l39);
    printf("    mains hum: 60 Hz line %.2e (pinned 1e-3 at wear 1)\n", l60);
    double def_rms = 0.0;
    tw_organ_init(&o, RATE); /* ships at the default */
    for (int i = 0; i < 4800; i++) (void)tw_organ_tick(&o);
    for (int i = 0; i < 2 * RATE; i++) {
        float y = tw_organ_tick(&o);
        def_rms += (double)y * y;
    }
    def_rms = sqrt(def_rms / (2.0 * RATE));
    printf("  idle floor at the shipped default: rms %.5f (%.1f dB)\n",
           def_rms, 20.0 * log10(def_rms));

    /* the A/B/C renders + the idle-floor evidence render */
    render_chord(ab_off, AB_FRAMES, 0.0f);
    render_chord(ab_def, AB_FRAMES, TW_WEAR_DEFAULT);
    render_chord(ab_full, AB_FRAMES, 1.0f);
    tw_organ_init(&o, RATE);
    tw_organ_set_wear(&o, 1.0f);
    for (int i = 0; i < 2 * AB_FRAMES; i++) idle_buf[i] = tw_organ_tick(&o);

    /* exhibit scale: the repo's 1/8 headroom convention; the idle floor
     * instead gets +24 dB makeup (x16) or it would sit near -48 dBFS */
    for (long i = 0; i < AB_FRAMES; i++) {
        ab_off[i] *= 0.125f;
        ab_def[i] *= 0.125f;
        ab_full[i] *= 0.125f;
    }
    for (long i = 0; i < 2L * AB_FRAMES; i++) idle_buf[i] *= 16.0f;
    int rc = 0;
    rc |= wav_write_f32("build/m7_wear_off.wav", ab_off, AB_FRAMES, RATE, 1);
    rc |= wav_write_f32("build/m7_wear_default.wav", ab_def, AB_FRAMES, RATE, 1);
    rc |= wav_write_f32("build/m7_wear_full.wav", ab_full, AB_FRAMES, RATE, 1);
    rc |= wav_write_f32("build/m7_idle_floor.wav", idle_buf, 2L * AB_FRAMES, RATE, 1);
    printf("\n  wavs: build/m7_wear_off.wav / m7_wear_default.wav /\n"
           "        m7_wear_full.wav (one chord, wear 0 / 0.2 / 1: level\n"
           "        spread, toothing, shimmer, pickup asymmetry, floor in\n"
           "        the tail), m7_idle_floor.wav (the sec 13 idle-organ\n"
           "        noise-floor render at wear 1, +24 dB makeup)%s\n",
           rc ? " (WRITE FAILED)" : "");

    int verdict = h_id == pre_m7 && h_d1 == h_d2 && h_d1 != h_id
                && t_h3 / t_h1 > 0.018 && t_h3 / t_h1 < 0.042
                && t_h2 / t_h1 > 0.0075
                && p_h2 / p_h1 > 0.052 && p_h2 / p_h1 < 0.096
                && am_d > 0.005 && am_d < 0.05
                && frms > 0.015 && frms < 0.06
                && l20 > 2.0 * l39
                && l60 > 0.6e-3 && l60 < 1.4e-3
                && def_rms > 1e-3 && def_rms < 2e-2
                && rc == 0;
    printf("\n  exhibit verdict: %s\n", verdict ? "PASS" : "FAIL");
    return verdict ? 0 : 1;
}
