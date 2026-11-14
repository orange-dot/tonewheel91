/* The module panels, the macro strip and the top bar (plan §2.2–§2.5).
 * Every picture is drawn from the probe's result: values computed by
 * running the voice's own code at the probe note. */
#include "pb_app.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/ma_raster_table.h"

/* ---- field controls ----------------------------------------------------------- */

static double f_to(const void *c, double x) { return pb_field_to_unit(c, x); }
static double f_from(const void *c, double u) { return pb_field_from_unit(c, u); }
static void f_format(const void *c, double x, char *t, size_t n) { pb_field_format(c, x, t, n); }
static bool f_parse(const void *c, const char *t, double *x) { return pb_field_parse(c, t, x); }

static double field_value(const ma_patch *p, int index) {
    double x = NAN;
    (void)ma_patch_field_get(p, (size_t)index, &x);
    return x;
}

bool pb_field_control(pb_app *a, struct nk_context *ctx, const char *name, bool fader) {
    int index = pb_field_index(name);
    pb_field_view v;
    if (index < 0 || !pb_field_view_at((size_t)index, &v)) {
        nk_label(ctx, name, NK_TEXT_LEFT);
        return false;
    }
    ma_patch *p = pb_authored(a);
    double value = field_value(p, index);
    double parent = a->has_parent ? field_value(&a->parent, index) : NAN;
    pb_scale_fn s = { f_to, f_from, f_format, f_parse, &v, v.info.fine_step, v.info.coarse_step,
                      a->has_parent ? parent : v.display->init, v.info.integer };
    pb_marks m = { .parent = parent, .b = NAN, .effective = NAN, .mod_lo = NAN, .mod_hi = NAN };
    m.inherited = a->has_parent && value == parent;
    if (a->slot_valid[!a->current]) {
        double b = field_value(&a->slot[!a->current], index);
        if (b != value) m.b = b;
    }
    const pb_probe_result *r = a->shown;
    if (r && index < 64 && isfinite(r->effective[index]) && fabs(r->effective[index] - value) > 1e-6 * (1 + fabs(value)))
        m.effective = r->effective[index];
    if (r && r->note_ready && !strcmp(name, "filter.cutoff_hz")) {
        m.mod_lo = r->cutoff_env_low_hz;
        m.mod_hi = r->cutoff_env_high_hz;
    }
    if (r && !strcmp(name, "source.crossmod") && r->portrait_count && p->bcs_amount > 0) {
        m.mod_lo = value;
        m.mod_hi = fmin(1, value + p->bcs_amount * .25);
    }
    bool changed = fader ? pb_fader(&a->ui, ctx, index, v.display->module, v.display->label, &value, &s, &m)
                         : pb_bar(&a->ui, ctx, index, v.display->module, v.display->label, &value, &s, &m);
    if (changed) {
        if (a->ui.begin) {
            pb_begin_edit(a, name);
            a->ui.begin = false;
        }
        (void)ma_patch_field_set(p, (size_t)index, pb_field_clamp(&v, value));
        pb_edited(a);
    }
    return changed;
}

/* Bench voice controls: not patch fields. */
typedef struct { double lo, hi; bool log, integer; const char *unit; } bench_scale;
static double b_to(const void *c, double x) {
    const bench_scale *s = c;
    if (s->log) return log(fmax(x, s->lo) / s->lo) / log(s->hi / s->lo);
    return (x - s->lo) / (s->hi - s->lo);
}
static double b_from(const void *c, double u) {
    const bench_scale *s = c;
    u = u < 0 ? 0 : u > 1 ? 1 : u;
    double x = s->log ? s->lo * pow(s->hi / s->lo, u) : s->lo + u * (s->hi - s->lo);
    return s->integer ? round(x) : x;
}
static void b_format(const void *c, double x, char *t, size_t n) {
    const bench_scale *s = c;
    if (!strcmp(s->unit, "dB")) snprintf(t, n, "%+.1f dB", x > 0 ? 20 * log10(x) : -INFINITY);
    else if (s->integer) snprintf(t, n, "%.0f%s", x, s->unit);
    else snprintf(t, n, "%.3g%s", x, s->unit);
}
static bool b_parse(const void *c, const char *t, double *x) {
    const bench_scale *s = c;
    char *end;
    double v = strtod(t, &end);
    if (end == t) return false;
    if (!strcmp(s->unit, "dB")) v = pow(10, v / 20);
    if (!(v >= s->lo && v <= s->hi)) return false;
    *x = s->integer ? round(v) : v;
    return true;
}

static bool bench_control(pb_app *a, struct nk_context *ctx, int id, const char *label, float *target,
                          bench_scale scale, double reset) {
    double value = *target;
    pb_scale_fn s = { b_to, b_from, b_format, b_parse, &scale, scale.integer ? 1 : (scale.hi - scale.lo) / 100,
                      scale.integer ? 4 : (scale.hi - scale.lo) / 10, reset, scale.integer };
    pb_marks m = { .parent = NAN, .b = NAN, .effective = NAN, .mod_lo = NAN, .mod_hi = NAN };
    if (!pb_bar(&a->ui, ctx, id, PB_MOD_VOICE, label, &value, &s, &m)) return false;
    a->ui.begin = false;
    *target = (float)value;
    return true;
}

/* ---- module frame ------------------------------------------------------------------ */

static const struct { pb_module module; int solo; int mute; } MONITOR[] = {
    { PB_MOD_VCO1, PB_SRC_VCO1, -1 }, { PB_MOD_VCO2, PB_SRC_VCO2, -1 }, { PB_MOD_RASTER, PB_SRC_RASTER, -1 },
    { PB_MOD_MOZAIK, PB_SRC_MOZAIK, -1 }, { PB_MOD_MIXER, PB_SRC_NOISE, PB_STAGE_PRESSURE },
    { PB_MOD_BCS, -1, PB_STAGE_BCS }, { PB_MOD_FILTER, -1, PB_STAGE_FILTER }, { PB_MOD_GLAS, -1, PB_STAGE_GLAS },
    { PB_MOD_OUTPUT, -1, PB_STAGE_BODY },
};

static bool bypassed(const ma_patch *p, pb_module m) {
    switch (m) {
    case PB_MOD_VCO1:
        return !p->vco1.saw_level && !p->vco1.pulse_level && !p->vco1.triangle_level && !p->vco1.sine_level
            && !p->vco1.tri_sine_level && !p->vco1.tri_saw_level;
    case PB_MOD_VCO2: return !p->vco2_level;
    case PB_MOD_SYNC: return !p->sync_amount && !p->crossmod_amount && !p->vco_analog;
    case PB_MOD_RASTER: return !p->raster_mix;
    case PB_MOD_MOZAIK: return !p->mozaik_mix;
    case PB_MOD_MIXER: return !p->noise_level && !p->mixer_pressure;
    case PB_MOD_BCS: return !p->bcs_amount;
    case PB_MOD_GLAS: return !p->formant_mix && !p->grain_mix;
    case PB_MOD_OUTPUT: return !p->body_drive;
    default: return false;
    }
}

/* Set every field of a module from `from` (or to its init value). */
static void module_fields(pb_app *a, pb_module m, const ma_patch *from, const char *label) {
    pb_begin_edit(a, label);
    ma_patch *p = pb_authored(a);
    pb_field_view v;
    for (size_t i = 0; pb_field_view_at(i, &v); i++)
        if (v.display->module == m)
            (void)ma_patch_field_set(p, i, from ? field_value(from, (int)i) : v.display->init);
    pb_edited(a);
}

