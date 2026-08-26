/* Hosted Mamut Analog performance study: a slow F-sharp-minor blues
 * improvisation for Lead, Tepih and Dubina. It is a listening exhibit, not
 * an MA2 polyphony prototype and not a transcription of a copyrighted score.
 * The small fixed overdub desk exists only to let the MA1 voice play a whole
 * arrangement before the real five-card allocator lands. */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/mamutanalog.h"
#include "wav.h"

enum {
    RATE = 48000,
    PERFORMANCE_SECONDS = 168,
    TAIL_SECONDS = 12,
    FRAME_COUNT = RATE * (PERFORMANCE_SECONDS + TAIL_SECONDS),
    PAD_VOICES = 8,
    BASS_VOICES = 2,
    LEAD_VOICES = 4,
    VOICE_COUNT = PAD_VOICES + BASS_VOICES + LEAD_VOICES,
    CONTROL_PERIOD = 64,
    COMB_COUNT = 4,
    COMB_CAPACITY = 1536,
    ALLPASS_COUNT = 2,
    ALLPASS_CAPACITY = 640,
};

static constexpr char OUTPUT_PATH[] = "build/ma_blade_runner_blues.wav";
static constexpr float TWO_PI = 6.2831853071795864769f;

typedef enum {
    ROLE_PAD,
    ROLE_BASS,
    ROLE_LEAD,
} voice_role;

typedef struct {
    float at_seconds;
    float held_seconds;
    float pan;
    float pressure;
    uint8_t note;
    uint8_t velocity;
} note_event;

typedef struct {
    ma_synth synth;
    size_t started_at;
    size_t release_at;
    float left_gain;
    float right_gain;
    float pressure;
    uint8_t note;
    voice_role role;
    bool active;
    bool released;
} performance_voice;

typedef struct {
    float comb_l[COMB_COUNT][COMB_CAPACITY];
    float comb_r[COMB_COUNT][COMB_CAPACITY];
    float comb_filter_l[COMB_COUNT];
    float comb_filter_r[COMB_COUNT];
    unsigned comb_position_l[COMB_COUNT];
    unsigned comb_position_r[COMB_COUNT];
    float allpass_l[ALLPASS_COUNT][ALLPASS_CAPACITY];
    float allpass_r[ALLPASS_COUNT][ALLPASS_CAPACITY];
    unsigned allpass_position_l[ALLPASS_COUNT];
    unsigned allpass_position_r[ALLPASS_COUNT];
} performance_reverb;

typedef struct {
    double sum_squares;
    float peak;
    unsigned notes;
    unsigned steals;
    unsigned nonfinite;
    unsigned clipped;
    unsigned peak_voices;
} performance_metrics;

static const note_event PAD_EVENTS[] = {
    {   0, 21, -.72f, .18f, 42, 76 },
    {   0, 21, -.24f, .20f, 49, 70 },
    {   0, 21,  .24f, .22f, 52, 72 },
    {   0, 21,  .72f, .24f, 57, 68 },
    {  18, 21, -.68f, .20f, 38, 72 },
    {  18, 21, -.22f, .18f, 45, 68 },
    {  18, 21,  .22f, .24f, 49, 72 },
    {  18, 21,  .68f, .20f, 54, 70 },
    {  36, 21, -.72f, .24f, 40, 74 },
    {  36, 21, -.24f, .20f, 47, 68 },
    {  36, 21,  .24f, .18f, 54, 70 },
    {  36, 21,  .72f, .22f, 56, 66 },
    {  54, 21, -.70f, .22f, 35, 76 },
    {  54, 21, -.23f, .20f, 42, 70 },
    {  54, 21,  .23f, .24f, 45, 72 },
    {  54, 21,  .70f, .18f, 50, 68 },
    {  72, 21, -.68f, .20f, 38, 74 },
    {  72, 21, -.22f, .24f, 45, 68 },
    {  72, 21,  .22f, .20f, 49, 70 },
    {  72, 21,  .68f, .22f, 54, 72 },
    {  90, 21, -.72f, .24f, 37, 76 },
    {  90, 21, -.24f, .18f, 44, 68 },
    {  90, 21,  .24f, .22f, 47, 72 },
    {  90, 21,  .72f, .25f, 54, 70 },
    { 108, 21, -.70f, .20f, 42, 78 },
    { 108, 21, -.23f, .22f, 49, 70 },
    { 108, 21,  .23f, .24f, 52, 74 },
    { 108, 21,  .70f, .20f, 57, 70 },
    { 126, 21, -.68f, .22f, 40, 74 },
    { 126, 21, -.22f, .20f, 47, 68 },
    { 126, 21,  .22f, .18f, 54, 70 },
    { 126, 21,  .68f, .24f, 56, 72 },
    { 144, 20, -.72f, .24f, 42, 78 },
    { 144, 20, -.24f, .22f, 49, 72 },
    { 144, 20,  .24f, .20f, 52, 74 },
    { 144, 20,  .72f, .26f, 57, 72 },
};

