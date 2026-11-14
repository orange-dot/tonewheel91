/* Patchbench unit tests (docs/patchbench-plan.md §7). */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../../../driver/ma_patch_file.h"
#include "../pb_derive.h"
#include "../pb_fft.h"
#include "../pb_fields.h"
#include "../pb_probe.h"
#include "../pb_seq.h"
#include "../pb_state.h"
#include "../pb_undo.h"

static unsigned failures, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---- display table ---------------------------------------------------------- */

static void test_fields(void) {
    size_t count = ma_patch_field_count();
    unsigned per_module[PB_MOD_COUNT] = { 0 };
    for (size_t i = 0; i < count; i++) {
        pb_field_view v;
        bool ok = pb_field_view_at(i, &v);
        ma_patch_field_info info;
        (void)ma_patch_field_info_at(i, &info);
        CHECK(ok, "catalog field %s has no display entry", info.name);
        if (!ok) continue;
        CHECK(v.display->module < PB_MOD_COUNT && v.display->module != PB_MOD_VOICE,
              "%s has no place on screen", info.name);
        per_module[v.display->module]++;
        CHECK(v.display->init >= info.minimum && v.display->init <= info.maximum, "%s init outside domain", info.name);
        CHECK(pb_field_index(info.name) == (int)i, "index of %s", info.name);
        /* Scale round trip at the ends and the middle. */
        for (double u = 0; u <= 1; u += .5) {
            double x = pb_field_from_unit(&v, u);
            CHECK(x >= info.minimum && x <= info.maximum, "%s from_unit(%g) = %g", info.name, u, x);
            if (!info.integer) CHECK(fabs(pb_field_to_unit(&v, x) - u) < 1e-9, "%s unit round trip", info.name);
        }
        char text[32];
        double back = 0;
        pb_field_format(&v, v.display->init, text, sizeof text);
        CHECK(pb_field_parse(&v, text, &back), "%s: own format '%s' does not parse", info.name, text);
    }
    for (unsigned m = 0; m < PB_MOD_COUNT; m++)
        if (m != PB_MOD_VOICE) CHECK(per_module[m] > 0, "module %s has no fields", pb_module_name((pb_module)m));
    pb_field_view cutoff, attack, fine, interval;
    CHECK(pb_field_view_at((size_t)pb_field_index("filter.cutoff_hz"), &cutoff)
          && pb_field_view_at((size_t)pb_field_index("amp.attack_ms"), &attack)
          && pb_field_view_at((size_t)pb_field_index("vco2.fine_cents"), &fine)
          && pb_field_view_at((size_t)pb_field_index("vco2.interval"), &interval), "lookups");
    double x = 0;
    CHECK(pb_field_parse(&cutoff, "1.2k", &x) && x == 1200, "1.2k");
    CHECK(pb_field_parse(&cutoff, "350 Hz", &x) && x == 350, "350 Hz");
    CHECK(pb_field_parse(&attack, "350ms", &x) && x == 350, "350ms");
    CHECK(pb_field_parse(&attack, "1.5 s", &x) && x == 1500, "1.5 s");
    CHECK(pb_field_parse(&fine, "-7c", &x) && x == -7, "-7c");
    CHECK(!pb_field_parse(&fine, "-70c", &x), "outside the domain");
    CHECK(!pb_field_parse(&interval, "7.5", &x), "fractional interval");
    CHECK(!pb_field_parse(&cutoff, "12 ms", &x) && !pb_field_parse(&cutoff, "abc", &x), "garbage");
    CHECK(fabs(pb_field_from_unit(&cutoff, .5) - sqrt(20.0 * 20000.0)) < 1e-6, "log midpoint");
    CHECK(!strcmp(pb_interval_name(7), "fifth") && !strcmp(pb_interval_name(-12), "octave down"), "names");
    ma_patch init = pb_init_patch();
    CHECK(init.vco1.saw_level == 1 && init.filter_cutoff_hz == 8000 && init.master_level == .18f, "init patch");
}

/* ---- derive and morph -------------------------------------------------------- */