static void header(pb_app *a, struct nk_context *ctx, pb_module m, const char *extra) {
    const wb_theme *t = pb_theme(a);
    const ma_patch *p = pb_authored(a);
    int solo = -1, mute = -1;
    for (size_t i = 0; i < sizeof MONITOR / sizeof *MONITOR; i++)
        if (MONITOR[i].module == m) {
            solo = MONITOR[i].solo;
            mute = MONITOR[i].mute;
        }
    nk_layout_row_begin(ctx, NK_STATIC, 18, 9);
    nk_layout_row_push(ctx, 12);
    struct nk_rect dot;
    if (nk_widget(&dot, ctx)) {
        struct nk_rect c = nk_rect(dot.x + 2, dot.y + 5, 8, 8);
        if (bypassed(p, m)) nk_fill_circle(nk_window_get_canvas(ctx), c, t->dim);
        else nk_stroke_circle(nk_window_get_canvas(ctx), c, 1, t->dim);
    }
    float width = nk_window_get_content_region(ctx).w;
    float name_w = width - 12 - 26 * 6 - (extra ? 120 : 0) - 8;
    nk_layout_row_push(ctx, name_w > 40 ? name_w : 40);
    nk_label_colored(ctx, pb_module_name(m), NK_TEXT_LEFT, t->text);
    if (extra) {
        nk_layout_row_push(ctx, 120);
        nk_label_colored(ctx, extra, NK_TEXT_RIGHT, t->steal_released);
    }
    nk_layout_row_push(ctx, 24);
    if (solo >= 0) {
        bool on = a->monitor.solo[solo];
        if (nk_select_label(ctx, "S", NK_TEXT_CENTERED, on) != on) a->monitor.solo[solo] = !on;
    } else nk_spacing(ctx, 1);
    nk_layout_row_push(ctx, 24);
    if (mute >= 0) {
        bool on = a->monitor.mute[mute];
        if (nk_select_label(ctx, "M", NK_TEXT_CENTERED, on) != on) a->monitor.mute[mute] = !on;
    } else nk_spacing(ctx, 1);
    if (m == PB_MOD_VOICE || m == PB_MOD_MACROS) {
        nk_layout_row_end(ctx);
        return;
    }
    nk_layout_row_push(ctx, 24);
    if (nk_button_label(ctx, "I")) module_fields(a, m, nullptr, "init module");
    nk_layout_row_push(ctx, 24);
    if (nk_button_label(ctx, "C")) {
        a->clipboard = *p;
        a->clipboard_module = m;
        a->has_clipboard = true;
        pb_say(a, false, "copied %s", pb_module_name(m));
    }
    nk_layout_row_push(ctx, 24);
    if (nk_button_label(ctx, "P")) {
        if (a->has_clipboard && a->clipboard_module == m) module_fields(a, m, &a->clipboard, "paste module");
        else pb_say(a, true, "the clipboard holds no %s", pb_module_name(m));
    }
    nk_layout_row_push(ctx, 24);
    if (nk_button_label(ctx, "R")) {
        if (a->has_parent) module_fields(a, m, &a->parent, "reset module to parent");
        else module_fields(a, m, nullptr, "reset module");
    }
    nk_layout_row_end(ctx);
}

/* Reserve a picture of height h, framed. */
static struct nk_rect picture(pb_app *a, struct nk_context *ctx, float h) {
    /* A module's first picture gives up what the module's content ran past
     * its frame by on the last frame, so the controls below stay in view on
     * a short window. */
    if (!a->module_picture_done) {
        h -= a->module_shrink[a->module_drawing];
        a->module_picture_done = true;
    }
    if (h < 40) h = 40;
    nk_layout_row_dynamic(ctx, h, 1);
    struct nk_rect r = { 0 };
    if (!nk_widget(&r, ctx)) return r;
    pb_frame(nk_window_get_canvas(ctx), r, pb_theme(a));
    return r;
}

static void grid_x(struct nk_command_buffer *out, struct nk_rect r, float u, struct nk_color c) {
    nk_stroke_line(out, r.x + r.w * u, r.y, r.x + r.w * u, r.y + r.h, 1, c);
}

static void small(pb_app *a, struct nk_context *ctx, struct nk_rect r, const char *text, struct nk_color c) {
    (void)a;
    pb_text(ctx, r, text, c);
}

static float peak_of(const float *x, unsigned n) {
    float p = 0;
    for (unsigned i = 0; i < n; i++) if (fabsf(x[i]) > p) p = fabsf(x[i]);
    return p;
}

/* ---- sources ---------------------------------------------------------------------------- */

static void draw_harmonics(pb_app *a, struct nk_context *ctx, struct nk_rect r, const pb_tap_capture *c) {
    const wb_theme *t = pb_theme(a);
    struct nk_command_buffer *out = nk_window_get_canvas(ctx);
    const pb_harmonics *h = &c->harmonics;
    float top = -6, floor_db = -90;
    for (unsigned k = 0; k < h->count; k++) {
        float u = (h->harmonic_db[k] - floor_db) / (top - floor_db);
        u = u < 0 ? 0 : u > 1 ? 1 : u;
        float w = r.w / 32;
        nk_fill_rect(out, nk_rect(r.x + w * (float)k + 1, r.y + r.h * (1 - u), w - 2, r.h * u), 0, pb_alpha(t->live, 170));
    }
    /* Energy between the harmonics: warning colour, at its place on the axis. */
    float strongest = h->count ? h->harmonic_db[0] : -180;
    for (unsigned k = 0; k < h->count; k++) if (h->harmonic_db[k] > strongest) strongest = h->harmonic_db[k];
    if (h->inharmonic_db > strongest - 70 && a->shown) {
        float n = h->inharmonic_hz / a->shown->f0;
        if (n <= 32) {
            float u = (h->inharmonic_db - floor_db) / (top - floor_db);
            u = u < 0 ? 0 : u > 1 ? 1 : u;
            float x = r.x + r.w / 32 * (n - .5f);
            nk_fill_rect(out, nk_rect(x - 1, r.y + r.h * (1 - u), 3, r.h * u), 0, t->clip);
        }
        char text[64];
        snprintf(text, sizeof text, "between: %.0f dB @ %.0f Hz", h->inharmonic_db, h->inharmonic_hz);
        small(a, ctx, nk_rect(r.x + 2, r.y, r.w, 12), text, t->clip);
    }
}

static void draw_wave(pb_app *a, struct nk_context *ctx, struct nk_rect r, const pb_tap_capture *c,
                      const float components[PB_COMPONENTS][PB_COMPONENT_POINTS], bool components_ready) {
    const wb_theme *t = pb_theme(a);
    struct nk_command_buffer *out = nk_window_get_canvas(ctx);
    if (!c->count || c->cycle < 2) return;
    const float *x = c->x + c->count - c->cycle;
    float range = peak_of(x, c->cycle) * 1.1f;
    if (range < 1e-6f) range = 1;
    nk_stroke_line(out, r.x, r.y + r.h / 2, r.x + r.w, r.y + r.h / 2, 1, t->line);
    grid_x(out, r, .5f, t->line);
    if (components && components_ready)
        for (int k = 0; k < PB_COMPONENTS; k++)
            if (peak_of(components[k], PB_COMPONENT_POINTS) > 0)
                pb_trace(out, r, components[k], PB_COMPONENT_POINTS, -range, range, pb_alpha(t->dim, 110), 1);
    pb_trace(out, r, x, c->cycle, -range, range, t->live, 1.5f);
}

static void vco_module(pb_app *a, struct nk_context *ctx, pb_module m, struct nk_rect bounds) {
    const pb_probe_result *r = a->shown;
    bool two = m == PB_MOD_VCO2;
    const char *prefix = two ? "vco2." : "vco1.";
    char extra[48] = "";
    if (r && r->guard[two] < 1) snprintf(extra, sizeof extra, "guard %.0f %%", 100 * r->guard[two]);
    header(a, ctx, m, extra[0] ? extra : nullptr);
    float ph = bounds.h * .44f;
    struct nk_rect pr = picture(a, ctx, ph);
    const wb_theme *t = pb_theme(a);
    if (r && !two && a->compass_sheet && r->compass_ready) {
        /* Compass: C1..C6 side by side, guard state under each. */
        static const char *NAMES[PB_COMPASS] = { "C1", "C2", "C3", "C4", "C5", "C6" };
        for (int k = 0; k < PB_COMPASS; k++) {
            struct nk_rect cell = nk_rect(pr.x + pr.w / PB_COMPASS * (float)k, pr.y, pr.w / PB_COMPASS - 2, pr.h - 12);
            float range = peak_of(r->compass[k], PB_COMPASS_POINTS) * 1.1f;
            pb_trace(nk_window_get_canvas(ctx), cell, r->compass[k], PB_COMPASS_POINTS, -range, range, t->live, 1);
            char label[24];
            snprintf(label, sizeof label, "%s %.0f%%", NAMES[k], 100 * r->compass_guard[k]);
            small(a, ctx, nk_rect(cell.x, pr.y + pr.h - 12, cell.w, 12), label,
                  r->compass_guard[k] < 1 ? t->steal_released : t->dim);
        }
    } else if (r) {
        struct nk_rect wave = nk_rect(pr.x, pr.y, pr.w, pr.h * .62f);
        struct nk_rect harm = nk_rect(pr.x, pr.y + pr.h * .64f, pr.w, pr.h * .36f);
        draw_wave(a, ctx, wave, &r->tap[two ? PB_TAP_VCO2 : PB_TAP_VCO1], r->component[two], r->components_ready);
        draw_harmonics(a, ctx, harm, &r->tap[two ? PB_TAP_VCO2 : PB_TAP_VCO1]);
        if (two && pb_authored(a)->vco2_level == 0)
            small(a, ctx, nk_rect(pr.x + 4, pr.y + 2, pr.w, 12), "level 0: drawn at 1", t->dim);
        /* Drag the pulse edge: horizontal drag moves the width. */
        float dx = 0, dy = 0;
        bool began = false;
        if (pb_drag_area(&a->ui, ctx, 1000 + (int)m, wave, &dx, &dy, &began) && dx != 0) {
            if (began) pb_begin_edit(a, "pulse width");
            ma_vco_controls *v = two ? &pb_authored(a)->vco2 : &pb_authored(a)->vco1;
            float pw = v->pulse_width + dx / wave.w;
            v->pulse_width = pw < .05f ? .05f : pw > .95f ? .95f : pw;
            pb_edited(a);
        } else if (began) pb_begin_edit(a, "pulse width");
    }
    nk_layout_row_dynamic(ctx, bounds.h * .30f, 6);
    static const char *WAVES[] = { "saw", "pulse", "triangle", "sine", "tri_sine", "tri_saw" };
    char name[32];
    for (int k = 0; k < 6; k++) {
        snprintf(name, sizeof name, "%s%s", prefix, WAVES[k]);
        pb_field_control(a, ctx, name, true);
    }
    nk_layout_row_dynamic(ctx, 18, two ? 2 : 1);
    snprintf(name, sizeof name, "%spulse_width", prefix);
    pb_field_control(a, ctx, name, false);
    if (two) {
        pb_field_control(a, ctx, "vco2.level", false);
        const ma_patch *p = pb_authored(a);
        nk_layout_row_begin(ctx, NK_DYNAMIC, 18, 4);
        nk_layout_row_push(ctx, .55f);
        pb_field_control(a, ctx, "vco2.interval", false);
        nk_layout_row_push(ctx, .1f);
        if (nk_button_label(ctx, "-12") && p->vco2_interval >= -12) {
            pb_begin_edit(a, "interval");
            pb_authored(a)->vco2_interval -= 12;
            pb_edited(a);
        }
        nk_layout_row_push(ctx, .1f);
        if (nk_button_label(ctx, "+12") && p->vco2_interval <= 12) {
            pb_begin_edit(a, "interval");
            pb_authored(a)->vco2_interval += 12;
            pb_edited(a);
        }
        nk_layout_row_push(ctx, .25f);
        nk_label_colored(ctx, pb_interval_name(p->vco2_interval), NK_TEXT_LEFT, t->dim);
        nk_layout_row_end(ctx);
        nk_layout_row_begin(ctx, NK_DYNAMIC, 18, 2);
        nk_layout_row_push(ctx, .65f);
        pb_field_control(a, ctx, "vco2.fine_cents", false);
        nk_layout_row_push(ctx, .35f);
        if (r) {
            /* Beat against VCO1's nearest harmonic at the probe note. */
            double f2 = r->f0 * pow(2.0, (p->vco2_interval * 100.0 + p->vco2_fine_cents) / 1200.0);
            double n = fmax(1, round(f2 / r->f0));
            char beat[32];
            snprintf(beat, sizeof beat, "beat %.2f Hz", fabs(f2 - n * r->f0));
            nk_label_colored(ctx, beat, NK_TEXT_LEFT, t->dim);
        } else nk_spacing(ctx, 1);
        nk_layout_row_end(ctx);
    } else {
        nk_layout_row_dynamic(ctx, 18, 2);
        bool sheet = a->compass_sheet;
        if (pb_toggle(ctx, t, "compass sheet C1..C6", &sheet)) a->compass_sheet = sheet;
        if (r) {
            char f0[32];
            snprintf(f0, sizeof f0, "f0 %.2f Hz", r->f0);
            nk_label_colored(ctx, f0, NK_TEXT_RIGHT, t->dim);
        }
    }
}

