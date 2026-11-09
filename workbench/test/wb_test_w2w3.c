/* W2/W3 tests, included by wb_test.c: time map, orchestration on synthetic
 * SMF fixtures, derived MIDI read-back, and render determinism. */
#include "../desk/wb_orch.h"
#include "../desk/wb_render.h"
#include "../desk/wb_smf.h"

static void test_timemap(void) {
    wb_timemap m;
    uint64_t t0 = 0;
    uint32_t us = 740740;
    CHECK(wb_timemap_init(&m, 480, 48000, 1920, 8, &t0, &us, 1));
    /* The MA3 exhibits' o_frame and o_bar, exactly. */
    unsigned frame_bad = 0, bar_bad = 0;
    for (uint64_t tick = 0; tick < 200000; tick++)
        if (wb_timemap_frame(&m, tick) != (tick * 740740u + 5000u) / 10000u) frame_bad++;
    for (uint64_t frame = 0; frame < 14000000; frame += 480) {
        float want = (float)frame * 10000.0f / (740740.0f * 1920) + 8;
        float got = wb_timemap_bar(&m, frame);
        if (memcmp(&want, &got, sizeof want)) bar_bad++;
    }
    CHECK(frame_bad == 0 && bar_bad == 0);

    /* Against render_midi's double arithmetic on a tempo map: equal except
     * where the exact position is a half frame, where the integer path
     * rounds up and the double path may land either side. */
    uint64_t ticks[4] = { 0, 1000, 5000, 9001 };
    uint32_t uss[4] = { 500000, 433333, 612345, 250000 };
    CHECK(wb_timemap_init(&m, 96, 44100, 384, 0, ticks, uss, 4));
    unsigned differ = 0, unexplained = 0;
    for (uint64_t tick = 0; tick < 20000; tick++) {
        double seconds = 0;
        uint64_t seg = 0;
        uint32_t cur = uss[0];
        for (unsigned i = 1; i < 4 && ticks[i] <= tick; i++) {
            seconds += (double)(ticks[i] - seg) * cur / (1e6 * 96);
            seg = ticks[i];
            cur = uss[i];
        }
        seconds += (double)(tick - seg) * cur / (1e6 * 96);
        uint64_t d = (uint64_t)(seconds * 44100 + 0.5), e = wb_timemap_frame(&m, tick);
        if (d != e) {
            differ++;
            double fraction = seconds * 44100 - floor(seconds * 44100);
            if (fabs(fraction - 0.5) > 1e-6) unexplained++;
        }
    }
    CHECK(unexplained == 0);
    (void)differ;
    /* A tempo change at the same tick replaces the earlier one. */
    uint64_t same[2] = { 0, 0 };
    uint32_t two[2] = { 500000, 250000 };
    CHECK(wb_timemap_init(&m, 480, 48000, 1920, 0, same, two, 2) && m.count == 1
          && m.segment[0].us_per_quarter == 250000);
    uint64_t late = 5;
    CHECK(!wb_timemap_init(&m, 480, 48000, 1920, 0, &late, two, 1));
}

/* ---- synthetic source ------------------------------------------------------ */

static void note(wb_smf_track_writer *w, uint64_t tick, uint8_t status, uint8_t a, uint8_t b) {
    uint8_t bytes[3] = { status, a, b };
    wb_smf_track_event(w, tick, bytes, (status & 0xf0u) == 0xc0u ? 2 : 3);
}

typedef struct { uint64_t tick; uint8_t status, a, b; } raw;

static int compare_raw(const void *x, const void *y) {
    const raw *p = x, *q = y;
    return (p->tick > q->tick) - (p->tick < q->tick);
}

static void track_from(wb_buf *out, const char *name, raw *ev, size_t n) {
    wb_smf_track_writer w;
    wb_smf_track_begin(&w);
    wb_smf_track_meta(&w, 0, 0x03, name, strlen(name));
    /* Stable by insertion: bubble the few events instead of qsort. */
    for (size_t i = 1; i < n; i++)
        for (size_t j = i; j > 0 && compare_raw(&ev[j - 1], &ev[j]) > 0; j--) {
            raw t = ev[j]; ev[j] = ev[j - 1]; ev[j - 1] = t;
        }
    for (size_t i = 0; i < n; i++) note(&w, ev[i].tick, ev[i].status, ev[i].a, ev[i].b);
    wb_smf_track_end(&w, out);
}

