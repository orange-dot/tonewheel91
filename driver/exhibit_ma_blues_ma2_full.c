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
#include "ma_dark_lead.h"
#include "wav.h"

enum {
    RATE = 48000,
    PERFORMANCE_SECONDS = 234,
    TAIL_SECONDS = 14,
    FRAME_COUNT = RATE * (PERFORMANCE_SECONDS + TAIL_SECONDS),
    PAD_DARK_VOICES = 8,
    PAD_HAZE_VOICES = 6,
    BASS_ROOT_VOICES = 2,
    BASS_MOTION_VOICES = 3,
    LEAD_VOICES = 4,
    VOICE_COUNT = PAD_DARK_VOICES + PAD_HAZE_VOICES
                + BASS_ROOT_VOICES + BASS_MOTION_VOICES + LEAD_VOICES,
    BLOCK_FRAMES = 4096,
    CONTROL_PERIOD = 64,
    COMB_COUNT = 4,
    COMB_CAPACITY = 1536,
    ALLPASS_COUNT = 2,
    ALLPASS_CAPACITY = 640,
};

static constexpr char OUTPUT_PATH[] =
    "build/ma_blade_runner_blues_ma2_full.wav";
static constexpr char TEMP_OUTPUT_PATH[] =
    "build/ma_blade_runner_blues_ma2_full.wav.ma-tmp";
static constexpr float TWO_PI = 6.2831853071795864769f;

typedef enum {
    ROLE_PAD_DARK,
    ROLE_PAD_HAZE,
    ROLE_BASS_ROOT,
    ROLE_BASS_MOTION,
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
    uint64_t hash;
    double sum_squares;
    float peak;
    unsigned notes;
    unsigned steals;
    unsigned nonfinite;
    unsigned clipped;
    unsigned peak_voices;
} performance_metrics;

static const note_event PAD_DARK_EVENTS[] = {
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
    { 144, 21, -.72f, .24f, 42, 78 },
    { 144, 21, -.24f, .22f, 49, 72 },
    { 144, 21,  .24f, .20f, 52, 74 },
    { 144, 21,  .72f, .26f, 57, 72 },
    { 162, 21, -.68f, .22f, 37, 76 },
    { 162, 21, -.22f, .20f, 44, 70 },
    { 162, 21,  .22f, .24f, 49, 74 },
    { 162, 21,  .68f, .20f, 52, 70 },
    { 180, 21, -.72f, .24f, 38, 78 },
    { 180, 21, -.24f, .20f, 45, 70 },
    { 180, 21,  .24f, .22f, 50, 72 },
    { 180, 21,  .72f, .24f, 54, 70 },
    { 198, 21, -.70f, .20f, 40, 76 },
    { 198, 21, -.23f, .22f, 47, 70 },
    { 198, 21,  .23f, .24f, 52, 74 },
    { 198, 21,  .70f, .20f, 56, 70 },
    { 216, 18, -.72f, .24f, 42, 80 },
    { 216, 18, -.24f, .22f, 49, 74 },
    { 216, 18,  .24f, .20f, 52, 76 },
    { 216, 18,  .72f, .26f, 57, 74 },
};

static const note_event PAD_HAZE_EVENTS[] = {
    {   6, 12, -.48f, .14f, 49, 60 }, {   6, 12,  .48f, .18f, 57, 58 },
    {  24, 12, -.44f, .18f, 45, 62 }, {  24, 12,  .44f, .14f, 54, 58 },
    {  42, 12, -.50f, .16f, 47, 60 }, {  42, 12,  .50f, .20f, 56, 62 },
    {  60, 12, -.46f, .20f, 42, 64 }, {  60, 12,  .46f, .16f, 50, 60 },
    {  78, 12, -.52f, .14f, 45, 60 }, {  78, 12,  .52f, .18f, 54, 62 },
    {  96, 12, -.48f, .18f, 44, 62 }, {  96, 12,  .48f, .14f, 52, 58 },
    { 114, 12, -.44f, .16f, 49, 64 }, { 114, 12,  .44f, .20f, 57, 60 },
    { 132, 12, -.50f, .20f, 47, 62 }, { 132, 12,  .50f, .16f, 56, 60 },
    { 150, 12, -.46f, .14f, 49, 64 }, { 150, 12,  .46f, .18f, 57, 62 },
    { 168, 12, -.52f, .18f, 44, 62 }, { 168, 12,  .52f, .14f, 52, 60 },
    { 186, 12, -.48f, .16f, 45, 64 }, { 186, 12,  .48f, .20f, 54, 62 },
    { 204, 12, -.44f, .20f, 47, 64 }, { 204, 12,  .44f, .16f, 56, 62 },
    { 222, 10, -.50f, .18f, 49, 66 }, { 222, 10,  .50f, .22f, 57, 64 },
};