static void sync_module(pb_app *a, struct nk_context *ctx, struct nk_rect bounds) {
    const pb_probe_result *r = a->shown;
    const wb_theme *t = pb_theme(a);
    header(a, ctx, PB_MOD_SYNC, nullptr);
    struct nk_rect pr = picture(a, ctx, bounds.h * .5f);
    struct nk_command_buffer *out = nk_window_get_canvas(ctx);
    if (r) {
        const pb_tap_capture *c = &r->tap[PB_TAP_VCO2];
        if (c->cycle > 1) {
            const float *x = c->x + c->count - c->cycle;
            /* VCO1's wraps: two master periods across the picture. */
            for (int k = 0; k <= 2; k++) grid_x(out, pr, (float)k / 2, t->steal_released);
            float range = peak_of(x, c->cycle) * 1.1f;
            if (range < 1e-6f) range = 1;
            pb_trace(out, pr, x, c->cycle, -range, range, t->live, 1.5f);
            if (isfinite(r->crossmod_effective) && r->crossmod_effective > 0) {
                /* VCO1's instantaneous frequency ratio, 1 + .25·xmod·vco2. */
                static float ratio[PB_CAPTURE];
                float xm = r->crossmod_effective;
                for (unsigned i = 0; i < c->cycle; i++) {
                    float q = 1 + .25f * xm * x[i] / range * 1.1f;
                    ratio[i] = q < .25f ? .25f : q > 4 ? 4 : q;
                }
                pb_trace(out, pr, ratio, c->cycle, .5f, 1.5f, pb_alpha(t->cached, 200), 1);
                small(a, ctx, nk_rect(pr.x + 2, pr.y + pr.h - 12, pr.w, 12), "green: VCO1 frequency ratio", t->cached);
            }
        }
    }
    nk_layout_row_dynamic(ctx, 18, 1);
    pb_field_control(a, ctx, "source.sync", false);
    pb_field_control(a, ctx, "source.sync_softness", false);
    pb_field_control(a, ctx, "source.crossmod", false);
    /* Both oscillators' VCO-2 pitch law, and how much of the reset dead time
     * is left untrimmed (flat at the top). */
    pb_field_control(a, ctx, "vco.analog", false);
    pb_field_control(a, ctx, "vco.dead_time", false);
    if (r) {
        const ma_patch *p = pb_authored(a);
        int si = pb_field_index("source.sync"), xi = pb_field_index("source.crossmod");
        double es = r->effective[si], ex = r->effective[xi];
        char text[96];
        snprintf(text, sizeof text, "eff. sync %.2f (macros %+.2f)", r->sync_effective, es - p->sync_amount);
        nk_label_colored(ctx, text, NK_TEXT_LEFT, t->steal_released);
        snprintf(text, sizeof text, "eff. cross-mod %.2f (macros %+.2f)", ex, ex - p->crossmod_amount);
        nk_label_colored(ctx, text, NK_TEXT_LEFT, t->steal_released);
    }
}

static void raster_module(pb_app *a, struct nk_context *ctx, struct nk_rect bounds) {
    const pb_probe_result *r = a->shown;
    const wb_theme *t = pb_theme(a);
    header(a, ctx, PB_MOD_RASTER, nullptr);
    struct nk_rect pr = picture(a, ctx, bounds.h * .5f);
    struct nk_command_buffer *out = nk_window_get_canvas(ctx);
    ma_patch *p = pb_authored(a);
    float row_h = pr.h / MA_RASTER_FAMILIES;
    /* The families' frames, every fourth table sample (64 points a row). */
    static float row[MA_RASTER_SAMPLES / 4];
    for (int f = 0; f < MA_RASTER_FAMILIES; f++) {
        for (int i = 0; i < MA_RASTER_SAMPLES / 4; i++) row[i] = MA_RASTER_TABLE[f][0][4 * i] / 32767.0f;
        pb_trace(out, nk_rect(pr.x, pr.y + row_h * (float)f + 1, pr.w, row_h - 2), row, MA_RASTER_SAMPLES / 4, -1.1f,
                 1.1f, pb_alpha(t->dim, 150), 1);
    }
    float y = pr.y + pr.h * p->raster_position * (MA_RASTER_FAMILIES - 1) / MA_RASTER_FAMILIES + row_h / 2;
    nk_stroke_line(out, pr.x, y, pr.x + pr.w, y, 2, t->steal_released);
    if (r && p->raster_mix > 0) {
        const pb_tap_capture *c = &r->tap[PB_TAP_RASTER];
        unsigned one = c->cycle / 2;
        if (one > 1) pb_trace(out, pr, c->x + c->count - one, one, -1.2f, 1.2f, t->live, 2);
    }
    float dx = 0, dy = 0;
    bool began = false;
    if (pb_drag_area(&a->ui, ctx, 1100, pr, &dx, &dy, &began)) {
        if (began) pb_begin_edit(a, "raster position");
        float v = p->raster_position + dy / (pr.h * (MA_RASTER_FAMILIES - 1) / MA_RASTER_FAMILIES);
        p->raster_position = v < 0 ? 0 : v > 1 ? 1 : v;
        if (dy != 0) pb_edited(a);
    }
    nk_layout_row_dynamic(ctx, 18, 1);
    if (r) {
        char text[96];
        unsigned kept = 128u >> r->raster_mip;
        snprintf(text, sizeof text, "mip %u: harmonics kept %u -> %u, %.0f %%", r->raster_mip, kept, kept / 2,
                 100 * r->raster_blend);
        nk_label_colored(ctx, text, NK_TEXT_LEFT, t->dim);
    }
    pb_field_control(a, ctx, "raster.mix", false);
    pb_field_control(a, ctx, "raster.position", false);
    pb_field_control(a, ctx, "raster.warp", false);
}

