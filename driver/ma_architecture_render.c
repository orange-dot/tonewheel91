#include "ma_architecture_render.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ma_dark_lead.h"
#include "wav.h"

enum {
    CONTROL_PERIOD = 64,
    COMB_COUNT = 4,
    COMB_CAPACITY = 6144,
    ALLPASS_COUNT = 2,
    ALLPASS_CAPACITY = 2560,
};

static constexpr float TWO_PI = 6.2831853071795864769f;

typedef struct {
    uint64_t start[MA_ARCH_EVENT_CAPACITY];
    uint64_t end[MA_ARCH_EVENT_CAPACITY];
} architecture_timeline;

typedef struct {
    ma_synth synth;
    size_t next_note;
    uint64_t release_frame;
    uint64_t started_frame;
    uint8_t note;
    bool sounding;
    bool started;
} architecture_voice;

typedef struct {
    float comb_l[COMB_COUNT][COMB_CAPACITY];
    float comb_r[COMB_COUNT][COMB_CAPACITY];
    float comb_filter_l[COMB_COUNT];
    float comb_filter_r[COMB_COUNT];
    unsigned comb_position_l[COMB_COUNT];
    unsigned comb_position_r[COMB_COUNT];
    unsigned comb_length_l[COMB_COUNT];
    unsigned comb_length_r[COMB_COUNT];
    float allpass_l[ALLPASS_COUNT][ALLPASS_CAPACITY];
    float allpass_r[ALLPASS_COUNT][ALLPASS_CAPACITY];
    unsigned allpass_position_l[ALLPASS_COUNT];
    unsigned allpass_position_r[ALLPASS_COUNT];
    unsigned allpass_length_l[ALLPASS_COUNT];
    unsigned allpass_length_r[ALLPASS_COUNT];
} architecture_reverb;

typedef struct {
    architecture_voice voices[MA_ARCH_LINE_COUNT];
    architecture_reverb reverb;
    const ma_arch_score *score;
    const architecture_timeline *timeline;
    unsigned rate_hz;
} architecture_renderer;

static ma_patch line_patch(unsigned line) {
    if (line <= 1) return ma_patch_dubina;
    if (line == 2) {
        ma_patch patch = ma_patch_dubina;
        patch.filter_cutoff_hz = 940.0f;
        patch.amp_adsr.release_ms = 1500.0f;
        patch.width = .44f;
        return patch;
    }
    if (line == 3) {
        ma_patch patch = ma_dark_lead_patch();
        patch.amp_adsr = (ma_adsr){ 90.0f, 720.0f, .66f, 1800.0f };
        patch.filter_adsr = (ma_adsr){ 120.0f, 900.0f, .32f, 1600.0f };
        patch.master_level = .18f;
        return patch;
    }
    if (line <= 5) {
        ma_patch patch = ma_patch_lead;
        patch.vco1.saw_level = 0.0f;
        patch.vco1.pulse_level = 0.0f;
        patch.vco1.triangle_level = .58f;
        patch.vco1.sine_level = .42f;
        patch.vco2.saw_level = 0.0f;
        patch.vco2.pulse_level = 0.0f;
        patch.vco2.triangle_level = .46f;
        patch.vco2.sine_level = .54f;
        patch.sync_amount = .02f;
        patch.crossmod_amount = .02f;
        patch.mozaik_mix = 0.0f;
        patch.filter_cutoff_hz = 1350.0f;
        patch.amp_adsr = (ma_adsr){ 14.0f, 180.0f, .52f, 360.0f };
        patch.filter_adsr = (ma_adsr){ 9.0f, 240.0f, .24f, 320.0f };
        patch.master_level = .16f;
        return patch;
    }
    if (line <= 7) {
        ma_patch patch = ma_patch_tepih;
        patch.filter_cutoff_hz = 760.0f;
        patch.amp_adsr.release_ms = 5200.0f;
        patch.filter_adsr.release_ms = 4600.0f;
        patch.master_level = .15f;
        return patch;
    }
    return ma_dark_lead_patch();
}

static unsigned scaled_length(unsigned base, unsigned rate_hz,
                              unsigned capacity) {
    uint64_t scaled = (uint64_t)base * rate_hz + 24000u;
    scaled /= 48000u;
    if (!scaled) scaled = 1;
    return scaled > capacity ? capacity : (unsigned)scaled;
}

static size_t next_note_on_line(const ma_arch_score *score, size_t first,
                                unsigned line) {
    for (size_t i = first; i < score->note_count; i++)
        if (score->notes[i].line == line) return i;
    return score->note_count;
}

