#include "midi_map.h"
#include "midi_owner.h"

static int fold_note(int note) {
    while (note < EP_NOTE_MIN) note += 12;
    while (note > EP_NOTE_MAX) note -= 12;
    return note;
}

static int mapped_note(ep_midi_map *map, int source) {
    int destination = map->fold ? fold_note(source) : source;
    if (destination != source) map->stats.folded++;
    return destination;
}

void ep_midi_map_init(ep_midi_map *map, bool fold) {
    *map = (ep_midi_map){ .fold = fold };
}

void ep_midi_apply(ep_midi_map *map, ep_piano *piano,
                   uint8_t status, uint8_t d1, uint8_t d2) {
    unsigned channel = status & 0x0fu;
    switch (status & 0xf0u) {
    case 0x90: {
        map->stats.notes++;
        int note = mapped_note(map, d1);
        if (note < EP_NOTE_MIN || note > EP_NOTE_MAX) {
            ep_piano_note(piano, note, true, d2);
            return;
        }
        if (!d2) {
            owner_change change = owner_release(&map->owners, channel, d1);
            if (change.last) ep_piano_note(piano, change.note, false, 0);
            return;
        }
        (void)owner_press(&map->owners, channel, d1, note);
        ep_piano_note(piano, note, true, d2); /* every note-on may restrike */
        return;
    }
    case 0x80: {
        map->stats.notes++;
        int note = mapped_note(map, d1);
        if (note < EP_NOTE_MIN || note > EP_NOTE_MAX) {
            ep_piano_note(piano, note, false, 0);
            return;
        }
        owner_change change = owner_release(&map->owners, channel, d1);
        if (change.last) ep_piano_note(piano, change.note, false, 0);
        return;
    }
    case 0xa0:
        map->stats.pressures++;
        ep_piano_key_pressure(piano, mapped_note(map, d1), d2);
        return;
    case 0xb0:
        break;
    default:
        return;
    }

    bool recognized = true;
    if (d1 == 64) ep_piano_set_sustain(piano, d2 >= 64);
    else if (d1 == 85) ep_piano_set_drive(piano, (float)d2 / 127.0f);
    else if (d1 == 91) ep_piano_set_tremolo(piano, d2);
    else if (d1 == 92) ep_piano_set_cabinet(piano, (float)d2 / 127.0f);
    else if (d1 == 93) ep_piano_set_condition(piano, (float)d2 / 127.0f);
    else if (d1 == 120 || d1 == 123) {
        ep_piano_panic(piano);
        map->owners = (midi_owners){ 0 };
    } else recognized = false;
    if (recognized) map->stats.ccs++;
}
