/* MA3 Progenies of the Great Apocalypse (Dimmu Borgir), 2026-11-08: eight
 * Mamut parts, one per channel of notes-midi/local/progenies-20261108.mid
 * (driver/make_progenies.py), rendered in one pass by ma3_show.
 *   - two driven guitars on separately seeded pools (the transcription's two
 *     identical guitar tracks, double-tracked), bass;
 *   - strings, brass and a held choir from the keyboard tracks;
 *   - the MA3 Hurt kit as in driver/exhibit_ma3_mourning_palace.c: tonal
 *     kick on a one-slot choke pool and a pitched pulse.
 * One tempo, 140 BPM. Notes: docs/ma3-progenies.md. */
#include "ma3_show.h"
#include "ma_dark_lead.h"
#include "host_parse.h"
#include <math.h>
#include <stdio.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "ma3_hurt_material.h"
#include "ma3_hurt_parts.h"
#pragma GCC diagnostic pop

enum { P_GTR1, P_GTR2, P_BASS, P_STRINGS, P_BRASS, P_CHOIR, P_KICK, P_PULSE, P_PARTS };
enum { ST_GUITAR, ST_BASS, ST_STRINGS, ST_BRASS, ST_CHOIR, ST_RHYTHM, ST_COUNT };
enum { P_PPQ = 480, P_BAR = 4 * P_PPQ, P_US_PER_QUARTER = 428571, P_END_TICK = 308400,
       P_TAIL_SECONDS = 4 };
static const char *const P_NAMES[P_PARTS] = {
    "gtr1", "gtr2", "bass", "strings", "brass", "choir", "kick", "pulse",
};
static const char *const P_STEMS[ST_COUNT] = { "guitar", "bass", "strings", "brass", "choir", "rhythm" };

static ma3_show show;
static bool include[P_PARTS];
static unsigned show_seconds;

/* 48 kHz at 428571 us per 480-tick quarter: 42.8571 frames per tick. */
static size_t p_frame(uint64_t tick) { return (size_t)((tick * P_US_PER_QUARTER + 5000u) / 10000u); }
static double p_bar(size_t frame) { return (double)frame * 10000.0 / ((double)P_US_PER_QUARTER * P_BAR); }

/* Form energy over the transcription's sections: opening crash pattern,
 * verse beats (16, 24, 32), ride section (40), opening pattern again (48),
 * sextuplet kick (64), sparse bars (72, 80), kick runs (96, 104), the
 * orchestral interlude without band (113-128), the blast (129), and the
 * opening pattern to the end (145). */
static float energy(double bar) {
    static const float at[][2] = {
        {0, .55f}, {16, .70f}, {24, .75f}, {32, .80f}, {40, .85f}, {48, .55f},
        {64, .95f}, {72, .60f}, {76, .95f}, {80, .60f}, {96, .75f}, {104, .95f},
        {113, .25f}, {129, 1.0f}, {143, .70f}, {145, .60f}, {161, .50f},
    };
    constexpr unsigned n = sizeof at / sizeof at[0];
    if (bar <= at[0][0]) return at[0][1];
    for (unsigned i = 1; i < n; i++)
        if (bar < at[i][0])
            return at[i-1][1] + (at[i][1] - at[i-1][1]) * (float)((bar - at[i-1][0]) / (at[i][0] - at[i-1][0]));
    return at[n-1][1];
}

/* --- patches (guitar and bass as in exhibit_ma3_mourning_palace.c) --------- */