static void mozaik_module(pb_app *a, struct nk_context *ctx, struct nk_rect bounds) {
    const pb_probe_result *r = a->shown;
    const wb_theme *t = pb_theme(a);
    header(a, ctx, PB_MOD_MOZAIK, nullptr);
    struct nk_rect pr = picture(a, ctx, bounds.h * .45f);
    struct nk_command_buffer *out = nk_window_get_canvas(ctx);
    float ratio = 0;
    if (r && r->tile_count) {
        float lo = INFINITY, hi = 0, total = 0;
        for (unsigned i = 0; i < r->tile_count; i++) {
            lo = fminf(lo, r->tile_len[i]);
            hi = fmaxf(hi, r->tile_len[i]);
            total += r->tile_len[i];
        }
        ratio = lo > 0 ? hi / lo : 0;
        float x = pr.x, strip = pr.h * .3f;
        for (unsigned i = 0; i < r->tile_count; i++) {
            float w = pr.w * r->tile_len[i] / total;
            bool lng = r->tile_len[i] > (lo + hi) / 2;
            struct nk_color c = r->tile_sign[i] >= 0 ? t->live : t->steal_released;
            nk_fill_rect(out, nk_rect(x + .5f, pr.y + 2 + (lng ? 0 : strip * .4f), w - 1, lng ? strip : strip * .6f), 0,
                         pb_alpha(c, lng ? 220 : 130));
            x += w;
        }
        const pb_tap_capture *c = &r->tap[PB_TAP_MOZAIK];
        unsigned n = c->cycle * 2 < c->count ? c->cycle * 2 : c->count;
        if (n > 1) {
            const float *w = c->x + c->count - n;
            float range = peak_of(w, n) * 1.1f;
            if (range > 0)
                pb_trace(out, nk_rect(pr.x, pr.y + strip + 6, pr.w, pr.h - strip - 8), w, n, -range, range, t->live, 1.5f);
        }
    } else small(a, ctx, nk_rect(pr.x + 4, pr.y + 4, pr.w, 14), "mix 0: no tiles", t->dim);
    nk_layout_row_dynamic(ctx, 18, 1);
    pb_field_control(a, ctx, "mozaik.mix", false);
    pb_field_control(a, ctx, "mozaik.slope", false);
    /* The five slope detents and their snap zones, from the core's own
     * control-to-slope map. */
    struct nk_rect zr;
    nk_layout_row_dynamic(ctx, 6, 1);
    if (nk_widget(&zr, ctx)) {
        float label_w = zr.w * .32f;
        struct nk_rect bar = nk_rect(zr.x + label_w, zr.y, zr.w - label_w, zr.h);
        uint32_t prev = pb_mozaik_slope_q32(0);
        float start = 0;
        for (int i = 1; i <= 200; i++) {
            float u = (float)i / 200;
            uint32_t q = i < 200 ? pb_mozaik_slope_q32(u) : 0;
            if (q != prev || i == 200) {
                bool detent = false;
                for (int d = 0; d < 5; d++) detent = detent || pb_mozaik_detent_q32(d) == prev;
                if (detent) nk_fill_rect(out, nk_rect(bar.x + bar.w * start, bar.y, bar.w * (u - start), bar.h), 0,
                                         pb_alpha(t->steal_released, 160));
                start = u;
                prev = q;
            }
        }
    }
    nk_layout_row_dynamic(ctx, 18, 1);
    pb_field_control(a, ctx, "mozaik.contrast", false);
    pb_field_control(a, ctx, "mozaik.phason", false);
    pb_field_control(a, ctx, "mozaik.drift", false);
    if (ratio > 0) {
        char text[64];
        snprintf(text, sizeof text, "long : short = %.3f", ratio);
        nk_label_colored(ctx, text, NK_TEXT_LEFT, t->dim);
    }
}

static void mixer_module(pb_app *a, struct nk_context *ctx, struct nk_rect bounds) {
    const pb_probe_result *r = a->shown;
    const wb_theme *t = pb_theme(a);
    header(a, ctx, PB_MOD_MIXER, nullptr);
    ma_patch au = pb_audible(a);
    struct nk_rect pr = picture(a, ctx, bounds.h * .22f);
    struct nk_command_buffer *out = nk_window_get_canvas(ctx);
    /* The normalized bus: 1 : vco2 : noise over max(sum, 1), then Raster
     * and Mozaik by their mixes, then the grain crossfade. */
    float vco1 = au.vco1.saw_level + au.vco1.pulse_level + au.vco1.triangle_level + au.vco1.sine_level
               + au.vco1.tri_sine_level + au.vco1.tri_saw_level > 0 ? 1 : 0;
    float analog = fmaxf(1 + au.vco2_level + au.noise_level, 1);
    float total = analog + au.raster_mix + au.mozaik_mix, grain = au.grain_mix;
    struct { const char *name; float share; struct nk_color c; } part[] = {
        { "vco1", vco1 / analog * analog / total * (1 - grain), t->live },
        { "vco2", au.vco2_level / analog * analog / total * (1 - grain), t->cached },
        { "noise", au.noise_level / analog * analog / total * (1 - grain), t->dim },
        { "raster", au.raster_mix / total * (1 - grain), t->steal_released },
        { "mozaik", au.mozaik_mix / total * (1 - grain), t->select },
        { "grains", grain, t->clip },
    };
    float x = pr.x, bar_h = pr.h * .5f;
    for (size_t i = 0; i < sizeof part / sizeof *part; i++) {
        float w = pr.w * part[i].share;
        if (w <= 0) continue;
        nk_fill_rect(out, nk_rect(x, pr.y + 2, w, bar_h), 0, part[i].c);
        char label[32];
        snprintf(label, sizeof label, "%s %.0f%%", part[i].name, 100 * part[i].share);
        if (w > 40) small(a, ctx, nk_rect(x + 2, pr.y + bar_h + 2, w, 12), label, t->text);
        x += w;
    }
    struct nk_rect cr = picture(a, ctx, bounds.h * .3f);
    if (r) {
        static float xs[PB_CURVE];
        for (int i = 0; i < PB_CURVE; i++) xs[i] = -1.5f + 3.0f * (float)i / (PB_CURVE - 1);
        float diag[2] = { -1.5f, 1.5f };
        pb_xy(out, cr, diag, diag, 2, -1.5f, 1.5f, -1.5f, 1.5f, t->line, 1);
        pb_xy(out, cr, xs, r->pressure_curve, PB_CURVE, -1.5f, 1.5f, -1.5f, 1.5f, t->live, 1.5f);
        /* The current source peak on the curve. */
        float pk = r->pressure_peak > 1.5f ? 1.5f : r->pressure_peak;
        int i = (int)lroundf((pk + 1.5f) / 3.0f * (PB_CURVE - 1));
        float px = cr.x + cr.w * (pk + 1.5f) / 3, py = cr.y + cr.h - cr.h * (r->pressure_curve[i] + 1.5f) / 3;
        nk_fill_circle(out, nk_rect(px - 3, py - 3, 6, 6), t->steal_released);
        char text[48];
        snprintf(text, sizeof text, "source peak %.2f", r->pressure_peak);
        small(a, ctx, nk_rect(cr.x + 2, cr.y + 2, cr.w, 12), text, t->dim);
    }
    nk_layout_row_dynamic(ctx, 18, 1);
    pb_field_control(a, ctx, "source.noise", false);
    pb_field_control(a, ctx, "filter.mixer_pressure", false);
}

/* ---- shaping -------------------------------------------------------------------------- */

static void bcs_module(pb_app *a, struct nk_context *ctx, struct nk_rect bounds) {
    const pb_probe_result *r = a->shown;
    const wb_theme *t = pb_theme(a);
    header(a, ctx, PB_MOD_BCS, nullptr);
    struct nk_rect pr = picture(a, ctx, bounds.h * .45f);
    struct nk_command_buffer *out = nk_window_get_canvas(ctx);
    if (r && r->portrait_count > 1 && pb_audible(a).bcs_amount > 0) {
        unsigned n = r->portrait_count;
        struct nk_rect h = nk_rect(pr.x, pr.y, pr.w / 2 - 1, pr.h * .75f), d = nk_rect(pr.x + pr.w / 2 + 1, pr.y, pr.w / 2 - 1, pr.h * .75f);
        float hm = fmaxf(peak_of(r->portrait[0], n), peak_of(r->portrait[1], n)) * 1.1f + 1e-6f;
        float dx = peak_of(r->portrait[2], n) * 1.1f + 1e-6f, dv = peak_of(r->portrait[3], n) * 1.1f + 1e-6f;
        pb_xy(out, h, r->portrait[0], r->portrait[1], n, -hm, hm, -hm, hm, t->live, 1);
        pb_xy(out, d, r->portrait[2], r->portrait[3], n, -dx, dx, -dv, dv, t->cached, 1);
        small(a, ctx, nk_rect(h.x + 2, h.y, h.w, 12), "Hopf re/im", t->dim);
        small(a, ctx, nk_rect(d.x + 2, d.y, d.w, 12), "Duffing x/v", t->dim);
        struct nk_rect tr = nk_rect(pr.x, pr.y + pr.h * .77f, pr.w, pr.h * .23f);
        float m = peak_of(r->bcs_trace, n) * 1.1f + 1e-6f;
        pb_trace(out, tr, r->bcs_trace, n, -m, m, t->steal_released, 1);
    } else if (pr.w > 0) small(a, ctx, nk_rect(pr.x + 4, pr.y + 4, pr.w, 14), "amount 0: bypassed", t->dim);
    nk_layout_row_dynamic(ctx, 18, 1);
    pb_field_control(a, ctx, "bcs.amount", false);
    pb_field_control(a, ctx, "bcs.regime", false);
    /* The regime ruler's landmarks. */
    struct nk_rect lr;
    nk_layout_row_dynamic(ctx, 14, 1);
    if (nk_widget(&lr, ctx)) {
        float label_w = lr.w * .32f;
        static const char *MARKS[] = { "stable", "edge", "sub", "recov" };
        for (int i = 0; i < 4; i++) {
            float x = lr.x + label_w + (lr.w - label_w) * (float)i / 3;
            nk_stroke_line(out, x, lr.y, x, lr.y + 4, 1, t->dim);
            small(a, ctx, nk_rect(i == 3 ? x - 36 : i ? x - 14 : x + 1, lr.y + 2, 40, 12), MARKS[i], t->dim);
        }
    }
    nk_layout_row_dynamic(ctx, 18, 1);
    char text[96];
    snprintf(text, sizeof text, "sounding: resets %llu, max state %.2f", (unsigned long long)a->meters.bcs_resets,
             (double)a->meters.bcs_maximum);
    nk_label_colored(ctx, text, NK_TEXT_LEFT, t->dim);
    if (r) {
        snprintf(text, sizeof text, "probe: resets %llu, max state %.2f", (unsigned long long)r->bcs_resets,
                 (double)r->bcs_maximum);
        nk_label_colored(ctx, text, NK_TEXT_LEFT, t->dim);
    }
}

