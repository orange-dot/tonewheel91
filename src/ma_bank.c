/* Mamut Analog MA2 fixed-card bank and allocator. */
#include "mamutanalog.h"

static void reap_idle_cards(ma_card_bank *bank) {
    for (uint8_t slot = 0; slot < MA_CARD_COUNT; slot++) {
        ma_card_owner *owner = &bank->owner[slot];
        if (owner->phase == MA_CARD_RELEASED
            && bank->card[slot].amp_envelope.stage == MA_ENVELOPE_IDLE)
            *owner = (ma_card_owner){ .phase = MA_CARD_IDLE };
    }
}

void ma_card_bank_init_patch(ma_card_bank *bank, float sample_rate_hz,
                             const ma_patch *patch) {
    *bank = (ma_card_bank){ .next_age = 1 };
    for (uint8_t slot = 0; slot < MA_CARD_COUNT; slot++)
        ma_synth_init_patch(&bank->card[slot], sample_rate_hz, patch);
}

void ma_card_bank_init(ma_card_bank *bank, float sample_rate_hz) {
    ma_card_bank_init_patch(bank, sample_rate_hz, &ma_patch_tepih);
}

uint8_t ma_card_bank_note_off(ma_card_bank *bank, uint8_t channel,
                              uint8_t note, uint8_t release_velocity) {
    if (channel >= 16 || note >= 128) return MA_CARD_NONE;
    bank->ignored_release_velocities++;

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

    ma_synth_note_off(&bank->card[chosen], channel, note, release_velocity);
    bank->owner[chosen].phase = MA_CARD_RELEASED;
    return chosen;
}

uint8_t ma_card_bank_note_on(ma_card_bank *bank, uint8_t channel,
                             uint8_t note, uint8_t velocity) {
    if (channel >= 16 || note >= 128) return MA_CARD_NONE;
    if (velocity == 0)
        return ma_card_bank_note_off(bank, channel, note, 0);

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

void ma_card_bank_tick(ma_card_bank *bank,
                       ma_frame frames[static MA_CARD_COUNT]) {
    for (uint8_t slot = 0; slot < MA_CARD_COUNT; slot++)
        frames[slot] = ma_synth_tick(&bank->card[slot]);
    reap_idle_cards(bank);
}
