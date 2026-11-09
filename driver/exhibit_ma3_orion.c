/* MA3 Orion (Metallica), 2026-11-08: ten Mamut parts, one per channel of
 * notes-midi/local/orion-20261108.mid (driver/make_orion.py), rendered in
 * one pass by ma3_show.
 *   - the three melody/solo guitars on the orchestration lead voice
 *     (driver/exhibit_ma3_hurt_orch.c), each on its own pool, so the
 *     harmonized lines of the slow middle section stay three voices;
 *   - the doubled bass as a lead bass (brighter, more drive) and a picked
 *     bass; the rhythm guitar and the machine-gun guitar on the driven
 *     guitar voices of driver/exhibit_ma3_mourning_palace.c; the chord
 *     tracks on its tenor strings voice;
 *   - the MA3 Hurt kit: tonal kick on a one-slot choke pool, pitched pulse.
 * The score is humanized (timing and velocity) by the generator; the mix is
 * as wet as The Cry of Mankind edition 2. Tempo map 133/83/133 BPM, applied
 * exactly. Notes: docs/ma3-orion.md. */
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

enum { O_LEAD1, O_LEAD2, O_LEAD3, O_BASSL, O_BASSR, O_GTR1, O_GTR2, O_CHORDS, O_KICK, O_PULSE, O_PARTS };
enum { ST_LEAD, ST_BASS, ST_GUITAR, ST_CHORDS, ST_RHYTHM, ST_COUNT };
enum { M_PPQ = 480, M_BAR = 4 * M_PPQ, M_TEMPOS = 3, M_END_TICK = 432121, M_TAIL_SECONDS = 4 };
static const char *const O_NAMES[O_PARTS] = {
    "lead1", "lead2", "lead3", "bassl", "bassr", "gtr1", "gtr2", "chords", "kick", "pulse",
};
static const char *const O_STEMS[ST_COUNT] = { "lead", "bass", "guitar", "chords", "rhythm" };
static const uint64_t TEMPO_TICK[M_TEMPOS] = { 0, 236160, 346080 };
static const uint64_t TEMPO_US[M_TEMPOS] = { 451127, 722891, 451127 };

static ma3_show show;
static bool include[O_PARTS];
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

/* Form energy over the derived bars (source bar - 4): the fade-in of the
 * opening chords and bass, drums (14), rhythm guitar (32), machine-gun
 * section (56), first melody (96), the break (118), the slow middle section
 * at 83 BPM (123) with its harmonized lines (130) and chords (171), back to
 * 133 BPM (181), the closing double kick (209) and the fade (223). */
