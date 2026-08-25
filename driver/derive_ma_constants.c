/* Hosted MA0 derivation and numeric audit. This program is development
 * tooling, not a core dependency: review its output, then pin the rounded
 * constants in docs/ma-constants.md and the owning ma_ translation unit. */
#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

enum {
    MIDI_VALUES = 128,
    PREWARP_ORDER = 6,
    PREWARP_POINTS = PREWARP_ORDER + 1,
    OSC_HALFBAND_TAPS = 31,
};

static const double PI_D = 3.14159265358979323846264338327950288;
static const double LN2_D = 0.693147180559945309417232121458176568;
static const double PREWARP_EDGE = 0.42;
static const double HALFBAND_PASS_EDGE = 0.25;

static const uint32_t MOZAIK_SLOPE[5] = {
    0x80000000u, 0x9999999au, 0x9e3779b9u, 0xa0000000u, 0xaaaaaaabu,
};
static const uint32_t MOZAIK_PHASON[3] = {
    0x00000000u, 0x12345678u, 0xdeadbeefu,
};
static const uint64_t MOZAIK_KINDS_64[5][3] = {
    { 0xaaaaaaaaaaaaaaaau, 0xaaaaaaaaaaaaaaaau, 0x5555555555555555u },
    { 0xad6b5ad6b5ad6b5au, 0xad6b5ad6b5ad6b5au, 0xb5ad6b5ad6b5ad6bu },
    { 0xadad6d6b6b6b5b5au, 0x6d6d6b6b6b5b5adau, 0xb5adadad6d6d6b6bu },
    { 0xdadadadadadadadau, 0xdadadadadadadadau, 0x6b6b6b6b6b6b6b6bu },
    { 0x6db6db6db6db6db6u, 0x6db6db6db6db6db6u, 0xb6db6db6db6db6dbu },
};
static const uint32_t MOZAIK_FRAC_64[5][3] = {
    { 0x00000000u, 0x12345678u, 0xdeadbeefu },
    { 0x66666680u, 0x789abcf8u, 0x4514256fu },
    { 0x8dde6e40u, 0xa012c4b8u, 0x6c8c2d2fu },
    { 0x00000000u, 0x12345678u, 0xdeadbeefu },
    { 0xaaaaaac0u, 0xbcdf0138u, 0x895869afu },
};

static void print_table(const char *name, const float *values, size_t count) {
    printf("static constexpr float %s[%zu] = {\n", name, count);
    for (size_t i = 0; i < count; i++) {
        if (i % 8 == 0) fputs("    ", stdout);
        printf("%.9ef", (double)values[i]);
        if (i + 1 != count) fputs(", ", stdout);
        if (i % 8 == 7 || i + 1 == count) putchar('\n');
    }
    fputs("};\n\n", stdout);
}

static double prewarp_target(double x) {
    if (x == 0.0) return 0.0;
    double g = tan(PI_D * x);
    return g / (1.0 + g);
}

/* Approximate G/x rather than G. Multiplication by x then gives G(0) = 0
 * exactly and keeps relative cutoff error controlled at the low end. */
static void derive_prewarp(float coefficients[PREWARP_POINTS]) {
    for (int k = 0; k <= PREWARP_ORDER; k++) {
        double sum = 0.0;
        for (int j = 0; j <= PREWARP_ORDER; j++) {
            double theta = PI_D * ((double)j + 0.5) / PREWARP_POINTS;
            double z = cos(theta);
            double x = 0.5 * PREWARP_EDGE * (z + 1.0);
            double h = prewarp_target(x) / x;
            sum += h * cos((double)k * theta);
        }
        double c = 2.0 * sum / PREWARP_POINTS;
        coefficients[k] = (float)(k == 0 ? 0.5 * c : c);
    }
}

static float prewarp_approx(float x, const float c[PREWARP_POINTS]) {
    float z = 2.0f * x * (1.0f / (float)PREWARP_EDGE) - 1.0f;
    float b1 = 0.0f, b2 = 0.0f;
    for (int k = PREWARP_ORDER; k >= 1; k--) {
        float b = 2.0f * z * b1 - b2 + c[k];
        b2 = b1;
        b1 = b;
    }
    return x * (z * b1 - b2 + c[0]);
}