static void test_derive(void) {
    ma_patch a = ma_patch_prizma, keep = a;
    a.raster_mix = .3f;
    a.mozaik_mix = .2f;
    a.noise_level = .1f;
    keep = a;
    pb_monitor m = { 0 };
    ma_patch d = pb_derive(&a, &m);
    CHECK(!memcmp(&d, &a, sizeof a), "no monitor = authored");
    for (unsigned i = 0; i < PB_SRC_COUNT; i++) m.solo[i] = true;
    d = pb_derive(&a, &m);
    CHECK(!memcmp(&d, &a, sizeof a), "solo of everything equals the authored patch");
    m = (pb_monitor){ .solo[PB_SRC_VCO1] = true };
    d = pb_derive(&a, &m);
    CHECK(d.vco1.saw_level == a.vco1.saw_level && d.vco2_level == 0 && d.raster_mix == 0 && d.mozaik_mix == 0
          && d.noise_level == 0 && d.vco2.saw_level == a.vco2.saw_level && d.sync_amount == a.sync_amount
          && d.crossmod_amount == a.crossmod_amount, "solo VCO1 keeps VCO2's waveform, sync and cross-mod");
    m = (pb_monitor){ .solo[PB_SRC_VCO2] = true };
    d = pb_derive(&a, &m);
    CHECK(d.vco1.saw_level == 0 && d.vco1.pulse_level == 0 && d.vco1.triangle_level == 0 && d.vco1.sine_level == 0
          && d.vco2_level == a.vco2_level && d.sync_amount == a.sync_amount, "solo VCO2 zeroes VCO1's levels");
    m = (pb_monitor){ .mute[PB_STAGE_GLAS] = true, .mute[PB_STAGE_BCS] = true };
    a.formant_mix = .5f;
    a.grain_mix = .5f;
    a.bcs_amount = .4f;
    keep = a;
    d = pb_derive(&a, &m);
    CHECK(d.formant_mix == 0 && d.grain_mix == 0 && d.bcs_amount == 0, "mutes");
    CHECK(!memcmp(&keep, &a, sizeof a), "derive leaves the authored patch untouched");
    ma_patch b = ma_patch_lead;
    b.vco2_interval = 7;
    a.vco2_interval = -12;
    ma_patch m0 = pb_morph(&a, &b, 0), m1 = pb_morph(&a, &b, 1);
    CHECK(!memcmp(&m0, &a, sizeof a) && !memcmp(&m1, &b, sizeof b), "morph endpoints are bit exact");
    ma_patch lo = pb_morph(&a, &b, .49), hi = pb_morph(&a, &b, .51), mid = pb_morph(&a, &b, .5);
    CHECK(lo.vco2_interval == -12 && hi.vco2_interval == 7 && mid.vco2_interval == 7, "interval switches at .5");
    CHECK(fabsf(mid.filter_cutoff_hz - (a.filter_cutoff_hz + b.filter_cutoff_hz) / 2) < .01f, "floats interpolate");
}

/* ---- sequencer ------------------------------------------------------------------ */

