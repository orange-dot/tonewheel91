/* Hosted Mamut Analog reinterpretation of a user-supplied Hurt MIDI.
 * MIDI track names describe score roles only; every sound is synthesized. */
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/mamutanalog.h"
#include "host_parse.h"
#include "smf.h"
#include "wav.h"

enum {
    RATE = 48000,
    DEFAULT_SECONDS = 253,
    MAX_SECONDS = 270,
    BLOCK_FRAMES = 4096,
    PLUCK_VOICES = 6,
    PAD_VOICES = 3,
    TENOR_VOICES = 3,
    RHYTHM_VOICES = 3,
    COMB_COUNT = 4,
    COMB_CAPACITY = 1536,
    ALLPASS_COUNT = 2,
    ALLPASS_CAPACITY = 640,
};

static constexpr uint64_t FIRST_PAD_PHRASE_END = 76800;
static constexpr uint64_t SECOND_PAD_PHRASE_END = 163200;
static constexpr char DEFAULT_INPUT[] =
    "notes-midi/local/nine-inch-nails-hurt.mid";
static constexpr char DEFAULT_OUTPUT[] =
    "build/ma_nin_hurt_noir_ma2_full.wav";
static constexpr float MASTER_GAIN = 1.28f;

typedef struct {
    ma_synth synth;
    uint64_t age;
    uint8_t channel;
    uint8_t note;
    bool held;
} fixed_voice;

typedef struct {
    float left;
    float right;
} pan_gain;

typedef struct {
    fixed_voice pluck[PLUCK_VOICES];
    fixed_voice pad[PAD_VOICES];
    fixed_voice bass;
    fixed_voice tenor[TENOR_VOICES];
    fixed_voice pulse[RHYTHM_VOICES];
    pan_gain pluck_pan[PLUCK_VOICES];
    pan_gain pad_pan[PAD_VOICES];
    pan_gain bass_pan;
    pan_gain tenor_pan[TENOR_VOICES];
    pan_gain pulse_pan[RHYTHM_VOICES];
    uint64_t next_age;
    uint8_t pad_source;
    bool pad_held;
} performance;

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
    unsigned midi_notes;
    unsigned synth_attacks;
    unsigned steals;
    unsigned peak_voices;
    unsigned nonfinite;
    unsigned clipped;
} performance_metrics;

typedef struct {
    uint8_t *bytes;
    smf_file midi;
    size_t *event_frame;
    size_t end_frame;
} performance_score;

static pan_gain make_pan(float pan, float gain) {
    return (pan_gain){
        .left = gain * sqrtf(.5f * (1.0f - pan)),
        .right = gain * sqrtf(.5f * (1.0f + pan)),
    };
}

static ma_patch pluck_patch(float fine_cents) {
    ma_patch patch = ma_patch_tepih;
    patch.vco1.saw_level = .12f;
    patch.vco1.pulse_level = 0.0f;
    patch.vco1.triangle_level = .48f;
    patch.vco1.sine_level = .34f;
    patch.vco2.saw_level = .03f;
    patch.vco2.pulse_level = 0.0f;
    patch.vco2.triangle_level = .32f;
    patch.vco2.sine_level = .56f;
    patch.vco2_level = .52f;
    patch.vco2_fine_cents = fine_cents;
    patch.noise_level = .012f;
    patch.bcs_amount = .06f;
    patch.bcs_regime = .14f;
    patch.mozaik_mix = .055f;
    patch.filter_cutoff_hz = 980.0f;
    patch.filter_resonance = .14f;
    patch.filter_drive = .065f;
    patch.filter_env_amount = .31f;
    patch.amp_adsr = (ma_adsr){ 260.0f, 1250.0f, .64f, 2900.0f };
    patch.filter_adsr = (ma_adsr){ 180.0f, 1500.0f, .34f, 2500.0f };
    patch.macro[MA_MACRO_BLOOM] = .07f;
    patch.body_drive = .055f;
    patch.width = .34f;
    patch.crossfeed = .12f;
    patch.master_level = .15f;
    return patch;
}

