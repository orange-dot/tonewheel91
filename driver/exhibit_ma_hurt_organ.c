/* Hurt: complete vocal MIDI on gritty Mamut, dark backing and wet organ. */
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
    RATE = 48000, DEFAULT_SECONDS = 253,
    BLOCK_FRAMES = 4096, PLUCK_VOICES = 6, TENOR_VOICES = 3,
    COMB_COUNT = 4, COMB_CAPACITY = 1536,
    ALLPASS_COUNT = 2, ALLPASS_CAPACITY = 640,
    TICKS_PER_BAR = 1920, BARS = 85, EVENT_CAPACITY = 16000,
};
static constexpr char DEFAULT_INPUT[] = "notes-midi/local/nine-inch-nails-hurt.mid";
static constexpr char DEFAULT_VOCAL[] = "notes-midi/local/hurt-songparts-vocals.mid";
static constexpr char DEFAULT_OUTPUT[] = "build/ma_hurt_vocal";

typedef enum { PLUCK, PAD, BASS, TENOR, TEXTURE, MELODY, KICK, PULSE, VOCAL, HARMONY,
               PART_COUNT } part_id;
typedef enum { TONAL_STEM, RHYTHM_STEM, ORGAN_STEM, VOCAL_STEM, HARMONY_STEM,
               STEM_COUNT } stem_id;
enum { MIX_OUTPUT = STEM_COUNT, OUTPUT_COUNT = STEM_COUNT + 1 };
static const char *const PART_NAMES[] = {
    "pluck", "pad", "bass", "tenor", "texture", "organ", "kick", "pulse", "vocal", "harmony",
};
static const char *const STEM_NAMES[] = { "tonal", "rhythm", "organ", "vocal", "harmony", "mix" };

typedef struct {
    ma_synth synth;
    uint64_t age;
    uint8_t channel, note;
    bool held;
} fixed_voice;
typedef struct { float left, right; } pan_gain;
typedef struct {
    fixed_voice pluck[PLUCK_VOICES], bass, tenor[TENOR_VOICES];
    fixed_voice vocal[3], kick, pulse[2];
    ma_card_bank pad, texture;
    tw_instrument organ, harmony;
    ma_frame organ_lp, harmony_lp;
    float harmony_envelope;
    unsigned harmony_held;
    pan_gain pluck_pan[PLUCK_VOICES], tenor_pan[TENOR_VOICES];
    uint64_t next_age;
    float energy, kick_envelope;
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
    ma_frame input_lp, wet_lp, wet_lp2;
} performance_reverb;

typedef struct {
    uint64_t hash;
    double sum_squares;
    float peak;
    unsigned synth_attacks, organ_attacks, steals, tail_reuses;
    unsigned nonfinite, clipped;
} performance_metrics;

typedef struct {
    uint32_t tick, order;
    part_id part;
    uint8_t note, velocity;
    bool on;
} arrangement_event;
typedef struct {
    uint8_t *bytes, *vocal_bytes;
    smf_file midi, vocal;
    arrangement_event event[EVENT_CAPACITY];
    size_t count;
    uint8_t root[BARS * 2], chord[BARS * 2][3];
} performance_score;

static size_t tick_frame(uint32_t tick) {
    return (size_t)(((uint64_t)tick * 714285u + 5000u) / 10000u);
}

static pan_gain make_pan(float pan, float gain) {
    return (pan_gain){
        .left = gain * sqrtf(.5f * (1.0f - pan)),
        .right = gain * sqrtf(.5f * (1.0f + pan)),
    };
}

