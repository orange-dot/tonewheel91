/* Raw float PCM for arithmetic A/B work (for example a change of rounding).
 * Renders a fixed matrix -- six factory patches x BCS off/on x Glas off/on --
 * through the pool and writes one interleaved stereo float32 file per case,
 * plus an FNV-64 line per case on stdout. Build the same source against two
 * trees and compare the files with driver/pcm_diff.py. Hosted only. */
#include "../src/mamutanalog.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { RATE = 48000, BLOCK = 128, SECONDS = 6 };
static ma_voice_pool pool;

static void gesture(unsigned block) {
    static const uint8_t chord[4] = { 36, 48, 55, 64 };
    unsigned frame = block * BLOCK;
    if (block == 4)
        for (unsigned i = 0; i < 4; i++)
            (void)ma_voice_pool_note_on(&pool, 0, chord[i], (uint8_t)(70 + 15 * i));
    float t = (float)frame / RATE;
    if (block % 8 == 0) {
        ma_voice_pool_set_mod_wheel(&pool, 0, t < 3 ? t / 3 : 1);
        ma_voice_pool_set_channel_pressure(&pool, 0, t < 2 ? 0 : (t - 2) / 4);
        ma_voice_pool_set_pitch_bend(&pool, 0, t > 2.5f && t < 3.5f ? (t - 2.5f) * 0.6f : 0);
    }
    if (frame >= 2 * RATE && frame < 2 * RATE + BLOCK)
        (void)ma_voice_pool_note_on(&pool, 0, 79, 110);
    if (frame >= 4 * RATE && frame < 4 * RATE + BLOCK) {
        for (unsigned i = 0; i < 4; i++) (void)ma_voice_pool_note_off(&pool, 0, chord[i], 64);
        (void)ma_voice_pool_note_off(&pool, 0, 79, 64);
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fputs("usage: dump_ma_pcm OUTDIR  (writes <patch>-bcs<b>-glas<g>-<engine>.f32)\n", stderr);
        return 2;
    }
    const char *names[] = { "Tepih", "Lead", "Dubina", "Raster", "Prizma", "Granica" };
    const ma_patch *patches[] = { &ma_patch_tepih, &ma_patch_lead, &ma_patch_dubina,
                                  &ma_patch_raster, &ma_patch_prizma, &ma_patch_granica };
    static ma_frame out[BLOCK];
    for (unsigned p = 0; p < 6; p++)
    for (unsigned bcs = 0; bcs < 2; bcs++)
    for (unsigned glas = 0; glas < 2; glas++)
    for (unsigned engine = 0; engine < 2; engine++) {
        ma_voice_pool_config c = ma_voice_pool_default_config;
        c.limit = 16; c.unison_count = 2;
        c.engine = engine ? MA_POOL_ENGINE_SCALAR : MA_POOL_ENGINE_AUTO;
        ma_patch patch = *patches[p];
        patch.bcs_amount = bcs ? ma_patch_granica.bcs_amount : 0;
        patch.bcs_regime = ma_patch_granica.bcs_regime;
        if (glas) {
            patch.formant_mix = .8f; patch.formant_vowel = .4f; patch.formant_shift = 0;
            patch.formant_orbita = .6f; patch.grain_mix = .5f;
        }
        if (!ma_voice_pool_init(&pool, RATE, &c, &patch)) return 1;
        char path[512];
        snprintf(path, sizeof path, "%s/%s-bcs%u-glas%u-%s.f32", argv[1], names[p], bcs, glas,
                 engine ? "scalar" : "auto");
        FILE *f = fopen(path, "wb");
        if (!f) { perror(path); return 1; }
        uint64_t hash = 0;
        for (unsigned b = 0; b < SECONDS * RATE / BLOCK; b++) {
            gesture(b);
            ma_voice_pool_render(&pool, out, BLOCK);
            hash = tw_fnv1a64(out, sizeof out, hash);
            if (fwrite(out, sizeof out, 1, f) != 1) { perror(path); return 1; }
        }
        fclose(f);
        printf("%s-bcs%u-glas%u-%s %016" PRIx64 "\n", names[p], bcs, glas,
               engine ? "scalar" : "auto", hash);
    }
    return 0;
}