static ma_patch pad_patch(float fine_cents, float raster_mix,
                          float raster_position, float raster_warp) {
    ma_patch patch = ma_patch_tepih;
    patch.vco1.saw_level = .16f;
    patch.vco1.pulse_level = 0.0f;
    patch.vco1.triangle_level = .39f;
    patch.vco1.sine_level = .52f;
    patch.vco2.saw_level = .06f;
    patch.vco2.pulse_level = 0.0f;
    patch.vco2.triangle_level = .46f;
    patch.vco2.sine_level = .45f;
    patch.vco2_level = .61f;
    patch.vco2_fine_cents = fine_cents;
    patch.sync_amount = .012f;
    patch.crossmod_amount = .018f;
    patch.noise_level = .009f;
    patch.raster_mix = raster_mix;
    patch.raster_position = raster_position;
    patch.raster_warp = raster_warp;
    patch.bcs_amount = raster_mix > 0.0f ? .12f : .07f;
    patch.bcs_regime = raster_mix > 0.0f ? .26f : .16f;
    patch.mozaik_mix = .07f;
    patch.mixer_pressure = .045f;
    patch.filter_cutoff_hz = 820.0f;
    patch.filter_resonance = .135f;
    patch.filter_drive = .065f;
    patch.filter_env_amount = .20f;
    patch.amp_adsr = (ma_adsr){ 1150.0f, 2500.0f, .79f, 7200.0f };
    patch.filter_adsr = (ma_adsr){ 900.0f, 2900.0f, .42f, 6200.0f };
    patch.macro[MA_MACRO_GRAVITACIJA] = .04f;
    patch.macro[MA_MACRO_BLOOM] = .11f;
    patch.body_drive = .052f;
    patch.width = .78f;
    patch.crossfeed = .15f;
    patch.master_level = .125f;
    return patch;
}

static ma_patch bass_patch(void) {
    ma_patch patch = ma_patch_granica;
    patch.vco1.saw_level = .10f;
    patch.vco1.pulse_level = 0.0f;
    patch.vco1.triangle_level = .52f;
    patch.vco1.sine_level = .66f;
    patch.vco2.saw_level = .04f;
    patch.vco2.pulse_level = 0.0f;
    patch.vco2.triangle_level = .38f;
    patch.vco2.sine_level = .62f;
    patch.vco2_level = .61f;
    patch.bcs_amount = .62f;
    patch.bcs_regime = .58f;
    patch.filter_cutoff_hz = 390.0f;
    patch.filter_resonance = .12f;
    patch.filter_drive = .11f;
    patch.filter_env_amount = .23f;
    patch.amp_adsr = (ma_adsr){ 58.0f, 980.0f, .76f, 3400.0f };
    patch.filter_adsr = (ma_adsr){ 42.0f, 1200.0f, .38f, 2900.0f };
    patch.macro[MA_MACRO_GRAVITACIJA] = .10f;
    patch.body_drive = .09f;
    patch.width = .18f;
    patch.crossfeed = .32f;
    patch.master_level = .17f;
    return patch;
}

static ma_patch tenor_patch(float fine_cents) {
    ma_patch patch = ma_patch_tepih;
    patch.vco1.saw_level = .19f;
    patch.vco1.pulse_level = .025f;
    patch.vco1.triangle_level = .38f;
    patch.vco1.sine_level = .37f;
    patch.vco2.saw_level = .08f;
    patch.vco2.pulse_level = 0.0f;
    patch.vco2.triangle_level = .42f;
    patch.vco2.sine_level = .43f;
    patch.vco2_level = .62f;
    patch.vco2_fine_cents = fine_cents;
    patch.crossmod_amount = .045f;
    patch.noise_level = .018f;
    patch.raster_mix = .12f;
    patch.raster_position = .20f;
    patch.raster_warp = .07f;
    patch.bcs_amount = .30f;
    patch.bcs_regime = .42f;
    patch.mozaik_mix = .075f;
    patch.filter_cutoff_hz = 760.0f;
    patch.filter_resonance = .19f;
    patch.filter_drive = .12f;
    patch.filter_env_amount = .29f;
    patch.amp_adsr = (ma_adsr){ 240.0f, 1750.0f, .70f, 4800.0f };
    patch.filter_adsr = (ma_adsr){ 180.0f, 2100.0f, .36f, 4100.0f };
    patch.macro[MA_MACRO_BLOOM] = .085f;
    patch.macro[MA_MACRO_HEAT] = .075f;
    patch.body_drive = .11f;
    patch.width = .42f;
    patch.crossfeed = .18f;
    patch.master_level = .14f;
    return patch;
}

