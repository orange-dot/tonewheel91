/* tonewheel91 tests — hosted; libm is the oracle. Run: make test */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../src/tonewheel.h"

static int fails, checks;
static const double TAU_D = 6.283185307179586;

#define CHECK(cond, ...) do { \
    checks++; \
    if (!(cond)) { \
        fails++; \
        printf("FAIL %d: ", __LINE__); \
        printf(__VA_ARGS__); \
        putchar('\n'); \
    } \
} while (0)

static double cents_vs_et(int wheel) {
    double et = 440.0 * pow(2.0, (wheel - 46) / 12.0);
    return 1200.0 * log2((double)tw_wheel_freq_hz(wheel) / et);
}

static void test_frequency_table(void) {
    CHECK(tw_wheel_freq_hz(46) == 440.0f, "wheel 46 must be exactly 440");
    struct { int wheel; double hz; } anchor[] = {
        { 1, 32.6923077 }, { 13, 65.3846154 }, { 73, 2092.3076923 },
        { 85, 4189.0909091 }, { 91, 5924.5714286 },
    };
    for (size_t i = 0; i < sizeof anchor / sizeof *anchor; i++) {
        double got = tw_wheel_freq_hz(anchor[i].wheel);
        CHECK(fabs(got / anchor[i].hz - 1.0) < 1e-6,
              "wheel %d: %.7f vs %.7f", anchor[i].wheel, got, anchor[i].hz);
    }
    CHECK(tw_wheel_freq_hz(0) == 0.0f && tw_wheel_freq_hz(92) == 0.0f,
          "out-of-range wheels return 0");

    double worst84 = 0.0, worst_top = 0.0;
    int argmax84 = 0;
    for (int n = 1; n <= 91; n++) {
        double d = fabs(cents_vs_et(n));
        int cls = (n - 1) % 12;
        if (n <= 84) {
            if (d > worst84) { worst84 = d; argmax84 = cls; }
            if (cls == 9) CHECK(d < 1e-3, "A wheel %d off by %.4f cents", n, d);
        } else if (d > worst_top) {
            worst_top = d;
        }
    }
    CHECK(worst84 > 0.68 && worst84 < 0.73 && argmax84 == 8,
          "worst 1..84 dev %.3f cents (class %d), expected ~0.71 on G#",
          worst84, argmax84);
    CHECK(worst_top > 1.90 && worst_top < 2.05,
          "worst top-seven dev %.3f cents, expected ~1.98", worst_top);
}

static void test_foldback(void) {
    static const int expect_min[9] = { 13, 20, 13, 25, 32, 37, 41, 44, 49 };
    static const int expect_max[9] = { 61, 80, 73, 85, 91, 91, 91, 91, 91 };
    for (int d = 0; d < TW_DRAWBARS; d++) {
        int lo = 99, hi = 0;
        for (int k = 1; k <= TW_KEYS; k++) {
            int w = tw_wheel_index(k, d);
            CHECK(w >= TW_WHEEL_MIN && w <= TW_WHEEL_MAX,
                  "(%d,%d) -> %d out of range", k, d, w);
            if (w < lo) lo = w;
            if (w > hi) hi = w;
        }
        CHECK(lo == expect_min[d] && hi == expect_max[d],
              "drawbar %d spans [%d,%d], expected [%d,%d]",
              d, lo, hi, expect_min[d], expect_max[d]);
    }
    CHECK(tw_wheel_index(25, 2) == 37 && tw_wheel_index(37, 0) == 37,
          "exhibit pair must collide on wheel 37");
    CHECK(tw_wheel_index(1, 0) == 13, "lowest key 16' folds up to 13");
    CHECK(tw_wheel_index(61, 4) == 80, "top key 2-2/3' folds to 80");
    CHECK(tw_wheel_index(61, 7) == 80, "top key 1-1/3' folds to 80");
    CHECK(tw_wheel_index(61, 8) == 85, "top key 1' folds to 85");
    CHECK(tw_wheel_index(-5, 99) >= TW_WHEEL_MIN, "hostile args stay total");
    CHECK(tw_wheel_index(32, 1) == tw_wheel_index(25, 1) + 7,
          "5-1/3' tracks keys chromatically");
}

static void test_drawbar_gain(void) {
    static const float expect[9] = {
        0.0f, 0.09375f, 0.125f, 0.171875f, 0.25f,
        0.34375f, 0.5f, 0.703125f, 1.0f,
    };
    for (int i = 0; i < 9; i++)
        CHECK(TW_DRAWBAR_GAIN[i] == expect[i], "gain[%d] = %.6f",
              i, (double)TW_DRAWBAR_GAIN[i]);
    for (int i = 2; i <= 8; i++) {
        double r = (double)TW_DRAWBAR_GAIN[i] / TW_DRAWBAR_GAIN[i - 1];
        CHECK(r > 1.30 && r < 1.46, "step %d ratio %.4f not ~sqrt(2)", i, r);
    }
    static const int8_t off[9] = { -12, 7, 0, 12, 19, 24, 28, 31, 36 };
    CHECK(memcmp(off, TW_DRAWBAR_OFFSET, sizeof off) == 0, "offset table");
}

static void test_sine_kernel(void) {
    double worst = 0.0;
    for (int i = 0; i <= 200000; i++) {
        float p = (float)(i / 200001.0);
        double err = fabs((double)tw_sin_turns(p) - sin(TAU_D * (double)p));
        if (err > worst) worst = err;
    }
    CHECK(worst < 5e-6, "sine kernel worst error %.2e", worst);
}

static void test_determinism(void) {
    float t[TW_WHEELS] = { 0 };
    t[36] = 1.0f;
    t[43] = 0.7f;
    float out1[4800], out2[4800];
    for (int run = 0; run < 2; run++) {
        tw_generator g;
        tw_generator_init(&g, 48000.0f, 0.001f);
        tw_generator_set_keyed_targets(&g, t);
        float *dst = run ? out2 : out1;
        for (int i = 0; i < 4800; i++) dst[i] = tw_generator_tick(&g).keyed;
    }
    CHECK(memcmp(out1, out2, sizeof out1) == 0, "two runs differ");
    CHECK(tw_fnv1a64(out1, sizeof out1, 0) == tw_fnv1a64(out2, sizeof out2, 0),
          "FNV mismatch");
}

static void test_sanitize_and_smoothing(void) {
    tw_generator g;
    tw_generator_init(&g, 48000.0f, 0.001f);
    float t[TW_WHEELS] = { 0 };
    t[0] = 0.0f / 0.0f;   /* NaN  */
    t[1] = 1.0f / 0.0f;   /* +inf */
    t[2] = -3.0f;
    t[3] = 1e9f;
    t[45] = 1.0f;
    tw_generator_set_keyed_targets(&g, t);
    CHECK(g.keyed_target[0] == 0.0f && g.keyed_target[1] == 16.0f
              && g.keyed_target[2] == 0.0f && g.keyed_target[3] == 16.0f,
          "hostile targets must sanitize");
    for (int i = 0; i < 480; i++) {
        tw_frame f = tw_generator_tick(&g);
        CHECK(f.keyed == f.keyed && f.percussion == f.percussion, "NaN output");
    }
    CHECK(tw_fabsf(g.keyed_gain[45] - 1.0f) < 0.01f,
          "gain %.4f after 10 tau, expected ~1", (double)g.keyed_gain[45]);

    /* decaying bank snaps to target instead of entering denormals */
    float zero[TW_WHEELS] = { 0 };
    tw_generator_set_keyed_targets(&g, zero);
    for (int i = 0; i < 48000; i++) (void)tw_generator_tick(&g);
    CHECK(g.keyed_gain[45] == 0.0f, "gain must snap to exact 0");
}

static void test_output_frequency(void) {
    tw_generator g;
    tw_generator_init(&g, 48000.0f, 0.001f);
    float t[TW_WHEELS] = { 0 };
    t[45] = 1.0f; /* wheel 46 = 440 Hz */
    tw_generator_set_keyed_targets(&g, t);
    for (int i = 0; i < 2400; i++) (void)tw_generator_tick(&g);
    int crossings = 0;
    float prev = tw_generator_tick(&g).keyed;
    for (int i = 0; i < 48000; i++) {
        float s = tw_generator_tick(&g).keyed;
        if ((s > 0.0f) != (prev > 0.0f)) crossings++;
        prev = s;
    }
    CHECK(crossings >= 877 && crossings <= 883,
          "wheel 46: %d crossings/s, expected ~880", crossings);
}

static int feed(tw_midi_parser *p, const uint8_t *bytes, int n, tw_midi_msg *last) {
    int got = 0;
    for (int i = 0; i < n; i++)
        if (tw_midi_parse(p, bytes[i], last)) got++;
    return got;
}

static void test_midi_parser(void) {
    tw_midi_parser p = { 0 };
    tw_midi_msg m = { 0 };

    static const uint8_t on[] = { 0x90, 60, 100 };
    CHECK(feed(&p, on, 3, &m) == 1 && m.status == 0x90 && m.d1 == 60 && m.d2 == 100,
          "plain note-on");

    static const uint8_t running[] = { 62, 0 }; /* running status, vel 0 */
    CHECK(feed(&p, running, 2, &m) == 1 && m.status == 0x90 && m.d1 == 62 && m.d2 == 0,
          "running status persists");

    static const uint8_t rt[] = { 64, 0xF8, 90 }; /* clock mid-message */
    CHECK(feed(&p, rt, 3, &m) == 1 && m.d1 == 64 && m.d2 == 90,
          "real-time byte is transparent");

    static const uint8_t pc[] = { 0xC5, 7 };
    CHECK(feed(&p, pc, 2, &m) == 1 && m.status == 0xC5 && m.d1 == 7 && m.d2 == 0,
          "program change carries one byte");

    static const uint8_t sysex[] = { 0xF0, 1, 2, 3, 0xF7, 60, 60 };
    CHECK(feed(&p, sysex, 7, &m) == 0, "sysex cancels running status");

    static const uint8_t cc[] = { 0xB0, 11, 127 };
    CHECK(feed(&p, cc, 3, &m) == 1 && m.status == 0xB0 && m.d1 == 11 && m.d2 == 127,
          "control change");

    static const uint8_t stray[] = { 42, 42 };
    tw_midi_parser q = { 0 };
    CHECK(feed(&q, stray, 2, &m) == 0, "stray data bytes ignored");
}

/* Taper oracle — the test's own double copy of the constants.md 6.1
 * classes (dB-ladder order) and the section 6/6.1 merge law. */
static const double TG[6] = { 2.2387, 1.4962, 1.0, 0.6683, 0.4467, 0.3162 };
static const double TR[6] = { 10.0, 15.0, 24.0, 34.0, 50.0, 100.0 };

static double merge_contrib(const int *cls, int n) {
    double g = 0.0, inv_rw = 0.0, inv_rtot = 0.0;
    for (int i = 0; i < n; i++) {
        g += TG[cls[i]];
        inv_rw += 1.0 / TR[cls[i]];
        inv_rtot += 1.0 / (5.0 + TR[cls[i]]);
    }
    if (n == 1) return g;
    return g / ((5.0 + 1.0 / inv_rw) * inv_rtot);
}

/* Settle one organ long past stagger+bounce and return a wheel target. */
static double settled_target(tw_organ *o, const uint8_t *reg,
                             const int *notes, int n, int wheel) {
    tw_organ_init(o, 48000.0f);
    tw_organ_set_registration(o, reg);
    for (int i = 0; i < n; i++) tw_organ_note(o, notes[i], true, 127);
    for (int i = 0; i < 480; i++) (void)tw_organ_tick(o);
    return o->gen.keyed_target[wheel - 1];
}

static void test_taper_and_robbing(void) {
    tw_organ o;
    tw_organ_init(&o, 48000.0f);

    /* Tap lists: every (key, bus) lands exactly once; collision census
     * matches constants.md 6.1 — 53 sites of 2-3 keys, 7 of them triple. */
    static const int expect_sites[9] = { 12, 0, 0, 0, 1, 6, 10, 12, 12 };
    int sites = 0, triples = 0, entries = 0;
    for (int b = 0; b < TW_DRAWBARS; b++) {
        int bus_sites = 0;
        int seen[TW_KEYS + 1] = { 0 };
        for (int w = 0; w <= TW_WHEELS; w++) {
            int n = 0;
            while (n < TW_TAP_MAX && o.taps[b][w][n]) {
                CHECK(tw_wheel_index(o.taps[b][w][n], b) == w,
                      "tap (%d,%d) key %d does not fold there",
                      b, w, o.taps[b][w][n]);
                seen[o.taps[b][w][n]]++;
                n++;
            }
            entries += n;
            if (n >= 2) bus_sites++;
            if (n == 3) triples++;
        }
        for (int k = 1; k <= TW_KEYS; k++)
            CHECK(seen[k] == 1, "bus %d key %d in %d tap lists", b, k, seen[k]);
        CHECK(bus_sites == expect_sites[b],
              "bus %d has %d collision sites, expected %d",
              b, bus_sites, expect_sites[b]);
        sites += bus_sites;
    }
    CHECK(entries == TW_KEYS * TW_DRAWBARS, "tap entries %d != 549", entries);
    CHECK(sites == 53 && triples == 7,
          "collision census %d sites / %d triples, expected 53 / 7", sites, triples);

    /* The oracle itself: equal wires collapse to a(k) = 4k/(k+3). */
    static const int eq2[2] = { 1, 1 }, eq3[3] = { 1, 1, 1 };
    CHECK(fabs(merge_contrib(eq2, 2) / TG[1] - 1.6) < 1e-12,
          "equal 15-ohm pair must collapse to a(2)");
    CHECK(fabs(merge_contrib(eq3, 3) / TG[1] - 2.0) < 1e-12,
          "equal 15-ohm triple must collapse to a(3)");

    /* Taper spot checks across run boundaries: a single contact passes
     * its class gain exactly (merge_ratio == 1). MIDI note = key + 35. */
    static const struct { int bus, key, cls; } spot[] = {
        { 0, 10, 5 }, { 0, 11, 4 }, { 0, 61, 0 }, /* 16' break and top   */
        { 2, 24, 2 },                             /* 8' reference run    */
        { 3, 39, 2 }, { 3, 40, 3 },               /* 4' dip shoulder     */
        { 4, 12, 0 },                             /* 2-2/3' bass boost   */
        { 7, 48, 3 },                             /* 1-1/3' break        */
        { 8, 44, 4 },                             /* 1' break            */
    };
    for (size_t i = 0; i < sizeof spot / sizeof *spot; i++) {
        uint8_t reg[TW_DRAWBARS] = { 0 };
        reg[spot[i].bus] = 8;
        int note = spot[i].key + 35;
        double t = settled_target(&o, reg, &note, 1,
                                  tw_wheel_index(spot[i].key, spot[i].bus));
        CHECK(fabs(t - TG[spot[i].cls]) < 1e-6,
              "bus %d key %d: target %f, class gain %f",
              spot[i].bus, spot[i].key, t, TG[spot[i].cls]);
    }

    /* Triple collision, 1' bus wheel 80: keys 32 (24 ohm) + 44 + 56
     * (both 50 ohm) — merge_ratio ~0.8185, the strongest real robbing. */
    static const uint8_t reg1[TW_DRAWBARS] = { 0, 0, 0, 0, 0, 0, 0, 0, 8 };
    static const int trip[3] = { 67, 79, 91 };
    static const int trip_cls[3] = { 2, 4, 4 };
    double want = merge_contrib(trip_cls, 3);
    double t80 = settled_target(&o, reg1, trip, 3, 80);
    CHECK(fabs(t80 - want) < 1e-5,
          "1' triple on wheel 80: %f, law says %f", t80, want);
}

