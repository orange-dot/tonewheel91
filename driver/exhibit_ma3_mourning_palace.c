/* MA3 Mourning Palace (Dimmu Borgir), 2026-11-08: nine Mamut parts, one per
 * channel of notes-midi/local/mourning-palace-20261108.mid
 * (driver/make_mourning_palace.py), rendered in one pass by ma3_show.
 *   - two driven guitars (the transcription's Guitar I and II, double-tracked
 *     on separately seeded pools), bass;
 *   - the four keyboard tracks: low lead, low strings, held pad, sixteenth
 *     figure;
 *   - the MA3 Hurt kit: tonal kick and pitched pulse (driver/ma3_hurt_material.h
 *     drum_patch). The kick plays on a one-slot pool, so each hit chokes the
 *     previous one through the pool's 5 ms steal crossfade; double-kick runs
 *     keep every attack instead of piling up same-pitch tails.
 * The source tempo map (120, 195, 120, 180 BPM) is applied exactly.
 * Notes: docs/ma3-mourning-palace.md. */
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

enum { M_GTR1, M_GTR2, M_BASS, M_SYNTH, M_STRINGS, M_PAD, M_PIZZ, M_KICK, M_PULSE, M_PARTS };
enum { ST_GUITAR, ST_BASS, ST_KEYS, ST_PADS, ST_PIZZ, ST_RHYTHM, ST_COUNT };
enum { M_PPQ = 480, M_BAR = 4 * M_PPQ, M_TEMPOS = 4, M_END_TICK = 348960, M_TAIL_SECONDS = 4 };
static const char *const M_NAMES[M_PARTS] = {
    "gtr1", "gtr2", "bass", "synth", "strings", "pad", "pizz", "kick", "pulse",
};
static const char *const M_STEMS[ST_COUNT] = { "guitar", "bass", "keys", "pads", "pizz", "rhythm" };
static const uint64_t TEMPO_TICK[M_TEMPOS] = { 0, 178080, 193440, 224160 };
static const uint64_t TEMPO_US[M_TEMPOS] = { 500000, 307692, 500000, 333333 };

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

/* Form energy over the transcription's sections: keyboard intro, verse
 * beat, first double-kick section, second verse, second double-kick
 * section, half-time with the china, the fast passage, sextuplet kick,
 * the break, 180 BPM double kick, the sparse china bars and the closing
 * blast. */
