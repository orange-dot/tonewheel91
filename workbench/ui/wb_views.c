/* Workbench views (docs/workbench-plan.md §5).  Every edit goes through the
 * app_* functions, so it lands in the journal, the session file and the
 * derived score; renders, probes and ledger actions run as `wb` jobs. */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wb_appstate.h"
#include "../desk/wb_patch.h"

enum { ROW = WB_ROW_HEIGHT };

/* ---- small helpers ------------------------------------------------------- */

[[gnu::format(printf, 3, 4)]]
static void textf(struct nk_context *ctx, struct nk_color color, const char *format, ...) {
    char text[512];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof text, format, args);
    va_end(args);
    nk_label_colored(ctx, text, NK_TEXT_LEFT, color);
}

static void line(struct nk_context *ctx) { nk_layout_row_dynamic(ctx, ROW, 1); }

static struct nk_color part_color(unsigned index) {
    static const struct nk_color COLORS[] = {
        { 0x6f, 0xa8, 0xdc, 0xff }, { 0xe6, 0x91, 0x38, 0xff }, { 0x93, 0xc4, 0x7d, 0xff },
        { 0xc2, 0x7b, 0xa0, 0xff }, { 0x8e, 0x7c, 0xc3, 0xff }, { 0xd5, 0xa6, 0xbd, 0xff },
        { 0x76, 0xa5, 0xaf, 0xff }, { 0xff, 0xd9, 0x66, 0xff }, { 0xb4, 0x5f, 0x06, 0xff },
        { 0x45, 0x81, 0x8e, 0xff }, { 0xa6, 0x4d, 0x79, 0xff }, { 0x6a, 0xa8, 0x4f, 0xff },
        { 0x3d, 0x85, 0xc6, 0xff }, { 0xcc, 0x41, 0x25, 0xff }, { 0x99, 0x99, 0x99, 0xff },
        { 0xf6, 0xb2, 0x6b, 0xff },
    };
    return COLORS[index % (sizeof COLORS / sizeof *COLORS)];
}

static const char *const NOTE_NAMES[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

static void note_name(unsigned n, char out[8]) {
    snprintf(out, 8, "%s%d", NOTE_NAMES[n % 12], (int)(n & 127) / 12 - 1);
}

/* Copy the session, let `edit` change one object, write it back through the
 * journal as a block replace. */
typedef void edit_fn(wb_session *copy, void *user);
static bool edit_object(app *a, wb_kind kind, unsigned index, edit_fn *edit, void *user) {
    wb_session *copy = malloc(sizeof *copy);
    if (!copy) return false;
    memcpy(copy, a->session, sizeof *copy);
    edit(copy, user);
    wb_buf block = { 0 };
    bool ok = wb_write_object(copy, kind, index, &block) && app_replace(a, kind, index, block.data);
    wb_buf_free(&block);
    free(copy);
    return ok;
}

static void section_ribbon(app *a, struct nk_context *ctx, struct nk_rect r, float from_bar, float bars) {
    struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
    const wb_theme *t = app_theme(a);
    nk_fill_rect(canvas, r, 0, t->background);
    if (!a->session) return;
    const wb_form *f = &a->session->form;
    for (unsigned i = 0; i < f->section_count; i++) {
        float x0 = r.x + ((float)f->section[i].start_bar - from_bar) / bars * r.w;
        float x1 = r.x + ((float)f->section[i].end_bar - from_bar) / bars * r.w;
        if (x1 < r.x || x0 > r.x + r.w) continue;
        if (x0 < r.x) x0 = r.x;
        if (x1 > r.x + r.w) x1 = r.x + r.w;
        struct nk_rect s = nk_rect(x0, r.y, x1 - x0 - 1, r.h);
        nk_fill_rect(canvas, s, 0, i & 1 ? t->line : t->select);
        nk_draw_text(canvas, nk_rect(x0 + 2, r.y, x1 - x0 - 2, r.h), f->section[i].name,
                     (int)strlen(f->section[i].name), ctx->style.font, t->background, t->text);
    }
}

/* ---- Sources (§5.1) ------------------------------------------------------ */

static void view_sources(app *a, struct nk_context *ctx, float width) {
    const wb_theme *t = app_theme(a);
    if (!a->session || !a->session->source_count) {
        line(ctx);
        textf(ctx, t->dim, "no sources: use Import or `wb add-source`");
        return;
    }
    nk_layout_row_begin(ctx, NK_STATIC, ROW, (int)a->session->source_count + 1);
    for (unsigned i = 0; i < a->session->source_count; i++) {
        nk_layout_row_push(ctx, 120);
        if (select_clicked(ctx, a->session->source[i].id, NK_TEXT_CENTERED, (int)i == a->source_index))
            a->source_index = (int)i;
    }
    nk_layout_row_end(ctx);
    if (a->source_index < 0 || (unsigned)a->source_index >= a->session->source_count) a->source_index = 0;
    const wb_source *src = &a->session->source[a->source_index];
    source_view *v = &a->source[a->source_index];
    line(ctx);
    if (!v->loaded) {
        textf(ctx, t->clip, "%s: %s", src->path, v->error);
        return;
    }
    wb_smf_tempo tempo[4];
    size_t tempos = wb_smf_tempos(&v->smf, tempo, 4);
    textf(ctx, t->text, "%s  sha256 %.12s..  format %u  %u tracks  PPQ %u  %s%.1f BPM  %u ticks/bar  %u bars  offset %+d",
          src->path, src->sha256, v->smf.format, v->smf.track_count, v->smf.division,
          tempos > 1 ? "tempo map, first " : "", tempos ? 60e6 / tempo[0].us_per_quarter : 120.0,
          v->ticks_per_bar, v->bars, src->offset_bars);

    static const float W[] = { 24, 150, 30, 36, 50, 80, 90, 90, 40, 90, 40, 40 };
    static const char *const H[] = { "#", "track", "ch", "prog", "notes", "range", "vel min/med/max",
                                     "dur med/max", "held", "bars", "long", "ctl" };
    nk_layout_row(ctx, NK_STATIC, ROW, 12, W);
    for (unsigned c = 0; c < 12; c++) textf(ctx, t->dim, "%s", H[c]);
    for (unsigned i = 0; i < v->smf.track_count; i++) {
        const track_stats *x = &v->track[i];
        if (!x->notes && !v->smf.track[i].name[0]) continue;
        nk_layout_row(ctx, NK_STATIC, ROW, 12, W);
        char lo[8], hi[8];
        note_name(x->low, lo);
        note_name(x->high, hi);
        struct nk_color c = x->notes ? t->text : t->dim;
        textf(ctx, c, "%u", i);
        if (select_clicked(ctx, v->smf.track[i].name[0] ? v->smf.track[i].name : "(unnamed)", NK_TEXT_LEFT,
                            (int)i == a->track_index))
            a->track_index = (int)i;
        textf(ctx, c, "%d", x->channel + 1);
        textf(ctx, c, "%d", x->program);
        textf(ctx, c, "%u", x->notes);
        textf(ctx, c, x->notes ? "%s-%s" : "-", lo, hi);
        textf(ctx, c, "%u/%u/%u", x->velocity_min, x->velocity_median, x->velocity_max);
        textf(ctx, c, "%.2f/%.2f", x->duration_median_bars, x->duration_max_bars);
        textf(ctx, c, "%u", x->held_peak);
        textf(ctx, c, "%.1f-%.1f", x->first_bar, x->last_bar);
        textf(ctx, x->long_notes ? t->steal_released : c, "%u", x->long_notes);
        textf(ctx, c, "%u", x->controllers);
    }

    /* Density heatmap: tracks x bars, the form's sections above. */
    unsigned rows = 0;
    for (unsigned i = 0; i < v->smf.track_count; i++) rows += v->track[i].notes != 0;
    float cell = 9, height = 14 + rows * cell;
    nk_layout_row_dynamic(ctx, height, 1);
    struct nk_rect r;
    if (nk_widget(&r, ctx) && v->bars) {
        struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
        section_ribbon(a, ctx, nk_rect(r.x, r.y, r.w, 12), 0, (float)v->bars);
        unsigned max = 1;
        for (unsigned i = 0; i < v->smf.track_count; i++)
            for (unsigned b = 0; v->track[i].density && b < v->bars; b++)
                if (v->track[i].density[b] > max) max = v->track[i].density[b];
        float bw = r.w / (float)v->bars, y = r.y + 14;
        for (unsigned i = 0; i < v->smf.track_count; i++) {
            const track_stats *x = &v->track[i];
            if (!x->notes) continue;
            for (unsigned b = 0; x->density && b < v->bars; b++) {
                if (!x->density[b]) continue;
                float level = (float)x->density[b] / (float)max;
                struct nk_color c = t->live;
                c.a = (nk_byte)(40 + 215 * level);
                nk_fill_rect(canvas, nk_rect(r.x + b * bw, y, bw > 1 ? bw - 0.5f : bw, cell - 1), 0, c);
            }
            if ((int)i == a->track_index)
                nk_stroke_rect(canvas, nk_rect(r.x, y - 0.5f, r.w, cell), 0, 1, t->text);
            y += cell;
        }
    }
    (void)width;

    /* Pitch-class histogram and actions for the selected track. */
    if (a->track_index >= 0 && (unsigned)a->track_index < v->smf.track_count) {
        const track_stats *x = &v->track[a->track_index];
        nk_layout_row_begin(ctx, NK_STATIC, ROW, 14);
        nk_layout_row_push(ctx, 140);
        textf(ctx, t->dim, "pitch classes, track %d", a->track_index);
        unsigned max = 1;
        for (unsigned k = 0; k < 12; k++) if (x->pitch_class[k] > max) max = x->pitch_class[k];
        for (unsigned k = 0; k < 12; k++) {
            nk_layout_row_push(ctx, 40);
            textf(ctx, x->pitch_class[k] * 4 > max * 3 ? t->live : t->text, "%s %u", NOTE_NAMES[k], x->pitch_class[k]);
        }
        nk_layout_row_push(ctx, 220);
        char label[96];
        snprintf(label, sizeof label, "A: add rule to part '%s'",
                 a->part >= 0 && (unsigned)a->part < a->session->part_count ? a->session->part[a->part].id : "-");
        if (nk_button_label(ctx, label) && a->part >= 0) {
            char block[512];
            snprintf(block, sizeof block, "[rule]\npart=%s\nfrom=%s:name=\"%s\",track=%d\n",
                     a->session->part[a->part].id, src->id, v->smf.track[a->track_index].name, a->track_index);
            if (app_insert(a, WB_KIND_RULE, a->session->rule_count, block))
                app_inspect(a, WB_KIND_RULE, (int)a->session->rule_count - 1);
        }
        nk_layout_row_end(ctx);
    }
}

/* ---- Import (§5.2) --------------------------------------------------------- */

static const char *const SPLIT_NAMES[] = { "track+channel", "track", "channel", "program", "manual" };

static void import_options(app *a, wb_import_options *o) {
    wb_import_options_default(o);
    o->split = (wb_split)a->import_split;
    o->drums = a->import_drums;
    o->keep_leading_silence = a->import_keep_silence;
}

static void view_import(app *a, struct nk_context *ctx) {
    const wb_theme *t = app_theme(a);
    nk_layout_row_begin(ctx, NK_STATIC, ROW + 4, 10);
    nk_layout_row_push(ctx, 60);
    textf(ctx, t->dim, "MIDI file");
    nk_layout_row_push(ctx, 520);
    nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, a->import_path, sizeof a->import_path, nk_filter_default);
    nk_layout_row_push(ctx, 30);
    textf(ctx, t->dim, "id");
    nk_layout_row_push(ctx, 80);
    nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, a->import_id, sizeof a->import_id, nk_filter_default);
    nk_layout_row_push(ctx, 140);
    a->import_split = nk_combo(ctx, SPLIT_NAMES, 4, a->import_split, ROW, nk_vec2(160, 120));
    nk_layout_row_push(ctx, 140);
    nk_property_int(ctx, "#drum ch:", 0, &a->import_drums, 16, 1, 0.2f);
    nk_layout_row_push(ctx, 150);
    nk_bool keep = a->import_keep_silence;
    nk_checkbox_label(ctx, "keep leading silence", &keep);
    a->import_keep_silence = keep;
    nk_layout_row_push(ctx, 80);
    bool preview = nk_button_label(ctx, "preview");
    nk_layout_row_push(ctx, 140);
    bool into = a->session && nk_button_label(ctx, "import into session");
    nk_layout_row_push(ctx, 120);
    bool fresh = nk_button_label(ctx, "new session");
    nk_layout_row_end(ctx);
    if (preview) {
        wb_smf smf;
        wb_import_options o;
        import_options(a, &o);
        wb_error e = { 0 };
        a->plan_ok = wb_smf_load(a->import_path, &smf, &e) && wb_import_plan_build(&smf, &o, a->plan, &e);
        snprintf(a->plan_error, sizeof a->plan_error, "%s", e.message);
        wb_smf_free(&smf);
    }
    if (into || fresh) {
        char drums[8];
        snprintf(drums, sizeof drums, "%d", a->import_drums);
        const char *args[16] = { "import", a->import_path, "--split", SPLIT_NAMES[a->import_split],
                                 "--drums", drums, "--id", a->import_id[0] ? a->import_id : "src" };
        unsigned n = 8;
        char name[64];
        if (into) { args[n++] = "--session"; args[n++] = a->path; }
        else {
            /* The new session is named after the file. */
            const char *base = strrchr(a->import_path, '/');
            base = base ? base + 1 : a->import_path;
            size_t k = 0;
            for (; base[k] && base[k] != '.' && k < sizeof name - 1; k++)
                name[k] = (base[k] >= 'a' && base[k] <= 'z') || (base[k] >= 'A' && base[k] <= 'Z')
                       || (base[k] >= '0' && base[k] <= '9') ? base[k] : '-';
            name[k] = 0;
            args[n++] = "--new";
            args[n++] = name;
        }
        if (a->import_keep_silence) args[n++] = "--keep-leading-silence";
        args[n] = nullptr;
        app_job(a, into ? "import into session" : "import as new session", args);
    }
    line(ctx);
    if (!a->plan_ok) {
        textf(ctx, a->plan_error[0] ? t->clip : t->dim, "%s", a->plan_error[0] ? a->plan_error : "preview a file to see the proposed parts");
        return;
    }
    const wb_import_plan *p = a->plan;
    textf(ctx, t->text, "suggested form bars %d..%d, %u sections from markers, title \"%s\"; "
          "%u unmatched releases, %u stuck notes", p->start_bar, p->end_bar, p->section_count, p->title,
          p->unmatched_offs, p->stuck_notes);
    static const float W[] = { 150, 60, 80, 70, 50, 40, 70, 70, 110, 80, 260 };
    static const char *const H[] = { "part (suggested)", "engine", "template", "stem", "notes", "held",
                                     "limit/uni", "range", "bars", "velocity", "streams" };
    nk_layout_row(ctx, NK_STATIC, ROW, 11, W);
    for (unsigned c = 0; c < 11; c++) textf(ctx, t->dim, "%s", H[c]);
    for (unsigned i = 0; i < p->part_count; i++) {
        const wb_import_part *x = &p->part[i];
        char lo[8], hi[8], streams[256] = "";
        note_name(x->low, lo);
        note_name(x->high, hi);
        for (unsigned r = 0; r < x->rule_count; r++) {
            size_t used = strlen(streams);
            snprintf(streams + used, sizeof streams - used, "%st%d%s%s", r ? " " : "", x->rule[r].track,
                     x->rule[r].channel > 0 ? "c" : "", "");
            used = strlen(streams);
            if (x->rule[r].channel > 0) snprintf(streams + used, sizeof streams - used, "%d", x->rule[r].channel);
        }
        nk_layout_row(ctx, NK_STATIC, ROW, 11, W);
        textf(ctx, t->text, "%s", x->id);
        textf(ctx, strcmp(x->engine, "mamut") ? t->steal_released : t->text, "%s", x->engine);
        textf(ctx, t->text, "%s", x->template_name);
        textf(ctx, t->text, "%s", x->stem);
        textf(ctx, t->text, "%u", x->notes);
        textf(ctx, t->text, "%u", x->held_peak);
        textf(ctx, t->text, "%u/%u", x->limit, x->unison);
        textf(ctx, x->compass_ok ? t->text : t->clip, "%s-%s", lo, hi);
        textf(ctx, t->text, "%.1f-%.1f", x->first_bar, x->last_bar);
        textf(ctx, t->text, "%u-%u", x->velocity_min, x->velocity_max);
        textf(ctx, t->dim, "%s", streams);
    }
}

