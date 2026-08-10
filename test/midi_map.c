#include <stdio.h>

#include "../driver/midi_map.h"

static unsigned checks, failures;

#define CHECK(condition, ...) do {                                            \
    checks++;                                                                 \
    if (!(condition)) {                                                       \
        failures++;                                                           \
        fprintf(stderr, "FAIL: ");                                           \
        fprintf(stderr, __VA_ARGS__);                                         \
        fputc('\n', stderr);                                                   \
    }                                                                         \
} while (0)

static void test_organ_channels(void) {
    tw_instrument instrument;
    tw_instrument_init(&instrument, 48000.0f);
    organ_midi_map map;
    organ_midi_map_init(&map, false, false);
    int key = 60 - TW_NOTE_MIN;

    organ_midi_apply(&map, &instrument, 0x90, 60, 100);
    organ_midi_apply(&map, &instrument, 0x91, 60, 90);
    CHECK(instrument.organ.held[key] && map.owners.count[60] == 2,
          "two channels must own one organ key independently");
    organ_midi_apply(&map, &instrument, 0x80, 60, 0);
    CHECK(instrument.organ.held[key] && map.owners.count[60] == 1,
          "first channel note-off released another channel's organ key");
    organ_midi_apply(&map, &instrument, 0x81, 60, 0);
    CHECK(!instrument.organ.held[key] && map.owners.count[60] == 0,
          "last channel note-off did not release the organ key");

    organ_midi_apply(&map, &instrument, 0x90, 60, 100);
    organ_midi_apply(&map, &instrument, 0x90, 60, 110);
    CHECK(map.owners.count[60] == 1,
          "duplicate source note-on increased organ ownership");
    organ_midi_apply(&map, &instrument, 0x90, 60, 0);
    CHECK(!instrument.organ.held[key],
          "zero-velocity note-on did not release its source owner");

    unsigned before = instrument.organ.out_of_compass;
    organ_midi_apply(&map, &instrument, 0x90, 20, 100);
    organ_midi_apply(&map, &instrument, 0x91, 20, 100);
    organ_midi_apply(&map, &instrument, 0x80, 20, 0);
    organ_midi_apply(&map, &instrument, 0xa0, 20, 64);
    CHECK(instrument.organ.out_of_compass == before + 4,
          "ownership suppressed out-of-compass organ accounting");
}

static void test_organ_depth_and_fold(void) {
    tw_instrument instrument;
    tw_instrument_init(&instrument, 48000.0f);
    organ_midi_map map;
    organ_midi_map_init(&map, false, false);

    organ_midi_apply(&map, &instrument, 0x90, 60, 100);
    organ_midi_apply(&map, &instrument, 0x91, 60, 100);
    organ_midi_apply(&map, &instrument, 0xa0, 60, 40);
    organ_midi_apply(&map, &instrument, 0xa1, 60, 90);
    CHECK(map.owners.max_depth[60] == 90,
          "organ depth owners did not aggregate by maximum");
    organ_midi_apply(&map, &instrument, 0x81, 60, 0);
    CHECK(map.owners.max_depth[60] == 40
          && instrument.organ.held[60 - TW_NOTE_MIN],
          "organ depth did not fall to the remaining owner's value");
    uint8_t made = instrument.organ.made[60 - TW_NOTE_MIN];
    organ_midi_apply(&map, &instrument, 0xa2, 60, 127);
    CHECK(instrument.organ.made[60 - TW_NOTE_MIN] == made,
          "pressure from an inactive source changed another owner's key");

    tw_instrument_init(&instrument, 48000.0f);
    organ_midi_map_init(&map, true, false);
    organ_midi_apply(&map, &instrument, 0x90, 24, 100); /* folds to 36 */
    organ_midi_apply(&map, &instrument, 0x90, 36, 100);
    CHECK(map.owners.count[36] == 2 && instrument.organ.held[0],
          "two source pitches did not share their folded organ key");
    organ_midi_apply(&map, &instrument, 0x80, 24, 0);
    CHECK(instrument.organ.held[0] && map.owners.count[36] == 1,
          "first folded note-off released the remaining source");
    organ_midi_apply(&map, &instrument, 0xb0, 120, 0);
    CHECK(!instrument.organ.held[0] && map.owners.count[36] == 0,
          "organ panic did not clear core and hosted ownership");
}

