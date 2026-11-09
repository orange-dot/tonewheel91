#define _POSIX_C_SOURCE 200809L
#include "wb_format.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "wb_hash.h"

/* ---- text buffer ------------------------------------------------------- */

void wb_buf_free(wb_buf *b) {
    free(b->data);
    *b = (wb_buf){ 0 };
}

void wb_buf_reset(wb_buf *b) {
    b->length = 0;
    b->failed = false;
    if (b->data) b->data[0] = 0;
}

bool wb_buf_append(wb_buf *b, const char *text, size_t size) {
    if (b->failed) return false;
    if (b->length + size + 1 > b->capacity) {
        size_t capacity = b->capacity ? b->capacity : 256;
        while (capacity < b->length + size + 1) {
            if (capacity > SIZE_MAX / 2) { b->failed = true; return false; }
            capacity *= 2;
        }
        char *data = realloc(b->data, capacity);
        if (!data) { b->failed = true; return false; }
        b->data = data;
        b->capacity = capacity;
    }
    memcpy(b->data + b->length, text, size);
    b->length += size;
    b->data[b->length] = 0;
    return true;
}

bool wb_buf_printf(wb_buf *b, const char *format, ...) {
    char small[512];
    va_list args;
    va_start(args, format);
    int n = vsnprintf(small, sizeof small, format, args);
    va_end(args);
    if (n < 0) { b->failed = true; return false; }
    if ((size_t)n < sizeof small) return wb_buf_append(b, small, (size_t)n);
    char *large = malloc((size_t)n + 1);
    if (!large) { b->failed = true; return false; }
    va_start(args, format);
    vsnprintf(large, (size_t)n + 1, format, args);
    va_end(args);
    bool ok = wb_buf_append(b, large, (size_t)n);
    free(large);
    return ok;
}

/* ---- numbers ----------------------------------------------------------- */

/* Shortest %g text that reads back exactly; when that needs an exponent,
 * a plain %f form that also reads back exactly and fits is preferred, so
 * 10, 500 and 0.00001 do not turn into 1e+01, 5e+02 and 1e-05. */
static void shortest(double value, bool single, char out[32]) {
    int max = single ? 9 : 17;
    for (int precision = 1; precision <= max; precision++) {
        snprintf(out, 32, "%.*g", precision, value);
        if (single ? strtof(out, nullptr) == (float)value : strtod(out, nullptr) == value)
            break;
    }
    if (!strchr(out, 'e')) return;
    char plain[64];
    for (int decimals = 0; decimals <= 24; decimals++) {
        int n = snprintf(plain, sizeof plain, "%.*f", decimals, value);
        if (n < 0 || n > 24) return;
        if (single ? strtof(plain, nullptr) == (float)value : strtod(plain, nullptr) == value) {
            memcpy(out, plain, (size_t)n + 1);
            return;
        }
    }
}

void wb_format_f32(float value, char out[32]) { shortest((double)value, true, out); }
void wb_format_f64(double value, char out[32]) { shortest(value, false, out); }

static bool parse_i64(const char *text, long long lo, long long hi, long long *out) {
    if (!text || !*text || *text == ' ') return false;
    errno = 0;
    char *end = nullptr;
    long long value = strtoll(text, &end, 10);
    if (errno || end == text || *end || value < lo || value > hi) return false;
    *out = value;
    return true;
}

static bool parse_f64(const char *text, double lo, double hi, double *out) {
    if (!text || !*text || *text == ' ') return false;
    errno = 0;
    char *end = nullptr;
    double value = strtod(text, &end);
    if (errno == ERANGE || end == text || *end || !isfinite(value)
        || value < lo || value > hi)
        return false;
    *out = value;
    return true;
}

/* ---- field catalog ----------------------------------------------------- */

typedef enum { T_TEXT, T_NAME, T_PATH, T_SHA, T_I32, T_U32, T_F32, T_F64, T_ENUM, T_CUSTOM } ftype;
enum { REQ = 1u, REPEAT = 2u, ALWAYS = 4u };
enum { WHY_MAX = 96 };

typedef bool parse_fn(void *object, const char *value, char why[WHY_MAX]);
/* Writes complete "key=value\n" lines (none for an empty repeat). */
typedef bool write_fn(const void *object, const char *key, wb_buf *out);

typedef struct {
    const char *key;
    ftype type;
    size_t offset, size;
    double lo, hi;
    const char *const *names;
    unsigned name_count;
    parse_fn *parse;
    write_fn *write;
    unsigned flags;
} field;

static const char *const ENGINE_NAMES[] = { "auto", "scalar" };
static const char *const COMPAT_NAMES[] = { "desk", "render_midi" };
static const char *const PART_ENGINE_NAMES[] = { "mamut", "organ", "ep73" };
static const char *const DEDUPE_NAMES[] = { "restrike", "keep-loudest", "off" };
static const char *const SHAPE_NAMES[] = { "linear", "smoothstep", "step" };
static const char *const OP_NAMES[] = { "set", "mul", "add" };
static const char *const VOICE_NAMES[] = { "all", "top", "rest" };
static const char *const SPLIT_NAMES[] = { "track+channel", "track", "channel", "program", "manual" };
static const char *const YES_NO[] = { "no", "yes" };
static const char *const OVERLAY_OPS[] = { "add", "delete" };

#define MEMBER(type, member) offsetof(type, member), sizeof(((type *)0)->member)
#define TEXT(t, k, m, f) { k, T_TEXT, MEMBER(t, m), 0, 0, nullptr, 0, nullptr, nullptr, f }
#define NAME(t, k, m, f) { k, T_NAME, MEMBER(t, m), 0, 0, nullptr, 0, nullptr, nullptr, f }
#define PATH(t, k, m, f) { k, T_PATH, MEMBER(t, m), 0, 0, nullptr, 0, nullptr, nullptr, f }
#define SHA(t, k, m, f) { k, T_SHA, MEMBER(t, m), 0, 0, nullptr, 0, nullptr, nullptr, f }
#define I32(t, k, m, lo, hi, f) { k, T_I32, MEMBER(t, m), lo, hi, nullptr, 0, nullptr, nullptr, f }
#define U32(t, k, m, lo, hi, f) { k, T_U32, MEMBER(t, m), lo, hi, nullptr, 0, nullptr, nullptr, f }
#define F32(t, k, m, lo, hi, f) { k, T_F32, MEMBER(t, m), lo, hi, nullptr, 0, nullptr, nullptr, f }
#define F64(t, k, m, lo, hi, f) { k, T_F64, MEMBER(t, m), lo, hi, nullptr, 0, nullptr, nullptr, f }
#define ENUM(t, k, m, names, f) { k, T_ENUM, MEMBER(t, m), 0, 0, names, sizeof names / sizeof *names, nullptr, nullptr, f }
#define CUSTOM(k, p, w, f) { k, T_CUSTOM, 0, 0, 0, 0, nullptr, 0, p, w, f }

/* --- custom: source tempo ---------------------------------------------- */

static bool parse_tempo(void *o, const char *v, char why[WHY_MAX]) {
    wb_source *s = o;
    if (!strcmp(v, "source")) { s->us_per_quarter = 0; return true; }
    long long n;
    if (!parse_i64(v, 1, 0xffffff, &n)) {
        snprintf(why, WHY_MAX, "tempo is 'source' or microseconds per quarter 1..16777215");
        return false;
    }
    s->us_per_quarter = (uint32_t)n;
    return true;
}
static bool write_tempo(const void *o, const char *k, wb_buf *out) {
    const wb_source *s = o;
    if (!s->us_per_quarter) return wb_buf_printf(out, "%s=source\n", k);
    return wb_buf_printf(out, "%s=%u\n", k, s->us_per_quarter);
}

/* --- custom: form sections ---------------------------------------------- */

static bool name_valid(const char *text, size_t max);

