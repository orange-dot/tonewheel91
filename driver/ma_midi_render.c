#include "ma_midi_render.h"

bool ma_midi_renderer_init(ma_midi_renderer *r, unsigned rate,
    const ma_voice_pool_config *config, const ma_patch *patch, uint16_t channels,
    const smf_event *events, const size_t *event_frames, size_t count) {
    if (count && (!events || !event_frames)) return false;
    for (size_t i = 1; i < count; i++)
        if (event_frames[i] < event_frames[i-1]) return false;
    if (!ma_midi_map_init(&r->midi, rate, config, patch, channels)) return false;
    r->events = events;
    r->event_frames = event_frames;
    r->event_count = count;
    r->next_event = 0;
    r->frame = 0;
    return true;
}

bool ma_midi_render(ma_midi_renderer *r, float *pcm, size_t frames) {
    if ((frames && !pcm) || frames > SIZE_MAX - r->frame
        || frames > SIZE_MAX / (2 * sizeof *pcm)) return false;
    size_t written = 0;
    while (written < frames) {
        while (r->next_event < r->event_count
            && r->event_frames[r->next_event] <= r->frame) {
            const smf_event *e = &r->events[r->next_event++];
            ma_midi_apply(&r->midi, e->status, e->d1, e->d2);
        }
        size_t count = frames - written;
        if (count > 128) count = 128;
        if (r->next_event < r->event_count
            && r->event_frames[r->next_event] - r->frame < count)
            count = r->event_frames[r->next_event] - r->frame;
        ma_frame block[128];
        ma_voice_pool_render(&r->midi.pool, block, count);
        for (size_t i = 0; i < count; i++) {
            pcm[2 * (written + i)] = block[i].left;
            pcm[2 * (written + i) + 1] = block[i].right;
        }
        written += count;
        r->frame += count;
    }
    return true;
}
