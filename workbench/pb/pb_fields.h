/* Patchbench display table (docs/patchbench-plan.md §2.3): module, label,
 * unit, scale and init value for every .mapatch catalog field, keyed by the
 * catalog name.  The catalog (driver/ma_patch_file.c) stays the authority
 * for domains and steps; this table only says how a field is shown. */
#ifndef PB_FIELDS_H
#define PB_FIELDS_H

#include <stdbool.h>
#include <stddef.h>

#include "../../driver/ma_patch_file.h"

typedef enum : unsigned char {
    PB_MOD_VCO1, PB_MOD_VCO2, PB_MOD_SYNC, PB_MOD_RASTER, PB_MOD_MOZAIK, PB_MOD_MIXER,
    PB_MOD_BCS, PB_MOD_FILTER, PB_MOD_GLAS, PB_MOD_AMP, PB_MOD_OUTPUT, PB_MOD_VOICE,
    PB_MOD_MACROS, PB_MOD_COUNT
} pb_module;

typedef enum : unsigned char { PB_SCALE_LINEAR, PB_SCALE_LOG, PB_SCALE_BIPOLAR, PB_SCALE_STEPPED } pb_scale;
typedef enum : unsigned char { PB_UNIT_NONE, PB_UNIT_HZ, PB_UNIT_MS, PB_UNIT_CENTS, PB_UNIT_SEMITONES } pb_unit;

typedef struct {
    const char *name;        /* catalog key */
    pb_module module;
    const char *label;
    pb_unit unit;
    pb_scale scale;
    double init;
} pb_field;

typedef struct {
    const pb_field *display;
    ma_patch_field_info info;
} pb_field_view;

const char *pb_module_name(pb_module module);
/* The display entry of a catalog name, or null. */
const pb_field *pb_field_find(const char *name);
/* Catalog index → display entry and domain; false past the catalog or for
 * a catalog field without an entry. */
bool pb_field_view_at(size_t index, pb_field_view *view);
/* Catalog index of a name, or -1. */
int pb_field_index(const char *name);

/* Value ↔ position on the control's scale, 0..1. */
double pb_field_to_unit(const pb_field_view *v, double value);
double pb_field_from_unit(const pb_field_view *v, double unit);
/* Clamp to the domain and, for integers, round. */
double pb_field_clamp(const pb_field_view *v, double value);

/* "1.25k", "350 ms", "+7.0 c", "-12 st", "0.430". */
void pb_field_format(const pb_field_view *v, double value, char *text, size_t size);
/* Accepts a number with an optional unit suffix (k, hz, ms, s, c, st);
 * false for garbage or a value outside the domain. */
bool pb_field_parse(const pb_field_view *v, const char *text, double *value);

/* The interval's name: "fifth", "octave down", ... */
const char *pb_interval_name(int semitones);

/* The init patch: every field at its display init value. */
ma_patch pb_init_patch(void);

#endif
