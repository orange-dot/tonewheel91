/* Mamut Analog MA2 fixed-card bank and allocator. */
#include "mamutanalog.h"
#include <float.h>

static uint64_t character_seed(uint8_t slot, uint64_t tag) {
    return UINT64_C(0x4d41434841523031)
         ^ ((uint64_t)(slot + 1) * UINT64_C(0x9e3779b97f4a7c15)) ^ tag;
}

static float character_draw(uint8_t slot, uint64_t tag) {
    uint64_t state = character_seed(slot, tag);
    uint32_t top = (uint32_t)(tw_splitmix64(&state) >> 40);
    return (float)top * (1.0f / 8388608.0f) - 1.0f;
}

static ma_character card_character(uint8_t slot) {
    return (ma_character){
        .amount = .20f,
        .smoother = { .current = .20f, .target = .20f },
        .vco1_cents = 6.0f * character_draw(slot, 0x4d410101),
        .vco2_cents = 8.0f * character_draw(slot, 0x4d410102),
        .cutoff_octaves = .12f * character_draw(slot, 0x4d410104),
        .amp_time_bias = {
            .attack = .08f * character_draw(slot, 0x4d410110),
            .decay = .08f * character_draw(slot, 0x4d410111),
            .release = .08f * character_draw(slot, 0x4d410113),
        },
        .filter_time_bias = {
            .attack = .08f * character_draw(slot, 0x4d410120),
            .decay = .08f * character_draw(slot, 0x4d410121),
            .release = .08f * character_draw(slot, 0x4d410123),
        },
        .vca_bias = .03f * character_draw(slot, 0x4d410130),
        .walk_state = character_seed(slot, 0x4d410103),
        .assigned = true,
    };
}

static void reap_idle_cards(ma_card_bank *bank) {
    for (uint8_t slot = 0; slot < MA_CARD_COUNT; slot++) {
        ma_card_owner *owner = &bank->owner[slot];
        if (owner->phase == MA_CARD_RELEASED
            && bank->card[slot].amp_envelope.stage == MA_ENVELOPE_IDLE)
            *owner = (ma_card_owner){ .phase = MA_CARD_IDLE };
    }
}

static void release_card(ma_card_bank *bank, uint8_t slot,
                         uint8_t release_velocity) {
    ma_card_owner *owner = &bank->owner[slot];
    ma_synth_note_off(&bank->card[slot], owner->channel, owner->note,
                      release_velocity);
    owner->phase = MA_CARD_RELEASED;
}

void ma_card_bank_init_patch(ma_card_bank *bank, float sample_rate_hz,
                             const ma_patch *patch) {
    *bank = (ma_card_bank){ .next_age = 1 };
    for (uint8_t slot = 0; slot < MA_CARD_COUNT; slot++) {
        ma_synth_init_patch(&bank->card[slot], sample_rate_hz, patch);
        bank->card[slot].character = card_character(slot);
    }
}

void ma_card_bank_set_character(ma_card_bank *bank, float amount) {
    if (!(amount >= -FLT_MAX && amount <= FLT_MAX)) amount = 0.0f;
    if (amount < 0.0f) amount = 0.0f;
    if (amount > 1.0f) amount = 1.0f;
    for (uint8_t slot = 0; slot < MA_CARD_COUNT; slot++)
        bank->card[slot].character.amount = amount;
}

void ma_card_bank_init(ma_card_bank *bank, float sample_rate_hz) {
    ma_card_bank_init_patch(bank, sample_rate_hz, &ma_patch_tepih);
}

