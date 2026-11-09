#define _POSIX_C_SOURCE 200809L
#include "wb_orch.h"

#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "wb_hash.h"

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

void wb_derived_free(wb_derived *d) {
    for (unsigned p = 0; p < WB_PARTS; p++) {
        free(d->part[p].note);
        free(d->part[p].ctl);
    }
    wb_buf_free(&d->midi);
    memset(d, 0, sizeof *d);
}

static bool add(wb_opart *p, wb_onote n) {
    if (p->count == p->capacity) {
        size_t capacity = p->capacity ? 2 * p->capacity : 256;
        wb_onote *grown = realloc(p->note, capacity * sizeof *grown);
        if (!grown) return false;
        p->note = grown;
        p->capacity = capacity;
    }
    p->note[p->count++] = n;
    p->taken++;
    return true;
}

static bool add_ctl(wb_opart *p, wb_octl c) {
    if (p->ctl_count == p->ctl_capacity) {
        size_t capacity = p->ctl_capacity ? 2 * p->ctl_capacity : 256;
        wb_octl *grown = realloc(p->ctl, capacity * sizeof *grown);
        if (!grown) return false;
        p->ctl = grown;
        p->ctl_capacity = capacity;
    }
    c.order = (uint32_t)p->ctl_count;
    p->ctl[p->ctl_count++] = c;
    return true;
}

/* Bar grid of the master source: the meters in force, in ticks. */
typedef struct {
    unsigned count;
    struct { uint64_t tick; double bar; unsigned ticks_per_bar; } at[WB_ORCH_METERS];
} bar_grid;

static void grid_init(bar_grid *g, const wb_smf_meter *meter, unsigned count, unsigned ppq) {
    g->count = 0;
    double bar = 0;
    uint64_t tick = 0;
    unsigned tpb = 4 * ppq;
    g->at[g->count++] = (typeof(g->at[0])){ 0, 0, tpb };
    for (unsigned i = 0; i < count && g->count < WB_ORCH_METERS; i++) {
        unsigned denominator = 1u << meter[i].denominator_log2;
        unsigned next = meter[i].numerator * 4 * ppq / (denominator ? denominator : 4);
        if (!next) continue;
        bar += (double)(meter[i].tick - tick) / tpb;
        tick = meter[i].tick;
        tpb = next;
        if (meter[i].tick == g->at[g->count - 1].tick) g->count--;
        g->at[g->count++] = (typeof(g->at[0])){ tick, bar, tpb };
    }
}

static uint64_t grid_tick(const bar_grid *g, double bar) {
    unsigned i = 0;
    while (i + 1 < g->count && g->at[i + 1].bar <= bar) i++;
    double ticks = (double)g->at[i].tick + (bar - g->at[i].bar) * g->at[i].ticks_per_bar;
    return ticks <= 0 ? 0 : (uint64_t)llround(ticks);
}

static double grid_bar(const bar_grid *g, uint64_t tick) {
    unsigned i = 0;
    while (i + 1 < g->count && g->at[i + 1].tick <= tick) i++;
    return g->at[i].bar + (double)(tick - g->at[i].tick) / g->at[i].ticks_per_bar;
}

static unsigned grid_tpb(const bar_grid *g, uint64_t tick) {
    unsigned i = 0;
    while (i + 1 < g->count && g->at[i + 1].tick <= tick) i++;
    return g->at[i].ticks_per_bar;
}

static bool match_track(const wb_rule *r, const wb_smf *smf, unsigned t) {
    if (r->track >= 0 && (unsigned)r->track != t) return false;
    if (r->track_name[0] && strcmp(r->track_name, smf->track[t].name)) return false;
    return true;
}

static int clamp_note(int n, const wb_rule *r) {
    if (r->fold_below >= 0) while (n < r->fold_below && n + 12 <= 127) n += 12;
    if (r->fold_above >= 0) while (n > r->fold_above && n - 12 >= 0) n -= 12;
    return n;
}

/* Skyline: a note is the top voice when no other candidate sounding at its
 * start is higher. */
static void skyline(const wb_smf_note *const *cand, size_t n, bool *top) {
    for (size_t i = 0; i < n; i++) {
        top[i] = true;
        for (size_t j = 0; j < n && top[i]; j++) {
            if (j == i) continue;
            bool sounding = cand[j]->start <= cand[i]->start && cand[i]->start < cand[j]->end;
            if (sounding && cand[j]->note > cand[i]->note) top[i] = false;
        }
    }
}

