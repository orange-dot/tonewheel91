/* MA3 Spellbound (by the Devil) (Dimmu Borgir), 2026-11-08: eight Mamut
 * parts, one per channel of notes-midi/local/spellbound-20261108.mid
 * (driver/make_spellbound.py), rendered in one pass by ma3_show.
 *   - two driven guitars on separately seeded pools, bass (as in
 *     driver/exhibit_ma3_mourning_palace.c);
 *   - the keyboard tracks: choir, low held drone, and the organ lines on a
 *     fast-gated Mamut organ voice;
 *   - the MA3 Hurt kit: tonal kick on a one-slot choke pool, pitched pulse.
 * Tempo map 150, 140, 110 BPM, applied exactly. Notes: docs/ma3-spellbound.md. */
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

enum { M_GTR1, M_GTR2, M_BASS, M_CHOIR, M_DRONE, M_ORGAN, M_KICK, M_PULSE, M_PARTS };
enum { ST_GUITAR, ST_BASS, ST_CHOIR, ST_PADS, ST_ORGAN, ST_RHYTHM, ST_COUNT };
enum { M_PPQ = 480, M_BAR = 4 * M_PPQ, M_TEMPOS = 3, M_END_TICK = 265010, M_TAIL_SECONDS = 4 };
static const char *const M_NAMES[M_PARTS] = {
    "gtr1", "gtr2", "bass", "choir", "drone", "organ", "kick", "pulse",
};
static const char *const M_STEMS[ST_COUNT] = { "guitar", "bass", "choir", "pads", "organ", "rhythm" };
static const uint64_t TEMPO_TICK[M_TEMPOS] = { 0, 53760, 172800 };
static const uint64_t TEMPO_US[M_TEMPOS] = { 400000, 428571, 545454 };

static ma3_show show;
static bool include[M_PARTS];
static unsigned show_seconds;

/* Exact tempo-map position: at 48 kHz and 480 PPQ a tick at U us per quarter
 * is U/10000 frames, so frames = sum(ticks * U) / 10000, rounded once. */
static uint64_t scaled(uint64_t tick) {
    uint64_t s = 0;
    for (unsigned i = 0; i < M_TEMPOS; i++) {
        uint64_t a = TEMPO_TICK[i], b = i + 1 < M_TEMPOS ? TEMPO_TICK[i + 1] : UINT64_MAX;
        if (tick <= a) break;
        s += ((tick < b ? tick : b) - a) * TEMPO_US[i];
    }
    return s;
}
static size_t m_frame(uint64_t tick) { return (size_t)((scaled(tick) + 5000u) / 10000u); }
/* Source bar (fractional) at a frame. */
static double m_bar(size_t frame) {
    double s = (double)frame * 10000.0;
    for (unsigned i = M_TEMPOS; i-- > 0;) {
        double at = (double)scaled(TEMPO_TICK[i]);
        if (s >= at) return ((double)TEMPO_TICK[i] + (s - at) / (double)TEMPO_US[i]) / M_BAR;
    }
    return 0;
}

/* Form energy over the transcription's sections: keyboard intro, double
 * kick at 150 BPM (4), backbeat (12), double kick (20), the 140 BPM break
 * (28), ride and hat sections (30-41), sparse bars with the organ entry
 * (42), the blast (46), sextuplet kick (50), sparse and backbeat bars
 * (54-73), china section (74), the 110 BPM keyboard interlude (90), the
 * return (98) and the closing sextuplet kick (114-137). */
static float energy(double bar) {
    static const float at[][2] = {
        {0, .30f}, {4, .80f}, {12, .55f}, {20, .80f}, {28, .40f}, {30, .60f},
        {34, .80f}, {38, .60f}, {42, .45f}, {46, 1.0f}, {50, .95f}, {54, .45f},
        {58, .60f}, {62, .65f}, {74, .80f}, {82, .65f}, {90, .25f}, {98, .70f},
        {106, .75f}, {114, .95f}, {122, 1.0f}, {138, 1.0f},
    };
    constexpr unsigned n = sizeof at / sizeof at[0];
    if (bar <= at[0][0]) return at[0][1];
    for (unsigned i = 1; i < n; i++)
        if (bar < at[i][0])
            return at[i-1][1] + (at[i][1] - at[i-1][1]) * (float)((bar - at[i-1][0]) / (at[i][0] - at[i-1][0]));
    return at[n-1][1];
}

/* --- patches --------------------------------------------------------------- */