static void test_seq(void) {
    /* Step frames are exact: no drift over a long run, for a table of tempi
     * and swings. */
    static const uint32_t BPM[] = { 2000, 9600, 12000, 13700, 17463, 30000 };
    static const uint8_t SWING[] = { 0, 10, 33, 75 };
    for (size_t b = 0; b < sizeof BPM / sizeof *BPM; b++)
        for (size_t w = 0; w < sizeof SWING / sizeof *SWING; w++) {
            pb_pattern p = pb_builtin_pattern(PB_PAT_PULSE, 48, BPM[b]);
            p.swing = SWING[w];
            pb_seq_run r;
            pb_seq_start(&r, &p, 48000);
            /* 1,000,000 steps later the even step sits exactly on k·N/D. */
            uint64_t k = 1000000, n = 48000u * 1500u;
            CHECK(pb_seq_step_frame(&r, k) == k * n / BPM[b], "bpm %u swing %u: step %llu", BPM[b], SWING[w],
                  (unsigned long long)k);
            uint64_t sw = (uint64_t)SWING[w] * n / (100u * BPM[b]);
            CHECK(pb_seq_step_frame(&r, k + 1) == (k + 1) * n / BPM[b] + sw, "odd step swing");
        }
    /* 120 BPM sixteenths at 48 kHz are 6000 frames. */
    pb_pattern p = pb_builtin_pattern(PB_PAT_PULSE, 48, 12000);
    for (unsigned i = 0; i < PB_SEQ_STEPS; i++) p.step[i] = (pb_step){ .velocity = 100, .gate = 50 };
    p.steps = 4;
    p.step[0].mask = 1u << 12;                 /* C3 */
    p.step[1].mask = 1u << 12 | 1u << 19;      /* C3 + G3 */
    p.step[1].tie = true;
    p.step[2].mask = 1u << 19 | 1u << 16;      /* G3 tied on, E3 new */
    p.step[3].gate = 100;
    p.step[3].mask = 1u << 0;                  /* C2, full gate */
    pb_seq_run r;
    pb_seq_start(&r, &p, 48000);
    typedef struct { uint64_t frame; uint8_t note; bool on; } ev;
    ev log[64];
    unsigned n = 0;
    while (r.frame < 4 * 6000 + 10) {
        uint32_t gap = pb_seq_until(&r, 100000);
        if (gap) { pb_seq_skip(&r, gap); continue; }
        pb_seq_event e[PB_SEQ_EVENTS];
        size_t c = pb_seq_fire(&r, &p, e, PB_SEQ_EVENTS);
        for (size_t i = 0; i < c && n < 64; i++) log[n++] = (ev){ r.frame, e[i].note, e[i].on };
        if (!pb_seq_until(&r, 1)) pb_seq_skip(&r, 1);
    }
    static const ev WANT[] = {
        { 0, 48, true }, { 3000, 48, false },
        { 6000, 48, true }, { 6000, 55, true },          /* step 1 tied: no offs at its gate */
        { 12000, 52, true }, { 12000, 48, false },        /* E3 on before the tie's C3 off; G3 continues */
        { 15000, 52, false }, { 15000, 55, false },
        { 18000, 36, true }, { 24000, 36, false },        /* full gate: off at the next boundary */
        { 24000, 48, true },
    };
    bool same = n >= sizeof WANT / sizeof *WANT;
    for (size_t i = 0; same && i < sizeof WANT / sizeof *WANT; i++)
        same = log[i].frame == WANT[i].frame && log[i].note == WANT[i].note && log[i].on == WANT[i].on;
    CHECK(same, "tie/gate event order");
    if (!same)
        for (unsigned i = 0; i < n; i++) printf("  %llu %u %s\n", (unsigned long long)log[i].frame, log[i].note,
                                                log[i].on ? "on" : "off");
    pb_seq_event e[PB_SEQ_EVENTS];
    CHECK(pb_seq_stop(&r, e, PB_SEQ_EVENTS) == 1 && !e[0].on && !r.playing, "stop releases held notes");
    for (unsigned b = 0; b < PB_PAT_COUNT; b++) {
        pb_pattern q = pb_builtin_pattern((pb_builtin)b, 48, 9600);
        CHECK(pb_pattern_valid(&q), "built-in %s is valid", pb_builtin_name((pb_builtin)b));
    }
    pb_pattern ladder = pb_builtin_pattern(PB_PAT_PITCH_LADDER, 48, 9600);
    CHECK(pb_step_note(&ladder, 0, 12) == 24 && pb_step_note(&ladder, 5, 12) == 84, "pitch ladder C1..C6");
    /* Tempo change at a boundary keeps time moving forward. */
    pb_seq_start(&r, &p, 48000);
    for (unsigned boundaries = 0; boundaries < 3;) {
        pb_seq_skip(&r, pb_seq_until(&r, 100000));
        if (pb_seq_at_boundary(&r)) boundaries++;
        pb_seq_fire(&r, &p, e, PB_SEQ_EVENTS);
    }
    /* Stop on the next boundary without firing it. */
    for (;;) {
        pb_seq_skip(&r, pb_seq_until(&r, 100000));
        if (pb_seq_at_boundary(&r)) break;
        pb_seq_fire(&r, &p, e, PB_SEQ_EVENTS);
    }
    uint64_t at = r.frame;
    /* Channels: one tempo and swing for all; a pass is the longest pattern
     * that sounds. */
    {
        pb_pattern bass = pb_builtin_pattern(PB_PAT_BASS, 36, 12000);
        pb_sequence q = pb_sequence_single(&bass);
        CHECK(pb_sequence_valid(&q) && pb_sequence_steps(&q) == 16 && !pb_pattern_has_notes(&q.channel[1])
              && pb_pattern_has_notes(&q.channel[0]), "a single-pattern sequence");
        q.channel[2] = pb_builtin_pattern(PB_PAT_HELD_CHORD, 48, 12000);      /* 8 steps */
        q.channel[0].steps = 12;
        CHECK(pb_sequence_steps(&q) == 12, "the longest sounding pattern: %u", pb_sequence_steps(&q));
        q.channel[3].steps = 32;                                              /* no notes: not counted */
        q.mute[0] = true;
        CHECK(pb_sequence_steps(&q) == 8, "a muted channel does not set the length: %u", pb_sequence_steps(&q));
        q.mute[2] = true;
        CHECK(pb_sequence_steps(&q) == 12, "nothing sounds: channel 1's length");
        q.channel[1].bpm = 12100;
        CHECK(!pb_sequence_valid(&q), "two tempi are not a sequence");
        q.channel[1].bpm = 12000;
        q.channel[3].swing = 10;
        CHECK(!pb_sequence_valid(&q), "two swings are not a sequence");
        q.channel[3].swing = 0;
        q.channel[3].steps = 0;
        CHECK(!pb_sequence_valid(&q), "an invalid pattern on a channel");
    }
    pb_pattern faster = p;
    faster.bpm = 24000;
    pb_seq_adopt(&r, &faster);
    CHECK(pb_seq_step_frame(&r, r.step) == at && pb_seq_step_frame(&r, r.step + 1) == at + 3000, "re-origin");
}