static void renderer_init(architecture_renderer *renderer,
                          const ma_arch_score *score,
                          const architecture_timeline *timeline,
                          unsigned rate_hz) {
    static const unsigned comb_l[COMB_COUNT] = { 1215, 1293, 1397, 1473 };
    static const unsigned comb_r[COMB_COUNT] = { 1238, 1316, 1420, 1496 };
    static const unsigned allpass_l[ALLPASS_COUNT] = { 225, 556 };
    static const unsigned allpass_r[ALLPASS_COUNT] = { 248, 579 };
    *renderer = (architecture_renderer){
        .score = score,
        .timeline = timeline,
        .rate_hz = rate_hz,
    };
    for (unsigned line = 0; line < MA_ARCH_LINE_COUNT; line++) {
        architecture_voice *voice = &renderer->voices[line];
        ma_patch patch = line_patch(line);
        ma_synth_init_patch(&voice->synth, (float)rate_hz, &patch);
        voice->next_note = next_note_on_line(score, 0, line);
    }
    for (unsigned i = 0; i < COMB_COUNT; i++) {
        renderer->reverb.comb_length_l[i] =
            scaled_length(comb_l[i], rate_hz, COMB_CAPACITY);
        renderer->reverb.comb_length_r[i] =
            scaled_length(comb_r[i], rate_hz, COMB_CAPACITY);
    }
    for (unsigned i = 0; i < ALLPASS_COUNT; i++) {
        renderer->reverb.allpass_length_l[i] =
            scaled_length(allpass_l[i], rate_hz, ALLPASS_CAPACITY);
        renderer->reverb.allpass_length_r[i] =
            scaled_length(allpass_r[i], rate_hz, ALLPASS_CAPACITY);
    }
}

static float comb_tick(float buffer[COMB_CAPACITY], unsigned length,
                       unsigned *position, float *filtered, float input) {
    float delayed = buffer[*position];
    *filtered = .76f * delayed + .24f * *filtered;
    buffer[*position] = input + .79f * *filtered;
    *position = (*position + 1u) % length;
    return delayed;
}

static float allpass_tick(float buffer[ALLPASS_CAPACITY], unsigned length,
                          unsigned *position, float input) {
    float delayed = buffer[*position];
    float output = delayed - input;
    buffer[*position] = input + .52f * delayed;
    *position = (*position + 1u) % length;
    return output;
}

static ma_frame reverb_tick(architecture_reverb *reverb, ma_frame dry,
                            float amount) {
    float input_l = .74f * dry.left + .26f * dry.right;
    float input_r = .74f * dry.right + .26f * dry.left;
    float wet_l = 0.0f, wet_r = 0.0f;
    for (unsigned i = 0; i < COMB_COUNT; i++) {
        wet_l += comb_tick(reverb->comb_l[i], reverb->comb_length_l[i],
                           &reverb->comb_position_l[i],
                           &reverb->comb_filter_l[i], input_l);
        wet_r += comb_tick(reverb->comb_r[i], reverb->comb_length_r[i],
                           &reverb->comb_position_r[i],
                           &reverb->comb_filter_r[i], input_r);
    }
    wet_l *= .25f;
    wet_r *= .25f;
    for (unsigned i = 0; i < ALLPASS_COUNT; i++) {
        wet_l = allpass_tick(reverb->allpass_l[i],
                             reverb->allpass_length_l[i],
                             &reverb->allpass_position_l[i], wet_l);
        wet_r = allpass_tick(reverb->allpass_r[i],
                             reverb->allpass_length_r[i],
                             &reverb->allpass_position_r[i], wet_r);
    }
    return (ma_frame){
        .left = dry.left + amount * wet_l,
        .right = dry.right + amount * wet_r,
    };
}

static void start_due_note(architecture_renderer *renderer, unsigned line,
                           uint64_t frame, ma_arch_render_metrics *metrics) {
    architecture_voice *voice = &renderer->voices[line];
    while (voice->next_note < renderer->score->note_count
           && renderer->timeline->start[voice->next_note] <= frame) {
        size_t index = voice->next_note;
        const ma_arch_note *note = &renderer->score->notes[index];
        ma_synth_note_on(&voice->synth, (uint8_t)line,
                         note->note, note->velocity);
        float pressure = line >= 8 ? .46f : line >= 6 ? .20f : .28f;
        ma_synth_set_channel_pressure(&voice->synth, pressure);
        ma_synth_set_poly_pressure(&voice->synth, (uint8_t)line,
                                   note->note, pressure);
        ma_synth_set_mod_wheel(&voice->synth,
                               line >= 8 ? .22f + .35f * pressure
                               : line >= 6 ? .18f : .06f);
        voice->note = note->note;
        voice->release_frame = renderer->timeline->end[index];
        voice->started_frame = frame;
        voice->sounding = true;
        voice->started = true;
        voice->next_note = next_note_on_line(renderer->score, index + 1u, line);
        metrics->notes++;
    }
}

