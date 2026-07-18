/* tonewheel91 M6 exhibit -- the rotary speaker.
 *
 * Three A/B pairs on one organ passage: chorale vs tremolo (the two
 * [FOLK] speed plateaus), a chorale -> tremolo -> chorale transition
 * (the horn's ~1 s rise against the drum's [RS]-pinned 5-8 s fall,
 * audibly different clocks), and horn-FM vs drum-AM (the [HX] pin: the
 * high side swings pitch through the rotating horn, the low side only
 * breathes in level -- no Doppler on the drum).
 *
 * Measurements (printed, recorded in docs/m6-evidence.md): crossover
 * magnitudes at the 800 Hz split and the recombined flatness, rotor
 * rise/fall time constants and the brake's front park, horn pitch
 * swing and AM depth, drum pitch residual and its band-limited AM,
 * bypass identity against the mono chain, and two-run FNV determinism
 * of the transition render.
 *
 * Caveat carried into the evidence doc: every rotor speed here is
 * [FOLK] -- community folklore adopted openly as a working default, no
 * primary source in hand (constants.md sec 15) -- and the AM depths and
 * Doppler swing are [decision] working values. The by-ear verdict
 * against reference recordings overrides all of them.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../src/tonewheel.h"
#include "wav.h"

#define RATE 48000
#define AB_SECS 8
#define TR_SECS 16
#define AB_FRAMES (RATE * AB_SECS)
#define TR_FRAMES (RATE * TR_SECS)

static const double TAU_D = 6.283185307179586;

static float ab_a[2 * AB_FRAMES], ab_b[2 * AB_FRAMES];
static float tr_buf[2 * TR_FRAMES];
static float fm_a[2 * AB_FRAMES], fm_b[2 * AB_FRAMES];

/* --- measurement helpers (libm is the driver-layer oracle) --- */

static float mh[8192];
static double mag_at(const float *h, int n, double f) {
    double re = 0.0, im = 0.0;
    for (int i = 0; i < n; i++) {
        double a = TAU_D * f * (double)i / RATE;
        re += (double)h[i] * cos(a);
        im -= (double)h[i] * sin(a);
    }
    return sqrt(re * re + im * im);
}

/* Both rotors locked at poked rates: targets and deviations overridden. */
static void rotary_lock(tw_rotary *r, float horn_hz, float drum_hz) {
    tw_rotary_init(r, RATE);
    tw_rotary_set_mode(r, TW_ROT_TREMOLO);
    r->horn_target = horn_hz;
    r->drum_target = drum_hz;
    r->horn_dev = 0.0f;
    r->drum_dev = 0.0f;
}

/* Quadrature demod with a win-stride phase slope (the test.c method:
 * per-sample differentiation amplifies the leaked 2f image). */
static double dm_i[48000], dm_q[48000], dm_ph[48000];

static void demod_span(const float *x, int n, double f, int win,
                       double *env_lo, double *env_hi,
                       double *frq_lo, double *frq_hi) {
    for (int i = 0; i < n; i++) {
        double w = TAU_D * f * (double)i / RATE;
        dm_i[i] = (double)x[i] * cos(w);
        dm_q[i] = -(double)x[i] * sin(w);
    }
    double si = 0.0, sq = 0.0;
    for (int i = 0; i < win; i++) {
        si += dm_i[i];
        sq += dm_q[i];
    }
    int out = 0;
    static double env[48000];
    for (int j = 0; j + win + 1 < n; j++) {
        env[out] = 2.0 * sqrt(si * si + sq * sq) / win;
        dm_ph[out] = atan2(sq, si);
        out++;
        si += dm_i[j + win] - dm_i[j];
        sq += dm_q[j + win] - dm_q[j];
    }
    *env_lo = 1e9;
    *env_hi = -1e9;
    *frq_lo = 1e9;
    *frq_hi = -1e9;
    for (int j = 500; j < out - 500; j++) {
        if (env[j] < *env_lo) *env_lo = env[j];
        if (env[j] > *env_hi) *env_hi = env[j];
        if (j < win) continue;
        double d = dm_ph[j] - dm_ph[j - win];
        d -= (d > TAU_D / 2.0) ? TAU_D : 0.0;
        d += (d < -TAU_D / 2.0) ? TAU_D : 0.0;
        double fi = f + d * RATE / (TAU_D * (double)win);
        if (fi < *frq_lo) *frq_lo = fi;
        if (fi > *frq_hi) *frq_hi = fi;
    }
}