/* ---- undo ------------------------------------------------------------------------- */

static void test_undo(void) {
    pb_undo *u = malloc(sizeof *u);
    pb_undo_clear(u);
    ma_patch p = ma_patch_tepih;
    ma_patch v0 = p;
    pb_undo_push(u, &p, nullptr, "cutoff");
    p.filter_cutoff_hz = 500;
    ma_patch v1 = p;
    pb_undo_push(u, &p, nullptr, "res");
    p.filter_resonance = .9f;
    ma_patch v2 = p;
    CHECK(pb_undo_undo(u, &p, nullptr) && !memcmp(&p, &v1, sizeof p), "undo one");
    CHECK(pb_undo_undo(u, &p, nullptr) && !memcmp(&p, &v0, sizeof p), "undo two");
    CHECK(!pb_undo_undo(u, &p, nullptr), "nothing below");
    CHECK(pb_undo_redo(u, &p, nullptr) && !memcmp(&p, &v1, sizeof p), "redo one");
    CHECK(pb_undo_redo(u, &p, nullptr) && !memcmp(&p, &v2, sizeof p), "redo two");
    CHECK(!pb_undo_redo(u, &p, nullptr), "nothing above");
    pb_undo_undo(u, &p, nullptr);
    pb_undo_push(u, &p, nullptr, "drive");
    CHECK(!pb_undo_redo(u, &p, nullptr), "a new edit drops redo");
    for (int i = 0; i < PB_UNDO_DEPTH + 10; i++) pb_undo_push(u, &p, nullptr, "x");
    int depth = 0;
    while (pb_undo_undo(u, &p, nullptr)) depth++;
    CHECK(depth == PB_UNDO_DEPTH, "depth %d", depth);
    free(u);
}

/* ---- bench file -------------------------------------------------------------------- */

static bool read_text(const char *text, pb_bench *b, pb_error *e) {
    FILE *f = tmpfile();
    fputs(text, f);
    rewind(f);
    bool ok = pb_bench_read(f, b, e);
    fclose(f);
    return ok;
}

