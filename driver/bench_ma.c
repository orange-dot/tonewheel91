/* Hosted five-card cost referee. Hashing and event dispatch are not timed. */
#define _POSIX_C_SOURCE 200809L
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "../src/mamutanalog.h"

enum { RATE = 48000, FRAMES = 128, WARMUP = 256, BLOCKS = 1024 };

static double microseconds(struct timespec start, struct timespec end) {
    return (double)(end.tv_sec - start.tv_sec) * 1e6
         + (double)(end.tv_nsec - start.tv_nsec) * 1e-3;
}

static struct timespec clock_read(clockid_t clock) {
    struct timespec value = { 0 };
    if (clock_gettime(clock, &value)) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    return value;
}

static int compare(const void *left, const void *right) {
    double a = *(const double *)left;
    double b = *(const double *)right;
    return (a > b) - (a < b);
}

static void gesture(ma_card_bank *bank, unsigned block) {
    unsigned phase = block % 256;
    if (phase == 0) {
        (void)ma_card_bank_note_on(bank, 0, 72, 117);
        for (unsigned slot = 0; slot < MA_CARD_COUNT; slot++) {
            ma_synth_set_mod_wheel(&bank->card[slot], .8f);
            ma_synth_set_channel_pressure(&bank->card[slot], .6f);
            ma_synth_set_lfo(&bank->card[slot], .1f, 3.0f);
            ma_synth_set_glide(&bank->card[slot], true, .12f);
        }
    }
    if (phase == 32) ma_card_bank_set_sustain(bank, true);
    if (phase == 64) (void)ma_card_bank_note_off(bank, 0, 72, 0);
    if (phase == 96) ma_card_bank_set_sustain(bank, false);
    if (phase == 128) {
        ma_card_bank_set_unison(bank, true);
        (void)ma_card_bank_note_on(bank, 0, 96, 108);
    }
    if (phase == 160) ma_card_bank_panic(bank);
    if (phase == 192) {
        ma_card_bank_set_unison(bank, false);
        (void)ma_card_bank_note_on(bank, 0, 36, 100);
    }
}

static uint64_t measure(const char *name, const ma_patch *patch, bool idle,
                         bool moving, float character, bool stereo, unsigned pass) {
    static constexpr uint8_t notes[MA_CARD_COUNT] = { 36, 43, 48, 55, 60 };
    ma_card_bank bank = { 0 };
    ma_card_bank_init_patch(&bank, RATE, patch);
    ma_card_bank_set_character(&bank, character);
    if (!idle)
        for (unsigned slot = 0; slot < MA_CARD_COUNT; slot++)
            (void)ma_card_bank_note_on(&bank, 0, notes[slot], 100);

    double cpu[BLOCKS] = { 0 }, wall[BLOCKS] = { 0 };
    double total = 0.0;
    uint64_t hash = 0;
    for (unsigned block = 0; block < WARMUP + BLOCKS; block++) {
        if (moving) gesture(&bank, block);
        ma_frame samples[FRAMES][MA_CARD_COUNT] = { 0 };
        struct timespec wall_start = clock_read(CLOCK_MONOTONIC);
        struct timespec cpu_start = clock_read(CLOCK_THREAD_CPUTIME_ID);
        for (unsigned frame = 0; frame < FRAMES; frame++) {
            if (stereo) samples[frame][0] = ma_card_bank_tick_stereo(&bank);
            else ma_card_bank_tick(&bank, samples[frame]);
        }
        struct timespec cpu_end = clock_read(CLOCK_THREAD_CPUTIME_ID);
        struct timespec wall_end = clock_read(CLOCK_MONOTONIC);
        if (block >= WARMUP) {
            unsigned index = block - WARMUP;
            cpu[index] = microseconds(cpu_start, cpu_end);
            wall[index] = microseconds(wall_start, wall_end);
            total += cpu[index];
        }
        for (unsigned frame = 0; frame < FRAMES; frame++) {
            for (unsigned slot = 0; slot < (stereo ? 1u : MA_CARD_COUNT); slot++) {
                ma_frame sample = samples[frame][slot];
                if (!isfinite(sample.left) || !isfinite(sample.right)) {
                    fprintf(stderr, "%s: non-finite PCM\n", name);
                    exit(EXIT_FAILURE);
                }
                hash = tw_fnv1a64(&sample.left, sizeof sample.left, hash);
                hash = tw_fnv1a64(&sample.right, sizeof sample.right, hash);
            }
        }
    }
    qsort(cpu, BLOCKS, sizeof *cpu, compare);
    qsort(wall, BLOCKS, sizeof *wall, compare);
    printf("%s pass=%u cpu_mean_us=%.2f cpu_p99_us=%.2f "
           "wall_p50_us=%.2f wall_p99_us=%.2f hash=%016" PRIx64 "\n",
           name, pass, total / BLOCKS, cpu[BLOCKS * 99 / 100],
           wall[BLOCKS / 2], wall[BLOCKS * 99 / 100], hash);
    fflush(stdout);
    return hash;
}

