#define _POSIX_C_SOURCE 200809L
#include "wb_session.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "../../driver/ma_patch_file.h"

static const char *const KIND_NAMES[WB_KIND_COUNT] = {
    "session", "source", "form", "stem", "part", "rule", "groove", "curve",
    "lane", "marker", "import", "overlay",
};

const char *wb_kind_name(wb_kind kind) {
    return kind < WB_KIND_COUNT ? KIND_NAMES[kind] : "?";
}

bool wb_kind_from_name(const char *name, wb_kind *kind) {
    for (unsigned k = 0; k < WB_KIND_COUNT; k++)
        if (!strcmp(name, KIND_NAMES[k])) {
            *kind = (wb_kind)k;
            return true;
        }
    return false;
}

bool wb_kind_singleton(wb_kind kind) {
    return kind == WB_KIND_SESSION || kind == WB_KIND_FORM;
}

unsigned wb_kind_count(const wb_session *s, wb_kind kind) {
    switch (kind) {
    case WB_KIND_SESSION: return s->has_head;
    case WB_KIND_SOURCE: return s->source_count;
    case WB_KIND_FORM: return s->has_form;
    case WB_KIND_STEM: return s->stem_count;
    case WB_KIND_PART: return s->part_count;
    case WB_KIND_RULE: return s->rule_count;
    case WB_KIND_GROOVE: return s->groove_count;
    case WB_KIND_CURVE: return s->curve_count;
    case WB_KIND_LANE: return s->lane_count;
    case WB_KIND_MARKER: return s->marker_count;
    case WB_KIND_IMPORT: return s->import_count;
    case WB_KIND_OVERLAY: return s->overlay_count;
    default: return 0;
    }
}

unsigned wb_kind_capacity(wb_kind kind) {
    switch (kind) {
    case WB_KIND_SESSION: return 1;
    case WB_KIND_SOURCE: return WB_SOURCES;
    case WB_KIND_FORM: return 1;
    case WB_KIND_STEM: return WB_STEMS;
    case WB_KIND_PART: return WB_PARTS;
    case WB_KIND_RULE: return WB_RULES;
    case WB_KIND_GROOVE: return WB_GROOVES;
    case WB_KIND_CURVE: return WB_CURVES;
    case WB_KIND_LANE: return WB_LANES;
    case WB_KIND_MARKER: return WB_MARKERS;
    case WB_KIND_IMPORT: return WB_IMPORTS;
    case WB_KIND_OVERLAY: return WB_OVERLAYS;
    default: return 0;
    }
}

void wb_session_init(wb_session *s) {
    memset(s, 0, sizeof *s);
    s->head = (wb_session_head){ .rate = 48000, .control_frames = 480,
                                 .engine = WB_ENGINE_AUTO };
    wb_form_default(&s->form);
}

void wb_source_default(wb_source *source) {
    *source = (wb_source){ .tempo_scale = 1.0 };
}

void wb_form_default(wb_form *form) {
    *form = (wb_form){ .start_bar = 0, .end_bar = 1, .tail_seconds = 10.0,
                       .fade_in_ms = 500.0, .fade_out_ms = 2000.0 };
}

void wb_part_default(wb_part *part) {
    /* The values of ma3_part_init, so a session part with only id, patch
     * and stem renders like an MA3 exhibit part. */
    *part = (wb_part){
        .engine = WB_PART_MAMUT, .limit = 16, .unison = 1,
        .detune_cents = 4.0f, .seed_from_name = true, .character = 0.20f,
        .chorus = 0.0f, .chorus_depth = 0.35f, .chorus_rate_hz = 0.25f,
        .reverb = 0.0f, .reverb_size = 0.65f, .reverb_damping = 0.55f,
        .gain = 1.0f, .pressure = 0.0f, .dedupe = WB_DEDUPE_RESTRIKE,
    };
}

void wb_rule_default(wb_rule *rule) {
    *rule = (wb_rule){
        .track = -1, .channel = -1, .program = -1, .map_to = -1,
        .fold_below = -1, .fold_above = -1, .velocity_num = 1,
        .velocity_den = 1, .velocity_min = 1, .velocity_max = 127,
        .bar_in = -1, .bar_out = -1,
    };
}

