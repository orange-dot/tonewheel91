/* The insert rack row (RK1) and the parametric EQ editor (RK1b),
 * docs/ma-rack-plan.md.  The row shows the eight slots in chain order and a
 * compact editor for the selected block; the EQ editor takes the place of
 * the two module rows.  Every EQ curve is evaluated from the coefficients
 * the core computed for the block (a private rack, settled, one tick), not
 * from a formula of its own; the DSP loop itself is refereed against the
 * analytic response in test/ma_rack.c.  Edits go through the same undo
 * steps as the patch. */
#include "pb_app.h"

#include <complex.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../driver/ma_patch_file.h"
#include "pb_fft.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

enum {
    CURVE_MAX = 1024, ID_BASE = 3000, ID_SLOT = 64, ID_MIX = 60, ID_DRAG = 3600, ID_EQ = 3700,
};
static const double LO_HZ = 20, HI_HZ = 20000;
static const float RANGES[] = { 6, 12, 24, 36 };

/* ---- the block's response, from its own coefficients ----------------------------- */

/* H of one TPT SVF from its coefficients: g = a2/a1, k from a1. */
static double complex svf_h(const ma_rack_svf *f, double hz, double fs) {
    double a1 = f->a1, g = f->a2 / f->a1, k = (1 / a1 - 1) / g - g;
    double complex s = I * tan(M_PI * fmin(hz, .4999 * fs) / fs) / g;
    double complex den = s * s + k * s + 1;
    return f->m0 + f->m1 * s / den + f->m2 / den;
}

typedef struct {
    ma_rack_slot_spec spec;
    int band;                 /* -1: the whole block */
    float rate;
    unsigned n;
    bool valid;
    float db[CURVE_MAX];
} curve_cache;

static curve_cache thumbs[MA_RACK_SLOTS], big_sum, big_band;

static double point_hz(unsigned i, unsigned n) { return LO_HZ * pow(HI_HZ / LO_HZ, (double)i / (n - 1)); }

/* The block alone in a private rack (or one band of it), settled, ticked
 * once so the core computes every band's coefficients, then evaluated at n
 * log-spaced points. Mix and output gain are included. */
static const float *curve(curve_cache *c, const ma_rack_slot_spec *slot, int band, float rate, unsigned n) {
    if (n > CURVE_MAX) n = CURVE_MAX;
    if (n < 2) n = 2;
    if (c->valid && c->band == band && c->rate == rate && c->n == n && !memcmp(&c->spec, slot, sizeof *slot))
        return c->db;
    static ma_rack probe;
    ma_rack_spec spec = { 0 };
    spec.slot[0] = *slot;
    spec.slot[0].enabled = true;      /* show what the block does when on */
    if (band >= 0) {
        ma_rack_slot_spec *s = &spec.slot[0];
        float fields[MA_EQ_BAND_FIELDS];
        for (unsigned f = 0; f < MA_EQ_BAND_FIELDS; f++) fields[f] = s->param[MA_EQ_BAND(band, f)];
        ma_rack_slot_init(s, MA_RACK_EQ, slot->id);
        s->param[MA_EQ_BAND_COUNT] = 1;
        for (unsigned f = 0; f < MA_EQ_BAND_FIELDS; f++) s->param[MA_EQ_BAND(0, f)] = fields[f];
        s->param[MA_EQ_BAND(0, MA_EQ_ON)] = 1;
    }
    ma_rack_init(&probe, rate);
    c->valid = false;
    if (!ma_rack_apply(&probe, &spec)) return nullptr;
    ma_rack_settle(&probe);
    (void)ma_rack_tick(&probe, (ma_frame){ 0, 0 });
    const ma_rack_block *b = &probe.block[0];
    const ma_rack_slot_spec *s = &spec.slot[0];
    double out = pow(10, s->param[MA_EQ_OUTPUT_DB] / 20), mix = s->mix;
    for (unsigned i = 0; i < n; i++) {
        double hz = point_hz(i, n);
        double complex h = 1;
        if (s->kind == MA_RACK_EQ) {
            for (unsigned k = 0; k < MA_EQ_BANDS; k++)
                if (b->svf[k].active && b->svf[k].weight > 0) h *= svf_h(&b->svf[k], hz, rate);
            h *= out;
        }
        h = 1 + mix * (h - 1);
        double m = cabs(h);
        c->db[i] = (float)(20 * log10(m > 1e-9 ? m : 1e-9));
    }
    c->spec = *slot;
    c->band = band;
    c->rate = rate;
    c->n = n;
    c->valid = true;
    return c->db;
}

/* ---- small helpers ----------------------------------------------------------- */

static unsigned band_count(const ma_rack_slot_spec *s) {
    return s->kind == MA_RACK_EQ ? (unsigned)s->param[MA_EQ_BAND_COUNT] : 0;
}
static float bandv(const ma_rack_slot_spec *s, unsigned b, unsigned field) { return s->param[MA_EQ_BAND(b, field)]; }
static bool gain_shape(int type) { return type <= MA_EQ_HIGH_SHELF; }

static bool neutral(const ma_rack_slot_spec *s) {
    if (s->kind == MA_RACK_EQ) {
        if (s->param[MA_EQ_OUTPUT_DB] != 0) return false;
        for (unsigned b = 0; b < band_count(s); b++)
            if (bandv(s, b, MA_EQ_ON) && !(gain_shape((int)bandv(s, b, MA_EQ_TYPE)) && bandv(s, b, MA_EQ_GAIN_DB) == 0))
                return false;
        return true;
    }
    if (s->kind == MA_RACK_UTILITY)
        return !s->param[MA_UTILITY_GAIN_DB] && s->param[MA_UTILITY_WIDTH] == 1 && !s->param[MA_UTILITY_PAN];
    return true;
}