static double organ_rms(tw_organ *o, int frames) {
    double acc = 0.0;
    for (int i = 0; i < frames; i++) {
        double s = tw_organ_tick(o);
        acc += s * s;
    }
    return sqrt(acc / frames);
}

static void run_script(float *dst, int frames, float wear) {
    tw_organ o;
    tw_organ_init(&o, 48000.0f);
    if (wear >= 0.0f) tw_organ_set_wear(&o, wear); /* < 0: shipped default */
    for (int i = 0; i < frames; i++) {
        if (i == 4800) tw_organ_note(&o, 60, true, 90);
        if (i == 9600) tw_organ_set_drawbar(&o, 4, 6);
        if (i == 14400) tw_organ_note(&o, 60, false, 0);
        dst[i] = tw_organ_tick(&o);
    }
}

static void test_organ(void) {
    static const uint8_t reg16[TW_DRAWBARS] = { 8, 0, 0, 0, 0, 0, 0, 0, 0 };
    tw_organ o;
    tw_organ_init(&o, 48000.0f);
    /* M7: the shipped default carries the sec 13 idle floor; contact
     * machinery is probed on the idealized (wear 0) reference. */
    tw_organ_set_wear(&o, 0.0f);
    for (int i = 0; i < 100; i++)
        CHECK(tw_organ_tick(&o) == 0.0f, "idle organ must be silent");

    /* Merge law on a real foldback collision: keys 1 and 13 share
     * wheel 13 on the 16' bus with 100- and 50-ohm wires. One contact
     * passes its taper gain exactly; two merge to merge_ratio ~0.9416
     * times the coherent sum (a(2)-equivalent ~1.88 against the old
     * flat-taper 1.6, but taper puts the pair at ~0.7183 absolute). */
    tw_organ_init(&o, 48000.0f);
    tw_organ_set_registration(&o, reg16);
    tw_organ_note(&o, 36, true, 127);
    for (int i = 0; i < 9600; i++) (void)tw_organ_tick(&o);
    CHECK(tw_fabsf(o.gen.keyed_target[12] - 0.3162f) < 1e-6f,
          "one 16' key-1 contact must pass its -10 dB gain, got %f",
          (double)o.gen.keyed_target[12]);
    tw_organ_note(&o, 48, true, 127);
    for (int i = 0; i < 9600; i++) (void)tw_organ_tick(&o);
    static const int pair_cls[2] = { 5, 4 }; /* 100 ohm + 50 ohm */
    double pair_want = merge_contrib(pair_cls, 2);
    CHECK(fabs((double)o.gen.keyed_target[12] - pair_want) < 1e-5,
          "16'/wheel-13 collision merges to %f, got %f",
          pair_want, (double)o.gen.keyed_target[12]);

    /* velocity must not scale loudness */
    tw_organ a, b;
    tw_organ_init(&a, 48000.0f);
    tw_organ_init(&b, 48000.0f);
    tw_organ_note(&a, 60, true, 127);
    tw_organ_note(&b, 60, true, 1);
    for (int i = 0; i < 9600; i++) { (void)tw_organ_tick(&a); (void)tw_organ_tick(&b); }
    for (int w = 0; w < TW_WHEELS; w++)
        CHECK(a.gen.keyed_target[w] == b.gen.keyed_target[w],
              "velocity leaked into loudness at wheel %d", w + 1);

    /* out-of-compass notes are ignored and counted (idealized: the M7
     * default's idle floor would mask the silence assertion) */
    tw_organ_init(&o, 48000.0f);
    tw_organ_set_wear(&o, 0.0f);
    tw_organ_note(&o, 35, true, 100);
    tw_organ_note(&o, 97, true, 100);
    CHECK(o.out_of_compass == 2, "compass counter, got %u", o.out_of_compass);
    for (int i = 0; i < 4800; i++)
        CHECK(tw_organ_tick(&o) == 0.0f, "compass notes must stay silent");

    /* note-off during the stagger window must settle to silence
     * (idealized, as above) */
    tw_organ_init(&o, 48000.0f);
    tw_organ_set_wear(&o, 0.0f);
    tw_organ_note(&o, 60, true, 1); /* slowest press, ~15 ms stagger */
    for (int i = 0; i < 240; i++) (void)tw_organ_tick(&o); /* 5 ms in */
    tw_organ_note(&o, 60, false, 0);
    for (int i = 0; i < 48000; i++) (void)tw_organ_tick(&o);
    double tail = organ_rms(&o, 4800);
    CHECK(tail < 1e-6, "mid-stagger release leaves residue rms %.2e", tail);

    /* panic empties everything immediately (idealized, as above) */
    tw_organ_init(&o, 48000.0f);
    tw_organ_set_wear(&o, 0.0f);
    tw_organ_note(&o, 60, true, 127);
    tw_organ_note(&o, 64, true, 127);
    for (int i = 0; i < 9600; i++) (void)tw_organ_tick(&o);
    tw_organ_panic(&o);
    for (int i = 0; i < 480; i++) (void)tw_organ_tick(&o); /* one click tau out */
    double after = organ_rms(&o, 4800);
    CHECK(after < 1e-6, "panic residue rms %.2e", after);

    /* swell closes to silence and reopens */
    tw_organ_init(&o, 48000.0f);
    tw_organ_note(&o, 60, true, 127);
    tw_organ_set_swell(&o, 0.0f);
    for (int i = 0; i < 14400; i++) (void)tw_organ_tick(&o); /* ~30 tau */
    CHECK(organ_rms(&o, 4800) < 1e-6, "swell 0 must mute");
    tw_organ_set_swell(&o, 1.0f);
    for (int i = 0; i < 4800; i++) (void)tw_organ_tick(&o);
    CHECK(organ_rms(&o, 4800) > 0.1, "swell 1 must reopen");

    /* two identical scripted runs are bit-identical */
    static float r1[19200], r2[19200];
    run_script(r1, 19200, -1.0f);
    run_script(r2, 19200, -1.0f);
    CHECK(memcmp(r1, r2, sizeof r1) == 0, "scripted runs differ");
    CHECK(tw_fnv1a64(r1, sizeof r1, 0) == tw_fnv1a64(r2, sizeof r2, 0),
          "scripted FNV differs");
}

/* Percussion oracle constants -- the test's own copies of organ.c's
 * private ones, sourced constants.md section 8. */
static const double PERC_TAU_FAST = 0.375, PERC_TAU_SLOW = 1.551; /* ratio 4.133:1 */
static const double PERC_SOFT_PAD = 0.5;              /* [decision] */
static const double PERC_NORMAL_ATTEN = 5.0 / (5.0 + 22.0); /* R_SRC / (R_SRC+R50), [derived] */

static void test_percussion_trigger(void) {
    tw_organ o;
    tw_organ_init(&o, 48000.0f);
    CHECK(o.perc.armed, "fresh organ starts armed");
    tw_organ_set_percussion(&o, true, false, false, true); /* on, 2nd, fast, normal */

    int w60 = tw_wheel_index(60 - TW_NOTE_MIN + 1, 3); /* 2nd harmonic = 4' tap */
    int w67 = tw_wheel_index(67 - TW_NOTE_MIN + 1, 3);

    static const struct { int note; bool down; bool armed_after; } seq[6] = {
        { 60, true,  false }, /* first key of the phrase: triggers        */
        { 64, true,  false }, /* legato add: no retrigger, stays disarmed */
        { 60, false, false }, /* one of two released: other still held    */
        { 64, false, true  }, /* last key up: re-arms                     */
        { 67, true,  false }, /* new phrase: triggers again                */
        { 67, false, true  }, /* release: re-arms                          */
    };
    int expect_wheel[6] = { w60, w60, w60, w60, w67, w67 };
    for (int i = 0; i < 6; i++) {
        tw_organ_note(&o, seq[i].note, seq[i].down, 100);
        CHECK(o.perc.armed == seq[i].armed_after,
              "step %d (note %d %s): armed %d, expected %d", i, seq[i].note,
              seq[i].down ? "down" : "up", o.perc.armed, seq[i].armed_after);
        CHECK(o.perc.wheel == expect_wheel[i],
              "step %d: wheel %d, expected %d", i, o.perc.wheel, expect_wheel[i]);
    }

    /* Percussion off: the state machine still tracks phrase edges, but no
     * trigger ever fires (wheel never picked). */
    tw_organ_init(&o, 48000.0f);
    tw_organ_set_percussion(&o, false, false, false, true);
    tw_organ_note(&o, 60, true, 100);
    CHECK(!o.perc.armed && o.perc.wheel == 0,
          "percussion off: disarms on first key but never picks a wheel");

    /* Wheel selection follows the harmonic switch (sec 4/8). */
    tw_organ_init(&o, 48000.0f);
    tw_organ_set_percussion(&o, true, false, false, true); /* 2nd */
    tw_organ_note(&o, 60, true, 100);
    CHECK(o.perc.wheel == tw_wheel_index(60 - TW_NOTE_MIN + 1, 3),
          "2nd harmonic must tap the 4' bus, got wheel %d", o.perc.wheel);
    tw_organ_init(&o, 48000.0f);
    tw_organ_set_percussion(&o, true, true, false, true); /* 3rd */
    tw_organ_note(&o, 60, true, 100);
    CHECK(o.perc.wheel == tw_wheel_index(60 - TW_NOTE_MIN + 1, 4),
          "3rd harmonic must tap the 2-2/3' bus, got wheel %d", o.perc.wheel);

    /* Hostile setter arguments clamp through the bool parameter conversion. */
    tw_organ_init(&o, 48000.0f);
    tw_organ_set_percussion(&o, 5, -3, 300, 0);
    CHECK(o.perc.on == true && o.perc.third == true && o.perc.slow == true
              && o.perc.normal == false,
          "hostile ints must clamp to true/true/true/false");
}

static long perc_decay_samples(bool slow, double frac) {
    tw_organ o;
    tw_organ_init(&o, 48000.0f);
    tw_organ_set_percussion(&o, true, false, slow, true);
    tw_organ_note(&o, 60, true, 100);
    int w = o.perc.wheel;
    double peak = (double)o.gen.perc_target[w - 1];
    long n = 0;
    while ((double)o.gen.perc_target[w - 1] > peak * frac && n < 10L * 48000) {
        (void)tw_organ_tick(&o);
        n++;
    }
    return n;
}

static void test_percussion_decay(void) {
    double frac = exp(-1.0);
    long n_fast = perc_decay_samples(false, frac);
    long n_slow = perc_decay_samples(true, frac);
    double ratio = (double)n_slow / (double)n_fast;
    CHECK(ratio > 4.0 && ratio < 4.3,
          "decay ratio slow/fast = %.3f, expected ~4.133", ratio);
    double want_fast = PERC_TAU_FAST * 48000.0;
    double want_slow = PERC_TAU_SLOW * 48000.0;
    CHECK(fabs((double)n_fast - want_fast) / want_fast < 0.02,
          "fast tau %ld samples, expected ~%.0f", n_fast, want_fast);
    CHECK(fabs((double)n_slow - want_slow) / want_slow < 0.02,
          "slow tau %ld samples, expected ~%.0f", n_slow, want_slow);
}

static void test_percussion_levels(void) {
    /* Ninth-drawbar theft: the 1' bus contributes nothing once percussion
     * is on, regardless of registration. */
    tw_organ o;
    static const uint8_t reg1[TW_DRAWBARS] = { 0, 0, 0, 0, 0, 0, 0, 0, 8 };
    tw_organ_init(&o, 48000.0f);
    tw_organ_set_registration(&o, reg1);
    tw_organ_note(&o, 60, true, 127);
    for (int i = 0; i < 9600; i++) (void)tw_organ_tick(&o);
    double before = 0.0;
    for (int w = 0; w < TW_WHEELS; w++) before += (double)o.gen.keyed_target[w];
    CHECK(before > 0.0, "1' bus must sound before percussion is on");

    tw_organ_set_percussion(&o, true, false, false, true);
    double after = 0.0;
    for (int w = 0; w < TW_WHEELS; w++) after += (double)o.gen.keyed_target[w];
    CHECK(after == 0.0, "1' bus must be muted while percussion is on, got %f", after);

    /* NORMAL attenuates the borrowed bus's sustained tone by the pinned
     * R50/R_SRC ratio; SOFT leaves the sustained tone alone. */
    static const uint8_t reg4[TW_DRAWBARS] = { 0, 0, 0, 8, 0, 0, 0, 0, 0 }; /* 4' only */
    tw_organ_init(&o, 48000.0f);
    tw_organ_set_registration(&o, reg4);
    tw_organ_note(&o, 60, true, 127);
    for (int i = 0; i < 9600; i++) (void)tw_organ_tick(&o);
    int w4 = tw_wheel_index(60 - TW_NOTE_MIN + 1, 3);
    double base = (double)o.gen.keyed_target[w4 - 1];

    tw_organ_note(&o, 60, false, 0);
    for (int i = 0; i < 480; i++) (void)tw_organ_tick(&o);
    tw_organ_set_percussion(&o, true, false, false, true); /* NORMAL */
    tw_organ_note(&o, 60, true, 127);
    for (int i = 0; i < 9600; i++) (void)tw_organ_tick(&o);
    double normal = (double)o.gen.keyed_target[w4 - 1];
    CHECK(fabs(normal / base - PERC_NORMAL_ATTEN) < 1e-4,
          "NORMAL attenuation ratio %f, expected %f", normal / base, PERC_NORMAL_ATTEN);

    tw_organ_note(&o, 60, false, 0);
    for (int i = 0; i < 480; i++) (void)tw_organ_tick(&o);
    tw_organ_set_percussion(&o, true, false, false, false); /* SOFT */
    tw_organ_note(&o, 60, true, 127);
    for (int i = 0; i < 9600; i++) (void)tw_organ_tick(&o);
    double soft = (double)o.gen.keyed_target[w4 - 1];
    CHECK(fabs(soft - base) < 1e-6,
          "SOFT must leave the sustained tone alone, got %f vs base %f", soft, base);

    /* SOFT pads percussion's own peak; NORMAL does not. */
    tw_organ_init(&o, 48000.0f);
    tw_organ_set_percussion(&o, true, false, false, true); /* NORMAL */
    tw_organ_note(&o, 60, true, 100);
    double peak_normal = (double)o.gen.perc_target[o.perc.wheel - 1];
    tw_organ_init(&o, 48000.0f);
    tw_organ_set_percussion(&o, true, false, false, false); /* SOFT */
    tw_organ_note(&o, 60, true, 100);
    double peak_soft = (double)o.gen.perc_target[o.perc.wheel - 1];
    CHECK(fabs(peak_soft / peak_normal - PERC_SOFT_PAD) < 1e-6,
          "SOFT peak ratio %f, expected %f", peak_soft / peak_normal, PERC_SOFT_PAD);
}

