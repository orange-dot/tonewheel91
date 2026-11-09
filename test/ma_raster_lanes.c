/* White-box exactness gate: the eight-lane raster (ml_raster) against two
 * render_raster calls at the 2x rate. Random lane sets, sample rates, base
 * frequencies (zero, mip edges, above Nyquist), positions (family edges
 * included), warps and phases (near 0 and near wrap). Both outputs, the
 * phase and the mip compared bit for bit; lanes outside the set must read 0.
 * Skips without AVX2. */
#include "../src/ma_lanes.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint64_t seed = UINT64_C(0x4d41525354313031);
static float uniform(float lo, float hi) {
    return lo + (hi - lo) * (float)(tw_splitmix64(&seed) >> 40) * 0x1p-24f;
}
static uint32_t bits(float v) { uint32_t b; memcpy(&b, &v, 4); return b; }

int main(void) {
#if defined(__x86_64__)
    if (!ma_lanes_supported()) { puts("ma_raster_lanes: no AVX2; skipped"); return 0; }
    static const float rates[] = { 44100.0f, 48000.0f, 96000.0f, 192000.0f };
    static ma_synth scalar[MA_LANES], lanes[MA_LANES];
    unsigned compared = 0, blended = 0, top_mip = 0, edges = 0;
    for (unsigned trial = 0; trial < 50000; trial++) {
        ma_voice_front front[MA_LANES] = {};
        ma_synth *voice[MA_LANES];
        unsigned on = (unsigned)(tw_splitmix64(&seed) & 0xffu) | 1u;
        for (unsigned l = 0; l < MA_LANES; l++) {
            ma_synth *s = &scalar[l];
            if (trial % 1000 == 0 || tw_splitmix64(&seed) % 61 == 0)
                s->sample_rate_hz = rates[tw_splitmix64(&seed) % 4];
            if (s->sample_rate_hz == 0.0f) s->sample_rate_hz = 48000.0f;
            switch (tw_splitmix64(&seed) % 16) {
            case 0: s->raster.phase_q48 = 0; break;
            case 1: s->raster.phase_q48 = PHASE_MASK_Q48 - tw_splitmix64(&seed) % 4096; break;
            case 2: s->raster.phase_q48 = tw_splitmix64(&seed) % 4096; break;
            case 3: s->raster.mip = (uint8_t)(tw_splitmix64(&seed) % MA_RASTER_MIPS); break;
            default: break;
            }
            ma_render_controls *c = &front[l].controls;
            c->raster_mix = uniform(0.001f, 1.0f);
            uint64_t r = tw_splitmix64(&seed);
            c->raster_position = r % 8 == 0 ? (float)(r / 8 % MA_RASTER_FAMILIES) / (float)(MA_RASTER_FAMILIES - 1)
                               : uniform(0.0f, 1.0f);
            edges += r % 8 == 0;
            c->raster_warp = tw_splitmix64(&seed) % 8 == 0 ? (float)(tw_splitmix64(&seed) % 2)
                                                           : uniform(0.0f, 1.0f);
            switch (tw_splitmix64(&seed) % 8) {
            case 0: front[l].base_hz = 0.0f; break;
            case 1: front[l].base_hz = uniform(4000.0f, 120000.0f); break;
            default: front[l].base_hz = uniform(8.0f, 4200.0f); break;
            }
            lanes[l].sample_rate_hz = s->sample_rate_hz;
            lanes[l].raster = s->raster;
            voice[l] = &lanes[l];
        }
        float expected[2][MA_LANES] = {};
        for (unsigned l = 0; l < MA_LANES; l++) {
            if (!(on >> l & 1u)) continue;
            float blend = 0.0f;
            float step = phase_step(front[l].base_hz, 2.0f * scalar[l].sample_rate_hz);
            uint8_t mip = ma_voice_raster_mip(step, front[l].controls.raster_warp, &blend);
            blended += blend > 0.0f;
            top_mip += mip == MA_RASTER_MIPS - 1;
            for (int k = 0; k < 2; k++)
                expected[k][l] = ma_voice_render_raster(&scalar[l], &front[l].controls, front[l].base_hz,
                                                        2.0f * scalar[l].sample_rate_hz);
        }
        ml_in in;
        ml_benign(&in);
        for (int k = 0; k < 2; k++)
            for (unsigned l = 0; l < MA_LANES; l++) in.raster[k][l] = -1.0f;
        ml_raster(voice, front, on, &in);
        for (unsigned l = 0; l < MA_LANES; l++) {
            for (int k = 0; k < 2; k++)
                if (bits(in.raster[k][l]) != bits(expected[k][l])) {
                    fprintf(stderr, "ma_raster_lanes: step %d mismatch, trial %u lane %u (on %u): %a vs %a\n",
                            k, trial, l, on >> l & 1u, (double)in.raster[k][l], (double)expected[k][l]);
                    return 1;
                }
            if (lanes[l].raster.phase_q48 != scalar[l].raster.phase_q48
                || lanes[l].raster.mip != scalar[l].raster.mip) {
                fprintf(stderr, "ma_raster_lanes: state mismatch, trial %u lane %u\n", trial, l);
                return 1;
            }
            compared += on >> l & 1u;
        }
    }
    assert(compared > 200000 && blended > 10000 && top_mip > 1000 && edges > 10000);
    printf("ma_raster_lanes: %u lane-samples bit-identical (both 2x steps, phase, mip); "
           "%u mip blends, %u top mip, %u family edges\n", compared, blended, top_mip, edges);
#else
    puts("ma_raster_lanes: not x86-64; skipped");
#endif
    return 0;
}
