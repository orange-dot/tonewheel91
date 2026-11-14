/* Mamut patch parent chains (docs/workbench-plan.md §5.7, W5).
 *
 *   wbpatch=1
 *   name=keys
 *   parent=factory:tepih | patches/pluck.mapatch | patches/base.wbpatch
 *   filter.cutoff_hz=1250          overrides only, any .mapatch field
 *   rack=1                         optional (wbpatch=2): the child replaces
 *   rack.1.kind=eq ...             the parent's insert rack with these
 *                                  rack.<n>.* keys (none = an empty rack);
 *                                  without rack=1 the rack is inherited
 *
 * A .wbpatch stores what differs from its parent; flattening resolves the
 * chain (at most eight levels, no cycles) into a full ma_patch.  Parent paths
 * are relative to the session directory.  Every part loader goes through
 * wb_patch_load, which takes .mapatch and .wbpatch alike. */
#ifndef WB_PATCH_H
#define WB_PATCH_H

#include <stdbool.h>
#include <stddef.h>

#include "../../driver/ma_patch_file.h"
#include "wb_session.h"

enum { WB_PATCH_DEPTH = 8, WB_PATCH_FIELDS = 64 };

typedef struct {
    char name[MA_PATCH_NAME_MAX + 1];
    char parent[WB_PATH_MAX + 1];
    unsigned override_count;
    unsigned field[WB_PATCH_FIELDS];   /* catalog index */
    double value[WB_PATCH_FIELDS];
    bool has_rack;                     /* rack=1: `rack` replaces the parent's */
    ma_rack_spec rack;
} wb_patch_overrides;

bool wb_patch_is_chain(const char *path);
bool wb_patch_read_overrides(const char *path, wb_patch_overrides *patch, wb_error *error);
bool wb_patch_write_overrides(const char *path, const wb_patch_overrides *patch, wb_error *error);

/* The flattened patch of `relative` (a .mapatch or .wbpatch) inside the
 * session directory. */
bool wb_patch_load(const char *session_dir, const char *relative, ma_patch_document *document,
                   wb_error *error);
/* The parent's flattened value (for "reset to parent" and dim display). */
bool wb_patch_parent(const char *session_dir, const wb_patch_overrides *patch, ma_patch *parent,
                     wb_error *error);
/* The same, with the rack the parent resolves to (an empty one for a
 * factory parent). */
bool wb_patch_parent_rack(const char *session_dir, const wb_patch_overrides *patch, ma_patch *parent,
                          ma_rack_spec *rack, wb_error *error);
/* Set or clear (`clear`) one override, keeping catalog order. */
void wb_patch_override(wb_patch_overrides *patch, unsigned field, double value, bool clear);
bool wb_patch_overridden(const wb_patch_overrides *patch, unsigned field);

#endif
