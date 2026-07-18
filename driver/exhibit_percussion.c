/* tonewheel91 M3 exhibit, part 2 -- percussion.
 *
 * Single-trigger vs naive retrigger: registration is silenced (all digits
 * 0) so the keyed/manual tone contributes nothing and the render is pure
 * percussion, measured through the public API only. A legato three-note
 * chord (each key added before the last is released) must produce exactly
 * one trigger; the same three notes played staccato (each released before
 * the next) must produce three -- that is the single-trigger + re-arm
 * rule (constants.md section 8) made audible.
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
#define GAP 4800 /* 100 ms between notes */

static float legato_buf[4 * GAP], staccato_buf[4 * GAP];
static float decay_fast[2 * RATE], decay_slow[2 * RATE];

/* Silences the keyed/manual tone (all drawbars 0) so the render is pure
 * percussion; counts trigger events via the armed->disarmed edge. */
static int render_phrase(float *dst, long frames, const int *notes, int n, bool legato) {
    tw_organ o;
    tw_organ_init(&o, RATE);
    static const uint8_t silent[TW_DRAWBARS] = { 0 };
    tw_organ_set_registration(&o, silent);
    tw_organ_set_percussion(&o, true, false, false, true); /* on, 2nd, fast, normal */
    int hits = 0;
    long i = 0;
    for (int k = 0; k < n; k++) {
        bool was_armed = o.perc.armed;
        tw_organ_note(&o, notes[k], true, 100);
        if (was_armed && !o.perc.armed) hits++;
        for (long s = 0; s < GAP && i < frames; s++, i++)
            dst[i] = tw_organ_tick(&o) * 0.3f;
        if (!legato) tw_organ_note(&o, notes[k], false, 0);
    }
    if (legato) for (int k = 0; k < n; k++) tw_organ_note(&o, notes[k], false, 0);
    for (; i < frames; i++) dst[i] = tw_organ_tick(&o) * 0.3f;
    return hits;
}

static long render_decay(float *dst, long frames, bool slow) {
    tw_organ o;
    tw_organ_init(&o, RATE);
    static const uint8_t silent[TW_DRAWBARS] = { 0 };
    tw_organ_set_registration(&o, silent);
    tw_organ_set_percussion(&o, true, false, slow, true);
    tw_organ_note(&o, 60, true, 100);
    int w = o.perc.wheel;
    double peak = (double)o.gen.perc_target[w - 1];
    double frac = exp(-1.0);
    long n_1e = -1;
    for (long i = 0; i < frames; i++) {
        dst[i] = tw_organ_tick(&o) * 0.3f;
        if (n_1e < 0 && (double)o.gen.perc_target[w - 1] <= peak * frac) n_1e = i;
    }
    return n_1e;
}

int main(void) {
    printf("tonewheel91 M3 exhibit, part 2 -- percussion\n\n");

    static const int chord[3] = { 60, 64, 67 };
    int hits_legato = render_phrase(legato_buf, 4 * GAP, chord, 3, true);
    int hits_staccato = render_phrase(staccato_buf, 4 * GAP, chord, 3, false);
    printf("  single-trigger vs naive retrigger (registration silenced,"
           " pure percussion):\n");
    printf("    legato three-note chord:   %d trigger(s), expected 1\n", hits_legato);
    printf("    staccato three notes:      %d trigger(s), expected 3\n", hits_staccato);

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
    float r1[4 * GAP], r2[4 * GAP];
    render_phrase(r1, 4 * GAP, chord, 3, true);
    render_phrase(r2, 4 * GAP, chord, 3, true);
    uint64_t h1 = tw_fnv1a64(r1, sizeof r1, 0);
    uint64_t h2 = tw_fnv1a64(r2, sizeof r2, 0);
    printf("\n  scripted determinism: FNV64 %016llx %s\n",
           (unsigned long long)h1, h1 == h2 ? "(two runs identical)" : "MISMATCH");

    int rc = 0;
    rc |= wav_write_f32("build/m3_percussion_legato.wav", legato_buf, 4 * GAP, RATE, 1);
    rc |= wav_write_f32("build/m3_percussion_staccato.wav", staccato_buf, 4 * GAP, RATE, 1);
    rc |= wav_write_f32("build/m3_percussion_decay_fast.wav", decay_fast, 2 * RATE, RATE, 1);
    rc |= wav_write_f32("build/m3_percussion_decay_slow.wav", decay_slow, 2 * RATE, RATE, 1);
    printf("\n  wavs: build/m3_percussion_legato.wav, m3_percussion_staccato.wav,\n"
           "        m3_percussion_decay_fast.wav, m3_percussion_decay_slow.wav%s\n",
           rc ? " (WRITE FAILED)" : "");

    int verdict = hits_legato == 1 && hits_staccato == 3
               && ratio > 4.0 && ratio < 4.3
               && h1 == h2 && rc == 0;
    printf("\n  exhibit verdict: %s\n", verdict ? "PASS" : "FAIL");
    return verdict ? 0 : 1;
}