static ma_patch pulse_patch(float fine_cents, float raster_position,
                            float raster_warp, float bcs_regime) {
    ma_patch patch = ma_patch_prizma;
    patch.vco1.saw_level = .24f;
    patch.vco1.pulse_level = .03f;
    patch.vco1.triangle_level = .18f;
    patch.vco1.sine_level = .48f;
    patch.vco2.saw_level = .12f;
    patch.vco2.pulse_level = 0.0f;
    patch.vco2.triangle_level = .28f;
    patch.vco2.sine_level = .38f;
    patch.vco2_level = .48f;
    patch.vco2_fine_cents = fine_cents;
    patch.crossmod_amount = .055f;
    patch.noise_level = .10f;
    patch.raster_mix = .30f;
    patch.raster_position = raster_position;
    patch.raster_warp = raster_warp;
    patch.bcs_amount = .24f;
    patch.bcs_regime = bcs_regime;
    patch.mozaik_mix = .14f;
    patch.mozaik_slope = .54f;
    patch.mozaik_contrast = .57f;
    patch.mozaik_drift = .065f;
    patch.filter_cutoff_hz = 1420.0f;
    patch.filter_resonance = .21f;
    patch.filter_drive = .13f;
    patch.filter_env_amount = .48f;
    patch.amp_adsr = (ma_adsr){ 8.0f, 220.0f, .36f, 1800.0f };
    patch.filter_adsr = (ma_adsr){ 4.0f, 380.0f, .08f, 1100.0f };
    patch.macro[MA_MACRO_HEAT] = .075f;
    patch.macro[MA_MACRO_RUIN] = .025f;
    patch.body_drive = .11f;
    patch.width = .58f;
    patch.crossfeed = .08f;
    patch.master_level = .13f;
    return patch;
}

static void performance_init(performance *show) {
    static const float PLUCK_PAN[PLUCK_VOICES] = {
        -.72f, -.43f, -.16f, .16f, .43f, .72f,
    };
    static const float PLUCK_FINE[PLUCK_VOICES] = {
        -2.5f, 1.5f, -1.0f, 1.0f, -1.5f, 2.5f,
    };
    static const float PAD_PAN[PAD_VOICES] = { -.62f, 0.0f, .62f };
    static const float PAD_FINE[PAD_VOICES] = { -3.0f, 0.0f, 3.0f };
    static const float PAD_RASTER_MIX[PAD_VOICES] = { 0.0f, 0.0f, .18f };
    static const float TENOR_PAN[TENOR_VOICES] = { -.48f, 0.0f, .48f };
    static const float TENOR_FINE[TENOR_VOICES] = { -2.0f, 0.0f, 2.0f };
    static const float RHYTHM_PAN[RHYTHM_VOICES] = { -.34f, .08f, .48f };
    static const float RHYTHM_FINE[RHYTHM_VOICES] = { -2.5f, 0.0f, 2.5f };
    static const float RHYTHM_POSITION[RHYTHM_VOICES] = { .24f, .38f, .52f };
    static const float RHYTHM_WARP[RHYTHM_VOICES] = { .12f, .18f, .24f };
    static const float RHYTHM_BCS[RHYTHM_VOICES] = { .28f, .46f, .64f };

    *show = (performance){ .next_age = 1 };
    for (unsigned i = 0; i < PLUCK_VOICES; ++i) {
        ma_patch patch = pluck_patch(PLUCK_FINE[i]);
        ma_synth_init_patch(&show->pluck[i].synth, RATE, &patch);
        show->pluck_pan[i] = make_pan(PLUCK_PAN[i], .135f);
    }
    for (unsigned i = 0; i < PAD_VOICES; ++i) {
        ma_patch patch = pad_patch(PAD_FINE[i], PAD_RASTER_MIX[i],
                                   .31f, .10f);
        ma_synth_init_patch(&show->pad[i].synth, RATE, &patch);
        ma_synth_set_lfo(&show->pad[i].synth, .012f, .08f);
        show->pad_pan[i] = make_pan(PAD_PAN[i], .105f);
    }

    ma_patch bass = bass_patch();
    ma_synth_init_patch(&show->bass.synth, RATE, &bass);
    show->bass_pan = make_pan(0.0f, .52f);

    for (unsigned i = 0; i < TENOR_VOICES; ++i) {
        ma_patch patch = tenor_patch(TENOR_FINE[i]);
        ma_synth_init_patch(&show->tenor[i].synth, RATE, &patch);
        show->tenor_pan[i] = make_pan(TENOR_PAN[i], .20f);
    }
    for (unsigned i = 0; i < RHYTHM_VOICES; ++i) {
        ma_patch patch = pulse_patch(RHYTHM_FINE[i], RHYTHM_POSITION[i],
                                     RHYTHM_WARP[i], RHYTHM_BCS[i]);
        ma_synth_init_patch(&show->pulse[i].synth, RATE, &patch);
        ma_synth_set_lfo(&show->pulse[i].synth, .008f, .14f);
        show->pulse_pan[i] = make_pan(RHYTHM_PAN[i], .20f);
    }
}

