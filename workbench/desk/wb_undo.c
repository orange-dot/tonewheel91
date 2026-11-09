#define _POSIX_C_SOURCE 200809L
#include "wb_undo.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "wb_hash.h"

static const char *const OP_NAMES[] = { "replace", "insert", "remove" };

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

void wb_journal_init(wb_journal *j) { *j = (wb_journal){ 0 }; }

static void edit_free(wb_edit *e) {
    free(e->before);
    free(e->after);
    *e = (wb_edit){ 0 };
}

static void truncate_to(wb_journal *j, size_t count) {
    while (j->count > count) edit_free(&j->edit[--j->count]);
    if (j->cursor > count) j->cursor = count;
}

void wb_journal_free(wb_journal *j) {
    truncate_to(j, 0);
    free(j->edit);
    free(j->path);
    *j = (wb_journal){ 0 };
}

bool wb_session_sha256(const wb_session *s, char hex[65]) {
    wb_buf text = { 0 };
    bool ok = wb_write(s, &text);
    if (ok) wb_sha256_bytes_hex(text.data, text.length, hex);
    wb_buf_free(&text);
    return ok;
}

static char *dup_text(const char *text, size_t size) {
    char *copy = malloc(size + 1);
    if (!copy) return nullptr;
    memcpy(copy, text, size);
    copy[size] = 0;
    return copy;
}

static bool push(wb_journal *j, wb_edit edit) {
    truncate_to(j, j->cursor);
    if (j->count == j->capacity) {
        size_t capacity = j->capacity ? 2 * j->capacity : 64;
        wb_edit *grown = realloc(j->edit, capacity * sizeof *grown);
        if (!grown) return false;
        j->edit = grown;
        j->capacity = capacity;
    }
    j->edit[j->count++] = edit;
    j->cursor = j->count;
    return true;
}

/* ---- file records -------------------------------------------------------- */

static bool append_lines(wb_buf *out, char prefix, const char *text) {
    for (const char *p = text; *p;) {
        const char *end = strchr(p, '\n');
        size_t length = end ? (size_t)(end - p) : strlen(p);
        if (!wb_buf_append(out, &prefix, 1) || !wb_buf_append(out, p, length)
            || !wb_buf_append(out, "\n", 1))
            return false;
        p += length + (end ? 1 : 0);
    }
    return true;
}

static bool write_record(wb_journal *j, const char *record, size_t size) {
    if (!j->path) return true;
    FILE *file = fopen(j->path, "ab");
    if (!file) return false;
    bool ok = true;
    if (ftell(file) == 0) ok = fputs("wbj=1\n", file) >= 0;
    ok = ok && fwrite(record, 1, size, file) == size && fflush(file) == 0
      && fdatasync(fileno(file)) == 0;
    if (fclose(file)) ok = false;
    return ok;
}

static bool record_edit(wb_journal *j, const wb_edit *e, const char *sha) {
    wb_buf r = { 0 };
    bool ok = wb_buf_printf(&r, "E %s %s %u %s\n", OP_NAMES[e->op],
                            wb_kind_name(e->kind), e->index, sha)
           && append_lines(&r, '-', e->before) && append_lines(&r, '+', e->after)
           && wb_buf_append(&r, ".\n", 2)
           && write_record(j, r.data, r.length);
    wb_buf_free(&r);
    return ok;
}

static bool record_move(wb_journal *j, char what, const char *sha) {
    char line[80];
    int n = snprintf(line, sizeof line, "%c %s\n", what, sha);
    return write_record(j, line, (size_t)n);
}

/* ---- applying ------------------------------------------------------------ */

/* Move the session from one side of an edit to the other. */
static bool apply(wb_session *s, const wb_edit *e, bool forward, wb_error *error) {
    const char *target = forward ? e->after : e->before;
    bool removing = forward ? e->op == WB_EDIT_REMOVE : e->op == WB_EDIT_INSERT;
    bool inserting = forward ? e->op == WB_EDIT_INSERT : e->op == WB_EDIT_REMOVE;
    if (removing) {
        if (!wb_remove_object(s, e->kind, e->index))
            return fail(error, "cannot remove [%s] %u", wb_kind_name(e->kind), e->index);
        return true;
    }
    return wb_read_object(s, e->kind, e->index, inserting, target, strlen(target), error);
}

