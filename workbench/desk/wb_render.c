#define _POSIX_C_SOURCE 200809L
#include "wb_render.h"

#include <math.h>
#include <stdckdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../driver/ma_patch_file.h"
#include "../../driver/smf.h"
#include "../../driver/smf_schedule.h"
#include "../../driver/wav.h"
#include "../../src/mamutanalog.h"
#include "wb_engine.h"
#include "wb_format.h"
#include "wb_hash.h"
#include "wb_patch.h"

enum { BLOCK = 128 };

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

/* ---- time ---------------------------------------------------------------- */

bool wb_timemap_init(wb_timemap *m, unsigned ppq, unsigned rate, unsigned ticks_per_bar,
                     int32_t start_bar, const uint64_t *ticks, const uint32_t *us,
                     unsigned count) {
    if (!ppq || !rate || !ticks_per_bar || !count || count > WB_TEMPO_SEGMENTS || ticks[0])
        return false;
    *m = (wb_timemap){ .ppq = ppq, .rate = rate, .ticks_per_bar = ticks_per_bar,
                       .start_bar = start_bar };
    uint64_t acc = 0;
    for (unsigned i = 0; i < count; i++) {
        if (!us[i]) return false;
        if (i) {
            if (ticks[i] < ticks[i - 1]) return false;
            uint64_t add;
            if (ckd_mul(&add, ticks[i] - ticks[i - 1], (uint64_t)us[i - 1])
                || ckd_add(&acc, acc, add))
                return false;
        }
        /* A later tempo at the same tick replaces the earlier one. */
        if (i && ticks[i] == m->segment[m->count - 1].tick) m->count--;
        m->segment[m->count++] = (wb_tempo_segment){ ticks[i], us[i], acc };
    }
    return true;
}

static const wb_tempo_segment *segment_at_tick(const wb_timemap *m, uint64_t tick) {
    unsigned i = 0;
    while (i + 1 < m->count && m->segment[i + 1].tick <= tick) i++;
    return &m->segment[i];
}

uint64_t wb_timemap_frame(const wb_timemap *m, uint64_t tick) {
    const wb_tempo_segment *s = segment_at_tick(m, tick);
    /* floor((us_ticks * rate + d/2) / d) without a 128-bit product: split
     * us_ticks = q*d + r, then q*rate + floor((r*rate + d/2) / d) is the
     * same integer, and r*rate stays below 2^53. */
    uint64_t us_ticks = s->us_ticks_before + (tick - s->tick) * s->us_per_quarter;
    uint64_t d = (uint64_t)m->ppq * 1000000u;
    uint64_t q = us_ticks / d, r = us_ticks % d;
    return q * m->rate + (r * m->rate + d / 2) / d;
}

float wb_timemap_bar(const wb_timemap *m, uint64_t frame) {
    unsigned i = 0;
    while (i + 1 < m->count && wb_timemap_frame(m, m->segment[i + 1].tick) <= frame) i++;
    const wb_tempo_segment *s = &m->segment[i];
    float k1 = (float)((double)m->ppq * 1e6 / (double)m->rate);
    float k2 = (float)s->us_per_quarter * (float)m->ticks_per_bar;
    if (i == 0)
        return (float)frame * k1 / k2 + (float)m->start_bar;
    uint64_t f0 = wb_timemap_frame(m, s->tick);
    float b0 = (float)m->start_bar + (float)((double)s->tick / m->ticks_per_bar);
    return (float)(frame - f0) * k1 / k2 + b0;
}

uint64_t wb_timemap_bar_frame(const wb_timemap *m, double bar) {
    double ticks = (bar - m->start_bar) * m->ticks_per_bar;
    if (ticks <= 0) return 0;
    return wb_timemap_frame(m, (uint64_t)ceil(ticks));
}

/* ---- score --------------------------------------------------------------- */

void wb_score_free(wb_score *s) {
    free(s->event);
    *s = (wb_score){ 0 };
}

static bool push_event(wb_score *s, wb_note_event e) {
    if (s->count == s->capacity) {
        size_t capacity = s->capacity ? 2 * s->capacity : 4096;
        wb_note_event *grown = realloc(s->event, capacity * sizeof *grown);
        if (!grown) return false;
        s->event = grown;
        s->capacity = capacity;
    }
    e.order = (uint32_t)s->count;
    s->event[s->count++] = e;
    return true;
}