static const note_event BASS_ROOT_EVENTS[] = {
    {   0, 19.5f, 0, .18f, 42, 92 },
    {  18, 19.5f, 0, .20f, 38, 88 },
    {  36, 19.5f, 0, .22f, 40, 90 },
    {  54, 19.5f, 0, .24f, 35, 94 },
    {  72, 19.5f, 0, .20f, 38, 90 },
    {  90, 19.5f, 0, .26f, 37, 96 },
    { 108, 19.5f, 0, .24f, 42, 98 },
    { 126, 19.5f, 0, .22f, 40, 92 },
    { 144, 19.5f, 0, .28f, 42, 100 },
    { 162, 19.5f, 0, .24f, 37, 96 },
    { 180, 19.5f, 0, .26f, 38, 98 },
    { 198, 19.5f, 0, .24f, 40, 96 },
    { 216, 18.0f, 0, .30f, 42, 102 },
};

static const note_event BASS_MOTION_EVENTS[] = {
    {  10, 5.0f, -.18f, .30f, 42, 78 },
    {  16, 3.0f,  .16f, .34f, 45, 82 },
    {  27, 6.0f, -.14f, .28f, 40, 76 },
    {  34, 4.0f,  .18f, .36f, 47, 84 },
    {  47, 7.0f, -.16f, .32f, 38, 80 },
    {  58, 4.0f,  .14f, .34f, 42, 82 },
    {  64, 3.0f, -.18f, .38f, 49, 86 },
    {  76, 6.0f,  .16f, .30f, 45, 80 },
    {  84, 4.0f, -.14f, .40f, 52, 88 },
    { 100, 5.0f,  .18f, .32f, 44, 82 },
    { 106, 3.0f, -.16f, .36f, 47, 84 },
    { 119, 7.0f,  .14f, .30f, 42, 80 },
    { 130, 5.0f, -.18f, .34f, 40, 82 },
    { 137, 4.0f,  .16f, .38f, 47, 86 },
    { 154, 6.0f, -.14f, .32f, 42, 82 },
    { 166, 4.0f,  .18f, .36f, 44, 84 },
    { 173, 3.0f, -.16f, .40f, 49, 88 },
    { 188, 7.0f,  .14f, .34f, 38, 82 },
    { 200, 5.0f, -.18f, .36f, 40, 84 },
    { 207, 4.0f,  .16f, .42f, 47, 90 },
    { 220, 7.0f, -.14f, .36f, 42, 86 },
    { 228, 4.0f,  .18f, .44f, 49, 92 },
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
    { 181.0f, 7.5f,  .04f, .48f, 57, 104 },
    { 204.0f, 8.0f, -.05f, .54f, 62, 110 },
    { 224.0f, 7.5f,  .03f, .58f, 64, 112 },
};

