/* MA3 arrangement/patch material from exhibit_ma_hurt_organ.c.
 * Complete local vocal MIDI and source-derived backing; no organ DSP. */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "smf.h"
enum {TICKS_PER_BAR=1920, BARS=85, EVENT_CAPACITY=16000};
typedef enum { PLUCK, PAD, BASS, TENOR, TEXTURE, MELODY, KICK, PULSE, VOCAL, HARMONY,
               PART_COUNT } part_id;
static const char *const PART_NAMES[] = {
    "pluck", "pad", "bass", "tenor", "texture", "melody", "kick", "pulse", "vocal", "harmony",
};
typedef struct {
    uint32_t tick, order;
    part_id part;
    uint8_t note, velocity;
    bool on;
} arrangement_event;
typedef struct {
    uint8_t *bytes, *vocal_bytes;
    smf_file midi, vocal;
    arrangement_event event[EVENT_CAPACITY];
    size_t count;
    uint8_t root[BARS * 2], chord[BARS * 2][3];
} performance_score;

static size_t tick_frame(uint32_t tick) {
    return (size_t)(((uint64_t)tick * 714285u + 5000u) / 10000u);
}

static ma_patch dark_patch(ma_patch p) {
    p.raster_mix = p.mozaik_mix = p.noise_level = 0;
    p.crossmod_amount = p.sync_amount = 0;
    p.vco1.pulse_level = p.vco2.pulse_level = 0;
    p.vco1.saw_level *= .45f;
    p.vco2.saw_level *= .35f;
    if (p.filter_cutoff_hz > 650) p.filter_cutoff_hz = 650;
    if (p.filter_resonance > .12f) p.filter_resonance = .12f;
    p.filter_keytrack = 0;
    p.filter_drive = .035f;
    p.filter_env_amount = .16f;
    p.bcs_amount = .07f;
    p.bcs_regime = .20f;
    p.body_drive = .045f;
    for (unsigned i = 0; i < MA_MACRO_COUNT; ++i) p.macro[i] = 0;
    return p;
}

static ma_patch pluck_patch(float fine_cents) {
    ma_patch patch = ma_patch_tepih;
    patch.vco1.saw_level = .12f;
    patch.vco1.pulse_level = 0.0f;
    patch.vco1.triangle_level = .48f;
    patch.vco1.sine_level = .34f;
    patch.vco2.saw_level = .03f;
    patch.vco2.pulse_level = 0.0f;
    patch.vco2.triangle_level = .32f;
    patch.vco2.sine_level = .56f;
    patch.vco2_level = .52f;
    patch.vco2_fine_cents = fine_cents;
    patch.noise_level = .012f;
    patch.bcs_amount = .06f;
    patch.bcs_regime = .14f;
    patch.mozaik_mix = .055f;
    patch.filter_cutoff_hz = 980.0f;
    patch.filter_resonance = .14f;
    patch.filter_drive = .065f;
    patch.filter_env_amount = .31f;
    patch.amp_adsr = (ma_adsr){ 260.0f, 1250.0f, .64f, 2900.0f };
    patch.filter_adsr = (ma_adsr){ 180.0f, 1500.0f, .34f, 2500.0f };
    patch.macro[MA_MACRO_BLOOM] = .07f;
    patch.body_drive = .055f;
    patch.width = .34f;
    patch.crossfeed = .12f;
    patch.master_level = .15f;
    return patch;
}