void wb_groove_default(wb_groove *groove) {
    *groove = (wb_groove){ .note = 36, .length_ticks = 96, .steps_per_bar = 16 };
}

void wb_curve_default(wb_curve *curve) {
    *curve = (wb_curve){ .shape = WB_SHAPE_LINEAR };
}

void wb_lane_default(wb_lane *lane) {
    *lane = (wb_lane){ .op = WB_LANE_SET, .shape = WB_SHAPE_LINEAR,
                       .scale = 1.0f, .offset = 0.0f, .minimum = 0.0f,
                       .maximum = 1.0f };
}

void wb_marker_default(wb_marker *marker) {
    *marker = (wb_marker){ 0 };
}

void wb_overlay_default(wb_overlay *overlay) {
    *overlay = (wb_overlay){ .op = WB_OVERLAY_ADD, .length_bars = 0.25, .note = 60, .velocity = 100 };
}

void wb_import_default(wb_import *import) {
    *import = (wb_import){ .split = WB_SPLIT_TRACK_CHANNEL };
}

bool wb_ctlset_has(const wb_ctlset *set, unsigned kind) {
    return kind < WB_CTL_COUNT && (set->bits[kind >> 6] >> (kind & 63) & 1u);
}

void wb_ctlset_add(wb_ctlset *set, unsigned kind) {
    if (kind < WB_CTL_COUNT) set->bits[kind >> 6] |= UINT64_C(1) << (kind & 63);
}

bool wb_ctlset_empty(const wb_ctlset *set) {
    return !set->bits[0] && !set->bits[1] && !set->bits[2];
}

bool wb_id_valid(const char *text) {
    size_t length = text ? strnlen(text, WB_ID_MAX + 1) : 0;
    if (!length || length > WB_ID_MAX) return false;
    for (size_t i = 0; i < length; i++) {
        char c = text[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
               || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        if (!ok) return false;
    }
    return true;
}

bool wb_text_valid(const char *text, size_t max) {
    size_t length = text ? strnlen(text, max + 1) : 0;
    if (length > max) return false;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c < 0x20 || c == 0x7f) return false;
    }
    /* No leading or trailing blanks: they would not survive a hand edit. */
    return !length || (text[0] != ' ' && text[length - 1] != ' ');
}

bool wb_path_valid(const char *text) {
    size_t length = text ? strnlen(text, WB_PATH_MAX + 1) : 0;
    if (!length || length > WB_PATH_MAX || text[0] == '/') return false;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c <= 0x20 || c >= 0x7f || c == '\\' || c == '"') return false;
    }
    const char *component = text;
    for (;;) {
        const char *slash = strchr(component, '/');
        size_t size = slash ? (size_t)(slash - component) : strlen(component);
        if (!size || (size == 1 && component[0] == '.')
            || (size == 2 && component[0] == '.' && component[1] == '.'))
            return false;
        if (!slash) return true;
        component = slash + 1;
    }
}

bool wb_noteset_has(const wb_noteset *set, unsigned note) {
    return note < 128 && (set->bits[note >> 6] >> (note & 63) & 1u);
}

void wb_noteset_add(wb_noteset *set, unsigned note) {
    if (note < 128) set->bits[note >> 6] |= UINT64_C(1) << (note & 63);
}

#define FIND(name, array, count, member)                                    \
    int name(const wb_session *s, const char *id) {                         \
        for (unsigned i = 0; i < s->count; i++)                             \
            if (!strcmp(s->array[i].member, id)) return (int)i;             \
        return -1;                                                          \
    }
FIND(wb_find_source, source, source_count, id)
FIND(wb_find_stem, stem, stem_count, id)
FIND(wb_find_part, part, part_count, id)
FIND(wb_find_curve, curve, curve_count, id)
#undef FIND

static const char *const POOL_PARAMS[] = {
    "pool.macro.gravitacija", "pool.macro.bloom", "pool.macro.heat",
    "pool.macro.ruin", "pool.macro.swarm", "pool.mod_wheel",
    "pool.channel_pressure", "pool.pitch_bend", "pool.lfo_depth",
    "pool.lfo_rate_hz", "pool.character", "part.gain",
};

