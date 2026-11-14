#include "pb_widgets.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

void pb_ui_init(pb_ui *ui) {
    *ui = (pb_ui){ .active = -1, .focus = -1, .editing = -1, .changed = -1, .hover = -1 };
}

void pb_ui_begin(pb_ui *ui, const wb_theme *theme) {
    ui->theme = theme;
    ui->changed = -1;
    ui->hover = -1;
    ui->order_count = 0;
}

void pb_ui_move_focus(pb_ui *ui, int step, bool group) {
    unsigned n = ui->order_count;
    if (!n) return;
    unsigned at = 0;
    while (at < n && ui->order[at] != ui->focus) at++;
    if (at == n) {
        ui->focus = ui->order[step < 0 ? n - 1 : 0];
        return;
    }
    if (!group) {
        unsigned next = (at + (unsigned)(step < 0 ? n - 1 : 1)) % n;
        ui->focus = ui->order[next];
        return;
    }
    unsigned char g = ui->order_group[at];
    for (unsigned k = 1; k < n; k++) {
        unsigned j = (at + (step < 0 ? n - k : k)) % n;
        if (ui->order_group[j] != g) {
            /* The first control of that group. */
            unsigned char h = ui->order_group[j];
            while (j > 0 && ui->order_group[j - 1] == h) j--;
            ui->focus = ui->order[j];
            return;
        }
    }
}

struct nk_color pb_alpha(struct nk_color c, unsigned char a) {
    c.a = a;
    return c;
}

void pb_frame(struct nk_command_buffer *out, struct nk_rect r, const wb_theme *t) {
    nk_fill_rect(out, r, 0, t->background);
    nk_stroke_rect(out, r, 0, 1, t->line);
}

void pb_text(struct nk_context *ctx, struct nk_rect r, const char *text, struct nk_color color) {
    nk_draw_text(nk_window_get_canvas(ctx), r, text, (int)strlen(text), ctx->style.font, nk_rgba(0, 0, 0, 0), color);
}

static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

/* Vertex budget: the SDL3 backend draws with 16-bit indices, so a frame
 * of one window holds at most 65535 vertices, and an anti-aliased polyline
 * costs about four per point.  Traces therefore keep one point per pixel
 * column, two only where a column spans more than two pixels, and
 * scatter-lines keep at most PB_XY_POINTS points. */
enum { PB_XY_POINTS = 400 };

void pb_trace(struct nk_command_buffer *out, struct nk_rect r, const float *y, unsigned n, float lo, float hi,
              struct nk_color color, float thickness) {
    if (n < 2 || !(hi > lo)) return;
    unsigned columns = (unsigned)r.w;
    if (columns < 2) return;
    if (columns > 2048) columns = 2048;
    static float pts[2 * 2 * 2048];
    unsigned count = 0;
#define PY(v) (r.y + r.h - r.h * clampf(((v) - lo) / (hi - lo), 0, 1))
    if (n <= columns) {
        for (unsigned i = 0; i < n; i++) {
            pts[2 * count] = r.x + r.w * (float)i / (float)(n - 1);
            pts[2 * count + 1] = PY(y[i]);
            count++;
        }
    } else {
        for (unsigned c = 0; c < columns; c++) {
            unsigned a = (unsigned)((unsigned long long)c * n / columns), b = (unsigned)((unsigned long long)(c + 1) * n / columns);
            float mn = y[a], mx = y[a];
            unsigned imn = a, imx = a;
            for (unsigned i = a; i < b; i++) {
                if (y[i] < mn) { mn = y[i]; imn = i; }
                if (y[i] > mx) { mx = y[i]; imx = i; }
            }
            float x = r.x + (float)c, ymn = PY(mn), ymx = PY(mx);
            if (ymn - ymx <= 2) {
                pts[2 * count] = x;
                pts[2 * count + 1] = PY(y[(a + b) / 2]);
                count++;
            } else {
                /* Both extremes, in the order they occur. */
                bool min_first = imn < imx;
                pts[2 * count] = x;
                pts[2 * count + 1] = min_first ? ymn : ymx;
                count++;
                pts[2 * count] = x;
                pts[2 * count + 1] = min_first ? ymx : ymn;
                count++;
            }
        }
    }
#undef PY
    nk_stroke_polyline(out, pts, (int)count, thickness, color);
}