static bool parse_section(void *o, const char *v, char why[WHY_MAX]) {
    wb_form *f = o;
    if (f->section_count == WB_SECTIONS) {
        snprintf(why, WHY_MAX, "more than %d sections", WB_SECTIONS);
        return false;
    }
    char name[64], start[32], end[32], extra[2];
    if (sscanf(v, "%63s %31s %31s %1s", name, start, end, extra) != 3
        || !name_valid(name, WB_ID_MAX)) {
        snprintf(why, WHY_MAX, "section is 'name start_bar end_bar'");
        return false;
    }
    long long a, b;
    if (!parse_i64(start, 0, 100000, &a) || !parse_i64(end, 0, 100000, &b)) {
        snprintf(why, WHY_MAX, "section bars must be 0..100000");
        return false;
    }
    wb_section *x = &f->section[f->section_count++];
    memcpy(x->name, name, strlen(name) + 1); /* name_valid bounds it */
    x->start_bar = (int32_t)a;
    x->end_bar = (int32_t)b;
    return true;
}
static bool write_section(const void *o, const char *k, wb_buf *out) {
    const wb_form *f = o;
    for (unsigned i = 0; i < f->section_count; i++)
        if (!wb_buf_printf(out, "%s=%s %d %d\n", k, f->section[i].name,
                           f->section[i].start_bar, f->section[i].end_bar))
            return false;
    return true;
}

/* --- custom: part seed -------------------------------------------------- */

static bool parse_seed(void *o, const char *v, char why[WHY_MAX]) {
    wb_part *p = o;
    if (!strcmp(v, "name")) { p->seed_from_name = true; p->seed = 0; return true; }
    size_t length = strlen(v);
    if (length > 2 && length <= 18 && v[0] == '0' && v[1] == 'x') {
        uint64_t value = 0;
        bool ok = true;
        for (size_t i = 2; i < length && ok; i++) {
            char c = v[i];
            unsigned digit = c >= '0' && c <= '9' ? (unsigned)(c - '0')
                           : c >= 'a' && c <= 'f' ? (unsigned)(c - 'a' + 10) : 16;
            ok = digit < 16;
            value = value << 4 | digit;
        }
        if (ok) { p->seed_from_name = false; p->seed = value; return true; }
    }
    snprintf(why, WHY_MAX, "seed is 'name' or 0x followed by 1..16 lowercase hex digits");
    return false;
}
static bool write_seed(const void *o, const char *k, wb_buf *out) {
    const wb_part *p = o;
    if (p->seed_from_name) return wb_buf_printf(out, "%s=name\n", k);
    return wb_buf_printf(out, "%s=0x%016llx\n", k, (unsigned long long)p->seed);
}

/* --- custom: rule source selector --------------------------------------- */

static bool parse_from(void *o, const char *v, char why[WHY_MAX]) {
    wb_rule *r = o;
    const char *colon = strchr(v, ':');
    size_t id_length = colon ? (size_t)(colon - v) : 0;
    if (!colon || !id_length || id_length > WB_ID_MAX) goto bad;
    char id[WB_ID_MAX + 1];
    memcpy(id, v, id_length);
    id[id_length] = 0;
    if (!wb_id_valid(id)) goto bad;
    snprintf(r->source, sizeof r->source, "%s", id);
    r->track_name[0] = 0;
    r->track = r->channel = r->program = -1;
    const char *p = colon + 1;
    if (!strcmp(p, "*")) return true;
    if (*p == '"') {
        const char *close = strchr(p + 1, '"');
        size_t n = close ? (size_t)(close - p - 1) : 0;
        if (!close || close[1] || !n || n > WB_TEXT_MAX) goto bad;
        memcpy(r->track_name, p + 1, n);
        r->track_name[n] = 0;
        if (!wb_text_valid(r->track_name, WB_TEXT_MAX)) goto bad;
        return true;
    }
    bool seen[4] = { 0 };
    while (*p) {
        const char *eq = strchr(p, '=');
        if (!eq) goto bad;
        size_t klen = (size_t)(eq - p);
        const char *value = eq + 1;
        if (klen == 4 && !strncmp(p, "name", 4)) {
            if (seen[0] || *value != '"') goto bad;
            const char *close = strchr(value + 1, '"');
            size_t n = close ? (size_t)(close - value - 1) : 0;
            if (!close || !n || n > WB_TEXT_MAX || (close[1] && close[1] != ',')) goto bad;
            memcpy(r->track_name, value + 1, n);
            r->track_name[n] = 0;
            if (!wb_text_valid(r->track_name, WB_TEXT_MAX)) goto bad;
            seen[0] = true;
            p = close[1] ? close + 2 : close + 1;
            if (close[1] && !*p) goto bad;
            continue;
        }
        const char *comma = strchr(value, ',');
        size_t vlen = comma ? (size_t)(comma - value) : strlen(value);
        char number[16];
        if (!vlen || vlen >= sizeof number) goto bad;
        memcpy(number, value, vlen);
        number[vlen] = 0;
        long long n;
        if (klen == 5 && !strncmp(p, "track", 5) && !seen[1] && parse_i64(number, 0, 65535, &n)) {
            r->track = (int32_t)n; seen[1] = true;
        } else if (klen == 7 && !strncmp(p, "channel", 7) && !seen[2] && parse_i64(number, 1, 16, &n)) {
            r->channel = (int32_t)n; seen[2] = true;
        } else if (klen == 7 && !strncmp(p, "program", 7) && !seen[3] && parse_i64(number, 0, 127, &n)) {
            r->program = (int32_t)n; seen[3] = true;
        } else goto bad;
        p = comma ? comma + 1 : value + vlen;
        if (comma && !*p) goto bad;
    }
    if (!seen[0] && !seen[1] && !seen[2] && !seen[3]) goto bad;
    return true;
bad:
    snprintf(why, WHY_MAX, "from is source:\"track name\", source:* or "
             "source:name=\"..\",track=N,channel=1..16,program=0..127");
    return false;
}
static bool write_from(const void *o, const char *k, wb_buf *out) {
    const wb_rule *r = o;
    bool name = r->track_name[0], other = r->track >= 0 || r->channel >= 0 || r->program >= 0;
    if (!wb_buf_printf(out, "%s=%s:", k, r->source)) return false;
    if (!name && !other) return wb_buf_printf(out, "*\n");
    if (name && !other) return wb_buf_printf(out, "\"%s\"\n", r->track_name);
    const char *sep = "";
    if (name) { wb_buf_printf(out, "name=\"%s\"", r->track_name); sep = ","; }
    if (r->track >= 0) { wb_buf_printf(out, "%strack=%d", sep, r->track); sep = ","; }
    if (r->channel >= 0) { wb_buf_printf(out, "%schannel=%d", sep, r->channel); sep = ","; }
    if (r->program >= 0) wb_buf_printf(out, "%sprogram=%d", sep, r->program);
    return wb_buf_printf(out, "\n");
}

/* --- custom: note sets -------------------------------------------------- */

static bool parse_notes(void *o, const char *v, char why[WHY_MAX]) {
    wb_rule *r = o;
    wb_noteset set = { 0 };
    const char *p = v;
    while (*p) {
        char *end;
        errno = 0;
        if (*p < '0' || *p > '9') goto bad;
        long a = strtol(p, &end, 10), b = a;
        if (errno || a > 127) goto bad;
        if (*end == '-') {
            const char *q = end + 1;
            if (*q < '0' || *q > '9') goto bad;
            b = strtol(q, &end, 10);
            if (errno || b > 127 || b < a) goto bad;
        }
        for (long n = a; n <= b; n++) wb_noteset_add(&set, (unsigned)n);
        if (*end == ',') {
            p = end + 1;
            if (!*p) goto bad;
        } else if (*end) goto bad;
        else p = end;
    }
    if (!set.bits[0] && !set.bits[1]) goto bad;
    r->notes = set;
    r->has_notes = true;
    return true;
bad:
    snprintf(why, WHY_MAX, "notes is a list of 0..127 notes and ranges, e.g. 35-36,41");
    return false;
}
static bool write_notes(const void *o, const char *k, wb_buf *out) {
    const wb_rule *r = o;
    if (!r->has_notes) return true;
    if (!wb_buf_printf(out, "%s=", k)) return false;
    const char *sep = "";
    for (unsigned n = 0; n < 128; n++) {
        if (!wb_noteset_has(&r->notes, n)) continue;
        unsigned m = n;
        while (m + 1 < 128 && wb_noteset_has(&r->notes, m + 1)) m++;
        if (m == n) wb_buf_printf(out, "%s%u", sep, n);
        else wb_buf_printf(out, "%s%u-%u", sep, n, m);
        sep = ",";
        n = m;
    }
    return wb_buf_printf(out, "\n");
}