/* Driven saw guitar (the Hurt industrial guitar) with a short release so
 * sixteenth chugs stay separate. The second guitar is a little darker and
 * detuned the other way, for the double-tracked spread. */
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
/* Choir: the Hurt pad voice with a faster bloom for 640-tick chords. */
static ma_patch choir_patch(void) {
    ma_patch p = hurt_voice_patch(PAD);
    p.amp_adsr = (ma_adsr){ 180, 1200, .80f, 1200 };
    p.filter_cutoff_hz = 900;
    return p;
}
/* Low held drone (MIDI 37-45): the Hurt harmony voice. */
static ma_patch drone_patch(void) {
    ma_patch p = hurt_voice_patch(HARMONY);
    p.amp_adsr = (ma_adsr){ 600, 1800, .80f, 2500 };
    return p;
}
/* Organ lines on eighths and sixteenths: sine/triangle with an octave
 * VCO2, a little saw for bite, gate-shaped (fast on, full sustain, short
 * release). */
static ma_patch organ_patch(void) {
    ma_patch p = ma_patch_tepih;
    p.vco1 = (ma_vco_controls){ .saw_level = .10f, .triangle_level = .35f, .sine_level = .60f, .pulse_width = .5f };
    p.vco2 = (ma_vco_controls){ .triangle_level = .15f, .sine_level = .70f, .pulse_width = .5f };
    p.vco2_level = .45f; p.vco2_interval = 12; p.vco2_fine_cents = 0;
    p.sync_amount = 0; p.crossmod_amount = 0; p.noise_level = .004f;
    p.raster_mix = 0; p.mozaik_mix = 0; p.bcs_amount = .05f; p.bcs_regime = .15f;
    p.filter_cutoff_hz = 1800; p.filter_resonance = .08f; p.filter_keytrack = .30f;
    p.filter_drive = .10f; p.filter_env_amount = .10f;
    p.amp_adsr = (ma_adsr){ 5, 150, .92f, 140 };
    p.filter_adsr = (ma_adsr){ 3, 200, .80f, 150 };
    for (unsigned i = 0; i < MA_MACRO_COUNT; i++) p.macro[i] = 0;
    p.body_drive = .12f; p.width = .55f; p.crossfeed = .10f; p.master_level = .14f;
    return p;
}
static ma_patch part_patch(unsigned part) {
    switch (part) {
    case M_GTR1: return gtr_patch(false);
    case M_GTR2: return gtr_patch(true);
    case M_BASS: return bass_patch_driven();
    case M_CHOIR: return choir_patch();
    case M_DRONE: return drone_patch();
    case M_ORGAN: return organ_patch();
    case M_KICK: return drum_patch(KICK);
    default: return drum_patch(PULSE);
    }
}

static void controls(ma_voice_pool *p, const ma3_show *s, size_t frame) {
    (void)s;
    double bar = m_bar(frame);
    float e = energy(bar);
    ma_voice_pool_set_macro(&p[M_GTR1], MA_MACRO_HEAT, .20f + .25f * e);
    ma_voice_pool_set_macro(&p[M_GTR2], MA_MACRO_HEAT, .20f + .25f * e);
    ma_voice_pool_set_macro(&p[M_CHOIR], MA_MACRO_BLOOM, .05f + .12f * e);
    ma_voice_pool_set_lfo(&p[M_CHOIR], .008f, .08f);
    ma_voice_pool_set_lfo(&p[M_DRONE], .006f, .10f);
}

static void parts_init(ma3_show *s) {
    /* Limits cover held peak x unison (ma3_check); released tails above that
     * are stolen and reported. The kick's single slot is the choke. */
    const unsigned limits[] = { 16, 16, 4, 16, 8, 8, 1, 4 };
    const unsigned unison[] = { 2, 2, 1, 2, 2, 1, 1, 1 };
    const unsigned stems[] = { ST_GUITAR, ST_GUITAR, ST_BASS, ST_CHOIR, ST_PADS, ST_ORGAN,
                               ST_RHYTHM, ST_RHYTHM };
    /* From a first full pass at 1/1/1.59/1.90/1.90/1.70/1.1/1.7 (the balanced
     * Mourning Palace guitars, bass and kit) and its per-section stem RMS
     * (docs/ma3-spellbound.md): choir +2 dB, organ -1 dB. */
    const float gain[] = { 1.0f, 1.0f, 1.59f, 2.39f, 1.90f, 1.51f, 1.1f, 1.7f };
    const float ch[] = { .14f, .14f, 0, .28f, .20f, .12f, 0, .06f };
    const float rv[] = { .16f, .16f, .05f, .30f, .32f, .22f, .04f, .14f };
    for (unsigned p = 0; p < M_PARTS; p++) {
        ma3_part_init(&s->part[p], M_NAMES[p], part_patch(p), limits[p], unison[p], stems[p],
                      gain[p], ch[p], rv[p]);
        s->part[p].config.detune_cents = p == M_GTR1 ? 7 : p == M_GTR2 ? 9 : 4;
    }
}