/* ppq 480, 4/4, 500000 us/q; bar = 1920 ticks. */
static void make_source(wb_buf *out) {
    wb_smf_header(out, 1, 4, 480);
    wb_smf_track_writer w;
    wb_smf_track_begin(&w);
    wb_smf_track_meta(&w, 0, 0x03, "song", 4);
    static const uint8_t METER[4] = { 4, 2, 24, 8 }, TEMPO[3] = { 0x07, 0xa1, 0x20 };
    wb_smf_track_meta(&w, 0, 0x58, METER, 4);
    wb_smf_track_meta(&w, 0, 0x51, TEMPO, 3);
    wb_smf_track_meta(&w, 1920, 0x06, "Verse", 5);
    wb_smf_track_end(&w, out);
    raw keys[] = {
        { 0, 0xc0, 4, 0 },
        /* bar 0: C4 held 2 bars (ring cap cuts it), exact duplicate quieter */
        { 0, 0x90, 60, 100 }, { 3840, 0x80, 60, 0 },
        { 0, 0x90, 60, 80 }, { 1000, 0x90, 60, 0 },   /* zero-velocity off */
        /* bar 1: E4, restruck at 1.25 while held */
        { 1920, 0x90, 64, 90 }, { 3840, 0x80, 64, 0 },
        { 2400, 0x90, 64, 70 }, { 2880, 0x80, 64, 0 },
        /* bar 2: G4 with a program change before it */
        { 3800, 0xc0, 5, 0 },
        { 3840, 0x90, 67, 50 }, { 4800, 0x80, 67, 0 },
        { 5000, 0x80, 72, 0 },                          /* unmatched release */
    };
    track_from(out, "Keys", keys, sizeof keys / sizeof *keys);
    raw drums[] = {
        { 1920, 0x99, 37, 100 }, { 2000, 0x89, 37, 0 },
        { 1920, 0x99, 36, 110 }, { 2000, 0x89, 36, 0 },
        { 3840, 0x99, 37, 1 }, { 3900, 0x89, 37, 0 },
    };
    track_from(out, "Drums", drums, sizeof drums / sizeof *drums);
    raw bass[] = {
        { 1920, 0x91, 28, 90 }, { 2400, 0x81, 28, 0 },
        { 2400, 0x91, 52, 90 }, { 2880, 0x81, 52, 0 },
        { 7680, 0x91, 40, 90 },                         /* stuck: closed at end */
        { 9600, 0x91, 41, 0 },
    };
    track_from(out, "Bass", bass, sizeof bass / sizeof *bass);
}

static void test_smf_reader(const wb_buf *bytes) {
    wb_smf smf;
    wb_error e = { 0 };
    CHECK(wb_smf_read((const uint8_t *)bytes->data, bytes->length, &smf, &e));
    CHECK(smf.track_count == 4 && smf.division == 480 && !strcmp(smf.track[1].name, "Keys"));
    CHECK(smf.zero_velocity_offs == 2);
    wb_smf_notes n;
    CHECK(wb_smf_pair_notes(&smf, 1, &n));
    CHECK(n.count == 5 && n.unmatched_offs == 1 && n.stuck == 0);
    /* FIFO: the first C4 note-on is closed by the first release (tick 1000). */
    CHECK(n.note[0].start == 0 && n.note[0].end == 1000 && n.note[0].velocity == 100);
    CHECK(n.note[1].end == 3840);
    CHECK(n.note[0].program == 4 && n.note[4].program == 5);
    wb_smf_notes_free(&n);
    CHECK(wb_smf_pair_notes(&smf, 3, &n) && n.stuck == 1 && n.note[2].end == 9600);
    wb_smf_notes_free(&n);
    wb_smf_tempo tempo[4];
    wb_smf_meter meter[4];
    CHECK(wb_smf_tempos(&smf, tempo, 4) == 1 && tempo[0].us_per_quarter == 500000);
    CHECK(wb_smf_meters(&smf, meter, 4) == 1 && meter[0].numerator == 4);
    wb_smf_free(&smf);
    /* Refusals. */
    unsigned char smpte[14] = { 'M','T','h','d',0,0,0,6,0,0,0,1,0xe7,0x28 };
    CHECK(!wb_smf_read(smpte, sizeof smpte, &smf, &e) && strstr(e.message, "SMPTE"));
    unsigned char format2[14] = { 'M','T','h','d',0,0,0,6,0,2,0,1,1,0xe0 };
    CHECK(!wb_smf_read(format2, sizeof format2, &smf, &e) && strstr(e.message, "format 2"));
    CHECK(!wb_smf_read((const uint8_t *)bytes->data, bytes->length - 5, &smf, &e));
}