/* ---- Orchestrate (§5.3) -------------------------------------------------- */

static int rule_for(const wb_session *s, unsigned part, unsigned source, int track, const char *name) {
    for (unsigned i = 0; i < s->rule_count; i++) {
        const wb_rule *r = &s->rule[i];
        if (strcmp(r->part, s->part[part].id) || strcmp(r->source, s->source[source].id)) continue;
        bool track_ok = r->track >= 0 ? r->track == track : r->track_name[0] && !strcmp(r->track_name, name);
        if (track_ok) return (int)i;
    }
    return -1;
}

static void rule_summary(const wb_rule *r, char *out, size_t size) {
    size_t n = 0;
    out[0] = 0;
#define ADD(...) do { if (n < size) n += (size_t)snprintf(out + n, size - n, __VA_ARGS__); } while (0)
    if (r->transpose) ADD("%+d ", r->transpose);
    if (r->map_to >= 0) ADD("->%d ", r->map_to);
    if (r->ring_max_bars > 0) ADD("<=%gb ", r->ring_max_bars);
    if (r->velocity_den != 1 || r->velocity_num != 1) ADD("v*%u/%u ", r->velocity_num, r->velocity_den);
    if (r->has_notes) ADD("n ");
    if (r->fold_below >= 0) ADD("fold ");
    if (r->voice) ADD("%s ", r->voice == WB_VOICE_TOP ? "top" : "rest");
    if (!n) ADD("all");
#undef ADD
}

static void view_orchestrate(app *a, struct nk_context *ctx) {
    const wb_theme *t = app_theme(a);
    const wb_session *s = a->session;
    if (!s) return;
    line(ctx);
    if (!a->derived_ok) textf(ctx, t->clip, "derived score: %s", a->derived_error);
    else textf(ctx, t->dim, "derived score sha256 %.16s..  %u parts  (click a cell: edit its rule or add one; Del in "
               "the inspector removes)", a->derived->sha256, a->derived->part_count);
    /* Matrix: source tracks with notes x parts. */
    unsigned columns = s->part_count + 1;
    float widths[WB_PARTS + 1];
    widths[0] = 170;
    for (unsigned p = 0; p < s->part_count; p++) widths[p + 1] = 88;
    nk_layout_row(ctx, NK_STATIC, ROW, (int)columns, widths);
    textf(ctx, t->dim, "source track \\ part");
    for (unsigned p = 0; p < s->part_count; p++)
        textf(ctx, (int)p == a->part ? t->live : t->dim, "%s", s->part[p].id);
    for (unsigned si = 0; si < s->source_count; si++) {
        const source_view *v = &a->source[si];
        if (!v->loaded) continue;
        for (unsigned tr = 0; tr < v->smf.track_count; tr++) {
            if (!v->track[tr].notes) continue;
            nk_layout_row(ctx, NK_STATIC, ROW, (int)columns, widths);
            textf(ctx, t->text, "%s:%u %s", s->source[si].id, tr, v->smf.track[tr].name);
            for (unsigned p = 0; p < s->part_count; p++) {
                int r = rule_for(s, p, si, (int)tr, v->smf.track[tr].name);
                char text[64];
                if (r >= 0) rule_summary(&s->rule[r], text, sizeof text);
                else snprintf(text, sizeof text, ".");
                bool selected = r >= 0 && a->inspect_kind == WB_KIND_RULE && a->inspect_index == r;
                if (select_clicked(ctx, text, NK_TEXT_CENTERED, selected)) {
                    if (r >= 0) app_inspect(a, WB_KIND_RULE, r);
                    else {
                        char block[512];
                        snprintf(block, sizeof block, "[rule]\npart=%s\nfrom=%s:name=\"%s\",track=%u\n",
                                 s->part[p].id, s->source[si].id, v->smf.track[tr].name, tr);
                        if (app_insert(a, WB_KIND_RULE, a->session->rule_count, block))
                            app_inspect(a, WB_KIND_RULE, (int)a->session->rule_count - 1);
                    }
                }
            }
        }
    }
    /* Grooves feed parts too. */
    for (unsigned g = 0; g < s->groove_count; g++) {
        line(ctx);
        textf(ctx, t->dim, "groove -> %s: %u patterns, %u uses, %u hits", s->groove[g].part,
              s->groove[g].pattern_count, s->groove[g].use_count, s->groove[g].hit_count);
    }
    if (!a->derived_ok) return;
    /* One line per part: make_hurt_orchestration.py's printout, live. */
    static const float W[] = { 110, 70, 170, 90, 90, 120 };
    nk_layout_row(ctx, NK_STATIC, ROW, 6, W);
    static const char *const H[] = { "part", "notes", "held x unison / limit", "range", "merged/cut", "bars" };
    for (unsigned c = 0; c < 6; c++) textf(ctx, t->dim, "%s", H[c]);
    for (unsigned p = 0; p < s->part_count; p++) {
        const wb_opart *x = &a->derived->part[p];
        unsigned lo = 127, hi = 0;
        uint64_t first = UINT64_MAX, last = 0;
        for (size_t i = 0; i < x->count; i++) {
            if (x->note[i].note < lo) lo = x->note[i].note;
            if (x->note[i].note > hi) hi = x->note[i].note;
            if (x->note[i].start < first) first = x->note[i].start;
            if (x->note[i].end > last) last = x->note[i].end;
        }
        bool over = x->held_peak * s->part[p].unison > s->part[p].limit;
        double tpb = a->derived->ticks_per_bar;
        char lo_name[8], hi_name[8];
        note_name(lo, lo_name);
        note_name(hi, hi_name);
        nk_layout_row(ctx, NK_STATIC, ROW, 6, W);
        textf(ctx, part_color(p), "%s", s->part[p].id);
        textf(ctx, x->count ? t->text : t->clip, "%zu", x->count);
        textf(ctx, over ? t->clip : t->text, "%u x %u / %u%s", x->held_peak, s->part[p].unison, s->part[p].limit,
              over ? "  over" : "");
        textf(ctx, t->text, x->count ? "%s-%s" : "-", lo_name, hi_name);
        textf(ctx, t->text, "%u/%u", x->merged, x->cut);
        textf(ctx, t->text, x->count ? "%.2f-%.2f" : "-", s->form.start_bar + (x->count ? first : 0) / tpb,
              s->form.start_bar + last / tpb);
    }
}

