/* The .mws session file (docs/workbench-plan.md §3.2).
 *
 *   mws=1                    version line, first non-comment line
 *   [kind]                   starts one object: session, source, form,
 *   key=value                stem, part, rule, groove, curve, lane, marker
 *
 * Strict: unknown sections and keys, duplicate keys, missing required keys,
 * values outside their domain, non-finite numbers and over-long lines are
 * errors with line numbers.  Lines starting with '#' and blank lines are
 * ignored; there are no trailing comments.  Floats are written with the
 * shortest digits that round-trip their f32 (or f64) value, so
 * write(read(write(s))) is byte-identical to write(s).
 *
 * The writer emits every key of the engine-facing kinds, including values
 * equal to the default, so a later change of a default cannot silently
 * change a saved session.  Rules and markers emit only keys that differ from
 * their neutral defaults. */
#ifndef WB_FORMAT_H
#define WB_FORMAT_H

#include <stdbool.h>
#include <stddef.h>

#include "wb_session.h"

enum { WB_MWS_VERSION = 1, WB_LINE_MAX = 4095 };

/* Growable text buffer; a failed allocation latches `failed`. */
typedef struct {
    char *data;
    size_t length, capacity;
    bool failed;
} wb_buf;

void wb_buf_free(wb_buf *buf);
void wb_buf_reset(wb_buf *buf);
bool wb_buf_append(wb_buf *buf, const char *text, size_t size);
[[gnu::format(printf, 2, 3)]]
bool wb_buf_printf(wb_buf *buf, const char *format, ...);

/* Shortest decimal text that reads back to the same value. */
void wb_format_f32(float value, char out[32]);
void wb_format_f64(double value, char out[32]);

/* Whole session.  On failure *session holds an unspecified partial state. */
bool wb_read(const char *text, size_t size, wb_session *session,
             wb_error *error);
bool wb_write(const wb_session *session, wb_buf *out);

/* One object as a block ("[kind]\nkey=value\n...").  Reading a block
 * replaces object `index` of `kind`, or with `insert` puts it before
 * `index` (index == count appends).  The session is unchanged on failure. */
bool wb_write_object(const wb_session *session, wb_kind kind, unsigned index,
                     wb_buf *out);
bool wb_read_object(wb_session *session, wb_kind kind, unsigned index,
                    bool insert, const char *text, size_t size,
                    wb_error *error);
/* Remove object `index` of a non-singleton kind, keeping the order. */
bool wb_remove_object(wb_session *session, wb_kind kind, unsigned index);
/* Edit one key of one object through the same parser the reader uses.
 * Repeatable keys (section, point, pattern, use, hit) cannot be set this
 * way; edit them through wb_read_object. */
bool wb_set_key(wb_session *session, wb_kind kind, unsigned index,
                const char *key, const char *value, wb_error *error);
/* The current text of one key ("" when it is unset and optional). */
bool wb_get_key(const wb_session *session, wb_kind kind, unsigned index,
                const char *key, wb_buf *out);

/* Files: whole-file read, and save through a sibling temporary file with
 * fsync and an atomic rename (the ma_patch_save path). */
bool wb_load(const char *path, wb_session *session, wb_error *error);
bool wb_save(const char *path, const wb_session *session, wb_error *error);
bool wb_read_file(const char *path, char **data, size_t *size);
bool wb_write_file_atomic(const char *path, const char *data, size_t size);

#endif