static void test_ep_ownership(void) {
    ep_piano piano;
    ep_piano_init(&piano, 48000.0f);
    ep_midi_map map;
    ep_midi_map_init(&map, false);
    int key = 64 - EP_NOTE_MIN;

    ep_midi_apply(&map, &piano, 0x90, 64, 100);
    ep_midi_apply(&map, &piano, 0x91, 64, 90);
    CHECK(piano.held[key] && map.owners.count[64] == 2,
          "two channels must own one EP key independently");
    ep_midi_apply(&map, &piano, 0x80, 64, 0);
    CHECK(piano.held[key] && piano.bank.damper_up[key],
          "first channel note-off lowered an EP damper");
    ep_midi_apply(&map, &piano, 0x81, 64, 0);
    CHECK(!piano.held[key] && !piano.bank.damper_up[key],
          "last channel note-off did not lower the EP damper");

    ep_piano_init(&piano, 48000.0f);
    ep_midi_map_init(&map, false);
    ep_midi_apply(&map, &piano, 0x90, 64, 80);
    uint32_t strikes = piano.bank.strikes;
    ep_midi_apply(&map, &piano, 0x90, 64, 100);
    CHECK(map.owners.count[64] == 1 && piano.bank.strikes == strikes + 1,
          "repeated EP note-on must restrike without duplicating ownership");
    ep_midi_apply(&map, &piano, 0xb0, 123, 0);
    CHECK(!piano.held[key] && map.owners.count[64] == 0,
          "EP panic did not clear core and hosted ownership");

    unsigned before = piano.bank.out_of_compass;
    ep_midi_apply(&map, &piano, 0x90, 20, 100);
    ep_midi_apply(&map, &piano, 0x91, 20, 100);
    ep_midi_apply(&map, &piano, 0x80, 20, 0);
    CHECK(piano.bank.out_of_compass == before + 3,
          "ownership suppressed out-of-compass EP accounting");
}

static void test_ep_fold_and_controls(void) {
    ep_piano piano;
    ep_piano_init(&piano, 48000.0f);
    ep_midi_map map;
    ep_midi_map_init(&map, true);

    ep_midi_apply(&map, &piano, 0x90, 16, 80); /* folds to 28 */
    ep_midi_apply(&map, &piano, 0x90, 28, 90);
    CHECK(map.owners.count[28] == 2 && piano.held[0],
          "two source pitches did not share their folded EP key");
    ep_midi_apply(&map, &piano, 0x80, 16, 0);
    CHECK(piano.held[0] && map.owners.count[28] == 1,
          "first folded EP note-off lowered the shared damper");

    unsigned long before = map.stats.ccs;
    ep_midi_apply(&map, &piano, 0xb0, 85, 64);
    ep_midi_apply(&map, &piano, 0xb0, 91, 64);
    ep_midi_apply(&map, &piano, 0xb0, 92, 64);
    ep_midi_apply(&map, &piano, 0xb0, 93, 127);
    CHECK(map.stats.ccs == before + 4 && piano.bank.cond == 1.0f,
          "wired EP controls were not dispatched and counted");
    ep_midi_apply(&map, &piano, 0xb0, 1, 64);
    CHECK(map.stats.ccs == before + 4,
          "unknown EP control was counted as applied");
}

int main(void) {
    test_organ_channels();
    test_organ_depth_and_fold();
    test_ep_ownership();
    test_ep_fold_and_controls();
    printf("midi-map: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
