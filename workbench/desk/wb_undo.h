/* Undo/redo journal of session edits (docs/workbench-plan.md §8).
 *
 * Every edit is recorded as whole-object text: the block before and the
 * block after (empty for an insert or a remove).  A single-key change is a
 * replace whose blocks differ in one line, so one record type covers field
 * edits and structural edits, and undo is "read the other block back".
 *
 * The journal file (<session>.mws.journal) is append-only:
 *
 *   wbj=1
 *   E <replace|insert|remove> <kind> <index> <sha256 of the session after>
 *   -<line of the old block>
 *   +<line of the new block>
 *   .
 *   U <sha256 after>          undo
 *   R <sha256 after>          redo
 *
 * Replaying the records rebuilds the edit list and the cursor.  The last
 * recorded hash must equal the hash of the saved session text; otherwise the
 * journal belongs to another state and is set aside, never applied. */
#ifndef WB_UNDO_H
#define WB_UNDO_H

#include <stdbool.h>
#include <stddef.h>

#include "wb_format.h"
#include "wb_session.h"

typedef enum : uint8_t { WB_EDIT_REPLACE, WB_EDIT_INSERT, WB_EDIT_REMOVE } wb_edit_op;

typedef struct {
    wb_edit_op op;
    wb_kind kind;
    unsigned index;
    char *before;              /* NUL-terminated block text, "" for insert */
    char *after;               /* "" for remove */
} wb_edit;

typedef struct {
    wb_edit *edit;
    size_t count, capacity;
    size_t cursor;             /* edits [0, cursor) are applied */
    char *path;                /* journal file, or null for memory only */
} wb_journal;

void wb_journal_init(wb_journal *journal);
void wb_journal_free(wb_journal *journal);

/* Attach a journal file and replay it.  `session` must be the session as
 * loaded from disk.  A missing file starts an empty journal.  A journal
 * whose last hash does not match is renamed to <path>.stale and an empty
 * journal is started; *stale reports that. */
bool wb_journal_open(wb_journal *journal, const char *path,
                     const wb_session *session, bool *stale, wb_error *error);
/* Drop all history (checkpoint) and truncate the file. */
bool wb_journal_clear(wb_journal *journal, wb_error *error);

/* Edits: apply to the session and record.  On failure nothing changes. */
bool wb_edit_set_key(wb_journal *journal, wb_session *session, wb_kind kind,
                     unsigned index, const char *key, const char *value,
                     wb_error *error);
bool wb_edit_replace(wb_journal *journal, wb_session *session, wb_kind kind,
                     unsigned index, const char *block, wb_error *error);
bool wb_edit_insert(wb_journal *journal, wb_session *session, wb_kind kind,
                    unsigned index, const char *block, wb_error *error);
bool wb_edit_remove(wb_journal *journal, wb_session *session, wb_kind kind,
                    unsigned index, wb_error *error);

bool wb_journal_can_undo(const wb_journal *journal);
bool wb_journal_can_redo(const wb_journal *journal);
bool wb_journal_undo(wb_journal *journal, wb_session *session, wb_error *error);
bool wb_journal_redo(wb_journal *journal, wb_session *session, wb_error *error);

/* SHA-256 of the canonical text of a session. */
bool wb_session_sha256(const wb_session *session, char hex[65]);

#endif
