/* Hosted MA2-BCS listening exhibit: Granica feedback motion over a Prizma
 * floor. The score automates one continuous regime coordinate; the core has
 * no scenario player and BCS never enters the source mix directly. */
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
    BASS_SECONDS = 2,
};

static constexpr char OUTPUT_PATH[] = "build/ma2_bcs_granica.wav";

static constexpr uint8_t CHORDS[][4] = {
    { 31, 43, 50, 58 }, { 29, 41, 48, 56 }, { 27, 39, 46, 55 },
    { 32, 44, 51, 60 }, { 31, 43, 50, 58 }, { 34, 46, 53, 62 },
    { 29, 41, 48, 55 }, { 27, 39, 46, 53 }, { 24, 36, 43, 51 },
    { 29, 41, 48, 56 }, { 31, 43, 50, 58 }, { 27, 39, 46, 55 },
};

static constexpr uint8_t BASS_LINE[] = {
    31, 38, 43, 46, 29, 36, 41, 48, 27, 34, 39, 46,
    32, 39, 44, 51, 31, 38, 43, 50, 24, 31, 36, 43,
};

typedef struct {
    ma_card_bank granica;
    ma_card_bank prizma;
    uint64_t hash;
    double sum_squares;
    float peak;
    float bcs_peak_state;
    uint64_t bcs_resets;
    unsigned nonfinite;
    unsigned clipped;
    uint8_t chord_index;
    uint8_t bass_note;
    bool chord_active;
    bool bass_active;
} performance;

typedef struct {
    uint64_t hash;
    double sum_squares;
    float peak;
    float bcs_peak_state;
    uint64_t bcs_resets;
    unsigned nonfinite;
    unsigned clipped;
} metrics;

static void set_bank_bcs(ma_card_bank *bank, float amount, float regime) {
    for (uint8_t slot = 0; slot < MA_CARD_COUNT; slot++)
        ma_synth_set_bcs(&bank->card[slot], amount, regime);
}

static void performance_init(performance *p) {
    *p = (performance){ 0 };
    ma_card_bank_init_patch(&p->granica, RATE, &ma_patch_granica);
    ma_card_bank_init_patch(&p->prizma, RATE, &ma_patch_prizma);
    for (uint8_t slot = 0; slot < MA_CARD_COUNT; slot++) {
        ma_synth_set_lfo(&p->granica.card[slot], 0.018f, 0.13f);
        ma_synth_set_lfo(&p->prizma.card[slot], 0.012f, 0.09f);
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
                                   (uint8_t)(66 + 4 * voice));
    p->chord_active = true;
}

static void start_bass(performance *p, uint8_t note) {
    if (p->bass_active)
        (void)ma_card_bank_note_off(&p->granica, 1, p->bass_note, 0);
    (void)ma_card_bank_note_on(&p->granica, 1, note, 108);
    p->bass_note = note;
    p->bass_active = true;
}

static void run_events(performance *p, size_t frame) {
    static constexpr size_t music_frames = MUSIC_SECONDS * (size_t)RATE;
    if (frame < music_frames && frame % (CHORD_SECONDS * RATE) == 0)
        start_chord(p, (uint8_t)(frame / (CHORD_SECONDS * RATE)));
    if (frame < music_frames && frame % (BASS_SECONDS * RATE) == 0) {
        size_t index = frame / (BASS_SECONDS * RATE);
        start_bass(p, BASS_LINE[index % (sizeof BASS_LINE
                                          / sizeof *BASS_LINE)]);
    }
    if (frame == music_frames) {
        release_chord(p);
        if (p->bass_active)
            (void)ma_card_bank_note_off(&p->granica, 1, p->bass_note, 0);
        p->bass_active = false;
    }
    if (frame < music_frames && frame % BLOCK_FRAMES == 0) {
        float regime = (float)frame / (float)(music_frames - 1);
        set_bank_bcs(&p->granica, 0.72f, regime);
    }
}

static ma_frame mix_bank(const ma_frame card[MA_CARD_COUNT], float gain,
                         bool reverse) {
    static constexpr float pan[MA_CARD_COUNT] = {
        -0.76f, -0.36f, 0.0f, 0.36f, 0.76f,
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
    ma_frame granica_cards[MA_CARD_COUNT], prizma_cards[MA_CARD_COUNT];
    ma_card_bank_tick(&p->granica, granica_cards);
    ma_card_bank_tick(&p->prizma, prizma_cards);
    ma_frame granica = mix_bank(granica_cards, 0.58f, false);
    ma_frame prizma = mix_bank(prizma_cards, 0.38f, true);
    ma_frame output = {
        .left = 1.45f * (granica.left + prizma.left),
        .right = 1.45f * (granica.right + prizma.right),
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

static void collect_bcs_metrics(performance *p) {
    for (uint8_t slot = 0; slot < MA_CARD_COUNT; slot++) {
        ma_bcs const *bcs = &p->granica.card[slot].bcs;
        p->bcs_resets += bcs->reset_count;
        if (bcs->maximum_state > p->bcs_peak_state)
            p->bcs_peak_state = bcs->maximum_state;
    }
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
    collect_bcs_metrics(&p);
    *result = (metrics){
        .hash = p.hash,
        .sum_squares = p.sum_squares,
        .peak = p.peak,
        .bcs_peak_state = p.bcs_peak_state,
        .bcs_resets = p.bcs_resets,
        .nonfinite = p.nonfinite,
        .clipped = p.clipped,
    };
    return p.nonfinite == 0 && p.clipped == 0 && p.bcs_resets == 0
        && p.peak > 1.0e-4f && p.sum_squares > 1.0e-8;
}

int main(void) {
    puts("Mamut Analog MA2-BCS — Granica");
    puts("  Granica bass traverses stable -> edge -> subharmonic -> recovery");
    puts("  Prizma supplies the floor; BCS remains inside feedback control");
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
    printf("  BCS max state %.6f, safety resets %llu\n",
           (double)second.bcs_peak_state,
           (unsigned long long)second.bcs_resets);
    printf("  FNV64 %016llx %s\n", (unsigned long long)second.hash,
           deterministic ? "(two runs identical)" : "MISMATCH");
    printf("  wav: %s%s\n", OUTPUT_PATH, written ? "" : " (FAILED)");
    return written ? 0 : 1;
}
