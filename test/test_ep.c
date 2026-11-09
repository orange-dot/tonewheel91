/* Core checks for the electric piano (ep73, EP1..EP7). */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../src/tonewheel.h"
#include "../src/epiano.h"
#include "../src/mamutanalog.h"
#include "check.h"

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

    /* The explicit hard-silence operation clears resonator phases. */
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

    /* A configuration-time return to 0, before oscillators have advanced,
     * is bit-identical to a bank kept ideal — the scanner-OFF discipline. */
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

    /* Every named deviation gets a full 13-bit field of its own. In
     * particular the pickup offset used to read a 12-bit tail at shift 52,
     * which made every draw negative. */
    int pickup_pos = 0, pickup_neg = 0, split_pos = 0, split_neg = 0;
    int pickup_split_same = 0;
    for (int k = 0; k < EP_KEYS; k++) {
        double pickup = (b.pk_x0[k] / EP_PICKUP_OFFSET - 1.0) / 0.30;
        double polar = (b.h_step[k] * EP_RATE / b.f1[k] - 1.0)
                     / EP_POLAR_SPLIT;
        pickup_pos += pickup > 0.0;
        pickup_neg += pickup < 0.0;
        split_pos += polar > 0.0;
        split_neg += polar < 0.0;
        pickup_split_same += fabs(pickup - polar) < 1e-7;
    }
    CHECK(pickup_pos && pickup_neg && split_pos && split_neg,
          "independent 13-bit fields must span both signs (%d/%d, %d/%d)",
          pickup_pos, pickup_neg, split_pos, split_neg);
    CHECK(pickup_split_same < EP_KEYS,
          "pickup and polar deviations must not reuse one random field");

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

    /* sec 16: the horizontal polarisation. Unsensed at condition 0 and
     * present per note above it, where it adds a nearly equal pitch. */
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
    CHECK(pb.h_amp[32] > 0.0f && pb.h_depth[32] == 0.0f,
          "condition 0 must gate pickup depth, not erase physical excitation");

    ep_bank_set_condition(&pb, 1.0f);
    /* The ceiling is the constant itself, not a number chosen beside it: the
     * split is that scale times a draw in [-1, 1), so no note can reach it
     * and the bound stays exact through every re-pin of sec 16.3. Reading it
     * off the shipped value instead would have to be widened by hand each
     * time the rate moves, which is how a bound stops being a bound. */
    int flat = 0, unsplit = 0;
    for (int k = 0; k < EP_KEYS; k++) {
        if (pb.h_depth[k] <= 0.0f) flat++;
        double split = fabs(pb.h_step[k] * EP_RATE / pb.f1[k] - 1.0);
        if (split < 1e-6 || split > EP_POLAR_SPLIT) unsplit++;
    }
    CHECK(flat <= 2 && unsplit == 0,
          "at condition 1 every note must carry a split twin (%d flat, %d off)",
          flat, unsplit);

    /* The draw spans its scale rather than hugging one end of it, which is
     * what keeps the ceiling above from being vacuous: a bound nothing
     * approaches would pass on a bank whose splits were all near zero. It
     * does not catch a mis-typed constant — both sides move together — and
     * nothing in this suite does; that is the ballot's job, and the arms
     * are verified against the beat period they must carry before they are
     * rendered. */
    double smax = 0.0;
    for (int k = 0; k < EP_KEYS; k++) {
        double split = fabs(pb.h_step[k] * EP_RATE / pb.f1[k] - 1.0);
        if (split > smax) smax = split;
    }
    CHECK(smax > 0.9 * EP_POLAR_SPLIT,
          "the widest drawn split must approach its scale, %.5f of %.5f",
          smax, (double)EP_POLAR_SPLIT);

    /* And the whole stage rides condition linearly, so half the condition is
     * half of every split — the gate in sec 15, measured rather than assumed. */
    double full[EP_KEYS];
    for (int k = 0; k < EP_KEYS; k++)
        full[k] = fabs(pb.h_step[k] * EP_RATE / pb.f1[k] - 1.0);
    ep_bank_set_condition(&pb, 0.5f);
    int nonlin = 0;
    for (int k = 0; k < EP_KEYS; k++) {
        double half = fabs(pb.h_step[k] * EP_RATE / pb.f1[k] - 1.0);
        if (fabs(half * 2.0 - full[k]) > 1e-6) nonlin++;
    }
    CHECK(nonlin == 0,
          "the split must scale linearly with condition, %d do not", nonlin);
    ep_bank_set_condition(&pb, 1.0f);

    /* The twin is a share of the fundamental, not a partial of its own: it
     * must never exceed the share of the ceiling the modes already bound. */
    ep_bank_strike(&pb, 64, 127);
    float pcap = pb.ceiling[0][36];
    CHECK(pb.h_amp[36] <= pcap + 1e-7f,
          "the polarisation must stay inside its share of the ceiling");
    for (int i = 0; i < 200; i++) ep_bank_strike(&pb, 64, 127);
    CHECK(pb.h_amp[36] <= pcap + 1e-7f,
          "stacked blows must not run the polarisation away");

    /* A coefficient rebuild preserves the physical state already in flight:
     * held and sustained tines stay undamped, and condition 0 gates the
     * second plane without resetting phase or energy. */
    ep_bank live;
    ep_bank_init(&live, (float)EP_RATE);
    ep_bank_set_condition(&live, 1.0f);
    ep_bank_strike(&live, 64, 110);
    for (int i = 0; i < 100; i++) ep_bank_tick_gated(&live);
    int lk = 64 - EP_NOTE_MIN;
    float phase_before = live.h_phase[lk];
    float amp_before = live.h_amp[lk];
    ep_bank_set_condition(&live, 0.4f);
    CHECK(live.damper_up[lk] && live.dec[0][lk] == live.dec_free[0][lk]
          && live.h_dec[lk] == live.h_dec_free[lk],
          "condition rebuild must preserve a held tine's free decay");
    CHECK(live.h_phase[lk] == phase_before && live.h_amp[lk] == amp_before,
          "condition rebuild must preserve horizontal phase and energy");
    ep_bank_set_condition(&live, 0.0f);
    CHECK(live.h_depth[lk] == 0.0f && live.h_amp[lk] == amp_before,
          "condition 0 must gate, not clear, live horizontal energy");
    ep_bank gated = live;
    gated.h_amp[lk] = 0.0f;
    int leaked = 0;
    for (int i = 0; i < 100; i++) {
        float x = ep_bank_tick_gated(&live);
        float y = ep_bank_tick_gated(&gated);
        if (memcmp(&x, &y, sizeof x)) leaked++;
    }
    CHECK(leaked == 0,
          "condition-zero horizontal energy must be inaudible, %d samples differ",
          leaked);
    ep_bank_damp(&live, 64);
    ep_bank_set_condition(&live, 0.8f);
    CHECK(!live.damper_up[lk] && live.dec[0][lk] == live.dec_damp[0][lk],
          "condition rebuild must preserve a lowered damper");

    ep_piano sustained;
    ep_piano_init(&sustained, (float)EP_RATE);
    ep_piano_note(&sustained, 64, true, 100);
    ep_piano_set_sustain(&sustained, true);
    ep_piano_note(&sustained, 64, false, 0);
    ep_piano_set_condition(&sustained, 0.2f);
    CHECK(sustained.bank.damper_up[lk]
          && sustained.bank.dec[0][lk] == sustained.bank.dec_free[0][lk],
          "condition rebuild must preserve sustain-owned free decay");
    ep_piano_set_sustain(&sustained, false);
    CHECK(!sustained.bank.damper_up[lk],
          "sustain release must lower an unheld key's damper");

    /* sec 16.2: the fork cancels the base reaction in its own plane and
     * nowhere else, so the horizontal plane is the shorter-lived one at
     * every note — and the damper, which lands on the tine rather than on
     * one of its planes, is faster still, so damping never slows it. */
    ep_bank_init(&pb, (float)EP_RATE);
    ep_bank_set_condition(&pb, 1.0f);
    int slower = 0, damp_slower = 0;
    for (int k = 0; k < EP_KEYS; k++) {
        if (pb.h_dec_free[k] <= pb.dec_free[0][k]) slower++;
        if (pb.dec_damp[0][k] <= pb.h_dec_free[k]) damp_slower++;
    }
    CHECK(slower == 0 && damp_slower == 0,
          "the horizontal plane must decay faster than the fundamental (%d do"
          " not) and slower than the damper (%d do not)", slower, damp_slower);

    /* And it must reach the bus. The stage is only worth its cost if the
     * envelope actually moves, so this compares two renders of one note
     * that differ in nothing but the second plane's depth: two nearly-equal
     * frequencies summed at the sensor beat, and the beat is what a listener
     * hears as the note breathing. An earlier build gated the voice with the
     * twin instead of summing it, which measured correct as a modulation
     * index and put its whole contribution on the octave, where nothing in
     * the envelope moved at all (sec 16.1). */
    ep_bank wet, dry;
    ep_bank_init(&wet, (float)EP_RATE);
    ep_bank_init(&dry, (float)EP_RATE);
    ep_bank_set_condition(&wet, 1.0f);
    ep_bank_set_condition(&dry, 1.0f);
    wet.hum_gain = wet.floor_gain = 0.0f; /* a floor would blur the ratio */
    dry.hum_gain = dry.floor_gain = 0.0f;
    const int pk = 88 - EP_NOTE_MIN;
    /* The check pins its own depth rather than riding the shipped one: the
     * property under test is that the second plane reaches the bus as an
     * envelope at all, and that must not be re-derived every time the
     * calibration in sec 16.3 moves. */
    wet.h_depth[pk] = 0.5f;
    dry.h_depth[pk] = 0.0f;
    ep_bank_strike(&wet, 88, 110);
    ep_bank_strike(&dry, 88, 110);
    int win = 256, wins = 2 * EP_RATE / win;
    double rlo = 1e30, rhi = -1e30;
    for (int i = 0; i < wins; i++) {
        double sw = 0.0, sd = 0.0;
        for (int j = 0; j < win; j++) {
            double a = ep_bank_tick(&wet), c2 = ep_bank_tick(&dry);
            sw += a * a;
            sd += c2 * c2;
        }
        if (sd <= 1e-18) continue;
        double r = sqrt(sw / sd);
        if (r < rlo) rlo = r;
        if (r > rhi) rhi = r;
    }
    double swing = 20.0 * log10(rhi / rlo);
    CHECK(swing > 3.0,
          "the second polarisation must move the envelope at the bus, not"
          " only the index: %.2f dB over two seconds", swing);

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

void test_ep_suite(void) {
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
}
