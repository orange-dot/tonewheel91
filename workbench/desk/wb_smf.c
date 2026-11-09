#define _POSIX_C_SOURCE 200809L
#include "wb_smf.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

typedef struct {
    const uint8_t *data;
    size_t pos, end;
} cursor;

static bool vlq(cursor *c, uint32_t *out) {
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; i++) {
        if (c->pos >= c->end) return false;
        uint8_t byte = c->data[c->pos++];
        value = value << 7 | (byte & 0x7fu);
        if (!(byte & 0x80u)) {
            *out = value;
            return true;
        }
    }
    return false;
}

static bool push(wb_smf *s, size_t *capacity, wb_smf_event e) {
    if (s->event_count == *capacity) {
        size_t grown = *capacity ? 2 * *capacity : 1024;
        wb_smf_event *p = realloc(s->event, grown * sizeof *p);
        if (!p) return false;
        s->event = p;
        *capacity = grown;
    }
    e.seq = (uint32_t)s->event_count;
    s->event[s->event_count++] = e;
    return true;
}

bool wb_smf_read(const uint8_t *data, size_t size, wb_smf *s, wb_error *error) {
    memset(s, 0, sizeof *s);
    if (size < 14 || memcmp(data, "MThd", 4) || be32(data + 4) < 6)
        return fail(error, "not a Standard MIDI File");
    s->format = (unsigned)data[8] << 8 | data[9];
    unsigned tracks = (unsigned)data[10] << 8 | data[11];
    s->division = (unsigned)data[12] << 8 | data[13];
    if (s->format == 2) return fail(error, "SMF format 2 (independent sequences) is not supported");
    if (s->format > 2) return fail(error, "unknown SMF format %u", s->format);
    if (s->division & 0x8000u) return fail(error, "SMPTE time division is not supported; PPQ only");
    if (!s->division) return fail(error, "zero PPQ division");
    if (!tracks || tracks > WB_SMF_TRACKS) return fail(error, "%u tracks (1..%d supported)", tracks, WB_SMF_TRACKS);
    if (s->format == 0 && tracks != 1) return fail(error, "format 0 with %u tracks", tracks);
    s->data = malloc(size);
    if (!s->data) return fail(error, "out of memory");
    memcpy(s->data, data, size);
    s->size = size;
    size_t capacity = 0, pos = 8 + be32(data + 4);
    for (unsigned t = 0; t < tracks; t++) {
        /* Skip unknown chunks between tracks, as the specification allows. */
        while (pos + 8 <= size && memcmp(data + pos, "MTrk", 4))
            pos += 8 + (size_t)be32(data + pos + 4);
        if (pos + 8 > size) return wb_smf_free(s), fail(error, "missing track %u", t + 1);
        size_t length = be32(data + pos + 4);
        if (length > size - pos - 8) return wb_smf_free(s), fail(error, "track %u is truncated", t + 1);
        cursor c = { data, pos + 8, pos + 8 + length };
        wb_smf_track *track = &s->track[t];
        track->first = s->event_count;
        uint64_t tick = 0;
        uint8_t running = 0;
        bool ended = false;
        while (c.pos < c.end && !ended) {
            uint32_t delta;
            if (!vlq(&c, &delta)) return wb_smf_free(s), fail(error, "track %u: bad delta time", t + 1);
            tick += delta;
            if (c.pos >= c.end) return wb_smf_free(s), fail(error, "track %u: missing status", t + 1);
            uint8_t status = c.data[c.pos];
            if (status & 0x80u) {
                c.pos++;
                if (status < 0xf0u) running = status;
            } else if (running) {
                status = running;
                s->running_status++;
            } else return wb_smf_free(s), fail(error, "track %u: data byte without status", t + 1);
            wb_smf_event e = { .tick = tick, .track = (uint16_t)t, .status = status };
            if (status == 0xffu) {
                uint32_t meta_length;
                if (c.pos >= c.end) return wb_smf_free(s), fail(error, "track %u: truncated meta", t + 1);
                e.d1 = c.data[c.pos++];
                if (!vlq(&c, &meta_length) || meta_length > c.end - c.pos)
                    return wb_smf_free(s), fail(error, "track %u: truncated meta", t + 1);
                e.offset = (uint32_t)c.pos;
                e.length = meta_length;
                c.pos += meta_length;
                if (e.d1 == 0x2f) ended = true;
                if (e.d1 == 0x03) {
                    size_t n = meta_length > WB_SMF_NAME_MAX ? WB_SMF_NAME_MAX : meta_length;
                    memcpy(track->name, data + e.offset, n);
                    track->name[n] = 0;
                }
            } else if (status == 0xf0u || status == 0xf7u) {
                uint32_t sysex_length;
                if (!vlq(&c, &sysex_length) || sysex_length > c.end - c.pos)
                    return wb_smf_free(s), fail(error, "track %u: truncated sysex", t + 1);
                c.pos += sysex_length;
                s->sysex++;
                continue;
            } else if (status >= 0xf0u) {
                return wb_smf_free(s), fail(error, "track %u: system message %02x in a file", t + 1, status);
            } else {
                unsigned kind = status >> 4, bytes = kind == 0xc || kind == 0xd ? 1 : 2;
                if (bytes > c.end - c.pos) return wb_smf_free(s), fail(error, "track %u: truncated event", t + 1);
                e.d1 = c.data[c.pos++] & 0x7fu;
                if (bytes == 2) e.d2 = c.data[c.pos++] & 0x7fu;
                if (kind == 0x9 && !e.d2) s->zero_velocity_offs++;
            }
            if (!push(s, &capacity, e)) return wb_smf_free(s), fail(error, "out of memory");
        }
        track->count = s->event_count - track->first;
        track->end_tick = tick;
        pos = c.end;
    }
    s->track_count = tracks;
    return true;
}

