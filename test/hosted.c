#define _DEFAULT_SOURCE 1
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../driver/host_parse.h"
#include "../driver/live_io.h"
#include "../driver/ma_patch_file.h"
#include "../driver/smf.h"
#include "../driver/wav.h"

static unsigned checks, failures;

#define CHECK(condition, ...) do {                                            \
    checks++;                                                                 \
    if (!(condition)) {                                                       \
        failures++;                                                           \
        fprintf(stderr, "FAIL: ");                                           \
        fprintf(stderr, __VA_ARGS__);                                         \
        fputc('\n', stderr);                                                   \
    }                                                                         \
} while (0)

static const uint8_t valid_smf[] = {
    'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 1, 0xe0,
    'M', 'T', 'r', 'k', 0, 0, 0, 20,
    0, 0xff, 0x51, 3, 0x07, 0xa1, 0x20,
    0, 0x90, 60, 100,
    0x81, 0x70, 0x80, 60, 0,
    0, 0xff, 0x2f, 0,
};

static const uint8_t running_smf[] = {
    'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 96,
    'M', 'T', 'r', 'k', 0, 0, 0, 18,
    0, 0x90, 60, 100,
    96, 64, 80,
    96, 0x80, 60, 0,
    0, 64, 0,
    0, 0xff, 0x2f, 0,
};

static void test_smf_valid(void) {
    smf_file file = { 0 };
    smf_error error = { 0 };
    CHECK(smf_parse(valid_smf, sizeof valid_smf, UINT16_MAX, &file, &error),
          "valid SMF rejected at %zu: %s", error.offset,
          error.message ? error.message : "no error");
    CHECK(file.format == 0 && file.tracks == 1 && file.division == 480,
          "SMF header decoded incorrectly");
    CHECK(file.event_count == 2 && file.tempo_count == 1,
          "SMF event counts are %zu/%zu", file.event_count, file.tempo_count);
    CHECK(file.event_count == 2 && file.events[0].tick == 0
          && file.events[1].tick == 240,
          "SMF event ticks decoded incorrectly");
    CHECK(file.tempo_count == 1 && file.tempos[0].us_per_quarter == 500000,
          "SMF tempo decoded incorrectly");
    smf_dispose(&file);

    CHECK(smf_parse(valid_smf, sizeof valid_smf, 0, &file, &error),
          "tempo-only parse rejected");
    CHECK(file.event_count == 0 && file.tempo_count == 1,
          "channel mask did not filter events");
    smf_dispose(&file);

    CHECK(smf_parse(running_smf, sizeof running_smf, UINT16_MAX,
                    &file, &error),
          "valid running-status SMF rejected at %zu: %s", error.offset,
          error.message ? error.message : "no error");
    CHECK(file.event_count == 4 && file.events[1].status == 0x90
          && file.events[1].d1 == 64 && file.events[3].status == 0x80,
          "SMF running status was decoded incorrectly");
    smf_dispose(&file);
}

static void test_smf_malformed(void) {
    for (size_t size = 0; size < sizeof valid_smf; size++) {
        smf_file file = { 0 };
        smf_error error = { 0 };
        bool ok = smf_parse(valid_smf, size, UINT16_MAX, &file, &error);
        CHECK(!ok, "SMF truncation at byte %zu was accepted", size);
        smf_dispose(&file);
    }

    uint8_t bad[sizeof valid_smf];
    memcpy(bad, valid_smf, sizeof bad);
    bad[12] = bad[13] = 0;
    smf_file file = { 0 };
    smf_error error = { 0 };
    CHECK(!smf_parse(bad, sizeof bad, UINT16_MAX, &file, &error),
          "zero PPQ division was accepted");

    memcpy(bad, valid_smf, sizeof bad);
    memset(bad + 22, 0x81, 4);
    CHECK(!smf_parse(bad, sizeof bad, UINT16_MAX, &file, &error),
          "overlong delta VLQ was accepted");

    memcpy(bad, valid_smf, sizeof bad);
    bad[31] = 0x80;
    CHECK(!smf_parse(bad, sizeof bad, UINT16_MAX, &file, &error),
          "status bit in channel data was accepted");

    memcpy(bad, valid_smf, sizeof bad);
    bad[21] = 16;
    CHECK(!smf_parse(bad, sizeof bad - 4, UINT16_MAX, &file, &error),
          "track without end-of-track was accepted");

    memcpy(bad, valid_smf, sizeof bad);
    bad[11] = 2;
    CHECK(!smf_parse(bad, sizeof bad, UINT16_MAX, &file, &error),
          "format-0 file with two tracks was accepted");

    memcpy(bad, valid_smf, sizeof bad);
    bad[18] = 0x7f;
    CHECK(!smf_parse(bad, sizeof bad, UINT16_MAX, &file, &error),
          "oversized track chunk was accepted");

    uint8_t trailing[sizeof valid_smf + 1];
    memcpy(trailing, valid_smf, sizeof valid_smf);
    trailing[sizeof valid_smf] = 0;
    CHECK(!smf_parse(trailing, sizeof trailing, UINT16_MAX, &file, &error),
          "trailing data was accepted");
}