static unsigned ctl_kind(uint8_t status, uint8_t d1) {
    switch (status & 0xf0u) {
    case 0xb0u: return d1;
    case 0xe0u: return WB_CTL_BEND;
    case 0xd0u: return WB_CTL_PRESSURE;
    case 0xa0u: return WB_CTL_POLY;
    default: return WB_CTL_COUNT;
    }
}

static bool apply_rule(const wb_rule *r, const wb_source *src, const wb_smf *smf, const bar_grid *grid,
                       unsigned master_ppq, wb_opart *part, wb_derived *d, wb_error *error) {
    bool any_track = false;
    int64_t offset = (int64_t)src->offset_bars * grid_tpb(grid, 0);
    for (unsigned t = 0; t < smf->track_count; t++) {
        if (!match_track(r, smf, t)) continue;
        wb_smf_notes notes;
        if (!wb_smf_pair_notes(smf, t, &notes)) return fail(error, "out of memory");
        d->unmatched_offs += notes.unmatched_offs;
        d->stuck_notes += notes.stuck;
        any_track = any_track || notes.count;
        const wb_smf_note **cand = calloc(notes.count ? notes.count : 1, sizeof *cand);
        bool *top = calloc(notes.count ? notes.count : 1, sizeof *top);
        if (!cand || !top) {
            free(cand); free(top); wb_smf_notes_free(&notes);
            return fail(error, "out of memory");
        }
        size_t n = 0;
        for (size_t i = 0; i < notes.count; i++) {
            const wb_smf_note *x = &notes.note[i];
            if (r->channel >= 0 && x->channel != (unsigned)(r->channel - 1)) continue;
            if (r->program >= 0 && x->program != (unsigned)r->program) continue;
            if (r->has_notes && !wb_noteset_has(&r->notes, x->note)) continue;
            cand[n++] = x;
        }
        if (r->voice != WB_VOICE_ALL) skyline(cand, n, top);
        bool ok = true;
        for (size_t i = 0; i < n && ok; i++) {
            const wb_smf_note *x = cand[i];
            if (r->voice == WB_VOICE_TOP && !top[i]) continue;
            if (r->voice == WB_VOICE_REST && top[i]) continue;
            /* Source ticks to master ticks, then the source's bar offset. */
            int64_t start = (int64_t)(x->start * master_ppq / smf->division) + offset;
            int64_t end = (int64_t)(x->end * master_ppq / smf->division) + offset;
            if (start < 0) continue;
            double bar = grid_bar(grid, (uint64_t)start);
            if (r->bar_in >= 0 && bar < r->bar_in) continue;
            if (r->bar_out >= 0 && bar >= r->bar_out) continue;
            if ((uint64_t)(end - start) < r->min_length_ticks) continue;
            if (r->ring_max_bars > 0) {
                int64_t longest = (int64_t)floor(r->ring_max_bars * grid_tpb(grid, (uint64_t)start));
                if (end > start + longest) end = start + longest;
            }
            int pitch = r->map_to >= 0 ? r->map_to : (int)x->note + r->transpose;
            pitch = clamp_note(pitch, r);
            if (pitch < 0 || pitch > 127) continue;
            int velocity = (int)(x->velocity * r->velocity_num / r->velocity_den) + r->velocity_offset;
            if (velocity < (int)r->velocity_min) velocity = (int)r->velocity_min;
            if (velocity > (int)r->velocity_max) velocity = (int)r->velocity_max;
            ok = add(part, (wb_onote){ (uint64_t)start, (uint64_t)end, (uint8_t)pitch, (uint8_t)velocity });
        }
        /* Forwarded controller streams of the same track and channel. */
        const wb_smf_track *track = &smf->track[t];
        for (size_t i = 0; ok && !wb_ctlset_empty(&r->controllers) && i < track->count; i++) {
            const wb_smf_event *e = &smf->event[track->first + i];
            if (e->status == 0xffu) continue;
            unsigned kind = ctl_kind(e->status, e->d1);
            if (kind == WB_CTL_COUNT || !wb_ctlset_has(&r->controllers, kind)) continue;
            if (r->channel >= 0 && (e->status & 15u) != (unsigned)(r->channel - 1)) continue;
            int64_t tick = (int64_t)(e->tick * master_ppq / smf->division) + offset;
            ok = add_ctl(part, (wb_octl){ .tick = tick < 0 ? 0 : (uint64_t)tick,
                                          .kind = (uint8_t)(e->status & 0xf0u), .d1 = e->d1, .d2 = e->d2 });
        }
        free(cand);
        free(top);
        wb_smf_notes_free(&notes);
        if (!ok) return fail(error, "out of memory");
    }
    if (!any_track) return fail(error, "rule for part '%s': no notes match in source '%s'", r->part, src->id);
    return true;
}