static const char ORCH[] =
    "mws=1\n[session]\nname=orch test\n"
    "[source]\nid=s\npath=sources/src.mid\nsha256=%s\n"
    "[form]\nstart_bar=1\nend_bar=4\nlength_seconds=9\n"
    "[stem]\nid=mix\n"
    "[part]\nid=keys\npatch=patches/p.mapatch\nstem=mix\nlimit=8\n"
    "[part]\nid=snare\npatch=patches/p.mapatch\nstem=mix\n"
    "[part]\nid=bass\npatch=patches/p.mapatch\nstem=mix\n"
    "[part]\nid=kick\npatch=patches/p.mapatch\nstem=mix\n"
    "[part]\nid=prog\npatch=patches/p.mapatch\nstem=mix\n"
    "[rule]\npart=keys\nfrom=s:\"Keys\"\nring_max_bars=0.5\n"
    "[rule]\npart=snare\nfrom=s:channel=10\nnotes=37\nmap_to=52\nvelocity_scale=11/20\nvelocity_min=1\n"
    "[rule]\npart=bass\nfrom=s:\"Bass\"\nfold_below=40\nbar_out=4\n"
    "[rule]\npart=prog\nfrom=s:program=5\n"
    "[groove]\npart=kick\nnote=35\nwobble=7,3,9\npattern=a 0 8 vel=60 accent0=+6\n"
    "use=1..3:a\nhit=2 vel=120\n";

static bool make_session(char *dir, wb_session *s, wb_buf *src) {
    char path[512], text[4096], sha[65];
    if (!mkdtemp(dir)) return false;
    snprintf(path, sizeof path, "%s/sources", dir);
    mkdir(path, 0755);
    snprintf(path, sizeof path, "%s/patches", dir);
    mkdir(path, 0755);
    snprintf(path, sizeof path, "%s/sources/src.mid", dir);
    if (!wb_write_file_atomic(path, src->data, src->length)) return false;
    wb_sha256_bytes_hex(src->data, src->length, sha);
    ma_patch_document doc = { .value = ma_patch_dubina };
    snprintf(doc.name, sizeof doc.name, "p");
    snprintf(path, sizeof path, "%s/patches/p.mapatch", dir);
    ma_patch_error pe;
    if (!ma_patch_save(path, &doc, &pe)) return false;
    snprintf(text, sizeof text, ORCH, sha);
    wb_error e = { 0 };
    bool ok = wb_read(text, strlen(text), s, &e);
    if (!ok) fprintf(stderr, "  orch session: %u %s\n", e.line, e.message);
    return ok;
}

static void cleanup(const char *dir) {
    char cmd[600];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
    CHECK(system(cmd) == 0);
}

static bool has(const wb_opart *p, uint64_t start, uint64_t end, unsigned note, unsigned velocity) {
    for (size_t i = 0; i < p->count; i++)
        if (p->note[i].start == start && p->note[i].end == end && p->note[i].note == note
            && p->note[i].velocity == velocity)
            return true;
    return false;
}

