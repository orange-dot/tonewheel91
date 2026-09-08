/* Opt-in listening evidence, independent of existing arrangements. */
#include "../src/mamutanalog.h"
#include "wav.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { RATE = 48000, SECONDS = 12, FRAMES = RATE * SECONDS, CASES = 4 };
static ma_voice_pool pool;
static const char *names[] = {"pad", "overlap", "steal", "unison8"};
static void note(uint8_t pitch) {
    if (!ma_voice_pool_note_on(&pool, 0, pitch, 100)) abort();
}
static void render(unsigned scenario, float *pcm) {
    ma_voice_pool_config c = ma_voice_pool_default_config;
    c.limit = scenario == 2 ? 8 : 32;
    c.unison_count = scenario == 3 ? 8 : 1;
    const ma_patch *patch = scenario == 2 ? &ma_patch_lead : &ma_patch_tepih;
    if (!ma_voice_pool_init(&pool, RATE, &c, patch)) abort();
    ma_voice_pool_set_output(&pool, patch->body_drive, .8f, .1f, .7f);
    for (unsigned i = 0; i < FRAMES; i++) {
        unsigned second = i / RATE;
        if (i % RATE == 0) {
            if (second == 0) { note(48); note(55); note(60); note(64); }
            if (scenario == 1 && (second == 3 || second == 6)) {
                ma_voice_pool_panic(&pool);
                note((uint8_t)(48 + second)); note((uint8_t)(55 + second));
                note((uint8_t)(60 + second)); note((uint8_t)(64 + second));
            }
            if (scenario == 3 && second == 4) { note(50); note(57); }
            if (second == 8) ma_voice_pool_panic(&pool);
        }
        if (scenario == 2 && i % (RATE/4) == 0 && second < 8)
            note((uint8_t)(48 + (i / (RATE/4)) % 24));
        ma_frame f = ma_voice_pool_tick(&pool);
        if (!isfinite(f.left) || !isfinite(f.right)) abort();
        pcm[2*i] = f.left; pcm[2*i+1] = f.right;
    }
}
int main(int argc, char **argv) {
    if (argc != 3 || strcmp(argv[1], "--render")) {
        puts("Usage: exhibit_ma_pool --render OUTPUT_PREFIX\nWrites four 12 s raw and RMS-matched stereo WAVs; prints levels, gains and repeat-verified hashes. Parent directory must exist.");
        return argc == 1 || (argc == 2 && !strcmp(argv[1], "--help")) ? 0 : 1;
    }
    float *pcm[CASES] = {0}, *repeat = calloc(2u * FRAMES, sizeof(float));
    double rms[CASES], peak[CASES], target = pow(10, -22.0/20);
    if (!repeat) return 1;
    for (unsigned c = 0; c < CASES; c++) {
        pcm[c] = calloc(2u * FRAMES, sizeof(float));
        if (!pcm[c]) return 1;
        render(c, pcm[c]); render(c, repeat);
        if (memcmp(pcm[c], repeat, 2u * FRAMES * sizeof(float))) return 1;
        double energy = 0; peak[c] = 0;
        for (unsigned i = 0; i < 2u * FRAMES; i++) {
            double x = pcm[c][i]; energy += x*x;
            if (fabs(x) > peak[c]) peak[c] = fabs(x);
        }
        rms[c] = sqrt(energy / (2u * FRAMES));
        if (rms[c] == 0) return 1;
        double ceiling = .707945784 * rms[c] / peak[c];
        if (ceiling < target) target = ceiling;
    }
    for (unsigned c = 0; c < CASES; c++) {
        char path[1024];
        int n = snprintf(path, sizeof path, "%s_%s_raw.wav", argv[2], names[c]);
        if (n < 0 || (size_t)n >= sizeof path || wav_write_f32(path, pcm[c], FRAMES, RATE, 2)) return 1;
        uint64_t hash = tw_fnv1a64(pcm[c], 2u * FRAMES * sizeof(float), 0);
        double gain = target / rms[c];
        for (unsigned i = 0; i < 2u * FRAMES; i++) pcm[c][i] *= (float)gain;
        n = snprintf(path, sizeof path, "%s_%s_listen.wav", argv[2], names[c]);
        if (n < 0 || (size_t)n >= sizeof path || wav_write_f32(path, pcm[c], FRAMES, RATE, 2)) return 1;
        printf("%s raw_hash=%016" PRIx64 " rms_dbfs=%.6f peak_dbfs=%.6f gain_db=%.6f matched_rms_dbfs=%.6f\n",
            names[c], hash, 20*log10(rms[c]), 20*log10(peak[c]), 20*log10(gain), 20*log10(target));
        free(pcm[c]);
    }
    free(repeat);
}