bool wb_score_load_midi(const wb_session *session, const char *path, wb_score *score,
                        wb_error *error) {
    *score = (wb_score){ 0 };
    char *bytes;
    size_t size;
    if (!wb_read_file(path, &bytes, &size)) return fail(error, "cannot read %s", path);
    wb_sha256_bytes_hex(bytes, size, score->sha256);
    uint16_t mask = (uint16_t)((1u << session->part_count) - 1u);
    if (session->part_count >= 16) mask = 0xffff;
    score->compat = session->head.compat == WB_COMPAT_RENDER_MIDI;
    /* render_midi drives one instrument from every channel. */
    bool one_part = score->compat && session->part_count == 1;
    if (one_part) mask = 0xffff;
    smf_file midi = { 0 };
    smf_error smf_err = { 0 };
    bool ok = smf_parse((const uint8_t *)bytes, size, 0xffff, &midi, &smf_err);
    free(bytes);
    if (!ok) return fail(error, "%s: byte %zu: %s", path, smf_err.offset,
                         smf_err.message ? smf_err.message : "invalid MIDI");
    uint64_t ticks[WB_TEMPO_SEGMENTS];
    uint32_t us[WB_TEMPO_SEGMENTS];
    unsigned tempo_count = 0;
    if (!midi.tempo_count || midi.tempos[0].tick) { ticks[0] = 0; us[0] = 500000; tempo_count = 1; }
    for (size_t i = 0; i < midi.tempo_count && ok; i++) {
        if (tempo_count == WB_TEMPO_SEGMENTS) ok = fail(error, "%s: more than %d tempo changes", path, WB_TEMPO_SEGMENTS);
        else { ticks[tempo_count] = midi.tempos[i].tick; us[tempo_count++] = midi.tempos[i].us_per_quarter; }
    }
    if (ok && !wb_timemap_init(&score->map, midi.division, session->head.rate, 4 * midi.division,
                               session->form.start_bar, ticks, us, tempo_count))
        ok = fail(error, "%s: unusable tempo map", path);
    size_t *compat_frame = nullptr, compat_frames = 0;
    if (ok && score->compat
        && !smf_make_event_frames(&midi, session->head.rate, 0.0, &compat_frame, &compat_frames))
        ok = fail(error, "%s: render_midi timing failed", path);
    for (size_t i = 0; ok && i < midi.event_count; i++) {
        const smf_event *e = &midi.events[i];
        unsigned type = e->status & 0xf0u, channel = e->status & 15u;
        unsigned part = one_part ? 0 : channel;
        uint64_t frame = score->compat ? compat_frame[i] : wb_timemap_frame(&score->map, e->tick);
        if (frame > score->last_frame) score->last_frame = frame;
        bool ctl = type == 0xa0u || type == 0xb0u || type == 0xd0u || type == 0xe0u;
        if (type != 0x80u && type != 0x90u && !ctl)
            ok = fail(error, "%s: status %02x is not a note or a forwarded controller", path, e->status);
        else if (!(mask >> channel & 1u))
            ok = fail(error, "%s: channel %u has no part (%u parts)", path, channel + 1, session->part_count);
        else if (ctl) {
            ok = push_event(score, (wb_note_event){
                .frame = frame, .part = (uint8_t)part,
                .kind = (uint8_t)type, .d1 = e->d1, .d2 = e->d2, .status = e->status });
            if (!ok) fail(error, "out of memory");
        } else {
            bool on = type == 0x90u && e->d2;
            if (e->tick > score->last_tick) score->last_tick = e->tick;
            ok = push_event(score, (wb_note_event){
                .frame = frame, .part = (uint8_t)part,
                .note = e->d1, .velocity = on ? e->d2 : 0, .on = on, .d2 = e->d2, .status = e->status });
            if (!ok) fail(error, "out of memory");
        }
    }
    free(compat_frame);
    smf_dispose(&midi);
    if (!ok) wb_score_free(score);
    else if (!score->compat) wb_score_sort(score);   /* render_midi keeps file order */
    return ok;
}

static int compare_events(const void *a, const void *b);
/* At one frame: releases, controllers, attacks; then file order. */
static int rank(const wb_note_event *e) { return e->kind ? 1 : e->on ? 2 : 0; }

static int compare_events(const void *a, const void *b) {
    const wb_note_event *x = a, *y = b;
    if (x->frame != y->frame) return x->frame < y->frame ? -1 : 1;
    if (rank(x) != rank(y)) return rank(x) < rank(y) ? -1 : 1;
    return (x->order > y->order) - (x->order < y->order);
}

void wb_score_sort(wb_score *s) {
    if (s->count > 1) qsort(s->event, s->count, sizeof *s->event, compare_events);
}

uint64_t wb_render_length(const wb_session *s, const wb_score *score) {
    if (s->form.length_seconds) return (uint64_t)s->form.length_seconds * s->head.rate;
    if (score->compat)
        return score->last_frame + (uint64_t)(s->form.tail_seconds * s->head.rate);
    uint64_t end = wb_timemap_frame(&score->map, score->last_tick);
    double seconds = ceil((double)end / s->head.rate + s->form.tail_seconds);
    return (uint64_t)seconds * s->head.rate;
}

/* ---- parts --------------------------------------------------------------- */

typedef struct {
    wb_part_engine engine;
    wb_twreg twreg;
    wb_epset epset;
    ma_patch patch;
    ma_voice_pool_config config;
    ma_chorus_controls chorus;
    ma_reverb_controls reverb;
    unsigned stem;
} part_setup;