static void test_orchestration(void) {
    wb_buf src = { 0 };
    make_source(&src);
    test_smf_reader(&src);
    char dir[] = "/tmp/wb-orch-XXXXXX";
    wb_session *s = malloc(sizeof *s);
    CHECK(make_session(dir, s, &src));
    wb_derived *d = calloc(1, sizeof *d);
    wb_error e = { 0 };
    bool built = wb_orch_build(s, dir, d, &e);
    CHECK(built);
    if (!built) fprintf(stderr, "  orch build: %s\n", e.message);
    if (built) {
        CHECK(d->start_tick == 1920 && d->end_tick == 7680 && d->ticks_per_bar == 1920);
        /* keys: bar-0 C4s fall before the form; E4 is capped at 960 ticks
         * and cut by its own re-strike (FIFO: the re-strike owns the later
         * release at 3840, capped at 2400 + 960); the G4 starts at bar 2. */
        const wb_opart *keys = &d->part[0];
        CHECK(keys->count == 3);
        for (size_t i = 0; i < keys->count; i++)
            if (getenv("WB_TEST_VERBOSE"))
                fprintf(stderr, "  keys %llu..%llu n%u v%u\n", (unsigned long long)keys->note[i].start,
                        (unsigned long long)keys->note[i].end, keys->note[i].note, keys->note[i].velocity);
        CHECK(has(keys, 0, 480, 64, 90) && has(keys, 480, 1440, 64, 70) && has(keys, 1920, 2880, 67, 50));
        CHECK(keys->cut == 1);
        /* snare: side stick 37 -> 52 at velocity floor(v*11/20), at least 1. */
        const wb_opart *snare = &d->part[1];
        CHECK(snare->count == 2 && has(snare, 0, 80, 52, 55) && has(snare, 1920, 1980, 52, 1));
        /* bass: 28 folds up to 40; the stuck note at bar 4 is outside bar_out. */
        const wb_opart *bass = &d->part[2];
        CHECK(bass->count == 2 && has(bass, 0, 480, 40, 90) && has(bass, 480, 960, 52, 90));
        /* program filter: only the G4 after the change to program 5. */
        CHECK(d->part[4].count == 1 && has(&d->part[4], 1920, 2880, 67, 50));
        /* kick: bar 1 pattern, bar 2 the hit; wobble (bar*7+step*3)%9-4. */
        const wb_opart *kick = &d->part[3];
        int w0 = (int)((1 * 7 + 0 * 3) % 9) - 4, w8 = (int)((1 * 7 + 8 * 3) % 9) - 4;
        CHECK(kick->count == 3 && has(kick, 0, 96, 35, (unsigned)(66 + w0))
              && has(kick, 960, 1056, 35, (unsigned)(60 + w8)) && has(kick, 1920, 2016, 35, 120));
        /* The derived MIDI reads back to the same notes, one track per part. */
        wb_smf back;
        CHECK(wb_smf_read((const uint8_t *)d->midi.data, d->midi.length, &back, &e));
        CHECK(back.track_count == 1 + s->part_count && !strcmp(back.track[1].name, "keys"));
        for (unsigned p = 0; p < s->part_count; p++) {
            wb_smf_notes n;
            CHECK(wb_smf_pair_notes(&back, p + 1, &n) && n.count == d->part[p].count);
            for (size_t i = 0; i < n.count; i++)
                CHECK(has(&d->part[p], n.note[i].start, n.note[i].end, n.note[i].note, n.note[i].velocity)
                      && n.note[i].channel == p);
            wb_smf_notes_free(&n);
        }
        wb_smf_free(&back);
        /* Same session, same bytes. */
        wb_derived *again = calloc(1, sizeof *again);
        CHECK(wb_orch_build(s, dir, again, &e) && !strcmp(again->sha256, d->sha256));
        wb_derived_free(again);
        free(again);
        char path[4400];
        CHECK(wb_orch_write(d, s, dir, path, sizeof path, &e) && strstr(path, "/derived/orch-test-"));
        CHECK(wb_orch_write(d, s, dir, path, sizeof path, &e));   /* idempotent */
    }
    /* A changed source is refused. */
    char path[512];
    snprintf(path, sizeof path, "%s/sources/src.mid", dir);
    chmod(path, 0644);
    CHECK(wb_write_file_atomic(path, "MThd", 4));
    wb_derived_free(d);
    CHECK(!wb_orch_build(s, dir, d, &e) && strstr(e.message, "changed"));
    wb_derived_free(d);
    free(d);
    free(s);
    cleanup(dir);
    wb_buf_free(&src);
}

