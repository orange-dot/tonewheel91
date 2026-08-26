#include "ma_architecture_score.h"

#include <stdlib.h>
#include <string.h>

enum {
    EXORDIUM_END = 34560,
    INVENTION_END = 126720,
    PASSACAGLIA_END = 264960,
    CHORALE_END = 334080,
    FUGUE_END = 487680,
    CODA_END = 518400,
};

const ma_arch_tempo ma_arch_tempos[MA_ARCH_TEMPO_COUNT] = {
    {      0,         0, 1000000 },
    {  34560,  72000000,  750000 },
    { 126720, 216000000,  937500 },
    { 264960, 486000000, 1000000 },
    { 334080, 630000000,  800000 },
    { 487680, 886000000, 1000000 },
    { 518400, 950000000, 1000000 },
};

const ma_arch_section ma_arch_sections[MA_ARCH_SECTION_COUNT] = {
    { "Exordium",     0, EXORDIUM_END,       0,  72, .12f },
    { "Invention",   EXORDIUM_END, INVENTION_END, 72, 216, .10f },
    { "Passacaglia", INVENTION_END, PASSACAGLIA_END, 216, 486, .14f },
    { "Chorale",     PASSACAGLIA_END, CHORALE_END, 486, 630, .28f },
    { "Fugue",       CHORALE_END, FUGUE_END, 630, 886, .18f },
    { "Coda",        FUGUE_END, CODA_END, 886, 950, .30f },
};

const ma_arch_line ma_arch_lines[MA_ARCH_LINE_COUNT] = {
    {  0.0f, .46f, true  },
    { -.18f, .34f, false },
    { -.45f, .24f, false },
    {  .45f, .24f, false },
    { -.28f, .22f, false },
    {  .28f, .22f, false },
    { -.78f, .15f, true  },
    {  .78f, .15f, true  },
    { -.08f, .42f, false },
    {  .08f, .42f, false },
};

const unsigned ma_arch_passacaglia_density[12] = {
    2, 3, 4, 5, 6, 7, 8, 6, 8, 9, 10, 4,
};

static const uint8_t GROUND[16] = {
    42, 49, 52, 50, 45, 47, 44, 49, 38, 45, 40, 47, 49, 44, 40, 42,
};

static const uint8_t SUBJECT[8] = { 66, 69, 68, 73, 71, 76, 74, 73 };
static const uint16_t SUBJECT_RHYTHM[8] = {
    480, 240, 240, 480, 240, 240, 480, 1440,
};
static const uint8_t COUNTERPOINT[8] = { 73, 71, 69, 71, 68, 69, 66, 68 };
static const uint8_t CANTUS[12] = {
    66, 69, 68, 73, 71, 76, 74, 73, 69, 71, 68, 66,
};

static bool push_note(ma_arch_score *score, unsigned line, int note,
                      unsigned velocity, uint32_t start, uint32_t end,
                      unsigned flags) {
    if (score->note_count == MA_ARCH_EVENT_CAPACITY || line >= MA_ARCH_LINE_COUNT
        || note < 0 || note > 127 || velocity > 127 || start >= end
        || end > MA_ARCH_MUSIC_END_TICK)
        return false;
    score->notes[score->note_count++] = (ma_arch_note){
        .start_tick = start,
        .end_tick = end,
        .line = (uint8_t)line,
        .note = (uint8_t)note,
        .velocity = (uint8_t)velocity,
        .flags = (uint8_t)flags,
    };
    return true;
}

static bool push_invention_note(ma_arch_score *score, unsigned line, int note,
                                unsigned velocity, uint32_t start,
                                uint32_t end, unsigned flags) {
    uint32_t gap_start = EXORDIUM_END + 35u * 1920u + 1280u;
    uint32_t gap_end = EXORDIUM_END + 36u * 1920u;
    if (start < gap_start && end > gap_start) end = gap_start;
    if (start < gap_end && end > gap_end) start = gap_end;
    if (start >= gap_start && end <= gap_end) return true;
    return push_note(score, line, note, velocity, start, end, flags);
}

static bool push_subject(ma_arch_score *score, unsigned line, uint32_t start,
                         int transpose, unsigned flags, bool invention) {
    for (size_t i = 0; i < 8; i++) {
        uint32_t end = start + SUBJECT_RHYTHM[i];
        bool ok = invention
                ? push_invention_note(score, line, SUBJECT[i] + transpose,
                                      94, start, end, flags)
                : push_note(score, line, SUBJECT[i] + transpose, 94,
                            start, end, flags);
        if (!ok) return false;
        start = end;
        flags = 0;
    }
    return true;
}