/* --- custom: velocity scale --------------------------------------------- */

static bool parse_velocity_scale(void *o, const char *v, char why[WHY_MAX]) {
    wb_rule *r = o;
    char number[24];
    const char *slash = strchr(v, '/');
    long long num, den = 1;
    size_t length = slash ? (size_t)(slash - v) : strlen(v);
    if (length && length < sizeof number) {
        memcpy(number, v, length);
        number[length] = 0;
        if (parse_i64(number, 0, 1000, &num)
            && (!slash || parse_i64(slash + 1, 1, 1000, &den))) {
            r->velocity_num = (uint32_t)num;
            r->velocity_den = (uint32_t)den;
            return true;
        }
    }
    snprintf(why, WHY_MAX, "velocity_scale is an exact ratio N or N/D (0..1000 / 1..1000)");
    return false;
}
static bool write_velocity_scale(const void *o, const char *k, wb_buf *out) {
    const wb_rule *r = o;
    if (r->velocity_den == 1) return wb_buf_printf(out, "%s=%u\n", k, r->velocity_num);
    return wb_buf_printf(out, "%s=%u/%u\n", k, r->velocity_num, r->velocity_den);
}

/* --- custom: groove ----------------------------------------------------- */

static bool parse_wobble(void *o, const char *v, char why[WHY_MAX]) {
    wb_groove *g = o;
    if (!strcmp(v, "off")) { g->wobble_a = g->wobble_b = g->wobble_m = 0; return true; }
    char a[16], b[16], m[16], extra[2];
    long long x, y, z;
    if (sscanf(v, "%15[0-9],%15[0-9],%15[0-9]%1s", a, b, m, extra) == 3
        && parse_i64(a, 0, 1000, &x) && parse_i64(b, 0, 1000, &y)
        && parse_i64(m, 1, 127, &z)) {
        g->wobble_a = (uint32_t)x; g->wobble_b = (uint32_t)y; g->wobble_m = (uint32_t)z;
        return true;
    }
    snprintf(why, WHY_MAX, "wobble is 'off' or a,b,m with m 1..127");
    return false;
}
static bool write_wobble(const void *o, const char *k, wb_buf *out) {
    const wb_groove *g = o;
    if (!g->wobble_m) return wb_buf_printf(out, "%s=off\n", k);
    return wb_buf_printf(out, "%s=%u,%u,%u\n", k, g->wobble_a, g->wobble_b, g->wobble_m);
}

/* Split v into blank-separated tokens (single blanks only). */
static unsigned tokens(char *v, char **token, unsigned max) {
    unsigned n = 0;
    if (!*v) return 0;
    for (char *p = v;;) {
        if (n == max) return max + 1;
        token[n++] = p;
        char *blank = strchr(p, ' ');
        if (!blank) return n;
        *blank = 0;
        p = blank + 1;
        if (!*p || *p == ' ') return max + 1;
    }
}

static bool parse_pattern(void *o, const char *v, char why[WHY_MAX]) {
    wb_groove *g = o;
    char copy[WB_LINE_MAX + 1];
    snprintf(copy, sizeof copy, "%s", v);
    char *token[2 * WB_PATTERN_STEPS + 2];
    unsigned n = tokens(copy, token, sizeof token / sizeof *token);
    if (g->pattern_count == WB_PATTERNS) {
        snprintf(why, WHY_MAX, "more than %d patterns", WB_PATTERNS);
        return false;
    }
    wb_pattern p = { 0 };
    bool velocity = false;
    int accent_step[WB_PATTERN_STEPS];
    int accent_value[WB_PATTERN_STEPS];
    unsigned accents = 0;
    if (n == 0 || n > sizeof token / sizeof *token || !name_valid(token[0], WB_ID_MAX)) goto bad;
    snprintf(p.name, sizeof p.name, "%s", token[0]);
    for (unsigned i = 1; i < n; i++) {
        long long x;
        if (!strncmp(token[i], "vel=", 4)) {
            if (velocity || !parse_i64(token[i] + 4, 1, 127, &x)) goto bad;
            p.velocity = (uint32_t)x;
            velocity = true;
        } else if (!strncmp(token[i], "accent", 6)) {
            char *eq = strchr(token[i], '=');
            long long step, value;
            if (!eq || accents == WB_PATTERN_STEPS) goto bad;
            *eq = 0;
            if (!parse_i64(token[i] + 6, 0, WB_PATTERN_STEPS - 1, &step)
                || (eq[1] != '+' && eq[1] != '-')
                || !parse_i64(eq + 1, -127, 127, &value))
                goto bad;
            accent_step[accents] = (int)step;
            accent_value[accents++] = (int)value;
        } else {
            if (velocity || accents || p.step_count == WB_PATTERN_STEPS
                || !parse_i64(token[i], 0, WB_PATTERN_STEPS - 1, &x))
                goto bad;
            if (p.step_count && x <= p.step[p.step_count - 1]) goto bad;
            p.step[p.step_count++] = (uint8_t)x;
        }
    }
    if (!velocity) goto bad;
    for (unsigned a = 0; a < accents; a++) {
        unsigned i = 0;
        while (i < p.step_count && p.step[i] != accent_step[a]) i++;
        if (i == p.step_count || p.accent[i] || !accent_value[a]) goto bad;
        p.accent[i] = (int8_t)accent_value[a];
    }
    g->pattern[g->pattern_count++] = p;
    return true;
bad:
    snprintf(why, WHY_MAX, "pattern is 'name step.. vel=1..127 [accentS=+N..]' "
             "with increasing steps");
    return false;
}
static bool write_pattern(const void *o, const char *k, wb_buf *out) {
    const wb_groove *g = o;
    for (unsigned i = 0; i < g->pattern_count; i++) {
        const wb_pattern *p = &g->pattern[i];
        wb_buf_printf(out, "%s=%s", k, p->name);
        for (unsigned s = 0; s < p->step_count; s++) wb_buf_printf(out, " %u", p->step[s]);
        wb_buf_printf(out, " vel=%u", p->velocity);
        for (unsigned s = 0; s < p->step_count; s++)
            if (p->accent[s]) wb_buf_printf(out, " accent%u=%+d", p->step[s], p->accent[s]);
        if (!wb_buf_printf(out, "\n")) return false;
    }
    return true;
}

static bool parse_use(void *o, const char *v, char why[WHY_MAX]) {
    wb_groove *g = o;
    char copy[WB_LINE_MAX + 1];
    snprintf(copy, sizeof copy, "%s", v);
    char *token[WB_USES + 1];
    unsigned n = tokens(copy, token, WB_USES);
    if (!n || n > WB_USES) goto bad;
    for (unsigned i = 0; i < n; i++) {
        char *dots = strstr(token[i], "..");
        char *colon = dots ? strchr(dots, ':') : nullptr;
        if (!dots || !colon || g->use_count == WB_USES) goto bad;
        *dots = 0;
        *colon = 0;
        long long a, b;
        if (!parse_i64(token[i], 0, 100000, &a) || !parse_i64(dots + 2, 0, 100000, &b))
            goto bad;
        unsigned pattern = 0;
        while (pattern < g->pattern_count && strcmp(g->pattern[pattern].name, colon + 1))
            pattern++;
        if (pattern == g->pattern_count) {
            snprintf(why, WHY_MAX, "use names pattern '%.40s' before it is defined", colon + 1);
            return false;
        }
        g->use[g->use_count++] = (wb_use){ (int32_t)a, (int32_t)b, (uint8_t)pattern };
    }
    return true;
bad:
    snprintf(why, WHY_MAX, "use is 'from..to:pattern ...' with bars 0..100000");
    return false;
}
static bool write_use(const void *o, const char *k, wb_buf *out) {
    const wb_groove *g = o;
    if (!g->use_count) return true;
    wb_buf_printf(out, "%s=", k);
    for (unsigned i = 0; i < g->use_count; i++)
        wb_buf_printf(out, "%s%d..%d:%s", i ? " " : "", g->use[i].from_bar,
                      g->use[i].to_bar, g->pattern[g->use[i].pattern].name);
    return wb_buf_printf(out, "\n");
}

