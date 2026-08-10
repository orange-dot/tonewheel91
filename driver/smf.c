#include "smf.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    const uint8_t *data;
    size_t size, pos, origin;
} cursor;

typedef struct {
    void *data;
    size_t count, capacity, width;
} vector;

static bool fail(smf_error *error, size_t offset, const char *message) {
    if (error) *error = (smf_error){ offset, message };
    return false;
}

static bool take_u8(cursor *c, uint8_t *out) {
    if (c->pos == c->size) return false;
    *out = c->data[c->pos++];
    return true;
}

static bool take_be16(cursor *c, uint16_t *out) {
    if (c->size - c->pos < 2) return false;
    *out = (uint16_t)((uint16_t)c->data[c->pos] << 8 | c->data[c->pos + 1]);
    c->pos += 2;
    return true;
}

static bool take_be32(cursor *c, uint32_t *out) {
    if (c->size - c->pos < 4) return false;
    *out = (uint32_t)c->data[c->pos] << 24
         | (uint32_t)c->data[c->pos + 1] << 16
         | (uint32_t)c->data[c->pos + 2] << 8
         | c->data[c->pos + 3];
    c->pos += 4;
    return true;
}

static bool take_span(cursor *c, size_t size, cursor *out) {
    if (size > c->size - c->pos) return false;
    *out = (cursor){ c->data + c->pos, size, 0, c->origin + c->pos };
    c->pos += size;
    return true;
}

static bool take_vlq(cursor *c, uint32_t *out) {
    size_t pos = c->pos;
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; i++) {
        if (pos == c->size) return false;
        uint8_t byte = c->data[pos++];
        value = value << 7 | (byte & 0x7fu);
        if (!(byte & 0x80u)) {
            c->pos = pos;
            *out = value;
            return true;
        }
    }
    return false;
}

static void *vector_push(vector *v) {
    if (v->count == v->capacity) {
        size_t capacity = v->capacity ? v->capacity * 2 : 64;
        if (capacity < v->capacity || capacity > SIZE_MAX / v->width)
            return 0;
        void *data = realloc(v->data, capacity * v->width);
        if (!data) return 0;
        v->data = data;
        v->capacity = capacity;
    }
    return (unsigned char *)v->data + v->count++ * v->width;
}

static int compare_event(const void *a, const void *b) {
    const smf_event *x = a, *y = b;
    if (x->tick != y->tick) return x->tick < y->tick ? -1 : 1;
    return x->seq < y->seq ? -1 : x->seq > y->seq;
}

static int compare_tempo(const void *a, const void *b) {
    const smf_tempo *x = a, *y = b;
    if (x->tick != y->tick) return x->tick < y->tick ? -1 : 1;
    return x->seq < y->seq ? -1 : x->seq > y->seq;
}

static bool next_seq(uint32_t *seq, uint32_t *out) {
    if (*seq == UINT32_MAX) return false;
    *out = (*seq)++;
    return true;
}

