/* tonewheel91 M3 exhibit, part 2 -- percussion.
 *
 * Single-trigger vs naive retrigger: registration is silenced (all digits
 * 0) so the keyed/manual tone contributes nothing and the render is pure
 * percussion, measured through the public API only. A legato three-note
 * chord (each key added before the last is released) must produce exactly
 * one trigger; the same three notes played staccato (each released, and
 * *detached*, before the next) must produce three -- that is the
 * single-trigger + re-arm rule (constants.md section 8) made audible.
 *
 * Since the trigger became contact-driven, "detached" is a measurable
 * quantity rather than a word: the sensing line is the 1' contact, and
 * the grid recovers through R55/R56 in ~34 ms, so a release the next
 * attack treads on retriggers nothing. The staccato phrase below leaves
 * a real gap; the third phrase walks the gap across the threshold and
 * counts hits on each side of it.
 *
 * Fast/slow decay: one note, DECAY FAST vs SLOW, measured as samples to
 * decay to 1/e of the trigger peak; constants.md section 8 pins the ratio
 * at 4.133:1 from the RC values (SLOW = R58 alone, FAST = R57||R58).
 */
#include <math.h>
#include <stdio.h>
#include "../src/tonewheel.h"
#include "wav.h"

#define RATE 48000
#define GAP 4800 /* 100 ms of note, and of detachment */

static float legato_buf[7 * GAP], staccato_buf[7 * GAP];
static float decay_fast[2 * RATE], decay_slow[2 * RATE];

static void perc_organ(tw_organ *o, bool slow) {
    tw_organ_init(o, RATE);
    /* M7: pin the idealized reference (wear 0) — this exhibit's recorded
     * signatures predate the wear stage and must stay bit-stable. */
    tw_organ_set_wear(o, 0.0f);
    static const uint8_t silent[TW_DRAWBARS] = { 0 };
    tw_organ_set_registration(o, silent);
    tw_organ_set_percussion(o, true, false, slow, true); /* on, 2nd, normal */
}

/* Render `frames` and count trigger events on the way: the envelope
 * jumping upward is a hit, which is the only way to count them now that
 * the trigger fires somewhere inside the contact timeline. */
static int run(tw_organ *o, float *dst, long *i, long frames, long n) {
    int hits = 0;
    for (long s = 0; s < n && *i < frames; s++, (*i)++) {
        double before = o->perc.wheel ? o->gen.perc_target[o->perc.wheel - 1] : 0.0;
        dst[*i] = tw_organ_tick(o) * 0.3f;
        int w = o->perc.wheel;
        if (w && (double)o->gen.perc_target[w - 1] > before) hits++;
    }
    return hits;
}

/* legato: each key added before the last is released, so the sensing
 * line never lifts. staccato: each key released and left detached for
 * GAP before the next -- longer than the recovery. */
static int render_phrase(float *dst, long frames, const int *notes, int n, bool legato) {
    tw_organ o;
    perc_organ(&o, false);
    int hits = 0;
    long i = 0;
    for (int k = 0; k < n; k++) {
        tw_organ_note(&o, notes[k], true, 100);
        hits += run(&o, dst, &i, frames, GAP);
        if (!legato) {
            tw_organ_note(&o, notes[k], false, 0);
            hits += run(&o, dst, &i, frames, GAP); /* the detachment */
        }
    }
    if (legato) for (int k = 0; k < n; k++) tw_organ_note(&o, notes[k], false, 0);
    while (i < frames) hits += run(&o, dst, &i, frames, frames - i);
    return hits;
}

/* Two notes separated by `gap` frames of silence between release and
 * attack: how many hits survive that detachment. */
static int hits_across_gap(long gap) {
    static float sink[6 * RATE];
    tw_organ o;
    perc_organ(&o, false);
    long i = 0, frames = (long)(sizeof sink / sizeof *sink);
    int hits = 0;
    tw_organ_note(&o, 60, true, 100);
    hits += run(&o, sink, &i, frames, GAP);
    tw_organ_note(&o, 60, false, 0);
    hits += run(&o, sink, &i, frames, gap);
    tw_organ_note(&o, 64, true, 100);
    hits += run(&o, sink, &i, frames, GAP);
    return hits;
}

