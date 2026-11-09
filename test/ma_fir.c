/* White-box gate for the folded 31-tap halfband decimator (2026-11-09,
 * docs/ma3-fir-fold-20261109.md). The table must be symmetric with zero odd
 * taps, so that the seven pairs and the centre tap are the whole filter. At
 * every circular position the result must lie within 10 u sum |h_t x_t| of
 * the exact 31-tap sum (u = 2^-24): one rounding per pair sum and one per
 * fused step, eight steps. Precondition, as in the engine: |x| <= FLT_MAX/2,
 * so a pair sum cannot overflow (oscillator samples are clamped to 1.25). */
#include "../src/ma_voice_impl.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static double worst_ratio;
static unsigned checks;

static void check(const float history[31]) {
    float saved[31];
    memcpy(saved, history, sizeof saved);
    for (uint8_t position = 0; position < 31; position++) {
        double exact = 0.0, magnitude = 0.0;
        int index = position;
        for (int tap = 0; tap < 31; tap++) {
            if (--index < 0) index = 30;
            double term = (double)OSC_HALFBAND_31[tap] * (double)history[index];
            exact += term;
            magnitude += fabs(term);
        }
        double actual = ma_voice_decimate_oversample(history, position);
        double bound = 10.0 * 0x1p-24 * magnitude + 10.0 * FLT_TRUE_MIN;
        double error = fabs(actual - exact);
        assert(isfinite(actual) && error <= bound);
        if (error / bound > worst_ratio) worst_ratio = error / bound;
        assert(memcmp(saved, history, sizeof saved) == 0);
        checks++;
    }
}

int main(void) {
    for (int tap = 0; tap < 31; tap++) {
        assert(OSC_HALFBAND_31[tap] == OSC_HALFBAND_31[30 - tap]);
        if (tap % 2 && tap != OSC_HALFBAND_31_CENTRE) assert(OSC_HALFBAND_31[tap] == 0.0f);
    }
    assert(OSC_HALFBAND_31[0] == 0.0f && OSC_HALFBAND_31_CENTRE == 15);
    for (int k = 0; k < OSC_HALFBAND_31_PAIRS; k++) assert(OSC_HALFBAND_31_PAIR[k] == 2 + 2 * k);

    const float half_max = FLT_MAX / 2;
    const float values[] = {
        0.0f, -0.0f, 1.0f, -1.0f, 1.25f, -1.25f, FLT_TRUE_MIN, -FLT_TRUE_MIN,
        FLT_MIN, -FLT_MIN, half_max, -half_max, 0x1p-20f, -0x1p20f,
    };
    const unsigned count = sizeof values / sizeof *values;
    float history[31] = { 0 };
    for (unsigned v = 0; v < count; v++) {
        for (unsigned i = 0; i < 31; i++) history[i] = values[v];
        check(history);
        for (unsigned i = 0; i < 31; i++) history[i] = i % 2 ? -values[v] : values[v];
        check(history);
        for (unsigned at = 0; at < 31; at++) {
            for (unsigned i = 0; i < 31; i++) history[i] = 0.0f;
            history[at] = values[v];
            check(history);
        }
    }
    for (unsigned shift = 0; shift < count; shift++) {
        for (unsigned i = 0; i < 31; i++) history[i] = values[(i + shift) % count];
        check(history);
    }
    uint32_t state = UINT32_C(0x6d616669);
    for (unsigned pattern = 0; pattern < 4096; pattern++) {
        for (unsigned i = 0; i < 31; i++) {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            uint32_t bits = state;
            if (((bits >> 23) & 0xffu) >= 0xfeu) bits &= ~(UINT32_C(1) << 30);  /* |x| < 2^127 */
            if (pattern % 2) bits = (bits & UINT32_C(0x807fffff)) | UINT32_C(0x3f000000); /* audio range */
            memcpy(&history[i], &bits, sizeof bits);
        }
        check(history);
    }
    printf("31-tap FIR: %u folded results within 10 u sum|h x| of the exact sum "
           "(worst %.3f of the bound); table symmetric; history unchanged\n", checks, worst_ratio);
    return 0;
}
