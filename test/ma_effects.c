#include "../src/mamutanalog.h"
#include "../driver/ma_midi_render.h"
#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static ma_effects fx, saved;
static ma_voice_pool pool, dry_pool;
static ma_midi_renderer renderer;
static ma_chorus_controls chorus = {true, .3f, .35f, .25f};
static ma_reverb_controls reverb = {true, .25f, .65f, .55f};

static void safe(ma_frame f) {
    assert(isfinite(f.left) && isfinite(f.right));
    assert(fabsf(f.left) <= 1 && fabsf(f.right) <= 1);
}
static void bypass(void) {
    ma_effects_init(&fx, 48000);
    saved = fx;
    const ma_frame inputs[] = {{-0.0f, 0}, {.2f, -.3f}, {NAN, INFINITY}};
    for (unsigned i = 0; i < 3; i++) {
        ma_frame f = ma_effects_tick(&fx, inputs[i]);
        assert(memcmp(&f, &inputs[i], sizeof f) == 0);
    }
    assert(memcmp(&fx, &saved, sizeof fx) == 0);
    ma_chorus_controls c = chorus; c.mix = 0;
    ma_reverb_controls r = reverb; r.mix = 0;
    ma_effects_set_chorus(&fx, c); ma_effects_set_reverb(&fx, r);
    for (unsigned i = 0; i < 3; i++) {
        ma_frame f = ma_effects_tick(&fx, inputs[i]);
        assert(memcmp(&f, &inputs[i], sizeof f) == 0);
    }
    assert(fx.chorus.write_index == 3 && fx.reverb.write_index == 3);
    assert(fx.sanitizations > 0);
    ma_effects_set_chorus(&fx, chorus); ma_effects_set_reverb(&fx, reverb);
    assert(fx.chorus.write_index == 3 && fx.reverb.write_index == 3);
    for (unsigned i = 0; i < 24000; i++) {
        ma_frame f = ma_effects_tick(&fx, (ma_frame){.1f, -.07f}); safe(f);
    }
    c.enabled = false; r.enabled = false;
    ma_effects_set_chorus(&fx, c); ma_effects_set_reverb(&fx, r);
    saved = fx;
    for (unsigned i = 0; i < 100; i++) {
        ma_frame f = ma_effects_tick(&fx, (ma_frame){0}); assert(f.left == 0 && f.right == 0);
    }
    assert(memcmp(&fx, &saved, sizeof fx) == 0);
    /* Re-enabling one stage does not touch the other stage's tail. */
    ma_effects_set_chorus(&fx, chorus);
    assert(memcmp(&fx.reverb, &saved.reverb, sizeof fx.reverb) == 0);
    ma_effects_set_reverb(&fx, reverb);
    for (unsigned i = 0; i < 48000; i++) {
        ma_frame f = ma_effects_tick(&fx, (ma_frame){0}); assert(f.left == 0 && f.right == 0);
    }
    ma_effects_reset(&fx);
    assert(fx.chorus.controls.enabled && fx.reverb.controls.enabled);
    assert(fx.chorus.write_index == 0 && fx.reverb.write_index == 0);
    assert(fx.chorus.phase == 0 && fx.clips == 0 && fx.pre_peak == 0);
}

static void impulse(void) {
    /* Independent analytic anchors for donor tap ordering and gains. */
    ma_effects_init(&fx, 48000);
    ma_effects_set_chorus(&fx, (ma_chorus_controls){true, 1, 0, 1});
    ma_frame f = ma_effects_tick(&fx, (ma_frame){1, 0});
    assert(fabsf(f.left - (.28f + .06f * .78f)) < 1e-6f);
    assert(fabsf(f.right - .06f * .78f) < 1e-6f);
    for (unsigned i = 1; i <= 672; i++) f = ma_effects_tick(&fx, (ma_frame){0});
    assert(fabsf(f.left - .66f * .78f) < 1e-6f);
    ma_effects_init(&fx, 48000);
    ma_effects_set_reverb(&fx, (ma_reverb_controls){true, 1, 0, 0});
    f = ma_effects_tick(&fx, (ma_frame){1, 0});
    assert(fabsf(f.left - .42f) < 1e-6f && f.right == 0);
    unsigned first = (unsigned)((float)fx.reverb.length * .16f + .5f);
    for (unsigned i = 1; i <= first; i++) {
        f = ma_effects_tick(&fx, (ma_frame){0});
        if (i < first) assert(f.left == 0 && f.right == 0);
    }
    assert(fabsf(f.left - .42f * .18f * .62f) < 1e-6f && f.right == 0);
    unsigned cross = (unsigned)((float)fx.reverb.length * .31f + .5f);
    for (unsigned i = first + 1; i <= cross; i++) f = ma_effects_tick(&fx, (ma_frame){0});
    assert(fabsf(f.right - .42f * .26f * .62f) < 1e-6f);
}