static ma_patch performance_patch(voice_role role) {
    ma_patch patch = role == ROLE_PAD_DARK ? ma_patch_prizma
                   : role == ROLE_PAD_HAZE ? ma_patch_tepih
                   : role <= ROLE_BASS_MOTION ? ma_patch_granica
                   : ma_dark_lead_patch();
    if (role == ROLE_PAD_DARK) {
        patch.raster_mix = .28f;
        patch.raster_position = .24f;
        patch.raster_warp = .08f;
        patch.bcs_amount = .08f;
        patch.bcs_regime = .12f;
        patch.filter_cutoff_hz = 720.0f;
        patch.amp_adsr.release_ms = 6200.0f;
        patch.filter_adsr.release_ms = 5200.0f;
        patch.width = .86f;
        patch.master_level = .15f;
    } else if (role == ROLE_PAD_HAZE) {
        patch.vco1.saw_level = .24f;
        patch.vco1.pulse_level = .05f;
        patch.vco1.triangle_level = .36f;
        patch.vco1.sine_level = .55f;
        patch.vco2.saw_level = .08f;
        patch.vco2.pulse_level = 0.0f;
        patch.vco2.triangle_level = .48f;
        patch.vco2.sine_level = .42f;
        patch.vco2_level = .56f;
        patch.vco2_fine_cents = -4.0f;
        patch.noise_level = .015f;
        patch.raster_mix = .44f;
        patch.raster_position = .62f;
        patch.raster_warp = .21f;
        patch.bcs_amount = .12f;
        patch.bcs_regime = .28f;
        patch.mozaik_mix = .12f;
        patch.filter_cutoff_hz = 980.0f;
        patch.filter_resonance = .12f;
        patch.filter_drive = .07f;
        patch.filter_env_amount = .20f;
        patch.amp_adsr = (ma_adsr){ 1400.0f, 2400.0f, .74f, 7800.0f };
        patch.filter_adsr = (ma_adsr){ 1200.0f, 2600.0f, .42f, 6800.0f };
        patch.macro[MA_MACRO_BLOOM] = .18f;
        patch.macro[MA_MACRO_SWARM] = .08f;
        patch.body_drive = .06f;
        patch.width = .92f;
        patch.master_level = .13f;
    } else if (role == ROLE_BASS_ROOT) {
        patch.bcs_amount = .62f;
        patch.bcs_regime = .58f;
        patch.filter_cutoff_hz = 420.0f;
        patch.amp_adsr.release_ms = 2800.0f;
        patch.master_level = .14f;
    } else if (role == ROLE_BASS_MOTION) {
        patch.raster_mix = .14f;
        patch.raster_position = .19f;
        patch.raster_warp = .07f;
        patch.bcs_amount = .46f;
        patch.bcs_regime = .40f;
        patch.vco1.saw_level = .06f;
        patch.vco1.triangle_level = .24f;
        patch.vco1.sine_level = .30f;
        patch.vco2.saw_level = .03f;
        patch.vco2.triangle_level = .30f;
        patch.vco2.sine_level = .72f;
        patch.vco2_level = .72f;
        patch.crossmod_amount = .025f;
        patch.filter_cutoff_hz = 680.0f;
        patch.filter_resonance = .16f;
        patch.filter_drive = .12f;
        patch.filter_env_amount = .28f;
        patch.amp_adsr = (ma_adsr){ 80.0f, 520.0f, .72f, 3200.0f };
        patch.filter_adsr = (ma_adsr){ 120.0f, 720.0f, .36f, 2600.0f };
        patch.macro[MA_MACRO_BLOOM] = .05f;
        patch.macro[MA_MACRO_HEAT] = .08f;
        patch.body_drive = .10f;
        patch.master_level = .13f;
    } else {
        patch.raster_mix = .10f;
        patch.raster_position = .17f;
        patch.raster_warp = .06f;
        patch.bcs_amount = .20f;
        patch.bcs_regime = .34f;
    }
    return patch;
}

