#define _POSIX_C_SOURCE 200809L
#include "ma_patch_file.h"

#include <errno.h>
#include <stdarg.h>
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
    F("raster.mix", raster_mix, 0, 1, .01, .10),
    F("raster.position", raster_position, 0, 1, .01, .10),
    F("raster.warp", raster_warp, 0, 1, .01, .10),
    F("bcs.amount", bcs_amount, 0, 1, .01, .10),
    F("bcs.regime", bcs_regime, 0, 1, .01, .10),
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
    /* Version 4: Glas, appended so earlier indices do not move. */
    F("glas.formant_mix", formant_mix, 0, 1, .01, .10),
    F("glas.formant_vowel", formant_vowel, 0, 1, .01, .10),
    F("glas.formant_shift", formant_shift, -1, 1, .01, .10),
    F("glas.formant_orbita", formant_orbita, 0, 1, .01, .10),
    F("glas.grain_mix", grain_mix, 0, 1, .01, .10),
    F("vco.analog", vco_analog, 0, 1, .01, .10),
    F("vco.dead_time", vco_dead_time, 0, 1, .01, .10),
    /* Version 7: the triangle core's shapers (docs/ma-vco-plan.md, V3). */
    F("vco1.tri_sine", vco1.tri_sine_level, 0, 1, .01, .10),
    F("vco1.tri_saw", vco1.tri_saw_level, 0, 1, .01, .10),
    F("vco2.tri_sine", vco2.tri_sine_level, 0, 1, .01, .10),
    F("vco2.tri_saw", vco2.tri_saw_level, 0, 1, .01, .10),
};

static_assert(sizeof FIELDS / sizeof *FIELDS < 64,
              "patch required-field mask must fit uint64_t");
static constexpr unsigned RASTER_FIRST_FIELD = 17;
static constexpr unsigned RASTER_FIELD_COUNT = 3;
static constexpr unsigned BCS_FIRST_FIELD = 20;
static constexpr unsigned BCS_FIELD_COUNT = 2;
static constexpr unsigned GLAS_FIRST_FIELD = 50;
static constexpr unsigned GLAS_FIELD_COUNT = 5;
/* Version 6: the circuit-derived oscillator pitch (docs/ma-vco-plan.md). */
static constexpr unsigned VCO_FIRST_FIELD = 55;
static constexpr unsigned VCO_FIELD_COUNT = 2;
static_assert(GLAS_FIRST_FIELD + GLAS_FIELD_COUNT == VCO_FIRST_FIELD,
              "the oscillator-pitch block follows the Glas block");
static constexpr unsigned SHAPER_FIRST_FIELD = 57;
static constexpr unsigned SHAPER_FIELD_COUNT = 4;
static_assert(VCO_FIRST_FIELD + VCO_FIELD_COUNT == SHAPER_FIRST_FIELD,
              "the shaper block follows the oscillator-pitch block");
static_assert(SHAPER_FIRST_FIELD + SHAPER_FIELD_COUNT == sizeof FIELDS / sizeof *FIELDS,
              "the shaper block closes the catalog");

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

/* --- version 5: rack.<n>.* ------------------------------------------- */

static const char *const RACK_KIND_NAMES[MA_RACK_KIND_COUNT] = {
    [MA_RACK_EMPTY] = "empty", [MA_RACK_EQ] = "eq", [MA_RACK_UTILITY] = "utility",
};
static const char *const EQ_TYPE_NAMES[MA_EQ_TYPE_COUNT] = {
    [MA_EQ_BELL] = "bell", [MA_EQ_LOW_SHELF] = "low_shelf", [MA_EQ_HIGH_SHELF] = "high_shelf",
    [MA_EQ_LOW_CUT] = "low_cut", [MA_EQ_HIGH_CUT] = "high_cut", [MA_EQ_NOTCH] = "notch",
};
static const char *const EQ_FIELD_NAMES[MA_EQ_BAND_FIELDS] = {
    [MA_EQ_TYPE] = "type", [MA_EQ_FREQ_HZ] = "freq_hz", [MA_EQ_GAIN_DB] = "gain_db",
    [MA_EQ_Q] = "q", [MA_EQ_ON] = "on",
};
static const char *const UTILITY_PARAM_NAMES[MA_UTILITY_PARAM_COUNT] = {
    [MA_UTILITY_GAIN_DB] = "gain_db", [MA_UTILITY_WIDTH] = "width", [MA_UTILITY_PAN] = "pan",
};
/* "band.<b>.<field>", b 1-based, built once. */
static_assert(MA_RACK_PARAMS <= 64, "the reader marks keys in a uint64_t");
static char eq_names[MA_EQ_BANDS * MA_EQ_BAND_FIELDS][24];

