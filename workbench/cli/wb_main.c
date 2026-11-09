/* wb: the headless Workbench.  Every operation the UI can start on a
 * session, this command can start on the same session file (plan §2.1). */
#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <signal.h>
#include <unistd.h>

#include "../desk/wb_analysis.h"
#include "../desk/wb_cache.h"
#include "../desk/wb_format.h"
#include "../desk/wb_hash.h"
#include <math.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include "../desk/wb_ledger.h"
#include "../desk/wb_import.h"
#include "../desk/wb_orch.h"
#include "../desk/wb_smf.h"
#include "../desk/wb_render.h"
#include "../desk/wb_root.h"
#include "../desk/wb_session.h"
#include "../desk/wb_undo.h"

static void usage(FILE *out) {
    fputs(
        "usage: wb COMMAND ...\n"
        "\n"
        "  root-init                          create the session root (WB_HOME, default\n"
        "                                     " WB_HOME_DEFAULT ")\n"
        "  new NAME                           create <root>/NAME/ with a skeleton NAME.mws\n"
        "  check FILE.mws [--no-files]        strict parse, semantic checks, source\n"
        "                                     hashes and patch files\n"
        "  fmt FILE.mws [-w]                  print the canonical text (-w rewrites)\n"
        "  get FILE.mws OBJECT [KEY]          print an object block or one key\n"
        "  set FILE.mws OBJECT KEY VALUE      edit one key (VALUE \"\" resets it)\n"
        "  add FILE.mws KIND BLOCK|-          append an object from block text\n"
        "  replace FILE.mws OBJECT BLOCK|-    replace an object from block text\n"
        "  rm FILE.mws OBJECT                 remove an object\n"
        "  add-source FILE.mws ID FILE.mid    copy a MIDI file into sources/ and add it\n"
        "  undo FILE.mws | redo FILE.mws      step through the edit journal\n"
        "  history FILE.mws                   list the journal\n"
        "  checkpoint FILE.mws LABEL          clear the journal and commit the root\n"
        "  import FILE.mid (--new NAME | --session FILE.mws) [--id SRC] [--preview]\n"
        "         [--split track+channel|track|channel|program|manual] [--drums N|0]\n"
        "         [--pitch-split TRACK:NOTE|TRACK:top] [--assign TRACK[:CH]=PART]\n"
        "         [--controllers LIST|none] [--keep-leading-silence] [--map FILE]\n"
        "                                     split an arrangement into parts\n"
        "  reimport FILE.mws SRC NEW.mid [--apply]\n"
        "                                     per-part diff against a changed source\n"
        "  probe FILE.mws (SECTION | FROM TO) [--preroll BARS] [--solo a,b] [--wav]\n"
        "         [--target STEM=REF+DB] [--midi SCORE.mid]\n"
        "                                     render a bar range after a pre-roll and\n"
        "                                     measure parts, stems, mix, peak driver\n"
        "  listening MIX.wav OUT.wav          constant-gain copy peaking just below -3 dBFS\n"
        "  cache FILE.mws [PART...]           render missing or stale part caches\n"
        "  ledger FILE.mws                    runs, newest first, with verdicts\n"
        "  verdict FILE.mws RUN TEXT          the run's one free-text verdict\n"
        "  diff FILE.mws RUN_A RUN_B          session and output differences\n"
        "  reproduce FILE.mws RUN             re-render a run's snapshot; compare FNVs\n"
        "  export FILE.mws RUN                Markdown snippet of a run (stdout only)\n"
        "  build-midi FILE.mws [-o OUT.mid]   apply rules and grooves; write the derived\n"
        "                                     score (default derived/<name>-<sha12>.mid)\n"
        "  render FILE.mws [--midi SCORE.mid] [--seconds N] [--label L] [--scalar]\n"
        "         [--measure] [--check] [--no-inhibit] [--no-commit]\n"
        "                                     single-pass render: stems + mix into\n"
        "                                     runs/<id>/, manifest, ledger line, commit\n"
        "\n"
        "OBJECT is session, form, KIND:ID (source, stem, part, curve; marker by\n"
        "name) or KIND:#N with N counted from 1.  Edits go through the journal\n"
        "FILE.mws.journal and save the session atomically.\n",
        out);
}

static void report(const char *what, const wb_error *e) {
    if (e->line) fprintf(stderr, "%s:%u: %s\n", what, e->line, e->message);
    else fprintf(stderr, "%s: %s\n", what, e->message);
}

static bool resolve(const wb_session *s, const char *text, wb_kind *kind, unsigned *index) {
    char name[16];
    const char *colon = strchr(text, ':');
    size_t length = colon ? (size_t)(colon - text) : strlen(text);
    if (!length || length >= sizeof name) return false;
    memcpy(name, text, length);
    name[length] = 0;
    if (!wb_kind_from_name(name, kind)) return false;
    if (wb_kind_singleton(*kind)) {
        *index = 0;
        return !colon && wb_kind_count(s, *kind);
    }
    if (!colon || !colon[1]) return false;
    const char *key = colon + 1;
    if (key[0] == '#') {
        char *end;
        unsigned long n = strtoul(key + 1, &end, 10);
        if (*end || !n || n > wb_kind_count(s, *kind)) return false;
        *index = (unsigned)n - 1;
        return true;
    }
    int found = -1;
    switch (*kind) {
    case WB_KIND_SOURCE: found = wb_find_source(s, key); break;
    case WB_KIND_STEM: found = wb_find_stem(s, key); break;
    case WB_KIND_PART: found = wb_find_part(s, key); break;
    case WB_KIND_CURVE: found = wb_find_curve(s, key); break;
    case WB_KIND_MARKER:
        for (unsigned i = 0; i < s->marker_count && found < 0; i++)
            if (!strcmp(s->marker[i].name, key)) found = (int)i;
        break;
    default: break;
    }
    if (found < 0) return false;
    *index = (unsigned)found;
    return true;
}

static char *read_block(const char *arg) {
    if (strcmp(arg, "-")) {
        char *data;
        size_t size;
        if (wb_read_file(arg, &data, &size)) return data;
        /* Not a file: the argument is the block text itself. */
        return strdup(arg);
    }
    wb_buf b = { 0 };
    char chunk[4096];
    size_t got;
    while ((got = fread(chunk, 1, sizeof chunk, stdin))) wb_buf_append(&b, chunk, got);
    if (!b.data) wb_buf_append(&b, "", 0);
    return b.data;
}

typedef struct {
    const char *path;
    wb_session *session;
    wb_journal journal;
} open_session;

static bool open_for_edit(open_session *o, const char *path) {
    *o = (open_session){ .path = path, .session = malloc(sizeof(wb_session)) };
    wb_journal_init(&o->journal);
    wb_error e = { 0 };
    if (!o->session || !wb_load(path, o->session, &e)) {
        report(path, &e);
        return false;
    }
    char journal[4200];
    snprintf(journal, sizeof journal, "%s.journal", path);
    bool stale = false;
    if (!wb_journal_open(&o->journal, journal, o->session, &stale, &e)) {
        report(journal, &e);
        return false;
    }
    if (stale) fprintf(stderr, "%s: did not match the session; moved to %s.stale\n", journal, journal);
    return true;
}

static void close_session(open_session *o) {
    wb_journal_free(&o->journal);
    free(o->session);
}

static int save_or_fail(open_session *o) {
    wb_error e = { 0 };
    if (!wb_save(o->path, o->session, &e)) {
        report(o->path, &e);
        return 1;
    }
    return 0;
}

static int cmd_check(int argc, char **argv) {
    if (argc < 1 || argc > 2 || (argc == 2 && strcmp(argv[1], "--no-files"))) return 2;
    wb_session *s = malloc(sizeof *s);
    wb_error e = { 0 };
    int rc = 1;
    if (!s) return 1;
    if (!wb_load(argv[0], s, &e)) report(argv[0], &e);
    else if (!wb_session_validate(s, &e)) report(argv[0], &e);
    else {
        char dir[4096];
        if (argc == 1 && (!wb_dir_of(argv[0], dir, sizeof dir) || !wb_check_files(s, dir, &e)))
            report(argv[0], &e);
        else {
            printf("%s: ok: %u sources, %u stems, %u parts, %u rules, %u grooves, "
                   "%u curves, %u lanes, %u markers, form bars %d..%d, %u sections\n",
                   argv[0], s->source_count, s->stem_count, s->part_count, s->rule_count,
                   s->groove_count, s->curve_count, s->lane_count, s->marker_count,
                   s->form.start_bar, s->form.end_bar, s->form.section_count);
            rc = 0;
        }
    }
    free(s);
    return rc;
}

