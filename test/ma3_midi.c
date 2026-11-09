#include "../driver/ma_midi_render.h"
#include "../driver/smf_schedule.h"
#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ma_midi_map map, bytes_map;
static ma_midi_renderer renderer;
static ma_voice_pool direct;

static void init(unsigned limit, unsigned unison) {
    ma_voice_pool_config c = ma_voice_pool_default_config;
    c.limit = (uint8_t)limit; c.unison_count = (uint8_t)unison;
    assert(ma_midi_map_init(&map, 48000, &c, NULL, UINT16_MAX));
}
static void tick(unsigned count) {
    for (unsigned i = 0; i < count; i++) {
        ma_frame f = ma_voice_pool_tick(&map.pool);
        assert(isfinite(f.left) && isfinite(f.right));
        assert(fabsf(f.left) <= 1 && fabsf(f.right) <= 1);
    }
}
static void ownership(void) {
    for (unsigned u = 1; u <= 8; u *= 2) {
        init(8, u);
        for (unsigned n = 0; n < 12; n++) ma_midi_apply(&map, 0x90, 60, 100);
        assert(map.pool.diagnostics.active_slots == 8);
        assert(map.pool.diagnostics.steals_held == 12 - 8/u);
        for (unsigned n = 0; n < 12; n++) ma_midi_apply(&map, 0x90, 60, 0);
        assert(map.stats.unmatched_note_offs == 12 - 8/u);
        assert(map.stats.note_ons == 12 && map.stats.note_offs == 12);
        for (unsigned g = 0; g < 8; g++) assert(map.pool.group[g].phase != MA_CARD_HELD);
    }
    init(8, 2);
    ma_midi_apply(&map, 0x90, 60, 100);
    ma_midi_apply(&map, 0x90, 60, 110);
    ma_midi_apply(&map, 0x91, 60, 90);
    ma_midi_apply(&map, 0xb0, 64, 127);
    ma_midi_apply(&map, 0x80, 60, 40);
    assert(map.pool.group[0].phase == MA_CARD_SUSTAINED);
    assert(map.pool.group[1].phase == MA_CARD_HELD);
    ma_midi_apply(&map, 0x81, 60, 0);
    assert(map.pool.group[2].phase == MA_CARD_RELEASED);
    ma_midi_apply(&map, 0xa0, 60, 100);
    for (unsigned i = 0; i < 4; i++) assert(map.pool.slot[i].voice.poly_pressure == 100/127.0f);
    assert(map.pool.slot[4].voice.poly_pressure == 0);
    ma_midi_apply(&map, 0xb0, 64, 0);
    assert(map.pool.group[0].phase == MA_CARD_RELEASED);
    ma_midi_apply(&map, 0xb1, 120, 0);
    assert(map.pool.group[1].phase == MA_CARD_RELEASED);
    assert(!map.pool.sustain[0] && !map.pool.sustain[1]);
    map.pool.next_allocation = 0;
    ma_midi_apply(&map, 0x90, 64, 100);
    assert(map.stats.allocation_failures == 1);
}

