#define _POSIX_C_SOURCE 200809L
#include "ma_patch_file.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "host_parse.h"

typedef enum { FIELD_FLOAT, FIELD_INT } field_kind;

typedef struct {
    const char *key;
    size_t offset;
    field_kind kind;
    double low;
    double high;
    double fine_step;
    double coarse_step;
} patch_field;

#define F(key, member, low, high, fine, coarse) \
    { key, offsetof(ma_patch, member), FIELD_FLOAT, low, high, fine, coarse }
#define I(key, member, low, high, fine, coarse) \
    { key, offsetof(ma_patch, member), FIELD_INT, low, high, fine, coarse }

static const patch_field FIELDS[] = {
    F("vco1.saw", vco1.saw_level, 0, 1, .01, .10),
    F("vco1.pulse", vco1.pulse_level, 0, 1, .01, .10),
    F("vco1.triangle", vco1.triangle_level, 0, 1, .01, .10),
    F("vco1.sine", vco1.sine_level, 0, 1, .01, .10),
    F("vco1.pulse_width", vco1.pulse_width, .05, .95, .01, .10),
    F("vco2.saw", vco2.saw_level, 0, 1, .01, .10),
    F("vco2.pulse", vco2.pulse_level, 0, 1, .01, .10),
    F("vco2.triangle", vco2.triangle_level, 0, 1, .01, .10),
    F("vco2.sine", vco2.sine_level, 0, 1, .01, .10),
    F("vco2.pulse_width", vco2.pulse_width, .05, .95, .01, .10),
    F("vco2.level", vco2_level, 0, 1, .01, .10),
    I("vco2.interval", vco2_interval, -24, 24, 1, 12),
    F("vco2.fine_cents", vco2_fine_cents, -50, 50, 1, 5),
    F("source.sync", sync_amount, 0, 1, .01, .10),
    F("source.sync_softness", sync_softness, 0, 1, .01, .10),
    F("source.crossmod", crossmod_amount, 0, 1, .01, .10),
    F("source.noise", noise_level, 0, 1, .01, .10),
    F("mozaik.mix", mozaik_mix, 0, 1, .01, .10),
    F("mozaik.slope", mozaik_slope, 0, 1, .01, .10),
    F("mozaik.contrast", mozaik_contrast, 0, 1, .01, .10),
    F("mozaik.phason", mozaik_phason, 0, 1, .01, .10),
    F("mozaik.drift", mozaik_drift, 0, 1, .01, .10),
    F("filter.mixer_pressure", mixer_pressure, 0, 1, .01, .10),
    F("filter.cutoff_hz", filter_cutoff_hz, 20, 20000, 10, 100),
    F("filter.resonance", filter_resonance, 0, 1, .01, .10),
    F("filter.drive", filter_drive, 0, 1, .01, .10),
    F("filter.env_amount", filter_env_amount, 0, 1, .01, .10),
    F("filter.keytrack", filter_keytrack, 0, 1, .01, .10),
    F("amp.attack_ms", amp_adsr.attack_ms, 1, 20000, 10, 100),
    F("amp.decay_ms", amp_adsr.decay_ms, 1, 20000, 10, 100),
    F("amp.sustain", amp_adsr.sustain, 0, 1, .01, .10),
    F("amp.release_ms", amp_adsr.release_ms, 1, 20000, 10, 100),
    F("filter_env.attack_ms", filter_adsr.attack_ms, 1, 20000, 10, 100),
    F("filter_env.decay_ms", filter_adsr.decay_ms, 1, 20000, 10, 100),
    F("filter_env.sustain", filter_adsr.sustain, 0, 1, .01, .10),
    F("filter_env.release_ms", filter_adsr.release_ms, 1, 20000, 10, 100),
    F("macro.gravitacija", macro[MA_MACRO_GRAVITACIJA], 0, 1, .01, .10),
    F("macro.bloom", macro[MA_MACRO_BLOOM], 0, 1, .01, .10),
    F("macro.heat", macro[MA_MACRO_HEAT], 0, 1, .01, .10),
    F("macro.ruin", macro[MA_MACRO_RUIN], 0, 1, .01, .10),
    F("macro.swarm", macro[MA_MACRO_SWARM], 0, 1, .01, .10),
    F("output.body_drive", body_drive, 0, 1, .01, .10),
    F("output.width", width, 0, 1, .01, .10),
    F("output.crossfeed", crossfeed, 0, 1, .01, .10),
    F("output.master", master_level, 0, 1, .01, .10),
};

