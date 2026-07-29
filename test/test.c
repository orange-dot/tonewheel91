/* tonewheel91 tests — hosted; libm is the oracle. Run: make test */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../src/tonewheel.h"
#include "../src/epiano.h"

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

/* Tick past whatever a step schedules. 4800 frames (100 ms) clears the
 * slowest press stagger, the bounce window, and the 34 ms percussion
 * recovery; smaller counts are used where a test is watching one of
 * those happen. */
static void settle(tw_organ *o, int frames) {
    for (int i = 0; i < frames; i++) (void)tw_organ_tick(o);
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
static const double PERC_REARM_S = 0.034; /* C31 through R55+R56, [derived] */

/* The trigger is the 1' contact, not the key event (sec 8), so it lands
 * somewhere on the contact timeline. Tick until it fires; returns the
 * tapped wheel, or 0 if nothing ever grounded the sensing line. */
static int perc_await_trigger(tw_organ *o, int frames) {
    for (int i = 0; i < frames && o->perc.wheel == 0; i++) (void)tw_organ_tick(o);
    return o->perc.wheel;
}

static void test_percussion_trigger(void) {
    tw_organ o;
    tw_organ_init(&o, 48000.0f);
    CHECK(o.perc.armed, "fresh organ starts armed");
    tw_organ_set_percussion(&o, true, false, false, true); /* on, 2nd, fast, normal */

    int w60 = tw_wheel_index(60 - TW_NOTE_MIN + 1, 3); /* 2nd harmonic = 4' tap */
    int w67 = tw_wheel_index(67 - TW_NOTE_MIN + 1, 3);

    /* Each step settles past the press stagger, the bounce window, and
     * the 34 ms recovery before the state machine is read. */
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
        settle(&o, 4800);
        CHECK(o.perc.armed == seq[i].armed_after,
              "step %d (note %d %s): armed %d, expected %d", i, seq[i].note,
              seq[i].down ? "down" : "up", o.perc.armed, seq[i].armed_after);
        CHECK(o.perc.wheel == expect_wheel[i],
              "step %d: wheel %d, expected %d", i, o.perc.wheel, expect_wheel[i]);
    }

    /* The trigger waits for the ninth contact. At the slowest press the
     * nine buses take ~15 ms, and the envelope fires at the end of that
     * travel, not at the note event. */
    tw_organ_init(&o, 48000.0f);
    tw_organ_set_percussion(&o, true, false, false, true);
    tw_organ_note(&o, 60, true, 1); /* ~15 ms of stagger */
    settle(&o, 240);                /* 5 ms in: the 1' contact is not there yet */
    CHECK(o.perc.armed && o.perc.wheel == 0,
          "the trigger must wait for the 1' contact: armed %d, wheel %d",
          o.perc.armed, o.perc.wheel);
    settle(&o, 1200);               /* past the full travel */
    CHECK(!o.perc.armed && o.perc.wheel == w60,
          "the 1' contact must fire it: armed %d, wheel %d",
          o.perc.armed, o.perc.wheel);

    /* Bounce on the sensing line cannot retrigger: the recovery is an
     * order above the 2 ms bounce window, so one press is one hit. */
    tw_organ_init(&o, 48000.0f);
    tw_organ_set_percussion(&o, true, false, false, true);
    tw_organ_note(&o, 60, true, 1);
    double prev = 0.0;
    int rises = 0;
    for (int i = 0; i < 4800; i++) {
        (void)tw_organ_tick(&o);
        double now = (double)o.gen.perc_target[w60 - 1];
        if (now > prev + 1e-9) rises++;
        prev = now;
    }
    CHECK(rises == 1, "one press through its own bounce fired %d times", rises);

    /* Detachment is what gates the next hit (sec 8): a gap longer than
     * the recovery retriggers, a shorter one does not. */
    static const int gap[2] = { 480, 4800 }; /* 10 ms, 100 ms vs 34 ms */
    for (int g = 0; g < 2; g++) {
        tw_organ_init(&o, 48000.0f);
        tw_organ_set_percussion(&o, true, false, false, true);
        tw_organ_note(&o, 60, true, 100);
        settle(&o, 4800);
        tw_organ_note(&o, 60, false, 0);
        settle(&o, gap[g]);
        tw_organ_note(&o, 64, true, 100);
        settle(&o, 4800);
        int want = g ? tw_wheel_index(64 - TW_NOTE_MIN + 1, 3) : w60;
        CHECK(o.perc.wheel == want,
              "%d ms gap: wheel %d, expected %d (recovery is %.0f ms)",
              gap[g] / 48, o.perc.wheel, want, PERC_REARM_S * 1000.0);
    }

    /* Percussion off: the state machine still tracks the sensing line,
     * but no trigger ever fires (wheel never picked). */
    tw_organ_init(&o, 48000.0f);
    tw_organ_set_percussion(&o, false, false, false, true);
    tw_organ_note(&o, 60, true, 100);
    settle(&o, 4800);
    CHECK(!o.perc.armed && o.perc.wheel == 0,
          "percussion off: the sensing line still disarms, but picks no wheel");

    /* Wheel selection follows the harmonic switch (sec 4/8). */
    tw_organ_init(&o, 48000.0f);
    tw_organ_set_percussion(&o, true, false, false, true); /* 2nd */
    tw_organ_note(&o, 60, true, 100);
    CHECK(perc_await_trigger(&o, 4800) == tw_wheel_index(60 - TW_NOTE_MIN + 1, 3),
          "2nd harmonic must tap the 4' bus, got wheel %d", o.perc.wheel);
    tw_organ_init(&o, 48000.0f);
    tw_organ_set_percussion(&o, true, true, false, true); /* 3rd */
    tw_organ_note(&o, 60, true, 100);
    CHECK(perc_await_trigger(&o, 4800) == tw_wheel_index(60 - TW_NOTE_MIN + 1, 4),
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
    int w = perc_await_trigger(&o, 4800);
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

    /* 100 ms between the release and the next attack: the trigger needs a
     * real detachment now, not just a note-off (sec 8 recovery). */
    tw_organ_note(&o, 60, false, 0);
    settle(&o, 4800);
    tw_organ_set_percussion(&o, true, false, false, true); /* NORMAL */
    tw_organ_note(&o, 60, true, 127);
    for (int i = 0; i < 9600; i++) (void)tw_organ_tick(&o);
    double normal = (double)o.gen.keyed_target[w4 - 1];
    CHECK(fabs(normal / base - PERC_NORMAL_ATTEN) < 1e-4,
          "NORMAL attenuation ratio %f, expected %f", normal / base, PERC_NORMAL_ATTEN);

    tw_organ_note(&o, 60, false, 0);
    settle(&o, 4800);
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
    double peak_normal = (double)o.gen.perc_target[perc_await_trigger(&o, 4800) - 1];
    tw_organ_init(&o, 48000.0f);
    tw_organ_set_percussion(&o, true, false, false, false); /* SOFT */
    tw_organ_note(&o, 60, true, 100);
    double peak_soft = (double)o.gen.perc_target[perc_await_trigger(&o, 4800) - 1];
    CHECK(fabs(peak_soft / peak_normal - PERC_SOFT_PAD) < 1e-6,
          "SOFT peak ratio %f, expected %f", peak_soft / peak_normal, PERC_SOFT_PAD);
}

/* --- key depth: the per-note travel (constants.md section 7.1) --- */

/* Depth oracle — the test's own copies of organ.c's make points and band. */
static const int DEPTH_HYST = 4;
static int make_point(int i) { return (i + 1) * 128 / (TW_DRAWBARS + 1); }

/* A position that forces exactly n made contacts from any prior count:
 * the midpoint between make points n-1 and n sits inside the plateau
 * [make(n-1)+band, make(n)-band), where both walks stop at n. */
static int depth_forcing(int n) {
    if (n <= 0) return 0;
    if (n >= TW_DRAWBARS) return 127;
    return (make_point(n - 1) + make_point(n)) / 2;
}

static int contacts_made(const tw_organ *o, int key) {
    int n = 0;
    for (int b = 0; b < TW_DRAWBARS; b++) n += o->contact[key][b];
    return n;
}

/* Contact machinery, so every organ here is the idealized reference
 * (wear 0) — the shipped default's idle floor would mask silence. */
static void depth_organ(tw_organ *o, const uint8_t *reg) {
    tw_organ_init(o, 48000.0f);
    tw_organ_set_wear(o, 0.0f);
    if (reg) tw_organ_set_registration(o, reg);
}

/* run_script above, with an optional depth stream laid over it: same
 * organ, same notes, same drawbar move, same wear. */
static void run_depth_script(float *dst, int frames, bool send_depth) {
    tw_organ o;
    tw_organ_init(&o, 48000.0f);
    tw_organ_set_wear(&o, 0.0f);
    for (int i = 0; i < frames; i++) {
        if (i == 4800) tw_organ_note(&o, 60, true, 90);
        if (send_depth && i > 4800 && i % 240 == 0)
            tw_organ_note_depth(&o, 60, 64 + (i / 240 % 5) * 12);
        if (i == 9600) tw_organ_set_drawbar(&o, 4, 6);
        if (i == 14400) tw_organ_note(&o, 60, false, 0);
        dst[i] = tw_organ_tick(&o);
    }
}

static void test_key_depth(void) {
    static const uint8_t all8[TW_DRAWBARS] = { 8, 8, 8, 8, 8, 8, 8, 8, 8 };
    tw_organ o;

    /* A press bottoms out: nine contacts, all nine buses, as before. */
    depth_organ(&o, all8);
    tw_organ_note(&o, 60, true, 127);
    settle(&o, 4800);
    CHECK(o.made[24] == TW_DRAWBARS && contacts_made(&o, 24) == TW_DRAWBARS,
          "note-on must bottom out, made %d contacts %d",
          o.made[24], contacts_made(&o, 24));

    /* Every step of the travel makes exactly its contacts, in bus order
     * 0..8 — walked down and back up, so both hysteresis directions run. */
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i <= TW_DRAWBARS; i++) {
            int n = pass ? i : TW_DRAWBARS - i;
            tw_organ_note_depth(&o, 60, depth_forcing(n));
            settle(&o, 480);
            CHECK(o.made[24] == n && contacts_made(&o, 24) == n,
                  "depth %d: made %d contacts %d, expected %d",
                  depth_forcing(n), o.made[24], contacts_made(&o, 24), n);
            for (int b = 0; b < TW_DRAWBARS; b++)
                CHECK(o.contact[24][b] == (b < n),
                      "depth %d bus %d %s, contacts must close in bus order",
                      n, b, o.contact[24][b] ? "closed" : "open");
        }

    /* The band itself: one position, two answers, depending on approach.
     * Make point 4 sits at 64 — the travel's midpoint. */
    depth_organ(&o, all8);
    tw_organ_note(&o, 60, true, 127);
    settle(&o, 4800);
    tw_organ_note_depth(&o, 60, make_point(4));
    CHECK(o.made[24] == 5, "arriving at the make point from above holds 5, got %d",
          o.made[24]);
    tw_organ_note_depth(&o, 60, 0);
    tw_organ_note_depth(&o, 60, make_point(4));
    CHECK(o.made[24] == 4, "arriving at the make point from below holds 4, got %d",
          o.made[24]);

    /* Chatter: a finger dithering inside the band must not toggle a
     * contact, and must not draw from the RNG either. */
    settle(&o, 480);
    uint64_t rng_before = o.rng;
    int before = contacts_made(&o, 24);
    int jitter = DEPTH_HYST - 1;
    for (int i = 0; i < 200; i++)
        tw_organ_note_depth(&o, 60, make_point(4) + (i % 2 ? jitter : -jitter));
    CHECK(o.rng == rng_before && o.made[24] == 4 && contacts_made(&o, 24) == before,
          "dithering on a make point chattered: made %d, rng %s",
          o.made[24], o.rng == rng_before ? "held" : "advanced");

    /* Depth changes which contacts are closed and nothing else: a made
     * bus passes exactly what it passes on a full press — taper, merge
     * law, drawbar gain and all — and an unmade one passes exactly zero.
     * Key 25's nine taps land on nine distinct wheels, so each bus is
     * read off its own wheel. Bit-exact, not approximate: the same
     * closed set through the same law is the same float. */
    const int key60 = 60 - TW_NOTE_MIN + 1;
    double full[TW_DRAWBARS];
    depth_organ(&o, all8);
    tw_organ_note(&o, 60, true, 127);
    settle(&o, 4800);
    for (int b = 0; b < TW_DRAWBARS; b++)
        full[b] = (double)o.gen.keyed_target[tw_wheel_index(key60, b) - 1];
    CHECK(full[0] > 0.0 && full[TW_DRAWBARS - 1] > 0.0,
          "the full-press reference must sound on every bus");
    for (int n = TW_DRAWBARS; n >= 0; n--) {
        tw_organ_note_depth(&o, 60, depth_forcing(n));
        settle(&o, 4800);
        for (int b = 0; b < TW_DRAWBARS; b++) {
            double want = b < n ? full[b] : 0.0;
            double got = (double)o.gen.keyed_target[tw_wheel_index(key60, b) - 1];
            CHECK(got == want, "depth %d bus %d: target %.9f, expected %.9f",
                  n, b, got, want);
        }
    }

    /* Depth 0 is a held key with every contact open — not a note-off.
     * It must fall silent and come back without a new note-on. */
    depth_organ(&o, all8);
    tw_organ_note(&o, 60, true, 127);
    settle(&o, 4800);
    tw_organ_note_depth(&o, 60, 0);
    settle(&o, 4800);
    CHECK(o.held[24] && o.made[24] == 0,
          "depth 0 must leave the key held, held %d made %d",
          o.held[24], o.made[24]);
    CHECK(organ_rms(&o, 4800) < 1e-6, "a fully-open held key must be silent");
    tw_organ_note_depth(&o, 60, 127);
    settle(&o, 4800);
    CHECK(organ_rms(&o, 4800) > 0.1, "the same held key must sound again");

    /* Depth takes over mid-stagger, and note-off after it still empties
     * the key: the pending list is rewritten from the contacts as they
     * stand, never from the target that scheduled them. */
    depth_organ(&o, all8);
    tw_organ_note(&o, 60, true, 1); /* slowest press, ~15 ms of stagger */
    settle(&o, 240);                /* 5 ms in, mid-flight */
    CHECK(contacts_made(&o, 24) > 0 && contacts_made(&o, 24) < TW_DRAWBARS,
          "the stagger should be mid-flight here, %d contacts",
          contacts_made(&o, 24));
    tw_organ_note_depth(&o, 60, depth_forcing(5));
    settle(&o, 4800);
    CHECK(contacts_made(&o, 24) == 5, "depth mid-stagger settled at %d, expected 5",
          contacts_made(&o, 24));
    for (int b = 0; b < TW_DRAWBARS; b++)
        CHECK(o.contact[24][b] == (b < 5), "bus %d survived the takeover", b);
    tw_organ_note(&o, 60, false, 0);
    settle(&o, 4800);
    CHECK(contacts_made(&o, 24) == 0 && !o.held[24],
          "note-off after depth left %d contacts", contacts_made(&o, 24));
    CHECK(organ_rms(&o, 4800) < 1e-6, "note-off after depth must reach silence");

    /* A key that is not held ignores depth entirely, before and after. */
    depth_organ(&o, all8);
    tw_organ_note_depth(&o, 60, 127);
    settle(&o, 4800);
    CHECK(!o.held[24] && contacts_made(&o, 24) == 0 && organ_rms(&o, 4800) < 1e-6,
          "depth on an untouched key must do nothing");
    tw_organ_note(&o, 60, true, 127);
    settle(&o, 4800);
    tw_organ_note(&o, 60, false, 0);
    settle(&o, 4800);
    tw_organ_note_depth(&o, 60, 127); /* a late message from the surface */
    settle(&o, 4800);
    CHECK(!o.held[24] && contacts_made(&o, 24) == 0 && organ_rms(&o, 4800) < 1e-6,
          "depth after note-off must not resurrect the key");

    /* Out-of-compass depth is ignored and counted, like an out-of-compass
     * note. */
    depth_organ(&o, all8);
    tw_organ_note_depth(&o, 35, 100);
    tw_organ_note_depth(&o, 97, 100);
    CHECK(o.out_of_compass == 2, "compass counter, got %u", o.out_of_compass);

    /* Ninth-drawbar theft (sec 8): with percussion on, the 1' bus is the
     * trigger-sensing line, so the top step of the travel is out of
     * service — depth 8 and depth 9 are the same sound, exactly. */
    static float d8[TW_WHEELS], d9[TW_WHEELS];
    depth_organ(&o, all8);
    tw_organ_set_percussion(&o, true, false, false, true);
    tw_organ_note(&o, 60, true, 127);
    settle(&o, 4800);
    tw_organ_note_depth(&o, 60, depth_forcing(8));
    settle(&o, 4800);
    memcpy(d8, o.gen.keyed_target, sizeof d8);
    tw_organ_note_depth(&o, 60, depth_forcing(9));
    settle(&o, 4800);
    memcpy(d9, o.gen.keyed_target, sizeof d9);
    CHECK(memcmp(d8, d9, sizeof d8) == 0,
          "percussion on: the ninth contact must be silent");
    tw_organ_set_percussion(&o, false, false, false, true);
    settle(&o, 4800);
    memcpy(d9, o.gen.keyed_target, sizeof d9);
    CHECK(memcmp(d8, d9, sizeof d8) != 0,
          "percussion off: the ninth contact must return");

    /* Percussion follows the 1' contact (sec 8), so a press that stops
     * short of the ninth contact never fires it — and leaves the envelope
     * armed for whoever does bottom out. The depth message arrives inside
     * a slow press, which is what a surface actually sends. */
    const int w60 = tw_wheel_index(60 - TW_NOTE_MIN + 1, 3);
    depth_organ(&o, all8);
    tw_organ_set_percussion(&o, true, false, false, true);
    tw_organ_note(&o, 60, true, 1); /* ~15 ms of travel to intercept */
    settle(&o, 240);
    tw_organ_note_depth(&o, 60, depth_forcing(5));
    settle(&o, 4800);
    CHECK(o.perc.armed && o.perc.wheel == 0,
          "a half-press must not fire percussion: armed %d, wheel %d",
          o.perc.armed, o.perc.wheel);

    /* Pushing the same held key the rest of the way down fires it, with
     * no new note event anywhere. */
    tw_organ_note_depth(&o, 60, 127);
    settle(&o, 480);
    CHECK(!o.perc.armed && o.perc.wheel == w60,
          "bottoming out must fire it: armed %d, wheel %d",
          o.perc.armed, o.perc.wheel);

    /* And riding back off the ninth contact and onto it again retriggers,
     * once the recovery has passed — the travel is a percussion control
     * in its own right. */
    tw_organ_note_depth(&o, 60, depth_forcing(8));
    settle(&o, 4800);
    CHECK(o.perc.armed, "lifting off the ninth contact must re-arm");
    double quiet = (double)o.gen.perc_target[w60 - 1];
    tw_organ_note_depth(&o, 60, 127);
    settle(&o, 48);
    CHECK((double)o.gen.perc_target[w60 - 1] > quiet,
          "riding back onto it must retrigger without a note event");

    /* Panic clears the travel and the sensing line with everything else. */
    depth_organ(&o, all8);
    tw_organ_note(&o, 60, true, 127);
    settle(&o, 4800);
    tw_organ_note_depth(&o, 60, depth_forcing(5));
    settle(&o, 480);
    tw_organ_panic(&o);
    CHECK(o.made[24] == 0 && !o.held[24] && contacts_made(&o, 24) == 0,
          "panic must clear the travel, made %d", o.made[24]);
    CHECK(o.perc.armed && o.perc.sense_n == 0 && o.perc.rearm_at == 0,
          "panic must un-ground the sensing line, armed %d sense %u",
          o.perc.armed, o.perc.sense_n);

    /* The sensing-line count is derived state, and percussion now leans
     * on it. Hammer it: overlapping notes at mixed velocities, depth
     * moves across the ninth make point, panics mid-flight — and after
     * every single tick it must still equal the closed 1' contacts.
     * (A stuck count would silently arm or disarm percussion forever.) */
    depth_organ(&o, all8);
    tw_organ_set_percussion(&o, true, false, false, true);
    uint64_t seed = 0x5eed1234u;
    int bad = 0;
    for (int i = 0; i < 24000; i++) {
        uint64_t r = tw_splitmix64(&seed);
        int note = 48 + (int)(r % 12);
        switch ((r >> 8) % 5) {
        case 0: tw_organ_note(&o, note, true, 1 + (int)((r >> 16) % 127)); break;
        case 1: tw_organ_note(&o, note, false, 0); break;
        case 2: tw_organ_note_depth(&o, note, (int)((r >> 24) % 128)); break;
        case 3: tw_organ_note_depth(&o, note, 127); break;
        default: if ((r >> 32) % 400 == 0) tw_organ_panic(&o); break;
        }
        (void)tw_organ_tick(&o);
        int closed = 0;
        for (int k = 0; k < TW_KEYS; k++) closed += o.contact[k][TW_DRAWBARS - 1];
        if (o.perc.sense_n != closed) bad++;
    }
    CHECK(bad == 0, "sense_n drifted from the closed 1' contacts on %d ticks", bad);

    /* Determinism: the depth stream is an event stream like any other,
     * and a stream that is never sent leaves the render untouched. */
    static float r1[19200], r2[19200], plain[19200], base[19200];
    run_depth_script(r1, 19200, true);
    run_depth_script(r2, 19200, true);
    CHECK(memcmp(r1, r2, sizeof r1) == 0, "depth script runs differ");
    run_depth_script(plain, 19200, false);
    run_script(base, 19200, 0.0f);
    CHECK(memcmp(plain, base, sizeof base) == 0,
          "no depth message must render the pre-depth organ bit-for-bit");
    CHECK(memcmp(r1, plain, sizeof r1) != 0, "the depth stream must be audible");
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

    /* the DC image under a loud steady tone: the derived triode curve
     * is asymmetric (conduction rail +3.72 vs cutoff floor -1.83), so
     * rectification pushes the shaper's mean POSITIVE — the reference's
     * rising plate-current mean (its vk_mean climbs; warmth-evidence),
     * and the coupling cap breathes on exactly this image. Measured as
     * the tracker's mean over the last 11 whole cycles: a single
     * end-sample would alias the tracker's ~0.06 fundamental ripple
     * (the old kernel's -0.02 pass sat inside that ripple). */
    tw_drive_init(&d, 48000.0f);
    tw_drive_set(&d, 0.8f);
    double ph = 0.0, dc = 0.0;
    for (int i = 0; i < 48000; i++) {
        (void)tw_drive_tick(&d, 2.0f * tw_sin_turns((float)ph));
        if (i >= 45600) dc += (double)d.hp_lp; /* 2400 = 11 x 220 Hz */
        ph += 220.0 / 48000.0;
        if (ph >= 1.0) ph -= 1.0;
    }
    dc /= 2400.0;
    CHECK(d.env > 0.5f && d.env < 1.4f,
          "follower must ride the loud tone, env %f", (double)d.env);
    CHECK(dc > 0.02,
          "rectified DC image must swing positive, got %f", dc);
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
 * each bit-for-bit — the M7-7 identity contract. The DRIVE pin was
 * re-captured at the warmth pass (docs/warmth-evidence.md): the preamp
 * kernel changed by derivation, so the driven script legitimately
 * renders anew (was 0x730ac52bff1f129d); the other three are
 * drive-free or rotary-only and survive the pass untouched. */
static const uint64_t PRE_M7_SCRIPT_FNV = 0xf0b4c7c3f7705480u;
static const uint64_t PRE_M7_VIB_FNV = 0xb01485a1702721a3u;
static const uint64_t PRE_M7_DRIVE_FNV = 0xa3c0070288f0a1cdu;
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

/* --- EP1: the ep73 struck-voice bank (ep-constants.md 2-6, 9) --- */

/* The test's own copies of the pinned constants: an oracle in double
 * precision, not a second reference to the implementation's tables. */
static const double EPO_F_E1 = 41.203444614108747;                /* sec 2   */
static const double EPO_RATIO[3] = { 1.0, 6.2669, 17.5475 };      /* sec 3   */
static const double EPO_MODE_T60[3] = { 1.0, 0.18480, 0.05535 };  /* sec 4   */
static const double EPO_BASE_W[3] = { 1.0, 1.0, 1.0 };            /* sec 5.2 */
static const double EPO_CORNER[5] = { 55.556, 83.333, 119.048, 175.439, 256.410 };
static const double EPO_GAMMA = 1.042; /* sec 5.1, D3a closed by ear */
static const double EPO_BETA[3] = { 1.8751041, 4.6940911, 7.8547574 };
static const double EPO_BEAM_K = 1.34517;   /* f1 = K / L^2, L in metres  */
static const double EPO_X0_BASS = 57.150, EPO_X0_TREBLE = 3.175; /* mm    */
static const double EPO_L_MAX = 111.125; /* longest blank, [EP-SM 5-1]     */
static const double EPO_T60_E1 = 17.0, EPO_T60_RATIO = 0.980099;
static const double EPO_DRIVE_REF = 0.38555271;   /* 2^(-11/8), sec 6.1 */
static const double EPO_PICKUP_F_REF = 329.6276;
static const double EPO_PICKUP_OFFSET = 0.35;
static const double EP_RATE = 48000.0;

static int epo_zone(int midi) {
    return midi <= 50 ? 0 : midi <= 60 ? 1 : midi <= 70 ? 2 : midi <= 84 ? 3 : 4;
}

static double epo_freq(int key) { return EPO_F_E1 * pow(2.0, key / 12.0); }

static double epo_t60(int key, int mode) {
    return EPO_T60_E1 * pow(EPO_T60_RATIO, key) * EPO_MODE_T60[mode];
}

/* The exact clamped-free mode shape, in double and with libm, against
 * which the core's pinned cubic fit has to hold up. */
static double epo_phi(double b, double xi) {
    double s = (cosh(b) + cos(b)) / (sinh(b) + sin(b));
    return cosh(b * xi) - cos(b * xi) - s * (sinh(b * xi) - sin(b * xi));
}

static double epo_xi(int key) {
    double L_mm = 1000.0 * sqrt(EPO_BEAM_K / epo_freq(key));
    if (L_mm > EPO_L_MAX) L_mm = EPO_L_MAX; /* the harp is only so deep */
    double x0 = EPO_X0_BASS
              * pow(EPO_X0_TREBLE / EPO_X0_BASS, key / (double)(EP_KEYS - 1));
    return x0 / L_mm;
}

static double epo_shape(int key, int mode) {
    if (mode == 0) return 1.0;
    double xi = epo_xi(key);
    return fabs(epo_phi(EPO_BETA[mode], xi) / epo_phi(EPO_BETA[0], xi));
}

static double epo_weight(int key, int mode, int velocity) {
    double f1 = epo_freq(key), fm = f1 * EPO_RATIO[mode];
    double c0 = EPO_CORNER[epo_zone(key + EP_NOTE_MIN)];
    double fc2 = c0 * c0 * sqrt(velocity / 127.0);
    return EPO_BASE_W[mode] * epo_shape(key, mode)
         * (fc2 + f1 * f1) / (fc2 + fm * fm);
}

/* Hann-windowed amplitude of the bin at f; ratios between bins read
 * directly as harmonic ratios. */
static double ep_bin(const float *x, int n, double f) {
    double w = TAU_D * f / EP_RATE, sr = 0.0, si = 0.0, wsum = 0.0;
    for (int i = 0; i < n; i++) {
        double win = 0.5 - 0.5 * cos(TAU_D * i / (n - 1));
        sr += x[i] * win * cos(w * i);
        si -= x[i] * win * sin(w * i);
        wsum += win;
    }
    return sqrt(sr * sr + si * si) / wsum;
}

static void test_ep_note_map(void) {
    CHECK(ep_key_freq_hz(-1) == 0.0f && ep_key_freq_hz(EP_KEYS) == 0.0f,
          "out-of-range key must return 0");
    int bad = 0;
    for (int k = 0; k < EP_KEYS; k++)
        if (fabs(ep_key_freq_hz(k) / epo_freq(k) - 1.0) > 2e-6) bad++;
    CHECK(bad == 0, "equal-temperament table off the oracle at %d keys", bad);

    /* The compass endpoints and the tuning anchor, sec 2. */
    CHECK(fabs(ep_key_freq_hz(0) - 41.2034) < 1e-3, "E1 must be 41.2034 Hz");
    CHECK(fabs(ep_key_freq_hz(72) - 2637.02) < 1e-2, "E7 must be 2637.02 Hz");
    CHECK(fabs(ep_key_freq_hz(69 - EP_NOTE_MIN) - 440.0) < 1e-3,
          "MIDI 69 must land on A440");

    /* Hammer-hardness zone boundaries, sec 5.2 [EP-SM 4-3]. */
    CHECK(ep_zone(28) == 0 && ep_zone(50) == 0 && ep_zone(51) == 1, "zone 0/1 edge");
    CHECK(ep_zone(60) == 1 && ep_zone(61) == 2 && ep_zone(70) == 2, "zone 1/2 edge");
    CHECK(ep_zone(71) == 3 && ep_zone(84) == 3 && ep_zone(85) == 4, "zone 3/4 edge");

    /* Out-of-compass notes are ignored and counted, as in the organ. */
    ep_bank b;
    ep_bank_init(&b, (float)EP_RATE);
    ep_bank_strike(&b, EP_NOTE_MIN - 1, 100);
    ep_bank_strike(&b, EP_NOTE_MAX + 1, 100);
    double e = 0.0;
    for (int i = 0; i < 480; i++) e += fabs(ep_bank_tick(&b));
    CHECK(b.out_of_compass == 2 && e == 0.0,
          "out-of-compass strikes must count (%u) and stay silent (%g)",
          b.out_of_compass, e);
}

static void test_ep_modes(void) {
    /* sec 3: the clamped-free cantilever eigenvalues, squared. */
    double b1 = 1.8751041, b2 = 4.6940911, b3 = 7.8547574;
    CHECK(fabs(EP_MODE_RATIO[1] - (b2 / b1) * (b2 / b1)) < 1e-3,
          "mode 2 ratio must be the clamped-free value, got %g", (double)EP_MODE_RATIO[1]);
    CHECK(fabs(EP_MODE_RATIO[2] - (b3 / b1) * (b3 / b1)) < 1e-3,
          "mode 3 ratio must be the clamped-free value, got %g", (double)EP_MODE_RATIO[2]);

    /* sec 3.1: a mode at or above 0.45 fs renders at gain zero — silence,
     * not foldback — and its step is parked at 0. */
    ep_bank b;
    ep_bank_init(&b, (float)EP_RATE);
    int gate_bad = 0, step_bad = 0;
    for (int k = 0; k < EP_KEYS; k++)
        for (int m = 0; m < EP_MODES; m++) {
            double f = epo_freq(k) * EPO_RATIO[m];
            float want = f < 0.45 * EP_RATE ? 1.0f : 0.0f;
            if (b.gate[m][k] != want) gate_bad++;
            if (want == 0.0f ? b.step[m][k] != 0.0f
                             : fabs(b.step[m][k] - f / EP_RATE) > 1e-6) step_bad++;
        }
    CHECK(gate_bad == 0 && step_bad == 0,
          "Nyquist gating wrong: %d gates, %d steps", gate_bad, step_bad);

    CHECK(b.gate[2][87 - EP_NOTE_MIN] == 0.0f && b.gate[2][86 - EP_NOTE_MIN] == 1.0f,
          "at 48 kHz mode 3 must silence from MIDI 87");
    CHECK(b.gate[1][EP_KEYS - 1] == 1.0f,
          "at 48 kHz mode 2 must survive the whole compass");

    ep_bank_init(&b, 44100.0f);
    CHECK(b.gate[2][86 - EP_NOTE_MIN] == 0.0f && b.gate[2][85 - EP_NOTE_MIN] == 1.0f,
          "at 44.1 kHz mode 3 must silence from MIDI 86");

    /* A muted mode must stay silent however hard it is struck. */
    ep_bank_init(&b, (float)EP_RATE);
    ep_bank_strike(&b, 100, 127);
    CHECK(b.amp[2][72] == 0.0f, "a Nyquist-muted mode must take no energy");
}

static void test_ep_decay(void) {
    CHECK(ep_t60_s(-1, 0) == 0.0f && ep_t60_s(0, EP_MODES) == 0.0f,
          "out-of-range t60 must return 0");

    /* The two sourced anchors, sec 4 [EP-P61]: 17 s at the bottom, and the
     * patent's 3-5 s band at the top. */
    CHECK(ep_t60_s(0, 0) == 17.0f, "E1 t60 must be the pinned 17 s exactly");
    CHECK(ep_t60_s(72, 0) > 3.0f && ep_t60_s(72, 0) < 5.0f,
          "E7 t60 must land in the patent's 3-5 s band, got %g",
          (double)ep_t60_s(72, 0));

    int bad = 0;
    for (int k = 0; k < EP_KEYS; k++)
        for (int m = 0; m < EP_MODES; m++)
            if (fabs(ep_t60_s(k, m) / epo_t60(k, m) - 1.0) > 1e-5) bad++;
    CHECK(bad == 0, "t60 table off the oracle at %d (key, mode) pairs", bad);

    /* The rendered decay, not the table: strike the top note and time the
     * -60 dB crossing of mode 1's amplitude state. */
    ep_bank b;
    ep_bank_init(&b, (float)EP_RATE);
    ep_bank_strike(&b, 100, 127);
    float a0 = b.amp[0][72];
    long n = 0;
    while (b.amp[0][72] > a0 * 1e-3f && n < 40L * (long)EP_RATE) {
        ep_bank_tick_gated(&b);
        n++;
    }
    double meas = n / EP_RATE;
    CHECK(fabs(meas / ep_t60_s(72, 0) - 1.0) < 0.01,
          "measured t60 %.3f s must match the pinned %.3f s", meas,
          (double)ep_t60_s(72, 0));

    /* sec 4.1: the epsilon snap must reach exact silence, so a decayed
     * voice never enters denormal territory. */
    while (b.live_n > 0 && n < 400L * (long)EP_RATE) {
        ep_bank_tick_gated(&b);
        n++;
    }
    CHECK(b.amp[0][72] == 0.0f && b.amp[1][72] == 0.0f && b.amp[2][72] == 0.0f,
          "a decayed voice must snap to exact zero");
    for (int i = 0; i < 24000; i++) ep_bank_tick_gated(&b); /* discharge */
    CHECK(ep_bank_tick_gated(&b) == 0.0f && ep_bank_tick(&b) == 0.0f,
          "a silent bank must render exact zero in both layouts");
}

static void test_ep_strike(void) {
    /* The core carries the mode shape as a pinned table because it has no
     * libm and because the length law has a corner in it; the oracle
     * computes the exact transcendental shape and every entry must match
     * to the table's own rounding. */
    int shape_bad = 0;
    for (int k = 0; k < EP_KEYS; k++)
        for (int m = 0; m < EP_MODES; m++)
            if (fabs(ep_mode_shape(k, m) / epo_shape(k, m) - 1.0) > 1e-4)
                shape_bad++;
    CHECK(shape_bad == 0,
          "the pinned mode-shape table is off the exact shape at %d (key, mode)",
          shape_bad);
    CHECK(ep_mode_shape(0, 0) == 1.0f && ep_mode_shape(72, 0) == 1.0f,
          "mode 1's shape factor is 1 by construction");
    CHECK(ep_mode_shape(-1, 1) == 0.0f && ep_mode_shape(0, EP_MODES) == 0.0f,
          "out-of-range mode shape must return 0");

    /* Striking near the clamp couples to the high modes, so the shape
     * factors exceed 1 and grow toward the treble as the strike moves
     * proportionally closer to the fixed end (sec 5.2). */
    CHECK(ep_mode_shape(0, 1) > 1.0f && ep_mode_shape(72, 1) > ep_mode_shape(0, 1),
          "mode 2's shape factor must exceed 1 and rise toward the treble");

    /* The corner in the length law is the sourced part: below the point
     * where the tine would have to be longer than the longest blank the
     * factory ships, it stops growing and the counterweight carries the
     * pitch instead. That is what pulls the bass strike back toward the
     * middle of the tine and takes the clang off the top of the note. */
    CHECK(ep_mode_shape(0, 1) < 0.6f * ep_mode_shape(17, 1),
          "the length cap must cut the bass shape factor well below the"
          " uncapped part of the compass");

    int bad = 0;
    for (int k = 0; k < EP_KEYS; k += 7)
        for (int m = 0; m < EP_MODES; m++)
            for (int v = 1; v <= 127; v += 9)
                if (fabs(ep_mode_weight(k, m, v) / epo_weight(k, m, v) - 1.0) > 3e-3)
                    bad++;
    CHECK(bad == 0, "mode weight off the oracle in %d cases", bad);

    /* The contact roll-off has to be a hammer, not a tone control: the
     * corner implied by each zone's contact time is fc = 1/(2 t_c). */
    int corner_bad = 0;
    for (int z = 0; z < EP_ZONES; z++)
        if (fabs(EP_CORNER_HZ[z] * 2.0 * EP_CONTACT_MS[z] * 1e-3 - 1.0) > 1e-4)
            corner_bad++;
    CHECK(corner_bad == 0, "%d zone corners disagree with their contact time",
          corner_bad);

    /* sec 5.2: the roll-off is normalised at mode 1, so velocity moves
     * loudness and timbre through separate laws. */
    int norm_bad = 0;
    for (int k = 0; k < EP_KEYS; k++)
        for (int v = 1; v <= 127; v += 13)
            if (ep_mode_weight(k, 0, v) != EP_BASE_W[0]) norm_bad++;
    CHECK(norm_bad == 0, "mode 1 weight must be exactly EP_BASE_W[0], %d off", norm_bad);

    /* A harder blow may only ever brighten. */
    int mono_bad = 0;
    for (int k = 0; k < EP_KEYS; k += 5)
        for (int m = 1; m < EP_MODES; m++)
            for (int v = 2; v <= 127; v++)
                if (ep_mode_weight(k, m, v) < ep_mode_weight(k, m, v - 1)) mono_bad++;
    CHECK(mono_bad == 0, "mode weight must rise with velocity, %d inversions", mono_bad);

    CHECK(ep_mode_weight(-1, 0, 100) == 0.0f && ep_mode_weight(0, EP_MODES, 100) == 0.0f,
          "hostile key/mode must return 0");
    CHECK(ep_mode_weight(30, 1, 0) == ep_mode_weight(30, 1, 1) &&
          ep_mode_weight(30, 1, 999) == ep_mode_weight(30, 1, 127),
          "hostile velocity must clamp into 1..127");

    /* sec 5.1: the level law is (v/127)^gamma with gamma pinned by ear at
     * 1.042, so doubling velocity gives slightly more than double. Mode 1's
     * flat weight is what isolates the law from the spectrum. */
    ep_bank b;
    int law_bad = 0;
    for (int v = 1; v <= 127; v += 3) {
        ep_bank_init(&b, (float)EP_RATE);
        ep_bank_strike(&b, 64, v);
        double want = pow(v / 127.0, EPO_GAMMA) * ep_mode_weight(36, 0, v);
        if (fabs(b.amp[0][36] / want - 1.0) > 1e-5) law_bad++;
    }
    CHECK(law_bad == 0, "the level law is off its exponent at %d velocities",
          law_bad);
    ep_bank_init(&b, (float)EP_RATE);
    ep_bank_strike(&b, 64, 40);
    float a40 = b.amp[0][36];
    ep_bank_init(&b, (float)EP_RATE);
    ep_bank_strike(&b, 64, 80);
    float a80 = b.amp[0][36];
    CHECK((double)a80 / a40 > 2.0 && (double)a80 / a40 < 2.1,
          "gamma just above 1 must make doubling velocity slightly more than"
          " double, got %g", (double)a80 / a40);

    /* sec 5.3: a mode at rest has its phase reset, a ringing one keeps it. */
    ep_bank_silence(&b);
    for (int i = 0; i < 100; i++) ep_bank_tick(&b);
    ep_bank_strike(&b, 64, 100);
    CHECK(b.phase[0][36] == 0.0f, "a strike on a silent mode must reset its phase");
    for (int i = 0; i < 100; i++) ep_bank_tick(&b);
    float held = b.phase[0][36];
    ep_bank_strike(&b, 64, 100);
    CHECK(b.phase[0][36] == held, "a strike on a ringing mode must keep its phase");
}

static void test_ep_pickup(void) {
    /* sec 6: the field itself, against the transcendental it is derived
     * from. The core has no libm and computes it by seeded Newton; the
     * oracle here has libm, so the kernel is validated rather than
     * trusted — the same arrangement the mode-shape tables use. */
    double field_worst = 0.0;
    for (int i = -60000; i <= 60000; i++) {
        double u = i * 0.001;
        double want = pow(1.0 + u * u, -1.5);
        double err = fabs((double)ep_field((float)u) - want) / want;
        if (err > field_worst) field_worst = err;
    }
    CHECK(field_worst < 1e-6,
          "ep_field must match (1+u^2)^-3/2, worst relative error %.3e",
          field_worst);

    CHECK(ep_field(0.0f) == 1.0f, "the field must be exactly 1 at dead centre");
    int even_bad = 0;
    for (int i = 1; i <= 8000; i++)
        if (ep_field(i * 0.001f) != ep_field(i * -0.001f)) even_bad++;
    CHECK(even_bad == 0, "the field must be exactly even in u, %d mismatches",
          even_bad);

    /* The property that replaces monotonicity, and the whole reason this
     * kernel exists: the field has no rail. Past dead centre it falls
     * forever as u^-3 and never goes flat, so no operating point can pin
     * the output against a ceiling and flatten a note's envelope. The old
     * saturator reached its bound at |u| = 3 with exactly zero slope and
     * the bass sat on it. */
    int flat = 0;
    for (int i = 1; i <= 20000; i++) {
        float u = i * 0.01f;
        if (ep_field(u) <= ep_field(u + 0.01f)) flat++;
    }
    CHECK(flat == 0,
          "the field must fall strictly for every u > 0, %d flat or rising",
          flat);

    /* sec 6: the rest offset is a by-ear setup adjustment [EP-SM 4-7] and
     * carries no derivation, but the field does forbid one value. Psi''
     * vanishes at exactly u = 1/2, so an offset there generates no second
     * harmonic at all and the voice comes out odd-dominant and hollow.
     * The pin must stay clear of it, and on the side the manual names —
     * "slightly above dead center", so below the null, where the ladder is
     * even-dominant. */
    double infl = 0.0;
    for (int i = 1; i <= 30000; i++) {
        double u = i * 0.0001;
        if (12.0 * u * u - 3.0 >= 0.0) { infl = u; break; }
    }
    CHECK(fabs(infl - 0.5) < 1e-3,
          "the field's inflection must sit at u = 1/2, found %.4f", infl);
    CHECK(EP_PICKUP_OFFSET < 0.45f && EP_PICKUP_OFFSET > 0.15f,
          "the rest offset must stay clear of the H2 null and stay slight, %.3f",
          (double)EP_PICKUP_OFFSET);

    float ref0 = ep_field(EP_PICKUP_OFFSET);
    CHECK(ep_pickup(0.0f, 1.0f, EP_PICKUP_OFFSET, ref0) == 0.0f,
          "the pickup must pass zero at zero");

    /* sec 6: the ladder must stay filled past the third harmonic — the
     * defect that made the cubic series leave the clang standing alone
     * over an empty octave and a half. */
    static float lad[8192];
    double f = 300.0, gg = 0.6, x0 = EP_PICKUP_OFFSET;
    for (int i = 0; i < 8192; i++)
        lad[i] = ep_pickup((float)sin(TAU_D * f * i / EP_RATE), (float)gg,
                           (float)x0, ref0);
    double h1 = ep_bin(lad, 8192, f);
    int thin = 0;
    for (int k = 4; k <= 6; k++)
        if (20 * log10(ep_bin(lad, 8192, k * f) / h1) < -60.0) thin++;
    CHECK(thin == 0,
          "the pickup must carry harmonics past the third, %d of 3 missing",
          thin);

    /* sec 6.1: the drive law, uncapped, and its geometric anchor. */
    CHECK(ep_pickup_drive(-1) == 0.0f && ep_pickup_drive(EP_KEYS) == 0.0f,
          "out-of-range pickup drive must return 0");
    int drive_bad = 0;
    for (int k = 0; k < EP_KEYS; k++) {
        double want = EPO_DRIVE_REF * pow(EPO_PICKUP_F_REF / epo_freq(k), (double)EP_PICKUP_SLOPE);
        if (fabs(ep_pickup_drive(k) / want - 1.0) > 1e-4) drive_bad++;
    }
    CHECK(drive_bad == 0, "pickup drive off its law at %d keys", drive_bad);
    CHECK(ep_pickup_drive(0) > ep_pickup_drive(36)
          && ep_pickup_drive(36) > ep_pickup_drive(72),
          "the bass must reach its bark at a lower dynamic than the treble");

    /* The anchor: the hardest blow on the lowest tine sweeps exactly half
     * a gap. Mode 1 at velocity 127 has amplitude exactly 1.0 by the sec 7
     * reference, so the swing in gap units is the drive itself. */
    CHECK(fabs((double)ep_pickup_drive(0) - 0.5) < 1e-5,
          "E1 at full strike must sweep half a gap, got %.6f",
          (double)ep_pickup_drive(0));
    int over = 0;
    for (int k = 0; k < EP_KEYS; k++)
        if (ep_pickup_drive(k) > ep_pickup_drive(0)) over++;
    CHECK(over == 0,
          "the uncapped law must peak at the bottom of the compass, %d above",
          over);

    /* And the bark is graded rather than global: at full strike the tine
     * crosses dead centre at the bottom of the compass and never does at
     * the top. That is the invariant the drive law exists to produce, and
     * it holds for any slope in the section 6.1 bracket — the earlier form
     * of this check pinned the crossing to the lower middle, which encoded
     * one slope setting rather than the property, and failed the moment
     * the ballot moved it. Where it ends is a consequence of the pin and
     * is reported in sec 6.1, not asserted here. */
    CHECK(ep_pickup_drive(0) > EP_PICKUP_OFFSET,
          "the lowest tine must cross dead centre at full strike");
    CHECK(ep_pickup_drive(EP_KEYS - 1) < EP_PICKUP_OFFSET,
          "the highest tine must never cross it");
    int cross_hi = -1;
    for (int k = 0; k < EP_KEYS; k++)
        if (ep_pickup_drive(k) > EP_PICKUP_OFFSET) cross_hi = k;
    CHECK(cross_hi > 0 && cross_hi < EP_KEYS - 1,
          "crossing must end inside the compass, ends at key %d", cross_hi);

    /* sec 6: the f32 kernel must reproduce the transcendental's harmonics,
     * not merely its values, across the whole swing range the compass uses.
     * This is the claim the EP1 exhibit used to carry, and it belongs here
     * instead: the exhibit measures a rendered note, where the contact
     * transient lands in the same bins and moves the ratio either way. */
    {
        double gE4 = ep_pickup_drive(36), worst = 0.0;
        const double LV[7] = { 0.0064245, 0.0560862, 0.1685609, 0.3549462,
                               0.5375621, 0.7795366, 1.0 };
        for (int i = 0; i < 7; i++) {
            double sw = gE4 * LV[i];
            double fr1 = 0, fi1 = 0, fr2 = 0, fi2 = 0;
            double dr1 = 0, di1 = 0, dr2 = 0, di2 = 0;
            for (int n = 0; n < 4096; n++) {
                double th = TAU_D * n / 4096.0, u = EP_PICKUP_OFFSET + sw * sin(th);
                double yf = (double)(ep_field(EP_PICKUP_OFFSET) - ep_field((float)u));
                double yd = pow(1.0 + (double)EP_PICKUP_OFFSET * EP_PICKUP_OFFSET, -1.5)
                          - pow(1.0 + u * u, -1.5);
                fr1 += yf * cos(th); fi1 += yf * sin(th);
                fr2 += yf * cos(2 * th); fi2 += yf * sin(2 * th);
                dr1 += yd * cos(th); di1 += yd * sin(th);
                dr2 += yd * cos(2 * th); di2 += yd * sin(2 * th);
            }
            double rf = sqrt(fr2 * fr2 + fi2 * fi2) / sqrt(fr1 * fr1 + fi1 * fi1);
            double rd = sqrt(dr2 * dr2 + di2 * di2) / sqrt(dr1 * dr1 + di1 * di1);
            double e = fabs(20 * log10(rf / rd));
            if (e > worst) worst = e;
        }
        CHECK(worst < 0.5,
              "f32 field must track the transcendental's H2/H1, worst %.2f dB",
              worst);
    }

    /* sec 6, and the reason this kernel replaced the saturator: a note's
     * envelope must track its tine. Drive the field at the bass operating
     * point with a decaying sine and check the output decays with it —
     * against the old saturator's bass, which held its level while the
     * tine fell 12 dB behind it. */
    double lo_out = 0.0, hi_out = 0.0;
    for (int i = 0; i < 8192; i++) {
        double env_a = 1.0, env_b = 0.25;   /* 12 dB apart */
        double s = sin(TAU_D * f * i / EP_RATE);
        double ya = (double)ep_pickup((float)(env_a * s), ep_pickup_drive(0),
                                      EP_PICKUP_OFFSET, ref0);
        double yb = (double)ep_pickup((float)(env_b * s), ep_pickup_drive(0),
                                      EP_PICKUP_OFFSET, ref0);
        hi_out += ya * ya;
        lo_out += yb * yb;
    }
    double tracked = 10 * log10(hi_out / lo_out);
    CHECK(tracked > 9.0,
          "a 12 dB fall at the tine must reach the bus, only %.1f dB did",
          tracked);

    /* The mean-square term is what keeps a decaying voice from dragging a
     * DC thump behind it: the bus must stay centred while a note dies. */
    ep_bank b;
    ep_bank_init(&b, (float)EP_RATE);
    ep_bank_strike(&b, 64, 127);
    double sum = 0.0, sq = 0.0;
    long n = (long)EP_RATE;
    for (long i = 0; i < n; i++) {
        double x = ep_bank_tick(&b);
        sum += x;
        sq += x * x;
    }
    double mean = sum / n, rms = sqrt(sq / n);
    CHECK(fabs(mean) < 0.02 * rms,
          "pickup DC residual %.5f must stay under 2%% of rms %.5f", mean, rms);
}

static void test_ep_bank(void) {
    /* sec 9: the two layouts differ in cost, never in output — which is
     * what lets decision D4 rest on measurement alone. */
    ep_bank adv, gat;
    ep_bank_init(&adv, (float)EP_RATE);
    ep_bank_init(&gat, (float)EP_RATE);
    uint64_t ha = 0, hg = 0;
    int diff = 0;
    long n = (long)EP_RATE;
    for (long i = 0; i < n; i++) {
        if (i % 4800 == 0) {
            int s = (int)(i / 4800);
            ep_bank_strike(&adv, 34 + 6 * s, 15 + 11 * s);
            ep_bank_strike(&gat, 34 + 6 * s, 15 + 11 * s);
        }
        float xa = ep_bank_tick(&adv), xg = ep_bank_tick_gated(&gat);
        if (memcmp(&xa, &xg, sizeof xa) != 0) diff++;
        ha = tw_fnv1a64(&xa, sizeof xa, ha);
        hg = tw_fnv1a64(&xg, sizeof xg, hg);
    }
    CHECK(diff == 0 && ha == hg,
          "the two bank layouts must be bit-identical: %d samples differ", diff);

    /* Same input, same binary, same bits. */
    ep_bank_init(&adv, (float)EP_RATE);
    uint64_t h2 = 0;
    for (long i = 0; i < n; i++) {
        if (i % 4800 == 0) {
            int s = (int)(i / 4800);
            ep_bank_strike(&adv, 34 + 6 * s, 15 + 11 * s);
        }
        float x = ep_bank_tick(&adv);
        h2 = tw_fnv1a64(&x, sizeof x, h2);
    }
    CHECK(h2 == ha, "two runs of one script must render identical bits");

    /* Panic to exact silence, phases included. */
    ep_bank_silence(&adv);
    int live = 0;
    for (int m = 0; m < EP_MODES; m++)
        for (int k = 0; k < EP_KEYS; k++)
            if (adv.amp[m][k] != 0.0f || adv.phase[m][k] != 0.0f) live++;
    CHECK(live == 0 && adv.live_n == 0, "silence must clear every voice, %d left", live);

    /* A hostile sample rate falls back to the default rather than dividing
     * by nonsense — the tw_generator_init contract. */
    ep_bank_init(&adv, 0.0f);
    CHECK(fabs(adv.step[0][36] - epo_freq(36) / EP_RATE) < 1e-6,
          "a hostile sample rate must fall back to 48 kHz");
}

/* --- EP2: dampers, the sustain pedal, and the restrike law (sec 5.4, 8) --- */

static const double EPO_DAMP_T60_E1 = 0.35, EPO_DAMP_RATIO = 0.981119;

static double epo_damp_t60(int key) {
    return EPO_DAMP_T60_E1 * pow(EPO_DAMP_RATIO, key);
}

/* Samples until a mode's amplitude falls to 1e-3 of where it started. */
static long ep_fall_to_t60(ep_bank *b, int key, int mode, long limit) {
    float a0 = b->amp[mode][key];
    long n = 0;
    while (b->amp[mode][key] > a0 * 1e-3f && n < limit) {
        ep_bank_tick_gated(b);
        n++;
    }
    return n;
}

static void test_ep_damper(void) {
    CHECK(ep_damp_t60_s(-1) == 0.0f && ep_damp_t60_s(EP_KEYS) == 0.0f,
          "out-of-range damped t60 must return 0");
    CHECK(ep_damp_t60_s(0) == 0.35f, "E1 damped t60 must be the pinned 0.35 s");
    int bad = 0;
    for (int k = 0; k < EP_KEYS; k++)
        if (fabs(ep_damp_t60_s(k) / epo_damp_t60(k) - 1.0) > 1e-5) bad++;
    CHECK(bad == 0, "damped t60 table off the oracle at %d keys", bad);

    /* sec 8: the felt sets the rate and does not care which mode it stops,
     * so unlike the free table this one carries no per-mode factor. */
    ep_bank b;
    ep_bank_init(&b, (float)EP_RATE);
    int modey = 0;
    for (int k = 0; k < EP_KEYS; k++)
        for (int m = 1; m < EP_MODES; m++)
            if (b.dec_damp[m][k] != b.dec_damp[0][k]) modey++;
    CHECK(modey == 0, "the damped decrement must be per key only, %d differ", modey);

    /* At rest the dampers are on; a strike lifts them; damping puts them
     * back. The live decrement is the whole of it. */
    int k = 36;
    CHECK(b.dec[0][k] == b.dec_damp[0][k], "a bank at rest must be damped");
    ep_bank_strike(&b, k + EP_NOTE_MIN, 100);
    CHECK(b.dec[0][k] == b.dec_free[0][k], "a strike must lift the damper");
    ep_bank_damp(&b, k + EP_NOTE_MIN);
    CHECK(b.dec[0][k] == b.dec_damp[0][k], "damping must restore the damped rate");
    ep_bank_undamp(&b, k + EP_NOTE_MIN);
    CHECK(b.dec[0][k] == b.dec_free[0][k], "undamping must restore the free rate");

    /* The rendered damped decay against the pinned table. */
    ep_bank_init(&b, (float)EP_RATE);
    ep_bank_strike(&b, k + EP_NOTE_MIN, 127);
    ep_bank_damp(&b, k + EP_NOTE_MIN);
    double meas = ep_fall_to_t60(&b, k, 0, 10L * (long)EP_RATE) / EP_RATE;
    CHECK(fabs(meas / ep_damp_t60_s(k) - 1.0) < 0.01,
          "measured damped t60 %.4f s must match the pinned %.4f s", meas,
          (double)ep_damp_t60_s(k));

    /* And that it is very much faster than free decay. */
    CHECK(ep_damp_t60_s(k) < 0.05f * ep_t60_s(k, 0),
          "the damper must stop a tine far faster than free decay");
}

static void test_ep_piano_keys(void) {
    ep_piano p;
    ep_piano_init(&p, (float)EP_RATE);

    /* sec 15: an untouched instrument is not silent, it idles on its own
     * floor — the shipped condition is nonzero because a factory-new
     * instrument has tolerances. It is silent only at condition 0. */
    double sq = 0.0;
    for (int i = 0; i < 4800; i++) {
        double x = ep_piano_tick(&p);
        sq += x * x;
    }
    double idle = sqrt(sq / 4800);
    CHECK(idle > 0.0 && idle < 1e-3,
          "an idle piano must carry a quiet floor, got rms %.2e", idle);
    /* At condition 0 it reaches exact silence — but not instantly: the
     * coupling capacitor of sec 6.2 has to discharge first, and the snap
     * that takes it to exact zero is the same one every envelope uses. */
    ep_piano_set_condition(&p, 0.0f);
    for (int i = 0; i < 24000; i++) ep_piano_tick(&p);
    double e = 0.0;
    for (int i = 0; i < 480; i++) e += fabs(ep_piano_tick(&p));
    CHECK(e == 0.0, "at condition 0 an untouched piano must reach exact silence");
    ep_piano_set_condition(&p, EP_CONDITION_DEFAULT);

    /* A release with the pedal up damps; with the pedal down it does not. */
    ep_piano_note(&p, 64, true, 100);
    CHECK(p.held[36] && p.bank.dec[0][36] == p.bank.dec_free[0][36],
          "a held key must ring freely");
    ep_piano_note(&p, 64, false, 0);
    CHECK(!p.held[36] && p.bank.dec[0][36] == p.bank.dec_damp[0][36],
          "a release with the pedal up must damp");

    ep_piano_init(&p, (float)EP_RATE);
    ep_piano_set_sustain(&p, true);
    ep_piano_note(&p, 64, true, 100);
    ep_piano_note(&p, 64, false, 0);
    CHECK(p.bank.dec[0][36] == p.bank.dec_free[0][36],
          "a release with the pedal down must keep the free rate");
    ep_piano_set_sustain(&p, false);
    CHECK(p.bank.dec[0][36] == p.bank.dec_damp[0][36],
          "letting the pedal go must damp a key that is not held");

    /* Catching the pedal late under a note already dying on its damper. */
    ep_piano_init(&p, (float)EP_RATE);
    ep_piano_note(&p, 64, true, 100);
    ep_piano_note(&p, 64, false, 0);
    for (int i = 0; i < 480; i++) ep_piano_tick(&p);
    CHECK(p.bank.dec[0][36] == p.bank.dec_damp[0][36], "should be damping");
    ep_piano_set_sustain(&p, true);
    CHECK(p.bank.dec[0][36] == p.bank.dec_free[0][36],
          "catching the pedal late must lift the damper off a dying note");

    /* A held key is unaffected by the pedal going up. */
    ep_piano_init(&p, (float)EP_RATE);
    ep_piano_set_sustain(&p, true);
    ep_piano_note(&p, 64, true, 100);
    ep_piano_set_sustain(&p, false);
    CHECK(p.bank.dec[0][36] == p.bank.dec_free[0][36],
          "a still-held key must not be damped by the pedal going up");

    /* Note-on with velocity 0 is a note-off. */
    ep_piano_init(&p, (float)EP_RATE);
    ep_piano_note(&p, 64, true, 100);
    ep_piano_note(&p, 64, true, 0);
    CHECK(!p.held[36] && p.bank.dec[0][36] == p.bank.dec_damp[0][36],
          "note-on at velocity 0 must act as a note-off");

    /* Panic drops dampers rather than hard-muting: still ringing, but on
     * the damped rate, with the pedal let go. */
    ep_piano_init(&p, (float)EP_RATE);
    ep_piano_set_sustain(&p, true);
    for (int n = 60; n <= 67; n++) ep_piano_note(&p, n, true, 110);
    ep_piano_panic(&p);
    int held = 0, undamped = 0;
    for (int i = 0; i < EP_KEYS; i++) {
        if (p.held[i]) held++;
        if (p.bank.dec[0][i] != p.bank.dec_damp[0][i]) undamped++;
    }
    double after = 0.0;
    for (int i = 0; i < 48; i++) after += fabs(ep_piano_tick(&p));
    CHECK(!p.sustain && held == 0 && undamped == 0,
          "panic must drop every damper and the pedal (%d held, %d undamped)",
          held, undamped);
    CHECK(after > 0.0, "panic must silence in the damper tau, not hard-mute");

    /* Compass and poly key pressure, sec 10.1. */
    ep_piano_init(&p, (float)EP_RATE);
    ep_piano_note(&p, EP_NOTE_MIN - 1, true, 100);
    ep_piano_note(&p, EP_NOTE_MAX + 1, false, 0);
    ep_piano_key_pressure(&p, EP_NOTE_MAX + 1, 64);
    ep_piano_key_pressure(&p, 64, 64);
    CHECK(p.bank.out_of_compass == 3 && p.pressure == 1,
          "out-of-compass %u and pressure %u must both be counted",
          p.bank.out_of_compass, p.pressure);
    ep_piano_set_condition(&p, 0.0f);
    for (int i = 0; i < 24000; i++) ep_piano_tick(&p);
    double q = 0.0;
    for (int i = 0; i < 480; i++) q += fabs(ep_piano_tick(&p));
    CHECK(q == 0.0, "key pressure must not make a sound");
}

static void test_ep_restrike(void) {
    /* sec 5.4, D5: a blow onto a ringing voice adds to it. */
    ep_bank a;
    ep_bank_init(&a, (float)EP_RATE);
    ep_bank_strike(&a, 64, 60);
    float first = a.amp[0][36];
    ep_bank_strike(&a, 64, 60);
    CHECK(a.amp[0][36] > 1.9f * first,
          "a second blow must add to a ringing mode, got %g from %g",
          (double)a.amp[0][36], (double)first);

    /* sec 5.4: the blow adds, bounded by the hardest single blow. */
    ep_bank c;
    ep_bank_init(&c, (float)EP_RATE);
    int over = 0;
    for (int i = 0; i < 60; i++) {
        ep_bank_strike(&c, 64, 127);
        for (int j = 0; j < 120; j++) ep_bank_tick_gated(&c);
    }
    over = 0;
    for (int m = 0; m < EP_MODES; m++)
        if (c.amp[m][36] > c.ceiling[m][36]) over++;
    CHECK(over == 0, "add must respect the ceiling under stacked blows, %d over", over);
    CHECK(c.amp[0][36] > 0.9f * c.ceiling[0][36],
          "stacked ff blows should actually reach the ceiling");

    /* sec 5.3: a ringing mode keeps its phase through a blow. */
    ep_bank_init(&c, (float)EP_RATE);
    ep_bank_strike(&c, 64, 100);
    for (int i = 0; i < 200; i++) ep_bank_tick_gated(&c);
    float kept = c.phase[0][36];
    CHECK(kept != 0.0f, "the phase should have advanced");
    ep_bank_strike(&c, 64, 100);
    CHECK(c.phase[0][36] == kept, "a blow must leave a ringing mode's phase alone");
}

/* --- EP3: the contact transient (sec 5.5) --- */

static void test_ep_hammer(void) {
    ep_bank b;
    ep_bank_init(&b, (float)EP_RATE);
    CHECK(b.hn_amp[36] == 0.0f, "a bank at rest carries no transient");

    /* sec 5.5: what comes out rises as the square root of the strike level,
     * so a soft blow is proportionally more knock than tone. Measured on
     * the emitted burst, not on hn_amp: that field also carries the filter
     * compensation, whose corner moves with velocity too. Averaged over
     * several notes because a short burst is a short noise record. */
    double e32 = 0.0, e127 = 0.0;
    for (int note = 40; note <= 76; note += 6) {
        int key = note - EP_NOTE_MIN;
        for (int pass = 0; pass < 2; pass++) {
            ep_bank_init(&b, (float)EP_RATE);
            ep_bank_strike(&b, note, pass ? 127 : 32);
            for (int m = 0; m < EP_MODES; m++) b.amp[m][key] = 0.0f;
            double sq = 0.0;
            for (int i = 0; i < 1920; i++) {
                float x = ep_bank_tick(&b);
                sq += (double)x * x;
            }
            if (pass) e127 += sq; else e32 += sq;
        }
    }
    double got = sqrt(e127 / e32), want = sqrt(127.0 / 32.0);
    CHECK(fabs(got / want - 1.0) < 0.10,
          "the burst must scale as sqrt(velocity), got %.3f vs %.3f", got, want);

    /* Its bandwidth is its own, not the mode roll-off's: reusing that
     * corner once put a filter slower than the burst in front of it. */
    int wide = 0;
    for (int z = 0; z < EP_ZONES; z++)
        if (EP_HAMMER_HZ[z] > 4.0f * EP_CORNER_HZ[z]) wide++;
    CHECK(wide == EP_ZONES,
          "the burst corner must sit well above the mode corner, %d of %d do",
          wide, EP_ZONES);

    /* It reaches exact silence on its pinned t60, like every other
     * envelope here. */
    ep_bank_init(&b, (float)EP_RATE);
    ep_bank_strike(&b, 64, 127);
    for (int m = 0; m < EP_MODES; m++) b.amp[m][36] = 0.0f;
    float h0 = b.hn_amp[36];
    long n = 0;
    while (b.hn_amp[36] > h0 * 1e-3f && n < (long)EP_RATE) {
        ep_bank_tick(&b);
        n++;
    }
    double meas = 1000.0 * n / EP_RATE;
    CHECK(fabs(meas / EP_HAMMER_MS[ep_zone(64)] - 1.0) < 0.02,
          "burst t60 %.2f ms must match the pinned %.2f ms", meas,
          (double)EP_HAMMER_MS[ep_zone(64)]);
    while (b.hn_amp[36] > 0.0f && n < 10L * (long)EP_RATE) {
        ep_bank_tick(&b);
        n++;
    }
    CHECK(b.hn_amp[36] == 0.0f, "the burst must snap to exact zero");

    /* Deterministic from the note and the strike ordinal alone. */
    ep_bank x, y;
    ep_bank_init(&x, (float)EP_RATE);
    ep_bank_init(&y, (float)EP_RATE);
    ep_bank_strike(&x, 64, 100);
    ep_bank_strike(&y, 64, 100);
    int diff = 0;
    for (int i = 0; i < 4800; i++) {
        float a = ep_bank_tick(&x), c = ep_bank_tick(&y);
        if (memcmp(&a, &c, sizeof a) != 0) diff++;
    }
    CHECK(diff == 0, "two fresh banks must render the same burst, %d differ", diff);

    /* But successive blows on one note must not repeat the same burst, or
     * a trill would buzz with one waveform. */
    ep_bank_init(&x, (float)EP_RATE);
    ep_bank_strike(&x, 64, 100);
    float first[64];
    for (int i = 0; i < 64; i++) first[i] = ep_bank_tick(&x);
    ep_bank_strike(&x, 64, 100);
    int same = 1;
    for (int i = 0; i < 64; i++)
        if (ep_bank_tick(&x) != first[i]) same = 0;
    CHECK(!same, "a second blow must not replay the first blow's burst");

    /* The layout identity has to survive a strike onto a voice that went
     * fully silent, which is where the burst's seed and filter state could
     * have drifted between the two ticks. */
    ep_bank adv, gat;
    ep_bank_init(&adv, (float)EP_RATE);
    ep_bank_init(&gat, (float)EP_RATE);
    int bad = 0;
    for (long i = 0; i < 3L * (long)EP_RATE; i++) {
        if (i % 24000 == 0) {
            ep_bank_strike(&adv, 100, 90);
            ep_bank_strike(&gat, 100, 90);
        }
        float a = ep_bank_tick(&adv), g = ep_bank_tick_gated(&gat);
        if (memcmp(&a, &g, sizeof a) != 0) bad++;
    }
    CHECK(bad == 0,
          "layouts must stay identical across silence and restrike, %d differ",
          bad);

    ep_bank_silence(&adv);
    CHECK(adv.hn_amp[72] == 0.0f && adv.hn_lp[72] == 0.0f,
          "silence must clear the transient and its filter");
}

/* --- EP4: the tremolo (sec 12) --- */

static void test_ep_tremolo(void) {
    ep_tremolo t;
    ep_tremolo_init(&t, (float)EP_RATE);
    CHECK(t.depth == 0.0f && t.mode == EP_TREM_AM,
          "the tremolo must start off and mono");

    /* Depth 0 is a bit-exact bypass, so every pre-EP4 render stays pinned. */
    int bad = 0;
    for (int i = 0; i < 4800; i++) {
        float x = (float)sin(i * 0.01);
        tw_stereo y = ep_tremolo_tick(&t, x);
        if (memcmp(&y.l, &x, sizeof x) != 0 || memcmp(&y.r, &x, sizeof x) != 0)
            bad++;
    }
    CHECK(bad == 0, "depth 0 must pass the input bit-identically, %d differ", bad);

    /* The same, through the instrument: the stereo tick must duplicate the
     * mono one exactly while the tremolo is off. */
    ep_piano a, b;
    ep_piano_init(&a, (float)EP_RATE);
    ep_piano_init(&b, (float)EP_RATE);
    ep_piano_note(&a, 64, true, 100);
    ep_piano_note(&b, 64, true, 100);
    int split = 0;
    for (int i = 0; i < 9600; i++) {
        float m = ep_piano_tick(&a);
        tw_stereo st = ep_piano_tick_stereo(&b);
        if (memcmp(&st.l, &m, sizeof m) != 0 || memcmp(&st.r, &m, sizeof m) != 0)
            split++;
    }
    CHECK(split == 0, "the stereo tick must duplicate the mono one at depth 0,"
          " %d differ", split);

    /* Mono variant moves both channels together; the pan variant opposes. */
    ep_tremolo_init(&t, (float)EP_RATE);
    ep_tremolo_set_depth(&t, 1.0f);
    ep_tremolo_set_mode(&t, EP_TREM_AM);
    int uneven = 0;
    double lo = 1e9, hi = -1e9;
    for (int i = 0; i < 48000; i++) {
        tw_stereo y = ep_tremolo_tick(&t, 1.0f);
        if (y.l != y.r) uneven++;
        if (y.l < lo) lo = y.l;
        if (y.l > hi) hi = y.l;
    }
    CHECK(uneven == 0, "the mono variant must move both channels together");
    CHECK(fabs(hi - 1.0) < 1e-3 && fabs(lo) < 1e-3,
          "depth 1 must sweep the gain over the whole range, got %.4f..%.4f",
          lo, hi);

    ep_tremolo_set_mode(&t, EP_TREM_PAN);
    double worst = 0.0;
    for (int i = 0; i < 48000; i++) {
        tw_stereo y = ep_tremolo_tick(&t, 1.0f);
        double sum = (double)y.l + y.r;
        if (fabs(sum - 1.0) > worst) worst = fabs(sum - 1.0);
    }
    CHECK(worst < 1e-5,
          "the pan variant must trade between channels, worst sum error %.2e",
          worst);

    /* The rate, measured off the LFO, and its clamps. */
    ep_tremolo_init(&t, (float)EP_RATE);
    ep_tremolo_set_depth(&t, 1.0f);
    ep_tremolo_set_rate(&t, (float)EP_RATE, 6.0f);
    int zc = 0;
    float prev = 0.0f;
    for (int i = 0; i < EP_RATE; i++) {
        float g = ep_tremolo_tick(&t, 1.0f).l - 0.5f;
        if (prev <= 0.0f && g > 0.0f) zc++;
        prev = g;
    }
    CHECK(zc == 6, "a 6 Hz tremolo must cross zero upward six times, got %d", zc);

    ep_tremolo_set_rate(&t, (float)EP_RATE, 99.0f);
    float fast = t.step;
    ep_tremolo_set_rate(&t, (float)EP_RATE, 10.0f);
    CHECK(fast == t.step, "the rate must clamp at the top of its range");
    ep_tremolo_set_rate(&t, (float)EP_RATE, -1.0f);
    float slow = t.step;
    ep_tremolo_set_rate(&t, (float)EP_RATE, 1.0f);
    CHECK(slow == t.step, "hostile rates must clamp to the bottom");

    /* One control covering off, the mono wobble and the stereo pan. */
    ep_tremolo_set_cc(&t, 0);
    CHECK(t.depth == 0.0f, "CC 0 must be off");
    ep_tremolo_set_cc(&t, 63);
    CHECK(t.mode == EP_TREM_AM && fabs(t.depth - 1.0) < 1e-6,
          "CC 63 must be the mono wobble at full depth");
    ep_tremolo_set_cc(&t, 64);
    CHECK(t.mode == EP_TREM_PAN && t.depth > 0.0f && t.depth < 0.05f,
          "CC 64 must cross into the pan variant at low depth");
    ep_tremolo_set_cc(&t, 127);
    CHECK(t.mode == EP_TREM_PAN && fabs(t.depth - 1.0) < 1e-6,
          "CC 127 must be the pan variant at full depth");
    ep_tremolo_set_cc(&t, 999);
    CHECK(fabs(t.depth - 1.0) < 1e-6, "hostile CC values must clamp");
    ep_tremolo_set_depth(&t, -1.0f);
    CHECK(t.depth == 0.0f, "hostile depth must sanitize to 0");
}

/* --- EP5: the preamp drive (sec 13) --- */

static void test_ep_drive(void) {
    /* Drive 0 is an exact bypass through the whole instrument, so every
     * pre-EP5 render stays pinned. The scale in and out of tw_drive is a
     * power of two precisely so this holds. */
    ep_piano a, b;
    ep_piano_init(&a, (float)EP_RATE);
    ep_piano_init(&b, (float)EP_RATE);
    ep_piano_set_drive(&b, 0.0f);
    ep_piano_note(&a, 52, true, 100);
    ep_piano_note(&b, 52, true, 100);
    ep_piano_note(&a, 64, true, 120);
    ep_piano_note(&b, 64, true, 120);
    int bad = 0;
    for (long i = 0; i < 2L * (long)EP_RATE; i++) {
        float x = ep_piano_tick(&a), y = ep_piano_tick(&b);
        if (memcmp(&x, &y, sizeof x) != 0) bad++;
    }
    CHECK(bad == 0, "drive 0 must be a bit-exact bypass, %d samples differ", bad);

    CHECK(EP_DRIVE_SCALE == 2.0f, "the drive scale must stay a power of two");
    CHECK(a.drive.odd, "sec 13: the EP uses the odd kernel, not the triode curve");

    /* The point of the stage: it fills the signal in. Crest factor has to
     * fall monotonically as the knob opens — that is the measurable thing
     * the reference library's own crest of about 5 asks for, against the
     * bare voice bank's 8-plus. */
    double prev = 1e9;
    int rising = 0;
    double crest_at_eighth = 0.0;
    for (int step = 0; step <= 4; step++) {
        float d = (float)step * 0.125f;
        ep_piano p;
        ep_piano_init(&p, (float)EP_RATE);
        ep_piano_set_drive(&p, d);
        ep_piano_set_sustain(&p, true);
        static const int ch[4] = { 40, 52, 59, 64 };
        for (int i = 0; i < 4; i++) ep_piano_note(&p, ch[i], true, 100);
        double sq = 0.0, pk = 0.0;
        long n = 2L * (long)EP_RATE;
        for (long i = 0; i < n; i++) {
            double x = ep_piano_tick(&p);
            sq += x * x;
            if (fabs(x) > pk) pk = fabs(x);
        }
        double crest = pk / sqrt(sq / n);
        if (crest >= prev) rising++;
        prev = crest;
        if (step == 1) crest_at_eighth = crest;
    }
    CHECK(rising == 0, "crest factor must fall as the drive opens, %d steps did not",
          rising);
    CHECK(crest_at_eighth < 7.0,
          "a light drive should already pull the crest under 7, got %.2f",
          crest_at_eighth);

    /* Hostile values sanitize, through the organ's own contract. */
    ep_piano c;
    ep_piano_init(&c, (float)EP_RATE);
    ep_piano_set_drive(&c, -1.0f);
    CHECK(c.drive.drive == 0.0f, "a negative drive must sanitize to 0");
    ep_piano_set_drive(&c, 9.0f);
    CHECK(c.drive.drive == 1.0f, "an excessive drive must clamp to 1");
}

/* --- EP6: the cabinet's bandwidth (sec 14) --- */

static void test_ep_cabinet(void) {
    ep_cabinet c;
    ep_cabinet_init(&c, (float)EP_RATE);
    CHECK(c.mix == 0.0f, "the cabinet must start bypassed");

    /* Bypass is bit-exact even though the filters keep running. */
    int bad = 0;
    for (int i = 0; i < 4800; i++) {
        float v = (float)sin(i * 0.03) * 0.7f;
        tw_stereo y = ep_cabinet_tick(&c, (tw_stereo){ v, -v });
        if (memcmp(&y.l, &v, sizeof v) != 0) bad++;
        float nv = -v;
        if (memcmp(&y.r, &nv, sizeof nv) != 0) bad++;
    }
    CHECK(bad == 0, "cabinet at 0 must pass both channels untouched, %d differ", bad);

    /* Fully engaged, it is a bandpass: a tone in the passband survives, one
     * well above the cone's corner and one well below the box's do not. */
    ep_cabinet_set(&c, 1.0f);
    double keep[3] = { 0, 0, 0 };
    static const double F[3] = { 40.0, 700.0, 12000.0 };
    for (int t = 0; t < 3; t++) {
        ep_cabinet_init(&c, (float)EP_RATE);
        ep_cabinet_set(&c, 1.0f);
        double sq = 0.0;
        for (int i = 0; i < EP_RATE; i++) {
            float v = (float)sin(TAU_D * F[t] * i / EP_RATE);
            tw_stereo y = ep_cabinet_tick(&c, (tw_stereo){ v, v });
            if (i > EP_RATE / 2) sq += (double)y.l * y.l;
        }
        keep[t] = sqrt(sq / (EP_RATE / 2)) / sqrt(0.5);
    }
    CHECK(keep[1] > 0.7, "700 Hz must pass the cabinet, kept %.3f", keep[1]);
    CHECK(keep[0] < 0.6 * keep[1],
          "40 Hz must be cut by the box, kept %.3f against %.3f", keep[0], keep[1]);
    CHECK(keep[2] < 0.15 * keep[1],
          "12 kHz must be cut by the cone, kept %.3f against %.3f", keep[2], keep[1]);

    /* Hostile values, and the whole-instrument bypass. */
    ep_cabinet_set(&c, -3.0f);
    CHECK(c.mix == 0.0f, "a negative cabinet must sanitize to 0");
    ep_cabinet_set(&c, 5.0f);
    CHECK(c.mix == 1.0f, "an excessive cabinet must clamp to 1");

    ep_piano a, b;
    ep_piano_init(&a, (float)EP_RATE);
    ep_piano_init(&b, (float)EP_RATE);
    ep_piano_set_cabinet(&b, 0.0f);
    ep_piano_note(&a, 55, true, 110);
    ep_piano_note(&b, 55, true, 110);
    int split = 0;
    for (long i = 0; i < (long)EP_RATE; i++) {
        float m = ep_piano_tick(&a);
        tw_stereo st = ep_piano_tick_stereo(&b);
        if (memcmp(&st.l, &m, sizeof m) != 0) split++;
    }
    CHECK(split == 0,
          "with tremolo and cabinet at zero the stereo tick must still equal"
          " the mono one, %d differ", split);
}

/* --- EP7: condition, the per-note deviations behind one knob (sec 15) --- */

static void test_ep_condition(void) {
    ep_bank b;
    ep_bank_init(&b, (float)EP_RATE);
    CHECK(b.cond == 0.0f, "a bare bank starts idealized");

    /* Every deviation is exactly neutral at condition 0. */
    int off = 0;
    for (int k = 0; k < EP_KEYS; k++) {
        if (b.f1[k] != ep_key_freq_hz(k)) off++;
        for (int m = 0; m < EP_MODES; m++)
            if (b.trim[m][k] != 1.0f) off++;
    }
    CHECK(off == 0 && b.hum_gain == 0.0f && b.floor_gain == 0.0f,
          "condition 0 must leave every deviation exactly neutral, %d off", off);

    /* And a render at condition 0 is bit-identical to one that never knew
     * about condition at all — the scanner-OFF discipline. */
    ep_bank a, c;
    ep_bank_init(&a, (float)EP_RATE);
    ep_bank_init(&c, (float)EP_RATE);
    ep_bank_set_condition(&c, 0.6f);
    ep_bank_set_condition(&c, 0.0f);
    ep_bank_strike(&a, 55, 100);
    ep_bank_strike(&c, 55, 100);
    int bad = 0;
    for (long i = 0; i < (long)EP_RATE; i++) {
        float x = ep_bank_tick_gated(&a), y = ep_bank_tick_gated(&c);
        if (memcmp(&x, &y, sizeof x) != 0) bad++;
    }
    CHECK(bad == 0, "a return to condition 0 must rejoin the idealized render,"
          " %d differ", bad);

    /* Turned up, every note moves, and no two notes move alike. */
    ep_bank_init(&b, (float)EP_RATE);
    ep_bank_set_condition(&b, 1.0f);
    int same = 0, wild = 0;
    double worst_cents = 0.0;
    for (int k = 0; k < EP_KEYS; k++) {
        double cents = 1200.0 * log2(b.f1[k] / ep_key_freq_hz(k));
        if (fabs(cents) > worst_cents) worst_cents = fabs(cents);
        if (b.f1[k] == ep_key_freq_hz(k)) same++;
        if (fabs(cents) > 7.0) wild++;
    }
    CHECK(same == 0, "at condition 1 no note should sit exactly on its ideal");
    CHECK(wild == 0 && worst_cents > 3.0,
          "tuning spread must stay inside its pinned band, worst %.2f cents",
          worst_cents);

    /* The pickup and the voicing spread too, and the hum and floor arrive. */
    int pk_same = 0;
    for (int k = 0; k < EP_KEYS; k++)
        if (b.pk_g[k] == ep_pickup_drive(k)
            && b.pk_x0[k] == EP_PICKUP_OFFSET) pk_same++;
    CHECK(pk_same == 0, "the pickup curve must vary per note at condition 1");
    CHECK(fabs(EP_PICKUP_OFFSET - EPO_PICKUP_OFFSET) < 1e-6,
          "the pinned off-centre offset must match the doc");
    CHECK(b.hum_gain > 0.0f && b.floor_gain > 0.0f,
          "the hum and noise floors must arrive with condition");

    /* The floor is bank-level, so it must not break the layout identity
     * that decision D4 rests on. */
    ep_bank adv, gat;
    ep_bank_init(&adv, (float)EP_RATE);
    ep_bank_init(&gat, (float)EP_RATE);
    ep_bank_set_condition(&adv, 0.7f);
    ep_bank_set_condition(&gat, 0.7f);
    int split = 0;
    for (long i = 0; i < (long)EP_RATE; i++) {
        if (i % 9000 == 0) {
            ep_bank_strike(&adv, 45 + (int)(i / 9000) * 5, 90);
            ep_bank_strike(&gat, 45 + (int)(i / 9000) * 5, 90);
        }
        float x = ep_bank_tick(&adv), y = ep_bank_tick_gated(&gat);
        if (memcmp(&x, &y, sizeof x) != 0) split++;
    }
    CHECK(split == 0, "condition must not break the layout identity, %d differ",
          split);

    /* Deterministic: the draws come from a fixed seed, so two banks at the
     * same condition are the same instrument. */
    ep_bank d, e;
    ep_bank_init(&d, (float)EP_RATE);
    ep_bank_init(&e, (float)EP_RATE);
    ep_bank_set_condition(&d, 0.35f);
    ep_bank_set_condition(&e, 0.35f);
    int drift = 0;
    for (int k = 0; k < EP_KEYS; k++)
        if (d.f1[k] != e.f1[k] || d.pk_g[k] != e.pk_g[k]) drift++;
    CHECK(drift == 0, "the condition draws must be reproducible, %d differ", drift);

    /* sec 16: the horizontal polarisation. Absent at condition 0, present
     * and per-note above it, and it modulates rather than adding a pitch. */
    ep_bank pb;
    ep_bank_init(&pb, (float)EP_RATE);
    int pol_on = 0, unsplit0 = 0;
    for (int k = 0; k < EP_KEYS; k++) {
        if (pb.h_depth[k] != 0.0f) pol_on++;
        /* the twin exists but is not split, so it can carry no beat; the
         * depth being zero is what makes the stage vanish */
        if (pb.h_step[k] != pb.step[0][k]) unsplit0++;
    }
    CHECK(pol_on == 0 && unsplit0 == 0,
          "at condition 0 the second polarisation must carry no depth (%d) and"
          " no split (%d)", pol_on, unsplit0);
    ep_bank_strike(&pb, 60, 100);
    CHECK(pb.h_amp[32] == 0.0f,
          "a strike at condition 0 must put nothing into the second plane");

    ep_bank_set_condition(&pb, 1.0f);
    int flat = 0, unsplit = 0;
    for (int k = 0; k < EP_KEYS; k++) {
        if (pb.h_depth[k] <= 0.0f) flat++;
        double split = fabs(pb.h_step[k] * EP_RATE / pb.f1[k] - 1.0);
        if (split < 1e-6 || split > 0.005) unsplit++;
    }
    CHECK(flat <= 2 && unsplit == 0,
          "at condition 1 every note must carry a split twin (%d flat, %d off)",
          flat, unsplit);

    /* The twin is a modulation, not a partial: it must never make the voice
     * louder than the ceiling the modes already bound. */
    ep_bank_strike(&pb, 64, 127);
    CHECK(pb.h_amp[36] <= pb.h_depth[36] + 1e-7f,
          "the polarisation index must stay inside its own depth");
    for (int i = 0; i < 200; i++) ep_bank_strike(&pb, 64, 127);
    CHECK(pb.h_amp[36] <= pb.h_depth[36] + 1e-7f,
          "stacked blows must not run the polarisation away");

    /* Hostile values, and the shipped default. */
    ep_bank_set_condition(&d, -2.0f);
    CHECK(d.cond == 0.0f, "a negative condition must sanitize to 0");
    ep_bank_set_condition(&d, 7.0f);
    CHECK(d.cond == 1.0f, "an excessive condition must clamp to 1");
    ep_piano pn;
    ep_piano_init(&pn, (float)EP_RATE);
    CHECK(pn.bank.cond == EP_CONDITION_DEFAULT,
          "the instrument must ship with tolerance, not idealized");
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
    test_key_depth();
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
    test_ep_note_map();
    test_ep_modes();
    test_ep_decay();
    test_ep_strike();
    test_ep_pickup();
    test_ep_bank();
    test_ep_damper();
    test_ep_piano_keys();
    test_ep_restrike();
    test_ep_hammer();
    test_ep_tremolo();
    test_ep_drive();
    test_ep_cabinet();
    test_ep_condition();
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
