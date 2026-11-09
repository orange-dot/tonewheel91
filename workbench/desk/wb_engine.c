#define _POSIX_C_SOURCE 200809L
#include "wb_engine.h"

#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "wb_format.h"

[[gnu::format(printf, 3, 4)]]
static bool fail(wb_error *error, unsigned line, const char *format, ...) {
    if (error) {
        error->line = line;
        va_list args;
        va_start(args, format);
        vsnprintf(error->message, sizeof error->message, format, args);
        va_end(args);
    }
    return false;
}

void wb_twreg_default(wb_twreg *r) {
    /* render_midi's defaults: 888000000, no vibrato, no percussion, drive
     * and wear 0, rotary bypass. */
    *r = (wb_twreg){ .registration = { 8, 8, 8 }, .percussion_soft = false,
                     .rotary_balance = -1, .rotary_width = -1, .rotary_drive = -1, .swell = -1 };
}

void wb_epset_default(wb_epset *e) {
    *e = (wb_epset){ .condition = EP_CONDITION_DEFAULT, .tremolo = -1 };
}

/* ---- catalogs ----------------------------------------------------------- */

static const wb_engine_field TWREG_FIELDS[] = {
    { "registration", 0, 888888888, 1, 1, true },
    { "vibrato", 0, 6, 1, 1, true },
    { "percussion", 0, 1, 1, 1, true },
    { "percussion_third", 0, 1, 1, 1, true },
    { "percussion_slow", 0, 1, 1, 1, true },
    { "percussion_soft", 0, 1, 1, 1, true },
    { "drive", 0, 1, .01, .1, false },
    { "rotary", 0, 3, 1, 1, true },
    { "wear", 0, 1, .01, .1, false },
    { "fold", 0, 1, 1, 1, true },
    { "rotary_balance", 0, 1, .01, .1, false },
    { "rotary_width", 0, 1, .01, .1, false },
    { "rotary_drive", 0, 1, .01, .1, false },
    { "swell", 0, 1, .01, .1, false },
};

static const wb_engine_field EPSET_FIELDS[] = {
    { "drive", 0, 1, .01, .1, false },
    { "cabinet", 0, 1, .01, .1, false },
    { "condition", 0, 1, .01, .1, false },
    { "fold", 0, 1, 1, 1, true },
    { "tremolo", 0, 127, 1, 8, true },
};

size_t wb_twreg_field_count(void) { return sizeof TWREG_FIELDS / sizeof *TWREG_FIELDS; }
const wb_engine_field *wb_twreg_field(size_t i) { return i < wb_twreg_field_count() ? &TWREG_FIELDS[i] : nullptr; }
size_t wb_epset_field_count(void) { return sizeof EPSET_FIELDS / sizeof *EPSET_FIELDS; }
const wb_engine_field *wb_epset_field(size_t i) { return i < wb_epset_field_count() ? &EPSET_FIELDS[i] : nullptr; }

/* ---- strict reading ------------------------------------------------------- */

static bool number(const char *v, double lo, double hi, bool integer, double *out) {
    char *end;
    if (!*v || *v == ' ') return false;
    double x = strtod(v, &end);
    if (*end || !isfinite(x) || x < lo || x > hi || (integer && x != floor(x))) return false;
    *out = x;
    return true;
}

static bool yes_no(const char *v, bool *out) {
    if (!strcmp(v, "yes") || !strcmp(v, "on")) { *out = true; return true; }
    if (!strcmp(v, "no") || !strcmp(v, "off")) { *out = false; return true; }
    return false;
}

typedef bool assign_fn(void *object, const char *key, const char *value);