static fixed_voice *choose_voice(fixed_voice *voice, size_t count,
                                 performance_metrics *metrics) {
    fixed_voice *chosen = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!voice[i].held
            && voice[i].synth.amp_envelope.stage == MA_ENVELOPE_IDLE)
            return &voice[i];
        if (!voice[i].held && (!chosen || voice[i].age < chosen->age))
            chosen = &voice[i];
    }
    if (chosen) return chosen;
    for (size_t i = 0; i < count; ++i)
        if (!chosen || voice[i].age < chosen->age) chosen = &voice[i];
    metrics->steals++;
    return chosen;
}

static void fixed_note_on(performance *show, fixed_voice *voice, size_t count,
                          uint8_t channel, uint8_t note, uint8_t velocity,
                          performance_metrics *metrics) {
    fixed_voice *chosen = choose_voice(voice, count, metrics);
    ma_synth_note_on(&chosen->synth, channel, note, velocity);
    chosen->age = show->next_age++;
    chosen->channel = channel;
    chosen->note = note;
    chosen->held = true;
    metrics->synth_attacks++;
}

static void fixed_note_off(fixed_voice *voice, size_t count, uint8_t channel,
                           uint8_t note, uint8_t velocity) {
    fixed_voice *chosen = 0;
    for (size_t i = 0; i < count; ++i)
        if (voice[i].held && voice[i].channel == channel
            && voice[i].note == note
            && (!chosen || voice[i].age < chosen->age))
            chosen = &voice[i];
    if (!chosen) return;
    ma_synth_note_off(&chosen->synth, channel, note, velocity);
    chosen->held = false;
}

static void apply_piano_event(performance *show, const smf_event *event,
                              bool note_on, bool note_off,
                              performance_metrics *metrics) {
    static const int PAD_INTERVAL[PAD_VOICES] = { -12, 0, 7 };
    uint8_t channel = event->status & 0x0fu;
    if (note_on) {
        if (show->pad_held && show->pad_source == event->d1) return;
        for (unsigned i = 0; i < PAD_VOICES; ++i) {
            int shifted = event->d1 + PAD_INTERVAL[i];
            uint8_t note = (uint8_t)(shifted < 0 ? 0
                                   : shifted > 127 ? 127 : shifted);
            ma_synth_note_on(&show->pad[i].synth, channel,
                             note, event->d2);
            show->pad[i].age = show->next_age++;
            show->pad[i].channel = channel;
            show->pad[i].note = note;
            show->pad[i].held = true;
        }
        show->pad_source = event->d1;
        show->pad_held = true;
        metrics->synth_attacks += PAD_VOICES;
        return;
    }
    if (!note_off || !show->pad_held
        || (event->tick != FIRST_PAD_PHRASE_END
            && event->tick != SECOND_PAD_PHRASE_END))
        return;
    for (unsigned i = 0; i < PAD_VOICES; ++i) {
        ma_synth_note_off(&show->pad[i].synth, channel,
                          show->pad[i].note, event->d2);
        show->pad[i].held = false;
    }
    show->pad_held = false;
}