static bool parse_hit(void *o, const char *v, char why[WHY_MAX]) {
    wb_groove *g = o;
    char bar[16], vel[16], extra[2];
    long long b, x;
    if (g->hit_count < WB_HITS
        && sscanf(v, "%15[0-9] vel=%15[0-9]%1s", bar, vel, extra) == 2
        && parse_i64(bar, 0, 100000, &b) && parse_i64(vel, 1, 127, &x)) {
        g->hit[g->hit_count++] = (wb_hit){ (int32_t)b, (uint32_t)x };
        return true;
    }
    snprintf(why, WHY_MAX, "hit is 'bar vel=1..127' (at most %d)", WB_HITS);
    return false;
}
static bool write_hit(const void *o, const char *k, wb_buf *out) {
    const wb_groove *g = o;
    for (unsigned i = 0; i < g->hit_count; i++)
        if (!wb_buf_printf(out, "%s=%d vel=%u\n", k, g->hit[i].bar, g->hit[i].velocity))
            return false;
    return true;
}

/* --- custom: points (curve and lane) ------------------------------------ */

static bool parse_point_into(unsigned *count, wb_point *point, const char *v,
                             char why[WHY_MAX]) {
    char bar[40], value[40], extra[2];
    double b, x;
    if (*count < WB_POINTS && sscanf(v, "%39s %39s %1s", bar, value, extra) == 2
        && parse_f64(bar, -100000, 100000, &b) && parse_f64(value, -1e9, 1e9, &x)) {
        point[(*count)++] = (wb_point){ b, (float)x };
        return true;
    }
    snprintf(why, WHY_MAX, "point is 'bar value' (at most %d)", WB_POINTS);
    return false;
}
static bool write_points(unsigned count, const wb_point *point, const char *k, wb_buf *out) {
    for (unsigned i = 0; i < count; i++) {
        char b[32], x[32];
        wb_format_f64(point[i].bar, b);
        wb_format_f32(point[i].value, x);
        if (!wb_buf_printf(out, "%s=%s %s\n", k, b, x)) return false;
    }
    return true;
}
static bool parse_curve_point(void *o, const char *v, char why[WHY_MAX]) {
    wb_curve *c = o;
    return parse_point_into(&c->point_count, c->point, v, why);
}
static bool write_curve_point(const void *o, const char *k, wb_buf *out) {
    const wb_curve *c = o;
    return write_points(c->point_count, c->point, k, out);
}
static bool parse_lane_point(void *o, const char *v, char why[WHY_MAX]) {
    wb_lane *l = o;
    return parse_point_into(&l->point_count, l->point, v, why);
}
static bool write_lane_point(const void *o, const char *k, wb_buf *out) {
    const wb_lane *l = o;
    return write_points(l->point_count, l->point, k, out);
}

static bool parse_clamp(void *o, const char *v, char why[WHY_MAX]) {
    wb_lane *l = o;
    if (!strcmp(v, "off")) { l->clamp = false; l->minimum = 0; l->maximum = 1; return true; }
    char a[40], b[40], extra[2];
    double x, y;
    if (sscanf(v, "%39s %39s %1s", a, b, extra) == 2
        && parse_f64(a, -1e9, 1e9, &x) && parse_f64(b, -1e9, 1e9, &y)) {
        l->clamp = true; l->minimum = (float)x; l->maximum = (float)y;
        return true;
    }
    snprintf(why, WHY_MAX, "clamp is 'off' or 'min max'");
    return false;
}
static bool write_clamp(const void *o, const char *k, wb_buf *out) {
    const wb_lane *l = o;
    if (!l->clamp) return wb_buf_printf(out, "%s=off\n", k);
    char a[32], b[32];
    wb_format_f32(l->minimum, a);
    wb_format_f32(l->maximum, b);
    return wb_buf_printf(out, "%s=%s %s\n", k, a, b);
}

/* --- custom: controller sets -------------------------------------------- */

static bool parse_ctlset(wb_ctlset *set, const char *v, char why[WHY_MAX]) {
    wb_ctlset out = { 0 };
    if (strcmp(v, "none")) {
        const char *p = v;
        while (*p) {
            const char *comma = strchr(p, ',');
            size_t n = comma ? (size_t)(comma - p) : strlen(p);
            char item[16];
            if (!n || n >= sizeof item) goto bad;
            memcpy(item, p, n);
            item[n] = 0;
            long long cc;
            if (!strcmp(item, "bend")) wb_ctlset_add(&out, WB_CTL_BEND);
            else if (!strcmp(item, "pressure")) wb_ctlset_add(&out, WB_CTL_PRESSURE);
            else if (!strcmp(item, "poly")) wb_ctlset_add(&out, WB_CTL_POLY);
            else if (!strncmp(item, "cc", 2) && parse_i64(item + 2, 0, 127, &cc)) wb_ctlset_add(&out, (unsigned)cc);
            else goto bad;
            p = comma ? comma + 1 : p + n;
            if (comma && !*p) goto bad;
        }
        if (wb_ctlset_empty(&out)) goto bad;
    }
    *set = out;
    return true;
bad:
    snprintf(why, WHY_MAX, "controllers is 'none' or a list of ccN, bend, pressure, poly");
    return false;
}

static bool write_ctlset(const wb_ctlset *set, const char *k, wb_buf *out) {
    if (wb_ctlset_empty(set)) return wb_buf_printf(out, "%s=none\n", k);
    wb_buf_printf(out, "%s=", k);
    const char *sep = "";
    for (unsigned c = 0; c < WB_CTL_COUNT; c++) {
        if (!wb_ctlset_has(set, c)) continue;
        if (c < 128) wb_buf_printf(out, "%scc%u", sep, c);
        else wb_buf_printf(out, "%s%s", sep, c == WB_CTL_BEND ? "bend" : c == WB_CTL_PRESSURE ? "pressure" : "poly");
        sep = ",";
    }
    return wb_buf_printf(out, "\n");
}

static bool parse_rule_ctl(void *o, const char *v, char why[WHY_MAX]) {
    return parse_ctlset(&((wb_rule *)o)->controllers, v, why);
}
static bool write_rule_ctl(const void *o, const char *k, wb_buf *out) {
    return write_ctlset(&((const wb_rule *)o)->controllers, k, out);
}
static bool parse_import_ctl(void *o, const char *v, char why[WHY_MAX]) {
    return parse_ctlset(&((wb_import *)o)->controllers, v, why);
}
static bool write_import_ctl(const void *o, const char *k, wb_buf *out) {
    return write_ctlset(&((const wb_import *)o)->controllers, k, out);
}