/* ---- Roll (§5.4) ------------------------------------------------------- */

static void view_roll(app *a, struct nk_context *ctx, float height) {
    const wb_theme *t = app_theme(a);
    const wb_session *s = a->session;
    if (!s || !a->derived_ok) {
        line(ctx);
        textf(ctx, t->clip, "no derived score%s%s", a->derived_error[0] ? ": " : "", a->derived_error);
        return;
    }
    const wb_derived *d = a->derived;
    float form_bars = (float)(s->form.end_bar - s->form.start_bar);
    if (a->roll_bars <= 0) { a->roll_bars = form_bars; a->roll_from_bar = (float)s->form.start_bar; }
    line(ctx);
    textf(ctx, t->dim, "bars %.1f..%.1f   wheel: zoom   shift+wheel: scroll   0: whole form   "
          "lanes fold to each part's range; the step line is held voices against limit/unison",
          a->roll_from_bar, a->roll_from_bar + a->roll_bars);
    nk_layout_row_dynamic(ctx, height - 3 * ROW, 1);
    struct nk_rect r;
    if (!nk_widget(&r, ctx)) return;
    struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
    const struct nk_input *in = &ctx->input;
    if (nk_input_is_mouse_hovering_rect(in, r) && in->mouse.scroll_delta.y != 0) {
        bool shift = nk_input_is_key_down(in, NK_KEY_SHIFT);
        float mouse_bar = a->roll_from_bar + (in->mouse.pos.x - r.x) / r.w * a->roll_bars;
        if (shift) a->roll_from_bar -= in->mouse.scroll_delta.y * a->roll_bars * 0.08f;
        else {
            float scale = in->mouse.scroll_delta.y > 0 ? 0.8f : 1.25f;
            a->roll_bars = fminf(fmaxf(a->roll_bars * scale, 1), form_bars * 1.1f);
            a->roll_from_bar = mouse_bar - (in->mouse.pos.x - r.x) / r.w * a->roll_bars;
        }
    }
    nk_fill_rect(canvas, r, 0, t->background);
    section_ribbon(a, ctx, nk_rect(r.x, r.y, r.w, 14), a->roll_from_bar, a->roll_bars);
    /* Bar lines. */
    float px_per_bar = r.w / a->roll_bars;
    int step = px_per_bar > 40 ? 1 : px_per_bar > 10 ? 4 : 8;
    for (int b = (int)floorf(a->roll_from_bar / step) * step; b <= a->roll_from_bar + a->roll_bars; b += step) {
        float x = r.x + ((float)b - a->roll_from_bar) * px_per_bar;
        if (x < r.x) continue;
        nk_stroke_line(canvas, x, r.y + 14, x, r.y + r.h, 1, t->line);
        char label[16];
        snprintf(label, sizeof label, "%d", b);
        nk_draw_text(canvas, nk_rect(x + 2, r.y + 14, 40, 14), label, (int)strlen(label), ctx->style.font,
                     t->background, t->dim);
    }
    /* Lanes: each part only as tall as its range. */
    float total = 0, lane_h[WB_PARTS];
    unsigned lo[WB_PARTS], hi[WB_PARTS];
    for (unsigned p = 0; p < s->part_count; p++) {
        lo[p] = 127; hi[p] = 0;
        for (size_t i = 0; i < d->part[p].count; i++) {
            if (d->part[p].note[i].note < lo[p]) lo[p] = d->part[p].note[i].note;
            if (d->part[p].note[i].note > hi[p]) hi[p] = d->part[p].note[i].note;
        }
        if (lo[p] > hi[p]) lo[p] = hi[p] = 60;
        lane_h[p] = (float)(hi[p] - lo[p] + 1) * 3 + 6;
        total += lane_h[p];
    }
    float avail = r.h - 30, scale = total > avail ? avail / total : 1, y = r.y + 30;
    double tpb = d->ticks_per_bar;
    bool click = nk_input_is_mouse_click_in_rect(in, NK_BUTTON_LEFT, r);
    bool shift = nk_input_is_key_down(in, NK_KEY_SHIFT);
    bool hit = false;
    for (unsigned p = 0; p < s->part_count; p++) {
        float h = lane_h[p] * scale, key = (h - 4) / (float)(hi[p] - lo[p] + 1);
        /* Shift+click on a lane adds a note (an overlay) under the mouse. */
        if (click && shift && in->mouse.pos.y >= y && in->mouse.pos.y < y + h) {
            double bar = a->roll_from_bar + (in->mouse.pos.x - r.x) / px_per_bar;
            bar = floor(bar * 16 + 0.5) / 16;
            int note = (int)hi[p] - (int)floorf((in->mouse.pos.y - y - 2) / key);
            if (note >= 0 && note <= 127) {
                char block[256];
                snprintf(block, sizeof block, "[overlay]\npart=%s\nop=add\nbar=%g\nlength_bars=0.25\nnote=%d\nvelocity=%d\n",
                         s->part[p].id, bar, note, a->velocity);
                app_insert(a, WB_KIND_OVERLAY, a->session->overlay_count, block);
                return;
            }
        }
        nk_stroke_line(canvas, r.x, y + h, r.x + r.w, y + h, 1, t->line);
        nk_draw_text(canvas, nk_rect(r.x + 2, y, 80, 14), s->part[p].id, (int)strlen(s->part[p].id), ctx->style.font,
                     t->background, part_color(p));
        struct nk_color c = part_color(p);
        if ((int)p != a->part && a->part >= 0) c.a = 150;
        for (size_t i = 0; i < d->part[p].count; i++) {
            const wb_onote *n = &d->part[p].note[i];
            float b0 = s->form.start_bar + (float)(n->start / tpb), b1 = s->form.start_bar + (float)(n->end / tpb);
            if (b1 < a->roll_from_bar || b0 > a->roll_from_bar + a->roll_bars) continue;
            float x0 = r.x + (b0 - a->roll_from_bar) * px_per_bar, x1 = r.x + (b1 - a->roll_from_bar) * px_per_bar;
            float ny = y + 2 + (float)(hi[p] - n->note) * key;
            struct nk_rect nr = nk_rect(x0, ny, fmaxf(x1 - x0, 1), fmaxf(key, 1.5f));
            nk_fill_rect(canvas, nr, 0, c);
            if (click && !shift && !hit && in->mouse.pos.x >= nr.x && in->mouse.pos.x <= nr.x + nr.w
                && in->mouse.pos.y >= nr.y - 1 && in->mouse.pos.y <= nr.y + nr.h + 1) {
                hit = true;
                a->roll_selected = true;
                a->roll_part = (int)p;
                a->roll_bar = b0;
                a->roll_note = n->note;
                a->part = (int)p;
                app_say(a, false, "%s note %u at bar %.4f, velocity %u (Del removes it as an overlay)",
                        s->part[p].id, n->note, b0, n->velocity);
            }
            if (a->roll_selected && a->roll_part == (int)p && a->roll_note == n->note && fabs(a->roll_bar - b0) < 1e-6)
                nk_stroke_rect(canvas, nk_rect(nr.x - 1, nr.y - 1, nr.w + 2, nr.h + 2), 0, 1, t->text);
        }
        y += h;
    }
}

/* ---- Groove (§5.5) ------------------------------------------------------- */

typedef struct { unsigned groove, pattern, step; } toggle;

static void toggle_step(wb_session *copy, void *user) {
    const toggle *x = user;
    wb_pattern *p = &copy->groove[x->groove].pattern[x->pattern];
    unsigned i = 0;
    while (i < p->step_count && p->step[i] < x->step) i++;
    if (i < p->step_count && p->step[i] == x->step) {
        memmove(&p->step[i], &p->step[i + 1], p->step_count - i - 1);
        memmove(&p->accent[i], &p->accent[i + 1], p->step_count - i - 1);
        p->step_count--;
    } else if (p->step_count < WB_PATTERN_STEPS) {
        memmove(&p->step[i + 1], &p->step[i], p->step_count - i);
        memmove(&p->accent[i + 1], &p->accent[i], p->step_count - i);
        p->step[i] = (uint8_t)x->step;
        p->accent[i] = 0;
        p->step_count++;
    }
}

