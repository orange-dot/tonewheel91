#ifndef MA_MIDI_MAP_H
#define MA_MIDI_MAP_H

#include "../src/mamutanalog.h"

typedef struct {
    uint64_t note_ons, note_offs, unmatched_note_offs, allocation_failures;
    uint64_t bends, channel_pressures, poly_pressures, ccs;
    uint64_t ignored_ccs, unsupported_messages, invalid_messages, filtered;
} ma_midi_stats;

typedef struct {
    ma_voice_pool pool;
    ma_patch patch;
    ma_midi_stats stats;
    uint16_t channel_mask;
} ma_midi_map;

/* Single timbre, global direct CCs, per-channel expression and sustain.
 * Pool owns repeated notes; no boolean hosted owner table collapses attacks.
 * All calls occur on the render thread. No allocation, I/O or locks. */
bool ma_midi_map_init(ma_midi_map *map, float rate,
    const ma_voice_pool_config *config, const ma_patch *patch, uint16_t channels);
void ma_midi_apply(ma_midi_map *map, uint8_t status, uint8_t d1, uint8_t d2);

#endif