/* Run a locked-rotor tone through one channel; returns the L capture. */
static float tone_l[43200];
static void tone_capture(float horn_hz, float drum_hz, float balance, double f) {
    tw_rotary r;
    rotary_lock(&r, horn_hz, drum_hz);
    tw_rotary_set_balance(&r, balance);
    for (int i = 0; i < 43200; i++) {
        float x = (float)sin(TAU_D * f * (double)i / RATE);
        tw_stereo y = tw_rotary_tick(&r, x);
        tone_l[i] = y.l;
    }
}

/* --- the shared organ passage: mid chord, drive 0.4, vibrato off so
 * the rotary carries all the motion --- */
static void render_passage(float *dst, int frames, int rot_mode,
                           int trem_at, int chor_at) {
    tw_instrument ins;
    tw_instrument_init(&ins, RATE);
    static const uint8_t reg[TW_DRAWBARS] = { 8, 8, 8, 8, 0, 0, 0, 0, 0 };
    tw_organ_set_registration(&ins.organ, reg);
    tw_instrument_set_drive(&ins, 0.4f);
    tw_rotary_set_mode(&ins.rotary, rot_mode);
    static const int chord[3] = { 60, 64, 67 };
    for (int i = 0; i < frames; i++) {
        if (i == RATE / 2)
            for (int k = 0; k < 3; k++) tw_organ_note(&ins.organ, chord[k], true, 100);
        if (i == trem_at) tw_rotary_set_mode(&ins.rotary, TW_ROT_TREMOLO);
        if (i == chor_at) tw_rotary_set_mode(&ins.rotary, TW_ROT_CHORALE);
        if (i == frames - RATE)
            for (int k = 0; k < 3; k++) tw_organ_note(&ins.organ, chord[k], false, 0);
        tw_stereo y = tw_instrument_tick_stereo(&ins);
        dst[2 * i] = y.l;
        dst[2 * i + 1] = y.r;
    }
}

/* One high and one low note through the full instrument: the horn path
 * swings pitch, the drum path only breathes -- the [HX] A/B. */
static void render_fm_am(float *dst, int frames, int note) {
    tw_instrument ins;
    tw_instrument_init(&ins, RATE);
    static const uint8_t reg[TW_DRAWBARS] = { 0, 0, 8, 0, 0, 0, 0, 0, 0 };
    tw_organ_set_registration(&ins.organ, reg);
    tw_rotary_set_mode(&ins.rotary, TW_ROT_TREMOLO);
    for (int i = 0; i < frames; i++) {
        if (i == RATE / 2) tw_organ_note(&ins.organ, note, true, 100);
        if (i == frames - RATE) tw_organ_note(&ins.organ, note, false, 0);
        tw_stereo y = tw_instrument_tick_stereo(&ins);
        dst[2 * i] = y.l;
        dst[2 * i + 1] = y.r;
    }
}