static bool push_augmented_subject(ma_arch_score *score, unsigned line,
                                   uint32_t start, int transpose,
                                   unsigned flags) {
    for (size_t i = 0; i < 8; i++) {
        uint32_t end = start + 2u * SUBJECT_RHYTHM[i];
        if (!push_note(score, line, SUBJECT[i] + transpose, 88, start, end,
                       flags | MA_ARCH_NOTE_AUGMENTED_SUBJECT))
            return false;
        start = end;
        flags = 0;
    }
    return true;
}

static bool push_counterpoint(ma_arch_score *score, unsigned line,
                              uint32_t start, int transpose, bool invention) {
    for (size_t i = 0; i < 8; i++) {
        uint32_t end = start + 240u;
        bool ok = invention
                ? push_invention_note(score, line,
                                      COUNTERPOINT[i] + transpose, 82,
                                      start, end, 0)
                : push_note(score, line, COUNTERPOINT[i] + transpose, 82,
                            start, end, 0);
        if (!ok) return false;
        start = end;
    }
    return true;
}

static bool build_exordium(ma_arch_score *score) {
    static const uint8_t order[8] = { 6, 0, 7, 2, 3, 4, 5, 1 };
    static const int chord[8] = { 54, 42, 61, 57, 64, 66, 69, 49 };
    for (unsigned arc = 0; arc < 12; arc++) {
        unsigned voices = 1u + arc * 7u / 11u;
        uint32_t base = arc * 2880u;
        for (unsigned voice = 0; voice < voices; voice++)
            if (!push_note(score, order[voice], chord[voice] + (int)(arc % 3),
                           62u + 3u * voice, base + 48u * voice,
                           base + 2400u - 24u * voice, 0))
                return false;
    }
    return true;
}

static bool build_invention(ma_arch_score *score) {
    uint32_t start = EXORDIUM_END;
    for (unsigned bar = 0; bar < 48; bar++) {
        uint32_t at = start + bar * 1920u;
        if (!push_invention_note(score, 0, GROUND[bar % 16] - 12, 76,
                                  at, at + 1680u, 0))
            return false;
        if (!(bar % 4)
            && !push_invention_note(score, 1, GROUND[(bar * 3) % 16], 66,
                                     at, at + 4u * 1920u - 240u, 0))
            return false;
        if (!(bar % 4)) {
            if (!push_invention_note(score, 6, 54 + (int)(bar / 4 % 5), 58,
                                     at + 80u, at + 4u * 1920u - 320u, 0)
                || !push_invention_note(score, 7, 61 + (int)(bar / 4 % 4), 56,
                                        at + 160u,
                                        at + 4u * 1920u - 240u, 0))
                return false;
        }
    }
    for (unsigned cycle = 0; cycle < 6; cycle++) {
        uint32_t base = start + cycle * 8u * 1920u;
        if (!push_subject(score, 2, base, 0, 0, true)
            || !push_subject(score, 3, base + 2u * 1920u, -5, 0, true)
            || !push_subject(score, 4, base + 4u * 1920u, 7, 0, true)
            || !push_subject(score, 5, base + 6u * 1920u, 2, 0, true))
            return false;
        if (!push_counterpoint(score, 4, base, -5, true)
            || !push_counterpoint(score, 4, base + 2u * 1920u, -3, true)
            || !push_counterpoint(score, 5, base, -10, true)
            || !push_counterpoint(score, 5, base + 2u * 1920u, -8, true)
            || !push_counterpoint(score, 5, base + 4u * 1920u, -5, true)
            || !push_counterpoint(score, 2, base + 4u * 1920u, -12, true)
            || !push_counterpoint(score, 3, base + 6u * 1920u, -7, true))
            return false;
    }
    return true;
}

static bool pass_line_active(unsigned variation, unsigned line) {
    static const uint16_t masks[12] = {
        0x003, 0x007, 0x00f, 0x01f, 0x03f, 0x07f,
        0x17f, 0x14f, 0x17f, 0x1ff, 0x3ff, 0x0c3,
    };
    return (masks[variation] & (1u << line)) != 0;
}