static_assert(sizeof FIELDS / sizeof *FIELDS < 64,
              "patch required-field mask must fit uint64_t");

#undef F
#undef I

static void set_error(ma_patch_error *error, unsigned line,
                      const char *message) {
    if (!error) return;
    error->line = line;
    snprintf(error->message, sizeof error->message, "%s", message);
}

static bool valid_name(const char *name) {
    size_t length = name ? strnlen(name, MA_PATCH_NAME_MAX + 1) : 0;
    if (!length || length > MA_PATCH_NAME_MAX) return false;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c < 0x20 || c > 0x7e || c == '=') return false;
    }
    return true;
}

static int field_index(const char *key) {
    for (size_t i = 0; i < sizeof FIELDS / sizeof *FIELDS; i++)
        if (!strcmp(key, FIELDS[i].key)) return (int)i;
    return -1;
}

static bool assign_field(ma_patch *patch, const patch_field *field,
                         const char *text) {
    unsigned char *destination = (unsigned char *)patch + field->offset;
    double value = 0.0;
    if (!host_parse_double(text, field->low, field->high, &value))
        return false;
    if (field->kind == FIELD_INT) {
        int integer = (int)value;
        if ((double)integer != value) return false;
        memcpy(destination, &integer, sizeof integer);
    } else {
        float real = (float)value;
        memcpy(destination, &real, sizeof real);
    }
    return true;
}

size_t ma_patch_field_count(void) {
    return sizeof FIELDS / sizeof *FIELDS;
}

bool ma_patch_field_info_at(size_t index, ma_patch_field_info *info) {
    if (index >= ma_patch_field_count() || !info) return false;
    const patch_field *field = &FIELDS[index];
    *info = (ma_patch_field_info){
        .name = field->key,
        .minimum = field->low,
        .maximum = field->high,
        .fine_step = field->fine_step,
        .coarse_step = field->coarse_step,
        .integer = field->kind == FIELD_INT,
    };
    return true;
}

bool ma_patch_field_get(const ma_patch *patch, size_t index, double *value) {
    if (!patch || index >= ma_patch_field_count() || !value) return false;
    const patch_field *field = &FIELDS[index];
    const unsigned char *source = (const unsigned char *)patch + field->offset;
    if (field->kind == FIELD_INT) {
        int integer = 0;
        memcpy(&integer, source, sizeof integer);
        *value = integer;
    } else {
        float real = 0.0f;
        memcpy(&real, source, sizeof real);
        *value = real;
    }
    return true;
}

bool ma_patch_field_set(ma_patch *patch, size_t index, double value) {
    if (!patch || index >= ma_patch_field_count()) return false;
    const patch_field *field = &FIELDS[index];
    if (!(value >= field->low && value <= field->high)) return false;
    unsigned char *destination = (unsigned char *)patch + field->offset;
    if (field->kind == FIELD_INT) {
        int integer = (int)value;
        if ((double)integer != value) return false;
        memcpy(destination, &integer, sizeof integer);
    } else {
        float real = (float)value;
        memcpy(destination, &real, sizeof real);
    }
    return true;
}

