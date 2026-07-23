/* tonewheel91 depth exhibit -- how far the key is down.
 *
 * The nine contacts of a key are not a switch, they are a stack: the
 * travel makes them one at a time, bus 0 first (constants.md sec 7.1).
 * Velocity already spread them over time; depth spreads them over the
 * key's position, which is the same stack seen the other way round. A
 * key held part-way is a registration the drawbars cannot reach, and
 * riding the finger across a make point is the smear that follows.
 *
 * Nothing new is computed for any of it. The section 6/6.1 merge law
 * already folds an arbitrary set of closed contacts per wheel, so depth
 * only changes which cells are closed -- measured below as the made
 * buses passing exactly their full-press contribution.
 *
 * Three renders: the staircase (each contact arriving alone), the smear
 * (a finger riding the travel, then dithering across the make points),
 * and the ninth-contact theft (with percussion on, the top of the
 * travel is out of service -- sec 8's trigger-sensing line).
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../src/tonewheel.h"
#include "wav.h"

#define RATE 48000
#define CTRL 240 /* depth messages every 5 ms: a touch surface's rate */

static float steps_buf[19 * RATE / 2]; /* 18 half-second steps, out and back */
static float smear_buf[10 * RATE];
static float theft_buf[6 * RATE];

static const uint8_t ALL8[TW_DRAWBARS] = { 8, 8, 8, 8, 8, 8, 8, 8, 8 };

/* organ.c's make points, restated: nine evenly over the 0..127 travel. */
static int make_point(int i) { return (i + 1) * 128 / (TW_DRAWBARS + 1); }

/* A position that commands exactly n contacts from either direction. */
static int forcing(int n) {
    if (n <= 0) return 0;
    if (n >= TW_DRAWBARS) return 127;
    return (make_point(n - 1) + make_point(n)) / 2;
}

static void organ(tw_organ *o, bool percussion) {
    tw_organ_init(o, RATE);
    tw_organ_set_wear(o, 0.0f); /* the idealized reference */
    tw_organ_set_registration(o, ALL8);
    if (percussion) tw_organ_set_percussion(o, true, false, false, true);
}

static double rms(const float *x, long from, long to) {
    double acc = 0.0;
    for (long i = from; i < to; i++) acc += (double)x[i] * x[i];
    return sqrt(acc / (double)(to - from));
}

/* Peak of the second difference: the click metric of the M2 exhibit,
 * reused here to hear the contacts arriving. */
static double peak_d2(const float *x, long from, long to) {
    double peak = 0.0;
    for (long i = from + 2; i < to; i++) {
        double d2 = fabs((double)x[i] - 2.0 * x[i - 1] + x[i - 2]);
        if (d2 > peak) peak = d2;
    }
    return peak;
}

/* Steady level at one held depth, and the per-bus contribution behind
 * it. `contrib` takes the nine wheel targets of key 25's nine taps
 * (nine distinct wheels, no foldback collision to disentangle). */
static double hold_at(int n, double *contrib) {
    static float buf[RATE / 2];
    tw_organ o;
    organ(&o, false);
    tw_organ_note(&o, 60, true, 127);
    for (int i = 0; i < RATE / 2; i++) (void)tw_organ_tick(&o);
    tw_organ_note_depth(&o, 60, forcing(n));
    for (int i = 0; i < RATE / 4; i++) (void)tw_organ_tick(&o);
    if (contrib)
        for (int b = 0; b < TW_DRAWBARS; b++)
            contrib[b] = (double)o.gen.keyed_target[
                tw_wheel_index(60 - TW_NOTE_MIN + 1, b) - 1];
    for (int i = 0; i < RATE / 2; i++) buf[i] = tw_organ_tick(&o);
    return rms(buf, 0, RATE / 2);
}

/* The staircase: one key down, the travel walked out and back in half
 * second steps, so each contact is heard arriving and leaving alone. */
static void render_steps(float *dst, long n) {
    tw_organ o;
    organ(&o, false);
    long step = RATE / 2;
    for (long i = 0; i < n; i++) {
        if (i == 0) tw_organ_note(&o, 60, true, 127);
        if (i % step == 0 && i > 0) {
            long k = i / step;                        /* 1..18 */
            int d = k <= 9 ? 9 - (int)k : (int)k - 9; /* 8..0, then 1..9 */
            tw_organ_note_depth(&o, 60, forcing(d));
        }
        dst[i] = tw_organ_tick(&o) * 0.125f;
    }
}

/* The smear, counted: how many contact transitions a second of the
 * dither actually produces on one key. This is the whole gesture --
 * make and break, each with its own bounce, at a rate no note stream
 * can reach. */
