#define _POSIX_C_SOURCE 200809L
#include "wb_take.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wb_format.h"
#include "wb_smf.h"

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

static int compare_time(const void *a, const void *b) {
    const wb_take_event *x = a, *y = b;
    return (x->microseconds > y->microseconds) - (x->microseconds < y->microseconds);
}

static uint64_t tick_of(uint64_t microseconds) {
    /* Round to the nearest tick: us * PPQ / us_per_quarter. */
    return (microseconds * WB_TAKE_PPQ + WB_TAKE_US_PER_QUARTER / 2) / WB_TAKE_US_PER_QUARTER;
}

bool wb_take_write(const char *path, wb_take_event *ev, size_t count, const char *const names[4], const char *title,
                   wb_error *error) {
    /* Stable by time: insertion order breaks ties (qsort is not stable, so
     * sort indices with the original position as the second key). */
    typedef struct { wb_take_event e; size_t order; } item;
    item *items = malloc((count ? count : 1) * sizeof *items);
    if (!items) return fail(error, "out of memory");
    for (size_t i = 0; i < count; i++) items[i] = (item){ ev[i], i };
    for (size_t i = 1; i < count; i++) {
        item x = items[i];
        size_t j = i;
        while (j > 0 && compare_time(&items[j - 1].e, &x.e) > 0) { items[j] = items[j - 1]; j--; }
        items[j] = x;
    }
    unsigned used = 0;
    for (size_t i = 0; i < count; i++)
        if (items[i].e.track < 4) used |= 1u << items[i].e.track;
    unsigned tracks = 1;
    for (unsigned t = 0; t < 4; t++) tracks += (used >> t) & 1u;
    uint64_t last = count ? tick_of(items[count - 1].e.microseconds) : 0;

    wb_buf out = { 0 };
    wb_smf_header(&out, 1, tracks, WB_TAKE_PPQ);
    wb_smf_track_writer w;
    wb_smf_track_begin(&w);
    wb_smf_track_meta(&w, 0, 0x03, title, strlen(title));
    static const uint8_t METER[4] = { 4, 2, 24, 8 };
    const uint8_t tempo[3] = { WB_TAKE_US_PER_QUARTER >> 16, (WB_TAKE_US_PER_QUARTER >> 8) & 0xff,
                               WB_TAKE_US_PER_QUARTER & 0xff };
    wb_smf_track_meta(&w, 0, 0x58, METER, 4);
    wb_smf_track_meta(&w, 0, 0x51, tempo, 3);
    wb_smf_track_end(&w, &out);
    for (unsigned t = 0; t < 4; t++) {
        if (!(used >> t & 1u)) continue;
        bool held[128] = { 0 };
        wb_smf_track_begin(&w);
        const char *name = names && names[t] ? names[t] : "take";
        wb_smf_track_meta(&w, 0, 0x03, name, strlen(name));
        for (size_t i = 0; i < count; i++) {
            const wb_take_event *e = &items[i].e;
            if (e->track != t) continue;
            uint8_t kind = e->status & 0xf0u;
            uint8_t bytes[3] = { (uint8_t)(kind | t), (uint8_t)(e->d1 & 0x7f), (uint8_t)(e->d2 & 0x7f) };
            if (kind == 0x90u && bytes[2]) held[bytes[1]] = true;
            if (kind == 0x80u || (kind == 0x90u && !bytes[2])) {
                if (!held[bytes[1]]) continue;   /* a release without its note */
                held[bytes[1]] = false;
            }
            wb_smf_track_event(&w, tick_of(e->microseconds), bytes, kind == 0xc0u || kind == 0xd0u ? 2 : 3);
        }
        for (unsigned n = 0; n < 128; n++)
            if (held[n]) {
                uint8_t off[3] = { (uint8_t)(0x80u | t), (uint8_t)n, 0 };
                wb_smf_track_event(&w, last, off, 3);
            }
        wb_smf_track_end(&w, &out);
    }
    free(items);
    bool ok = !out.failed && wb_write_file_atomic(path, out.data, out.length);
    wb_buf_free(&out);
    return ok || fail(error, "cannot write %s", path);
}
