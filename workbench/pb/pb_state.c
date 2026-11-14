#define _POSIX_C_SOURCE 200809L
#include "pb_state.h"

#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../driver/host_parse.h"
#include "../../driver/ma_patch_file.h"

pb_voice pb_voice_default(void) {
    return (pb_voice){ .limit = 8, .unison = 1, .seed = MA_POOL_DEFAULT_SEED, .lfo_rate_hz = 5,
                       .chorus_depth = .5f, .chorus_rate_hz = .6f, .reverb_size = .5f, .reverb_damping = .5f,
                       .gain = 16 };
}

static bool in(float x, float lo, float hi) { return x >= lo && x <= hi; }

bool pb_voice_valid(const pb_voice *v) {
    return v->limit >= 1 && v->limit <= MA_POOL_CAPACITY
        && (v->unison == 1 || v->unison == 2 || v->unison == 4 || v->unison == 8) && v->limit % v->unison == 0
        && in(v->detune_cents, 0, 100) && in(v->character, 0, 1) && in(v->glide_s, 0, 10)
        && in(v->lfo_depth, 0, 1) && in(v->lfo_rate_hz, 0, 20) && in(v->chorus_mix, 0, 1)
        && in(v->chorus_depth, 0, 1) && in(v->chorus_rate_hz, 0, 10) && in(v->reverb_mix, 0, 1)
        && in(v->reverb_size, 0, 1) && in(v->reverb_damping, 0, 1) && in(v->gain, 0, 64);
}

bool pb_voice_needs_pool(const pb_voice *a, const pb_voice *b) {
    return a->limit != b->limit || a->unison != b->unison || a->detune_cents != b->detune_cents || a->seed != b->seed;
}

void pb_bench_default(pb_bench *b) {
    *b = (pb_bench){ .probe = { 48, 100, false } };
    for (unsigned c = 0; c < PB_CHANNELS; c++) {
        b->channel[c].voice = pb_voice_default();
        b->channel[c].pattern = c ? pb_pattern_empty(48, 9600) : pb_builtin_pattern(PB_PAT_PULSE, 48, 9600);
    }
}

pb_sequence pb_bench_sequence(const pb_bench *b) {
    pb_sequence s;
    for (unsigned c = 0; c < PB_CHANNELS; c++) {
        s.channel[c] = b->channel[c].pattern;
        s.mute[c] = b->channel[c].mute;
    }
    return s;
}

void pb_bench_set_tempo(pb_bench *b, uint32_t bpm, uint8_t swing) {
    for (unsigned c = 0; c < PB_CHANNELS; c++) {
        b->channel[c].pattern.bpm = bpm;
        b->channel[c].pattern.swing = swing;
    }
}

/* ---- reading -------------------------------------------------------------- */

typedef enum { SEC_TOP, SEC_VOICE, SEC_PROBE, SEC_SEQUENCE, SEC_STEP, SEC_SNAPSHOT, SEC_CHANNEL, SEC_COUNT } section;
static const char *const SECTION_NAMES[SEC_COUNT] = { "", "[voice]", "[probe]", "[sequence]", "[step]", "[snapshot]",
                                                      "[channel]" };

typedef struct { const char *key; size_t offset; double lo, hi; bool integer; bool f32; } key_spec;
#define VF(k, m, lo, hi) { k, offsetof(pb_voice, m), lo, hi, false, true }
#define VU(k, m, lo, hi) { k, offsetof(pb_voice, m), lo, hi, true, false }
static const key_spec VOICE_KEYS[] = {
    VU("limit", limit, 1, 64), VU("unison", unison, 1, 8), VF("detune_cents", detune_cents, 0, 100),
    { "seed", offsetof(pb_voice, seed), 0, 0, true, false },
    VF("character", character, 0, 1), VF("glide_s", glide_s, 0, 10), VF("lfo_depth", lfo_depth, 0, 1),
    VF("lfo_rate_hz", lfo_rate_hz, 0, 20), VF("chorus_mix", chorus_mix, 0, 1),
    VF("chorus_depth", chorus_depth, 0, 1), VF("chorus_rate_hz", chorus_rate_hz, 0, 10),
    VF("reverb_mix", reverb_mix, 0, 1), VF("reverb_size", reverb_size, 0, 1),
    VF("reverb_damping", reverb_damping, 0, 1), VF("gain", gain, 0, 64),
};
#undef VF
#undef VU
enum { VOICE_KEY_COUNT = sizeof VOICE_KEYS / sizeof *VOICE_KEYS };
static const char *const PROBE_KEYS[] = { "note", "velocity", "pinned" };
/* Version 2 has the first two; steps and root moved into [channel]. */
static const char *const SEQ_KEYS[] = { "bpm", "swing", "steps", "root" };
static const char *const CHANNEL_KEYS[] = { "index", "patch", "mute", "steps", "root" };
static const char *const STEP_KEYS[] = { "index", "notes", "velocity", "gate", "tie", "octave" };