static int cmd_fmt(int argc, char **argv) {
    if (argc < 1 || argc > 2 || (argc == 2 && strcmp(argv[1], "-w"))) return 2;
    wb_session *s = malloc(sizeof *s);
    wb_error e = { 0 };
    if (!s) return 1;
    int rc = 1;
    if (!wb_load(argv[0], s, &e)) report(argv[0], &e);
    else if (argc == 2) {
        if (wb_save(argv[0], s, &e)) rc = 0;
        else report(argv[0], &e);
    } else {
        wb_buf out = { 0 };
        if (wb_write(s, &out) && fwrite(out.data, 1, out.length, stdout) == out.length) rc = 0;
        wb_buf_free(&out);
    }
    free(s);
    return rc;
}

static int cmd_get(int argc, char **argv) {
    if (argc < 2 || argc > 3) return 2;
    wb_session *s = malloc(sizeof *s);
    wb_error e = { 0 };
    if (!s) return 1;
    int rc = 1;
    wb_kind kind;
    unsigned index;
    wb_buf out = { 0 };
    if (!wb_load(argv[0], s, &e)) report(argv[0], &e);
    else if (!resolve(s, argv[1], &kind, &index)) fprintf(stderr, "no object %s\n", argv[1]);
    else if (argc == 2 ? wb_write_object(s, kind, index, &out)
                       : wb_get_key(s, kind, index, argv[2], &out)) {
        fputs(out.data, stdout);
        if (argc == 3) putchar('\n');
        rc = 0;
    } else fprintf(stderr, "no key %s\n", argc == 3 ? argv[2] : "");
    wb_buf_free(&out);
    free(s);
    return rc;
}

typedef enum { EDIT_SET, EDIT_ADD, EDIT_REPLACE, EDIT_RM, EDIT_UNDO, EDIT_REDO } edit_kind;

static int cmd_edit(edit_kind what, int argc, char **argv) {
    static const int ARGS[] = { 4, 3, 3, 2, 1, 1 };
    if (argc != ARGS[what]) return 2;
    open_session o;
    int rc = 1;
    wb_error e = { 0 };
    if (!open_for_edit(&o, argv[0])) goto done;
    wb_kind kind = WB_KIND_SESSION;
    unsigned index = 0;
    if (what == EDIT_ADD) {
        if (!wb_kind_from_name(argv[1], &kind)) {
            fprintf(stderr, "unknown kind %s\n", argv[1]);
            goto done;
        }
        index = wb_kind_count(o.session, kind);
    } else if ((what == EDIT_SET || what == EDIT_REPLACE || what == EDIT_RM)
               && !resolve(o.session, argv[1], &kind, &index)) {
        fprintf(stderr, "no object %s\n", argv[1]);
        goto done;
    }
    bool ok;
    char *block = nullptr;
    switch (what) {
    case EDIT_SET: ok = wb_edit_set_key(&o.journal, o.session, kind, index, argv[2], argv[3], &e); break;
    case EDIT_ADD:
    case EDIT_REPLACE:
        block = read_block(argv[2]);
        ok = block && (what == EDIT_ADD
            ? wb_edit_insert(&o.journal, o.session, kind, index, block, &e)
            : wb_edit_replace(&o.journal, o.session, kind, index, block, &e));
        break;
    case EDIT_RM: ok = wb_edit_remove(&o.journal, o.session, kind, index, &e); break;
    case EDIT_UNDO: ok = wb_journal_undo(&o.journal, o.session, &e); break;
    default: ok = wb_journal_redo(&o.journal, o.session, &e); break;
    }
    free(block);
    if (!ok) {
        report(argv[0], &e);
        goto done;
    }
    rc = save_or_fail(&o);
    if (!rc) {
        wb_error v = { 0 };
        if (!wb_session_validate(o.session, &v))
            fprintf(stderr, "%s: saved; not yet complete: %s\n", argv[0], v.message);
    }
done:
    close_session(&o);
    return rc;
}

static int cmd_history(int argc, char **argv) {
    if (argc != 1) return 2;
    open_session o;
    int rc = 1;
    if (open_for_edit(&o, argv[0])) {
        static const char *const OPS[] = { "replace", "insert", "remove" };
        for (size_t i = 0; i < o.journal.count; i++) {
            const wb_edit *e = &o.journal.edit[i];
            printf("%c %3zu %-7s [%s] #%u\n", i < o.journal.cursor ? '*' : ' ', i + 1,
                   OPS[e->op], wb_kind_name(e->kind), e->index + 1);
        }
        printf("%zu edits, %zu applied\n", o.journal.count, o.journal.cursor);
        rc = 0;
    }
    close_session(&o);
    return rc;
}

static int cmd_add_source(int argc, char **argv) {
    if (argc != 3) return 2;
    open_session o;
    int rc = 1;
    wb_error e = { 0 };
    char dir[4096];
    if (!open_for_edit(&o, argv[0])) goto done;
    if (!wb_dir_of(argv[0], dir, sizeof dir)) goto done;
    /* Copy and hash on a scratch session, then record the block through the
     * journal like any other insert. */
    wb_session *scratch = malloc(sizeof *scratch);
    if (!scratch) goto done;
    memcpy(scratch, o.session, sizeof *scratch);
    wb_buf block = { 0 };
    bool ok = wb_add_source(scratch, dir, argv[1], argv[2], &e)
           && wb_write_object(scratch, WB_KIND_SOURCE, scratch->source_count - 1, &block)
           && wb_edit_insert(&o.journal, o.session, WB_KIND_SOURCE, o.session->source_count,
                             block.data, &e);
    if (ok) {
        printf("%s: source %s = %s sha256 %s\n", argv[0], argv[1],
               o.session->source[o.session->source_count - 1].path,
               o.session->source[o.session->source_count - 1].sha256);
        rc = save_or_fail(&o);
    } else report(argv[0], &e);
    wb_buf_free(&block);
    free(scratch);
done:
    close_session(&o);
    return rc;
}

static int cmd_checkpoint(int argc, char **argv) {
    if (argc != 2) return 2;
    open_session o;
    int rc = 1;
    wb_error e = { 0 };
    char root[4096], message[256];
    if (!open_for_edit(&o, argv[0])) goto done;
    if (!wb_root_of(argv[0], root, sizeof root)) {
        fprintf(stderr, "%s: cannot find the session root\n", argv[0]);
        goto done;
    }
    snprintf(message, sizeof message, "%s: checkpoint %s", o.session->head.name, argv[1]);
    bool committed = false;
    if (save_or_fail(&o) || !wb_journal_clear(&o.journal, &e)
        || !wb_root_commit(root, message, &committed, &e)) {
        if (e.message[0]) report(argv[0], &e);
        goto done;
    }
    printf("%s: %s\n", root, committed ? message : "nothing to commit");
    rc = 0;
done:
    close_session(&o);
    return rc;
}

static volatile int cancel_requested;
static void on_terminate(int signal_number) {
    (void)signal_number;
    cancel_requested = 1;
}

static void catch_terminate(void) {
    struct sigaction action = { .sa_handler = on_terminate };
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGINT, &action, nullptr);
}

static void print_derived(const wb_session *s, const wb_derived *d) {
    for (unsigned p = 0; p < s->part_count; p++) {
        const wb_opart *x = &d->part[p];
        unsigned lo = 127, hi = 0;
        uint64_t last = 0;
        for (size_t i = 0; i < x->count; i++) {
            if (x->note[i].note < lo) lo = x->note[i].note;
            if (x->note[i].note > hi) hi = x->note[i].note;
            if (x->note[i].end > last) last = x->note[i].end;
        }
        double tpb = d->ticks_per_bar, start = s->form.start_bar;
        printf("  ch%-2u %-8s %4zu notes  held peak %u x unison %u / limit %u%s  range %u-%u  bars %.2f-%.2f"
               "  merged %u cut %u\n",
               p, s->part[p].id, x->count, x->held_peak, s->part[p].unison, s->part[p].limit,
               x->held_peak * s->part[p].unison > s->part[p].limit ? " OVER" : "",
               x->count ? lo : 0, x->count ? hi : 0,
               x->count ? start + (double)x->note[0].start / tpb : 0, start + (double)last / tpb,
               x->merged, x->cut);
    }
    if (d->unmatched_offs || d->stuck_notes)
        printf("  sources: %u unmatched releases dropped, %u stuck notes closed at track end\n",
               d->unmatched_offs, d->stuck_notes);
}

