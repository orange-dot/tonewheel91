#define _POSIX_C_SOURCE 200809L
#include "wb_import.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../../driver/ma_patch_file.h"
#include "../../src/mamutanalog.h"
#include "wb_format.h"
#include "wb_root.h"

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

void wb_import_options_default(wb_import_options *o) {
    *o = (wb_import_options){ .split = WB_SPLIT_TRACK_CHANNEL, .drums = 10 };
    static const unsigned DEFAULT[] = { 1, 7, 10, 11, 64, WB_CTL_BEND, WB_CTL_PRESSURE, WB_CTL_POLY };
    for (size_t i = 0; i < sizeof DEFAULT / sizeof *DEFAULT; i++) wb_ctlset_add(&o->controllers, DEFAULT[i]);
}

/* ---- engine and template suggestion ---------------------------------------- */

typedef struct {
    bool by_name;
    char match[32];                /* name substring (lowercase) */
    unsigned low, high;            /* program range */
    char engine[8];
    char template_name[WB_TEMPLATE_MAX + 1];
} map_row;

/* Built-in table (plan §5.2).  Names are checked before program families. */
static const map_row BUILTIN[] = {
    { true, "kick", 0, 0, "mamut", "kick" }, { true, "snare", 0, 0, "mamut", "snare" },
    { true, "tom", 0, 0, "mamut", "toms" }, { true, "hat", 0, 0, "mamut", "hats" },
    { true, "bass", 0, 0, "mamut", "bass" }, { true, "vocal", 0, 0, "mamut", "vocal" },
    { true, "voice", 0, 0, "mamut", "vocal" }, { true, "pad", 0, 0, "mamut", "pad" },
    { true, "string", 0, 0, "mamut", "pad" }, { true, "organ", 0, 0, "organ", "organ" },
    { true, "lead", 0, 0, "mamut", "lead" }, { true, "guitar", 0, 0, "mamut", "pluck" },
    { true, "arp", 0, 0, "mamut", "pluck" }, { true, "piano", 0, 0, "mamut", "keys" },
    { true, "keys", 0, 0, "mamut", "keys" }, { true, "drive", 0, 0, "mamut", "drive" },
    { true, "fx", 0, 0, "mamut", "wash" }, { true, "wash", 0, 0, "mamut", "wash" },
    { false, "", 4, 5, "ep73", "ep" },
    { false, "", 0, 7, "mamut", "keys" },
    { false, "", 16, 23, "organ", "organ" },
    { false, "", 29, 30, "mamut", "drive" },
    { false, "", 24, 31, "mamut", "pluck" },
    { false, "", 32, 39, "mamut", "bass" },
    { false, "", 40, 55, "mamut", "pad" },
    { false, "", 88, 95, "mamut", "pad" },
    { false, "", 80, 87, "mamut", "lead" },
    { false, "", 96, 103, "mamut", "wash" },
    { false, "", 120, 127, "mamut", "wash" },
};

typedef struct {
    unsigned count;
    map_row row[128];
} import_map;

static bool load_map(const char *path, import_map *map, wb_error *error) {
    map->count = 0;
    if (!path) return true;
    FILE *f = fopen(path, "r");
    if (!f) return errno == ENOENT || fail(error, "cannot read %s", path);
    char line[256];
    unsigned number = 0;
    while (fgets(line, sizeof line, f)) {
        number++;
        char kind[16], match[40], engine[16], template_name[40];
        if (line[0] == '#' || line[strspn(line, " \t\r\n")] == 0) continue;
        if (sscanf(line, "%15s %39s %15s %39s", kind, match, engine, template_name) != 4
            || map->count == sizeof map->row / sizeof *map->row
            || (strcmp(engine, "mamut") && strcmp(engine, "organ") && strcmp(engine, "ep73"))
            || !wb_id_valid(template_name)) {
            fclose(f);
            return fail(error, "%s:%u: expected 'program LOW-HIGH|name WORD  mamut|organ|ep73  TEMPLATE'",
                        path, number);
        }
        map_row *r = &map->row[map->count];
        *r = (map_row){ 0 };
        if (!strcmp(kind, "name")) {
            r->by_name = true;
            for (size_t i = 0; match[i] && i < sizeof r->match - 1; i++) r->match[i] = (char)tolower((unsigned char)match[i]);
        } else if (!strcmp(kind, "program") && sscanf(match, "%u-%u", &r->low, &r->high) >= 1) {
            if (!strchr(match, '-')) r->high = r->low;
        } else {
            fclose(f);
            return fail(error, "%s:%u: unknown row kind '%s'", path, number, kind);
        }
        snprintf(r->engine, sizeof r->engine, "%.7s", engine);
        snprintf(r->template_name, sizeof r->template_name, "%.31s", template_name);
        map->count++;
    }
    fclose(f);
    return true;
}