bool wb_smf_load(const char *path, wb_smf *s, wb_error *error) {
    char *bytes;
    size_t size;
    memset(s, 0, sizeof *s);
    if (!wb_read_file(path, &bytes, &size)) return fail(error, "cannot read %s", path);
    bool ok = wb_smf_read((const uint8_t *)bytes, size, s, error);
    free(bytes);
    return ok;
}

void wb_smf_free(wb_smf *s) {
    free(s->event);
    free(s->data);
    memset(s, 0, sizeof *s);
}

/* ---- notes ------------------------------------------------------------------ */

typedef struct { uint64_t tick; uint32_t seq; uint8_t channel, program; } program_change;

static int compare_program(const void *a, const void *b) {
    const program_change *x = a, *y = b;
    if (x->tick != y->tick) return x->tick < y->tick ? -1 : 1;
    return (x->seq > y->seq) - (x->seq < y->seq);
}

bool wb_smf_pair_notes(const wb_smf *s, unsigned t, wb_smf_notes *out) {
    *out = (wb_smf_notes){ 0 };
    if (t >= s->track_count) return false;
    /* Program changes of every track, in file time. */
    size_t program_count = 0;
    for (size_t i = 0; i < s->event_count; i++)
        if ((s->event[i].status & 0xf0u) == 0xc0u && s->event[i].status != 0xffu) program_count++;
    program_change *programs = calloc(program_count ? program_count : 1, sizeof *programs);
    const wb_smf_track *track = &s->track[t];
    /* Held notes: a FIFO of event indices per (channel, pitch). */
    size_t *held = calloc(track->count ? track->count : 1, sizeof *held);
    size_t *next_held = calloc(track->count ? track->count : 1, sizeof *next_held);
    size_t head[16][128], tail[16][128];
    out->note = calloc(track->count ? track->count : 1, sizeof *out->note);
    if (!programs || !held || !next_held || !out->note) {
        free(programs); free(held); free(next_held); free(out->note);
        *out = (wb_smf_notes){ 0 };
        return false;
    }
    size_t n = 0;
    for (size_t i = 0; i < s->event_count; i++) {
        const wb_smf_event *e = &s->event[i];
        if (e->status != 0xffu && (e->status & 0xf0u) == 0xc0u)
            programs[n++] = (program_change){ e->tick, e->seq, e->status & 15u, e->d1 };
    }
    qsort(programs, program_count, sizeof *programs, compare_program);
    for (unsigned c = 0; c < 16; c++)
        for (unsigned k = 0; k < 128; k++) head[c][k] = tail[c][k] = SIZE_MAX;
    size_t pending = 0;
    for (size_t i = 0; i < track->count; i++) {
        const wb_smf_event *e = &s->event[track->first + i];
        if (e->status == 0xffu) continue;
        unsigned kind = e->status & 0xf0u, channel = e->status & 15u;
        if (kind != 0x80u && kind != 0x90u) continue;
        if (kind == 0x90u && e->d2) {
            /* Program in effect at this note-on. */
            uint8_t program = 0;
            for (size_t p = 0; p < program_count; p++) {
                const program_change *x = &programs[p];
                if (x->tick > e->tick || (x->tick == e->tick && x->seq > e->seq)) break;
                if (x->channel == channel) program = x->program;
            }
            size_t slot = pending++;
            held[slot] = i;
            next_held[slot] = SIZE_MAX;
            out->note[slot] = (wb_smf_note){ .start = e->tick, .end = UINT64_MAX, .channel = (uint8_t)channel,
                                             .note = e->d1, .velocity = e->d2, .program = program,
                                             .seq = e->seq };
            if (tail[channel][e->d1] == SIZE_MAX) head[channel][e->d1] = slot;
            else next_held[tail[channel][e->d1]] = slot;
            tail[channel][e->d1] = slot;
        } else {
            size_t slot = head[channel][e->d1];
            if (slot == SIZE_MAX) { out->unmatched_offs++; continue; }
            out->note[slot].end = e->tick;
            head[channel][e->d1] = next_held[slot];
            if (head[channel][e->d1] == SIZE_MAX) tail[channel][e->d1] = SIZE_MAX;
        }
    }
    for (size_t i = 0; i < pending; i++)
        if (out->note[i].end == UINT64_MAX) {
            out->note[i].end = track->end_tick;
            out->stuck++;
        }
    out->count = pending;
    free(programs);
    free(held);
    free(next_held);
    return true;
}

