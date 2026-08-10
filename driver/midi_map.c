#include "midi_map.h"
#include "midi_owner.h"

static int fold_note(int note, int minimum, int maximum) {
    while (note < minimum) note += 12;
    while (note > maximum) note -= 12;
    return note;
}

static int mapped_note(bool fold, int source, int minimum, int maximum,
                       midi_map_stats *stats) {
    int destination = fold ? fold_note(source, minimum, maximum) : source;
    if (destination != source) stats->folded++;
    return destination;
}

void organ_midi_map_init(organ_midi_map *map, bool fold, bool percussion_on) {
    *map = (organ_midi_map){
        .fold = fold,
        .percussion_on = percussion_on,
        .percussion_normal = true,
    };
}

static void apply_percussion(const organ_midi_map *map, tw_organ *organ) {
    tw_organ_set_percussion(organ, map->percussion_on, map->percussion_third,
                            map->percussion_slow, map->percussion_normal);
}

void organ_midi_apply(organ_midi_map *map, tw_instrument *instrument,
                      uint8_t status, uint8_t d1, uint8_t d2) {
    unsigned channel = status & 0x0fu;
    switch (status & 0xf0u) {
    case 0x90: {
        map->stats.notes++;
        int note = mapped_note(map->fold, d1, TW_NOTE_MIN, TW_NOTE_MAX,
                               &map->stats);
        if (note < TW_NOTE_MIN || note > TW_NOTE_MAX) {
            tw_organ_note(&instrument->organ, note, true, d2);
            return;
        }
        if (!d2) {
            owner_change change = owner_release(&map->owners, channel, d1);
            if (change.last)
                tw_organ_note(&instrument->organ, change.note, false, 0);
            else if (change.found && change.old_depth != change.new_depth)
                tw_organ_note_depth(&instrument->organ, change.note,
                                    change.new_depth);
            return;
        }
        owner_change change = owner_press(&map->owners, channel, d1, note);
        if (change.first)
            tw_organ_note(&instrument->organ, note, true, d2);
        else if (change.old_depth != change.new_depth)
            tw_organ_note_depth(&instrument->organ, note, change.new_depth);
        return;
    }
    case 0x80: {
        map->stats.notes++;
        int note = mapped_note(map->fold, d1, TW_NOTE_MIN, TW_NOTE_MAX,
                               &map->stats);
        if (note < TW_NOTE_MIN || note > TW_NOTE_MAX) {
            tw_organ_note(&instrument->organ, note, false, d2);
            return;
        }
        owner_change change = owner_release(&map->owners, channel, d1);
        if (change.last)
            tw_organ_note(&instrument->organ, change.note, false, d2);
        else if (change.found && change.old_depth != change.new_depth)
            tw_organ_note_depth(&instrument->organ, change.note, change.new_depth);
        return;
    }
    case 0xa0: {
        map->stats.depths++;
        int note = mapped_note(map->fold, d1, TW_NOTE_MIN, TW_NOTE_MAX,
                               &map->stats);
        if (note < TW_NOTE_MIN || note > TW_NOTE_MAX) {
            tw_organ_note_depth(&instrument->organ, note, d2);
            return;
        }
        owner_change change = owner_set_depth(&map->owners, channel, d1, d2);
        if (change.found && change.old_depth != change.new_depth)
            tw_organ_note_depth(&instrument->organ, change.note, change.new_depth);
        return;
    }
    case 0xb0:
        break;
    default:
        return;
    }

    bool recognized = true;
    if (d1 == 11) tw_organ_set_swell(&instrument->organ, (float)d2 / 127.0f);
    else if (d1 >= 70 && d1 <= 78)
        tw_organ_set_drawbar(&instrument->organ, d1 - 70, (d2 * 8 + 63) / 127);
    else if (d1 == 120 || d1 == 123) {
        tw_organ_panic(&instrument->organ);
        map->owners = (midi_owners){ 0 };
    } else if (d1 >= 80 && d1 <= 83) {
        bool value = d2 >= 64;
        if (d1 == 80) map->percussion_on = value;
        else if (d1 == 81) map->percussion_third = value;
        else if (d1 == 82) map->percussion_slow = value;
        else map->percussion_normal = value;
        apply_percussion(map, &instrument->organ);
    } else if (d1 == 84) tw_organ_set_vibrato(&instrument->organ, d2 / 19);
    else if (d1 == 85) tw_instrument_set_drive(instrument, (float)d2 / 127.0f);
    else if (d1 == 86) tw_rotary_set_mode(&instrument->rotary, d2 / 32);
    else if (d1 == 87)
        tw_rotary_set_mode(&instrument->rotary,
                           d2 >= 64 ? TW_ROT_TREMOLO : TW_ROT_CHORALE);
    else if (d1 == 88)
        tw_rotary_set_balance(&instrument->rotary, (float)d2 / 127.0f);
    else if (d1 == 89)
        tw_rotary_set_width(&instrument->rotary, (float)d2 / 127.0f);
    else if (d1 == 90)
        tw_rotary_set_drive(&instrument->rotary, (float)d2 / 127.0f);
    else recognized = false;
    if (recognized) map->stats.ccs++;
}