bool ma_patch_read(FILE *file, ma_patch_document *document,
                   ma_patch_error *error) {
    if (!file || !document) {
        set_error(error, 0, "null patch input");
        return false;
    }
    *document = (ma_patch_document){ 0 };
    if (error) *error = (ma_patch_error){ 0 };
    uint64_t seen = 0;
    bool header = false, name = false;
    char line[256];
    unsigned line_number = 0;
    while (fgets(line, sizeof line, file)) {
        line_number++;
        size_t length = strlen(line);
        if (length && line[length - 1] == '\n') line[--length] = 0;
        else if (!feof(file)) {
            set_error(error, line_number, "line exceeds 255 bytes");
            return false;
        }
        if (length && line[length - 1] == '\r') line[--length] = 0;
        if (!length || line[0] == '#') continue;
        char *separator = strchr(line, '=');
        if (!separator || separator == line || !separator[1]) {
            set_error(error, line_number, "expected key=value");
            return false;
        }
        *separator++ = 0;
        if (!strcmp(line, "mamutanalog_patch")) {
            if (header || strcmp(separator, "1")) {
                set_error(error, line_number, "invalid or duplicate version");
                return false;
            }
            header = true;
            continue;
        }
        if (!strcmp(line, "name")) {
            if (name || !valid_name(separator)) {
                set_error(error, line_number, "invalid or duplicate name");
                return false;
            }
            snprintf(document->name, sizeof document->name, "%s", separator);
            name = true;
            continue;
        }
        int index = field_index(line);
        if (index < 0) {
            set_error(error, line_number, "unknown patch key");
            return false;
        }
        uint64_t bit = UINT64_C(1) << index;
        if (seen & bit) {
            set_error(error, line_number, "duplicate patch key");
            return false;
        }
        if (!assign_field(&document->value, &FIELDS[index], separator)) {
            set_error(error, line_number, "patch value outside its domain");
            return false;
        }
        seen |= bit;
    }
    if (ferror(file)) {
        set_error(error, line_number, "patch read failed");
        return false;
    }
    uint64_t required = (UINT64_C(1) << (sizeof FIELDS / sizeof *FIELDS)) - 1;
    if (!header || !name || seen != required) {
        set_error(error, line_number, "patch is missing required fields");
        return false;
    }
    return true;
}

bool ma_patch_write(FILE *file, const ma_patch_document *document) {
    if (!file || !document || !valid_name(document->name)) return false;
    for (size_t i = 0; i < ma_patch_field_count(); i++) {
        double value = 0.0;
        if (!ma_patch_field_get(&document->value, i, &value)
            || !(value >= FIELDS[i].low && value <= FIELDS[i].high))
            return false;
        if (FIELDS[i].kind == FIELD_INT && value != (int)value) return false;
    }
    if (fprintf(file, "mamutanalog_patch=1\nname=%s\n", document->name) < 0)
        return false;
    for (size_t i = 0; i < sizeof FIELDS / sizeof *FIELDS; i++) {
        const unsigned char *source = (const unsigned char *)&document->value
                                    + FIELDS[i].offset;
        int result;
        if (FIELDS[i].kind == FIELD_INT) {
            int value = 0;
            memcpy(&value, source, sizeof value);
            result = fprintf(file, "%s=%d\n", FIELDS[i].key, value);
        } else {
            float value = 0.0f;
            memcpy(&value, source, sizeof value);
            result = fprintf(file, "%s=%.9g\n", FIELDS[i].key,
                             (double)value);
        }
        if (result < 0) return false;
    }
    return !ferror(file);
}

bool ma_patch_load(const char *path, ma_patch_document *document,
                   ma_patch_error *error) {
    if (!path) {
        set_error(error, 0, "null patch path");
        return false;
    }
    FILE *file = fopen(path, "rb");
    if (!file) {
        set_error(error, 0, "cannot open patch");
        return false;
    }
    bool result = ma_patch_read(file, document, error);
    if (fclose(file) && result) {
        set_error(error, 0, "cannot close patch");
        result = false;
    }
    return result;
}

bool ma_patch_save(const char *path, const ma_patch_document *document,
                   ma_patch_error *error) {
    if (!path || !document) {
        set_error(error, 0, "null patch output");
        return false;
    }
    char temporary[4096];
    int length = snprintf(temporary, sizeof temporary, "%s.tmp.XXXXXX", path);
    if (length < 0 || (size_t)length >= sizeof temporary) {
        set_error(error, 0, "patch path is too long");
        return false;
    }
    int descriptor = mkstemp(temporary);
    if (descriptor < 0) {
        set_error(error, 0, "cannot create temporary patch");
        return false;
    }
    FILE *file = fdopen(descriptor, "wb");
    if (!file) {
        int saved_errno = errno;
        close(descriptor);
        remove(temporary);
        errno = saved_errno;
        set_error(error, 0, "cannot open temporary patch stream");
        return false;
    }
    bool result = ma_patch_write(file, document)
               && fflush(file) == 0 && fsync(fileno(file)) == 0;
    if (fclose(file)) result = false;
    if (result && rename(temporary, path) == 0) return true;
    int saved_errno = errno;
    remove(temporary);
    errno = saved_errno;
    set_error(error, 0, "cannot commit patch file");
    return false;
}