typedef struct {
    pb_bench *b;
    pb_error *error;
    unsigned line;
    unsigned version;              /* 1 or 2 */
    section sec;
    uint64_t seen;                 /* keys of the open section */
    unsigned sections_seen;        /* bit per singleton section */
    uint32_t bpm;                  /* [sequence]: one tempo and swing for all channels */
    uint8_t swing;
    /* the channel the [voice] and [step] sections belong to; version 1: 0 */
    unsigned channel;
    bool channel_open;             /* version 2: a [channel] came before */
    bool channel_seen[PB_CHANNELS], voice_seen[PB_CHANNELS];
    pb_channel staged;             /* the open [channel] */
    unsigned staged_index;
    bool steps_used[PB_SEQ_STEPS];
    /* the open [step] */
    pb_step step;
    unsigned step_index;
    int notes[PB_SEQ_SPAN];
    unsigned note_count;
    /* the open [snapshot] */
    unsigned slot;
    ma_patch snap;
} reader;

[[gnu::format(printf, 2, 3)]]
static bool fail(reader *r, const char *format, ...) {
    if (r->error) {
        r->error->line = r->line;
        va_list args;
        va_start(args, format);
        vsnprintf(r->error->message, sizeof r->error->message, format, args);
        va_end(args);
    }
    return false;
}

static int key_at(const char *const *keys, size_t count, const char *key) {
    for (size_t i = 0; i < count; i++) if (!strcmp(keys[i], key)) return (int)i;
    return -1;
}

static bool parse_u(reader *r, const char *v, uint64_t lo, uint64_t hi, uint64_t *out) {
    return host_parse_u64(v, lo, hi, out) || fail(r, "value '%s' outside %llu..%llu", v,
                                                  (unsigned long long)lo, (unsigned long long)hi);
}

static bool parse_i(reader *r, const char *v, int lo, int hi, int *out) {
    char *end = nullptr;
    errno = 0;
    long x = strtol(v, &end, 10);
    if (end == v || *end || errno || x < lo || x > hi) return fail(r, "value '%s' outside %d..%d", v, lo, hi);
    *out = (int)x;
    return true;
}

static uint64_t section_mask(const reader *r, section s) {
    size_t n = s == SEC_VOICE ? VOICE_KEY_COUNT : s == SEC_PROBE ? 3 : s == SEC_SEQUENCE ? (r->version == 1 ? 4 : 2)
             : s == SEC_STEP ? 6 : s == SEC_CHANNEL ? 5 : s == SEC_SNAPSHOT ? ma_patch_field_count() + 1 : 0;
    return n >= 64 ? UINT64_MAX : (UINT64_C(1) << n) - 1;
}

/* The [snapshot] blocks a file may leave out, whole: the oscillator-pitch
 * block (0), the triangle shapers (1) and the cores' supply (2), which
 * snapshots saved before them do not have. A missing block stays at zero,
 * its bypass. */
static uint64_t snapshot_optional(int block) {
    uint64_t mask = 0;
    ma_patch_field_info info;
    for (size_t j = 0; ma_patch_field_info_at(j, &info); j++) {
        bool supply = !strcmp(info.name, "vco.supply");
        bool in = block == 2 ? supply
                : block == 1 ? strstr(info.name, ".tri_") != nullptr
                : !strncmp(info.name, "vco.", 4) && !supply;
        if (in) mask |= UINT64_C(1) << (j + 1);
    }
    return mask;
}