static ma_patch gtr_patch(bool second) {
    ma_patch p = tenor_patch(0);
    p.vco1.saw_level = .55f; p.vco1.pulse_level = .12f; p.vco1.pulse_width = second ? .38f : .42f;
    p.vco2.saw_level = .32f; p.vco2_level = .55f; p.vco2_fine_cents = second ? -9 : 9;
    p.raster_mix = .08f; p.bcs_amount = .40f; p.bcs_regime = .30f;
    p.mixer_pressure = .45f;
    p.filter_cutoff_hz = second ? 1350 : 1500; p.filter_resonance = .18f;
    p.filter_drive = .55f; p.filter_env_amount = .30f;
    p.amp_adsr = (ma_adsr){ 4, 300, .85f, 160 };
    p.filter_adsr = (ma_adsr){ 3, 260, .45f, 160 };
    p.macro[MA_MACRO_HEAT] = .30f; p.body_drive = .50f;
    p.width = .80f; p.master_level = .15f;
    return p;
}
static ma_patch bass_patch_driven(void) {
    ma_patch p = hurt_voice_patch(BASS);
    p.vco1.saw_level = .25f;
    p.filter_cutoff_hz = 520; p.filter_drive = .40f; p.mixer_pressure = .30f;
    p.body_drive = .35f;
    p.amp_adsr.release_ms = 220;
    return p;
}
/* Upper strings (MIDI 58-77) on the tenor voice, bowed attack. */
static ma_patch strings_patch(void) {
    ma_patch p = tenor_patch(-4);
    p.amp_adsr = (ma_adsr){ 90, 900, .78f, 1100 };
    p.filter_adsr = (ma_adsr){ 70, 1000, .42f, 1000 };
    p.filter_cutoff_hz = 1100; p.filter_keytrack = .20f;
    p.width = .70f;
    return p;
}
/* Brass: saw-forward, filter swell on each attack. */
static ma_patch brass_patch(void) {
    ma_patch p = tenor_patch(0);
    p.vco1.saw_level = .45f; p.vco1.triangle_level = .20f; p.vco1.sine_level = .20f;
    p.vco2.saw_level = .30f; p.vco2_fine_cents = 6;
    p.raster_mix = .04f; p.mozaik_mix = .03f;
    p.filter_cutoff_hz = 700; p.filter_resonance = .14f;
    p.filter_drive = .22f; p.filter_env_amount = .55f;
    p.amp_adsr = (ma_adsr){ 35, 500, .80f, 400 };
    p.filter_adsr = (ma_adsr){ 60, 400, .35f, 400 };
    p.body_drive = .20f; p.width = .45f; p.master_level = .14f;
    return p;
}
/* Held choir: the Hurt harmony organ voice (ma_patch_dubina), slow. */
static ma_patch choir_patch(void) {
    ma_patch p = hurt_voice_patch(HARMONY);
    p.amp_adsr = (ma_adsr){ 900, 1800, .75f, 3000 };
    p.filter_cutoff_hz = 900;
    return p;
}
static ma_patch part_patch(unsigned part) {
    switch (part) {
    case P_GTR1: return gtr_patch(false);
    case P_GTR2: return gtr_patch(true);
    case P_BASS: return bass_patch_driven();
    case P_STRINGS: return strings_patch();
    case P_BRASS: return brass_patch();
    case P_CHOIR: return choir_patch();
    case P_KICK: return drum_patch(KICK);
    default: return drum_patch(PULSE);
    }
}

static void controls(ma_voice_pool *p, const ma3_show *s, size_t frame) {
    (void)s;
    float e = energy(p_bar(frame));
    ma_voice_pool_set_macro(&p[P_GTR1], MA_MACRO_HEAT, .20f + .25f * e);
    ma_voice_pool_set_macro(&p[P_GTR2], MA_MACRO_HEAT, .20f + .25f * e);
    ma_voice_pool_set_macro(&p[P_CHOIR], MA_MACRO_BLOOM, .05f + .12f * e);
    ma_voice_pool_set_lfo(&p[P_CHOIR], .008f, .08f);
    ma_voice_pool_set_mod_wheel(&p[P_BRASS], 0, .04f + .10f * e);
}

static void parts_init(ma3_show *s) {
    const unsigned limits[] = { 16, 16, 4, 8, 8, 8, 1, 4 };
    const unsigned unison[] = { 2, 2, 1, 2, 2, 2, 1, 1 };
    const unsigned stems[] = { ST_GUITAR, ST_GUITAR, ST_BASS, ST_STRINGS, ST_BRASS, ST_CHOIR,
                               ST_RHYTHM, ST_RHYTHM };
    /* From a first full pass at 1/1/1.59/1.70/1.50/1.90/1.1/1.7 (the balanced
     * Mourning Palace guitars, bass and kit) and its per-section stem RMS
     * (docs/ma3-progenies.md): strings +3 dB, brass +6 dB, choir +2 dB. */
    const float gain[] = { 1.0f, 1.0f, 1.59f, 2.40f, 2.99f, 2.39f, 1.1f, 1.7f };
    const float ch[] = { .14f, .14f, 0, .16f, .12f, .28f, 0, .06f };
    const float rv[] = { .16f, .16f, .05f, .24f, .20f, .32f, .04f, .14f };
    for (unsigned p = 0; p < P_PARTS; p++) {
        ma3_part_init(&s->part[p], P_NAMES[p], part_patch(p), limits[p], unison[p], stems[p],
                      gain[p], ch[p], rv[p]);
        s->part[p].config.detune_cents = p == P_GTR1 ? 7 : p == P_GTR2 ? 9 : 4;
    }
}

