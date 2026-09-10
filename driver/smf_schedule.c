#include "smf_schedule.h"
#include "host_parse.h"
#include <math.h>
#include <stdlib.h>

static constexpr double RENDER_MAX_SECONDS = 7200.0;

bool smf_make_event_frames(const smf_file *midi, unsigned rate,
                              double tail_seconds, size_t **out,
                              size_t *out_frames) {
    *out = 0;
    *out_frames = 0;
    if (!midi->division || !rate || !isfinite(tail_seconds)
        || tail_seconds < 0 || tail_seconds > RENDER_MAX_SECONDS) return false;
    size_t *frames = 0;
    if (midi->event_count) {
        size_t bytes = 0;
        if (!host_size_mul(midi->event_count, sizeof *frames, &bytes))
            return false;
        frames = malloc(bytes);
        if (!frames) return false;
    }

    double seconds = 0.0;
    uint64_t segment_tick = 0;
    uint32_t us_per_quarter = 500000;
    size_t tempo = 0;
    size_t last = 0;
    double max_frame = RENDER_MAX_SECONDS * rate;
    for (size_t event = 0; event < midi->event_count; event++) {
        while (tempo < midi->tempo_count
               && midi->tempos[tempo].tick <= midi->events[event].tick) {
            seconds += (double)(midi->tempos[tempo].tick - segment_tick)
                     * us_per_quarter * 1e-6 / midi->division;
            segment_tick = midi->tempos[tempo].tick;
            us_per_quarter = midi->tempos[tempo].us_per_quarter;
            tempo++;
        }
        double at = seconds
                  + (double)(midi->events[event].tick - segment_tick)
                  * us_per_quarter * 1e-6 / midi->division;
        double frame = at * rate + 0.5;
        if (!isfinite(frame) || frame < 0.0 || frame > max_frame
            || frame >= (double)SIZE_MAX) {
            free(frames);
            return false;
        }
        frames[event] = (size_t)frame;
        last = frames[event];
    }

    double tail_frame_value = tail_seconds * rate;
    if (!isfinite(tail_frame_value) || tail_frame_value >= (double)SIZE_MAX) {
        free(frames);
        return false;
    }
    size_t total = 0;
    if (!host_size_add(last, (size_t)tail_frame_value, &total)
        || (double)total > max_frame) {
        free(frames);
        return false;
    }
    *out = frames;
    *out_frames = total;
    return true;
}