static int velocity_clamp(int v) { return v < 1 ? 1 : v > 127 ? 127 : v; }

static bool apply_groove(const wb_groove *g, const bar_grid *grid, wb_opart *part, wb_error *error) {
    for (unsigned u = 0; u < g->use_count; u++) {
        const wb_use *use = &g->use[u];
        const wb_pattern *p = &g->pattern[use->pattern];
        for (int32_t bar = use->from_bar; bar < use->to_bar; bar++) {
            bool hit = false;
            for (unsigned h = 0; h < g->hit_count; h++) hit = hit || g->hit[h].bar == bar;
            if (hit) continue;
            uint64_t at = grid_tick(grid, bar);
            unsigned tpb = grid_tpb(grid, at);
            if (tpb % g->steps_per_bar)
                return fail(error, "groove for '%s': %u ticks per bar do not split into %u steps",
                            g->part, tpb, g->steps_per_bar);
            unsigned step_ticks = tpb / g->steps_per_bar;
            for (unsigned i = 0; i < p->step_count; i++) {
                unsigned step = p->step[i];
                int v = (int)p->velocity + p->accent[i];
                if (g->wobble_m)
                    v += (int)(((uint64_t)bar * g->wobble_a + (uint64_t)step * g->wobble_b) % g->wobble_m)
                       - (int)(g->wobble_m / 2);
                uint64_t start = at + (uint64_t)step * step_ticks;
                if (!add(part, (wb_onote){ start, start + g->length_ticks, (uint8_t)g->note,
                                           (uint8_t)velocity_clamp(v) }))
                    return fail(error, "out of memory");
            }
        }
    }
    for (unsigned h = 0; h < g->hit_count; h++) {
        uint64_t at = grid_tick(grid, g->hit[h].bar);
        if (!add(part, (wb_onote){ at, at + g->length_ticks, (uint8_t)g->note,
                                   (uint8_t)g->hit[h].velocity }))
            return fail(error, "out of memory");
    }
    return true;
}

static int compare_dedupe(const void *a, const void *b) {
    const wb_onote *x = a, *y = b;
    if (x->note != y->note) return x->note < y->note ? -1 : 1;
    if (x->start != y->start) return x->start < y->start ? -1 : 1;
    return (x->velocity < y->velocity) - (x->velocity > y->velocity);
}

static int compare_final(const void *a, const void *b) {
    const wb_onote *x = a, *y = b;
    if (x->start != y->start) return x->start < y->start ? -1 : 1;
    return (x->note > y->note) - (x->note < y->note);
}

/* qsort is not stable; the sorts here use complete keys or keys whose ties
 * are merged identically whatever their order. */
static void finish_part(wb_opart *p, wb_dedupe dedupe, uint64_t start_tick, uint64_t end_tick) {
    size_t n = 0;
    for (size_t i = 0; i < p->count; i++) {
        wb_onote x = p->note[i];
        if (x.start >= end_tick) continue;
        if (x.end > end_tick) x.end = end_tick;
        p->note[n++] = x;
    }
    p->count = n;
    if (dedupe != WB_DEDUPE_OFF && n) {
        qsort(p->note, n, sizeof *p->note, compare_dedupe);
        size_t kept = 0;
        for (size_t i = 0; i < n; i++) {
            wb_onote x = p->note[i];
            if (kept && p->note[kept - 1].note == x.note && x.start < p->note[kept - 1].end) {
                wb_onote *prev = &p->note[kept - 1];
                if (x.start == prev->start) {
                    if (x.end > prev->end) prev->end = x.end;
                    p->merged++;
                    continue;
                }
                if (dedupe == WB_DEDUPE_KEEP_LOUDEST && x.velocity <= prev->velocity) {
                    p->merged++;
                    continue;
                }
                prev->end = x.start;
                p->cut++;
            }
            p->note[kept++] = x;
        }
        n = kept;
    }
    size_t m = 0;
    for (size_t i = 0; i < n; i++) {
        wb_onote x = p->note[i];
        if (x.end <= x.start || x.start < start_tick) continue;
        x.start -= start_tick;
        x.end -= start_tick;
        p->note[m++] = x;
    }
    p->count = m;
    qsort(p->note, m, sizeof *p->note, compare_final);

    /* Controllers: the last value of each stream before the form start
     * moves to tick 0; events at or after the end are dropped. */
    size_t c = 0;
    for (size_t i = 0; i < p->ctl_count; i++) {
        wb_octl x = p->ctl[i];
        if (x.tick >= end_tick) continue;
        if (x.tick < start_tick) {
            bool later = false;
            for (size_t j = i + 1; j < p->ctl_count && !later; j++) {
                const wb_octl *y = &p->ctl[j];
                bool same = y->kind == x.kind && (x.kind == 0xd0u || x.kind == 0xe0u || y->d1 == x.d1);
                later = same && y->tick < start_tick && y->tick >= x.tick;
            }
            if (later) continue;
            x.tick = 0;
        } else x.tick -= start_tick;
        p->ctl[c++] = x;
    }
    p->ctl_count = c;
}

