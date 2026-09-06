/* Hosted MA2-DIG listening exhibit for the Raster wavetable source and its
 * hybrid Prizma factory patch. */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "../src/mamutanalog.h"
#include "wav.h"

enum {
    RATE = 48000,
    MUSIC_SECONDS = 48,
    TAIL_SECONDS = 8,
    FRAME_COUNT = RATE * (MUSIC_SECONDS + TAIL_SECONDS),
    BLOCK_FRAMES = 256,
    CHORD_SECONDS = 4,
};

static constexpr char OUTPUT_PATH[] = "build/ma2_dig_raster.wav";

static constexpr uint8_t CHORDS[][4] = {
    { 36, 48, 55, 63 }, { 34, 46, 53, 62 }, { 31, 43, 50, 58 },
    { 32, 44, 51, 60 }, { 36, 48, 55, 60 }, { 39, 51, 58, 63 },
    { 34, 46, 53, 60 }, { 31, 43, 50, 55 }, { 29, 41, 48, 56 },
    { 32, 44, 51, 59 }, { 36, 48, 55, 63 }, { 31, 43, 50, 58 },
};

static constexpr uint8_t RASTER_LINE[] = {
    48, 55, 60, 63, 67, 63, 60, 55,
    46, 53, 58, 62, 65, 62, 58, 53,
    43, 50, 55, 58, 62, 58, 55, 50,
    44, 51, 56, 60, 63, 60, 56, 51,
};

typedef struct {
    ma_card_bank raster;
    ma_card_bank prizma;
    uint64_t hash;
    double sum_squares;
    float peak;
    unsigned nonfinite;
    unsigned clipped;
    uint8_t chord_index;
    uint8_t raster_note;
    bool chord_active;
    bool raster_active;
} performance;

typedef struct {
    uint64_t hash;
    double sum_squares;
    float peak;
    unsigned nonfinite;
    unsigned clipped;
} metrics;

static void set_bank_raster(ma_card_bank *bank, float mix, float position,
                            float warp) {
    for (uint8_t slot = 0; slot < MA_CARD_COUNT; slot++)
        ma_synth_set_raster(&bank->card[slot], mix, position, warp);
}

static void performance_init(performance *p) {
    *p = (performance){ 0 };
    ma_card_bank_init_patch(&p->raster, RATE, &ma_patch_raster);
    ma_card_bank_init_patch(&p->prizma, RATE, &ma_patch_prizma);
    for (uint8_t slot = 0; slot < MA_CARD_COUNT; slot++) {
        ma_synth_set_lfo(&p->raster.card[slot], 0.035f, 0.17f);
        ma_synth_set_lfo(&p->prizma.card[slot], 0.018f, 0.11f);
    }
}

static void release_chord(performance *p) {
    if (!p->chord_active) return;
    uint8_t const *notes = CHORDS[p->chord_index];
    for (uint8_t voice = 0; voice < 4; voice++)
        (void)ma_card_bank_note_off(&p->prizma, 0, notes[voice], 0);
    p->chord_active = false;
}

static void start_chord(performance *p, uint8_t index) {
    release_chord(p);
    p->chord_index = index;
    for (uint8_t voice = 0; voice < 4; voice++)
        (void)ma_card_bank_note_on(&p->prizma, 0, CHORDS[index][voice],
                                   (uint8_t)(72 + 4 * voice));
    p->chord_active = true;
}

static void start_raster_note(performance *p, uint8_t note) {
    if (p->raster_active)
        (void)ma_card_bank_note_off(&p->raster, 1, p->raster_note, 0);
    (void)ma_card_bank_note_on(&p->raster, 1, note, 104);
    p->raster_note = note;
    p->raster_active = true;
}

static void run_events(performance *p, size_t frame) {
    static constexpr size_t chord_frames = CHORD_SECONDS * RATE;
    static constexpr size_t raster_frames = RATE / 2;
    if (frame < MUSIC_SECONDS * (size_t)RATE
        && frame % chord_frames == 0) {
        uint8_t index = (uint8_t)(frame / chord_frames);
        start_chord(p, index);
    }
    if (frame < MUSIC_SECONDS * (size_t)RATE
        && frame % raster_frames == 0) {
        size_t step = frame / raster_frames;
        uint8_t note = RASTER_LINE[step % (sizeof RASTER_LINE
                                           / sizeof *RASTER_LINE)];
        if ((step / 32) & 1u) note = (uint8_t)(note - 12);
        start_raster_note(p, note);
    }
    if (frame == MUSIC_SECONDS * (size_t)RATE) {
        release_chord(p);
        if (p->raster_active)
            (void)ma_card_bank_note_off(&p->raster, 1, p->raster_note, 0);
        p->raster_active = false;
    }
    if (frame % BLOCK_FRAMES == 0) {
        float turn = (float)(frame % (16u * RATE)) / (16.0f * RATE);
        float scan = 0.5f + 0.5f * tw_sin_turns(turn);
        float counter = 0.5f + 0.5f * tw_sin_turns(turn + 0.25f);
        set_bank_raster(&p->raster, 1.0f,
                        0.06f + 0.90f * scan,
                        0.12f + 0.62f * counter);
        set_bank_raster(&p->prizma, 0.48f,
                        0.18f + 0.56f * counter,
                        0.08f + 0.24f * scan);
    }
}

