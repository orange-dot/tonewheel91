/* MA2-5 dry stereo evidence: fixed gain, fresh state, exact second pass. */
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/mamutanalog.h"
#include "wav.h"

enum { RATE = 48000, FRAMES = RATE * 6 };

static bool exhibit(float *pcm, unsigned voices, float width, float drive) {
    static constexpr uint8_t notes[MA_CARD_COUNT] = { 48, 55, 60, 64, 67 };
    uint64_t hash = 0;
    double sum_left = 0, sum_right = 0, ll = 0, rr = 0, lr = 0;
    ma_output_diagnostics diagnostics = { 0 };
    for (unsigned pass = 0; pass < 2; pass++) {
        ma_patch patch = ma_patch_tepih;
        patch.width = width;
        patch.body_drive = drive;
        ma_card_bank bank = { 0 };
        ma_card_bank_init_patch(&bank, RATE, &patch);
        for (unsigned i = 0; i < voices; i++) {
            unsigned slot = voices == 3 ? i * 2 : i;
            bank.cursor = (uint8_t)slot;
            (void)ma_card_bank_note_on(&bank, 0, notes[slot], 100);
        }
        for (unsigned frame = 0; frame < FRAMES; frame++) {
            if (frame == RATE * 4) ma_card_bank_panic(&bank);
            ma_frame sample = ma_card_bank_tick_stereo(&bank);
            if (!isfinite(sample.left) || !isfinite(sample.right)
                || fabsf(sample.left) > 1 || fabsf(sample.right) > 1) return false;
            if (pass == 0) {
                pcm[2 * frame] = sample.left;
                pcm[2 * frame + 1] = sample.right;
                hash = tw_fnv1a64(&sample.left, sizeof sample.left, hash);
                hash = tw_fnv1a64(&sample.right, sizeof sample.right, hash);
                sum_left += sample.left;
                sum_right += sample.right;
                ll += (double)sample.left * sample.left;
                rr += (double)sample.right * sample.right;
                lr += (double)sample.left * sample.right;
            } else if (memcmp(&pcm[2 * frame], &sample.left, sizeof sample.left)
                       || memcmp(&pcm[2 * frame + 1], &sample.right, sizeof sample.right)) {
                return false;
            }
        }
        diagnostics = bank.stereo.output.diagnostics;
    }
    if (diagnostics.sanitization_count != 0) return false;
    char path[128] = { 0 };
    int length = snprintf(path, sizeof path,
        "build/ma2-5-%u-cards-width-%03u-body-%03u.wav", voices,
        (unsigned)(width * 100), (unsigned)(drive * 100));
    if (length < 0 || (size_t)length >= sizeof path
        || wav_write_f32(path, pcm, FRAMES, RATE, 2)) return false;
    double covariance = FRAMES * lr - sum_left * sum_right;
    double denominator = sqrt((FRAMES * ll - sum_left * sum_left)
                              * (FRAMES * rr - sum_right * sum_right));
    printf("%s hash=%016" PRIx64 " pre_peak=%.6f post_peak=%.6f "
           "rms=%.6f correlation=%.6f knee_hits=%" PRIu64 " repeat=exact\n",
        path, hash, (double)diagnostics.pre_peak, (double)diagnostics.post_peak,
        sqrt((ll + rr) / (2 * FRAMES)), denominator > 0 ? covariance / denominator : 0,
        diagnostics.knee_hit_count);
    fflush(stdout);
    return true;
}

int main(void) {
    float *pcm = malloc(2 * FRAMES * sizeof *pcm);
    if (!pcm) return EXIT_FAILURE;
    bool ok = true;
    for (unsigned voices = 1; voices <= 5 && ok; voices += 2) {
        ok = exhibit(pcm, voices, 0, .1f);
        if (ok) ok = exhibit(pcm, voices, .7f, .1f);
    }
    if (ok) ok = exhibit(pcm, 5, 1, 0);
    if (ok) ok = exhibit(pcm, 5, 1, .8f);
    free(pcm);
    if (!ok) fprintf(stderr, "stereo exhibit: render or WAV failure\n");
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
