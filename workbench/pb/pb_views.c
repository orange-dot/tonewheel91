/* Notes, analysis, status and the overlays (plan §2.6–§2.8). */
#include "pb_app.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static bool begin_panel(struct nk_context *ctx, const char *name, struct nk_rect r) {
    nk_window_set_bounds(ctx, name, r);
    return nk_begin(ctx, name, r, NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR);
}

static float peak_of_note(const pb_probe_result *s);

static float note_hz(int note) { return 440.0f * powf(2.0f, (float)(note - 69) / 12.0f); }

/* ---- notes ------------------------------------------------------------------------- */

static void lane(pb_app *a, struct nk_context *ctx, struct nk_rect r, unsigned steps, int which) {
    const wb_theme *t = pb_theme(a);
    struct nk_command_buffer *out = nk_window_get_canvas(ctx);
    pb_pattern *p = &pb_channel_now(a)->pattern;
    float w = r.w / (float)steps;
    nk_fill_rect(out, r, 0, t->background);
    const struct nk_input *in = &ctx->input;
    for (unsigned s = 0; s < steps; s++) {
        struct nk_rect cell = nk_rect(r.x + w * (float)s, r.y, w, r.h);
        pb_step *st = &p->step[s];
        if (which < 2) {
            float u = which == 0 ? st->velocity / 127.0f : st->gate / 100.0f;
            nk_fill_rect(out, nk_rect(cell.x + 1, cell.y + cell.h * (1 - u), cell.w - 2, cell.h * u), 0,
                         pb_alpha(st->mask ? t->live : t->dim, st->mask ? 200 : 90));
            if (nk_input_is_mouse_hovering_rect(in, cell) && in->mouse.buttons[NK_BUTTON_LEFT].down) {
                float v = 1 - (in->mouse.pos.y - cell.y) / cell.h;
                v = v < 0 ? 0 : v > 1 ? 1 : v;
                if (which == 0) st->velocity = (uint8_t)(1 + lroundf(v * 126));
                else st->gate = (uint8_t)(1 + lroundf(v * 99));
            }
        } else {
            char text[8];
            if (which == 2) snprintf(text, sizeof text, "%s", st->tie ? "~" : "");
            else snprintf(text, sizeof text, "%+d", st->octave);
            if (which == 3 && !st->octave) text[0] = 0;
            pb_text(ctx, nk_rect(cell.x + 2, cell.y, cell.w, cell.h), text, st->tie || st->octave ? t->text : t->dim);
            if (nk_input_is_mouse_click_in_rect(in, NK_BUTTON_LEFT, cell)) {
                if (which == 2) st->tie = !st->tie;
                else if (st->octave < PB_SEQ_OCTAVE_MAX) st->octave++;
            }
            if (which == 3 && nk_input_is_mouse_click_in_rect(in, NK_BUTTON_RIGHT, cell) && st->octave > -PB_SEQ_OCTAVE_MAX)
                st->octave--;
        }
    }
    /* One outline for the lane; pb_draw_notes rules the steps through all
     * four lanes at once.  (A stroked rectangle per cell cost 768 vertices
     * of the frame's 65535.) */
    nk_stroke_rect(out, r, 0, 1, t->line);
}