/* Lit while the block hands its input on unchanged (RKD3). */
static bool bypassed(const pb_app *a, unsigned i, const ma_rack_slot_spec *s) {
    return !s->enabled || s->mix == 0 || neutral(s) || a->rack_mute[i];
}

static uint32_t next_id(pb_app *a) {
    uint32_t top = a->rack_next_id;
    for (unsigned k = 0; k < 2; k++)
        for (unsigned i = 0; i < MA_RACK_SLOTS; i++)
            if (a->rack[k].slot[i].id > top) top = a->rack[k].slot[i].id;
    a->rack_next_id = top + 1;
    return a->rack_next_id;
}

static const char *kind_label(ma_rack_kind k) {
    return k == MA_RACK_EQ ? "EQ" : k == MA_RACK_UTILITY ? "Utility" : "empty";
}

static const char *const TYPE_LABEL[MA_EQ_TYPE_COUNT] = {
    [MA_EQ_BELL] = "bell", [MA_EQ_LOW_SHELF] = "low shelf", [MA_EQ_HIGH_SHELF] = "high shelf",
    [MA_EQ_LOW_CUT] = "low cut", [MA_EQ_HIGH_CUT] = "high cut", [MA_EQ_NOTCH] = "notch",
};

/* Eight hues of middle lightness, distinct on the dark and the light theme. */
static struct nk_color band_color(const wb_theme *t, unsigned b) {
    (void)t;
    static const struct nk_color c[8] = {
        { 86, 156, 214, 255 }, { 226, 132, 64, 255 }, { 96, 186, 96, 255 }, { 214, 84, 84, 255 },
        { 168, 116, 214, 255 }, { 58, 182, 182, 255 }, { 212, 182, 58, 255 }, { 206, 112, 166, 255 },
    };
    return c[b % 8];
}

static void format_hz(double hz, char *t, size_t n) {
    if (hz >= 1000) snprintf(t, n, "%.2fk", hz / 1000);
    else snprintf(t, n, "%.0f", hz);
}

/* Bandwidth in octaves of a bell or notch of quality q. */
static double octaves(double q) { return 2 / log(2) * asinh(1 / (2 * q)); }

/* ---- parameter bars ------------------------------------------------------------ */

typedef struct {
    ma_rack_param_info info;
    const char *unit;              /* "Hz", "dB" or "" */
} param_scale;

static double p_to(const void *c, double x) {
    const param_scale *s = c;
    if (s->info.curve == MA_RACK_LOG) return log(x / s->info.minimum) / log(s->info.maximum / s->info.minimum);
    return (x - s->info.minimum) / (s->info.maximum - s->info.minimum);
}
static double p_from(const void *c, double u) {
    const param_scale *s = c;
    u = u < 0 ? 0 : u > 1 ? 1 : u;
    if (s->info.curve == MA_RACK_LOG) return s->info.minimum * pow(s->info.maximum / s->info.minimum, u);
    return s->info.minimum + u * (s->info.maximum - s->info.minimum);
}
static void p_format(const void *c, double x, char *t, size_t n) {
    const param_scale *s = c;
    if (!strcmp(s->unit, "Hz")) {
        if (x >= 1000) snprintf(t, n, "%.2f kHz", x / 1000);
        else snprintf(t, n, "%.0f Hz", x);
    } else if (!strcmp(s->unit, "dB")) snprintf(t, n, "%+.1f dB", x);
    else snprintf(t, n, "%.2f", x);
}
static bool p_parse(const void *c, const char *t, double *x) {
    const param_scale *s = c;
    char *end;
    double v = strtod(t, &end);
    if (end == t) return false;
    while (*end == ' ') end++;
    if (*end == 'k' || *end == 'K') v *= 1000;
    if (!(v >= s->info.minimum && v <= s->info.maximum)) return false;
    *x = v;
    return true;
}

static void begin(pb_app *a, const char *label) {
    if (a->ui.begin) {
        pb_begin_edit(a, label);
        a->ui.begin = false;
    }
}

/* One continuous parameter of slot `slot` as a bar. */
static void param_bar(pb_app *a, struct nk_context *ctx, unsigned slot, unsigned index, const char *label,
                      const char *unit) {
    ma_rack_slot_spec *s = &pb_authored_rack(a)->slot[slot];
    param_scale ps = { .unit = unit };
    (void)ma_rack_param_info_at(s->kind, index, &ps.info);
    double value = s->param[index];
    double span = ps.info.maximum - ps.info.minimum;
    double fine = ps.info.curve == MA_RACK_LOG ? ps.info.minimum * .02 : span / 200;
    pb_scale_fn fn = { p_to, p_from, p_format, p_parse, &ps, fine, fine * 10, ps.info.initial, false };
    pb_marks m = { .parent = NAN, .b = NAN, .effective = NAN, .mod_lo = NAN, .mod_hi = NAN };
    if (a->slot_valid[!a->current]) {
        const ma_rack_slot_spec *o = &a->rack[!a->current].slot[slot];
        if (o->kind == s->kind && o->id == s->id && o->param[index] != s->param[index]) m.b = o->param[index];
    }
    if (pb_bar(&a->ui, ctx, ID_BASE + (int)(slot * ID_SLOT + index), PB_MOD_COUNT, label, &value, &fn, &m)) {
        begin(a, "rack");
        if (value < ps.info.minimum) value = ps.info.minimum;
        if (value > ps.info.maximum) value = ps.info.maximum;
        s->param[index] = (float)value;
        pb_edited(a);
    }
}