static bool setup_parts(const wb_session *s, const char *dir, part_setup *setup, wb_error *error) {
    for (unsigned i = 0; i < s->part_count; i++) {
        const wb_part *p = &s->part[i];
        char path[4400];
        snprintf(path, sizeof path, "%s/%s", dir, p->patch);
        setup[i].engine = p->engine;
        int stem_index = wb_find_stem(s, p->stem);
        if (stem_index < 0) return fail(error, "part '%s': undefined stem", p->id);
        setup[i].stem = (unsigned)stem_index;
        if (p->engine == WB_PART_ORGAN) {
            if (!wb_twreg_load(path, &setup[i].twreg, error)) return false;
            continue;
        }
        if (p->engine == WB_PART_EP73) {
            if (!wb_epset_load(path, &setup[i].epset, error)) return false;
            continue;
        }
        ma_patch_document doc;
        wb_error pe = { 0 };
        if (!wb_patch_load(dir, p->patch, &doc, &pe))
            return fail(error, "part '%s': %s", p->id, pe.message);
        part_setup *x = &setup[i];
        x->patch = doc.value;
        x->config = ma_voice_pool_default_config;
        x->config.limit = (uint8_t)p->limit;
        x->config.unison_count = (uint8_t)p->unison;
        x->config.detune_cents = p->detune_cents;
        x->config.session_seed = p->seed_from_name
            ? ma_voice_pool_default_config.session_seed ^ tw_fnv1a64(p->id, strlen(p->id), 0)
            : p->seed;
        x->config.engine = s->head.engine == WB_ENGINE_SCALAR ? MA_POOL_ENGINE_SCALAR
                                                              : MA_POOL_ENGINE_AUTO;
        x->chorus = (ma_chorus_controls){ p->chorus > 0, p->chorus, p->chorus_depth, p->chorus_rate_hz };
        x->reverb = (ma_reverb_controls){ p->reverb > 0, p->reverb, p->reverb_size, p->reverb_damping };
        int stem = wb_find_stem(s, p->stem);
        if (stem < 0) return fail(error, "part '%s': undefined stem", p->id);
        x->stem = (unsigned)stem;
    }
    return true;
}

/* ---- lanes --------------------------------------------------------------- */

typedef enum {
    CALL_PATCH, CALL_MACRO, CALL_MOD_WHEEL, CALL_CHANNEL_PRESSURE, CALL_PITCH_BEND,
    CALL_LFO_DEPTH, CALL_LFO_RATE, CALL_CHARACTER, CALL_GAIN,
} lane_call;

typedef struct {
    unsigned part;
    lane_call call;
    size_t field;                  /* patch field index or macro id */
    const wb_lane *lane;
    const wb_curve *curve;         /* null for a points lane */
    float first_bar;
} lane_plan;

static const char *const MACRO_PARAMS[MA_MACRO_COUNT] = {
    "pool.macro.gravitacija", "pool.macro.bloom", "pool.macro.heat",
    "pool.macro.ruin", "pool.macro.swarm",
};

static bool plan_lanes(const wb_session *s, lane_plan *plan, wb_error *error) {
    for (unsigned i = 0; i < s->lane_count; i++) {
        const wb_lane *l = &s->lane[i];
        lane_plan *x = &plan[i];
        *x = (lane_plan){ .part = (unsigned)wb_find_part(s, l->part), .lane = l };
        if (l->curve[0]) {
            int c = wb_find_curve(s, l->curve);
            if (c < 0) return fail(error, "lane %u: undefined curve", i + 1);
            x->curve = &s->curve[c];
            x->first_bar = (float)x->curve->point[0].bar;
        } else x->first_bar = (float)l->point[0].bar;
        const char *p = l->param;
        bool found = false;
        for (unsigned m = 0; m < MA_MACRO_COUNT && !found; m++)
            if (!strcmp(p, MACRO_PARAMS[m])) { x->call = CALL_MACRO; x->field = m; found = true; }
        static const struct { const char *name; lane_call call; } SIMPLE[] = {
            { "pool.mod_wheel", CALL_MOD_WHEEL }, { "pool.channel_pressure", CALL_CHANNEL_PRESSURE },
            { "pool.pitch_bend", CALL_PITCH_BEND }, { "pool.lfo_depth", CALL_LFO_DEPTH },
            { "pool.lfo_rate_hz", CALL_LFO_RATE }, { "pool.character", CALL_CHARACTER },
            { "part.gain", CALL_GAIN },
        };
        for (size_t k = 0; k < sizeof SIMPLE / sizeof *SIMPLE && !found; k++)
            if (!strcmp(p, SIMPLE[k].name)) { x->call = SIMPLE[k].call; found = true; }
        for (size_t f = 0; f < ma_patch_field_count() && !found; f++) {
            ma_patch_field_info info;
            if (ma_patch_field_info_at(f, &info) && !strcmp(info.name, p)) {
                x->call = CALL_PATCH;
                x->field = f;
                found = true;
            }
        }
        if (!found) return fail(error, "lane %u: unknown parameter '%s'", i + 1, p);
    }
    return true;
}

/* Breakpoint evaluation with the MA3 exhibits' f32 operation order. */
static float eval_points(const wb_point *point, unsigned count, wb_shape shape, float bar) {
    if (bar <= (float)point[0].bar) return point[0].value;
    for (unsigned i = 1; i < count; i++) {
        float x1 = (float)point[i].bar;
        if (bar < x1) {
            float x0 = (float)point[i - 1].bar, v0 = point[i - 1].value, v1 = point[i].value;
            switch (shape) {
            case WB_SHAPE_LINEAR: return v0 + (v1 - v0) * (bar - x0) / (x1 - x0);
            case WB_SHAPE_SMOOTHSTEP: {
                float t = (bar - x0) / (x1 - x0);
                float p = t * t * (3 - 2 * t);
                return v0 + (v1 - v0) * p;
            }
            default: return v0;
            }
        }
    }
    return point[count - 1].value;
}

