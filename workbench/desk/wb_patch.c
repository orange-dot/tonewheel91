#define _POSIX_C_SOURCE 200809L
#include "wb_patch.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/mamutanalog.h"
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

bool wb_patch_is_chain(const char *path) {
    size_t n = strlen(path);
    return n > 8 && !strcmp(path + n - 8, ".wbpatch");
}

static int field_index(const char *key) {
    for (size_t i = 0; i < ma_patch_field_count(); i++) {
        ma_patch_field_info info;
        if (ma_patch_field_info_at(i, &info) && !strcmp(info.name, key)) return (int)i;
    }
    return -1;
}

bool wb_patch_read_overrides(const char *path, wb_patch_overrides *p, wb_error *error) {
    *p = (wb_patch_overrides){ 0 };
    char *data;
    size_t size;
    if (!wb_read_file(path, &data, &size)) return fail(error, 0, "cannot read %s", path);
    bool header = false, name = false, parent = false, ok = true;
    unsigned number = 0;
    for (char *line = data; ok && line && *line;) {
        number++;
        char *end = strchr(line, '\n');
        if (end) *end = 0;
        size_t length = strlen(line);
        if (length && line[length - 1] == '\r') line[--length] = 0;
        char *next = end ? end + 1 : nullptr;
        if (!length || line[0] == '#') { line = next; continue; }
        char *eq = strchr(line, '=');
        if (!eq || eq == line) { ok = fail(error, number, "expected key=value"); break; }
        *eq = 0;
        const char *v = eq + 1;
        if (!header) {
            if (strcmp(line, "wbpatch") || strcmp(v, "1")) { ok = fail(error, number, "expected wbpatch=1 first"); break; }
            header = true;
        } else if (!strcmp(line, "name")) {
            if (name || !*v || strlen(v) > MA_PATCH_NAME_MAX) { ok = fail(error, number, "invalid or duplicate name"); break; }
            snprintf(p->name, sizeof p->name, "%s", v);
            name = true;
        } else if (!strcmp(line, "parent")) {
            bool factory = !strncmp(v, "factory:", 8);
            if (parent || (!factory && !wb_path_valid(v))) { ok = fail(error, number, "invalid or duplicate parent"); break; }
            snprintf(p->parent, sizeof p->parent, "%s", v);
            parent = true;
        } else {
            int f = field_index(line);
            if (f < 0) { ok = fail(error, number, "unknown patch field '%s'", line); break; }
            if (wb_patch_overridden(p, (unsigned)f)) { ok = fail(error, number, "duplicate field '%s'", line); break; }
            ma_patch_field_info info;
            char *stop;
            double x = strtod(v, &stop);
            if (*stop || !isfinite(x) || !ma_patch_field_info_at((size_t)f, &info) || x < info.minimum
                || x > info.maximum || (info.integer && x != floor(x))) {
                ok = fail(error, number, "%s outside its domain", line);
                break;
            }
            if (p->override_count == WB_PATCH_FIELDS) { ok = fail(error, number, "too many overrides"); break; }
            wb_patch_override(p, (unsigned)f, x, false);
        }
        line = next;
    }
    free(data);
    if (ok && (!header || !name || !parent)) ok = fail(error, number, "a .wbpatch needs wbpatch=1, name and parent");
    return ok;
}

bool wb_patch_write_overrides(const char *path, const wb_patch_overrides *p, wb_error *error) {
    wb_buf b = { 0 };
    wb_buf_printf(&b, "wbpatch=1\nname=%s\nparent=%s\n", p->name, p->parent);
    for (unsigned i = 0; i < p->override_count; i++) {
        ma_patch_field_info info;
        if (!ma_patch_field_info_at(p->field[i], &info)) continue;
        if (info.integer) wb_buf_printf(&b, "%s=%d\n", info.name, (int)p->value[i]);
        else {
            char text[32];
            wb_format_f32((float)p->value[i], text);
            wb_buf_printf(&b, "%s=%s\n", info.name, text);
        }
    }
    bool ok = !b.failed && wb_write_file_atomic(path, b.data, b.length);
    wb_buf_free(&b);
    return ok || fail(error, 0, "cannot write %s", path);
}

