/* MA1-6: hosted listening comparisons for identity and performance motion. */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/mamutanalog.h"
#include "wav.h"

enum {
    RATE = 48000,
    DURATION_SECONDS = 9,
    FRAME_COUNT = RATE * DURATION_SECONDS,
    NOTE_ON_FRAME = RATE / 4,
    CONTROL_ON_FRAME = 2 * RATE,
    BEND_UP_FRAME = 7 * RATE / 2,
    CONTROL_OFF_FRAME = 5 * RATE,
    NOTE_OFF_FRAME = 6 * RATE,
    CHANNEL = 3,
    NOTE = 48,
    VELOCITY = 96,
};

static constexpr float MONITOR_GAIN = 0.5f;

typedef enum {
    TAKE_REFERENCE,
    TAKE_GRAVITACIJA,
    TAKE_BLOOM,
    TAKE_HEAT,
    TAKE_RUIN,
    TAKE_SWARM,
    TAKE_AFTERTOUCH,
    TAKE_MOD_WHEEL,
    TAKE_PITCH_BEND,
    TAKE_POLY_PRESSURE,
} take_kind;

typedef struct {
    take_kind kind;
    char const *name;
    char const *path;
} audition_take;

typedef struct {
    double sum;
    double sum_squares;
    float raw_peak;
    unsigned nonfinite;
    unsigned clipped;
    unsigned stereo_mismatch;
} audition_metrics;

static audition_take const TAKES[] = {
    { TAKE_REFERENCE, "reference", "build/ma1-6_reference.wav" },
    { TAKE_GRAVITACIJA, "gravitacija", "build/ma1-6_macro_gravitacija.wav" },
    { TAKE_BLOOM, "bloom", "build/ma1-6_macro_bloom.wav" },
    { TAKE_HEAT, "heat", "build/ma1-6_macro_heat.wav" },
    { TAKE_RUIN, "ruin", "build/ma1-6_macro_ruin.wav" },
    { TAKE_SWARM, "swarm", "build/ma1-6_macro_swarm.wav" },
    { TAKE_AFTERTOUCH, "aftertouch", "build/ma1-6_aftertouch.wav" },
    { TAKE_MOD_WHEEL, "mod-wheel", "build/ma1-6_mod_wheel.wav" },
    { TAKE_PITCH_BEND, "pitch-bend", "build/ma1-6_pitch_bend.wav" },
    { TAKE_POLY_PRESSURE, "poly-pressure", "build/ma1-6_poly_pressure.wav" },
};

static float output[2 * FRAME_COUNT];
static float reference[2 * FRAME_COUNT];
static float repeat_output[2 * FRAME_COUNT];

static void set_control(ma_synth *synth, take_kind kind, float value) {
    switch (kind) {
    case TAKE_REFERENCE:
        break;
    case TAKE_GRAVITACIJA:
        ma_synth_set_macro(synth, MA_MACRO_GRAVITACIJA, value);
        break;
    case TAKE_BLOOM:
        ma_synth_set_macro(synth, MA_MACRO_BLOOM, value);
        break;
    case TAKE_HEAT:
        ma_synth_set_macro(synth, MA_MACRO_HEAT, value);
        break;
    case TAKE_RUIN:
        ma_synth_set_macro(synth, MA_MACRO_RUIN, value);
        break;
    case TAKE_SWARM:
        ma_synth_set_macro(synth, MA_MACRO_SWARM, value);
        break;
    case TAKE_AFTERTOUCH:
        ma_synth_set_channel_pressure(synth, value);
        break;
    case TAKE_MOD_WHEEL:
        ma_synth_set_mod_wheel(synth, value);
        break;
    case TAKE_PITCH_BEND:
        ma_synth_set_pitch_bend(synth, value);
        break;
    case TAKE_POLY_PRESSURE:
        ma_synth_set_poly_pressure(synth, CHANNEL, NOTE, value);
        break;
    }
}

static bool render(audition_take take, float *dst,
                   audition_metrics *metrics) {
    ma_synth synth;
    ma_synth_init(&synth, RATE);
    *metrics = (audition_metrics){ 0 };

    for (int frame = 0; frame < FRAME_COUNT; frame++) {
        if (frame == NOTE_ON_FRAME)
            ma_synth_note_on(&synth, CHANNEL, NOTE, VELOCITY);
        if (frame == CONTROL_ON_FRAME)
            set_control(&synth, take.kind,
                        take.kind == TAKE_PITCH_BEND ? -2.0f : 1.0f);
        if (frame == BEND_UP_FRAME && take.kind == TAKE_PITCH_BEND)
            set_control(&synth, take.kind, 2.0f);
        if (frame == CONTROL_OFF_FRAME) set_control(&synth, take.kind, 0.0f);
        if (frame == NOTE_OFF_FRAME)
            ma_synth_note_off(&synth, CHANNEL, NOTE, 64);

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

static bool repeats(audition_take take, float const *expected) {
    audition_metrics metrics = { 0 };
    if (!render(take, repeat_output, &metrics)) return false;
    return memcmp(repeat_output, expected, sizeof repeat_output) == 0;
}

static unsigned difference_frames(float const *first, float const *second) {
    unsigned different = 0;
    for (int frame = 0; frame < FRAME_COUNT; frame++)
        different += memcmp(first + 2 * frame, second + 2 * frame,
                            2 * sizeof *first) != 0;
    return different;
}

int main(void) {
    int result = 0;
    printf("MA1-6 identity/performance audition -- 48 kHz, gain %.2f\n"
           "note %.2f s, control 2.00..5.00 s, release 6.00 s\n\n",
           MONITOR_GAIN, (double)NOTE_ON_FRAME / RATE);

    for (size_t i = 0; i < sizeof TAKES / sizeof *TAKES; i++) {
        audition_take take = TAKES[i];
        audition_metrics metrics = { 0 };
        bool valid = render(take, output, &metrics);
        unsigned changed = i == 0 ? 0 : difference_frames(reference, output);
        bool distinct = i == 0 || changed > RATE;
        uint64_t hash = tw_fnv1a64(output, sizeof output, 0);
        double mean = metrics.sum / FRAME_COUNT;
        double rms = sqrt(metrics.sum_squares / FRAME_COUNT);
        bool written = wav_write_f32(take.path, output, FRAME_COUNT,
                                     RATE, 2) == 0;
        if (i == 0) memcpy(reference, output, sizeof reference);
        bool deterministic = repeats(take, output);

        printf("%-13s peak %.6f  rms %.6f  dc %+.7f  changed %6u  "
               "FNV64 %016llx\n"
               "              finite %s  dual-mono %s  headroom %s  "
               "distinct %s  repeat %s  wav %s\n",
               take.name, metrics.raw_peak, rms, mean, changed,
               (unsigned long long)hash,
               metrics.nonfinite == 0 ? "yes" : "NO",
               metrics.stereo_mismatch == 0 ? "yes" : "NO",
               metrics.clipped == 0 ? "yes" : "NO",
               distinct ? "yes" : "NO",
               deterministic ? "yes" : "NO", written ? take.path : "FAILED");

        if (!valid || !distinct || !deterministic || !written) result = 1;
    }
    return result;
}