const char *ma_rack_kind_name(ma_rack_kind kind) {
    return kind < MA_RACK_KIND_COUNT ? RACK_KIND_NAMES[kind] : nullptr;
}

const char *ma_eq_type_name(ma_eq_type type) {
    return type < MA_EQ_TYPE_COUNT ? EQ_TYPE_NAMES[type] : nullptr;
}

const char *ma_rack_param_name(ma_rack_kind kind, unsigned index) {
    if (kind == MA_RACK_UTILITY && index < MA_UTILITY_PARAM_COUNT) return UTILITY_PARAM_NAMES[index];
    if (kind != MA_RACK_EQ || index >= MA_EQ_PARAM_COUNT) return nullptr;
    if (index == MA_EQ_BAND_COUNT) return "bands";
    if (index == MA_EQ_OUTPUT_DB) return "output_db";
    unsigned at = index - MA_EQ_FIRST_BAND;
    if (!eq_names[at][0])
        snprintf(eq_names[at], sizeof eq_names[at], "band.%u.%s", at / MA_EQ_BAND_FIELDS + 1,
                 EQ_FIELD_NAMES[at % MA_EQ_BAND_FIELDS]);
    return eq_names[at];
}

static bool is_band(ma_rack_kind kind, unsigned index) {
    return kind == MA_RACK_EQ && index >= MA_EQ_FIRST_BAND;
}
static unsigned band_of(unsigned index) { return (index - MA_EQ_FIRST_BAND) / MA_EQ_BAND_FIELDS; }

/* One rack.<n>.<key>=value line. The kind must come first in its slot, so
 * the parameter names it owns are known; it also puts every parameter at
 * its initial value, which is what an EQ band past the count holds. */
bool ma_rack_read_key(ma_rack_spec *rack, ma_rack_reader *reader,
                      const char *key, const char *text, const char **why) {
    if (strncmp(key, "rack.", 5)) { *why = "not a rack key"; return false; }
    const char *p = key + 5;
    if (*p < '1' || *p > '8' || p[1] != '.') { *why = "rack slot must be 1..8"; return false; }
    unsigned n = (unsigned)(*p - '1');
    const char *field = p + 2;
    ma_rack_slot_spec *slot = &rack->slot[n];
    typeof(reader->slot[0]) *s = &reader->slot[n];
    if (!strcmp(field, "kind")) {
        if (s->kind) { *why = "duplicate rack key"; return false; }
        for (unsigned k = MA_RACK_EQ; k < MA_RACK_KIND_COUNT; k++)
            if (!strcmp(text, RACK_KIND_NAMES[k])) {
                ma_rack_slot_init(slot, (ma_rack_kind)k, 1);   /* initial values */
                slot->id = 0;
                slot->enabled = false;
                slot->mix = 0;
                s->kind = true;
                return true;
            }
        *why = "unknown rack kind";
        return false;
    }
    if (!s->kind) { *why = "rack kind must come first in its slot"; return false; }
    double value = 0;
    if (!strcmp(field, "id")) {
        if (s->id) { *why = "duplicate rack key"; return false; }
        if (!host_parse_double(text, 1, 4294967295.0, &value) || value != (double)(uint32_t)value) {
            *why = "rack id must be an integer 1..4294967295";
            return false;
        }
        slot->id = (uint32_t)value;
        s->id = true;
        return true;
    }
    if (!strcmp(field, "enabled")) {
        if (s->enabled) { *why = "duplicate rack key"; return false; }
        if (strcmp(text, "0") && strcmp(text, "1")) { *why = "rack enabled must be 0 or 1"; return false; }
        slot->enabled = text[0] == '1';
        s->enabled = true;
        return true;
    }
    if (!strcmp(field, "mix")) {
        if (s->mix) { *why = "duplicate rack key"; return false; }
        if (!host_parse_double(text, 0, 1, &value)) { *why = "rack mix outside 0..1"; return false; }
        slot->mix = (float)value;
        s->mix = true;
        return true;
    }
    unsigned count = ma_rack_param_count(slot->kind);
    for (unsigned i = 0; i < count; i++) {
        if (strcmp(field, ma_rack_param_name(slot->kind, i))) continue;
        if (s->params >> i & 1u) { *why = "duplicate rack key"; return false; }
        ma_rack_param_info info = { 0 };
        (void)ma_rack_param_info_at(slot->kind, i, &info);
        if (is_band(slot->kind, i) && (i - MA_EQ_FIRST_BAND) % MA_EQ_BAND_FIELDS == MA_EQ_TYPE) {
            unsigned t = 0;
            while (t < MA_EQ_TYPE_COUNT && strcmp(text, EQ_TYPE_NAMES[t])) t++;
            if (t == MA_EQ_TYPE_COUNT) { *why = "unknown EQ band type"; return false; }
            value = t;
        } else if (!host_parse_double(text, info.minimum, info.maximum, &value)) {
            *why = "rack value outside its domain";
            return false;
        }
        if (info.curve == MA_RACK_TOGGLE && value != 0 && value != 1) {
            *why = "rack toggle must be 0 or 1";
            return false;
        }
        if (info.curve == MA_RACK_STEPPED && value != (double)(int)value) {
            *why = "rack value must be whole";
            return false;
        }
        slot->param[i] = (float)value;
        s->params |= UINT64_C(1) << i;
        return true;
    }
    *why = "unknown rack key";
    return false;
}

