#ifndef MA_MIDI_RENDER_H
#define MA_MIDI_RENDER_H
#include "ma_midi_map.h"
#include "smf.h"

typedef struct {
    ma_midi_map midi;
    const smf_event *events;
    const size_t *event_frames;
    size_t event_count, next_event, frame;
} ma_midi_renderer;

/* Caller retains event storage, ordered by nondecreasing frame. No allocation.
 * Pass a fresh/stopped renderer; false rejects an invalid schedule/config. */
bool ma_midi_renderer_init(ma_midi_renderer *renderer, unsigned rate,
    const ma_voice_pool_config *config, const ma_patch *patch, uint16_t channels,
    const smf_event *events, const size_t *event_frames, size_t event_count);
/* Interleaved dry stereo; arbitrary block sizes split exactly at MIDI events.
 * Returns false on position overflow, before mutating the renderer or PCM. */
bool ma_midi_render(ma_midi_renderer *renderer, float *pcm, size_t frames);
#endif
