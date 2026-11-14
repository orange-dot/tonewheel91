/* Bounce (docs/patchbench-plan.md §2.8, PB9): the sequence rendered offline
 * through the live engine's own code in non-real time, so a bounce is what
 * the desk played.  A manifest records what made it. */
#ifndef PB_BOUNCE_H
#define PB_BOUNCE_H

#include <stdbool.h>
#include <stddef.h>

#include "pb_engine.h"

/* Frames of `loops` passes of the sequence from a start; a pass is
 * pb_sequence_steps long. */
uint64_t pb_sequence_frames(const pb_sequence *sequence, unsigned rate, unsigned loops);

/* `patch` and `voice` hold one entry per channel, and so does `rack` unless
 * it is null (no racks); a rack starts in place, without its fades.
 * Interleaved stereo f32 of `loops` passes and `tail_s` seconds after the
 * stop; *pcm is malloc'ed. */
bool pb_bounce_render(unsigned rate, const ma_patch patch[PB_CHANNELS], const ma_rack_spec *rack,
                      const pb_voice voice[PB_CHANNELS], const pb_sequence *sequence, unsigned loops, float tail_s,
                      float **pcm, size_t *frames);

/* WAV plus `<wav>.manifest`: per channel the patch SHA-256 (of its .mapatch
 * text, rack included), bench SHA-256 (of its .pbench text), FNV-64 of the
 * PCM bytes. */
bool pb_bounce_write(const char *wav_path, unsigned rate, const ma_patch patch[PB_CHANNELS],
                     const ma_rack_spec *rack, const pb_bench *bench, const float *pcm, size_t frames,
                     char *message, size_t message_size);

#endif