static uint32_t get_u32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8
         | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t get_u16(const unsigned char *p) {
    return (uint16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8);
}

static void test_wav(void) {
    char path[] = "/tmp/tonewheel91-wav-XXXXXX";
    int descriptor = mkstemp(path);
    CHECK(descriptor >= 0, "could not create a temporary WAV fixture");
    if (descriptor < 0) return;
    close(descriptor);
    float samples[4] = { -1.0f, 1.0f, 0.5f, -0.5f };
    CHECK(wav_write_f32(path, samples, 2, 48000, 2) == 0,
          "valid stereo WAV write failed");
    FILE *file = fopen(path, "rb");
    unsigned char bytes[72] = { 0 };
    size_t size = file ? fread(bytes, 1, sizeof bytes, file) : 0;
    if (file) fclose(file);
    CHECK(size == 72, "WAV size is %zu, expected 72", size);
    CHECK(!memcmp(bytes, "RIFF", 4) && !memcmp(bytes + 8, "WAVE", 4)
          && !memcmp(bytes + 12, "fmt ", 4)
          && !memcmp(bytes + 36, "fact", 4)
          && !memcmp(bytes + 48, "data", 4),
          "WAV chunk IDs are invalid");
    CHECK(get_u32(bytes + 4) == 64 && get_u32(bytes + 44) == 2
          && get_u32(bytes + 52) == 16,
          "WAV sizes or fact frame count are invalid");
    CHECK(get_u32(bytes + 16) == 16 && get_u16(bytes + 20) == 3
          && get_u16(bytes + 22) == 2 && get_u32(bytes + 24) == 48000
          && get_u32(bytes + 28) == 384000 && get_u16(bytes + 32) == 8
          && get_u16(bytes + 34) == 32 && get_u32(bytes + 40) == 4,
          "WAV float format fields are invalid");
    CHECK(wav_write_f32(path, samples, UINT32_MAX, 48000, 2) == -1,
          "oversized RIFF was accepted");
    CHECK(wav_write_f32(path, samples, 2, 0, 2) == -1,
          "zero WAV rate was accepted");
    CHECK(wav_write_f32(path, samples, 2, 48000, 0) == -1,
          "zero WAV channel count was accepted");
    CHECK(wav_write_f32(path, 0, 0, 48000, 2) == 0,
          "zero-frame WAV with no sample storage was rejected");
    CHECK(wav_write_f32(path, 0, 1, 48000, 2) == -1,
          "nonempty WAV accepted a null sample pointer");

    wav_f32_writer writer = { 0 };
    CHECK(wav_f32_open(&writer, path, 2, 48000, 2) == 0
          && wav_f32_write(&writer, samples, 1) == 0
          && wav_f32_write(&writer, samples + 2, 1) == 0
          && wav_f32_close(&writer) == 0,
          "valid block WAV write failed");
    file = fopen(path, "rb");
    memset(bytes, 0, sizeof bytes);
    size = file ? fread(bytes, 1, sizeof bytes, file) : 0;
    if (file) fclose(file);
    CHECK(size == 72 && get_u32(bytes + 44) == 2
          && get_u32(bytes + 52) == 16
          && !memcmp(bytes + 56, samples, sizeof samples),
          "block WAV header or samples are invalid");

    writer = (wav_f32_writer){ 0 };
    CHECK(wav_f32_open(&writer, path, 1, 48000, 2) == 0
          && wav_f32_write(&writer, samples, 2) == -1
          && wav_f32_close(&writer) == -1
          && access(path, F_OK) < 0,
          "block WAV overflow did not remove the incomplete file");
    writer = (wav_f32_writer){ 0 };
    CHECK(wav_f32_open(&writer, path, 2, 48000, 2) == 0
          && wav_f32_write(&writer, samples, 1) == 0
          && wav_f32_close(&writer) == -1
          && access(path, F_OK) < 0,
          "incomplete block WAV close was accepted");
    writer = (wav_f32_writer){ 0 };
    CHECK(wav_f32_open(&writer, path, 2, 48000, 2) == 0,
          "block WAV open for abort failed");
    wav_f32_abort(&writer);
    CHECK(access(path, F_OK) < 0 && !writer.file && !writer.path,
          "block WAV abort left a temporary file or live state");
    remove(path);
}

