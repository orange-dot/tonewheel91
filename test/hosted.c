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
        if (!strncmp(line, "raster.", 7) || !strncmp(line, "bcs.", 4)
            || !strncmp(line, "glas.", 5))
            continue;
        char const *output = !strcmp(line, "mamutanalog_patch=4\n")
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
        if (!strncmp(line, "bcs.", 4) || !strncmp(line, "glas.", 5)) continue;
        char const *output = !strcmp(line, "mamutanalog_patch=4\n")
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
    FILE *version3 = tmpfile();
    bool version3_copy = file && version3 && fseek(file, 0, SEEK_SET) == 0;
    while (version3_copy && fgets(line, sizeof line, file)) {
        if (!strncmp(line, "glas.", 5)) continue;
        char const *output = !strcmp(line, "mamutanalog_patch=4\n")
                           ? "mamutanalog_patch=3\n" : line;
        version3_copy = fputs(output, version3) >= 0;
    }
    version3_copy = version3_copy && !ferror(file) && fflush(version3) == 0
                  && fseek(version3, 0, SEEK_SET) == 0;
    migrated = (ma_patch_document){ 0 };
    CHECK(version3_copy && ma_patch_read(version3, &migrated, &error)
          && !memcmp(&migrated.value, &source.value, sizeof source.value),
          "legacy v3 MA patch did not migrate with Glas bypassed: %s",
          error.message);
    if (version3) fclose(version3);
    if (file) fclose(file);

    /* Version 4 carries a Glas patch exactly. */
    ma_patch_document glas_source = { .name = "Glas", .value = ma_patch_tepih };
    glas_source.value.formant_mix = .6f;
    glas_source.value.formant_vowel = .4f;
    glas_source.value.formant_shift = -.35f;
    glas_source.value.formant_orbita = .3f;
    glas_source.value.grain_mix = .2f;
    file = tmpfile();
    ma_patch_document glas_roundtrip = { 0 };
    CHECK(file && ma_patch_write(file, &glas_source) && fflush(file) == 0
          && fseek(file, 0, SEEK_SET) == 0
          && ma_patch_read(file, &glas_roundtrip, &error)
          && !memcmp(&glas_roundtrip.value, &glas_source.value,
                     sizeof glas_source.value),
          "MA patch v4 did not round-trip its Glas fields: %s", error.message);
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
    CHECK(!read_patch_text("mamutanalog_patch=8\nname=x\n",
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
          && strstr(error.message, "newer"),
          "MA patch v2 accepted a BCS field");
    CHECK(!read_patch_text("mamutanalog_patch=3\nname=x\n"
                           "glas.grain_mix=.2\n",
                           &invalid, &error)
          && strstr(error.message, "version 4"),
          "MA patch v3 accepted a Glas field");
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

    CHECK(ma_patch_field_count() == 61,
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
    CHECK(ma_patch_field_info_at(52, &field)
          && !strcmp(field.name, "glas.formant_shift")
          && field.minimum == -1.0 && field.maximum == 1.0,
          "MA patch Glas shift field metadata drifted");
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

/* Whole text of a written patch, for line-level edits in the tests. */
static bool patch_text(const ma_patch_document *doc, char *out, size_t size) {
    FILE *file = tmpfile();
    if (!file) return false;
    bool ok = ma_patch_write(file, doc) && fflush(file) == 0 && fseek(file, 0, SEEK_SET) == 0;
    size_t n = ok ? fread(out, 1, size - 1, file) : 0;
    out[n] = 0;
    fclose(file);
    return ok && n < size - 1;
}

/* Replace the first occurrence of `from` in text (in place, bounded). */
static bool edit_text(char *text, size_t size, const char *from, const char *to) {
    char *at = strstr(text, from);
    if (!at) return false;
    size_t head = (size_t)(at - text), tail = strlen(at + strlen(from));
    if (head + strlen(to) + tail + 1 > size) return false;
    memmove(at + strlen(to), at + strlen(from), tail + 1);
    memcpy(at, to, strlen(to));
    return true;
}

/* Version 6 carries the oscillator pitch block (docs/ma-vco-plan.md); a patch
 * without it keeps writing version 4 or 5, without the vco.* keys. */
static void test_ma_patch_vco(void) {
    ma_patch_document plain = { .name = "Lead", .value = ma_patch_lead };
    static char text[16384], older[16384];
    CHECK(patch_text(&plain, older, sizeof older) && !strncmp(older, "mamutanalog_patch=4\n", 20)
          && !strstr(older, "vco."), "a patch without the pitch law must keep writing version 4");

    ma_patch_document analog = plain;
    analog.value.vco_analog = .75f;
    analog.value.vco_dead_time = .25f;
    CHECK(patch_text(&analog, text, sizeof text) && !strncmp(text, "mamutanalog_patch=6\n", 20)
          && strstr(text, "vco.analog=0.75\n") && strstr(text, "vco.dead_time=0.25\n"),
          "the pitch law did not write as version 6");
    ma_patch_document back = { 0 };
    ma_patch_error error = { 0 };
    CHECK(read_patch_text(text, &back, &error) && !memcmp(&back.value, &analog.value, sizeof analog.value),
          "the pitch law did not round-trip: line %u %s", error.line, error.message);
    /* The dead time alone is kept too, although it does nothing at amount zero. */
    analog.value.vco_analog = 0;
    CHECK(patch_text(&analog, text, sizeof text) && !strncmp(text, "mamutanalog_patch=6\n", 20)
          && read_patch_text(text, &back, &error) && back.value.vco_dead_time == .25f,
          "a dead time without an amount was lost");
    /* With a rack as well: version 6, both blocks. */
    analog.value.vco_analog = 1;
    ma_rack_slot_init(&analog.rack.slot[0], MA_RACK_UTILITY, 1);
    CHECK(patch_text(&analog, text, sizeof text) && !strncmp(text, "mamutanalog_patch=6\n", 20)
          && strstr(text, "rack.1.kind=utility\n") && read_patch_text(text, &back, &error)
          && !memcmp(&back.rack, &analog.rack, sizeof analog.rack) && back.value.vco_analog == 1,
          "version 6 with a rack did not round-trip: line %u %s", error.line, error.message);

    struct { const char *from, *to, *why; } bad[] = {
        { "mamutanalog_patch=6", "mamutanalog_patch=5", "version 6" },
        { "vco.dead_time=0.25\n", "", "missing" },
        { "vco.analog=1", "vco.analog=1.5", "domain" },
        { "vco.analog=1\n", "vco.analog=1\nvco.analog=1\n", "duplicate" },
    };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        static char edited[16384];
        snprintf(edited, sizeof edited, "%s", text);
        bool replaced = edit_text(edited, sizeof edited, bad[i].from, bad[i].to);
        CHECK(replaced && !read_patch_text(edited, &back, &error) && strstr(error.message, bad[i].why),
              "malformed version 6 patch %u accepted or wrong reason: '%s'", i, error.message);
    }
    /* An older file loads with the block at its bypass. */
    CHECK(read_patch_text(older, &back, &error) && back.value.vco_analog == 0 && back.value.vco_dead_time == 0,
          "a version 4 patch did not load with the pitch law off");
}

/* Version 7 carries the triangle shapers (docs/ma-vco-plan.md, V3); a patch
 * without them keeps writing version 6 or below. */
static void test_ma_patch_shapers(void) {
    ma_patch_document plain = { .name = "Lead", .value = ma_patch_lead };
    static char text[16384], older[16384];
    CHECK(patch_text(&plain, older, sizeof older) && !strstr(older, "tri_"),
          "a patch without the shapers wrote their keys");
    ma_patch_document shaped = plain;
    shaped.value.vco1.tri_sine_level = .5f;
    shaped.value.vco2.tri_saw_level = .25f;
    CHECK(patch_text(&shaped, text, sizeof text) && !strncmp(text, "mamutanalog_patch=7\n", 20)
          && strstr(text, "vco1.tri_sine=0.5\n") && strstr(text, "vco2.tri_saw=0.25\n")
          && strstr(text, "vco1.tri_saw=0\n") && strstr(text, "vco.analog=0\n"),
          "the shapers did not write as version 7 with the pitch block");
    ma_patch_document back = { 0 };
    ma_patch_error error = { 0 };
    CHECK(read_patch_text(text, &back, &error) && !memcmp(&back.value, &shaped.value, sizeof shaped.value),
          "the shapers did not round-trip: line %u %s", error.line, error.message);
    struct { const char *from, *to, *why; } bad[] = {
        { "mamutanalog_patch=7", "mamutanalog_patch=6", "version 7" },
        { "vco2.tri_sine=0\n", "", "missing" },
        { "vco1.tri_sine=0.5", "vco1.tri_sine=2", "domain" },
    };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        static char edited[16384];
        snprintf(edited, sizeof edited, "%s", text);
        bool replaced = edit_text(edited, sizeof edited, bad[i].from, bad[i].to);
        CHECK(replaced && !read_patch_text(edited, &back, &error) && strstr(error.message, bad[i].why),
              "malformed version 7 patch %u accepted or wrong reason: '%s'", i, error.message);
    }
    CHECK(read_patch_text(older, &back, &error) && back.value.vco1.tri_sine_level == 0
          && back.value.vco2.tri_saw_level == 0, "an older patch did not load with the shapers off");
}