static float lane_value(const lane_plan *x, float bar) {
    const wb_lane *l = x->lane;
    float v;
    if (x->curve) {
        float c = eval_points(x->curve->point, x->curve->point_count, x->curve->shape, bar);
        v = l->offset + l->scale * c;
    } else v = eval_points(l->point, l->point_count, l->shape, bar);
    if (l->clamp) v = fminf(fmaxf(v, l->minimum), l->maximum);
    return v;
}

static float apply_op(wb_lane_op op, float base, float v) {
    return op == WB_LANE_MUL ? base * v : op == WB_LANE_ADD ? base + v : v;
}

typedef struct {
    ma_voice_pool *pool;
    const part_setup *setup;
    float gain[WB_PARTS];
    float lfo_depth[WB_PARTS], lfo_rate[WB_PARTS];
    wb_part_report *report;
} control_state;

static void set_patch_field(ma_patch *q, size_t field, wb_lane_op op, float v, uint64_t *clamps) {
    ma_patch_field_info info;
    double current = 0;
    if (!ma_patch_field_info_at(field, &info) || !ma_patch_field_get(q, field, &current)) return;
    double value = apply_op(op, (float)current, v);
    if (info.integer) value = nearbyint(value);
    if (value < info.minimum) { value = info.minimum; (*clamps)++; }
    if (value > info.maximum) { value = info.maximum; (*clamps)++; }
    (void)ma_patch_field_set(q, field, value);
}

/* Apply every active lane in session order.  Consecutive patch lanes of one
 * part become one set_patch on a copy of the part's base patch; consecutive
 * LFO lanes of one part become one set_lfo. */
static void run_controls(const wb_session *s, const lane_plan *plan, control_state *c, float bar) {
    unsigned i = 0;
    while (i < s->lane_count) {
        const lane_plan *x = &plan[i];
        if (!(bar >= x->first_bar)) { i++; continue; }
        unsigned part = x->part;
        ma_voice_pool *pool = &c->pool[part];
        float v = lane_value(x, bar);
        wb_lane_op op = x->lane->op;
        if (x->call == CALL_PATCH || x->call == CALL_LFO_DEPTH || x->call == CALL_LFO_RATE) {
            bool patch = x->call == CALL_PATCH;
            ma_patch q = c->setup[part].patch;
            float depth = c->lfo_depth[part], rate = c->lfo_rate[part];
            unsigned j = i;
            for (; j < s->lane_count; j++) {
                const lane_plan *y = &plan[j];
                bool same = y->part == part
                    && (patch ? y->call == CALL_PATCH
                              : (y->call == CALL_LFO_DEPTH || y->call == CALL_LFO_RATE));
                if (!same) break;
                if (!(bar >= y->first_bar)) continue;
                float w = j == i ? v : lane_value(y, bar);
                if (patch) set_patch_field(&q, y->field, y->lane->op, w, &c->report[part].lane_clamps);
                else if (y->call == CALL_LFO_DEPTH) depth = apply_op(y->lane->op, depth, w);
                else rate = apply_op(y->lane->op, rate, w);
            }
            if (patch) ma_voice_pool_set_patch(pool, &q);
            else {
                c->lfo_depth[part] = depth;
                c->lfo_rate[part] = rate;
                ma_voice_pool_set_lfo(pool, depth, rate);
            }
            i = j;
            continue;
        }
        const ma_patch *base = &c->setup[part].patch;
        switch (x->call) {
        case CALL_MACRO:
            ma_voice_pool_set_macro(pool, (ma_macro_id)x->field, apply_op(op, base->macro[x->field], v));
            break;
        case CALL_MOD_WHEEL: ma_voice_pool_set_mod_wheel(pool, 0, apply_op(op, 0, v)); break;
        case CALL_CHANNEL_PRESSURE: ma_voice_pool_set_channel_pressure(pool, 0, apply_op(op, 0, v)); break;
        case CALL_PITCH_BEND: ma_voice_pool_set_pitch_bend(pool, 0, apply_op(op, 0, v)); break;
        case CALL_CHARACTER:
            ma_voice_pool_set_character(pool, apply_op(op, s->part[part].character, v));
            break;
        case CALL_GAIN: c->gain[part] = apply_op(op, s->part[part].gain, v); break;
        default: break;
        }
        i++;
    }
}

/* ---- desk mix controls ------------------------------------------------------ */

/* Per-part desk gain and pan.  Inactive (no pan, no CC7/10/11 seen) keeps
 * the plain gain path so sessions without them stay bit-identical to the
 * MA3 exhibits. */
typedef struct {
    bool active;
    float volume, expression, pan, left, right;
} part_mix;

static void set_pan(part_mix *m, float pan) {
    m->active = true;
    m->pan = pan;
    if (pan == 0) {
        m->left = m->right = 1;
        return;
    }
    /* Equal power, unity at the centre: the pool's own pan law. */
    float turns = (pan + 1) * .125f;
    m->left = 1.41421356237f * tw_sin_turns(.25f - turns);
    m->right = 1.41421356237f * tw_sin_turns(turns);
}

