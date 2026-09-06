/* MA2-4: identical dry scores at character 0 / .20 / 1, rerendered exactly. */
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/mamutanalog.h"
#include "wav.h"

enum { RATE = 48000, FRAMES = RATE * 8 };

static bool exhibit(float *pcm, float amount, bool unison) {
    static constexpr uint8_t notes[MA_CARD_COUNT] = { 48, 55, 60, 64, 67 };
    uint64_t hash = 0;
    double energy = 0;
    float peak = 0;
    for (unsigned pass = 0; pass < 2; pass++) {
        ma_card_bank bank = { 0 };
        ma_card_bank_init(&bank, RATE);
        ma_card_bank_set_character(&bank, amount);
        ma_card_bank_set_unison(&bank, unison);
        if (unison)
            (void)ma_card_bank_note_on(&bank, 0, 48, 100);
        else
            for (unsigned slot = 0; slot < MA_CARD_COUNT; slot++)
                (void)ma_card_bank_note_on(&bank, 0, notes[slot], 100);
        for (unsigned frame = 0; frame < FRAMES; frame++) {
            if (frame == RATE * 6) ma_card_bank_panic(&bank);
            ma_frame cards[MA_CARD_COUNT] = { 0 };
            ma_card_bank_tick(&bank, cards);
            float sample = 0;
            for (unsigned slot = 0; slot < MA_CARD_COUNT; slot++)
                sample += cards[slot].left * .20f;
            if (!isfinite(sample) || fabsf(sample) > 1) return false;
            if (pass == 0) {
                pcm[frame] = sample;
                hash = tw_fnv1a64(&sample, sizeof sample, hash);
                energy += (double)sample * sample;
                if (fabsf(sample) > peak) peak = fabsf(sample);
            } else if (memcmp(&pcm[frame], &sample, sizeof sample)) {
                return false;
            }
        }
    }
    char path[96] = { 0 };
    int length = snprintf(path, sizeof path, "build/ma2-4-%s-%03u.wav",
                          unison ? "unison" : "chord", (unsigned)(amount * 100));
    if (length < 0 || (size_t)length >= sizeof path
        || wav_write_f32(path, pcm, FRAMES, RATE, 1)) return false;
    printf("%s hash=%016" PRIx64 " peak=%.6f rms=%.6f repeat=exact\n",
           path, hash, (double)peak, sqrt(energy / FRAMES));
    fflush(stdout);
    return true;
}

int main(void) {
    float *pcm = malloc(FRAMES * sizeof *pcm);
    if (!pcm) return EXIT_FAILURE;
    const float amounts[] = { 0, .20f, 1 };
    for (unsigned mode = 0; mode < 2; mode++) {
        for (unsigned i = 0; i < sizeof amounts / sizeof *amounts; i++) {
            if (!exhibit(pcm, amounts[i], mode != 0)) {
                fprintf(stderr, "character exhibit: render or WAV failure\n");
                free(pcm);
                return EXIT_FAILURE;
            }
        }
    }
    free(pcm);
    return EXIT_SUCCESS;
}