static int cmd_build_midi(int argc, char **argv) {
    if (argc != 1 && !(argc == 3 && !strcmp(argv[1], "-o"))) return 2;
    wb_session *s = malloc(sizeof *s);
    wb_derived *d = calloc(1, sizeof *d);
    wb_error e = { 0 };
    char dir[4096], path[4400];
    int rc = 1;
    if (!s || !d) goto done;
    if (!wb_load(argv[0], s, &e) || !wb_dir_of(argv[0], dir, sizeof dir) || !wb_orch_build(s, dir, d, &e)) {
        fprintf(stderr, "%s: %s\n", argv[0], e.message);
        goto done;
    }
    if (argc == 3) {
        snprintf(path, sizeof path, "%s", argv[2]);
        if (!wb_write_file_atomic(path, d->midi.data, d->midi.length)) {
            fprintf(stderr, "%s: cannot write\n", path);
            goto done;
        }
    } else if (!wb_orch_write(d, s, dir, path, sizeof path, &e)) {
        fprintf(stderr, "%s: %s\n", argv[0], e.message);
        goto done;
    }
    printf("%s: sha256 %s\n", path, d->sha256);
    print_derived(s, d);
    rc = 0;
done:
    if (d) wb_derived_free(d);
    free(d);
    free(s);
    return rc;
}

static const char *const SPLITS[] = { "track+channel", "track", "channel", "program", "manual" };

static void print_plan(const wb_import_plan *plan) {
    printf("  form bars %d..%d, %u sections from markers, title \"%s\"\n", plan->start_bar,
           plan->end_bar, plan->section_count, plan->title);
    printf("  %-18s %-6s %-8s %-7s %5s %5s %-9s %-9s %-13s %s\n", "part (suggested)", "engine",
           "template", "stem", "notes", "held", "limit/uni", "range", "bars", "velocity");
    for (unsigned i = 0; i < plan->part_count; i++) {
        const wb_import_part *p = &plan->part[i];
        char lim[16], range[16], bars[24], vel[16];
        snprintf(lim, sizeof lim, "%u/%u", p->limit, p->unison);
        snprintf(range, sizeof range, "%u-%u", p->low, p->high);
        snprintf(bars, sizeof bars, "%.2f-%.2f", p->first_bar, p->last_bar);
        snprintf(vel, sizeof vel, "%u-%u", p->velocity_min, p->velocity_max);
        printf("  %-18s %-6s %-8s %-7s %5u %5u %-9s %-9s %-13s %s%s%s\n", p->id, p->engine,
               p->template_name, p->stem, p->notes, p->held_peak, lim, range, bars, vel,
               p->compass_ok ? "" : "  outside organ compass 36..96",
               strcmp(p->engine, "mamut") ? "  (plays on Mamut until W9)" : "");
    }
    if (plan->empty_streams) printf("  %u tracks without notes (no part)\n", plan->empty_streams);
    if (plan->unmatched_offs || plan->stuck_notes || plan->running_status || plan->zero_velocity_offs)
        printf("  source: %u unmatched releases dropped, %u stuck notes closed at track end, "
               "%u running-status events, %u zero-velocity note-offs\n",
               plan->unmatched_offs, plan->stuck_notes, plan->running_status, plan->zero_velocity_offs);
}

/* Record the difference between two sessions as journal edits: objects
 * appended by an import become inserts, a changed head or form a replace. */
static bool journal_growth(wb_journal *j, wb_session *s, const wb_session *next, wb_error *e) {
    for (unsigned k = 0; k < WB_KIND_COUNT; k++) {
        wb_kind kind = (wb_kind)k;
        if (wb_kind_singleton(kind)) {
            if (!wb_kind_count(next, kind)) continue;
            wb_buf a = { 0 }, b = { 0 };
            bool same = wb_kind_count(s, kind) && wb_write_object(s, kind, 0, &a)
                     && wb_write_object(next, kind, 0, &b) && a.length == b.length
                     && !memcmp(a.data, b.data, a.length);
            bool ok = same || (b.length || wb_write_object(next, kind, 0, &b));
            if (ok && !same)
                ok = wb_kind_count(s, kind) ? wb_edit_replace(j, s, kind, 0, b.data, e)
                                             : wb_edit_insert(j, s, kind, 0, b.data, e);
            wb_buf_free(&a);
            wb_buf_free(&b);
            if (!ok) return false;
            continue;
        }
        for (unsigned i = wb_kind_count(s, kind); i < wb_kind_count(next, kind); i++) {
            wb_buf b = { 0 };
            bool ok = wb_write_object(next, kind, i, &b) && wb_edit_insert(j, s, kind, i, b.data, e);
            wb_buf_free(&b);
            if (!ok) return false;
        }
    }
    return true;
}

static int cmd_import(int argc, char **argv) {
    if (argc < 1) return 2;
    const char *midi = argv[0], *new_name = nullptr, *session_path = nullptr, *id = "src";
    bool preview = false, drums_set = false;
    wb_import_options o;
    wb_import_options_default(&o);
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : nullptr;
        if (!strcmp(a, "--new") && v) { new_name = v; i++; }
        else if (!strcmp(a, "--session") && v) { session_path = v; i++; }
        else if (!strcmp(a, "--id") && v) { id = v; i++; }
        else if (!strcmp(a, "--preview")) preview = true;
        else if (!strcmp(a, "--keep-leading-silence")) o.keep_leading_silence = true;
        else if (!strcmp(a, "--map") && v) { o.map_path = v; i++; }
        else if (!strcmp(a, "--split") && v) {
            unsigned k = 0;
            while (k < 5 && strcmp(v, SPLITS[k])) k++;
            if (k == 5) return 2;
            o.split = (wb_split)k;
            i++;
        } else if (!strcmp(a, "--drums") && v) {
            o.drums = atoi(v);
            if (o.drums < 0 || o.drums > 16) return 2;
            drums_set = true;
            i++;
        } else if (!strcmp(a, "--program-split")) o.program_split = true;
        else if (!strcmp(a, "--pitch-split") && v && o.pitch_split_count < WB_PITCH_SPLITS) {
            wb_pitch_split *p = &o.pitch_split[o.pitch_split_count];
            const char *colon = strchr(v, ':');
            if (!colon) return 2;
            p->track = atoi(v);
            p->note = !strcmp(colon + 1, "top") ? -1 : atoi(colon + 1);
            if (p->note == 0 || p->note > 127) return 2;
            o.pitch_split_count++;
            i++;
        } else if (!strcmp(a, "--assign") && v && o.assign_count < WB_ASSIGNS) {
            wb_assign *as = &o.assign[o.assign_count];
            const char *eq = strchr(v, '=');
            if (!eq || !wb_id_valid(eq + 1)) return 2;
            as->track = atoi(v);
            const char *colon = strchr(v, ':');
            as->channel = colon && colon < eq ? atoi(colon + 1) : -1;
            snprintf(as->part, sizeof as->part, "%s", eq + 1);
            o.assign_count++;
            o.split = WB_SPLIT_MANUAL;
            i++;
        } else if (!strcmp(a, "--controllers") && v) {
            /* Reuse the session parser through a scratch rule block. */
            wb_session *tmp = malloc(sizeof *tmp);
            wb_error pe;
            char block[512];
            snprintf(block, sizeof block, "mws=1\n[rule]\npart=x\nfrom=x:*\ncontrollers=%s\n", v);
            bool ok = tmp && wb_read(block, strlen(block), tmp, &pe);
            if (ok) o.controllers = tmp->rule[0].controllers;
            free(tmp);
            if (!ok) return 2;
            i++;
        } else return 2;
    }
    /* The track split keeps whole tracks: no drum-note groups unless asked. */
    if (!drums_set && o.split == WB_SPLIT_TRACK) o.drums = 0;
    if (!preview && !new_name == !session_path) {
        fprintf(stderr, "import: give exactly one of --new NAME or --session FILE.mws (or --preview)\n");
        return 2;
    }
    wb_smf *smf = calloc(1, sizeof *smf);
    wb_import_plan *plan = calloc(1, sizeof *plan);
    wb_error e = { 0 };
    int rc = 1;
    char *created = nullptr;
    open_session os = { 0 };
    wb_journal_init(&os.journal);
    if (!smf || !plan) goto done;
    if (!wb_smf_load(midi, smf, &e) || !wb_import_plan_build(smf, &o, plan, &e)) {
        fprintf(stderr, "%s: %s\n", midi, e.message);
        goto done;
    }
    printf("%s: format %u, %u tracks, PPQ %u, split %s%s\n", midi, smf->format, smf->track_count,
           smf->division, SPLITS[o.split], o.drums ? ", drum groups on the drum channel" : "");
    print_plan(plan);
    if (preview) { rc = 0; goto done; }
    if (new_name) {
        if (!wb_session_create(wb_home(), new_name, &created, &e)) {
            fprintf(stderr, "%s: %s\n", new_name, e.message);
            goto done;
        }
        session_path = created;
    }
    if (!open_for_edit(&os, session_path)) goto done;
    char dir[4096], templates[4200];
    wb_dir_of(session_path, dir, sizeof dir);
    snprintf(templates, sizeof templates, "%s/templates", wb_home());
    wb_session *next = malloc(sizeof *next);
    if (!next) goto done;
    memcpy(next, os.session, sizeof *next);
    if (new_name) {
        /* A new session takes its name from the source title when it has one. */
        if (plan->title[0]) snprintf(next->head.name, sizeof next->head.name, "%s", plan->title);
    }
    bool ok = wb_import_apply(next, dir, id, midi, plan, &o, templates, &e)
           && journal_growth(&os.journal, os.session, next, &e);
    free(next);
    if (!ok) {
        fprintf(stderr, "%s: %s\n", session_path, e.message);
        goto done;
    }
    rc = save_or_fail(&os);
    if (!rc) printf("%s: imported %u parts from %s as source '%s'\n", session_path, plan->part_count, midi, id);