static void controls(void) {
    init(8, 1);
    ma_midi_apply(&map, 0xe3, 0, 0); assert(map.pool.bend[3] == -2);
    ma_midi_apply(&map, 0xe3, 0, 64); assert(map.pool.bend[3] == 0);
    ma_midi_apply(&map, 0xe3, 127, 127); assert(map.pool.bend[3] == 2);
    ma_midi_apply(&map, 0xd3, 127, 255); /* one-byte message ignores d2 */
    ma_midi_apply(&map, 0xb3, 1, 64);
    ma_midi_apply(&map, 0xb0, 109, 64);
    ma_midi_apply(&map, 0xb0, 113, 127);
    ma_midi_apply(&map, 0xb0, 74, 0); assert(map.pool.control.filter_cutoff_hz == 20);
    ma_midi_apply(&map, 0xb0, 74, 127); assert(map.pool.control.filter_cutoff_hz == 20000);
    ma_midi_apply(&map, 0xb0, 73, 0); assert(map.pool.control.amp_adsr.attack_ms == 1);
    ma_midi_apply(&map, 0xb0, 72, 127); assert(map.pool.control.amp_adsr.release_ms == 20000);
    ma_midi_apply(&map, 0xb0, 14, 0); assert(map.pool.control.vco2_interval == -24);
    ma_midi_apply(&map, 0xb0, 14, 127); assert(map.pool.control.vco2_interval == 24);
    ma_midi_apply(&map, 0xb0, 87, 0); assert(map.pool.control.vco2_fine_cents == -50);
    ma_midi_apply(&map, 0xb0, 87, 64); assert(map.pool.control.vco2_fine_cents == 0);
    ma_midi_apply(&map, 0xb0, 87, 127); assert(map.pool.control.vco2_fine_cents == 50);
    ma_midi_apply(&map, 0xb0, 103, 127); assert(map.pool.control.mozaik_phason_q32 == 0xfe000000u);
    ma_midi_apply(&map, 0xb0, 7, 100); assert(map.pool.control.master_level == 100/127.0f);
    ma_midi_apply(&map, 0xb0, 78, 127); assert(map.pool.control.lfo.rate_hz == 20);
    ma_midi_apply(&map, 0xb0, 77, 64);
    tick(512); /* controls progress while the pool is silent */
    ma_midi_apply(&map, 0x93, 60, 100);
    const ma_synth *s = &map.pool.slot[0].voice;
    assert(s->pitch_bend_semitones == 2 && s->channel_pressure == 1);
    assert(s->mod_wheel == 64/127.0f && s->lfo.depth == 64/127.0f);
    assert(s->macro[MA_MACRO_GRAVITACIJA] == 64/127.0f);
    assert(s->macro[MA_MACRO_SWARM] == 1);
    uint64_t id = map.pool.slot[0].allocation_id;
    tick(16);
    float level = s->amp_envelope.level;
    ma_midi_apply(&map, 0xb0, 18, 32);
    assert(map.pool.slot[0].allocation_id == id && s->amp_envelope.level == level);
    assert(s->macro[MA_MACRO_SWARM] == 1 && s->channel_pressure == 1);
    ma_midi_apply(&map, 0xb0, 92, 0);
    ma_frame f = ma_voice_pool_tick(&map.pool); assert(f.left == f.right);

    const uint8_t assigned[] = {1,5,7,14,15,17,18,19,20,64,65,71,72,73,74,75,76,
        77,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,102,103,104,
        105,106,107,108,109,110,111,112,113,120,123};
    for (unsigned cc = 0; cc < 128; cc++) {
        bool supported = false;
        for (size_t i = 0; i < sizeof assigned; i++) supported |= cc == assigned[i];
        uint64_t applied = map.stats.ccs, ignored = map.stats.ignored_ccs;
        ma_midi_apply(&map, 0xb0, (uint8_t)cc, 0);
        ma_midi_apply(&map, 0xb0, (uint8_t)cc, 127);
        assert(map.stats.ccs == applied + (supported ? 2 : 0));
        assert(map.stats.ignored_ccs == ignored + (supported ? 0 : 2));
    }
    assert(map.pool.config.unison_count == 1); /* CC114 never changes config */
    map.channel_mask = 1;
    ma_midi_apply(&map, 0x91, 60, 100);
    ma_midi_apply(&map, 0xb1, 7, 0);
    assert(map.stats.filtered == 2 && map.pool.control.master_level == 1);
    ma_midi_apply(&map, 0x90, 128, 100);
    ma_midi_apply(&map, 0xa0, 60, 128);
    ma_midi_apply(&map, 0xb0, 128, 0);
    assert(map.stats.invalid_messages == 3);
    ma_midi_apply(&map, 0xc0, 20, 0);
    ma_midi_apply(&map, 0xf0, 0, 0);
    assert(map.stats.unsupported_messages == 2);
}