void pb_draw_notes(pb_app *a, struct nk_context *ctx, struct nk_rect r) {
    if (!begin_panel(ctx, "NOTES", r)) {
        nk_end(ctx);
        return;
    }
    const wb_theme *t = pb_theme(a);
    /* The pattern of the channel on the desk.  Tempo and swing are one for
     * all channels; steps, root and the notes are the channel's own. */
    pb_pattern *p = &pb_channel_now(a)->pattern;
    unsigned step_now = a->meters.seq_step[a->bench.selected];
    bool narrow = r.w < 640;
    nk_layout_row_begin(ctx, NK_STATIC, 20, 12);
    nk_layout_row_push(ctx, 36);
    if (nk_button_label(ctx, a->meters.seq_playing ? "stop" : "play") && a->engine)
        pb_engine_send(a->engine, (pb_event){ .kind = a->meters.seq_playing ? PB_EV_SEQ_STOP : PB_EV_SEQ_PLAY });
    nk_layout_row_push(ctx, narrow ? 96 : 120);
    int bpm = (int)p->bpm / 100;
    bpm = nk_propertyi(ctx, "bpm", 20, bpm, 300, 1, 1);
    uint32_t tempo = (uint32_t)bpm * 100 != p->bpm / 100 * 100 ? (uint32_t)bpm * 100 : p->bpm;
    nk_layout_row_push(ctx, narrow ? 98 : 100);
    p->steps = (uint8_t)nk_propertyi(ctx, "steps", 1, p->steps, PB_SEQ_STEPS, 1, 1);
    nk_layout_row_push(ctx, narrow ? 92 : 100);
    uint8_t swing = (uint8_t)nk_propertyi(ctx, "swing", 0, p->swing, PB_SEQ_SWING_MAX, 1, 1);
    if (tempo != p->bpm || swing != p->swing) pb_bench_set_tempo(&a->bench, tempo, swing);
    nk_layout_row_push(ctx, narrow ? 88 : 100);
    p->root = (uint8_t)nk_propertyi(ctx, "root", 12, p->root, 115, 1, 1);
    /* Built-in patterns: a pick replaces this channel's notes and length. */
    nk_layout_row_push(ctx, narrow ? 84 : 110);
    if (nk_combo_begin_label(ctx, "patterns", nk_vec2(180, 240))) {
        nk_layout_row_dynamic(ctx, 18, 1);
        for (int b = 0; b < PB_PAT_COUNT; b++)
            if (nk_combo_item_label(ctx, pb_builtin_name((pb_builtin)b), NK_TEXT_LEFT)) {
                uint8_t keep = p->swing;
                *p = pb_builtin_pattern((pb_builtin)b, p->root, p->bpm);
                p->swing = keep;
            }
        nk_combo_end(ctx);
    }
    nk_layout_row_push(ctx, narrow ? 46 : 60);
    if (nk_button_label(ctx, "clear")) for (unsigned s = 0; s < PB_SEQ_STEPS; s++) p->step[s].mask = 0;
    nk_layout_row_end(ctx);

    /* The grid: steps across, 25 semitones up. */
    float lanes = 26 + 18 + 12 + 12 + 26;
    struct nk_rect grid;
    nk_layout_row_dynamic(ctx, r.h - 20 - lanes - 26, 1);
    if (nk_widget(&grid, ctx)) {
        struct nk_command_buffer *out = nk_window_get_canvas(ctx);
        float label_w = 34;
        struct nk_rect g = nk_rect(grid.x + label_w, grid.y, grid.w - label_w, grid.h);
        float cw = g.w / p->steps, rh = g.h / PB_SEQ_SPAN;
        nk_fill_rect(out, g, 0, t->background);
        static const bool BLACK[12] = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };
        for (unsigned row = 0; row < PB_SEQ_SPAN; row++) {
            int note = (int)p->root - 12 + (int)row;
            float y = g.y + g.h - rh * (float)(row + 1);
            if (BLACK[(note % 12 + 12) % 12]) nk_fill_rect(out, nk_rect(g.x, y, g.w, rh), 0, pb_alpha(t->panel, 255));
            if (row == 12) nk_fill_rect(out, nk_rect(g.x, y, g.w, rh), 0, pb_alpha(t->select, 200));
            if (row % 12 == 0) {
                char name[8];
                snprintf(name, sizeof name, "C%d", note / 12 - 1);
                pb_text(ctx, nk_rect(grid.x, y - 3, label_w, rh + 6), name, t->dim);
            }
        }
        if (a->meters.seq_playing && step_now < p->steps)
            nk_fill_rect(out, nk_rect(g.x + cw * (float)step_now, g.y, cw, g.h), 0, pb_alpha(t->live, 40));
        for (unsigned s = 0; s < p->steps; s++) {
            for (unsigned row = 0; row < PB_SEQ_SPAN; row++) {
                struct nk_rect cell = nk_rect(g.x + cw * (float)s, g.y + g.h - rh * (float)(row + 1), cw, rh);
                if (p->step[s].mask >> row & 1u)
                    nk_fill_rect(out, nk_rect(cell.x + 1, cell.y + 1, cell.w - 2, cell.h - 1), 0, t->live);
                if (nk_input_is_mouse_click_in_rect(&ctx->input, NK_BUTTON_LEFT, cell)) {
                    p->step[s].mask ^= 1u << row;
                    a->seq_step_selected = s;
                }
            }
            nk_fill_rect(out, nk_rect(g.x + cw * (float)s, g.y, 1, g.h), 0, pb_alpha(t->line, s % 4 ? 130 : 255));
        }
    }
    struct nk_rect lr, lanes_top = { 0 };
    static const struct { float height; const char *name; } LANES[] = { { 26, "vel" }, { 18, "gate" }, { 12, "tie" },
                                                                        { 12, "oct" } };
    for (int k = 0; k < 4; k++) {
        nk_layout_row_dynamic(ctx, LANES[k].height, 1);
        if (!nk_widget(&lr, ctx)) continue;
        lane(a, ctx, nk_rect(lr.x + 34, lr.y, lr.w - 34, lr.h), p->steps, k);
        pb_text(ctx, lr, LANES[k].name, t->dim);
        if (!k) lanes_top = lr;
        if (k == 3 && lanes_top.w > 0) {
            struct nk_command_buffer *out = nk_window_get_canvas(ctx);
            float x0 = lr.x + 34, cw = (lr.w - 34) / (float)p->steps;
            for (unsigned s = 1; s < p->steps; s++)
                nk_fill_rect(out, nk_rect(x0 + cw * (float)s, lanes_top.y, 1, lr.y + lr.h - lanes_top.y), 0, t->line);
        }
    }

    /* On-screen keyboard: two octaves from the keyboard octave; mouse plays,
     * sounding notes lit. */
    nk_layout_row_dynamic(ctx, 26, 1);
    struct nk_rect kr;
    if (nk_widget(&kr, ctx)) {
        struct nk_command_buffer *out = nk_window_get_canvas(ctx);
        int base = 12 * (a->octave + 1);
        float kw = kr.w / 25;
        static int held = -1;
        static const bool BLACK[12] = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };
        nk_fill_rect(out, kr, 0, t->line);   /* shows between the keys */
        for (int k = 0; k < 25; k++) {
            int note = base + k;
            if (note > 127) break;
            struct nk_rect key = nk_rect(kr.x + kw * (float)k, kr.y, kw - 1, kr.h);
            /* Key colours stay piano colours in both themes. */
            struct nk_color c = a->sounding[note] ? t->live : BLACK[note % 12] ? nk_rgb(40, 42, 46) : nk_rgb(222, 224, 228);
            nk_fill_rect(out, key, 0, c);
            if (nk_input_is_mouse_hovering_rect(&ctx->input, key) && nk_input_is_mouse_pressed(&ctx->input, NK_BUTTON_LEFT)) {
                held = note;
                pb_note(a, (uint8_t)note, true);
            }
        }
        if (held >= 0 && !ctx->input.mouse.buttons[NK_BUTTON_LEFT].down) {
            if (!a->latch) pb_note(a, (uint8_t)held, false);
            held = -1;
        }
    }
    nk_end(ctx);
}