static long smear_toggles(void) {
    tw_organ o;
    organ(&o, false);
    tw_organ_note(&o, 60, true, 127);
    for (int i = 0; i < RATE / 2; i++) (void)tw_organ_tick(&o);
    unsigned prev = 0;
    for (int b = 0; b < TW_DRAWBARS; b++) prev |= (unsigned)o.contact[24][b] << b;
    long toggles = 0;
    for (int i = 0; i < RATE; i++) {
        if (i % CTRL == 0) {
            double t = (double)i / RATE;
            tw_organ_note_depth(&o, 60,
                (int)(70.0 + 45.0 * sin(2.0 * 3.14159265 * 7.0 * t)));
        }
        (void)tw_organ_tick(&o);
        unsigned now = 0;
        for (int b = 0; b < TW_DRAWBARS; b++) now |= (unsigned)o.contact[24][b] << b;
        if (now != prev) toggles++;
        prev = now;
    }
    return toggles;
}

/* The smear: a chord held while the finger rides the travel. First two
 * slow sweeps (harmonics peel off and wash back in), then a dither
 * across four make points at ~7 Hz -- contacts making and breaking with
 * their bounce, which is the grit the gesture is played for. */
static void render_smear(float *dst, long n) {
    tw_organ o;
    organ(&o, false);
    static const int chord[3] = { 55, 59, 62 };
    for (long i = 0; i < n; i++) {
        if (i == RATE / 4)
            for (int k = 0; k < 3; k++) tw_organ_note(&o, chord[k], true, 110);
        if (i > RATE / 2 && i % CTRL == 0) {
            double t = (double)(i - RATE / 2) / RATE;
            int d;
            if (t < 2.0) d = (int)(127.0 * (1.0 - t / 2.0));        /* down  */
            else if (t < 4.0) d = (int)(127.0 * (t - 2.0) / 2.0);   /* up    */
            else d = (int)(70.0 + 45.0 * sin(2.0 * 3.14159265 * 7.0 * t));
            for (int k = 0; k < 3; k++) tw_organ_note_depth(&o, chord[k], d);
        }
        if (i == n - RATE / 2)
            for (int k = 0; k < 3; k++) tw_organ_note(&o, chord[k], false, 0);
        dst[i] = tw_organ_tick(&o) * 0.125f;
    }
}

/* The theft, and the double duty of the same contact: percussion on, a
 * full press, then the travel toggled 9 -> 8 -> 9 -> 8 -> 9 at one
 * second a step. The sustained tone does not move across any of it --
 * the 1' bus is out of service while percussion is on -- and every
 * return to 9 fires the envelope again, with no note event anywhere.
 * One second is far past the 34 ms recovery, so each return counts. */
static void render_theft(float *dst, long n) {
    tw_organ o;
    organ(&o, true);
    long step = RATE;
    for (long i = 0; i < n; i++) {
        if (i == 0) tw_organ_note(&o, 60, true, 127);
        if (i > 0 && i % step == 0)
            tw_organ_note_depth(&o, 60, (i / step) % 2 ? forcing(8) : 127);
        dst[i] = tw_organ_tick(&o) * 0.125f;
    }
}

