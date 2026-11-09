/* Workbench desk tests: hashes, number text, .mws round trips, strict
 * rejects, semantic checks, key edits and the undo journal. */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../driver/ma_patch_file.h"
#include "../desk/wb_format.h"
#include "../desk/wb_hash.h"
#include "../desk/wb_root.h"
#include "../desk/wb_session.h"
#include "../desk/wb_undo.h"

static unsigned checks, failures;

#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); } } while (0)

static const char *FIXTURE = "workbench/test/fixtures/hurt-v2.mws";

static char *slurp(const char *path, size_t *size) {
    char *data;
    if (!wb_read_file(path, &data, size)) {
        fprintf(stderr, "cannot read %s (run from the repository root)\n", path);
        exit(1);
    }
    return data;
}

static void test_sha256(void) {
    static const struct { const char *text, *hex; } V[] = {
        { "", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" },
        { "abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" },
        { "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1" },
    };
    char hex[65];
    for (size_t i = 0; i < sizeof V / sizeof *V; i++) {
        wb_sha256_bytes_hex(V[i].text, strlen(V[i].text), hex);
        CHECK(!strcmp(hex, V[i].hex));
    }
    /* One million 'a', fed in uneven pieces. */
    wb_sha256 h;
    wb_sha256_init(&h);
    char chunk[997];
    memset(chunk, 'a', sizeof chunk);
    size_t left = 1000000;
    while (left) {
        size_t n = left < sizeof chunk ? left : sizeof chunk;
        wb_sha256_update(&h, chunk, n);
        left -= n;
    }
    uint8_t digest[32];
    wb_sha256_final(&h, digest);
    wb_sha256_hex(digest, hex);
    CHECK(!strcmp(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
    CHECK(wb_sha256_hex_valid(V[0].hex));
    CHECK(!wb_sha256_hex_valid("E3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK(!wb_sha256_hex_valid("e3b0"));
}

static void test_numbers(void) {
    char text[32];
    wb_format_f32(10.0f, text); CHECK(!strcmp(text, "10"));
    wb_format_f32(0.1f, text); CHECK(!strcmp(text, "0.1"));
    wb_format_f32(0.00001f, text); CHECK(!strcmp(text, "0.00001"));
    wb_format_f32(20000.0f, text); CHECK(!strcmp(text, "20000"));
    wb_format_f64(1.5, text); CHECK(!strcmp(text, "1.5"));
    /* Round trip over a deterministic spread of f32 bit patterns. */
    uint32_t x = 0x12345678u;
    unsigned bad = 0;
    for (unsigned i = 0; i < 200000; i++) {
        x = x * 1664525u + 1013904223u;
        float f;
        memcpy(&f, &x, sizeof f);
        if (!isfinite(f)) continue;
        wb_format_f32(f, text);
        float back = strtof(text, nullptr);
        if (memcmp(&back, &f, sizeof f)) bad++;
    }
    CHECK(bad == 0);
    uint64_t y = 0x0123456789abcdefu;
    bad = 0;
    for (unsigned i = 0; i < 200000; i++) {
        y = y * 6364136223846793005u + 1442695040888963407u;
        double d;
        memcpy(&d, &y, sizeof d);
        if (!isfinite(d)) continue;
        wb_format_f64(d, text);
        double back = strtod(text, nullptr);
        if (memcmp(&back, &d, sizeof d)) bad++;
    }
    CHECK(bad == 0);
}

static void test_fixture_round_trip(void) {
    size_t size;
    char *text = slurp(FIXTURE, &size);
    wb_session *a = malloc(sizeof *a), *b = malloc(sizeof *b);
    wb_error e = { 0 };
    CHECK(wb_read(text, size, a, &e));
    if (e.message[0]) fprintf(stderr, "fixture: %u: %s\n", e.line, e.message);
    CHECK(wb_session_validate(a, &e));
    wb_buf one = { 0 }, two = { 0 };
    CHECK(wb_write(a, &one));
    /* The fixture is canonical text: writing it reproduces its bytes. */
    CHECK(one.length == size && !memcmp(one.data, text, size));
    CHECK(wb_read(one.data, one.length, b, &e));
    CHECK(wb_write(b, &two));
    CHECK(one.length == two.length && !memcmp(one.data, two.data, one.length));

    /* Spot checks of decoded values. */
    CHECK(a->part_count == 10 && a->stem_count == 6 && a->rule_count == 10);
    CHECK(!strcmp(a->part[5].id, "kick") && !strcmp(a->part[5].stem, "pads"));
    CHECK(a->part[3].pressure == 0.22f && a->part[3].gain == 2.2f);
    CHECK(a->part[7].detune_cents == 6.0f && a->part[7].unison == 2);
    const wb_rule *snare = &a->rule[9];
    CHECK(!strcmp(snare->track_name, "Drums") && snare->map_to == 52);
    CHECK(snare->velocity_num == 11 && snare->velocity_den == 20);
    CHECK(wb_noteset_has(&snare->notes, 37) && !wb_noteset_has(&snare->notes, 38));
    CHECK(a->rule[2].fold_below == 26 && a->rule[0].ring_max_bars == 1.5);
    const wb_groove *kick = &a->groove[0];
    CHECK(kick->pattern_count == 5 && kick->use_count == 7 && kick->hit_count == 1);
    CHECK(kick->pattern[3].step_count == 4 && kick->pattern[3].accent[0] == 6);
    CHECK(kick->wobble_a == 7 && kick->wobble_b == 3 && kick->wobble_m == 9);
    CHECK(a->curve[0].point_count == 10 && a->curve[0].point[7].value == 1.0f);
    CHECK(!strcmp(a->lane[5].curve, "darken") && a->lane[5].scale == -560.0f && a->lane[5].offset == 900.0f);
    CHECK(a->curve[1].shape == WB_SHAPE_SMOOTHSTEP && a->curve[2].point[0].bar == 94.0);
    CHECK(a->form.length_seconds == 283 && a->form.section_count == 8);

    /* CRLF and comments read the same session. */
    wb_buf crlf = { 0 };
    wb_buf_printf(&crlf, "# comment\r\n\r\n");
    for (size_t i = 0; i < size; i++) {
        if (text[i] == '\n') wb_buf_append(&crlf, "\r\n", 2);
        else wb_buf_append(&crlf, &text[i], 1);
    }
    wb_buf three = { 0 };
    CHECK(wb_read(crlf.data, crlf.length, b, &e) && wb_write(b, &three));
    CHECK(three.length == one.length && !memcmp(three.data, one.data, one.length));
    wb_buf_free(&crlf);
    wb_buf_free(&three);
    wb_buf_free(&one);
    wb_buf_free(&two);
    free(a);
    free(b);
    free(text);
}

/* A small valid session to mutate. */
static const char BASE[] =
    "mws=1\n[session]\nname=t\n[source]\nid=s\npath=sources/a.mid\n"
    "sha256=e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\n"
    "[form]\nstart_bar=0\nend_bar=8\n[stem]\nid=mix\n"
    "[part]\nid=p\npatch=patches/p.mapatch\nstem=mix\n";

static void expect_reject(const char *suffix, unsigned line, const char *needle) {
    wb_buf text = { 0 };
    wb_buf_printf(&text, "%s%s", BASE, suffix);
    wb_session *s = malloc(sizeof *s);
    wb_error e = { 0 };
    bool ok = wb_read(text.data, text.length, s, &e);
    CHECK(!ok);
    bool line_ok = e.line == line, text_ok = strstr(e.message, needle) != nullptr;
    CHECK(line_ok && text_ok);
    if (ok || !line_ok || !text_ok)
        fprintf(stderr, "  reject case %s -> line %u '%s'\n", suffix, e.line, e.message);
    free(s);
    wb_buf_free(&text);
}

static void test_strict_rejects(void) {
    wb_session *s = malloc(sizeof *s);
    wb_error e = { 0 };
    CHECK(wb_read(BASE, strlen(BASE), s, &e));
    CHECK(wb_session_validate(s, &e));
    /* BASE has 16 lines; the suffix starts at line 17. */
    expect_reject("[nope]\n", 17, "unknown section");
    expect_reject("[stem]\nid=x\nwhat=1\n", 19, "unknown key");
    expect_reject("[stem]\nid=x\nid=y\n", 19, "duplicate key");
    expect_reject("[stem]\n", 17, "missing required key 'id'");
    expect_reject("[part]\nid=q\npatch=a.mapatch\nstem=mix\nlimit=65\n", 21, "integer outside");
    expect_reject("[part]\nid=q\npatch=a.mapatch\nstem=mix\ngain=nan\n", 21, "finite number");
    expect_reject("[part]\nid=q\npatch=a.mapatch\nstem=mix\ngain=inf\n", 21, "finite number");
    expect_reject("[part]\nid=q\npatch=a.mapatch\nstem=mix\ngain=17\n", 21, "outside");
    expect_reject("[part]\nid=q\npatch=a.mapatch\nstem=mix\ngain= 1\n", 21, "finite number");
    expect_reject("[part]\nid=q\npatch=../a.mapatch\nstem=mix\n", 19, "relative");
    expect_reject("[part]\nid=q\npatch=/a.mapatch\nstem=mix\n", 19, "relative");
    expect_reject("[part]\nid=q w\n", 18, "a name is");
    expect_reject("[part]\nid=q\npatch=a\nstem=mix\nseed=12\n", 21, "seed is");
    expect_reject("[part]\nid=q\npatch=a\nstem=mix\nengine=piano\n", 21, "one of: mamut organ ep73");
    expect_reject("[session]\nname=again\n", 17, "duplicate [session]");
    expect_reject("[form]\nstart_bar=0\nend_bar=1\n", 17, "duplicate [form]");
    expect_reject("[source]\nid=t\npath=a.mid\nsha256=abc\n", 20, "64 lowercase hex");
    expect_reject("[source]\nid=t\npath=a.mid\ntempo=fast\n", 20, "tempo is");
    expect_reject("[rule]\npart=p\nfrom=s\n", 19, "from is");
    expect_reject("[rule]\npart=p\nfrom=s:\"\"\n", 19, "from is");
    expect_reject("[rule]\npart=p\nfrom=s:channel=17\n", 19, "from is");
    expect_reject("[rule]\npart=p\nfrom=s:*\nnotes=40-30\n", 20, "notes is");
    expect_reject("[rule]\npart=p\nfrom=s:*\nnotes=128\n", 20, "notes is");
    expect_reject("[rule]\npart=p\nfrom=s:*\nnotes=1,\n", 20, "notes is");
    expect_reject("[rule]\npart=p\nfrom=s:*\nvelocity_scale=0.55\n", 20, "exact ratio");
    expect_reject("[groove]\npart=p\nuse=0..4:x\n", 19, "before it is defined");
    expect_reject("[groove]\npart=p\npattern=a 4 2 vel=10\n", 19, "increasing steps");
    expect_reject("[groove]\npart=p\npattern=a 4 vel=10 accent5=+1\n", 19, "pattern is");
    expect_reject("[groove]\npart=p\npattern=a 4\n", 19, "pattern is");
    expect_reject("[groove]\npart=p\nhit=4 vel=0\n", 19, "hit is");
    expect_reject("[groove]\npart=p\nwobble=1,2,0\n", 19, "wobble is");
    expect_reject("[curve]\nid=c\npoint=1\n", 19, "point is");
    expect_reject("[lane]\npart=p\nparam=x\nclamp=1\n", 20, "clamp is");
    expect_reject("[marker]\nbar=1\nname=m\ntext= padded\n", 20, "edge blanks");
    expect_reject("[stem\n", 17, "malformed section");
    expect_reject("stray=1\n", 17, "unknown key 'stray' in [part]");

    /* Whole-file rejects. */
    CHECK(!wb_read("", 0, s, &e) && strstr(e.message, "missing version"));
    CHECK(!wb_read("mws=2\n", 6, s, &e) && e.line == 1);
    CHECK(!wb_read("mws=1\nx=1\n", 10, s, &e) && strstr(e.message, "outside a section"));
    CHECK(!wb_read("mws=1\n\0", 7, s, &e) && strstr(e.message, "NUL"));
    wb_buf long_line = { 0 };
    wb_buf_printf(&long_line, "mws=1\n[session]\nname=");
    for (unsigned i = 0; i < WB_LINE_MAX; i++) wb_buf_append(&long_line, "a", 1);
    CHECK(!wb_read(long_line.data, long_line.length, s, &e) && e.line == 3
          && strstr(e.message, "line exceeds"));
    wb_buf_free(&long_line);
    /* Capacity. */
    wb_buf many = { 0 };
    wb_buf_printf(&many, "%s", BASE);
    for (unsigned i = 0; i < WB_STEMS; i++) wb_buf_printf(&many, "[stem]\nid=s%u\n", i);
    CHECK(!wb_read(many.data, many.length, s, &e) && strstr(e.message, "more than 8 [stem]"));
    wb_buf_free(&many);
    free(s);
}

static void expect_invalid(const char *suffix, const char *needle) {
    wb_buf text = { 0 };
    wb_buf_printf(&text, "%s%s", BASE, suffix);
    wb_session *s = malloc(sizeof *s);
    wb_error e = { 0 };
    bool parsed = wb_read(text.data, text.length, s, &e);
    CHECK(parsed);
    if (!parsed) fprintf(stderr, "  parse failed: %u %s\n", e.line, e.message);
    bool valid = parsed && wb_session_validate(s, &e);
    CHECK(!valid && strstr(e.message, needle));
    if (valid || !strstr(e.message, needle))
        fprintf(stderr, "  invalid case %s -> '%s'\n", suffix, e.message);
    free(s);
    wb_buf_free(&text);
}

static void test_validate(void) {
    expect_invalid("[stem]\nid=empty\n", "stem 'empty' has no part");
    expect_invalid("[part]\nid=q\npatch=a\nstem=none\n", "undefined stem");
    expect_invalid("[part]\nid=p\npatch=a\nstem=mix\n", "duplicate part id");
    expect_invalid("[part]\nid=q\npatch=a\nstem=mix\nlimit=6\nunison=4\n", "does not divide");
    expect_invalid("[part]\nid=q\nengine=organ\npatch=a\nstem=mix\n[lane]\npart=q\nparam=filter.cutoff_hz\npoint=0 1\n",
                   "take only part.gain");
    expect_invalid("[stem]\nid=mix\n", "duplicate stem id");
    expect_invalid("[source]\nid=s\npath=b.mid\nsha256="
                   "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\n",
                   "duplicate source id");
    expect_invalid("[rule]\npart=x\nfrom=s:*\n", "undefined part");
    expect_invalid("[rule]\npart=p\nfrom=t:*\n", "undefined source");
    expect_invalid("[rule]\npart=p\nfrom=s:*\nvelocity_min=90\nvelocity_max=80\n", "velocity_min");
    expect_invalid("[rule]\npart=p\nfrom=s:*\nbar_in=4\nbar_out=4\n", "empty bar range");
    expect_invalid("[lane]\npart=p\nparam=filter.nope\npoint=0 1\n", "unknown parameter");
    expect_invalid("[lane]\npart=p\nparam=filter.cutoff_hz\n", "either points or a curve");
    expect_invalid("[curve]\nid=c\npoint=0 1\n[lane]\npart=p\nparam=part.gain\ncurve=c\npoint=0 1\n",
                   "either points or a curve");
    expect_invalid("[lane]\npart=p\nparam=part.gain\ncurve=nope\n", "undefined curve");
    expect_invalid("[lane]\npart=p\nparam=part.gain\npoint=2 1\npoint=1 1\n", "increasing bars");
    expect_invalid("[curve]\nid=c\n", "strictly increasing points");
    expect_invalid("[groove]\npart=p\nsteps_per_bar=8\npattern=a 9 vel=1\n", "outside 8 steps");
    expect_invalid("[groove]\npart=p\npattern=a 0 vel=1\nuse=4..8:a 2..6:a\n", "overlap");
    expect_invalid("[groove]\npart=p\nhit=5 vel=1\nhit=4 vel=1\n", "hits out of order");
    expect_invalid("[marker]\nbar=4\nname=b\n[marker]\nbar=2\nname=a\n", "bar order");

    /* Sections against the form. */
    wb_session *s = malloc(sizeof *s);
    wb_error e = { 0 };
    const char *form_cases[][2] = {
        { "section=a 0 4\nsection=b 3 8\n", "overlaps" },
        { "section=a 0 9\n", "outside the form" },
        { "section=a 2 2\n", "is empty" },
        { "section=a 0 2\nsection=a 2 4\n", "duplicate section" },
    };
    for (size_t i = 0; i < sizeof form_cases / sizeof *form_cases; i++) {
        wb_buf t = { 0 };
        wb_buf_printf(&t, "%s", BASE);
        /* Put the sections into the existing [form] block. */
        char *at = strstr(t.data, "end_bar=8\n") + strlen("end_bar=8\n");
        wb_buf u = { 0 };
        wb_buf_append(&u, t.data, (size_t)(at - t.data));
        wb_buf_printf(&u, "%s%s", form_cases[i][0], at);
        CHECK(wb_read(u.data, u.length, s, &e));
        CHECK(!wb_session_validate(s, &e) && strstr(e.message, form_cases[i][1]));
        wb_buf_free(&t);
        wb_buf_free(&u);
    }
    free(s);
}

static void test_keys(void) {
    size_t size;
    char *text = slurp(FIXTURE, &size);
    wb_session *s = malloc(sizeof *s);
    wb_error e = { 0 };
    CHECK(wb_read(text, size, s, &e));
    int vocal = wb_find_part(s, "vocal");
    CHECK(vocal == 3);
    CHECK(wb_set_key(s, WB_KIND_PART, 3, "gain", "1.75", &e) && s->part[3].gain == 1.75f);
    wb_buf v = { 0 };
    CHECK(wb_get_key(s, WB_KIND_PART, 3, "gain", &v) && !strcmp(v.data, "1.75"));
    CHECK(!wb_set_key(s, WB_KIND_PART, 3, "gain", "99", &e) && s->part[3].gain == 1.75f);
    CHECK(!wb_set_key(s, WB_KIND_PART, 3, "id", "", &e) && strstr(e.message, "required"));
    CHECK(!wb_set_key(s, WB_KIND_FORM, 0, "section", "x 1 2", &e) && strstr(e.message, "list"));
    /* Reset of an optional custom key. */
    CHECK(s->rule[9].has_notes);
    CHECK(wb_set_key(s, WB_KIND_RULE, 9, "notes", "", &e) && !s->rule[9].has_notes);
    CHECK(wb_set_key(s, WB_KIND_RULE, 9, "velocity_scale", "", &e)
          && s->rule[9].velocity_num == 1 && s->rule[9].velocity_den == 1);
    CHECK(wb_set_key(s, WB_KIND_RULE, 9, "notes", "36-38,40", &e)
          && wb_noteset_has(&s->rule[9].notes, 37) && !wb_noteset_has(&s->rule[9].notes, 39));
    wb_buf_reset(&v);
    CHECK(wb_get_key(s, WB_KIND_RULE, 9, "notes", &v) && !strcmp(v.data, "36-38,40"));
    CHECK(wb_set_key(s, WB_KIND_RULE, 0, "from", "sp:name=\"Guitar 2\",channel=3", &e));
    wb_buf_reset(&v);
    CHECK(wb_get_key(s, WB_KIND_RULE, 0, "from", &v)
          && !strcmp(v.data, "sp:name=\"Guitar 2\",channel=3"));
    CHECK(wb_set_key(s, WB_KIND_PART, 0, "seed", "0xdeadbeef", &e) && !s->part[0].seed_from_name);
    wb_buf_reset(&v);
    CHECK(wb_get_key(s, WB_KIND_PART, 0, "seed", &v) && !strcmp(v.data, "0x00000000deadbeef"));
    /* Optional names reset to unset; the lane then fails validation. */
    CHECK(wb_set_key(s, WB_KIND_LANE, 0, "curve", "", &e) && !s->lane[0].curve[0]);
    CHECK(!wb_session_validate(s, &e));
    wb_buf_free(&v);
    free(s);
    free(text);
}

static void test_objects(void) {
    wb_session *s = malloc(sizeof *s);
    wb_error e = { 0 };
    CHECK(wb_read(BASE, strlen(BASE), s, &e));
    CHECK(wb_read_object(s, WB_KIND_STEM, 0, true, "[stem]\nid=first\n", 16, &e));
    CHECK(s->stem_count == 2 && !strcmp(s->stem[0].id, "first") && !strcmp(s->stem[1].id, "mix"));
    CHECK(!wb_read_object(s, WB_KIND_STEM, 0, false, "[part]\nid=x\n", 12, &e));
    CHECK(!wb_read_object(s, WB_KIND_STEM, 0, false, "[stem]\nid=a\n[stem]\nid=b\n", 24, &e));
    CHECK(!wb_read_object(s, WB_KIND_STEM, 5, true, "[stem]\nid=a\n", 12, &e));
    CHECK(s->stem_count == 2);
    CHECK(wb_remove_object(s, WB_KIND_STEM, 0) && s->stem_count == 1 && !strcmp(s->stem[0].id, "mix"));
    CHECK(!wb_remove_object(s, WB_KIND_STEM, 3));
    free(s);
}

static void test_journal(void) {
    char dir[] = "/tmp/wb-test-XXXXXX";
    CHECK(mkdtemp(dir) != nullptr);
    char journal_path[256];
    snprintf(journal_path, sizeof journal_path, "%s/s.mws.journal", dir);
    size_t size;
    char *text = slurp(FIXTURE, &size);
    wb_session *s = malloc(sizeof *s), *original = malloc(sizeof *original);
    wb_error e = { 0 };
    CHECK(wb_read(text, size, s, &e));
    memcpy(original, s, sizeof *s);
    char sha0[65], sha[65];
    CHECK(wb_session_sha256(s, sha0));

    wb_journal j;
    wb_journal_init(&j);
    bool stale = true;
    CHECK(wb_journal_open(&j, journal_path, s, &stale, &e) && !stale && j.count == 0);
    CHECK(wb_edit_set_key(&j, s, WB_KIND_PART, 3, "gain", "2", &e));
    CHECK(wb_edit_set_key(&j, s, WB_KIND_PART, 3, "gain", "2", &e)); /* no-op */
    CHECK(j.count == 1);
    CHECK(wb_edit_insert(&j, s, WB_KIND_MARKER, 0, "[marker]\nbar=8\nname=start\n", &e));
    CHECK(wb_edit_remove(&j, s, WB_KIND_RULE, 9, &e) && s->rule_count == 9);
    CHECK(wb_edit_replace(&j, s, WB_KIND_STEM, 0, "[stem]\nid=tonal\n", &e)); /* no-op */
    CHECK(j.count == 3 && j.cursor == 3);
    CHECK(!wb_edit_set_key(&j, s, WB_KIND_PART, 3, "gain", "x", &e) && j.count == 3);

    /* Reopen: the journal replays to the same history. */
    wb_journal k;
    wb_journal_init(&k);
    CHECK(wb_journal_open(&k, journal_path, s, &stale, &e) && !stale);
    CHECK(k.count == 3 && k.cursor == 3);
    CHECK(wb_journal_undo(&k, s, &e) && s->rule_count == 10 && s->rule[9].map_to == 52);
    CHECK(wb_journal_undo(&k, s, &e) && s->marker_count == 1);
    CHECK(wb_journal_undo(&k, s, &e) && s->part[3].gain == 2.2f);
    CHECK(!wb_journal_undo(&k, s, &e));
    CHECK(wb_session_sha256(s, sha) && !strcmp(sha, sha0));
    CHECK(!memcmp(s, original, sizeof *s));
    CHECK(wb_journal_redo(&k, s, &e) && s->part[3].gain == 2.0f);
    wb_journal_free(&k);

    /* Reopen after undo/redo; then a new edit drops the redo tail. */
    wb_journal_init(&k);
    CHECK(wb_journal_open(&k, journal_path, s, &stale, &e) && !stale);
    CHECK(k.count == 3 && k.cursor == 1);
    CHECK(wb_edit_set_key(&k, s, WB_KIND_FORM, 0, "tail_seconds", "12", &e));
    CHECK(k.count == 2 && k.cursor == 2 && !wb_journal_can_redo(&k));
    wb_journal_free(&k);
    wb_journal_init(&k);
    CHECK(wb_journal_open(&k, journal_path, s, &stale, &e) && !stale && k.count == 2);
    wb_journal_free(&k);

    /* A session that moved on without the journal sets the journal aside. */
    s->part[0].gain = 0.5f;
    wb_journal_init(&k);
    CHECK(wb_journal_open(&k, journal_path, s, &stale, &e) && stale && k.count == 0);
    char aside[300];
    snprintf(aside, sizeof aside, "%s.stale", journal_path);
    struct stat st;
    CHECK(stat(aside, &st) == 0 && stat(journal_path, &st) != 0);
    CHECK(wb_journal_clear(&k, &e));
    wb_journal_free(&k);
    wb_journal_free(&j);
    remove(aside);
    rmdir(dir);
    free(s);
    free(original);
    free(text);
}

static void test_files(void) {
    char dir[] = "/tmp/wb-test-XXXXXX";
    CHECK(mkdtemp(dir) != nullptr);
    char path[512];
    snprintf(path, sizeof path, "%s/sources", dir);
    CHECK(mkdir(path, 0755) == 0);
    snprintf(path, sizeof path, "%s/patches", dir);
    CHECK(mkdir(path, 0755) == 0);
    /* A synthetic SMF: one empty format-0 track. */
    static const unsigned char SMF[] = {
        'M','T','h','d',0,0,0,6,0,0,0,1,1,0xe0,'M','T','r','k',0,0,0,4,0,0xff,0x2f,0,
    };
    char midi[512];
    snprintf(midi, sizeof midi, "%s/in.mid", dir);
    CHECK(wb_write_file_atomic(midi, (const char *)SMF, sizeof SMF));
    wb_session *s = malloc(sizeof *s);
    wb_error e = { 0 };
    CHECK(wb_read(BASE, strlen(BASE), s, &e));
    s->source_count = 0;
    CHECK(wb_add_source(s, dir, "in", midi, &e) && s->source_count == 1);
    CHECK(!strcmp(s->source[0].path, "sources/in.mid"));
    CHECK(wb_add_source(s, dir, "in2", midi, &e)); /* identical copy reused */
    CHECK(!wb_add_source(s, dir, "in2", midi, &e) && strstr(e.message, "already exists"));
    snprintf(path, sizeof path, "%s/sources/in.mid", dir);
    struct stat st;
    CHECK(stat(path, &st) == 0 && (st.st_mode & 0222) == 0);
    /* Patch missing, then present. */
    CHECK(!wb_check_files(s, dir, &e) && strstr(e.message, "part 'p'"));
    ma_patch_document doc = { .value = ma_patch_dubina };
    snprintf(doc.name, sizeof doc.name, "p");
    snprintf(path, sizeof path, "%s/patches/p.mapatch", dir);
    ma_patch_error pe;
    CHECK(ma_patch_save(path, &doc, &pe));
    CHECK(wb_check_files(s, dir, &e));
    /* A changed source no longer matches. */
    memcpy(s->source[0].sha256, "00", 2);
    CHECK(!wb_check_files(s, dir, &e) && strstr(e.message, "sha256"));
    char cleanup[600];
    snprintf(cleanup, sizeof cleanup, "rm -rf '%s'", dir);
    CHECK(system(cleanup) == 0);
    free(s);
}

#include "wb_test_w2w3.c"

int main(void) {
    test_sha256();
    test_numbers();
    test_fixture_round_trip();
    test_strict_rejects();
    test_validate();
    test_keys();
    test_objects();
    test_journal();
    test_files();
    test_timemap();
    test_orchestration();
    test_render();
    test_import();
    test_patch_chain();
    test_overlays();
    printf("workbench desk: %u checks, %u failed\n", checks, failures);
    return failures ? 1 : 0;
}
