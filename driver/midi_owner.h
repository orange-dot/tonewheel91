#ifndef MIDI_OWNER_H
#define MIDI_OWNER_H

#include "midi_map.h"

typedef struct {
    int note;
    uint8_t old_depth, new_depth;
    bool found, first, last;
} owner_change;

static inline uint8_t owner_destination_depth(const midi_owners *owners,
                                              int note) {
    uint8_t depth = 0;
    for (unsigned channel = 0; channel < 16; channel++)
        for (unsigned source = 0; source < 128; source++)
            if (owners->active[channel][source]
                && owners->destination[channel][source] == note
                && owners->depth[channel][source] > depth)
                depth = owners->depth[channel][source];
    return depth;
}

static inline owner_change owner_press(midi_owners *owners, unsigned channel,
                                       unsigned source, int destination) {
    owner_change change = {
        .note = destination,
        .old_depth = owners->max_depth[destination],
        .found = true,
    };
    if (!owners->active[channel][source]) {
        owners->active[channel][source] = true;
        owners->destination[channel][source] = (uint8_t)destination;
        change.first = owners->count[destination]++ == 0;
    }
    owners->depth[channel][source] = 127;
    change.new_depth = owner_destination_depth(owners, destination);
    owners->max_depth[destination] = change.new_depth;
    return change;
}

static inline owner_change owner_release(midi_owners *owners, unsigned channel,
                                         unsigned source) {
    if (!owners->active[channel][source]) return (owner_change){ 0 };
    int destination = owners->destination[channel][source];
    owner_change change = {
        .note = destination,
        .old_depth = owners->max_depth[destination],
        .found = true,
    };
    owners->active[channel][source] = false;
    owners->depth[channel][source] = 0;
    change.last = --owners->count[destination] == 0;
    change.new_depth = change.last ? 0
                     : owner_destination_depth(owners, destination);
    owners->max_depth[destination] = change.new_depth;
    return change;
}

static inline owner_change owner_set_depth(midi_owners *owners,
                                           unsigned channel, unsigned source,
                                           uint8_t depth) {
    if (!owners->active[channel][source]) return (owner_change){ 0 };
    int destination = owners->destination[channel][source];
    owner_change change = {
        .note = destination,
        .old_depth = owners->max_depth[destination],
        .found = true,
    };
    owners->depth[channel][source] = depth;
    change.new_depth = owner_destination_depth(owners, destination);
    owners->max_depth[destination] = change.new_depth;
    return change;
}

#endif