static void mix_bar(pb_app *a, struct nk_context *ctx, unsigned slot) {
    ma_rack_slot_spec *s = &pb_authored_rack(a)->slot[slot];
    param_scale ps = { .info = { 0, 1, 1, MA_RACK_LINEAR }, .unit = "" };
    double value = s->mix;
    pb_scale_fn fn = { p_to, p_from, p_format, p_parse, &ps, .01, .1, 1, false };
    pb_marks m = { .parent = NAN, .b = NAN, .effective = NAN, .mod_lo = NAN, .mod_hi = NAN };
    if (pb_bar(&a->ui, ctx, ID_BASE + (int)(slot * ID_SLOT + ID_MIX), PB_MOD_COUNT, "mix", &value, &fn, &m)) {
        begin(a, "rack mix");
        s->mix = (float)(value < 0 ? 0 : value > 1 ? 1 : value);
        pb_edited(a);
    }
}

static void enabled_toggle(pb_app *a, struct nk_context *ctx, ma_rack_slot_spec *s) {
    bool on = s->enabled;
    if (pb_toggle(ctx, pb_theme(a), "enabled", &on)) {
        pb_begin_edit(a, "rack enable");
        s->enabled = on;
        pb_edited(a);
    }
}

/* ---- structure: slots ---------------------------------------------------------- */

static void set_band(ma_rack_slot_spec *s, unsigned b, ma_eq_type type, float hz, float db, float q) {
    s->param[MA_EQ_BAND(b, MA_EQ_TYPE)] = type;
    s->param[MA_EQ_BAND(b, MA_EQ_FREQ_HZ)] = hz;
    s->param[MA_EQ_BAND(b, MA_EQ_GAIN_DB)] = db;
    s->param[MA_EQ_BAND(b, MA_EQ_Q)] = q;
    s->param[MA_EQ_BAND(b, MA_EQ_ON)] = 1;
}

static void insert(pb_app *a, unsigned at, ma_rack_kind kind) {
    pb_begin_edit(a, "rack insert");
    ma_rack_slot_spec *s = &pb_authored_rack(a)->slot[at];
    ma_rack_slot_init(s, kind, next_id(a));
    if (kind == MA_RACK_EQ) {
        /* A neutral starting point: four bands at 0 dB, nodes in view. */
        s->param[MA_EQ_BAND_COUNT] = 4;
        set_band(s, 0, MA_EQ_LOW_SHELF, 100, 0, .7071f);
        set_band(s, 1, MA_EQ_BELL, 400, 0, 1);
        set_band(s, 2, MA_EQ_BELL, 2500, 0, 1);
        set_band(s, 3, MA_EQ_HIGH_SHELF, 8000, 0, .7071f);
    }
    a->rack_mute[at] = false;
    a->rack_selected = (int)at;
    a->eq_band = 0;
    pb_edited(a);
}

static void remove_slot(pb_app *a, unsigned at) {
    pb_begin_edit(a, "rack remove");
    pb_authored_rack(a)->slot[at] = (ma_rack_slot_spec){ 0 };
    a->rack_mute[at] = false;
    if (a->rack_selected == (int)at) {
        a->rack_selected = -1;
        a->eq_open = false;
    }
    pb_edited(a);
}

static void move(pb_app *a, unsigned at, int step) {
    int to = (int)at + step;
    if (to < 0 || to >= MA_RACK_SLOTS) return;
    pb_begin_edit(a, "rack move");
    ma_rack_spec *r = pb_authored_rack(a);
    ma_rack_slot_spec x = r->slot[at];
    r->slot[at] = r->slot[to];
    r->slot[to] = x;
    bool mute = a->rack_mute[at];
    a->rack_mute[at] = a->rack_mute[to];
    a->rack_mute[to] = mute;
    if (a->rack_selected == (int)at) a->rack_selected = to;
    else if (a->rack_selected == to) a->rack_selected = (int)at;
    pb_edited(a);
}

/* ---- structure: EQ bands ------------------------------------------------------- */

static ma_rack_slot_spec *selected_eq(pb_app *a) {
    if (a->rack_selected < 0) return nullptr;
    ma_rack_slot_spec *s = &pb_authored_rack(a)->slot[a->rack_selected];
    return s->kind == MA_RACK_EQ ? s : nullptr;
}

static void add_band(pb_app *a, ma_rack_slot_spec *s, float hz, float db) {
    unsigned n = band_count(s);
    if (n >= MA_EQ_BANDS) {
        pb_say(a, true, "this EQ has all %d bands; add another EQ to the rack for more", MA_EQ_BANDS);
        return;
    }
    pb_begin_edit(a, "eq add band");
    set_band(s, n, MA_EQ_BELL, hz < 10 ? 10 : hz > 22000 ? 22000 : hz, db < -24 ? -24 : db > 24 ? 24 : db, 1);
    s->param[MA_EQ_BAND_COUNT] = (float)(n + 1);
    a->eq_band = (int)n;
    pb_edited(a);
}

/* Bands above move down; the last slot goes back to its initial values,
 * which is what the spec requires past the count. */
static void remove_band(pb_app *a, ma_rack_slot_spec *s, unsigned b) {
    unsigned n = band_count(s);
    if (b >= n) return;
    pb_begin_edit(a, "eq remove band");
    for (unsigned k = b; k + 1 < n; k++)
        for (unsigned f = 0; f < MA_EQ_BAND_FIELDS; f++) s->param[MA_EQ_BAND(k, f)] = s->param[MA_EQ_BAND(k + 1, f)];
    ma_rack_slot_spec fresh;
    ma_rack_slot_init(&fresh, MA_RACK_EQ, 1);
    for (unsigned f = 0; f < MA_EQ_BAND_FIELDS; f++) s->param[MA_EQ_BAND(n - 1, f)] = fresh.param[MA_EQ_BAND(n - 1, f)];
    s->param[MA_EQ_BAND_COUNT] = (float)(n - 1);
    if (a->eq_band >= (int)n - 1) a->eq_band = (int)n - 2;
    pb_edited(a);
}