static ma_patch dark_patch(ma_patch p) {
    p.raster_mix = p.mozaik_mix = p.noise_level = 0;
    p.crossmod_amount = p.sync_amount = 0;
    p.vco1.pulse_level = p.vco2.pulse_level = 0;
    p.vco1.saw_level *= .45f;
    p.vco2.saw_level *= .35f;
    if (p.filter_cutoff_hz > 650) p.filter_cutoff_hz = 650;
    if (p.filter_resonance > .12f) p.filter_resonance = .12f;
    p.filter_keytrack = 0;
    p.filter_drive = .035f;
    p.filter_env_amount = .16f;
    p.bcs_amount = .07f;
    p.bcs_regime = .20f;
    p.body_drive = .045f;
    for (unsigned i = 0; i < MA_MACRO_COUNT; ++i) p.macro[i] = 0;
    return p;
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
    if (chosen) { metrics->tail_reuses++; return chosen; }
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

static float comb_tick(float buffer[COMB_CAPACITY], unsigned length,
                       unsigned *position, float *filtered, float input) {
    float delayed = buffer[*position];
    *filtered = .24f * delayed + .76f * *filtered;
    buffer[*position] = input + .90f * *filtered;
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
    reverb->input_lp.left += .07f * (dry.left - reverb->input_lp.left);
    reverb->input_lp.right += .07f * (dry.right - reverb->input_lp.right);
    float input_l = .76f * reverb->input_lp.left + .24f * reverb->input_lp.right;
    float input_r = .76f * reverb->input_lp.right + .24f * reverb->input_lp.left;
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
    reverb->wet_lp.left += .10f * (wet_l - reverb->wet_lp.left);
    reverb->wet_lp.right += .10f * (wet_r - reverb->wet_lp.right);
    reverb->wet_lp2.left += .10f * (reverb->wet_lp.left - reverb->wet_lp2.left);
    reverb->wet_lp2.right += .10f * (reverb->wet_lp.right - reverb->wet_lp2.right);
    return reverb->wet_lp2;
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

static bool score_validate(const performance_score *score,
                           const char **reason) {
    static const unsigned EXPECTED_NOTES[16] = {
        [0] = 704, [2] = 125, [4] = 224, [6] = 189, [9] = 248,
    };
    if (score->midi.format != 1 || score->midi.tracks != 6
        || score->midi.division != 480 || score->midi.event_count != 2980
        || score->midi.tempo_count != 1
        || score->midi.tempos[0].tick != 0
        || score->midi.tempos[0].us_per_quarter != 714285
        || score->midi.events[score->midi.event_count - 1].tick > BARS * TICKS_PER_BAR) {
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

static bool midi_load(smf_file *midi, uint8_t **bytes, const char *path,
                      uint16_t channel_mask, const char **reason) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        *reason = strerror(errno);
        return false;
    }
    bool ok = fseek(file, 0, SEEK_END) == 0;
    long length = ok ? ftell(file) : -1;
    ok = length > 0 && fseek(file, 0, SEEK_SET) == 0;
    *bytes = ok ? malloc((size_t)length) : 0;
    ok = ok && *bytes
       && fread(*bytes, 1, (size_t)length, file) == (size_t)length;
    bool closed = fclose(file) == 0;
    if (!ok || !closed) {
        *reason = "could not read MIDI file";
        return false;
    }
    smf_error error = { 0 };
    if (!smf_parse(*bytes, (size_t)length, channel_mask, midi, &error)) {
        static char message[160];
        snprintf(message, sizeof message, "MIDI error at byte %zu: %s",
                 error.offset, error.message ? error.message : "unknown error");
        *reason = message;
        return false;
    }
    return true;
}

static void score_dispose(performance_score *score) {
    smf_dispose(&score->midi);
    smf_dispose(&score->vocal);
    free(score->bytes);
    free(score->vocal_bytes);
    *score = (performance_score){ 0 };
}

static bool push_event(performance_score *score, uint32_t tick, part_id part,
                       uint8_t note, uint8_t velocity, bool on) {
    if (score->count == EVENT_CAPACITY || tick > BARS * TICKS_PER_BAR
        || note > 127)
        return false;
    score->event[score->count] = (arrangement_event){
        .tick = tick, .order = (uint32_t)score->count,
        .part = part, .note = note, .velocity = velocity, .on = on,
    };
    score->count++;
    return true;
}

static bool add_note(performance_score *score, uint32_t tick, uint32_t duration,
                     part_id part, uint8_t note, uint8_t velocity) {
    return push_event(score, tick, part, note, velocity, true)
        && push_event(score, tick + duration, part, note, 0, false);
}

static int compare_events(const void *a, const void *b) {
    const arrangement_event *x = a, *y = b;
    if (x->tick != y->tick) return x->tick < y->tick ? -1 : 1;
    if (x->on != y->on) return x->on ? 1 : -1;
    return (x->order > y->order) - (x->order < y->order);
}

static bool arrange_vocal(performance_score *score) {
    const smf_file *vocal = &score->vocal;
    /* The inspected full-score download puts eight bars before our backing.
     * Keep every vocal note, velocity, gate and rest after that fixed shift.
     * Only the performance tempo changes from 81 to the backing's 84 BPM. */
    constexpr uint64_t INTRO_TICKS = 8u * TICKS_PER_BAR;
    if (vocal->format != 1 || vocal->tracks != 11 || vocal->division != 480
        || vocal->tempo_count != 1 || vocal->tempos[0].tick != 0
        || vocal->tempos[0].us_per_quarter != 740740) return false;
    unsigned attacks = 0, releases = 0;
    bool ok = true;
    for (size_t i = 0; i < vocal->event_count; ++i) {
        const smf_event *e = &vocal->events[i];
        unsigned type = e->status & 0xf0u;
        if (type != 0x80u && type != 0x90u) continue;
        if ((e->status & 15u) != 0 || e->tick < INTRO_TICKS
            || e->tick - INTRO_TICKS > BARS * TICKS_PER_BAR
            || e->d1 < 45 || e->d1 > 69) return false;
        bool on = type == 0x90u && e->d2;
        if (on) attacks++; else releases++;
        ok &= push_event(score, (uint32_t)(e->tick - INTRO_TICKS), VOCAL,
                         e->d1, e->d2, on);
    }
    return ok && attacks == 200 && releases == 200;
}

/* Half-bar harmony follows source bass, then the lowest guitar note.
 * Voicings use the source guitar's most frequent pitch classes; a melody-only
 * passage borrows the most recent voicing with the same bass pitch class. */
static void derive_harmony(performance_score *score) {
    unsigned weight[BARS * 2][12] = { 0 };
    uint8_t low[BARS * 2] = { 0 }, bass[BARS * 2] = { 0 };
    for (size_t i = 0; i < score->midi.event_count; ++i) {
        const smf_event *e = &score->midi.events[i];
        if ((e->status & 0xf0u) != 0x90u || !e->d2) continue;
        size_t h = e->tick / (TICKS_PER_BAR / 2);
        if (h >= BARS * 2) continue;
        unsigned channel = e->status & 15u;
        if (channel == 4 && (!bass[h] || e->d1 < bass[h])) bass[h] = e->d1;
        if (channel != 0 && channel != 6) continue;
        if (!low[h] || e->d1 < low[h]) low[h] = e->d1;
        weight[h][e->d1 % 12]++;
    }
    for (unsigned h = 0; h < BARS * 2; ++h) {
        int root = bass[h] ? bass[h] : low[h] ? low[h] : h ? score->root[h-1] : 35;
        while (root > 42) root -= 12;
        while (root < 31) root += 12;
        score->root[h] = (uint8_t)root;
        unsigned total = 0;
        for (unsigned pc = 0; pc < 12; ++pc) total += weight[h][pc];
        if (!total && h) {
            unsigned previous = h - 1;
            for (unsigned j = h; j-- > 0;)
                if (score->root[j] % 12 == root % 12) { previous = j; break; }
            memcpy(score->chord[h], score->chord[previous], sizeof score->chord[h]);
            continue;
        }
        /* Keep the harmonic root, then two source tones above it. */
        score->chord[h][0] = (uint8_t)(root + 24);
        weight[h][root % 12] = 0;
        for (unsigned v = 1; v < 3; ++v) {
            unsigned pc = (unsigned)(root + 7) % 12;
            for (unsigned j = 0; j < 12; ++j)
                if (weight[h][j] > weight[h][pc]) pc = j;
            int note = 60 + (int)pc;
            if (note - score->chord[h][0] > 11) note -= 12;
            if (!weight[h][pc]) note = root + (v == 1 ? 31 : 36);
            score->chord[h][v] = (uint8_t)note;
            weight[h][pc] = 0;
        }
    }
}

static bool arrange(performance_score *score) {
    derive_harmony(score);
    bool ok = true;
    uint8_t texture_note[128] = { 0 };
    for (size_t i = 0; i < score->midi.event_count; ++i) {
        const smf_event *e = &score->midi.events[i];
        unsigned channel = e->status & 15u;
        if (channel == 4) continue; /* Revoiced below on the groove grid. */
        part_id part = channel == 0 ? PLUCK : channel == 2 ? MELODY
                     : channel == 6 ? TENOR : TEXTURE;
        bool on = (e->status & 0xf0u) == 0x90u && e->d2;
        uint8_t note = e->d1;
        if (part == TEXTURE) {
            unsigned h = (unsigned)(e->tick / 960);
            if (h >= BARS * 2) h = BARS * 2 - 1;
            if (on) texture_note[note] = (uint8_t)(score->root[h] + 24);
            note = texture_note[note];
        }
        if (part == MELODY) note -= 12;
        ok &= push_event(score, (uint32_t)e->tick, part, note, e->d2, on);
    }
    static const unsigned KICK_STEPS[] = { 0, 6, 11 };
    static const unsigned BASS_STEPS[] = { 2, 10 };
    for (unsigned bar = 0; bar < BARS; ++bar) {
        uint32_t at = bar * TICKS_PER_BAR;
        bool intro = bar < 8, breakdown = bar >= 40 && bar < 48;
        bool full = (bar >= 24 && bar < 40) || (bar >= 56 && bar < 80);
        bool climax = bar >= 64 && bar < 80, outro = bar >= 80;
        bool groove = !intro && !breakdown && !outro;
        for (unsigned half = 0; half < 2; ++half) {
            unsigned h = 2 * bar + half;
            uint32_t beat = at + half * 960;
            if (!(intro && bar < 4)) {
                /* Two held notes leave space for the previous pair's release. */
                for (unsigned v = 0; v < 2; ++v)
                    ok &= add_note(score, beat, 840, PAD,
                                   score->chord[h][v], climax ? 77 : 58);
            }
        }
        /* One diffuse harmonic swell before the late main-organ entrance.
         * A separate instrument leaves the established lead chain intact. */
        if (bar >= 64 && bar < 68)
            for (unsigned v = 0; v < 3; ++v)
                ok &= add_note(score, at + 120, 1680, HARMONY,
                               score->chord[2 * bar][v], 68);
        if (groove) {
            unsigned kick_count = full ? 3 : 2;
            for (unsigned j = 0; j < kick_count; ++j)
                ok &= add_note(score, at + 120 * KICK_STEPS[j], 96, KICK, 35,
                               (uint8_t)((j ? 94 : 113) + (climax ? 8 : 0)));
            ok &= add_note(score, at + 960, 260, PULSE,
                           (uint8_t)(score->root[2 * bar + 1] + 12), climax ? 92 : 78);
            for (unsigned j = 0; j < 2; ++j) {
                unsigned step = BASS_STEPS[j];
                unsigned h = 2 * bar + (step >= 8);
                uint8_t note = score->root[h];
                ok &= add_note(score, at + step * 120, 600,
                               BASS, note, (uint8_t)(j % 2 ? 84 : 97));
            }
        } else if ((intro && bar >= 4 && bar % 2 == 0)
                   || (outro && bar < 83)) {
            ok &= add_note(score, at, 120, KICK, 35, outro ? 78 : 67);
        }
    }
    ok &= arrange_vocal(score);
    qsort(score->event, score->count, sizeof *score->event, compare_events);
    return ok;
}

static bool check_arrangement(const performance_score *score) {
    unsigned held[PART_COUNT][128] = { 0 }, attacks[PART_COUNT] = { 0 };
    unsigned active[PART_COUNT] = { 0 }, peak[PART_COUNT] = { 0 };
    static const unsigned CAPACITY[] = { 6, 5, 1, 3, 5, 1, 1, 2, 3, 3 };
    for (size_t i = 0; i < score->count; ++i) {
        const arrangement_event *e = &score->event[i];
        bool organ = e->part == MELODY || e->part == HARMONY;
        if (organ && (e->note < 36 || e->note > 96)) return false;
        if (organ && e->on && held[e->part][e->note]) return false;
        if (i && compare_events(&score->event[i-1], e) > 0) return false;
        if (e->on) {
            held[e->part][e->note]++;
            attacks[e->part]++;
            active[e->part]++;
            if (active[e->part] > peak[e->part]) peak[e->part] = active[e->part];
        } else {
            if (!held[e->part][e->note]) {
                fprintf(stderr, "unowned %s release %u at tick %u\n",
                        PART_NAMES[e->part], e->note, e->tick);
                return false;
            }
            held[e->part][e->note]--;
            active[e->part]--;
        }
    }
    for (unsigned p = 0; p < PART_COUNT; ++p) {
        printf("  %-8s %4u attacks, %u held peak\n", PART_NAMES[p], attacks[p], peak[p]);
        if (active[p] || peak[p] > CAPACITY[p]) return false;
    }
    return true;
}

static ma_patch drum_patch(part_id part) {
    ma_patch p = { 0 };
    p.vco1.pulse_width = p.vco2.pulse_width = .5f;
    p.vco1.sine_level = .95f;
    p.vco1.triangle_level = part == KICK ? .18f : .30f;
    p.noise_level = 0;
    p.filter_cutoff_hz = part == KICK ? 125.0f : 330.0f;
    p.filter_resonance = part == KICK ? .24f : .12f;
    p.filter_drive = part == KICK ? .18f : .26f;
    p.filter_env_amount = part == KICK ? .62f : .26f;
    p.amp_adsr = part == KICK ? (ma_adsr){ 18, 210, 0, 270 }
                             : (ma_adsr){ 80, 480, .12f, 800 };
    p.filter_adsr = (ma_adsr){ 1, 65, 0, 100 };
    p.body_drive = part == KICK ? .13f : .23f;
    p.bcs_amount = part == KICK ? .08f : .18f;
    p.bcs_regime = .35f;
    p.master_level = part == KICK ? .48f : .33f;
    return dark_patch(p);
}

static void performance_init(performance *show) {
    *show = (performance){ .next_age = 1 };
    tw_instrument_init(&show->organ, RATE);
    static const uint8_t REGISTRATION[TW_DRAWBARS] = { 5, 0, 8, 3, 0, 0, 0, 0, 0 };
    tw_organ_set_registration(&show->organ.organ, REGISTRATION);
    tw_organ_set_wear(&show->organ.organ, 0);
    tw_organ_set_percussion(&show->organ.organ, false, false, false, true);
    tw_instrument_set_drive(&show->organ, .11f);
    tw_rotary_set_mode(&show->organ.rotary, TW_ROT_CHORALE);
    tw_rotary_set_balance(&show->organ.rotary, .38f);
    tw_rotary_set_width(&show->organ.rotary, .78f);
    tw_instrument_init(&show->harmony, RATE);
    static const uint8_t HARMONY_REGISTRATION[TW_DRAWBARS] = { 6, 0, 8, 4, 0, 0, 0, 0, 0 };
    tw_organ_set_registration(&show->harmony.organ, HARMONY_REGISTRATION);
    tw_organ_set_wear(&show->harmony.organ, 0);
    tw_organ_set_percussion(&show->harmony.organ, false, false, false, true);
    tw_instrument_set_drive(&show->harmony, .11f);
    tw_rotary_set_mode(&show->harmony.rotary, TW_ROT_CHORALE);
    tw_rotary_set_balance(&show->harmony.rotary, .32f);
    tw_rotary_set_width(&show->harmony.rotary, .95f);
    static const float PAN[] = { -.72f, -.43f, -.16f, .16f, .43f, .72f };
    static const float FINE[] = { -2.5f, 1.5f, -1, 1, -1.5f, 2.5f };
    for (unsigned i = 0; i < PLUCK_VOICES; ++i) {
        ma_patch p = dark_patch(pluck_patch(FINE[i]));
        ma_synth_init_patch(&show->pluck[i].synth, RATE, &p);
        show->pluck_pan[i] = make_pan(PAN[i], .135f);
    }
    ma_patch p = dark_patch(bass_patch());
    p.amp_adsr = (ma_adsr){ 45, 500, .60f, 700 };
    p.filter_adsr = (ma_adsr){ 35, 500, .20f, 650 };
    p.width = 0;
    ma_synth_init_patch(&show->bass.synth, RATE, &p);
    for (unsigned i = 0; i < TENOR_VOICES; ++i) {
        p = dark_patch(tenor_patch((float)i * 2 - 2));
        ma_synth_init_patch(&show->tenor[i].synth, RATE, &p);
        show->tenor_pan[i] = make_pan((float)i * .48f - .48f, .18f);
    }
    p = dark_patch(tenor_patch(-3));
    p.vco1.saw_level = .16f;
    p.vco1.triangle_level = .50f;
    p.vco1.sine_level = .34f;
    p.vco2.saw_level = .04f;
    p.vco2.triangle_level = .40f;
    p.vco2.sine_level = .38f;
    p.vco2_level = .38f;
    p.amp_adsr = (ma_adsr){ 28, 220, .72f, 420 };
    p.filter_adsr = (ma_adsr){ 18, 320, .30f, 500 };
    p.filter_cutoff_hz = 850;
    p.filter_resonance = .16f;
    p.filter_drive = .16f;
    p.filter_env_amount = .18f;
    p.bcs_amount = .18f;
    p.bcs_regime = .27f;
    p.body_drive = .14f;
    p.master_level = .14f;
    for (unsigned i = 0; i < 3; ++i)
        ma_synth_init_patch(&show->vocal[i].synth, RATE, &p);
    p = dark_patch(pad_patch(0, 0, 0, 0));
    p.amp_adsr = (ma_adsr){ 700, 2000, .78f, 2500 };
    p.filter_adsr = (ma_adsr){ 600, 2100, .40f, 2200 };
    ma_card_bank_init_patch(&show->pad, RATE, &p);
    ma_card_bank_set_character(&show->pad, .23f);
    p = dark_patch(pulse_patch(0, 0, 0, .20f));
    p.filter_cutoff_hz = 480;
    p.amp_adsr = (ma_adsr){ 230, 650, .38f, 1800 };
    p.filter_adsr = (ma_adsr){ 180, 800, .20f, 1600 };
    ma_card_bank_init_patch(&show->texture, RATE, &p);
    ma_card_bank_set_character(&show->texture, .16f);
    p = drum_patch(KICK);
    ma_synth_init_patch(&show->kick.synth, RATE, &p);
    p = drum_patch(PULSE);
    for (unsigned i = 0; i < 2; ++i)
        ma_synth_init_patch(&show->pulse[i].synth, RATE, &p);
}

typedef struct { float bar, energy; } section_control;
static const section_control SECTIONS[] = {
    { 0, .08f }, { 7, .18f }, { 8, .34f },
    { 23, .48f }, { 24, .65f }, { 38, .74f },
    { 40, .09f }, { 47, .12f }, { 48, .38f },
    { 56, .62f }, { 64, .82f }, { 68, .94f },
    { 78, 1.0f }, { 80, .62f }, { 83, .12f }, { 85, .06f },
};

static void update_controls(performance *show, size_t frame) {
    if (frame % 480 != 0) return;
    float bar = (float)((double)frame * 10000.0 / (714285.0 * TICKS_PER_BAR));
    size_t s = 0;
    while (s + 1 < sizeof SECTIONS / sizeof *SECTIONS && bar >= SECTIONS[s+1].bar) s++;
    section_control a = SECTIONS[s], b = a;
    if (s + 1 < sizeof SECTIONS / sizeof *SECTIONS) b = SECTIONS[s+1];
    float u = b.bar > a.bar ? (bar - a.bar) / (b.bar - a.bar) : 0;
    u = u * u * (3 - 2 * u);
    show->energy = a.energy + u * (b.energy - a.energy);
    float e = show->energy;
    ma_card_bank_set_output(&show->pad, .045f, .55f + .30f * e,
                            .18f, .125f);
    ma_card_bank_set_output(&show->texture, .045f, .45f + .25f * e,
                            .12f, .13f);
    for (unsigned i = 0; i < MA_CARD_COUNT; ++i) {
        ma_synth_set_lfo(&show->pad.card[i], .007f + .005f * e, .08f);
        ma_synth_set_lfo(&show->texture.card[i], .007f, .11f);
    }
    ma_synth_set_bcs(&show->bass.synth, .07f + .05f * e, .20f);
    /* Vocal color closes continuously through the form. Keep note timing,
     * pitches and gates untouched: only the Mamut filter/driven edge moves.
     * The eased progress leaves the opening readable, then concentrates the
     * darker cut toward the final chorus and outro. */
    float vocal_progress = bar / (float)BARS;
    if (vocal_progress < 0) vocal_progress = 0;
    if (vocal_progress > 1) vocal_progress = 1;
    vocal_progress = vocal_progress * vocal_progress
                   * (3.0f - 2.0f * vocal_progress);
    float vocal_cutoff = 850.0f - 610.0f * vocal_progress;
    float vocal_resonance = .16f + .05f * vocal_progress;
    float vocal_drive = .16f + .05f * vocal_progress;
    float vocal_pressure = .14f + .04f * vocal_progress;
    for (unsigned i = 0; i < 3; ++i)
        ma_synth_set_filter(&show->vocal[i].synth, vocal_cutoff,
                            vocal_resonance, vocal_drive, vocal_pressure);
}

static void apply_event(performance *show, const arrangement_event *e,
                        performance_metrics *metrics) {
    if (e->part == MELODY || e->part == HARMONY) {
        tw_organ *organ = e->part == MELODY ? &show->organ.organ : &show->harmony.organ;
        tw_organ_note(organ, e->note, e->on, e->velocity);
        if (e->part == HARMONY) {
            if (e->on) show->harmony_held++;
            else if (show->harmony_held) show->harmony_held--;
        }
        if (e->on) metrics->organ_attacks++;
        return;
    }
    ma_card_bank *bank = e->part == PAD ? &show->pad
                       : e->part == TEXTURE ? &show->texture : 0;
    if (bank) {
        if (e->on) {
            ma_card_owner before[MA_CARD_COUNT];
            memcpy(before, bank->owner, sizeof before);
            uint8_t slot = ma_card_bank_note_on(bank, (uint8_t)e->part, e->note, e->velocity);
            if (slot != MA_CARD_NONE) {
                if (before[slot].phase == MA_CARD_HELD) metrics->steals++;
                else if (before[slot].phase != MA_CARD_IDLE) metrics->tail_reuses++;
                metrics->synth_attacks++;
            }
        } else {
            (void)ma_card_bank_note_off(bank, (uint8_t)e->part, e->note, 0);
        }
        return;
    }
    fixed_voice *v = 0;
    size_t count = 1;
    switch (e->part) {
    case PLUCK: v = show->pluck; count = PLUCK_VOICES; break;
    case BASS: v = &show->bass; break;
    case TENOR: v = show->tenor; count = TENOR_VOICES; break;
    case VOCAL: v = show->vocal; count = 3; break;
    case KICK: v = &show->kick; break;
    case PULSE: v = show->pulse; count = 2; break;
    default: return;
    }
    if (e->on) {
        fixed_note_on(show, v, count, (uint8_t)e->part, e->note, e->velocity, metrics);
        if (e->part == KICK) show->kick_envelope = 1;
    } else {
        fixed_note_off(v, count, (uint8_t)e->part, e->note, 0);
    }
}

static void mix_fixed(ma_frame *mix, fixed_voice *v, pan_gain gain) {
    ma_frame x = ma_synth_tick(&v->synth);
    mix->left += x.left * gain.left;
    mix->right += x.right * gain.right;
}

static void mix_bank(ma_frame *mix, ma_card_bank *bank, float gain) {
    ma_frame x = ma_card_bank_tick_stereo(bank);
    mix->left += gain * x.left;
    mix->right += gain * x.right;
}

static void performance_tick(performance *show, ma_frame stem[STEM_COUNT]) {
    tw_stereo organ = tw_instrument_tick_stereo(&show->organ);
    show->organ_lp.left += .075f * (organ.l - show->organ_lp.left);
    show->organ_lp.right += .075f * (organ.r - show->organ_lp.right);
    stem[ORGAN_STEM] = (ma_frame){ .left = .0085f * show->organ_lp.left,
                                  .right = .0085f * show->organ_lp.right };
    tw_stereo harmony = tw_instrument_tick_stereo(&show->harmony);
    float target = show->harmony_held ? 1.0f : 0.0f;
    float envelope_rate = show->harmony_held ? .000026f : .000014f;
    show->harmony_envelope += envelope_rate * (target - show->harmony_envelope);
    show->harmony_lp.left += .055f * (harmony.l - show->harmony_lp.left);
    show->harmony_lp.right += .055f * (harmony.r - show->harmony_lp.right);
    float harmony_gain = .0045f * show->harmony_envelope;
    stem[HARMONY_STEM] = (ma_frame){ .left = harmony_gain * show->harmony_lp.left,
                                    .right = harmony_gain * show->harmony_lp.right };
    for (unsigned i = 0; i < PLUCK_VOICES; ++i)
        mix_fixed(&stem[TONAL_STEM], &show->pluck[i], show->pluck_pan[i]);
    for (unsigned i = 0; i < TENOR_VOICES; ++i)
        mix_fixed(&stem[TONAL_STEM], &show->tenor[i], show->tenor_pan[i]);
    for (unsigned i = 0; i < 3; ++i)
        mix_fixed(&stem[VOCAL_STEM], &show->vocal[i], (pan_gain){ .18f, .18f });
    mix_bank(&stem[TONAL_STEM], &show->pad, .12f + .035f * show->energy);
    mix_bank(&stem[TONAL_STEM], &show->texture, .08f + .04f * show->energy);
    float bass_gain = .45f * (1 - .20f * show->kick_envelope);
    mix_fixed(&stem[TONAL_STEM], &show->bass, (pan_gain){ bass_gain, bass_gain });
    /* The kick envelope gently clears bass transients; it never pumps the mix. */
    show->kick_envelope *= .99970f;
    mix_fixed(&stem[RHYTHM_STEM], &show->kick, (pan_gain){ .17f, .17f });
    for (unsigned i = 0; i < 2; ++i)
        mix_fixed(&stem[RHYTHM_STEM], &show->pulse[i], (pan_gain){ .30f, .30f });
}

static bool render_stream(const performance_score *score, wav_f32_writer writer[OUTPUT_COUNT],
                          size_t start, size_t frames, performance_metrics metrics[OUTPUT_COUNT]) {
    performance show = { 0 };
    performance_reverb reverb[STEM_COUNT] = { 0 };
    float block[OUTPUT_COUNT][2 * BLOCK_FRAMES] = { 0 };
    size_t next = 0, written = 0;
    /* Previews reconstruct ownership, then warm ten seconds of DSP history.
     * Full renders run every oscillator and character clock from frame zero. */
    size_t warm = start > 10u * RATE ? start - 10u * RATE : 0;
    size_t end = start + frames;
    performance_init(&show);
    for (unsigned s = 0; s < OUTPUT_COUNT; ++s)
        metrics[s] = (performance_metrics){ .hash = UINT64_C(14695981039346656037) };
    for (size_t frame = warm; frame < end; ++frame) {
        update_controls(&show, frame);
        while (next < score->count && tick_frame(score->event[next].tick) <= frame)
            apply_event(&show, &score->event[next++], &metrics[MIX_OUTPUT]);
        ma_frame stem[STEM_COUNT] = { 0 };
        performance_tick(&show, stem);
        /* Source groups own linear reverb returns, so delivered stems sum to mix. */
        const float send[] = { .48f + .08f * show.energy, .38f, .78f, .60f, 1.0f };
        float fade = frame > 250u * RATE ? (float)(253u * RATE - frame) / (3 * RATE) : 1;
        if (fade < 0) fade = 0;
        if (fade > 1) fade = 1;
        ma_frame mix = { 0 };
        for (unsigned s = 0; s < STEM_COUNT; ++s) {
            ma_frame wet = reverb_tick(&reverb[s], stem[s]);
            float direct = s == ORGAN_STEM ? .48f : s == HARMONY_STEM ? .20f : .72f;
            stem[s].left = fade * (direct * stem[s].left + send[s] * wet.left);
            stem[s].right = fade * (direct * stem[s].right + send[s] * wet.right);
            mix.left += stem[s].left;
            mix.right += stem[s].right;
        }
        if (frame < start) continue;
        for (unsigned s = 0; s < OUTPUT_COUNT; ++s) {
            ma_frame sample = s < STEM_COUNT ? stem[s] : mix;
            measure_sample(&metrics[s], &sample);
            block[s][2 * written] = sample.left;
            block[s][2 * written + 1] = sample.right;
            const unsigned char *bytes = (const unsigned char *)&block[s][2 * written];
            for (size_t j = 0; j < 2 * sizeof(float); ++j) {
                metrics[s].hash ^= bytes[j];
                metrics[s].hash *= UINT64_C(1099511628211);
            }
        }
        written++;
        if (written == BLOCK_FRAMES || frame + 1 == end) {
            for (unsigned s = 0; s < OUTPUT_COUNT; ++s)
                if (wav_f32_write(&writer[s], block[s], written) < 0) return false;
            written = 0;
        }
        if (frame % (10u * RATE) == 0) {
            fprintf(stderr, "  rendered %.1f / %.1f s\n", (frame-start)/(double)RATE,
                    frames/(double)RATE);
        }
    }
    for (unsigned s = 0; s < OUTPUT_COUNT; ++s)
        if (metrics[s].nonfinite || metrics[s].clipped) return false;
    return metrics[MIX_OUTPUT].steals == 0 && metrics[MIX_OUTPUT].peak > 1e-7f;
}

static bool render_files(const performance_score *score, const char *prefix,
                         size_t start, size_t frames, performance_metrics metrics[OUTPUT_COUNT]) {
    wav_f32_writer writer[OUTPUT_COUNT] = { 0 };
    char path[OUTPUT_COUNT][1024] = { 0 }, temporary[OUTPUT_COUNT][1040] = { 0 };
    bool ok = true;
    for (unsigned s = 0; s < OUTPUT_COUNT && ok; ++s) {
        int n = snprintf(path[s], sizeof path[s], "%s_%s.wav", prefix, STEM_NAMES[s]);
        ok = n > 0 && (size_t)n < sizeof path[s];
        if (!ok) break;
        memcpy(temporary[s], path[s], (size_t)n);
        memcpy(temporary[s] + n, ".ma-tmp", sizeof ".ma-tmp");
        ok = wav_f32_open(&writer[s], temporary[s], frames, RATE, 2) == 0;
    }
    if (ok) ok = render_stream(score, writer, start, frames, metrics);
    for (unsigned s = 0; s < OUTPUT_COUNT; ++s) {
        if (ok) ok = wav_f32_close(&writer[s]) == 0;
        else wav_f32_abort(&writer[s]);
    }
    if (ok) {
        for (unsigned s = 0; s < OUTPUT_COUNT; ++s)
            if (rename(temporary[s], path[s]) != 0) { ok = false; break; }
    }
    if (!ok)
        for (unsigned s = 0; s < OUTPUT_COUNT; ++s)
            if (*temporary[s]) (void)remove(temporary[s]);
    return ok;
}

static int usage(const char *program, int status) {
    fprintf(status ? stderr : stdout,
            "usage: %s [-i Hurt.mid] [-v vocals.mid] [-o prefix] [-s start_seconds] [-d seconds] [--check]\n"
            "  six aligned float32 WAVs: prefix_{tonal,rhythm,organ,vocal,harmony,mix}.wav\n"
            "  defaults: start 0, duration 253; previews warm ten seconds\n", program);
    return status;
}

int main(int argc, char **argv) {
    const char *input = DEFAULT_INPUT, *vocal = DEFAULT_VOCAL, *output = DEFAULT_OUTPUT;
    double start = 0, seconds = DEFAULT_SECONDS;
    bool check = false;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-i") && i + 1 < argc) input = argv[++i];
        else if (!strcmp(argv[i], "-v") && i + 1 < argc) vocal = argv[++i];
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) output = argv[++i];
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) {
            if (!host_parse_double(argv[++i], 0, DEFAULT_SECONDS, &start)) return usage(argv[0], 2);
        } else if (!strcmp(argv[i], "-d") && i + 1 < argc) {
            if (!host_parse_double(argv[++i], 0, DEFAULT_SECONDS, &seconds) || seconds <= 0)
                return usage(argv[0], 2);
        } else if (!strcmp(argv[i], "--check")) check = true;
        else if (!strcmp(argv[i], "-h")) return usage(argv[0], 0);
        else return usage(argv[0], 2);
    }
    if (start + seconds > DEFAULT_SECONDS) return usage(argv[0], 2);
    performance_score *score = calloc(1, sizeof *score);
    if (!score) return 1;
    const char *reason = 0;
    bool ok = midi_load(&score->midi, &score->bytes, input, UINT16_MAX, &reason)
           && score_validate(score, &reason);
    if (ok) ok = midi_load(&score->vocal, &score->vocal_bytes, vocal, 1u, &reason);
    if (ok) ok = arrange(score) && check_arrangement(score);
    size_t first = (size_t)(start * RATE + .5), frames = (size_t)(seconds * RATE + .5);
    if (!frames) ok = false;
    performance_metrics metrics[OUTPUT_COUNT] = { 0 };
    if (ok && !check) {
        ok = render_files(score, output, first, frames, metrics);
        for (unsigned s = 0; s < OUTPUT_COUNT; ++s)
            printf("  %s: peak %.8f, RMS %.8f, nonfinite %u, clipped %u, FNV64 %016llx\n",
                   STEM_NAMES[s], (double)metrics[s].peak,
                   sqrt(metrics[s].sum_squares / (2.0 * frames)),
                   metrics[s].nonfinite, metrics[s].clipped,
                   (unsigned long long)metrics[s].hash);
        printf("  MA attacks %u, organ attacks %u, held steals %u, release reuses %u\n",
               metrics[MIX_OUTPUT].synth_attacks, metrics[MIX_OUTPUT].organ_attacks,
               metrics[MIX_OUTPUT].steals, metrics[MIX_OUTPUT].tail_reuses);
    }
    if (!ok) fprintf(stderr, "Hurt arrangement failed: %s\n", reason ? reason : "schedule, render or output check");
    score_dispose(score);
    free(score);
    return ok ? 0 : 1;
}