static bool parse_pitch_split(void *o, const char *v, char why[WHY_MAX]) {
    wb_import *im = o;
    const char *colon = strchr(v, ':');
    char track[16];
    long long t, n = -1;
    size_t length = colon ? (size_t)(colon - v) : 0;
    if (im->pitch_split_count < WB_PITCH_SPLITS && colon && length && length < sizeof track) {
        memcpy(track, v, length);
        track[length] = 0;
        if (parse_i64(track, 0, WB_ID_MAX * 1000, &t)
            && (!strcmp(colon + 1, "top") || parse_i64(colon + 1, 1, 127, &n))) {
            im->pitch_split[im->pitch_split_count++] = (wb_pitch_split){ (int32_t)t, (int32_t)n };
            return true;
        }
    }
    snprintf(why, WHY_MAX, "pitch_split is TRACK:NOTE (1..127) or TRACK:top");
    return false;
}
static bool write_pitch_split(const void *o, const char *k, wb_buf *out) {
    const wb_import *im = o;
    for (unsigned i = 0; i < im->pitch_split_count; i++) {
        const wb_pitch_split *x = &im->pitch_split[i];
        if (x->note < 0) wb_buf_printf(out, "%s=%d:top\n", k, x->track);
        else wb_buf_printf(out, "%s=%d:%d\n", k, x->track, x->note);
    }
    return !out->failed;
}

/* --- per-kind tables ---------------------------------------------------- */

static const field SESSION_FIELDS[] = {
    TEXT(wb_session_head, "name", name, REQ),
    TEXT(wb_session_head, "title", title, 0),
    U32(wb_session_head, "rate", rate, 44100, 96000, ALWAYS),
    U32(wb_session_head, "control_frames", control_frames, 1, 48000, ALWAYS),
    ENUM(wb_session_head, "engine", engine, ENGINE_NAMES, ALWAYS),
    ENUM(wb_session_head, "compat", compat, COMPAT_NAMES, ALWAYS),
};

static const field SOURCE_FIELDS[] = {
    NAME(wb_source, "id", id, REQ),
    PATH(wb_source, "path", path, REQ),
    SHA(wb_source, "sha256", sha256, REQ),
    I32(wb_source, "offset_bars", offset_bars, -10000, 10000, ALWAYS),
    CUSTOM("tempo", parse_tempo, write_tempo, ALWAYS),
    F64(wb_source, "tempo_scale", tempo_scale, 0.125, 8.0, ALWAYS),
};

static const field FORM_FIELDS[] = {
    I32(wb_form, "start_bar", start_bar, 0, 100000, REQ),
    I32(wb_form, "end_bar", end_bar, 1, 100000, REQ),
    F64(wb_form, "tail_seconds", tail_seconds, 0, 120, ALWAYS),
    U32(wb_form, "length_seconds", length_seconds, 0, 3600, ALWAYS),
    F64(wb_form, "fade_in_ms", fade_in_ms, 0, 60000, ALWAYS),
    F64(wb_form, "fade_out_ms", fade_out_ms, 0, 60000, ALWAYS),
    CUSTOM("section", parse_section, write_section, REPEAT),
};

static const field STEM_FIELDS[] = {
    NAME(wb_stem, "id", id, REQ),
};

static const field PART_FIELDS[] = {
    NAME(wb_part, "id", id, REQ),
    ENUM(wb_part, "engine", engine, PART_ENGINE_NAMES, ALWAYS),
    PATH(wb_part, "patch", patch, REQ),
    U32(wb_part, "limit", limit, 1, 64, ALWAYS),
    U32(wb_part, "unison", unison, 1, 8, ALWAYS),
    F32(wb_part, "detune_cents", detune_cents, 0, 100, ALWAYS),
    CUSTOM("seed", parse_seed, write_seed, ALWAYS),
    F32(wb_part, "character", character, 0, 1, ALWAYS),
    F32(wb_part, "chorus", chorus, 0, 1, ALWAYS),
    F32(wb_part, "chorus_depth", chorus_depth, 0, 1, ALWAYS),
    F32(wb_part, "chorus_rate_hz", chorus_rate_hz, 0, 20, ALWAYS),
    F32(wb_part, "reverb", reverb, 0, 1, ALWAYS),
    F32(wb_part, "reverb_size", reverb_size, 0, 1, ALWAYS),
    F32(wb_part, "reverb_damping", reverb_damping, 0, 1, ALWAYS),
    F32(wb_part, "gain", gain, 0, 16, ALWAYS),
    F32(wb_part, "pressure", pressure, 0, 1, ALWAYS),
    NAME(wb_part, "stem", stem, REQ),
    ENUM(wb_part, "dedupe", dedupe, DEDUPE_NAMES, ALWAYS),
    F32(wb_part, "pan", pan, -1, 1, ALWAYS),
};

static const field RULE_FIELDS[] = {
    NAME(wb_rule, "part", part, REQ),
    CUSTOM("from", parse_from, write_from, REQ),
    CUSTOM("notes", parse_notes, write_notes, 0),
    I32(wb_rule, "transpose", transpose, -127, 127, 0),
    I32(wb_rule, "map_to", map_to, 0, 127, 0),
    I32(wb_rule, "fold_below", fold_below, 0, 127, 0),
    I32(wb_rule, "fold_above", fold_above, 0, 127, 0),
    CUSTOM("velocity_scale", parse_velocity_scale, write_velocity_scale, 0),
    I32(wb_rule, "velocity_offset", velocity_offset, -127, 127, 0),
    U32(wb_rule, "velocity_min", velocity_min, 1, 127, 0),
    U32(wb_rule, "velocity_max", velocity_max, 1, 127, 0),
    F64(wb_rule, "ring_max_bars", ring_max_bars, 0, 1000, 0),
    U32(wb_rule, "min_length_ticks", min_length_ticks, 0, 1000000, 0),
    I32(wb_rule, "bar_in", bar_in, 0, 100000, 0),
    I32(wb_rule, "bar_out", bar_out, 0, 100000, 0),
    ENUM(wb_rule, "voice", voice, VOICE_NAMES, 0),
    CUSTOM("controllers", parse_rule_ctl, write_rule_ctl, 0),
};

static const field GROOVE_FIELDS[] = {
    NAME(wb_groove, "part", part, REQ),
    U32(wb_groove, "note", note, 0, 127, ALWAYS),
    U32(wb_groove, "length_ticks", length_ticks, 1, 100000, ALWAYS),
    U32(wb_groove, "steps_per_bar", steps_per_bar, 1, WB_PATTERN_STEPS, ALWAYS),
    CUSTOM("wobble", parse_wobble, write_wobble, ALWAYS),
    CUSTOM("pattern", parse_pattern, write_pattern, REPEAT),
    CUSTOM("use", parse_use, write_use, REPEAT),
    CUSTOM("hit", parse_hit, write_hit, REPEAT),
};

static const field CURVE_FIELDS[] = {
    NAME(wb_curve, "id", id, REQ),
    ENUM(wb_curve, "shape", shape, SHAPE_NAMES, ALWAYS),
    CUSTOM("point", parse_curve_point, write_curve_point, REPEAT),
};

static const field LANE_FIELDS[] = {
    NAME(wb_lane, "part", part, REQ),
    NAME(wb_lane, "param", param, REQ),
    ENUM(wb_lane, "op", op, OP_NAMES, ALWAYS),
    ENUM(wb_lane, "shape", shape, SHAPE_NAMES, ALWAYS),
    NAME(wb_lane, "curve", curve, 0),
    F32(wb_lane, "scale", scale, -1e9, 1e9, 0),
    F32(wb_lane, "offset", offset, -1e9, 1e9, 0),
    CUSTOM("clamp", parse_clamp, write_clamp, 0),
    CUSTOM("point", parse_lane_point, write_lane_point, REPEAT),
};

static const field IMPORT_FIELDS[] = {
    NAME(wb_import, "source", source, REQ),
    ENUM(wb_import, "split", split, SPLIT_NAMES, ALWAYS),
    I32(wb_import, "drums", drums, 0, 16, ALWAYS),
    ENUM(wb_import, "program_split", program_split, YES_NO, ALWAYS),
    CUSTOM("pitch_split", parse_pitch_split, write_pitch_split, REPEAT),
    CUSTOM("controllers", parse_import_ctl, write_import_ctl, ALWAYS),
};