/* Every key of an occupied slot once; for an EQ, the five keys of each band
 * below the count and none of a band past it. */
bool ma_rack_read_finish(const ma_rack_spec *rack, const ma_rack_reader *reader, const char **why) {
    for (unsigned n = 0; n < MA_RACK_SLOTS; n++) {
        const typeof(reader->slot[0]) *s = &reader->slot[n];
        if (!s->kind) continue;
        const ma_rack_slot_spec *slot = &rack->slot[n];
        unsigned count = ma_rack_param_count(slot->kind);
        unsigned bands = slot->kind == MA_RACK_EQ ? (unsigned)slot->param[MA_EQ_BAND_COUNT] : 0;
        uint64_t want = 0;
        for (unsigned i = 0; i < count; i++)
            if (!is_band(slot->kind, i) || band_of(i) < bands) want |= UINT64_C(1) << i;
        if (s->params & ~want) {
            *why = "EQ band past the band count";
            return false;
        }
        if (!s->id || !s->enabled || !s->mix || s->params != want) {
            *why = "rack slot is missing required keys";
            return false;
        }
    }
    if (!ma_rack_spec_valid(rack)) {
        *why = "rack ids must be unique";
        return false;
    }
    return true;
}

[[gnu::format(printf, 4, 5)]]
static bool emit(char *out, size_t size, size_t *used, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int k = vsnprintf(out + *used, size - *used, format, args);
    va_end(args);
    if (k < 0 || (size_t)k >= size - *used) return false;
    *used += (size_t)k;
    return true;
}