static bool read_kv(const char *text, size_t size, const char *header, void *object, char *name,
                    const char *const *required, assign_fn *assign, wb_error *error) {
    char line[512];
    unsigned number_of_line = 0, seen_required = 0, required_count = 0;
    while (required[required_count]) required_count++;
    bool version = false, have_name = false;
    char seen_keys[32][32];
    unsigned seen_count = 0;
    for (size_t at = 0; at < size;) {
        number_of_line++;
        const char *nl = memchr(text + at, '\n', size - at);
        size_t length = nl ? (size_t)(nl - text - at) : size - at;
        if (length >= sizeof line) return fail(error, number_of_line, "line too long");
        memcpy(line, text + at, length);
        line[length] = 0;
        at += length + (nl ? 1 : 0);
        if (length && line[length - 1] == '\r') line[--length] = 0;
        if (!length || line[0] == '#') continue;
        char *eq = strchr(line, '=');
        if (!eq || eq == line) return fail(error, number_of_line, "expected key=value");
        *eq = 0;
        const char *key = line, *value = eq + 1;
        if (!version) {
            if (strcmp(key, header) || strcmp(value, "1"))
                return fail(error, number_of_line, "expected %s=1 first", header);
            version = true;
            continue;
        }
        for (unsigned i = 0; i < seen_count; i++)
            if (!strcmp(seen_keys[i], key)) return fail(error, number_of_line, "duplicate key '%s'", key);
        if (seen_count < 32) snprintf(seen_keys[seen_count++], 32, "%.31s", key);
        if (!strcmp(key, "name")) {
            size_t n = strlen(value);
            if (!n || n > WB_ENGINE_NAME_MAX || !wb_text_valid(value, WB_ENGINE_NAME_MAX))
                return fail(error, number_of_line, "invalid name");
            memcpy(name, value, n + 1);
            have_name = true;
            continue;
        }
        if (!assign(object, key, value))
            return fail(error, number_of_line, "unknown key or value outside its domain: %s=%s", key, value);
        for (unsigned r = 0; r < required_count; r++) seen_required += !strcmp(required[r], key);
    }
    if (!version) return fail(error, number_of_line, "missing %s=1", header);
    if (!have_name || seen_required != required_count)
        return fail(error, number_of_line, "missing required keys");
    return true;
}

static bool assign_twreg(void *o, const char *key, const char *v) {
    wb_twreg *r = o;
    double x;
    if (!strcmp(key, "registration")) {
        if (strlen(v) != TW_DRAWBARS) return false;
        for (unsigned i = 0; i < TW_DRAWBARS; i++) {
            if (v[i] < '0' || v[i] > '8') return false;
            r->registration[i] = (uint8_t)(v[i] - '0');
        }
        return true;
    }
    if (!strcmp(key, "percussion")) return yes_no(v, &r->percussion);
    if (!strcmp(key, "percussion_third")) return yes_no(v, &r->percussion_third);
    if (!strcmp(key, "percussion_slow")) return yes_no(v, &r->percussion_slow);
    if (!strcmp(key, "percussion_soft")) return yes_no(v, &r->percussion_soft);
    if (!strcmp(key, "fold")) return yes_no(v, &r->fold);
    if (!strcmp(key, "rotary")) {
        static const char *const MODES[] = { "bypass", "chorale", "tremolo", "brake" };
        for (int i = 0; i < 4; i++) if (!strcmp(v, MODES[i])) { r->rotary = i; return true; }
        return false;
    }
    if (!strcmp(key, "vibrato")) {
        static const char *const MODES[] = { "off", "v1", "v2", "v3", "c1", "c2", "c3" };
        for (int i = 0; i < 7; i++) if (!strcmp(v, MODES[i])) { r->vibrato = i; return true; }
        return false;
    }
    float *f = !strcmp(key, "drive") ? &r->drive : !strcmp(key, "wear") ? &r->wear
             : !strcmp(key, "rotary_balance") ? &r->rotary_balance
             : !strcmp(key, "rotary_width") ? &r->rotary_width
             : !strcmp(key, "rotary_drive") ? &r->rotary_drive
             : !strcmp(key, "swell") ? &r->swell : nullptr;
    if (!f || !number(v, 0, 1, false, &x)) return false;
    *f = (float)x;
    return true;
}

static bool assign_epset(void *o, const char *key, const char *v) {
    wb_epset *e = o;
    double x;
    if (!strcmp(key, "fold")) return yes_no(v, &e->fold);
    if (!strcmp(key, "tremolo")) {
        if (!strcmp(v, "untouched")) { e->tremolo = -1; return true; }
        if (!number(v, 0, 127, true, &x)) return false;
        e->tremolo = (int)x;
        return true;
    }
    float *f = !strcmp(key, "drive") ? &e->drive : !strcmp(key, "cabinet") ? &e->cabinet
             : !strcmp(key, "condition") ? &e->condition : nullptr;
    if (!f || !number(v, 0, 1, false, &x)) return false;
    *f = (float)x;
    return true;
}