int main(void) {
    ma_patch bcs_off = ma_patch_granica;
    bcs_off.bcs_amount = 0.0f;
    const struct {
        const char *name;
        const ma_patch *patch;
        bool idle;
        bool moving;
        uint64_t expected;
        float character;
        bool stereo;
    } cases[] = {
        { "tepih-idle", &ma_patch_tepih, true, false,
          UINT64_C(0xc4c2a0b9a1f22325), 0, false },
        { "tepih-five", &ma_patch_tepih, false, false,
          UINT64_C(0x8b94b7d526875ec1), 0, false },
        { "granica-bcs-off", &bcs_off, false, false,
          UINT64_C(0xfc137a48a372ec55), 0, false },
        { "granica-bcs-on", &ma_patch_granica, false, false,
          UINT64_C(0x3e5cf262b15baaf1), 0, false },
        { "lead-gesture", &ma_patch_lead, false, true,
          UINT64_C(0x83b5c4826511163d), 0, false },
        { "tepih-character-020", &ma_patch_tepih, false, false,
          UINT64_C(0x8fc634b744781f75), .20f, false },
        { "tepih-character-100", &ma_patch_tepih, false, false,
          UINT64_C(0x7806d75a328f6745), 1, false },
        { "granica-character-020", &ma_patch_granica, false, false,
          UINT64_C(0x026a47c65f63b845), .20f, false },
        { "granica-character-100", &ma_patch_granica, false, false,
          UINT64_C(0x9d82f80e3cbfd119), 1, false },
        { "tepih-stereo", &ma_patch_tepih, false, false,
          UINT64_C(0x1a254c3750cc209f), .20f, true },
        { "granica-stereo", &ma_patch_granica, false, false,
          UINT64_C(0x6b4ddec5f3916af7), .20f, true },
        { "lead-stereo-gesture", &ma_patch_lead, false, true,
          UINT64_C(0x028a8d6205d46ad5), .20f, true },
    };
    printf("rate=%u frames=%u warmup=%u blocks=%u voice_bytes=%zu "
           "bank_bytes=%zu; host timing only\n",
           RATE, FRAMES, WARMUP, BLOCKS, sizeof(ma_synth), sizeof(ma_card_bank));
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        uint64_t first = measure(cases[i].name, cases[i].patch,
                                  cases[i].idle, cases[i].moving,
                                  cases[i].character, cases[i].stereo, 1);
        uint64_t second = measure(cases[i].name, cases[i].patch,
                                   cases[i].idle, cases[i].moving,
                                   cases[i].character, cases[i].stereo, 2);
        if (first != second || first != cases[i].expected) {
            fprintf(stderr, "%s: PCM mismatch\n", cases[i].name);
            return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}