static float log_x(float hz) { return log10f(fmaxf(hz, 20) / 20) / 3.0f; }

static void filter_module(pb_app *a, struct nk_context *ctx, struct nk_rect bounds) {
    const pb_probe_result *r = a->shown;
    const wb_theme *t = pb_theme(a);
    header(a, ctx, PB_MOD_FILTER, a->monitor.mute[PB_STAGE_FILTER] ? "M: opened" : nullptr);
    float ph = bounds.h * .5f;
    nk_layout_row_begin(ctx, NK_DYNAMIC, ph, 2);
    nk_layout_row_push(ctx, .68f);
    struct nk_rect pr = { 0 }, er = { 0 };
    if (nk_widget(&pr, ctx)) pb_frame(nk_window_get_canvas(ctx), pr, t);
    nk_layout_row_push(ctx, .32f);
    if (nk_widget(&er, ctx)) pb_frame(nk_window_get_canvas(ctx), er, t);
    nk_layout_row_end(ctx);
    struct nk_command_buffer *out = nk_window_get_canvas(ctx);
    float top = 24, bottom = -48;
    for (float db = -36; db <= 12; db += 12) {
        float y = pr.y + pr.h * (top - db) / (top - bottom);
        nk_stroke_line(out, pr.x, y, pr.x + pr.w, y, 1, t->line);
    }
    for (float hz = 100; hz <= 10000; hz *= 10) grid_x(out, pr, log_x(hz), t->line);
    if (r && r->response_ready) {
        if (r->note_ready && r->cutoff_env_high_hz > r->cutoff_env_low_hz) {
            float x0 = log_x(r->cutoff_env_low_hz), x1 = log_x(r->cutoff_env_high_hz);
            nk_fill_rect(out, nk_rect(pr.x + pr.w * x0, pr.y, pr.w * (x1 - x0), pr.h), 0, pb_alpha(t->live, 40));
        }
        static float xs[PB_RESPONSE];
        for (int k = 0; k < PB_RESPONSE; k++) xs[k] = log_x(r->response_hz[k]);
        pb_xy(out, pr, xs, r->response_low_db, PB_RESPONSE, 0, 1, bottom, top, pb_alpha(t->dim, 220), 1);
        pb_xy(out, pr, xs, r->response_nominal_db, PB_RESPONSE, 0, 1, bottom, top, t->live, 1.5f);
        small(a, ctx, nk_rect(pr.x + 2, pr.y + 1, pr.w, 12), "dim -26 dBFS in, bright -6 dBFS in", t->dim);
    }
    if (r) {
        float x = pr.x + pr.w * log_x(r->cutoff_effective_hz);
        nk_stroke_line(out, x, pr.y, x, pr.y + pr.h, 1, t->steal_released);
        char text[48];
        snprintf(text, sizeof text, "eff. %.0f Hz", r->cutoff_effective_hz);
        small(a, ctx, nk_rect(x + 2, pr.y + pr.h - 13, 100, 12), text, t->steal_released);
    }
    /* Drag: x sets cutoff, y resonance. */
    float dx = 0, dy = 0;
    bool began = false;
    ma_patch *p = pb_authored(a);
    if (pb_drag_area(&a->ui, ctx, 1200, pr, &dx, &dy, &began)) {
        if (began) pb_begin_edit(a, "cutoff/resonance");
        float u = log_x(p->filter_cutoff_hz) + dx / pr.w;
        u = u < 0 ? 0 : u > 1 ? 1 : u;
        p->filter_cutoff_hz = fminf(20000, 20 * powf(1000, u));
        float q = p->filter_resonance - dy / pr.h;
        p->filter_resonance = q < 0 ? 0 : q > 1 ? 1 : q;
        if (dx != 0 || dy != 0) pb_edited(a);
    }
    if (r && r->note_ready && r->note_count > 1) {
        pb_trace(out, er, r->note_filter, r->note_count, 0, 1.05f, t->live, 1.5f);
        float release = r->note_release_at / r->note_seconds;
        grid_x(out, er, release, t->dim);
        small(a, ctx, nk_rect(er.x + 2, er.y + 1, er.w, 12), "filter env", t->dim);
    }
    nk_layout_row_dynamic(ctx, 18, 2);
    static const char *LEFT[] = { "filter.cutoff_hz", "filter.resonance", "filter.drive", "filter.env_amount",
                                  "filter.keytrack" };
    static const char *RIGHT[] = { "filter_env.attack_ms", "filter_env.decay_ms", "filter_env.sustain",
                                   "filter_env.release_ms", nullptr };
    for (int i = 0; i < 5; i++) {
        pb_field_control(a, ctx, LEFT[i], false);
        if (RIGHT[i]) pb_field_control(a, ctx, RIGHT[i], false);
        else nk_spacing(ctx, 1);
    }
}

static void glas_module(pb_app *a, struct nk_context *ctx, struct nk_rect bounds) {
    const pb_probe_result *r = a->shown;
    const wb_theme *t = pb_theme(a);
    header(a, ctx, PB_MOD_GLAS, nullptr);
    struct nk_rect pr = picture(a, ctx, bounds.h * .42f);
    struct nk_command_buffer *out = nk_window_get_canvas(ctx);
    ma_patch *p = pb_authored(a);
    /* 100 Hz .. 6.4 kHz, log. */
    float lo = 100, span = log2f(64);
#define GX(hz) (pr.x + pr.w * log2f(fmaxf((hz), lo) / lo) / span)
    if (r) {
        for (int n = 1; r->f0 * n < lo * 64; n++)
            if (r->f0 * n >= lo) nk_stroke_line(out, GX(r->f0 * n), pr.y + pr.h * .35f, GX(r->f0 * n), pr.y + pr.h, 1, t->line);
        static const float GAIN[MA_FORMANTS] = { 1.0f, 0.63f, 0.25f, 0.14f };
        for (int k = 0; k < MA_FORMANTS; k++) {
            float hz = r->glas_centre_hz[k];
            if (!(hz > 0)) continue;
            float x = GX(hz), h = pr.h * .8f * GAIN[k], w = pr.w * .06f;
            nk_fill_triangle(out, x - w, pr.y + pr.h, x + w, pr.y + pr.h, x, pr.y + pr.h - h,
                             pb_alpha(t->live, p->formant_mix > 0 ? 160 : 60));
            if (p->formant_orbita > 0) {
                struct nk_rect moon = nk_rect(x - 4, pr.y + 6, 8, 8);
                if (r->glas_captured[k] >= 0) nk_fill_circle(out, moon, t->steal_released);
                else nk_stroke_circle(out, moon, 1, t->steal_released);
            }
        }
    }
#undef GX
    small(a, ctx, nk_rect(pr.x + 2, pr.y + 1, pr.w, 12), "grains: before pressure; bank: after ladder", t->dim);
    float dx = 0, dy = 0;
    bool began = false;
    if (pb_drag_area(&a->ui, ctx, 1300, pr, &dx, &dy, &began)) {
        if (began) pb_begin_edit(a, "vowel");
        float v = p->formant_vowel + dx / pr.w;
        p->formant_vowel = v < 0 ? 0 : v > 1 ? 1 : v;
        if (dx != 0) pb_edited(a);
    }
    nk_layout_row_dynamic(ctx, 18, 1);
    pb_field_control(a, ctx, "glas.formant_mix", false);
    pb_field_control(a, ctx, "glas.formant_vowel", false);
    struct nk_rect vr;
    nk_layout_row_dynamic(ctx, 12, 1);
    if (nk_widget(&vr, ctx)) {
        float label_w = vr.w * .32f;
        static const char *V[] = { "u", "o", "a", "e", "i" };
        for (int i = 0; i < 5; i++)
            small(a, ctx, nk_rect(vr.x + label_w + (vr.w - label_w) * (float)i / 4 - (i == 4 ? 8 : 0), vr.y, 12, 12), V[i],
                  t->dim);
    }
    nk_layout_row_dynamic(ctx, 18, 1);
    pb_field_control(a, ctx, "glas.formant_shift", false);
    pb_field_control(a, ctx, "glas.formant_orbita", false);
    pb_field_control(a, ctx, "glas.grain_mix", false);
    char text[96];
    snprintf(text, sizeof text, "sounding: captures %llu releases %llu stolen %llu",
             (unsigned long long)a->meters.glas_captures, (unsigned long long)a->meters.glas_releases,
             (unsigned long long)a->meters.glas_stolen);
    nk_label_colored(ctx, text, NK_TEXT_LEFT, t->dim);
}