static bool build_passacaglia(ma_arch_score *score) {
    for (unsigned variation = 0; variation < 12; variation++) {
        uint32_t base = INVENTION_END + variation * 11520u;
        for (unsigned cell = 0; cell < 16; cell++) {
            uint32_t at = base + cell * 720u;
            unsigned flags = cell ? 0 : MA_ARCH_NOTE_GROUND_CYCLE;
            if (!push_note(score, 1, GROUND[cell], 82, at, at + 660u, flags))
                return false;
        }
        for (unsigned line = 0; line < MA_ARCH_LINE_COUNT; line++) {
            if (line == 1 || !pass_line_active(variation, line)) continue;
            unsigned stride = line == 0 ? 2u : line >= 6 ? 4u : 1u;
            if (line == 4 || line == 5) stride = 1u;
            for (unsigned cell = 0; cell < 16; cell += stride) {
                uint32_t at = base + cell * 720u;
                uint32_t length = stride * 720u - 60u;
                int source = line >= 8 ? CANTUS[(cell / stride) % 12]
                           : line >= 6 ? SUBJECT[(cell / stride) % 8]
                           : COUNTERPOINT[(cell + line) % 8];
                int transpose = line == 0 ? -36
                              : line == 2 ? -17
                              : line == 3 ? -12
                              : line == 4 ? -5
                              : line == 5 ? 0
                              : line == 6 ? -12
                              : line == 7 ? -7
                              : line == 8 ? -12 : -7;
                unsigned flags = 0;
                if (!cell && line == 8 && variation == 6)
                    flags = MA_ARCH_NOTE_DARK_LEAD_ENTRY;
                if (!cell && line == 9 && variation == 10)
                    flags = MA_ARCH_NOTE_DARK_LEAD_ENTRY;
                if (!push_note(score, line, source + transpose,
                               70u + 2u * variation, at, at + length, flags))
                    return false;
            }
        }
    }
    return true;
}

static bool build_chorale(ma_arch_score *score) {
    static const unsigned lines[5] = { 0, 2, 3, 6, 7 };
    static const int offsets[5] = { -36, -17, -12, -5, 0 };
    for (unsigned bar = 0; bar < 24; bar++) {
        uint32_t at = PASSACAGLIA_END + bar * 2880u;
        for (size_t i = 0; i < 3; i++)
            if (!push_note(score, lines[i], CANTUS[bar / 2] + offsets[i],
                           68u + 4u * i, at, at + 2640u, 0))
                return false;
        if (!(bar % 2))
            for (size_t i = 3; i < 5; i++)
                if (!push_note(score, lines[i], CANTUS[bar / 2] + offsets[i],
                               62u + 3u * i, at, at + 5520u, 0))
                    return false;
    }
    for (unsigned phrase = 0; phrase < 12; phrase++) {
        uint32_t at = PASSACAGLIA_END + phrase * 5760u;
        if (!push_note(score, 8, CANTUS[phrase] - 12, 84,
                       at, at + 5520u, MA_ARCH_NOTE_AUGMENTED_SUBJECT))
            return false;
    }
    return true;
}

static bool build_fugue_line(ma_arch_score *score, unsigned line,
                             unsigned entry_bar) {
    bool occupied[80] = { false };
    unsigned starts[8] = {
        entry_bar, entry_bar + 16u, entry_bar + 32u, entry_bar + 48u,
        64u + 2u * (line - 2u), 72u + 2u * (line - 2u), 80, 80,
    };
    for (size_t statement = 0; statement < 6; statement++) {
        unsigned bar = starts[statement];
        if (bar + 2u > 80u) continue;
        unsigned flags = statement ? 0 : MA_ARCH_NOTE_FUGUE_ENTRY;
        if (!push_subject(score, line, CHORALE_END + bar * 1920u,
                          (int)(line - 2u) * 5 - 5, flags, false))
            return false;
        occupied[bar] = occupied[bar + 1u] = true;
    }
    for (unsigned bar = entry_bar; bar < 80; bar++)
        if (!occupied[bar]
            && !push_counterpoint(score, line, CHORALE_END + bar * 1920u,
                                  (int)(line - 2u) * 3 - 17, false))
            return false;
    return true;
}