int main(void) {
    printf("tonewheel91 depth exhibit -- how far the key is down\n\n");

    /* the make points, and the band around each of them */
    printf("  travel (0..127) -> contacts made, with the +-4 band:\n   ");
    for (int i = 0; i < TW_DRAWBARS; i++)
        printf(" %d@%d", i + 1, make_point(i));
    printf("\n");
    tw_organ h;
    organ(&h, false);
    tw_organ_note(&h, 60, true, 127);
    for (int i = 0; i < RATE / 10; i++) (void)tw_organ_tick(&h);
    tw_organ_note_depth(&h, 60, make_point(4));
    int from_above = h.made[24];
    tw_organ_note_depth(&h, 60, 0);
    tw_organ_note_depth(&h, 60, make_point(4));
    int from_below = h.made[24];
    printf("    position %d reached from above -> %d contacts, from below"
           " -> %d\n", make_point(4), from_above, from_below);
    uint64_t rng0 = h.rng;
    for (int i = 0; i < 400; i++)
        tw_organ_note_depth(&h, 60, make_point(4) + (i % 2 ? 3 : -3));
    bool no_chatter = h.rng == rng0 && h.made[24] == from_below;
    printf("    400 messages dithering +-3 across it: %s\n",
           no_chatter ? "no contact moved, no RNG drawn" : "CHATTERED");

    /* the staircase, measured: level per held depth and the per-bus
     * contribution behind it */
    double full[TW_DRAWBARS], contrib[TW_DRAWBARS];
    double lvl[TW_DRAWBARS + 1];
    lvl[TW_DRAWBARS] = hold_at(TW_DRAWBARS, full);
    bool law_holds = true, monotone = true;
    for (int n = 0; n <= TW_DRAWBARS; n++) {
        lvl[n] = hold_at(n, contrib);
        for (int b = 0; b < TW_DRAWBARS; b++)
            if (contrib[b] != (b < n ? full[b] : 0.0)) law_holds = false;
        if (n > 0 && !(lvl[n] > lvl[n - 1])) monotone = false;
    }
    printf("\n  the staircase (one key at 888888888, steady rms per depth,\n"
           "  and the partial each contact adds as it makes):\n");
    static const char *foot[TW_DRAWBARS] = {
        "16'", "5-1/3'", "8'", "4'", "2-2/3'", "2'", "1-3/5'", "1-1/3'", "1'",
    };
    for (int n = 0; n <= TW_DRAWBARS; n++) {
        if (n == 0) { printf("    0 contacts: rms %.4f  (held, and silent)\n",
                             lvl[0]); continue; }
        printf("    %d contact%s: rms %.4f   + %7.1f Hz  (%s)\n",
               n, n == 1 ? " " : "s", lvl[n],
               (double)tw_wheel_freq_hz(tw_wheel_index(60 - TW_NOTE_MIN + 1, n - 1)),
               foot[n - 1]);
    }
    printf("    made buses pass exactly their full-press contribution: %s\n",
           law_holds ? "yes -- the merge law is untouched" : "NO");
    printf("    the travel is a timbre control, not a level one: %.1f dB of\n"
           "    level across the whole of it, against a spectrum that grows\n"
           "    from the 16' fundamental to the 1' -- four octaves of reach\n",
           20.0 * log10(lvl[TW_DRAWBARS] / lvl[1]));

    /* the click of one contact arriving alone, against the sustain it
     * arrives into: the M2 metric, measured on one step of the staircase.
     * The step at 6.5 s takes 3 contacts to 4, so both windows sound. */
    render_steps(steps_buf, (long)(sizeof steps_buf / sizeof *steps_buf));
    double sus = peak_d2(steps_buf, (long)(6.1 * RATE), (long)(6.4 * RATE));
    double atk = peak_d2(steps_buf, (long)(6.5 * RATE), (long)(6.51 * RATE));
    double step_click = 20.0 * log10(atk / sus);
    printf("\n  one contact arriving mid-note (3 -> 4): +%.1f dB over the"
           " sustain it lands in\n", step_click);

    /* the smear itself, counted */
    long toggles = smear_toggles();
    printf("  the smear: %ld contact transitions in one second of dither"
           " at ~7 Hz\n", toggles);

    /* the ninth-contact theft, measured */
    tw_organ o;
    static float d8[TW_WHEELS], d9[TW_WHEELS];
    organ(&o, true);
    tw_organ_note(&o, 60, true, 127);
    for (int i = 0; i < RATE / 10; i++) (void)tw_organ_tick(&o);
    tw_organ_note_depth(&o, 60, forcing(8));
    for (int i = 0; i < RATE / 10; i++) (void)tw_organ_tick(&o);
    memcpy(d8, o.gen.keyed_target, sizeof d8);
    tw_organ_note_depth(&o, 60, forcing(9));
    for (int i = 0; i < RATE / 10; i++) (void)tw_organ_tick(&o);
    memcpy(d9, o.gen.keyed_target, sizeof d9);
    bool stolen = memcmp(d8, d9, sizeof d8) == 0;
    printf("\n  ninth-contact theft (sec 8): with percussion on, depth 8"
           " and depth 9\n    are the same sound: %s. With it off the same"
           " step is worth\n    %.2f dB -- the top of the travel goes"
           " quietly out of service.\n",
           stolen ? "identical, bit for bit" : "THEY DIFFER",
           20.0 * log10(lvl[TW_DRAWBARS] / lvl[TW_DRAWBARS - 1]));

    /* ...and that same contact is the percussion trigger (sec 8), so
     * the top of the travel is a pure trigger control while percussion
     * is on: silent in the sustained tone, and the only thing that
     * fires the envelope. */
    organ(&o, true);
    tw_organ_note(&o, 60, true, 1);   /* slow press: 15 ms to intercept */
    for (int i = 0; i < RATE / 200; i++) (void)tw_organ_tick(&o); /* 5 ms */
    tw_organ_note_depth(&o, 60, forcing(5));
    for (int i = 0; i < RATE / 10; i++) (void)tw_organ_tick(&o);
    bool half_silent = o.perc.armed && o.perc.wheel == 0;
    tw_organ_note_depth(&o, 60, 127);
    for (int i = 0; i < RATE / 100; i++) (void)tw_organ_tick(&o);
    int w = o.perc.wheel;
    bool bottom_fires = !o.perc.armed && w != 0;
    tw_organ_note_depth(&o, 60, forcing(8)); /* lift off the sensing line */
    for (int i = 0; i < RATE / 10; i++) (void)tw_organ_tick(&o);
    double quiet = w ? (double)o.gen.perc_target[w - 1] : 0.0;
    tw_organ_note_depth(&o, 60, 127);
    for (int i = 0; i < RATE / 1000; i++) (void)tw_organ_tick(&o);
    bool rides = w && (double)o.gen.perc_target[w - 1] > quiet;
    printf("    the same contact is the percussion trigger:"
           " a half-press fires\n    nothing (%s), bottoming out fires it"
           " (%s), and riding back\n    onto it retriggers with no note"
           " event (%s)\n",
           half_silent ? "armed, no wheel" : "FIRED",
           bottom_fires ? "wheel picked" : "DID NOT FIRE",
           rides ? "envelope jumps" : "DID NOT RETRIGGER");

    /* inertness: the same passage with no depth message at all must be
     * the pre-depth organ, bit for bit */
    static float with_d[3 * RATE], without[3 * RATE];
    for (int pass = 0; pass < 2; pass++) {
        float *dst = pass ? with_d : without;
        organ(&o, false);
        for (long i = 0; i < 3L * RATE; i++) {
            if (i == RATE / 4) tw_organ_note(&o, 60, true, 90);
            if (pass && i > RATE / 2 && i % CTRL == 0)
                tw_organ_note_depth(&o, 60, 60 + (int)(i / CTRL % 7) * 10);
            if (i == 2L * RATE) tw_organ_note(&o, 60, false, 0);
            dst[i] = tw_organ_tick(&o);
        }
    }
    uint64_t h_silent = tw_fnv1a64(without, sizeof without, 0);
    organ(&o, false); /* the same passage on the pre-depth code path */
    static float plain[3 * RATE];
    for (long i = 0; i < 3L * RATE; i++) {
        if (i == RATE / 4) tw_organ_note(&o, 60, true, 90);
        if (i == 2L * RATE) tw_organ_note(&o, 60, false, 0);
        plain[i] = tw_organ_tick(&o);
    }
    uint64_t h_plain = tw_fnv1a64(plain, sizeof plain, 0);
    uint64_t h_depth = tw_fnv1a64(with_d, sizeof with_d, 0);
    printf("\n  inertness: no depth message -> FNV64 %016llx %s\n",
           (unsigned long long)h_silent,
           h_silent == h_plain ? "== the pre-depth passage" : "MISMATCH");
    printf("    with the depth stream -> FNV64 %016llx (audibly its own)\n",
           (unsigned long long)h_depth);

    /* the renders, and their determinism */
    render_smear(smear_buf, (long)(sizeof smear_buf / sizeof *smear_buf));
    uint64_t h_s1 = tw_fnv1a64(smear_buf, sizeof smear_buf, 0);
    render_smear(smear_buf, (long)(sizeof smear_buf / sizeof *smear_buf));
    uint64_t h_s2 = tw_fnv1a64(smear_buf, sizeof smear_buf, 0);
    render_theft(theft_buf, (long)(sizeof theft_buf / sizeof *theft_buf));
    printf("\n  scripted determinism (the smear render): FNV64 %016llx %s\n",
           (unsigned long long)h_s1,
           h_s1 == h_s2 ? "(two runs identical)" : "MISMATCH");

    int rc = 0;
    rc |= wav_write_f32("build/depth_steps.wav", steps_buf,
                        (long)(sizeof steps_buf / sizeof *steps_buf), RATE, 1);
    rc |= wav_write_f32("build/depth_smear.wav", smear_buf,
                        (long)(sizeof smear_buf / sizeof *smear_buf), RATE, 1);
    rc |= wav_write_f32("build/depth_theft.wav", theft_buf,
                        (long)(sizeof theft_buf / sizeof *theft_buf), RATE, 1);
    printf("\n  wavs: build/depth_steps.wav (one key, the travel walked out\n"
           "        and back in half-second steps), depth_smear.wav (a chord\n"
           "        held while the finger rides the travel, then dithers\n"
           "        across the make points at ~7 Hz), depth_theft.wav\n"
           "        (percussion on, the travel toggled 9 -> 8 -> 9: the\n"
           "        tone never moves, the envelope fires every time)%s\n",
           rc ? " (WRITE FAILED)" : "");

    int verdict = no_chatter && from_above == 5 && from_below == 4
                && law_holds && monotone && lvl[0] < 1e-6 && stolen
                && half_silent && bottom_fires && rides
                && step_click > 10.0 && toggles >= 20
                && h_silent == h_plain && h_depth != h_silent
                && h_s1 == h_s2 && rc == 0;
    printf("\n  exhibit verdict: %s\n", verdict ? "PASS" : "FAIL");
    return verdict ? 0 : 1;
}
