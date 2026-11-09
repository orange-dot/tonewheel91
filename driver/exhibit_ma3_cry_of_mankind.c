/* MA3 The Cry of Mankind (My Dying Bride), 2026-11-08: seven Mamut parts,
 * one per channel of notes-midi/local/cry-of-mankind-20261108.mid
 * (driver/make_cry_of_mankind.py), rendered in one pass by ma3_show.
 *   - the rhythm guitar on the driven guitar voice of
 *     driver/exhibit_ma3_mourning_palace.c, and the high guitar line that
 *     carries the motif on the orchestration lead voice
 *     (driver/exhibit_ma3_hurt_orch.c);
 *   - bass (driven), piano on the struck keys voice of
 *     driver/exhibit_ma3_hurt_industrial.c;
 *   - the MA3 Hurt kit: tonal kick on a one-slot choke pool, pitched pulse
 *     on the snare, and in this song the tom groove on a second pulse pool
 *     balanced on its own.
 * One tempo, 100 BPM at 960 PPQ: exactly 30 frames per tick.
 * --edition 2: the humanized score (notes-midi/local/cry-of-mankind-20261108-v2.mid,
 * make_cry_of_mankind.py --edition 2) in a wetter mix: more chorus and
 * reverb on every part, a larger room on the guitar, lead, piano and the
 * pulse voices. --edition 3: edition 2 with the lead line on a cutting
 * voice (saw and pulse, detuned VCO2, sync, resonance, pre-filter drive, a
 * fast filter bite on every note) whose filter, resonance, drive, sync and
 * vibrato open and close with the song's energy. --edition 4: edition 2
 * with that cutting voice and its energy automation on the guitar line (the
 * sustained melodic line, channel 0) instead of the lead, which stays as in
 * edition 2. --edition 5: edition 4 on the octave-doubled score
 * (notes-midi/local/cry-of-mankind-20261108-v3.mid, make_cry_of_mankind.py
 * --edition 3), the guitar pool widened to 16 slots for the added octave.
 * --edition 6: edition 5 with the v6 listening balance folded into the part
 * gains (the stem gains and guitar trim of that mix), and the kick on a stem
 * of its own, so bass and kick can be balanced separately afterwards.
 * Editions 1 to 5 are unchanged.
 * Notes: docs/ma3-cry-of-mankind.md. */
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

enum { P_GTR, P_LEAD, P_BASS, P_PIANO, P_KICK, P_PULSE, P_TOMS, P_PARTS };
enum { ST_GUITAR, ST_LEAD, ST_BASS, ST_PIANO, ST_RHYTHM, ST_COUNT };
enum { P_PPQ = 960, P_BAR = 4 * P_PPQ, P_US_PER_QUARTER = 600000, P_FRAMES_PER_TICK = 30,
       P_END_TICK = 760320, P2_END_TICK = 760640, P_TAIL_SECONDS = 4 };
static const char *const P_NAMES[P_PARTS] = { "gtr", "lead", "bass", "piano", "kick", "pulse", "toms" };
static const char *const P_STEMS[ST_COUNT] = { "guitar", "lead", "bass", "piano", "rhythm" };

static ma3_show show;
static bool include[P_PARTS];
static unsigned edition = 1;
static unsigned show_seconds;

/* 48 kHz at 600000 us per 960-tick quarter: 30 frames per tick. */
static size_t p_frame(uint64_t tick) { return (size_t)(tick * P_FRAMES_PER_TICK); }
static double p_bar(size_t frame) { return (double)frame / ((double)P_FRAMES_PER_TICK * P_BAR); }

/* Form energy over the transcription's sections: the guitar line alone
 * (0-3), ride and kick under it (4), the tom groove (20-35), the full band
 * with the piano (36), the long band section with the piano returning
 * (68, 124), the tom groove again (160), the drums stop (168), bass and
 * line alone (169-182), the line alone to the end (183-197). */
