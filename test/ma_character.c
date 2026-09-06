/* MA2-4 calibration, routing, bypass and physical-card lifecycle referees. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../src/mamutanalog.h"

static unsigned checks, failures;
#define CHECK(condition, message) do { \
    checks++; \
    if (!(condition)) { failures++; printf("FAIL %u: %s\n", __LINE__, message); } \
} while (0)

static void calibration(void) {
    /* Independently derived SplitMix top-24 words; column order matches values. */
    static constexpr uint32_t golden[MA_CARD_COUNT][10] = {
        { 0x2f48bf, 0x5dd189, 0x8f9c02, 0x1e0e34, 0x7902f6, 0x8215ce, 0xb95ca7, 0x439600, 0xfce95e, 0xb6194c },
        { 0xfbe4d8, 0x43847c, 0xc4600c, 0xaaaeeb, 0x7dd431, 0xccb6c9, 0x06da8e, 0xf457e2, 0x2ceccb, 0xdf1a40 },
        { 0x09fce0, 0xc32e8d, 0xf6c4fd, 0x881b12, 0x2885ce, 0x2313cc, 0x053e9d, 0x74df98, 0x5abaf0, 0x85fb89 },
        { 0x6e9b8e, 0x9e3250, 0x305195, 0x168356, 0x5b5e40, 0xb3a073, 0x26fe83, 0x0f5ffe, 0xf4a16f, 0x7990ac },
        { 0x470d7f, 0x1fd508, 0xd85213, 0xba0281, 0x619821, 0xad1853, 0xdc67bf, 0x7fe07b, 0xf20f77, 0xf445dd },
    };
    static constexpr float bounds[] = { 6, 8, .12f, .08f, .08f, .08f,
                                        .08f, .08f, .08f, .03f };
    ma_card_bank bank = { 0 };
    ma_card_bank_init(&bank, 48000);
    for (unsigned slot = 0; slot < MA_CARD_COUNT; slot++) {
        ma_character c = bank.card[slot].character;
        float values[] = { c.vco1_cents, c.vco2_cents, c.cutoff_octaves,
            c.amp_time_bias.attack, c.amp_time_bias.decay,
            c.amp_time_bias.release, c.filter_time_bias.attack,
            c.filter_time_bias.decay, c.filter_time_bias.release, c.vca_bias };
        CHECK(c.assigned && c.amount == .20f && c.smoother.current == .20f,
              "factory character must start at .20 on every card");
        for (unsigned parameter = 0; parameter < 10; parameter++) {
            float draw = (float)((int32_t)golden[slot][parameter] - 8388608)
                       / 8388608.0f;
            CHECK(values[parameter] == bounds[parameter] * draw,
                  "stable card/tag calibration vector");
        }
    }
    const float inputs[] = { -1, 2, NAN, INFINITY, -INFINITY, .35f };
    const float expected[] = { 0, 1, 0, 0, 0, .35f };
    for (unsigned i = 0; i < sizeof inputs / sizeof *inputs; i++) {
        ma_card_bank_set_character(&bank, inputs[i]);
        for (unsigned slot = 0; slot < MA_CARD_COUNT; slot++)
            CHECK(bank.card[slot].character.amount == expected[i],
                  "character input sanitization");
    }
}