done:
    if (os.session) close_session(&os);
    else wb_journal_free(&os.journal);
    if (smf) wb_smf_free(smf);
    free(smf);
    free(plan);
    free(created);
    return rc;
}

/* Multiset difference of two note lists, per part. */
static void diff_part(const wb_opart *a, const wb_opart *b, unsigned *added, unsigned *removed, unsigned *moved) {
    bool *used = calloc(b->count ? b->count : 1, sizeof *used);
    bool *matched = calloc(a->count ? a->count : 1, sizeof *matched);
    *added = *removed = *moved = 0;
    for (size_t i = 0; i < a->count && used && matched; i++)
        for (size_t j = 0; j < b->count; j++)
            if (!used[j] && !memcmp(&a->note[i], &b->note[j], sizeof a->note[i])) {
                used[j] = matched[i] = true;
                break;
            }
    /* A remaining pair with the same pitch and velocity is a moved note. */
    for (size_t i = 0; i < a->count && used && matched; i++) {
        if (matched[i]) continue;
        bool found = false;
        for (size_t j = 0; j < b->count && !found; j++)
            if (!used[j] && a->note[i].note == b->note[j].note && a->note[i].velocity == b->note[j].velocity) {
                used[j] = found = true;
                (*moved)++;
            }
        if (!found) (*removed)++;
    }
    for (size_t j = 0; j < b->count && used; j++) *added += !used[j];
    free(used);
    free(matched);
}

static int cmd_reimport(int argc, char **argv) {
    if (argc != 3 && !(argc == 4 && !strcmp(argv[3], "--apply"))) return 2;
    open_session os;
    int rc = 1;
    wb_error e = { 0 };
    wb_derived *before = calloc(1, sizeof *before), *after = calloc(1, sizeof *after);
    wb_session *next = malloc(sizeof *next);
    char dir[4096];
    if (!before || !after || !next || !open_for_edit(&os, argv[0])) goto done;
    wb_dir_of(argv[0], dir, sizeof dir);
    int src = wb_find_source(os.session, argv[1]);
    if (src < 0) { fprintf(stderr, "no source '%s'\n", argv[1]); goto done; }
    if (!wb_orch_build(os.session, dir, before, &e)) goto report;
    /* The new file goes in beside the old one under a content-named copy. */
    memcpy(next, os.session, sizeof *next);
    wb_source old = next->source[src];
    next->source_count = 0;
    char id[WB_ID_MAX + 1], sha[65];
    snprintf(id, sizeof id, "%s", old.id);
    if (!wb_sha256_file_hex(argv[2], sha)) { fprintf(stderr, "cannot read %s\n", argv[2]); goto done; }
    char renamed[4400];
    const char *base = strrchr(argv[2], '/');
    base = base ? base + 1 : argv[2];
    char stem[200];
    snprintf(stem, sizeof stem, "%s", base);
    char *dot = strrchr(stem, '.');
    if (dot) *dot = 0;
    snprintf(renamed, sizeof renamed, "%s/%s-%.8s.mid", dir, stem, sha);
    {
        char *bytes;
        size_t size;
        if (!wb_read_file(argv[2], &bytes, &size)) goto done;
        bool wrote = wb_write_file_atomic(renamed, bytes, size);
        free(bytes);
        if (!wrote) goto done;
    }
    wb_session *probe = malloc(sizeof *probe);
    if (!probe) goto done;
    memcpy(probe, os.session, sizeof *probe);
    probe->source_count = 0;
    bool added = wb_add_source(probe, dir, id, renamed, &e);
    remove(renamed);
    if (added) {
        wb_source fresh = probe->source[0];
        fresh.offset_bars = old.offset_bars;
        fresh.us_per_quarter = old.us_per_quarter;
        fresh.tempo_scale = old.tempo_scale;
        memcpy(next, os.session, sizeof *next);
        next->source[src] = fresh;
    }
    free(probe);
    if (!added || !wb_orch_build(next, dir, after, &e)) goto report;
    printf("%s: source '%s' %.12s.. -> %.12s..\n", argv[0], id, old.sha256, next->source[src].sha256);
    for (unsigned p = 0; p < os.session->part_count; p++) {
        unsigned add, rem, mov;
        diff_part(&before->part[p], &after->part[p], &add, &rem, &mov);
        long ctl = (long)after->part[p].ctl_count - (long)before->part[p].ctl_count;
        printf("  %-12s %5u added %5u removed %5u moved  controllers %+ld%s\n", os.session->part[p].id,
               add, rem, mov, ctl, add || rem || mov || ctl ? "" : "  (unchanged)");
    }
    if (argc == 4) {
        wb_buf block = { 0 };
        bool ok = wb_write_object(next, WB_KIND_SOURCE, (unsigned)src, &block)
               && wb_edit_replace(&os.journal, os.session, WB_KIND_SOURCE, (unsigned)src, block.data, &e);
        wb_buf_free(&block);
        if (!ok) goto report;
        rc = save_or_fail(&os);
        if (!rc) printf("  relinked; patches, gains and hand edits kept\n");
    } else rc = 0;
    goto done;
report:
    fprintf(stderr, "%s: %s\n", argv[0], e.message);
done:
    if (before) wb_derived_free(before);
    if (after) wb_derived_free(after);
    free(before);
    free(after);
    free(next);
    close_session(&os);
    return rc;
}

static double out_rms_db(const wb_render_report *r, unsigned o) {
    return wb_dbfs(sqrt(r->level[o].square / (2.0 * (double)r->frames)));
}