static ma_patch pad_patch(float fine_cents, float raster_mix,
                          float raster_position, float raster_warp) {
    ma_patch patch = ma_patch_tepih;
    patch.vco1.saw_level = .16f;
    patch.vco1.pulse_level = 0.0f;
    patch.vco1.triangle_level = .39f;
    patch.vco1.sine_level = .52f;
    patch.vco2.saw_level = .06f;
    patch.vco2.pulse_level = 0.0f;
    patch.vco2.triangle_level = .46f;
    patch.vco2.sine_level = .45f;
    patch.vco2_level = .61f;
    patch.vco2_fine_cents = fine_cents;
    patch.sync_amount = .012f;
    patch.crossmod_amount = .018f;
    patch.noise_level = .009f;
    patch.raster_mix = raster_mix;
    patch.raster_position = raster_position;
    patch.raster_warp = raster_warp;
    patch.bcs_amount = raster_mix > 0.0f ? .12f : .07f;
    patch.bcs_regime = raster_mix > 0.0f ? .26f : .16f;
    patch.mozaik_mix = .07f;
    patch.mixer_pressure = .045f;
    patch.filter_cutoff_hz = 820.0f;
    patch.filter_resonance = .135f;
    patch.filter_drive = .065f;
    patch.filter_env_amount = .20f;
    patch.amp_adsr = (ma_adsr){ 1150.0f, 2500.0f, .79f, 7200.0f };
    patch.filter_adsr = (ma_adsr){ 900.0f, 2900.0f, .42f, 6200.0f };
    patch.macro[MA_MACRO_GRAVITACIJA] = .04f;
    patch.macro[MA_MACRO_BLOOM] = .11f;
    patch.body_drive = .052f;
    patch.width = .78f;
    patch.crossfeed = .15f;
    patch.master_level = .125f;
    return patch;
}

static ma_patch bass_patch(void) {
    ma_patch patch = ma_patch_granica;
    patch.vco1.saw_level = .10f;
    patch.vco1.pulse_level = 0.0f;
    patch.vco1.triangle_level = .52f;
    patch.vco1.sine_level = .66f;
    patch.vco2.saw_level = .04f;
    patch.vco2.pulse_level = 0.0f;
    patch.vco2.triangle_level = .38f;
    patch.vco2.sine_level = .62f;
    patch.vco2_level = .61f;
    patch.bcs_amount = .62f;
    patch.bcs_regime = .58f;
    patch.filter_cutoff_hz = 390.0f;
    patch.filter_resonance = .12f;
    patch.filter_drive = .11f;
    patch.filter_env_amount = .23f;
    patch.amp_adsr = (ma_adsr){ 58.0f, 980.0f, .76f, 3400.0f };
    patch.filter_adsr = (ma_adsr){ 42.0f, 1200.0f, .38f, 2900.0f };
    patch.macro[MA_MACRO_GRAVITACIJA] = .10f;
    patch.body_drive = .09f;
    patch.width = .18f;
    patch.crossfeed = .32f;
    patch.master_level = .17f;
    return patch;
}

static ma_patch tenor_patch(float fine_cents) {
    ma_patch patch = ma_patch_tepih;
    patch.vco1.saw_level = .19f;
    patch.vco1.pulse_level = .025f;
    patch.vco1.triangle_level = .38f;
    patch.vco1.sine_level = .37f;
    patch.vco2.saw_level = .08f;
    patch.vco2.pulse_level = 0.0f;
    patch.vco2.triangle_level = .42f;
    patch.vco2.sine_level = .43f;
    patch.vco2_level = .62f;
    patch.vco2_fine_cents = fine_cents;
    patch.crossmod_amount = .045f;
    patch.noise_level = .018f;
    patch.raster_mix = .12f;
    patch.raster_position = .20f;
    patch.raster_warp = .07f;
    patch.bcs_amount = .30f;
    patch.bcs_regime = .42f;
    patch.mozaik_mix = .075f;
    patch.filter_cutoff_hz = 760.0f;
    patch.filter_resonance = .19f;
    patch.filter_drive = .12f;
    patch.filter_env_amount = .29f;
    patch.amp_adsr = (ma_adsr){ 240.0f, 1750.0f, .70f, 4800.0f };
    patch.filter_adsr = (ma_adsr){ 180.0f, 2100.0f, .36f, 4100.0f };
    patch.macro[MA_MACRO_BLOOM] = .085f;
    patch.macro[MA_MACRO_HEAT] = .075f;
    patch.body_drive = .11f;
    patch.width = .42f;
    patch.crossfeed = .18f;
    patch.master_level = .14f;
    return patch;
}