static double halfband_response(double a, double omega) {
    double b = 0.25 - a;
    return 0.5 + 2.0 * b * cos(omega) + 2.0 * a * cos(3.0 * omega);
}

static double halfband_error(double a) {
    double worst = 0.0;
    for (int i = 0; i <= 8192; i++) {
        double omega = HALFBAND_PASS_EDGE * PI_D * (double)i / 8192.0;
        double error = fabs(halfband_response(a, omega) - 1.0);
        if (error > worst) worst = error;
    }
    return worst;
}

/* A seven-tap exact halfband has form [a,0,b,.5,b,0,a], a+b=.25.
 * Search its only free coefficient for minimum worst passband error. */
static float derive_halfband_side(void) {
    double lo = -0.125, hi = 0.0;
    for (int pass = 0; pass < 5; pass++) {
        double best_a = lo, best_error = DBL_MAX;
        for (int i = 0; i <= 4096; i++) {
            double a = lo + (hi - lo) * (double)i / 4096.0;
            double error = halfband_error(a);
            if (error < best_error) {
                best_a = a;
                best_error = error;
            }
        }
        double radius = (hi - lo) / 4096.0;
        lo = best_a - radius;
        hi = best_a + radius;
    }
    return (float)((lo + hi) * 0.5);
}

/* A longer Blackman-windowed exact halfband is reserved for the 4x VCO
 * edge boundary. Odd offsets carry the windowed ideal sinc, even offsets
 * are exact zero, and the sides are normalized around an exact .5 center. */
static void derive_oscillator_halfband(float taps[OSC_HALFBAND_TAPS]) {
    int middle = OSC_HALFBAND_TAPS / 2;
    double side_sum = 0.0;
    for (int i = 0; i < OSC_HALFBAND_TAPS; i++) {
        int offset = i - middle;
        if (offset == 0) {
            taps[i] = 0.5f;
        } else if ((offset & 1) == 0) {
            taps[i] = 0.0f;
        } else {
            double window = 0.42
                          - 0.5 * cos(2.0 * PI_D * i
                                      / (OSC_HALFBAND_TAPS - 1))
                          + 0.08 * cos(4.0 * PI_D * i
                                       / (OSC_HALFBAND_TAPS - 1));
            taps[i] = (float)(sin(0.5 * PI_D * offset)
                              / (PI_D * offset) * window);
            if (i == 0 || i + 1 == OSC_HALFBAND_TAPS) taps[i] = 0.0f;
            side_sum += taps[i];
        }
    }
    double scale = 0.5 / side_sum;
    for (int i = 0; i < OSC_HALFBAND_TAPS; i++) {
        if (i != middle) taps[i] = (float)(taps[i] * scale);
    }
}

static double fir_response(const float *taps, int count, double omega) {
    double response = 0.0;
    int middle = count / 2;
    for (int i = 0; i < count; i++)
        response += taps[i] * cos(omega * (i - middle));
    return response;
}

static float exp2_small(float x, const float c[6]) {
    float y = c[5];
    for (int i = 4; i >= 0; i--) y = y * x + c[i];
    return y;
}

static uint64_t mozaik_kinds_64(uint32_t slope, uint32_t phason) {
    uint64_t kinds = 0;
    for (uint64_t i = 0; i < 64; i++) {
        uint64_t previous = phason + i * slope;
        uint64_t next = previous + slope;
        if ((next >> 32) > (previous >> 32)) kinds |= UINT64_C(1) << i;
    }
    return kinds;
}

static uint32_t mozaik_frac_after(uint32_t slope, uint32_t phason,
                                  uint32_t tiles) {
    return phason + slope * tiles;
}

static uint32_t mozaik_card_phason(uint64_t seed, uint64_t card) {
    uint64_t state = seed
                   ^ (card * UINT64_C(0x9e3779b97f4a7c15)
                      + UINT64_C(0x9e3779b97f4a7c15));
    state = (state ^ state >> 30) * UINT64_C(0xbf58476d1ce4e5b9);
    state = (state ^ state >> 27) * UINT64_C(0x94d049bb133111eb);
    state ^= state >> 31;
    return (uint32_t)(state >> 32);
}