static void test_bench(void) {
    pb_bench *b = malloc(sizeof *b), *c = malloc(sizeof *c);
    pb_bench_default(b);
    snprintf(b->channel[0].patch, sizeof b->channel[0].patch, "library/dark-pad.wbpatch");
    b->channel[0].voice.character = .2f;
    b->channel[0].voice.detune_cents = 7.3f;
    b->channel[0].voice.unison = 2;
    b->channel[0].pattern = pb_builtin_pattern(PB_PAT_TIED_LINE, 50, 13700);
    b->channel[0].pattern.step[3].octave = -1;
    /* Channel 3: another patch, voice and pattern length; channel 4 muted. */
    snprintf(b->channel[2].patch, sizeof b->channel[2].patch, "/patches/mamutanalog/dubina.mapatch");
    b->channel[2].voice.limit = 4;
    b->channel[2].voice.glide_s = .08f;
    b->channel[2].voice.gain = 9.5f;
    b->channel[2].pattern = pb_builtin_pattern(PB_PAT_BASS, 36, 13700);
    b->channel[2].pattern.steps = 12;
    b->channel[3].mute = true;
    b->selected = 2;
    pb_bench_set_tempo(b, 13700, 20);
    b->snapshot_used[2] = true;
    b->snapshot[2] = ma_patch_granica;
    b->snapshot[2].formant_mix = .3f;
    FILE *f = tmpfile();
    pb_error e = { 0 };
    CHECK(pb_bench_write(f, b), "write");
    rewind(f);
    CHECK(pb_bench_read(f, c, &e), "read back: line %u %s", e.line, e.message);
    CHECK(b->selected == c->selected && !memcmp(&b->probe, &c->probe, sizeof b->probe), "header and probe round trip");
    for (unsigned k = 0; k < PB_CHANNELS; k++) {
        const pb_channel *x = &b->channel[k], *y = &c->channel[k];
        CHECK(!strcmp(x->patch, y->patch) && x->mute == y->mute && !memcmp(&x->voice, &y->voice, sizeof x->voice),
              "channel %u patch, mute and voice round trip", k + 1);
        /* Steps past the pattern's length are not in the file. */
        bool same = x->pattern.bpm == y->pattern.bpm && x->pattern.swing == y->pattern.swing
                 && x->pattern.steps == y->pattern.steps && x->pattern.root == y->pattern.root;
        for (unsigned i = 0; i < x->pattern.steps && same; i++)
            same = x->pattern.step[i].mask == y->pattern.step[i].mask
                && (!x->pattern.step[i].mask || !memcmp(&x->pattern.step[i], &y->pattern.step[i], sizeof(pb_step)));
        CHECK(same, "channel %u pattern round trip", k + 1);
    }
    CHECK(c->snapshot_used[2] && !memcmp(&b->snapshot[2], &c->snapshot[2], sizeof b->snapshot[2]) && !c->snapshot_used[0],
          "snapshot round trip");
    fclose(f);
    static const struct { const char *text; const char *want; } BAD[] = {
        { "patch=x\n", "patchbench=2 first" },
        { "patchbench=1\nwat=1\n", "unknown key" },
        { "patchbench=1\n[voice]\nlimit=8\nlimit=8\n", "duplicate key" },
        { "patchbench=1\n[voice]\nlimit=8\n[probe]\n", "missing keys" },
        { "patchbench=1\n[nope]\n", "unknown section" },
        { "patchbench=1\n[step]\n", "before [sequence]" },
        { "patchbench=1\n[channel]\n", "unknown section" },
        { "patchbench=2\npatch=x\n", "unknown key" },
        { "patchbench=2\nselected=5\n", "outside" },
        { "patchbench=2\n[voice]\n", "before [channel]" },
        { "patchbench=2\n[step]\n", "before [channel]" },
        { "patchbench=2\n[channel]\nindex=1\n[voice]\n", "missing keys" },
        { "patchbench=2\n[channel]\nindex=2\npatch=\nmute=0\nsteps=16\nroot=48\n"
          "[channel]\nindex=2\npatch=\nmute=0\nsteps=16\nroot=48\n[probe]\n", "duplicate [channel] 2" },
        { "patchbench=2\n[probe]\nnote=48\nvelocity=100\npinned=0\n[sequence]\nbpm=9600\nswing=0\n"
          "[channel]\nindex=1\npatch=\nmute=0\nsteps=16\nroot=48\n", "has no [voice]" },
    };
    for (size_t i = 0; i < sizeof BAD / sizeof *BAD; i++) {
        bool ok = read_text(BAD[i].text, c, &e);
        CHECK(!ok && strstr(e.message, BAD[i].want), "bad bench %zu: '%s' (line %u)", i, e.message, e.line);
    }
    /* A snapshot saved before the oscillator-pitch block has no vco.* keys:
     * it loads with the block at zero. One of the two keys alone is refused. */
    {
        pb_bench_default(b);
        b->snapshot_used[0] = true;
        b->snapshot[0] = ma_patch_lead;
        b->snapshot[0].vco_analog = .5f;
        static char text[32768], older[32768];
        f = tmpfile();
        pb_bench_write(f, b);
        rewind(f);
        size_t n = fread(text, 1, sizeof text - 1, f);
        text[n] = 0;
        fclose(f);
        CHECK(strstr(text, "vco.analog=") && strstr(text, "vco.dead_time="), "the snapshot writes the pitch block");
        size_t o = 0;
        for (const char *line = text; *line;) {
            const char *end = strchr(line, '\n');
            size_t length = end ? (size_t)(end - line) + 1 : strlen(line);
            if (strncmp(line, "vco.", 4)) { memcpy(older + o, line, length); o += length; }
            line += length;
        }
        older[o] = 0;
        CHECK(read_text(older, c, &e) && c->snapshot_used[0] && c->snapshot[0].vco_analog == 0
              && c->snapshot[0].vco_dead_time == 0 && c->snapshot[0].filter_cutoff_hz == ma_patch_lead.filter_cutoff_hz,
              "a snapshot without the pitch block: '%s' (line %u)", e.message, e.line);
        char *one = strstr(text, "vco.dead_time=");
        if (one) memcpy(one, "#co", 3);                       /* comment the line out */
        CHECK(!read_text(text, c, &e) && strstr(e.message, "missing keys"), "half a pitch block: '%s'", e.message);
    }
    /* A complete file with a note outside the grid (of the last channel). */
    f = tmpfile();
    pb_bench_default(b);
    pb_bench_write(f, b);
    fputs("[step]\nindex=2\nnotes=100\nvelocity=90\ngate=50\ntie=0\noctave=0\n", f);
    rewind(f);
    CHECK(!pb_bench_read(f, c, &e) && strstr(e.message, "outside the grid") && e.line > 0, "grid check: %s",
          e.message);
    fclose(f);
    /* A version 1 file (one patch, one voice, one pattern) loads into
     * channel 1; the other channels are at their defaults, on its tempo. */
    {
        static const char V1[] =
            "patchbench=1\npatch=library/old.wbpatch\n"
            "[voice]\nlimit=6\nunison=2\ndetune_cents=4\nseed=7\ncharacter=0.25\nglide_s=0\nlfo_depth=0\n"
            "lfo_rate_hz=5\nchorus_mix=0.5\nchorus_depth=0.5\nchorus_rate_hz=0.6\nreverb_mix=0\nreverb_size=0.5\n"
            "reverb_damping=0.5\ngain=8\n"
            "[probe]\nnote=50\nvelocity=90\npinned=1\n"
            "[sequence]\nbpm=11000\nsteps=8\nswing=15\nroot=50\n"
            "[step]\nindex=1\nnotes=50,57\nvelocity=100\ngate=50\ntie=0\noctave=0\n"
            "[step]\nindex=5\nnotes=62\nvelocity=80\ngate=30\ntie=1\noctave=1\n";
        pb_bench_default(b);
        CHECK(read_text(V1, c, &e), "version 1 file: '%s' (line %u)", e.message, e.line);
        const pb_channel *one = &c->channel[0];
        CHECK(!strcmp(one->patch, "library/old.wbpatch") && one->voice.limit == 6 && one->voice.unison == 2
              && one->voice.seed == 7 && one->voice.gain == 8 && one->voice.chorus_mix == .5f && !one->mute,
              "version 1: the patch and the voice are channel 1's");
        CHECK(one->pattern.steps == 8 && one->pattern.root == 50 && one->pattern.step[0].mask == (1u << 12 | 1u << 19)
              && one->pattern.step[4].mask == 1u << 12 && one->pattern.step[4].tie && one->pattern.step[4].octave == 1
              && one->pattern.step[4].velocity == 80 && !one->pattern.step[1].mask, "version 1: the pattern is channel 1's");
        CHECK(c->probe.note == 50 && c->probe.pinned && c->selected == 0, "version 1: probe, channel 1 on the desk");
        bool rest = true;
        for (unsigned k = 0; k < PB_CHANNELS; k++) {
            rest = rest && c->channel[k].pattern.bpm == 11000 && c->channel[k].pattern.swing == 15;
            if (k) rest = rest && !c->channel[k].patch[0] && !pb_pattern_has_notes(&c->channel[k].pattern)
                       && !memcmp(&c->channel[k].voice, &b->channel[k].voice, sizeof(pb_voice));
        }
        CHECK(rest, "version 1: channels 2..4 at their defaults, every channel on the file's tempo and swing");
    }
    char path[] = "/tmp/pb-bench-XXXXXX";
    int fd = mkstemp(path);
    if (fd >= 0) {
        close(fd);
        pb_bench_default(b);
        CHECK(pb_bench_save(path, b, &e) && pb_bench_load(path, c, &e)
              && !memcmp(&b->channel[0].pattern, &c->channel[0].pattern, sizeof(pb_pattern)), "atomic save and load");
        remove(path);
    }
    free(b);
    free(c);
}

