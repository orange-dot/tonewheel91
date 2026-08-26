#define _DEFAULT_SOURCE 1
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../driver/ma_architecture_cli.h"
#include "../driver/ma_architecture_render.h"
#include "../driver/ma_architecture_score.h"
#include "../driver/ma_dark_lead.h"

static unsigned checks, failures;

#define CHECK(condition, ...) do {                                            \
    checks++;                                                                 \
    if (!(condition)) {                                                       \
        failures++;                                                           \
        fprintf(stderr, "FAIL: ");                                           \
        fprintf(stderr, __VA_ARGS__);                                         \
        fputc('\n', stderr);                                                  \
    }                                                                         \
} while (0)

static void test_score(void) {
    ma_arch_score score = { 0 };
    const char *reason = 0;
    CHECK(ma_arch_score_build(&score), "score build failed");
    CHECK(ma_arch_score_validate(&score, &reason),
          "score validation failed: %s", reason ? reason : "no reason");
    CHECK(score.note_count > 1000 && score.note_count < MA_ARCH_EVENT_CAPACITY,
          "unexpected score event count: %zu", score.note_count);
    CHECK(score.ground_cycle_count == 12 && score.fugue_entry_count == 4,
          "architectural entry metadata drifted");
    CHECK(MA_ARCH_LINE_COUNT == 10
          && ma_arch_lines[0].future_organ_candidate
          && ma_arch_lines[6].future_organ_candidate
          && ma_arch_lines[7].future_organ_candidate
          && !ma_arch_lines[1].future_organ_candidate,
          "line metadata does not identify the three V2 candidates");
    CHECK(ma_arch_sections[0].start_second == 0
          && ma_arch_sections[0].end_second == 72
          && ma_arch_sections[1].end_second == 216
          && ma_arch_sections[2].end_second == 486
          && ma_arch_sections[3].end_second == 630
          && ma_arch_sections[4].end_second == 886
          && ma_arch_sections[5].end_second == 950,
          "section second boundaries drifted");
    CHECK(ma_arch_tick_us(34560) == UINT64_C(72000000)
          && ma_arch_tick_us(126720) == UINT64_C(216000000)
          && ma_arch_tick_us(264960) == UINT64_C(486000000)
          && ma_arch_tick_us(334080) == UINT64_C(630000000)
          && ma_arch_tick_us(487680) == UINT64_C(886000000)
          && ma_arch_tick_us(518400) == UINT64_C(950000000)
          && ma_arch_tick_us(MA_ARCH_END_TICK) == UINT64_C(960000000),
          "tempo map does not land on exact section boundaries");
    CHECK(score.track_count == 8
          && ma_arch_automation_value(&score, 0, 0) !=
             ma_arch_automation_value(&score, 0, 126720),
          "piecewise-linear automation is missing");

    unsigned ground = 0, fugue = 0, dark = 0;
    for (size_t i = 0; i < score.note_count; i++) {
        ground += !!(score.notes[i].flags & MA_ARCH_NOTE_GROUND_CYCLE);
        fugue += !!(score.notes[i].flags & MA_ARCH_NOTE_FUGUE_ENTRY);
        dark += !!(score.notes[i].flags & MA_ARCH_NOTE_DARK_LEAD_ENTRY);
    }
    CHECK(ground == 12 && fugue == 4 && dark == 2,
          "tagged architecture counts are %u/%u/%u", ground, fugue, dark);

    static const uint8_t expected_ground[16] = {
        42, 49, 52, 50, 45, 47, 44, 49, 38, 45, 40, 47, 49, 44, 40, 42,
    };
    for (unsigned variation = 0; variation < 12; variation++) {
        uint32_t start = 126720u + variation * 11520u;
        unsigned found = 0;
        for (size_t i = 0; i < score.note_count; i++) {
            const ma_arch_note *note = &score.notes[i];
            if (note->line == 1 && note->start_tick >= start
                && note->start_tick < start + 11520u) {
                CHECK(found < 16 && note->note == expected_ground[found],
                      "ground note drift in variation %u at %u",
                      variation + 1u, found);
                found++;
            }
        }
        CHECK(found == 16, "variation %u has %u ground notes",
              variation + 1u, found);
    }
}

static ma_arch_cli_status parse(int argc, char *argv[],
                                ma_arch_cli_options *options) {
    const char *reason = 0;
    return ma_arch_cli_parse(argc, argv, options, &reason);
}

static void test_cli(void) {
    ma_arch_cli_options options = { 0 };
    char *defaults[] = { "render" };
    CHECK(parse(1, defaults, &options) == MA_ARCH_CLI_OK
          && options.duration_seconds == 960.0
          && options.rate_hz == 48000
          && !strcmp(options.output_path, "build/mamut_architecture_v1.wav"),
          "CLI defaults drifted");
    char *valid[] = { "render", "-d", "180", "-r", "48000",
                      "-o", "/tmp/a.wav" };
    CHECK(parse(7, valid, &options) == MA_ARCH_CLI_OK
          && options.duration_seconds == 180.0
          && options.rate_hz == 48000
          && !strcmp(options.output_path, "/tmp/a.wav"),
          "valid CLI options were rejected");
    char *zero[] = { "render", "-d", "0" };
    char *negative[] = { "render", "-d", "-1" };
    char *nan[] = { "render", "-d", "nan" };
    char *long_duration[] = { "render", "-d", "960.01" };
    CHECK(parse(3, zero, &options) == MA_ARCH_CLI_ERROR
          && parse(3, negative, &options) == MA_ARCH_CLI_ERROR
          && parse(3, nan, &options) == MA_ARCH_CLI_ERROR
          && parse(3, long_duration, &options) == MA_ARCH_CLI_ERROR,
          "invalid CLI duration was accepted");
    char *help[] = { "render", "-h" };
    CHECK(parse(2, help, &options) == MA_ARCH_CLI_HELP,
          "CLI help option was rejected");
    uint64_t frames = 0;
    CHECK(ma_arch_duration_frames(180.0, 48000, &frames)
          && frames == UINT64_C(8640000),
          "180 s did not map to 8,640,000 frames");
    CHECK(!ma_arch_duration_frames(0.0, 48000, &frames)
          && !ma_arch_duration_frames(-1.0, 48000, &frames)
          && !ma_arch_duration_frames(NAN, 48000, &frames)
          && !ma_arch_duration_frames(961.0, 48000, &frames),
          "render frame conversion accepted an invalid duration");
}