/* Hand edits from the Roll, after the rules: deletes match a note of the
 * same pitch starting within half a 1/64 bar; adds append and re-sort. */
static bool apply_overlays(const wb_session *s, unsigned part, const bar_grid *grid, wb_derived *d,
                           wb_error *error) {
    wb_opart *p = &d->part[part];
    bool changed = false;
    for (unsigned i = 0; i < s->overlay_count; i++) {
        const wb_overlay *o = &s->overlay[i];
        if (strcmp(o->part, s->part[part].id)) continue;
        uint64_t at = grid_tick(grid, o->bar);
        if (at < d->start_tick || at >= d->end_tick) continue;
        uint64_t start = at - d->start_tick;
        uint64_t slack = grid_tpb(grid, at) / 128;
        if (o->op == WB_OVERLAY_DELETE) {
            for (size_t k = 0; k < p->count; k++) {
                const wb_onote *n = &p->note[k];
                uint64_t gap = n->start > start ? n->start - start : start - n->start;
                if (n->note == o->note && gap <= slack) {
                    memmove(&p->note[k], &p->note[k + 1], (p->count - k - 1) * sizeof *p->note);
                    p->count--;
                    changed = true;
                    break;
                }
            }
        } else {
            uint64_t end = grid_tick(grid, o->bar + o->length_bars);
            if (end > d->end_tick) end = d->end_tick;
            if (end <= at) continue;
            if (!add(p, (wb_onote){ start, end - d->start_tick, (uint8_t)o->note, (uint8_t)o->velocity }))
                return fail(error, "out of memory");
            changed = true;
        }
    }
    if (changed) qsort(p->note, p->count, sizeof *p->note, compare_final);
    return true;
}

typedef struct { uint64_t tick; uint8_t kind; uint32_t order; uint8_t bytes[3]; } timed;

static int compare_timed(const void *a, const void *b) {
    const timed *x = a, *y = b;
    if (x->tick != y->tick) return x->tick < y->tick ? -1 : 1;
    if (x->kind != y->kind) return x->kind < y->kind ? -1 : 1;
    return (x->order > y->order) - (x->order < y->order);
}

static void held_peak(wb_opart *p) {
    /* Edges sorted by tick with releases first, as the generator counts. */
    size_t n = 2 * p->count;
    timed *edge = malloc((n ? n : 1) * sizeof *edge);
    if (!edge) return;
    for (size_t i = 0; i < p->count; i++) {
        edge[2 * i] = (timed){ p->note[i].start, 1, (uint32_t)(2 * i), { 0 } };
        edge[2 * i + 1] = (timed){ p->note[i].end, 0, (uint32_t)(2 * i + 1), { 0 } };
    }
    qsort(edge, n, sizeof *edge, compare_timed);
    unsigned active = 0;
    for (size_t i = 0; i < n; i++) {
        if (edge[i].kind) active++;
        else active--;
        if (active > p->held_peak) p->held_peak = active;
    }
    free(edge);
}

