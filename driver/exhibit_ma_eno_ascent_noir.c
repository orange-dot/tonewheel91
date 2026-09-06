/* Hosted Mamut Analog arrangement of a user-supplied two-line MIDI study.
 * The source remains local; this exhibit supplies the MA instrumentation. */
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/mamutanalog.h"
#include "ma_dark_lead.h"
#include "smf.h"
#include "wav.h"

enum {
    RATE = 48000,
    RENDER_SECONDS = 180,
    FRAME_COUNT = RATE * RENDER_SECONDS,
    CYCLES = 2,
    BLOCK_FRAMES = 4096,
    COMB_COUNT = 4,
    COMB_CAPACITY = 1536,
    ALLPASS_COUNT = 2,
    ALLPASS_CAPACITY = 640,
};

static constexpr char DEFAULT_INPUT[] =
    "notes-midi/local/brian-eno-an-ending-ascent.mid";
static constexpr char DEFAULT_OUTPUT[] =
    "build/ma_eno_an_ending_ascent_noir.wav";

typedef enum {
    ROLE_PAD_LEFT,
    ROLE_PAD_RIGHT,
    ROLE_HAZE,
    ROLE_BASS,
    ROLE_MOTION,
    ROLE_LEAD,
    ROLE_COUNT,
} voice_role;

typedef struct {
    ma_card_bank bank[ROLE_COUNT];
    uint8_t pad_role[128];
    unsigned harmony_index;
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
    unsigned synth_notes;
    unsigned steals;
    unsigned peak_voices;
    unsigned nonfinite;
    unsigned clipped;
} performance_metrics;

typedef struct {
    uint8_t *bytes;
    smf_file midi;
    size_t *event_frame;
    size_t cycle_frames;
} performance_score;

static ma_patch patch_for(voice_role role) {
    if (role == ROLE_LEAD) return ma_dark_lead_patch();

    ma_patch patch = role == ROLE_BASS || role == ROLE_MOTION
                   ? ma_patch_dubina : ma_patch_tepih;
    if (role == ROLE_PAD_LEFT || role == ROLE_PAD_RIGHT) {
        patch.filter_cutoff_hz = 720.0f;
        patch.amp_adsr = (ma_adsr){ 1200.0f, 2200.0f, .76f, 4800.0f };
        patch.filter_adsr.release_ms = 4400.0f;
        patch.width = .86f;
        patch.master_level = .14f;
    } else if (role == ROLE_HAZE) {
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
        patch.mozaik_mix = .12f;
        patch.filter_cutoff_hz = 980.0f;
        patch.filter_resonance = .12f;
        patch.filter_drive = .07f;
        patch.filter_env_amount = .20f;
        patch.amp_adsr = (ma_adsr){ 1400.0f, 2400.0f, .74f, 6800.0f };
        patch.filter_adsr = (ma_adsr){ 1200.0f, 2600.0f, .42f, 5800.0f };
        patch.macro[MA_MACRO_BLOOM] = .18f;
        patch.macro[MA_MACRO_SWARM] = .08f;
        patch.body_drive = .06f;
        patch.width = .92f;
        patch.master_level = .12f;
    } else if (role == ROLE_BASS) {
        patch.filter_cutoff_hz = 420.0f;
        patch.amp_adsr.release_ms = 2800.0f;
        patch.master_level = .14f;
    } else {
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
        patch.amp_adsr = (ma_adsr){ 80.0f, 520.0f, .72f, 2400.0f };
        patch.filter_adsr = (ma_adsr){ 120.0f, 720.0f, .36f, 2200.0f };
        patch.macro[MA_MACRO_BLOOM] = .05f;
        patch.macro[MA_MACRO_HEAT] = .08f;
        patch.body_drive = .10f;
        patch.master_level = .12f;
    }
    return patch;
}