bool wb_patch_overridden(const wb_patch_overrides *p, unsigned field) {
    for (unsigned i = 0; i < p->override_count; i++)
        if (p->field[i] == field) return true;
    return false;
}

void wb_patch_override(wb_patch_overrides *p, unsigned field, double value, bool clear) {
    unsigned i = 0;
    while (i < p->override_count && p->field[i] < field) i++;
    bool present = i < p->override_count && p->field[i] == field;
    if (clear) {
        if (present) {
            memmove(&p->field[i], &p->field[i + 1], (p->override_count - i - 1) * sizeof *p->field);
            memmove(&p->value[i], &p->value[i + 1], (p->override_count - i - 1) * sizeof *p->value);
            p->override_count--;
        }
        return;
    }
    if (!present) {
        if (p->override_count == WB_PATCH_FIELDS) return;
        memmove(&p->field[i + 1], &p->field[i], (p->override_count - i) * sizeof *p->field);
        memmove(&p->value[i + 1], &p->value[i], (p->override_count - i) * sizeof *p->value);
        p->override_count++;
        p->field[i] = field;
    }
    p->value[i] = value;
}

static bool factory(const char *name, ma_patch *out) {
    static const struct { const char *name; const ma_patch *patch; } F[] = {
        { "tepih", &ma_patch_tepih }, { "lead", &ma_patch_lead }, { "dubina", &ma_patch_dubina },
        { "raster", &ma_patch_raster }, { "prizma", &ma_patch_prizma }, { "granica", &ma_patch_granica },
    };
    for (size_t i = 0; i < sizeof F / sizeof *F; i++)
        if (!strcmp(name, F[i].name)) { *out = *F[i].patch; return true; }
    return false;
}

static bool flatten(const char *dir, const char *relative, ma_patch *out, char *name, unsigned depth,
                    wb_error *error) {
    if (depth > WB_PATCH_DEPTH) return fail(error, 0, "patch parent chain deeper than %d (a cycle?)", WB_PATCH_DEPTH);
    if (!strncmp(relative, "factory:", 8)) {
        if (!factory(relative + 8, out)) return fail(error, 0, "unknown factory patch '%s'", relative + 8);
        if (name) snprintf(name, MA_PATCH_NAME_MAX + 1, "%s", relative + 8);
        return true;
    }
    char path[4400];
    snprintf(path, sizeof path, "%s/%s", dir, relative);
    if (!wb_patch_is_chain(relative)) {
        ma_patch_document doc;
        ma_patch_error pe = { 0 };
        if (!ma_patch_load(path, &doc, &pe)) return fail(error, pe.line, "%s: %s", relative, pe.message);
        *out = doc.value;
        if (name) snprintf(name, MA_PATCH_NAME_MAX + 1, "%s", doc.name);
        return true;
    }
    wb_patch_overrides p;
    if (!wb_patch_read_overrides(path, &p, error)) return false;
    if (!flatten(dir, p.parent, out, nullptr, depth + 1, error)) return false;
    for (unsigned i = 0; i < p.override_count; i++)
        if (!ma_patch_field_set(out, p.field[i], p.value[i]))
            return fail(error, 0, "%s: override outside its domain", relative);
    if (name) snprintf(name, MA_PATCH_NAME_MAX + 1, "%s", p.name);
    return true;
}

bool wb_patch_load(const char *dir, const char *relative, ma_patch_document *doc, wb_error *error) {
    *doc = (ma_patch_document){ 0 };
    return flatten(dir, relative, &doc->value, doc->name, 0, error);
}

bool wb_patch_parent(const char *dir, const wb_patch_overrides *p, ma_patch *parent, wb_error *error) {
    return flatten(dir, p->parent, parent, nullptr, 1, error);
}