/* --- M4: vibrato/chorus scanner (constants.md section 9) --- */

static double scan_mag(const float *h, int n, double f, double fs) {
    double re = 0.0, im = 0.0;
    for (int i = 0; i < n; i++) {
        double w = TAU_D * f * (double)i / fs;
        re += (double)h[i] * cos(w);
        im -= (double)h[i] * sin(w);
    }
    return sqrt(re * re + im * im);
}

/* Highest frequency (4..9 kHz scan) still above thresh x |H(500)| —
 * robust to the near-edge ripple the mismatched termination creates. */
static double scan_edge(const float *h, int n, double fs, double thresh) {
    double ref = scan_mag(h, n, 500.0, fs);
    double edge = 0.0;
    for (double f = 4000.0; f <= 9000.0; f += 50.0)
        if (scan_mag(h, n, f, fs) > thresh * ref) edge = f;
    return edge;
}

static float line_h[32768];

static void line_impulse(double fs, int n) {
    tw_scanner s;
    tw_scanner_init(&s, (float)fs);
    s.mode = TW_VIB_V1;
    for (int i = 0; i < n; i++) {
        (void)tw_scanner_tick(&s, 0.0f, i == 0 ? 1.0f : 0.0f);
        line_h[i] = s.vn[18]; /* v19: the line's terminated far end */
    }
}

static void test_scanner_line(void) {
    /* impulse timing at 48 kHz: idealized total delay ~0.85 ms (sec 9) */
    line_impulse(48000.0, 8192);
    int peak = 2;
    for (int i = 1; i < 8192; i++)
        if (tw_fabsf(line_h[i]) > tw_fabsf(line_h[peak])) peak = i;
    CHECK(peak >= 30 && peak <= 62,
          "line impulse peak at %.2f ms, expected ~0.8", peak / 48.0);

    /* dispersion: the impulse smears along the line — no clean spike */
    double e_tot = 0.0, e_peak = 0.0;
    for (int i = 0; i < 8192; i++) e_tot += (double)line_h[i] * line_h[i];
    for (int i = peak - 2; i <= peak + 2; i++)
        e_peak += (double)line_h[i] * line_h[i];
    CHECK(e_tot > 0.0 && e_peak / e_tot < 0.5,
          "undispersed impulse: %.2f of energy in 5 samples", e_peak / e_tot);

    /* passband edge: ~7075 Hz analog [DAFx16]; plain bilinear warps it
     * ~6% low at 48 kHz (sec 9 [decision]) and converges at 192 kHz */
    double edge48 = scan_edge(line_h, 8192, 48000.0, 0.5);
    CHECK(edge48 > 6200.0 && edge48 < 7200.0,
          "48 kHz passband edge %.0f Hz, expected ~6.7 kHz", edge48);
    double ref48 = scan_mag(line_h, 8192, 500.0, 48000.0);
    CHECK(scan_mag(line_h, 8192, 3000.0, 48000.0) / ref48 > 0.25,
          "3 kHz must sit in the passband");
    CHECK(scan_mag(line_h, 8192, 8500.0, 48000.0) / ref48 < 0.01,
          "8.5 kHz must sit far in the stopband");

    line_impulse(192000.0, 32768);
    double edge192 = scan_edge(line_h, 32768, 192000.0, 0.5);
    CHECK(edge192 > 6600.0 && edge192 < 7600.0,
          "192 kHz passband edge %.0f Hz, expected ~7.1 kHz", edge192);
    CHECK(scan_mag(line_h, 32768, 8500.0, 192000.0)
              / scan_mag(line_h, 32768, 500.0, 192000.0) < 0.01,
          "8.5 kHz must sit far in the stopband at 192 kHz");
}

static void test_scanner_sweep(void) {
    tw_scanner s;
    tw_scanner_init(&s, 48000.0f);
    s.mode = TW_VIB_V1;
    /* rate: one second advances 412/60 = 6.8667 turns (sec 9) */
    for (int i = 0; i < 48000; i++) (void)tw_scanner_tick(&s, 0.0f, 0.0f);
    CHECK(tw_fabsf(s.phase - 0.86667f) < 3e-3f,
          "rotor after 1 s at %f turns, expected ~0.8667", (double)s.phase);

    /* Charge the line to DC steady state: every node -> input = 1. The
     * lossless interior rings a slowly-decaying near-cutoff mode (the
     * termination barely damps it), so give it 2 s, and take every
     * paired reading from a copy of one frozen state — consecutive
     * ticks still wobble at the f32 noise floor. */
    tw_scanner_init(&s, 48000.0f);
    s.mode = TW_VIB_V1;
    for (int i = 0; i < 96000; i++) (void)tw_scanner_tick(&s, 0.0f, 1.0f);
    CHECK(tw_fabsf(s.vn[18] - 1.0f) < 1e-4f,
          "line DC gain at v19: %f", (double)s.vn[18]);

    /* plate boundaries read single terminals: t1 through its -2.9 dB
     * divider (68/95), t9 (plate 8 = phase 0.5) undivided */
    tw_scanner t = s;
    t.phase = 0.0f;
    float y1 = tw_scanner_tick(&t, 0.0f, 1.0f);
    CHECK(fabs((double)y1 - 68.0 / 95.0) < 1e-4, "t1 at DC: %f", (double)y1);
    t = s;
    t.phase = 0.5f;
    float y9 = tw_scanner_tick(&t, 0.0f, 1.0f);
    CHECK(fabs((double)y9 - 1.0) < 1e-4, "t9 at DC: %f", (double)y9);

    /* there-and-back symmetry: mirrored rotor angles read the same
     * crossfade of the same terminal pair */
    static const float phi[5] = { 0.03f, 0.11f, 0.23f, 0.37f, 0.44f };
    for (int i = 0; i < 5; i++) {
        t = s;
        t.phase = phi[i];
        float ya = tw_scanner_tick(&t, 0.0f, 1.0f);
        t = s;
        t.phase = 1.0f - phi[i];
        float yb = tw_scanner_tick(&t, 0.0f, 1.0f);
        CHECK(tw_fabsf(ya - yb) < 1e-5f,
              "sweep asymmetric at %.2f: %f vs %f",
              (double)phi[i], (double)ya, (double)yb);
    }
}

static void test_scanner_modes(void) {
    tw_scanner s;
    tw_scanner_init(&s, 48000.0f);
    s.mode = TW_VIB_V1;
    for (int i = 0; i < 96000; i++) (void)tw_scanner_tick(&s, 0.0f, 1.0f);

    /* C = (dry + swept)/2 against its V twin, per depth and position
     * ([decision] sec 9, [P39] equal-power precedent); both of a pair
     * read from a copy of the same frozen line state */
    static const float pos[2] = { 0.0f, 0.5f };
    for (int d = 0; d < 3; d++)
        for (int i = 0; i < 2; i++) {
            tw_scanner t = s;
            t.mode = TW_VIB_V1 + d;
            t.phase = pos[i];
            float yv = tw_scanner_tick(&t, 0.0f, 1.0f);
            t = s;
            t.mode = TW_VIB_C1 + d;
            t.phase = pos[i];
            float yc = tw_scanner_tick(&t, 0.0f, 1.0f);
            CHECK(tw_fabsf(yc - 0.5f * (1.0f + yv)) < 1e-6f,
                  "C%d at %.1f: %f, V twin %f", d + 1,
                  (double)pos[i], (double)yc, (double)yv);
        }

    /* depth spans (sec 9 tap tables): at t9, V1 reads v9 (~8 sections),
     * V3 reads v19 (the whole line) — impulse arrivals must order */
    int peaks[2];
    static const int modes[2] = { TW_VIB_V1, TW_VIB_V3 };
    for (int m = 0; m < 2; m++) {
        tw_scanner_init(&s, 48000.0f);
        s.mode = modes[m];
        int peak = 0;
        float pv = 0.0f;
        for (int i = 0; i < 128; i++) {
            s.phase = 0.5f; /* pin the pickup on t9 */
            float y = tw_scanner_tick(&s, 0.0f, i == 0 ? 1.0f : 0.0f);
            if (tw_fabsf(y) > pv) { pv = tw_fabsf(y); peak = i; }
        }
        peaks[m] = peak;
    }
    CHECK(peaks[0] >= 10 && peaks[0] <= 28,
          "V1 t9 impulse at %d samples, expected ~17", peaks[0]);
    CHECK(peaks[1] >= 30 && peaks[1] <= 62,
          "V3 t9 impulse at %d samples, expected ~39", peaks[1]);
    CHECK(peaks[1] > peaks[0] + 8, "V3 must read a longer line than V1");
}

static void test_scanner_bass_split(void) {
    /* generator: keyed_low is exactly the wheels 1..16 prefix */
    tw_generator g;
    tw_generator_init(&g, 48000.0f, 0.001f);
    float t[TW_WHEELS] = { 0 };
    for (int i = 0; i < TW_SCAN_LOW_WHEELS; i++) t[i] = 1.0f;
    tw_generator_set_keyed_targets(&g, t);
    for (int i = 0; i < 200; i++) {
        tw_frame f = tw_generator_tick(&g);
        CHECK(f.keyed_low == f.keyed, "low-only bank: keyed_low != keyed");
    }
    tw_generator_init(&g, 48000.0f, 0.001f);
    float t2[TW_WHEELS] = { 0 };
    for (int i = TW_SCAN_LOW_WHEELS; i < TW_WHEELS; i++) t2[i] = 1.0f;
    tw_generator_set_keyed_targets(&g, t2);
    for (int i = 0; i < 200; i++) {
        tw_frame f = tw_generator_tick(&g);
        CHECK(f.keyed_low == 0.0f, "high-only bank leaked into keyed_low");
    }

    /* organ: 16'-only lowest key sounds wheel 13 alone (65.4 Hz), all of
     * it below the sec 9 bass line — V3 on is bit-identical to off.
     * Idealized (wear 0): the M7 bleed bus is full-band and rides the
     * line, which is its own test — this one probes the bass split. */
    static const uint8_t reg16[TW_DRAWBARS] = { 8, 0, 0, 0, 0, 0, 0, 0, 0 };
    static float on_buf[24000], off_buf[24000];
    tw_organ a, b;
    tw_organ_init(&a, 48000.0f);
    tw_organ_set_wear(&a, 0.0f);
    tw_organ_set_registration(&a, reg16);
    tw_organ_set_vibrato(&a, TW_VIB_V3);
    tw_organ_note(&a, 36, true, 127);
    for (int i = 0; i < 24000; i++) on_buf[i] = tw_organ_tick(&a);
    tw_organ_init(&b, 48000.0f);
    tw_organ_set_wear(&b, 0.0f);
    tw_organ_set_registration(&b, reg16);
    tw_organ_note(&b, 36, true, 127);
    for (int i = 0; i < 24000; i++) off_buf[i] = tw_organ_tick(&b);
    CHECK(memcmp(on_buf, off_buf, sizeof on_buf) == 0,
          "sub-80 Hz registration must pass the scanner untouched");

    /* ...and a high wheel must not: 8' top key = wheel 73 (2092 Hz) */
    static const uint8_t reg8[TW_DRAWBARS] = { 0, 0, 8, 0, 0, 0, 0, 0, 0 };
    tw_organ_init(&a, 48000.0f);
    tw_organ_set_registration(&a, reg8);
    tw_organ_set_vibrato(&a, TW_VIB_V3);
    tw_organ_note(&a, 96, true, 127);
    for (int i = 0; i < 24000; i++) on_buf[i] = tw_organ_tick(&a);
    tw_organ_init(&b, 48000.0f);
    tw_organ_set_registration(&b, reg8);
    tw_organ_note(&b, 96, true, 127);
    for (int i = 0; i < 24000; i++) off_buf[i] = tw_organ_tick(&b);
    CHECK(memcmp(on_buf, off_buf, sizeof on_buf) != 0,
          "a 2 kHz wheel must be audibly swept by V3");
}

static void run_vib_script(float *dst, int frames, float wear) {
    tw_organ o;
    tw_organ_init(&o, 48000.0f);
    if (wear >= 0.0f) tw_organ_set_wear(&o, wear);
    for (int i = 0; i < frames; i++) {
        if (i == 2400) tw_organ_note(&o, 72, true, 100);
        if (i == 4800) tw_organ_set_vibrato(&o, TW_VIB_V3);
        if (i == 9600) tw_organ_set_vibrato(&o, TW_VIB_C2);
        if (i == 14400) tw_organ_note(&o, 72, false, 0);
        dst[i] = tw_organ_tick(&o);
    }
}