static float cc_gain(uint8_t v) {
    float x = (float)v / 127.0f;
    return x * x;
}

static void dispatch_controller(ma_voice_pool *pool, part_mix *m, const wb_note_event *e,
                                wb_part_report *r) {
    r->controllers++;
    switch (e->kind) {
    case 0xb0u:
        switch (e->d1) {
        case 1: ma_voice_pool_set_mod_wheel(pool, 0, (float)e->d2 / 127.0f); return;
        case 7: m->active = true; m->volume = cc_gain(e->d2); return;
        case 11: m->active = true; m->expression = cc_gain(e->d2); return;
        case 10: {
            float pan = ((float)e->d2 - 64.0f) / 63.0f;
            set_pan(m, pan < -1 ? -1 : pan > 1 ? 1 : pan);
            return;
        }
        case 64: ma_voice_pool_set_sustain(pool, 0, e->d2 >= 64); return;
        default: r->controllers_ignored++; return;
        }
    case 0xe0u: {
        /* The MIDI map's law: +-2 semitones, asymmetric 14-bit centre. */
        unsigned word = (unsigned)e->d1 | (unsigned)e->d2 << 7;
        float bend = 2.0f * (float)((int)word - 8192) / (word <= 8192 ? 8192 : 8191);
        ma_voice_pool_set_pitch_bend(pool, 0, bend);
        return;
    }
    case 0xd0u: ma_voice_pool_set_channel_pressure(pool, 0, (float)e->d1 / 127.0f); return;
    case 0xa0u: ma_voice_pool_set_poly_pressure(pool, 0, e->d1, (float)e->d2 / 127.0f); return;
    default: r->controllers_ignored++; return;
    }
}

/* ---- check --------------------------------------------------------------- */

bool wb_render_check(const wb_session *s, const char *dir, const wb_score *score,
                     wb_render_report *report, wb_error *error) {
    *report = (wb_render_report){ 0 };
    if (!wb_session_validate(s, error)) return false;
    part_setup *setup = calloc(WB_PARTS, sizeof *setup);
    ma_voice_pool *pool = malloc(sizeof *pool);
    lane_plan *plan = calloc(WB_LANES, sizeof *plan);
    unsigned (*held)[128] = calloc(WB_PARTS, sizeof *held);
    bool ok = setup && pool && plan && held;
    if (!ok) fail(error, "out of memory");
    ok = ok && setup_parts(s, dir, setup, error) && plan_lanes(s, plan, error);
    uint64_t frames = ok ? wb_render_length(s, score) : 0;
    unsigned active[WB_PARTS] = { 0 };
    for (size_t i = 0; ok && i < score->count; i++) {
        const wb_note_event *e = &score->event[i];
        if (e->kind) {
            if (e->part >= s->part_count) ok = fail(error, "controller event %zu outside the parts", i);
            continue;
        }
        if (e->part >= s->part_count || e->note >= 128 || e->velocity >= 128)
            ok = fail(error, "event %zu outside the session's parts or MIDI range", i);
        else if (e->frame >= frames)
            ok = fail(error, "part '%s': event at frame %llu after the end (%llu frames)",
                      s->part[e->part].id, (unsigned long long)e->frame, (unsigned long long)frames);
        else if (e->on) {
            held[e->part][e->note]++;
            report->part[e->part].notes++;
            if (++active[e->part] > report->part[e->part].held_peak)
                report->part[e->part].held_peak = active[e->part];
        } else if (!held[e->part][e->note]) {
            /* render_midi tolerates stray releases; the desk does not. */
            if (!score->compat)
                ok = fail(error, "part '%s': release of note %u that is not held", s->part[e->part].id, e->note);
        }
        else {
            held[e->part][e->note]--;
            active[e->part]--;
        }
    }
    for (unsigned p = 0; ok && p < s->part_count; p++) {
        const wb_part *part = &s->part[p];
        if (active[p] && !score->compat) ok = fail(error, "part '%s': %u notes never released", part->id, active[p]);
        else if (part->engine != WB_PART_MAMUT) continue;
        else if (report->part[p].held_peak * part->unison > part->limit)
            ok = fail(error, "part '%s': held peak %u x unison %u exceeds limit %u",
                      part->id, report->part[p].held_peak, part->unison, part->limit);
        else if (!ma_voice_pool_init(pool, (float)s->head.rate, &setup[p].config, &setup[p].patch))
            ok = fail(error, "part '%s': the pool rejects its configuration", part->id);
    }
    report->total_frames = frames;
    free(setup);
    free(pool);
    free(plan);
    free(held);
    return ok;
}

/* ---- render -------------------------------------------------------------- */

static double now_seconds(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + t.tv_nsec * 1e-9;
}