bool wb_lane_param_valid(const char *param) {
    if (!param || !*param) return false;
    for (size_t i = 0; i < sizeof POOL_PARAMS / sizeof *POOL_PARAMS; i++)
        if (!strcmp(param, POOL_PARAMS[i])) return true;
    for (size_t i = 0; i < ma_patch_field_count(); i++) {
        ma_patch_field_info info;
        if (ma_patch_field_info_at(i, &info) && !strcmp(info.name, param))
            return true;
    }
    return false;
}

[[gnu::format(printf, 2, 3)]]
static bool fail(wb_error *error, const char *format, ...) {
    if (error) {
        error->line = 0;
        va_list args;
        va_start(args, format);
        vsnprintf(error->message, sizeof error->message, format, args);
        va_end(args);
    }
    return false;
}

static bool points_valid(const wb_point *point, unsigned count) {
    for (unsigned i = 0; i < count; i++) {
        if (!isfinite(point[i].bar) || !isfinite(point[i].value)) return false;
        if (i && !(point[i].bar > point[i - 1].bar)) return false;
    }
    return true;
}

bool wb_session_validate(const wb_session *s, wb_error *error) {
    if (!s->has_head) return fail(error, "missing [session]");
    if (!s->has_form) return fail(error, "missing [form]");
    if (!s->source_count) return fail(error, "a session needs at least one [source]");
    if (!s->stem_count) return fail(error, "a session needs at least one [stem]");
    if (!s->part_count) return fail(error, "a session needs at least one [part]");

    for (unsigned i = 0; i < s->source_count; i++)
        for (unsigned j = 0; j < i; j++)
            if (!strcmp(s->source[i].id, s->source[j].id))
                return fail(error, "duplicate source id '%s'", s->source[i].id);
    for (unsigned i = 0; i < s->stem_count; i++)
        for (unsigned j = 0; j < i; j++)
            if (!strcmp(s->stem[i].id, s->stem[j].id))
                return fail(error, "duplicate stem id '%s'", s->stem[i].id);
    for (unsigned i = 0; i < s->curve_count; i++)
        for (unsigned j = 0; j < i; j++)
            if (!strcmp(s->curve[i].id, s->curve[j].id))
                return fail(error, "duplicate curve id '%s'", s->curve[i].id);

    const wb_form *f = &s->form;
    if (f->end_bar <= f->start_bar)
        return fail(error, "form end_bar must be after start_bar");
    for (unsigned i = 0; i < f->section_count; i++) {
        const wb_section *x = &f->section[i];
        if (x->end_bar <= x->start_bar)
            return fail(error, "section '%s' is empty", x->name);
        if (x->start_bar < f->start_bar || x->end_bar > f->end_bar)
            return fail(error, "section '%s' lies outside the form", x->name);
        if (i && x->start_bar < f->section[i - 1].end_bar)
            return fail(error, "section '%s' overlaps or precedes '%s'",
                        x->name, f->section[i - 1].name);
        for (unsigned j = 0; j < i; j++)
            if (!strcmp(x->name, f->section[j].name))
                return fail(error, "duplicate section name '%s'", x->name);
    }

    bool stem_used[WB_STEMS] = { 0 };
    for (unsigned i = 0; i < s->part_count; i++) {
        const wb_part *p = &s->part[i];
        for (unsigned j = 0; j < i; j++)
            if (!strcmp(p->id, s->part[j].id))
                return fail(error, "duplicate part id '%s'", p->id);
        int stem = wb_find_stem(s, p->stem);
        if (stem < 0)
            return fail(error, "part '%s' names undefined stem '%s'", p->id, p->stem);
        stem_used[stem] = true;
        if (p->engine == WB_PART_MAMUT && p->limit % p->unison)
            return fail(error, "part '%s': unison %u does not divide limit %u",
                        p->id, p->unison, p->limit);

    }
    /* An empty stem would render silence under a name that suggests music;
     * the v2 kick-in-pads slip is the reason this is an error. */
    for (unsigned i = 0; i < s->stem_count; i++)
        if (!stem_used[i])
            return fail(error, "stem '%s' has no part", s->stem[i].id);

    for (unsigned i = 0; i < s->rule_count; i++) {
        const wb_rule *r = &s->rule[i];
        if (wb_find_part(s, r->part) < 0)
            return fail(error, "rule %u names undefined part '%s'", i + 1, r->part);
        if (wb_find_source(s, r->source) < 0)
            return fail(error, "rule %u names undefined source '%s'", i + 1, r->source);
        if (r->velocity_min > r->velocity_max)
            return fail(error, "rule %u: velocity_min above velocity_max", i + 1);
        if (r->bar_in >= 0 && r->bar_out >= 0 && r->bar_out <= r->bar_in)
            return fail(error, "rule %u: empty bar range", i + 1);
    }

    for (unsigned i = 0; i < s->groove_count; i++) {
        const wb_groove *g = &s->groove[i];
        if (wb_find_part(s, g->part) < 0)
            return fail(error, "groove %u names undefined part '%s'", i + 1, g->part);
        for (unsigned j = 0; j < g->pattern_count; j++) {
            const wb_pattern *p = &g->pattern[j];
            for (unsigned k = 0; k < j; k++)
                if (!strcmp(p->name, g->pattern[k].name))
                    return fail(error, "groove %u: duplicate pattern '%s'", i + 1, p->name);
            for (unsigned k = 0; k < p->step_count; k++)
                if (p->step[k] >= g->steps_per_bar)
                    return fail(error, "groove %u: pattern '%s' step %u outside %u steps",
                                i + 1, p->name, p->step[k], g->steps_per_bar);
        }
        for (unsigned j = 0; j < g->use_count; j++) {
            const wb_use *u = &g->use[j];
            if (u->pattern >= g->pattern_count)
                return fail(error, "groove %u: use names an undefined pattern", i + 1);
            if (u->to_bar <= u->from_bar)
                return fail(error, "groove %u: empty use range", i + 1);
            if (j && u->from_bar < g->use[j - 1].to_bar)
                return fail(error, "groove %u: use ranges overlap or are out of order", i + 1);
        }
        for (unsigned j = 1; j < g->hit_count; j++)
            if (g->hit[j].bar <= g->hit[j - 1].bar)
                return fail(error, "groove %u: hits out of order", i + 1);
    }

    for (unsigned i = 0; i < s->curve_count; i++) {
        const wb_curve *c = &s->curve[i];
        if (!c->point_count || !points_valid(c->point, c->point_count))
            return fail(error, "curve '%s' needs strictly increasing points", c->id);
    }

    for (unsigned i = 0; i < s->lane_count; i++) {
        const wb_lane *l = &s->lane[i];
        if (wb_find_part(s, l->part) < 0)
            return fail(error, "lane %u names undefined part '%s'", i + 1, l->part);
        if (!wb_lane_param_valid(l->param))
            return fail(error, "lane %u: unknown parameter '%s'", i + 1, l->param);
        int lp = wb_find_part(s, l->part);
        if (s->part[lp].engine != WB_PART_MAMUT && strcmp(l->param, "part.gain"))
            return fail(error, "lane %u: %s parts take only part.gain lanes (console automation "
                        "goes through forwarded controllers)", i + 1,
                        s->part[lp].engine == WB_PART_ORGAN ? "organ" : "ep73");
        bool curve = l->curve[0] != 0;
        if (curve == (l->point_count != 0))
            return fail(error, "lane %u needs either points or a curve", i + 1);
        if (curve && wb_find_curve(s, l->curve) < 0)
            return fail(error, "lane %u names undefined curve '%s'", i + 1, l->curve);
        if (!points_valid(l->point, l->point_count))
            return fail(error, "lane %u: points must have increasing bars", i + 1);
        if (l->clamp && !(l->minimum <= l->maximum))
            return fail(error, "lane %u: clamp minimum above maximum", i + 1);
    }

    for (unsigned i = 1; i < s->marker_count; i++)
        if (s->marker[i].bar < s->marker[i - 1].bar)
            return fail(error, "markers must be in bar order");
    for (unsigned i = 0; i < s->overlay_count; i++)
        if (wb_find_part(s, s->overlay[i].part) < 0)
            return fail(error, "overlay %u names undefined part '%s'", i + 1, s->overlay[i].part);
    for (unsigned i = 0; i < s->import_count; i++) {
        if (wb_find_source(s, s->import[i].source) < 0)
            return fail(error, "import %u names undefined source '%s'", i + 1, s->import[i].source);
        for (unsigned j = 0; j < i; j++)
            if (!strcmp(s->import[i].source, s->import[j].source))
                return fail(error, "source '%s' has two [import] blocks", s->import[i].source);
    }
    return true;
}