static bool close_section(reader *r) {
    if (r->sec == SEC_TOP) return true;
    uint64_t full = section_mask(r, r->sec);
    bool complete = r->seen == full;
    for (int leave = 1; r->sec == SEC_SNAPSHOT && !complete && leave < 8; leave++) {
        uint64_t left = (leave & 1 ? snapshot_optional(0) : 0) | (leave & 2 ? snapshot_optional(1) : 0)
                      | (leave & 4 ? snapshot_optional(2) : 0);
        complete = r->seen == (full & ~left);
    }
    if (!complete) return fail(r, "%s is missing keys", SECTION_NAMES[r->sec]);
    pb_pattern *p = &r->b->channel[r->channel].pattern;
    if (r->sec == SEC_CHANNEL) {
        if (r->channel_seen[r->staged_index]) return fail(r, "duplicate [channel] %u", r->staged_index + 1);
        r->channel_seen[r->staged_index] = true;
        r->channel = r->staged_index;
        r->channel_open = true;
        pb_channel *ch = &r->b->channel[r->channel];
        memcpy(ch->patch, r->staged.patch, sizeof ch->patch);
        ch->mute = r->staged.mute;
        ch->pattern.steps = r->staged.pattern.steps;
        ch->pattern.root = r->staged.pattern.root;
        memset(r->steps_used, 0, sizeof r->steps_used);
    } else if (r->sec == SEC_STEP) {
        if (r->step_index >= p->steps) return fail(r, "[step] index %u beyond %u steps", r->step_index + 1, p->steps);
        if (r->steps_used[r->step_index]) return fail(r, "duplicate [step] %u", r->step_index + 1);
        r->steps_used[r->step_index] = true;
        for (unsigned i = 0; i < r->note_count; i++) {
            int row = r->notes[i] - (int)p->root + 12 - 12 * r->step.octave;
            if (row < 0 || row >= PB_SEQ_SPAN)
                return fail(r, "[step] %u: note %d outside the grid around root %u", r->step_index + 1, r->notes[i],
                            p->root);
            r->step.mask |= UINT32_C(1) << row;
        }
        p->step[r->step_index] = r->step;
    } else if (r->sec == SEC_SNAPSHOT) {
        if (r->b->snapshot_used[r->slot]) return fail(r, "duplicate [snapshot] %u", r->slot + 1);
        r->b->snapshot_used[r->slot] = true;
        r->b->snapshot[r->slot] = r->snap;
    } else if (r->sec == SEC_VOICE) {
        if (!pb_voice_valid(&r->b->channel[r->channel].voice))
            return fail(r, "[voice] limit must be a multiple of unison (1, 2, 4, 8)");
        r->voice_seen[r->channel] = true;
    }
    return true;
}

static bool open_section(reader *r, const char *name) {
    if (!close_section(r)) return false;
    section s = SEC_TOP;
    for (unsigned i = 1; i < SEC_COUNT; i++) if (!strcmp(name, SECTION_NAMES[i])) s = (section)i;
    if (s == SEC_TOP || (s == SEC_CHANNEL && r->version == 1)) return fail(r, "unknown section %s", name);
    bool many = s == SEC_STEP || s == SEC_SNAPSHOT || s == SEC_CHANNEL || (s == SEC_VOICE && r->version == 2);
    if (!many) {
        if (r->sections_seen & 1u << s) return fail(r, "duplicate section %s", name);
        r->sections_seen |= 1u << s;
    }
    if (r->version == 1) {
        if (s == SEC_STEP && !(r->sections_seen & 1u << SEC_SEQUENCE)) return fail(r, "[step] before [sequence]");
    } else if (s == SEC_STEP || s == SEC_VOICE) {
        if (!r->channel_open) return fail(r, "%s before [channel]", name);
        if (s == SEC_VOICE && r->voice_seen[r->channel]) return fail(r, "duplicate [voice] in [channel] %u", r->channel + 1);
    }
    r->staged = (pb_channel){ 0 };
    r->staged_index = 0;
    r->sec = s;
    r->seen = 0;
    r->step = (pb_step){ 0 };
    r->note_count = 0;
    r->snap = (ma_patch){ 0 };
    return true;
}

static bool mark(reader *r, int index, const char *key) {
    if (index < 0) return fail(r, "unknown key '%s' in %s", key, r->sec ? SECTION_NAMES[r->sec] : "header");
    uint64_t bit = UINT64_C(1) << index;
    if (r->seen & bit) return fail(r, "duplicate key '%s'", key);
    r->seen |= bit;
    return true;
}