int ma_rack_format(const ma_rack_spec *rack, char *out, size_t size) {
    if (!rack || !out || !size || !ma_rack_spec_valid(rack)) return -1;
    size_t used = 0;
    out[0] = 0;
    for (unsigned n = 0; n < MA_RACK_SLOTS; n++) {
        const ma_rack_slot_spec *slot = &rack->slot[n];
        if (slot->kind == MA_RACK_EMPTY) continue;
        if (!emit(out, size, &used, "rack.%u.kind=%s\nrack.%u.id=%u\nrack.%u.enabled=%d\nrack.%u.mix=%.9g\n",
                  n + 1, RACK_KIND_NAMES[slot->kind], n + 1, (unsigned)slot->id,
                  n + 1, slot->enabled ? 1 : 0, n + 1, (double)slot->mix))
            return -1;
        unsigned bands = slot->kind == MA_RACK_EQ ? (unsigned)slot->param[MA_EQ_BAND_COUNT] : 0;
        for (unsigned i = 0; i < ma_rack_param_count(slot->kind); i++) {
            if (is_band(slot->kind, i) && band_of(i) >= bands) break;
            const char *name = ma_rack_param_name(slot->kind, i);
            bool type = is_band(slot->kind, i) && (i - MA_EQ_FIRST_BAND) % MA_EQ_BAND_FIELDS == MA_EQ_TYPE;
            bool ok = type ? emit(out, size, &used, "rack.%u.%s=%s\n", n + 1, name,
                                  EQ_TYPE_NAMES[(unsigned)slot->param[i]])
                           : emit(out, size, &used, "rack.%u.%s=%.9g\n", n + 1, name, (double)slot->param[i]);
            if (!ok) return -1;
        }
    }
    return (int)used;
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
    ma_rack_reader rack = { 0 };
    bool header = false, name = false;
    unsigned version = 0;
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
            if (header || (strcmp(separator, "1")
                           && strcmp(separator, "2")
                           && strcmp(separator, "3")
                           && strcmp(separator, "4")
                           && strcmp(separator, "5")
                           && strcmp(separator, "6")
                           && strcmp(separator, "7"))) {
                set_error(error, line_number, "invalid or duplicate version");
                return false;
            }
            version = (unsigned)(separator[0] - '0');
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
        if (!strncmp(line, "rack.", 5)) {
            const char *why = nullptr;
            if (version < 5) {
                set_error(error, line_number, "rack keys require patch version 5");
                return false;
            }
            if (!ma_rack_read_key(&document->rack, &rack, line, separator, &why)) {
                set_error(error, line_number, why);
                return false;
            }
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
    uint64_t raster_fields = ((UINT64_C(1) << RASTER_FIELD_COUNT) - 1)
                           << RASTER_FIRST_FIELD;
    uint64_t bcs_fields = ((UINT64_C(1) << BCS_FIELD_COUNT) - 1)
                        << BCS_FIRST_FIELD;
    uint64_t glas_fields = ((UINT64_C(1) << GLAS_FIELD_COUNT) - 1)
                         << GLAS_FIRST_FIELD;
    uint64_t vco_fields = version < 6
                        ? ((UINT64_C(1) << VCO_FIELD_COUNT) - 1) << VCO_FIRST_FIELD : 0;
    if (seen & vco_fields) {
        set_error(error, line_number, "oscillator pitch fields require patch version 6");
        return false;
    }
    required &= ~vco_fields;
    uint64_t shaper_fields = version < 7
                           ? ((UINT64_C(1) << SHAPER_FIELD_COUNT) - 1) << SHAPER_FIRST_FIELD : 0;
    if (seen & shaper_fields) {
        set_error(error, line_number, "triangle shaper fields require patch version 7");
        return false;
    }
    required &= ~shaper_fields;
    /* Earlier versions predate a block: its fields are refused and the
     * block stays at the reader's zero, the bypass the compiled bank uses. */
    uint64_t newer = version == 1 ? raster_fields | bcs_fields | glas_fields
                   : version == 2 ? bcs_fields | glas_fields
                   : version == 3 ? glas_fields : 0;
    if (seen & newer) {
        set_error(error, line_number,
                  version == 3 ? "Glas fields require patch version 4"
                  : version == 2 ? "BCS and Glas fields require newer patch versions"
                  : "Raster, BCS and Glas fields require newer patch versions");
        return false;
    }
    required &= ~newer;
    if (!header || !name || seen != required) {
        set_error(error, line_number, "patch is missing required fields");
        return false;
    }
    const char *why = nullptr;
    if (!ma_rack_read_finish(&document->rack, &rack, &why)) {
        set_error(error, line_number, why);
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
    if (!ma_rack_spec_valid(&document->rack)) return false;
    /* An empty rack keeps writing version 4, byte for byte as before; so
     * does a patch without the oscillator pitch law (version 4 or 5), and
     * one without the triangle shapers (version 6 or below). */
    bool racked = !ma_rack_spec_empty(&document->rack);
    const ma_patch *v = &document->value;
    bool shaped = v->vco1.tri_sine_level != 0.0f || v->vco1.tri_saw_level != 0.0f
               || v->vco2.tri_sine_level != 0.0f || v->vco2.tri_saw_level != 0.0f;
    bool analog = shaped || v->vco_analog != 0.0f || v->vco_dead_time != 0.0f;
    int version = shaped ? 7 : analog ? 6 : racked ? 5 : 4;
    size_t count = shaped ? sizeof FIELDS / sizeof *FIELDS
                 : analog ? SHAPER_FIRST_FIELD : VCO_FIRST_FIELD;
    if (fprintf(file, "mamutanalog_patch=%d\nname=%s\n", version, document->name) < 0)
        return false;
    for (size_t i = 0; i < count; i++) {
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
    if (racked) {
        char text[8192];
        if (ma_rack_format(&document->rack, text, sizeof text) < 0 || fputs(text, file) < 0)
            return false;
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
