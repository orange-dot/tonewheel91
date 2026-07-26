/* ep73 EP2 exhibit: the restrike law, decision D5.
 *
 * What a hammer does to a tine that is already ringing. Two axes —
 * amplitude replace or add, phase continue or reset — so four laws, all
 * four rendered here on the same passage.
 *
 * The passage keeps the sustain pedal down throughout, because that is
 * when the question exists at all: let a key up and the damper stops the
 * tine, and a restrike meets almost nothing. Pedal down, a repeated note
 * meets its own ring, which is the idiomatic case the law has to serve.
 *
 * Three figures, chosen so the laws disagree:
 *   1. eight repeats at one velocity — does the note pump, build, or sit?
 *   2. a soft blow onto a loud ring — does the note duck? (replace) or
 *      barely move? (add)
 *   3. a loud blow onto a soft ring — the reverse, where the laws agree
 *      most and any difference is the phase axis alone.
 *
 * The verdict here is PASS on the measurements being sound, not on which
 * law wins. That one is decided by ear; the loser is then deleted.
 */
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "../src/epiano.h"
#include "wav.h"

#define RATE   48000
#define SECS   9
#define FRAMES (RATE * SECS)
#define NOTE   64 /* E4, the middle of the compass */

static float buf[4][FRAMES];

static const char *const LAW_NAME[4] = {
    "replace/continue", "replace/reset", "add/continue", "add/reset",
};

/* Event script, in seconds. Velocity 0 marks the end. */
struct ev { double t; int vel; };
static const struct ev SCRIPT[] = {
    /* 1: eight repeats at one velocity */
    { 0.000, 80 }, { 0.125, 80 }, { 0.250, 80 }, { 0.375, 80 },
    { 0.500, 80 }, { 0.625, 80 }, { 0.750, 80 }, { 0.875, 80 },
    /* 2: a soft blow onto a loud ring */
    { 3.000, 120 }, { 3.300, 20 },
    /* 3: a loud blow onto a soft ring */
    { 6.000, 20 }, { 6.300, 120 },
    { 0.0, 0 },
};

static void run(float *dst, int amp_law, int phase_law) {
    ep_piano p;
    ep_piano_init(&p, RATE);
    ep_bank_set_restrike(&p.bank, amp_law, phase_law);
    ep_piano_set_sustain(&p, true); /* the whole passage is pedalled */

    int next = 0;
    for (long i = 0; i < FRAMES; i++) {
        while (SCRIPT[next].vel != 0 && (long)(SCRIPT[next].t * RATE) == i) {
            /* key down then straight back up: the pedal holds the damper
             * off, so the release changes nothing and the next blow meets
             * a ringing tine. */
            ep_piano_note(&p, NOTE, true, SCRIPT[next].vel);
            ep_piano_note(&p, NOTE, false, 0);
            next++;
        }
        dst[i] = ep_piano_tick(&p);
    }
}

static double rms(const float *x, long a, long b) {
    double s = 0.0;
    for (long i = a; i < b; i++) s += (double)x[i] * x[i];
    return sqrt(s / (double)(b - a));
}

static double peak(const float *x, long a, long b) {
    double m = 0.0;
    for (long i = a; i < b; i++) if (fabs(x[i]) > m) m = fabs(x[i]);
    return m;
}

/* Largest sample-to-sample step: a phase reset moves the modal sum
 * discontinuously, and that is what a click is. */
static double max_step(const float *x, long a, long b) {
    double m = 0.0;
    for (long i = a + 1; i < b; i++) {
        double d = fabs((double)x[i] - x[i - 1]);
        if (d > m) m = d;
    }
    return m;
}

static double db(double r) { return 20.0 * log10(r > 1e-30 ? r : 1e-30); }

static long at(double t) { return (long)(t * RATE); }