static void test_scanner_wiring(void) {
    tw_organ o;
    tw_organ_init(&o, 48000.0f);
    CHECK(o.scan.mode == TW_VIB_OFF, "fresh organ must start vibrato-off");
    tw_organ_set_vibrato(&o, 99);
    CHECK(o.scan.mode == TW_VIB_C3, "hostile high mode must clamp to C3");
    tw_organ_set_vibrato(&o, -5);
    CHECK(o.scan.mode == TW_VIB_OFF, "hostile low mode must clamp to OFF");

    /* switching OFF mid-note rejoins the scannerless render exactly:
     * the scanner owns no generator or RNG state */
    static float a_buf[4800], b_buf[4800];
    tw_organ a, b;
    tw_organ_init(&a, 48000.0f);
    tw_organ_note(&a, 60, true, 100);
    for (int i = 0; i < 4800; i++) (void)tw_organ_tick(&a);
    tw_organ_set_vibrato(&a, TW_VIB_V3);
    for (int i = 0; i < 4800; i++) (void)tw_organ_tick(&a);
    tw_organ_set_vibrato(&a, TW_VIB_OFF);
    for (int i = 0; i < 4800; i++) a_buf[i] = tw_organ_tick(&a);
    tw_organ_init(&b, 48000.0f);
    tw_organ_note(&b, 60, true, 100);
    for (int i = 0; i < 2 * 4800; i++) (void)tw_organ_tick(&b);
    for (int i = 0; i < 4800; i++) b_buf[i] = tw_organ_tick(&b);
    CHECK(memcmp(a_buf, b_buf, sizeof a_buf) == 0,
          "OFF after V3 must be bit-identical to never-on");

    /* scripted vibrato render: two runs bit-identical */
    static float r1[19200], r2[19200];
    run_vib_script(r1, 19200, -1.0f);
    run_vib_script(r2, 19200, -1.0f);
    CHECK(memcmp(r1, r2, sizeof r1) == 0, "vibrato script runs differ");
    CHECK(tw_fnv1a64(r1, sizeof r1, 0) == tw_fnv1a64(r2, sizeof r2, 0),
          "vibrato FNV differs");
}

/* --- M5: drive — saturator, bias follower, coupling cap, instrument
 * (constants.md section 14.1) --- */

/* Drive oracle constants — the test's own copies of section 14.1. */
static const double DRIVE_ATK_S = 0.005, DRIVE_REL_S = 0.050; /* [decision] */
static const double DRIVE_HP_HZ = 10.0;                       /* [decision] */

/* pregain / X_ref for a knob value: (1 + 7 v^2) / 8. */
static double drive_pre(double v) { return (1.0 + 7.0 * v * v) / 8.0; }

static void test_saturator(void) {
    /* odd symmetry is bit-exact: every operation is sign-symmetric */
    int odd_bad = 0;
    for (int i = 0; i <= 4000; i++) {
        float x = (float)i * 0.001f; /* 0..4, past the clamp */
        if (tw_sat(-x) != -tw_sat(x)) odd_bad++;
    }
    CHECK(odd_bad == 0, "saturator odd symmetry broken at %d points", odd_bad);

    /* bounded, monotone, and tanh-shaped (worst deviation ~0.024).
     * Tolerances are a few ULP at 1.0: the rational's flat top wiggles
     * by one rounding step in f32; the true curve is exactly bounded
     * and monotone, and the clamp region pins +-1 exactly (below). */
    double worst = 0.0, prev = -2.0;
    int mono_bad = 0, bound_bad = 0;
    for (int i = -30000; i <= 30000; i++) {
        double x = (double)i * 1e-4;
        double y = (double)tw_sat((float)x);
        if (fabs(y) > 1.0 + 3e-7) bound_bad++;
        if (y < prev - 3e-7) mono_bad++;
        prev = y;
        double err = fabs(y - tanh(x));
        if (err > worst) worst = err;
    }
    CHECK(bound_bad == 0, "saturator exceeds +-1 at %d points", bound_bad);
    CHECK(mono_bad == 0, "saturator non-monotone at %d points", mono_bad);
    CHECK(worst < 0.03, "saturator vs tanh worst deviation %.4f", worst);

    /* spot values: exact bound, unit small-signal slope, C1 clamp */
    CHECK(tw_sat(0.0f) == 0.0f, "sat(0) must be exact 0");
    CHECK(tw_sat(3.0f) == 1.0f && tw_sat(100.0f) == 1.0f,
          "clamp range must pin the bound at exactly 1");
    CHECK(tw_sat(-3.0f) == -1.0f && tw_sat(-100.0f) == -1.0f,
          "negative bound must pin at exactly -1");
    CHECK(fabs((double)tw_sat(1e-3f) / 1e-3 - 1.0) < 1e-4,
          "unit slope at 0, got %f", (double)tw_sat(1e-3f) / 1e-3);
    CHECK(tw_fabsf(tw_sat(3.0f) - tw_sat(2.999f)) < 1e-6f,
          "clamp join must be tangent (zero slope at x = 3)");
}

static void test_drive_follower(void) {
    tw_drive d;
    tw_drive_init(&d, 48000.0f);
    CHECK(d.drive == 0.0f, "fresh drive must start bypassed");

    /* bypass is exact identity and leaves state untouched */
    int id_bad = 0;
    for (int i = 0; i < 200; i++) {
        float x = (float)(i - 100) * 0.05f;
        if (tw_drive_tick(&d, x) != x) id_bad++;
    }
    CHECK(id_bad == 0, "drive 0 bypass broke identity at %d samples", id_bad);
    CHECK(d.env == 0.0f && d.hp_lp == 0.0f, "bypass must leave state untouched");

    /* hostile knob values sanitize */
    tw_drive_set(&d, 0.0f / 0.0f);
    CHECK(d.drive == 0.0f, "NaN drive must clamp to 0");
    tw_drive_set(&d, -4.0f);
    CHECK(d.drive == 0.0f, "negative drive must clamp to 0");
    tw_drive_set(&d, 9.0f);
    CHECK(d.drive == 1.0f, "huge drive must clamp to 1");
    CHECK(fabs((double)d.pre - drive_pre(1.0)) < 1e-7 && d.pre == 1.0f,
          "drive 1: pregain 8 over X_ref 8 must land at exactly 1");

    /* attack tau: at drive 1 (pre = 1) a step to |in| = 1 crosses
     * 1 - 1/e in ~tau_atk (sec 14.1: 5 ms = 240 samples at 48 kHz) */
    tw_drive_init(&d, 48000.0f);
    tw_drive_set(&d, 1.0f);
    double knee = 1.0 - exp(-1.0);
    int n = 0;
    while ((double)d.env < knee && n < 48000) {
        (void)tw_drive_tick(&d, 1.0f);
        n++;
    }
    double want_atk = DRIVE_ATK_S * 48000.0;
    CHECK(fabs((double)n - want_atk) <= 12.0,
          "attack tau %d samples, expected ~%.0f", n, want_atk);

    /* park the follower at 1 (f32 one-pole stalls ~1e-5 shy of a
     * nonzero target; the 1e-9 snap is for the zero-target decay),
     * then release: 1/e in ~tau_rel (50 ms = 2400 samples) */
    for (int i = 0; i < 48000; i++) (void)tw_drive_tick(&d, 1.0f);
    CHECK(tw_fabsf(d.env - 1.0f) < 1e-4f,
          "follower must settle at its target, env %f", (double)d.env);
    n = 0;
    while ((double)d.env > exp(-1.0) && n < 480000) {
        (void)tw_drive_tick(&d, 0.0f);
        n++;
    }
    double want_rel = DRIVE_REL_S * 48000.0;
    CHECK(fabs((double)n - want_rel) <= 120.0,
          "release tau %d samples, expected ~%.0f", n, want_rel);

    /* long silence snaps the whole stage to exact zero */
    for (int i = 0; i < 480000; i++) (void)tw_drive_tick(&d, 0.0f);
    CHECK(d.env == 0.0f, "env must snap to exact 0");
    CHECK(d.hp_lp == 0.0f, "coupling-cap state must snap to exact 0");
    CHECK(tw_drive_tick(&d, 0.0f) == 0.0f, "silent stage must output exact 0");

    /* the operating-point shift: a loud steady tone makes the follower
     * ride, and the bias drags the shaper's DC image negative — the
     * mechanism behind the even-harmonic bloom (sec 14.1) */
    tw_drive_init(&d, 48000.0f);
    tw_drive_set(&d, 0.8f);
    double ph = 0.0;
    for (int i = 0; i < 48000; i++) {
        (void)tw_drive_tick(&d, 2.0f * tw_sin_turns((float)ph));
        ph += 220.0 / 48000.0;
        if (ph >= 1.0) ph -= 1.0;
    }
    CHECK(d.env > 0.5f && d.env < 1.4f,
          "follower must ride the loud tone, env %f", (double)d.env);
    CHECK(d.hp_lp < -0.02f,
          "bias must pull the DC image negative, got %f", (double)d.hp_lp);
}

static float dbuf_a[48000], dbuf_b[48000];

static void test_drive_harmonics(void) {
    /* even/odd proxy (sec 14): the biased stage blooms H2 on a steady
     * loud tone; the bare odd shaper (same kernel, same pregain/makeup,
     * no state) cannot make even harmonics at all. 220 Hz = 220 exact
     * cycles per 48000-sample window. */
    tw_drive d;
    tw_drive_init(&d, 48000.0f);
    tw_drive_set(&d, 0.8f);
    float pre = (float)drive_pre(0.8), post = 1.0f / pre;
    double ph = 0.0;
    for (int i = 0; i < 96000; i++) {
        float x = 2.0f * tw_sin_turns((float)ph);
        float y = tw_drive_tick(&d, x);
        if (i >= 48000) {
            dbuf_a[i - 48000] = y;
            dbuf_b[i - 48000] = post * tw_sat(pre * x);
        }
        ph += 220.0 / 48000.0;
        if (ph >= 1.0) ph -= 1.0;
    }
    double h1 = scan_mag(dbuf_a, 48000, 220.0, 48000.0);
    double h2 = scan_mag(dbuf_a, 48000, 440.0, 48000.0);
    double n1 = scan_mag(dbuf_b, 48000, 220.0, 48000.0);
    double n2 = scan_mag(dbuf_b, 48000, 440.0, 48000.0);
    double n3 = scan_mag(dbuf_b, 48000, 660.0, 48000.0);
    CHECK(h2 / h1 > 0.02, "biased stage H2/H1 %.4f, expected a bloom", h2 / h1);
    CHECK(n2 / n1 < 1e-4, "bare odd shaper must have no H2, got %.2e", n2 / n1);
    CHECK(n3 / n1 > 0.01, "bare shaper must still make odd harmonics");
}

/* Small-signal gain of the whole stage at one frequency by DFT (immune
 * to the follower-ripple sidebands): |out bin| / (A N / 2). */
static double drive_tone_gain(double f_hz, double amp, float drive) {
    tw_drive d;
    tw_drive_init(&d, 48000.0f);
    tw_drive_set(&d, drive);
    double ph = 0.0;
    for (int i = 0; i < 144000; i++) {
        float y = tw_drive_tick(&d, (float)(amp * sin(TAU_D * ph)));
        if (i >= 96000) dbuf_a[i - 96000] = y;
        ph += f_hz / 48000.0;
        if (ph >= 1.0) ph -= 1.0;
    }
    return scan_mag(dbuf_a, 48000, f_hz, 48000.0) / (amp * 48000.0 / 2.0);
}

static void test_drive_highpass(void) {
    /* the coupling-cap pole (sec 14.1): -3 dB at 10 Hz, transparent in
     * the passband, DC fully rejected. Measured small-signal so the
     * shaper stays in its unit-slope region. */
    double g10 = drive_tone_gain(DRIVE_HP_HZ, 0.002, 0.1f);
    CHECK(g10 > 0.68 && g10 < 0.73,
          "|H(10 Hz)| = %.4f, expected ~0.707 (-3 dB)", g10);
    double g40 = drive_tone_gain(40.0, 0.002, 0.1f);
    CHECK(g40 > 0.955 && g40 < 1.005,
          "|H(40 Hz)| = %.4f, expected ~0.970", g40);
    double g200 = drive_tone_gain(200.0, 0.002, 0.1f);
    CHECK(g200 > 0.985 && g200 < 1.005,
          "|H(200 Hz)| = %.4f, passband must be transparent", g200);

    tw_drive d;
    tw_drive_init(&d, 48000.0f);
    tw_drive_set(&d, 0.5f);
    for (int i = 0; i < 192000; i++) (void)tw_drive_tick(&d, 0.01f);
    float y = tw_drive_tick(&d, 0.01f);
    CHECK(tw_fabsf(y) < 1e-5f, "DC must be blocked, got %e", (double)y);
}

static void run_drive_script(float *dst, int frames, float wear) {
    tw_instrument ins;
    tw_instrument_init(&ins, 48000.0f);
    if (wear >= 0.0f) tw_organ_set_wear(&ins.organ, wear);
    for (int i = 0; i < frames; i++) {
        if (i == 2400) {
            tw_organ_note(&ins.organ, 60, true, 100);
            tw_organ_note(&ins.organ, 67, true, 100);
        }
        if (i == 4800) tw_instrument_set_drive(&ins, 0.6f);
        if (i == 7200) tw_organ_set_swell(&ins.organ, 0.4f);
        if (i == 12000) tw_instrument_set_drive(&ins, 0.9f);
        if (i == 14400) {
            tw_organ_note(&ins.organ, 60, false, 0);
            tw_organ_note(&ins.organ, 67, false, 0);
        }
        dst[i] = tw_instrument_tick(&ins);
    }
}

