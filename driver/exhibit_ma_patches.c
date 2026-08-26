/* Hosted MA1-6R listening evidence for the compiled bank and the VCO1
 * Mamut-sine contribution. */
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
    TAKE_TEPIH,
    TAKE_LEAD,
    TAKE_DUBINA,
    TAKE_SINE_OFF,
    TAKE_SINE_ON,
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
    { TAKE_TEPIH, "tepih", "build/ma1-6r_tepih.wav" },
    { TAKE_LEAD, "lead", "build/ma1-6r_lead.wav" },
    { TAKE_DUBINA, "dubina", "build/ma1-6r_dubina.wav" },
    { TAKE_SINE_OFF, "sine-off", "build/ma1-6r_tepih_sine_off.wav" },
    { TAKE_SINE_ON, "sine-on", "build/ma1-6r_tepih_sine_on.wav" },
};

static float output[2 * FRAME_COUNT];
static float reference[2 * FRAME_COUNT];
static float sine_off[2 * FRAME_COUNT];
static float repeat_output[2 * FRAME_COUNT];

static void configure(ma_synth *synth, take_kind kind) {
    ma_patch patch = kind == TAKE_LEAD ? ma_patch_lead
                   : kind == TAKE_DUBINA ? ma_patch_dubina
                   : ma_patch_tepih;
    if (kind == TAKE_SINE_OFF) patch.vco1.sine_level = 0.0f;
    ma_synth_init_patch(synth, RATE, &patch);
}

static void dubina_script(ma_synth *synth, int frame) {
    if (frame == RATE / 2) ma_synth_note_on(synth, 0, 36, 100);
    if (frame == 5 * RATE) ma_synth_set_channel_pressure(synth, .45f);
    if (frame == 6 * RATE) ma_synth_set_mod_wheel(synth, .60f);
    if (frame == 8 * RATE) ma_synth_set_poly_pressure(synth, 0, 36, .70f);
    if (frame == 10 * RATE) ma_synth_note_off(synth, 0, 36, 0);
}

static void tepih_script(ma_synth *synth, int frame) {
    if (frame == RATE / 4) ma_synth_note_on(synth, 0, 48, 78);
    if (frame == 4 * RATE + RATE / 4)
        ma_synth_note_off(synth, 0, 48, 0);
    if (frame == 7 * RATE + RATE / 4)
        ma_synth_note_on(synth, 0, 55, 96);
    if (frame == 10 * RATE + 3 * RATE / 4)
        ma_synth_note_off(synth, 0, 55, 0);
}

static void sine_script(ma_synth *synth, int frame) {
    if (frame == RATE / 2) ma_synth_note_on(synth, 0, 48, 92);
    if (frame == 6 * RATE + RATE / 2)
        ma_synth_note_off(synth, 0, 48, 0);
}

static void lead_script(ma_synth *synth, int frame) {
    static const struct {
        int frame;
        uint8_t note;
        bool on;
    } events[] = {
        { RATE / 2, 60, true }, { RATE + RATE / 10, 60, false },
        { RATE + RATE / 4, 63, true }, { 2 * RATE, 63, false },
        { 2 * RATE + RATE / 8, 67, true },
        { 3 * RATE + RATE / 8, 67, false },
        { 3 * RATE + RATE / 2, 70, true },
        { 4 * RATE + RATE / 4, 70, false },
        { 4 * RATE + RATE / 2, 72, true },
        { 6 * RATE + RATE / 2, 72, false },
        { 7 * RATE, 67, true }, { 8 * RATE, 67, false },
    };
    for (size_t i = 0; i < sizeof events / sizeof *events; i++) {
        if (events[i].frame != frame) continue;
        if (events[i].on)
            ma_synth_note_on(synth, 0, events[i].note, 112);
        else
            ma_synth_note_off(synth, 0, events[i].note, 0);
    }
    if (frame == 2 * RATE + RATE / 2) ma_synth_set_mod_wheel(synth, 0.55f);
    if (frame == 4 * RATE) ma_synth_set_channel_pressure(synth, 0.50f);
    if (frame == 5 * RATE) ma_synth_set_pitch_bend(synth, 2.0f);
    if (frame == 5 * RATE + RATE / 2)
        ma_synth_set_pitch_bend(synth, -2.0f);
    if (frame == 6 * RATE) ma_synth_set_pitch_bend(synth, 0.0f);
    if (frame == 7 * RATE + RATE / 2) {
        ma_synth_set_mod_wheel(synth, 0.0f);
        ma_synth_set_channel_pressure(synth, 0.0f);
    }
}

static bool render(audition_take take, float *dst,
    audition_metrics *metrics) {
    ma_synth synth;
    configure(&synth, take.kind);
    *metrics = (audition_metrics){ 0 };

    for (int frame = 0; frame < FRAME_COUNT; frame++) {
        if (take.kind == TAKE_LEAD)
            lead_script(&synth, frame);
        else if (take.kind == TAKE_DUBINA)
            dubina_script(&synth, frame);
        else if (take.kind == TAKE_TEPIH)
            tepih_script(&synth, frame);
        else
            sine_script(&synth, frame);

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

static unsigned difference_frames(float const *first, float const *second) {
    unsigned different = 0;
    for (int frame = 0; frame < FRAME_COUNT; frame++)
        different += memcmp(first + 2 * frame, second + 2 * frame,
                            2 * sizeof *first) != 0;
    return different;
}

int main(void) {
    int result = 0;
    puts("MA1-6R compiled patch bank and Mamut sine audition\n");
    for (size_t i = 0; i < sizeof TAKES / sizeof *TAKES; i++) {
        audition_take take = TAKES[i];
        audition_metrics metrics = { 0 };
        bool valid = render(take, output, &metrics);
        bool deterministic = render(take, repeat_output,
                                    &(audition_metrics){ 0 })
                          && memcmp(output, repeat_output, sizeof output) == 0;
        uint64_t hash = tw_fnv1a64(output, sizeof output, 0);
        double mean = metrics.sum / FRAME_COUNT;
        double rms = sqrt(metrics.sum_squares / FRAME_COUNT);
        bool written = wav_write_f32(take.path, output, FRAME_COUNT,
                                     RATE, 2) == 0;
        unsigned changed = i == 0 ? 0
                         : difference_frames(reference, output);
        if (i == 0) memcpy(reference, output, sizeof reference);
        if (take.kind == TAKE_SINE_OFF)
            memcpy(sine_off, output, sizeof sine_off);
        if (take.kind == TAKE_SINE_ON) {
            unsigned sine_changed = difference_frames(sine_off, output);
            printf("sine A/B changed frames: %u\n", sine_changed);
            valid = valid && sine_changed > RATE;
        }

        printf("%-8s peak %.6f  rms %.6f  dc %+.7f  changed %6u  "
               "FNV64 %016llx\n"
               "         finite %s  dual-mono %s  headroom %s  repeat %s  "
               "wav %s\n",
               take.name, metrics.raw_peak, rms, mean, changed,
               (unsigned long long)hash,
               metrics.nonfinite == 0 ? "yes" : "NO",
               metrics.stereo_mismatch == 0 ? "yes" : "NO",
               metrics.clipped == 0 ? "yes" : "NO",
               deterministic ? "yes" : "NO",
               written ? take.path : "FAILED");
        if (!valid || !deterministic || !written) result = 1;
    }
    return result;
}