static void glide(void) {
    init(1, 1);
    ma_midi_apply(&map, 0xb0, 65, 127);
    ma_midi_apply(&map, 0xb0, 5, 1);
    ma_midi_apply(&map, 0x90, 48, 100);
    assert(map.pool.slot[0].voice.glide.current_note == 48);
    ma_midi_apply(&map, 0x90, 60, 100);
    assert(map.pool.slot[0].voice.glide.current_note == 48);
    tick(20);
    float pitch = map.pool.slot[0].voice.glide.current_note;
    assert(pitch > 48 && pitch < 60);
    ma_midi_apply(&map, 0x90, 72, 100);
    assert(map.pool.slot[0].voice.glide.current_note == pitch);
    ma_midi_apply(&map, 0xb0, 65, 0);
    assert(map.pool.slot[0].voice.glide.current_note == 72);
    ma_midi_apply(&map, 0xb0, 72, 0);
    ma_midi_apply(&map, 0xb0, 123, 0);
    tick(1000);
    assert(!map.pool.slot[0].awake);
    ma_midi_apply(&map, 0xb0, 65, 127);
    ma_midi_apply(&map, 0x90, 60, 100);
    assert(map.pool.slot[0].voice.glide.current_note == 72);
    ma_voice_pool_reset(&map.pool);
    ma_midi_apply(&map, 0x90, 64, 100);
    assert(map.pool.slot[0].voice.glide.current_note == 64);
}

/* Tiny format-1 file with a tempo track and running-status music track. */
static const uint8_t fixture[] = {
    'M','T','h','d',0,0,0,6,0,1,0,2,3,0xe8,
    'M','T','r','k',0,0,0,18,
    0,0xff,0x51,3,7,0xa1,0x20, 4,0xff,0x51,3,3,0xd0,0x90, 0,0xff,0x2f,0,
    'M','T','r','k',0,0,0,30,
    0,0x90,60,100, 0,60,110, 2,0x91,60,90,
    2,0x80,60,0, 0,60,0, 2,0x81,60,0, 1,0xb0,123,0, 0,0xff,0x2f,0,
};
static void fixture_direct(unsigned frame) {
    if (frame == 0) {
        assert(ma_voice_pool_note_on(&direct, 0, 60, 100));
        assert(ma_voice_pool_note_on(&direct, 0, 60, 110));
    }
    if (frame == 48) assert(ma_voice_pool_note_on(&direct, 1, 60, 90));
    if (frame == 96) {
        assert(ma_voice_pool_note_off(&direct, 0, 60, 0));
        assert(ma_voice_pool_note_off(&direct, 0, 60, 0));
    }
    if (frame == 120) assert(ma_voice_pool_note_off(&direct, 1, 60, 0));
    if (frame == 132) ma_voice_pool_panic(&direct);
}
static void smf_twin(const char *prefix) {
    smf_file smf = {0}; smf_error error = {0};
    assert(smf_parse(fixture, sizeof fixture, UINT16_MAX, &smf, &error));
    size_t *at = NULL, total = 0;
    assert(smf_make_event_frames(&smf, 48000, .02, &at, &total));
    const size_t expected[] = {0,0,48,96,96,120,132};
    assert(smf.event_count == 7 && total == 1092);
    assert(memcmp(at, expected, sizeof expected) == 0);
    ma_voice_pool_config config = ma_voice_pool_default_config;
    config.limit = 8; config.unison_count = 2;
    assert(ma_voice_pool_init(&direct, 48000, &config, NULL));
    assert(ma_midi_renderer_init(&renderer, 48000, &config, NULL, UINT16_MAX,
        smf.events, at, smf.event_count));
    float pcm[2184], reference[2184];
    for (unsigned i = 0; i < total; i++) {
        fixture_direct(i);
        ma_frame f = ma_voice_pool_tick(&direct);
        reference[2*i] = f.left; reference[2*i+1] = f.right;
    }
    assert(ma_midi_render(&renderer, NULL, 0));
    assert(ma_midi_render(&renderer, pcm, 97));
    assert(ma_midi_render(&renderer, pcm+194, total-97));
    assert(memcmp(pcm, reference, sizeof pcm) == 0);
    assert(renderer.next_event == smf.event_count);
    assert(renderer.midi.stats.note_ons == 3 && renderer.midi.stats.note_offs == 3);
    uint64_t hash = tw_fnv1a64(pcm, sizeof pcm, 0);
    assert(hash == UINT64_C(0x7af6ce1d2a2c3bdb));
    printf("ma3-1-smf-twin hash=%016" PRIx64 "\n", hash);
    if (prefix) {
        char path[1024];
        assert(snprintf(path, sizeof path, "%s.mid", prefix) < (int)sizeof path);
        FILE *f = fopen(path, "wb"); assert(f);
        assert(fwrite(fixture, 1, sizeof fixture, f) == sizeof fixture);
        assert(fclose(f) == 0);
        assert(snprintf(path, sizeof path, "%s.f32", prefix) < (int)sizeof path);
        f = fopen(path, "wb"); assert(f);
        assert(fwrite(reference, 1, sizeof reference, f) == sizeof reference);
        assert(fclose(f) == 0);
    }
    free(at); smf_dispose(&smf);
    assert(smf_parse(fixture, sizeof fixture, 2, &smf, &error));
    assert(smf_make_event_frames(&smf, 48000, .02, &at, &total));
    assert(smf.event_count == 2 && at[0] == 48 && at[1] == 120);
    free(at); smf_dispose(&smf);
}

