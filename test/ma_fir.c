/* White-box exactness gate: all circular positions against the full 31-tap
 * traversal, fused like the engine (2026-11-09). Skipping the zero taps
 * changes no bit for finite histories, with one exception: when the fused sum
 * rounds a tiny negative value to -0, a later zero tap (fma(0, x, -0) = +0)
 * would have turned it into +0. Only that sign of zero may differ. */
#include "../src/ma_voice.c"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static float reference_decimate(const float history[31], uint8_t position) {
    float output = 0.0f;
    int index = position;
    for (int tap = 0; tap < 31; tap++) {
        if (--index < 0) index = 30;
        output = ma_fmadd(OSC_HALFBAND_31[tap], history[index], output);
    }
    return output;
}

static uint32_t float_bits(float value) {
    uint32_t bits = 0;
    static_assert(sizeof bits == sizeof value);
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static unsigned zero_sign_only;
static void check(const float history[31]) {
    float saved[31];
    memcpy(saved, history, sizeof saved);
    for (uint8_t position = 0; position < 31; position++) {
        float expected = reference_decimate(history, position);
        float actual = decimate_oversample(history, position);
        if (float_bits(actual) != float_bits(expected)) {
            assert(actual == 0.0f && expected == 0.0f);
            zero_sign_only++;
        }
        assert(memcmp(saved, history, sizeof saved) == 0);
    }
}

int main(void) {
    const float values[] = {
        0.0f, -0.0f, 1.0f, -1.0f, FLT_TRUE_MIN, -FLT_TRUE_MIN,
        FLT_MIN, -FLT_MIN, FLT_MAX, -FLT_MAX, 0x1p-20f, -0x1p20f,
    };
    float history[31] = { 0 };
    for (unsigned v = 0; v < sizeof values / sizeof *values; v++) {
        for (unsigned i = 0; i < 31; i++) history[i] = values[v];
        check(history);
        for (unsigned i = 0; i < 31; i++)
            history[i] = i % 2 ? -values[v] : values[v];
        check(history);
        for (unsigned at = 0; at < 31; at++) {
            for (unsigned i = 0; i < 31; i++) history[i] = 0.0f;
            history[at] = values[v];
            check(history);
        }
    }
    for (unsigned shift = 0; shift < sizeof values / sizeof *values; shift++) {
        for (unsigned i = 0; i < 31; i++)
            history[i] = values[(i + shift) % (sizeof values / sizeof *values)];
        check(history);
    }
    uint32_t state = UINT32_C(0x6d616669);
    for (unsigned pattern = 0; pattern < 256; pattern++) {
        for (unsigned i = 0; i < 31; i++) {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            uint32_t bits = state;
            if ((bits & UINT32_C(0x7f800000)) == UINT32_C(0x7f800000))
                bits ^= UINT32_C(0x00800000);
            memcpy(&history[i], &bits, sizeof bits);
        }
        check(history);
    }
    printf("31-tap FIR: 15 nonzero taps match the fused 31-tap traversal at all positions "
           "(%u differ only in the sign of zero); history unchanged\n", zero_sign_only);
    return 0;
}