void pb_xy(struct nk_command_buffer *out, struct nk_rect r, const float *x, const float *y, unsigned n, float x0,
           float x1, float y0, float y1, struct nk_color color, float thickness) {
    static float pts[2 * PB_XY_POINTS];
    if (n < 2 || !(x1 > x0) || !(y1 > y0)) return;
    unsigned m = n < PB_XY_POINTS ? n : PB_XY_POINTS;
    for (unsigned i = 0; i < m; i++) {
        unsigned j = (unsigned)((unsigned long long)i * (n - 1) / (m - 1));
        pts[2 * i] = r.x + r.w * clampf((x[j] - x0) / (x1 - x0), 0, 1);
        pts[2 * i + 1] = r.y + r.h - r.h * clampf((y[j] - y0) / (y1 - y0), 0, 1);
    }
    nk_stroke_polyline(out, pts, (int)m, thickness, color);
}

/* ---- the active-control machine ------------------------------------------- */

static double snap(const pb_scale_fn *s, double v) {
    double u = s->to_unit(s->ctx, v);
    return s->from_unit(s->ctx, u);
}

static bool set_value(pb_ui *ui, int id, double *value, double next) {
    if (next == *value) return false;
    *value = next;
    ui->changed = id;
    return true;
}

/* Shared input handling: drag (along `vertical`), wheel, double-click
 * entry, right-click reset, keyboard on focus.  Returns true on change. */
static bool interact(pb_ui *ui, struct nk_context *ctx, int id, unsigned char group, struct nk_rect r, bool vertical,
                     double *value, const pb_scale_fn *s) {
    const struct nk_input *in = &ctx->input;
    bool changed = false;
    if (ui->order_count < 256) {
        ui->order_group[ui->order_count] = group;
        ui->order[ui->order_count++] = id;
    }
    bool hover = nk_input_is_mouse_hovering_rect(in, r);
    if (hover) ui->hover = id;
    bool shift = in->keyboard.keys[NK_KEY_SHIFT].down, ctrl = in->keyboard.keys[NK_KEY_CTRL].down;
    if (hover && nk_input_is_mouse_pressed(in, NK_BUTTON_LEFT) && ui->editing != id) {
        if (in->mouse.buttons[NK_BUTTON_DOUBLE].clicked) {
            ui->editing = id;
            ui->edit_length = 0;
            ui->edit[0] = 0;
            ui->active = -1;
        } else {
            ui->active = id;
            ui->begin = true;
            ui->drag_value = *value;
            ui->drag_unit = s->to_unit(s->ctx, *value);
            ui->drag_x = in->mouse.pos.x;
            ui->drag_y = in->mouse.pos.y;
        }
        ui->focus = id;
    }
    if (ui->active == id) {
        if (!in->mouse.buttons[NK_BUTTON_LEFT].down) ui->active = -1;
        else {
            float delta = vertical ? ui->drag_y - in->mouse.pos.y : in->mouse.pos.x - ui->drag_x;
            float span = vertical ? r.h : r.w;
            double next;
            if (shift || ctrl) {
                /* Steps per 4 px: fine with Shift, coarse with Ctrl. */
                double step = shift ? s->fine : s->coarse;
                next = ui->drag_value + step * floor(delta / 4.0f);
                double lo = s->from_unit(s->ctx, 0), hi = s->from_unit(s->ctx, 1);
                next = next < lo ? lo : next > hi ? hi : next;
                if (s->integer) next = round(next);
            } else next = s->from_unit(s->ctx, ui->drag_unit + delta / (span > 1 ? span : 1));
            changed = set_value(ui, id, value, next);
        }
    }
    if (hover && in->mouse.scroll_delta.y != 0 && ui->active < 0) {
        double step = (ctrl ? s->coarse : s->fine) * (in->mouse.scroll_delta.y > 0 ? 1 : -1);
        if (!changed) ui->begin = true;
        changed = set_value(ui, id, value, snap(s, *value + step)) || changed;
    }
    if (hover && nk_input_is_mouse_pressed(in, NK_BUTTON_RIGHT)) {
        ui->begin = true;
        changed = set_value(ui, id, value, s->reset) || changed;
    }
    if (ui->focus == id && ui->editing != id) {
        if (ui->key_steps || ui->key_coarse) {
            ui->begin = true;
            double next = *value + ui->key_steps * s->fine + ui->key_coarse * s->coarse;
            double lo = s->from_unit(s->ctx, 0), hi = s->from_unit(s->ctx, 1);
            next = next < lo ? lo : next > hi ? hi : next;
            if (s->integer) next = round(next);
            changed = set_value(ui, id, value, next) || changed;
            ui->key_steps = ui->key_coarse = 0;
        }
        if (ui->key_reset) {
            ui->begin = true;
            changed = set_value(ui, id, value, s->reset) || changed;
            ui->key_reset = false;
        }
        if (ui->key_enter) {
            ui->editing = id;
            ui->edit_length = 0;
            ui->edit[0] = 0;
            ui->key_enter = false;
        }
    }
    return changed;
}