static void test_host_parse(void) {
    uint64_t integer = 0;
    double real = 0.0;
    size_t size = 0;
    CHECK(host_parse_u64("48000", 44100, 192000, &integer) && integer == 48000,
          "valid integer option rejected");
    CHECK(!host_parse_u64("-1", 0, 10, &integer)
          && !host_parse_u64("4x", 0, 10, &integer),
          "invalid integer option accepted");
    CHECK(host_parse_double("0.25", 0.0, 1.0, &real) && real == 0.25,
          "valid real option rejected");
    CHECK(!host_parse_double("nan", 0.0, 1.0, &real)
          && !host_parse_double("inf", 0.0, 1.0, &real),
          "non-finite real option accepted");
    CHECK(host_size_add(2, 3, &size) && size == 5
          && !host_size_add(SIZE_MAX, 1, &size),
          "checked size addition failed");
    CHECK(host_size_mul(2, 3, &size) && size == 6
          && !host_size_mul(SIZE_MAX, 2, &size),
          "checked size multiplication failed");
}

static void test_live_layout(void) {
    size_t samples = 0, floats = 0, output = 0;
    CHECK(live_pcm_buffer_layout(128, &samples, &floats, &output)
          && samples == 256 && floats == 256 * sizeof(float)
          && output == 256 * sizeof(int32_t),
          "live buffer layout does not follow the negotiated period");
    CHECK(live_pcm_buffer_layout(LIVE_PERIOD_MAX, &samples, &floats, &output),
          "maximum supported live period was rejected");
    CHECK(!live_pcm_buffer_layout(0, &samples, &floats, &output)
          && !live_pcm_buffer_layout((snd_pcm_uframes_t)LIVE_PERIOD_MAX + 1,
                                     &samples, &floats, &output),
          "invalid negotiated live period was accepted");
}

static bool read_patch_text(const char *text, ma_patch_document *document,
                            ma_patch_error *error) {
    FILE *file = tmpfile();
    if (!file) return false;
    bool result = fputs(text, file) >= 0 && fflush(file) == 0
               && fseek(file, 0, SEEK_SET) == 0
               && ma_patch_read(file, document, error);
    fclose(file);
    return result;
}