int main(void) {
    printf("tonewheel91 M6 exhibit -- the rotary speaker\n\n");

    /* crossover (sec 15 [HX]: 800 Hz, 12 dB/oct, both sides) */
    tw_rotary r;
    tw_rotary_init(&r, RATE);
    tw_rotary_set_mode(&r, TW_ROT_TREMOLO);
    static float lp_h[8192], hp_h[8192];
    for (int i = 0; i < 8192; i++) {
        (void)tw_rotary_tick(&r, i == 0 ? 1.0f : 0.0f);
        lp_h[i] = r.xo_lp;
        hp_h[i] = r.xo_hp;
        mh[i] = r.xo_lp - r.xo_hp;
    }
    double lp8 = mag_at(lp_h, 8192, 800.0), hp8 = mag_at(hp_h, 8192, 800.0);
    double sum_lo = 1e9, sum_hi = -1e9;
    for (double f = 100.0; f <= 6400.0; f *= 2.0) {
        double s = mag_at(mh, 8192, f);
        if (s < sum_lo) sum_lo = s;
        if (s > sum_hi) sum_hi = s;
    }
    printf("  crossover (sec 15 [HX], 800 Hz 12 dB/oct):\n");
    printf("    |LP(800)| %.4f, |HP(800)| %.4f (pinned -3 dB = 0.707)\n", lp8, hp8);
    printf("    inverted-horn sum over 100..6400 Hz: %.3f..%.3f"
           " (flat + the 2nd-order +3 dB bump at the split)\n", sum_lo, sum_hi);

    /* rotor dynamics (sec 15.1: transitions read as ~3 tau) */
    tw_rotary_init(&r, RATE);
    tw_rotary_set_mode(&r, TW_ROT_TREMOLO);
    double knee = (400.0 / 60.0) * (1.0 - exp(-1.0));
    long n_rise = 0;
    while ((double)(r.horn_target + r.horn_dev) < knee && n_rise < 480000) {
        (void)tw_rotary_tick(&r, 0.0f);
        n_rise++;
    }
    rotary_lock(&r, (float)(400.0 / 60.0), (float)(340.0 / 60.0));
    tw_rotary_set_mode(&r, TW_ROT_CHORALE);
    knee = 40.0 / 60.0 + (340.0 / 60.0 - 40.0 / 60.0) * exp(-1.0);
    long n_fall = 0;
    while ((double)(r.drum_target + r.drum_dev) > knee && n_fall < 480000) {
        (void)tw_rotary_tick(&r, 0.0f);
        n_fall++;
    }
    rotary_lock(&r, (float)(400.0 / 60.0), (float)(340.0 / 60.0));
    for (int i = 0; i < 4800; i++) (void)tw_rotary_tick(&r, 0.0f);
    tw_rotary_set_mode(&r, TW_ROT_BRAKE);
    for (int i = 0; i < 8 * RATE; i++) (void)tw_rotary_tick(&r, 0.0f);
    int parked = r.horn_phase == 0.0f && r.horn_dev == 0.0f;
    printf("  rotors ([FOLK] speeds; [RS] drum fall):\n");
    printf("    horn rise tau: %ld samples = %.2f s (pinned 1/3 s)\n",
           n_rise, (double)n_rise / RATE);
    printf("    drum fall tau: %ld samples = %.2f s (pinned 6.5/3 s)\n",
           n_fall, (double)n_fall / RATE);
    printf("    brake: horn parks at the front stop, exact: %s\n",
           parked ? "yes" : "NO");

    /* horn FM + AM vs drum AM-only (sec 15 [HX]) */
    tone_capture((float)(400.0 / 60.0), 0.0f, 1.0f, 2000.0);
    double he_lo, he_hi, hf_lo, hf_hi;
    demod_span(tone_l + 4800, 38400, 2000.0, 48, &he_lo, &he_hi, &hf_lo, &hf_hi);
    tone_capture(0.0f, (float)(340.0 / 60.0), 0.0f, 500.0);
    double de_lo, de_hi, df_lo, df_hi;
    demod_span(tone_l + 4800, 38400, 500.0, 96, &de_lo, &de_hi, &df_lo, &df_hi);
    tone_capture(0.0f, (float)(340.0 / 60.0), 0.0f, 100.0);
    double fe_lo, fe_hi, ff_lo, ff_hi;
    demod_span(tone_l + 4800, 38400, 100.0, 480, &fe_lo, &fe_hi, &ff_lo, &ff_hi);
    double want_pp = 2.0 * 2000.0 * TAU_D * (400.0 / 60.0) * 0.0003;
    printf("  horn (2 kHz tone, tremolo): pitch swing %.1f Hz p-p"
           " (geometry says %.1f), AM env floor %.3f (depth 0.40)\n",
           hf_hi - hf_lo, want_pp, he_lo / he_hi);
    printf("  drum (500 Hz tone, tremolo): pitch residual %.2f Hz p-p"
           " (no Doppler by construction), AM env floor %.3f\n",
           df_hi - df_lo, de_lo / de_hi);
    printf("  drum (100 Hz tone): AM env floor %.3f"
           " (below the 200 Hz floor the drum barely modulates)\n",
           fe_lo / fe_hi);

    /* bypass identity: the stereo instrument against the mono chain */
    tw_instrument ia, ib;
    tw_instrument_init(&ia, RATE);
    tw_instrument_init(&ib, RATE);
    int byp_bad = 0;
    for (int i = 0; i < 4 * RATE; i++) {
        if (i == RATE / 2) {
            tw_organ_note(&ia.organ, 60, true, 100);
            tw_organ_note(&ib.organ, 60, true, 100);
        }
        tw_stereo ya = tw_instrument_tick_stereo(&ia);
        float yb = tw_instrument_tick(&ib);
        if (ya.l != yb || ya.r != yb) byp_bad++;
    }
    printf("\n  bypass identity: stereo == mono chain on both channels: %s\n",
           byp_bad == 0 ? "bit-exact" : "MISMATCH");

    /* the A/B renders + two-run determinism of the transition */
    render_passage(ab_a, AB_FRAMES, TW_ROT_CHORALE, -1, -1);
    render_passage(ab_b, AB_FRAMES, TW_ROT_TREMOLO, -1, -1);
    render_passage(tr_buf, TR_FRAMES, TW_ROT_CHORALE, 5 * RATE, 10 * RATE);
    uint64_t h1 = tw_fnv1a64(tr_buf, sizeof tr_buf, 0);
    render_passage(tr_buf, TR_FRAMES, TW_ROT_CHORALE, 5 * RATE, 10 * RATE);
    uint64_t h2 = tw_fnv1a64(tr_buf, sizeof tr_buf, 0);
    printf("  scripted determinism (transition render): FNV64 %016llx %s\n",
           (unsigned long long)h1, h1 == h2 ? "(two runs identical)" : "MISMATCH");
    render_fm_am(fm_a, AB_FRAMES, 96); /* 8' top: ~2.1 kHz, horn side */
    render_fm_am(fm_b, AB_FRAMES, 43); /* 8' low G: ~196 Hz, drum side */

    /* exhibit scale: the repo's 1/8 headroom convention */
    for (long i = 0; i < 2L * AB_FRAMES; i++) {
        ab_a[i] *= 0.125f;
        ab_b[i] *= 0.125f;
        fm_a[i] *= 0.125f;
        fm_b[i] *= 0.125f;
    }
    for (long i = 0; i < 2L * TR_FRAMES; i++) tr_buf[i] *= 0.125f;
    int rc = 0;
    rc |= wav_write_f32("build/m6_chorale.wav", ab_a, 2L * AB_FRAMES, RATE, 2);
    rc |= wav_write_f32("build/m6_tremolo.wav", ab_b, 2L * AB_FRAMES, RATE, 2);
    rc |= wav_write_f32("build/m6_transition.wav", tr_buf, 2L * TR_FRAMES, RATE, 2);
    rc |= wav_write_f32("build/m6_horn_fm.wav", fm_a, 2L * AB_FRAMES, RATE, 2);
    rc |= wav_write_f32("build/m6_drum_am.wav", fm_b, 2L * AB_FRAMES, RATE, 2);
    printf("\n  wavs: build/m6_chorale.wav / m6_tremolo.wav (the speed A/B),\n"
           "        m6_transition.wav (chorale -> tremolo at 5 s -> chorale\n"
           "        at 10 s: the horn changes in ~1 s, the drum takes 5-8 s),\n"
           "        m6_horn_fm.wav / m6_drum_am.wav (the [HX] A/B)%s\n",
           rc ? " (WRITE FAILED)" : "");

    int verdict = lp8 > 0.68 && lp8 < 0.73 && hp8 > 0.68 && hp8 < 0.73
               && sum_lo > 0.95 && sum_hi < 1.45
               && n_rise > 15200 && n_rise < 16800
               && n_fall > 100000 && n_fall < 108000
               && parked
               && hf_hi - hf_lo > want_pp - 8.0 && hf_hi - hf_lo < want_pp + 8.0
               && he_lo / he_hi > 0.57 && he_lo / he_hi < 0.64
               && df_hi - df_lo < 2.0
               && de_lo / de_hi > 0.85 && de_lo / de_hi < 0.94
               && fe_lo / fe_hi > 0.965
               && byp_bad == 0 && h1 == h2 && rc == 0;
    printf("\n  exhibit verdict: %s\n", verdict ? "PASS" : "FAIL");
    return verdict ? 0 : 1;
}