static void apply_event(performance *show, const smf_event *event,
                        performance_metrics *metrics) {
    uint8_t type = event->status & 0xf0u;
    uint8_t channel = event->status & 0x0fu;
    bool note_on = type == 0x90u && event->d2 != 0;
    bool note_off = type == 0x80u || (type == 0x90u && event->d2 == 0);
    if (!note_on && !note_off) return;
    if (note_on) metrics->midi_notes++;

    switch (channel) {
    case 0:
        if (note_on)
            fixed_note_on(show, show->pluck, PLUCK_VOICES, channel,
                          event->d1, event->d2, metrics);
        else
            fixed_note_off(show->pluck, PLUCK_VOICES, channel,
                           event->d1, event->d2);
        break;
    case 2:
        apply_piano_event(show, event, note_on, note_off, metrics);
        break;
    case 4:
        if (note_on)
            fixed_note_on(show, &show->bass, 1, channel,
                          event->d1, event->d2, metrics);
        else
            fixed_note_off(&show->bass, 1, channel, event->d1, event->d2);
        break;
    case 6:
        if (note_on)
            fixed_note_on(show, show->tenor, TENOR_VOICES, channel,
                          event->d1, event->d2, metrics);
        else
            fixed_note_off(show->tenor, TENOR_VOICES, channel,
                           event->d1, event->d2);
        break;
    case 9:
        if (note_on)
            fixed_note_on(show, show->pulse, RHYTHM_VOICES, channel,
                          event->d1, event->d2, metrics);
        else
            fixed_note_off(show->pulse, RHYTHM_VOICES, channel,
                           event->d1, event->d2);
        break;
    }
}

static void mix_voice(ma_frame *mixed, fixed_voice *voice,
                      pan_gain pan, unsigned *active) {
    ma_frame frame = ma_synth_tick(&voice->synth);
    mixed->left += pan.left * frame.left;
    mixed->right += pan.right * frame.right;
    *active += voice->synth.amp_envelope.stage != MA_ENVELOPE_IDLE;
}

static ma_frame performance_tick(performance *show,
                                 performance_metrics *metrics) {
    ma_frame mixed = { 0 };
    unsigned active = 0;
    for (unsigned i = 0; i < PLUCK_VOICES; ++i)
        mix_voice(&mixed, &show->pluck[i], show->pluck_pan[i], &active);
    for (unsigned i = 0; i < PAD_VOICES; ++i)
        mix_voice(&mixed, &show->pad[i], show->pad_pan[i], &active);
    mix_voice(&mixed, &show->bass, show->bass_pan, &active);
    for (unsigned i = 0; i < TENOR_VOICES; ++i)
        mix_voice(&mixed, &show->tenor[i], show->tenor_pan[i], &active);
    for (unsigned i = 0; i < RHYTHM_VOICES; ++i)
        mix_voice(&mixed, &show->pulse[i], show->pulse_pan[i], &active);

    if (active > metrics->peak_voices) metrics->peak_voices = active;
    return mixed;
}

static float comb_tick(float buffer[COMB_CAPACITY], unsigned length,
                       unsigned *position, float *filtered, float input) {
    float delayed = buffer[*position];
    *filtered = .75f * delayed + .25f * *filtered;
    buffer[*position] = input + .785f * *filtered;
    if (++*position == length) *position = 0;
    return delayed;
}