bool wb_twreg_read(const char *text, size_t size, wb_twreg *r, wb_error *error) {
    wb_twreg_default(r);
    static const char *const REQUIRED[] = { "registration", nullptr };
    return read_kv(text, size, "twreg", r, r->name, REQUIRED, assign_twreg, error);
}

bool wb_epset_read(const char *text, size_t size, wb_epset *e, wb_error *error) {
    wb_epset_default(e);
    static const char *const REQUIRED[] = { nullptr };
    return read_kv(text, size, "epset", e, e->name, REQUIRED, assign_epset, error);
}

static void put_float(FILE *f, const char *key, float v) {
    char text[32];
    wb_format_f32(v, text);
    fprintf(f, "%s=%s\n", key, text);
}

bool wb_twreg_write(FILE *f, const wb_twreg *r) {
    static const char *const VIB[] = { "off", "v1", "v2", "v3", "c1", "c2", "c3" };
    static const char *const ROT[] = { "bypass", "chorale", "tremolo", "brake" };
    if (r->vibrato < 0 || r->vibrato > 6 || r->rotary < 0 || r->rotary > 3) return false;
    fprintf(f, "twreg=1\nname=%s\nregistration=", r->name);
    for (unsigned i = 0; i < TW_DRAWBARS; i++) fputc('0' + r->registration[i], f);
    fprintf(f, "\nvibrato=%s\npercussion=%s\npercussion_third=%s\npercussion_slow=%s\npercussion_soft=%s\n",
            VIB[r->vibrato], r->percussion ? "on" : "off", r->percussion_third ? "yes" : "no",
            r->percussion_slow ? "yes" : "no", r->percussion_soft ? "yes" : "no");
    put_float(f, "drive", r->drive);
    fprintf(f, "rotary=%s\n", ROT[r->rotary]);
    put_float(f, "wear", r->wear);
    fprintf(f, "fold=%s\n", r->fold ? "yes" : "no");
    if (r->rotary_balance >= 0) put_float(f, "rotary_balance", r->rotary_balance);
    if (r->rotary_width >= 0) put_float(f, "rotary_width", r->rotary_width);
    if (r->rotary_drive >= 0) put_float(f, "rotary_drive", r->rotary_drive);
    if (r->swell >= 0) put_float(f, "swell", r->swell);
    return !ferror(f);
}

bool wb_epset_write(FILE *f, const wb_epset *e) {
    fprintf(f, "epset=1\nname=%s\n", e->name);
    put_float(f, "drive", e->drive);
    put_float(f, "cabinet", e->cabinet);
    put_float(f, "condition", e->condition);
    fprintf(f, "fold=%s\n", e->fold ? "yes" : "no");
    if (e->tremolo >= 0) fprintf(f, "tremolo=%d\n", e->tremolo);
    return !ferror(f);
}

typedef bool writer_fn(FILE *, const void *);
static bool w_twreg(FILE *f, const void *o) { return wb_twreg_write(f, o); }
static bool w_epset(FILE *f, const void *o) { return wb_epset_write(f, o); }

static bool save(const char *path, const void *o, writer_fn *write, wb_error *error) {
    char *data = nullptr;
    size_t size = 0;
    FILE *f = open_memstream(&data, &size);
    if (!f) return fail(error, 0, "out of memory");
    bool ok = write(f, o);
    if (fclose(f)) ok = false;
    ok = ok && wb_write_file_atomic(path, data, size);
    free(data);
    return ok || fail(error, 0, "cannot write %s", path);
}

bool wb_twreg_save(const char *path, const wb_twreg *r, wb_error *error) { return save(path, r, w_twreg, error); }
bool wb_epset_save(const char *path, const wb_epset *e, wb_error *error) { return save(path, e, w_epset, error); }

