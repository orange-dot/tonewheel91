/* MA1-AUD: a hosted listening path through the landed MA1-5 voice. */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/mamutanalog.h"
#include "wav.h"

enum {
    RATE = 48000,
    DURATION_SECONDS = 14,
    FRAME_COUNT = RATE * DURATION_SECONDS,
};

static constexpr float MONITOR_GAIN = 0.5f;

typedef enum {
    TAKE_FACTORY,
    TAKE_ANALOG_ONLY,
    TAKE_MOZAIK_FOCUS,
} take_kind;

typedef struct {
    take_kind kind;
    char const *name;
    char const *path;
} audition_take;

typedef struct {
    long frame;
    uint8_t note;
    uint8_t velocity;
    bool on;
} note_event;

typedef struct {
    double sum;
    double sum_squares;
    float raw_peak;
    unsigned nonfinite;
    unsigned clipped;
    unsigned stereo_mismatch;
} audition_metrics;

static audition_take const TAKES[] = {
    { TAKE_FACTORY, "factory", "build/ma1-5_factory.wav" },
    { TAKE_ANALOG_ONLY, "analog-only", "build/ma1-5_analog_only.wav" },
    { TAKE_MOZAIK_FOCUS, "mozaik-focus", "build/ma1-5_mozaik_focus.wav" },
};

/* The two notes leave enough room for the factory 600 ms attack and 3 s
 * release to be heard without overlap. */
static note_event const SCRIPT[] = {
    { RATE / 4,                  48, 78, true },
    { 4 * RATE + RATE / 4,      48, 0, false },
    { 7 * RATE + RATE / 4,      55, 96, true },
    { 10 * RATE + 3 * RATE / 4, 55, 0, false },
};

static float output[2 * FRAME_COUNT];

static void configure(ma_synth *synth, take_kind kind) {
    ma_synth_init(synth, RATE);
    switch (kind) {
    case TAKE_FACTORY:
        break;
    case TAKE_ANALOG_ONLY:
        ma_synth_set_mozaik(synth, 0.0f, 0.5601133f, 0.5150284f,
                            0.0f, 0.05f);
        break;
    case TAKE_MOZAIK_FOCUS:
        ma_synth_set_mozaik(synth, 0.60f, 0.5601133f, 0.75f, 0.0f, 0.35f);
        ma_synth_set_filter(synth, 1400.0f, 0.35f, 0.40f, 0.35f);
        break;
    }
}

static void apply_event(ma_synth *synth, note_event event) {
    if (event.on)
        ma_synth_note_on(synth, event.note, event.velocity);
    else
        ma_synth_note_off(synth, event.note);
}

static bool render(audition_take take, float *dst, audition_metrics *metrics) {
    ma_synth synth;
    configure(&synth, take.kind);
    *metrics = (audition_metrics){ 0 };

    size_t event = 0;
    for (long frame = 0; frame < FRAME_COUNT; frame++) {
        while (event < sizeof SCRIPT / sizeof *SCRIPT
               && SCRIPT[event].frame == frame)
            apply_event(&synth, SCRIPT[event++]);

        ma_frame sample = ma_synth_tick(&synth);
        if (!isfinite(sample.left) || !isfinite(sample.right)) {
            metrics->nonfinite++;
            sample = (ma_frame){ 0 };
        }
        if (memcmp(&sample.left, &sample.right, sizeof sample.left))
            metrics->stereo_mismatch++;

        float magnitude = fabsf(sample.left);
        if (magnitude > metrics->raw_peak) metrics->raw_peak = magnitude;
        metrics->sum += sample.left;
        metrics->sum_squares += (double)sample.left * sample.left;

        dst[2 * frame] = MONITOR_GAIN * sample.left;
        dst[2 * frame + 1] = MONITOR_GAIN * sample.right;
        if (fabsf(dst[2 * frame]) > 1.0f
            || fabsf(dst[2 * frame + 1]) > 1.0f)
            metrics->clipped++;
    }
    return metrics->nonfinite == 0 && metrics->clipped == 0
        && metrics->stereo_mismatch == 0 && metrics->raw_peak > 1e-4f
        && metrics->sum_squares > 1e-8;
}

static bool repeats(audition_take take, float const *reference) {
    ma_synth synth;
    configure(&synth, take.kind);

    size_t event = 0;
    for (long frame = 0; frame < FRAME_COUNT; frame++) {
        while (event < sizeof SCRIPT / sizeof *SCRIPT
               && SCRIPT[event].frame == frame)
            apply_event(&synth, SCRIPT[event++]);
        ma_frame sample = ma_synth_tick(&synth);
        float left = MONITOR_GAIN * sample.left;
        float right = MONITOR_GAIN * sample.right;
        if (memcmp(&left, reference + 2 * frame, sizeof left)
            || memcmp(&right, reference + 2 * frame + 1, sizeof right))
            return false;
    }
    return true;
}

int main(void) {
    int result = 0;
    printf("MA1-5 one-voice audition -- 48 kHz, dual mono, gain %.2f\n\n",
           MONITOR_GAIN);

    for (size_t i = 0; i < sizeof TAKES / sizeof *TAKES; i++) {
        audition_metrics metrics = { 0 };
        audition_take take = TAKES[i];
        bool valid = render(take, output, &metrics);
        bool deterministic = repeats(take, output);
        uint64_t hash = tw_fnv1a64(output, sizeof output, 0);
        double mean = metrics.sum / FRAME_COUNT;
        double rms = sqrt(metrics.sum_squares / FRAME_COUNT);
        bool written = wav_write_f32(take.path, output, FRAME_COUNT,
                                     RATE, 2) == 0;

        printf("%-13s raw peak %.6f  rms %.6f  dc %+.7f  "
               "FNV64 %016llx\n"
               "              finite %s  dual-mono %s  headroom %s  "
               "repeat %s  wav %s\n",
               take.name, metrics.raw_peak, rms, mean,
               (unsigned long long)hash,
               metrics.nonfinite == 0 ? "yes" : "NO",
               metrics.stereo_mismatch == 0 ? "yes" : "NO",
               metrics.clipped == 0 ? "yes" : "NO",
               deterministic ? "yes" : "NO", written ? take.path : "FAILED");

        if (!valid || !deterministic || !written) result = 1;
    }
    return result;
}