uint8_t ma_card_bank_note_off(ma_card_bank *bank, uint8_t channel,
                              uint8_t note, uint8_t release_velocity) {
    if (channel >= 16 || note >= 128) return MA_CARD_NONE;
    bank->ignored_release_velocities++;

    if (bank->unison) {
        uint8_t chosen = MA_CARD_NONE;
        for (uint8_t slot = 0; slot < MA_CARD_COUNT; slot++) {
            ma_card_owner *owner = &bank->owner[slot];
            if (owner->phase != MA_CARD_HELD || owner->channel != channel
                || owner->note != note)
                continue;
            if (chosen == MA_CARD_NONE) chosen = slot;
            if (bank->sustain)
                owner->phase = MA_CARD_SUSTAINED;
            else
                release_card(bank, slot, release_velocity);
        }
        return chosen;
    }

    uint8_t chosen = MA_CARD_NONE;
    for (uint8_t slot = 0; slot < MA_CARD_COUNT; slot++) {
        ma_card_owner const *owner = &bank->owner[slot];
        if (owner->phase == MA_CARD_HELD && owner->channel == channel
            && owner->note == note
            && (chosen == MA_CARD_NONE
                || owner->age < bank->owner[chosen].age))
            chosen = slot;
    }
    if (chosen == MA_CARD_NONE) return chosen;

    if (bank->sustain)
        bank->owner[chosen].phase = MA_CARD_SUSTAINED;
    else
        release_card(bank, chosen, release_velocity);
    return chosen;
}

uint8_t ma_card_bank_note_on(ma_card_bank *bank, uint8_t channel,
                             uint8_t note, uint8_t velocity) {
    if (channel >= 16 || note >= 128) return MA_CARD_NONE;
    if (velocity == 0)
        return ma_card_bank_note_off(bank, channel, note, 0);

    if (bank->unison) {
        for (uint8_t slot = 0; slot < MA_CARD_COUNT; slot++) {
            ma_synth_note_on(&bank->card[slot], channel, note, velocity);
            bank->owner[slot] = (ma_card_owner){
                .age = bank->next_age++,
                .channel = channel,
                .note = note,
                .phase = MA_CARD_HELD,
            };
        }
        bank->cursor = 0;
        return 0;
    }

    reap_idle_cards(bank);
    uint8_t chosen = MA_CARD_NONE;
    for (uint8_t offset = 0; offset < MA_CARD_COUNT; offset++) {
        uint8_t slot = (uint8_t)((bank->cursor + offset) % MA_CARD_COUNT);
        if (bank->owner[slot].phase == MA_CARD_IDLE) {
            chosen = slot;
            break;
        }
    }
    if (chosen == MA_CARD_NONE) {
        for (uint8_t slot = 0; slot < MA_CARD_COUNT; slot++) {
            if (chosen == MA_CARD_NONE
                || bank->owner[slot].age < bank->owner[chosen].age)
                chosen = slot;
        }
    }

    ma_synth_note_on(&bank->card[chosen], channel, note, velocity);
    bank->owner[chosen] = (ma_card_owner){
        .age = bank->next_age++,
        .channel = channel,
        .note = note,
        .phase = MA_CARD_HELD,
    };
    bank->cursor = (uint8_t)((chosen + 1) % MA_CARD_COUNT);
    return chosen;
}

void ma_card_bank_set_sustain(ma_card_bank *bank, bool down) {
    if (bank->sustain == down) return;
    bank->sustain = down;
    if (down) return;

    for (uint8_t slot = 0; slot < MA_CARD_COUNT; slot++)
        if (bank->owner[slot].phase == MA_CARD_SUSTAINED)
            release_card(bank, slot, 0);
}

void ma_card_bank_panic(ma_card_bank *bank) {
    bank->sustain = false;
    for (uint8_t slot = 0; slot < MA_CARD_COUNT; slot++)
        if (bank->owner[slot].phase == MA_CARD_HELD
            || bank->owner[slot].phase == MA_CARD_SUSTAINED)
            release_card(bank, slot, 0);
    reap_idle_cards(bank);
}

void ma_card_bank_set_unison(ma_card_bank *bank, bool enabled) {
    if (bank->unison == enabled) return;
    ma_card_bank_panic(bank);
    bank->unison = enabled;
}

void ma_card_bank_tick(ma_card_bank *bank,
                       ma_frame frames[static MA_CARD_COUNT]) {
    for (uint8_t slot = 0; slot < MA_CARD_COUNT; slot++)
        frames[slot] = ma_synth_tick(&bank->card[slot]);
    reap_idle_cards(bank);
}