bool wb_render(const wb_session *s, const char *dir, const wb_score *score,
               const wb_render_options *options, wb_render_report *report, wb_error *error) {
    double started = now_seconds();
    if (!wb_render_check(s, dir, score, report, error)) return false;
    uint64_t total = report->total_frames;
    uint64_t frames = options->frames && options->frames < total ? options->frames : total;
    uint64_t begin = options->preroll_from < frames ? options->preroll_from : 0;
    uint64_t measure_from = options->measure_from > begin ? options->measure_from : begin;
    if (measure_from >= frames) return fail(error, "empty measured window");
    unsigned stems = s->stem_count, outputs = stems + 1;
    report->frames = frames - measure_from;
    report->measure_from = measure_from;
    report->measure_to = frames;
    report->outputs = outputs;
    report->section_count = s->form.section_count;

    part_setup *setup = calloc(WB_PARTS, sizeof *setup);
    ma_voice_pool *pools = calloc(s->part_count, sizeof *pools);
    lane_plan *plan = calloc(WB_LANES, sizeof *plan);
    float (*pcm)[2 * BLOCK] = calloc(outputs, sizeof *pcm);
    wav_f32_writer *writers = calloc(outputs, sizeof *writers);
    bool ok = setup && pools && plan && pcm && writers;
    if (!ok) fail(error, "out of memory");
    ok = ok && setup_parts(s, dir, setup, error) && plan_lanes(s, plan, error);
    if (ok && options->engine_scalar)
        for (unsigned p = 0; p < s->part_count; p++) setup[p].config.engine = MA_POOL_ENGINE_SCALAR;

    wb_engine_instance engines[WB_PARTS] = { 0 };
    for (unsigned p = 0; ok && p < s->part_count; p++) {
        if (setup[p].engine == WB_PART_ORGAN || setup[p].engine == WB_PART_EP73) {
            bool opened_engine = setup[p].engine == WB_PART_ORGAN
                ? wb_engine_open_organ(&engines[p], s->head.rate, &setup[p].twreg)
                : wb_engine_open_ep(&engines[p], s->head.rate, &setup[p].epset);
            if (!opened_engine) ok = fail(error, "out of memory");
            continue;
        }
        if (!ma_voice_pool_init(&pools[p], (float)s->head.rate, &setup[p].config, &setup[p].patch)) {
            ok = fail(error, "part '%s': pool init failed", s->part[p].id);
            break;
        }
        ma_voice_pool_set_character(&pools[p], s->part[p].character);
        ma_effects_set_chorus(&pools[p].effects, setup[p].chorus);
        ma_effects_set_reverb(&pools[p].effects, setup[p].reverb);
    }

    unsigned opened = 0;
    if (ok && options->prefix) {
        for (; opened < outputs; opened++) {
            char path[4400];
            snprintf(path, sizeof path, "%s_%s.wav", options->prefix,
                     opened == stems ? "mix" : s->stem[opened].id);
            if (wav_f32_open(&writers[opened], path, frames - measure_from, s->head.rate, 2)) {
                ok = fail(error, "cannot open %s", path);
                break;
            }
        }
    }

    /* Section boundaries in frames. */
    uint64_t section_start[WB_SECTIONS], section_end[WB_SECTIONS];
    for (unsigned i = 0; i < s->form.section_count; i++) {
        section_start[i] = wb_timemap_bar_frame(&score->map, s->form.section[i].start_bar);
        section_end[i] = wb_timemap_bar_frame(&score->map, s->form.section[i].end_bar);
    }

    part_mix mix[WB_PARTS];
    for (unsigned p = 0; p < s->part_count; p++) {
        mix[p] = (part_mix){ .volume = 1, .expression = 1, .pan = s->part[p].pan, .left = 1, .right = 1 };
        if (s->part[p].pan != 0) set_pan(&mix[p], s->part[p].pan);
    }
    control_state control = { .pool = pools, .setup = setup, .report = report->part };
    for (unsigned p = 0; p < s->part_count; p++) control.gain[p] = s->part[p].gain;
    uint64_t fade_in = (uint64_t)llround(s->form.fade_in_ms * s->head.rate / 1000.0);
    uint64_t fade_out = (uint64_t)llround(s->form.fade_out_ms * s->head.rate / 1000.0);
    size_t next = 0;
    unsigned control_frames = s->head.control_frames;
    bool compat = s->head.compat == WB_COMPAT_RENDER_MIDI;
    if (ok && begin) {
        /* Pre-roll: notes sounding at `begin` are struck there; the last
         * value of every controller stream is applied first. */
        unsigned char *held = calloc(score->count ? score->count : 1, 1);
        if (!held) ok = fail(error, "out of memory");
        while (ok && next < score->count && score->event[next].frame < begin) {
            const wb_note_event *e = &score->event[next];
            if (!e->kind && e->on) held[next] = 1;
            else if (!e->kind)
                for (size_t k = 0; k < next; k++) {
                    const wb_note_event *x = &score->event[k];
                    if (held[k] && x->part == e->part && x->note == e->note) { held[k] = 0; break; }
                }
            else {
                held[next] = 2;
                for (size_t k = 0; k < next; k++) {
                    const wb_note_event *x = &score->event[k];
                    bool same = held[k] == 2 && x->part == e->part && x->kind == e->kind
                             && (e->kind == 0xd0u || e->kind == 0xe0u || x->d1 == e->d1);
                    if (same) held[k] = 0;
                }
            }
            next++;
        }
        run_controls(s, plan, &control, wb_timemap_bar(&score->map, begin));
        for (unsigned pass = 2; ok && pass >= 1; pass--)
            for (size_t k = 0; ok && k < next; k++) {
                if (held[k] != pass) continue;
                const wb_note_event *e = &score->event[k];
                if (engines[e->part].kind) wb_engine_midi(&engines[e->part], e->status, e->d1 ? e->d1 : e->note, e->d2);
                else if (pass == 2) dispatch_controller(&pools[e->part], &mix[e->part], e, &report->part[e->part]);
                else if (!ma_voice_pool_note_on(&pools[e->part], 0, e->note, e->velocity))
                    ok = fail(error, "part '%s': pre-roll note-on refused", s->part[e->part].id);
                else ma_voice_pool_set_poly_pressure(&pools[e->part], 0, e->note, s->part[e->part].pressure);
            }
        free(held);
    }
    FILE *part_file = nullptr;
    float part_block[2 * BLOCK] = { 0 };
    char part_temporary[4400] = "";
    if (ok && options->part_out) {
        snprintf(part_temporary, sizeof part_temporary, "%s.partial", options->part_out);
        part_file = fopen(part_temporary, "wb");
        if (!part_file) ok = fail(error, "cannot write %s", part_temporary);
    }
    enum { WINDOW = 4800 };
    uint64_t window_count = (frames - measure_from + WINDOW - 1) / WINDOW;
    wb_peak *windows = calloc(window_count ? window_count : 1, sizeof *windows);
    if (!windows) ok = fail(error, "out of memory");
    unsigned section = 0;
    uint64_t progress_step = 30ull * s->head.rate;

    for (uint64_t at = begin; ok && at < frames;) {
        size_t count = frames - at < BLOCK ? (size_t)(frames - at) : BLOCK;
        if (at < measure_from && at + count > measure_from) count = (size_t)(measure_from - at);
        for (size_t i = 0; ok && i < count; i++) {
            uint64_t frame = at + i;
            if (frame % control_frames == 0)
                run_controls(s, plan, &control, wb_timemap_bar(&score->map, frame));
            while (next < score->count && score->event[next].frame == frame) {
                const wb_note_event *e = &score->event[next++];
                if (options->solo_part && e->part != options->only_part) continue;
                ma_voice_pool *pool = &pools[e->part];
                if (engines[e->part].kind) {
                    /* Organ and ep73: the raw message through the driver's map. */
                    wb_engine_midi(&engines[e->part], e->status, e->kind ? e->d1 : e->note, e->d2);
                    if (e->kind) report->part[e->part].controllers++;
                    continue;
                }
                if (e->kind) {
                    dispatch_controller(pool, &mix[e->part], e, &report->part[e->part]);
                    continue;
                }
                if (e->on) {
                    if (!ma_voice_pool_note_on(pool, 0, e->note, e->velocity)) {
                        ok = fail(error, "part '%s': note-on refused", s->part[e->part].id);
                        break;
                    }
                    ma_voice_pool_set_poly_pressure(pool, 0, e->note, s->part[e->part].pressure);
                } else if (!ma_voice_pool_note_off(pool, 0, e->note, 0)) {
                    ok = fail(error, "part '%s': note-off refused", s->part[e->part].id);
                    break;
                }
            }
            ma_frame stem[WB_STEMS] = { 0 };
            bool stem_set[WB_STEMS] = { 0 };
            unsigned active = 0;
            bool measured = frame >= measure_from;
            for (unsigned p = 0; p < s->part_count; p++) {
                if (options->solo_part && p != options->only_part) continue;
                ma_frame f;
                if (engines[p].kind) {
                    tw_stereo y = wb_engine_tick(&engines[p]);
                    f = (ma_frame){ y.l, y.r };
                } else f = ma_voice_pool_tick(&pools[p]);
                float gain = control.gain[p];
                float l, r;
                if (!mix[p].active) {
                    /* The MA3 desk path, kept operation for operation. */
                    l = f.left * gain;
                    r = f.right * gain;
                } else {
                    float g = gain * mix[p].volume * mix[p].expression;
                    l = f.left * g * mix[p].left;
                    r = f.right * g * mix[p].right;
                }
                if (compat && !stem_set[setup[p].stem]) {
                    /* render_midi writes the instrument's samples as they
                     * are: no +0 from a zero start (keeps -0.0). */
                    stem[setup[p].stem] = (ma_frame){ l, r };
                    stem_set[setup[p].stem] = true;
                } else {
                    stem[setup[p].stem].left += l;
                    stem[setup[p].stem].right += r;
                }
                if (part_file && p == options->only_part) {
                    part_block[2 * i] = l;
                    part_block[2 * i + 1] = r;
                }
                if (measured) {
                    report->part_square[p] += (double)l * l + (double)r * r;
                    if (fabsf(l) > report->part_peak[p]) report->part_peak[p] = fabsf(l);
                    if (fabsf(r) > report->part_peak[p]) report->part_peak[p] = fabsf(r);
                }
                if (!engines[p].kind) active += pools[p].diagnostics.active_slots;
            }
            if (active > report->aggregate_peak) report->aggregate_peak = active;
            /* One presentation fade on every stem; no limiter, no riding. */
            float fade = 1;
            if (fade_in && frame < fade_in) fade = (float)frame / (float)fade_in;
            if (fade_out && frame >= total - fade_out)
                fade = (float)(total - 1 - frame) / (float)(fade_out - 1);
            ma_frame mix = { 0 };
            for (unsigned st = 0; st < stems; st++) {
                ma_frame f = { stem[st].left * fade, stem[st].right * fade };
                pcm[st][2 * i] = f.left;
                pcm[st][2 * i + 1] = f.right;
                if (compat && st == 0) mix = f;
                else {
                    mix.left += f.left;
                    mix.right += f.right;
                }
            }
            pcm[stems][2 * i] = mix.left;
            pcm[stems][2 * i + 1] = mix.right;
            if (measured) {
                wb_peak *w = &windows[(frame - measure_from) / WINDOW];
                bool right = fabsf(mix.right) > fabsf(mix.left);
                float m = right ? mix.right : mix.left;
                if (fabsf(m) > fabsf(w->mix)) {
                    w->frame = frame;
                    w->mix = m;
                    for (unsigned st = 0; st < stems; st++) w->stem[st] = pcm[st][2 * i + (right ? 1 : 0)];
                }
            }
            while (section < s->form.section_count && frame >= section_end[section]) section++;
            if (measured && section < s->form.section_count && frame >= section_start[section]) {
                report->section_frames[section]++;
                for (unsigned o = 0; o < outputs; o++)
                    report->section_square[section][o] += (double)pcm[o][2 * i] * pcm[o][2 * i]
                                                        + (double)pcm[o][2 * i + 1] * pcm[o][2 * i + 1];
            }
        }
        if (part_file && fwrite(part_block, sizeof(float), 2 * count, part_file) != 2 * count)
            ok = fail(error, "part cache write failed");
        bool measured_block = at >= measure_from;
        for (unsigned o = 0; ok && measured_block && o < outputs; o++) {
            wb_level *l = &report->level[o];
            for (size_t i = 0; i < 2 * count; i++) {
                float x = pcm[o][i];
                if (compat && isfinite(x) && fabsf(x) > 1) report->overs++;
                else if (!isfinite(x) || fabsf(x) > 1) {
                    ok = fail(error, "%s: sample %g at frame %llu (non-finite or above 0 dBFS)",
                              o == stems ? "mix" : s->stem[o].id, (double)x,
                              (unsigned long long)(at + i / 2));
                    break;
                }
                if (fabsf(x) > l->peak) l->peak = fabsf(x);
                l->square += (double)x * x;
            }
            l->hash = tw_fnv1a64(pcm[o], 2 * count * sizeof(float), l->hash);
            if (ok && options->prefix && wav_f32_write(&writers[o], pcm[o], count))
                ok = fail(error, "WAV write failed");
        }
        at += count;
        if (options->cancel && *options->cancel) ok = fail(error, "cancelled");
        if (options->progress && (at % progress_step == 0 || at == frames))
            options->progress(options->user, at, frames);
    }

    for (unsigned p = 0; p < s->part_count && pools; p++) {
        wb_part_report *r = &report->part[p];
        if (engines[p].kind) {
            r->out_of_compass = wb_engine_out_of_compass(&engines[p]);
            wb_engine_close(&engines[p]);
            continue;
        }
        ma_pool_diagnostics d = pools[p].diagnostics;
        r->slot_peak = d.peak_slots;
        r->steals_released = d.steals_released;
        r->steals_sustained = d.steals_sustained;
        r->steals_held = d.steals_held;
        r->sleeps = d.sleeps;
        r->fx_clips = pools[p].effects.clips;
        r->fx_sanitizations = pools[p].effects.sanitizations;
        r->active_at_end = d.active_slots;
        if (!ok || (options->solo_part && p != options->only_part)) continue;
        if (d.steals_held || d.steals_sustained || pools[p].effects.sanitizations)
            ok = fail(error, "part '%s': held/sustained steal or FX sanitization", s->part[p].id);
        else if (frames == total && !begin && d.active_slots)
            ok = fail(error, "part '%s': still sounding at the end", s->part[p].id);
    }
    for (unsigned o = 0; o < opened; o++) {
        if (ok) {
            if (wav_f32_close(&writers[o])) ok = fail(error, "WAV close failed");
        } else wav_f32_abort(&writers[o]);
    }
    if (part_file) {
        if (fclose(part_file)) ok = false;
        if (ok && rename(part_temporary, options->part_out)) ok = fail(error, "cannot commit %s", options->part_out);
        if (!ok) remove(part_temporary);
    }
    /* The loudest windows, loudest first. */
    for (uint64_t w = 0; windows && w < window_count; w++) {
        wb_peak x = windows[w];
        unsigned at = report->peak_count;
        while (at && fabsf(report->peak[at - 1].mix) < fabsf(x.mix)) at--;
        if (at >= WB_PEAKS) continue;
        unsigned last = report->peak_count < WB_PEAKS ? report->peak_count : WB_PEAKS - 1;
        memmove(&report->peak[at + 1], &report->peak[at], (last - at) * sizeof x);
        report->peak[at] = x;
        if (report->peak_count < WB_PEAKS) report->peak_count++;
    }
    free(windows);
    report->wall_seconds = now_seconds() - started;
    free(setup);
    free(pools);
    free(plan);
    free(pcm);
    free(writers);
    return ok;
}