static void expression_twin(void) {
    const smf_event events[] = {
        {.status=0xb2,.d1=1,.d2=64}, {.status=0xe2,.d1=127,.d2=127},
        {.status=0xd2,.d1=100}, {.status=0xb0,.d1=109,.d2=64},
        {.status=0x92,.d1=60,.d2=100}, {.status=0x92,.d1=60,.d2=110},
        {.status=0xa2,.d1=60,.d2=90}, {.status=0xb2,.d1=64,.d2=127},
        {.status=0x82,.d1=60}, {.status=0x92,.d1=60},
        {.status=0xb2,.d1=64}, {.status=0xb0,.d1=123},
    };
    const size_t at[] = {0,0,1,31,64,64,127,128,129,255,256,511};
    const unsigned rates[] = {44100,48000,96000,192000};
    for (unsigned u = 1; u <= 8; u *= 2) for (unsigned rate = 0; rate < 4; rate++) {
        ma_voice_pool_config config = ma_voice_pool_default_config;
        config.limit = 16; config.unison_count = (uint8_t)u;
        assert(ma_voice_pool_init(&direct, rates[rate], &config, &ma_patch_granica));
        assert(ma_midi_map_init(&bytes_map, rates[rate], &config, &ma_patch_granica, UINT16_MAX));
        assert(ma_midi_renderer_init(&renderer, rates[rate], &config, &ma_patch_granica,
            UINT16_MAX, events, at, sizeof at/sizeof *at));
        float pcm[2048];
        assert(ma_midi_render(&renderer, pcm, 129));
        assert(ma_midi_render(&renderer, pcm+258, 895));
        tw_midi_parser parser = {0}; tw_midi_msg message = {0};
        uint8_t last_status = 0;
        size_t next = 0;
        for (unsigned frame = 0; frame < 1024; frame++) {
            while (next < sizeof at/sizeof *at && at[next] == frame) {
                smf_event e = events[next];
                if (e.status != last_status) assert(!tw_midi_parse(&parser, e.status, &message));
                last_status = e.status;
                int ready = tw_midi_parse(&parser, e.d1, &message);
                assert(!tw_midi_parse(&parser, 0xf8, &message));
                if ((e.status & 0xf0) != 0xd0) ready = tw_midi_parse(&parser, e.d2, &message);
                assert(ready);
                ma_midi_apply(&bytes_map, message.status, message.d1, message.d2);
                switch (next++) {
                case 0: ma_voice_pool_set_mod_wheel(&direct, 2, 64/127.0f); break;
                case 1: ma_voice_pool_set_pitch_bend(&direct, 2, 2); break;
                case 2: ma_voice_pool_set_channel_pressure(&direct, 2, 100/127.0f); break;
                case 3: ma_voice_pool_set_macro(&direct, MA_MACRO_GRAVITACIJA, 64/127.0f); break;
                case 4: assert(ma_voice_pool_note_on(&direct, 2, 60, 100)); break;
                case 5: assert(ma_voice_pool_note_on(&direct, 2, 60, 110)); break;
                case 6: ma_voice_pool_set_poly_pressure(&direct, 2, 60, 90/127.0f); break;
                case 7: ma_voice_pool_set_sustain(&direct, 2, true); break;
                case 8: case 9: assert(ma_voice_pool_note_off(&direct, 2, 60, 0)); break;
                case 10: ma_voice_pool_set_sustain(&direct, 2, false); break;
                case 11: ma_voice_pool_panic(&direct); break;
                }
            }
            ma_frame expected = ma_voice_pool_tick(&direct);
            ma_frame raw = ma_voice_pool_tick(&bytes_map.pool);
            assert(expected.left == raw.left && expected.right == raw.right);
            assert(pcm[2*frame] == raw.left && pcm[2*frame+1] == raw.right);
        }
        assert(bytes_map.stats.note_ons == 2 && bytes_map.stats.note_offs == 2);
    }
}