static float allpass_tick(float buffer[ALLPASS_CAPACITY], unsigned length,
                          unsigned *position, float input) {
    float delayed = buffer[*position];
    float output = delayed - input;
    buffer[*position] = input + .51f * delayed;
    if (++*position == length) *position = 0;
    return output;
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
    float input_l = .76f * dry.left + .24f * dry.right;
    float input_r = .76f * dry.right + .24f * dry.left;
    float wet_l = 0.0f;
    float wet_r = 0.0f;
    for (unsigned i = 0; i < COMB_COUNT; ++i) {
        wet_l += comb_tick(reverb->comb_l[i], COMB_LENGTH_L[i],
                           &reverb->comb_position_l[i],
                           &reverb->comb_filter_l[i], input_l);
        wet_r += comb_tick(reverb->comb_r[i], COMB_LENGTH_R[i],
                           &reverb->comb_position_r[i],
                           &reverb->comb_filter_r[i], input_r);
    }
    wet_l *= .25f;
    wet_r *= .25f;
    for (unsigned i = 0; i < ALLPASS_COUNT; ++i) {
        wet_l = allpass_tick(reverb->allpass_l[i], ALLPASS_LENGTH_L[i],
                             &reverb->allpass_position_l[i], wet_l);
        wet_r = allpass_tick(reverb->allpass_r[i], ALLPASS_LENGTH_R[i],
                             &reverb->allpass_position_r[i], wet_r);
    }
    return (ma_frame){ wet_l, wet_r };
}

static float reverb_amount(size_t frame) {
    if (frame < 60u * RATE) return .105f;
    if (frame < 115u * RATE) return .145f;
    if (frame < 150u * RATE) return .19f;
    return .235f;
}

static void measure_sample(performance_metrics *metrics, ma_frame *sample) {
    if (!isfinite(sample->left) || !isfinite(sample->right)) {
        metrics->nonfinite++;
        *sample = (ma_frame){ 0 };
    }
    float left = fabsf(sample->left);
    float right = fabsf(sample->right);
    float peak = left > right ? left : right;
    if (peak > metrics->peak) metrics->peak = peak;
    if (peak > 1.0f) metrics->clipped++;
    metrics->sum_squares += (double)sample->left * sample->left
                          + (double)sample->right * sample->right;
}

static bool render_stream(const performance_score *score,
                          wav_f32_writer *writer, size_t frames,
                          performance_metrics *metrics) {
    performance show;
    performance_reverb reverb = { 0 };
    float block[2u * BLOCK_FRAMES];
    size_t next_event = 0;
    size_t progress_step = frames / 10u;
    size_t next_progress = progress_step;
    performance_init(&show);
    *metrics = (performance_metrics){ 0 };

    for (size_t first = 0; first < frames; first += BLOCK_FRAMES) {
        size_t count = frames - first;
        if (count > BLOCK_FRAMES) count = BLOCK_FRAMES;
        for (size_t i = 0; i < count; ++i) {
            size_t frame_number = first + i;
            while (next_event < score->midi.event_count
                   && score->event_frame[next_event] <= frame_number)
                apply_event(&show, &score->midi.events[next_event++], metrics);
            ma_frame dry = performance_tick(&show, metrics);
            ma_frame wet = reverb_tick(&reverb, dry);
            float amount = reverb_amount(frame_number);
            ma_frame sample = {
                .left = MASTER_GAIN
                      * (.91f * dry.left + amount * wet.left),
                .right = MASTER_GAIN
                       * (.91f * dry.right + amount * wet.right),
            };
            measure_sample(metrics, &sample);
            block[2u * i] = sample.left;
            block[2u * i + 1u] = sample.right;
        }
        metrics->hash = tw_fnv1a64(block, 2u * count * sizeof block[0],
                                   metrics->hash);
        if (wav_f32_write(writer, block, count) < 0) return false;
        size_t completed = first + count;
        if (progress_step && completed >= next_progress) {
            unsigned percent = (unsigned)(100u * completed / frames);
            if (percent > 100u) percent = 100u;
            fprintf(stderr, "\r  render %3u%%", percent);
            fflush(stderr);
            while (next_progress <= completed
                   && next_progress <= frames - progress_step)
                next_progress += progress_step;
        }
    }
    fputc('\n', stderr);
    return metrics->nonfinite == 0 && metrics->clipped == 0
        && metrics->peak > 1e-5f;
}