bool wb_twreg_load(const char *path, wb_twreg *r, wb_error *error) {
    char *data;
    size_t size;
    if (!wb_read_file(path, &data, &size)) return fail(error, 0, "cannot read %s", path);
    bool ok = wb_twreg_read(data, size, r, error);
    free(data);
    return ok;
}

bool wb_epset_load(const char *path, wb_epset *e, wb_error *error) {
    char *data;
    size_t size;
    if (!wb_read_file(path, &data, &size)) return fail(error, 0, "cannot read %s", path);
    bool ok = wb_epset_read(data, size, e, error);
    free(data);
    return ok;
}

/* ---- instances ------------------------------------------------------------- */

bool wb_engine_open_organ(wb_engine_instance *e, unsigned rate, const wb_twreg *r) {
    *e = (wb_engine_instance){ .kind = WB_PART_ORGAN };
    e->organ = malloc(sizeof *e->organ);
    e->organ_map = malloc(sizeof *e->organ_map);
    if (!e->organ || !e->organ_map) { wb_engine_close(e); return false; }
    /* render_midi's order, setter for setter. */
    tw_instrument_init(e->organ, (float)rate);
    tw_organ_set_registration(&e->organ->organ, r->registration);
    tw_organ_set_wear(&e->organ->organ, r->wear);
    tw_organ_set_vibrato(&e->organ->organ, r->vibrato);
    tw_instrument_set_drive(e->organ, r->drive);
    tw_rotary_set_mode(&e->organ->rotary, r->rotary);
    organ_midi_map_init(e->organ_map, r->fold, r->percussion);
    tw_organ_set_percussion(&e->organ->organ, r->percussion, r->percussion_third, r->percussion_slow,
                            !r->percussion_soft);
    if (r->rotary_balance >= 0) tw_rotary_set_balance(&e->organ->rotary, r->rotary_balance);
    if (r->rotary_width >= 0) tw_rotary_set_width(&e->organ->rotary, r->rotary_width);
    if (r->rotary_drive >= 0) tw_rotary_set_drive(&e->organ->rotary, r->rotary_drive);
    if (r->swell >= 0) tw_organ_set_swell(&e->organ->organ, r->swell);
    return true;
}

bool wb_engine_open_ep(wb_engine_instance *e, unsigned rate, const wb_epset *s) {
    *e = (wb_engine_instance){ .kind = WB_PART_EP73 };
    e->ep = malloc(sizeof *e->ep);
    e->ep_map = malloc(sizeof *e->ep_map);
    if (!e->ep || !e->ep_map) { wb_engine_close(e); return false; }
    ep_piano_init(e->ep, (float)rate);
    ep_piano_set_drive(e->ep, s->drive);
    ep_piano_set_cabinet(e->ep, s->cabinet);
    ep_piano_set_condition(e->ep, s->condition);
    ep_midi_map_init(e->ep_map, s->fold);
    if (s->tremolo >= 0) ep_piano_set_tremolo(e->ep, s->tremolo);
    return true;
}

void wb_engine_close(wb_engine_instance *e) {
    free(e->organ);
    free(e->organ_map);
    free(e->ep);
    free(e->ep_map);
    *e = (wb_engine_instance){ 0 };
}

void wb_engine_midi(wb_engine_instance *e, uint8_t status, uint8_t d1, uint8_t d2) {
    if (e->kind == WB_PART_ORGAN) organ_midi_apply(e->organ_map, e->organ, status, d1, d2);
    else if (e->kind == WB_PART_EP73) ep_midi_apply(e->ep_map, e->ep, status, d1, d2);
}

tw_stereo wb_engine_tick(wb_engine_instance *e) {
    if (e->kind == WB_PART_ORGAN) return tw_instrument_tick_stereo(e->organ);
    return ep_piano_tick_stereo(e->ep);
}

unsigned long wb_engine_notes(const wb_engine_instance *e) {
    return e->kind == WB_PART_ORGAN ? e->organ_map->stats.notes : e->ep_map->stats.notes;
}

uint32_t wb_engine_out_of_compass(const wb_engine_instance *e) {
    return e->kind == WB_PART_ORGAN ? e->organ->organ.out_of_compass : e->ep->bank.out_of_compass;
}