bool pb_eq_key(pb_app *a, bool remove, bool close) {
    if (!a->eq_open) return false;
    if (close) {
        a->eq_open = false;
        return true;
    }
    ma_rack_slot_spec *s = selected_eq(a);
    if (remove && s && a->eq_band >= 0 && a->eq_band < (int)band_count(s)) {
        remove_band(a, s, (unsigned)a->eq_band);
        return true;
    }
    return false;
}

/* ---- drawing helpers ----------------------------------------------------------- */

static float hz_to_x(struct nk_rect r, double hz) {
    return r.x + r.w * (float)(log(hz / LO_HZ) / log(HI_HZ / LO_HZ));
}
static double x_to_hz(struct nk_rect r, float x) {
    double u = (x - r.x) / r.w;
    return LO_HZ * pow(HI_HZ / LO_HZ, u < 0 ? 0 : u > 1 ? 1 : u);
}
static float db_to_y(struct nk_rect r, double db, float range) {
    double u = db / range;
    u = u < -1 ? -1 : u > 1 ? 1 : u;
    return r.y + r.h / 2 - (float)u * r.h / 2;
}

/* A node's height: its gain for a gain shape, else the curve there. */
static float node_db(const ma_rack_slot_spec *s, unsigned b, const float *sum, unsigned n) {
    int type = (int)bandv(s, b, MA_EQ_TYPE);
    if (gain_shape(type)) return bandv(s, b, MA_EQ_GAIN_DB);
    if (!sum) return 0;
    double u = log(bandv(s, b, MA_EQ_FREQ_HZ) / LO_HZ) / log(HI_HZ / LO_HZ);
    int i = (int)lround(u * (n - 1));
    return sum[i < 0 ? 0 : i >= (int)n ? (int)n - 1 : i];
}

static void draw_thumb(pb_app *a, struct nk_context *ctx, struct nk_rect r, unsigned slot,
                       const ma_rack_slot_spec *s) {
    const wb_theme *t = pb_theme(a);
    struct nk_command_buffer *out = nk_window_get_canvas(ctx);
    pb_frame(out, r, t);
    float zero = r.y + r.h / 2;
    nk_stroke_line(out, r.x, zero, r.x + r.w, zero, 1, t->line);
    if (s->kind == MA_RACK_EQ) {
        unsigned n = (unsigned)r.w;
        const float *db = curve(&thumbs[slot], s, -1, (float)a->rate, n);
        if (db) pb_trace(out, r, db, thumbs[slot].n, -18, 18, bypassed(a, slot, s) ? t->dim : t->live, 1);
        for (unsigned b = 0; b < band_count(s); b++) {
            float x = hz_to_x(r, bandv(s, b, MA_EQ_FREQ_HZ)), y = db_to_y(r, node_db(s, b, db, thumbs[slot].n), 18);
            nk_fill_circle(out, nk_rect(x - 2, y - 2, 4, 4), bandv(s, b, MA_EQ_ON) ? band_color(t, b) : t->dim);
        }
    } else if (s->kind == MA_RACK_UTILITY) {
        char text[64];
        snprintf(text, sizeof text, "%+.1f dB  w %.2f  pan %+.2f", (double)s->param[MA_UTILITY_GAIN_DB],
                 (double)s->param[MA_UTILITY_WIDTH], (double)s->param[MA_UTILITY_PAN]);
        pb_text(ctx, nk_rect(r.x + 4, r.y, r.w - 8, r.h), text, bypassed(a, slot, s) ? t->dim : t->text);
    }
}

/* ---- the rack row ---------------------------------------------------------------- */

static void draw_cell(pb_app *a, struct nk_context *ctx, unsigned i) {
    const wb_theme *t = pb_theme(a);
    ma_rack_slot_spec *s = &pb_authored_rack(a)->slot[i];
    char title[32];
    snprintf(title, sizeof title, "rack cell %u", i);
    if (!nk_group_begin(ctx, title, NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BORDER)) return;
    struct nk_rect area = nk_window_get_content_region(ctx);
    bool selected = a->rack_selected == (int)i;
    nk_layout_row_begin(ctx, NK_DYNAMIC, 16, 2);
    nk_layout_row_push(ctx, .75f);
    char head[32];
    if (s->kind == MA_RACK_EQ) snprintf(head, sizeof head, "%u EQ %u", i + 1, band_count(s));
    else snprintf(head, sizeof head, "%u %s", i + 1, kind_label(s->kind));
    if (nk_select_label(ctx, head, NK_TEXT_LEFT, selected) != selected && s->kind != MA_RACK_EMPTY)
        a->rack_selected = selected ? -1 : (int)i;
    nk_layout_row_push(ctx, .25f);
    if (s->kind != MA_RACK_EMPTY) {
        struct nk_rect dot;
        if (nk_widget(&dot, ctx)) {
            struct nk_command_buffer *out = nk_window_get_canvas(ctx);
            struct nk_rect d = nk_rect(dot.x + dot.w - 10, dot.y + 3, 8, 8);
            if (bypassed(a, i, s)) nk_fill_circle(out, d, t->stale);
            nk_stroke_circle(out, d, 1, t->dim);
        }
    } else nk_spacing(ctx, 1);
    nk_layout_row_end(ctx);
    float picture = area.h - 16 - 18 - 14;
    if (picture < 12) picture = 12;
    nk_layout_row_dynamic(ctx, picture, 1);
    struct nk_rect r;
    if (nk_widget(&r, ctx)) {
        if (s->kind == MA_RACK_EMPTY) pb_text(ctx, r, "  insert:", t->dim);
        else draw_thumb(a, ctx, r, i, s);
        const struct nk_input *in = &ctx->input;
        if (s->kind != MA_RACK_EMPTY && nk_input_is_mouse_click_in_rect(in, NK_BUTTON_LEFT, r))
            a->rack_selected = (int)i;
        if (s->kind == MA_RACK_EQ && nk_input_has_mouse_click_down_in_rect(in, NK_BUTTON_DOUBLE, r, nk_true)) {
            a->rack_selected = (int)i;
            a->eq_open = true;
        }
    }
    if (s->kind == MA_RACK_EMPTY) {
        nk_layout_row_dynamic(ctx, 18, 2);
        if (nk_button_label(ctx, "+EQ")) insert(a, i, MA_RACK_EQ);
        if (nk_button_label(ctx, "+Util")) insert(a, i, MA_RACK_UTILITY);
    } else {
        nk_layout_row_dynamic(ctx, 18, 4);
        if (nk_button_label(ctx, "<")) move(a, i, -1);
        if (nk_button_label(ctx, ">")) move(a, i, +1);
        bool mute = a->rack_mute[i];
        if (pb_toggle(ctx, t, "M", &mute)) a->rack_mute[i] = mute;   /* monitor only */
        if (nk_button_label(ctx, "x")) remove_slot(a, i);
    }
    nk_group_end(ctx);
}