static void view_groove(app *a, struct nk_context *ctx) {
    const wb_theme *t = app_theme(a);
    const wb_session *s = a->session;
    if (!s) return;
    nk_layout_row_begin(ctx, NK_STATIC, ROW, (int)s->groove_count + 2);
    for (unsigned g = 0; g < s->groove_count; g++) {
        nk_layout_row_push(ctx, 120);
        if (select_clicked(ctx, s->groove[g].part, NK_TEXT_CENTERED, (int)g == a->groove_index)) {
            a->groove_index = (int)g;
            app_inspect(a, WB_KIND_GROOVE, (int)g);
        }
    }
    nk_layout_row_push(ctx, 220);
    if (nk_button_label(ctx, "add groove for selected part") && a->part >= 0) {
        char block[256];
        snprintf(block, sizeof block, "[groove]\npart=%s\npattern=a 0 8 vel=90\n", s->part[a->part].id);
        if (app_insert(a, WB_KIND_GROOVE, a->session->groove_count, block)) a->groove_index = (int)a->session->groove_count - 1;
    }
    nk_layout_row_end(ctx);
    if (a->groove_index < 0 || (unsigned)a->groove_index >= s->groove_count) {
        line(ctx);
        textf(ctx, t->dim, "no groove selected");
        return;
    }
    const wb_groove *g = &s->groove[a->groove_index];
    line(ctx);
    textf(ctx, t->dim, "note %u, %u steps/bar, length %u ticks, wobble %s; click a step to toggle it; "
          "uses and hits are edited in the inspector", g->note, g->steps_per_bar, g->length_ticks,
          g->wobble_m ? "(bar*a + step*b) % m - m/2" : "off");
    for (unsigned pi = 0; pi < g->pattern_count; pi++) {
        const wb_pattern *p = &g->pattern[pi];
        nk_layout_row_begin(ctx, NK_STATIC, ROW + 4, (int)g->steps_per_bar + 1);
        nk_layout_row_push(ctx, 110);
        textf(ctx, t->text, "%s vel %u", p->name, p->velocity);
        for (unsigned st = 0; st < g->steps_per_bar; st++) {
            bool on = false;
            int accent = 0;
            for (unsigned k = 0; k < p->step_count; k++)
                if (p->step[k] == st) { on = true; accent = p->accent[k]; }
            nk_layout_row_push(ctx, 26);
            char label[8];
            snprintf(label, sizeof label, on ? (accent ? "%+d" : "#") : (st % 4 ? "." : "|"), accent);
            if (select_clicked(ctx, label, NK_TEXT_CENTERED, on)) {
                toggle x = { (unsigned)a->groove_index, pi, st };
                edit_object(a, WB_KIND_GROOVE, (unsigned)a->groove_index, toggle_step, &x);
            }
        }
        nk_layout_row_end(ctx);
    }
    line(ctx);
    char uses[512] = "";
    for (unsigned u = 0; u < g->use_count; u++) {
        size_t n = strlen(uses);
        snprintf(uses + n, sizeof uses - n, "%d..%d:%s ", g->use[u].from_bar, g->use[u].to_bar, g->pattern[g->use[u].pattern].name);
    }
    textf(ctx, t->text, "uses: %s", uses[0] ? uses : "none");
    line(ctx);
    char hits[256] = "";
    for (unsigned h = 0; h < g->hit_count; h++) {
        size_t n = strlen(hits);
        snprintf(hits + n, sizeof hits - n, "bar %d vel %u  ", g->hit[h].bar, g->hit[h].velocity);
    }
    textf(ctx, t->text, "hits: %s", hits[0] ? hits : "none");
}

/* ---- Form (§5.6) ---------------------------------------------------------- */

static void add_section(wb_session *copy, void *user) {
    (void)user;
    wb_form *f = &copy->form;
    if (f->section_count == WB_SECTIONS) return;
    int32_t start = f->section_count ? f->section[f->section_count - 1].end_bar : f->start_bar;
    if (start >= f->end_bar) return;
    wb_section *x = &f->section[f->section_count];
    snprintf(x->name, sizeof x->name, "s%u", f->section_count + 1);
    x->start_bar = start;
    x->end_bar = f->end_bar;
    f->section_count++;
}

static void drop_section(wb_session *copy, void *user) {
    (void)user;
    if (copy->form.section_count) copy->form.section_count--;
}

static void view_form(app *a, struct nk_context *ctx) {
    const wb_theme *t = app_theme(a);
    const wb_session *s = a->session;
    if (!s) return;
    const wb_form *f = &s->form;
    line(ctx);
    textf(ctx, t->text, "bars %d..%d (%d bars)  tail %.3g s  length %s  fade in %.0f ms  out %.0f ms",
          f->start_bar, f->end_bar, f->end_bar - f->start_bar, f->tail_seconds,
          f->length_seconds ? "fixed" : "derived", f->fade_in_ms, f->fade_out_ms);
    nk_layout_row_dynamic(ctx, 18, 1);
    struct nk_rect r;
    if (nk_widget(&r, ctx))
        section_ribbon(a, ctx, r, (float)f->start_bar, (float)(f->end_bar - f->start_bar));
    for (unsigned i = 0; i < f->section_count; i++) {
        line(ctx);
        textf(ctx, t->text, "%-12s %4d .. %-4d (%d bars)", f->section[i].name, f->section[i].start_bar,
              f->section[i].end_bar, f->section[i].end_bar - f->section[i].start_bar);
    }
    nk_layout_row_static(ctx, ROW, 200, 3);
    if (nk_button_label(ctx, "add section to the end")) edit_object(a, WB_KIND_FORM, 0, add_section, nullptr);
    if (nk_button_label(ctx, "remove last section")) edit_object(a, WB_KIND_FORM, 0, drop_section, nullptr);
    if (nk_button_label(ctx, "edit in inspector")) app_inspect(a, WB_KIND_FORM, 0);
    for (unsigned i = 0; i < s->curve_count; i++) {
        line(ctx);
        textf(ctx, t->dim, "curve %s: %u points (%s)", s->curve[i].id, s->curve[i].point_count,
              s->curve[i].shape == WB_SHAPE_SMOOTHSTEP ? "smoothstep" : s->curve[i].shape == WB_SHAPE_STEP ? "step" : "linear");
    }
    for (unsigned i = 0; i < s->marker_count; i++) {
        line(ctx);
        textf(ctx, t->dim, "marker %s at bar %g: %s", s->marker[i].name, s->marker[i].bar, s->marker[i].text);
    }
}

/* ---- Patch (§5.7) -------------------------------------------------------- */

static bool load_patch(app *a) {
    const wb_part *p = &a->session->part[a->patch_part];
    char path[4400];
    snprintf(path, sizeof path, "%s/%s", a->dir, p->patch);
    wb_error e = { 0 };
    bool ok;
    if (p->engine == WB_PART_ORGAN) ok = wb_twreg_load(path, &a->twreg, &e);
    else if (p->engine == WB_PART_EP73) ok = wb_epset_load(path, &a->epset, &e);
    else {
        a->patch_chain = wb_patch_is_chain(p->patch);
        ok = wb_patch_load(a->dir, p->patch, &a->patch, &e);
        if (ok && a->patch_chain)
            ok = wb_patch_read_overrides(path, &a->overrides, &e)
              && wb_patch_parent(a->dir, &a->overrides, &a->patch_parent, &e);
    }
    snprintf(a->patch_error, sizeof a->patch_error, "%.60s: %.90s", p->patch, ok ? "" : e.message);
    return ok;
}

static void save_patch(app *a) {
    const wb_part *p = &a->session->part[a->patch_part];
    char path[4400];
    snprintf(path, sizeof path, "%s/%s", a->dir, p->patch);
    wb_error e = { 0 };
    bool ok;
    if (p->engine == WB_PART_ORGAN) ok = wb_twreg_save(path, &a->twreg, &e);
    else if (p->engine == WB_PART_EP73) ok = wb_epset_save(path, &a->epset, &e);
    else if (a->patch_chain) {
        /* A field equal to the parent's value is inherited, not overridden. */
        for (size_t f = 0; f < ma_patch_field_count(); f++) {
            double mine = 0, theirs = 0;
            if (!ma_patch_field_get(&a->patch.value, f, &mine) || !ma_patch_field_get(&a->patch_parent, f, &theirs))
                continue;
            wb_patch_override(&a->overrides, (unsigned)f, mine, mine == theirs);
        }
        ok = wb_patch_write_overrides(path, &a->overrides, &e);
    } else {
        ma_patch_error pe = { 0 };
        ok = ma_patch_save(path, &a->patch, &pe);
        snprintf(e.message, sizeof e.message, "%s", pe.message);
    }
    if (!ok) app_say(a, true, "%s: %s", path, e.message);
    else a->audio_generation++;
}

