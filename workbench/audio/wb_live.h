/* Live audio (docs/workbench-plan.md §6, W7): one ALSA thread that plays a
 * rendered take and up to four live instruments at once.
 *
 * The UI talks to the audio thread only through a lock-free single-producer
 * single-consumer command ring; meters come back through a seqlock'ed
 * snapshot.  The real-time part (wb_live_rt.c) allocates nothing, takes no
 * locks and does no I/O; `make check-workbench-symbols` audits that.
 *
 * Instruments and the playback buffer are installed while the audio thread
 * is stopped (wb_live_stop / wb_live_start), which the UI does around a part
 * change; commands then drive them in real time. */
#ifndef WB_LIVE_H
#define WB_LIVE_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../driver/midi_map.h"
#include "../../src/mamutanalog.h"
#include "../desk/wb_engine.h"
#include "../desk/wb_render.h"
#include "../desk/wb_session.h"

enum { WB_LIVE_SLOTS = 4, WB_LIVE_RING = 1024 };

typedef enum : uint8_t {
    WB_LIVE_NOTE_ON, WB_LIVE_NOTE_OFF, WB_LIVE_MIDI, WB_LIVE_PANIC, WB_LIVE_PLAY, WB_LIVE_STOP,
    WB_LIVE_LOCATE, WB_LIVE_GAIN, WB_LIVE_PLAYBACK_GAIN, WB_LIVE_SELECT_TAKE, WB_LIVE_RESET_SAFETY,
} wb_live_kind;

typedef struct {
    wb_live_kind kind;
    uint8_t slot, d0, d1, d2;
    float value;
    uint64_t frame;
} wb_live_cmd;

typedef struct {
    _Atomic uint32_t head, tail;
    wb_live_cmd cmd[WB_LIVE_RING];
} wb_live_ring;

typedef struct {
    float peak_left, peak_right, rms;
    float budget_ms;               /* DSP time of the last period */
    float budget_peak_ms;          /* worst period since start */
    float period_ms;               /* the period's real-time length */
    uint64_t position;             /* playback frame */
    uint64_t xruns, periods;
    unsigned active_voices;
    bool muted;                    /* safety tripped: non-finite or > 0 dBFS for 10 ms */
    bool playing;
} wb_live_meters;

typedef struct {
    wb_part_engine kind;
    bool active;
    ma_voice_pool *pool;           /* Mamut */
    wb_engine_instance engine;     /* organ / ep73 */
    float gain;
    float pressure;
} wb_live_slot;

typedef struct wb_live {
    unsigned rate;
    wb_live_ring ring;
    wb_live_slot slot[WB_LIVE_SLOTS];
    /* Two playback takes for A/B; constant gains, switched at the same
     * position.  Interleaved stereo f32. */
    const float *take[2];
    uint64_t take_frames[2];
    float take_gain[2];
    unsigned take_selected;
    uint64_t position;
    bool playing;
    /* Live parts of an arrangement: score events dispatched at their frame
     * while playing; part_slot maps a part to its rack slot (-1: frozen). */
    const wb_note_event *score;
    size_t score_count, score_next;
    int8_t part_slot[WB_PARTS];
    /* Safety. */
    unsigned over_run;
    bool muted;
    /* Last period's levels, copied into the meters by wb_live_publish. */
    float last_peak_left, last_peak_right, last_rms;
    /* Meters, written by the audio thread under a sequence counter. */
    _Atomic uint32_t meter_sequence;
    wb_live_meters meters;
    /* Thread. */
    _Atomic bool running;
    void *thread;                  /* pthread_t, opaque here */
    void *pcm;                     /* live_pcm */
    char device[64];
    unsigned period;
    /* What the audio thread got: 3 real time through RealtimeKit, 2
     * SCHED_FIFO, 1 raised nice, 0 normal. */
    _Atomic int scheduling;
    /* Optional: asks the desktop (RealtimeKit) for real time when the
     * process may not set SCHED_FIFO itself; called on the audio thread. */
    bool (*raise_priority)(void);
    char error[160];
} wb_live;

/* ---- real time (wb_live_rt.c) ------------------------------------------- */

/* UI side: false when the ring is full (the command is dropped). */
bool wb_live_send(wb_live *live, wb_live_cmd cmd);
/* Audio side: apply pending commands, then render `frames` stereo frames. */
void wb_live_process(wb_live *live, float *out, size_t frames);
void wb_live_publish(wb_live *live, float budget_ms, float period_ms, uint64_t xruns);
/* UI side: a consistent copy of the meters. */
wb_live_meters wb_live_read(wb_live *live);

/* ---- setup and thread (wb_live.c) ------------------------------------------ */

wb_live *wb_live_create(unsigned rate);
void wb_live_destroy(wb_live *live);
/* Install a session part as a live instrument (audio stopped). */
bool wb_live_load_part(wb_live *live, unsigned slot, const wb_session *session, const char *session_dir,
                       unsigned part, wb_error *error);
void wb_live_clear_slot(wb_live *live, unsigned slot);
/* Install a playback take (audio stopped); the buffer stays owned by the caller. */
void wb_live_set_take(wb_live *live, unsigned index, const float *samples, uint64_t frames, float gain);
/* Install score events for live parts (audio stopped); null clears. */
void wb_live_set_score(wb_live *live, const wb_note_event *events, size_t count, const int8_t part_slot[WB_PARTS]);
bool wb_live_start(wb_live *live, const char *device, unsigned period, unsigned periods);
void wb_live_stop(wb_live *live);
bool wb_live_running(const wb_live *live);

#endif