/* ---- scope ------------------------------------------------------------------------- */

static const char *const TAP_NAMES[PB_TAP_COUNT] = { "v1", "v2", "src", "rst", "moz", "grn", "prs", "lad", "gls",
                                                     "vca", "out" };

static void source_row(pb_app *a, struct nk_context *ctx, bool with_note) {
    nk_layout_row_begin(ctx, NK_STATIC, 18, PB_TAP_COUNT + 3);
    for (int k = 0; k < PB_TAP_COUNT; k++) {
        nk_layout_row_push(ctx, 30);
        if (nk_select_label(ctx, TAP_NAMES[k], NK_TEXT_CENTERED, a->scope_tap == k)) a->scope_tap = k;
    }
    nk_layout_row_push(ctx, 40);
    if (nk_select_label(ctx, "live", NK_TEXT_CENTERED, a->scope_tap < 0)) a->scope_tap = -1;
    if (with_note) {
        nk_layout_row_push(ctx, 60);
        bool v = a->note_view;
        if (pb_toggle(ctx, pb_theme(a), "note", &v)) a->note_view = v;
    }
    nk_layout_row_end(ctx);
}

void pb_draw_scope(pb_app *a, struct nk_context *ctx, struct nk_rect r) {
    if (!begin_panel(ctx, "SCOPE", r)) {
        nk_end(ctx);
        return;
    }
    const wb_theme *t = pb_theme(a);
    source_row(a, ctx, true);
    nk_layout_row_dynamic(ctx, r.h - 50, 1);
    struct nk_rect pr;
    if (nk_widget(&pr, ctx)) {
        struct nk_command_buffer *out = nk_window_get_canvas(ctx);
        pb_frame(out, pr, t);
        nk_stroke_line(out, pr.x, pr.y + pr.h / 2, pr.x + pr.w, pr.y + pr.h / 2, 1, t->line);
        const pb_probe_result *s = a->shown;
        char text[96] = "";
        if (a->note_view && s && s->note_ready && s->note_count > 1) {
            static float cut[PB_NOTE_POINTS];
            for (unsigned i = 0; i < s->note_count; i++) cut[i] = log10f(fmaxf(s->note_cutoff[i], 20) / 20) / 3;
            pb_trace(out, pr, s->note_peak, s->note_count, 0, peak_of_note(s), pb_alpha(t->dim, 200), 1);
            pb_trace(out, pr, s->note_amp, s->note_count, 0, 1.05f, t->live, 1.5f);
            pb_trace(out, pr, cut, s->note_count, 0, 1, t->steal_released, 1.5f);
            float release = s->note_release_at / s->note_seconds;
            nk_stroke_line(out, pr.x + pr.w * release, pr.y, pr.x + pr.w * release, pr.y + pr.h, 1, t->dim);
            snprintf(text, sizeof text, "note %.2f s: amp (blue), cutoff log (amber), peak (grey)", s->note_seconds);
        } else if (a->scope_tap >= 0 && s) {
            const pb_tap_capture *c = &s->tap[a->scope_tap];
            if (c->cycle > 1) {
                const float *x = c->x + c->count - c->cycle;
                float m = 0;
                for (unsigned i = 0; i < c->cycle; i++) m = fmaxf(m, fabsf(x[i]));
                m = m * 1.1f + 1e-9f;
                pb_trace(out, pr, x, c->cycle, -m, m, t->live, 1.5f);
                snprintf(text, sizeof text, "%s: two cycles at %.0f kHz, peak %.3f", TAP_NAMES[a->scope_tap],
                         c->rate_hz / 1000, m / 1.1f);
            }
        } else if (a->scope_tap < 0 && a->engine && a->audio_on && a->meters.newest_valid) {
            /* Pitch lock: two periods of the newest note, phase-aligned to
             * its start frame. */
            static float lr[2 * 4096], y[4096];
            uint64_t end = pb_engine_scope(a->engine, lr, 4096);
            double period = a->rate / note_hz(a->meters.newest_note);
            if (period * 2 < 4000 && end > a->meters.newest_frame) {
                double phase = fmod((double)(end - a->meters.newest_frame), period);
                unsigned n = (unsigned)(2 * period);
                unsigned start = (unsigned)(4096 - phase - n);
                for (unsigned i = 0; i < n; i++) y[i] = lr[2 * (start + i)];
                float m = 0;
                for (unsigned i = 0; i < n; i++) m = fmaxf(m, fabsf(y[i]));
                m = m * 1.1f + 1e-6f;
                pb_trace(out, pr, y, n, -m, m, t->live, 1.5f);
                snprintf(text, sizeof text, "live, locked to note %u (%.1f Hz)", a->meters.newest_note,
                         (double)note_hz(a->meters.newest_note));
            }
        } else snprintf(text, sizeof text, a->scope_tap < 0 ? "live: audio off or nothing played" : "probe pending");
        pb_text(ctx, nk_rect(pr.x + 4, pr.y + 2, pr.w, 14), text, t->dim);
    }
    nk_end(ctx);
}