/* Text entry in place of the control: Enter commits; a click elsewhere or
 * Escape (the app clears ui->editing) cancels. */
static bool entry(pb_ui *ui, struct nk_context *ctx, int id, double *value, const pb_scale_fn *s) {
    nk_edit_focus(ctx, NK_EDIT_ALWAYS_INSERT_MODE);
    nk_flags flags = nk_edit_string(ctx, NK_EDIT_FIELD | NK_EDIT_SIG_ENTER, ui->edit, &ui->edit_length,
                                    (int)sizeof ui->edit - 1, nk_filter_default);
    ui->edit[ui->edit_length] = 0;
    bool changed = false;
    if (flags & NK_EDIT_COMMITED) {
        double v;
        if (s->parse(s->ctx, ui->edit, &v)) {
            ui->begin = true;
            changed = set_value(ui, id, value, v);
        }
        ui->editing = -1;
    } else if (flags & NK_EDIT_DEACTIVATED) ui->editing = -1;
    return changed;
}

static void marks_draw(struct nk_command_buffer *out, const wb_theme *t, struct nk_rect r, bool vertical,
                       const pb_scale_fn *s, const pb_marks *m) {
    if (!m) return;
    if (!isnan(m->mod_lo) && !isnan(m->mod_hi)) {
        float a = (float)s->to_unit(s->ctx, m->mod_lo), b = (float)s->to_unit(s->ctx, m->mod_hi);
        if (a > b) { float x = a; a = b; b = x; }
        struct nk_rect band = vertical ? nk_rect(r.x + r.w - 3, r.y + r.h * (1 - b), 3, r.h * (b - a))
                                       : nk_rect(r.x + r.w * a, r.y + r.h - 3, r.w * (b - a), 3);
        nk_fill_rect(out, band, 0, t->live);
    }
    struct { double v; struct nk_color c; } mark[3] = {
        { m->parent, t->dim }, { m->b, t->cached }, { m->effective, t->steal_released },
    };
    for (int k = 0; k < 3; k++) {
        if (isnan(mark[k].v)) continue;
        float u = clampf((float)s->to_unit(s->ctx, mark[k].v), 0, 1);
        if (vertical) {
            float y = r.y + r.h * (1 - u);
            if (k == 2) nk_fill_triangle(out, r.x, y - 3, r.x, y + 3, r.x + 5, y, mark[k].c);
            else nk_stroke_line(out, r.x, y, r.x + r.w, y, k == 1 ? 2 : 1, mark[k].c);
        } else {
            float x = r.x + r.w * u;
            if (k == 2) nk_fill_triangle(out, x - 3, r.y, x + 3, r.y, x, r.y + 5, mark[k].c);
            else nk_stroke_line(out, x, r.y, x, r.y + r.h, k == 1 ? 2 : 1, mark[k].c);
        }
    }
}

bool pb_bar(pb_ui *ui, struct nk_context *ctx, int id, unsigned char group, const char *label, double *value,
            const pb_scale_fn *s, const pb_marks *m) {
    if (ui->editing == id) return entry(ui, ctx, id, value, s);
    struct nk_rect r;
    enum nk_widget_layout_states state = nk_widget(&r, ctx);
    if (state == NK_WIDGET_INVALID) return false;
    const wb_theme *t = ui->theme;
    struct nk_command_buffer *out = nk_window_get_canvas(ctx);
    float label_w = r.w * .32f;
    struct nk_rect bar = nk_rect(r.x + label_w, r.y + 1, r.w - label_w, r.h - 2);
    bool changed = interact(ui, ctx, id, group, bar, false, value, s);
    bool inherited = m && m->inherited;
    struct nk_color text = inherited ? t->dim : t->text;
    pb_text(ctx, nk_rect(r.x, r.y, label_w - 2, r.h), label, ui->focus == id ? t->live : text);
    nk_fill_rect(out, bar, 0, t->background);
    float u = clampf((float)s->to_unit(s->ctx, *value), 0, 1);
    struct nk_color fill = ui->active == id ? t->live : pb_alpha(t->live, inherited ? 70 : 140);
    nk_fill_rect(out, nk_rect(bar.x, bar.y, bar.w * u, bar.h), 0, fill);
    nk_stroke_rect(out, bar, 0, 1, ui->focus == id ? t->live : t->line);
    marks_draw(out, t, bar, false, s, m);
    char buffer[32];
    s->format(s->ctx, *value, buffer, sizeof buffer);
    pb_text(ctx, nk_rect(bar.x + 4, bar.y, bar.w - 4, bar.h), buffer, text);
    return changed;
}