static float energy(double bar) {
    static const float at[][2] = {
        {0, .20f}, {4, .35f}, {20, .55f}, {36, .80f}, {44, .70f}, {68, .85f},
        {84, .75f}, {124, .95f}, {148, .80f}, {160, .55f}, {168, .35f}, {183, .20f},
        {198, .10f},
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
static ma_patch lead_patch(void) {
    ma_patch p = ma_dark_lead_patch();
    p.vco1.saw_level = .18f; p.vco2.saw_level = .08f;
    p.filter_cutoff_hz = 1300;
    p.amp_adsr = (ma_adsr){ 25, 600, .75f, 900 };
    p.filter_adsr = (ma_adsr){ 20, 700, .40f, 900 };
    p.bcs_amount = .12f; p.bcs_regime = .28f;
    return p;
}
/* Edition 3 lead (operator: "too poor, needs more cut and drama"): bright
 * saw/pulse voice with a filter bite per note; the energy e (0..1) of the
 * form opens filter, resonance, drive, sync and body together. */
static ma_patch cutting_lead_patch(float e) {
    ma_patch p = lead_patch();
    p.vco1 = (ma_vco_controls){ .saw_level = .50f, .pulse_level = .18f, .triangle_level = .12f,
                                .sine_level = .18f, .pulse_width = .38f };
    p.vco2 = (ma_vco_controls){ .saw_level = .38f, .triangle_level = .10f, .sine_level = .15f,
                                .pulse_width = .5f };
    p.vco2_level = .62f; p.vco2_fine_cents = 8;
    p.sync_amount = .08f + .16f * e; p.crossmod_amount = .05f; p.noise_level = .01f;
    p.mixer_pressure = .20f;
    p.filter_cutoff_hz = 1000 + 2000 * e; p.filter_resonance = .24f + .16f * e;
    p.filter_drive = .22f + .28f * e; p.filter_env_amount = .40f; p.filter_keytrack = .25f;
    p.amp_adsr = (ma_adsr){ 10, 500, .86f, 650 };
    p.filter_adsr = (ma_adsr){ 6, 320, .30f, 500 };
    p.bcs_amount = .15f; p.bcs_regime = .30f;
    p.macro[MA_MACRO_HEAT] = .10f + .15f * e;
    p.body_drive = .18f + .14f * e; p.width = .62f; p.master_level = .16f;
    return p;
}
static ma_patch keys_patch(void) {
    ma_patch p = pluck_patch(0);
    p.vco1 = (ma_vco_controls){ .saw_level = .08f, .triangle_level = .40f, .sine_level = .52f, .pulse_width = .5f };
    p.vco2 = (ma_vco_controls){ .triangle_level = .20f, .sine_level = .70f, .pulse_width = .5f };
    p.vco2_level = .26f; p.vco2_interval = 12; p.vco2_fine_cents = 1.5f;
    p.noise_level = .004f; p.mozaik_mix = .03f; p.bcs_amount = .05f;
    p.filter_cutoff_hz = 1350; p.filter_resonance = .12f; p.filter_env_amount = .35f;
    p.filter_keytrack = .25f; p.filter_drive = .12f;
    p.amp_adsr = (ma_adsr){ 4, 1400, .22f, 1300 };
    p.filter_adsr = (ma_adsr){ 2, 900, .25f, 1000 };
    p.body_drive = .12f; p.width = .50f; p.master_level = .15f;
    return p;
}
static ma_patch part_patch(unsigned part) {
    switch (part) {
    case P_GTR: return edition >= 4 ? cutting_lead_patch(energy(0)) : gtr_patch(false);
    case P_LEAD: return edition == 3 ? cutting_lead_patch(energy(0)) : lead_patch();
    case P_BASS: return bass_patch_driven();
    case P_PIANO: return keys_patch();
    case P_KICK: return drum_patch(KICK);
    default: return drum_patch(PULSE);   /* pulse and toms */
    }
}

static void controls(ma_voice_pool *p, const ma3_show *s, size_t frame) {
    (void)s;
    float e = energy(p_bar(frame));
    if (edition >= 4) {
        ma_patch line = cutting_lead_patch(e);
        ma_voice_pool_set_patch(&p[P_GTR], &line);
        ma_voice_pool_set_mod_wheel(&p[P_GTR], 0, .05f + .20f * e);
    } else ma_voice_pool_set_macro(&p[P_GTR], MA_MACRO_HEAT, .20f + .25f * e);
    if (edition == 3) {
        ma_patch lead = cutting_lead_patch(e);
        ma_voice_pool_set_patch(&p[P_LEAD], &lead);
        ma_voice_pool_set_mod_wheel(&p[P_LEAD], 0, .05f + .20f * e);
    } else ma_voice_pool_set_mod_wheel(&p[P_LEAD], 0, .06f + .12f * e);
}

static void parts_init(ma3_show *s) {
    const unsigned limits[] = { 8, 8, 4, 8, 1, 4, 4 };
    const unsigned unison[] = { 2, 2, 1, 1, 1, 1, 1 };
    const unsigned stems[] = { ST_GUITAR, ST_LEAD, ST_BASS, ST_PIANO, ST_RHYTHM, ST_RHYTHM, ST_RHYTHM };
    /* From a first full pass with the toms on the pulse pool, at gains
     * 1/1.2/1.59/2.13/1.1/1.7, and its per-section stem RMS
     * (docs/ma3-cry-of-mankind.md): lead +5 dB, guitar +3 dB, bass -1 dB;
     * the toms on their own pool 4 dB under the pulse gain. */
    const float gain[] = { 1.41f, 2.13f, 1.42f, 2.13f, 1.1f, 1.7f, 1.07f };
    const float ch[] = { .14f, .16f, 0, .10f, 0, .06f, .06f };
    const float rv[] = { .16f, .26f, .05f, .24f, .04f, .14f, .14f };
    for (unsigned p = 0; p < P_PARTS; p++) {
        ma3_part_init(&s->part[p], P_NAMES[p], part_patch(p), limits[p], unison[p], stems[p],
                      gain[p], ch[p], rv[p]);
        s->part[p].config.detune_cents = p == P_GTR ? 7 : p == P_LEAD ? 6 : 4;
    }
    if (edition >= 5) s->part[P_GTR].config.limit = 16;
    if (edition == 6) {
        /* v6 listening mix: stem gains guitar +0.9 (+2.5 trim), lead +1.3,
         * bass +0.4, piano +4.5, rhythm +0.7 dB on top of the part gains. */
        const float folded[] = { 1.41f * 1.479108f, 2.13f * 1.161449f, 1.42f * 1.047129f,
                                 2.13f * 1.678804f, 1.1f * 1.083927f, 1.7f * 1.083927f,
                                 1.07f * 1.083927f };
        for (unsigned p = 0; p < P_PARTS; p++) s->part[p].gain = folded[p];
        s->part[P_KICK].stem = ST_COUNT;   /* the sixth stem, "kick" */
    }
    if (edition >= 2) {
        /* Wetter (operator): chorus and reverb mix up on every part, and a
         * larger, slightly brighter room where the parts sustain. */
        const float ch2[] = { .20f, .24f, 0, .18f, 0, .10f, .10f };
        const float rv2[] = { .30f, .42f, .12f, .40f, .10f, .26f, .26f };
        const bool big[] = { true, true, false, true, false, true, true };
        for (unsigned p = 0; p < P_PARTS; p++) {
            s->part[p].chorus = (ma_chorus_controls){ ch2[p] > 0, ch2[p], .35f, .25f };
            s->part[p].reverb = (ma_reverb_controls){ true, rv2[p], big[p] ? .82f : .65f, big[p] ? .50f : .55f };
        }
    }
}

static bool load_score(ma3_show *s, const smf_file *m, const char **reason) {
    if (m->format != 1 || m->division != P_PPQ || m->tempo_count != 1
        || m->tempos[0].tick != 0 || m->tempos[0].us_per_quarter != P_US_PER_QUARTER) {
        *reason = "MIDI is not the 2026-11-08 Cry of Mankind layout";
        return false;
    }
    unsigned attacks[P_PARTS] = { 0 };
    for (size_t i = 0; i < m->event_count; i++) {
        const smf_event *e = &m->events[i];
        unsigned type = e->status & 0xf0u, channel = e->status & 15u;
        if ((type != 0x80u && type != 0x90u) || channel >= P_PARTS
            || e->tick > (edition >= 2 ? P2_END_TICK : P_END_TICK)) {
            *reason = "unexpected event in the Cry of Mankind score";
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
    const char *input = NULL, *prefix = NULL, *parts = NULL;
    show_seconds = (unsigned)((p_frame(P_END_TICK) + MA3_RATE - 1) / MA3_RATE) + P_TAIL_SECONDS;
    unsigned seconds = 0;
    bool check = false;
    for (unsigned c = 0; c < P_PARTS; c++) include[c] = true;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--render") && i + 1 < argc) prefix = argv[++i];
        else if (!strcmp(argv[i], "-i") && i + 1 < argc) input = argv[++i];
        else if (!strcmp(argv[i], "--parts") && i + 1 < argc) parts = argv[++i];
        else if (!strcmp(argv[i], "--edition") && i + 1 < argc) {
            uint64_t n = 0;
            if (!host_parse_u64(argv[++i], 1, 6, &n)) return 2;
            edition = (unsigned)n;
        }
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) {
            uint64_t n = 0;
            if (!host_parse_u64(argv[++i], 1, show_seconds, &n)) return 2;
            seconds = (unsigned)n;
        } else if (!strcmp(argv[i], "--check")) check = true;
        else {
            fprintf(stderr, "Usage: %s [--edition 1..6] --check | --render PREFIX [--seconds 1..%u] "
                    "[--parts gtr,lead,bass,piano,kick,pulse,toms] [-i score.mid]\n",
                    argv[0], show_seconds);
            return 2;
        }
    }
    if ((check && prefix) || (seconds && !prefix) || (!check && !prefix)) return 2;
    if (parts && !select_parts(parts)) return 2;
    if (!input) input = edition == 1 ? "notes-midi/local/cry-of-mankind-20261108.mid"
                      : edition >= 5 ? "notes-midi/local/cry-of-mankind-20261108-v3.mid"
                                     : "notes-midi/local/cry-of-mankind-20261108-v2.mid";
    show = (ma3_show){ .name = edition == 1 ? "MA3 The Cry of Mankind 2026-11-08"
                             : edition == 2 ? "MA3 The Cry of Mankind 2026-11-08 v2"
                             : edition == 3 ? "MA3 The Cry of Mankind 2026-11-08 v3"
                             : edition == 4 ? "MA3 The Cry of Mankind 2026-11-08 v4"
                             : edition == 5 ? "MA3 The Cry of Mankind 2026-11-08 v6"
                                            : "MA3 The Cry of Mankind 2026-11-08 v7", .part_count = P_PARTS,
        .stem_count = edition == 6 ? ST_COUNT + 1 : ST_COUNT, .seconds = show_seconds, .controls = controls };
    for (unsigned st = 0; st < ST_COUNT; st++) show.stem_name[st] = P_STEMS[st];
    if (edition == 6) show.stem_name[ST_COUNT] = "kick";
    parts_init(&show);
    smf_file midi = { 0 };
    uint8_t *bytes = NULL;
    const char *reason = NULL;
    bool ok = midi_load(&midi, &bytes, input, 0x007fu, &reason) && load_score(&show, &midi, &reason)
        && ma3_check(&show);
    if (ok) {
        printf("  routing:");
        for (unsigned p = 0; p < P_PARTS; p++) printf(" %s->%s", P_NAMES[p], show.stem_name[show.part[p].stem]);
        printf("\n");
    }
    if (ok && prefix) ok = ma3_render(&show, prefix, seconds);
    if (!ok) fprintf(stderr, "Cry of Mankind: %s\n", reason ? reason : "score/render validation failed");
    smf_dispose(&midi);
    free(bytes);
    return ok ? 0 : 1;
}
