/* MA1-7 hosted output-body and safety audition. */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/mamutanalog.h"
#include "wav.h"

enum {
    RATE = 48000,
    DURATION_SECONDS = 8,
    FRAME_COUNT = RATE * DURATION_SECONDS,
};

typedef enum {
    TAKE_BYPASS,
    TAKE_BODY,
    TAKE_IDENTITY_LOAD,
} take_kind;

typedef struct {
    take_kind kind;
    char const *name;
    char const *path;
} output_take;

typedef struct {
    double sum;
    double sum_squares;
    float peak;
    unsigned nonfinite;
    unsigned stereo_mismatch;
    unsigned body_changed;
    ma_output_diagnostics output;
} output_metrics;

static const output_take TAKES[] = {
    { TAKE_BYPASS, "body-bypass", "build/ma1-7_body_bypass.wav" },
    { TAKE_BODY, "body-direct", "build/ma1-7_body_direct.wav" },
    { TAKE_IDENTITY_LOAD, "identity-load", "build/ma1-7_identity_load.wav" },
};

static float rendered[2 * FRAME_COUNT];
static float repeated[2 * FRAME_COUNT];

static void configure(ma_synth *synth, take_kind kind) {
    ma_patch patch = ma_patch_tepih;
    patch.body_drive = kind == TAKE_BYPASS ? 0.0f : 0.10f;
    patch.master_level = 0.75f;
    ma_synth_init_patch(synth, RATE, &patch);
    if (kind == TAKE_IDENTITY_LOAD) {
        ma_synth_set_macro(synth, MA_MACRO_GRAVITACIJA, 0.72f);
        ma_synth_set_macro(synth, MA_MACRO_HEAT, 0.80f);
        ma_synth_set_macro(synth, MA_MACRO_RUIN, 0.38f);
    }
}

static bool render(output_take take, float dst[2 * FRAME_COUNT],
                   output_metrics *metrics) {
    ma_synth synth;
    configure(&synth, take.kind);
    *metrics = (output_metrics){ 0 };

    for (int frame = 0; frame < FRAME_COUNT; frame++) {
        if (frame == RATE / 2) ma_synth_note_on(&synth, 0, 48, 92);
        if (frame == 7 * RATE / 2) ma_synth_note_off(&synth, 0, 48, 40);
        if (frame == 17 * RATE / 4) ma_synth_note_on(&synth, 0, 55, 108);
        if (frame == 27 * RATE / 4) ma_synth_note_off(&synth, 0, 55, 72);

        ma_frame sample = ma_synth_tick(&synth);
        if (!isfinite(sample.left) || !isfinite(sample.right)) {
            metrics->nonfinite++;
            sample = (ma_frame){ 0 };
        }
        metrics->stereo_mismatch += memcmp(
            &sample.left, &sample.right, sizeof sample.left) != 0;
        metrics->body_changed += synth.output.pre_body
                               != synth.output.post_body;
        float magnitude = fabsf(sample.left);
        if (magnitude > metrics->peak) metrics->peak = magnitude;
        metrics->sum += sample.left;
        metrics->sum_squares += (double)sample.left * sample.left;
        dst[2 * frame] = sample.left;
        dst[2 * frame + 1] = sample.right;
    }
    metrics->output = synth.output.diagnostics;
    return metrics->nonfinite == 0 && metrics->stereo_mismatch == 0
        && metrics->peak > 1.0e-5f && metrics->output.post_peak <= 1.0f
        && (take.kind == TAKE_BYPASS ? metrics->body_changed == 0
                                    : metrics->body_changed > 0);
}

int main(void) {
    int result = 0;
    puts("MA1-7 output audition -- 48 kHz, centered dual mono");
    puts("take             peak      rms       dc       pre/post  knee  flush  FNV64");

    for (size_t index = 0; index < sizeof TAKES / sizeof *TAKES; index++) {
        output_take take = TAKES[index];
        output_metrics first = { 0 }, second = { 0 };
        bool first_ok = render(take, rendered, &first);
        uint64_t first_hash = tw_fnv1a64(rendered, sizeof rendered, 0);
        bool second_ok = render(take, repeated, &second);
        uint64_t second_hash = tw_fnv1a64(repeated, sizeof repeated, 0);
        bool deterministic = first_hash == second_hash
                          && memcmp(rendered, repeated, sizeof rendered) == 0
                          && memcmp(&first.output, &second.output,
                                    sizeof first.output) == 0;
        bool written = wav_write_f32(take.path, rendered, FRAME_COUNT,
                                     RATE, 2) == 0;
        double rms = sqrt(first.sum_squares / FRAME_COUNT);
        double dc = first.sum / FRAME_COUNT;
        printf("%-16s %.6f  %.6f  %+.7f  %.3f/%.3f  %5llu  %5llu  %016llx\n",
               take.name, (double)first.peak, rms, dc,
               (double)first.output.pre_peak,
               (double)first.output.post_peak,
               (unsigned long long)first.output.knee_hit_count,
               (unsigned long long)first.output.tiny_flush_count,
               (unsigned long long)first_hash);
        printf("                 body %6u frames  repeat %s  wav %s\n",
               first.body_changed, deterministic ? "yes" : "NO",
               written ? take.path : "FAILED");
        if (!first_ok || !second_ok || !deterministic || !written) result = 1;
    }

    puts("\nsafety anchors: input -> output");
    static const float ANCHORS[] = { -1.10f, -1.0f, -0.99f, -0.98f,
                                     0.98f, 0.99f, 1.0f, 1.10f };
    for (size_t index = 0; index < sizeof ANCHORS / sizeof *ANCHORS; index++)
        printf("%+.3f -> %+.6f\n", (double)ANCHORS[index],
               (double)ma_safety_curve(ANCHORS[index]));
    return result;
}
