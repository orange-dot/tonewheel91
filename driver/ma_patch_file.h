#ifndef MA_PATCH_FILE_H
#define MA_PATCH_FILE_H

/* Hosted version-1 Mamut Analog patch text and its editor-facing field
 * catalog.  The freestanding core knows only the concrete ma_patch value. */

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "../src/mamutanalog.h"

enum { MA_PATCH_NAME_MAX = 32, MA_PATCH_ERROR_MAX = 96 };

typedef struct {
    char name[MA_PATCH_NAME_MAX + 1];
    ma_patch value;
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

[[nodiscard]] size_t ma_patch_field_count(void);
[[nodiscard]] bool ma_patch_field_info_at(size_t index,
                                          ma_patch_field_info *info);
[[nodiscard]] bool ma_patch_field_get(const ma_patch *patch, size_t index,
                                      double *value);
bool ma_patch_field_set(ma_patch *patch, size_t index, double value);

#endif