static bool score_validate(const performance_score *score,
                           const char **reason) {
    static const unsigned EXPECTED_NOTES[16] = {
        [0] = 704, [2] = 125, [4] = 224, [6] = 189, [9] = 248,
    };
    if (score->midi.format != 1 || score->midi.tracks != 6
        || score->midi.division != 480 || score->midi.event_count != 2980
        || score->midi.tempo_count != 1
        || score->midi.tempos[0].tick != 0
        || score->midi.tempos[0].us_per_quarter != 714285) {
        *reason = "MIDI is not the inspected six-track Hurt source";
        return false;
    }
    unsigned note_count[16] = { 0 };
    for (size_t i = 0; i < score->midi.event_count; ++i) {
        const smf_event *event = &score->midi.events[i];
        uint8_t type = event->status & 0xf0u;
        uint8_t channel = event->status & 0x0fu;
        if ((type != 0x80u && type != 0x90u) || event->d1 > 127u
            || (i && (event->tick < score->midi.events[i - 1u].tick
                      || (event->tick == score->midi.events[i - 1u].tick
                          && event->seq < score->midi.events[i - 1u].seq)))) {
            *reason = "MIDI events are invalid or unsorted";
            return false;
        }
        if (type == 0x90u && event->d2) note_count[channel]++;
    }
    for (unsigned channel = 0; channel < 16; ++channel)
        if (note_count[channel] != EXPECTED_NOTES[channel]) {
            *reason = "MIDI note layout differs from the inspected source";
            return false;
        }
    return true;
}

static bool event_frames(performance_score *score, const char **reason) {
    score->event_frame = malloc(score->midi.event_count
                              * sizeof *score->event_frame);
    if (!score->event_frame) {
        *reason = "out of memory for MIDI timeline";
        return false;
    }
    double seconds = 0.0;
    uint64_t tick0 = 0;
    uint32_t us_per_quarter = 500000;
    size_t tempo = 0;
    for (size_t i = 0; i < score->midi.event_count; ++i) {
        const smf_event *event = &score->midi.events[i];
        while (tempo < score->midi.tempo_count
               && score->midi.tempos[tempo].tick <= event->tick) {
            const smf_tempo *change = &score->midi.tempos[tempo++];
            seconds += (double)(change->tick - tick0) * us_per_quarter
                     * 1e-6 / score->midi.division;
            tick0 = change->tick;
            us_per_quarter = change->us_per_quarter;
        }
        double at = seconds + (double)(event->tick - tick0) * us_per_quarter
                  * 1e-6 / score->midi.division;
        if (!isfinite(at) || at < 0.0 || at > MAX_SECONDS) {
            *reason = "MIDI timeline is outside the exhibit limit";
            return false;
        }
        score->event_frame[i] = (size_t)(at * RATE + .5);
    }
    score->end_frame = score->event_frame[score->midi.event_count - 1u];
    return true;
}

static bool score_load(performance_score *score, const char *path,
                       const char **reason) {
    *score = (performance_score){ 0 };
    FILE *file = fopen(path, "rb");
    if (!file) {
        *reason = strerror(errno);
        return false;
    }
    bool ok = fseek(file, 0, SEEK_END) == 0;
    long length = ok ? ftell(file) : -1;
    ok = length > 0 && fseek(file, 0, SEEK_SET) == 0;
    score->bytes = ok ? malloc((size_t)length) : 0;
    ok = ok && score->bytes
       && fread(score->bytes, 1, (size_t)length, file) == (size_t)length;
    bool closed = fclose(file) == 0;
    if (!ok || !closed) {
        *reason = "could not read MIDI file";
        return false;
    }
    smf_error error = { 0 };
    if (!smf_parse(score->bytes, (size_t)length, UINT16_MAX,
                   &score->midi, &error)) {
        static char message[160];
        snprintf(message, sizeof message, "MIDI error at byte %zu: %s",
                 error.offset, error.message ? error.message : "unknown error");
        *reason = message;
        return false;
    }
    return score_validate(score, reason) && event_frames(score, reason);
}

static void score_dispose(performance_score *score) {
    free(score->event_frame);
    smf_dispose(&score->midi);
    free(score->bytes);
    *score = (performance_score){ 0 };
}