static bool load_score(ma3_show *s, const smf_file *m, const char **reason) {
    if (m->format != 1 || m->division != P_PPQ || m->tempo_count != 1
        || m->tempos[0].tick != 0 || m->tempos[0].us_per_quarter != P_US_PER_QUARTER) {
        *reason = "MIDI is not the 2026-11-08 Progenies layout";
        return false;
    }
    unsigned attacks[P_PARTS] = { 0 };
    for (size_t i = 0; i < m->event_count; i++) {
        const smf_event *e = &m->events[i];
        unsigned type = e->status & 0xf0u, channel = e->status & 15u;
        if ((type != 0x80u && type != 0x90u) || channel >= P_PARTS || e->tick > P_END_TICK) {
            *reason = "unexpected event in the Progenies score";
            return false;
        }
        bool on = type == 0x90u && e->d2;
        attacks[channel] += on;
        if (!include[channel]) continue;
        if (!ma3_add_event(s, (ma3_event){ .frame = p_frame(e->tick), .part = (uint8_t)channel,
                .note = e->d1, .velocity = on ? e->d2 : 0, .on = on, .pressure = .12f })) {
            *reason = "event capacity exceeded";
            return false;
        }
    }
    for (unsigned p = 0; p < P_PARTS; p++)
        if (!attacks[p]) { *reason = "a part has no notes"; return false; }
    return true;
}

static bool select_parts(const char *list) {
    for (unsigned c = 0; c < P_PARTS; c++) include[c] = false;
    while (*list) {
        size_t n = strcspn(list, ",");
        bool found = false;
        for (unsigned c = 0; c < P_PARTS; c++)
            if (strlen(P_NAMES[c]) == n && !strncmp(list, P_NAMES[c], n)) include[c] = found = true;
        if (!found) return false;
        list += n + (list[n] == ',');
    }
    return true;
}

int main(int argc, char **argv) {
    const char *input = "notes-midi/local/progenies-20261108.mid", *prefix = NULL, *parts = NULL;
    show_seconds = (unsigned)((p_frame(P_END_TICK) + MA3_RATE - 1) / MA3_RATE) + P_TAIL_SECONDS;
    unsigned seconds = 0;
    bool check = false;
    for (unsigned c = 0; c < P_PARTS; c++) include[c] = true;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--render") && i + 1 < argc) prefix = argv[++i];
        else if (!strcmp(argv[i], "-i") && i + 1 < argc) input = argv[++i];
        else if (!strcmp(argv[i], "--parts") && i + 1 < argc) parts = argv[++i];
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) {
            uint64_t n = 0;
            if (!host_parse_u64(argv[++i], 1, show_seconds, &n)) return 2;
            seconds = (unsigned)n;
        } else if (!strcmp(argv[i], "--check")) check = true;
        else {
            fprintf(stderr, "Usage: %s --check | --render PREFIX [--seconds 1..%u] "
                    "[--parts gtr1,gtr2,bass,strings,brass,choir,kick,pulse] [-i score.mid]\n",
                    argv[0], show_seconds);
            return 2;
        }
    }
    if ((check && prefix) || (seconds && !prefix) || (!check && !prefix)) return 2;
    if (parts && !select_parts(parts)) return 2;
    show = (ma3_show){ .name = "MA3 Progenies 2026-11-08", .part_count = P_PARTS,
        .stem_count = ST_COUNT, .seconds = show_seconds, .controls = controls };
    for (unsigned st = 0; st < ST_COUNT; st++) show.stem_name[st] = P_STEMS[st];
    parts_init(&show);
    smf_file midi = { 0 };
    uint8_t *bytes = NULL;
    const char *reason = NULL;
    bool ok = midi_load(&midi, &bytes, input, 0x00ffu, &reason) && load_score(&show, &midi, &reason)
        && ma3_check(&show);
    if (ok) {
        printf("  routing:");
        for (unsigned p = 0; p < P_PARTS; p++) printf(" %s->%s", P_NAMES[p], P_STEMS[show.part[p].stem]);
        printf("\n");
    }
    if (ok && prefix) ok = ma3_render(&show, prefix, seconds);
    if (!ok) fprintf(stderr, "Progenies: %s\n", reason ? reason : "score/render validation failed");
    smf_dispose(&midi);
    free(bytes);
    return ok ? 0 : 1;
}