static uint64_t stress(unsigned rate, bool extreme) {
    ma_effects_init(&fx, (float)rate);
    ma_effects_set_chorus(&fx, (ma_chorus_controls){true, 1, 1, 20});
    ma_effects_set_reverb(&fx, (ma_reverb_controls){true, 1, 1, extreme ? 0 : .99f});
    uint64_t hash = 0;
    for (unsigned i = 0; i < rate * 2; i++) {
        ma_frame in = i < rate / 4 ? (ma_frame){.8f, -.6f} : (ma_frame){0};
        if (extreme && i % 97 == 0) in = (ma_frame){FLT_MAX, -FLT_MAX};
        if (extreme && i % 101 == 0) in = (ma_frame){NAN, INFINITY};
        ma_frame f = ma_effects_tick(&fx, in); safe(f);
        assert(fx.chorus.phase >= 0 && fx.chorus.phase < 1);
        assert(fx.chorus.write_index < fx.chorus.length);
        assert(fx.reverb.write_index < fx.reverb.length);
        hash = tw_fnv1a64(&f, sizeof f, hash);
    }
    return hash;
}
static void controls(void) {
    const float values[] = {NAN, INFINITY, -INFINITY, -FLT_MAX, FLT_MAX, -1, 0, 1};
    for (unsigned i = 0; i < sizeof values / sizeof *values; i++) {
        float v = values[i];
        ma_effects_init(&fx, v);
        assert(fx.sample_rate_hz == 48000);
        ma_effects_set_chorus(&fx, (ma_chorus_controls){true, v, v, v});
        ma_effects_set_reverb(&fx, (ma_reverb_controls){true, v, v, v});
        assert(fx.chorus.controls.rate_hz >= .03f && fx.chorus.controls.rate_hz <= 20);
        assert(fx.reverb.controls.damping >= 0 && fx.reverb.controls.damping <= .99f);
        for (unsigned j = 0; j < 128; j++) safe(ma_effects_tick(&fx, (ma_frame){.5f, -.5f}));
    }
}

static void pool_tail(void) {
    ma_patch patch = ma_patch_lead;
    patch.amp_adsr.release_ms = 5;
    ma_voice_pool_config config = ma_voice_pool_default_config;
    config.limit = 1;
    assert(ma_voice_pool_init(&pool, 48000, &config, &patch));
    dry_pool = pool;
    ma_effects_set_chorus(&pool.effects, (ma_chorus_controls){true, 0, .35f, .25f});
    ma_effects_set_reverb(&pool.effects, (ma_reverb_controls){true, 0, .65f, .55f});
    assert(ma_voice_pool_note_on(&pool, 0, 60, 100));
    assert(ma_voice_pool_note_on(&dry_pool, 0, 60, 100));
    for (unsigned i = 0; i < 12000; i++) {
        ma_frame wet = ma_voice_pool_tick(&pool), dry = ma_voice_pool_tick(&dry_pool);
        assert(memcmp(&wet, &dry, sizeof wet) == 0);
    }
    /* Mix changes rejoin running history; panic must not reset that history. */
    ma_effects_set_chorus(&pool.effects, chorus);
    ma_effects_set_reverb(&pool.effects, reverb);
    saved = pool.effects;
    ma_voice_pool_panic(&pool);
    assert(memcmp(&pool.effects, &saved, sizeof saved) == 0);
    bool tail = false;
    for (unsigned i = 0; i < 48000; i++) {
        ma_frame f = ma_voice_pool_tick(&pool); safe(f);
        if (i > 24000 && !pool.diagnostics.active_slots && fabsf(f.left) > 1e-6f) tail = true;
    }
    assert(tail && pool.diagnostics.active_slots == 0);
    ma_voice_pool_set_patch(&pool, &ma_patch_granica);
    assert(pool.effects.reverb.controls.mix == reverb.mix);
    ma_voice_pool_reset(&pool);
    assert(pool.effects.chorus.controls.mix == chorus.mix);
    assert(pool.effects.reverb.controls.enabled && pool.effects.reverb.write_index == 0);
    for (unsigned i = 0; i < 24000; i++) {
        ma_frame f = ma_voice_pool_tick(&pool); assert(f.left == 0 && f.right == 0);
    }
}