static bool render_file(const performance_score *score, const char *output,
                        size_t frames, performance_metrics *metrics,
                        const char **reason) {
    size_t output_size = strlen(output);
    if (output_size > SIZE_MAX - sizeof ".ma-tmp") {
        *reason = "output path is too long";
        return false;
    }
    char *temporary = malloc(output_size + sizeof ".ma-tmp");
    if (!temporary) {
        *reason = "could not allocate temporary output path";
        return false;
    }
    memcpy(temporary, output, output_size);
    memcpy(temporary + output_size, ".ma-tmp", sizeof ".ma-tmp");

    wav_f32_writer writer = { 0 };
    if (wav_f32_open(&writer, temporary, frames, RATE, 2) < 0) {
        free(temporary);
        *reason = "could not open temporary WAV";
        return false;
    }
    bool rendered = render_stream(score, &writer, frames, metrics);
    bool closed = rendered && wav_f32_close(&writer) == 0;
    if (!closed) {
        wav_f32_abort(&writer);
        (void)remove(temporary);
        free(temporary);
        *reason = rendered ? "could not close completed WAV"
                           : "render failed safety checks";
        return false;
    }
    if (rename(temporary, output) < 0) {
        (void)remove(temporary);
        free(temporary);
        *reason = "could not publish completed WAV";
        return false;
    }
    free(temporary);
    return true;
}

static int usage(const char *program, int status) {
    fprintf(status ? stderr : stdout,
            "usage: %s [-i input.mid] [-o output.wav] [-d seconds]\n"
            "  defaults: -d 253 -o %s\n",
            program, DEFAULT_OUTPUT);
    return status;
}

int main(int argc, char **argv) {
    const char *input = DEFAULT_INPUT;
    const char *output = DEFAULT_OUTPUT;
    double seconds = DEFAULT_SECONDS;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-i") && i + 1 < argc)
            input = argv[++i];
        else if (!strcmp(argv[i], "-o") && i + 1 < argc)
            output = argv[++i];
        else if (!strcmp(argv[i], "-d") && i + 1 < argc) {
            if (!host_parse_double(argv[++i], 0.0, MAX_SECONDS, &seconds)
                || seconds <= 0.0)
                return usage(argv[0], 2);
        } else if (!strcmp(argv[i], "-h")) {
            return usage(argv[0], 0);
        } else {
            return usage(argv[0], 2);
        }
    }
    size_t frames = (size_t)(seconds * RATE + .5);
    if (!frames) return usage(argv[0], 2);

    performance_score score;
    const char *reason = 0;
    if (!score_load(&score, input, &reason)) {
        fprintf(stderr, "%s: %s\n", input, reason ? reason : "load failed");
        score_dispose(&score);
        return 1;
    }
    performance_metrics metrics = { 0 };
    bool ok = render_file(&score, output, frames, &metrics, &reason);
    double rms = ok ? sqrt(metrics.sum_squares / (2.0 * frames)) : 0.0;
    printf("Mamut Analog — Hurt Noir, MA2 full-spectrum arrangement\n");
    printf("  %.3f s, %zu stereo frames at %d Hz, single streaming pass\n",
           frames / (double)RATE, frames, RATE);
    printf("  MIDI 'Piano': three long Tepih synth layers; no piano or lead\n");
    printf("  Raster/Prizma orchestra; Granica bass; BCS rhythmic tension\n");
    printf("  fixed master gain %.2f, dynamics intact; no limiter/normalizer\n",
           (double)MASTER_GAIN);
    printf("  %u MIDI notes, %u synth attacks, %u-voice peak, %u steals\n",
           metrics.midi_notes, metrics.synth_attacks,
           metrics.peak_voices, metrics.steals);
    printf("  peak %.6f, RMS %.6f, finite %s, headroom %s\n",
           (double)metrics.peak, rms,
           metrics.nonfinite == 0 ? "yes" : "NO",
           metrics.clipped == 0 ? "yes" : "NO");
    printf("  FNV64 %016llx\n", (unsigned long long)metrics.hash);
    printf("  wav: %s%s\n", output, ok ? "" : " (FAILED)");
    if (!ok) fprintf(stderr, "%s\n", reason ? reason : "render failed");
    score_dispose(&score);
    return ok ? 0 : 1;
}