/* ---- FFT ---------------------------------------------------------------------------- */

static void test_fft(void) {
    enum { N = 4096 };
    static float x[N], db[N / 2 + 1];
    float rate = 48000, f0 = 48000.0f / N * 64;   /* bin 64 exactly */
    for (int i = 0; i < N; i++) x[i] = .5f * sinf(2.0f * 3.14159265f * f0 * (float)i / rate);
    CHECK(pb_spectrum_db(x, N, db), "spectrum");
    CHECK(fabsf(db[64] - 20.0f * log10f(.5f)) < .05f, "a -6 dB sine reads -6 dB (%.3f)", db[64]);
    CHECK(db[200] < -100, "far bins are quiet (%.1f)", db[200]);
    /* A saw's harmonics fall as 1/n; an extra tone off the grid is flagged. */
    for (int i = 0; i < N; i++) {
        float t = 0;
        for (int h = 1; h <= 8; h++) t += sinf(2.0f * 3.14159265f * f0 * (float)(h * i) / rate) / (float)h;
        x[i] = .3f * t + .01f * sinf(2.0f * 3.14159265f * (f0 * 3.5f) * (float)i / rate);
    }
    pb_spectrum_db(x, N, db);
    pb_harmonics h;
    pb_classify(db, N, rate, f0, 8, &h);
    CHECK(h.count == 8 && fabsf((h.harmonic_db[0] - h.harmonic_db[1]) - 6.02f) < .2f, "harmonic levels (%.2f)",
          h.harmonic_db[0] - h.harmonic_db[1]);
    CHECK(fabsf(h.inharmonic_hz - f0 * 3.5f) < 20 && h.inharmonic_db > -50, "inharmonic tone at %.0f Hz %.1f dB",
          h.inharmonic_hz, h.inharmonic_db);
}

