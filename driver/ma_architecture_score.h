#ifndef MA_ARCHITECTURE_SCORE_H
#define MA_ARCHITECTURE_SCORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../src/mamutanalog.h"

enum {
    MA_ARCH_PPQ = 480,
    MA_ARCH_LINE_COUNT = 10,
    MA_ARCH_SECTION_COUNT = 6,
    MA_ARCH_TEMPO_COUNT = 7,
    MA_ARCH_EVENT_CAPACITY = 8192,
    MA_ARCH_AUTOMATION_CAPACITY = 128,
    MA_ARCH_AUTOMATION_TRACK_CAPACITY = 16,
    MA_ARCH_END_TICK = 523200,
    MA_ARCH_MUSIC_END_TICK = 518400,
};

typedef enum {
    MA_ARCH_EXORDIUM,
    MA_ARCH_INVENTION,
    MA_ARCH_PASSACAGLIA,
    MA_ARCH_CHORALE,
    MA_ARCH_FUGUE,
    MA_ARCH_CODA,
} ma_arch_section_id;

typedef enum {
    MA_ARCH_NOTE_GROUND_CYCLE = 1u << 0,
    MA_ARCH_NOTE_FUGUE_ENTRY = 1u << 1,
    MA_ARCH_NOTE_DARK_LEAD_ENTRY = 1u << 2,
    MA_ARCH_NOTE_AUGMENTED_SUBJECT = 1u << 3,
} ma_arch_note_flags;

typedef struct {
    uint32_t start_tick;
    uint32_t end_tick;
    uint8_t line;
    uint8_t note;
    uint8_t velocity;
    uint8_t flags;
} ma_arch_note;

typedef struct {
    uint32_t start_tick;
    uint64_t start_us;
    uint32_t us_per_quarter;
} ma_arch_tempo;

typedef struct {
    const char *name;
    uint32_t start_tick;
    uint32_t end_tick;
    unsigned start_second;
    unsigned end_second;
    float reverb_wet;
} ma_arch_section;

typedef struct {
    float pan;
    float gain;
    bool future_organ_candidate;
} ma_arch_line;

typedef struct {
    uint32_t tick;
    float value;
} ma_arch_automation_knot;

typedef struct {
    uint8_t line;
    ma_macro_id macro;
    uint16_t first_knot;
    uint16_t knot_count;
} ma_arch_automation_track;

typedef struct {
    ma_arch_note notes[MA_ARCH_EVENT_CAPACITY];
    ma_arch_automation_knot knots[MA_ARCH_AUTOMATION_CAPACITY];
    ma_arch_automation_track tracks[MA_ARCH_AUTOMATION_TRACK_CAPACITY];
    size_t note_count;
    size_t knot_count;
    size_t track_count;
    unsigned ground_cycle_count;
    unsigned fugue_entry_count;
} ma_arch_score;

extern const ma_arch_tempo ma_arch_tempos[MA_ARCH_TEMPO_COUNT];
extern const ma_arch_section ma_arch_sections[MA_ARCH_SECTION_COUNT];
extern const ma_arch_line ma_arch_lines[MA_ARCH_LINE_COUNT];
extern const unsigned ma_arch_passacaglia_density[12];

bool ma_arch_score_build(ma_arch_score *score);
bool ma_arch_score_validate(const ma_arch_score *score, const char **reason);
[[nodiscard]] uint64_t ma_arch_tick_us(uint32_t tick);
[[nodiscard]] uint32_t ma_arch_frame_tick(uint64_t frame, unsigned rate_hz);
[[nodiscard]] ma_arch_section_id ma_arch_section_at_tick(uint32_t tick);
[[nodiscard]] float ma_arch_automation_value(const ma_arch_score *score,
                                             size_t track, uint32_t tick);

#endif