static void test_render(void) {
    wb_buf src = { 0 };
    make_source(&src);
    char dir[] = "/tmp/wb-render-XXXXXX";
    wb_session *s = malloc(sizeof *s);
    CHECK(make_session(dir, s, &src));
    /* Add lanes of every call kind to exercise the control path. */
    const char *lanes[] = {
        "[curve]\nid=c\npoint=1 0\npoint=3 1\n",
        "[lane]\npart=keys\nparam=filter.cutoff_hz\ncurve=c\nscale=-400\noffset=1200\n",
        "[lane]\npart=keys\nparam=output.master\nop=mul\ncurve=c\nscale=-0.5\noffset=1\n",
        "[lane]\npart=bass\nparam=pool.macro.bloom\npoint=1 0\npoint=4 0.3\n",
        "[lane]\npart=bass\nparam=pool.lfo_depth\nshape=step\npoint=0 0.01\n",
        "[lane]\npart=bass\nparam=pool.lfo_rate_hz\nshape=step\npoint=0 0.2\n",
        "[lane]\npart=kick\nparam=part.gain\nop=mul\npoint=2 1\npoint=3 0.5\n",
        "[lane]\npart=snare\nparam=filter.resonance\nclamp=0 0.5\npoint=1 0.2\npoint=3 2\n",
    };
    wb_error e = { 0 };
    for (size_t i = 0; i < sizeof lanes / sizeof *lanes; i++) {
        wb_kind kind = lanes[i][1] == 'c' ? WB_KIND_CURVE : WB_KIND_LANE;
        CHECK(wb_read_object(s, kind, wb_kind_count(s, kind), true, lanes[i], strlen(lanes[i]), &e));
    }
    CHECK(wb_session_validate(s, &e));
    wb_derived *d = calloc(1, sizeof *d);
    char midi[4400];
    CHECK(wb_orch_build(s, dir, d, &e) && wb_orch_write(d, s, dir, midi, sizeof midi, &e));
    wb_score score;
    CHECK(wb_score_load_midi(s, midi, &score, &e));
    CHECK(!strcmp(score.sha256, d->sha256));
    wb_render_report *a = calloc(1, sizeof *a), *b = calloc(1, sizeof *b), *c = calloc(1, sizeof *c);
    wb_render_options options = { 0 };
    bool ok = wb_render(s, dir, &score, &options, a, &e);
    CHECK(ok);
    if (!ok) fprintf(stderr, "  render: %s\n", e.message);
    CHECK(a->frames == 9u * 48000u && a->outputs == 2);
    CHECK(a->level[1].peak > 0 && a->part[0].notes == 3);
    CHECK(wb_render(s, dir, &score, &options, b, &e));
    CHECK(a->level[1].hash == b->level[1].hash);      /* deterministic */
    options.engine_scalar = true;
    CHECK(wb_render(s, dir, &score, &options, c, &e));
    CHECK(a->level[1].hash == c->level[1].hash);      /* AUTO == scalar */
    /* A prefix is the same audio up to its end. */
    char prefix[600];
    snprintf(prefix, sizeof prefix, "%s/pre", dir);
    options = (wb_render_options){ .prefix = prefix, .frames = 48000 };
    CHECK(wb_render(s, dir, &score, &options, c, &e) && c->frames == 48000);
    snprintf(prefix, sizeof prefix, "%s/pre_mix.wav", dir);
    struct stat st;
    CHECK(stat(prefix, &st) == 0 && st.st_size > 48000 * 8);
    /* The clamp lane counted its clamps; the resonance lane stayed in range. */
    CHECK(a->part[1].lane_clamps == 0);
    /* Notes after the end of the form are refused before rendering. */
    s->form.length_seconds = 1;
    CHECK(!wb_render_check(s, dir, &score, c, &e) && strstr(e.message, "after the end"));
    wb_score_free(&score);
    wb_derived_free(d);
    free(d);
    free(a);
    free(b);
    free(c);
    free(s);
    cleanup(dir);
    wb_buf_free(&src);
}

#include "../desk/wb_import.h"

static const wb_import_part *plan_part(const wb_import_plan *p, const char *id) {
    for (unsigned i = 0; i < p->part_count; i++)
        if (!strcmp(p->part[i].id, id)) return &p->part[i];
    return nullptr;
}