static void midi_twin(void) {
    const smf_event events[] = {
        {.status=0xb0,.d1=107,.d2=50}, {.status=0xb0,.d1=108,.d2=60},
        {.status=0x90,.d1=48,.d2=100}, {.status=0xb0,.d1=107,.d2=0},
        {.status=0xb0,.d1=108,.d2=0}, {.status=0xb0,.d1=107,.d2=50},
        {.status=0xb0,.d1=108,.d2=60}, {.status=0xb0,.d1=123},
    };
    const size_t at[] = {0,0,0,3000,3000,8191,8192,12000};
    ma_voice_pool_config config = ma_voice_pool_default_config; config.limit = 8;
    static float expected[48000 * 2], pcm[48000 * 2];
    assert(ma_voice_pool_init(&pool, 48000, &config, &ma_patch_tepih));
    size_t next = 0;
    for (unsigned i = 0; i < 48000; i++) {
        while (next < 8 && at[next] == i) {
            smf_event e = events[next++];
            if (e.d1 == 107) {
                ma_chorus_controls c = chorus; c.mix = (float)e.d2 / 127;
                ma_effects_set_chorus(&pool.effects, c);
            } else if (e.d1 == 108) {
                ma_reverb_controls r = reverb; r.mix = (float)e.d2 / 127;
                ma_effects_set_reverb(&pool.effects, r);
            } else if (e.status == 0x90) assert(ma_voice_pool_note_on(&pool, 0, e.d1, e.d2));
            else ma_voice_pool_panic(&pool);
        }
        ma_frame f = ma_voice_pool_tick(&pool);
        expected[2*i] = f.left; expected[2*i+1] = f.right;
    }
    const unsigned blocks[] = {1, 17, 128, 512, 4096};
    for (unsigned b = 0; b < 5; b++) {
        assert(ma_midi_renderer_init(&renderer, 48000, &config, &ma_patch_tepih,
            UINT16_MAX, events, at, 8));
        for (unsigned i = 0; i < 48000;) {
            unsigned n = blocks[b]; if (n > 48000-i) n = 48000-i;
            assert(ma_midi_render(&renderer, pcm+2*i, n)); i += n;
        }
        assert(memcmp(expected, pcm, sizeof pcm) == 0);
    }
    uint64_t hash = tw_fnv1a64(pcm, sizeof pcm, 0);
    /* Re-pinned 2026-11-10 for the triangle integrator gain (was
     * e95149977684d494; docs/ma-source-fix-20261110.md). */
    assert(hash == UINT64_C(0xeca2f01ea356eda1));
    printf("ma3-2 MIDI/direct wet twin=%016" PRIx64 "\n", hash);
}

int main(void) {
    bypass(); impulse(); controls(); pool_tail(); midi_twin();
    const unsigned rates[] = {44100,48000,96000,192000};
    const uint64_t expected[4][2] = {
        {UINT64_C(0x98cb43b5bb4f5e7f), UINT64_C(0x90c2304e52eeb393)},
        {UINT64_C(0xbe5928f6853f293d), UINT64_C(0x174e5e5455320b7a)},
        {UINT64_C(0xefbaea3c2910b74f), UINT64_C(0x21cfc3635b23b12a)},
        {UINT64_C(0xa92585678511bf0a), UINT64_C(0x330a68835dcf23fe)},
    };
    for (unsigned r = 0; r < 4; r++) for (unsigned extreme = 0; extreme < 2; extreme++) {
        uint64_t hash = stress(rates[r], extreme);
        assert(hash == stress(rates[r], extreme));
        assert(hash == expected[r][extreme]);
        printf("ma3-2 effects rate=%u hostile=%u hash=%016" PRIx64 "\n", rates[r], extreme, hash);
    }
    printf("ma3-2: bypass, rejoin, taps, tails, hostile controls, block twins pass; "
        "effects=%zu pool=%zu voice=%zu bytes\n", sizeof(ma_effects), sizeof(ma_voice_pool), sizeof(ma_synth));
}