static void amp_module(pb_app *a, struct nk_context *ctx, struct nk_rect bounds) {
    const pb_probe_result *r = a->shown;
    const wb_theme *t = pb_theme(a);
    header(a, ctx, PB_MOD_AMP, nullptr);
    float ph = bounds.h * .45f;
    nk_layout_row_begin(ctx, NK_DYNAMIC, ph, 2);
    nk_layout_row_push(ctx, .72f);
    struct nk_rect pr = { 0 }, vr = { 0 };
    if (nk_widget(&pr, ctx)) pb_frame(nk_window_get_canvas(ctx), pr, t);
    nk_layout_row_push(ctx, .28f);
    if (nk_widget(&vr, ctx)) pb_frame(nk_window_get_canvas(ctx), vr, t);
    nk_layout_row_end(ctx);
    struct nk_command_buffer *out = nk_window_get_canvas(ctx);
    ma_patch *p = pb_authored(a);
    if (r && r->note_ready && r->note_count > 1) {
        /* The envelope code's own curve, on a real time axis. */
        pb_trace(out, pr, r->note_amp, r->note_count, 0, 1.05f, t->live, 1.5f);
        float sec = r->note_seconds;
        float xa = p->amp_adsr.attack_ms / 1000 / sec, xd = (p->amp_adsr.attack_ms + p->amp_adsr.decay_ms) / 1000 / sec;
        float xr = r->note_release_at / sec, ys = 1 - p->amp_adsr.sustain / 1.05f;
        struct { float x, y; } h[3] = { { xa, 1 - 1 / 1.05f }, { xd, ys }, { xr, ys } };
        for (int k = 0; k < 3; k++)
            nk_fill_circle(out, nk_rect(pr.x + pr.w * fminf(h[k].x, 1) - 4, pr.y + pr.h * h[k].y - 4, 8, 8), t->steal_released);
        char text[48];
        snprintf(text, sizeof text, "%.2f s", sec);
        small(a, ctx, nk_rect(pr.x + pr.w - 50, pr.y + pr.h - 13, 50, 12), text, t->dim);
        /* Handles: drag near one; time handles move on a log scale. */
        static int handle = -1;
        float dx = 0, dy = 0;
        bool began = false;
        if (pb_drag_area(&a->ui, ctx, 1400, pr, &dx, &dy, &began)) {
            if (began) {
                pb_begin_edit(a, "amp envelope");
                float mx = (ctx->input.mouse.pos.x - pr.x) / pr.w;
                float best = 1e9f;
                for (int k = 0; k < 3; k++)
                    if (fabsf(mx - h[k].x) < best) { best = fabsf(mx - h[k].x); handle = k; }
            }
            float factor = powf(2, dx / 40);
            if (handle == 0) p->amp_adsr.attack_ms = fminf(20000, fmaxf(1, p->amp_adsr.attack_ms * factor));
            else if (handle == 1) {
                p->amp_adsr.decay_ms = fminf(20000, fmaxf(1, p->amp_adsr.decay_ms * factor));
                p->amp_adsr.sustain = fminf(1, fmaxf(0, p->amp_adsr.sustain - dy / pr.h));
            } else if (handle == 2) {
                p->amp_adsr.release_ms = fminf(20000, fmaxf(1, p->amp_adsr.release_ms * factor));
                p->amp_adsr.sustain = fminf(1, fmaxf(0, p->amp_adsr.sustain - dy / pr.h));
            }
            if (dx != 0 || dy != 0) pb_edited(a);
        }
    }
    /* Inset: velocity to level and to filter, probe velocity marked. */
    static float lv[128], fv[128];
    for (int v = 0; v < 128; v++) {
        lv[v] = pb_velocity_gain((uint8_t)v);
        fv[v] = ma_velocity_filter((uint8_t)v);
    }
    pb_trace(out, vr, lv, 128, 0, 1.05f, t->live, 1);
    pb_trace(out, vr, fv, 128, 0, 1.05f, t->cached, 1);
    grid_x(out, vr, a->bench.probe.velocity / 127.0f, t->steal_released);
    small(a, ctx, nk_rect(vr.x + 2, vr.y + 1, vr.w, 12), "vel: lvl, flt", t->dim);
    nk_layout_row_dynamic(ctx, 18, 1);
    pb_field_control(a, ctx, "amp.attack_ms", false);
    pb_field_control(a, ctx, "amp.decay_ms", false);
    pb_field_control(a, ctx, "amp.sustain", false);
    pb_field_control(a, ctx, "amp.release_ms", false);
}

static void output_module(pb_app *a, struct nk_context *ctx, struct nk_rect bounds) {
    const pb_probe_result *r = a->shown;
    const wb_theme *t = pb_theme(a);
    header(a, ctx, PB_MOD_OUTPUT, nullptr);
    float ph = bounds.h * .42f;
    nk_layout_row_dynamic(ctx, ph, 2);
    struct nk_rect br = { 0 }, sr = { 0 };
    if (nk_widget(&br, ctx)) pb_frame(nk_window_get_canvas(ctx), br, t);
    if (nk_widget(&sr, ctx)) pb_frame(nk_window_get_canvas(ctx), sr, t);
    struct nk_command_buffer *out = nk_window_get_canvas(ctx);
    if (r && r->body_count > 1) {
        float m = fmaxf(peak_of(r->body_in, r->body_count), peak_of(r->body_out, r->body_count)) * 1.1f + 1e-6f;
        float diag[2] = { -m, m };
        pb_xy(out, br, diag, diag, 2, -m, m, -m, m, t->line, 1);
        pb_xy(out, br, r->body_in, r->body_out, r->body_count, -m, m, -m, m, t->live, 1.5f);
        float knee = .98f;
        if (knee < m) {
            float y = br.y + br.h * (1 - (knee + m) / (2 * m));
            nk_stroke_line(out, br.x, y, br.x + br.w, y, 1, t->clip);
        }
        small(a, ctx, nk_rect(br.x + 2, br.y + 1, br.w, 12), "body in/out", t->dim);
    }
    /* L/R vector scope of the live output. */
    if (a->engine && a->audio_on) {
        static float lr[2 * 1024], xs[1024], ys[1024];
        pb_engine_scope(a->engine, lr, 1024);
        for (int i = 0; i < 1024; i++) {
            xs[i] = (lr[2 * i] - lr[2 * i + 1]) * .7071f;
            ys[i] = (lr[2 * i] + lr[2 * i + 1]) * .7071f;
        }
        float m = fmaxf(peak_of(ys, 1024), peak_of(xs, 1024)) * 1.1f + 1e-4f;
        pb_xy(out, sr, xs, ys, 1024, -m, m, -m, m, pb_alpha(t->live, 160), 1);
    }
    small(a, ctx, nk_rect(sr.x + 2, sr.y + 1, sr.w, 12), "L/R (live)", t->dim);
    nk_layout_row_dynamic(ctx, 18, 1);
    pb_field_control(a, ctx, "output.body_drive", false);
    pb_field_control(a, ctx, "output.width", false);
    pb_field_control(a, ctx, "output.crossfeed", false);
    pb_field_control(a, ctx, "output.master", false);
    char text[96];
    snprintf(text, sizeof text, "knee %llu  sanit. %llu  pre %.2f post %.2f", (unsigned long long)a->meters.knee_hits,
             (unsigned long long)a->meters.sanitizations, (double)a->meters.pre_peak, (double)a->meters.post_peak);
    nk_label_colored(ctx, text, NK_TEXT_LEFT, t->dim);
}