static bool object_text(const wb_session *s, wb_kind kind, unsigned index, char **out) {
    wb_buf b = { 0 };
    bool ok = wb_write_object(s, kind, index, &b);
    *out = ok ? dup_text(b.data, b.length) : nullptr;
    wb_buf_free(&b);
    return *out != nullptr;
}

/* Record an edit whose session change is already applied in `after_state`.
 * Commits it to `session` only when the record is written. */
static bool commit(wb_journal *j, wb_session *s, const wb_session *after_state,
                   wb_edit edit, wb_error *error) {
    char sha[65];
    if (!wb_session_sha256(after_state, sha)) {
        edit_free(&edit);
        return fail(error, "cannot serialize the edited session");
    }
    if (!record_edit(j, &edit, sha)) {
        edit_free(&edit);
        return fail(error, "cannot write journal %s", j->path);
    }
    if (!push(j, edit)) {
        edit_free(&edit);
        return fail(error, "out of memory");
    }
    memcpy(s, after_state, sizeof *s);
    return true;
}

static wb_session *clone(const wb_session *s) {
    wb_session *copy = malloc(sizeof *copy);
    if (copy) memcpy(copy, s, sizeof *copy);
    return copy;
}

static bool edit_common(wb_journal *j, wb_session *s, wb_edit_op op, wb_kind kind,
                        unsigned index, const char *key, const char *value,
                        const char *block, wb_error *error) {
    if (kind >= WB_KIND_COUNT) return fail(error, "bad kind");
    wb_session *next = clone(s);
    if (!next) return fail(error, "out of memory");
    wb_edit e = { .op = op, .kind = kind, .index = index };
    bool ok = true;
    if (op != WB_EDIT_INSERT) ok = object_text(s, kind, index, &e.before)
                                || fail(error, "no [%s] %u", wb_kind_name(kind), index);
    else ok = (e.before = dup_text("", 0)) != nullptr;
    if (ok) {
        if (op == WB_EDIT_REMOVE) {
            ok = wb_remove_object(next, kind, index)
              || fail(error, "cannot remove [%s] %u", wb_kind_name(kind), index);
            if (ok) ok = (e.after = dup_text("", 0)) != nullptr;
        } else {
            if (key) ok = wb_set_key(next, kind, index, key, value, error);
            else ok = wb_read_object(next, kind, index, op == WB_EDIT_INSERT, block,
                                     strlen(block), error);
            if (ok) ok = object_text(next, kind, index, &e.after);
        }
    }
    if (ok && op == WB_EDIT_REPLACE && !strcmp(e.before, e.after)) {
        /* No change: nothing to record. */
        edit_free(&e);
        free(next);
        return true;
    }
    if (ok) ok = commit(j, s, next, e, error);
    else edit_free(&e);
    free(next);
    return ok;
}

bool wb_edit_set_key(wb_journal *j, wb_session *s, wb_kind kind, unsigned index,
                     const char *key, const char *value, wb_error *error) {
    return edit_common(j, s, WB_EDIT_REPLACE, kind, index, key, value, nullptr, error);
}

bool wb_edit_replace(wb_journal *j, wb_session *s, wb_kind kind, unsigned index,
                     const char *block, wb_error *error) {
    return edit_common(j, s, WB_EDIT_REPLACE, kind, index, nullptr, nullptr, block, error);
}

bool wb_edit_insert(wb_journal *j, wb_session *s, wb_kind kind, unsigned index,
                    const char *block, wb_error *error) {
    return edit_common(j, s, WB_EDIT_INSERT, kind, index, nullptr, nullptr, block, error);
}

bool wb_edit_remove(wb_journal *j, wb_session *s, wb_kind kind, unsigned index,
                    wb_error *error) {
    return edit_common(j, s, WB_EDIT_REMOVE, kind, index, nullptr, nullptr, nullptr, error);
}

bool wb_journal_can_undo(const wb_journal *j) { return j->cursor > 0; }
bool wb_journal_can_redo(const wb_journal *j) { return j->cursor < j->count; }

static bool move(wb_journal *j, wb_session *s, bool undo, wb_error *error) {
    if (undo ? !j->cursor : j->cursor == j->count)
        return fail(error, undo ? "nothing to undo" : "nothing to redo");
    const wb_edit *e = &j->edit[undo ? j->cursor - 1 : j->cursor];
    wb_session *next = clone(s);
    if (!next) return fail(error, "out of memory");
    char sha[65];
    bool ok = apply(next, e, !undo, error)
           && (wb_session_sha256(next, sha) || fail(error, "cannot serialize"))
           && (record_move(j, undo ? 'U' : 'R', sha)
               || fail(error, "cannot write journal %s", j->path));
    if (ok) {
        memcpy(s, next, sizeof *s);
        j->cursor += undo ? (size_t)-1 : 1;
    }
    free(next);
    return ok;
}