static void suggest(const import_map *map, const char *name, int program, bool drums,
                    const char *drum_group, wb_import_part *p) {
    if (drums) {
        snprintf(p->engine, sizeof p->engine, "mamut");
        snprintf(p->template_name, sizeof p->template_name, "%s", drum_group);
        return;
    }
    char lower[WB_SMF_NAME_MAX + 1];
    size_t n = 0;
    for (; name[n] && n < sizeof lower - 1; n++) lower[n] = (char)tolower((unsigned char)name[n]);
    lower[n] = 0;
    for (unsigned pass = 0; pass < 2; pass++) {
        const map_row *rows = pass ? BUILTIN : map->row;
        size_t count = pass ? sizeof BUILTIN / sizeof *BUILTIN : map->count;
        for (unsigned by_name = 1; by_name <= 1; by_name--) {
            for (size_t i = 0; i < count; i++) {
                const map_row *r = &rows[i];
                if (r->by_name != (bool)by_name) continue;
                bool hit = by_name ? r->match[0] && strstr(lower, r->match)
                                   : program >= 0 && (unsigned)program >= r->low && (unsigned)program <= r->high;
                if (hit) {
                    snprintf(p->engine, sizeof p->engine, "%s", r->engine);
                    snprintf(p->template_name, sizeof p->template_name, "%s", r->template_name);
                    return;
                }
            }
            if (!by_name) break;
        }
    }
    snprintf(p->engine, sizeof p->engine, "mamut");
    snprintf(p->template_name, sizeof p->template_name, "tepih");
}

static const char *stem_for(const char *template_name) {
    static const struct { const char *t, *stem; } STEMS[] = {
        { "bass", "bass" }, { "pad", "pads" }, { "wash", "pads" }, { "vocal", "vocal" },
        { "lead", "lead" }, { "drive", "lead" }, { "kick", "rhythm" }, { "snare", "rhythm" },
        { "stick", "rhythm" }, { "toms", "rhythm" }, { "hats", "rhythm" }, { "cymbals", "rhythm" },
        { "perc", "rhythm" },
    };
    for (size_t i = 0; i < sizeof STEMS / sizeof *STEMS; i++)
        if (!strcmp(template_name, STEMS[i].t)) return STEMS[i].stem;
    return "tonal";
}

/* ---- streams ---------------------------------------------------------------- */

/* GM drum note groups (plan §5.2). */
static const struct { const char *name, *template_name; const char *notes; } DRUM_GROUPS[] = {
    { "kick", "kick", "35-36" }, { "snare", "snare", "38,40" }, { "stick", "stick", "37" },
    { "toms", "toms", "41,43,45,47-48,50" }, { "hats", "hats", "42,44,46" },
    { "crash", "cymbals", "49,57" }, { "ride", "cymbals", "51,53,59" },
};

static void noteset_from(const char *text, wb_noteset *set) {
    *set = (wb_noteset){ 0 };
    const char *p = text;
    while (*p) {
        char *end;
        long a = strtol(p, &end, 10), b = a;
        if (*end == '-') b = strtol(end + 1, &end, 10);
        for (long n = a; n <= b; n++) wb_noteset_add(set, (unsigned)n);
        p = *end == ',' ? end + 1 : end;
    }
}