static void performance_init(performance *show) {
    *show = (performance){ 0 };
    for (unsigned role = 0; role < ROLE_COUNT; ++role) {
        ma_patch patch = patch_for((voice_role)role);
        ma_card_bank_init_patch(&show->bank[role], RATE, &patch);
    }
    ma_card_bank_set_unison(&show->bank[ROLE_LEAD], true);
    for (unsigned slot = 0; slot < MA_CARD_COUNT; ++slot) {
        ma_synth *lead = &show->bank[ROLE_LEAD].card[slot];
        ma_synth_set_glide(lead, true, 1.15f);
        ma_synth_set_lfo(lead, .075f, 3.65f);
        ma_synth_set_channel_pressure(lead, .44f);
        ma_synth_set_mod_wheel(lead, .37f);
    }
}

static bool bank_full(const ma_card_bank *bank) {
    for (unsigned slot = 0; slot < MA_CARD_COUNT; ++slot)
        if (bank->owner[slot].phase == MA_CARD_IDLE) return false;
    return true;
}

static uint8_t start_note(performance *show, voice_role role, uint8_t channel,
                          uint8_t note, uint8_t velocity,
                          performance_metrics *metrics) {
    ma_card_bank *bank = &show->bank[role];
    bool full = bank_full(bank);
    uint8_t slot = ma_card_bank_note_on(bank, channel, note, velocity);
    if (slot == MA_CARD_NONE) return slot;
    metrics->synth_notes += role == ROLE_LEAD ? MA_CARD_COUNT : 1u;
    metrics->steals += full && !bank->unison;
    float pressure = role == ROLE_LEAD ? .44f
                   : role <= ROLE_HAZE ? .20f : .28f;
    if (bank->unison) {
        for (unsigned i = 0; i < MA_CARD_COUNT; ++i) {
            ma_synth_set_channel_pressure(&bank->card[i], pressure);
            ma_synth_set_poly_pressure(&bank->card[i], channel, note, pressure);
        }
    } else {
        ma_synth_set_channel_pressure(&bank->card[slot], pressure);
        ma_synth_set_poly_pressure(&bank->card[slot], channel, note, pressure);
    }
    return slot;
}

static void stop_note(performance *show, voice_role role, uint8_t channel,
                      uint8_t note, uint8_t velocity) {
    (void)ma_card_bank_note_off(&show->bank[role], channel, note, velocity);
}

static void chord_voicing(uint8_t source, uint8_t note[4]) {
    switch (source % 12u) {
    case 0:
        memcpy(note, (uint8_t[4]){ 48, 55, 60, 64 }, 4);
        break;
    case 7:
        memcpy(note, (uint8_t[4]){ 43, 50, 55, 59 }, 4);
        break;
    case 2:
        memcpy(note, (uint8_t[4]){ 50, 57, 62, 66 }, 4);
        break;
    default: {
        uint8_t root = source >= 12u ? (uint8_t)(source - 12u) : source;
        memcpy(note, (uint8_t[4]){
            root, (uint8_t)(root + 7u),
            (uint8_t)(root + 12u), (uint8_t)(root + 16u),
        }, 4);
        break;
    }
    }
}

static void harmony_on(performance *show, uint8_t source, uint8_t velocity,
                       unsigned cycle, performance_metrics *metrics) {
    voice_role pad = show->harmony_index++ % 2u
                   ? ROLE_PAD_RIGHT : ROLE_PAD_LEFT;
    show->pad_role[source] = (uint8_t)pad;
    uint8_t chord[4];
    chord_voicing(source, chord);
    uint8_t pad_velocity = (uint8_t)(cycle ? 70u : 60u);
    for (unsigned i = 0; i < 4; ++i)
        (void)start_note(show, pad, 2, chord[i], pad_velocity, metrics);

    uint8_t bass = source >= 24u ? (uint8_t)(source - 24u) : source;
    (void)start_note(show, ROLE_BASS, 2, bass,
                     (uint8_t)(cycle ? 88u : 78u), metrics);

    if (cycle || source % 12u == 2u) {
        uint8_t haze = chord[3] <= 115u ? (uint8_t)(chord[3] + 12u)
                                       : chord[3];
        (void)start_note(show, ROLE_HAZE, 2, haze,
                         (uint8_t)(cycle ? 58u : 48u), metrics);
    }
    if (cycle) {
        uint8_t motion = source >= 12u ? (uint8_t)(source - 12u) : source;
        (void)start_note(show, ROLE_MOTION, 2, motion,
                         velocity > 72u ? 72u : velocity, metrics);
    }
}