static ma_patch pulse_patch(float fine_cents, float raster_position,
                            float raster_warp, float bcs_regime) {
    ma_patch patch = ma_patch_prizma;
    patch.vco1.saw_level = .24f;
    patch.vco1.pulse_level = .03f;
    patch.vco1.triangle_level = .18f;
    patch.vco1.sine_level = .48f;
    patch.vco2.saw_level = .12f;
    patch.vco2.pulse_level = 0.0f;
    patch.vco2.triangle_level = .28f;
    patch.vco2.sine_level = .38f;
    patch.vco2_level = .48f;
    patch.vco2_fine_cents = fine_cents;
    patch.crossmod_amount = .055f;
    patch.noise_level = .10f;
    patch.raster_mix = .30f;
    patch.raster_position = raster_position;
    patch.raster_warp = raster_warp;
    patch.bcs_amount = .24f;
    patch.bcs_regime = bcs_regime;
    patch.mozaik_mix = .14f;
    patch.mozaik_slope = .54f;
    patch.mozaik_contrast = .57f;
    patch.mozaik_drift = .065f;
    patch.filter_cutoff_hz = 1420.0f;
    patch.filter_resonance = .21f;
    patch.filter_drive = .13f;
    patch.filter_env_amount = .48f;
    patch.amp_adsr = (ma_adsr){ 8.0f, 220.0f, .36f, 1800.0f };
    patch.filter_adsr = (ma_adsr){ 4.0f, 380.0f, .08f, 1100.0f };
    patch.macro[MA_MACRO_HEAT] = .075f;
    patch.macro[MA_MACRO_RUIN] = .025f;
    patch.body_drive = .11f;
    patch.width = .58f;
    patch.crossfeed = .08f;
    patch.master_level = .13f;
    return patch;
}

static bool score_validate(const performance_score *score,
                           const char **reason) {
    static const unsigned EXPECTED_NOTES[16] = {
        [0] = 704, [2] = 125, [4] = 224, [6] = 189, [9] = 248,
    };
    if (score->midi.format != 1 || score->midi.tracks != 6
        || score->midi.division != 480 || score->midi.event_count != 2980
        || score->midi.tempo_count != 1
        || score->midi.tempos[0].tick != 0
        || score->midi.tempos[0].us_per_quarter != 714285
        || score->midi.events[score->midi.event_count - 1].tick > BARS * TICKS_PER_BAR) {
        *reason = "MIDI is not the inspected six-track Hurt source";
        return false;
    }
    unsigned note_count[16] = { 0 };
    for (size_t i = 0; i < score->midi.event_count; ++i) {
        const smf_event *event = &score->midi.events[i];
        uint8_t type = event->status & 0xf0u;
        uint8_t channel = event->status & 0x0fu;
        if ((type != 0x80u && type != 0x90u) || event->d1 > 127u
            || (i && (event->tick < score->midi.events[i - 1u].tick
                      || (event->tick == score->midi.events[i - 1u].tick
                          && event->seq < score->midi.events[i - 1u].seq)))) {
            *reason = "MIDI events are invalid or unsorted";
            return false;
        }
        if (type == 0x90u && event->d2) note_count[channel]++;
    }
    for (unsigned channel = 0; channel < 16; ++channel)
        if (note_count[channel] != EXPECTED_NOTES[channel]) {
            *reason = "MIDI note layout differs from the inspected source";
            return false;
        }
    return true;
}