static void test_import(void) {
    wb_buf src = { 0 };
    make_source(&src);
    wb_smf smf;
    wb_error e = { 0 };
    CHECK(wb_smf_read((const uint8_t *)src.data, src.length, &smf, &e));
    wb_import_plan *plan = calloc(1, sizeof *plan);
    wb_import_options o;

    /* Default: track x channel, drum groups on channel 10. */
    wb_import_options_default(&o);
    CHECK(wb_import_plan_build(&smf, &o, plan, &e));
    CHECK(plan->part_count == 4);
    const wb_import_part *kick = plan_part(plan, "kick"), *stick = plan_part(plan, "stick");
    CHECK(plan_part(plan, "keys") && plan_part(plan, "bass") && kick && stick);
    CHECK(kick && kick->notes == 1 && !strcmp(kick->template_name, "kick") && !strcmp(kick->stem, "rhythm"));
    CHECK(stick && stick->notes == 2 && stick->rule[0].channel == 10);
    const wb_import_part *keys = plan_part(plan, "keys");
    /* FIFO pairing: C4 0..1000 and 0..3840, E4 1920..2880 and 2400..3840. */
    CHECK(keys && keys->notes == 5 && !strcmp(keys->template_name, "keys") && keys->held_peak == 3);
    CHECK(!strcmp(plan_part(plan, "bass")->template_name, "bass"));
    CHECK(plan->start_bar == 0 && plan->end_bar == 5 && plan->section_count == 1
          && !strcmp(plan->section[0].name, "verse") && plan->section[0].start_bar == 1);
    CHECK(!strcmp(plan->title, "song") && plan->stuck_notes == 1 && plan->unmatched_offs == 2);

    /* Program split: keys becomes two parts. */
    o.program_split = true;
    CHECK(wb_import_plan_build(&smf, &o, plan, &e));
    CHECK(plan_part(plan, "keys-p4") && plan_part(plan, "keys-p5"));
    CHECK(plan_part(plan, "keys-p4")->notes == 4 && plan_part(plan, "keys-p5")->notes == 1);

    /* Pitch splits: at a note, and skyline top/rest. */
    wb_import_options_default(&o);
    o.pitch_split[o.pitch_split_count++] = (wb_pitch_split){ 1, 64 };
    CHECK(wb_import_plan_build(&smf, &o, plan, &e));
    CHECK(plan_part(plan, "keys-low") && plan_part(plan, "keys-low")->notes == 2
          && plan_part(plan, "keys-high")->notes == 3);
    o.pitch_split[0].note = -1;
    CHECK(wb_import_plan_build(&smf, &o, plan, &e));
    const wb_import_part *top = plan_part(plan, "keys-top"), *rest = plan_part(plan, "keys-rest");
    /* No note starts under a higher one, so every note is top and the
     * empty "rest" stream gives no part. */
    CHECK(top && top->notes == 5 && !rest);

    /* Manual: two streams into one part, the rest dropped. */
    wb_import_options_default(&o);
    o.split = WB_SPLIT_MANUAL;
    o.assign[o.assign_count++] = (wb_assign){ 1, -1, "low" };
    o.assign[o.assign_count++] = (wb_assign){ 3, -1, "low" };
    CHECK(wb_import_plan_build(&smf, &o, plan, &e));
    CHECK(plan->part_count == 1 && plan->part[0].rule_count == 2 && plan->part[0].notes == 8);

    /* Channel split. */
    wb_import_options_default(&o);
    o.split = WB_SPLIT_CHANNEL;
    o.drums = 0;
    CHECK(wb_import_plan_build(&smf, &o, plan, &e));
    CHECK(plan->part_count == 3 && plan_part(plan, "ch10") && plan_part(plan, "ch1"));

    /* An engine map file overrides the built-in table. */
    char dir[] = "/tmp/wb-import-XXXXXX";
    CHECK(mkdtemp(dir) != nullptr);
    char map[600];
    snprintf(map, sizeof map, "%s/import-map.txt", dir);
    const char MAP[] = "# test\nname keys organ organ\nprogram 0-127 mamut tepih\n";
    CHECK(wb_write_file_atomic(map, MAP, sizeof MAP - 1));
    wb_import_options_default(&o);
    o.map_path = map;
    CHECK(wb_import_plan_build(&smf, &o, plan, &e));
    CHECK(!strcmp(plan_part(plan, "keys")->engine, "organ") && plan_part(plan, "keys")->compass_ok);
    /* The map file is consulted before the built-in table. */
    CHECK(!strcmp(plan_part(plan, "bass")->template_name, "tepih"));
    const char BAD[] = "program x mamut tepih\n";
    CHECK(wb_write_file_atomic(map, BAD, sizeof BAD - 1));
    CHECK(!wb_import_plan_build(&smf, &o, plan, &e) && strstr(e.message, "import-map.txt:1"));
    wb_smf_free(&smf);

    /* Round trip: an orchestration's derived MIDI imported with the track
     * split rebuilds to the same bytes. */
    wb_session *s = malloc(sizeof *s);
    char sdir[] = "/tmp/wb-rt-XXXXXX";
    CHECK(make_session(sdir, s, &src));
    wb_derived *d = calloc(1, sizeof *d), *again = calloc(1, sizeof *again);
    char midi[4400];
    CHECK(wb_orch_build(s, sdir, d, &e) && wb_orch_write(d, s, sdir, midi, sizeof midi, &e));
    CHECK(wb_smf_load(midi, &smf, &e));
    wb_import_options_default(&o);
    o.split = WB_SPLIT_TRACK;
    o.drums = 0;
    CHECK(wb_import_plan_build(&smf, &o, plan, &e) && plan->part_count == s->part_count);
    wb_session *fresh = malloc(sizeof *fresh);
    wb_session_init(fresh);
    fresh->has_head = fresh->has_form = true;
    snprintf(fresh->head.name, sizeof fresh->head.name, "rt");
    CHECK(mkdir(strcat(strcpy(map, dir), "/s"), 0755) == 0);
    bool applied = wb_import_apply(fresh, map, "d", midi, plan, &o, nullptr, &e);
    CHECK(applied);
    if (!applied) fprintf(stderr, "  import apply: %s\n", e.message);
    CHECK(applied && wb_orch_build(fresh, map, again, &e));
    CHECK(applied && again->midi.length == d->midi.length
          && !memcmp(again->midi.data, d->midi.data, d->midi.length));
    /* A part id already in the session is refused, leaving it unchanged. */
    unsigned parts = fresh->part_count;
    CHECK(!wb_import_apply(fresh, map, "d2", midi, plan, &o, nullptr, &e) && fresh->part_count == parts);
    wb_smf_free(&smf);
    wb_derived_free(d);
    wb_derived_free(again);
    free(d);
    free(again);
    free(fresh);
    free(s);
    free(plan);
    cleanup(sdir);
    cleanup(dir);
    wb_buf_free(&src);
}