static void voice_module(pb_app *a, struct nk_context *ctx, struct nk_rect bounds) {
    const wb_theme *t = pb_theme(a);
    header(a, ctx, PB_MOD_VOICE, nullptr);
    pb_voice *v = &pb_channel_now(a)->voice;
    char note[48];
    snprintf(note, sizeof note, "channel %u, not saved in the patch", a->bench.selected + 1);
    nk_layout_row_dynamic(ctx, 14, 1);
    nk_label_colored(ctx, note, NK_TEXT_LEFT, t->steal_released);
    /* Twelve rows and no picture to give up: the rows themselves get lower
     * on a short window (header 18, note 14, frame and padding 16). */
    float row_h = floorf((bounds.h - 18 - 14 - 16) / 12);
    nk_layout_row_dynamic(ctx, row_h > 18 ? 18 : row_h < 14 ? 14 : row_h, 1);
    float limit = v->limit, unison = v->unison;
    if (bench_control(a, ctx, 2000, "limit", &limit, (bench_scale){ 1, 64, false, true, "" }, 8)) {
        unsigned u = v->unison, l = (unsigned)limit;
        l = l < u ? u : l / u * u;
        v->limit = (uint8_t)l;
    }
    if (bench_control(a, ctx, 2001, "unison", &unison, (bench_scale){ 1, 8, false, true, "" }, 1)) {
        unsigned u = (unsigned)unison;
        u = u >= 8 ? 8 : u >= 4 ? 4 : u >= 2 ? 2 : 1;
        v->unison = (uint8_t)u;
        if (v->limit % u) v->limit = (uint8_t)(v->limit < u ? u : v->limit / u * u);
    }
    bench_control(a, ctx, 2002, "detune", &v->detune_cents, (bench_scale){ 0, 100, false, false, " c" }, 0);
    bench_control(a, ctx, 2003, "char", &v->character, (bench_scale){ 0, 1, false, false, "" }, 0);
    bench_control(a, ctx, 2004, "glide", &v->glide_s, (bench_scale){ 0, 10, false, false, " s" }, 0);
    bench_control(a, ctx, 2005, "LFO", &v->lfo_depth, (bench_scale){ 0, 1, false, false, "" }, 0);
    bench_control(a, ctx, 2006, "LFO Hz", &v->lfo_rate_hz, (bench_scale){ 0, 20, false, false, " Hz" }, 5);
    bench_control(a, ctx, 2007, "chorus", &v->chorus_mix, (bench_scale){ 0, 1, false, false, "" }, 0);
    bench_control(a, ctx, 2008, "ch depth", &v->chorus_depth, (bench_scale){ 0, 1, false, false, "" }, .5);
    bench_control(a, ctx, 2009, "reverb", &v->reverb_mix, (bench_scale){ 0, 1, false, false, "" }, 0);
    bench_control(a, ctx, 2010, "rv size", &v->reverb_size, (bench_scale){ 0, 1, false, false, "" }, .5);
    bench_control(a, ctx, 2011, "gain", &v->gain, (bench_scale){ .01, 64, true, false, "dB" }, 16);
}