static ma_frame mix_bank(const ma_frame card[MA_CARD_COUNT], float gain,
                         bool reverse) {
    static constexpr float pan[MA_CARD_COUNT] = {
        -0.78f, -0.38f, 0.0f, 0.38f, 0.78f,
    };
    ma_frame output = { 0 };
    for (uint8_t slot = 0; slot < MA_CARD_COUNT; slot++) {
        float position = reverse ? -pan[slot] : pan[slot];
        float mono = 0.5f * (card[slot].left + card[slot].right);
        output.left += gain * mono * (0.75f - 0.25f * position);
        output.right += gain * mono * (0.75f + 0.25f * position);
    }
    return output;
}

static ma_frame performance_tick(performance *p, size_t frame) {
    run_events(p, frame);
    ma_frame raster_cards[MA_CARD_COUNT], prizma_cards[MA_CARD_COUNT];
    ma_card_bank_tick(&p->raster, raster_cards);
    ma_card_bank_tick(&p->prizma, prizma_cards);
    ma_frame raster = mix_bank(raster_cards, 0.62f, false);
    ma_frame prizma = mix_bank(prizma_cards, 0.48f, true);
    ma_frame output = {
        .left = 1.6f * (raster.left + prizma.left),
        .right = 1.6f * (raster.right + prizma.right),
    };
    size_t fade_start = (MUSIC_SECONDS + TAIL_SECONDS - 3u) * (size_t)RATE;
    if (frame > fade_start) {
        float fade = (float)(FRAME_COUNT - frame)
                   / (float)(FRAME_COUNT - fade_start);
        output.left *= fade;
        output.right *= fade;
    }
    return output;
}

static bool render_pass(char const *name, metrics *result,
                        wav_f32_writer *writer) {
    performance p;
    performance_init(&p);
    float block[2 * BLOCK_FRAMES] = { 0 };
    unsigned next_progress = 10;
    printf("  %s: 0%%", name);
    fflush(stdout);
    for (size_t first = 0; first < FRAME_COUNT; first += BLOCK_FRAMES) {
        size_t count = FRAME_COUNT - first;
        if (count > BLOCK_FRAMES) count = BLOCK_FRAMES;
        for (size_t i = 0; i < count; i++) {
            ma_frame sample = performance_tick(&p, first + i);
            if (!isfinite(sample.left) || !isfinite(sample.right)) {
                p.nonfinite++;
                sample = (ma_frame){ 0 };
            }
            float left = fabsf(sample.left), right = fabsf(sample.right);
            float peak = left > right ? left : right;
            if (peak > p.peak) p.peak = peak;
            p.clipped += peak > 1.0f;
            p.sum_squares += (double)sample.left * sample.left
                           + (double)sample.right * sample.right;
            block[2 * i] = sample.left;
            block[2 * i + 1] = sample.right;
        }
        p.hash = tw_fnv1a64(block, 2 * count * sizeof *block, p.hash);
        if (writer && wav_f32_write(writer, block, count) < 0) return false;
        unsigned progress = (unsigned)((first + count) * 100 / FRAME_COUNT);
        if (progress >= next_progress) {
            printf(" ... %u%%", next_progress);
            fflush(stdout);
            next_progress += 10;
        }
    }
    putchar('\n');
    *result = (metrics){
        .hash = p.hash,
        .sum_squares = p.sum_squares,
        .peak = p.peak,
        .nonfinite = p.nonfinite,
        .clipped = p.clipped,
    };
    return p.nonfinite == 0 && p.clipped == 0
        && p.peak > 1.0e-4f && p.sum_squares > 1.0e-8;
}

int main(void) {
    puts("Mamut Analog MA2-DIG — Raster / Prizma");
    puts("  pure digital Raster line and hybrid Prizma five-card pad");
    metrics first = { 0 }, second = { 0 };
    bool first_ok = render_pass("verify", &first, nullptr);
    wav_f32_writer writer = { 0 };
    bool opened = first_ok
               && wav_f32_open(&writer, OUTPUT_PATH, FRAME_COUNT, RATE, 2) == 0;
    bool second_ok = opened && render_pass("write ", &second, &writer);
    bool deterministic = second_ok && first.hash == second.hash;
    bool written = deterministic && wav_f32_close(&writer) == 0;
    if (!written) wav_f32_abort(&writer);
    double rms = sqrt(second.sum_squares / (2.0 * FRAME_COUNT));
    printf("  peak %.6f, RMS %.6f, finite %s, headroom %s\n",
           (double)second.peak, rms,
           second.nonfinite == 0 ? "yes" : "NO",
           second.clipped == 0 ? "yes" : "NO");
    printf("  FNV64 %016llx %s\n", (unsigned long long)second.hash,
           deterministic ? "(two runs identical)" : "MISMATCH");
    printf("  wav: %s%s\n", OUTPUT_PATH, written ? "" : " (FAILED)");
    return written ? 0 : 1;
}