static void test_instrument(void) {
    /* drive 0 (the default): the instrument is bit-identical to the bare
     * organ over a real script — the M5 identity contract */
    static float ia[19200], ib[19200];
    tw_instrument ins;
    tw_instrument_init(&ins, 48000.0f);
    CHECK(ins.drive.drive == 0.0f, "fresh instrument must start at drive 0");
    tw_organ o;
    tw_organ_init(&o, 48000.0f);
    for (int i = 0; i < 19200; i++) {
        if (i == 4800) {
            tw_organ_note(&ins.organ, 60, true, 90);
            tw_organ_note(&o, 60, true, 90);
        }
        if (i == 9600) {
            tw_organ_set_drawbar(&ins.organ, 4, 6);
            tw_organ_set_drawbar(&o, 4, 6);
        }
        if (i == 14400) {
            tw_organ_note(&ins.organ, 60, false, 0);
            tw_organ_note(&o, 60, false, 0);
        }
        ia[i] = tw_instrument_tick(&ins);
        ib[i] = tw_organ_tick(&o);
    }
    CHECK(memcmp(ia, ib, sizeof ia) == 0,
          "drive 0 instrument must be bit-identical to the organ");
    CHECK(tw_fnv1a64(ia, sizeof ia, 0) == tw_fnv1a64(ib, sizeof ib, 0),
          "drive 0 FNV differs from the organ's");

    /* silence stays exactly silent at any drive (idealized: the M7
     * default's idle floor is by design nonzero and drive-visible) */
    tw_instrument_init(&ins, 48000.0f);
    tw_organ_set_wear(&ins.organ, 0.0f);
    tw_instrument_set_drive(&ins, 1.0f);
    int noisy = 0;
    for (int i = 0; i < 4800; i++)
        if (tw_instrument_tick(&ins) != 0.0f) noisy++;
    CHECK(noisy == 0, "idle instrument at full drive leaked %d samples", noisy);

    /* a nonzero knob must actually drive, and turning it back to 0
     * rejoins the bypass render bit-exactly mid-note (scanner-OFF
     * discipline: the stage owns no organ or RNG state) */
    tw_instrument a, b;
    tw_instrument_init(&a, 48000.0f);
    tw_instrument_init(&b, 48000.0f);
    tw_organ_note(&a.organ, 60, true, 100);
    tw_organ_note(&b.organ, 60, true, 100);
    for (int i = 0; i < 4800; i++) {
        (void)tw_instrument_tick(&a);
        (void)tw_instrument_tick(&b);
    }
    tw_instrument_set_drive(&a, 0.7f);
    int ndiff = 0;
    for (int i = 0; i < 4800; i++)
        if (tw_instrument_tick(&a) != tw_instrument_tick(&b)) ndiff++;
    CHECK(ndiff > 1000, "drive 0.7 changed only %d of 4800 samples", ndiff);
    tw_instrument_set_drive(&a, 0.0f);
    for (int i = 0; i < 4800; i++) ia[i] = tw_instrument_tick(&a);
    for (int i = 0; i < 4800; i++) ib[i] = tw_instrument_tick(&b);
    CHECK(memcmp(ia, ib, 4800 * sizeof(float)) == 0,
          "drive back to 0 must rejoin the bypass render exactly");

    /* scripted two-run determinism with the knob moving */
    static float r1[19200], r2[19200];
    run_drive_script(r1, 19200, -1.0f);
    run_drive_script(r2, 19200, -1.0f);
    CHECK(memcmp(r1, r2, sizeof r1) == 0, "drive script runs differ");
    CHECK(tw_fnv1a64(r1, sizeof r1, 0) == tw_fnv1a64(r2, sizeof r2, 0),
          "drive FNV differs");
}

/* --- M6: rotary speaker (constants.md sections 15/15.1) --- */

/* Rotary oracle constants — the test's own copies of section 15.1. */
static const double ROT_HORN_TREM = 400.0 / 60.0; /* [FOLK]              */
static const double ROT_DRUM_TREM = 340.0 / 60.0; /* [FOLK]              */
static const double ROT_HORN_RISE_TAU = 1.0 / 3.0; /* [FOLK] rise / 3    */
static const double ROT_DRUM_RISE_TAU = 2.5 / 3.0; /* [decision]         */
static const double ROT_DRUM_FALL_TAU = 6.5 / 3.0; /* [RS] 5-8 s mid / 3 */
static const double ROT_DOP_BASE_S = 0.0010, ROT_DOP_AMP_S = 0.0003;
static const double ROT_HORN_AM = 0.40, ROT_DRUM_AM = 0.15; /* [decision] */
static const double ROT_MIC = 0.125; /* mic angle, turns [decision]       */

/* Complex DFT bin (scan_mag is its magnitude; the Doppler test needs
 * the phase too). */
static void dft_at(const float *h, int n, double f, double fs,
                   double *re, double *im) {
    double a = 0.0, b = 0.0;
    for (int i = 0; i < n; i++) {
        double w = TAU_D * f * (double)i / fs;
        a += (double)h[i] * cos(w);
        b -= (double)h[i] * sin(w);
    }
    *re = a;
    *im = b;
}

/* Box-averaged quadrature demodulation at f_hz: env (2|IQ|) and
 * instantaneous frequency per output point; returns the point count.
 * The frequency is the phase slope over a win-sample stride, not one
 * sample: per-sample differentiation amplifies the box filter's tiny
 * leaked 2f image into a ripple as large as the deviation itself
 * (verified against a synthetic FM tone), while a win stride kills it
 * and a rotor-rate modulation is still far slower than the stride. */
static double dm_i[48000], dm_q[48000], dm_ph[48000];

static int rot_demod(const float *x, int n, double f_hz, double fs, int win,
                     double *env, double *freq) {
    for (int i = 0; i < n; i++) {
        double w = TAU_D * f_hz * (double)i / fs;
        dm_i[i] = (double)x[i] * cos(w);
        dm_q[i] = -(double)x[i] * sin(w);
    }
    double si = 0.0, sq = 0.0;
    for (int i = 0; i < win; i++) {
        si += dm_i[i];
        sq += dm_q[i];
    }
    int out = 0;
    for (int j = 0; j + win + 1 < n; j++) {
        env[out] = 2.0 * sqrt(si * si + sq * sq) / win;
        dm_ph[out] = atan2(sq, si);
        out++;
        si += dm_i[j + win] - dm_i[j];
        sq += dm_q[j + win] - dm_q[j];
    }
    for (int j = 0; j < out; j++) {
        if (j < win) {
            freq[j] = f_hz;
            continue;
        }
        double d = dm_ph[j] - dm_ph[j - win];
        d -= (d > TAU_D / 2.0) ? TAU_D : 0.0;
        d += (d < -TAU_D / 2.0) ? TAU_D : 0.0;
        freq[j] = f_hz + d * fs / (TAU_D * (double)win);
    }
    return out;
}

/* Engage processing with both rotors locked at poked rates — targets
 * and deviations overridden after set_mode, so tick holds them. */
static void rotary_lock(tw_rotary *r, float horn_hz, float drum_hz) {
    tw_rotary_init(r, 48000.0f);
    tw_rotary_set_mode(r, TW_ROT_TREMOLO);
    r->horn_target = horn_hz;
    r->drum_target = drum_hz;
    r->horn_dev = 0.0f;
    r->drum_dev = 0.0f;
}

static float rot_lp_h[8192], rot_hp_h[8192], rot_sum_h[8192];

static void test_rotary_crossover(void) {
    /* impulse through the engaged rotary; the split taps carry the
     * crossover alone (sec 15 [HX]: 800 Hz, 12 dB/oct, Butterworth) */
    tw_rotary r;
    tw_rotary_init(&r, 48000.0f);
    tw_rotary_set_mode(&r, TW_ROT_TREMOLO);
    for (int i = 0; i < 8192; i++) {
        (void)tw_rotary_tick(&r, i == 0 ? 1.0f : 0.0f);
        rot_lp_h[i] = r.xo_lp;
        rot_hp_h[i] = r.xo_hp;
        rot_sum_h[i] = r.xo_lp - r.xo_hp; /* the recombination polarity */
    }
    double lp8 = scan_mag(rot_lp_h, 8192, 800.0, 48000.0);
    double hp8 = scan_mag(rot_hp_h, 8192, 800.0, 48000.0);
    CHECK(lp8 > 0.68 && lp8 < 0.73, "|LP(800)| = %.4f, expected ~0.707", lp8);
    CHECK(hp8 > 0.68 && hp8 < 0.73, "|HP(800)| = %.4f, expected ~0.707", hp8);

    /* 12 dB/oct: one octave off the split is ~-12.3 dB (0.2425) */
    double lp16 = scan_mag(rot_lp_h, 8192, 1600.0, 48000.0);
    double hp04 = scan_mag(rot_hp_h, 8192, 400.0, 48000.0);
    CHECK(lp16 > 0.22 && lp16 < 0.27, "|LP(1600)| = %.4f, expected ~0.243", lp16);
    CHECK(hp04 > 0.22 && hp04 < 0.27, "|HP(400)| = %.4f, expected ~0.243", hp04);

    /* passbands and stopbands */
    CHECK(scan_mag(rot_lp_h, 8192, 100.0, 48000.0) > 0.98, "LP passband at 100");
    CHECK(scan_mag(rot_hp_h, 8192, 100.0, 48000.0) < 0.03, "HP stopband at 100");
    CHECK(scan_mag(rot_lp_h, 8192, 3200.0, 48000.0) < 0.08, "LP stopband at 3200");
    CHECK(scan_mag(rot_hp_h, 8192, 3200.0, 48000.0) > 0.95, "HP passband at 3200");

    /* the inverted-horn sum is ~flat: 1 at the edges, +3 dB at the
     * split (a 2nd-order crossover's bump; the same-polarity sum would
     * null there instead) */
    static const double sweep[7] = { 100, 200, 400, 800, 1600, 3200, 6400 };
    for (int i = 0; i < 7; i++) {
        double s = scan_mag(rot_sum_h, 8192, sweep[i], 48000.0);
        CHECK(s > 0.95 && s < 1.45,
              "|LP - HP| at %.0f Hz = %.4f, expected in [1, 1.414]", sweep[i], s);
    }
}

static void test_rotary_inertia(void) {
    tw_rotary r;
    tw_rotary_init(&r, 48000.0f);
    CHECK(r.mode == TW_ROT_BYPASS, "fresh rotary must start bypassed");
    tw_rotary_set_mode(&r, 99);
    CHECK(r.mode == TW_ROT_BRAKE, "hostile high mode must clamp to BRAKE");
    tw_rotary_set_mode(&r, -5);
    CHECK(r.mode == TW_ROT_BYPASS, "hostile low mode must clamp to BYPASS");

    /* horn rise from rest: 1 - 1/e of tremolo in ~tau (sec 15.1) */
    tw_rotary_init(&r, 48000.0f);
    tw_rotary_set_mode(&r, TW_ROT_TREMOLO);
    double knee = ROT_HORN_TREM * (1.0 - exp(-1.0));
    long n = 0;
    while ((double)(r.horn_target + r.horn_dev) < knee && n < 480000) {
        (void)tw_rotary_tick(&r, 0.0f);
        n++;
    }
    double want = ROT_HORN_RISE_TAU * 48000.0;
    CHECK(fabs((double)n - want) / want < 0.03,
          "horn rise tau %ld samples, expected ~%.0f", n, want);

    /* drum rise likewise */
    tw_rotary_init(&r, 48000.0f);
    tw_rotary_set_mode(&r, TW_ROT_TREMOLO);
    knee = ROT_DRUM_TREM * (1.0 - exp(-1.0));
    n = 0;
    while ((double)(r.drum_target + r.drum_dev) < knee && n < 480000) {
        (void)tw_rotary_tick(&r, 0.0f);
        n++;
    }
    want = ROT_DRUM_RISE_TAU * 48000.0;
    CHECK(fabs((double)n - want) / want < 0.03,
          "drum rise tau %ld samples, expected ~%.0f", n, want);

    /* drum fall, tremolo -> chorale: the [RS] service figure, as tau */
    rotary_lock(&r, (float)(400.0 / 60.0), (float)(340.0 / 60.0));
    tw_rotary_set_mode(&r, TW_ROT_CHORALE);
    double target = 40.0 / 60.0;
    knee = target + (ROT_DRUM_TREM - target) * exp(-1.0);
    n = 0;
    while ((double)(r.drum_target + r.drum_dev) > knee && n < 480000) {
        (void)tw_rotary_tick(&r, 0.0f);
        n++;
    }
    want = ROT_DRUM_FALL_TAU * 48000.0;
    CHECK(fabs((double)n - want) / want < 0.03,
          "drum fall tau %ld samples, expected ~%.0f", n, want);

    /* rates land on target exactly (deviation decay has no f32 stall;
     * the drum's absorption point sits ~17 tau = 14 s out, so run 16 s) */
    tw_rotary_init(&r, 48000.0f);
    tw_rotary_set_mode(&r, TW_ROT_TREMOLO);
    for (int i = 0; i < 16 * 48000; i++) (void)tw_rotary_tick(&r, 0.0f);
    CHECK((float)(r.horn_target + r.horn_dev) == 400.0f / 60.0f,
          "horn rate must land exactly on the tremolo target");
    CHECK((float)(r.drum_target + r.drum_dev) == 340.0f / 60.0f,
          "drum rate must land exactly on the tremolo target");

    /* brake from settled tremolo: the horn parks facing forward, both
     * phase and rate snapped exact (sec 15 [LB]) */
    rotary_lock(&r, (float)(400.0 / 60.0), (float)(340.0 / 60.0));
    for (int i = 0; i < 4800; i++) (void)tw_rotary_tick(&r, 0.0f);
    tw_rotary_set_mode(&r, TW_ROT_BRAKE);
    for (int i = 0; i < 8 * 48000; i++) (void)tw_rotary_tick(&r, 0.0f);
    CHECK(r.horn_phase == 0.0f && r.horn_dev == 0.0f,
          "horn must park at the front stop, got phase %f rate %f",
          (double)r.horn_phase, (double)(r.horn_target + r.horn_dev));

    /* drum park from the rear half wraps forward to the same stop */
    tw_rotary_init(&r, 48000.0f);
    tw_rotary_set_mode(&r, TW_ROT_BRAKE);
    r.drum_dev = 0.002f;
    r.drum_phase = 0.7f;
    for (int i = 0; i < 3 * 48000; i++) (void)tw_rotary_tick(&r, 0.0f);
    CHECK(r.drum_phase == 0.0f && r.drum_dev == 0.0f,
          "drum must park forward from the rear half, got phase %f",
          (double)r.drum_phase);
}

static float rot_y[43200], rot_x[43200];
static double rot_env[48000], rot_frq[48000];

/* Group delay of the horn path at f (two-tone phase slope over df),
 * horn parked at a poked angle. Includes the crossover's own ~2.4
 * samples at 2 kHz, which cancels in differences. */
static double horn_group_delay(float phase, double f, double df) {
    double d_est[2];
    for (int t = 0; t < 2; t++) {
        double ft = f + (t ? df : -df);
        tw_rotary r;
        rotary_lock(&r, 0.0f, 0.0f);
        tw_rotary_set_balance(&r, 1.0f);
        r.horn_phase = phase;
        for (int i = 0; i < 4800; i++) {
            float x = (float)sin(TAU_D * ft * (double)i / 48000.0);
            tw_stereo y = tw_rotary_tick(&r, x);
            r.horn_phase = phase; /* hold the poked angle */
            if (i >= 2400) {
                rot_x[i - 2400] = x;
                rot_y[i - 2400] = y.l;
            }
        }
        double xr, xi, yr, yi;
        dft_at(rot_x, 2400, ft, 48000.0, &xr, &xi);
        dft_at(rot_y, 2400, ft, 48000.0, &yr, &yi);
        /* phase of Y/X */
        d_est[t] = atan2(yi * xr - yr * xi, yr * xr + yi * xi);
    }
    double dphi = d_est[1] - d_est[0];
    dphi -= (dphi > TAU_D / 2.0) ? TAU_D : 0.0;
    dphi += (dphi < -TAU_D / 2.0) ? TAU_D : 0.0;
    return -dphi * 48000.0 / (TAU_D * 2.0 * df);
}