static bool line_key(reader *r, const char *key, const char *v) {
    pb_bench *b = r->b;
    uint64_t u = 0;
    int i = 0;
    double d = 0;
    switch (r->sec) {
    case SEC_TOP:
        if (strcmp(key, r->version == 1 ? "patch" : "selected"))
            return fail(r, "unknown key '%s' before the first section", key);
        if (r->seen & 1u) return fail(r, "duplicate key '%s'", key);
        r->seen |= 1u;
        if (r->version == 1) {
            if (strlen(v) >= sizeof b->channel[0].patch) return fail(r, "patch path too long");
            snprintf(b->channel[0].patch, sizeof b->channel[0].patch, "%s", v);
        } else {
            if (!parse_u(r, v, 1, PB_CHANNELS, &u)) return false;
            b->selected = (uint8_t)(u - 1);
        }
        return true;
    case SEC_CHANNEL: {
        int k = key_at(CHANNEL_KEYS, 5, key);
        if (!mark(r, k, key)) return false;
        if (k == 1) {
            if (strlen(v) >= sizeof r->staged.patch) return fail(r, "patch path too long");
            snprintf(r->staged.patch, sizeof r->staged.patch, "%s", v);
            return true;
        }
        static const uint64_t LO[] = { 1, 0, 0, 1, 12 }, HI[] = { PB_CHANNELS, 0, 1, PB_SEQ_STEPS, 115 };
        if (!parse_u(r, v, LO[k], HI[k], &u)) return false;
        if (k == 0) r->staged_index = (unsigned)u - 1;
        else if (k == 2) r->staged.mute = u;
        else if (k == 3) r->staged.pattern.steps = (uint8_t)u;
        else r->staged.pattern.root = (uint8_t)u;
        return true;
    }
    case SEC_VOICE: {
        int k = -1;
        for (int j = 0; j < (int)VOICE_KEY_COUNT; j++) if (!strcmp(VOICE_KEYS[j].key, key)) k = j;
        if (!mark(r, k, key)) return false;
        const key_spec *s = &VOICE_KEYS[k];
        unsigned char *dst = (unsigned char *)&b->channel[r->channel].voice + s->offset;
        if (!strcmp(key, "seed")) {
            if (!parse_u(r, v, 0, UINT64_MAX, &u)) return false;
            memcpy(dst, &u, sizeof u);
        } else if (s->integer) {
            if (!parse_u(r, v, (uint64_t)s->lo, (uint64_t)s->hi, &u)) return false;
            *dst = (uint8_t)u;
        } else {
            if (!host_parse_double(v, s->lo, s->hi, &d)) return fail(r, "value '%s' outside %g..%g", v, s->lo, s->hi);
            float f = (float)d;
            memcpy(dst, &f, sizeof f);
        }
        return true;
    }
    case SEC_PROBE: {
        int k = key_at(PROBE_KEYS, 3, key);
        if (!mark(r, k, key)) return false;
        if (!parse_u(r, v, k == 1 ? 1 : 0, k == 2 ? 1 : 127, &u)) return false;
        if (k == 0) b->probe.note = (uint8_t)u;
        else if (k == 1) b->probe.velocity = (uint8_t)u;
        else b->probe.pinned = u;
        return true;
    }
    case SEC_SEQUENCE: {
        int k = key_at(SEQ_KEYS, r->version == 1 ? 4 : 2, key);
        if (!mark(r, k, key)) return false;
        static const uint64_t LO[] = { PB_SEQ_BPM_MIN, 0, 1, 12 },
                              HI[] = { PB_SEQ_BPM_MAX, PB_SEQ_SWING_MAX, PB_SEQ_STEPS, 115 };
        if (!parse_u(r, v, LO[k], HI[k], &u)) return false;
        pb_pattern *p = &b->channel[0].pattern;   /* steps and root: version 1 only */
        if (k == 0) r->bpm = (uint32_t)u;
        else if (k == 1) r->swing = (uint8_t)u;
        else if (k == 2) p->steps = (uint8_t)u;
        else p->root = (uint8_t)u;
        return true;
    }
    case SEC_STEP: {
        int k = key_at(STEP_KEYS, 6, key);
        if (!mark(r, k, key)) return false;
        if (k == 0) {
            if (!parse_u(r, v, 1, PB_SEQ_STEPS, &u)) return false;
            r->step_index = (unsigned)u - 1;
        } else if (k == 1) {
            const char *at = v;
            while (*at) {
                char item[8] = { 0 };
                size_t n = strcspn(at, ",");
                if (!n || n >= sizeof item || r->note_count == PB_SEQ_SPAN) return fail(r, "bad notes list '%s'", v);
                memcpy(item, at, n);
                if (!parse_i(r, item, 0, 127, &i)) return false;
                for (unsigned j = 0; j < r->note_count; j++)
                    if (r->notes[j] == i) return fail(r, "duplicate note %d", i);
                r->notes[r->note_count++] = i;
                at += n;
                if (*at == ',' && !*++at) return fail(r, "bad notes list '%s'", v);
            }
        } else if (k == 2) {
            if (!parse_u(r, v, 1, 127, &u)) return false;
            r->step.velocity = (uint8_t)u;
        } else if (k == 3) {
            if (!parse_u(r, v, 1, 100, &u)) return false;
            r->step.gate = (uint8_t)u;
        } else if (k == 4) {
            if (!parse_u(r, v, 0, 1, &u)) return false;
            r->step.tie = u;
        } else {
            if (!parse_i(r, v, -PB_SEQ_OCTAVE_MAX, PB_SEQ_OCTAVE_MAX, &i)) return false;
            r->step.octave = (int8_t)i;
        }
        return true;
    }
    case SEC_SNAPSHOT: {
        if (!strcmp(key, "slot")) {
            if (!mark(r, 0, key) || !parse_u(r, v, 1, PB_SNAPSHOTS, &u)) return false;
            r->slot = (unsigned)u - 1;
            return true;
        }
        int f = -1;
        ma_patch_field_info info;
        for (size_t j = 0; ma_patch_field_info_at(j, &info); j++) if (!strcmp(info.name, key)) f = (int)j;
        if (!mark(r, f < 0 ? -1 : f + 1, key)) return false;
        (void)ma_patch_field_info_at((size_t)f, &info);
        if (!host_parse_double(v, info.minimum, info.maximum, &d) || (info.integer && d != (int)d)
            || !ma_patch_field_set(&r->snap, (size_t)f, d))
            return fail(r, "value '%s' outside the domain of %s", v, key);
        return true;
    }
    case SEC_COUNT: break;
    }
    return false;
}