static int cmd_probe(int argc, char **argv) {
    if (argc < 2) return 2;
    const char *path = argv[0], *midi = nullptr, *label = "probe", *solo = nullptr, *target = nullptr;
    double from = 0, to = 0, preroll = 4;
    bool wav = false;
    int i = 1;
    wb_session *s = malloc(sizeof *s);
    wb_error e = { 0 };
    wb_score score = { 0 };
    wb_render_report *report = calloc(1, sizeof *report);
    char dir[4096];
    int rc = 1;
    if (!s || !report || !wb_load(path, s, &e) || !wb_dir_of(path, dir, sizeof dir)) {
        fprintf(stderr, "%s: %s\n", path, e.message);
        goto done;
    }
    /* A section name or two bar numbers. */
    char *end;
    from = strtod(argv[1], &end);
    if (*end) {
        unsigned k = 0;
        while (k < s->form.section_count && strcmp(s->form.section[k].name, argv[1])) k++;
        if (k == s->form.section_count) { fprintf(stderr, "no section '%s'\n", argv[1]); goto done; }
        from = s->form.section[k].start_bar;
        to = s->form.section[k].end_bar;
        i = 2;
    } else {
        if (argc < 3) { rc = 2; goto done; }
        to = strtod(argv[2], &end);
        if (*end || to <= from) { rc = 2; goto done; }
        i = 3;
    }
    for (; i < argc; i++) {
        const char *v = i + 1 < argc ? argv[i + 1] : nullptr;
        if (!strcmp(argv[i], "--preroll") && v) { preroll = atof(v); i++; }
        else if (!strcmp(argv[i], "--solo") && v) { solo = v; i++; }
        else if (!strcmp(argv[i], "--midi") && v) { midi = v; i++; }
        else if (!strcmp(argv[i], "--label") && v) { label = v; i++; }
        else if (!strcmp(argv[i], "--target") && v) { target = v; i++; }
        else if (!strcmp(argv[i], "--wav")) wav = true;
        else { rc = 2; goto done; }
    }
    char built[4400];
    if (!midi) {
        wb_derived *d = calloc(1, sizeof *d);
        bool ok = d && wb_orch_build(s, dir, d, &e) && wb_orch_write(d, s, dir, built, sizeof built, &e);
        if (d) wb_derived_free(d);
        free(d);
        if (!ok) { fprintf(stderr, "%s: %s\n", path, e.message); goto done; }
        midi = built;
    }
    if (!wb_score_load_midi(s, midi, &score, &e)) { fprintf(stderr, "%s: %s\n", path, e.message); goto done; }
    if (solo) {
        /* Probe-only mute: parts outside the solo list play at gain zero. */
        for (unsigned p = 0; p < s->part_count; p++) {
            char list[512];
            snprintf(list, sizeof list, ",%s,", solo);
            char needle[40];
            snprintf(needle, sizeof needle, ",%s,", s->part[p].id);
            if (!strstr(list, needle)) s->part[p].gain = 0;
        }
    }
    double start = from - preroll < s->form.start_bar ? s->form.start_bar : from - preroll;
    wb_run_info run = { .score_path = midi, .probe_from_bar = from, .probe_to_bar = to,
                        .probe_preroll_bars = from - start };
    if (!wb_session_sha256(s, run.session_sha256) || !wb_run_begin(&run, dir, "probe", label, &e)) {
        fprintf(stderr, "%s: %s\n", path, e.message);
        goto done;
    }
    char prefix[4400], snapshot[4500];
    snprintf(prefix, sizeof prefix, "%s/%s", run.run_dir, run.id);
    snprintf(snapshot, sizeof snapshot, "%s/session.mws", run.run_dir);
    if (!wb_save(snapshot, s, &e)) { fprintf(stderr, "%s: %s\n", path, e.message); goto done; }
    wb_render_options options = {
        .prefix = wav ? prefix : nullptr,
        .frames = wb_timemap_bar_frame(&score.map, to),
        .preroll_from = wb_timemap_bar_frame(&score.map, start),
        .measure_from = wb_timemap_bar_frame(&score.map, from),
        .cancel = &cancel_requested,
    };
    catch_terminate();
    printf("%s: probe bars %g..%g (pre-roll from %g), run %s\n", path, from, to, start, run.id);
    fflush(stdout);
    wb_host_info host;
    wb_host_probe(&host);
    if (!wb_render(s, dir, &score, &options, report, &e)) {
        fprintf(stderr, "%s: probe failed: %s\n", path, e.message);
        goto done;
    }
    if (!wb_manifest_write(&run, s, dir, &score, report, &host, &e) || !wb_ledger_append(dir, &run, report, &e)) {
        fprintf(stderr, "%s: %s\n", path, e.message);
        goto done;
    }
    char previous[4600] = "", manifest[4700];
    bool have_previous = wb_previous_probe(dir, run.id, from, to, previous, sizeof previous);
    snprintf(manifest, sizeof manifest, "%s/manifest.mwr", previous);
    unsigned mix = report->outputs - 1;
    double mix_peak = report->level[mix].peak;
    printf("  %-10s %9s %9s %7s %9s%s\n", "output", "RMS dBFS", "peak dBFS", "crest", "of peak",
           have_previous ? "   delta RMS vs previous probe" : "");
    for (unsigned p = 0; p < s->part_count; p++) {
        double r = wb_dbfs(sqrt(report->part_square[p] / (2.0 * (double)report->frames)));
        double pk = wb_dbfs(report->part_peak[p]);
        printf("  %-10s %9.2f %9.2f %7.2f %8.0f%%\n", s->part[p].id, r, pk, pk - r,
               mix_peak > 0 ? 100.0 * report->part_peak[p] / mix_peak : 0);
    }
    for (unsigned o = 0; o < report->outputs; o++) {
        const char *name = o == mix ? "mix" : s->stem[o].id;
        double r = out_rms_db(report, o), pk = wb_dbfs(report->level[o].peak);
        char delta[48] = "";
        char key[64], value[64];
        snprintf(key, sizeof key, "out.%s.rms_dbfs", name);
        if (have_previous && wb_manifest_get(manifest, key, value, sizeof value))
            snprintf(delta, sizeof delta, "   %+.2f dB", r - atof(value));
        printf("  [%-8s] %9.2f %9.2f %7.2f %8.0f%%%s\n", name, r, pk, pk - r,
               mix_peak > 0 ? 100.0 * report->level[o].peak / mix_peak : 0, delta);
    }
    if (report->peak_count) {
        /* The stem that sets the mix peak, which decides the listening gain. */
        const wb_peak *x = &report->peak[0];
        unsigned best = 0;
        for (unsigned st = 1; st < s->stem_count; st++)
            if (x->stem[st] * x->mix > x->stem[best] * x->mix) best = st;
        printf("  peak driver: %s (%.0f%% of the mix peak at bar %.2f)\n", s->stem[best].id,
               x->mix != 0 ? 100.0 * x->stem[best] / x->mix : 0,
               (double)wb_timemap_bar(&score.map, x->frame));
    }
    if (target) {
        /* STEM=REF+DB: the gain change that puts STEM at REF's RMS + DB.
         * Shown only; the operator applies it. */
        char a[40], b[40];
        double offset = 0;
        if (sscanf(target, "%39[^=]=%39[^+-]%lf", a, b, &offset) >= 2) {
            int sa = wb_find_stem(s, a), sb = wb_find_stem(s, b);
            if (sa >= 0 && sb >= 0) {
                double change = out_rms_db(report, (unsigned)sb) + offset - out_rms_db(report, (unsigned)sa);
                printf("  suggestion: parts in stem '%s' %+.2f dB (gain x%.4f) puts it at %s %+.1f dB\n",
                       a, change, pow(10, change / 20), b, offset);
            } else fprintf(stderr, "--target names unknown stems\n");
        }
    }
    printf("  wall %.1f s, manifest %s/manifest.mwr\n", report->wall_seconds, run.run_dir);
    rc = 0;
done:
    wb_score_free(&score);
    free(report);
    free(s);
    return rc;
}

/* ---- W10: ledger, verdicts, diff, reproduce, export ------------------------ */

static bool run_paths(const char *mws, const char *run, char *dir, char *run_dir, char *manifest) {
    if (!wb_dir_of(mws, dir, 4096)) return false;
    snprintf(run_dir, 4400, "%s/runs/%s", dir, run);
    snprintf(manifest, 4500, "%s/manifest.mwr", run_dir);
    return access(manifest, R_OK) == 0;
}

static int cmd_ledger(int argc, char **argv) {
    if (argc != 1) return 2;
    char dir[4096], path[4200];
    if (!wb_dir_of(argv[0], dir, sizeof dir)) return 1;
    snprintf(path, sizeof path, "%s/ledger.tsv", dir);
    FILE *f = fopen(path, "r");
    if (!f) { printf("no runs yet\n"); return 0; }
    /* Newest first, with the first line of each verdict. */
    char *lines[4096];
    size_t n = 0;
    char line[2048];
    bool header = true;
    while (n < 4096 && fgets(line, sizeof line, f)) {
        if (header) { header = false; continue; }
        lines[n++] = strdup(line);
    }
    fclose(f);
    printf("%-30s %-6s %-20s %-16s %-12s %8s  %s\n", "id", "kind", "label", "mix fnv64", "listening", "wall s", "verdict");
    for (size_t i = n; i-- > 0;) {
        char *field[7] = { 0 }, *save = nullptr;
        unsigned k = 0;
        for (char *t = strtok_r(lines[i], "\t\n", &save); t && k < 7; t = strtok_r(nullptr, "\t\n", &save)) field[k++] = t;
        if (k < 6) { free(lines[i]); continue; }
        char verdict_path[4600], verdict[200] = "";
        snprintf(verdict_path, sizeof verdict_path, "%s/runs/%s/verdict.txt", dir, field[0]);
        FILE *v = fopen(verdict_path, "r");
        if (v) {
            if (fgets(verdict, sizeof verdict, v)) verdict[strcspn(verdict, "\n")] = 0;
            fclose(v);
        }
        printf("%-30s %-6s %-20s %-16s %-12.12s %8s  %s\n", field[0], field[1], field[2], field[3], field[4],
               field[5], verdict);
        free(lines[i]);
    }
    return 0;
}

