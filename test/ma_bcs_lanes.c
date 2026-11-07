/* White-box exactness gate: the eight-lane BCS (ml_bcs) against render_bcs.
 * Random regimes, amounts, frequencies (clamp edges included), feedback,
 * sample rates and states, some outside the safe range (NaN, Inf, above the
 * ceiling) so the reset path runs. Output and the whole ma_bcs compared bit
 * for bit after every step. Skips without AVX2. */
#include "../src/ma_voice.c"
#include "../src/ma_lanes.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint64_t seed = UINT64_C(0x4d41424353313031);
static float uniform(float lo, float hi) {
    return lo + (hi - lo) * (float)(tw_splitmix64(&seed) >> 40) * 0x1p-24f;
}
static float wild_state(void) {
    uint64_t r = tw_splitmix64(&seed);
    switch (r % 64) {
    case 0: return __builtin_nanf("");
    case 1: return __builtin_inff();
    case 2: return 31.999998f;
    case 3: return -40.0f;
    case 4: return 0.0f;
    default: return uniform(-2.0f, 2.0f);
    }
}

int main(void) {
#if defined(__x86_64__)
    if (!ma_lanes_supported()) { puts("ma_bcs_lanes: no AVX2; skipped"); return 0; }
    static ma_synth scalar[MA_LANES], lanes[MA_LANES];
    static const float rates[] = { 44100.0f, 48000.0f, 96000.0f, 192000.0f };
    unsigned resets = 0, compared = 0;
    for (unsigned trial = 0; trial < 20000; trial++) {
        ma_voice_front front[MA_LANES] = {};
        float y4[MA_LANES], expected[MA_LANES], actual[MA_LANES] = {};
        ma_synth *voice[MA_LANES];
        unsigned on = (unsigned)(tw_splitmix64(&seed) & 0xffu) | 1u;
        for (unsigned l = 0; l < MA_LANES; l++) {
            if (trial % 500 == 0 || tw_splitmix64(&seed) % 97 == 0) {
                memset(&scalar[l], 0, sizeof scalar[l]);
                scalar[l].sample_rate_hz = rates[tw_splitmix64(&seed) % 4];
                scalar[l].bcs.hopf_re = wild_state();
                scalar[l].bcs.hopf_im = wild_state();
                scalar[l].bcs.duffing_x = wild_state();
                scalar[l].bcs.duffing_v = wild_state();
                scalar[l].bcs.maximum_state = uniform(0.0f, 4.0f);
            }
            front[l].controls.bcs_regime = tw_splitmix64(&seed) % 8 == 0
                ? (float)(tw_splitmix64(&seed) % 4) / 3.0f : uniform(0.0f, 1.0f);
            front[l].controls.bcs_amount = uniform(0.001f, 1.0f);
            front[l].base_hz = tw_splitmix64(&seed) % 16 == 0 ? uniform(0.0f, 30000.0f)
                                                               : uniform(20.0f, 4000.0f);
            y4[l] = uniform(-2.5f, 2.5f);
            lanes[l] = scalar[l];
            voice[l] = &lanes[l];
        }
        uint64_t before[MA_LANES];
        for (unsigned l = 0; l < MA_LANES; l++) before[l] = scalar[l].bcs.reset_count;
        for (unsigned l = 0; l < MA_LANES; l++) {
            expected[l] = 0.0f;
            if (on >> l & 1u)
                expected[l] = render_bcs(&scalar[l], front[l].base_hz, front[l].controls.bcs_amount,
                                         front[l].controls.bcs_regime, y4[l]);
        }
        ml_bcs(voice, front, y4, on, actual);
        for (unsigned l = 0; l < MA_LANES; l++) {
            if (!(on >> l & 1u)) continue;
            uint32_t a, e;
            memcpy(&a, &actual[l], 4); memcpy(&e, &expected[l], 4);
            if (a != e || memcmp(&lanes[l].bcs, &scalar[l].bcs, sizeof scalar[l].bcs)) {
                fprintf(stderr, "ma_bcs_lanes: mismatch at trial %u lane %u (expected %a, got %a)\n",
                        trial, l, (double)expected[l], (double)actual[l]);
                return 1;
            }
            compared++;
        }
        for (unsigned l = 0; l < MA_LANES; l++) resets += (unsigned)(scalar[l].bcs.reset_count - before[l]);
    }
    assert(compared > 50000 && resets > 100);
    printf("ma_bcs_lanes: %u lane-steps bit-identical (output and state), %u reset paths exercised\n",
           compared, resets);
#else
    puts("ma_bcs_lanes: not x86-64; skipped");
#endif
    return 0;
}