static bool midi_load(smf_file *midi, uint8_t **bytes, const char *path,
                      uint16_t channel_mask, const char **reason) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        *reason = strerror(errno);
        return false;
    }
    bool ok = fseek(file, 0, SEEK_END) == 0;
    long length = ok ? ftell(file) : -1;
    ok = length > 0 && fseek(file, 0, SEEK_SET) == 0;
    *bytes = ok ? malloc((size_t)length) : 0;
    ok = ok && *bytes
       && fread(*bytes, 1, (size_t)length, file) == (size_t)length;
    bool closed = fclose(file) == 0;
    if (!ok || !closed) {
        *reason = "could not read MIDI file";
        return false;
    }
    smf_error error = { 0 };
    if (!smf_parse(*bytes, (size_t)length, channel_mask, midi, &error)) {
        static char message[160];
        snprintf(message, sizeof message, "MIDI error at byte %zu: %s",
                 error.offset, error.message ? error.message : "unknown error");
        *reason = message;
        return false;
    }
    return true;
}

static void score_dispose(performance_score *score) {
    smf_dispose(&score->midi);
    smf_dispose(&score->vocal);
    free(score->bytes);
    free(score->vocal_bytes);
    *score = (performance_score){ 0 };
}

static bool push_event(performance_score *score, uint32_t tick, part_id part,
                       uint8_t note, uint8_t velocity, bool on) {
    if (score->count == EVENT_CAPACITY || tick > BARS * TICKS_PER_BAR
        || note > 127)
        return false;
    score->event[score->count] = (arrangement_event){
        .tick = tick, .order = (uint32_t)score->count,
        .part = part, .note = note, .velocity = velocity, .on = on,
    };
    score->count++;
    return true;
}

static bool add_note(performance_score *score, uint32_t tick, uint32_t duration,
                     part_id part, uint8_t note, uint8_t velocity) {
    return push_event(score, tick, part, note, velocity, true)
        && push_event(score, tick + duration, part, note, 0, false);
}

static int compare_events(const void *a, const void *b) {
    const arrangement_event *x = a, *y = b;
    if (x->tick != y->tick) return x->tick < y->tick ? -1 : 1;
    if (x->on != y->on) return x->on ? 1 : -1;
    return (x->order > y->order) - (x->order < y->order);
}

static bool arrange_vocal(performance_score *score) {
    const smf_file *vocal = &score->vocal;
    /* The inspected full-score download puts eight bars before our backing.
     * Keep every vocal note, velocity, gate and rest after that fixed shift.
     * Only the performance tempo changes from 81 to the backing's 84 BPM. */
    constexpr uint64_t INTRO_TICKS = 8u * TICKS_PER_BAR;
    if (vocal->format != 1 || vocal->tracks != 11 || vocal->division != 480
        || vocal->tempo_count != 1 || vocal->tempos[0].tick != 0
        || vocal->tempos[0].us_per_quarter != 740740) return false;
    unsigned attacks = 0, releases = 0;
    bool ok = true;
    for (size_t i = 0; i < vocal->event_count; ++i) {
        const smf_event *e = &vocal->events[i];
        unsigned type = e->status & 0xf0u;
        if (type != 0x80u && type != 0x90u) continue;
        if ((e->status & 15u) != 0 || e->tick < INTRO_TICKS
            || e->tick - INTRO_TICKS > BARS * TICKS_PER_BAR
            || e->d1 < 45 || e->d1 > 69) return false;
        bool on = type == 0x90u && e->d2;
        if (on) attacks++; else releases++;
        ok &= push_event(score, (uint32_t)(e->tick - INTRO_TICKS), VOCAL,
                         e->d1, e->d2, on);
    }
    return ok && attacks == 200 && releases == 200;
}

/* Half-bar harmony follows source bass, then the lowest guitar note.
 * Voicings use the source guitar's most frequent pitch classes; a melody-only
 * passage borrows the most recent voicing with the same bass pitch class. */