static void slug(const char *text, char *out, size_t size) {
    size_t n = 0;
    bool dash = false;
    for (const char *c = text; *c && n + 1 < size; c++) {
        unsigned char x = (unsigned char)*c;
        if (isalnum(x)) {
            if (dash && n) out[n++] = '-';
            if (n + 1 < size) out[n++] = (char)tolower(x);
            dash = false;
        } else dash = true;
    }
    out[n] = 0;
}

static void unique_id(wb_import_plan *plan, const char *base, wb_id out) {
    char stem[WB_ID_MAX - 3];
    snprintf(stem, sizeof stem, "%s", base[0] ? base : "part");
    snprintf(out, WB_ID_MAX + 1, "%s", stem);
    for (unsigned k = 2;; k++) {
        bool taken = false;
        for (unsigned i = 0; i < plan->part_count && !taken; i++) taken = !strcmp(plan->part[i].id, out);
        if (!taken) return;
        snprintf(out, WB_ID_MAX + 1, "%s-%u", stem, k);
    }
}

typedef struct { uint64_t tick; int delta; } edge;

static int compare_edge(const void *a, const void *b) {
    const edge *x = a, *y = b;
    if (x->tick != y->tick) return x->tick < y->tick ? -1 : 1;
    return x->delta - y->delta;
}

static bool skyline_top(const wb_smf_notes *n, size_t i) {
    for (size_t j = 0; j < n->count; j++) {
        if (j == i || n->note[j].channel != n->note[i].channel) continue;
        bool sounding = n->note[j].start <= n->note[i].start && n->note[i].start < n->note[j].end;
        if (sounding && n->note[j].note > n->note[i].note) return false;
    }
    return true;
}

/* Does note i of track t belong to the rule's stream?  Mirrors the
 * orchestration's own filters so the statistics match what it builds. */
static bool rule_takes(const wb_rule *r, unsigned t, const wb_smf_notes *n, size_t i) {
    const wb_smf_note *x = &n->note[i];
    if (r->track >= 0 && (unsigned)r->track != t) return false;
    if (r->channel >= 0 && x->channel != (unsigned)(r->channel - 1)) return false;
    if (r->program >= 0 && x->program != (unsigned)r->program) return false;
    if (r->has_notes && !wb_noteset_has(&r->notes, x->note)) return false;
    if (r->voice != WB_VOICE_ALL) {
        bool top = skyline_top(n, i);
        if ((r->voice == WB_VOICE_TOP) != top) return false;
    }
    return true;
}

static void part_stats(const wb_smf *smf, wb_import_part *p, unsigned tpb) {
    p->low = 127; p->high = 0; p->velocity_min = 127; p->velocity_max = 0;
    p->first_bar = INFINITY; p->last_bar = 0;
    size_t cap = 0, count = 0;
    edge *edges = nullptr;
    for (unsigned t = 0; t < smf->track_count; t++) {
        wb_smf_notes n;
        if (!wb_smf_pair_notes(smf, t, &n)) continue;
        for (size_t i = 0; i < n.count; i++) {
            bool taken = false;
            for (unsigned r = 0; r < p->rule_count && !taken; r++) taken = rule_takes(&p->rule[r], t, &n, i);
            if (!taken) continue;
            const wb_smf_note *x = &n.note[i];
            p->notes++;
            if (x->note < p->low) p->low = x->note;
            if (x->note > p->high) p->high = x->note;
            if (x->velocity < p->velocity_min) p->velocity_min = x->velocity;
            if (x->velocity > p->velocity_max) p->velocity_max = x->velocity;
            double a = (double)x->start / tpb, b = (double)x->end / tpb;
            if (a < p->first_bar) p->first_bar = a;
            if (b > p->last_bar) p->last_bar = b;
            if (count + 2 > cap) {
                cap = cap ? 2 * cap : 256;
                edge *grown = realloc(edges, cap * sizeof *edges);
                if (!grown) break;
                edges = grown;
            }
            edges[count++] = (edge){ x->start, 1 };
            edges[count++] = (edge){ x->end, -1 };
        }
        wb_smf_notes_free(&n);
    }
    if (edges) {
        qsort(edges, count, sizeof *edges, compare_edge);
        int active = 0;
        for (size_t i = 0; i < count; i++) {
            active += edges[i].delta;
            if (active > (int)p->held_peak) p->held_peak = (unsigned)active;
        }
    }
    free(edges);
    if (!p->notes) p->first_bar = 0;
}

