/* Short, opt-in MA3-2 listening pairs. Same notes/seed/patch in all FX arms. */
#include "../src/mamutanalog.h"
#include "wav.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { RATE = 48000, FRAMES = RATE * 8, ACTIVE = RATE * 4, ARMS = 4 };
static ma_voice_pool pool;
static const char *arms[] = {"dry", "chorus", "reverb", "wet"};
static void chord(unsigned offset) {
    const uint8_t notes[] = {48, 55, 60, 64};
    for (unsigned i = 0; i < 4; i++)
        if (!ma_voice_pool_note_on(&pool, 0, (uint8_t)(notes[i]+offset), 96)) abort();
}
static void render(const ma_patch *patch, unsigned arm, float *pcm) {
    ma_voice_pool_config config = ma_voice_pool_default_config;
    config.limit = 16; config.unison_count = 2;
    if (!ma_voice_pool_init(&pool, RATE, &config, patch)) abort();
    if (arm == 1 || arm == 3)
        ma_effects_set_chorus(&pool.effects, (ma_chorus_controls){true, .30f, .35f, .25f});
    if (arm == 2 || arm == 3)
        ma_effects_set_reverb(&pool.effects, (ma_reverb_controls){true, .25f, .65f, .55f});
    for (unsigned i = 0; i < FRAMES; i++) {
        if (i == 0) chord(0);
        if (i == RATE) {
            ma_voice_pool_set_sustain(&pool, 0, true);
            const uint8_t notes[] = {48, 55, 60, 64};
            for (unsigned n = 0; n < 4; n++)
                if (!ma_voice_pool_note_off(&pool, 0, notes[n], 0)) abort();
        }
        if (i == RATE * 2) chord(2);
        if (i == RATE * 3) ma_voice_pool_set_sustain(&pool, 0, false);
        if (i == ACTIVE) ma_voice_pool_panic(&pool);
        ma_frame f = ma_voice_pool_tick(&pool);
        if (!isfinite(f.left) || !isfinite(f.right) || fabsf(f.left)>1 || fabsf(f.right)>1) abort();
        pcm[2*i] = f.left; pcm[2*i+1] = f.right;
    }
}
int main(int argc, char **argv) {
    if (argc != 3 || strcmp(argv[1], "--render")) {
        puts("Usage: exhibit_ma_effects --render OUTPUT_PREFIX\n"
             "Eight 8-second raw and level-matched WAVs: Tepih/Granica x dry/chorus/reverb/wet.");
        return argc == 1 ? 0 : 1;
    }
    float *pcm[ARMS] = {0}, *repeat = calloc(2u * FRAMES, sizeof(float));
    int result = 1;
    for (unsigned a = 0; a < ARMS; a++) pcm[a] = calloc(2u * FRAMES, sizeof(float));
    if (!repeat || !pcm[0] || !pcm[1] || !pcm[2] || !pcm[3]) goto done;
    const ma_patch *patches[] = {&ma_patch_tepih, &ma_patch_granica};
    const char *names[] = {"tepih", "granica"};
    printf("effects=%zu pool=%zu bytes; 48kHz limit16 unison2; active RMS window 0..4s\n",
        sizeof(ma_effects), sizeof(ma_voice_pool));
    for (unsigned p = 0; p < 2; p++) {
        double rms[ARMS], peak[ARMS], target = pow(10, -22.0/20);
        for (unsigned a = 0; a < ARMS; a++) {
            render(patches[p], a, pcm[a]); render(patches[p], a, repeat);
            if (memcmp(pcm[a], repeat, 2u*FRAMES*sizeof(float))) goto done;
            double energy = 0; peak[a] = 0;
            for (unsigned i = 0; i < 2u * FRAMES; i++) {
                double x = pcm[a][i];
                if (i < 2u*ACTIVE) energy += x*x;
                if (fabs(x) > peak[a]) peak[a] = fabs(x);
            }
            rms[a] = sqrt(energy / (2u*ACTIVE));
            if (rms[a] == 0) goto done;
            double ceiling = .707945784 * rms[a] / peak[a];
            if (ceiling < target) target = ceiling;
        }
        for (unsigned a = 0; a < ARMS; a++) {
            char path[1024];
            int n = snprintf(path, sizeof path, "%s_%s_%s_raw.wav", argv[2], names[p], arms[a]);
            if (n < 0 || (size_t)n >= sizeof path || wav_write_f32(path, pcm[a], FRAMES, RATE, 2)) goto done;
            uint64_t hash = tw_fnv1a64(pcm[a], 2u*FRAMES*sizeof(float), 0);
            float gain = (float)(target / rms[a]);
            for (unsigned i = 0; i < 2u*FRAMES; i++) pcm[a][i] *= gain;
            n = snprintf(path, sizeof path, "%s_%s_%s_listen.wav", argv[2], names[p], arms[a]);
            if (n < 0 || (size_t)n >= sizeof path || wav_write_f32(path, pcm[a], FRAMES, RATE, 2)) goto done;
            printf("%s/%s hash=%016" PRIx64 " rms_dbfs=%.6f peak_dbfs=%.6f gain_db=%.6f matched_rms_dbfs=%.6f\n",
                names[p], arms[a], hash, 20*log10(rms[a]), 20*log10(peak[a]), 20*log10(gain), 20*log10(target));
        }
    }
    result = 0;
done:
    for (unsigned a = 0; a < ARMS; a++) free(pcm[a]);
    free(repeat);
    return result;
}