static void test_rotary_doppler(void) {
    /* static delay trajectory (sec 15.1): per-mic delay = base
     * - amp * cos(horn - mic), verified point by point through the
     * left mic at -1/8 turn; differences cancel the crossover's own
     * group delay, the absolute check carries it as +2.4 samples */
    static const double phases[4] = { 0.0, 0.15, 0.35, 0.6 };
    double d_est[4], d_want[4];
    for (int i = 0; i < 4; i++) {
        d_est[i] = horn_group_delay((float)phases[i], 2000.0, 100.0);
        d_want[i] = 48000.0 * (ROT_DOP_BASE_S
                    - ROT_DOP_AMP_S * cos(TAU_D * (phases[i] + ROT_MIC)));
    }
    for (int i = 1; i < 4; i++) {
        double got = d_est[i] - d_est[0], want = d_want[i] - d_want[0];
        CHECK(fabs(got - want) < 0.4,
              "delay swing at phase %.2f: %.2f samples, oracle %.2f",
              phases[i], got, want);
    }
    CHECK(fabs(d_est[0] - (d_want[0] + 2.44)) < 1.0,
          "absolute horn delay %.2f, oracle %.2f + crossover 2.44",
          d_est[0], d_want[0]);

    /* dynamic pitch swing at tremolo (M6-3 accept): peak-to-peak
     * instantaneous frequency 2 f 2pi f_rot amp = ~50.3 Hz at 2 kHz */
    tw_rotary r;
    rotary_lock(&r, (float)ROT_HORN_TREM, 0.0f);
    tw_rotary_set_balance(&r, 1.0f);
    for (int i = 0; i < 43200; i++) {
        float x = (float)sin(TAU_D * 2000.0 * (double)i / 48000.0);
        tw_stereo y = tw_rotary_tick(&r, x);
        rot_y[i] = y.l;
    }
    int n = rot_demod(rot_y + 4800, 38400, 2000.0, 48000.0, 48,
                      rot_env, rot_frq);
    double fmin = 1e9, fmax = -1e9;
    for (int j = 500; j < n - 500; j++) {
        if (rot_frq[j] < fmin) fmin = rot_frq[j];
        if (rot_frq[j] > fmax) fmax = rot_frq[j];
    }
    double want_pp = 2.0 * 2000.0 * TAU_D * ROT_HORN_TREM * ROT_DOP_AMP_S;
    CHECK(fmax - fmin > want_pp - 8.0 && fmax - fmin < want_pp + 8.0,
          "horn pitch swing %.1f Hz p-p, expected ~%.1f", fmax - fmin, want_pp);

    /* horn AM depth (M6-4 accept): envelope floor = 1 - depth */
    double emin = 1e9, emax = -1e9;
    for (int j = 500; j < n - 500; j++) {
        if (rot_env[j] < emin) emin = rot_env[j];
        if (rot_env[j] > emax) emax = rot_env[j];
    }
    double ratio = emin / emax;
    CHECK(ratio > 1.0 - ROT_HORN_AM - 0.03 && ratio < 1.0 - ROT_HORN_AM + 0.04,
          "horn AM env floor %.3f, expected ~%.2f", ratio, 1.0 - ROT_HORN_AM);
}

static void test_rotary_drum(void) {
    /* the drum is AM-only (sec 15 [HX]): a mid-band tone through the
     * spinning drum keeps its pitch — the residual is the crossover-
     * region wobble [HX]'s own hedge allows, orders under the horn's */
    tw_rotary r;
    rotary_lock(&r, 0.0f, (float)ROT_DRUM_TREM);
    tw_rotary_set_balance(&r, 0.0f);
    for (int i = 0; i < 43200; i++) {
        float x = (float)sin(TAU_D * 500.0 * (double)i / 48000.0);
        tw_stereo y = tw_rotary_tick(&r, x);
        rot_y[i] = y.l;
    }
    int n = rot_demod(rot_y + 4800, 38400, 500.0, 48000.0, 96,
                      rot_env, rot_frq);
    double fmin = 1e9, fmax = -1e9, emin = 1e9, emax = -1e9;
    for (int j = 500; j < n - 500; j++) {
        if (rot_frq[j] < fmin) fmin = rot_frq[j];
        if (rot_frq[j] > fmax) fmax = rot_frq[j];
        if (rot_env[j] < emin) emin = rot_env[j];
        if (rot_env[j] > emax) emax = rot_env[j];
    }
    CHECK(fmax - fmin < 2.0,
          "drum pitch must not shift: %.2f Hz p-p residual", fmax - fmin);
    CHECK(emin / emax > 0.85 && emin / emax < 0.94,
          "drum AM at 500 Hz: env floor %.3f, expected ~0.91 (depth %.2f"
          " over the mid band)", emin / emax, ROT_DRUM_AM);

    /* the AM floor (sec 15 [HX]): below ~200 Hz the drum barely
     * modulates — same spin, 100 Hz tone, nearly flat envelope */
    rotary_lock(&r, 0.0f, (float)ROT_DRUM_TREM);
    tw_rotary_set_balance(&r, 0.0f);
    for (int i = 0; i < 43200; i++) {
        float x = (float)sin(TAU_D * 100.0 * (double)i / 48000.0);
        tw_stereo y = tw_rotary_tick(&r, x);
        rot_y[i] = y.l;
    }
    n = rot_demod(rot_y + 4800, 38400, 100.0, 48000.0, 480,
                  rot_env, rot_frq);
    emin = 1e9;
    emax = -1e9;
    for (int j = 500; j < n - 500; j++) {
        if (rot_env[j] < emin) emin = rot_env[j];
        if (rot_env[j] > emax) emax = rot_env[j];
    }
    CHECK(emin / emax > 0.965,
          "drum AM must fade below the 200 Hz floor: env floor %.3f",
          emin / emax);
}

static void test_rotary_stereo(void) {
    /* bypass is exact identity and leaves state untouched */
    tw_rotary r;
    tw_rotary_init(&r, 48000.0f);
    int id_bad = 0;
    for (int i = 0; i < 200; i++) {
        float x = (float)(i - 100) * 0.03f;
        tw_stereo y = tw_rotary_tick(&r, x);
        if (y.l != x || y.r != x) id_bad++;
    }
    CHECK(id_bad == 0, "bypass broke identity at %d samples", id_bad);
    CHECK(r.widx == 0 && r.xo_ic1 == 0.0f && r.xo_ic2 == 0.0f
              && r.dm_lp == 0.0f && r.amp.env == 0.0f,
          "bypass must leave state untouched");

    /* parked forward the mic pair is symmetric: L == R bit-exactly */
    rotary_lock(&r, 0.0f, 0.0f);
    int sym_bad = 0;
    for (int i = 0; i < 2400; i++) {
        float x = tw_sin_turns((float)(i % 240) / 240.0f)
                + 0.5f * tw_sin_turns((float)(i % 37) / 37.0f);
        tw_stereo y = tw_rotary_tick(&r, x);
        if (y.l != y.r) sym_bad++;
    }
    CHECK(sym_bad == 0, "parked-front field asymmetric at %d samples", sym_bad);

    /* width 0 collapses the spinning field to exact mono */
    rotary_lock(&r, (float)ROT_HORN_TREM, (float)ROT_DRUM_TREM);
    tw_rotary_set_width(&r, 0.0f);
    int mono_bad = 0;
    for (int i = 0; i < 4800; i++) {
        float x = tw_sin_turns((float)(i % 24) / 24.0f);
        tw_stereo y = tw_rotary_tick(&r, x);
        if (y.l != y.r) mono_bad++;
    }
    CHECK(mono_bad == 0, "width 0 left %d decorrelated samples", mono_bad);

    /* spinning at full width, the mics decorrelate (M6-6 accept) */
    rotary_lock(&r, (float)ROT_HORN_TREM, (float)ROT_DRUM_TREM);
    double sll = 0.0, srr = 0.0, slr = 0.0;
    for (int i = 0; i < 4800 + 14400; i++) {
        float x = tw_sin_turns((float)(i % 120) / 120.0f)
                + tw_sin_turns((float)(i % 20) / 20.0f);
        tw_stereo y = tw_rotary_tick(&r, x);
        if (i >= 4800) {
            sll += (double)y.l * y.l;
            srr += (double)y.r * y.r;
            slr += (double)y.l * y.r;
        }
    }
    CHECK(sll > 0.0 && srr > 0.0, "spinning field must carry energy");
    double corr = slr / sqrt(sll * srr);
    CHECK(corr < 0.95, "L/R correlation %.3f, expected decorrelation", corr);

    /* balance ends kill the other rotor's band */
    rotary_lock(&r, (float)ROT_HORN_TREM, (float)ROT_DRUM_TREM);
    tw_rotary_set_balance(&r, 1.0f); /* horn only: 100 Hz nearly gone */
    double acc = 0.0;
    for (int i = 0; i < 4800 + 9600; i++) {
        float x = (float)sin(TAU_D * 100.0 * (double)i / 48000.0);
        tw_stereo y = tw_rotary_tick(&r, x);
        if (i >= 4800) acc += (double)y.l * y.l;
    }
    CHECK(sqrt(acc / 9600.0) < 0.035,
          "balance 1 must mute the drum band, rms %.4f", sqrt(acc / 9600.0));
    rotary_lock(&r, (float)ROT_HORN_TREM, (float)ROT_DRUM_TREM);
    tw_rotary_set_balance(&r, 0.0f); /* drum only: 6 kHz nearly gone */
    acc = 0.0;
    for (int i = 0; i < 4800 + 9600; i++) {
        float x = (float)sin(TAU_D * 6000.0 * (double)i / 48000.0);
        tw_stereo y = tw_rotary_tick(&r, x);
        if (i >= 4800) acc += (double)y.l * y.l;
    }
    CHECK(sqrt(acc / 9600.0) < 0.06,
          "balance 0 must mute the horn band, rms %.4f", sqrt(acc / 9600.0));

    /* hostile knob values sanitize */
    tw_rotary_set_balance(&r, 0.0f / 0.0f);
    CHECK(r.balance == 0.0f, "NaN balance must clamp");
    tw_rotary_set_width(&r, 7.0f);
    CHECK(r.width == 1.0f, "huge width must clamp to 1");
}

static void test_rotary_amp(void) {
    /* rotary drive 0 (default): the amp stage passes exactly and its
     * state stays untouched even under a loud input */
    tw_rotary r;
    rotary_lock(&r, (float)ROT_HORN_TREM, (float)ROT_DRUM_TREM);
    for (int i = 0; i < 9600; i++) {
        float x = 4.0f * tw_sin_turns((float)(i % 218) / 218.0f);
        (void)tw_rotary_tick(&r, x);
    }
    CHECK(r.amp.env == 0.0f && r.amp.hp_lp == 0.0f,
          "rotary drive 0 must leave the amp stage untouched");

    /* the 40 W ceiling (M6-7 accept): driven hard, the output stays
     * bounded and compresses instead of scaling linearly */
    double peak = 0.0, rms_hi = 0.0, rms_lo = 0.0;
    rotary_lock(&r, (float)ROT_HORN_TREM, (float)ROT_DRUM_TREM);
    tw_rotary_set_drive(&r, 0.9f);
    for (int i = 0; i < 4800 + 48000; i++) {
        float x = 4.0f * (float)sin(TAU_D * 220.0 * (double)i / 48000.0);
        tw_stereo y = tw_rotary_tick(&r, x);
        if (i >= 4800) {
            double a = fabs((double)y.l), b = fabs((double)y.r);
            if (a > peak) peak = a;
            if (b > peak) peak = b;
            rms_hi += (double)y.l * y.l;
        }
    }
    rotary_lock(&r, (float)ROT_HORN_TREM, (float)ROT_DRUM_TREM);
    tw_rotary_set_drive(&r, 0.9f);
    for (int i = 0; i < 4800 + 48000; i++) {
        float x = 0.4f * (float)sin(TAU_D * 220.0 * (double)i / 48000.0);
        tw_stereo y = tw_rotary_tick(&r, x);
        if (i >= 4800) rms_lo += (double)y.l * y.l;
    }
    CHECK(peak < 2.0, "driven output must stay bounded, peak %.3f", peak);
    double ratio = sqrt(rms_hi / rms_lo);
    CHECK(ratio > 2.0 && ratio < 7.0,
          "10x input grew output %.2fx: expected compression, not linear",
          ratio);

    /* hostile knob sanitizes through the M5 setter */
    tw_rotary_set_drive(&r, 0.0f / 0.0f);
    CHECK(r.amp.drive == 0.0f, "NaN rotary drive must clamp to 0");
}

static void run_rotary_script(float *dst, int frames, float wear) {
    tw_instrument ins;
    tw_instrument_init(&ins, 48000.0f);
    if (wear >= 0.0f) tw_organ_set_wear(&ins.organ, wear);
    for (int i = 0; i < frames; i++) {
        if (i == 2400) {
            tw_organ_note(&ins.organ, 60, true, 100);
            tw_organ_note(&ins.organ, 64, true, 100);
        }
        if (i == 4800) tw_rotary_set_mode(&ins.rotary, TW_ROT_CHORALE);
        if (i == 7200) tw_rotary_set_mode(&ins.rotary, TW_ROT_TREMOLO);
        if (i == 9600) tw_rotary_set_balance(&ins.rotary, 0.7f);
        if (i == 12000) tw_rotary_set_mode(&ins.rotary, TW_ROT_BRAKE);
        if (i == 14400) {
            tw_organ_note(&ins.organ, 60, false, 0);
            tw_organ_note(&ins.organ, 64, false, 0);
        }
        tw_stereo y = tw_instrument_tick_stereo(&ins);
        dst[2 * i] = y.l;
        dst[2 * i + 1] = y.r;
    }
}