static bool write_midi(const wb_session *s, wb_derived *d, wb_error *error) {
    wb_buf *out = &d->midi;
    wb_smf_header(out, 1, 1 + s->part_count, d->ppq);
    wb_smf_track_writer w;
    wb_smf_track_begin(&w);
    const char *title = s->head.title[0] ? s->head.title : s->head.name;
    wb_smf_track_meta(&w, 0, 0x03, title, strlen(title));
    /* Meters and tempos interleaved by tick, meters first at equal ticks. */
    unsigned m = 0, t = 0;
    while (m < d->meter_count || t < d->tempo_count) {
        bool meter = m < d->meter_count && (t == d->tempo_count || d->meter[m].tick <= d->tempo[t].tick);
        if (meter) {
            const wb_smf_meter *x = &d->meter[m++];
            uint8_t payload[4] = { x->numerator, x->denominator_log2, x->clocks, x->thirty_seconds };
            wb_smf_track_meta(&w, x->tick, 0x58, payload, 4);
        } else {
            const wb_smf_tempo *x = &d->tempo[t++];
            uint8_t payload[3] = { (uint8_t)(x->us_per_quarter >> 16), (uint8_t)(x->us_per_quarter >> 8),
                                   (uint8_t)x->us_per_quarter };
            wb_smf_track_meta(&w, x->tick, 0x51, payload, 3);
        }
    }
    wb_smf_track_end(&w, out);
    for (unsigned p = 0; p < s->part_count; p++) {
        const wb_opart *part = &d->part[p];
        wb_smf_track_begin(&w);
        wb_smf_track_meta(&w, 0, 0x03, s->part[p].id, strlen(s->part[p].id));
        /* At equal ticks: releases, then controllers, then attacks. */
        size_t n = 2 * part->count + part->ctl_count;
        timed *ev = malloc((n ? n : 1) * sizeof *ev);
        if (!ev) return fail(error, "out of memory");
        for (size_t i = 0; i < part->count; i++) {
            const wb_onote *x = &part->note[i];
            ev[2 * i] = (timed){ x->start, 2, (uint32_t)(2 * i), { (uint8_t)(0x90u | p), x->note, x->velocity } };
            ev[2 * i + 1] = (timed){ x->end, 0, (uint32_t)(2 * i + 1), { (uint8_t)(0x80u | p), x->note, 0 } };
        }
        for (size_t i = 0; i < part->ctl_count; i++) {
            const wb_octl *x = &part->ctl[i];
            ev[2 * part->count + i] = (timed){ x->tick, 1, (uint32_t)(2 * part->count + i),
                                               { (uint8_t)(x->kind | p), x->d1, x->d2 } };
        }
        qsort(ev, n, sizeof *ev, compare_timed);
        for (size_t i = 0; i < n; i++)
            wb_smf_track_event(&w, ev[i].tick, ev[i].bytes, (ev[i].bytes[0] & 0xf0u) == 0xd0u ? 2 : 3);
        free(ev);
        wb_smf_track_end(&w, out);
    }
    if (out->failed) return fail(error, "out of memory");
    wb_sha256_bytes_hex(out->data, out->length, d->sha256);
    return true;
}

