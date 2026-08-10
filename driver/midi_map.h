#ifndef MIDI_MAP_H
#define MIDI_MAP_H

#include <stdbool.h>
#include <stdint.h>

#include "../src/epiano.h"
#include "../src/tonewheel.h"

typedef struct {
    bool active[16][128];
    uint8_t destination[16][128];
    uint8_t depth[16][128];
    uint16_t count[128];
    uint8_t max_depth[128];
} midi_owners;

typedef struct {
    unsigned long notes, depths, pressures, ccs, folded;
} midi_map_stats;

typedef struct {
    midi_owners owners;
    midi_map_stats stats;
    bool fold;
    bool percussion_on, percussion_third, percussion_slow, percussion_normal;
} organ_midi_map;

typedef struct {
    midi_owners owners;
    midi_map_stats stats;
    bool fold;
} ep_midi_map;

void organ_midi_map_init(organ_midi_map *map, bool fold, bool percussion_on);
void organ_midi_apply(organ_midi_map *map, tw_instrument *instrument,
                      uint8_t status, uint8_t d1, uint8_t d2);

void ep_midi_map_init(ep_midi_map *map, bool fold);
void ep_midi_apply(ep_midi_map *map, ep_piano *piano,
                   uint8_t status, uint8_t d1, uint8_t d2);

#endif