static void test_rotary_instrument(void) {
    /* rotary bypass (the default): the stereo tick duplicates the mono
     * chain bit-identically on both channels — the M6 identity contract */
    tw_instrument a, b;
    tw_instrument_init(&a, 48000.0f);
    tw_instrument_init(&b, 48000.0f);
    CHECK(a.rotary.mode == TW_ROT_BYPASS, "fresh instrument must bypass the rotary");
    int byp_bad = 0;
    for (int i = 0; i < 19200; i++) {
        if (i == 2400) {
            tw_organ_note(&a.organ, 60, true, 90);
            tw_organ_note(&b.organ, 60, true, 90);
        }
        if (i == 14400) {
            tw_organ_note(&a.organ, 60, false, 0);
            tw_organ_note(&b.organ, 60, false, 0);
        }
        tw_stereo ya = tw_instrument_tick_stereo(&a);
        float yb = tw_instrument_tick(&b);
        if (ya.l != yb || ya.r != yb) byp_bad++;
    }
    CHECK(byp_bad == 0,
          "rotary bypass differs from the mono chain at %d samples", byp_bad);

    /* engaging mid-note and re-bypassing rejoins the mono render
     * exactly: the rotary owns no organ, drive, or RNG state */
    tw_instrument_init(&a, 48000.0f);
    tw_instrument_init(&b, 48000.0f);
    tw_organ_note(&a.organ, 60, true, 100);
    tw_organ_note(&b.organ, 60, true, 100);
    for (int i = 0; i < 4800; i++) {
        (void)tw_instrument_tick_stereo(&a);
        (void)tw_instrument_tick_stereo(&b);
    }
    tw_rotary_set_mode(&a.rotary, TW_ROT_TREMOLO);
    int ndiff = 0;
    for (int i = 0; i < 4800; i++) {
        tw_stereo ya = tw_instrument_tick_stereo(&a);
        tw_stereo yb = tw_instrument_tick_stereo(&b);
        if (ya.l != yb.l || ya.r != yb.r) ndiff++;
    }
    CHECK(ndiff > 1000, "tremolo changed only %d of 4800 samples", ndiff);
    tw_rotary_set_mode(&a.rotary, TW_ROT_BYPASS);
    int rejoin_bad = 0;
    for (int i = 0; i < 4800; i++) {
        tw_stereo ya = tw_instrument_tick_stereo(&a);
        tw_stereo yb = tw_instrument_tick_stereo(&b);
        if (ya.l != yb.l || ya.r != yb.r) rejoin_bad++;
    }
    CHECK(rejoin_bad == 0,
          "re-bypass must rejoin the mono render, %d samples differ", rejoin_bad);

    /* scripted two-run determinism with modes and knobs moving */
    static float r1[2 * 19200], r2[2 * 19200];
    run_rotary_script(r1, 19200, -1.0f);
    run_rotary_script(r2, 19200, -1.0f);
    CHECK(memcmp(r1, r2, sizeof r1) == 0, "rotary script runs differ");
    CHECK(tw_fnv1a64(r1, sizeof r1, 0) == tw_fnv1a64(r2, sizeof r2, 0),
          "rotary FNV differs");
}

/* --- M7: wear — the structured-deviation banks (constants.md 11-13) --- */

/* Wear oracle constants — the test's own copies of sections 11.1/12/13. */
static const uint64_t WEAR_SEED = 0x7765617274773931u; /* [decision]     */
static const double WEAR_LEVEL_SPREAD = 0.12;          /* [FOLK] sec 11.1 */
static const double WEAR_ZONE_TRIM[3] = { 0.0, -0.02, 0.02 }; /* [FOLK] */

static uint64_t wear_mix64(uint64_t *s) { /* the sec 12 splitmix64 */
    uint64_t z = (*s += 0x9e3779b97f4a7c15u);
    z = (z ^ z >> 30) * 0xbf58476d1ce4e5b9u;
    z = (z ^ z >> 27) * 0x94d049bb133111ebu;
    return z ^ z >> 31;
}

static void test_wear_level(void) {
    /* M7-1: wear = 0 is the idealized flat profile, exactly */
    tw_generator g;
    tw_generator_init(&g, 48000.0f, 0.001f);
    CHECK(g.wear == 0.0f, "fresh generator must start at wear 0");
    for (int i = 0; i < TW_WHEELS; i++)
        CHECK(g.level[i] == 1.0f, "wear 0 level[%d] must be exactly 1", i);

    /* wear up and back to 0 restores the exact flat bank, and a render
     * after the round trip is bit-identical to a never-worn one */
    tw_generator_set_wear(&g, 0.7f);
    tw_generator_set_wear(&g, 0.0f);
    for (int i = 0; i < TW_WHEELS; i++)
        CHECK(g.level[i] == 1.0f, "wear 0.7 -> 0 level[%d] must restore 1", i);
    tw_generator h;
    tw_generator_init(&h, 48000.0f, 0.001f);
    float t[TW_WHEELS] = { 0 };
    t[36] = 1.0f;
    t[60] = 0.5f;
    tw_generator_set_keyed_targets(&g, t);
    tw_generator_set_keyed_targets(&h, t);
    int id_bad = 0;
    for (int i = 0; i < 4800; i++) {
        tw_frame fg = tw_generator_tick(&g), fh = tw_generator_tick(&h);
        if (fg.keyed != fh.keyed || fg.percussion != fh.percussion
            || fg.leak != fh.leak || fg.keyed_low != fh.keyed_low)
            id_bad++;
    }
    CHECK(id_bad == 0, "wear round trip broke identity at %d samples", id_bad);

    /* wear = 1: the level law itself, against the oracle (sec 11.1) —
     * spread field is bits 0..20 of the per-wheel draw */
    tw_generator_set_wear(&g, 1.0f);
    int nonflat = 0;
    uint64_t s = WEAR_SEED;
    for (int i = 0; i < TW_WHEELS; i++) {
        uint64_t d = wear_mix64(&s);
        double ul = (double)(d & 0x1fffffu) / 2097152.0;
        int zone = (i + 1 <= 43) ? 0 : (i + 1 <= 48) ? 1 : 2;
        double want = 1.0 + WEAR_LEVEL_SPREAD * (2.0 * ul - 1.0)
                    + WEAR_ZONE_TRIM[zone];
        CHECK(fabs((double)g.level[i] - want) < 1e-6,
              "wear 1 level[%d] = %f, oracle %f", i, (double)g.level[i], want);
        CHECK(g.level[i] > 0.8f && g.level[i] < 1.2f,
              "wear 1 level[%d] = %f out of the +-spread+trim bound",
              i, (double)g.level[i]);
        if (g.level[i] != 1.0f) nonflat++;
    }
    CHECK(nonflat > 80, "wear 1 left %d/91 wheels off flat, expected ~all",
          nonflat);

    /* deviation scales with the knob (half wear ~ half deviation) */
    tw_generator_set_wear(&g, 0.5f);
    s = WEAR_SEED;
    for (int i = 0; i < TW_WHEELS; i++) {
        uint64_t d = wear_mix64(&s);
        double ul = (double)(d & 0x1fffffu) / 2097152.0;
        int zone = (i + 1 <= 43) ? 0 : (i + 1 <= 48) ? 1 : 2;
        double dev1 = WEAR_LEVEL_SPREAD * (2.0 * ul - 1.0) + WEAR_ZONE_TRIM[zone];
        CHECK(fabs((double)g.level[i] - (1.0 + 0.5 * dev1)) < 1e-6,
              "wear must scale the deviation linearly at wheel %d", i + 1);
    }

    /* hostile knob values sanitize */
    tw_generator_set_wear(&g, 0.0f / 0.0f);
    CHECK(g.wear == 0.0f, "NaN wear must clamp to 0");
    tw_generator_set_wear(&g, 7.0f);
    CHECK(g.wear == 1.0f, "huge wear must clamp to 1");
    tw_generator_set_wear(&g, -3.0f);
    CHECK(g.wear == 0.0f, "negative wear must clamp to 0");
}

/* Render one wheel alone at a given wear and DFT its harmonics. */
static float wear_buf[48000];

static void wear_render_wheel(int wheel, float wear, int frames) {
    tw_generator g;
    tw_generator_init(&g, 48000.0f, 0.001f);
    tw_generator_set_wear(&g, wear);
    float t[TW_WHEELS] = { 0 };
    t[wheel - 1] = 1.0f;
    tw_generator_set_keyed_targets(&g, t);
    for (int i = 0; i < 4800; i++) (void)tw_generator_tick(&g); /* settle */
    for (int i = 0; i < frames; i++) wear_buf[i] = tw_generator_tick(&g).keyed;
}

static void test_wear_tooth(void) {
    /* M7-2: the 4/teeth depth law against the oracle (sec 12.1) */
    tw_generator g;
    tw_generator_init(&g, 48000.0f, 0.001f);
    for (int i = 0; i < TW_WHEELS; i++)
        CHECK(g.t2[i] == 0.0f && g.t3[i] == 0.0f,
              "wear 0 toothing[%d] must be exactly 0", i);
    tw_generator_set_wear(&g, 1.0f);
    for (int i = 0; i < TW_WHEELS; i++) {
        int wheel = i + 1;
        int teeth = (wheel <= 84) ? 2 << ((wheel - 1) / 12) : 192;
        double f4 = 4.0 / teeth;
        CHECK(fabs((double)g.t2[i] - 0.015 * f4) < 1e-7
                  && fabs((double)g.t3[i] - 0.03 * f4) < 1e-7,
              "toothing law at wheel %d: t2 %f t3 %f, teeth %d",
              wheel, (double)g.t2[i], (double)g.t3[i], teeth);
    }

    /* spectral: wheel 13 (65.38 Hz, 4 teeth) carries its 2nd and 3rd
     * partials at the pinned depths; the idealized wheel does not */
    double f1 = tw_wheel_freq_hz(13);
    wear_render_wheel(13, 1.0f, 48000);
    double h1 = scan_mag(wear_buf, 48000, f1, 48000.0);
    double h2 = scan_mag(wear_buf, 48000, 2.0 * f1, 48000.0);
    double h3 = scan_mag(wear_buf, 48000, 3.0 * f1, 48000.0);
    CHECK(h3 / h1 > 0.6 * 0.03 && h3 / h1 < 1.4 * 0.03,
          "wheel 13 3rd partial %.4f of H1, pinned 0.03", h3 / h1);
    CHECK(h2 / h1 > 0.5 * 0.015,
          "wheel 13 2nd partial %.4f of H1, pinned 0.015", h2 / h1);
    wear_render_wheel(13, 0.0f, 48000);
    double h3_id = scan_mag(wear_buf, 48000, 3.0 * f1, 48000.0)
                 / scan_mag(wear_buf, 48000, f1, 48000.0);
    CHECK(h3_id < 0.002, "idealized wheel 13 must stay a near-sine, H3 %.5f",
          h3_id);

    /* the top wheels stay near-sine even at full wear (4/192 scaling) */
    CHECK(g.t3[90] < 7e-4, "wheel 91 toothing must be tiny, got %f",
          (double)g.t3[90]);
}

/* Magnitude of the demodulated-envelope ripple at f_hz, on env[j0..j1)
 * with its mean removed. */
static double env_ripple(const double *env, int j0, int j1, double f_hz,
                         double fs) {
    double mean = 0.0;
    for (int j = j0; j < j1; j++) mean += env[j];
    mean /= (double)(j1 - j0);
    double re = 0.0, im = 0.0;
    for (int j = j0; j < j1; j++) {
        double w = TAU_D * f_hz * (double)(j - j0) / fs;
        re += (env[j] - mean) * cos(w);
        im -= (env[j] - mean) * sin(w);
    }
    /* normalized: a pure A(1 + d sin) envelope returns d */
    return 2.0 * sqrt(re * re + im * im) / (mean * (double)(j1 - j0));
}

static void test_wear_motion_am(void) {
    /* M7-3: the rotation-rate law — every wheel's AM accumulator runs
     * at f_wheel/teeth = 20 rev/s x class ratio (sec 12) */
    tw_generator g;
    tw_generator_init(&g, 48000.0f, 0.001f);
    for (int i = 0; i < TW_WHEELS; i++) {
        int wheel = i + 1;
        int teeth = (wheel <= 84) ? 2 << ((wheel - 1) / 12) : 192;
        double want = (double)tw_wheel_freq_hz(wheel) / teeth / 48000.0;
        CHECK(fabs((double)g.rev_step[i] - want) < 1e-9,
              "rev rate at wheel %d: %e, want %e",
              wheel, (double)g.rev_step[i], want);
    }
    CHECK(fabs((double)g.rev_step[45] * 48000.0 - 27.5) < 1e-4,
          "wheel 46 must revolve at 27.5 Hz");
    CHECK(g.am_g[45] == 0.0f, "wear 0 AM depth must be exactly 0");

    /* the depth law against the per-wheel draw (bits 21..41, sec 11.1) */
    tw_generator_set_wear(&g, 1.0f);
    uint64_t s = WEAR_SEED;
    for (int i = 0; i < TW_WHEELS; i++) {
        uint64_t d = wear_mix64(&s);
        double ua = (double)((d >> 21) & 0x1fffffu) / 2097152.0;
        CHECK(fabs((double)g.am_g[i] - 0.05 * ua) < 1e-7,
              "AM depth law at wheel %d: %f, draw says %f",
              i + 1, (double)g.am_g[i], 0.05 * ua);
    }
    double want_d = (double)g.am_g[45]; /* wheel 46 drew ~0.034 */

    /* demodulate a worn wheel 46: the envelope ripples at the wheel's
     * own 27.5 Hz rotation rate, at the drawn depth, and not at
     * neighbouring rates; the idealized wheel holds flat */
    static double env[48000], frq[48000];
    wear_render_wheel(46, 1.0f, 43200);
    int n = rot_demod(wear_buf, 43200, 440.0, 48000.0, 218, env, frq);
    int j0 = 500, j1 = n - 500;
    double d_rev = env_ripple(env, j0, j1, 27.5, 48000.0);
    double d_lo = env_ripple(env, j0, j1, 21.0, 48000.0);
    double d_hi = env_ripple(env, j0, j1, 34.0, 48000.0);
    CHECK(d_rev > 0.75 * want_d && d_rev < 1.25 * want_d,
          "wheel 46 AM depth %.4f at 27.5 Hz, draw says %.4f", d_rev, want_d);
    CHECK(d_rev > 5.0 * d_lo && d_rev > 5.0 * d_hi,
          "AM must sit at the rotation rate: 27.5 Hz %.4f vs %.4f/%.4f",
          d_rev, d_lo, d_hi);
    wear_render_wheel(46, 0.0f, 43200);
    n = rot_demod(wear_buf, 43200, 440.0, 48000.0, 218, env, frq);
    double d_id = env_ripple(env, j0, n - 500, 27.5, 48000.0);
    CHECK(d_id < 1e-4, "idealized wheel 46 must not shimmer, got %.6f", d_id);
}

