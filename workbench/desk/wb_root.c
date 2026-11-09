#define _XOPEN_SOURCE 700
#include "wb_root.h"

#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../../driver/ma_patch_file.h"
#include "wb_engine.h"
#include "wb_format.h"
#include "wb_hash.h"
#include "wb_patch.h"

extern char **environ;

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

const char *wb_home(void) {
    const char *home = getenv("WB_HOME");
    return home && *home ? home : WB_HOME_DEFAULT;
}

static bool exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static bool mkdir_one(const char *path) {
    return mkdir(path, 0755) == 0 || errno == EEXIST;
}

static bool mkdir_parents(const char *path) {
    char copy[4096];
    if (snprintf(copy, sizeof copy, "%s", path) >= (int)sizeof copy) return false;
    for (char *p = copy + 1; *p; p++)
        if (*p == '/') {
            *p = 0;
            if (!mkdir_one(copy)) return false;
            *p = '/';
        }
    return mkdir_one(copy);
}

/* Run git with the given arguments; the exit status lands in *status. */
static bool git(const char *home, int *status, const char *const *args) {
    const char *argv[24] = { "git", "-C", home };
    unsigned n = 3;
    for (; args[n - 3] && n < 23; n++) argv[n] = args[n - 3];
    argv[n] = nullptr;
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    pid_t pid;
    int rc = posix_spawnp(&pid, "git", &actions, nullptr, (char *const *)argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (rc) return false;
    int wstatus;
    while (waitpid(pid, &wstatus, 0) < 0)
        if (errno != EINTR) return false;
    *status = WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : 128;
    return true;
}

static bool write_new(const char *path, const char *text) {
    if (exists(path)) return true;
    return wb_write_file_atomic(path, text, strlen(text));
}

static const char GITIGNORE[] =
    "# Rebuildable or large files stay on disk only. Run manifests carry\n"
    "# their SHA-256 and FNV-64 values, which is enough to verify them later.\n"
    "*/cache/\n"
    "*/derived/\n"
    "*/runs/*/*.wav\n"
    "*/runs/*/*.f32\n"
    "*/trash/\n"
    "*.journal\n"
    "*.journal.stale\n"
    "*.tmp.*\n";

static const char README[] =
    "tonewheel91 Workbench sessions\n"
    "\n"
    "Each directory here is one Workbench session: the session file (.mws),\n"
    "read-only copies of its source MIDI files, session patches, recorded\n"
    "takes, and the runs rendered from it.\n"
    "\n"
    "Nothing in this folder may be published. Sources are transcriptions\n"
    "and arrangements under third-party copyright, and the renders are\n"
    "derived from them. This Git repository has no remote on purpose.\n"
    "\n"
    "The Workbench never deletes anything here. Deleting a run moves it to\n"
    "the session's trash/ directory. Caches, derived MIDI and run audio are\n"
    "in .gitignore; the committed run manifests record their hashes.\n";

bool wb_root_init(const char *home, wb_error *error) {
    if (!mkdir_parents(home)) return fail(error, "cannot create %s", home);
    char path[4096];
    snprintf(path, sizeof path, "%s/.git", home);
    int status = 0;
    if (!exists(path)) {
        const char *init[] = { "init", "-q", "-b", "main", nullptr };
        if (!git(home, &status, init) || status)
            return fail(error, "git init failed in %s", home);
    }
    snprintf(path, sizeof path, "%s/.gitignore", home);
    if (!write_new(path, GITIGNORE)) return fail(error, "cannot write %s", path);
    snprintf(path, sizeof path, "%s/README.txt", home);
    if (!write_new(path, README)) return fail(error, "cannot write %s", path);
    bool committed;
    return wb_root_commit(home, "Initialize the session root", &committed, error);
}

bool wb_root_commit(const char *home, const char *message, bool *committed,
                    wb_error *error) {
    *committed = false;
    int status = 0;
    const char *add[] = { "add", "-A", "--", ".", nullptr };
    if (!git(home, &status, add) || status) return fail(error, "git add failed in %s", home);
    const char *diff[] = { "diff", "--cached", "--quiet", nullptr };
    if (!git(home, &status, diff)) return fail(error, "git diff failed in %s", home);
    if (status == 0) return true; /* nothing staged */
    const char *commit[] = { "commit", "-q", "--no-verify", "-m", message, nullptr };
    if (!git(home, &status, commit) || status)
        return fail(error, "git commit failed in %s", home);
    *committed = true;
    return true;
}

bool wb_dir_of(const char *mws_path, char *out, size_t size) {
    const char *slash = strrchr(mws_path, '/');
    if (!slash) return snprintf(out, size, ".") < (int)size;
    size_t length = (size_t)(slash - mws_path);
    if (!length) return snprintf(out, size, "/") < (int)size;
    if (length >= size) return false;
    memcpy(out, mws_path, length);
    out[length] = 0;
    return true;
}

bool wb_root_of(const char *mws_path, char *out, size_t size) {
    char dir[4096];
    if (!wb_dir_of(mws_path, dir, sizeof dir)) return false;
    char *real = realpath(dir, nullptr);
    if (!real) return false;
    char *slash = strrchr(real, '/');
    bool ok = slash && slash != real;
    if (ok) {
        *slash = 0;
        ok = snprintf(out, size, "%s", real) < (int)size;
    }
    free(real);
    return ok;
}

bool wb_session_create(const char *home, const char *name, char **mws_path,
                       wb_error *error) {
    *mws_path = nullptr;
    if (!wb_id_valid(name)) return fail(error, "session name must be a plain name");
    char dir[4096], path[4200];
    snprintf(dir, sizeof dir, "%s/%s", home, name);
    if (exists(dir)) return fail(error, "%s already exists", dir);
    if (!mkdir_one(dir)) return fail(error, "cannot create %s", dir);
    static const char *const SUBDIRS[] = {
        "sources", "patches", "takes", "derived", "cache", "runs", "trash",
    };
    for (size_t i = 0; i < sizeof SUBDIRS / sizeof *SUBDIRS; i++) {
        snprintf(path, sizeof path, "%s/%s", dir, SUBDIRS[i]);
        if (!mkdir_one(path)) return fail(error, "cannot create %s", path);
    }
    wb_session *s = malloc(sizeof *s);
    if (!s) return fail(error, "out of memory");
    wb_session_init(s);
    s->has_head = s->has_form = true;
    snprintf(s->head.name, sizeof s->head.name, "%s", name);
    snprintf(path, sizeof path, "%s/%s.mws", dir, name);
    bool ok = wb_save(path, s, error);
    free(s);
    if (ok && !(*mws_path = strdup(path))) ok = fail(error, "out of memory");
    return ok;
}

static bool copy_file(const char *from, const char *to) {
    char *data;
    size_t size;
    if (!wb_read_file(from, &data, &size)) return false;
    bool ok = wb_write_file_atomic(to, data, size) && chmod(to, 0444) == 0;
    free(data);
    return ok;
}

bool wb_add_source(wb_session *s, const char *session_dir, const char *id,
                   const char *midi_path, wb_error *error) {
    if (!wb_id_valid(id)) return fail(error, "source id must be a plain name");
    if (wb_find_source(s, id) >= 0) return fail(error, "source '%s' already exists", id);
    if (s->source_count == WB_SOURCES) return fail(error, "more than %d sources", WB_SOURCES);
    const char *base = strrchr(midi_path, '/');
    base = base ? base + 1 : midi_path;
    char relative[WB_PATH_MAX + 1], target[4400], sha[65], existing[65];
    if (snprintf(relative, sizeof relative, "sources/%s", base) >= (int)sizeof relative
        || !wb_path_valid(relative))
        return fail(error, "source file name '%s' is not usable in a session", base);
    if (!wb_sha256_file_hex(midi_path, sha)) return fail(error, "cannot read %s", midi_path);
    snprintf(target, sizeof target, "%s/sources", session_dir);
    if (!mkdir_one(target)) return fail(error, "cannot create %s", target);
    snprintf(target, sizeof target, "%s/%s", session_dir, relative);
    if (exists(target)) {
        if (!wb_sha256_file_hex(target, existing) || strcmp(sha, existing))
            return fail(error, "%s exists with different content", target);
    } else if (!copy_file(midi_path, target)) {
        return fail(error, "cannot copy %s to %s", midi_path, target);
    }
    wb_source *src = &s->source[s->source_count++];
    wb_source_default(src);
    snprintf(src->id, sizeof src->id, "%s", id);
    snprintf(src->path, sizeof src->path, "%s", relative);
    memcpy(src->sha256, sha, sizeof src->sha256);
    return true;
}

bool wb_check_files(const wb_session *s, const char *dir, wb_error *error) {
    char path[4400], sha[65];
    for (unsigned i = 0; i < s->source_count; i++) {
        const wb_source *src = &s->source[i];
        snprintf(path, sizeof path, "%s/%s", dir, src->path);
        if (!wb_sha256_file_hex(path, sha))
            return fail(error, "source '%s': cannot read %s", src->id, path);
        if (strcmp(sha, src->sha256))
            return fail(error, "source '%s': %s has sha256 %.12s.., session says %.12s..",
                        src->id, src->path, sha, src->sha256);
    }
    for (unsigned i = 0; i < s->part_count; i++) {
        const wb_part *p = &s->part[i];
        snprintf(path, sizeof path, "%s/%s", dir, p->patch);
        if (p->engine == WB_PART_ORGAN) {
            wb_twreg reg;
            if (!wb_twreg_load(path, &reg, error)) return false;
            continue;
        }
        if (p->engine == WB_PART_EP73) {
            wb_epset set;
            if (!wb_epset_load(path, &set, error)) return false;
            continue;
        }
        ma_patch_document document;
        wb_error patch_error = { 0 };
        if (!wb_patch_load(dir, p->patch, &document, &patch_error))
            return fail(error, "part '%s': %s", p->id, patch_error.message);
    }
    return true;
}
