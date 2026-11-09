/* Take recording (docs/workbench-plan.md §6.4, W12): the timestamped input
 * events of a live performance written as a format-1 SMF.  Notes and
 * console controls keep the engines' existing CC map, so render_midi and
 * the Workbench import read a take like any other file. */
#ifndef WB_TAKE_H
#define WB_TAKE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wb_session.h"

enum { WB_TAKE_PPQ = 960, WB_TAKE_US_PER_QUARTER = 500000 };

typedef struct {
    uint64_t microseconds;         /* since the take started */
    uint8_t track;                 /* rack slot: one track and channel each */
    uint8_t status, d1, d2;        /* channel message; the channel nibble is replaced */
} wb_take_event;

/* One track per used slot, named by `names[slot]`; 120 BPM, 4/4, PPQ 960
 * (one tick = 0.52 ms).  Events are sorted stably by time first.  An
 * unreleased note is released at the last event's tick. */
bool wb_take_write(const char *path, wb_take_event *events, size_t count, const char *const names[4],
                   const char *title, wb_error *error);

#endif