static int cmd_verdict(int argc, char **argv) {
    if (argc != 3) return 2;
    char dir[4096], run_dir[4400], manifest[4500], path[4600];
    if (!run_paths(argv[0], argv[1], dir, run_dir, manifest)) { fprintf(stderr, "no run %s\n", argv[1]); return 1; }
    snprintf(path, sizeof path, "%s/verdict.txt", run_dir);
    char text[4200];
    snprintf(text, sizeof text, "%s\n", argv[2]);
    if (!wb_write_file_atomic(path, text, strlen(text))) return 1;
    char root[4096];
    if (wb_root_of(argv[0], root, sizeof root)) {
        bool committed;
        wb_error e = { 0 };
        char message[200];
        snprintf(message, sizeof message, "verdict %s", argv[1]);
        wb_root_commit(root, message, &committed, &e);
    }
    printf("%s: verdict saved\n", path);
    return 0;
}

/* Object-level session diff: objects matched by kind and id (or position). */
static void session_diff(const wb_session *a, const wb_session *b) {
    unsigned changes = 0;
    for (unsigned k = 0; k < WB_KIND_COUNT; k++) {
        wb_kind kind = (wb_kind)k;
        unsigned na = wb_kind_count(a, kind), nb = wb_kind_count(b, kind), n = na > nb ? na : nb;
        for (unsigned i = 0; i < n; i++) {
            wb_buf x = { 0 }, y = { 0 };
            if (i < na) wb_write_object(a, kind, i, &x);
            if (i < nb) wb_write_object(b, kind, i, &y);
            if (x.length != y.length || (x.length && memcmp(x.data, y.data, x.length))) {
                changes++;
                printf("  [%s] #%u%s\n", wb_kind_name(kind), i + 1, i >= na ? " added" : i >= nb ? " removed" : "");
                /* Line diff of the two blocks. */
                for (int side = 0; side < 2; side++) {
                    wb_buf *me = side ? &y : &x, *other = side ? &x : &y;
                    for (char *line = me->data; line && *line;) {
                        char *end = strchr(line, '\n');
                        size_t length = end ? (size_t)(end - line) : strlen(line);
                        bool found = false;
                        for (char *o = other->data; o && *o && !found;) {
                            char *oe = strchr(o, '\n');
                            size_t ol = oe ? (size_t)(oe - o) : strlen(o);
                            found = ol == length && !memcmp(o, line, length);
                            o = oe ? oe + 1 : nullptr;
                        }
                        if (!found && line[0] != '[') printf("    %c %.*s\n", side ? '+' : '-', (int)length, line);
                        line = end ? end + 1 : nullptr;
                    }
                }
            }
            wb_buf_free(&x);
            wb_buf_free(&y);
        }
    }
    if (!changes) printf("  sessions identical\n");
}

static int cmd_diff(int argc, char **argv) {
    if (argc != 3) return 2;
    char dir[4096], ra[4400], rb[4400], ma[4500], mb[4500];
    if (!run_paths(argv[0], argv[1], dir, ra, ma) || !run_paths(argv[0], argv[2], dir, rb, mb)) {
        fprintf(stderr, "unknown run\n");
        return 1;
    }
    wb_session *a = malloc(sizeof *a), *b = malloc(sizeof *b);
    wb_error e;
    char pa[4600], pb[4600];
    snprintf(pa, sizeof pa, "%s/session.mws", ra);
    snprintf(pb, sizeof pb, "%s/session.mws", rb);
    int rc = 1;
    if (a && b && wb_load(pa, a, &e) && wb_load(pb, b, &e)) {
        printf("session %s -> %s\n", argv[1], argv[2]);
        session_diff(a, b);
        printf("outputs\n");
        for (unsigned st = 0; st <= b->stem_count; st++) {
            const char *name = st == b->stem_count ? "mix" : b->stem[st].id;
            char key[80], fa[40] = "", fb[40] = "", la[40] = "", lb[40] = "";
            snprintf(key, sizeof key, "out.%s.fnv64", name);
            wb_manifest_get(ma, key, fa, sizeof fa);
            wb_manifest_get(mb, key, fb, sizeof fb);
            snprintf(key, sizeof key, "out.%s.rms_dbfs", name);
            wb_manifest_get(ma, key, la, sizeof la);
            wb_manifest_get(mb, key, lb, sizeof lb);
            printf("  %-8s %s  %s -> %s  RMS %+.2f dB\n", name, !strcmp(fa, fb) ? "equal    " : "different",
                   fa[0] ? fa : "-", fb[0] ? fb : "-", la[0] && lb[0] ? atof(lb) - atof(la) : 0.0);
        }
        printf("sections (mix RMS)\n");
        for (unsigned i = 0; i < b->form.section_count; i++) {
            char key[80], sa[400] = "", sb[400] = "";
            snprintf(key, sizeof key, "section.%s", b->form.section[i].name);
            if (!wb_manifest_get(ma, key, sa, sizeof sa) || !wb_manifest_get(mb, key, sb, sizeof sb)) continue;
            const char *xa = strstr(sa, " mix "), *xb = strstr(sb, " mix ");
            if (xa && xb) printf("  %-10s %+.2f dB\n", b->form.section[i].name, atof(xb + 5) - atof(xa + 5));
        }
        rc = 0;
    } else fprintf(stderr, "%s\n", e.message);
    free(a);
    free(b);
    return rc;
}

static void progress(void *user, uint64_t done, uint64_t total);

static int cmd_reproduce(int argc, char **argv) {
    if (argc != 2) return 2;
    char dir[4096], run_dir[4400], manifest[4500], snapshot[4600], score_path[4600] = "";
    if (!run_paths(argv[0], argv[1], dir, run_dir, manifest)) { fprintf(stderr, "no run %s\n", argv[1]); return 1; }
    snprintf(snapshot, sizeof snapshot, "%s/session.mws", run_dir);
    wb_manifest_get(manifest, "score.path", score_path, sizeof score_path);
    wb_session *s = malloc(sizeof *s);
    wb_render_report *report = calloc(1, sizeof *report);
    wb_score score = { 0 };
    wb_error e = { 0 };
    int rc = 1;
    if (!s || !report || !wb_load(snapshot, s, &e) || !wb_score_load_midi(s, score_path, &score, &e)) {
        fprintf(stderr, "%s: %s\n", argv[1], e.message);
        goto done;
    }
    char want[65] = "";
    wb_manifest_get(manifest, "score.sha256", want, sizeof want);
    if (strcmp(want, score.sha256)) { fprintf(stderr, "%s: score %s changed since the run\n", argv[1], score_path); goto done; }
    wb_render_options options = { .progress = progress, .user = s };
    char frames[32] = "";
    wb_manifest_get(manifest, "frames", frames, sizeof frames);
    options.frames = strtoull(frames, nullptr, 10);
    printf("%s: reproducing %s (measure only)\n", argv[0], argv[1]);
    if (!wb_render(s, dir, &score, &options, report, &e)) { fprintf(stderr, "%s\n", e.message); goto done; }
    unsigned equal = 0;
    for (unsigned o = 0; o < report->outputs; o++) {
        const char *name = o + 1 == report->outputs ? "mix" : s->stem[o].id;
        char key[80], logged[40] = "", now[40];
        snprintf(key, sizeof key, "out.%s.fnv64", name);
        wb_manifest_get(manifest, key, logged, sizeof logged);
        snprintf(now, sizeof now, "%016llx", (unsigned long long)report->level[o].hash);
        bool same = !strcmp(now, logged);
        equal += same;
        printf("  %-8s %s %s\n", name, now, same ? "identical" : "DIFFERENT");
    }
    printf("  %u/%u outputs identical\n", equal, report->outputs);
    rc = equal == report->outputs ? 0 : 1;
done:
    wb_score_free(&score);
    free(report);
    free(s);
    return rc;
}