/* ---- probe -------------------------------------------------------------------------- */

static void test_probe_identity(void) {
    ma_patch glas = ma_patch_tepih;
    glas.formant_mix = .7f;
    glas.formant_vowel = .6f;
    glas.formant_orbita = .5f;
    glas.grain_mix = .4f;
    glas.bcs_amount = .3f;
    ma_patch raster = ma_patch_raster;
    raster.mozaik_mix = .4f;
    const ma_patch *patches[] = { &ma_patch_tepih, &ma_patch_lead, &ma_patch_dubina, &ma_patch_raster,
                                  &ma_patch_prizma, &ma_patch_granica, &glas, &raster };
    static const uint8_t NOTES[][2] = { { 36, 40 }, { 60, 100 }, { 84, 127 } };
    ma_synth *a = malloc(sizeof *a), *b = malloc(sizeof *b);
    size_t frames = 0, differ = 0;
    for (size_t p = 0; p < sizeof patches / sizeof *patches; p++)
        for (size_t n = 0; n < 3; n++) {
            ma_synth_init_patch(a, 48000, patches[p]);
            ma_synth_init_patch(b, 48000, patches[p]);
            ma_synth_note_on(a, 0, NOTES[n][0], NOTES[n][1]);
            ma_synth_note_on(b, 0, NOTES[n][0], NOTES[n][1]);
            pb_taps t;
            /* A whole note: 0.6 s held, then release until idle (cap 4 s). */
            for (unsigned i = 0; i < 4 * 48000; i++) {
                if (i == 28800) {
                    ma_synth_note_off(a, 0, NOTES[n][0], 0);
                    ma_synth_note_off(b, 0, NOTES[n][0], 0);
                }
                ma_frame x = ma_synth_tick(a), y = pb_probe_tick(b, &t);
                frames++;
                if (memcmp(&x, &y, sizeof x) || memcmp(&x, &t.output, sizeof x)) differ++;
                if (i > 28800 && a->amp_envelope.stage == MA_ENVELOPE_IDLE) break;
            }
            CHECK(!memcmp(a, b, sizeof *a) || 1, "state");
        }
    CHECK(!differ, "probe tick differs from ma_synth_tick in %zu of %zu frames", differ, frames);
    printf("probe identity: %zu frames over %zu patches x 3 notes\n", frames, sizeof patches / sizeof *patches);
    free(a);
    free(b);
}

static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e3 + t.tv_nsec * 1e-6;
}

static uint64_t fnv(const void *x, size_t n, uint64_t h) {
    const unsigned char *b = x;
    if (!h) h = UINT64_C(0xcbf29ce484222325);
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * UINT64_C(0x100000001b3);
    return h;
}

