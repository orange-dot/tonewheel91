/* OPT1 reference PCM: dynamic zero/nonzero cross-mod, sync and guard changes.
 * Expected hashes come from the preserved pre-OPT1 voice implementation. */
#include "../src/mamutanalog.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>

static uint64_t render(float rate, bool bcs) {
    ma_patch patch = bcs ? ma_patch_granica : ma_patch_tepih;
    for (unsigned m = 0; m < MA_MACRO_COUNT; m++) patch.macro[m] = 0;
    patch.sync_amount = patch.crossmod_amount = patch.bcs_amount = 0;
    patch.vco2_interval = 24;
    ma_synth voice;
    ma_synth_init_patch(&voice, rate, &patch);
    ma_synth_note_on(&voice, 0, 60, 103);
    uint64_t hash = 0;
    for (unsigned frame = 0; frame < 16384; frame++) {
        if (frame == 1024)
            ma_synth_set_oscillator_modulation(&voice, .8f, .1f, .35f, patch.noise_level);
        if (frame == 2048) ma_synth_note_on(&voice, 0, 127, 127);
        if (frame == 3072)
            ma_synth_set_oscillator_modulation(&voice, .8f, .1f, 0, patch.noise_level);
        if (frame == 4096) ma_synth_note_on(&voice, 0, 36, 78);
        if (frame == 5120)
            ma_synth_set_vco2(&voice, patch.vco2, .9f, -12, 11);
        if (frame == 6144)
            ma_synth_set_bcs(&voice, bcs ? .72f : 0, .62f);
        if (frame == 7168) ma_synth_set_pitch_bend(&voice, 2);
        if (frame == 8192) {
            ma_synth_set_bcs(&voice, 0, .62f);
            ma_synth_set_oscillator_modulation(&voice, 0, 0, 0, patch.noise_level);
        }
        if (frame == 9216) ma_synth_note_off(&voice, 0, 36, 0);
        if (frame == 10240) ma_synth_note_on(&voice, 0, 84, 96);
        if (frame == 11264) ma_synth_set_vco1(&voice, (ma_vco_controls){
            .saw_level = 1, .pulse_level = .7f, .triangle_level = .4f,
            .sine_level = .3f, .pulse_width = .05f });
        if (frame == 12288)
            ma_synth_set_oscillator_modulation(&voice, 1, 0, 1, patch.noise_level);
        if (frame == 13312)
            ma_synth_set_oscillator_modulation(&voice, 0, 0, 0, patch.noise_level);
        ma_frame sample = ma_synth_tick(&voice);
        if (!isfinite(sample.left) || !isfinite(sample.right)) return 0;
        hash = tw_fnv1a64(&sample, sizeof sample, hash);
        /* Hidden phase/triangle and guard evolution matters when a source
         * becomes audible again, even if the present PCM is silent. */
        const ma_oscillator *osc[] = { &voice.oscillator1, &voice.oscillator2 };
        for (unsigned i = 0; i < 2; i++) {
            hash = tw_fnv1a64(&osc[i]->phase_q48, sizeof osc[i]->phase_q48, hash);
            hash = tw_fnv1a64(&osc[i]->triangle, sizeof osc[i]->triangle, hash);
            hash = tw_fnv1a64(&osc[i]->guard_gain, sizeof osc[i]->guard_gain, hash);
        }
    }
    return hash;
}

int main(void) {
    const float rates[] = { 44100, 48000, 96000, 192000 };
    static const uint64_t expected[4][2] = {
        { UINT64_C(0x3e11f27384296f53), UINT64_C(0xa4e5657ce321b476) },
        { UINT64_C(0x3880a7751efc82f5), UINT64_C(0xcd0596a70310f8fb) },
        { UINT64_C(0xab51223edf28ab76), UINT64_C(0x4d4683a9457a444c) },
        { UINT64_C(0xa79622cbae1a85bd), UINT64_C(0x132c21ced63c3127) },
    };
    unsigned failures = 0;
    for (unsigned r = 0; r < 4; r++) {
        for (unsigned bcs = 0; bcs < 2; bcs++) {
            uint64_t hash = render(rates[r], bcs != 0);
            printf("OPT1 rate=%.0f bcs=%u hash=%016" PRIx64 "\n", rates[r], bcs, hash);
            if (!hash || hash != expected[r][bcs] || hash != render(rates[r], bcs != 0))
                failures++;
        }
    }
    if (failures) fprintf(stderr, "OPT1: %u reference mismatches\n", failures);
    return failures ? 1 : 0;
}