static bool load_score(ma3_show *s, const smf_file *m, const char **reason) {
    bool layout = m->format == 1 && m->division == M_PPQ && m->tempo_count == M_TEMPOS;
    for (unsigned i = 0; layout && i < M_TEMPOS; i++)
        layout = m->tempos[i].tick == TEMPO_TICK[i] && m->tempos[i].us_per_quarter == TEMPO_US[i];
    if (!layout) { *reason = "MIDI is not the 2026-11-08 Spellbound layout"; return false; }
    unsigned attacks[M_PARTS] = { 0 };
    for (size_t i = 0; i < m->event_count; i++) {
        const smf_event *e = &m->events[i];
        unsigned type = e->status & 0xf0u, channel = e->status & 15u;
        if ((type != 0x80u && type != 0x90u) || channel >= M_PARTS || e->tick > M_END_TICK) {
            *reason = "unexpected event in the Spellbound score";
            return false;
        }
        bool on = type == 0x90u && e->d2;
        attacks[channel] += on;
        if (!include[channel]) continue;
        if (!ma3_add_event(s, (ma3_event){ .frame = m_frame(e->tick), .part = (uint8_t)channel,
                .note = e->d1, .velocity = on ? e->d2 : 0, .on = on, .pressure = .12f })) {
            *reason = "event capacity exceeded";
            return false;
        }
    }
    for (unsigned p = 0; p < M_PARTS; p++)
        if (!attacks[p]) { *reason = "a part has no notes"; return false; }
    return true;
}

static bool select_parts(const char *list) {
    for (unsigned c = 0; c < M_PARTS; c++) include[c] = false;
    while (*list) {
        size_t n = strcspn(list, ",");
        bool found = false;
        for (unsigned c = 0; c < M_PARTS; c++)
            if (strlen(M_NAMES[c]) == n && !strncmp(list, M_NAMES[c], n)) include[c] = found = true;
        if (!found) return false;
        list += n + (list[n] == ',');
    }
    return true;
}

int main(int argc, char **argv) {
    const char *input = "notes-midi/local/spellbound-20261108.mid", *prefix = NULL, *parts = NULL;
    show_seconds = (unsigned)((m_frame(M_END_TICK) + MA3_RATE - 1) / MA3_RATE) + M_TAIL_SECONDS;
    unsigned seconds = 0;
    bool check = false;
    for (unsigned c = 0; c < M_PARTS; c++) include[c] = true;
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
                    "[--parts gtr1,gtr2,bass,choir,drone,organ,kick,pulse] [-i score.mid]\n",
                    argv[0], show_seconds);
            return 2;
        }
    }
    if ((check && prefix) || (seconds && !prefix) || (!check && !prefix)) return 2;
    if (parts && !select_parts(parts)) return 2;
    show = (ma3_show){ .name = "MA3 Spellbound 2026-11-08", .part_count = M_PARTS,
        .stem_count = ST_COUNT, .seconds = show_seconds, .controls = controls };
    for (unsigned st = 0; st < ST_COUNT; st++) show.stem_name[st] = M_STEMS[st];
    parts_init(&show);
    smf_file midi = { 0 };
    uint8_t *bytes = NULL;
    const char *reason = NULL;
    bool ok = midi_load(&midi, &bytes, input, 0x00ffu, &reason) && load_score(&show, &midi, &reason)
        && ma3_check(&show);
    if (ok) {
        printf("  routing:");
        for (unsigned p = 0; p < M_PARTS; p++) printf(" %s->%s", M_NAMES[p], M_STEMS[show.part[p].stem]);
        printf("\n");
    }
    if (ok && prefix) ok = ma3_render(&show, prefix, seconds);
    if (!ok) fprintf(stderr, "Spellbound: %s\n", reason ? reason : "score/render validation failed");
    smf_dispose(&midi);
    free(bytes);
    return ok ? 0 : 1;
}
