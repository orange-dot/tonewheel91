#ifndef MA_MIDI_RENDER_H
#define MA_MIDI_RENDER_H
#include "ma_midi_map.h"
#include "smf.h"

typedef struct {
    ma_midi_map midi;
    const smf_event *events;
    const size_t *event_frames;
    size_t event_count, next_event, frame;
    /* Optional tempo track for the rack's synced delays: quarter notes per
     * minute from each frame on. None leaves the rack at its 120. */
    const size_t *tempo_frames;
    const float *tempo_bpm;
    size_t tempo_count, next_tempo;
} ma_midi_renderer;

/* Caller retains event storage, ordered by nondecreasing frame. No allocation.
 * Pass a fresh/stopped renderer; false rejects an invalid schedule/config. */
bool ma_midi_renderer_init(ma_midi_renderer *renderer, unsigned rate,
    const ma_voice_pool_config *config, const ma_patch *patch, uint16_t channels,
    const smf_event *events, const size_t *event_frames, size_t event_count);
/* After init. Caller retains both arrays, ordered by nondecreasing frame;
 * false rejects a disordered track. */
bool ma_midi_renderer_set_tempo(ma_midi_renderer *renderer, const size_t *frames,
    const float *bpm, size_t count);
/* Events at frame zero find the rack in place (settled) when the first frame
 * renders; later chorus and reverb CCs come in through its ramps and fades.
 * Interleaved stereo; arbitrary block sizes split exactly at MIDI events.
 * Returns false on position overflow, before mutating the renderer or PCM. */
bool ma_midi_render(ma_midi_renderer *renderer, float *pcm, size_t frames);
#endif