static int cmd_export(int argc, char **argv) {
    if (argc != 2) return 2;
    char dir[4096], run_dir[4400], manifest[4500];
    if (!run_paths(argv[0], argv[1], dir, run_dir, manifest)) { fprintf(stderr, "no run %s\n", argv[1]); return 1; }
    FILE *f = fopen(manifest, "r");
    if (!f) return 1;
    char name[256] = "", label[128] = "", kind[16] = "", wall[32] = "";
    wb_manifest_get(manifest, "session.name", name, sizeof name);
    wb_manifest_get(manifest, "label", label, sizeof label);
    wb_manifest_get(manifest, "kind", kind, sizeof kind);
    wb_manifest_get(manifest, "wall_seconds", wall, sizeof wall);
    /* A renders.md-style block; printed, never written into the repo. */
    printf("### %s — %s (%s)\n\n```sh\nwb render %s --label %s\n```\n\n", name, argv[1], kind, argv[0], label);
    char line[2048];
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "out.", 4) && strstr(line, ".fnv64=")) {
            line[strcspn(line, "\n")] = 0;
            char *eq = strchr(line, '=');
            *eq = 0;
            printf("- %s FNV64 `%s`\n", line + 4, eq + 1);
        } else if (!strncmp(line, "listening.sha256=", 17) || !strncmp(line, "session.sha256=", 15)
                   || !strncmp(line, "score.sha256=", 13) || !strncmp(line, "binary.sha256=", 14)
                   || !strncmp(line, "git.describe=", 13)) {
            line[strcspn(line, "\n")] = 0;
            char *eq = strchr(line, '=');
            *eq = 0;
            printf("- %s `%s`\n", line, eq + 1);
        }
    }
    fclose(f);
    printf("- wall %s s\n", wall);
    return 0;
}

/* ---- W8: part cache ------------------------------------------------------- */

static int cmd_cache(int argc, char **argv) {
    if (argc < 1) return 2;
    wb_session *s = malloc(sizeof *s);
    wb_derived *d = calloc(1, sizeof *d);
    wb_render_report *report = calloc(1, sizeof *report);
    wb_score score = { 0 };
    wb_error e = { 0 };
    char dir[4096], built[4400];
    int rc = 1;
    if (!s || !d || !report || !wb_load(argv[0], s, &e) || !wb_dir_of(argv[0], dir, sizeof dir)
        || !wb_orch_build(s, dir, d, &e) || !wb_orch_write(d, s, dir, built, sizeof built, &e)
        || !wb_score_load_midi(s, built, &score, &e)) {
        fprintf(stderr, "%s: %s\n", argv[0], e.message);
        goto done;
    }
    char cache_dir[4200];
    snprintf(cache_dir, sizeof cache_dir, "%s/cache", dir);
    mkdir(cache_dir, 0755);
    catch_terminate();
    unsigned built_count = 0, valid = 0;
    for (unsigned p = 0; p < s->part_count; p++) {
        bool wanted = argc == 1;
        for (int i = 1; i < argc; i++) wanted = wanted || !strcmp(argv[i], s->part[p].id);
        if (!wanted) continue;
        char key[65], path[4400];
        if (!wb_cache_key(s, dir, d, p, key)) { fprintf(stderr, "%s: no key\n", s->part[p].id); goto done; }
        wb_cache_path(dir, key, path, sizeof path);
        if (wb_cache_valid(dir, key)) {
            printf("  %-12s cached  %.12s\n", s->part[p].id, key);
            valid++;
            continue;
        }
        wb_render_options options = { .solo_part = true, .only_part = p, .part_out = path,
                                      .cancel = &cancel_requested };
        if (!wb_render(s, dir, &score, &options, report, &e)) {
            fprintf(stderr, "%s: %s\n", s->part[p].id, e.message);
            goto done;
        }
        printf("  %-12s rendered %.12s  %.1f s wall\n", s->part[p].id, key, report->wall_seconds);
        fflush(stdout);
        built_count++;
    }
    printf("%s: %u part caches rendered, %u already valid\n", argv[0], built_count, valid);
    rc = 0;
done:
    wb_score_free(&score);
    if (d) wb_derived_free(d);
    free(d);
    free(report);
    free(s);
    return rc;
}

static void progress(void *user, uint64_t done, uint64_t total) {
    const wb_session *s = user;
    printf("  rendered %llu/%llu s\n", (unsigned long long)(done / s->head.rate),
           (unsigned long long)(total / s->head.rate));
    fflush(stdout);
}

/* Final renders hold a logind sleep inhibitor for their whole run: re-exec
 * under systemd-inhibit once, marked by WB_INHIBITED. */
static void inhibit_or_continue(int argc, char **argv) {
    if (getenv("WB_INHIBITED")) return;
    char **args = calloc((size_t)argc + 6, sizeof *args);
    if (!args) return;
    args[0] = "systemd-inhibit";
    args[1] = "--what=sleep:idle";
    args[2] = "--who=tonewheel91 Workbench";
    args[3] = "--why=final render";
    char self[4096];
    ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
    if (n <= 0) { free(args); return; }
    self[n] = 0;
    args[4] = self;
    for (int i = 1; i < argc; i++) args[4 + i] = argv[i];
    setenv("WB_INHIBITED", "1", 1);
    execvp(args[0], args);
    /* No systemd-inhibit: render without it; the manifest says so. */
    setenv("WB_INHIBITED", "0", 1);
    free(args);
}