static void update_controls(architecture_renderer *renderer, unsigned line,
                            uint64_t frame) {
    architecture_voice *voice = &renderer->voices[line];
    if (frame % CONTROL_PERIOD) return;
    uint32_t tick = ma_arch_frame_tick(frame, renderer->rate_hz);
    for (size_t track = 0; track < renderer->score->track_count; track++) {
        const ma_arch_automation_track *automation =
            &renderer->score->tracks[track];
        if (automation->line == line)
            ma_synth_set_macro(&voice->synth, automation->macro,
                               ma_arch_automation_value(renderer->score,
                                                         track, tick));
    }
    if (line < 8 || !voice->started) return;
    float age = (float)(frame - voice->started_frame) / renderer->rate_hz;
    float entry = age < 1.8f ? -.12f * (1.0f - age / 1.8f) : 0.0f;
    float onset = age < 2.2f ? 0.0f
                : age < 3.6f ? (age - 2.2f) / 1.4f : 1.0f;
    float vibrato = onset * (.012f + .020f * .46f)
                  * sinf(TWO_PI * 3.65f * age);
    ma_synth_set_pitch_bend(&voice->synth, entry + vibrato);
}

static ma_frame render_frame(architecture_renderer *renderer, uint64_t frame,
                             ma_arch_render_metrics *metrics) {
    ma_frame mixed = { 0 };
    unsigned active = 0;
    for (unsigned line = 0; line < MA_ARCH_LINE_COUNT; line++) {
        architecture_voice *voice = &renderer->voices[line];
        if (voice->sounding && frame >= voice->release_frame) {
            ma_synth_note_off(&voice->synth, (uint8_t)line, voice->note, 0);
            voice->sounding = false;
        }
        start_due_note(renderer, line, frame, metrics);
        update_controls(renderer, line, frame);
        ma_frame sample = ma_synth_tick(&voice->synth);
        if (!isfinite(sample.left) || !isfinite(sample.right)) {
            metrics->nonfinite++;
            sample = (ma_frame){ 0 };
        }
        float pan = ma_arch_lines[line].pan;
        float left = sqrtf(.5f * (1.0f - pan));
        float right = sqrtf(.5f * (1.0f + pan));
        float gain = ma_arch_lines[line].gain;
        mixed.left += gain * left * sample.left;
        mixed.right += gain * right * sample.right;
        active += voice->synth.amp_envelope.stage != MA_ENVELOPE_IDLE;
    }
    if (active > metrics->peak_voices) metrics->peak_voices = active;
    uint32_t tick = ma_arch_frame_tick(frame, renderer->rate_hz);
    float wet = ma_arch_sections[ma_arch_section_at_tick(tick)].reverb_wet;
    ma_frame output = reverb_tick(&renderer->reverb, mixed, wet);
    output.left *= .70f;
    output.right *= .70f;
    return output;
}

static bool render_pass(const ma_arch_score *score,
                        const architecture_timeline *timeline,
                        uint64_t frame_count, unsigned rate_hz,
                        wav_f32_writer *writer, ma_arch_render_metrics *metrics) {
    architecture_renderer renderer;
    float block[2 * MA_ARCH_BLOCK_FRAMES];
    renderer_init(&renderer, score, timeline, rate_hz);
    *metrics = (ma_arch_render_metrics){ .frames = frame_count };
    for (uint64_t first = 0; first < frame_count;) {
        size_t frames = frame_count - first > MA_ARCH_BLOCK_FRAMES
                      ? MA_ARCH_BLOCK_FRAMES : (size_t)(frame_count - first);
        for (size_t i = 0; i < frames; i++) {
            ma_frame sample = render_frame(&renderer, first + i, metrics);
            if (!isfinite(sample.left) || !isfinite(sample.right)) {
                metrics->nonfinite++;
                sample = (ma_frame){ 0 };
            }
            float left = fabsf(sample.left), right = fabsf(sample.right);
            float peak = left > right ? left : right;
            if (peak > metrics->peak) metrics->peak = peak;
            if (peak > 1.0f) metrics->clipped++;
            metrics->sum_squares += (double)sample.left * sample.left
                                  + (double)sample.right * sample.right;
            block[2u * i] = sample.left;
            block[2u * i + 1u] = sample.right;
        }
        metrics->hash = tw_fnv1a64(block, 2u * frames * sizeof block[0],
                                   metrics->hash);
        if (writer && wav_f32_write(writer, block, frames) < 0) return false;
        first += frames;
    }
    return metrics->nonfinite == 0 && metrics->clipped == 0
        && metrics->peak_voices <= MA_ARCH_LINE_COUNT;
}