#include "../desk/wb_patch.h"

static void test_patch_chain(void) {
    char dir[] = "/tmp/wb-patch-XXXXXX";
    CHECK(mkdtemp(dir) != nullptr);
    char path[600];
    snprintf(path, sizeof path, "%s/patches", dir);
    mkdir(path, 0755);
    ma_patch_document base = { .value = ma_patch_dubina };
    snprintf(base.name, sizeof base.name, "base");
    snprintf(path, sizeof path, "%s/patches/base.mapatch", dir);
    ma_patch_error pe;
    CHECK(ma_patch_save(path, &base, &pe));
    static const struct { const char *file, *text; } FILES[] = {
        { "same.wbpatch", "wbpatch=1\nname=same\nparent=patches/base.mapatch\n" },
        { "child.wbpatch", "wbpatch=1\nname=child\nparent=patches/base.mapatch\nfilter.cutoff_hz=1234\n" },
        { "grand.wbpatch", "wbpatch=1\nname=grand\nparent=patches/child.wbpatch\namp.attack_ms=5\nvco2.interval=-12\n" },
        { "fact.wbpatch", "wbpatch=1\nname=fact\nparent=factory:tepih\noutput.master=0.2\n" },
        { "a.wbpatch", "wbpatch=1\nname=a\nparent=patches/b.wbpatch\n" },
        { "b.wbpatch", "wbpatch=1\nname=b\nparent=patches/a.wbpatch\n" },
        { "bad.wbpatch", "wbpatch=1\nname=bad\nparent=patches/base.mapatch\nfilter.nope=1\n" },
        { "range.wbpatch", "wbpatch=1\nname=r\nparent=patches/base.mapatch\nfilter.resonance=2\n" },
    };
    for (size_t i = 0; i < sizeof FILES / sizeof *FILES; i++) {
        snprintf(path, sizeof path, "%s/patches/%s", dir, FILES[i].file);
        CHECK(wb_write_file_atomic(path, FILES[i].text, strlen(FILES[i].text)));
    }
    ma_patch_document doc;
    wb_error e = { 0 };
    /* No overrides: the parent, bit for bit. */
    CHECK(wb_patch_load(dir, "patches/same.wbpatch", &doc, &e) && !memcmp(&doc.value, &ma_patch_dubina, sizeof doc.value));
    CHECK(!strcmp(doc.name, "same"));
    ma_patch want = ma_patch_dubina;
    want.filter_cutoff_hz = 1234;
    CHECK(wb_patch_load(dir, "patches/child.wbpatch", &doc, &e) && !memcmp(&doc.value, &want, sizeof want));
    want.amp_adsr.attack_ms = 5;
    want.vco2_interval = -12;
    CHECK(wb_patch_load(dir, "patches/grand.wbpatch", &doc, &e) && !memcmp(&doc.value, &want, sizeof want));
    ma_patch fact = ma_patch_tepih;
    fact.master_level = 0.2f;
    CHECK(wb_patch_load(dir, "patches/fact.wbpatch", &doc, &e) && !memcmp(&doc.value, &fact, sizeof fact));
    CHECK(!wb_patch_load(dir, "patches/a.wbpatch", &doc, &e) && strstr(e.message, "cycle"));
    CHECK(!wb_patch_load(dir, "patches/bad.wbpatch", &doc, &e) && strstr(e.message, "unknown patch field"));
    CHECK(!wb_patch_load(dir, "patches/range.wbpatch", &doc, &e) && strstr(e.message, "outside"));
    /* A .mapatch loads through the same path unchanged. */
    CHECK(wb_patch_load(dir, "patches/base.mapatch", &doc, &e) && !memcmp(&doc.value, &ma_patch_dubina, sizeof doc.value));
    /* Override editing round trip. */
    wb_patch_overrides o;
    snprintf(path, sizeof path, "%s/patches/grand.wbpatch", dir);
    CHECK(wb_patch_read_overrides(path, &o, &e) && o.override_count == 2);
    wb_patch_override(&o, 28, 777, false);   /* filter.cutoff_hz, catalog index 28 */
    wb_patch_override(&o, 33, 0, true);      /* amp.attack_ms back to the parent */
    CHECK(wb_patch_write_overrides(path, &o, &e));
    want.filter_cutoff_hz = 777;
    want.amp_adsr.attack_ms = ma_patch_dubina.amp_adsr.attack_ms;
    CHECK(wb_patch_load(dir, "patches/grand.wbpatch", &doc, &e) && !memcmp(&doc.value, &want, sizeof want));
    ma_patch parent;
    CHECK(wb_patch_parent(dir, &o, &parent, &e) && parent.filter_cutoff_hz == 1234);
    cleanup(dir);
}