static void bypass_and_clock(void) {
    ma_card_bank bank = { 0 };
    ma_card_bank_init_patch(&bank, 48000, &ma_patch_granica);
    ma_card_bank_set_character(&bank, 0);
    ma_synth solo[MA_CARD_COUNT] = { 0 };
    for (unsigned slot = 0; slot < MA_CARD_COUNT; slot++) {
        ma_synth_init_patch(&solo[slot], 48000, &ma_patch_granica);
        (void)ma_card_bank_note_on(&bank, 0, (uint8_t)(48 + slot * 7), 105);
        ma_synth_note_on(&solo[slot], 0, (uint8_t)(48 + slot * 7), 105);
    }
    uint64_t seed = bank.card[0].character.walk_state;
    bool equal = true;
    for (unsigned frame = 0; frame < 4096; frame++) {
        ma_frame cards[MA_CARD_COUNT] = { 0 };
        ma_card_bank_tick(&bank, cards);
        for (unsigned slot = 0; slot < MA_CARD_COUNT; slot++) {
            ma_frame reference = ma_synth_tick(&solo[slot]);
            equal = equal && memcmp(&reference, &cards[slot], sizeof reference) == 0;
        }
        if (frame == 30)
            CHECK(bank.card[0].character.walk_state == seed,
                  "walk must wait for sample 32");
        if (frame == 31) {
            uint64_t word = tw_splitmix64(&seed);
            float draw = (float)(word >> 40) / 8388608.0f - 1.0f;
            CHECK(bank.card[0].character.walk_state == seed
                  && bank.card[0].character.walk_target == .03f * draw
                  && bank.card[0].character.walk_cents == 0,
                  "first walk draw at 32, with interpolation starting next sample");
        }
    }
    CHECK(equal, "character zero must equal five standalone pre-character voices");
    CHECK(bank.card[0].character.walk_cents != 0,
          "physical walk clock must continue during character bypass");
    ma_character saved = bank.card[0].character;
    (void)ma_card_bank_note_on(&bank, 0, 96, 100);
    ma_card_bank_set_sustain(&bank, true);
    (void)ma_card_bank_note_off(&bank, 0, 96, 0);
    ma_card_bank_panic(&bank);
    ma_card_bank_set_unison(&bank, true);
    (void)ma_card_bank_note_on(&bank, 0, 60, 100);
    ma_card_bank_set_unison(&bank, false);
    ma_synth_apply_patch(&bank.card[0], &ma_patch_lead);
    CHECK(bank.card[0].character.walk_state == saved.walk_state
          && bank.card[0].character.walk_cents == saved.walk_cents
          && bank.card[0].character.vco1_cents == saved.vco1_cents
          && bank.card[0].character.vca_bias == saved.vca_bias,
          "steal, sustain, panic, unison and patch edits retain physical character");

    ma_card_bank_set_character(&bank, 1);
    for (unsigned frame = 0; frame < 288; frame++) {
        ma_frame cards[MA_CARD_COUNT] = { 0 };
        ma_card_bank_tick(&bank, cards);
        CHECK(bank.card[0].character.smoother.current > 0
              && bank.card[0].character.smoother.current <= 1,
              "positive character edits ramp without overshoot");
    }
    CHECK(bank.card[0].character.smoother.current == 1
          && bank.card[0].character.smoother.remaining == 0,
          "positive edit settles at six milliseconds");
    ma_card_bank_set_character(&bank, 0);
    (void)ma_synth_tick(&bank.card[0]);
    CHECK(bank.card[0].character.smoother.current == 0,
          "zero bypass takes effect on the next frame");
}

static void routing(void) {
    ma_patch patch = ma_patch_tepih;
    patch.crossmod_amount = 0;
    patch.sync_amount = 0;
    ma_card_bank bank = { 0 };
    ma_card_bank_init_patch(&bank, 48000, &patch);
    for (unsigned slot = 0; slot < MA_CARD_COUNT; slot++) {
        for (unsigned level = 1; level <= 5; level += 4) {
            ma_synth s = bank.card[slot];
            s.character.amount = .20f * (float)level;
            s.character.smoother.current = s.character.amount;
            s.character.smoother.target = s.character.amount;
            ma_synth_note_on(&s, 0, 60, 100);
            ma_synth reference = s;
            reference.character.vco1_cents = 0;
            reference.character.vco2_cents = 0;
            reference.character.cutoff_octaves = 0;
            (void)ma_synth_tick(&s);
            (void)ma_synth_tick(&reference);
            double amount = s.character.amount;
            double ratio1 = exp2(amount * s.character.vco1_cents / 1200.0);
            double ratio2 = exp2(amount * (s.character.vco1_cents
                                          + s.character.vco2_cents) / 1200.0);
            CHECK(fabs((double)s.oscillator1.phase_q48
                       / (double)reference.oscillator1.phase_q48 - ratio1) < 2e-6,
                  "VCO1 static cents reach oscillator phase");
            CHECK(fabs((double)s.oscillator2.phase_q48
                       / (double)reference.oscillator2.phase_q48 - ratio2) < 2e-6,
                  "VCO2 receives common tuning and additional static cents");
            CHECK(fabs((double)s.filter_cutoff_effective_hz
                       / reference.filter_cutoff_effective_hz
                       - exp2(amount * s.character.cutoff_octaves)) < 2e-6,
                  "cutoff bias reaches ladder in octave units");
            for (unsigned stage = 0; stage < 3; stage++) {
                ma_envelope_stage stages[] = { MA_ENVELOPE_ATTACK,
                    MA_ENVELOPE_DECAY, MA_ENVELOPE_RELEASE };
                ma_synth probe = s;
                probe.amp_envelope = (ma_envelope){ .level = .6f, .stage = stages[stage] };
                probe.filter_envelope = probe.amp_envelope;
                ma_adsr authored[] = { s.amp_adsr, s.filter_adsr };
                ma_envelope_time_bias bias[] = {
                    s.character.amp_time_bias, s.character.filter_time_bias };
                (void)ma_synth_tick(&probe);
                float got[] = { probe.amp_envelope.level, probe.filter_envelope.level };
                for (unsigned envelope = 0; envelope < 2; envelope++) {
                    float times[] = { authored[envelope].attack_ms,
                        authored[envelope].decay_ms, authored[envelope].release_ms };
                    float offsets[] = { bias[envelope].attack,
                        bias[envelope].decay, bias[envelope].release };
                    double target = stage == 0 ? 1 : stage == 1 ? authored[envelope].sustain : 0;
                    double time = times[stage] * (1 + amount * offsets[stage]);
                    double expected = .6f + (1 - exp(-log(1000) / (.001 * time * 48000)))
                                            * (target - .6f);
                    CHECK(fabs(got[envelope] - expected) < 2e-7,
                          "each ADSR stage uses its independent time bias");
                }
            }
            for (unsigned frame = 0; frame < 1000; frame++) (void)ma_synth_tick(&s);
            reference = s;
            reference.character.vca_bias = 0;
            (void)ma_synth_tick(&s);
            (void)ma_synth_tick(&reference);
            CHECK(reference.output.pre_body != 0
                  && fabs((double)s.output.pre_body / reference.output.pre_body
                          - (1 + amount * s.character.vca_bias)) < 2e-6,
                  "VCA trim reaches pre-body output");
            CHECK(s.amp_adsr.attack_ms == patch.amp_adsr.attack_ms
                  && s.filter_adsr.release_ms == patch.filter_adsr.release_ms,
                  "character must not overwrite authored envelope controls");
            s.amp_envelope.stage = MA_ENVELOPE_SUSTAIN;
            s.filter_envelope.stage = MA_ENVELOPE_SUSTAIN;
            (void)ma_synth_tick(&s);
            CHECK(s.amp_envelope.level == patch.amp_adsr.sustain
                  && s.filter_envelope.level == patch.filter_adsr.sustain,
                  "character preserves both sustain levels");
        }
    }
}