/* RK1: .mapatch version 5 carries the rack (docs/ma-rack-plan.md RKD12). */
static void test_ma_patch_rack(void) {
    ma_patch_document plain = { .name = "Lead", .value = ma_patch_lead };
    static char text[16384], edited[16384];
    CHECK(patch_text(&plain, text, sizeof text) && !strncmp(text, "mamutanalog_patch=4\n", 20)
          && !strstr(text, "rack."), "an empty rack must keep writing version 4");

    ma_patch_document racked = plain;
    ma_rack_slot_init(&racked.rack.slot[0], MA_RACK_EQ, 5);
    racked.rack.slot[0].param[MA_EQ_BAND_COUNT] = 2;
    racked.rack.slot[0].param[MA_EQ_BAND(0, MA_EQ_TYPE)] = MA_EQ_LOW_CUT;
    racked.rack.slot[0].param[MA_EQ_BAND(0, MA_EQ_FREQ_HZ)] = 60;
    racked.rack.slot[0].param[MA_EQ_BAND(1, MA_EQ_GAIN_DB)] = -3.25f;
    racked.rack.slot[0].param[MA_EQ_BAND(1, MA_EQ_Q)] = 1.7f;
    racked.rack.slot[0].param[MA_EQ_OUTPUT_DB] = 1.5f;
    racked.rack.slot[0].mix = .8f;
    ma_rack_slot_init(&racked.rack.slot[3], MA_RACK_UTILITY, 9);
    racked.rack.slot[3].enabled = false;
    racked.rack.slot[3].param[MA_UTILITY_WIDTH] = 1.25f;
    CHECK(patch_text(&racked, text, sizeof text) && !strncmp(text, "mamutanalog_patch=5\n", 20)
          && strstr(text, "rack.1.kind=eq\n") && strstr(text, "rack.4.kind=utility\n")
          && strstr(text, "rack.4.enabled=0\n") && strstr(text, "rack.1.band.2.q=1.70000005\n")
          && strstr(text, "rack.1.band.1.type=low_cut\n") && strstr(text, "rack.1.bands=2\n")
          && !strstr(text, "rack.1.band.3."),
          "rack did not write as version 5");
    ma_patch_document back = { 0 };
    ma_patch_error error = { 0 };
    CHECK(read_patch_text(text, &back, &error)
          && !memcmp(&back.value, &racked.value, sizeof racked.value)
          && !memcmp(&back.rack, &racked.rack, sizeof racked.rack),
          "rack did not round-trip: line %u %s", error.line, error.message);

    struct { const char *from, *to, *why; } bad[] = {
        { "mamutanalog_patch=5", "mamutanalog_patch=4", "version 5" },
        { "rack.1.band.2.gain_db=-3.25\n", "", "missing" },
        { "rack.1.kind=eq\n", "rack.1.kind=eq\nrack.1.kind=eq\n", "duplicate" },
        { "rack.1.kind=eq", "rack.1.kind=flanger", "unknown rack kind" },
        { "rack.4.id=9", "rack.4.id=5", "unique" },
        { "rack.4.id=9", "rack.4.id=0", "integer" },
        { "rack.1.band.2.gain_db=-3.25", "rack.1.band.2.gain_db=25", "domain" },
        { "rack.1.band.1.on=1", "rack.1.band.1.on=0.5", "toggle" },
        { "rack.1.band.1.type=low_cut", "rack.1.band.1.type=wah", "unknown EQ band type" },
        { "rack.1.bands=2", "rack.1.bands=1.5", "whole" },
        { "rack.1.bands=2", "rack.1.bands=1", "past the band count" },
        { "rack.1.bands=2", "rack.1.bands=3", "missing" },
        { "rack.1.mix=0.800000012", "rack.1.mix=1.5", "mix" },
        { "rack.4.enabled=0", "rack.4.enabled=2", "enabled" },
        { "rack.1.band.2.q", "rack.9.band.2.q", "1..8" },
        { "rack.4.width", "rack.4.band.1.q", "unknown rack key" },
        { "rack.1.kind=eq\n", "", "kind must come first" },
    };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        memcpy(edited, text, sizeof text);
        ma_patch_document doc = { 0 };
        error = (ma_patch_error){ 0 };
        bool edited_ok = edit_text(edited, sizeof edited, bad[i].from, bad[i].to);
        CHECK(edited_ok && !read_patch_text(edited, &doc, &error) && strstr(error.message, bad[i].why),
              "rack case %zu (%s) gave \"%s\"", i, bad[i].why, error.message);
    }

    static const char *factory[] = { "dubina", "granica", "lead", "prizma", "raster", "tepih" };
    for (size_t i = 0; i < sizeof factory / sizeof *factory; i++) {
        char path[128];
        snprintf(path, sizeof path, "patches/mamutanalog/%s.mapatch", factory[i]);
        ma_patch_document doc = { 0 };
        CHECK(ma_patch_load(path, &doc, &error) && ma_rack_spec_empty(&doc.rack),
              "%s: %s", path, error.message);
    }
    ma_patch_document invalid = racked;
    invalid.rack.slot[3].id = 5;
    FILE *file = tmpfile();
    CHECK(file && !ma_patch_write(file, &invalid), "writer accepted duplicate rack ids");
    if (file) fclose(file);
}

int main(void) {
    test_smf_valid();
    test_smf_malformed();
    test_wav();
    test_host_parse();
    test_live_layout();
    test_ma_patch_files();
    test_ma_patch_rack();
    test_ma_patch_vco();
    test_ma_patch_shapers();
    printf("hosted: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