static float peak_of_note(const pb_probe_result *s) {
    float m = 1e-6f;
    for (unsigned i = 0; i < s->note_count; i++) m = fmaxf(m, s->note_peak[i]);
    return m * 1.1f;
}

/* ---- spectrum ---------------------------------------------------------------------- */

void pb_draw_spectrum(pb_app *a, struct nk_context *ctx, struct nk_rect r) {
    if (!begin_panel(ctx, "SPECTRUM", r)) {
        nk_end(ctx);
        return;
    }
    const wb_theme *t = pb_theme(a);
    nk_layout_row_begin(ctx, NK_STATIC, 18, 2);
    nk_layout_row_push(ctx, 100);
    bool hold = a->spectrum_hold;
    if (pb_toggle(ctx, t, "peak hold", &hold)) {
        a->spectrum_hold = hold;
        for (size_t i = 0; i <= PB_SPECTRUM / 2; i++) a->spectrum_peak[i] = -180;
    }
    nk_layout_row_push(ctx, 300);
    nk_label_colored(ctx, a->scope_tap < 0 ? "source: live output" : "source: the scope's stage tap", NK_TEXT_LEFT, t->dim);
    nk_layout_row_end(ctx);
    nk_layout_row_dynamic(ctx, r.h - 32, 1);
    struct nk_rect pr;
    if (!nk_widget(&pr, ctx)) {
        nk_end(ctx);
        return;
    }
    struct nk_command_buffer *out = nk_window_get_canvas(ctx);
    pb_frame(out, pr, t);
    const float *db = nullptr;
    size_t n = 0;
    float rate = 0, f0 = 0;
    const pb_probe_result *s = a->shown;
    if (a->scope_tap >= 0 && s) {
        const pb_tap_capture *c = &s->tap[a->scope_tap];
        n = PB_SPECTRUM;
        while (n > c->count && n > 64) n >>= 1;
        if (c->count >= n) {
            db = c->db;
            rate = c->rate_hz;
            f0 = s->f0;
        }
    } else if (a->scope_tap < 0 && a->engine && a->audio_on) {
        uint64_t now = SDL_GetTicks();
        if (now - a->live_db_at > 66) {
            static float lr[2 * PB_SPECTRUM], mono[PB_SPECTRUM];
            pb_engine_scope(a->engine, lr, PB_SPECTRUM);
            for (unsigned i = 0; i < PB_SPECTRUM; i++) mono[i] = lr[2 * i];
            pb_spectrum_db(mono, PB_SPECTRUM, a->live_db);
            a->live_db_at = now;
        }
        db = a->live_db;
        n = PB_SPECTRUM;
        rate = (float)a->rate;
        f0 = a->meters.newest_valid ? note_hz(a->meters.newest_note) : 0;
    }
    float top = 0, bottom = -96;
    for (float d = -24; d > bottom; d -= 24) {
        float y = pr.y + pr.h * (top - d) / (top - bottom);
        nk_stroke_line(out, pr.x, y, pr.x + pr.w, y, 1, t->line);
    }
#define SX(hz) (pr.x + pr.w * log10f(fmaxf((hz), 20) / 20) / 3)
    for (float hz = 100; hz <= 10000; hz *= 10) nk_stroke_line(out, SX(hz), pr.y, SX(hz), pr.y + pr.h, 1, t->line);
    if (db && n) {
        size_t bins = n / 2 + 1;
        float bin_hz = rate / (float)n;
        if (f0 > 0)
            for (int h = 1; h * f0 < 20000 && h < 64; h++)
                nk_stroke_line(out, SX(h * f0), pr.y, SX(h * f0), pr.y + pr.h, 1, pb_alpha(t->cached, 60));
        /* One point per pixel column: the strongest bin in it. */
        static float pts[2 * 4096], peak_pts[2 * 4096];
        int count = 0, peak_count = 0;
        size_t first = 1;
        for (int col = 0; col < (int)pr.w && col < 4096; col++) {
            float hz_hi = 20 * powf(1000, (float)(col + 1) / pr.w);
            float best = -180;
            size_t last = (size_t)(hz_hi / bin_hz);
            if (last >= bins) last = bins - 1;
            for (size_t b = first; b <= last; b++) {
                if (db[b] > best) best = db[b];
                if (a->spectrum_hold && b <= PB_SPECTRUM / 2) a->spectrum_peak[b] = fmaxf(a->spectrum_peak[b], db[b]);
            }
            if (last >= first) {
                float u = (best - bottom) / (top - bottom);
                pts[2 * count] = pr.x + (float)col;
                pts[2 * count + 1] = pr.y + pr.h * (1 - (u < 0 ? 0 : u > 1 ? 1 : u));
                count++;
                if (a->spectrum_hold) {
                    float pk = -180;
                    for (size_t b = first; b <= last; b++) pk = fmaxf(pk, a->spectrum_peak[b]);
                    float v = (pk - bottom) / (top - bottom);
                    peak_pts[2 * peak_count] = pr.x + (float)col;
                    peak_pts[2 * peak_count + 1] = pr.y + pr.h * (1 - (v < 0 ? 0 : v > 1 ? 1 : v));
                    peak_count++;
                }
                first = last + 1;
            }
        }
        if (peak_count > 1) nk_stroke_polyline(out, peak_pts, peak_count, 1, pb_alpha(t->dim, 160));
        if (count > 1) nk_stroke_polyline(out, pts, count, 1.2f, t->live);
        /* Off-grid peaks above -70 dB: aliasing or inharmonic content. */
        if (f0 > 0) {
            float spacing = f0 / bin_hz;
            unsigned marks = 0;
            for (size_t b = 3; b + 1 < bins && b * bin_hz < 20000 && marks < 128; b++) {
                if (db[b] < -70 || db[b] < db[b - 1] || db[b] < db[b + 1]) continue;
                float near = roundf((float)b / spacing) * spacing;
                if (spacing < 7 || fabsf((float)b - near) <= 3) continue;
                float u = (db[b] - bottom) / (top - bottom);
                float x = SX(b * bin_hz), y = pr.y + pr.h * (1 - u);
                nk_fill_rect(out, nk_rect(x - 2, y - 2, 4, 4), 0, t->clip);
                marks++;
            }
        }
    }
#undef SX
    pb_text(ctx, nk_rect(pr.x + 4, pr.y + 2, pr.w, 14), "20 Hz - 20 kHz, -96..0 dB; red: off the harmonic grid", t->dim);
    nk_end(ctx);
}