static void boundaries(void) {
    const unsigned rates[] = {44100,48000,96000,192000};
    for (unsigned r = 0; r < 4; r++) {
        assert(ma_midi_map_init(&map, rates[r], NULL, &ma_patch_granica, UINT16_MAX));
        ma_midi_apply(&map, 0xb0, 107, 127);
        ma_midi_apply(&map, 0xb0, 108, 127);
        ma_midi_apply(&map, 0xb0, 72, 0);
        for (unsigned n = 0; n < 128; n++) {
            ma_midi_apply(&map, 0x90 | (n % 16), (uint8_t)n, 127);
            tick(8);
        }
        assert(map.stats.note_ons == 128 && !map.stats.allocation_failures);
        ma_midi_apply(&map, 0xb0, 123, 0);
        tick(1000);
        assert(map.pool.diagnostics.active_slots == 0);
    }
    smf_event events[2] = {{0}}; size_t at[] = {100,99};
    assert(!ma_midi_renderer_init(&renderer, 48000, NULL, NULL, UINT16_MAX, events, at, 2));
    assert(ma_midi_renderer_init(&renderer, 48000, NULL, NULL, UINT16_MAX, NULL, NULL, 0));
    renderer.frame = SIZE_MAX;
    float out[2] = {1,2};
    assert(!ma_midi_render(&renderer, out, 1));
    assert(renderer.frame == SIZE_MAX && out[0] == 1 && out[1] == 2);
    size_t *schedule = NULL, total = 0;
    smf_file empty = {.division = 1000};
    assert(!smf_make_event_frames(&empty, 48000, NAN, &schedule, &total));
    assert(!smf_make_event_frames(&empty, 48000, -1, &schedule, &total));
    assert(!smf_make_event_frames(&empty, 48000, 7201, &schedule, &total));
    assert(!smf_make_event_frames(&empty, 0, 0, &schedule, &total));
    assert(smf_make_event_frames(&empty, 48000, 0, &schedule, &total));
    assert(!schedule && total == 0);
    smf_event far = {.tick = UINT64_MAX, .status = 0x90, .d1 = 60, .d2 = 100};
    empty.events = &far; empty.event_count = 1;
    assert(!smf_make_event_frames(&empty, 48000, 0, &schedule, &total));
    assert(!schedule && total == 0);
}

int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "--fixture")) { smf_twin(argv[2]); return 0; }
    assert(argc == 1);
    ownership(); controls(); glide(); smf_twin(NULL); expression_twin(); boundaries();
    puts("ma3-1: MIDI ownership, controls, parser/SMF/direct twins and boundaries pass");
}
