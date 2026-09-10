/* OPT2: replay every waveform combination against the OPT1 reference.
 * Each control ramp settles even at 192 kHz before the next change. */
#include "../src/mamutanalog.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>

static ma_vco_controls waveforms(unsigned mask, float width) {
    return (ma_vco_controls){
        .saw_level = mask & 1 ? .7f : 0,
        .pulse_level = mask & 2 ? .4f : 0,
        .triangle_level = mask & 4 ? .8f : 0,
        .sine_level = mask & 8 ? .3f : 0,
        .pulse_width = width,
    };
}

static uint64_t render(float rate, bool sync) {
    ma_patch patch = ma_patch_tepih;
    patch.vco1 = waveforms(0, .5f);
    patch.vco2 = waveforms(0, .5f);
    patch.vco2_level = patch.noise_level = patch.mozaik_mix = 0;
    patch.sync_amount = sync ? .8f : 0;
    patch.crossmod_amount = sync ? .35f : 0;
    ma_synth voice;
    ma_synth_init_patch(&voice, rate, &patch);
    ma_synth_note_on(&voice, 0, 84, 103);
    uint64_t hash = 0;
    for (unsigned frame = 0; frame < 16 * 2048; frame++) {
        if (frame % 2048 == 0) {
            unsigned segment = frame / 2048;
            unsigned mask = segment ^ (segment >> 1);
            float width = segment & 1 ? .05f : .95f;
            ma_vco_controls first = waveforms(mask, width);
            ma_vco_controls second = waveforms(mask ^ 15, 1 - width);
            /* Negative zero is a valid clamped control as well. */
            if (first.saw_level == 0) first.saw_level = -0.0f;
            if (second.pulse_level == 0) second.pulse_level = -0.0f;
            ma_synth_set_vco1(&voice, first);
            ma_synth_set_vco2(&voice, second,
                segment % 4 == 0 ? 0 : .65f, segment & 1 ? 24 : -12, 7);
            if (segment == 4) ma_synth_note_on(&voice, 0, 127, 127);
            if (segment == 8) ma_synth_note_on(&voice, 0, 36, 78);
            if (segment == 12) ma_synth_note_off(&voice, 0, 36, 0);
            if (segment == 14) ma_synth_note_on(&voice, 0, 60, 96);
        }
        ma_frame sample = ma_synth_tick(&voice);
        if (!isfinite(sample.left) || !isfinite(sample.right)) return 0;
        hash = tw_fnv1a64(&sample, sizeof sample, hash);
        const ma_oscillator *osc[] = { &voice.oscillator1, &voice.oscillator2 };
        for (unsigned i = 0; i < 2; i++) {
            hash = tw_fnv1a64(&osc[i]->phase_q48, sizeof osc[i]->phase_q48, hash);
            hash = tw_fnv1a64(&osc[i]->triangle, sizeof osc[i]->triangle, hash);
            hash = tw_fnv1a64(&osc[i]->guard_gain, sizeof osc[i]->guard_gain, hash);
            hash = tw_fnv1a64(&osc[i]->sync_residual, sizeof osc[i]->sync_residual, hash);
        }
    }
    return hash;
}

int main(void) {
    const float rates[] = { 44100, 48000, 96000, 192000 };
    static const uint64_t expected[4][2] = {
        { UINT64_C(0x7295de482d99f345), UINT64_C(0x136eee40a0327864) },
        { UINT64_C(0x7da1e292c9c4baa7), UINT64_C(0x3edc6acbdecfa2f9) },
        { UINT64_C(0x167a58c97fbc2d53), UINT64_C(0x3c0dcddaf008802d) },
        { UINT64_C(0x256ac9b06a4c7d5d), UINT64_C(0xf96809b3a851811b) },
    };
    unsigned failures = 0;
    for (unsigned r = 0; r < 4; r++) {
        for (unsigned sync = 0; sync < 2; sync++) {
            uint64_t hash = render(rates[r], sync != 0);
            printf("OPT2 rate=%.0f sync=%u hash=%016" PRIx64 "\n", rates[r], sync, hash);
            if (!hash || hash != expected[r][sync] || hash != render(rates[r], sync != 0))
                failures++;
        }
    }
    if (failures) fprintf(stderr, "OPT2: %u reference mismatches\n", failures);
    return failures ? 1 : 0;
}