bool wb_journal_undo(wb_journal *j, wb_session *s, wb_error *error) { return move(j, s, true, error); }
bool wb_journal_redo(wb_journal *j, wb_session *s, wb_error *error) { return move(j, s, false, error); }

/* ---- replay -------------------------------------------------------------- */

static bool parse_journal(wb_journal *j, const char *text, char last_sha[65]) {
    last_sha[0] = 0;
    const char *p = text;
    const char *end = strchr(p, '\n');
    if (!end || (size_t)(end - p) != 5 || strncmp(p, "wbj=1", 5)) return false;
    p = end + 1;
    while (*p) {
        end = strchr(p, '\n');
        if (!end) return false;
        size_t length = (size_t)(end - p);
        if ((p[0] == 'U' || p[0] == 'R') && length == 66 && p[1] == ' ') {
            if (p[0] == 'U' ? !j->cursor : j->cursor == j->count) return false;
            j->cursor += p[0] == 'U' ? (size_t)-1 : 1;
            memcpy(last_sha, p + 2, 64);
            last_sha[64] = 0;
            p = end + 1;
            continue;
        }
        char op[16], kind_name[16], sha[80];
        unsigned index;
        char line[160];
        if (p[0] != 'E' || length >= sizeof line) return false;
        memcpy(line, p, length);
        line[length] = 0;
        if (sscanf(line, "E %15s %15s %u %79s", op, kind_name, &index, sha) != 4
            || !wb_sha256_hex_valid(sha))
            return false;
        wb_edit e = { 0 };
        unsigned o = 0;
        while (o < 3 && strcmp(op, OP_NAMES[o])) o++;
        if (o == 3 || !wb_kind_from_name(kind_name, &e.kind)) return false;
        e.op = (wb_edit_op)o;
        e.index = index;
        wb_buf before = { 0 }, after = { 0 };
        p = end + 1;
        bool ok = true;
        for (;;) {
            end = strchr(p, '\n');
            if (!end) { ok = false; break; }
            size_t n = (size_t)(end - p);
            if (n == 1 && p[0] == '.') { p = end + 1; break; }
            wb_buf *side = p[0] == '-' ? &before : p[0] == '+' ? &after : nullptr;
            if (!side || (side == &before && after.length)) { ok = false; break; }
            ok = wb_buf_append(side, p + 1, n - 1) && wb_buf_append(side, "\n", 1);
            if (!ok) break;
            p = end + 1;
        }
        if (ok) {
            e.before = dup_text(before.data ? before.data : "", before.length);
            e.after = dup_text(after.data ? after.data : "", after.length);
            ok = e.before && e.after && push(j, e);
        }
        wb_buf_free(&before);
        wb_buf_free(&after);
        if (!ok) { edit_free(&e); return false; }
        memcpy(last_sha, sha, 65);
    }
    return true;
}

bool wb_journal_open(wb_journal *j, const char *path, const wb_session *s,
                     bool *stale, wb_error *error) {
    wb_journal_free(j);
    if (stale) *stale = false;
    j->path = strdup(path);
    if (!j->path) return fail(error, "out of memory");
    char *data;
    size_t size;
    if (!wb_read_file(path, &data, &size)) return true; /* no journal yet */
    char sha[65], last[65];
    bool ok = size && !memchr(data, 0, size) && parse_journal(j, data, last)
           && wb_session_sha256(s, sha) && (!last[0] || !strcmp(sha, last));
    free(data);
    if (ok) return true;
    truncate_to(j, 0);
    char aside[4200];
    snprintf(aside, sizeof aside, "%s.stale", path);
    if (rename(path, aside)) return fail(error, "cannot set aside stale journal %s", path);
    if (stale) *stale = true;
    return true;
}

bool wb_journal_clear(wb_journal *j, wb_error *error) {
    truncate_to(j, 0);
    if (j->path && remove(j->path) && access(j->path, F_OK) == 0)
        return fail(error, "cannot remove journal %s", j->path);
    return true;
}