static void walk_and_render(void) {
    const float rates[] = { 44100, 48000, 96000, 192000 };
    for (unsigned rate = 0; rate < sizeof rates / sizeof *rates; rate++) {
        ma_card_bank a = { 0 }, b = { 0 };
        ma_card_bank_init_patch(&a, rates[rate], &ma_patch_granica);
        ma_card_bank_init_patch(&b, rates[rate], &ma_patch_granica);
        ma_card_bank_set_character(&a, 1);
        ma_card_bank_set_character(&b, 1);
        uint64_t walk_seed = a.card[0].character.walk_state;
        bool valid = true, different = false;
        for (unsigned frame = 0; frame < 8192; frame++) {
            if (frame == 512) {
                ma_card_bank_set_unison(&a, true);
                ma_card_bank_set_unison(&b, true);
                (void)ma_card_bank_note_on(&a, 0, 127, 127);
                (void)ma_card_bank_note_on(&b, 0, 127, 127);
            }
            ma_frame left[MA_CARD_COUNT] = { 0 }, right[MA_CARD_COUNT] = { 0 };
            ma_card_bank_tick(&a, left);
            ma_card_bank_tick(&b, right);
            for (unsigned slot = 0; slot < MA_CARD_COUNT; slot++) {
                ma_character c = a.card[slot].character;
                valid = valid && isfinite(left[slot].left) && isfinite(left[slot].right)
                      && fabsf(left[slot].left) <= 1 && fabsf(c.walk_cents) <= 3
                      && fabsf(c.walk_target) <= 3
                      && memcmp(&left[slot], &right[slot], sizeof *left) == 0;
            }
            different = different || left[0].left != left[1].left;
        }
        CHECK(valid && different, "repeatable, distinct cards; finite highest-note render at every rate");
        CHECK(a.card[0].character.walk_state == b.card[0].character.walk_state
              && a.card[0].character.walk_state
                  == walk_seed + UINT64_C(256) * UINT64_C(0x9e3779b97f4a7c15),
              "idle and sounding cards share a deterministic physical clock");
        /* Force the bounded integrator to either edge, then cross a draw boundary. */
        for (int sign = -1; sign <= 1; sign += 2) {
            a.card[0].character.walk_target = 3.0f * (float)sign;
            a.card[0].character.walk_cents = a.card[0].character.walk_target;
            a.card[0].character.walk_step = 0;
            for (unsigned frame = 0; frame < 1024; frame++) {
                (void)ma_synth_tick(&a.card[0]);
                CHECK(fabsf(a.card[0].character.walk_target) <= 3
                      && fabsf(a.card[0].character.walk_cents) <= 3,
                      "walk remains bounded at both clamp edges");
            }
        }
    }
}

int main(void) {
    calibration();
    bypass_and_clock();
    routing();
    walk_and_render();
    printf("ma character: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