/* ---- level ------------------------------------------------------------------------- */

static float db_unit(float x) {
    float d = x > 0 ? 20 * log10f(x) : -120;
    float u = (d + 60) / 60;
    return u < 0 ? 0 : u > 1 ? 1 : u;
}

void pb_draw_level(pb_app *a, struct nk_context *ctx, struct nk_rect r) {
    if (!begin_panel(ctx, "LEVEL", r)) {
        nk_end(ctx);
        return;
    }
    const wb_theme *t = pb_theme(a);
    const pb_meters *m = &a->meters;
    nk_layout_row_dynamic(ctx, r.h - 110, 1);
    struct nk_rect pr;
    if (nk_widget(&pr, ctx)) {
        struct nk_command_buffer *out = nk_window_get_canvas(ctx);
        float pk[2] = { m->peak_left, m->peak_right }, rms[2] = { m->rms_left, m->rms_right };
        for (int c = 0; c < 2; c++) {
            struct nk_rect b = nk_rect(pr.x + 4 + (float)c * (pr.w / 2), pr.y, pr.w / 2 - 8, pr.h);
            pb_frame(out, b, t);
            float u = db_unit(rms[c]), p = db_unit(pk[c]);
            nk_fill_rect(out, nk_rect(b.x + 1, b.y + b.h * (1 - u), b.w - 2, b.h * u), 0, pb_alpha(t->live, 180));
            nk_stroke_line(out, b.x, b.y + b.h * (1 - p), b.x + b.w, b.y + b.h * (1 - p), 2, pk[c] > .98f ? t->clip : t->text);
            float knee = db_unit(.98f);
            nk_stroke_line(out, b.x, b.y + b.h * (1 - knee), b.x + b.w, b.y + b.h * (1 - knee), 1, t->clip);
            pb_text(ctx, nk_rect(b.x + 2, b.y + b.h - 14, b.w, 12), c ? "R" : "L", t->dim);
        }
    }
    char text[96];
    nk_layout_row_dynamic(ctx, 16, 1);
    snprintf(text, sizeof text, "peak %.1f / %.1f dB", m->peak_left > 0 ? 20 * log10f(m->peak_left) : -INFINITY,
             m->peak_right > 0 ? 20 * log10f(m->peak_right) : -INFINITY);
    nk_label_colored(ctx, text, NK_TEXT_LEFT, t->text);
    snprintf(text, sizeof text, "voices %u (peak %u)", m->active_voices, m->peak_voices);
    nk_label_colored(ctx, text, NK_TEXT_LEFT, t->dim);
    snprintf(text, sizeof text, "DSP %.2f / %.2f ms", (double)m->budget_ms, (double)m->period_ms);
    nk_label_colored(ctx, text, NK_TEXT_LEFT, m->budget_ms > .8f * m->period_ms ? t->clip : t->dim);
    snprintf(text, sizeof text, "xruns %llu", (unsigned long long)m->xruns);
    nk_label_colored(ctx, text, NK_TEXT_LEFT, m->xruns ? t->steal_released : t->dim);
    if (m->muted) {
        if (nk_button_label(ctx, "MUTED: reset") && a->engine)
            pb_engine_send(a->engine, (pb_event){ .kind = PB_EV_RESET_SAFETY });
    } else nk_label_colored(ctx, "safety ok", NK_TEXT_LEFT, t->dim);
    nk_end(ctx);
}