int main(void) {
    float note_hz[MIDI_VALUES];
    float velocity_level[MIDI_VALUES];
    float velocity_filter[MIDI_VALUES];
    float cutoff_hz[MIDI_VALUES];
    float time_ms[MIDI_VALUES];
    float lfo_hz[MIDI_VALUES];

    for (int i = 0; i < MIDI_VALUES; i++) {
        double u = (double)i / 127.0;
        note_hz[i] = (float)(440.0 * pow(2.0, ((double)i - 69.0) / 12.0));
        velocity_level[i] = i == 0 ? 0.0f : (float)pow(u, 0.78);
        velocity_filter[i] = i == 0 ? 0.0f : (float)pow(u, 1.08);
        cutoff_hz[i] = (float)(20.0 * pow(1000.0, u));
        time_ms[i] = (float)pow(20000.0, u);
        lfo_hz[i] = (float)(0.03 * pow(20.0 / 0.03, u));
    }

    float prewarp[PREWARP_POINTS];
    derive_prewarp(prewarp);

    double max_g_abs = 0.0, max_g_rel = 0.0, max_cutoff_rel = 0.0;
    double max_g_at = 0.0, max_cutoff_at = 0.0;
    for (int i = 1; i <= 1000000; i++) {
        double x = PREWARP_EDGE * (double)i / 1000000.0;
        double exact = prewarp_target(x);
        double approx = prewarp_approx((float)x, prewarp);
        double absolute = fabs(approx - exact);
        double relative = absolute / exact;
        double effective_x = atan(approx / (1.0 - approx)) / PI_D;
        double cutoff_relative = fabs(effective_x / x - 1.0);
        if (absolute > max_g_abs) max_g_abs = absolute;
        if (relative > max_g_rel) {
            max_g_rel = relative;
            max_g_at = x;
        }
        if (cutoff_relative > max_cutoff_rel) {
            max_cutoff_rel = cutoff_relative;
            max_cutoff_at = x;
        }
    }

    float halfband_a = derive_halfband_side();
    float halfband_b = 0.25f - halfband_a;
    float halfband[7] = {
        halfband_a, 0.0f, halfband_b, 0.5f,
        halfband_b, 0.0f, halfband_a,
    };
    float oscillator_halfband[OSC_HALFBAND_TAPS];
    derive_oscillator_halfband(oscillator_halfband);
    double pass_error = 0.0, stop_peak = 0.0;
    double oscillator_pass_error = 0.0, oscillator_stop_peak = 0.0;
    for (int i = 0; i <= 1000000; i++) {
        double omega = PI_D * (double)i / 1000000.0;
        double response = halfband_response(halfband_a, omega);
        if (omega <= HALFBAND_PASS_EDGE * PI_D) {
            double error = fabs(response - 1.0);
            if (error > pass_error) pass_error = error;
        }
        if (omega >= (1.0 - HALFBAND_PASS_EDGE) * PI_D) {
            double magnitude = fabs(response);
            if (magnitude > stop_peak) stop_peak = magnitude;
        }
        double oscillator_response = fir_response(
            oscillator_halfband, OSC_HALFBAND_TAPS, omega);
        if (omega <= HALFBAND_PASS_EDGE * PI_D) {
            double error = fabs(oscillator_response - 1.0);
            if (error > oscillator_pass_error)
                oscillator_pass_error = error;
        }
        if (omega >= (1.0 - HALFBAND_PASS_EDGE) * PI_D) {
            double magnitude = fabs(oscillator_response);
            if (magnitude > oscillator_stop_peak)
                oscillator_stop_peak = magnitude;
        }
    }

    float exp2_coeff[6];
    double factorial = 1.0;
    for (int i = 0; i < 6; i++) {
        if (i > 0) factorial *= i;
        exp2_coeff[i] = (float)(pow(LN2_D, i) / factorial);
    }
    double exp2_relative = 0.0, exp2_cents = 0.0;
    for (int i = 0; i <= 1000000; i++) {
        float x = -0.25f + 0.5f * (float)i / 1000000.0f;
        double exact = pow(2.0, x);
        double relative = fabs((double)exp2_small(x, exp2_coeff) / exact - 1.0);
        if (relative > exp2_relative) exp2_relative = relative;
    }
    exp2_cents = 1200.0 * log2(1.0 + exp2_relative);

    bool mozaik_ok = true;
    for (size_t slope = 0; slope < 5; slope++) {
        for (size_t phason = 0; phason < 3; phason++) {
            mozaik_ok = mozaik_ok
                     && mozaik_kinds_64(MOZAIK_SLOPE[slope],
                                        MOZAIK_PHASON[phason])
                        == MOZAIK_KINDS_64[slope][phason]
                     && mozaik_frac_after(MOZAIK_SLOPE[slope],
                                          MOZAIK_PHASON[phason], 64)
                        == MOZAIK_FRAC_64[slope][phason];
        }
    }

    print_table("MA_NOTE_HZ", note_hz, MIDI_VALUES);
    print_table("MA_VELOCITY_LEVEL", velocity_level, MIDI_VALUES);
    print_table("MA_VELOCITY_FILTER", velocity_filter, MIDI_VALUES);
    print_table("MA_MIDI_CUTOFF_HZ", cutoff_hz, MIDI_VALUES);
    print_table("MA_MIDI_TIME_MS", time_ms, MIDI_VALUES);
    print_table("MA_MIDI_LFO_HZ", lfo_hz, MIDI_VALUES);
    print_table("MA_PREWARP_CHEB", prewarp, PREWARP_POINTS);
    print_table("MA_HALFBAND_7", halfband, 7);
    print_table("MA_OSC_HALFBAND_31", oscillator_halfband,
                OSC_HALFBAND_TAPS);
    print_table("MA_EXP2_SMALL", exp2_coeff, 6);

    puts("numeric audit:");
    printf("  note A4: %.9g Hz; endpoints %.9g .. %.9g Hz\n",
           (double)note_hz[69], (double)note_hz[0], (double)note_hz[127]);
    printf("  prewarp G: max_abs=%.9g max_rel=%.9g at x=%.9g\n",
           max_g_abs, max_g_rel, max_g_at);
    printf("  prewarp effective cutoff: max_rel=%.9g at x=%.9g\n",
           max_cutoff_rel, max_cutoff_at);
    printf("  halfband: pass_edge=%.3f*pi max_pass_error=%.9g "
           "stop_edge=%.3f*pi max_stop=%.9g\n",
           HALFBAND_PASS_EDGE, pass_error,
           1.0 - HALFBAND_PASS_EDGE, stop_peak);
    printf("  oscillator halfband: pass_edge=%.3f*pi max_pass_error=%.9g "
           "stop_edge=%.3f*pi max_stop=%.9g\n",
           HALFBAND_PASS_EDGE, oscillator_pass_error,
           1.0 - HALFBAND_PASS_EDGE, oscillator_stop_peak);
    printf("  exp2 polynomial [-.25,.25]: max_rel=%.9g (%.9g cents)\n",
           exp2_relative, exp2_cents);
    printf("  velocity endpoints: level %.9g filter %.9g\n",
           (double)velocity_level[127], (double)velocity_filter[127]);
    printf("  MIDI endpoints: cutoff %.9g..%.9g Hz; time %.9g..%.9g ms; "
           "LFO %.9g..%.9g Hz\n",
           (double)cutoff_hz[0], (double)cutoff_hz[127],
           (double)time_ms[0], (double)time_ms[127],
           (double)lfo_hz[0], (double)lfo_hz[127]);
    puts("  Mozaik Q32 vectors: 15 x 64 tile kinds and final fractions");
    fputs("  Mozaik card phasons:", stdout);
    for (uint64_t card = 0; card < 5; card++)
        printf(" 0x%08" PRIx32,
               mozaik_card_phason(UINT64_C(0x4d6f7a31), card));
    putchar('\n');

    bool ok = note_hz[69] == 440.0f
           && velocity_level[0] == 0.0f && velocity_level[127] == 1.0f
           && velocity_filter[0] == 0.0f && velocity_filter[127] == 1.0f
           && max_cutoff_rel < 1.0e-4
           && pass_error < 0.015 && stop_peak < 0.015
           && oscillator_pass_error < 2.0e-4
           && oscillator_stop_peak < 2.0e-4
           && exp2_cents < 0.001 && mozaik_ok;
    puts(ok ? "  verdict: PASS" : "  verdict: FAIL");
    return ok ? 0 : 1;
}