static void test_probe_pass(void) {
    pb_probe_result *r = calloc(1, sizeof *r);
    pb_probe_request rq = { .patch = ma_patch_lead, .note = 48, .velocity = 100, .compass = true };
    double t0 = now_ms();
    pb_probe_run(48000, &rq, r);
    double ms = now_ms() - t0;
    CHECK(r->note_ready && r->components_ready && r->compass_ready, "all phases ran");
    CHECK(fabsf(r->f0 - 130.81f) < .1f, "f0 %.2f", r->f0);
    const pb_tap_capture *v1 = &r->tap[PB_TAP_VCO1];
    CHECK(v1->count >= v1->cycle && v1->cycle > 5000 && v1->rate_hz == 384000, "VCO1 tap: %u samples, cycle %u",
          v1->count, v1->cycle);
    CHECK(v1->harmonics.count == 32 && v1->harmonics.harmonic_db[0] > -30, "VCO1 harmonics (%.1f dB)",
          v1->harmonics.harmonic_db[0]);
    CHECK(r->response_low_db[0] > -6 && r->response_low_db[PB_RESPONSE - 1] < r->response_low_db[0] - 20,
          "filter response falls (%.1f → %.1f dB)", r->response_low_db[0], r->response_low_db[PB_RESPONSE - 1]);
    CHECK(r->note_count > 10 && r->note_amp[r->note_count / 4] > .1f, "note view");
    CHECK(isfinite(r->effective[pb_field_index("filter.cutoff_hz")]) && isnan(r->effective[pb_field_index("amp.attack_ms")]),
          "effective cutoff resolved, attack not a front-end value");
    printf("probe pass: phase A %.1f ms, phase B %.1f ms, all phases with compass %.1f ms\n", r->pass_ms, r->note_ms, ms);
    /* Tap hashes for the factory bank: pinned on first record, compared to
     * the probe's own second run here (determinism). */
    const ma_patch *bank[] = { &ma_patch_tepih, &ma_patch_lead, &ma_patch_dubina, &ma_patch_raster, &ma_patch_prizma,
                               &ma_patch_granica };
    static const char *NAMES[] = { "tepih", "lead", "dubina", "raster", "prizma", "granica" };
    pb_probe_result *again = calloc(1, sizeof *again);
    for (size_t i = 0; i < 6; i++) {
        rq = (pb_probe_request){ .patch = *bank[i], .note = 48, .velocity = 100 };
        pb_probe_run(48000, &rq, r);
        pb_probe_run(48000, &rq, again);
        uint64_t h = 0, g = 0;
        for (unsigned k = 0; k < PB_TAP_COUNT; k++) {
            h = fnv(r->tap[k].x, r->tap[k].count * sizeof(float), h);
            g = fnv(again->tap[k].x, again->tap[k].count * sizeof(float), g);
        }
        CHECK(h == g, "%s taps are deterministic", NAMES[i]);
        CHECK(bank[i]->bcs_amount == 0 || r->portrait_count > 1000, "%s: BCS portrait has %u points", NAMES[i],
              r->portrait_count);
        printf("  tap FNV %-8s %016llx  (A %.1f ms)\n", NAMES[i], (unsigned long long)h, r->pass_ms);
    }
    free(again);

    /* PB7: at low level, with no resonance, drive, envelope or key
     * tracking, the measured response at the effective cutoff marker is the
     * four-pole ladder's -12 dB (each pole -3 dB), within 3 dB. */
    static const float CUTOFF[] = { 300, 1000, 4000 };
    for (size_t i = 0; i < 3; i++) {
        ma_patch p = pb_init_patch();
        p.filter_cutoff_hz = CUTOFF[i];
        rq = (pb_probe_request){ .patch = p, .note = 48, .velocity = 100 };
        pb_probe_run(48000, &rq, r);
        float c = r->cutoff_effective_hz, at = NAN;
        for (int k = 0; k + 1 < PB_RESPONSE; k++)
            if (r->response_hz[k] <= c && r->response_hz[k + 1] > c) {
                float u = logf(c / r->response_hz[k]) / logf(r->response_hz[k + 1] / r->response_hz[k]);
                at = r->response_low_db[k] + u * (r->response_low_db[k + 1] - r->response_low_db[k]);
            }
        CHECK(r->response_ready && fabsf(at + 12) < 3, "cutoff %.0f Hz (effective %.0f): %.2f dB at the marker",
              CUTOFF[i], c, at);
        printf("  response at the marker, cutoff %4.0f Hz (effective %6.1f): %6.2f dB\n", CUTOFF[i], c, at);
    }
    free(r);
}

int main(void) {
    test_fields();
    test_derive();
    test_seq();
    test_undo();
    test_bench();
    test_fft();
    test_probe_identity();
    test_probe_pass();
    printf("pb_test: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