static void draw_compact(pb_app *a, struct nk_context *ctx, struct nk_rect area) {
    const wb_theme *t = pb_theme(a);
    int sel = a->rack_selected;
    ma_rack_slot_spec *s = sel >= 0 ? &pb_authored_rack(a)->slot[sel] : nullptr;
    if (!s || s->kind == MA_RACK_EMPTY) {
        a->rack_selected = -1;
        nk_layout_row_dynamic(ctx, 18, 1);
        nk_label_colored(ctx, "Select a block, or insert one into an empty slot. The rack runs after the "
                         "output DC block, before master.", NK_TEXT_LEFT, t->dim);
        return;
    }
    nk_layout_row_begin(ctx, NK_DYNAMIC, area.h - 8, 2);
    nk_layout_row_push(ctx, .42f);
    struct nk_rect r;
    if (nk_widget(&r, ctx)) {
        draw_thumb(a, ctx, r, (unsigned)sel, s);
        if (s->kind == MA_RACK_EQ && nk_input_has_mouse_click_down_in_rect(&ctx->input, NK_BUTTON_DOUBLE, r, nk_true))
            a->eq_open = true;
    }
    nk_layout_row_push(ctx, .58f);
    if (nk_group_begin(ctx, "rack controls", NK_WINDOW_NO_SCROLLBAR)) {
        nk_layout_row_dynamic(ctx, 18, 3);
        enabled_toggle(a, ctx, s);
        mix_bar(a, ctx, (unsigned)sel);
        char id[48];
        snprintf(id, sizeof id, "%s, id %u", kind_label(s->kind), (unsigned)s->id);
        nk_label_colored(ctx, id, NK_TEXT_RIGHT, t->dim);
        if (s->kind == MA_RACK_EQ) {
            nk_layout_row_dynamic(ctx, 18, 2);
            param_bar(a, ctx, (unsigned)sel, MA_EQ_OUTPUT_DB, "output", "dB");
            char label[48];
            snprintf(label, sizeof label, "open EQ editor (%u/%d bands)", band_count(s), MA_EQ_BANDS);
            if (nk_button_label(ctx, label)) a->eq_open = true;
        } else {
            nk_layout_row_dynamic(ctx, 18, 3);
            param_bar(a, ctx, (unsigned)sel, MA_UTILITY_GAIN_DB, "gain", "dB");
            param_bar(a, ctx, (unsigned)sel, MA_UTILITY_WIDTH, "width", "");
            param_bar(a, ctx, (unsigned)sel, MA_UTILITY_PAN, "pan", "");
        }
        nk_group_end(ctx);
    }
    nk_layout_row_end(ctx);
}