static void mamut_panel(app *a, struct nk_context *ctx) {
    const wb_theme *t = app_theme(a);
    /* Signal-flow columns: one per field-name prefix. */
    static const char *const GROUPS[] = { "vco1", "vco2", "source", "raster", "bcs", "mozaik", "filter",
                                          "amp", "filter_env", "macro", "output" };
    enum { GROUP_COUNT = sizeof GROUPS / sizeof *GROUPS };
    bool changed = false;
    nk_layout_row_static(ctx, ROW, 150, GROUP_COUNT);
    for (unsigned g = 0; g < GROUP_COUNT; g++) textf(ctx, t->dim, "%s", GROUPS[g]);
    size_t idx[GROUP_COUNT][16];
    unsigned count[GROUP_COUNT] = { 0 }, rows = 0;
    for (size_t f = 0; f < ma_patch_field_count(); f++) {
        ma_patch_field_info info;
        if (!ma_patch_field_info_at(f, &info)) continue;
        for (unsigned g = 0; g < GROUP_COUNT; g++) {
            size_t n = strlen(GROUPS[g]);
            if (!strncmp(info.name, GROUPS[g], n) && info.name[n] == '.' && count[g] < 16) {
                idx[g][count[g]++] = f;
                if (count[g] > rows) rows = count[g];
                break;
            }
        }
    }
    for (unsigned row = 0; row < rows; row++) {
        nk_layout_row_static(ctx, ROW + 2, 150, GROUP_COUNT);
        for (unsigned g = 0; g < GROUP_COUNT; g++) {
            if (row >= count[g]) { nk_spacing(ctx, 1); continue; }
            ma_patch_field_info info;
            double value = 0, other = 0;
            if (!ma_patch_field_info_at(idx[g][row], &info) || !ma_patch_field_get(&a->patch.value, idx[g][row], &value)) {
                nk_spacing(ctx, 1);
                continue;
            }
            bool differs = a->patch_b_valid && ma_patch_field_get(&a->patch_b.value, idx[g][row], &other) && other != value;
            bool overridden = a->patch_chain && wb_patch_overridden(&a->overrides, (unsigned)idx[g][row]);
            char name[48];
            snprintf(name, sizeof name, "#%s%s%s:", overridden ? "!" : "", differs ? "*" : "",
                     info.name + strlen(GROUPS[g]) + 1);
            if (info.integer) {
                int v = (int)value;
                nk_property_int(ctx, name, (int)info.minimum, &v, (int)info.maximum, (int)info.fine_step, 0.2f);
                if (v != (int)value && ma_patch_field_set(&a->patch.value, idx[g][row], v)) changed = true;
            } else {
                double v = value;
                nk_property_double(ctx, name, info.minimum, &v, info.maximum, info.fine_step,
                                   (float)info.fine_step);
                if (v != value && ma_patch_field_set(&a->patch.value, idx[g][row], v)) changed = true;
            }
        }
    }
    nk_layout_row_static(ctx, ROW, 180, 6);
    const wb_part *part = &a->session->part[a->patch_part];
    if (a->patch_chain) {
        if (nk_button_label(ctx, "flatten to .mapatch")) {
            /* The full patch beside the chain; the part points at it. */
            char relative[WB_PATH_MAX + 1], path[4400];
            snprintf(relative, sizeof relative, "patches/%s-flat.mapatch", part->id);
            snprintf(path, sizeof path, "%s/%s", a->dir, relative);
            ma_patch_error pe;
            if (ma_patch_save(path, &a->patch, &pe)) app_set_key(a, WB_KIND_PART, (unsigned)a->patch_part, "patch", relative);
            else app_say(a, true, "%s: %s", path, pe.message);
        }
        textf(ctx, t->dim, "parent %s, %u overrides (!)", a->overrides.parent, a->overrides.override_count);
    } else if (nk_button_label(ctx, "derive child (.wbpatch)")) {
        char relative[WB_PATH_MAX + 1], path[4400];
        snprintf(relative, sizeof relative, "patches/%s-child.wbpatch", part->id);
        snprintf(path, sizeof path, "%s/%s", a->dir, relative);
        wb_patch_overrides child = { 0 };
        snprintf(child.name, sizeof child.name, "%s", part->id);
        snprintf(child.parent, sizeof child.parent, "%s", part->patch);
        wb_error e = { 0 };
        if (wb_patch_write_overrides(path, &child, &e)) app_set_key(a, WB_KIND_PART, (unsigned)a->patch_part, "patch", relative);
        else app_say(a, true, "%s", e.message);
    }
    if (nk_button_label(ctx, "store as B")) { a->patch_b = a->patch; a->patch_b_valid = true; }
    if (nk_button_label(ctx, "swap A / B (Tab)") && a->patch_b_valid) {
        ma_patch_document x = a->patch;
        a->patch = a->patch_b;
        a->patch_b = x;
        changed = true;
    }
    if (a->patch_b_valid) {
        unsigned diffs = 0;
        for (size_t f = 0; f < ma_patch_field_count(); f++) {
            double x = 0, y = 0;
            if (ma_patch_field_get(&a->patch.value, f, &x) && ma_patch_field_get(&a->patch_b.value, f, &y))
                diffs += x != y;
        }
        textf(ctx, t->dim, "A/B: %u fields differ (*)", diffs);
    }
    if (changed) save_patch(a);
}

static void organ_panel(app *a, struct nk_context *ctx) {
    const wb_theme *t = app_theme(a);
    bool changed = false;
    nk_layout_row_static(ctx, ROW, 90, TW_DRAWBARS);
    static const char *const FEET[TW_DRAWBARS] = { "16'", "5 1/3'", "8'", "4'", "2 2/3'", "2'", "1 3/5'", "1 1/3'", "1'" };
    for (unsigned i = 0; i < TW_DRAWBARS; i++) textf(ctx, t->dim, "%s", FEET[i]);
    nk_layout_row_static(ctx, ROW + 2, 90, TW_DRAWBARS);
    for (unsigned i = 0; i < TW_DRAWBARS; i++) {
        int v = a->twreg.registration[i];
        nk_property_int(ctx, "#", 0, &v, 8, 1, 0.1f);
        if (v != a->twreg.registration[i]) { a->twreg.registration[i] = (uint8_t)v; changed = true; }
    }
    static const char *const VIB[] = { "off", "V1", "V2", "V3", "C1", "C2", "C3" };
    static const char *const ROT[] = { "bypass", "chorale", "tremolo", "brake" };
    nk_layout_row_static(ctx, ROW + 2, 160, 6);
    int vib = nk_combo(ctx, VIB, 7, a->twreg.vibrato, ROW, nk_vec2(160, 160));
    int rot = nk_combo(ctx, ROT, 4, a->twreg.rotary, ROW, nk_vec2(160, 120));
    nk_bool perc = a->twreg.percussion, third = a->twreg.percussion_third, slow = a->twreg.percussion_slow;
    nk_checkbox_label(ctx, "percussion", &perc);
    nk_checkbox_label(ctx, "3rd harmonic", &third);
    nk_checkbox_label(ctx, "slow decay", &slow);
    if (vib != a->twreg.vibrato || rot != a->twreg.rotary || perc != a->twreg.percussion
        || third != a->twreg.percussion_third || slow != a->twreg.percussion_slow) {
        a->twreg.vibrato = vib; a->twreg.rotary = rot; a->twreg.percussion = perc;
        a->twreg.percussion_third = third; a->twreg.percussion_slow = slow;
        changed = true;
    }
    nk_layout_row_static(ctx, ROW + 2, 200, 2);
    float drive = a->twreg.drive, wear = a->twreg.wear;
    nk_property_float(ctx, "#drive:", 0, &drive, 1, .01f, .005f);
    nk_property_float(ctx, "#wear:", 0, &wear, 1, .01f, .005f);
    if (drive != a->twreg.drive || wear != a->twreg.wear) { a->twreg.drive = drive; a->twreg.wear = wear; changed = true; }
    line(ctx);
    textf(ctx, t->dim, "console automation (swell, rotary speed, drawbars) arrives through forwarded CCs 70-90 "
          "and CC11 on the part's rules");
    if (changed) save_patch(a);
}

static void ep_panel(app *a, struct nk_context *ctx) {
    bool changed = false;
    nk_layout_row_static(ctx, ROW + 2, 200, 4);
    float drive = a->epset.drive, cab = a->epset.cabinet, cond = a->epset.condition;
    nk_property_float(ctx, "#drive:", 0, &drive, 1, .01f, .005f);
    nk_property_float(ctx, "#cabinet:", 0, &cab, 1, .01f, .005f);
    nk_property_float(ctx, "#condition:", 0, &cond, 1, .01f, .005f);
    nk_bool fold = a->epset.fold;
    nk_checkbox_label(ctx, "octave fold", &fold);
    if (drive != a->epset.drive || cab != a->epset.cabinet || cond != a->epset.condition || fold != a->epset.fold) {
        a->epset.drive = drive; a->epset.cabinet = cab; a->epset.condition = cond; a->epset.fold = fold;
        changed = true;
    }
    if (changed) save_patch(a);
}

static void view_patch(app *a, struct nk_context *ctx) {
    const wb_theme *t = app_theme(a);
    if (!a->session || a->part < 0 || (unsigned)a->part >= a->session->part_count) {
        line(ctx);
        textf(ctx, t->dim, "select a part");
        return;
    }
    if (a->patch_part != a->part || a->patch_version != a->version) {
        a->patch_part = a->part;
        a->patch_version = a->version;
        a->patch_ok = load_patch(a);
        a->patch_b_valid = false;
    }
    const wb_part *p = &a->session->part[a->part];
    line(ctx);
    textf(ctx, a->patch_ok ? t->text : t->clip, "%s (%s): %s%s", p->id,
          p->engine == WB_PART_ORGAN ? "organ" : p->engine == WB_PART_EP73 ? "ep73" : "mamut", p->patch,
          a->patch_ok ? "  - edits save the file at once" : a->patch_error);
    if (!a->patch_ok) return;
    if (p->engine == WB_PART_ORGAN) organ_panel(a, ctx);
    else if (p->engine == WB_PART_EP73) ep_panel(a, ctx);
    else mamut_panel(a, ctx);
    line(ctx);
    textf(ctx, t->dim, "part config (limit, unison, sends, gain, stem) is in the Mixer and the inspector");
}

/* ---- Mixer (§5.8) -------------------------------------------------------- */

static void mixer_strip(app *a, struct nk_context *ctx, unsigned p) {
    const wb_theme *t = app_theme(a);
    const wb_session *s = a->session;
    const wb_part *part = &s->part[p];
    nk_layout_row_dynamic(ctx, ROW, 1);
    if (select_clicked(ctx, part->id, NK_TEXT_CENTERED, (int)p == a->part)) a->part = (int)p;
    textf(ctx, t->dim, "%s  %u/%u", part->engine == WB_PART_ORGAN ? "organ" : part->engine == WB_PART_EP73 ? "ep73" : "mamut",
          part->limit, part->unison);
    const char *stems[WB_STEMS];
    int current = 0;
    for (unsigned i = 0; i < s->stem_count; i++) {
        stems[i] = s->stem[i].id;
        if (!strcmp(s->stem[i].id, part->stem)) current = (int)i;
    }
    int chosen = nk_combo(ctx, stems, (int)s->stem_count, current, ROW, nk_vec2(140, 160));
    if (chosen != current) app_set_key(a, WB_KIND_PART, p, "stem", s->stem[chosen].id);
    double db = part->gain > 0 ? 20 * log10(part->gain) : -60;
    double v = db;
    nk_property_double(ctx, "#gain dB:", -60, &v, 24, 0.1, 0.05f);
    if (v != db) {
        char value[32];
        wb_format_f32((float)pow(10, v / 20), value);
        app_set_key(a, WB_KIND_PART, p, "gain", value);
    }
    static const struct { const char *key, *label; float hi; } SENDS[] = {
        { "chorus", "#chorus:", 1 }, { "reverb", "#reverb:", 1 }, { "pan", "#pan:", 1 },
    };
    for (unsigned k = 0; k < 3; k++) {
        float cur = k == 0 ? part->chorus : k == 1 ? part->reverb : part->pan, x = cur;
        nk_property_float(ctx, SENDS[k].label, k == 2 ? -1 : 0, &x, SENDS[k].hi, .01f, .005f);
        if (x != cur) {
            char value[32];
            wb_format_f32(x, value);
            app_set_key(a, WB_KIND_PART, p, SENDS[k].key, value);
        }
    }
}