static int cmd_render(int argc, char **argv, int full_argc, char **full_argv) {
    if (argc < 1) return 2;
    const char *path = argv[0], *midi = nullptr, *label = "render";
    uint64_t seconds = 0;
    bool scalar = false, measure = false, check_only = false, no_inhibit = false, no_commit = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--midi") && i + 1 < argc) midi = argv[++i];
        else if (!strcmp(argv[i], "--label") && i + 1 < argc) label = argv[++i];
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) {
            char *end;
            seconds = strtoull(argv[++i], &end, 10);
            if (*end || !seconds) return 2;
        } else if (!strcmp(argv[i], "--scalar")) scalar = true;
        else if (!strcmp(argv[i], "--measure")) measure = true;
        else if (!strcmp(argv[i], "--check")) check_only = true;
        else if (!strcmp(argv[i], "--no-inhibit")) no_inhibit = true;
        else if (!strcmp(argv[i], "--no-commit")) no_commit = true;
        else return 2;
    }
    bool final = !seconds && !measure;
    if (final && !check_only && !no_inhibit) inhibit_or_continue(full_argc, full_argv);

    wb_session *s = malloc(sizeof *s);
    wb_error e = { 0 };
    wb_score score = { 0 };
    wb_render_report *report = calloc(1, sizeof *report);
    char dir[4096];
    int rc = 1;
    if (!s || !report) goto done;
    if (!wb_load(path, s, &e) || !wb_dir_of(path, dir, sizeof dir)) {
        report_error:
        fprintf(stderr, "%s: %s\n", path, e.message);
        goto done;
    }
    char built[4400];
    if (!midi) {
        /* Build the derived score from the session's rules and grooves. */
        wb_derived *d = calloc(1, sizeof *d);
        bool built_ok = d && wb_orch_build(s, dir, d, &e) && wb_orch_write(d, s, dir, built, sizeof built, &e);
        if (d) wb_derived_free(d);
        free(d);
        if (!built_ok) goto report_error;
        midi = built;
        printf("%s: derived score %s\n", path, built);
    }
    if (!wb_score_load_midi(s, midi, &score, &e)) goto report_error;
    if (check_only) {
        if (!wb_render_check(s, dir, &score, report, &e)) goto report_error;
        printf("%s: %zu events, %llu frames (%.1f s), %u parts\n", path, score.count,
               (unsigned long long)report->total_frames, (double)report->total_frames / s->head.rate,
               s->part_count);
        for (unsigned p = 0; p < s->part_count; p++)
            printf("  %-12s %4u notes, held peak %u, limit %u, unison %u\n", s->part[p].id,
                   report->part[p].notes, report->part[p].held_peak, s->part[p].limit, s->part[p].unison);
        rc = 0;
        goto done;
    }
    wb_run_info run = { .score_path = midi, .engine_scalar = scalar };
    const char *inhibited = getenv("WB_INHIBITED");
    run.inhibited = inhibited && !strcmp(inhibited, "1");
    if (!wb_session_sha256(s, run.session_sha256)
        || !wb_run_begin(&run, dir, final ? "final" : "probe", label, &e))
        goto report_error;
    /* Snapshot the session as rendered. */
    char snapshot[4400], prefix[4400];
    snprintf(snapshot, sizeof snapshot, "%s/session.mws", run.run_dir);
    if (!wb_save(snapshot, s, &e)) goto report_error;
    snprintf(prefix, sizeof prefix, "%s/%s", run.run_dir, run.id);
    wb_render_options options = { .prefix = measure ? nullptr : prefix,
                                  .frames = seconds * s->head.rate, .engine_scalar = scalar,
                                  .progress = progress, .user = s, .cancel = &cancel_requested };
    catch_terminate();
    printf("%s: run %s (%s)\n", path, run.id, run.kind);
    fflush(stdout);
    wb_host_info host;
    wb_host_probe(&host);
    if (strcmp(host.contamination, "none"))
        printf("  timing contamination: %s running\n", host.contamination);
    if (!measure) {
        /* Stems + mix (+ the listening copy) must fit before anything starts. */
        struct statvfs fs;
        uint64_t frames = seconds ? seconds * s->head.rate : wb_render_length(s, &score);
        uint64_t need = frames * 8u * (s->stem_count + 2u) + (16u << 20);
        if (statvfs(run.run_dir, &fs) == 0 && (uint64_t)fs.f_bavail * fs.f_frsize < need) {
            fprintf(stderr, "%s: %.1f GB free, the render needs %.1f GB\n", path,
                    (double)fs.f_bavail * fs.f_frsize / 1e9, (double)need / 1e9);
            goto done;
        }
    }
    if (!wb_render(s, dir, &score, &options, report, &e)) {
        fprintf(stderr, "%s: render failed: %s (incomplete WAVs removed)\n", path, e.message);
        goto done;
    }
    if (final) {
        char mix_path[4500], listening[4500];
        snprintf(mix_path, sizeof mix_path, "%s_mix.wav", prefix);
        snprintf(listening, sizeof listening, "%s/%s_listening.wav", run.run_dir, run.id);
        wb_audio mix;
        double gain = 0;
        if (wb_audio_load(mix_path, &mix, &e)
            && wb_listening_copy(&mix, listening, &gain, run.listening_sha256, &e)) {
            wb_audio copy;
            if (wb_audio_load(listening, &copy, &e)) {
                wb_measure m;
                wb_measure_audio(copy.sample, copy.frames, copy.rate, &m);
                run.listening = true;
                run.listening_gain_db = 20 * log10(gain);
                run.listening_peak_dbfs = m.peak_dbfs;
                run.listening_rms_dbfs = m.rms_dbfs;
                wb_audio_free(&copy);
            }
            wb_audio_free(&mix);
            printf("  listening copy %s gain %+.2f dB sha256 %s\n", listening, run.listening_gain_db,
                   run.listening_sha256);
        } else fprintf(stderr, "%s: listening copy: %s\n", path, e.message);
    }
    if (!wb_manifest_write(&run, s, dir, &score, report, &host, &e)
        || !wb_ledger_append(dir, &run, report, &e))
        goto report_error;
    for (unsigned o = 0; o < report->outputs; o++)
        printf("  %-8s FNV64=%016llx peak=%.9f\n", o + 1 == report->outputs ? "mix" : s->stem[o].id,
               (unsigned long long)report->level[o].hash, (double)report->level[o].peak);
    for (unsigned p = 0; p < s->part_count; p++) {
        const wb_part_report *x = &report->part[p];
        printf("  %-12s slots %u steals R/S/H=%llu/%llu/%llu sleeps=%llu FX clips=%llu\n", s->part[p].id,
               x->slot_peak, (unsigned long long)x->steals_released,
               (unsigned long long)x->steals_sustained, (unsigned long long)x->steals_held,
               (unsigned long long)x->sleeps, (unsigned long long)x->fx_clips);
    }
    if (report->overs) printf("  %llu samples above 0 dBFS (render_midi compat keeps them)\n",
                              (unsigned long long)report->overs);
    printf("  wall %.1f s, run dir %s\n", report->wall_seconds, run.run_dir);
    char root[4096];
    if (final && !no_commit && wb_root_of(path, root, sizeof root)) {
        char home[4096];
        char *real_home = realpath(wb_home(), nullptr);
        snprintf(home, sizeof home, "%s", real_home ? real_home : "");
        free(real_home);
        if (!strcmp(root, home)) {
            char message[256];
            bool committed = false;
            snprintf(message, sizeof message, "%s: run %s final", s->head.name, run.id);
            if (!wb_root_commit(root, message, &committed, &e))
                fprintf(stderr, "%s: %s\n", root, e.message);
            else if (committed) printf("  committed: %s\n", message);
        }
    }
    rc = 0;
done:
    wb_score_free(&score);
    free(report);
    free(s);
    return rc;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(stderr);
        return 2;
    }
    const char *command = argv[1];
    int n = argc - 2;
    char **args = argv + 2;
    int rc = 2;
    wb_error e = { 0 };
    if (!strcmp(command, "help") || !strcmp(command, "--help")) {
        usage(stdout);
        return 0;
    } else if (!strcmp(command, "root-init")) {
        if (n == 0) {
            rc = wb_root_init(wb_home(), &e) ? 0 : 1;
            if (rc) report(wb_home(), &e);
            else printf("%s: session root ready\n", wb_home());
        }
    } else if (!strcmp(command, "new")) {
        if (n == 1) {
            char *path = nullptr;
            rc = wb_session_create(wb_home(), args[0], &path, &e) ? 0 : 1;
            if (rc) report(args[0], &e);
            else printf("%s\n", path);
            free(path);
        }
    } else if (!strcmp(command, "check")) rc = cmd_check(n, args);
    else if (!strcmp(command, "fmt")) rc = cmd_fmt(n, args);
    else if (!strcmp(command, "get")) rc = cmd_get(n, args);
    else if (!strcmp(command, "set")) rc = cmd_edit(EDIT_SET, n, args);
    else if (!strcmp(command, "add")) rc = cmd_edit(EDIT_ADD, n, args);
    else if (!strcmp(command, "replace")) rc = cmd_edit(EDIT_REPLACE, n, args);
    else if (!strcmp(command, "rm")) rc = cmd_edit(EDIT_RM, n, args);
    else if (!strcmp(command, "undo")) rc = cmd_edit(EDIT_UNDO, n, args);
    else if (!strcmp(command, "redo")) rc = cmd_edit(EDIT_REDO, n, args);
    else if (!strcmp(command, "history")) rc = cmd_history(n, args);
    else if (!strcmp(command, "add-source")) rc = cmd_add_source(n, args);
    else if (!strcmp(command, "checkpoint")) rc = cmd_checkpoint(n, args);
    else if (!strcmp(command, "render")) rc = cmd_render(n, args, argc, argv);
    else if (!strcmp(command, "build-midi")) rc = cmd_build_midi(n, args);
    else if (!strcmp(command, "import")) rc = cmd_import(n, args);
    else if (!strcmp(command, "probe")) rc = cmd_probe(n, args);
    else if (!strcmp(command, "cache")) rc = cmd_cache(n, args);
    else if (!strcmp(command, "ledger")) rc = cmd_ledger(n, args);
    else if (!strcmp(command, "verdict")) rc = cmd_verdict(n, args);
    else if (!strcmp(command, "diff")) rc = cmd_diff(n, args);
    else if (!strcmp(command, "reproduce")) rc = cmd_reproduce(n, args);
    else if (!strcmp(command, "export")) rc = cmd_export(n, args);
    else if (!strcmp(command, "listening") && n == 2) {
        wb_audio mix;
        double gain;
        char sha[65];
        rc = wb_audio_load(args[0], &mix, &e) && wb_listening_copy(&mix, args[1], &gain, sha, &e) ? 0 : 1;
        if (rc) fprintf(stderr, "%s\n", e.message);
        else printf("%s: gain %+.2f dB sha256 %s\n", args[1], 20 * log10(gain), sha);
        wb_audio_free(&mix);
    }
    else if (!strcmp(command, "reimport")) rc = cmd_reimport(n, args);
    if (rc == 2) usage(stderr);
    return rc;
}