static void test_ma_patch_files(void) {
    ma_patch_document source = {
        .name = "Lead",
        .value = ma_patch_lead,
    };
    FILE *file = tmpfile();
    CHECK(file && ma_patch_write(file, &source),
          "could not write a valid MA patch");
    ma_patch_document roundtrip = { 0 };
    ma_patch_error error = { 0 };
    CHECK(file && fflush(file) == 0 && fseek(file, 0, SEEK_SET) == 0
          && ma_patch_read(file, &roundtrip, &error),
          "could not read a written MA patch: line %u %s",
          error.line, error.message);
    CHECK(!strcmp(roundtrip.name, source.name)
          && !memcmp(&roundtrip.value, &source.value, sizeof source.value),
          "MA patch text round-trip changed its value object");
    FILE *legacy = tmpfile();
    bool legacy_copy = file && legacy && fseek(file, 0, SEEK_SET) == 0;
    char line[256];
    while (legacy_copy && fgets(line, sizeof line, file)) {
        if (!strncmp(line, "raster.", 7) || !strncmp(line, "bcs.", 4))
            continue;
        char const *output = !strcmp(line, "mamutanalog_patch=3\n")
                           ? "mamutanalog_patch=1\n" : line;
        legacy_copy = fputs(output, legacy) >= 0;
    }
    legacy_copy = legacy_copy && !ferror(file) && fflush(legacy) == 0
               && fseek(legacy, 0, SEEK_SET) == 0;
    ma_patch_document migrated = { 0 };
    CHECK(legacy_copy && ma_patch_read(legacy, &migrated, &error)
          && !memcmp(&migrated.value, &source.value, sizeof source.value),
          "legacy v1 MA patch did not migrate with Raster bypassed: %s",
          error.message);
    if (legacy) fclose(legacy);
    FILE *version2 = tmpfile();
    bool version2_copy = file && version2 && fseek(file, 0, SEEK_SET) == 0;
    while (version2_copy && fgets(line, sizeof line, file)) {
        if (!strncmp(line, "bcs.", 4)) continue;
        char const *output = !strcmp(line, "mamutanalog_patch=3\n")
                           ? "mamutanalog_patch=2\n" : line;
        version2_copy = fputs(output, version2) >= 0;
    }
    version2_copy = version2_copy && !ferror(file) && fflush(version2) == 0
                  && fseek(version2, 0, SEEK_SET) == 0;
    migrated = (ma_patch_document){ 0 };
    CHECK(version2_copy && ma_patch_read(version2, &migrated, &error)
          && !memcmp(&migrated.value, &source.value, sizeof source.value),
          "legacy v2 MA patch did not migrate with BCS bypassed: %s",
          error.message);
    if (version2) fclose(version2);
    if (file) fclose(file);

    ma_patch_document invalid = { 0 };
    CHECK(!read_patch_text("mamutanalog_patch=1\nname=x\n",
                           &invalid, &error)
          && strstr(error.message, "missing"),
          "incomplete MA patch was accepted");
    CHECK(!read_patch_text("mamutanalog_patch=1\nname=x\nwat=1\n",
                           &invalid, &error)
          && strstr(error.message, "unknown"),
          "unknown MA patch key was accepted");
    CHECK(!read_patch_text("mamutanalog_patch=1\nname=x\n"
                           "vco1.saw=.2\nvco1.saw=.3\n",
                           &invalid, &error)
          && strstr(error.message, "duplicate"),
          "duplicate MA patch key was accepted");
    CHECK(!read_patch_text("mamutanalog_patch=1\nname=x\n"
                           "vco1.sine=nan\n",
                           &invalid, &error)
          && strstr(error.message, "domain"),
          "non-finite MA patch value was accepted");
    CHECK(!read_patch_text("mamutanalog_patch=1\nname=x\n"
                           "vco2.interval=1.5\n",
                           &invalid, &error)
          && strstr(error.message, "domain"),
          "fractional MA patch integer was accepted");
    CHECK(!read_patch_text("mamutanalog_patch=4\nname=x\n",
                           &invalid, &error)
          && strstr(error.message, "version"),
          "unsupported MA patch version was accepted");
    CHECK(!read_patch_text("mamutanalog_patch=1\nname=x\n"
                           "raster.mix=.2\n",
                           &invalid, &error)
          && strstr(error.message, "newer"),
          "MA patch v1 accepted a Raster field");
    CHECK(!read_patch_text("mamutanalog_patch=2\nname=x\n"
                           "bcs.amount=.2\n",
                           &invalid, &error)
          && strstr(error.message, "version 3"),
          "MA patch v2 accepted a BCS field");
    CHECK(!read_patch_text("mamutanalog_patch=1\nname=bad=name\n",
                           &invalid, &error)
          && strstr(error.message, "name"),
          "invalid MA patch name was accepted");

    char path[] = "/tmp/tonewheel91-patch-XXXXXX";
    int descriptor = mkstemp(path);
    CHECK(descriptor >= 0, "could not create a temporary MA patch path");
    if (descriptor >= 0) {
        close(descriptor);
        remove(path);
        CHECK(ma_patch_save(path, &source, &error),
              "atomic MA patch save failed: %s", error.message);
        ma_patch_document loaded = { 0 };
        CHECK(ma_patch_load(path, &loaded, &error)
              && !memcmp(&loaded.value, &source.value, sizeof source.value),
              "saved MA patch did not load exactly: %s", error.message);
        remove(path);
    }

    static const struct {
        const char *path;
        const char *name;
        const ma_patch *value;
    } builtins[] = {
        { "patches/mamutanalog/tepih.mapatch", "Tepih", &ma_patch_tepih },
        { "patches/mamutanalog/lead.mapatch", "Lead", &ma_patch_lead },
        { "patches/mamutanalog/dubina.mapatch", "Dubina", &ma_patch_dubina },
        { "patches/mamutanalog/raster.mapatch", "Raster", &ma_patch_raster },
        { "patches/mamutanalog/prizma.mapatch", "Prizma", &ma_patch_prizma },
        { "patches/mamutanalog/granica.mapatch", "Granica",
          &ma_patch_granica },
    };
    for (size_t i = 0; i < sizeof builtins / sizeof *builtins; i++) {
        ma_patch_document document = { 0 };
        CHECK(ma_patch_load(builtins[i].path, &document, &error)
              && !strcmp(document.name, builtins[i].name)
              && !memcmp(&document.value, builtins[i].value,
                         sizeof document.value),
              "built-in patch mirror %s drifted: line %u %s",
              builtins[i].name, error.line, error.message);
    }

    CHECK(ma_patch_field_count() == 50,
          "MA patch field catalog count changed unexpectedly");
    ma_patch_field_info field = { 0 };
    double value = 0.0;
    ma_patch edited = ma_patch_dubina;
    CHECK(ma_patch_field_info_at(8, &field)
          && !strcmp(field.name, "vco2.sine")
          && field.minimum == 0.0 && field.maximum == 1.0
          && field.fine_step == .01 && field.coarse_step == .10
          && !field.integer,
          "MA patch VCO2 sine field metadata drifted");
    CHECK(ma_patch_field_get(&edited, 8, &value) && value == .85f,
          "MA patch field getter did not expose VCO2 sine");
    CHECK(ma_patch_field_set(&edited, 8, .63)
          && ma_patch_field_get(&edited, 8, &value)
          && value == (double)(float).63,
          "MA patch field editor did not retain a legal float value");
    CHECK(!ma_patch_field_set(&edited, 8, 1.01)
          && !ma_patch_field_set(&edited, 8, 0.0 / 0.0),
          "MA patch field editor accepted an invalid float value");
    CHECK(ma_patch_field_info_at(11, &field) && field.integer
          && ma_patch_field_set(&edited, 11, -7.0)
          && !ma_patch_field_set(&edited, 11, -7.5),
          "MA patch integer field contract failed");
    CHECK(!ma_patch_field_info_at(ma_patch_field_count(), &field)
          && !ma_patch_field_get(&edited, ma_patch_field_count(), &value)
          && !ma_patch_field_set(&edited, ma_patch_field_count(), 0.0),
          "MA patch field catalog accepted an invalid index");
    ma_patch_document unwritable = source;
    unwritable.value.filter_cutoff_hz = 0.0f / 0.0f;
    file = tmpfile();
    CHECK(file && !ma_patch_write(file, &unwritable),
          "MA patch writer accepted a non-finite field");
    if (file) fclose(file);
    unwritable = source;
    memset(unwritable.name, 'x', sizeof unwritable.name);
    file = tmpfile();
    CHECK(file && !ma_patch_write(file, &unwritable),
          "MA patch writer read past a non-terminated name");
    if (file) fclose(file);
}

int main(void) {
    test_smf_valid();
    test_smf_malformed();
    test_wav();
    test_host_parse();
    test_live_layout();
    test_ma_patch_files();
    printf("hosted: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