static void test_overlays(void) {
    wb_buf src = { 0 };
    make_source(&src);
    char dir[] = "/tmp/wb-overlay-XXXXXX";
    wb_session *s = malloc(sizeof *s);
    CHECK(make_session(dir, s, &src));
    wb_error e = { 0 };
    /* keys: G4 at source bar 2 deleted; a new D5 added at bar 3 for half a bar. */
    const char *blocks[] = {
        "[overlay]\npart=keys\nop=delete\nbar=2\nnote=67\n",
        "[overlay]\npart=keys\nop=add\nbar=3\nlength_bars=0.5\nnote=74\nvelocity=77\n",
        "[overlay]\npart=keys\nop=add\nbar=0.5\nnote=50\n",           /* before the form: ignored */
    };
    for (size_t i = 0; i < sizeof blocks / sizeof *blocks; i++)
        CHECK(wb_read_object(s, WB_KIND_OVERLAY, s->overlay_count, true, blocks[i], strlen(blocks[i]), &e));
    CHECK(wb_session_validate(s, &e));
    wb_derived *d = calloc(1, sizeof *d);
    CHECK(wb_orch_build(s, dir, d, &e));
    const wb_opart *keys = &d->part[0];
    CHECK(keys->count == 3);
    CHECK(!has(keys, 1920, 2880, 67, 50) && has(keys, 3840, 4800, 74, 77));
    CHECK(has(keys, 0, 480, 64, 90));
    /* An overlay for an unknown part is refused. */
    const char bad[] = "[overlay]\npart=nobody\nbar=1\nnote=60\n";
    CHECK(wb_read_object(s, WB_KIND_OVERLAY, s->overlay_count, true, bad, sizeof bad - 1, &e));
    CHECK(!wb_session_validate(s, &e) && strstr(e.message, "undefined part"));
    wb_derived_free(d);
    free(d);
    free(s);
    cleanup(dir);
    wb_buf_free(&src);
}