void pb_draw_module(pb_app *a, struct nk_context *ctx, pb_module m, struct nk_rect r) {
    const char *name = pb_module_name(m);
    nk_window_set_bounds(ctx, name, r);
    if (nk_begin(ctx, name, r, NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
        a->module_drawing = m;
        a->module_picture_done = false;
        switch (m) {
        case PB_MOD_VCO1:
        case PB_MOD_VCO2: vco_module(a, ctx, m, r); break;
        case PB_MOD_SYNC: sync_module(a, ctx, r); break;
        case PB_MOD_RASTER: raster_module(a, ctx, r); break;
        case PB_MOD_MOZAIK: mozaik_module(a, ctx, r); break;
        case PB_MOD_MIXER: mixer_module(a, ctx, r); break;
        case PB_MOD_BCS: bcs_module(a, ctx, r); break;
        case PB_MOD_FILTER: filter_module(a, ctx, r); break;
        case PB_MOD_GLAS: glas_module(a, ctx, r); break;
        case PB_MOD_AMP: amp_module(a, ctx, r); break;
        case PB_MOD_OUTPUT: output_module(a, ctx, r); break;
        case PB_MOD_VOICE: voice_module(a, ctx, r); break;
        case PB_MOD_MACROS:
        case PB_MOD_COUNT: break;
        }
        /* How far the last row reaches past the frame; next frame the first
         * picture is that much lower (and higher again when there is room). */
        const struct nk_panel *panel = nk_window_get_panel(ctx);
        float over = panel->at_y + panel->row.height + 2 - (panel->bounds.y + panel->bounds.h);
        float shrink = a->module_shrink[m] + over;
        a->module_shrink[m] = shrink < 0 ? 0 : shrink > r.h ? r.h : shrink;
    }
    nk_end(ctx);
}

/* ---- channels ------------------------------------------------------------------------ */

/* The sequencer's channels: two rows each.  The name selects the channel
 * (its patch, voice setup and pattern go on the desk); M mutes its pattern,
 * S plays it alone.  Below, its gain, and its voices over a peak meter. */
void pb_draw_channels(pb_app *a, struct nk_context *ctx, struct nk_rect r) {
    nk_window_set_bounds(ctx, "CHANNELS", r);
    if (!nk_begin(ctx, "CHANNELS", r, NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
        nk_end(ctx);
        return;
    }
    const wb_theme *t = pb_theme(a);
    const pb_meters *m = &a->meters;
    float inner = r.w - 2 * ctx->style.window.padding.x - 2 * ctx->style.window.spacing.x - 2;
    /* Two rows a channel; a short window gets lower rows and no gaps. */
    float name_h = r.h < 184 ? 16 : 18, gain_h = r.h < 184 ? 14 : 16;
    float gap = floorf((r.h - 12 - PB_CHANNELS * (name_h + gain_h + 8)) / PB_CHANNELS);
    for (unsigned c = 0; c < PB_CHANNELS; c++) {
        pb_channel *ch = &a->bench.channel[c];
        bool selected = c == a->bench.selected;
        char text[64];
        snprintf(text, sizeof text, "%u  %.40s%s", c + 1, pb_channel_name(a, c), pb_channel_dirty(a, c) ? " *" : "");
        nk_layout_row_begin(ctx, NK_STATIC, name_h, 3);
        nk_layout_row_push(ctx, inner - 2 * 30);
        if (nk_select_label(ctx, text, NK_TEXT_LEFT, selected) && !selected) a->channel_request = (int)c;
        nk_layout_row_push(ctx, 26);
        bool mute = ch->mute, solo = a->channel_solo[c];
        if (pb_toggle(ctx, t, "M", &mute)) ch->mute = mute;
        nk_layout_row_push(ctx, 26);
        if (pb_toggle(ctx, t, "S", &solo)) a->channel_solo[c] = solo;
        nk_layout_row_end(ctx);

        nk_layout_row_begin(ctx, NK_STATIC, gain_h, 2);
        nk_layout_row_push(ctx, inner - 58);
        bench_control(a, ctx, 2100 + (int)c, "gain", &ch->voice.gain, (bench_scale){ .01, 64, true, false, "dB" }, 16);
        nk_layout_row_push(ctx, 54);
        struct nk_rect b;
        if (nk_widget(&b, ctx)) {
            struct nk_command_buffer *out = nk_window_get_canvas(ctx);
            float peak = a->audio_on ? m->channel_peak[c] : 0;
            float u = peak > 0 ? (20 * log10f(peak) + 60) / 60 : 0;
            u = u < 0 ? 0 : u > 1 ? 1 : u;
            nk_fill_rect(out, b, 0, t->background);
            nk_fill_rect(out, nk_rect(b.x, b.y, b.w * u, b.h), 0, pb_alpha(peak > .98f ? t->clip : t->live, 150));
            snprintf(text, sizeof text, "%u/%u", m->channel_voices[c], ch->voice.limit);
            pb_text(ctx, nk_rect(b.x + 3, b.y, b.w - 3, b.h), text, t->text);
        }
        nk_layout_row_end(ctx);
        if (gap >= 2 && c + 1 < PB_CHANNELS) {
            nk_layout_row_dynamic(ctx, gap > 8 ? 8 : gap, 1);
            nk_spacing(ctx, 1);
        }
    }
    nk_end(ctx);
}

/* ---- macros -------------------------------------------------------------------------- */

void pb_draw_macros(pb_app *a, struct nk_context *ctx, struct nk_rect r) {
    nk_window_set_bounds(ctx, "MACROS", r);
    if (!nk_begin(ctx, "MACROS", r, NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
        nk_end(ctx);
        return;
    }
    const wb_theme *t = pb_theme(a);
    nk_layout_row_begin(ctx, NK_DYNAMIC, r.h - 10, 2);
    nk_layout_row_push(ctx, .55f);
    if (nk_group_begin(ctx, "macro sliders", NK_WINDOW_NO_SCROLLBAR)) {
        nk_layout_row_dynamic(ctx, 18, 3);
        static const char *M[] = { "macro.gravitacija", "macro.bloom", "macro.heat", "macro.ruin", "macro.swarm" };
        for (int i = 0; i < 5; i++) pb_field_control(a, ctx, M[i], false);
        nk_spacing(ctx, 1);
        /* Snapshots (click recalls, Ctrl+click stores; Alt+1..8, Ctrl+1..8),
         * the morph between two of them, and A to B. */
        nk_layout_row_begin(ctx, NK_STATIC, 18, 14);
        nk_layout_row_push(ctx, 46);
        if (nk_button_label(ctx, a->current ? "B>A" : "A>B")) {
            a->slot[!a->current] = *pb_authored(a);
            a->rack[!a->current] = *pb_authored_rack(a);
            a->slot_valid[!a->current] = true;
            pb_say(a, false, "copied %c to %c", a->current ? 'B' : 'A', a->current ? 'A' : 'B');
        }
        nk_layout_row_push(ctx, 40);
        nk_label_colored(ctx, "snap", NK_TEXT_RIGHT, t->dim);
        bool ctrl = ctx->input.keyboard.keys[NK_KEY_CTRL].down;
        for (unsigned k = 0; k < PB_SNAPSHOTS; k++) {
            char label[8];
            snprintf(label, sizeof label, a->bench.snapshot_used[k] ? "[%u]" : "%u", k + 1);
            nk_layout_row_push(ctx, 26);
            if (nk_button_label(ctx, label)) {
                if (ctrl || !a->bench.snapshot_used[k]) pb_snapshot_store(a, k);
                else pb_snapshot_recall(a, k);
            }
        }
        nk_layout_row_push(ctx, 150);
        float morph = (float)a->morph;
        nk_property_float(ctx, "morph", 0, &morph, 1, .01f, .005f);
        a->morph = morph;
        nk_layout_row_push(ctx, 70);
        a->morph_a = (unsigned)nk_propertyi(ctx, "a", 1, (int)a->morph_a + 1, PB_SNAPSHOTS, 1, 1) - 1;
        nk_layout_row_push(ctx, 70);
        a->morph_b = (unsigned)nk_propertyi(ctx, "b", 1, (int)a->morph_b + 1, PB_SNAPSHOTS, 1, 1) - 1;
        nk_layout_row_end(ctx);
        nk_group_end(ctx);
    }
    nk_layout_row_push(ctx, .45f);
    if (nk_group_begin(ctx, "identity", NK_WINDOW_NO_SCROLLBAR)) {
        const pb_probe_result *pr = a->shown;
        if (pr) {
            const ma_identity *id = &pr->identity;
            struct { const char *name; float v; } x[] = {
                { "H open", id->horizont_open }, { "H air", id->horizont_air }, { "H span", id->horizont_span },
                { "P mass", id->pec_mass }, { "P heat", id->pec_heat }, { "P press", id->pec_pressure },
                { "B ready", id->baklja_ready }, { "B edge", id->baklja_edge }, { "B sync", id->baklja_sync_bias },
                { "mass", id->mass }, { "strain", id->strain }, { "headroom", id->headroom },
                { "focus", id->body_focus }, { "rupture", id->rupture_threshold }, { "disp", id->spatial_dispersion },
            };
            nk_layout_row_dynamic(ctx, 14, 5);
            for (size_t i = 0; i < sizeof x / sizeof *x; i++) {
                struct nk_rect b;
                if (!nk_widget(&b, ctx)) continue;
                struct nk_command_buffer *out = nk_window_get_canvas(ctx);
                float u = x[i].v < 0 ? 0 : x[i].v > 1 ? 1 : x[i].v;
                struct nk_rect bar = nk_rect(b.x + b.w * .55f, b.y + 3, b.w * .43f, b.h - 6);
                nk_fill_rect(out, bar, 0, t->background);
                struct nk_color c = i < 3 ? t->live : i < 6 ? t->steal_released : i < 9 ? t->clip : t->cached;
                nk_fill_rect(out, nk_rect(bar.x, bar.y, bar.w * u, bar.h), 0, c);
                pb_text(ctx, nk_rect(b.x, b.y, b.w * .55f, b.h), x[i].name, t->dim);
            }
        }
        nk_group_end(ctx);
    }
    nk_layout_row_end(ctx);
    nk_end(ctx);
}

/* ---- the top bar ------------------------------------------------------------------------ */

static const char *note_name(int note, char *buffer, size_t size) {
    static const char *N[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    snprintf(buffer, size, "%s%d", N[note % 12], note / 12 - 1);
    return buffer;
}

void pb_draw_bar(pb_app *a, struct nk_context *ctx, struct nk_rect r) {
    nk_window_set_bounds(ctx, "bar", r);
    if (!nk_begin(ctx, "bar", r, NK_WINDOW_NO_SCROLLBAR)) {
        nk_end(ctx);
        return;
    }
    const wb_theme *t = pb_theme(a);
    char text[400];
    nk_layout_row_begin(ctx, NK_STATIC, 22, 24);
    nk_layout_row_push(ctx, 240);
    snprintf(text, sizeof text, "%u: %s%s  v", a->bench.selected + 1, a->name, a->dirty ? " *" : "");
    if (nk_button_label(ctx, text)) {
        a->library_open = !a->library_open;
        if (a->library_open) pb_library_scan(a);
    }
    nk_layout_row_push(ctx, 220);
    snprintf(text, sizeof text, "parent: %.200s", a->has_parent ? a->parent_ref : a->factory ? "(factory file)" : "none");
    nk_label_colored(ctx, text, NK_TEXT_LEFT, t->dim);
    nk_layout_row_push(ctx, 24);
    if (nk_select_label(ctx, "A", NK_TEXT_CENTERED, a->current == 0) && a->current != 0) pb_swap_ab(a);
    nk_layout_row_push(ctx, 24);
    if (nk_select_label(ctx, "B", NK_TEXT_CENTERED, a->current == 1) && a->current != 1) pb_swap_ab(a);
    nk_layout_row_push(ctx, 50);
    if (nk_button_label(ctx, "undo") && pb_undo_undo(a->undo, pb_authored(a), pb_authored_rack(a))) pb_edited(a);
    nk_layout_row_push(ctx, 50);
    if (nk_button_label(ctx, "redo") && pb_undo_redo(a->undo, pb_authored(a), pb_authored_rack(a))) pb_edited(a);
    nk_layout_row_push(ctx, 50);
    if (nk_button_label(ctx, "reset")) pb_reset(a);
    nk_layout_row_push(ctx, 50);
    if (nk_button_label(ctx, "save")) pb_save(a);
    nk_layout_row_push(ctx, 60);
    if (nk_button_label(ctx, "derive")) pb_derive_child(a);
    nk_layout_row_push(ctx, 60);
    if (nk_button_label(ctx, "flatten")) pb_flatten(a);
    nk_layout_row_push(ctx, 60);
    if (nk_button_label(ctx, "bounce")) pb_bounce_now(a);
    nk_layout_row_push(ctx, 18);
    if (nk_button_label(ctx, "<")) a->bench.probe.note = (uint8_t)(a->bench.probe.note >= 12 ? a->bench.probe.note - 12 : 0);
    nk_layout_row_push(ctx, 110);
    char nn[8];
    snprintf(text, sizeof text, "probe %s v%u", note_name(a->bench.probe.note, nn, sizeof nn), a->bench.probe.velocity);
    nk_label_colored(ctx, text, NK_TEXT_CENTERED, t->text);
    nk_layout_row_push(ctx, 18);
    if (nk_button_label(ctx, ">") && a->bench.probe.note <= 115) a->bench.probe.note += 12;
    nk_layout_row_push(ctx, 50);
    bool pin = a->bench.probe.pinned;
    if (pb_toggle(ctx, t, "pin", &pin)) a->bench.probe.pinned = pin;
    nk_layout_row_push(ctx, 120);
    static const char *SCHED[] = { "normal", "nice", "FIFO", "RT" };
    snprintf(text, sizeof text, "audio %s %s", a->audio_on ? "on" : "off",
             a->audio_on && a->engine ? SCHED[atomic_load(&a->engine->scheduling)] : "off");
    if (nk_button_label(ctx, text)) pb_audio_toggle(a);
    nk_layout_row_push(ctx, 300);
    const pb_meters *m = &a->meters;
    snprintf(text, sizeof text, "%u/%u voices (%u all)  DSP %.0f%%  %llu xr  %llu steals",
             m->channel_voices[a->bench.selected], pb_channel_now(a)->voice.limit, m->active_voices,
             m->period_ms > 0 ? 100 * m->budget_ms / m->period_ms : 0.0, (unsigned long long)m->xruns,
             (unsigned long long)m->steals);
    nk_label_colored(ctx, text, NK_TEXT_LEFT, m->xruns ? t->steal_released : t->dim);
    nk_layout_row_push(ctx, 120);
    float peak = fmaxf(m->peak_left, m->peak_right);
    snprintf(text, sizeof text, "%s %.1f dBFS", m->muted ? "MUTED" : "peak", peak > 0 ? 20 * log10f(peak) : -INFINITY);
    nk_label_colored(ctx, text, NK_TEXT_LEFT, m->muted || peak > .98f ? t->clip : t->text);
    nk_layout_row_push(ctx, 80);
    bool latch = a->latch;
    if (pb_toggle(ctx, t, "latch", &latch)) a->latch = latch;
    nk_layout_row_end(ctx);
    nk_end(ctx);
}