bool wb_orch_build(const wb_session *s, const char *dir, wb_derived *d, wb_error *error) {
    memset(d, 0, sizeof *d);
    if (!wb_session_validate(s, error)) return false;
    if (s->part_count > 16) return fail(error, "a derived score holds at most 16 parts (one channel each)");
    wb_smf *smf = calloc(s->source_count, sizeof *smf);
    if (!smf) return fail(error, "out of memory");
    bool ok = true;
    for (unsigned i = 0; ok && i < s->source_count; i++) {
        char path[4400], sha[65];
        snprintf(path, sizeof path, "%s/%s", dir, s->source[i].path);
        if (!wb_sha256_file_hex(path, sha)) ok = fail(error, "source '%s': cannot read %s", s->source[i].id, path);
        else if (strcmp(sha, s->source[i].sha256))
            ok = fail(error, "source '%s': %s changed (sha256 %.12s.., session says %.12s..)",
                      s->source[i].id, s->source[i].path, sha, s->source[i].sha256);
        else if (!wb_smf_load(path, &smf[i], error)) ok = false;
    }
    bar_grid grid = { 0 };
    if (ok) {
        /* The first source is the timing master. */
        const wb_smf *master = &smf[0];
        d->ppq = master->division;
        wb_smf_meter meters[WB_ORCH_METERS];
        size_t meter_count = wb_smf_meters(master, meters, WB_ORCH_METERS);
        grid_init(&grid, meters, (unsigned)meter_count, d->ppq);
        d->ticks_per_bar = grid.at[0].ticks_per_bar;
        d->start_tick = grid_tick(&grid, s->form.start_bar);
        d->end_tick = grid_tick(&grid, s->form.end_bar);
        d->part_count = s->part_count;

        /* Derived tempo map: the one in force at the form start moves to tick 0. */
        wb_smf_tempo tempos[WB_ORCH_TEMPOS];
        size_t tempo_count = wb_smf_tempos(master, tempos, WB_ORCH_TEMPOS);
        const wb_source *src0 = &s->source[0];
        uint32_t at_start = 500000;
        for (size_t i = 0; i < tempo_count; i++)
            if (tempos[i].tick <= d->start_tick) at_start = tempos[i].us_per_quarter;
        d->tempo[d->tempo_count++] = (wb_smf_tempo){ 0, src0->us_per_quarter ? src0->us_per_quarter : at_start };
        for (size_t i = 0; !src0->us_per_quarter && i < tempo_count && d->tempo_count < WB_ORCH_TEMPOS; i++)
            if (tempos[i].tick > d->start_tick && tempos[i].tick < d->end_tick)
                d->tempo[d->tempo_count++] = (wb_smf_tempo){ tempos[i].tick - d->start_tick,
                                                             tempos[i].us_per_quarter };
        if (src0->tempo_scale != 1.0)
            for (unsigned i = 0; i < d->tempo_count; i++)
                d->tempo[i].us_per_quarter = (uint32_t)llround(d->tempo[i].us_per_quarter / src0->tempo_scale);
        wb_smf_meter at_meter = { 0, 4, 2, 24, 8 };
        for (size_t i = 0; i < meter_count; i++)
            if (meters[i].tick <= d->start_tick) { at_meter = meters[i]; at_meter.tick = 0; }
        d->meter[d->meter_count++] = at_meter;
        for (size_t i = 0; i < meter_count && d->meter_count < WB_ORCH_METERS; i++)
            if (meters[i].tick > d->start_tick && meters[i].tick < d->end_tick) {
                d->meter[d->meter_count] = meters[i];
                d->meter[d->meter_count++].tick -= d->start_tick;
            }
    }
    for (unsigned i = 0; ok && i < s->rule_count; i++) {
        const wb_rule *r = &s->rule[i];
        int part = wb_find_part(s, r->part), src = wb_find_source(s, r->source);
        ok = apply_rule(r, &s->source[src], &smf[src], &grid, d->ppq, &d->part[part], d, error);
    }
    for (unsigned i = 0; ok && i < s->groove_count; i++) {
        const wb_groove *g = &s->groove[i];
        ok = apply_groove(g, &grid, &d->part[wb_find_part(s, g->part)], error);
    }
    for (unsigned p = 0; ok && p < s->part_count; p++) {
        finish_part(&d->part[p], s->part[p].dedupe, d->start_tick, d->end_tick);
        ok = apply_overlays(s, p, &grid, d, error);
        held_peak(&d->part[p]);
    }
    if (ok) ok = write_midi(s, d, error);
    for (unsigned i = 0; i < s->source_count; i++) wb_smf_free(&smf[i]);
    free(smf);
    if (!ok) wb_derived_free(d);
    return ok;
}

bool wb_orch_write(const wb_derived *d, const wb_session *s, const char *dir, char *path,
                   size_t path_size, wb_error *error) {
    char derived[4200], name[WB_ID_MAX + 1];
    snprintf(derived, sizeof derived, "%s/derived", dir);
    if (mkdir(derived, 0755) && errno != EEXIST) return fail(error, "cannot create %s", derived);
    /* A file-system-safe stem from the session name. */
    size_t n = 0;
    for (const char *c = s->head.name; *c && n < WB_ID_MAX; c++) {
        bool ok = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9')
               || *c == '-' || *c == '_' || *c == '.';
        name[n++] = ok ? *c : '-';
    }
    name[n] = 0;
    if (snprintf(path, path_size, "%s/%s-%.12s.mid", derived, n ? name : "session", d->sha256)
        >= (int)path_size)
        return fail(error, "derived path too long");
    char existing[65];
    if (wb_sha256_file_hex(path, existing)) {
        if (!strcmp(existing, d->sha256)) return true;
        return fail(error, "%s exists with different bytes", path);
    }
    if (!wb_write_file_atomic(path, d->midi.data, d->midi.length))
        return fail(error, "cannot write %s", path);
    return true;
}