static float energy(double bar) {
    static const float at[][2] = {
        {0, .15f}, {14, .45f}, {32, .65f}, {56, .85f}, {88, .70f}, {96, .75f},
        {118, .45f}, {123, .30f}, {130, .40f}, {171, .55f}, {181, .80f}, {201, .85f},
        {209, 1.0f}, {223, .60f}, {226, .30f},
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
static ma_patch strings_patch(void) {
    ma_patch p = tenor_patch(-4);
    p.amp_adsr = (ma_adsr){ 70, 900, .75f, 900 };
    p.filter_adsr = (ma_adsr){ 60, 1000, .40f, 900 };
    p.filter_cutoff_hz = 820;
    return p;
}
static ma_patch lead_patch(void) {
    ma_patch p = ma_dark_lead_patch();
    p.vco1.saw_level = .18f; p.vco2.saw_level = .08f;
    p.filter_cutoff_hz = 1300;
    p.amp_adsr = (ma_adsr){ 25, 600, .75f, 900 };
    p.filter_adsr = (ma_adsr){ 20, 700, .40f, 900 };
    p.bcs_amount = .12f; p.bcs_regime = .28f;
    return p;
}
/* Lead bass: the driven bass, brighter and harder, for the doubled bass
 * track the transcription voices as a lead. */
static ma_patch lead_bass_patch(void) {
    ma_patch p = bass_patch_driven();
    p.vco1.saw_level = .40f;
    p.filter_cutoff_hz = 900; p.filter_drive = .55f; p.body_drive = .45f;
    p.master_level *= .85f;
    return p;
}
/* Machine-gun guitar: the second driven guitar with a shorter decay, so
 * the gallop stays tight. */
static ma_patch machine_gun_patch(void) {
    ma_patch p = gtr_patch(true);
    p.amp_adsr = (ma_adsr){ 3, 180, .70f, 110 };
    p.filter_adsr = (ma_adsr){ 2, 150, .40f, 110 };
    return p;
}
static ma_patch part_patch(unsigned part) {
    switch (part) {
    case O_LEAD1: case O_LEAD2: case O_LEAD3: return lead_patch();
    case O_BASSL: return lead_bass_patch();
    case O_BASSR: return bass_patch_driven();
    case O_GTR1: return gtr_patch(false);
    case O_GTR2: return machine_gun_patch();
    case O_CHORDS: return strings_patch();
    case O_KICK: return drum_patch(KICK);
    default: return drum_patch(PULSE);
    }
}

static void controls(ma_voice_pool *p, const ma3_show *s, size_t frame) {
    (void)s;
    float e = energy(m_bar(frame));
    ma_voice_pool_set_macro(&p[O_GTR1], MA_MACRO_HEAT, .20f + .25f * e);
    ma_voice_pool_set_macro(&p[O_GTR2], MA_MACRO_HEAT, .20f + .25f * e);
    for (unsigned l = O_LEAD1; l <= O_LEAD3; l++) ma_voice_pool_set_mod_wheel(&p[l], 0, .06f + .12f * e);
    ma_voice_pool_set_lfo(&p[O_CHORDS], .005f, .06f);
}

static void parts_init(ma3_show *s) {
    const unsigned limits[] = { 16, 8, 8, 8, 8, 16, 16, 16, 1, 4 };
    const unsigned unison[] = { 2, 2, 2, 1, 1, 2, 2, 2, 1, 1 };
    const unsigned stems[] = { ST_LEAD, ST_LEAD, ST_LEAD, ST_BASS, ST_BASS, ST_GUITAR, ST_GUITAR,
                               ST_CHORDS, ST_RHYTHM, ST_RHYTHM };
    /* Starting point: the Cry of Mankind edition 2 lead, guitar and kit
     * gains; the doubled bass at 1.0 each. Set from per-section stem RMS
     * (docs/ma3-orion.md). */
    const float gain[] = { 2.13f, 2.13f, 2.13f, 1.0f, 1.0f, 1.41f, 1.41f, 1.70f, 1.1f, 1.7f };
    /* As wet as The Cry of Mankind edition 2. */
    const float ch[] = { .24f, .24f, .24f, 0, 0, .20f, .16f, .22f, 0, .10f };
    const float rv[] = { .42f, .42f, .42f, .10f, .10f, .30f, .24f, .38f, .10f, .26f };
    const bool big[] = { true, true, true, false, false, true, true, true, false, true };
    for (unsigned p = 0; p < O_PARTS; p++) {
        ma3_part_init(&s->part[p], O_NAMES[p], part_patch(p), limits[p], unison[p], stems[p],
                      gain[p], ch[p], rv[p]);
        s->part[p].reverb = (ma_reverb_controls){ true, rv[p], big[p] ? .82f : .65f, big[p] ? .50f : .55f };
        s->part[p].config.detune_cents = p <= O_LEAD3 ? 6 : p == O_GTR1 ? 7 : p == O_GTR2 ? 9 : 4;
    }
}

static bool load_score(ma3_show *s, const smf_file *m, const char **reason) {
    bool layout = m->format == 1 && m->division == M_PPQ && m->tempo_count == M_TEMPOS;
    for (unsigned i = 0; layout && i < M_TEMPOS; i++)
        layout = m->tempos[i].tick == TEMPO_TICK[i] && m->tempos[i].us_per_quarter == TEMPO_US[i];
    if (!layout) { *reason = "MIDI is not the 2026-11-08 Orion layout"; return false; }
    unsigned attacks[O_PARTS] = { 0 };
    for (size_t i = 0; i < m->event_count; i++) {
        const smf_event *e = &m->events[i];
        unsigned type = e->status & 0xf0u, channel = e->status & 15u;
        if ((type != 0x80u && type != 0x90u) || channel >= O_PARTS || e->tick > M_END_TICK) {
            *reason = "unexpected event in the Orion score";
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
    for (unsigned p = 0; p < O_PARTS; p++)
        if (!attacks[p]) { *reason = "a part has no notes"; return false; }
    return true;
}

static bool select_parts(const char *list) {
    for (unsigned c = 0; c < O_PARTS; c++) include[c] = false;
    while (*list) {
        size_t n = strcspn(list, ",");
        bool found = false;
        for (unsigned c = 0; c < O_PARTS; c++)
            if (strlen(O_NAMES[c]) == n && !strncmp(list, O_NAMES[c], n)) include[c] = found = true;
        if (!found) return false;
        list += n + (list[n] == ',');
    }
    return true;
}

int main(int argc, char **argv) {
    const char *input = "notes-midi/local/orion-20261108.mid", *prefix = NULL, *parts = NULL;
    show_seconds = (unsigned)((m_frame(M_END_TICK) + MA3_RATE - 1) / MA3_RATE) + M_TAIL_SECONDS;
    unsigned seconds = 0;
    bool check = false;
    for (unsigned c = 0; c < O_PARTS; c++) include[c] = true;
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
                    "[--parts lead1,lead2,lead3,bassl,bassr,gtr1,gtr2,chords,kick,pulse] [-i score.mid]\n",
                    argv[0], show_seconds);
            return 2;
        }
    }
    if ((check && prefix) || (seconds && !prefix) || (!check && !prefix)) return 2;
    if (parts && !select_parts(parts)) return 2;
    show = (ma3_show){ .name = "MA3 Orion 2026-11-08", .part_count = O_PARTS,
        .stem_count = ST_COUNT, .seconds = show_seconds, .controls = controls };
    for (unsigned st = 0; st < ST_COUNT; st++) show.stem_name[st] = O_STEMS[st];
    parts_init(&show);
    smf_file midi = { 0 };
    uint8_t *bytes = NULL;
    const char *reason = NULL;
    bool ok = midi_load(&midi, &bytes, input, 0x03ffu, &reason) && load_score(&show, &midi, &reason)
        && ma3_check(&show);
    if (ok) {
        printf("  routing:");
        for (unsigned p = 0; p < O_PARTS; p++) printf(" %s->%s", O_NAMES[p], O_STEMS[show.part[p].stem]);
        printf("\n");
    }
    if (ok && prefix) ok = ma3_render(&show, prefix, seconds);
    if (!ok) fprintf(stderr, "Orion: %s\n", reason ? reason : "score/render validation failed");
    smf_dispose(&midi);
    free(bytes);
    return ok ? 0 : 1;
}
