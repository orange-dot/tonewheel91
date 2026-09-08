/* MA2-5: pan reference, raw routing, shared chassis and lifecycle checks. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../src/ma_internal.h"

static unsigned checks, failures;
#define CHECK(condition, message) do { \
    checks++; \
    if (!(condition)) { failures++; printf("FAIL %d: %s\n", __LINE__, message); } \
} while (0)

static void pan_reference(void) {
    for (unsigned step = 0; step <= 128; step++) {
        ma_patch patch = ma_patch_tepih;
        patch.width = (float)step / 128;
        ma_card_bank bank = { 0 };
        ma_card_bank_init_patch(&bank, 48000, &patch);
        for (unsigned slot = 0; slot < MA_CARD_COUNT; slot++) {
            ma_card_pan pan = bank.stereo.pan[slot];
            double position = ((double)slot - 2) * .375 * patch.width;
            double angle = (position + 1) * acos(-1) * .25;
            CHECK(fabs(pan.left - sqrt(2) * cos(angle)) < 2e-6
                  && fabs(pan.right - sqrt(2) * sin(angle)) < 2e-6,
                  "pan must match the independent equal-power reference");
            CHECK(fabs(pan.left * pan.left + pan.right * pan.right - 2) < 5e-6,
                  "pan power must remain normalized to two");
            CHECK(pan.position == (float)position && pan.left >= 0 && pan.right >= 0,
                  "fixed slot position and nonnegative coefficients");
            if (slot > 0)
                CHECK(pan.position >= bank.stereo.pan[slot - 1].position
                      && pan.left <= bank.stereo.pan[slot - 1].left
                      && pan.right >= bank.stereo.pan[slot - 1].right,
                      "slot order must remain monotonic at every width");
        }
        CHECK(bank.stereo.pan[2].left == 1 && bank.stereo.pan[2].right == 1,
              "center card coefficients must be literal unity");
    }
}

static void raw_and_center(void) {
    const float rates[] = { 44100, 48000, 96000, 192000 };
    for (unsigned rate = 0; rate < sizeof rates / sizeof *rates; rate++) {
        ma_card_bank bank = { 0 };
        ma_card_bank_init_patch(&bank, rates[rate], &ma_patch_granica);
        ma_card_bank_set_character(&bank, 0);
        bank.cursor = 2;
        (void)ma_card_bank_note_on(&bank, 0, 60, 100);
        ma_synth solo = { 0 };
        ma_synth_init_patch(&solo, rates[rate], &ma_patch_granica);
        ma_synth_note_on(&solo, 0, 60, 100);
        ma_synth raw = solo;
        ma_output_state untouched = raw.output;
        bool equal = true, raw_equal = true;
        for (unsigned frame = 0; frame < 4096; frame++) {
            ma_frame reference = ma_synth_tick(&solo);
            float tap = ma_voice_tick_raw(&raw);
            ma_frame stereo = ma_card_bank_tick_stereo(&bank);
            equal = equal && memcmp(&reference, &stereo, sizeof reference) == 0;
            raw_equal = raw_equal && tap == solo.output.pre_body;
        }
        CHECK(equal, "one center card must preserve the complete MA1 PCM path");
        CHECK(raw_equal && memcmp(&raw.output, &untouched, sizeof untouched) == 0,
              "raw rendering must equal the post-VCA tap and leave output state untouched");
        CHECK(bank.card[2].oscillator1.phase_q48 == solo.oscillator1.phase_q48
              && bank.card[2].noise_state == solo.noise_state,
              "stereo bank must render each card exactly once per sample");
    }
}

static void shared_body(void) {
    ma_patch patch = ma_patch_tepih;
    patch.body_drive = .8f;
    patch.width = 1;
    patch.crossfeed = .3f;
    ma_card_bank bank = { 0 };
    ma_card_bank_init_patch(&bank, 48000, &patch);
    for (unsigned slot = 0; slot < MA_CARD_COUNT; slot++)
        (void)ma_card_bank_note_on(&bank, 0, (uint8_t)(48 + 5 * slot), 120);
    ma_card_bank reference = bank;
    tw_drive body = bank.stereo.output.body;
    bool routed = true, body_once = true;
    double difference = 0;
    for (unsigned frame = 0; frame < 4096; frame++) {
        float left = 0, right = 0;
        for (unsigned slot = 0; slot < MA_CARD_COUNT; slot++) {
            float raw = ma_voice_tick_raw(&reference.card[slot]);
            left += raw * bank.stereo.pan[slot].left;
            right += raw * bank.stereo.pan[slot].right;
        }
        float mid = .5f * left + .5f * right;
        float side = .5f * left - .5f * right;
        ma_frame output = ma_card_bank_tick_stereo(&bank);
        tw_drive_set(&body, patch.body_drive);
        float expected = .25f * tw_drive_tick(&body, 4 * mid);
        routed = routed && bank.stereo.output.pre_body == mid
               && bank.stereo.pre_side == side
               && bank.stereo.post_side == side * (1 - patch.crossfeed);
        body_once = body_once && bank.stereo.output.post_body == expected;
        difference += fabs(output.left - output.right);
    }
    CHECK(routed, "pan before sum; crossfeed scales only side, without a second width gain");
    CHECK(body_once && memcmp(&body, &bank.stereo.output.body, sizeof body) == 0,
          "shared body must receive the unnormalized sum exactly once");
    CHECK(difference > .01, "five-note bank must produce audible stereo differences");
    ma_output_state dry = bank.stereo.output;
    tw_drive frozen = dry.body;
    ma_frame output = ma_output_render(&dry, .15f, .05f, 0, 1.5f, 1);
    CHECK(dry.post_body == .15f && memcmp(&dry.body, &frozen, sizeof frozen) == 0
          && output.left != output.right, "zero body drive preserves state and bypasses load scaling");

    ma_output_state side_only = { 0 };
    tw_drive_init(&side_only.body, 48000);
    output = ma_output_render(&side_only, 0, .2f, 1, 1.5f, 1);
    CHECK(output.left == .2f && output.right == -.2f,
          "side-only input must not pass through nonlinear body");

    ma_output_state invalid = bank.stereo.output;
    frozen = invalid.body;
    output = ma_output_render(&invalid, NAN, .1f, 1, 1, 1);
    CHECK(output.left == 0 && output.right == 0
          && invalid.diagnostics.sanitization_count == 2
          && memcmp(&frozen, &invalid.body, sizeof frozen) == 0,
          "nonfinite bus input must be counted without poisoning the chassis");
    ma_output_state loud = { 0 };
    output = ma_output_render(&loud, 2, 1, 0, 1, 1);
    CHECK(output.left == 1 && output.right == 1
          && loud.diagnostics.pre_peak == 3
          && loud.diagnostics.post_peak == 1
          && loud.diagnostics.maximum_reduction == 2,
          "shared safety must retain pre/post peak evidence");
}

static void automation_and_lifecycle(void) {
    ma_card_bank bank = { 0 };
    ma_card_bank_init(&bank, 48000);
    (void)ma_card_bank_note_on(&bank, 0, 48, 110);
    for (unsigned i = 0; i < 1024; i++) (void)ma_card_bank_tick_stereo(&bank);
    CHECK(bank.stereo.output.dc_lp_left != bank.stereo.output.dc_lp_right,
          "mono-transition fixture must have stereo DC history");
    ma_card_bank_set_output(&bank, .1f, 0, 0, .18f);
    ma_frame mono = ma_card_bank_tick_stereo(&bank);
    CHECK(memcmp(&mono.left, &mono.right, sizeof mono.left) == 0
          && bank.stereo.pre_side == 0,
          "width zero must be exact mono on the first frame after stereo");
    ma_card_bank_set_output(&bank, .1f, 1, 0, .18f);
    float previous = 0;
    bool smooth = true;
    for (unsigned i = 0; i < 288; i++) {
        (void)ma_card_bank_tick_stereo(&bank);
        float current = bank.stereo.pan[4].position;
        smooth = smooth && current >= previous && current - previous < .003f;
        previous = current;
    }
    CHECK(smooth && previous == .75f, "width must reopen from mono in six milliseconds");
    ma_card_bank reference = bank;
    ma_card_bank_set_output(&bank, .1f, 1, 1, .18f);
    bool same_mid = true;
    for (unsigned i = 0; i < 288; i++) {
        mono = ma_card_bank_tick_stereo(&bank);
        (void)ma_card_bank_tick_stereo(&reference);
        same_mid = same_mid && bank.stereo.output.pre_body == reference.stereo.output.pre_body
                 && bank.stereo.output.post_body == reference.stereo.output.post_body;
    }
    CHECK(same_mid && bank.stereo.post_side == 0
          && memcmp(&mono.left, &mono.right, sizeof mono.left) == 0,
          "full crossfeed must collapse side while preserving the mid/body path");

    for (unsigned slot = 0; slot < MA_CARD_COUNT; slot++)
        ma_synth_set_macro(&bank.card[slot], MA_MACRO_SWARM, 1);
    for (unsigned i = 0; i < 288; i++) (void)ma_card_bank_tick_stereo(&bank);
    CHECK(bank.stereo.dispersion > 0 && bank.stereo.pan[4].position > .75f,
          "identity dispersion must reach bounded card pan offsets");
    ma_card_bank_set_output(&bank, .1f, 0, 0, .18f);
    mono = ma_card_bank_tick_stereo(&bank);
    CHECK(mono.left == mono.right && bank.stereo.pan[4].position == 0,
          "direct zero width must override identity widening");
    ma_stereo_state saved = bank.stereo;
    ma_card_bank_set_sustain(&bank, true);
    (void)ma_card_bank_note_off(&bank, 0, 48, 0);
    ma_card_bank_panic(&bank);
    ma_card_bank_set_unison(&bank, true);
    (void)ma_card_bank_note_on(&bank, 0, 72, 110);
    ma_card_bank_set_unison(&bank, false);
    CHECK(memcmp(&bank.stereo, &saved, sizeof saved) == 0,
          "sustain, panic and unison transitions must retain shared output state");
    ma_card_bank_set_output(&bank, NAN, INFINITY, -INFINITY, NAN);
    CHECK(bank.card[0].body_drive == .1f && bank.card[0].width == .7f
          && bank.card[0].crossfeed == 0 && bank.card[0].master_level == .18f,
          "bank setter must reuse the MA1 hostile-control fallback contract");
}

static void render_sweep(void) {
    const float rates[] = { 44100, 48000, 96000, 192000 };
    for (unsigned rate = 0; rate < sizeof rates / sizeof *rates; rate++) {
        ma_patch patch = ma_patch_granica;
        patch.body_drive = 1;
        patch.master_level = 1;
        patch.filter_resonance = 1;
        ma_card_bank a = { 0 }, b = { 0 };
        ma_card_bank_init_patch(&a, rates[rate], &patch);
        ma_card_bank_init_patch(&b, rates[rate], &patch);
        ma_card_bank_set_character(&a, 1);
        ma_card_bank_set_character(&b, 1);
        bool valid = true;
        for (unsigned frame = 0; frame < 128 * 64; frame++) {
            if (frame % 64 == 0) {
                uint8_t note = (uint8_t)(frame / 64);
                (void)ma_card_bank_note_on(&a, 0, note, 127);
                (void)ma_card_bank_note_on(&b, 0, note, 127);
            }
            ma_frame x = ma_card_bank_tick_stereo(&a);
            ma_frame y = ma_card_bank_tick_stereo(&b);
            valid = valid && isfinite(x.left) && isfinite(x.right)
                  && fabsf(x.left) <= 1 && fabsf(x.right) <= 1
                  && isfinite(a.stereo.output.pre_body)
                  && isfinite(a.stereo.output.post_body)
                  && memcmp(&x, &y, sizeof x) == 0;
        }
        CHECK(valid && a.stereo.output.diagnostics.sanitization_count == 0,
              "full compass/steal stress must repeat and stay finite before and after safety");
    }
}

int main(void) {
    pan_reference();
    raw_and_center();
    shared_body();
    automation_and_lifecycle();
    render_sweep();
    printf("ma stereo: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