/* ---- status ------------------------------------------------------------------------ */

void pb_draw_status(pb_app *a, struct nk_context *ctx, struct nk_rect r) {
    nk_window_set_bounds(ctx, "status", r);
    if (!nk_begin(ctx, "status", r, NK_WINDOW_NO_SCROLLBAR)) {
        nk_end(ctx);
        return;
    }
    const wb_theme *t = pb_theme(a);
    char text[1600], hint[200] = "";
    int h = a->ui.hover >= 0 ? a->ui.hover : a->ui.focus;
    pb_field_view v;
    if (h >= 0 && h < 64 && pb_field_view_at((size_t)h, &v))
        snprintf(hint, sizeof hint, "%s  %g..%g  fine %g, coarse %g; drag, Shift fine, Ctrl coarse, dbl-click type, "
                                    "right-click reset", v.info.name, v.info.minimum, v.info.maximum, v.info.fine_step,
                 v.info.coarse_step);
    const pb_probe_result *s = a->shown;
    snprintf(text, sizeof text, "%.300s%s | probe %.0f ms + %.0f ms | DSP %.2f / %.2f ms | %.300s | %s",
             a->path[0] ? a->path : "(new patch)", a->dirty ? " (unsaved)" : "", s ? s->pass_ms : 0.0,
             s ? s->note_ms : 0.0, (double)a->meters.budget_ms, (double)a->meters.period_ms, a->status,
             hint[0] ? hint : (a->audio_on ? "audio on: close Patchbench before timed benchmarks" : ""));
    nk_layout_row_dynamic(ctx, r.h - 4, 1);
    nk_label_colored(ctx, text, NK_TEXT_LEFT, a->status_error ? t->clip : t->dim);
    nk_end(ctx);
}