static void derive_harmony(performance_score *score) {
    unsigned weight[BARS * 2][12] = { 0 };
    uint8_t low[BARS * 2] = { 0 }, bass[BARS * 2] = { 0 };
    for (size_t i = 0; i < score->midi.event_count; ++i) {
        const smf_event *e = &score->midi.events[i];
        if ((e->status & 0xf0u) != 0x90u || !e->d2) continue;
        size_t h = e->tick / (TICKS_PER_BAR / 2);
        if (h >= BARS * 2) continue;
        unsigned channel = e->status & 15u;
        if (channel == 4 && (!bass[h] || e->d1 < bass[h])) bass[h] = e->d1;
        if (channel != 0 && channel != 6) continue;
        if (!low[h] || e->d1 < low[h]) low[h] = e->d1;
        weight[h][e->d1 % 12]++;
    }
    for (unsigned h = 0; h < BARS * 2; ++h) {
        int root = bass[h] ? bass[h] : low[h] ? low[h] : h ? score->root[h-1] : 35;
        while (root > 42) root -= 12;
        while (root < 31) root += 12;
        score->root[h] = (uint8_t)root;
        unsigned total = 0;
        for (unsigned pc = 0; pc < 12; ++pc) total += weight[h][pc];
        if (!total && h) {
            unsigned previous = h - 1;
            for (unsigned j = h; j-- > 0;)
                if (score->root[j] % 12 == root % 12) { previous = j; break; }
            memcpy(score->chord[h], score->chord[previous], sizeof score->chord[h]);
            continue;
        }
        /* Keep the harmonic root, then two source tones above it. */
        score->chord[h][0] = (uint8_t)(root + 24);
        weight[h][root % 12] = 0;
        for (unsigned v = 1; v < 3; ++v) {
            unsigned pc = (unsigned)(root + 7) % 12;
            for (unsigned j = 0; j < 12; ++j)
                if (weight[h][j] > weight[h][pc]) pc = j;
            int note = 60 + (int)pc;
            if (note - score->chord[h][0] > 11) note -= 12;
            if (!weight[h][pc]) note = root + (v == 1 ? 31 : 36);
            score->chord[h][v] = (uint8_t)note;
            weight[h][pc] = 0;
        }
    }
}

static bool arrange(performance_score *score) {
    derive_harmony(score);
    bool ok = true;
    uint8_t texture_note[128] = { 0 };
    for (size_t i = 0; i < score->midi.event_count; ++i) {
        const smf_event *e = &score->midi.events[i];
        unsigned channel = e->status & 15u;
        if (channel == 4) continue; /* Revoiced below on the groove grid. */
        part_id part = channel == 0 ? PLUCK : channel == 2 ? MELODY
                     : channel == 6 ? TENOR : TEXTURE;
        bool on = (e->status & 0xf0u) == 0x90u && e->d2;
        uint8_t note = e->d1;
        if (part == TEXTURE) {
            unsigned h = (unsigned)(e->tick / 960);
            if (h >= BARS * 2) h = BARS * 2 - 1;
            if (on) texture_note[note] = (uint8_t)(score->root[h] + 24);
            note = texture_note[note];
        }
        if (part == MELODY) note -= 12;
        ok &= push_event(score, (uint32_t)e->tick, part, note, e->d2, on);
    }
    static const unsigned KICK_STEPS[] = { 0, 6, 11 };
    static const unsigned BASS_STEPS[] = { 2, 10 };
    for (unsigned bar = 0; bar < BARS; ++bar) {
        uint32_t at = bar * TICKS_PER_BAR;
        bool intro = bar < 8, breakdown = bar >= 40 && bar < 48;
        bool full = (bar >= 24 && bar < 40) || (bar >= 56 && bar < 80);
        bool climax = bar >= 64 && bar < 80, outro = bar >= 80;
        bool groove = !intro && !breakdown && !outro;
        for (unsigned half = 0; half < 2; ++half) {
            unsigned h = 2 * bar + half;
            uint32_t beat = at + half * 960;
            if (!(intro && bar < 4)) {
                /* Two held notes leave space for the previous pair's release. */
                for (unsigned v = 0; v < 2; ++v)
                    ok &= add_note(score, beat, 840, PAD,
                                   score->chord[h][v], climax ? 77 : 58);
            }
        }
        /* One diffuse harmonic swell before the late main-organ entrance.
         * A separate instrument leaves the established lead chain intact. */
        if (bar >= 64 && bar < 68)
            for (unsigned v = 0; v < 3; ++v)
                ok &= add_note(score, at + 120, 1680, HARMONY,
                               score->chord[2 * bar][v], 68);
        if (groove) {
            unsigned kick_count = full ? 3 : 2;
            for (unsigned j = 0; j < kick_count; ++j)
                ok &= add_note(score, at + 120 * KICK_STEPS[j], 96, KICK, 35,
                               (uint8_t)((j ? 94 : 113) + (climax ? 8 : 0)));
            ok &= add_note(score, at + 960, 260, PULSE,
                           (uint8_t)(score->root[2 * bar + 1] + 12), climax ? 92 : 78);
            for (unsigned j = 0; j < 2; ++j) {
                unsigned step = BASS_STEPS[j];
                unsigned h = 2 * bar + (step >= 8);
                uint8_t note = score->root[h];
                ok &= add_note(score, at + step * 120, 600,
                               BASS, note, (uint8_t)(j % 2 ? 84 : 97));
            }
        } else if ((intro && bar >= 4 && bar % 2 == 0)
                   || (outro && bar < 83)) {
            ok &= add_note(score, at, 120, KICK, 35, outro ? 78 : 67);
        }
    }
    ok &= arrange_vocal(score);
    qsort(score->event, score->count, sizeof *score->event, compare_events);
    return ok;
}