bool pb_bench_read(FILE *file, pb_bench *b, pb_error *error) {
    pb_bench_default(b);
    /* Only steps with notes are in the file. */
    b->channel[0].pattern = pb_pattern_empty(48, 9600);
    if (error) *error = (pb_error){ 0 };
    reader r = { .b = b, .error = error, .bpm = 9600 };
    char line[512];
    bool header = false;
    while (fgets(line, sizeof line, file)) {
        r.line++;
        size_t n = strlen(line);
        if (n && line[n - 1] == '\n') line[--n] = 0;
        else if (!feof(file)) return fail(&r, "line exceeds 511 bytes");
        if (n && line[n - 1] == '\r') line[--n] = 0;
        if (!n || line[0] == '#') continue;
        if (!header) {
            if (!strcmp(line, "patchbench=1")) r.version = 1;
            else if (!strcmp(line, "patchbench=2")) r.version = 2;
            else return fail(&r, "expected patchbench=1 or patchbench=2 first");
            header = true;
            continue;
        }
        if (line[0] == '[') {
            if (!open_section(&r, line)) return false;
            continue;
        }
        char *eq = strchr(line, '=');
        if (!eq || eq == line) return fail(&r, "expected key=value");
        *eq++ = 0;
        if (!line_key(&r, line, eq)) return false;
    }
    if (ferror(file)) return fail(&r, "read failed");
    if (!header) return fail(&r, "empty bench file");
    if (!close_section(&r)) return false;
    unsigned need = 1u << SEC_PROBE | 1u << SEC_SEQUENCE | (r.version == 1 ? 1u << SEC_VOICE : 0u);
    if ((r.sections_seen & need) != need)
        return fail(&r, r.version == 1 ? "missing [voice], [probe] or [sequence]" : "missing [probe] or [sequence]");
    for (unsigned c = 0; c < PB_CHANNELS; c++)
        if (r.channel_seen[c] && !r.voice_seen[c]) return fail(&r, "[channel] %u has no [voice]", c + 1);
    pb_bench_set_tempo(b, r.bpm, r.swing);
    for (unsigned c = 0; c < PB_CHANNELS; c++)
        if (!pb_pattern_valid(&b->channel[c].pattern)) return fail(&r, "invalid pattern in channel %u", c + 1);
    return true;
}

/* ---- writing -------------------------------------------------------------- */

