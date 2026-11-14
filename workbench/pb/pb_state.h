/* The bench file, .pbench (docs/patchbench-plan.md §3.2): session state of
 * the desk that is not part of the patch.  Strict key=value text in
 * sections, the .mapatch and .mws rules: version line first, every key of a
 * section required once, unknown or duplicate keys are errors with line
 * numbers, f32 values round-trip, atomic save.
 *
 *   patchbench=2
 *   selected=1                          the channel on the desk
 *   [probe]    note velocity pinned
 *   [sequence] bpm (centi-BPM) swing (percent)
 *   [channel]  index (1..4) patch (may be empty) mute steps root
 *   [voice]    of the channel above: limit unison detune_cents seed character
 *              glide_s lfo_depth lfo_rate_hz chorus_mix chorus_depth
 *              chorus_rate_hz reverb_mix reverb_size reverb_damping gain
 *   [step]     of the channel above: index (1-based) notes (absolute, comma
 *              separated) velocity gate (percent) tie octave; only steps
 *              with notes
 *   [snapshot] slot (1..8) and every .mapatch field
 *
 * A channel the file leaves out keeps its defaults.  Version 1 files (one
 * patch, one [voice], [sequence] with steps and root) load into channel 1. */
#ifndef PB_STATE_H
#define PB_STATE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "../../src/mamutanalog.h"
#include "pb_seq.h"

enum { PB_PATH_MAX = 1024, PB_SNAPSHOTS = 8 };

typedef struct {
    uint8_t limit;            /* 1..64, divisible by unison */
    uint8_t unison;           /* 1, 2, 4, 8 */
    float detune_cents;       /* 0..100 */
    uint64_t seed;
    float character;          /* 0..1 */
    float glide_s;            /* 0..10; 0 = off */
    float lfo_depth, lfo_rate_hz;
    float chorus_mix, chorus_depth, chorus_rate_hz;
    float reverb_mix, reverb_size, reverb_damping;
    float gain;               /* 0..64 linear; the pool runs near -40 dBFS */
} pb_voice;

typedef struct {
    uint8_t note, velocity;
    bool pinned;
} pb_probe_point;

/* A sequencer channel: the patch file on it, its voice setup and pattern.
 * Tempo and swing are one for the bench: every pattern carries the same. */
typedef struct {
    char patch[PB_PATH_MAX];
    pb_voice voice;
    pb_pattern pattern;
    bool mute;
} pb_channel;

typedef struct {
    pb_channel channel[PB_CHANNELS];
    uint8_t selected;         /* the channel on the desk */
    pb_probe_point probe;
    bool snapshot_used[PB_SNAPSHOTS];
    ma_patch snapshot[PB_SNAPSHOTS];
} pb_bench;

typedef struct {
    unsigned line;
    char message[160];
} pb_error;

pb_voice pb_voice_default(void);
bool pb_voice_valid(const pb_voice *v);
/* Fields that need a new pool (§5.3) differ. */
bool pb_voice_needs_pool(const pb_voice *a, const pb_voice *b);
void pb_bench_default(pb_bench *b);
/* The channels' patterns and mutes as the engine takes them. */
pb_sequence pb_bench_sequence(const pb_bench *b);
void pb_bench_set_tempo(pb_bench *b, uint32_t bpm, uint8_t swing);

bool pb_bench_read(FILE *file, pb_bench *bench, pb_error *error);
bool pb_bench_write(FILE *file, const pb_bench *bench);
bool pb_bench_load(const char *path, pb_bench *bench, pb_error *error);
bool pb_bench_save(const char *path, const pb_bench *bench, pb_error *error);

/* Atomic write of any text through a callback: temp file, fsync, rename. */
bool pb_atomic_write(const char *path, bool (*write)(FILE *, const void *), const void *data);

#endif