static void view_mixer(app *a, struct nk_context *ctx) {
    const wb_theme *t = app_theme(a);
    const wb_session *s = a->session;
    if (!s) return;
    line(ctx);
    textf(ctx, t->dim, "stem summation order: parts in column order inside each stem, stems in order: ");
    char order[256] = "";
    for (unsigned i = 0; i < s->stem_count; i++) {
        size_t n = strlen(order);
        snprintf(order + n, sizeof order - n, "%s%s", i ? " + " : "", s->stem[i].id);
    }
    line(ctx);
    textf(ctx, t->text, "%s", order);
    nk_layout_row_static(ctx, 8 * ROW + 40, 150, (int)s->part_count);
    for (unsigned p = 0; p < s->part_count; p++) {
        char name[16];
        snprintf(name, sizeof name, "strip%u", p);
        if (nk_group_begin(ctx, name, NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
            mixer_strip(a, ctx, p);
            nk_group_end(ctx);
        }
    }
}

/* ---- Probe and Render (§5.9, §5.10) ---------------------------------------- */

static void latest_manifest(app *a, const char *kind, char *path, size_t size) {
    path[0] = 0;
    for (unsigned i = a->ledger_count; i-- > 0;)
        if (!strcmp(a->ledger[i].kind, kind)) {
            snprintf(path, size, "%s/runs/%s/manifest.mwr", a->dir, a->ledger[i].id);
            return;
        }
}

static void manifest_table(app *a, struct nk_context *ctx, const char *path) {
    const wb_theme *t = app_theme(a);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char lines[256][256];
    unsigned n = 0;
    while (n < 256 && fgets(lines[n], sizeof lines[n], f)) {
        lines[n][strcspn(lines[n], "\n")] = 0;
        n++;
    }
    fclose(f);
    line(ctx);
    textf(ctx, t->dim, "%s", path);
    for (unsigned i = 0; i < n; i++) {
        const char *x = lines[i];
        bool show = !strncmp(x, "level.", 6) || !strncmp(x, "out.", 4) || !strncmp(x, "peak.1=", 7)
                 || !strncmp(x, "section.", 8) || !strncmp(x, "probe.", 6) || !strncmp(x, "listening.", 10)
                 || !strncmp(x, "wall_seconds", 12) || !strncmp(x, "host.contamination", 18);
        if (!show) continue;
        line(ctx);
        textf(ctx, strstr(x, "fnv64") ? t->dim : t->text, "%s", x);
    }
}

static void view_probe(app *a, struct nk_context *ctx) {
    const wb_theme *t = app_theme(a);
    if (!a->session) return;
    nk_layout_row_begin(ctx, NK_STATIC, ROW + 4, 12);
    nk_layout_row_push(ctx, 50); textf(ctx, t->dim, "bars");
    nk_layout_row_push(ctx, 60);
    nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, a->probe_from, sizeof a->probe_from, nk_filter_float);
    nk_layout_row_push(ctx, 60);
    nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, a->probe_to, sizeof a->probe_to, nk_filter_float);
    nk_layout_row_push(ctx, 60); textf(ctx, t->dim, "pre-roll");
    nk_layout_row_push(ctx, 40);
    nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, a->probe_preroll, sizeof a->probe_preroll, nk_filter_float);
    nk_layout_row_push(ctx, 40); textf(ctx, t->dim, "solo");
    nk_layout_row_push(ctx, 220);
    nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, a->probe_solo, sizeof a->probe_solo, nk_filter_default);
    nk_layout_row_push(ctx, 140);
    if (nk_button_label(ctx, "probe (Ctrl+Shift+P)")) {
        const char *args[12] = { "probe", a->path, a->probe_from, a->probe_to, "--preroll",
                                 a->probe_preroll[0] ? a->probe_preroll : "4" };
        unsigned n = 6;
        if (a->probe_solo[0]) { args[n++] = "--solo"; args[n++] = a->probe_solo; }
        args[n] = nullptr;
        app_job(a, "probe", args);
    }
    nk_layout_row_end(ctx);
    /* Sections fill the range. */
    const wb_form *f = &a->session->form;
    nk_layout_row_begin(ctx, NK_STATIC, ROW, (int)f->section_count + 1);
    for (unsigned i = 0; i < f->section_count; i++) {
        nk_layout_row_push(ctx, 8.0f * (float)strlen(f->section[i].name) + 14);
        if (nk_button_label(ctx, f->section[i].name)) {
            snprintf(a->probe_from, sizeof a->probe_from, "%d", f->section[i].start_bar);
            snprintf(a->probe_to, sizeof a->probe_to, "%d", f->section[i].end_bar);
        }
    }
    nk_layout_row_end(ctx);
    char path[4600];
    latest_manifest(a, "probe", path, sizeof path);
    if (path[0]) manifest_table(a, ctx, path);
    else { line(ctx); textf(ctx, t->dim, "no probe yet"); }
}

static void view_render(app *a, struct nk_context *ctx) {
    const wb_theme *t = app_theme(a);
    if (!a->session) return;
    nk_layout_row_begin(ctx, NK_STATIC, ROW + 4, 5);
    nk_layout_row_push(ctx, 50); textf(ctx, t->dim, "label");
    nk_layout_row_push(ctx, 200);
    nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, a->render_label, sizeof a->render_label, nk_filter_default);
    nk_layout_row_push(ctx, 220);
    if (nk_button_label(ctx, "final render (Ctrl+R)")) {
        const char *args[] = { "render", a->path, "--label", a->render_label[0] ? a->render_label : "render", nullptr };
        app_job(a, "final render", args);
    }
    nk_layout_row_push(ctx, 120);
    if (nk_button_label(ctx, "cancel")) wb_jobs_cancel(&a->jobs);
    nk_layout_row_push(ctx, 400);
    textf(ctx, t->dim, "single pass, stems + mix + listening copy, sleep inhibited, committed when done");
    nk_layout_row_end(ctx);
    line(ctx);
    textf(ctx, t->text, "queue: %u job(s)%s", a->jobs.count, wb_jobs_running(&a->jobs) ? ", one running" : "");
    for (unsigned i = 0; i < a->jobs.count; i++) {
        line(ctx);
        textf(ctx, i == 0 && wb_jobs_running(&a->jobs) ? t->live : t->text, "%s %s", i == 0 && wb_jobs_running(&a->jobs) ? ">" : " ",
              a->jobs.queue[i].label);
    }
    for (unsigned i = 0; i < a->jobs.line_count; i++) {
        line(ctx);
        textf(ctx, strstr(a->jobs.line[i], "fail") ? t->clip : t->dim, "%s", a->jobs.line[i]);
    }
    char path[4600];
    latest_manifest(a, "final", path, sizeof path);
    if (path[0]) manifest_table(a, ctx, path);
}

/* ---- Listen (§5.11) ------------------------------------------------------ */

static void load_listen(app *a, const char *run) {
    char path[4600];
    snprintf(path, sizeof path, "%s/runs/%s/%s_mix.wav", a->dir, run, run);
    wb_audio audio;
    wb_error e = { 0 };
    a->listen_columns = 0;
    snprintf(a->listen_run, sizeof a->listen_run, "%s", run);
    if (!wb_audio_load(path, &audio, &e)) { app_say(a, true, "%s", e.message); return; }
    unsigned columns = LISTEN_COLUMNS;
    uint64_t per = (audio.frames + columns - 1) / columns;
    if (!per) per = 1;
    for (unsigned c = 0; c < columns; c++) {
        float lo = 0, hi = 0;
        double square = 0;
        uint64_t from = c * per, to = from + per < audio.frames ? from + per : audio.frames;
        for (uint64_t i = from; i < to; i++) {
            float m = 0.5f * (audio.sample[2 * i] + audio.sample[2 * i + 1]);
            if (m < lo) lo = m;
            if (m > hi) hi = m;
            square += (double)m * m;
        }
        a->listen_min[c] = lo;
        a->listen_max[c] = hi;
        a->listen_rms[c] = to > from ? (float)sqrt(square / (double)(to - from)) : 0;
        if (to >= audio.frames) { a->listen_columns = c + 1; break; }
        a->listen_columns = c + 1;
    }
    a->listen_seconds = (double)audio.frames / audio.rate;
    wb_measure_audio(audio.sample, audio.frames, audio.rate, &a->listen_measure);
    wb_audio_free(&audio);
}