static bool add_part(wb_import_plan *plan, const char *base, wb_rule rule, wb_error *error) {
    if (plan->part_count == WB_IMPORT_PARTS)
        return fail(error, "more than %d proposed parts; choose a coarser split", WB_IMPORT_PARTS);
    wb_import_part *p = &plan->part[plan->part_count];
    *p = (wb_import_part){ 0 };
    unique_id(plan, base, p->id);
    snprintf(rule.part, sizeof rule.part, "%s", p->id);
    p->rule[p->rule_count++] = rule;
    plan->part_count++;
    return true;
}

static void stream_rule(wb_rule *r, int track, const char *name, int channel, int program) {
    wb_rule_default(r);
    r->track = track;
    if (name && name[0]) snprintf(r->track_name, sizeof r->track_name, "%.*s", WB_TEXT_MAX, name);
    r->channel = channel;
    r->program = program;
}

bool wb_import_plan_build(const wb_smf *smf, const wb_import_options *o, wb_import_plan *plan,
                          wb_error *error) {
    memset(plan, 0, sizeof *plan);
    import_map map;
    if (!load_map(o->map_path, &map, error)) return false;
    unsigned tpb = 4 * smf->division;
    wb_smf_meter meter;
    if (wb_smf_meters(smf, &meter, 1) && meter.tick == 0) {
        unsigned d = 1u << meter.denominator_log2;
        if (d && meter.numerator) tpb = meter.numerator * 4 * smf->division / d;
    }
    snprintf(plan->title, sizeof plan->title, "%s", smf->track[0].name);
    plan->running_status = smf->running_status;
    plan->zero_velocity_offs = smf->zero_velocity_offs;

    /* Streams: (track, channel) with their programs and note counts. */
    unsigned counts[WB_SMF_TRACKS][16] = { 0 };
    unsigned stream_programs[WB_SMF_TRACKS][16][4];
    unsigned stream_program_count[WB_SMF_TRACKS][16] = { 0 };
    bool channel_used[16] = { 0 };
    for (unsigned t = 0; t < smf->track_count; t++) {
        wb_smf_notes n;
        if (!wb_smf_pair_notes(smf, t, &n)) return fail(error, "out of memory");
        plan->unmatched_offs += n.unmatched_offs;
        plan->stuck_notes += n.stuck;
        for (size_t i = 0; i < n.count; i++) {
            const wb_smf_note *x = &n.note[i];
            counts[t][x->channel]++;
            channel_used[x->channel] = true;
            unsigned *k = &stream_program_count[t][x->channel];
            bool seen = false;
            for (unsigned j = 0; j < *k; j++) seen = seen || stream_programs[t][x->channel][j] == x->program;
            if (!seen && *k < 4) stream_programs[t][x->channel][(*k)++] = x->program;
        }
        bool any = false;
        for (unsigned c = 0; c < 16; c++) any = any || counts[t][c];
        if (!any && t) plan->empty_streams++;
        wb_smf_notes_free(&n);
    }

    if (o->split == WB_SPLIT_MANUAL) {
        for (unsigned a = 0; a < o->assign_count; a++) {
            const wb_assign *as = &o->assign[a];
            if (as->track < 0 || (unsigned)as->track >= smf->track_count)
                return fail(error, "assignment to '%s': no track %d", as->part, as->track);
            wb_rule r;
            stream_rule(&r, as->track, smf->track[as->track].name, as->channel, -1);
            int found = -1;
            for (unsigned i = 0; i < plan->part_count; i++)
                if (!strcmp(plan->part[i].id, as->part)) found = (int)i;
            if (found >= 0) {
                wb_import_part *p = &plan->part[found];
                if (p->rule_count == sizeof p->rule / sizeof *p->rule)
                    return fail(error, "part '%s': too many streams", as->part);
                snprintf(r.part, sizeof r.part, "%s", p->id);
                p->rule[p->rule_count++] = r;
            } else if (!add_part(plan, as->part, r, error)) return false;
        }
    } else if (o->split == WB_SPLIT_CHANNEL) {
        for (unsigned c = 0; c < 16; c++) {
            if (!channel_used[c]) continue;
            char base[40];
            if (o->drums && (int)c + 1 == o->drums) continue;
            snprintf(base, sizeof base, "ch%u", c + 1);
            wb_rule r;
            stream_rule(&r, -1, nullptr, (int)c + 1, -1);
            if (!add_part(plan, base, r, error)) return false;
        }
    } else {
        for (unsigned t = 0; t < smf->track_count; t++) {
            const char *name = smf->track[t].name;
            char base[40];
            slug(name, base, sizeof base);
            if (!base[0]) snprintf(base, sizeof base, "track%u", t);
            /* Pitch split of this track, if any. */
            const wb_pitch_split *ps = nullptr;
            for (unsigned i = 0; i < o->pitch_split_count; i++)
                if (o->pitch_split[i].track == (int)t) ps = &o->pitch_split[i];
            bool track_only = o->split == WB_SPLIT_TRACK && !ps;
            bool any = false;
            for (unsigned c = 0; c < 16; c++) any = any || counts[t][c];
            if (!any) continue;
            if (track_only) {
                bool drums_only = o->drums > 0;
                for (unsigned c = 0; c < 16; c++)
                    if (counts[t][c] && (int)c + 1 != o->drums) drums_only = false;
                if (!drums_only) {
                    wb_rule r;
                    stream_rule(&r, (int)t, name, -1, -1);
                    if (o->drums > 0) {
                        /* Keep drum-channel notes for the kit parts below. */
                        bool mixed = counts[t][o->drums - 1] != 0;
                        if (mixed) {
                            for (unsigned c = 0; c < 16; c++) {
                                if (!counts[t][c] || (int)c + 1 == o->drums) continue;
                                wb_rule rc;
                                stream_rule(&rc, (int)t, name, (int)c + 1, -1);
                                char b[48];
                                snprintf(b, sizeof b, "%s-ch%u", base, c + 1);
                                if (!add_part(plan, b, rc, error)) return false;
                            }
                        } else if (!add_part(plan, base, r, error)) return false;
                    } else if (!add_part(plan, base, r, error)) return false;
                }
            }
            for (unsigned c = 0; c < 16; c++) {
                if (!counts[t][c]) continue;
                bool drum = o->drums > 0 && (int)c + 1 == o->drums;
                if (track_only && !drum) continue;
                if (drum) {
                    for (size_t g = 0; g <= sizeof DRUM_GROUPS / sizeof *DRUM_GROUPS; g++) {
                        wb_rule r;
                        stream_rule(&r, (int)t, name, (int)c + 1, -1);
                        r.has_notes = true;
                        if (g < sizeof DRUM_GROUPS / sizeof *DRUM_GROUPS) noteset_from(DRUM_GROUPS[g].notes, &r.notes);
                        else {
                            /* "other": every note no group claims. */
                            wb_noteset all = { 0 };
                            for (size_t k = 0; k < sizeof DRUM_GROUPS / sizeof *DRUM_GROUPS; k++) {
                                wb_noteset x;
                                noteset_from(DRUM_GROUPS[k].notes, &x);
                                all.bits[0] |= x.bits[0];
                                all.bits[1] |= x.bits[1];
                            }
                            r.notes = (wb_noteset){ { ~all.bits[0], ~all.bits[1] } };
                        }
                        wb_import_part probe = { .rule_count = 1, .rule = { r } };
                        part_stats(smf, &probe, tpb);
                        if (!probe.notes) continue;
                        const char *group = g < sizeof DRUM_GROUPS / sizeof *DRUM_GROUPS ? DRUM_GROUPS[g].name : "perc";
                        if (!add_part(plan, group, r, error)) return false;
                    }
                    continue;
                }
                char b[48];
                if (o->split == WB_SPLIT_TRACK_CHANNEL || o->split == WB_SPLIT_PROGRAM || ps) {
                    unsigned channels = 0;
                    for (unsigned k = 0; k < 16; k++) channels += counts[t][k] != 0;
                    if (channels > 1) snprintf(b, sizeof b, "%s-ch%u", base, c + 1);
                    else snprintf(b, sizeof b, "%s", base);
                } else snprintf(b, sizeof b, "%s", base);
                unsigned program_variants = o->split == WB_SPLIT_PROGRAM || o->program_split
                                          ? stream_program_count[t][c] : 1;
                for (unsigned pv = 0; pv < (program_variants ? program_variants : 1); pv++) {
                    int program = program_variants > 1 ? (int)stream_programs[t][c][pv] : -1;
                    char pb[72];
                    if (program >= 0) snprintf(pb, sizeof pb, "%s-p%d", b, program);
                    else snprintf(pb, sizeof pb, "%s", b);
                    if (ps) {
                        for (unsigned half = 0; half < 2; half++) {
                            wb_rule r;
                            stream_rule(&r, (int)t, name, (int)c + 1, program);
                            char hb[88];
                            if (ps->note < 0) {
                                r.voice = half ? WB_VOICE_REST : WB_VOICE_TOP;
                                snprintf(hb, sizeof hb, "%s-%s", pb, half ? "rest" : "top");
                            } else {
                                r.has_notes = true;
                                for (unsigned k = half ? (unsigned)ps->note : 0; k < (half ? 128u : (unsigned)ps->note); k++)
                                    wb_noteset_add(&r.notes, k);
                                snprintf(hb, sizeof hb, "%s-%s", pb, half ? "high" : "low");
                            }
                            if (!add_part(plan, hb, r, error)) return false;
                        }
                    } else {
                        wb_rule r;
                        stream_rule(&r, (int)t, name, (int)c + 1, program);
                        if (!add_part(plan, pb, r, error)) return false;
                    }
                }
            }
        }
    }

    /* Statistics, suggestions, controller forwarding. */
    unsigned last_tick_bar = 0;
    double first = INFINITY;
    for (unsigned i = 0; i < plan->part_count; i++) {
        wb_import_part *p = &plan->part[i];
        part_stats(smf, p, tpb);
        if (!p->notes) {
            /* A stream filter that kept nothing: drop it. */
            memmove(p, p + 1, (plan->part_count - i - 1) * sizeof *p);
            plan->part_count--;
            i--;
            continue;
        }
        const wb_rule *r = &p->rule[0];
        bool drum = r->channel > 0 && r->channel == o->drums;
        const char *group = "perc";
        for (size_t g = 0; drum && g < sizeof DRUM_GROUPS / sizeof *DRUM_GROUPS; g++)
            if (!strncmp(p->id, DRUM_GROUPS[g].name, strlen(DRUM_GROUPS[g].name)))
                group = DRUM_GROUPS[g].template_name;
        int program = r->program;
        if (program < 0) {
            for (unsigned c = 0; c < 16 && program < 0; c++)
                if ((r->channel < 0 || (int)c + 1 == r->channel) && r->track >= 0 && counts[r->track][c])
                    program = (int)stream_programs[r->track][c][0];
        }
        suggest(&map, r->track_name, program, drum, group, p);
        snprintf(p->stem, sizeof p->stem, "%s", stem_for(p->template_name));
        p->unison = !strcmp(p->template_name, "pad") || !strcmp(p->template_name, "lead")
                 || !strcmp(p->template_name, "drive") ? 2 : 1;
        /* Twice the held peak (room for release tails), in steps of four
         * voices, a multiple of unison, at most 64. */
        unsigned want = 2 * p->held_peak * p->unison;
        unsigned limit = (want + 3) / 4 * 4;
        if (limit < 4) limit = 4;
        if (limit > 64) limit = 64;
        while (limit % p->unison) limit--;
        p->limit = limit;
        p->compass_ok = strcmp(p->engine, "organ") || (p->low >= 36 && p->high <= 96);
        for (unsigned k = 0; k < p->rule_count; k++) p->rule[k].controllers = o->controllers;
        if (p->first_bar < first) first = p->first_bar;
        if ((unsigned)ceil(p->last_bar) > last_tick_bar) last_tick_bar = (unsigned)ceil(p->last_bar);
    }
    if (!plan->part_count) return fail(error, "the file has no notes");

    /* Form from the file: first note (or bar 0), last release, markers. */
    plan->start_bar = o->keep_leading_silence ? 0 : (int32_t)floor(first);
    plan->end_bar = (int32_t)last_tick_bar;
    if (plan->end_bar <= plan->start_bar) plan->end_bar = plan->start_bar + 1;
    struct { int32_t bar; char name[WB_ID_MAX + 1]; } marks[WB_SECTIONS];
    unsigned mark_count = 0;
    for (size_t i = 0; i < smf->event_count && mark_count < WB_SECTIONS; i++) {
        const wb_smf_event *e = &smf->event[i];
        if (e->status != 0xffu || e->d1 != 0x06) continue;
        char text[WB_SMF_NAME_MAX + 1], name[WB_ID_MAX + 1];
        size_t n = e->length < WB_SMF_NAME_MAX ? e->length : WB_SMF_NAME_MAX;
        memcpy(text, smf->data + e->offset, n);
        text[n] = 0;
        slug(text, name, sizeof name);
        int32_t bar = (int32_t)floor((double)e->tick / tpb);
        if (!name[0] || bar >= plan->end_bar) continue;
        if (bar < plan->start_bar) bar = plan->start_bar;
        if (mark_count && marks[mark_count - 1].bar == bar) mark_count--;
        marks[mark_count].bar = bar;
        snprintf(marks[mark_count++].name, WB_ID_MAX + 1, "%s", name);
    }
    for (unsigned i = 0; i < mark_count; i++) {
        wb_section *x = &plan->section[plan->section_count];
        x->start_bar = marks[i].bar;
        x->end_bar = i + 1 < mark_count ? marks[i + 1].bar : plan->end_bar;
        if (x->end_bar <= x->start_bar) continue;
        snprintf(x->name, sizeof x->name, "%s", marks[i].name);
        for (unsigned k = 2; ; k++) {
            bool dup = false;
            for (unsigned j = 0; j < plan->section_count && !dup; j++)
                dup = !strcmp(plan->section[j].name, x->name);
            if (!dup) break;
            snprintf(x->name, sizeof x->name, "%.24s-%u", marks[i].name, k);
        }
        plan->section_count++;
    }
    return true;
}