static void test_wear_pickup(void) {
    /* M7-4: the alpha law (sec 12.1 [AS16]: alpha = wear x 0.3, cubic
     * series coefficients alpha/2 and alpha^2/6) */
    tw_generator g;
    tw_generator_init(&g, 48000.0f, 0.001f);
    CHECK(g.pk2 == 0.0f && g.pk3 == 0.0f,
          "wear 0 pickup must be exactly linear");
    tw_generator_set_wear(&g, 1.0f);
    CHECK(fabs((double)g.pk2 - 0.15) < 1e-7
              && fabs((double)g.pk3 - 0.015) < 1e-7,
          "alpha 0.3 series: pk2 %f pk3 %f", (double)g.pk2, (double)g.pk3);

    /* harmonics: wheel 73 (2092 Hz, toothing ~1e-3 — alpha dominates):
     * the x^2 term puts H2/H1 at ~alpha/4 = 0.074, scaling with wear */
    double f1 = tw_wheel_freq_hz(73);
    wear_render_wheel(73, 1.0f, 48000);
    double h1 = scan_mag(wear_buf, 48000, f1, 48000.0);
    double h2 = scan_mag(wear_buf, 48000, 2.0 * f1, 48000.0);
    double full = h2 / h1;
    CHECK(full > 0.063 && full < 0.086,
          "worn pickup H2/H1 %.4f, alpha/4 says ~0.074", full);
    double dc = 0.0;
    for (int i = 0; i < 48000; i++) dc += (double)wear_buf[i];
    dc /= 48000.0;
    CHECK(fabs(dc) < 1e-3,
          "the static term must stay subtracted, DC %.5f", dc);

    wear_render_wheel(73, 0.5f, 48000);
    double half = scan_mag(wear_buf, 48000, 2.0 * f1, 48000.0)
                / scan_mag(wear_buf, 48000, f1, 48000.0);
    CHECK(half / full > 0.42 && half / full < 0.58,
          "H2 must scale with wear: half/full = %.3f", half / full);

    wear_render_wheel(73, 0.0f, 48000);
    double id = scan_mag(wear_buf, 48000, 2.0 * f1, 48000.0)
              / scan_mag(wear_buf, 48000, f1, 48000.0);
    CHECK(id < 1e-3, "idealized pickup must add no H2, got %.5f", id);

    /* IMD-free (sec 12 [AS16]): each wheel meets only its own pickup,
     * so two worn wheels distort but never intermodulate — H2 of wheel
     * 46 present, f1+f2 sum tone absent */
    tw_generator_init(&g, 48000.0f, 0.001f);
    tw_generator_set_wear(&g, 1.0f);
    float t[TW_WHEELS] = { 0 };
    t[45] = 1.0f; /* 440.00 Hz */
    t[48] = 1.0f; /* 523.08 Hz */
    tw_generator_set_keyed_targets(&g, t);
    for (int i = 0; i < 4800; i++) (void)tw_generator_tick(&g);
    for (int i = 0; i < 48000; i++) wear_buf[i] = tw_generator_tick(&g).keyed;
    double fa = tw_wheel_freq_hz(46), fb = tw_wheel_freq_hz(49);
    double a1 = scan_mag(wear_buf, 48000, fa, 48000.0);
    double a2 = scan_mag(wear_buf, 48000, 2.0 * fa, 48000.0);
    double imd = scan_mag(wear_buf, 48000, fa + fb, 48000.0);
    CHECK(a2 / a1 > 0.05, "worn pair must still distort, H2 %.4f", a2 / a1);
    CHECK(imd / a1 < 2e-3,
          "per-wheel pickups must not intermodulate: %.5f at f1+f2", imd / a1);
}

static void test_wear_leakage(void) {
    /* M7-5: the bleed follows the bin/shaft layout (sec 13.1) — three
     * structural classes from the matrix contraction, not a function of
     * musical adjacency */
    tw_generator g;
    tw_generator_init(&g, 48000.0f, 0.001f);
    for (int i = 0; i < TW_WHEELS; i++)
        CHECK(g.leak_gain[i] == 0.0f, "wear 0 leak[%d] must be exactly 0", i);
    tw_generator_set_wear(&g, 1.0f);
    const double S = 3e-3, B = 8e-4; /* sec 13.1 [FOLK] */
    for (int i = 0; i < TW_WHEELS; i++) {
        int wheel = i + 1;
        int partner = wheel <= 36 ? wheel + 48
                    : wheel <= 41 ? 0
                    : wheel <= 48 ? wheel + 43
                    : wheel <= 84 ? wheel - 48 : wheel - 43;
        int mates = ((wheel >= 13 && wheel <= 17)
                     || (wheel >= 61 && wheel <= 65)) ? 1 : 2;
        double want = (partner ? S : 0.0) + B * mates;
        CHECK(fabs((double)g.leak_gain[i] - want) < 1e-9,
              "leak class at wheel %d: %e, matrix says %e",
              wheel, (double)g.leak_gain[i], want);
    }
    /* the structure in one line each: blank-partner wheels are the
     * quietest; adjacent wheel numbers 36/37 split across classes;
     * shaft pairs (20/68, 42/85) share theirs */
    CHECK(g.leak_gain[36] < g.leak_gain[35] && g.leak_gain[36] < g.leak_gain[37 + 5],
          "wheel 37 (blank partner) must bleed least");
    CHECK(g.leak_gain[35] != g.leak_gain[36],
          "bleed must follow bins, not neighbouring wheel numbers");
    CHECK(g.leak_gain[19] == g.leak_gain[67] && g.leak_gain[41] == g.leak_gain[84],
          "shaft pairs must share a bleed class");

    /* the bus itself: an unkeyed worn generator hums its floor — wheel
     * 46 present at its class weight (x level profile), silence at 0 */
    for (int i = 0; i < 4800; i++) (void)tw_generator_tick(&g);
    static float lk[48000];
    double rms = 0.0;
    for (int i = 0; i < 48000; i++) {
        lk[i] = tw_generator_tick(&g).leak;
        rms += (double)lk[i] * lk[i];
    }
    rms = sqrt(rms / 48000.0);
    CHECK(rms > 0.01 && rms < 0.06,
          "idle floor rms %.4f at wear 1, expected ~0.03 (-30 dB)", rms);
    double a46 = scan_mag(lk, 48000, tw_wheel_freq_hz(46), 48000.0)
               / (48000.0 / 2.0);
    double want46 = (double)g.leak_gain[45] * (double)g.level[45];
    CHECK(fabs(a46 / want46 - 1.0) < 0.3,
          "440 Hz line in the floor at %.2e, class weight %.2e", a46, want46);

    /* organ routing: an idle organ carries the floor once worn, and
     * stays exactly silent idealized (organ default is M7-7's slice) */
    tw_organ o;
    tw_organ_init(&o, 48000.0f);
    tw_generator_set_wear(&o.gen, 1.0f);
    double orms = organ_rms(&o, 4800);
    CHECK(orms > 0.005, "worn idle organ must carry its floor, rms %.4f", orms);
    tw_generator_set_wear(&o.gen, 0.0f);
    for (int i = 0; i < 100; i++)
        CHECK(tw_organ_tick(&o) == 0.0f, "idealized idle organ must be silent");
}

static float hum_buf[192000];

static void test_wear_hum(void) {
    /* M7-6: the 60 Hz line (sec 1) — law first */
    tw_generator g;
    tw_generator_init(&g, 48000.0f, 0.001f);
    CHECK(g.hum_gain == 0.0f, "wear 0 hum must be exactly 0");
    CHECK(fabs((double)g.hum_step * 48000.0 - 60.0) < 1e-4,
          "hum must sit at 60 Hz, got %.2f", (double)g.hum_step * 48000.0);
    tw_generator_set_wear(&g, 1.0f);
    CHECK(fabs((double)g.hum_gain - 1e-3) < 1e-9,
          "hum level at wear 1 must be the pinned 1e-3");

    /* presence: a 4 s idle worn organ carries the 60 Hz line at the
     * pinned level (4 s separates it from wheel 12's 61.7 Hz floor
     * line); the idealized organ carries nothing at all */
    tw_organ o;
    tw_organ_init(&o, 48000.0f);
    tw_generator_set_wear(&o.gen, 1.0f);
    for (int i = 0; i < 4800; i++) (void)tw_organ_tick(&o);
    for (int i = 0; i < 192000; i++) hum_buf[i] = tw_organ_tick(&o);
    double a60 = scan_mag(hum_buf, 192000, 60.0, 48000.0) / (192000.0 / 2.0);
    CHECK(a60 > 0.6e-3 && a60 < 1.4e-3,
          "60 Hz line at %.2e, pinned 1e-3", a60);
    tw_organ_init(&o, 48000.0f);
    tw_organ_set_wear(&o, 0.0f); /* the idealized reference, not the default */
    for (int i = 0; i < 4800; i++) (void)tw_organ_tick(&o);
    for (int i = 0; i < 192000; i++) hum_buf[i] = tw_organ_tick(&o);
    CHECK(scan_mag(hum_buf, 192000, 60.0, 48000.0) == 0.0,
          "idealized organ must carry no hum at all");
}

/* Pre-M7 baseline signatures of the four scripted renders, captured on
 * the M6 tree (7764 checks) the day M7 landed: wear = 0 must reproduce
 * each bit-for-bit — the M7-7 identity contract. */
static const uint64_t PRE_M7_SCRIPT_FNV = 0xf0b4c7c3f7705480u;
static const uint64_t PRE_M7_VIB_FNV = 0xb01485a1702721a3u;
static const uint64_t PRE_M7_DRIVE_FNV = 0x730ac52bff1f129du;
static const uint64_t PRE_M7_ROTARY_FNV = 0xf1d10bfe4b6cab4du;

static void test_wear_knob(void) {
    /* M7-7: one knob; the shipped default is nonzero (design.md — a
     * factory-new unit already deviates) and sanitizes hostile input */
    tw_organ o;
    tw_organ_init(&o, 48000.0f);
    CHECK(TW_WEAR_DEFAULT > 0.0f, "the shipped default must be nonzero");
    CHECK(o.gen.wear == TW_WEAR_DEFAULT,
          "a fresh organ must ship at the default wear");
    tw_organ_set_wear(&o, 0.0f / 0.0f);
    CHECK(o.gen.wear == 0.0f, "NaN wear must clamp to 0");
    tw_organ_set_wear(&o, 9.0f);
    CHECK(o.gen.wear == 1.0f, "huge wear must clamp to 1");

    /* the shipped idle floor sits near the sec 13.1 -44 dB estimate */
    tw_organ_init(&o, 48000.0f);
    for (int i = 0; i < 4800; i++) (void)tw_organ_tick(&o);
    double floor_rms = organ_rms(&o, 48000);
    CHECK(floor_rms > 1e-3 && floor_rms < 2e-2,
          "shipped idle floor rms %.5f, expected ~6e-3 (-44 dB)", floor_rms);

    /* wear = 0 reproduces every pre-M7 scripted render bit-for-bit */
    static float r[2 * 19200];
    run_script(r, 19200, 0.0f);
    CHECK(tw_fnv1a64(r, 19200 * sizeof(float), 0) == PRE_M7_SCRIPT_FNV,
          "wear 0 organ script must equal the pre-M7 signature");
    run_vib_script(r, 19200, 0.0f);
    CHECK(tw_fnv1a64(r, 19200 * sizeof(float), 0) == PRE_M7_VIB_FNV,
          "wear 0 vibrato script must equal the pre-M7 signature");
    run_drive_script(r, 19200, 0.0f);
    CHECK(tw_fnv1a64(r, 19200 * sizeof(float), 0) == PRE_M7_DRIVE_FNV,
          "wear 0 drive script must equal the pre-M7 signature");
    run_rotary_script(r, 19200, 0.0f);
    CHECK(tw_fnv1a64(r, sizeof r, 0) == PRE_M7_ROTARY_FNV,
          "wear 0 rotary script must equal the pre-M7 signature");

    /* ...and the shipped default is audibly its own render */
    run_script(r, 19200, -1.0f);
    CHECK(tw_fnv1a64(r, 19200 * sizeof(float), 0) != PRE_M7_SCRIPT_FNV,
          "the shipped default must not be the idealized render");

    /* mid-note wear -> 0 rejoins the never-worn render bit-exactly:
     * wear scales gain banks only, phase state advances identically */
    tw_organ a, b;
    tw_organ_init(&a, 48000.0f); /* ships worn */
    tw_organ_init(&b, 48000.0f);
    tw_organ_set_wear(&b, 0.0f);
    tw_organ_note(&a, 60, true, 100);
    tw_organ_note(&b, 60, true, 100);
    int ndiff = 0;
    for (int i = 0; i < 4800; i++)
        if (tw_organ_tick(&a) != tw_organ_tick(&b)) ndiff++;
    CHECK(ndiff > 1000, "default wear changed only %d of 4800 samples", ndiff);
    tw_organ_set_wear(&a, 0.0f);
    int rejoin_bad = 0;
    for (int i = 0; i < 4800; i++)
        if (tw_organ_tick(&a) != tw_organ_tick(&b)) rejoin_bad++;
    CHECK(rejoin_bad == 0,
          "wear back to 0 must rejoin the idealized render, %d differ",
          rejoin_bad);
}

int main(void) {
    test_frequency_table();
    test_foldback();
    test_drawbar_gain();
    test_sine_kernel();
    test_determinism();
    test_sanitize_and_smoothing();
    test_output_frequency();
    test_midi_parser();
    test_taper_and_robbing();
    test_organ();
    test_percussion_trigger();
    test_percussion_decay();
    test_percussion_levels();
    test_scanner_line();
    test_scanner_sweep();
    test_scanner_modes();
    test_scanner_bass_split();
    test_scanner_wiring();
    test_saturator();
    test_drive_follower();
    test_drive_harmonics();
    test_drive_highpass();
    test_instrument();
    test_rotary_crossover();
    test_rotary_inertia();
    test_rotary_doppler();
    test_rotary_drum();
    test_rotary_stereo();
    test_rotary_amp();
    test_rotary_instrument();
    test_wear_level();
    test_wear_tooth();
    test_wear_motion_am();
    test_wear_pickup();
    test_wear_leakage();
    test_wear_hum();
    test_wear_knob();
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