bool pb_bench_write(FILE *f, const pb_bench *b) {
    pb_sequence sequence = pb_bench_sequence(b);
    if (!pb_sequence_valid(&sequence) || b->selected >= PB_CHANNELS) return false;
    for (unsigned c = 0; c < PB_CHANNELS; c++)
        if (!pb_voice_valid(&b->channel[c].voice) || strchr(b->channel[c].patch, '\n')) return false;
    fprintf(f, "patchbench=2\nselected=%u\n", b->selected + 1);
    fprintf(f, "[probe]\nnote=%u\nvelocity=%u\npinned=%u\n", b->probe.note, b->probe.velocity, b->probe.pinned);
    fprintf(f, "[sequence]\nbpm=%u\nswing=%u\n", b->channel[0].pattern.bpm, b->channel[0].pattern.swing);
    for (unsigned c = 0; c < PB_CHANNELS; c++) {
        const pb_channel *ch = &b->channel[c];
        const pb_voice *v = &ch->voice;
        const pb_pattern *p = &ch->pattern;
        fprintf(f, "[channel]\nindex=%u\npatch=%s\nmute=%u\nsteps=%u\nroot=%u\n[voice]\n", c + 1, ch->patch, ch->mute,
                p->steps, p->root);
        for (size_t i = 0; i < VOICE_KEY_COUNT; i++) {
            const key_spec *s = &VOICE_KEYS[i];
            const unsigned char *src = (const unsigned char *)v + s->offset;
            if (!strcmp(s->key, "seed")) fprintf(f, "seed=%llu\n", (unsigned long long)v->seed);
            else if (s->integer) fprintf(f, "%s=%u\n", s->key, *src);
            else {
                float x;
                memcpy(&x, src, sizeof x);
                fprintf(f, "%s=%.9g\n", s->key, (double)x);
            }
        }
        for (unsigned s = 0; s < p->steps; s++) {
            const pb_step *st = &p->step[s];
            if (!st->mask) continue;
            fprintf(f, "[step]\nindex=%u\nnotes=", s + 1);
            bool first = true;
            for (unsigned row = 0; row < PB_SEQ_SPAN; row++)
                if (st->mask >> row & 1u) {
                    int note = pb_step_note(p, s, row);
                    if (note < 0) return false;
                    fprintf(f, first ? "%d" : ",%d", note);
                    first = false;
                }
            fprintf(f, "\nvelocity=%u\ngate=%u\ntie=%u\noctave=%d\n", st->velocity, st->gate, st->tie, st->octave);
        }
    }
    for (unsigned s = 0; s < PB_SNAPSHOTS; s++) {
        if (!b->snapshot_used[s]) continue;
        fprintf(f, "[snapshot]\nslot=%u\n", s + 1);
        ma_patch_field_info info;
        for (size_t i = 0; ma_patch_field_info_at(i, &info); i++) {
            double x = 0;
            (void)ma_patch_field_get(&b->snapshot[s], i, &x);
            if (info.integer) fprintf(f, "%s=%d\n", info.name, (int)x);
            else fprintf(f, "%s=%.9g\n", info.name, x);
        }
    }
    return !ferror(f);
}

bool pb_bench_load(const char *path, pb_bench *b, pb_error *error) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (error) *error = (pb_error){ 0 }, snprintf(error->message, sizeof error->message, "cannot open %s", path);
        return false;
    }
    bool ok = pb_bench_read(f, b, error);
    fclose(f);
    return ok;
}

bool pb_atomic_write(const char *path, bool (*write)(FILE *, const void *), const void *data) {
    char tmp[PB_PATH_MAX + 16];
    if (snprintf(tmp, sizeof tmp, "%s.tmp.XXXXXX", path) >= (int)sizeof tmp) return false;
    int fd = mkstemp(tmp);
    if (fd < 0) return false;
    FILE *f = fdopen(fd, "wb");
    if (!f) {
        close(fd);
        remove(tmp);
        return false;
    }
    bool ok = write(f, data) && fflush(f) == 0 && fsync(fileno(f)) == 0;
    if (fclose(f)) ok = false;
    if (ok && rename(tmp, path) == 0) return true;
    remove(tmp);
    return false;
}

static bool write_bench(FILE *f, const void *b) { return pb_bench_write(f, b); }

bool pb_bench_save(const char *path, const pb_bench *b, pb_error *error) {
    if (pb_atomic_write(path, write_bench, b)) return true;
    if (error) *error = (pb_error){ 0 }, snprintf(error->message, sizeof error->message, "cannot save %s", path);
    return false;
}