static bool build_fugue(ma_arch_score *score) {
    for (unsigned bar = 0; bar < 32; bar += 2) {
        uint32_t at = CHORALE_END + bar * 1920u;
        if (!push_note(score, 0, GROUND[(bar / 2) % 16] - 12, 76,
                       at, at + 3600u, 0))
            return false;
    }
    if (!push_augmented_subject(score, 0, CHORALE_END + 32u * 1920u,
                                -24, 0))
        return false;
    for (unsigned bar = 36; bar < 80; bar += 2) {
        uint32_t at = CHORALE_END + bar * 1920u;
        if (!push_note(score, 0, GROUND[(bar / 2) % 16] - 12, 78,
                       at, at + 3600u, 0))
            return false;
    }
    for (unsigned bar = 16; bar < 80; bar++) {
        uint32_t at = CHORALE_END + bar * 1920u;
        if (!push_note(score, 1, GROUND[bar % 16], 70,
                       at, at + 1800u, 0))
            return false;
    }
    for (unsigned line = 2; line <= 5; line++)
        if (!build_fugue_line(score, line, (line - 2u) * 4u)) return false;
    for (unsigned line = 6; line <= 7; line++)
        for (unsigned bar = 24u + (line - 6u) * 8u; bar < 80; bar++)
            if (!push_counterpoint(score, line, CHORALE_END + bar * 1920u,
                                   (int)line - 18, false))
                return false;
    static const unsigned lead_bars[2][3] = {
        { 48, 64, 72 }, { 56, 66, 74 },
    };
    for (unsigned lead = 0; lead < 2; lead++)
        for (unsigned statement = 0; statement < 3; statement++)
            if (!push_augmented_subject(score, 8u + lead,
                                        CHORALE_END
                                        + lead_bars[lead][statement] * 1920u,
                                        -12 + (int)lead * 5, 0))
                return false;
    return true;
}

static bool build_coda(ma_arch_score *score) {
    static const unsigned stop_bar[MA_ARCH_LINE_COUNT] = {
        16, 14, 14, 12, 12, 10, 10, 8, 8, 6,
    };
    static const int offset[MA_ARCH_LINE_COUNT] = {
        -36, -24, -17, -12, -5, 0, -12, -7, -12, -7,
    };
    for (unsigned line = 0; line < MA_ARCH_LINE_COUNT; line++) {
        unsigned ordinary_end = line ? stop_bar[line] : 12u;
        for (unsigned bar = 0; bar < ordinary_end; bar++) {
            uint32_t at = FUGUE_END + bar * 1920u;
            if (!push_note(score, line, CANTUS[bar % 12] + offset[line],
                           72u + line, at, at + 1800u, 0))
                return false;
        }
    }
    return push_note(score, 0, 42, 88, FUGUE_END + 12u * 1920u,
                     CODA_END, 0);
}

static int compare_notes(const void *left, const void *right) {
    const ma_arch_note *a = left;
    const ma_arch_note *b = right;
    if (a->start_tick != b->start_tick)
        return a->start_tick < b->start_tick ? -1 : 1;
    if (a->line != b->line) return a->line < b->line ? -1 : 1;
    if (a->end_tick != b->end_tick) return a->end_tick < b->end_tick ? -1 : 1;
    return (int)a->note - (int)b->note;
}

static bool build_automation(ma_arch_score *score) {
    static const uint32_t ticks[7] = {
        0, EXORDIUM_END, INVENTION_END, PASSACAGLIA_END,
        CHORALE_END, FUGUE_END, CODA_END,
    };
    for (unsigned line = 0; line < 8; line++) {
        if (score->track_count == MA_ARCH_AUTOMATION_TRACK_CAPACITY
            || score->knot_count + 7u > MA_ARCH_AUTOMATION_CAPACITY)
            return false;
        ma_arch_automation_track *track = &score->tracks[score->track_count++];
        *track = (ma_arch_automation_track){
            .line = (uint8_t)line,
            .macro = line % 2 ? MA_MACRO_BLOOM : MA_MACRO_GRAVITACIJA,
            .first_knot = (uint16_t)score->knot_count,
            .knot_count = 7,
        };
        for (unsigned knot = 0; knot < 7; knot++)
            score->knots[score->knot_count++] = (ma_arch_automation_knot){
                .tick = ticks[knot],
                .value = .03f + .012f * (float)((line + 2u * knot) % 6u),
            };
    }
    return true;
}