static long render_decay(float *dst, long frames, bool slow) {
    tw_organ o;
    perc_organ(&o, slow);
    tw_organ_note(&o, 60, true, 100);
    long i = 0;
    while (i < frames && o.perc.wheel == 0) dst[i++] = tw_organ_tick(&o) * 0.3f;
    int w = o.perc.wheel;
    double peak = (double)o.gen.perc_target[w - 1];
    double frac = exp(-1.0);
    long n_1e = -1, from = i;
    for (; i < frames; i++) {
        dst[i] = tw_organ_tick(&o) * 0.3f;
        if (n_1e < 0 && (double)o.gen.perc_target[w - 1] <= peak * frac) n_1e = i - from;
    }
    return n_1e;
}

int main(void) {
    printf("tonewheel91 M3 exhibit, part 2 -- percussion\n\n");

    static const int chord[3] = { 60, 64, 67 };
    int hits_legato = render_phrase(legato_buf, 7 * GAP, chord, 3, true);
    int hits_staccato = render_phrase(staccato_buf, 7 * GAP, chord, 3, false);
    printf("  single-trigger vs naive retrigger (registration silenced,"
           " pure percussion):\n");
    printf("    legato three-note chord:   %d trigger(s), expected 1\n", hits_legato);
    printf("    staccato three notes:      %d trigger(s), expected 3\n", hits_staccato);

    /* The trigger is the 1' contact, so detachment is now a duration the
     * exhibit can walk across. Recovery is ~34 ms (R55+R56 / C31). */
    static const long probe[5] = { 0, RATE / 100, RATE / 50, RATE / 25, RATE / 10 };
    printf("\n  detachment threshold (two notes, gap between release and"
           " attack):\n");
    bool threshold_ok = true;
    for (int p = 0; p < 5; p++) {
        int h = hits_across_gap(probe[p]);
        printf("    %5.0f ms gap: %d hit(s)%s\n", 1000.0 * probe[p] / RATE, h,
               probe[p] < RATE / 25 ? "  (inside the ~34 ms recovery)" : "");
        if ((probe[p] < RATE / 25) != (h == 1)) threshold_ok = false;
    }

    long n_fast = render_decay(decay_fast, 2 * RATE, false);
    long n_slow = render_decay(decay_slow, 2 * RATE, true);
    double ratio = (double)n_slow / (double)n_fast;
    printf("\n  decay fast/slow (samples to 1/e of trigger peak):\n");
    printf("    fast: %ld samples (%.3f s, expected ~0.375 s)\n",
           n_fast, (double)n_fast / RATE);
    printf("    slow: %ld samples (%.3f s, expected ~1.551 s)\n",
           n_slow, (double)n_slow / RATE);
    printf("    ratio slow/fast: %.3f, expected ~4.133\n", ratio);

    /* two-run determinism on the legato render */
    static float r1[7 * GAP], r2[7 * GAP];
    render_phrase(r1, 7 * GAP, chord, 3, true);
    render_phrase(r2, 7 * GAP, chord, 3, true);
    uint64_t h1 = tw_fnv1a64(r1, sizeof r1, 0);
    uint64_t h2 = tw_fnv1a64(r2, sizeof r2, 0);
    printf("\n  scripted determinism: FNV64 %016llx %s\n",
           (unsigned long long)h1, h1 == h2 ? "(two runs identical)" : "MISMATCH");

    int rc = 0;
    rc |= wav_write_f32("build/m3_percussion_legato.wav", legato_buf, 7 * GAP, RATE, 1);
    rc |= wav_write_f32("build/m3_percussion_staccato.wav", staccato_buf, 7 * GAP, RATE, 1);
    rc |= wav_write_f32("build/m3_percussion_decay_fast.wav", decay_fast, 2 * RATE, RATE, 1);
    rc |= wav_write_f32("build/m3_percussion_decay_slow.wav", decay_slow, 2 * RATE, RATE, 1);
    printf("\n  wavs: build/m3_percussion_legato.wav, m3_percussion_staccato.wav,\n"
           "        m3_percussion_decay_fast.wav, m3_percussion_decay_slow.wav%s\n",
           rc ? " (WRITE FAILED)" : "");

    int verdict = hits_legato == 1 && hits_staccato == 3 && threshold_ok
               && ratio > 4.0 && ratio < 4.3
               && h1 == h2 && rc == 0;
    printf("\n  exhibit verdict: %s\n", verdict ? "PASS" : "FAIL");
    return verdict ? 0 : 1;
}