static bool check_arrangement(const performance_score *score) {
    unsigned held[PART_COUNT][128] = { 0 }, attacks[PART_COUNT] = { 0 };
    unsigned active[PART_COUNT] = { 0 }, peak[PART_COUNT] = { 0 };
    static const unsigned CAPACITY[] = { 6, 5, 1, 3, 5, 1, 1, 2, 3, 3 };
    for (size_t i = 0; i < score->count; ++i) {
        const arrangement_event *e = &score->event[i];
        bool organ = e->part == MELODY || e->part == HARMONY;
        if (organ && (e->note < 36 || e->note > 96)) return false;
        if (organ && e->on && held[e->part][e->note]) return false;
        if (i && compare_events(&score->event[i-1], e) > 0) return false;
        if (e->on) {
            held[e->part][e->note]++;
            attacks[e->part]++;
            active[e->part]++;
            if (active[e->part] > peak[e->part]) peak[e->part] = active[e->part];
        } else {
            if (!held[e->part][e->note]) {
                fprintf(stderr, "unowned %s release %u at tick %u\n",
                        PART_NAMES[e->part], e->note, e->tick);
                return false;
            }
            held[e->part][e->note]--;
            active[e->part]--;
        }
    }
    for (unsigned p = 0; p < PART_COUNT; ++p) {
        printf("  %-8s %4u attacks, %u held peak\n", PART_NAMES[p], attacks[p], peak[p]);
        if (active[p] || peak[p] > CAPACITY[p]) return false;
    }
    return true;
}

static ma_patch drum_patch(part_id part) {
    ma_patch p = { 0 };
    p.vco1.pulse_width = p.vco2.pulse_width = .5f;
    p.vco1.sine_level = .95f;
    p.vco1.triangle_level = part == KICK ? .18f : .30f;
    p.noise_level = 0;
    p.filter_cutoff_hz = part == KICK ? 125.0f : 330.0f;
    p.filter_resonance = part == KICK ? .24f : .12f;
    p.filter_drive = part == KICK ? .18f : .26f;
    p.filter_env_amount = part == KICK ? .62f : .26f;
    p.amp_adsr = part == KICK ? (ma_adsr){ 18, 210, 0, 270 }
                             : (ma_adsr){ 80, 480, .12f, 800 };
    p.filter_adsr = (ma_adsr){ 1, 65, 0, 100 };
    p.body_drive = part == KICK ? .13f : .23f;
    p.bcs_amount = part == KICK ? .08f : .18f;
    p.bcs_regime = .35f;
    p.master_level = part == KICK ? .48f : .33f;
    return dark_patch(p);
}