static const field OVERLAY_FIELDS[] = {
    NAME(wb_overlay, "part", part, REQ),
    ENUM(wb_overlay, "op", op, OVERLAY_OPS, ALWAYS),
    F64(wb_overlay, "bar", bar, 0, 100000, REQ),
    F64(wb_overlay, "length_bars", length_bars, 0.001, 1000, ALWAYS),
    U32(wb_overlay, "note", note, 0, 127, REQ),
    U32(wb_overlay, "velocity", velocity, 1, 127, ALWAYS),
};

static const field MARKER_FIELDS[] = {
    F64(wb_marker, "bar", bar, 0, 100000, REQ),
    NAME(wb_marker, "name", name, REQ),
    TEXT(wb_marker, "text", text, 0),
};

typedef struct {
    const field *fields;
    unsigned count;
    size_t size;
} kind_info;

#define KIND(table, type) { table, sizeof table / sizeof *table, sizeof(type) }
static const kind_info KINDS[WB_KIND_COUNT] = {
    [WB_KIND_SESSION] = KIND(SESSION_FIELDS, wb_session_head),
    [WB_KIND_SOURCE] = KIND(SOURCE_FIELDS, wb_source),
    [WB_KIND_FORM] = KIND(FORM_FIELDS, wb_form),
    [WB_KIND_STEM] = KIND(STEM_FIELDS, wb_stem),
    [WB_KIND_PART] = KIND(PART_FIELDS, wb_part),
    [WB_KIND_RULE] = KIND(RULE_FIELDS, wb_rule),
    [WB_KIND_GROOVE] = KIND(GROOVE_FIELDS, wb_groove),
    [WB_KIND_CURVE] = KIND(CURVE_FIELDS, wb_curve),
    [WB_KIND_LANE] = KIND(LANE_FIELDS, wb_lane),
    [WB_KIND_MARKER] = KIND(MARKER_FIELDS, wb_marker),
    [WB_KIND_IMPORT] = KIND(IMPORT_FIELDS, wb_import),
    [WB_KIND_OVERLAY] = KIND(OVERLAY_FIELDS, wb_overlay),
};

static void object_default(wb_kind kind, void *o) {
    switch (kind) {
    case WB_KIND_SESSION: {
        wb_session_head *h = o;
        *h = (wb_session_head){ .rate = 48000, .control_frames = 480 };
        break;
    }
    case WB_KIND_SOURCE: wb_source_default(o); break;
    case WB_KIND_FORM: wb_form_default(o); break;
    case WB_KIND_STEM: memset(o, 0, sizeof(wb_stem)); break;
    case WB_KIND_PART: wb_part_default(o); break;
    case WB_KIND_RULE: wb_rule_default(o); break;
    case WB_KIND_GROOVE: wb_groove_default(o); break;
    case WB_KIND_CURVE: wb_curve_default(o); break;
    case WB_KIND_LANE: wb_lane_default(o); break;
    case WB_KIND_MARKER: wb_marker_default(o); break;
    case WB_KIND_IMPORT: wb_import_default(o); break;
    case WB_KIND_OVERLAY: wb_overlay_default(o); break;
    default: break;
    }
}

static void *object_at(wb_session *s, wb_kind kind, unsigned i) {
    switch (kind) {
    case WB_KIND_SESSION: return &s->head;
    case WB_KIND_SOURCE: return &s->source[i];
    case WB_KIND_FORM: return &s->form;
    case WB_KIND_STEM: return &s->stem[i];
    case WB_KIND_PART: return &s->part[i];
    case WB_KIND_RULE: return &s->rule[i];
    case WB_KIND_GROOVE: return &s->groove[i];
    case WB_KIND_CURVE: return &s->curve[i];
    case WB_KIND_LANE: return &s->lane[i];
    case WB_KIND_MARKER: return &s->marker[i];
    case WB_KIND_IMPORT: return &s->import[i];
    case WB_KIND_OVERLAY: return &s->overlay[i];
    default: return nullptr;
    }
}

static unsigned *count_of(wb_session *s, wb_kind kind) {
    switch (kind) {
    case WB_KIND_SOURCE: return &s->source_count;
    case WB_KIND_STEM: return &s->stem_count;
    case WB_KIND_PART: return &s->part_count;
    case WB_KIND_RULE: return &s->rule_count;
    case WB_KIND_GROOVE: return &s->groove_count;
    case WB_KIND_CURVE: return &s->curve_count;
    case WB_KIND_LANE: return &s->lane_count;
    case WB_KIND_MARKER: return &s->marker_count;
    case WB_KIND_IMPORT: return &s->import_count;
    case WB_KIND_OVERLAY: return &s->overlay_count;
    default: return nullptr;
    }
}

static const field *find_field(wb_kind kind, const char *key, unsigned *index) {
    for (unsigned i = 0; i < KINDS[kind].count; i++)
        if (!strcmp(KINDS[kind].fields[i].key, key)) {
            if (index) *index = i;
            return &KINDS[kind].fields[i];
        }
    return nullptr;
}