static const note_event BASS_EVENTS[] = {
    {   0, 19.5f, 0, .18f, 42, 92 },
    {  18, 19.5f, 0, .20f, 38, 88 },
    {  36, 19.5f, 0, .22f, 40, 90 },
    {  54, 19.5f, 0, .24f, 35, 94 },
    {  72, 19.5f, 0, .20f, 38, 90 },
    {  90, 19.5f, 0, .26f, 37, 96 },
    { 108, 19.5f, 0, .24f, 42, 98 },
    { 126, 19.5f, 0, .22f, 40, 92 },
    { 144, 20.0f, 0, .28f, 42, 100 },
};

static const note_event LEAD_EVENTS[] = {
    {  16.0f, 7.0f, -.08f, .34f, 54,  96 },
    {  31.5f, 6.5f,  .04f, .40f, 57, 100 },
    {  49.0f, 8.0f, -.03f, .44f, 61, 104 },
    {  69.5f, 6.0f,  .06f, .38f, 59, 100 },
    {  88.0f, 8.5f, -.05f, .46f, 62, 106 },
    { 109.5f, 7.0f,  .03f, .42f, 57, 102 },
    { 128.0f, 8.0f, -.04f, .48f, 61, 108 },
    { 148.0f, 8.5f,  .05f, .52f, 64, 110 },
    { 160.0f, 6.5f, -.03f, .56f, 61, 108 },
};

static float output[2 * FRAME_COUNT];

static ma_patch performance_patch(voice_role role) {
    ma_patch patch = role == ROLE_PAD ? ma_patch_tepih
                   : role == ROLE_BASS ? ma_patch_dubina
                   : ma_patch_lead;
    if (role == ROLE_PAD) {
        patch.filter_cutoff_hz = 720.0f;
        patch.amp_adsr.release_ms = 6200.0f;
        patch.filter_adsr.release_ms = 5200.0f;
        patch.width = .86f;
        patch.master_level = .15f;
    } else if (role == ROLE_BASS) {
        patch.filter_cutoff_hz = 520.0f;
        patch.amp_adsr.release_ms = 2200.0f;
        patch.master_level = .16f;
    } else if (role == ROLE_LEAD) {
        patch.vco1.saw_level = .04f;
        patch.vco1.pulse_level = 0.0f;
        patch.vco1.triangle_level = .34f;
        patch.vco1.sine_level = .52f;
        patch.vco2.saw_level = .03f;
        patch.vco2.pulse_level = 0.0f;
        patch.vco2.triangle_level = .31f;
        patch.vco2.sine_level = .49f;
        patch.vco2_level = .68f;
        patch.sync_amount = .055f;
        patch.crossmod_amount = .035f;
        patch.noise_level = 0.0f;
        patch.mozaik_mix = 0.0f;
        patch.mixer_pressure = .05f;
        patch.filter_cutoff_hz = 880.0f;
        patch.filter_resonance = .18f;
        patch.filter_drive = .085f;
        patch.filter_env_amount = .18f;
        patch.amp_adsr = (ma_adsr){ 520.0f, 1800.0f, .78f, 9000.0f };
        patch.filter_adsr = (ma_adsr){ 800.0f, 2200.0f, .38f, 7800.0f };
        patch.macro[MA_MACRO_GRAVITACIJA] = .06f;
        patch.macro[MA_MACRO_BLOOM] = .10f;
        patch.macro[MA_MACRO_HEAT] = 0.0f;
        patch.macro[MA_MACRO_RUIN] = 0.0f;
        patch.macro[MA_MACRO_SWARM] = 0.0f;
        patch.body_drive = .045f;
        patch.width = .58f;
        patch.crossfeed = .20f;
        patch.master_level = .20f;
    }
    return patch;
}

static void voice_range(voice_role role, unsigned *first, unsigned *count) {
    if (role == ROLE_PAD) {
        *first = 0;
        *count = PAD_VOICES;
    } else if (role == ROLE_BASS) {
        *first = PAD_VOICES;
        *count = BASS_VOICES;
    } else {
        *first = PAD_VOICES + BASS_VOICES;
        *count = LEAD_VOICES;
    }
}

static size_t at_frame(float seconds) {
    return (size_t)(seconds * RATE + .5f);
}