void wb_smf_notes_free(wb_smf_notes *n) {
    free(n->note);
    *n = (wb_smf_notes){ 0 };
}

typedef struct { uint64_t tick; uint32_t seq; size_t index; } meta_ref;

static int compare_meta(const void *a, const void *b) {
    const meta_ref *x = a, *y = b;
    if (x->tick != y->tick) return x->tick < y->tick ? -1 : 1;
    return (x->seq > y->seq) - (x->seq < y->seq);
}

static size_t sorted_meta(const wb_smf *s, uint8_t type, size_t length, meta_ref *refs, size_t max) {
    size_t n = 0;
    for (size_t i = 0; i < s->event_count && n < max; i++) {
        const wb_smf_event *e = &s->event[i];
        if (e->status == 0xffu && e->d1 == type && e->length == length)
            refs[n++] = (meta_ref){ e->tick, e->seq, i };
    }
    qsort(refs, n, sizeof *refs, compare_meta);
    return n;
}

size_t wb_smf_tempos(const wb_smf *s, wb_smf_tempo *out, size_t max) {
    meta_ref *refs = calloc(max ? max : 1, sizeof *refs);
    if (!refs) return 0;
    size_t n = sorted_meta(s, 0x51, 3, refs, max);
    for (size_t i = 0; i < n; i++) {
        const uint8_t *p = s->data + s->event[refs[i].index].offset;
        out[i] = (wb_smf_tempo){ refs[i].tick, (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2] };
    }
    free(refs);
    return n;
}

size_t wb_smf_meters(const wb_smf *s, wb_smf_meter *out, size_t max) {
    meta_ref *refs = calloc(max ? max : 1, sizeof *refs);
    if (!refs) return 0;
    size_t n = sorted_meta(s, 0x58, 4, refs, max);
    for (size_t i = 0; i < n; i++) {
        const uint8_t *p = s->data + s->event[refs[i].index].offset;
        out[i] = (wb_smf_meter){ refs[i].tick, p[0], p[1], p[2], p[3] };
    }
    free(refs);
    return n;
}

/* ---- writing ------------------------------------------------------------- */

void wb_smf_put_vlq(wb_buf *out, uint64_t value) {
    uint8_t bytes[10];
    unsigned n = 0;
    bytes[n++] = value & 0x7fu;
    while (value >>= 7) bytes[n++] = 0x80u | (value & 0x7fu);
    while (n) wb_buf_append(out, (const char *)&bytes[--n], 1);
}

void wb_smf_track_begin(wb_smf_track_writer *w) { *w = (wb_smf_track_writer){ 0 }; }

void wb_smf_track_event(wb_smf_track_writer *w, uint64_t tick, const uint8_t *bytes, size_t size) {
    wb_smf_put_vlq(&w->body, tick - w->last_tick);
    wb_buf_append(&w->body, (const char *)bytes, size);
    w->last_tick = tick;
}

void wb_smf_track_meta(wb_smf_track_writer *w, uint64_t tick, uint8_t type, const void *payload,
                       size_t size) {
    wb_smf_put_vlq(&w->body, tick - w->last_tick);
    uint8_t head[2] = { 0xff, type };
    wb_buf_append(&w->body, (const char *)head, 2);
    wb_smf_put_vlq(&w->body, size);
    wb_buf_append(&w->body, payload, size);
    w->last_tick = tick;
}

void wb_smf_track_end(wb_smf_track_writer *w, wb_buf *out) {
    static const uint8_t END[3] = { 0xff, 0x2f, 0x00 };
    wb_smf_track_event(w, w->last_tick, END, 3);
    uint8_t head[8] = { 'M', 'T', 'r', 'k' };
    size_t n = w->body.length;
    head[4] = (uint8_t)(n >> 24); head[5] = (uint8_t)(n >> 16);
    head[6] = (uint8_t)(n >> 8); head[7] = (uint8_t)n;
    wb_buf_append(out, (const char *)head, 8);
    wb_buf_append(out, w->body.data, n);
    if (w->body.failed) out->failed = true;
    wb_buf_free(&w->body);
}

void wb_smf_header(wb_buf *out, unsigned format, unsigned tracks, unsigned division) {
    uint8_t h[14] = { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, (uint8_t)format,
                      (uint8_t)(tracks >> 8), (uint8_t)tracks,
                      (uint8_t)(division >> 8), (uint8_t)division };
    wb_buf_append(out, (const char *)h, sizeof h);
}