static void view_listen(app *a, struct nk_context *ctx, float height) {
    const wb_theme *t = app_theme(a);
    if (!a->session) return;
    nk_layout_row_begin(ctx, NK_STATIC, ROW, 12);
    nk_layout_row_push(ctx, 60);
    textf(ctx, t->dim, "runs");
    unsigned shown = 0;
    for (unsigned i = a->ledger_count; i-- > 0 && shown < 10;) {
        if (strcmp(a->ledger[i].kind, "final")) continue;
        nk_layout_row_push(ctx, 230);
        if (select_clicked(ctx, a->ledger[i].id, NK_TEXT_CENTERED, !strcmp(a->listen_run, a->ledger[i].id)))
            load_listen(a, a->ledger[i].id);
        shown++;
    }
    nk_layout_row_end(ctx);
    if (!a->listen_columns) {
        line(ctx);
        textf(ctx, t->dim, "pick a final run; playback with A/B arrives with live audio (W7)");
        return;
    }
    const wb_measure *m = &a->listen_measure;
    line(ctx);
    textf(ctx, t->text, "%s  %.1f s  peak %.2f dBFS  RMS %.2f dBFS  crest %.2f dB  L/R corr %.3f  mono loss %.2f dB",
          a->listen_run, a->listen_seconds, m->peak_dbfs, m->rms_dbfs, m->crest_db, m->correlation, m->mono_loss_db);
    /* Playback: two takes A/B at matched RMS, switched at the same position. */
    nk_layout_row_begin(ctx, NK_STATIC, ROW + 2, 9);
    nk_layout_row_push(ctx, 90);
    if (nk_button_label(ctx, "load as A")) app_load_take(a, 0, a->listen_run);
    nk_layout_row_push(ctx, 90);
    if (nk_button_label(ctx, "load as B")) app_load_take(a, 1, a->listen_run);
    nk_layout_row_push(ctx, 70);
    if (nk_button_label(ctx, a->meters.playing ? "stop" : "play")) {
        if (!a->audio_on) app_audio_toggle(a);
        app_audio_send(a, (wb_live_cmd){ .kind = a->meters.playing ? WB_LIVE_STOP : WB_LIVE_PLAY });
    }
    nk_layout_row_push(ctx, 120);
    if (nk_button_label(ctx, a->take_selected ? "B playing (Tab)" : "A playing (Tab)") && a->take[1 - a->take_selected]) {
        a->take_selected ^= 1u;
        app_audio_send(a, (wb_live_cmd){ .kind = WB_LIVE_SELECT_TAKE, .d0 = (uint8_t)a->take_selected });
    }
    nk_layout_row_push(ctx, 700);
    textf(ctx, t->dim, "A %s  B %s  position %.1f s%s", a->take_run[0][0] ? a->take_run[0] : "-",
          a->take_run[1][0] ? a->take_run[1] : "-", (double)a->meters.position / 48000.0,
          a->audio_on ? "" : "  (audio off)");
    nk_layout_row_end(ctx);
    nk_layout_row_dynamic(ctx, height - 5 * ROW, 1);
    struct nk_rect r;
    if (!nk_widget(&r, ctx)) return;
    /* Click locates playback. */
    if (nk_input_is_mouse_click_in_rect(&ctx->input, NK_BUTTON_LEFT, r) && a->audio_on) {
        double at = (ctx->input.mouse.pos.x - r.x) / r.w * a->listen_seconds;
        app_audio_send(a, (wb_live_cmd){ .kind = WB_LIVE_LOCATE, .frame = (uint64_t)(at * 48000) });
    }
    struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
    nk_fill_rect(canvas, r, 0, t->background);
    /* The form's sections over the run's length (bars from the session's
     * derived tempo, exact for constant tempo). */
    float wave_h = (r.h - 16) * 0.7f, mid = r.y + 16 + wave_h / 2, lane_y = r.y + 16 + wave_h;
    float scale = m->peak > 0 ? (float)(0.48 * wave_h / m->peak) : 1;
    for (unsigned c = 0; c < a->listen_columns; c++) {
        float x = r.x + r.w * (float)c / (float)a->listen_columns;
        nk_stroke_line(canvas, x, mid - a->listen_max[c] * scale, x, mid - a->listen_min[c] * scale, 1, t->live);
        float db = a->listen_rms[c] > 0 ? 20 * log10f(a->listen_rms[c]) : -90;
        float level = fminf(fmaxf((db + 70) / 70, 0), 1);
        nk_stroke_line(canvas, x, r.y + r.h, x, r.y + r.h - level * (r.y + r.h - lane_y), 1, t->dim);
    }
    if (a->audio_on && a->listen_seconds > 0) {
        float x = r.x + r.w * (float)((double)a->meters.position / 48000.0 / a->listen_seconds);
        nk_stroke_line(canvas, x, r.y, x, r.y + r.h, 1, t->text);
    }
    if (a->derived_ok && a->session->form.section_count) {
        double seconds_per_bar = 0;
        /* Constant-tempo bar length from the derived tempo. */
        seconds_per_bar = a->derived->tempo[0].us_per_quarter * 1e-6 * a->derived->ticks_per_bar / a->derived->ppq;
        float bars = (float)(a->listen_seconds / seconds_per_bar);
        section_ribbon(a, ctx, nk_rect(r.x, r.y, r.w, 14), (float)a->session->form.start_bar, bars);
    }
}

/* ---- Ledger (§5.12) ------------------------------------------------------ */

static void view_ledger(app *a, struct nk_context *ctx) {
    const wb_theme *t = app_theme(a);
    if (!a->session) return;
    static const float W[] = { 230, 50, 160, 150, 110, 60, 400 };
    static const char *const H[] = { "id", "kind", "label", "mix fnv64", "listening", "wall s", "verdict" };
    nk_layout_row(ctx, NK_STATIC, ROW, 7, W);
    for (unsigned c = 0; c < 7; c++) textf(ctx, t->dim, "%s", H[c]);
    for (unsigned i = a->ledger_count; i-- > 0;) {
        const ledger_row *x = &a->ledger[i];
        nk_layout_row(ctx, NK_STATIC, ROW, 7, W);
        if (select_clicked(ctx, x->id, NK_TEXT_LEFT, (int)i == a->ledger_selected)) {
            if (a->ledger_selected != (int)i) a->ledger_compare = a->ledger_selected;
            a->ledger_selected = (int)i;
            snprintf(a->verdict, sizeof a->verdict, "%s", x->verdict);
        }
        textf(ctx, (int)i == a->ledger_compare ? t->steal_released : t->text, "%s", x->kind);
        textf(ctx, t->text, "%s", x->label);
        textf(ctx, t->dim, "%s", x->fnv);
        textf(ctx, t->dim, "%.10s", x->listening);
        textf(ctx, t->text, "%s", x->wall);
        textf(ctx, t->text, "%s", x->verdict);
    }
    if (a->ledger_selected < 0 || (unsigned)a->ledger_selected >= a->ledger_count) return;
    const ledger_row *sel = &a->ledger[a->ledger_selected];
    nk_layout_row_begin(ctx, NK_STATIC, ROW + 4, 6);
    nk_layout_row_push(ctx, 60); textf(ctx, t->dim, "verdict");
    nk_layout_row_push(ctx, 500);
    nk_flags flags = nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD | NK_EDIT_SIG_ENTER, a->verdict, sizeof a->verdict, nk_filter_default);
    nk_layout_row_push(ctx, 80);
    if (nk_button_label(ctx, "save") || (flags & NK_EDIT_COMMITED)) {
        const char *args[] = { "verdict", a->path, sel->id, a->verdict, nullptr };
        app_job(a, "verdict", args);
    }
    nk_layout_row_push(ctx, 170);
    if (nk_button_label(ctx, "diff with previous pick") && a->ledger_compare >= 0) {
        const char *args[] = { "diff", a->path, a->ledger[a->ledger_compare].id, sel->id, nullptr };
        app_job(a, "diff", args);
        a->console_open = true;
    }
    nk_layout_row_push(ctx, 100);
    if (nk_button_label(ctx, "reproduce")) {
        const char *args[] = { "reproduce", a->path, sel->id, nullptr };
        app_job(a, "reproduce", args);
    }
    nk_layout_row_push(ctx, 160);
    if (nk_button_label(ctx, "export to clipboard")) {
        /* The snippet goes to the clipboard, never into a tracked file. */
        char command[4600];
        snprintf(command, sizeof command, "'%s' export '%s' '%s'", a->jobs.wb_path, a->path, sel->id);
        FILE *p = popen(command, "r");
        wb_buf text = { 0 };
        char chunk[1024];
        size_t got;
        while (p && (got = fread(chunk, 1, sizeof chunk, p))) wb_buf_append(&text, chunk, got);
        if (p) pclose(p);
        if (text.data) {
            SDL_SetClipboardText(text.data);
            app_say(a, false, "export snippet of %s copied (%zu bytes)", sel->id, text.length);
        }
        wb_buf_free(&text);
    }
    nk_layout_row_end(ctx);
}

/* ---- Play (§6.4) ----------------------------------------------------------- */

static void organ_console(app *a, struct nk_context *ctx, unsigned slot) {
    const wb_theme *t = app_theme(a);
    static int drawbar[WB_LIVE_SLOTS][TW_DRAWBARS] = { { 8, 8, 8 }, { 8, 8, 8 }, { 8, 8, 8 }, { 8, 8, 8 } };
    static int swell[WB_LIVE_SLOTS] = { 127, 127, 127, 127 }, drive[WB_LIVE_SLOTS];
    nk_layout_row_begin(ctx, NK_STATIC, ROW + 2, 16);
    nk_layout_row_push(ctx, 120);
    textf(ctx, t->dim, "slot %u console", slot + 1);
    for (unsigned d = 0; d < TW_DRAWBARS; d++) {
        nk_layout_row_push(ctx, 64);
        int before = drawbar[slot][d];
        nk_property_int(ctx, "#", 0, &drawbar[slot][d], 8, 1, 0.1f);
        if (drawbar[slot][d] != before)
            app_audio_send(a, (wb_live_cmd){ .kind = WB_LIVE_MIDI, .slot = (uint8_t)slot, .d0 = 0xb0,
                                             .d1 = (uint8_t)(70 + d), .d2 = (uint8_t)(drawbar[slot][d] * 127 / 8) });
    }
    nk_layout_row_push(ctx, 120);
    if (nk_button_label(ctx, "chorale"))
        app_audio_send(a, (wb_live_cmd){ .kind = WB_LIVE_MIDI, .slot = (uint8_t)slot, .d0 = 0xb0, .d1 = 87, .d2 = 0 });
    nk_layout_row_push(ctx, 120);
    if (nk_button_label(ctx, "tremolo"))
        app_audio_send(a, (wb_live_cmd){ .kind = WB_LIVE_MIDI, .slot = (uint8_t)slot, .d0 = 0xb0, .d1 = 87, .d2 = 127 });
    nk_layout_row_push(ctx, 130);
    int before = swell[slot];
    nk_property_int(ctx, "#swell:", 0, &swell[slot], 127, 4, 1);
    if (swell[slot] != before)
        app_audio_send(a, (wb_live_cmd){ .kind = WB_LIVE_MIDI, .slot = (uint8_t)slot, .d0 = 0xb0, .d1 = 11, .d2 = (uint8_t)swell[slot] });
    nk_layout_row_push(ctx, 130);
    before = drive[slot];
    nk_property_int(ctx, "#drive:", 0, &drive[slot], 127, 4, 1);
    if (drive[slot] != before)
        app_audio_send(a, (wb_live_cmd){ .kind = WB_LIVE_MIDI, .slot = (uint8_t)slot, .d0 = 0xb0, .d1 = 85, .d2 = (uint8_t)drive[slot] });
    nk_layout_row_end(ctx);
}