bool ma_arch_score_build(ma_arch_score *score) {
    if (!score) return false;
    *score = (ma_arch_score){ 0 };
    if (!build_exordium(score) || !build_invention(score)
        || !build_passacaglia(score) || !build_chorale(score)
        || !build_fugue(score) || !build_coda(score)
        || !build_automation(score))
        return false;
    qsort(score->notes, score->note_count, sizeof score->notes[0],
          compare_notes);
    score->ground_cycle_count = 12;
    score->fugue_entry_count = 4;
    return true;
}

static bool invalid(const char **reason, const char *message) {
    if (reason) *reason = message;
    return false;
}

static unsigned active_lines_at(const ma_arch_score *score, uint32_t tick) {
    bool active[MA_ARCH_LINE_COUNT] = { false };
    for (size_t i = 0; i < score->note_count; i++)
        if (score->notes[i].start_tick <= tick
            && score->notes[i].end_tick > tick)
            active[score->notes[i].line] = true;
    unsigned count = 0;
    for (unsigned line = 0; line < MA_ARCH_LINE_COUNT; line++)
        count += active[line];
    return count;
}

bool ma_arch_score_validate(const ma_arch_score *score, const char **reason) {
    if (!score) return invalid(reason, "null score");
    if (!score->note_count || score->note_count > MA_ARCH_EVENT_CAPACITY
        || score->knot_count > MA_ARCH_AUTOMATION_CAPACITY
        || score->track_count > MA_ARCH_AUTOMATION_TRACK_CAPACITY)
        return invalid(reason, "score count exceeds fixed capacity");
    static const uint32_t boundaries[7] = {
        0, EXORDIUM_END, INVENTION_END, PASSACAGLIA_END,
        CHORALE_END, FUGUE_END, CODA_END,
    };
    uint32_t last_end[MA_ARCH_LINE_COUNT] = { 0 };
    unsigned ground_cycles = 0, fugue_entries = 0, dark_entries = 0;
    for (unsigned section = 0; section < MA_ARCH_SECTION_COUNT; section++)
        if (ma_arch_sections[section].start_tick != boundaries[section]
            || ma_arch_sections[section].end_tick != boundaries[section + 1u])
            return invalid(reason, "section boundary drift");
    for (size_t i = 0; i < score->note_count; i++) {
        const ma_arch_note *note = &score->notes[i];
        if (note->line >= MA_ARCH_LINE_COUNT || note->note > 127
            || note->velocity > 127 || note->start_tick >= note->end_tick
            || note->end_tick > MA_ARCH_MUSIC_END_TICK)
            return invalid(reason, "invalid note span");
        if (i && compare_notes(&score->notes[i - 1u], note) > 0)
            return invalid(reason, "unsorted note spans");
        if (note->start_tick < last_end[note->line])
            return invalid(reason, "overlapping monophonic line");
        last_end[note->line] = note->end_tick;
        if (note->flags & MA_ARCH_NOTE_GROUND_CYCLE) ground_cycles++;
        if (note->flags & MA_ARCH_NOTE_FUGUE_ENTRY) fugue_entries++;
        if (note->flags & MA_ARCH_NOTE_DARK_LEAD_ENTRY) {
            bool expected = note->line == 8
                         && note->start_tick == INVENTION_END + 6u * 11520u;
            expected |= note->line == 9
                     && note->start_tick == INVENTION_END + 10u * 11520u;
            if (!expected) return invalid(reason, "misplaced dark lead entry");
            dark_entries++;
        }
        if (note->start_tick >= INVENTION_END
            && note->start_tick < PASSACAGLIA_END && note->line >= 8) {
            unsigned variation = (note->start_tick - INVENTION_END) / 11520u;
            if ((note->line == 8 && (variation < 6 || variation > 10))
                || (note->line == 9 && variation != 10))
                return invalid(reason, "dark lead outside its variations");
        }
        if (note->start_tick >= CHORALE_END && note->start_tick < FUGUE_END
            && note->line >= 8
            && !(note->flags & MA_ARCH_NOTE_AUGMENTED_SUBJECT))
            return invalid(reason, "dark lead has fast fugue material");
    }
    if (ground_cycles != 12 || score->ground_cycle_count != 12)
        return invalid(reason, "ground cycle count drift");
    if (fugue_entries != 4 || score->fugue_entry_count != 4)
        return invalid(reason, "fugue entry count drift");
    if (dark_entries != 2) return invalid(reason, "dark lead entry drift");
    if (active_lines_at(score, 500) != 1
        || active_lines_at(score, 11u * 2880u + 500u) != 8)
        return invalid(reason, "exordium density arc drift");
    uint32_t preview_gap = EXORDIUM_END + 35u * 1920u + 1600u;
    if (active_lines_at(score, preview_gap) != 0)
        return invalid(reason, "180-second cadence gap is occupied");
    if (active_lines_at(score, CHORALE_END + 240u) != 2
        || active_lines_at(score, CHORALE_END + 74u * 1920u + 120u) != 10)
        return invalid(reason, "fugue does not grow from two to ten lines");
    for (unsigned variation = 0; variation < 12; variation++) {
        bool used[MA_ARCH_LINE_COUNT] = { false };
        uint32_t start = INVENTION_END + variation * 11520u;
        uint32_t end = start + 11520u;
        for (size_t i = 0; i < score->note_count; i++)
            if (score->notes[i].start_tick >= start
                && score->notes[i].start_tick < end)
                used[score->notes[i].line] = true;
        unsigned density = 0;
        for (unsigned line = 0; line < MA_ARCH_LINE_COUNT; line++)
            density += used[line];
        if (density != ma_arch_passacaglia_density[variation])
            return invalid(reason, "passacaglia density drift");
    }
    for (size_t i = 0; i < score->track_count; i++) {
        const ma_arch_automation_track *track = &score->tracks[i];
        size_t end = (size_t)track->first_knot + track->knot_count;
        if (track->line >= MA_ARCH_LINE_COUNT || track->macro >= MA_MACRO_COUNT
            || track->knot_count < 2 || end > score->knot_count)
            return invalid(reason, "invalid automation track");
        uint32_t previous = 0;
        for (size_t knot = track->first_knot; knot < end; knot++) {
            const ma_arch_automation_knot *value = &score->knots[knot];
            if (!(value->value >= 0.0f && value->value <= 1.0f)
                || value->tick > MA_ARCH_MUSIC_END_TICK
                || (knot > track->first_knot && value->tick <= previous))
                return invalid(reason, "invalid automation knot");
            previous = value->tick;
        }
    }
    if (reason) *reason = 0;
    return true;
}

