/* Oscillator study reference: the Mamut Analog oscillator alone (the MA_SOURCE_EVIDENCE
 * tick: sources only, no filter, no envelope), VCO1 with one shape, as
 * float32 mono at 48 kHz. Legato segments, each "hz:seconds". A shape may
 * carry the circuit amount (docs/ma-vco-plan.md), e.g. tri@1.
 *   build/vco_ma_ref tri|pulse|saw|sine|tri_sine|tri_saw[@AMOUNT] WIDTH OUT.f32 hz:sec ...
 * `pair@AMOUNT,SUPPLY,INTERVAL,CENTS` is two saws, VCO2 at the interval and
 * cents from VCO1, on the cores' supply (V4).
 * `sync@AMOUNT,SYNC,SOFTNESS,INTERVAL,CENTS` is VCO2's saw alone, synced to a
 * silent VCO1 (V5). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../../src/mamutanalog.h"

int main(int argc, char **argv) {
    if (argc < 5) return 2;
    static ma_synth s;
    ma_synth_init(&s, 48000.0f);
    ma_synth_set_mozaik(&s, 0.0f, 0.5601133f, 0.5150284f, 0.0f, 0.0f);
    ma_vco_controls c = { .pulse_width = (float)atof(argv[2]) };
    char shape[32];
    snprintf(shape, sizeof shape, "%s", argv[1]);
    char *at = strchr(shape, '@');
    float amount = at ? (float)atof(at + 1) : 0.0f;
    if (at) *at = 0;
    if (!strcmp(shape, "tri")) c.triangle_level = 1;
    else if (!strcmp(shape, "pulse")) c.pulse_level = 1;
    else if (!strcmp(shape, "sine")) c.sine_level = 1;
    else if (!strcmp(shape, "tri_sine")) c.tri_sine_level = 1;
    else if (!strcmp(shape, "tri_saw")) c.tri_saw_level = 1;
    else c.saw_level = 1;
    ma_synth_set_vco_analog(&s, amount, 0.0f);
    float supply = 0.0f, cents = 0.0f;
    int interval = 0;
    bool pair = !strcmp(shape, "pair"), sync = !strcmp(shape, "sync");
    float sync_amount = 0.0f, sync_softness = 0.0f;
    if (sync) {
        c.saw_level = 0;
        const char *x = strchr(at + 1, ',');
        if (x) { sync_amount = (float)atof(x + 1); x = strchr(x + 1, ','); }
        if (x) { sync_softness = (float)atof(x + 1); x = strchr(x + 1, ','); }
        if (x) { interval = atoi(x + 1); x = strchr(x + 1, ','); }
        if (x) cents = (float)atof(x + 1);
    }
    if (pair) {
        c.saw_level = 1;
        const char *x = strchr(at + 1, ',');
        if (x) { supply = (float)atof(x + 1); x = strchr(x + 1, ','); }
        if (x) { interval = atoi(x + 1); x = strchr(x + 1, ','); }
        if (x) cents = (float)atof(x + 1);
        ma_synth_set_vco_supply(&s, supply);
    }
    ma_synth_set_vco1(&s, c);
    ma_synth_set_vco2(&s, (ma_vco_controls){ .saw_level = pair || sync ? 1.0f : 0.0f, .pulse_width = .5f },
                      pair || sync ? 1.0f : 0.0f, interval, cents);
    ma_synth_set_oscillator_modulation(&s, sync_amount, sync_softness, 0, 0);
    ma_synth_set_glide(&s, false, 0);
    FILE *f = fopen(argv[3], "wb");
    if (!f) return 1;
    for (int a = 4; a < argc; a++) {
        double hz = atof(argv[a]), sec = atof(strchr(argv[a], ':') + 1);
        double semis = 69 + 12 * log2(hz / 440);
        int note = (int)lround(semis);
        ma_synth_set_pitch_bend(&s, (float)(semis - note));
        ma_synth_note_on(&s, 0, (uint8_t)note, 127);
        for (long n = lround(sec * 48000); n > 0; n--) {
            float x = ma_synth_tick(&s).left;
            fwrite(&x, sizeof x, 1, f);
        }
    }
    fclose(f);
    return 0;
}