static void voice_range(voice_role role, unsigned *first, unsigned *count) {
    if (role == ROLE_PAD_DARK) {
        *first = 0;
        *count = PAD_DARK_VOICES;
    } else if (role == ROLE_PAD_HAZE) {
        *first = PAD_DARK_VOICES;
        *count = PAD_HAZE_VOICES;
    } else if (role == ROLE_BASS_ROOT) {
        *first = PAD_DARK_VOICES + PAD_HAZE_VOICES;
        *count = BASS_ROOT_VOICES;
    } else if (role == ROLE_BASS_MOTION) {
        *first = PAD_DARK_VOICES + PAD_HAZE_VOICES + BASS_ROOT_VOICES;
        *count = BASS_MOTION_VOICES;
    } else {
        *first = PAD_DARK_VOICES + PAD_HAZE_VOICES
               + BASS_ROOT_VOICES + BASS_MOTION_VOICES;
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
    if (role == ROLE_LEAD) {
        ma_synth_set_glide(&choice->synth, true, .16f);
        ma_synth_set_lfo(&choice->synth, .018f, 3.65f);
    } else if (role <= ROLE_PAD_HAZE) {
        ma_synth_set_lfo(&choice->synth, .012f, .09f);
    }
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
                           : role <= ROLE_PAD_HAZE ? .18f : .06f);
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
    if (role == ROLE_PAD_DARK) return .28f;
    if (role == ROLE_PAD_HAZE) return .20f;
    if (role == ROLE_BASS_ROOT) return .52f;
    if (role == ROLE_BASS_MOTION) return .38f;
    return .68f;
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

static bool render(wav_f32_writer *writer, performance_metrics *metrics) {
    performance_voice voices[VOICE_COUNT] = { 0 };
    performance_reverb reverb = { 0 };
    float block[2u * BLOCK_FRAMES];
    size_t next_pad_dark = 0, next_pad_haze = 0;
    size_t next_bass_root = 0, next_bass_motion = 0, next_lead = 0;
    unsigned reported = 0;
    *metrics = (performance_metrics){ 0 };

    fputs("  render   0%", stderr);
    fflush(stderr);
    for (size_t first = 0; first < FRAME_COUNT; first += BLOCK_FRAMES) {
        size_t count = FRAME_COUNT - first;
        if (count > BLOCK_FRAMES) count = BLOCK_FRAMES;
        for (size_t i = 0; i < count; ++i) {
            size_t frame = first + i;
            start_due_events(PAD_DARK_EVENTS,
                             sizeof PAD_DARK_EVENTS / sizeof *PAD_DARK_EVENTS,
                             &next_pad_dark, ROLE_PAD_DARK,
                             frame, voices, metrics);
            start_due_events(PAD_HAZE_EVENTS,
                             sizeof PAD_HAZE_EVENTS / sizeof *PAD_HAZE_EVENTS,
                             &next_pad_haze, ROLE_PAD_HAZE,
                             frame, voices, metrics);
            start_due_events(BASS_ROOT_EVENTS,
                             sizeof BASS_ROOT_EVENTS / sizeof *BASS_ROOT_EVENTS,
                             &next_bass_root, ROLE_BASS_ROOT,
                             frame, voices, metrics);
            start_due_events(BASS_MOTION_EVENTS,
                             sizeof BASS_MOTION_EVENTS
                             / sizeof *BASS_MOTION_EVENTS,
                             &next_bass_motion, ROLE_BASS_MOTION,
                             frame, voices, metrics);
            start_due_events(LEAD_EVENTS,
                             sizeof LEAD_EVENTS / sizeof *LEAD_EVENTS,
                             &next_lead, ROLE_LEAD, frame, voices, metrics);
            ma_frame sample = reverb_tick(
                &reverb, render_voices(voices, frame, metrics));
            sample.left *= 2.35f;
            sample.right *= 2.35f;
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
        metrics->hash = tw_fnv1a64(block, 2u * count * sizeof block[0],
                                   metrics->hash);
        if (wav_f32_write(writer, block, count) < 0) return false;
        unsigned percent = (unsigned)(100u * (first + count) / FRAME_COUNT);
        unsigned milestone = percent / 5u * 5u;
        if (milestone > reported) {
            reported = milestone;
            fprintf(stderr, "\rrender %3u%%", reported);
            fflush(stderr);
        }
    }
    fputc('\n', stderr);
    return metrics->nonfinite == 0 && metrics->clipped == 0
        && metrics->peak > 1e-4f && metrics->sum_squares > 1e-8;
}

int main(void) {
    puts("Mamut Analog — Blade Runner blues, MA2 full-spectrum arrangement");
    puts("  Prizma/Tepih fields, Granica bass, sparse spectral dark Lead");

    performance_metrics metrics = { 0 };
    wav_f32_writer writer = { 0 };
    bool opened = wav_f32_open(&writer, TEMP_OUTPUT_PATH, FRAME_COUNT,
                               RATE, 2) == 0;
    bool rendered = opened && render(&writer, &metrics);
    bool closed = rendered && wav_f32_close(&writer) == 0;
    if (!closed) {
        wav_f32_abort(&writer);
        (void)remove(TEMP_OUTPUT_PATH);
    }
    bool written = closed && rename(TEMP_OUTPUT_PATH, OUTPUT_PATH) == 0;
    if (closed && !written) (void)remove(TEMP_OUTPUT_PATH);
    double rms = sqrt(metrics.sum_squares / (2.0 * FRAME_COUNT));

    printf("  %.0f s + %d s tail, %u notes, %u-voice peak, %u steals\n",
           (double)PERFORMANCE_SECONDS, TAIL_SECONDS, metrics.notes,
           metrics.peak_voices, metrics.steals);
    printf("  peak %.6f, RMS %.6f, finite %s, headroom %s\n",
           (double)metrics.peak, rms,
           metrics.nonfinite == 0 ? "yes" : "NO",
           metrics.clipped == 0 ? "yes" : "NO");
    printf("  FNV64 %016llx (single streaming pass)\n",
           (unsigned long long)metrics.hash);
    printf("  wav: %s%s\n", OUTPUT_PATH, written ? "" : " (FAILED)");
    return written ? 0 : 1;
}