void pb_draw_rack(pb_app *a, struct nk_context *ctx, struct nk_rect r) {
    nk_window_set_bounds(ctx, "RACK", r);
    if (!nk_begin(ctx, "RACK", r, NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
        nk_end(ctx);
        return;
    }
    float inner = r.h - 10;
    nk_layout_row_begin(ctx, NK_DYNAMIC, inner, 2);
    nk_layout_row_push(ctx, .56f);
    if (nk_group_begin(ctx, "rack cells", NK_WINDOW_NO_SCROLLBAR)) {
        nk_layout_row_dynamic(ctx, inner - 6, MA_RACK_SLOTS);
        for (unsigned i = 0; i < MA_RACK_SLOTS; i++) draw_cell(a, ctx, i);
        nk_group_end(ctx);
    }
    nk_layout_row_push(ctx, .44f);
    if (nk_group_begin(ctx, "rack editor", NK_WINDOW_NO_SCROLLBAR)) {
        draw_compact(a, ctx, nk_rect(0, 0, r.w * .44f, inner));
        nk_group_end(ctx);
    }
    nk_layout_row_end(ctx);
    nk_end(ctx);
}

/* ---- the EQ editor ------------------------------------------------------------------ */

static void draw_grid(pb_app *a, struct nk_context *ctx, struct nk_rect g, float range) {
    const wb_theme *t = pb_theme(a);
    struct nk_command_buffer *out = nk_window_get_canvas(ctx);
    pb_frame(out, g, t);
    static const double MAJOR[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    for (double decade = 10; decade < HI_HZ; decade *= 10)
        for (int m = 2; m <= 9; m++) {
            double hz = decade * m;
            if (hz < LO_HZ || hz > HI_HZ) continue;
            float x = hz_to_x(g, hz);
            nk_stroke_line(out, x, g.y, x, g.y + g.h, 1, pb_alpha(t->line, 70));
        }
    for (unsigned i = 0; i < sizeof MAJOR / sizeof *MAJOR; i++) {
        float x = hz_to_x(g, MAJOR[i]);
        nk_stroke_line(out, x, g.y, x, g.y + g.h, 1, t->line);
        char text[16];
        format_hz(MAJOR[i], text, sizeof text);
        pb_text(ctx, nk_rect(x + 2, g.y + g.h - 15, 44, 14), text, t->dim);
    }
    float step = range <= 6 ? 1 : range <= 12 ? 3 : 6;
    for (float db = -range + step; db < range - .01f; db += step) {   /* the edges stay unlabelled */
        float y = db_to_y(g, db, range);
        nk_stroke_line(out, g.x, y, g.x + g.w, y, 1, db == 0 ? t->dim : pb_alpha(t->line, 120));
        char text[16];
        snprintf(text, sizeof text, "%+.0f", (double)db);
        pb_text(ctx, nk_rect(g.x + 2, y - 7, 36, 14), text, t->dim);
    }
}

static void draw_analyzer(pb_app *a, struct nk_context *ctx, struct nk_rect g, float range) {
    if (!a->eq_analyzer || !a->engine || !a->audio_on) return;
    uint64_t now = SDL_GetTicks();
    if (now - a->live_db_at > 66) {
        static float lr[2 * PB_SPECTRUM], mono[PB_SPECTRUM];
        pb_engine_scope(a->engine, lr, PB_SPECTRUM);
        for (unsigned i = 0; i < PB_SPECTRUM; i++) mono[i] = lr[2 * i];
        pb_spectrum_db(mono, PB_SPECTRUM, a->live_db);
        a->live_db_at = now;
    }
    /* The output spectrum, placed so that -60 dBFS sits on the bottom edge
     * and 0 dBFS at the top: a picture of where the energy is, not a gain. */
    unsigned n = (unsigned)(g.w / 2);
    if (n > CURVE_MAX) n = CURVE_MAX;
    static float y[CURVE_MAX];
    for (unsigned i = 0; i < n; i++) {
        double hz = point_hz(i, n);
        size_t bin = (size_t)(hz * PB_SPECTRUM / a->rate);
        if (bin > PB_SPECTRUM / 2) bin = PB_SPECTRUM / 2;
        y[i] = (a->live_db[bin] + 60) / 60 * 2 * range - range;
    }
    pb_trace(nk_window_get_canvas(ctx), g, y, n, -range, range, pb_alpha(pb_theme(a)->dim, 150), 1);
}

/* Node under the pointer, within 10 px, or -1. */
static int node_at(const ma_rack_slot_spec *s, struct nk_rect g, float range, const float *sum, unsigned n,
                   struct nk_vec2 p) {
    int best = -1;
    float best_d = 100;
    for (unsigned b = 0; b < band_count(s); b++) {
        float dx = hz_to_x(g, bandv(s, b, MA_EQ_FREQ_HZ)) - p.x, dy = db_to_y(g, node_db(s, b, sum, n), range) - p.y;
        float d = dx * dx + dy * dy;
        if (d < best_d) {
            best_d = d;
            best = (int)b;
        }
    }
    return best;
}

static void draw_graph(pb_app *a, struct nk_context *ctx, struct nk_rect g, unsigned slot, ma_rack_slot_spec *s) {
    const wb_theme *t = pb_theme(a);
    struct nk_command_buffer *out = nk_window_get_canvas(ctx);
    float range = a->eq_range;
    draw_grid(a, ctx, g, range);
    draw_analyzer(a, ctx, g, range);
    unsigned n = (unsigned)(g.w / 2);
    const float *sum = curve(&big_sum, s, -1, (float)a->rate, n);
    unsigned count = band_count(s);
    if (a->eq_band >= (int)count) a->eq_band = (int)count - 1;
    if (a->eq_band >= 0) {
        const float *one = curve(&big_band, s, a->eq_band, (float)a->rate, n);
        if (one) pb_trace(out, g, one, big_band.n, -range, range, pb_alpha(band_color(t, (unsigned)a->eq_band), 160), 1);
    }
    if (sum) pb_trace(out, g, sum, big_sum.n, -range, range, bypassed(a, slot, s) ? t->dim : t->text, 2);
    for (unsigned b = 0; b < count; b++) {
        float x = hz_to_x(g, bandv(s, b, MA_EQ_FREQ_HZ)), y = db_to_y(g, node_db(s, b, sum, big_sum.n), range);
        struct nk_color c = bandv(s, b, MA_EQ_ON) ? band_color(t, b) : t->dim;
        float rad = (int)b == a->eq_band ? 8 : 6;
        if (bandv(s, b, MA_EQ_ON)) nk_fill_circle(out, nk_rect(x - rad, y - rad, 2 * rad, 2 * rad), pb_alpha(c, 200));
        nk_stroke_circle(out, nk_rect(x - rad, y - rad, 2 * rad, 2 * rad), (int)b == a->eq_band ? 2 : 1, c);
        char label[4];
        snprintf(label, sizeof label, "%u", b + 1);
        pb_text(ctx, nk_rect(x + rad + 1, y - rad - 12, 12, 12), label, c);
    }

    /* Pointer: press a node to select and drag it (x frequency, y gain;
     * Shift+drag vertically is width), wheel on the graph changes the
     * selected band's width, right-click a node switches it on or off,
     * double-click empty space adds a bell. */
    const struct nk_input *in = &ctx->input;
    struct nk_vec2 p = in->mouse.pos;
    bool inside = nk_input_is_mouse_hovering_rect(in, g);
    bool shift = in->keyboard.keys[NK_KEY_SHIFT].down;
    if (inside) {
        char readout[64], hz[16];
        format_hz(x_to_hz(g, p.x), hz, sizeof hz);
        snprintf(readout, sizeof readout, "%s Hz  %+.1f dB", hz, (double)((g.y + g.h / 2 - p.y) / (g.h / 2) * range));
        pb_text(ctx, nk_rect(g.x + g.w - 170, g.y + 4, 166, 14), readout, t->dim);
    }
    if (inside && nk_input_is_mouse_pressed(in, NK_BUTTON_LEFT)) {
        int hit = node_at(s, g, range, sum, big_sum.n, p);
        if (hit >= 0) {
            a->eq_band = hit;
            a->eq_drag = hit;
            a->eq_drag_y = p.y;
            a->eq_drag_q = bandv(s, (unsigned)hit, MA_EQ_Q);
            a->ui.active = ID_EQ;
            pb_begin_edit(a, "eq drag");
        }
    }
    if (a->eq_drag >= 0 && a->eq_drag < (int)count) {
        if (!in->mouse.buttons[NK_BUTTON_LEFT].down) {
            a->eq_drag = -1;
            if (a->ui.active == ID_EQ) a->ui.active = -1;
        } else {
            unsigned b = (unsigned)a->eq_drag;
            if (shift) {
                double q = a->eq_drag_q * pow(2, (a->eq_drag_y - p.y) / 60.0);
                s->param[MA_EQ_BAND(b, MA_EQ_Q)] = (float)(q < .1 ? .1 : q > 40 ? 40 : q);
            } else {
                double hz = x_to_hz(g, p.x);
                s->param[MA_EQ_BAND(b, MA_EQ_FREQ_HZ)] = (float)(hz < 10 ? 10 : hz > 22000 ? 22000 : hz);
                if (gain_shape((int)bandv(s, b, MA_EQ_TYPE))) {
                    double db = (g.y + g.h / 2 - p.y) / (g.h / 2) * range;
                    db = round((db < -24 ? -24 : db > 24 ? 24 : db) * 10) / 10;
                    s->param[MA_EQ_BAND(b, MA_EQ_GAIN_DB)] = (float)db;
                }
                a->eq_drag_y = p.y;
                a->eq_drag_q = bandv(s, b, MA_EQ_Q);
            }
            pb_edited(a);
        }
    } else {
        a->eq_drag = -1;
    }
    if (inside && in->mouse.scroll_delta.y != 0 && a->eq_band >= 0) {
        uint64_t now = SDL_GetTicks();
        if (now - a->eq_wheel_at > 600) pb_begin_edit(a, "eq width");   /* one step per gesture */
        a->eq_wheel_at = now;
        unsigned b = (unsigned)a->eq_band;
        double q = bandv(s, b, MA_EQ_Q) * pow(1.12, in->mouse.scroll_delta.y);
        s->param[MA_EQ_BAND(b, MA_EQ_Q)] = (float)(q < .1 ? .1 : q > 40 ? 40 : q);
        pb_edited(a);
    }
    if (inside && nk_input_is_mouse_pressed(in, NK_BUTTON_RIGHT)) {
        int hit = node_at(s, g, range, sum, big_sum.n, p);
        if (hit >= 0) {
            pb_begin_edit(a, "eq band on/off");
            s->param[MA_EQ_BAND((unsigned)hit, MA_EQ_ON)] = bandv(s, (unsigned)hit, MA_EQ_ON) ? 0 : 1;
            a->eq_band = hit;
            pb_edited(a);
        }
    }
    if (inside && nk_input_has_mouse_click_down_in_rect(in, NK_BUTTON_DOUBLE, g, nk_true)
        && node_at(s, g, range, sum, big_sum.n, p) < 0) {
        a->eq_drag = -1;
        add_band(a, s, (float)x_to_hz(g, p.x), (float)((g.y + g.h / 2 - p.y) / (g.h / 2) * range));
    }
}

static void draw_panel(pb_app *a, struct nk_context *ctx, unsigned slot, ma_rack_slot_spec *s) {
    const wb_theme *t = pb_theme(a);
    unsigned count = band_count(s);
    char text[96];
    nk_layout_row_dynamic(ctx, 18, 3);
    snprintf(text, sizeof text, "bands %u / %d", count, MA_EQ_BANDS);
    nk_label(ctx, text, NK_TEXT_LEFT);
    if (nk_button_label(ctx, "+ band")) add_band(a, s, 1000, 0);
    if (nk_button_label(ctx, "remove") && a->eq_band >= 0) remove_band(a, s, (unsigned)a->eq_band);
    count = band_count(s);
    nk_layout_row_dynamic(ctx, 18, 2);
    enabled_toggle(a, ctx, s);
    mix_bar(a, ctx, slot);
    nk_layout_row_dynamic(ctx, 18, 1);
    param_bar(a, ctx, slot, MA_EQ_OUTPUT_DB, "output", "dB");
    nk_layout_row_dynamic(ctx, 6, 1);
    nk_spacing(ctx, 1);

    int b = a->eq_band;
    if (b >= 0 && b < (int)count) {
        nk_layout_row_dynamic(ctx, 18, 1);
        snprintf(text, sizeof text, "band %d", b + 1);
        nk_label_colored(ctx, text, NK_TEXT_LEFT, band_color(t, (unsigned)b));
        nk_layout_row_dynamic(ctx, 18, 3);
        int type = (int)bandv(s, (unsigned)b, MA_EQ_TYPE);
        for (int k = 0; k < MA_EQ_TYPE_COUNT; k++)
            if (nk_select_label(ctx, TYPE_LABEL[k], NK_TEXT_CENTERED, k == type) && k != type) {
                pb_begin_edit(a, "eq band type");
                s->param[MA_EQ_BAND((unsigned)b, MA_EQ_TYPE)] = (float)k;
                pb_edited(a);
            }
        nk_layout_row_dynamic(ctx, 18, 1);
        param_bar(a, ctx, slot, MA_EQ_BAND(b, MA_EQ_FREQ_HZ), "freq", "Hz");
        if (gain_shape(type)) param_bar(a, ctx, slot, MA_EQ_BAND(b, MA_EQ_GAIN_DB), "gain", "dB");
        param_bar(a, ctx, slot, MA_EQ_BAND(b, MA_EQ_Q), type == MA_EQ_LOW_CUT || type == MA_EQ_HIGH_CUT ? "resonance"
                                                         : gain_shape(type) && type != MA_EQ_BELL ? "slope" : "Q",
                  "");
        nk_layout_row_dynamic(ctx, 18, 2);
        bool on = bandv(s, (unsigned)b, MA_EQ_ON) != 0;
        if (pb_toggle(ctx, t, "on", &on)) {
            pb_begin_edit(a, "eq band on/off");
            s->param[MA_EQ_BAND((unsigned)b, MA_EQ_ON)] = on ? 1 : 0;
            pb_edited(a);
        }
        if (type == MA_EQ_BELL || type == MA_EQ_NOTCH)
            snprintf(text, sizeof text, "width %.2f oct", octaves(bandv(s, (unsigned)b, MA_EQ_Q)));
        else snprintf(text, sizeof text, "%s", type == MA_EQ_LOW_CUT || type == MA_EQ_HIGH_CUT ? "12 dB/oct" : "");
        nk_label_colored(ctx, text, NK_TEXT_RIGHT, t->dim);
    }
    nk_layout_row_dynamic(ctx, 6, 1);
    nk_spacing(ctx, 1);
    nk_layout_row_dynamic(ctx, 16, 1);
    for (unsigned k = 0; k < count; k++) {
        char hz[16];
        format_hz(bandv(s, k, MA_EQ_FREQ_HZ), hz, sizeof hz);
        int type = (int)bandv(s, k, MA_EQ_TYPE);
        if (gain_shape(type))
            snprintf(text, sizeof text, "%u  %-10s %6s Hz %+5.1f dB  Q %.2f%s", k + 1, TYPE_LABEL[type], hz,
                     (double)bandv(s, k, MA_EQ_GAIN_DB), (double)bandv(s, k, MA_EQ_Q), bandv(s, k, MA_EQ_ON) ? "" : "  off");
        else
            snprintf(text, sizeof text, "%u  %-10s %6s Hz           Q %.2f%s", k + 1, TYPE_LABEL[type], hz,
                     (double)bandv(s, k, MA_EQ_Q), bandv(s, k, MA_EQ_ON) ? "" : "  off");
        if (nk_select_label(ctx, text, NK_TEXT_LEFT, (int)k == a->eq_band)) a->eq_band = (int)k;
    }
    nk_layout_row_dynamic(ctx, 6, 1);
    nk_spacing(ctx, 1);
    nk_layout_row_dynamic(ctx, 14, 1);
    nk_label_colored(ctx, "drag a node: frequency and gain; Shift+drag or wheel: width", NK_TEXT_LEFT, t->dim);
    nk_label_colored(ctx, "double-click: add a bell; right-click a node: on/off; Del: remove", NK_TEXT_LEFT, t->dim);
}

void pb_draw_eq(pb_app *a, struct nk_context *ctx, struct nk_rect r) {
    ma_rack_slot_spec *s = selected_eq(a);
    if (!s) {
        a->eq_open = false;
        return;
    }
    unsigned slot = (unsigned)a->rack_selected;
    const wb_theme *t = pb_theme(a);
    nk_window_set_bounds(ctx, "EQ", r);
    if (!nk_begin(ctx, "EQ", r, NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
        nk_end(ctx);
        return;
    }
    nk_layout_row_begin(ctx, NK_STATIC, 20, 9);
    char title[64];
    snprintf(title, sizeof title, "EQ  slot %u  id %u", slot + 1, (unsigned)s->id);
    nk_layout_row_push(ctx, 220);
    nk_label_colored(ctx, title, NK_TEXT_LEFT, t->text);
    nk_layout_row_push(ctx, 60);
    nk_label_colored(ctx, "range", NK_TEXT_RIGHT, t->dim);
    for (unsigned i = 0; i < sizeof RANGES / sizeof *RANGES; i++) {
        char label[8];
        snprintf(label, sizeof label, "%.0f", (double)RANGES[i]);
        nk_layout_row_push(ctx, 40);
        if (nk_select_label(ctx, label, NK_TEXT_CENTERED, a->eq_range == RANGES[i])) a->eq_range = RANGES[i];
    }
    nk_layout_row_push(ctx, 140);
    bool analyzer = a->eq_analyzer;
    if (pb_toggle(ctx, t, "output spectrum", &analyzer)) a->eq_analyzer = analyzer;
    nk_layout_row_push(ctx, 110);
    if (nk_button_label(ctx, "close (Esc)")) a->eq_open = false;
    nk_layout_row_end(ctx);

    float body = r.h - 40;
    nk_layout_row_begin(ctx, NK_DYNAMIC, body, 2);
    nk_layout_row_push(ctx, .74f);
    struct nk_rect g;
    if (nk_widget(&g, ctx)) draw_graph(a, ctx, g, slot, s);
    nk_layout_row_push(ctx, .26f);
    if (nk_group_begin(ctx, "eq panel", 0)) {
        draw_panel(a, ctx, slot, s);
        nk_group_end(ctx);
    }
    nk_layout_row_end(ctx);
    nk_end(ctx);
}