static bool parse_track(cursor track, uint16_t channel_mask, vector *events,
                        vector *tempos, uint32_t *seq, smf_error *error) {
    uint64_t tick = 0;
    uint8_t running = 0;
    bool end_seen = false;

    while (track.pos < track.size) {
        size_t event_offset = track.origin + track.pos;
        uint32_t delta = 0;
        if (!take_vlq(&track, &delta))
            return fail(error, event_offset, "invalid or truncated delta VLQ");
        if (tick > UINT64_MAX - delta)
            return fail(error, event_offset, "absolute tick overflow");
        tick += delta;

        uint8_t status = 0;
        if (track.pos == track.size)
            return fail(error, track.origin + track.pos, "missing event status");
        if (track.data[track.pos] & 0x80u) {
            take_u8(&track, &status);
            running = status < 0xf0u ? status : 0;
        } else {
            if (!running)
                return fail(error, track.origin + track.pos, "invalid running status");
            status = running;
        }

        if (status == 0xffu) {
            uint8_t type = 0;
            uint32_t length = 0;
            cursor payload;
            if (!take_u8(&track, &type) || !take_vlq(&track, &length)
                || !take_span(&track, length, &payload))
                return fail(error, event_offset, "truncated meta event");
            if (type == 0x2fu) {
                if (length)
                    return fail(error, payload.origin, "invalid end-of-track length");
                if (track.pos != track.size)
                    return fail(error, track.origin + track.pos,
                                "data follows end-of-track");
                end_seen = true;
            } else if (type == 0x51u) {
                if (length != 3)
                    return fail(error, payload.origin, "invalid tempo length");
                uint32_t tempo = (uint32_t)payload.data[0] << 16
                               | (uint32_t)payload.data[1] << 8
                               | payload.data[2];
                if (!tempo)
                    return fail(error, payload.origin, "zero tempo");
                uint32_t order = 0;
                if (!next_seq(seq, &order))
                    return fail(error, event_offset, "event sequence overflow");
                smf_tempo *item = vector_push(tempos);
                if (!item) return fail(error, event_offset, "out of memory");
                *item = (smf_tempo){ tick, order, tempo };
            }
            continue;
        }

        if (status == 0xf0u || status == 0xf7u) {
            uint32_t length = 0;
            cursor payload;
            if (!take_vlq(&track, &length) || !take_span(&track, length, &payload))
                return fail(error, event_offset, "truncated SysEx event");
            continue;
        }

        uint8_t type = status & 0xf0u;
        if (type < 0x80u || type > 0xe0u)
            return fail(error, event_offset, "unsupported system event");
        unsigned width = type == 0xc0u || type == 0xd0u ? 1 : 2;
        uint8_t d1 = 0, d2 = 0;
        if (!take_u8(&track, &d1) || (width == 2 && !take_u8(&track, &d2)))
            return fail(error, event_offset, "truncated channel event");
        if ((d1 & 0x80u) || (width == 2 && (d2 & 0x80u)))
            return fail(error, event_offset, "channel data byte has status bit set");

        uint8_t channel = status & 0x0fu;
        if ((type == 0x80u || type == 0x90u || type == 0xa0u || type == 0xb0u)
            && (channel_mask & (uint16_t)(1u << channel))) {
            uint32_t order = 0;
            if (!next_seq(seq, &order))
                return fail(error, event_offset, "event sequence overflow");
            smf_event *item = vector_push(events);
            if (!item) return fail(error, event_offset, "out of memory");
            *item = (smf_event){ tick, order, status, d1, d2 };
        }
    }
    if (!end_seen)
        return fail(error, track.origin + track.size, "missing end-of-track event");
    return true;
}

bool smf_parse(const uint8_t *data, size_t size, uint16_t channel_mask,
               smf_file *out, smf_error *error) {
    if (!out) return false;
    *out = (smf_file){ 0 };
    if (error) *error = (smf_error){ 0 };
    if (!data) return fail(error, 0, "null input");

    cursor file = { data, size, 0, 0 };
    cursor id, header;
    uint32_t header_size = 0;
    if (!take_span(&file, 4, &id) || memcmp(id.data, "MThd", 4)
        || !take_be32(&file, &header_size))
        return fail(error, 0, "missing MThd header");
    if (header_size < 6 || !take_span(&file, header_size, &header))
        return fail(error, 4, "invalid MThd length");

    uint16_t format = 0, tracks = 0, division = 0;
    if (!take_be16(&header, &format) || !take_be16(&header, &tracks)
        || !take_be16(&header, &division))
        return fail(error, header.origin, "truncated MThd body");
    if (format > 1 || !tracks || (format == 0 && tracks != 1))
        return fail(error, header.origin, "unsupported format/track count");
    if (!division || (division & 0x8000u))
        return fail(error, header.origin + 4, "unsupported time division");

    vector events = { .width = sizeof(smf_event) };
    vector tempos = { .width = sizeof(smf_tempo) };
    uint32_t seq = 0;
    for (unsigned i = 0; i < tracks; i++) {
        cursor track_id, track;
        uint32_t track_size = 0;
        size_t offset = file.pos;
        if (!take_span(&file, 4, &track_id) || memcmp(track_id.data, "MTrk", 4)
            || !take_be32(&file, &track_size) || !take_span(&file, track_size, &track)) {
            fail(error, offset, "missing or truncated MTrk chunk");
            goto failure;
        }
        if (!parse_track(track, channel_mask, &events, &tempos, &seq, error))
            goto failure;
    }
    if (file.pos != file.size) {
        fail(error, file.pos, "trailing data after declared tracks");
        goto failure;
    }

    if (events.count > 1)
        qsort(events.data, events.count, events.width, compare_event);
    if (tempos.count > 1)
        qsort(tempos.data, tempos.count, tempos.width, compare_tempo);
    *out = (smf_file){ format, tracks, division,
                      events.data, events.count, tempos.data, tempos.count };
    return true;

failure:
    free(events.data);
    free(tempos.data);
    return false;
}

void smf_dispose(smf_file *file) {
    if (!file) return;
    free(file->events);
    free(file->tempos);
    *file = (smf_file){ 0 };
}