static bool name_valid(const char *text, size_t max) {
    size_t length = text ? strnlen(text, max + 1) : 0;
    if (!length || length > max) return false;
    for (size_t i = 0; i < length; i++) {
        char c = text[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
               || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        if (!ok) return false;
    }
    return true;
}

static bool parse_field(const field *f, void *o, const char *v, char why[WHY_MAX]) {
    unsigned char *at = (unsigned char *)o + f->offset;
    switch (f->type) {
    case T_TEXT:
        if (!*v || !wb_text_valid(v, f->size - 1)) {
            snprintf(why, WHY_MAX, "text longer than %zu bytes, control bytes or edge blanks", f->size - 1);
            return false;
        }
        snprintf((char *)at, f->size, "%s", v);
        return true;
    case T_NAME:
        if (!name_valid(v, f->size - 1)) {
            snprintf(why, WHY_MAX, "a name is 1..%zu of A-Z a-z 0-9 _ - .", f->size - 1);
            return false;
        }
        snprintf((char *)at, f->size, "%s", v);
        return true;
    case T_PATH:
        if (!wb_path_valid(v)) {
            snprintf(why, WHY_MAX, "path must be relative, inside the session, without blanks or '..'");
            return false;
        }
        snprintf((char *)at, f->size, "%s", v);
        return true;
    case T_SHA:
        if (!wb_sha256_hex_valid(v)) {
            snprintf(why, WHY_MAX, "sha256 is 64 lowercase hex digits");
            return false;
        }
        memcpy(at, v, 65);
        return true;
    case T_I32:
    case T_U32: {
        long long n;
        if (!parse_i64(v, (long long)f->lo, (long long)f->hi, &n)) {
            snprintf(why, WHY_MAX, "integer outside %.0f..%.0f", f->lo, f->hi);
            return false;
        }
        if (f->type == T_I32) { int32_t x = (int32_t)n; memcpy(at, &x, sizeof x); }
        else { uint32_t x = (uint32_t)n; memcpy(at, &x, sizeof x); }
        return true;
    }
    case T_F32:
    case T_F64: {
        double x;
        if (!parse_f64(v, f->lo, f->hi, &x)) {
            snprintf(why, WHY_MAX, "finite number outside %g..%g", f->lo, f->hi);
            return false;
        }
        if (f->type == T_F32) {
            float y = (float)x;
            if (!(y >= (float)f->lo && y <= (float)f->hi)) {
                snprintf(why, WHY_MAX, "number outside %g..%g as f32", f->lo, f->hi);
                return false;
            }
            memcpy(at, &y, sizeof y);
        } else memcpy(at, &x, sizeof x);
        return true;
    }
    case T_ENUM:
        for (unsigned i = 0; i < f->name_count; i++)
            if (!strcmp(v, f->names[i])) { *at = (uint8_t)i; return true; }
        {
            int n = snprintf(why, WHY_MAX, "one of:");
            for (unsigned i = 0; i < f->name_count && n > 0 && n < WHY_MAX; i++)
                n += snprintf(why + n, WHY_MAX - (size_t)n, " %s", f->names[i]);
        }
        return false;
    case T_CUSTOM:
        return f->parse(o, v, why);
    }
    return false;
}

static bool write_field(const field *f, const void *o, wb_buf *out) {
    const unsigned char *at = (const unsigned char *)o + f->offset;
    char number[32];
    switch (f->type) {
    case T_TEXT:
    case T_NAME:
        /* Unset optional names and texts are left out; parse never stores
         * an empty value, so an empty one always means unset. */
        if (!*(const char *)at) return (f->flags & REQ) == 0;
        [[fallthrough]];
    case T_PATH:
    case T_SHA:
        return wb_buf_printf(out, "%s=%s\n", f->key, (const char *)at);
    case T_I32: { int32_t x; memcpy(&x, at, sizeof x); return wb_buf_printf(out, "%s=%d\n", f->key, x); }
    case T_U32: { uint32_t x; memcpy(&x, at, sizeof x); return wb_buf_printf(out, "%s=%u\n", f->key, x); }
    case T_F32: { float x; memcpy(&x, at, sizeof x); wb_format_f32(x, number); return wb_buf_printf(out, "%s=%s\n", f->key, number); }
    case T_F64: { double x; memcpy(&x, at, sizeof x); wb_format_f64(x, number); return wb_buf_printf(out, "%s=%s\n", f->key, number); }
    case T_ENUM: return *at < f->name_count && wb_buf_printf(out, "%s=%s\n", f->key, f->names[*at]);
    case T_CUSTOM: return f->write(o, f->key, out);
    }
    return false;
}

static bool write_object(wb_kind kind, const void *o, wb_buf *out) {
    const kind_info *info = &KINDS[kind];
    void *fallback = malloc(info->size);
    if (!fallback) { out->failed = true; return false; }
    object_default(kind, fallback);
    wb_buf mine = { 0 }, theirs = { 0 };
    bool ok = wb_buf_printf(out, "[%s]\n", wb_kind_name(kind));
    for (unsigned i = 0; ok && i < info->count; i++) {
        const field *f = &info->fields[i];
        if (f->flags & (REQ | ALWAYS | REPEAT)) {
            ok = write_field(f, o, out);
            continue;
        }
        wb_buf_reset(&mine);
        wb_buf_reset(&theirs);
        ok = write_field(f, o, &mine) && write_field(f, fallback, &theirs);
        bool same = mine.length == theirs.length
                 && (!mine.length || !memcmp(mine.data, theirs.data, mine.length));
        if (ok && !same && mine.length) ok = wb_buf_append(out, mine.data, mine.length);
    }
    wb_buf_free(&mine);
    wb_buf_free(&theirs);
    free(fallback);
    return ok && !out->failed;
}

/* ---- reader ------------------------------------------------------------ */

typedef struct {
    wb_session *session;     /* whole-file mode */
    void *block;             /* block mode: the one object */
    wb_kind block_kind;
    bool block_seen;
    wb_kind kind;
    void *object;
    uint64_t seen;
    unsigned header_line;
    wb_error *error;
} reader;

[[gnu::format(printf, 3, 4)]]
static bool fail_at(wb_error *error, unsigned line, const char *format, ...) {
    if (error) {
        error->line = line;
        va_list args;
        va_start(args, format);
        vsnprintf(error->message, sizeof error->message, format, args);
        va_end(args);
    }
    return false;
}

static bool finish(reader *r) {
    if (!r->object) return true;
    const kind_info *info = &KINDS[r->kind];
    for (unsigned i = 0; i < info->count; i++)
        if ((info->fields[i].flags & REQ) && !(r->seen >> i & 1u))
            return fail_at(r->error, r->header_line, "[%s] is missing required key '%s'",
                           wb_kind_name(r->kind), info->fields[i].key);
    r->object = nullptr;
    return true;
}

static bool begin(reader *r, const char *name, unsigned line) {
    if (!finish(r)) return false;
    wb_kind kind;
    if (!wb_kind_from_name(name, &kind))
        return fail_at(r->error, line, "unknown section [%s]", name);
    r->kind = kind;
    r->seen = 0;
    r->header_line = line;
    if (r->block) {
        if (r->block_seen || kind != r->block_kind)
            return fail_at(r->error, line, "expected exactly one [%s] block",
                           wb_kind_name(r->block_kind));
        r->block_seen = true;
        r->object = r->block;
    } else if (kind == WB_KIND_SESSION) {
        if (r->session->has_head) return fail_at(r->error, line, "duplicate [session]");
        r->session->has_head = true;
        r->object = &r->session->head;
    } else if (kind == WB_KIND_FORM) {
        if (r->session->has_form) return fail_at(r->error, line, "duplicate [form]");
        r->session->has_form = true;
        r->object = &r->session->form;
    } else {
        unsigned *count = count_of(r->session, kind);
        if (*count == wb_kind_capacity(kind))
            return fail_at(r->error, line, "more than %u [%s] blocks",
                           wb_kind_capacity(kind), name);
        r->object = object_at(r->session, kind, (*count)++);
    }
    object_default(kind, r->object);
    return true;
}

static bool key_value(reader *r, char *line, unsigned number) {
    if (!r->object) return fail_at(r->error, number, "key outside a section");
    char *eq = strchr(line, '=');
    if (!eq || eq == line) return fail_at(r->error, number, "expected key=value");
    *eq = 0;
    const char *value = eq + 1;
    unsigned index;
    const field *f = find_field(r->kind, line, &index);
    if (!f) return fail_at(r->error, number, "unknown key '%s' in [%s]", line, wb_kind_name(r->kind));
    if (!(f->flags & REPEAT) && (r->seen >> index & 1u))
        return fail_at(r->error, number, "duplicate key '%s'", line);
    char why[WHY_MAX] = "invalid value";
    if (!parse_field(f, r->object, value, why))
        return fail_at(r->error, number, "%s: %s", line, why);
    r->seen |= UINT64_C(1) << index;
    return true;
}

static bool run(reader *r, const char *text, size_t size, bool want_header) {
    bool header = !want_header;
    unsigned number = 0;
    char line[WB_LINE_MAX + 2];
    size_t at = 0;
    if (memchr(text, 0, size)) return fail_at(r->error, 0, "NUL byte in text");
    while (at < size) {
        number++;
        const char *newline = memchr(text + at, '\n', size - at);
        size_t length = newline ? (size_t)(newline - text - at) : size - at;
        if (length > WB_LINE_MAX) return fail_at(r->error, number, "line exceeds %d bytes", WB_LINE_MAX);
        memcpy(line, text + at, length);
        line[length] = 0;
        at += length + (newline ? 1 : 0);
        if (length && line[length - 1] == '\r') line[--length] = 0;
        if (!length || line[0] == '#') continue;
        if (!header) {
            if (strcmp(line, "mws=1")) return fail_at(r->error, number, "expected version line mws=1");
            header = true;
            continue;
        }
        if (line[0] == '[') {
            if (length < 3 || line[length - 1] != ']')
                return fail_at(r->error, number, "malformed section header");
            line[length - 1] = 0;
            if (!begin(r, line + 1, number)) return false;
            continue;
        }
        if (!key_value(r, line, number)) return false;
    }
    if (!header) return fail_at(r->error, number, "missing version line mws=1");
    return finish(r);
}

bool wb_read(const char *text, size_t size, wb_session *session, wb_error *error) {
    if (error) *error = (wb_error){ 0 };
    if (!text || !session) return fail_at(error, 0, "null input");
    wb_session_init(session);
    reader r = { .session = session, .error = error };
    return run(&r, text, size, true);
}

bool wb_write(const wb_session *s, wb_buf *out) {
    if (!wb_buf_printf(out, "mws=%d\n", WB_MWS_VERSION)) return false;
    for (unsigned k = 0; k < WB_KIND_COUNT; k++) {
        unsigned n = wb_kind_count(s, (wb_kind)k);
        for (unsigned i = 0; i < n; i++) {
            if (!wb_buf_printf(out, "\n")) return false;
            if (!write_object((wb_kind)k, object_at((wb_session *)s, (wb_kind)k, i), out))
                return false;
        }
    }
    return !out->failed;
}

bool wb_write_object(const wb_session *s, wb_kind kind, unsigned index, wb_buf *out) {
    if (kind >= WB_KIND_COUNT || index >= wb_kind_count(s, kind)) return false;
    return write_object(kind, object_at((wb_session *)s, kind, index), out);
}

bool wb_read_object(wb_session *s, wb_kind kind, unsigned index, bool insert,
                    const char *text, size_t size, wb_error *error) {
    if (error) *error = (wb_error){ 0 };
    if (kind >= WB_KIND_COUNT) return fail_at(error, 0, "bad kind");
    unsigned count = wb_kind_count(s, kind);
    bool singleton = wb_kind_singleton(kind);
    if (singleton ? index != 0 || (insert && count) : (insert ? index > count : index >= count))
        return fail_at(error, 0, "object index %u outside [%s] x%u", index, wb_kind_name(kind), count);
    if (insert && !singleton && count == wb_kind_capacity(kind))
        return fail_at(error, 0, "more than %u [%s] blocks", wb_kind_capacity(kind), wb_kind_name(kind));
    void *object = malloc(KINDS[kind].size);
    if (!object) return fail_at(error, 0, "out of memory");
    reader r = { .block = object, .block_kind = kind, .error = error };
    bool ok = run(&r, text, size, false);
    if (ok && !r.block_seen) ok = fail_at(error, 0, "no [%s] block", wb_kind_name(kind));
    if (ok) {
        if (singleton) {
            memcpy(object_at(s, kind, 0), object, KINDS[kind].size);
            if (kind == WB_KIND_SESSION) s->has_head = true;
            else s->has_form = true;
        } else {
            unsigned *n = count_of(s, kind);
            unsigned char *base = object_at(s, kind, 0);
            size_t step = KINDS[kind].size;
            if (insert) {
                memmove(base + (index + 1) * step, base + index * step, (*n - index) * step);
                (*n)++;
            }
            memcpy(base + index * step, object, step);
        }
    }
    free(object);
    return ok;
}

bool wb_remove_object(wb_session *s, wb_kind kind, unsigned index) {
    if (kind >= WB_KIND_COUNT) return false;
    if (wb_kind_singleton(kind)) {
        if (index || !wb_kind_count(s, kind)) return false;
        object_default(kind, object_at(s, kind, 0));
        if (kind == WB_KIND_SESSION) s->has_head = false;
        else s->has_form = false;
        return true;
    }
    unsigned *n = count_of(s, kind);
    if (index >= *n) return false;
    unsigned char *base = object_at(s, kind, 0);
    size_t step = KINDS[kind].size;
    memmove(base + index * step, base + (index + 1) * step, (*n - index - 1) * step);
    (*n)--;
    memset(base + *n * step, 0, step);
    return true;
}

bool wb_set_key(wb_session *s, wb_kind kind, unsigned index, const char *key,
                const char *value, wb_error *error) {
    if (error) *error = (wb_error){ 0 };
    if (kind >= WB_KIND_COUNT || index >= wb_kind_count(s, kind))
        return fail_at(error, 0, "no such object");
    const field *f = find_field(kind, key, nullptr);
    if (!f) return fail_at(error, 0, "unknown key '%s' in [%s]", key, wb_kind_name(kind));
    if (f->flags & REPEAT)
        return fail_at(error, 0, "'%s' is a list; edit the whole [%s] block", key, wb_kind_name(kind));
    void *target = object_at(s, kind, index);
    void *copy = malloc(KINDS[kind].size);
    if (!copy) return fail_at(error, 0, "out of memory");
    memcpy(copy, target, KINDS[kind].size);
    if (!*value) {
        free(copy);
        if (f->flags & REQ) return fail_at(error, 0, "'%s' is required", key);
        /* Reset to the default: write the block without this key and read
         * it back, so custom keys reset exactly as a fresh block would. */
        wb_buf block = { 0 }, kept = { 0 };
        bool ok = write_object(kind, target, &block);
        size_t key_length = strlen(key);
        for (size_t at = 0; ok && at < block.length;) {
            const char *end = memchr(block.data + at, '\n', block.length - at);
            size_t length = (size_t)(end - block.data - at) + 1;
            if (!(length > key_length && !memcmp(block.data + at, key, key_length)
                  && block.data[at + key_length] == '='))
                ok = wb_buf_append(&kept, block.data + at, length);
            at += length;
        }
        if (ok) ok = wb_read_object(s, kind, index, false, kept.data, kept.length, error);
        wb_buf_free(&block);
        wb_buf_free(&kept);
        return ok;
    }
    char why[WHY_MAX] = "invalid value";
    if (strlen(value) > WB_LINE_MAX || !parse_field(f, copy, value, why)) {
        free(copy);
        return fail_at(error, 0, "%s: %s", key, why);
    }
    memcpy(target, copy, KINDS[kind].size);
    free(copy);
    return true;
}

bool wb_get_key(const wb_session *s, wb_kind kind, unsigned index, const char *key, wb_buf *out) {
    if (kind >= WB_KIND_COUNT || index >= wb_kind_count(s, kind)) return false;
    const field *f = find_field(kind, key, nullptr);
    if (!f) return false;
    wb_buf line = { 0 };
    bool ok = write_field(f, object_at((wb_session *)s, kind, index), &line);
    if (ok && line.length) {
        size_t skip = strlen(key) + 1;
        ok = wb_buf_append(out, line.data + skip, line.length - skip - 1);
    } else if (ok) ok = wb_buf_append(out, "", 0);
    wb_buf_free(&line);
    return ok;
}

/* ---- files ------------------------------------------------------------- */

bool wb_read_file(const char *path, char **data, size_t *size) {
    *data = nullptr;
    *size = 0;
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    wb_buf buf = { 0 };
    char chunk[65536];
    size_t got;
    while ((got = fread(chunk, 1, sizeof chunk, file)))
        if (!wb_buf_append(&buf, chunk, got)) break;
    bool ok = !ferror(file) && !buf.failed;
    if (fclose(file)) ok = false;
    if (!ok) {
        wb_buf_free(&buf);
        return false;
    }
    if (!buf.data && !wb_buf_append(&buf, "", 0)) return false;
    *data = buf.data;
    *size = buf.length;
    return true;
}

bool wb_write_file_atomic(const char *path, const char *data, size_t size) {
    char temporary[4096];
    int length = snprintf(temporary, sizeof temporary, "%s.tmp.XXXXXX", path);
    if (length < 0 || (size_t)length >= sizeof temporary) return false;
    int descriptor = mkstemp(temporary);
    if (descriptor < 0) return false;
    /* mkstemp creates 0600; session files are ordinary 0644 text. */
    FILE *file = fchmod(descriptor, 0644) == 0 ? fdopen(descriptor, "wb") : nullptr;
    if (!file) {
        close(descriptor);
        remove(temporary);
        return false;
    }
    bool ok = fwrite(data, 1, size, file) == size && fflush(file) == 0
           && fsync(fileno(file)) == 0;
    if (fclose(file)) ok = false;
    if (ok && rename(temporary, path) == 0) return true;
    remove(temporary);
    return false;
}

bool wb_load(const char *path, wb_session *session, wb_error *error) {
    char *data;
    size_t size;
    if (!wb_read_file(path, &data, &size)) return fail_at(error, 0, "cannot read %s", path);
    bool ok = wb_read(data, size, session, error);
    free(data);
    return ok;
}

bool wb_save(const char *path, const wb_session *session, wb_error *error) {
    wb_buf text = { 0 };
    bool ok = wb_write(session, &text);
    if (!ok) fail_at(error, 0, "cannot serialize session");
    else if (!wb_write_file_atomic(path, text.data, text.length)) {
        ok = false;
        fail_at(error, 0, "cannot commit %s", path);
    }
    wb_buf_free(&text);
    return ok;
}