/* ---- apply ------------------------------------------------------------------ */

static bool factory(const char *template_name, ma_patch *patch) {
    static const struct { const char *name; const ma_patch *patch; } FACTORY[] = {
        { "tepih", &ma_patch_tepih }, { "lead", &ma_patch_lead }, { "dubina", &ma_patch_dubina },
        { "raster", &ma_patch_raster }, { "prizma", &ma_patch_prizma }, { "granica", &ma_patch_granica },
    };
    for (size_t i = 0; i < sizeof FACTORY / sizeof *FACTORY; i++)
        if (!strcmp(template_name, FACTORY[i].name)) {
            *patch = *FACTORY[i].patch;
            return true;
        }
    *patch = ma_patch_tepih;
    return false;
}

bool wb_import_apply(wb_session *s, const char *dir, const char *source_id, const char *midi_path,
                     const wb_import_plan *plan, const wb_import_options *o, const char *templates_dir,
                     wb_error *error) {
    wb_session *next = malloc(sizeof *next);
    if (!next) return fail(error, "out of memory");
    memcpy(next, s, sizeof *next);
    bool ok = wb_add_source(next, dir, source_id, midi_path, error);
    if (ok && next->import_count == WB_IMPORTS) ok = fail(error, "more than %d imports", WB_IMPORTS);
    if (ok) {
        wb_import *im = &next->import[next->import_count++];
        wb_import_default(im);
        snprintf(im->source, sizeof im->source, "%s", source_id);
        im->split = o->split;
        im->drums = o->drums;
        im->program_split = o->program_split;
        im->pitch_split_count = o->pitch_split_count;
        memcpy(im->pitch_split, o->pitch_split, sizeof im->pitch_split);
        im->controllers = o->controllers;
    }
    bool fresh_form = !next->form.section_count && next->form.end_bar <= next->form.start_bar + 1;
    if (ok && fresh_form) {
        next->form.start_bar = plan->start_bar;
        next->form.end_bar = plan->end_bar;
        next->form.section_count = plan->section_count;
        memcpy(next->form.section, plan->section, sizeof next->form.section);
        if (!next->head.title[0])
            snprintf(next->head.title, sizeof next->head.title, "%s", plan->title);
    }
    if (ok && next->part_count + plan->part_count > WB_PARTS)
        ok = fail(error, "%u parts would exceed %d; merge streams with a coarser split or --assign",
                  next->part_count + plan->part_count, WB_PARTS);
    char patches[4400];
    snprintf(patches, sizeof patches, "%s/patches", dir);
    if (ok && mkdir(patches, 0755) && errno != EEXIST) ok = fail(error, "cannot create %s", patches);
    for (unsigned i = 0; ok && i < plan->part_count; i++) {
        const wb_import_part *ip = &plan->part[i];
        if (wb_find_part(next, ip->id) >= 0) {
            ok = fail(error, "part '%s' already exists in the session", ip->id);
            break;
        }
        if (wb_find_stem(next, ip->stem) < 0) {
            if (next->stem_count == WB_STEMS) { ok = fail(error, "more than %d stems", WB_STEMS); break; }
            snprintf(next->stem[next->stem_count++].id, sizeof next->stem[0].id, "%s", ip->stem);
        }
        /* The patch: a session-local copy of the template. */
        char from[4400], to[4400], relative[WB_PATH_MAX + 1];
        snprintf(relative, sizeof relative, "patches/%s.mapatch", ip->id);
        snprintf(to, sizeof to, "%s/%s", dir, relative);
        snprintf(from, sizeof from, "%s/%s.mapatch", templates_dir ? templates_dir : "", ip->template_name);
        ma_patch_document doc;
        ma_patch_error pe = { 0 };
        if (!templates_dir || !ma_patch_load(from, &doc, &pe)) {
            doc = (ma_patch_document){ 0 };
            factory(ip->template_name, &doc.value);
        }
        snprintf(doc.name, sizeof doc.name, "%s", ip->id);
        if (!ma_patch_save(to, &doc, &pe)) { ok = fail(error, "cannot write %s", to); break; }

        wb_part *p = &next->part[next->part_count++];
        wb_part_default(p);
        snprintf(p->id, sizeof p->id, "%s", ip->id);
        snprintf(p->patch, sizeof p->patch, "%s", relative);
        snprintf(p->stem, sizeof p->stem, "%s", ip->stem);
        p->limit = ip->limit;
        p->unison = ip->unison;
        /* Organ and ep73 parts render from W9; until then the suggestion is
         * recorded in the import report and the part plays on Mamut. */
        p->engine = WB_PART_MAMUT;
        for (unsigned r = 0; r < ip->rule_count; r++) {
            if (next->rule_count == WB_RULES) { ok = fail(error, "more than %d rules", WB_RULES); break; }
            wb_rule rule = ip->rule[r];
            snprintf(rule.source, sizeof rule.source, "%s", source_id);
            next->rule[next->rule_count++] = rule;
        }
    }
    if (ok) ok = wb_session_validate(next, error);
    if (ok) memcpy(s, next, sizeof *s);
    free(next);
    return ok;
}