static bool same_audio_prefix(const char *short_path, const char *long_path,
                              size_t bytes) {
    FILE *short_file = fopen(short_path, "rb");
    FILE *long_file = fopen(long_path, "rb");
    bool same = short_file && long_file
             && fseek(short_file, 56, SEEK_SET) == 0
             && fseek(long_file, 56, SEEK_SET) == 0;
    unsigned char short_block[4096], long_block[4096];
    while (same && bytes) {
        size_t chunk = bytes < sizeof short_block ? bytes : sizeof short_block;
        same = fread(short_block, 1, chunk, short_file) == chunk
            && fread(long_block, 1, chunk, long_file) == chunk
            && !memcmp(short_block, long_block, chunk);
        bytes -= chunk;
    }
    if (short_file) fclose(short_file);
    if (long_file) fclose(long_file);
    return same;
}

static void test_prefix(void) {
    char short_path[] = "/tmp/ma-architecture-short-XXXXXX";
    char long_path[] = "/tmp/ma-architecture-long-XXXXXX";
    int short_fd = mkstemp(short_path);
    int long_fd = mkstemp(long_path);
    CHECK(short_fd >= 0 && long_fd >= 0,
          "could not reserve prefix-test paths");
    if (short_fd < 0 || long_fd < 0) {
        if (short_fd >= 0) close(short_fd);
        if (long_fd >= 0) close(long_fd);
        return;
    }
    close(short_fd);
    close(long_fd);
    remove(short_path);
    remove(long_path);
    ma_arch_score score = { 0 };
    ma_arch_render_result short_result = { 0 }, long_result = { 0 };
    const char *reason = 0;
    bool built = ma_arch_score_build(&score);
    int short_ok = built ? ma_arch_render_file(&score, short_path, .08, 44100,
                                                &short_result, &reason) : -1;
    CHECK(short_ok == 0, "short prefix render failed: %s",
          reason ? reason : "no reason");
    int long_ok = built ? ma_arch_render_file(&score, long_path, .16, 44100,
                                               &long_result, &reason) : -1;
    CHECK(long_ok == 0, "long prefix render failed: %s",
          reason ? reason : "no reason");
    uint64_t short_frames = 0;
    CHECK(ma_arch_duration_frames(.08, 44100, &short_frames)
          && short_ok == 0 && long_ok == 0
          && same_audio_prefix(short_path, long_path,
                               (size_t)short_frames * 2u * sizeof(float)),
          "short render is not the exact audio prefix of the long render");
    CHECK(short_result.first_pass.hash == short_result.second_pass.hash
          && long_result.first_pass.hash == long_result.second_pass.hash,
          "a prefix-test render was not deterministic across passes");
    remove(short_path);
    remove(long_path);
}

static ma_patch former_blues_dark_lead_patch(void) {
    ma_patch patch = ma_patch_lead;
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
    return patch;
}

static void test_dark_lead(void) {
    ma_patch patch = ma_dark_lead_patch();
    CHECK(patch.filter_cutoff_hz == 880.0f
          && patch.amp_adsr.attack_ms == 520.0f
          && patch.amp_adsr.decay_ms == 1800.0f
          && patch.amp_adsr.sustain == .78f
          && patch.amp_adsr.release_ms == 9000.0f
          && patch.vco1.triangle_level == .34f
          && patch.vco1.sine_level == .52f
          && patch.macro[MA_MACRO_GRAVITACIJA] == .06f
          && patch.macro[MA_MACRO_BLOOM] == .10f,
          "hosted dark-lead contract drifted");
    ma_patch former = former_blues_dark_lead_patch();
    CHECK(!memcmp(&patch, &former, sizeof patch),
          "dark-lead helper differs from the former blues-local patch");
    ma_synth actual = { 0 }, expected = { 0 };
    ma_synth_init_patch(&actual, 48000.0f, &patch);
    ma_synth_init_patch(&expected, 48000.0f, &former);
    ma_synth_set_channel_pressure(&actual, .46f);
    ma_synth_set_channel_pressure(&expected, .46f);
    ma_synth_set_mod_wheel(&actual, .381f);
    ma_synth_set_mod_wheel(&expected, .381f);
    ma_synth_note_on(&actual, 2, 61, 108);
    ma_synth_note_on(&expected, 2, 61, 108);
    bool identical = true;
    for (unsigned frame = 0; frame < 20000; frame++) {
        ma_frame a = ma_synth_tick(&actual);
        ma_frame b = ma_synth_tick(&expected);
        if (memcmp(&a, &b, sizeof a)) identical = false;
    }
    CHECK(identical,
          "extracted dark lead changed the former blues voice samples");
}

int main(void) {
    test_score();
    test_cli();
    test_prefix();
    test_dark_lead();
    printf("ma architecture: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
