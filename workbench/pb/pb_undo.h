/* Edit history of the authored patch (plan §2.3): whole patch values and
 * the insert rack (RK1), one pair per step.  A drag is one step: the caller opens a step when the drag
 * starts and the step keeps the value from before it. */
#ifndef PB_UNDO_H
#define PB_UNDO_H

#include <stdbool.h>
#include <stddef.h>

#include "../../src/mamutanalog.h"

enum { PB_UNDO_DEPTH = 512 };

typedef struct {
    ma_patch before[PB_UNDO_DEPTH];
    ma_rack_spec rack[PB_UNDO_DEPTH];
    char label[PB_UNDO_DEPTH][32];
    size_t first, count, position;   /* ring: position ≤ count; redo above */
} pb_undo;

void pb_undo_clear(pb_undo *u);
/* Record `before` (and `rack`, which may be null: an empty rack) as the
 * value an undo returns to; drops any redo. */
void pb_undo_push(pb_undo *u, const ma_patch *before, const ma_rack_spec *rack, const char *label);
/* Swap `*current` (and `*rack` unless null) with the step below / above;
 * false when there is none. */
bool pb_undo_undo(pb_undo *u, ma_patch *current, ma_rack_spec *rack);
bool pb_undo_redo(pb_undo *u, ma_patch *current, ma_rack_spec *rack);
const char *pb_undo_label(const pb_undo *u, bool redo);

#endif