int main(void) {
    printf("ep73 EP2 exhibit: the restrike law, decision D5"
           " (docs/ep-constants.md sec 5.4)\n\n");
    printf("   MIDI %d (E4), sustain pedal down throughout, %d Hz\n\n", NOTE, RATE);

    for (int i = 0; i < 4; i++) run(buf[i], i / 2, i % 2);

    printf("   1. eight repeats at velocity 80, 125 ms apart\n\n");
    printf("      law                 peak    rms during   ring-out rms\n");
    printf("                                  the repeats   at +1.5 s\n");
    for (int i = 0; i < 4; i++)
        printf("      %-18s %6.3f     %8.4f     %8.4f\n", LAW_NAME[i],
               peak(buf[i], 0, at(1.0)), rms(buf[i], 0, at(1.0)),
               rms(buf[i], at(2.3), at(2.5)));

    printf("\n   2. velocity 120, then velocity 20 onto its ring at +300 ms\n\n");
    printf("      law                 before     after    change    step in the\n");
    printf("                          the blow   50 ms             2 ms before / at\n");
    for (int i = 0; i < 4; i++) {
        double before = rms(buf[i], at(3.25), at(3.30));
        double after = rms(buf[i], at(3.30), at(3.35));
        printf("      %-18s %8.4f  %8.4f  %+6.1f dB  %7.5f / %.5f\n", LAW_NAME[i],
               before, after, db(after / before),
               max_step(buf[i], at(3.298), at(3.300)),
               max_step(buf[i], at(3.300), at(3.302)));
    }

    printf("\n   3. velocity 20, then velocity 120 onto its ring at +300 ms\n\n");
    printf("      law                 before     after    change    step in the\n");
    printf("                          the blow   50 ms             2 ms before / at\n");
    for (int i = 0; i < 4; i++) {
        double before = rms(buf[i], at(6.25), at(6.30));
        double after = rms(buf[i], at(6.30), at(6.35));
        printf("      %-18s %8.4f  %8.4f  %+6.1f dB  %7.5f / %.5f\n", LAW_NAME[i],
               before, after, db(after / before),
               max_step(buf[i], at(6.298), at(6.300)),
               max_step(buf[i], at(6.300), at(6.302)));
    }

    printf("\n   the two step columns span the same 2 ms, so the pair reads\n"
           "   as: what the waveform was already doing, then what the blow\n"
           "   made it do. A step no larger than the undisturbed one is not\n"
           "   a click.\n");

    /* replace/continue is the law EP1 ran, and every EP1 render is pinned
     * on it: setting it explicitly must change nothing. */
    ep_bank a, b;
    ep_bank_init(&a, RATE);
    ep_bank_init(&b, RATE);
    ep_bank_set_restrike(&b, EP_AMP_REPLACE, EP_PHASE_CONTINUE);
    uint64_t ha = 0, hb = 0;
    for (long i = 0; i < 2L * RATE; i++) {
        if (i % 6000 == 0) {
            ep_bank_strike(&a, NOTE, 60);
            ep_bank_strike(&b, NOTE, 60);
        }
        float xa = ep_bank_tick_gated(&a), xb = ep_bank_tick_gated(&b);
        ha = tw_fnv1a64(&xa, sizeof xa, ha);
        hb = tw_fnv1a64(&xb, sizeof xb, hb);
    }
    printf("\n   default law == replace/continue: %016llx vs %016llx  %s\n",
           (unsigned long long)ha, (unsigned long long)hb,
           ha == hb ? "identical" : "MISMATCH");

    /* The ceiling: add must never push a mode past its own hardest blow. */
    ep_bank c;
    ep_bank_init(&c, RATE);
    ep_bank_set_restrike(&c, EP_AMP_ADD, EP_PHASE_CONTINUE);
    for (int i = 0; i < 40; i++) {
        ep_bank_strike(&c, NOTE, 127);
        for (int j = 0; j < 240; j++) ep_bank_tick_gated(&c); /* 5 ms apart */
    }
    int over = 0;
    for (int m = 0; m < EP_MODES; m++)
        if (c.amp[m][NOTE - EP_NOTE_MIN] > c.ceiling[m][NOTE - EP_NOTE_MIN]) over++;
    printf("   ceiling holds under 40 stacked ff blows: %s\n",
           over ? "NO" : "yes");

    /* Two runs, same bits. */
    static float again[FRAMES];
    run(again, 0, 0);
    int diff = memcmp(again, buf[0], sizeof again) != 0;

    int rc = 0;
    for (int i = 0; i < 4; i++) {
        char path[64];
        snprintf(path, sizeof path, "build/ep2_d5_%d_%s.wav", i,
                 i == 0 ? "replace_continue" : i == 1 ? "replace_reset"
                 : i == 2 ? "add_continue" : "add_reset");
        for (long j = 0; j < FRAMES; j++) buf[i][j] *= 0.25f;
        rc |= wav_write_f32(path, buf[i], FRAMES, RATE, 1);
    }
    printf("   two runs identical: %s\n", diff ? "NO" : "yes");
    printf("\n   wavs: build/ep2_d5_{0..3}_*.wav%s\n", rc ? " (WRITE FAILED)" : "");
    printf("   D5 is an ear decision: these four are the ballot, not the count.\n");

    int verdict = (ha == hb) && !over && !diff && rc == 0;
    printf("\n  exhibit verdict: %s\n", verdict ? "PASS" : "FAIL");
    return verdict ? 0 : 1;
}