static performance_voice *start_note(performance_voice voices[VOICE_COUNT],
                                     voice_role role, note_event event,
                                     size_t frame, performance_metrics *metrics) {
    unsigned first = 0, count = 0;
    voice_range(role, &first, &count);
    performance_voice *choice = 0;
    for (unsigned i = first; i < first + count; i++) {
        if (!voices[i].active) {
            choice = &voices[i];
            break;
        }
    }
    if (!choice) {
        choice = &voices[first];
        for (unsigned i = first + 1; i < first + count; i++)
            if (voices[i].started_at < choice->started_at)
                choice = &voices[i];
        metrics->steals++;
    }

    ma_patch patch = performance_patch(role);
    ma_synth_init_patch(&choice->synth, RATE, &patch);
    float pan = event.pan < -1 ? -1 : event.pan > 1 ? 1 : event.pan;
    choice->left_gain = sqrtf(.5f * (1.0f - pan));
    choice->right_gain = sqrtf(.5f * (1.0f + pan));
    choice->started_at = frame;
    choice->release_at = frame + at_frame(event.held_seconds);
    choice->pressure = event.pressure;
    choice->note = event.note;
    choice->role = role;
    choice->active = true;
    choice->released = false;
    ma_synth_set_channel_pressure(&choice->synth, event.pressure);
    ma_synth_set_mod_wheel(&choice->synth,
                           role == ROLE_LEAD ? .22f + .35f * event.pressure
                           : role == ROLE_PAD ? .18f : .06f);
    ma_synth_note_on(&choice->synth, (uint8_t)role, event.note, event.velocity);
    ma_synth_set_poly_pressure(&choice->synth, (uint8_t)role, event.note,
                               event.pressure);
    metrics->notes++;
    return choice;
}

static void start_due_events(const note_event *events, size_t count,
                             size_t *next, voice_role role, size_t frame,
                             performance_voice voices[VOICE_COUNT],
                             performance_metrics *metrics) {
    while (*next < count && at_frame(events[*next].at_seconds) <= frame) {
        (void)start_note(voices, role, events[*next], frame, metrics);
        (*next)++;
    }
}

static float role_gain(voice_role role) {
    if (role == ROLE_PAD) return .31f;
    if (role == ROLE_BASS) return .64f;
    return .78f;
}

static void update_lead_expression(performance_voice *voice, size_t frame) {
    if (voice->role != ROLE_LEAD || frame % CONTROL_PERIOD) return;
    float age = (float)(frame - voice->started_at) / RATE;
    float entry = age < 1.8f ? -.12f * (1.0f - age / 1.8f) : 0;
    float onset = age < 2.2f ? 0 : age < 3.6f ? (age - 2.2f) / 1.4f : 1;
    float vibrato = onset * (.012f + .020f * voice->pressure)
                  * sinf(TWO_PI * 3.65f * age);
    ma_synth_set_pitch_bend(&voice->synth, entry + vibrato);
}

static ma_frame render_voices(performance_voice voices[VOICE_COUNT],
                              size_t frame, performance_metrics *metrics) {
    ma_frame mixed = { 0 };
    unsigned active = 0;
    for (unsigned i = 0; i < VOICE_COUNT; i++) {
        performance_voice *voice = &voices[i];
        if (!voice->active) continue;
        if (!voice->released && frame >= voice->release_at) {
            ma_synth_note_off(&voice->synth, (uint8_t)voice->role,
                              voice->note, 0);
            voice->released = true;
        }
        update_lead_expression(voice, frame);
        ma_frame sample = ma_synth_tick(&voice->synth);
        if (!isfinite(sample.left) || !isfinite(sample.right)) {
            metrics->nonfinite++;
            sample = (ma_frame){ 0 };
        }
        float gain = role_gain(voice->role);
        mixed.left += gain * sample.left * voice->left_gain;
        mixed.right += gain * sample.right * voice->right_gain;
        if (voice->released
            && voice->synth.amp_envelope.stage == MA_ENVELOPE_IDLE)
            voice->active = false;
        else
            active++;
    }
    if (active > metrics->peak_voices) metrics->peak_voices = active;
    return mixed;
}

static float comb_tick(float buffer[COMB_CAPACITY], unsigned length,
                       unsigned *position, float *filtered, float input) {
    float delayed = buffer[*position];
    *filtered = .76f * delayed + .24f * *filtered;
    buffer[*position] = input + .79f * *filtered;
    *position = (*position + 1) % length;
    return delayed;
}

static float allpass_tick(float buffer[ALLPASS_CAPACITY], unsigned length,
                          unsigned *position, float input) {
    float delayed = buffer[*position];
    float output_sample = delayed - input;
    buffer[*position] = input + .52f * delayed;
    *position = (*position + 1) % length;
    return output_sample;
}