static void harmony_off(performance *show, uint8_t source, uint8_t velocity,
                        unsigned cycle) {
    voice_role pad = (voice_role)show->pad_role[source];
    uint8_t chord[4];
    chord_voicing(source, chord);
    for (unsigned i = 0; i < 4; ++i)
        stop_note(show, pad, 2, chord[i], velocity);

    uint8_t bass = source >= 24u ? (uint8_t)(source - 24u) : source;
    stop_note(show, ROLE_BASS, 2, bass, velocity);
    if (cycle || source % 12u == 2u) {
        uint8_t haze = chord[3] <= 115u ? (uint8_t)(chord[3] + 12u)
                                       : chord[3];
        stop_note(show, ROLE_HAZE, 2, haze, velocity);
    }
    if (cycle) {
        uint8_t motion = source >= 12u ? (uint8_t)(source - 12u) : source;
        stop_note(show, ROLE_MOTION, 2, motion, velocity);
    }
}

static void apply_event(performance *show, const smf_event *event,
                        unsigned cycle, performance_metrics *metrics) {
    uint8_t type = event->status & 0xf0u;
    uint8_t channel = event->status & 0x0fu;
    bool note_on = type == 0x90u && event->d2 != 0;
    bool note_off = type == 0x80u || (type == 0x90u && event->d2 == 0);
    if (channel == 0u && note_on) {
        metrics->midi_notes++;
        (void)start_note(show, ROLE_LEAD, channel, event->d1, event->d2,
                         metrics);
    } else if (channel == 0u && note_off) {
        stop_note(show, ROLE_LEAD, channel, event->d1, event->d2);
    } else if (channel == 2u && note_on) {
        metrics->midi_notes++;
        harmony_on(show, event->d1, event->d2, cycle, metrics);
    } else if (channel == 2u && note_off) {
        harmony_off(show, event->d1, event->d2, cycle);
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
    float wet_l = 0.0f, wet_r = 0.0f;
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
    return (ma_frame){
        .left = .72f * dry.left + .48f * wet_l,
        .right = .72f * dry.right + .48f * wet_r,
    };
}

static ma_frame performance_tick(performance *show,
                                 performance_metrics *metrics) {
    static const float PAN[ROLE_COUNT] = {
        -.48f, .48f, .72f, 0.0f, -.16f, .03f,
    };
    static const float GAIN[ROLE_COUNT] = {
        .25f, .25f, .14f, .46f, .26f, .14f,
    };
    ma_frame mixed = { 0 };
    unsigned active = 0;
    for (unsigned role = 0; role < ROLE_COUNT; ++role) {
        ma_frame card[MA_CARD_COUNT];
        ma_card_bank_tick(&show->bank[role], card);
        float left = sqrtf(.5f * (1.0f - PAN[role]));
        float right = sqrtf(.5f * (1.0f + PAN[role]));
        for (unsigned slot = 0; slot < MA_CARD_COUNT; ++slot) {
            float mono = .5f * (card[slot].left + card[slot].right);
            mixed.left += GAIN[role] * left * mono;
            mixed.right += GAIN[role] * right * mono;
            active += show->bank[role].owner[slot].phase != MA_CARD_IDLE;
        }
    }
    if (active > metrics->peak_voices) metrics->peak_voices = active;
    return mixed;
}

static bool metrics_equal(const performance_metrics *a,
                          const performance_metrics *b) {
    return a->hash == b->hash && a->sum_squares == b->sum_squares
        && a->peak == b->peak && a->midi_notes == b->midi_notes
        && a->synth_notes == b->synth_notes && a->steals == b->steals
        && a->peak_voices == b->peak_voices
        && a->nonfinite == b->nonfinite && a->clipped == b->clipped;
}

static bool render_pass(const performance_score *score, wav_f32_writer *writer,
                        performance_metrics *metrics) {
    performance show;
    performance_reverb reverb = { 0 };
    float block[2u * BLOCK_FRAMES];
    size_t next = 0;
    unsigned cycle = 0;
    performance_init(&show);
    *metrics = (performance_metrics){ 0 };

    for (size_t first = 0; first < FRAME_COUNT; first += BLOCK_FRAMES) {
        size_t count = FRAME_COUNT - first;
        if (count > BLOCK_FRAMES) count = BLOCK_FRAMES;
        for (size_t i = 0; i < count; ++i) {
            size_t frame = first + i;
            while (cycle < CYCLES) {
                size_t event_at = score->event_frame[next]
                                + cycle * score->cycle_frames;
                if (event_at > frame) break;
                apply_event(&show, &score->midi.events[next], cycle, metrics);
                if (++next == score->midi.event_count) {
                    next = 0;
                    ++cycle;
                }
            }
            ma_frame sample = reverb_tick(&reverb,
                                          performance_tick(&show, metrics));
            sample.left *= 1.90f;
            sample.right *= 1.90f;
            if (!isfinite(sample.left) || !isfinite(sample.right)) {
                metrics->nonfinite++;
                sample = (ma_frame){ 0 };
            }
            float left = fabsf(sample.left);
            float right = fabsf(sample.right);
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
        if (writer && wav_f32_write(writer, block, count) < 0) return false;
    }
    return cycle == CYCLES && metrics->nonfinite == 0
        && metrics->clipped == 0 && metrics->peak > 1e-4f;
}

static bool event_frames(performance_score *score, const char **reason) {
    score->event_frame = score->midi.event_count
                       ? malloc(score->midi.event_count
                                * sizeof *score->event_frame)
                       : 0;
    if (score->midi.event_count && !score->event_frame) {
        *reason = "out of memory for MIDI timeline";
        return false;
    }
    double seconds = 0.0;
    uint64_t tick0 = 0;
    uint32_t us_per_quarter = 500000;
    size_t tempo = 0;
    size_t last = 0;
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
        if (!isfinite(at) || at < 0.0 || at >= RENDER_SECONDS / CYCLES) {
            *reason = "MIDI cycle is invalid or too long for 180 seconds";
            return false;
        }
        score->event_frame[i] = (size_t)(at * RATE + .5);
        last = score->event_frame[i];
    }
    if (!score->midi.event_count || !last || last > FRAME_COUNT / CYCLES) {
        *reason = "MIDI has no usable performance span";
        return false;
    }
    score->cycle_frames = last;
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
    ok = length >= 0 && fseek(file, 0, SEEK_SET) == 0;
    score->bytes = ok && length ? malloc((size_t)length) : 0;
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
    if (score->midi.division != 480u) {
        *reason = "this exhibit expects the supplied 480 PPQ MIDI";
        return false;
    }
    for (size_t i = 0; i < score->midi.event_count; ++i) {
        uint8_t channel = score->midi.events[i].status & 0x0fu;
        if (channel != 0u && channel != 2u) {
            *reason = "this exhibit expects melody on channel 1 and harmony on channel 3";
            return false;
        }
    }
    return event_frames(score, reason);
}

static void score_dispose(performance_score *score) {
    free(score->event_frame);
    smf_dispose(&score->midi);
    free(score->bytes);
    *score = (performance_score){ 0 };
}

static bool render_file(const performance_score *score, const char *output,
                        performance_metrics *metrics, const char **reason) {
    performance_metrics first = { 0 };
    if (!render_pass(score, 0, &first)) {
        *reason = "first render pass failed safety checks";
        return false;
    }

    size_t size = strlen(output);
    if (size > SIZE_MAX - sizeof ".ma-tmp") {
        *reason = "output path is too long";
        return false;
    }
    char *temporary = malloc(size + sizeof ".ma-tmp");
    if (!temporary) {
        *reason = "could not allocate temporary output path";
        return false;
    }
    memcpy(temporary, output, size);
    memcpy(temporary + size, ".ma-tmp", sizeof ".ma-tmp");
    wav_f32_writer writer = { 0 };
    if (wav_f32_open(&writer, temporary, FRAME_COUNT, RATE, 2) < 0) {
        free(temporary);
        *reason = "could not open temporary WAV";
        return false;
    }
    performance_metrics second = { 0 };
    bool rendered = render_pass(score, &writer, &second);
    bool closed = rendered && wav_f32_close(&writer) == 0;
    if (!closed || !metrics_equal(&first, &second)) {
        wav_f32_abort(&writer);
        (void)remove(temporary);
        free(temporary);
        *reason = closed ? "render passes differ" : "second render pass failed";
        return false;
    }
    if (rename(temporary, output) < 0) {
        (void)remove(temporary);
        free(temporary);
        *reason = "could not publish completed WAV";
        return false;
    }
    free(temporary);
    *metrics = second;
    return true;
}

static int usage(const char *program, int status) {
    fprintf(status ? stderr : stdout,
            "usage: %s [-i input.mid] [-o output.wav]\n", program);
    return status;
}

int main(int argc, char **argv) {
    const char *input = DEFAULT_INPUT;
    const char *output = DEFAULT_OUTPUT;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-i") && i + 1 < argc)
            input = argv[++i];
        else if (!strcmp(argv[i], "-o") && i + 1 < argc)
            output = argv[++i];
        else if (!strcmp(argv[i], "-h"))
            return usage(argv[0], 0);
        else
            return usage(argv[0], 2);
    }

    performance_score score;
    const char *reason = 0;
    if (!score_load(&score, input, &reason)) {
        fprintf(stderr, "%s: %s\n", input, reason ? reason : "load failed");
        score_dispose(&score);
        return 1;
    }
    performance_metrics metrics = { 0 };
    bool ok = render_file(&score, output, &metrics, &reason);
    double cycle_seconds = score.cycle_frames / (double)RATE;
    double tail_seconds = RENDER_SECONDS - CYCLES * cycle_seconds;
    double rms = sqrt(metrics.sum_squares / (2.0 * FRAME_COUNT));
    printf("Mamut Analog — An Ending (Ascent), noir arrangement\n");
    printf("  %d s: 2 x %.3f s MIDI + %.3f s release/reverb tail\n",
           RENDER_SECONDS, cycle_seconds, tail_seconds);
    printf("  %u MIDI notes, %u MA card attacks, %u-voice peak, %u steals\n",
           metrics.midi_notes, metrics.synth_notes,
           metrics.peak_voices, metrics.steals);
    printf("  lead: dark patch, five-card unison, 1.15 s glide, 3.65 Hz LFO\n");
    printf("  peak %.6f, RMS %.6f, finite %s, headroom %s\n",
           (double)metrics.peak, rms,
           metrics.nonfinite == 0 ? "yes" : "NO",
           metrics.clipped == 0 ? "yes" : "NO");
    printf("  FNV64 %016llx %s\n", (unsigned long long)metrics.hash,
           ok ? "(two runs identical)" : "FAILED");
    printf("  wav: %s%s\n", output, ok ? "" : " (FAILED)");
    if (!ok) fprintf(stderr, "%s\n", reason ? reason : "render failed");
    score_dispose(&score);
    return ok ? 0 : 1;
}
