#ifndef MA_PATCH_FILE_H
#define MA_PATCH_FILE_H

/* Hosted Mamut Analog patch text (versions 1..9) and its editor-facing field
 * catalog.  The freestanding core knows only the concrete ma_patch value. */

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "../src/mamutanalog.h"

enum { MA_PATCH_NAME_MAX = 32, MA_PATCH_ERROR_MAX = 96 };

typedef struct {
    char name[MA_PATCH_NAME_MAX + 1];
    ma_patch value;
    /* Version 5: the insert rack as optional rack.<n>.* keys (n = 1..8,
     * docs/ma-rack-plan.md RKD12). Empty when the file has none. Version 9:
     * the RK2 kinds (chorus, reverb, delay, saturator, compressor). */
    ma_rack_spec rack;
} ma_patch_document;

typedef struct {
    unsigned line;
    char message[MA_PATCH_ERROR_MAX];
} ma_patch_error;

typedef struct {
    const char *name;
    double minimum;
    double maximum;
    double fine_step;
    double coarse_step;
    bool integer;
} ma_patch_field_info;

bool ma_patch_read(FILE *file, ma_patch_document *document,
                   ma_patch_error *error);
bool ma_patch_write(FILE *file, const ma_patch_document *document);
bool ma_patch_load(const char *path, ma_patch_document *document,
                   ma_patch_error *error);
/* Save through a unique sibling temporary file, fsync and atomic rename. */
bool ma_patch_save(const char *path, const ma_patch_document *document,
                   ma_patch_error *error);

/* File names of rack kinds and of their parameters ("eq", "band.3.q"); the
 * Patchbench uses the same names for labels. nullptr when out of range. */
[[nodiscard]] const char *ma_rack_kind_name(ma_rack_kind kind);
[[nodiscard]] const char *ma_rack_param_name(ma_rack_kind kind, unsigned index);
/* "bell", "low_shelf", "high_shelf", "low_cut", "high_cut", "notch". */
[[nodiscard]] const char *ma_eq_type_name(ma_eq_type type);
/* The names of a parameter whose values are written as words (an EQ band's
 * type, a delay's sync: "free", "1/32", "1/16t" ... "1/1", a compressor's
 * detector: "peak", "rms"), indexed by value; nullptr for a numeric one. */
[[nodiscard]] const char *const *ma_rack_param_words(ma_rack_kind kind, unsigned index, unsigned *count);
/* True when every block is an EQ or a utility: what patch version 5 to 8
 * and wbpatch=2 can carry. The RK2 kinds need version 9 and wbpatch=3. */
[[nodiscard]] bool ma_rack_spec_basic(const ma_rack_spec *rack);

/* The rack.<n>.* key syntax, shared by .mapatch v5 and .wbpatch: feed every
 * rack key to ma_rack_read_key, then call ma_rack_read_finish once. The
 * kind must come first in its slot; every key of an occupied slot is
 * required once. */
typedef struct {
    struct { bool kind, id, enabled, mix; uint64_t params; } slot[MA_RACK_SLOTS];
    bool basic_only;               /* the file's version predates the RK2 kinds */
} ma_rack_reader;
bool ma_rack_read_key(ma_rack_spec *rack, ma_rack_reader *reader, const char *key,
                      const char *value, const char **why);
bool ma_rack_read_finish(const ma_rack_spec *rack, const ma_rack_reader *reader, const char **why);
/* The rack's keys, one per line; the length, or -1 when it does not fit or
 * the rack is invalid. MA_RACK_TEXT_MAX holds any valid rack: eight EQs of
 * eight bands, the longest, come to about 8.3 kB. */
enum { MA_RACK_TEXT_MAX = 16384 };
int ma_rack_format(const ma_rack_spec *rack, char *out, size_t size);

[[nodiscard]] size_t ma_patch_field_count(void);
[[nodiscard]] bool ma_patch_field_info_at(size_t index,
                                          ma_patch_field_info *info);
[[nodiscard]] bool ma_patch_field_get(const ma_patch *patch, size_t index,
                                      double *value);
bool ma_patch_field_set(ma_patch *patch, size_t index, double value);

#endif