static ma_frame reverb_tick(performance_reverb *reverb, ma_frame dry) {
    static const unsigned COMB_LENGTH_L[COMB_COUNT] = {
        1215, 1293, 1397, 1473,
    };
    static const unsigned COMB_LENGTH_R[COMB_COUNT] = {
        1238, 1316, 1420, 1496,
    };
    static const unsigned ALLPASS_LENGTH_L[ALLPASS_COUNT] = { 225, 556 };
    static const unsigned ALLPASS_LENGTH_R[ALLPASS_COUNT] = { 248, 579 };
    float input_l = .74f * dry.left + .26f * dry.right;
    float input_r = .74f * dry.right + .26f * dry.left;
    float wet_l = 0, wet_r = 0;
    for (unsigned i = 0; i < COMB_COUNT; i++) {
        wet_l += comb_tick(reverb->comb_l[i], COMB_LENGTH_L[i],
                           &reverb->comb_position_l[i],
                           &reverb->comb_filter_l[i], input_l);
        wet_r += comb_tick(reverb->comb_r[i], COMB_LENGTH_R[i],
                           &reverb->comb_position_r[i],
                           &reverb->comb_filter_r[i], input_r);
    }
    wet_l *= .25f;
    wet_r *= .25f;
    for (unsigned i = 0; i < ALLPASS_COUNT; i++) {
        wet_l = allpass_tick(reverb->allpass_l[i], ALLPASS_LENGTH_L[i],
                             &reverb->allpass_position_l[i], wet_l);
        wet_r = allpass_tick(reverb->allpass_r[i], ALLPASS_LENGTH_R[i],
                             &reverb->allpass_position_r[i], wet_r);
    }
    return (ma_frame){
        .left = .72f * dry.left + .48f * wet_l,
        .right = .72f * dry.right + .48f * wet_r,
    };
}

static bool render(float dst[2 * FRAME_COUNT], performance_metrics *metrics) {
    performance_voice voices[VOICE_COUNT] = { 0 };
    performance_reverb reverb = { 0 };
    size_t next_pad = 0, next_bass = 0, next_lead = 0;
    *metrics = (performance_metrics){ 0 };

    for (size_t frame = 0; frame < FRAME_COUNT; frame++) {
        start_due_events(PAD_EVENTS, sizeof PAD_EVENTS / sizeof *PAD_EVENTS,
                         &next_pad, ROLE_PAD, frame, voices, metrics);
        start_due_events(BASS_EVENTS, sizeof BASS_EVENTS / sizeof *BASS_EVENTS,
                         &next_bass, ROLE_BASS, frame, voices, metrics);
        start_due_events(LEAD_EVENTS, sizeof LEAD_EVENTS / sizeof *LEAD_EVENTS,
                         &next_lead, ROLE_LEAD, frame, voices, metrics);
        ma_frame sample = reverb_tick(&reverb,
                                      render_voices(voices, frame, metrics));
        sample.left *= 1.12f;
        sample.right *= 1.12f;
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
        dst[2 * frame] = sample.left;
        dst[2 * frame + 1] = sample.right;
    }
    return metrics->nonfinite == 0 && metrics->clipped == 0
        && metrics->peak > 1e-4f && metrics->sum_squares > 1e-8;
}

int main(void) {
    puts("Mamut Analog — F# minor Blade Runner blues performance study");
    puts("  hosted MA1 overdub; not the future MA2 allocator");

    performance_metrics first = { 0 }, second = { 0 };
    bool first_ok = render(output, &first);
    uint64_t first_hash = tw_fnv1a64(output, sizeof output, 0);
    bool second_ok = render(output, &second);
    uint64_t second_hash = tw_fnv1a64(output, sizeof output, 0);
    bool deterministic = first_hash == second_hash;
    bool written = first_ok && second_ok && deterministic
                && wav_write_f32(OUTPUT_PATH, output, FRAME_COUNT, RATE, 2) == 0;
    double rms = sqrt(second.sum_squares / (2.0 * FRAME_COUNT));

    printf("  %.0f s + %d s tail, %u notes, %u-voice peak, %u steals\n",
           (double)PERFORMANCE_SECONDS, TAIL_SECONDS, second.notes,
           second.peak_voices, second.steals);
    printf("  peak %.6f, RMS %.6f, finite %s, headroom %s\n",
           (double)second.peak, rms,
           second.nonfinite == 0 ? "yes" : "NO",
           second.clipped == 0 ? "yes" : "NO");
    printf("  FNV64 %016llx %s\n", (unsigned long long)second_hash,
           deterministic ? "(two runs identical)" : "MISMATCH");
    printf("  wav: %s%s\n", OUTPUT_PATH, written ? "" : " (FAILED)");
    return written ? 0 : 1;
}
