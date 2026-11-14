#include "pb_undo.h"

#include <stdio.h>

void pb_undo_clear(pb_undo *u) { u->first = u->count = u->position = 0; }

static size_t slot(const pb_undo *u, size_t i) { return (u->first + i) % PB_UNDO_DEPTH; }

void pb_undo_push(pb_undo *u, const ma_patch *before, const ma_rack_spec *rack, const char *label) {
    u->count = u->position;
    if (u->count == PB_UNDO_DEPTH) {
        u->first = (u->first + 1) % PB_UNDO_DEPTH;
        u->count--;
    }
    size_t s = slot(u, u->count);
    u->before[s] = *before;
    u->rack[s] = rack ? *rack : (ma_rack_spec){ 0 };
    snprintf(u->label[s], sizeof u->label[s], "%s", label ? label : "edit");
    u->position = ++u->count;
}

/* Each slot holds the value on the other side of its step: undo swaps the
 * current value in, so the same slot serves redo. */
static void exchange(pb_undo *u, size_t s, ma_patch *current, ma_rack_spec *rack) {
    ma_patch x = u->before[s];
    u->before[s] = *current;
    *current = x;
    if (rack) {
        ma_rack_spec r = u->rack[s];
        u->rack[s] = *rack;
        *rack = r;
    }
}

bool pb_undo_undo(pb_undo *u, ma_patch *current, ma_rack_spec *rack) {
    if (!u->position) return false;
    exchange(u, slot(u, --u->position), current, rack);
    return true;
}

bool pb_undo_redo(pb_undo *u, ma_patch *current, ma_rack_spec *rack) {
    if (u->position == u->count) return false;
    exchange(u, slot(u, u->position++), current, rack);
    return true;
}

const char *pb_undo_label(const pb_undo *u, bool redo) {
    if (redo ? u->position == u->count : !u->position) return "";
    return u->label[slot(u, redo ? u->position : u->position - 1)];
}