uint64_t ma_arch_tick_us(uint32_t tick) {
    size_t tempo = 0;
    while (tempo + 1u < MA_ARCH_TEMPO_COUNT
           && tick >= ma_arch_tempos[tempo + 1u].start_tick)
        tempo++;
    const ma_arch_tempo *segment = &ma_arch_tempos[tempo];
    uint64_t delta = tick - segment->start_tick;
    return segment->start_us
         + delta * segment->us_per_quarter / MA_ARCH_PPQ;
}

uint32_t ma_arch_frame_tick(uint64_t frame, unsigned rate_hz) {
    if (!rate_hz) return 0;
    uint64_t us = frame * UINT64_C(1000000) / rate_hz;
    size_t tempo = 0;
    while (tempo + 1u < MA_ARCH_TEMPO_COUNT
           && us >= ma_arch_tempos[tempo + 1u].start_us)
        tempo++;
    const ma_arch_tempo *segment = &ma_arch_tempos[tempo];
    uint64_t delta_us = us - segment->start_us;
    uint64_t tick = segment->start_tick
                  + delta_us * MA_ARCH_PPQ / segment->us_per_quarter;
    return tick > MA_ARCH_END_TICK ? MA_ARCH_END_TICK : (uint32_t)tick;
}

ma_arch_section_id ma_arch_section_at_tick(uint32_t tick) {
    for (unsigned section = 0; section + 1u < MA_ARCH_SECTION_COUNT; section++)
        if (tick < ma_arch_sections[section].end_tick)
            return (ma_arch_section_id)section;
    return MA_ARCH_CODA;
}

float ma_arch_automation_value(const ma_arch_score *score, size_t track_index,
                               uint32_t tick) {
    if (!score || track_index >= score->track_count) return 0.0f;
    const ma_arch_automation_track *track = &score->tracks[track_index];
    const ma_arch_automation_knot *knots = score->knots + track->first_knot;
    if (tick <= knots[0].tick) return knots[0].value;
    for (size_t i = 1; i < track->knot_count; i++) {
        if (tick > knots[i].tick) continue;
        float position = (float)(tick - knots[i - 1u].tick)
                       / (float)(knots[i].tick - knots[i - 1u].tick);
        return knots[i - 1u].value
             + position * (knots[i].value - knots[i - 1u].value);
    }
    return knots[track->knot_count - 1u].value;
}