static bool metrics_equal(const ma_arch_render_metrics *a,
                          const ma_arch_render_metrics *b) {
    return a->hash == b->hash && a->frames == b->frames
        && a->sum_squares == b->sum_squares && a->peak == b->peak
        && a->notes == b->notes && a->peak_voices == b->peak_voices
        && a->nonfinite == b->nonfinite && a->clipped == b->clipped;
}

bool ma_arch_duration_frames(double seconds, unsigned rate_hz,
                             uint64_t *frames) {
    if (!frames || !isfinite(seconds) || !(seconds > 0.0)
        || seconds > MA_ARCH_MAX_SECONDS
        || rate_hz < 44100u || rate_hz > 192000u)
        return false;
    double count = floor(seconds * rate_hz + .5);
    if (!(count > 0.0) || count > (double)UINT32_MAX
        || count * 8.0 > (double)(UINT32_MAX - 48u))
        return false;
    *frames = (uint64_t)count;
    return true;
}

static void make_timeline(const ma_arch_score *score, unsigned rate_hz,
                          architecture_timeline *timeline) {
    for (size_t i = 0; i < score->note_count; i++) {
        uint64_t start_us = ma_arch_tick_us(score->notes[i].start_tick);
        uint64_t end_us = ma_arch_tick_us(score->notes[i].end_tick);
        timeline->start[i] = (start_us * rate_hz + 500000u) / 1000000u;
        timeline->end[i] = (end_us * rate_hz + 500000u) / 1000000u;
    }
}

static int fail(const char **reason, const char *message) {
    if (reason) *reason = message;
    return -1;
}

int ma_arch_render_file(const ma_arch_score *score, const char *path,
                        double seconds, unsigned rate_hz,
                        ma_arch_render_result *result, const char **reason) {
    uint64_t frames = 0;
    const char *validation = 0;
    if (!score || !path || !*path || !result
        || !ma_arch_duration_frames(seconds, rate_hz, &frames))
        return fail(reason, "invalid render arguments");
    if (!ma_arch_score_validate(score, &validation))
        return fail(reason, validation ? validation : "invalid score");
    architecture_timeline timeline;
    make_timeline(score, rate_hz, &timeline);
    *result = (ma_arch_render_result){ 0 };
    if (!render_pass(score, &timeline, frames, rate_hz, 0,
                     &result->first_pass))
        return fail(reason, "first render pass failed safety checks");

    size_t path_size = strlen(path);
    if (path_size > SIZE_MAX - sizeof ".ma-tmp")
        return fail(reason, "output path is too long");
    char *temporary = malloc(path_size + sizeof ".ma-tmp");
    if (!temporary) return fail(reason, "could not allocate temporary path");
    memcpy(temporary, path, path_size);
    memcpy(temporary + path_size, ".ma-tmp", sizeof ".ma-tmp");
    wav_f32_writer writer = { 0 };
    if (wav_f32_open(&writer, temporary, (size_t)frames, rate_hz, 2) < 0) {
        free(temporary);
        return fail(reason, "could not open temporary WAV");
    }
    bool rendered = render_pass(score, &timeline, frames, rate_hz, &writer,
                                &result->second_pass);
    bool closed = rendered && wav_f32_close(&writer) == 0;
    if (!closed) {
        wav_f32_abort(&writer);
        (void)remove(temporary);
        free(temporary);
        return fail(reason, "second render pass or WAV close failed");
    }
    if (!metrics_equal(&result->first_pass, &result->second_pass)) {
        (void)remove(temporary);
        free(temporary);
        return fail(reason, "render passes differ");
    }
    if (rename(temporary, path) < 0) {
        (void)remove(temporary);
        free(temporary);
        return fail(reason, "could not publish completed WAV");
    }
    free(temporary);
    if (reason) *reason = 0;
    return 0;
}