/* ---- overlays ------------------------------------------------------------------------ */

void pb_draw_library(pb_app *a, struct nk_context *ctx, float width, float height) {
    struct nk_rect r = nk_rect(width / 2 - 450, height / 2 - 300, 900, 600);
    if (!nk_begin(ctx, "library", r, NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_MOVABLE)) {
        nk_end(ctx);
        return;
    }
    const wb_theme *t = pb_theme(a);
    nk_layout_row_dynamic(ctx, 20, 4);
    bool audition = a->auditioning;
    if (pb_toggle(ctx, t, "audition selected", &audition)) a->auditioning = audition;
    char open_label[24];
    snprintf(open_label, sizeof open_label, "open on channel %u", a->bench.selected + 1);
    if (nk_button_label(ctx, open_label) && a->library_selected >= 0) {
        a->auditioning = false;
        a->library_open = false;
        pb_open(a, a->library_list[a->library_selected].path);
    }
    if (nk_button_label(ctx, "rescan")) pb_library_scan(a);
    if (nk_button_label(ctx, "close")) {
        a->library_open = false;
        a->auditioning = false;
    }
    nk_layout_row_dynamic(ctx, 520, 1);
    if (nk_group_begin(ctx, "entries", 0)) {
        nk_layout_row_dynamic(ctx, 18, 1);
        for (unsigned i = 0; i < a->library_count; i++)
            if (select_clicked(ctx, a->library_list[i].label, NK_TEXT_LEFT, (int)i == a->library_selected)) {
                a->library_selected = (int)i;
                if (a->auditioning) {
                    ma_patch_document doc;
                    wb_error e = { 0 };
                    char dir[PB_PATH_MAX];
                    snprintf(dir, sizeof dir, "%s", a->library_list[i].path);
                    char *slash = strrchr(dir, '/');
                    if (slash) *slash = 0;
                    if (wb_patch_load(dir, slash ? slash + 1 : dir, &doc, &e)) {
                        a->audition = doc.value;
                        a->audition_rack = doc.rack;
                    }
                    else pb_say(a, true, "audition: %s", e.message);
                }
            }
        nk_group_end(ctx);
    }
    nk_end(ctx);
}

void pb_draw_find(pb_app *a, struct nk_context *ctx, float width, float height) {
    (void)height;
    struct nk_rect r = nk_rect(width / 2 - 200, 60, 400, 70);
    if (!nk_begin(ctx, "find a field", r, NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
        nk_end(ctx);
        return;
    }
    nk_layout_row_dynamic(ctx, 22, 1);
    nk_edit_focus(ctx, 0);
    nk_flags f = nk_edit_string(ctx, NK_EDIT_FIELD | NK_EDIT_SIG_ENTER, a->find, &a->find_length,
                                (int)sizeof a->find - 1, nk_filter_default);
    a->find[a->find_length] = 0;
    if (f & NK_EDIT_COMMITED) {
        ma_patch_field_info info;
        for (size_t i = 0; ma_patch_field_info_at(i, &info); i++)
            if (strstr(info.name, a->find)) {
                a->ui.focus = (int)i;
                pb_say(a, false, "focus: %s (arrows step it, Enter types a value)", info.name);
                break;
            }
        a->find_open = false;
    }
    nk_end(ctx);
}