bool pb_fader(pb_ui *ui, struct nk_context *ctx, int id, unsigned char group, const char *label, double *value,
              const pb_scale_fn *s, const pb_marks *m) {
    if (ui->editing == id) return entry(ui, ctx, id, value, s);
    struct nk_rect r;
    enum nk_widget_layout_states state = nk_widget(&r, ctx);
    if (state == NK_WIDGET_INVALID) return false;
    const wb_theme *t = ui->theme;
    struct nk_command_buffer *out = nk_window_get_canvas(ctx);
    float label_h = 16;
    struct nk_rect slot = nk_rect(r.x + r.w * .25f, r.y + 14, r.w * .5f, r.h - label_h - 16);
    bool changed = interact(ui, ctx, id, group, nk_rect(r.x, slot.y, r.w, slot.h), true, value, s);
    nk_fill_rect(out, slot, 0, t->background);
    float u = clampf((float)s->to_unit(s->ctx, *value), 0, 1);
    bool inherited = m && m->inherited;
    nk_fill_rect(out, nk_rect(slot.x, slot.y + slot.h * (1 - u), slot.w, slot.h * u), 0,
                 ui->active == id ? t->live : pb_alpha(t->live, inherited ? 70 : 140));
    nk_stroke_rect(out, slot, 0, 1, ui->focus == id ? t->live : t->line);
    marks_draw(out, t, slot, true, s, m);
    char buffer[32];
    s->format(s->ctx, *value, buffer, sizeof buffer);
    pb_text(ctx, nk_rect(r.x, r.y, r.w, 13), buffer, inherited ? t->dim : t->text);
    pb_text(ctx, nk_rect(r.x + 2, r.y + r.h - label_h, r.w, label_h), label, ui->focus == id ? t->live : t->dim);
    return changed;
}

bool pb_toggle(struct nk_context *ctx, const wb_theme *t, const char *label, bool *value) {
    struct nk_rect r;
    enum nk_widget_layout_states state = nk_widget(&r, ctx);
    if (state == NK_WIDGET_INVALID) return false;
    struct nk_command_buffer *out = nk_window_get_canvas(ctx);
    bool clicked = nk_input_is_mouse_click_in_rect(&ctx->input, NK_BUTTON_LEFT, r);
    if (clicked) *value = !*value;
    struct nk_rect box = nk_rect(r.x + 1, r.y + (r.h - 10) / 2, 10, 10);
    if (*value) nk_fill_rect(out, box, 0, t->live);
    nk_stroke_rect(out, box, 0, 1, t->dim);
    pb_text(ctx, nk_rect(r.x + 14, r.y, r.w - 14, r.h), label, *value ? t->text : t->dim);
    return clicked;
}

bool pb_drag_area(pb_ui *ui, struct nk_context *ctx, int id, struct nk_rect r, float *dx, float *dy, bool *began) {
    const struct nk_input *in = &ctx->input;
    *began = false;
    if (nk_input_is_mouse_hovering_rect(in, r) && nk_input_is_mouse_pressed(in, NK_BUTTON_LEFT)) {
        ui->active = id;
        ui->begin = true;
        *began = true;
        ui->drag_x = in->mouse.pos.x;
        ui->drag_y = in->mouse.pos.y;
    }
    if (ui->active != id) return false;
    if (!in->mouse.buttons[NK_BUTTON_LEFT].down) {
        ui->active = -1;
        return false;
    }
    *dx = in->mouse.pos.x - ui->drag_x;
    *dy = in->mouse.pos.y - ui->drag_y;
    ui->drag_x = in->mouse.pos.x;
    ui->drag_y = in->mouse.pos.y;
    return true;
}