static void view_play(app *a, struct nk_context *ctx) {
    const wb_theme *t = app_theme(a);
    const wb_session *s = a->session;
    line(ctx);
    textf(ctx, t->dim, "computer keyboard: Z..M lower row, Q..P upper row (number row = black keys), F7/F8 octave, "
          "Esc panic; a part per slot, each on both rows or one; an empty rack plays the selected part");
    const char *names[WB_PARTS + 1];
    names[0] = "(empty)";
    for (unsigned p = 0; p < s->part_count; p++) names[p + 1] = s->part[p].id;
    static const char *const ZONES[] = { "both rows", "lower row", "upper row" };
    bool changed = false;
    for (unsigned slot = 0; slot < WB_LIVE_SLOTS; slot++) {
        nk_layout_row_begin(ctx, NK_STATIC, ROW + 4, 4);
        nk_layout_row_push(ctx, 60);
        textf(ctx, t->text, "slot %u", slot + 1);
        nk_layout_row_push(ctx, 160);
        int chosen = nk_combo(ctx, names, (int)s->part_count + 1, a->rack_part[slot] + 1, ROW, nk_vec2(160, 300)) - 1;
        if (chosen != a->rack_part[slot]) { a->rack_part[slot] = chosen; changed = true; }
        nk_layout_row_push(ctx, 140);
        a->rack_zone[slot] = nk_combo(ctx, ZONES, 3, a->rack_zone[slot], ROW, nk_vec2(140, 100));
        nk_layout_row_push(ctx, 300);
        if (a->rack_part[slot] >= 0) {
            const wb_part *p = &s->part[a->rack_part[slot]];
            textf(ctx, t->dim, "%s, limit %u, gain %.2f", p->engine == WB_PART_ORGAN ? "organ" : p->engine == WB_PART_EP73 ? "ep73" : "mamut",
                  p->limit, (double)p->gain);
        } else textf(ctx, t->dim, "-");
        nk_layout_row_end(ctx);
    }
    nk_layout_row_static(ctx, ROW + 4, 180, 4);
    nk_property_int(ctx, "#velocity:", 1, &a->velocity, 127, 1, 0.5f);
    nk_property_int(ctx, "#octave:", 0, &a->octave, 8, 1, 0.1f);
    if (nk_button_label(ctx, a->audio_on ? "audio off" : "audio on")) app_audio_toggle(a);
    if (nk_button_label(ctx, "panic (Esc)"))
        app_audio_send(a, (wb_live_cmd){ .kind = WB_LIVE_PANIC });
    nk_layout_row_static(ctx, ROW + 4, 180, 4);
    if (nk_button_label(ctx, a->recording ? "stop recording" : "record take (MIDI)")) app_record_toggle(a);
    if (a->take_pending) {
        if (nk_button_label(ctx, "keep take -> source")) app_take_keep(a, true);
        if (nk_button_label(ctx, "discard take")) app_take_keep(a, false);
    }
    textf(ctx, a->recording ? t->clip : t->dim, a->recording ? "REC  %zu events" : "%zu events in the last take",
          a->take_count);
    if (changed && a->audio_on) app_audio_reload(a);
    for (unsigned slot = 0; slot < WB_LIVE_SLOTS; slot++)
        if (a->rack_part[slot] >= 0 && s->part[a->rack_part[slot]].engine == WB_PART_ORGAN) organ_console(a, ctx, slot);
    /* Arrangement: frozen parts from the cache, live parts on the rack. */
    line(ctx);
    textf(ctx, t->dim, "arrangement preview: frozen parts play from their caches (F freezes, U makes live, in other "
          "views); live parts take rack slots and follow the score; a preview is not the final render");
    nk_layout_row_begin(ctx, NK_STATIC, ROW + 4, 6);
    nk_layout_row_push(ctx, 120);
    if (nk_button_label(ctx, "freeze all stale")) {
        const char *args[] = { "cache", a->path, nullptr };
        app_job(a, "freeze all", args);
    }
    nk_layout_row_push(ctx, 90); textf(ctx, t->dim, "from bar");
    nk_layout_row_push(ctx, 60);
    nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, a->play_from, sizeof a->play_from, nk_filter_float);
    nk_layout_row_push(ctx, 160);
    if (nk_button_label(ctx, "play arrangement")) app_play_arrangement(a);
    nk_layout_row_push(ctx, 80);
    if (nk_button_label(ctx, "stop")) app_audio_send(a, (wb_live_cmd){ .kind = WB_LIVE_STOP });
    nk_layout_row_end(ctx);
    nk_layout_row_begin(ctx, NK_STATIC, ROW, (int)s->part_count + 1);
    nk_layout_row_push(ctx, 60);
    textf(ctx, t->dim, "live:");
    for (unsigned p = 0; p < s->part_count; p++) {
        nk_layout_row_push(ctx, 90);
        nk_bool on = a->part_live[p];
        nk_checkbox_label(ctx, s->part[p].id, &on);
        a->part_live[p] = on;
    }
    nk_layout_row_end(ctx);
    const wb_live_meters *m = &a->meters;
    line(ctx);
    textf(ctx, m->budget_ms > 0.8f * m->period_ms ? t->clip : t->text,
          "budget %.2f / %.2f ms per period (%.0f%%), worst %.2f ms, %u Mamut voices, xruns %llu%s",
          m->budget_ms, m->period_ms, m->period_ms > 0 ? 100 * m->budget_ms / m->period_ms : 0, m->budget_peak_ms,
          m->active_voices, (unsigned long long)m->xruns, m->muted ? "  MUTED by the safety" : "");
}

/* ---- dispatch ------------------------------------------------------------ */

void view_draw(app *a, struct nk_context *ctx, wb_view view, float width, float height) {
    const wb_theme *t = app_theme(a);
    if (!a->session && view != WB_VIEW_IMPORT) {
        line(ctx);
        textf(ctx, t->dim, "no session: open one (`workbench FILE.mws`) or create one in Import");
        return;
    }
    switch (view) {
    case WB_VIEW_SOURCES: view_sources(a, ctx, width); break;
    case WB_VIEW_IMPORT: view_import(a, ctx); break;
    case WB_VIEW_ORCHESTRATE: view_orchestrate(a, ctx); break;
    case WB_VIEW_ROLL: view_roll(a, ctx, height); break;
    case WB_VIEW_GROOVE: view_groove(a, ctx); break;
    case WB_VIEW_FORM: view_form(a, ctx); break;
    case WB_VIEW_PATCH: view_patch(a, ctx); break;
    case WB_VIEW_MIXER: view_mixer(a, ctx); break;
    case WB_VIEW_PROBE: view_probe(a, ctx); break;
    case WB_VIEW_RENDER: view_render(a, ctx); break;
    case WB_VIEW_LISTEN: view_listen(a, ctx, height); break;
    case WB_VIEW_LEDGER: view_ledger(a, ctx); break;
    case WB_VIEW_PLAY: view_play(a, ctx); break;
    default:
        line(ctx);
        textf(ctx, t->dim, "%s view: arrives with live audio (%s)", wb_view_name(view), wb_view_milestone(view));
    }
}

/* ---- Inspector ----------------------------------------------------------- */

/* Every key of the inspected object as an editable row; Enter writes the
 * whole block back through the journal, so list keys (sections, points,
 * patterns) edit like any other.  An empty value drops a list line. */
void view_inspector(app *a, struct nk_context *ctx) {
    const wb_theme *t = app_theme(a);
    if (!a->session || a->inspect_index < 0 || (unsigned)a->inspect_index >= wb_kind_count(a->session, a->inspect_kind)) {
        line(ctx);
        textf(ctx, t->dim, "nothing selected");
        return;
    }
    if (a->inspect_version != a->version || a->inspected_kind != a->inspect_kind
        || a->inspected_index != a->inspect_index) {
        a->inspect_version = a->version;
        a->inspected_kind = a->inspect_kind;
        a->inspected_index = a->inspect_index;
        a->inspect_rows = 0;
        wb_buf block = { 0 };
        if (wb_write_object(a->session, a->inspect_kind, (unsigned)a->inspect_index, &block)) {
            for (char *l = block.data; l && *l && a->inspect_rows < INSPECTOR_ROWS;) {
                char *end = strchr(l, '\n');
                if (end) *end = 0;
                char *eq = strchr(l, '=');
                if (eq && l[0] != '[') {
                    *eq = 0;
                    snprintf(a->inspect_key[a->inspect_rows], sizeof a->inspect_key[0], "%s", l);
                    snprintf(a->inspect_value[a->inspect_rows], sizeof a->inspect_value[0], "%s", eq + 1);
                    a->inspect_rows++;
                }
                l = end ? end + 1 : nullptr;
            }
        }
        wb_buf_free(&block);
    }
    line(ctx);
    textf(ctx, t->dim, "[%s] #%d  Enter applies, Ctrl+Z undoes", wb_kind_name(a->inspect_kind), a->inspect_index + 1);
    bool commit = false;
    for (unsigned i = 0; i < a->inspect_rows; i++) {
        nk_layout_row_begin(ctx, NK_STATIC, ROW + 2, 2);
        nk_layout_row_push(ctx, 120);
        textf(ctx, t->dim, "%s", a->inspect_key[i]);
        nk_layout_row_push(ctx, 230);
        nk_flags f = nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD | NK_EDIT_SIG_ENTER, a->inspect_value[i],
                                                    sizeof a->inspect_value[i], nk_filter_default);
        if (f & NK_EDIT_COMMITED) commit = true;
        nk_layout_row_end(ctx);
    }
    nk_layout_row_begin(ctx, NK_STATIC, ROW + 2, 2);
    nk_layout_row_push(ctx, 120);
    nk_flags fk = nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD | NK_EDIT_SIG_ENTER, a->inspect_new_key,
                                                 sizeof a->inspect_new_key, nk_filter_default);
    nk_layout_row_push(ctx, 230);
    nk_flags fv = nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD | NK_EDIT_SIG_ENTER, a->inspect_new_value,
                                                 sizeof a->inspect_new_value, nk_filter_default);
    nk_layout_row_end(ctx);
    if ((fk | fv) & NK_EDIT_COMMITED) commit = true;
    nk_layout_row_static(ctx, ROW, 170, 2);
    bool remove = nk_button_label(ctx, "remove this object");
    if (commit) {
        wb_buf block = { 0 };
        wb_buf_printf(&block, "[%s]\n", wb_kind_name(a->inspect_kind));
        for (unsigned i = 0; i < a->inspect_rows; i++)
            if (a->inspect_value[i][0]) wb_buf_printf(&block, "%s=%s\n", a->inspect_key[i], a->inspect_value[i]);
        if (a->inspect_new_key[0] && a->inspect_new_value[0])
            wb_buf_printf(&block, "%s=%s\n", a->inspect_new_key, a->inspect_new_value);
        if (app_replace(a, a->inspect_kind, (unsigned)a->inspect_index, block.data)) {
            a->inspect_new_key[0] = a->inspect_new_value[0] = 0;
        }
        /* On failure the version is unchanged, so the typed text stays for
         * correction; the error is in the console. */
        wb_buf_free(&block);
    }
    if (remove && !wb_kind_singleton(a->inspect_kind)) {
        if (app_remove(a, a->inspect_kind, (unsigned)a->inspect_index)) a->inspect_index = -1;
    }
}