static float energy(double bar) {
    static const float at[][2] = {
        {0, .15f}, {5, .45f}, {24, .75f}, {36, .50f}, {49, .80f}, {61, .40f},
        {63, .55f}, {75, .70f}, {84, .60f}, {93, .80f}, {101, .85f}, {117, .40f},
        {125, .90f}, {149, .60f}, {157, .70f}, {165, 1.0f}, {182, 1.0f},
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
static ma_patch synth_patch(void) {
    ma_patch p = ma_dark_lead_patch();
    p.vco1.saw_level = .20f; p.vco2.saw_level = .10f;
    p.filter_cutoff_hz = 1200;
    p.amp_adsr = (ma_adsr){ 30, 600, .78f, 700 };
    p.filter_adsr = (ma_adsr){ 25, 700, .40f, 800 };
    p.bcs_amount = .12f; p.bcs_regime = .28f;
    return p;
}
static ma_patch strings_patch(void) {
    ma_patch p = tenor_patch(-4);
    p.amp_adsr = (ma_adsr){ 70, 900, .75f, 900 };
    p.filter_adsr = (ma_adsr){ 60, 1000, .40f, 900 };
    p.filter_cutoff_hz = 820;
    return p;
}
static ma_patch pad_patch_held(void) {
    ma_patch p = hurt_voice_patch(PAD);
    p.amp_adsr.release_ms = 2500;
    return p;
}
static ma_patch pizz_patch(void) {
    ma_patch p = pluck_patch(0);
    p.vco1 = (ma_vco_controls){ .saw_level = .08f, .triangle_level = .40f, .sine_level = .52f, .pulse_width = .5f };
    p.vco2 = (ma_vco_controls){ .triangle_level = .20f, .sine_level = .70f, .pulse_width = .5f };
    p.vco2_level = .26f; p.vco2_interval = 12; p.vco2_fine_cents = 1.5f;
    p.noise_level = .004f; p.mozaik_mix = .03f; p.bcs_amount = .05f;
    p.filter_cutoff_hz = 1350; p.filter_resonance = .12f; p.filter_env_amount = .35f;
    p.filter_keytrack = .25f; p.filter_drive = .12f;
    p.amp_adsr = (ma_adsr){ 3, 700, .15f, 600 };
    p.filter_adsr = (ma_adsr){ 2, 500, .20f, 500 };
    p.body_drive = .12f; p.width = .50f; p.master_level = .15f;
    return p;
}
static ma_patch part_patch(unsigned part) {
    switch (part) {
    case M_GTR1: return gtr_patch(false);
    case M_GTR2: return gtr_patch(true);
    case M_BASS: return bass_patch_driven();
    case M_SYNTH: return synth_patch();
    case M_STRINGS: return strings_patch();
    case M_PAD: return pad_patch_held();
    case M_PIZZ: return pizz_patch();
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
    ma_voice_pool_set_macro(&p[M_PAD], MA_MACRO_BLOOM, .05f + .12f * e);
    ma_voice_pool_set_lfo(&p[M_PAD], .008f, .08f);
    ma_voice_pool_set_mod_wheel(&p[M_SYNTH], 0, .06f + .12f * e);
}

static void parts_init(ma3_show *s) {
    /* Limits cover held peak x unison (ma3_check); released tails above that
     * are stolen and reported. The kick's single slot is the choke. */
    const unsigned limits[] = { 16, 16, 4, 8, 8, 16, 8, 1, 4 };
    const unsigned unison[] = { 2, 2, 1, 2, 2, 2, 1, 1, 1 };
    const unsigned stems[] = { ST_GUITAR, ST_GUITAR, ST_BASS, ST_KEYS, ST_KEYS, ST_PADS, ST_PIZZ,
                               ST_RHYTHM, ST_RHYTHM };
    /* Balanced from per-section stem RMS of a first full pass at gains
     * 1/1/2/1.2/1.2/1.6/1.2/1.1/1.7 (docs/ma3-mourning-palace.md): keys
     * +3 dB, pad +1.5 dB, pizz +5 dB, bass -2 dB; guitars and kit kept. */
    const float gain[] = { 1.0f, 1.0f, 1.59f, 1.70f, 1.70f, 1.90f, 2.13f, 1.1f, 1.7f };
    const float ch[] = { .14f, .14f, 0, .16f, .12f, .28f, .10f, 0, .06f };
    const float rv[] = { .16f, .16f, .05f, .24f, .20f, .30f, .24f, .04f, .14f };
    for (unsigned p = 0; p < M_PARTS; p++) {
        ma3_part_init(&s->part[p], M_NAMES[p], part_patch(p), limits[p], unison[p], stems[p],
                      gain[p], ch[p], rv[p]);
        s->part[p].config.detune_cents = p == M_GTR1 ? 7 : p == M_GTR2 ? 9 : p == M_SYNTH ? 6 : 4;
    }
}

static bool load_score(ma3_show *s, const smf_file *m, const char **reason) {
    bool layout = m->format == 1 && m->division == M_PPQ && m->tempo_count == M_TEMPOS;
    for (unsigned i = 0; layout && i < M_TEMPOS; i++)
        layout = m->tempos[i].tick == TEMPO_TICK[i] && m->tempos[i].us_per_quarter == TEMPO_US[i];
    if (!layout) { *reason = "MIDI is not the 2026-11-08 Mourning Palace layout"; return false; }
    unsigned attacks[M_PARTS] = { 0 };
    for (size_t i = 0; i < m->event_count; i++) {
        const smf_event *e = &m->events[i];
        unsigned type = e->status & 0xf0u, channel = e->status & 15u;
        if ((type != 0x80u && type != 0x90u) || channel >= M_PARTS || e->tick > M_END_TICK) {
            *reason = "unexpected event in the Mourning Palace score";
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
    const char *input = "notes-midi/local/mourning-palace-20261108.mid", *prefix = NULL, *parts = NULL;
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
                    "[--parts gtr1,gtr2,bass,synth,strings,pad,pizz,kick,pulse] [-i score.mid]\n",
                    argv[0], show_seconds);
            return 2;
        }
    }
    if ((check && prefix) || (seconds && !prefix) || (!check && !prefix)) return 2;
    if (parts && !select_parts(parts)) return 2;
    show = (ma3_show){ .name = "MA3 Mourning Palace 2026-11-08", .part_count = M_PARTS,
        .stem_count = ST_COUNT, .seconds = show_seconds, .controls = controls };
    for (unsigned st = 0; st < ST_COUNT; st++) show.stem_name[st] = M_STEMS[st];
    parts_init(&show);
    smf_file midi = { 0 };
    uint8_t *bytes = NULL;
    const char *reason = NULL;
    bool ok = midi_load(&midi, &bytes, input, 0x01ffu, &reason) && load_score(&show, &midi, &reason)
        && ma3_check(&show);
    if (ok) {
        printf("  routing:");
        for (unsigned p = 0; p < M_PARTS; p++) printf(" %s->%s", M_NAMES[p], M_STEMS[show.part[p].stem]);
        printf("\n");
    }
    if (ok && prefix) ok = ma3_render(&show, prefix, seconds);
    if (!ok) fprintf(stderr, "Mourning Palace: %s\n", reason ? reason : "score/render validation failed");
    smf_dispose(&midi);
    free(bytes);
    return ok ? 0 : 1;
}
