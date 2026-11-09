/* The session root (docs/workbench-plan.md §3.1): one folder outside the
 * repository, default /data/tonewheel91-sessions, with its own local Git
 * history and no remote.  Small text files are committed on checkpoints and
 * finished runs; caches, derived MIDI and run audio stay on disk only.
 * Nothing under the root is ever deleted by the Workbench. */
#ifndef WB_ROOT_H
#define WB_ROOT_H

#include <stdbool.h>
#include <stddef.h>

#include "wb_session.h"

#define WB_HOME_DEFAULT "/data/tonewheel91-sessions"

/* WB_HOME from the environment, else the default. */
const char *wb_home(void);

/* Create the root, its Git repository, .gitignore and README.txt, and make
 * the first commit.  Idempotent: an existing root is left as it is and only
 * missing pieces are added. */
bool wb_root_init(const char *home, wb_error *error);

/* Create <home>/<name>/ with sources/, patches/, takes/, derived/, cache/,
 * runs/ and trash/, and a skeleton <name>.mws.  Refuses an existing
 * directory.  *mws_path receives the session file path (caller frees). */
bool wb_session_create(const char *home, const char *name, char **mws_path,
                       wb_error *error);

/* Stage everything Git does not ignore under `home` and commit it with
 * `message` when anything changed.  *committed reports whether a commit
 * was made. */
bool wb_root_commit(const char *home, const char *message, bool *committed,
                    wb_error *error);

/* The root that contains a session file: the parent of its directory. */
bool wb_root_of(const char *mws_path, char *out, size_t size);
/* The directory of a session file. */
bool wb_dir_of(const char *mws_path, char *out, size_t size);

/* Copy a MIDI file into <session dir>/sources/, read-only, and add a
 * [source] block with its SHA-256.  Refuses to overwrite a different file
 * of the same name; an identical copy is reused. */
bool wb_add_source(wb_session *session, const char *session_dir,
                   const char *id, const char *midi_path, wb_error *error);

/* File checks for `wb check`: every source exists and matches its SHA-256,
 * every Mamut patch loads as a .mapatch. */
bool wb_check_files(const wb_session *session, const char *session_dir,
                    wb_error *error);

#endif
