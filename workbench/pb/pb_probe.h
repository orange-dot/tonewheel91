/* The probe (docs/patchbench-plan.md §4): a worker thread that owns private
 * ma_synth voices and renders the audible patch at the probe note, tapping
 * the values between the stages of the voice tick.  The audio thread is not
 * involved.
 *
 * Tap route (§4.2): the hosted mirror.  pb_probe_tick repeats the scalar
 * tick of src/ma_voice.c from the stage functions ma_voice_impl.h exports;
 * the core is untouched.  The guarantee is the identity test: the mirror's
 * output equals ma_synth_tick bit for bit over whole notes. */
#ifndef PB_PROBE_H
#define PB_PROBE_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "../../src/mamutanalog.h"
#include "pb_fft.h"

typedef enum : unsigned char {
    PB_TAP_VCO1, PB_TAP_VCO2, PB_TAP_BUS,             /* 8x source rate */
    PB_TAP_RASTER, PB_TAP_MOZAIK, PB_TAP_GRAINS,      /* 2x */
    PB_TAP_PRESSURE, PB_TAP_LADDER, PB_TAP_GLAS, PB_TAP_VCA, PB_TAP_OUTPUT,   /* 1x */
    PB_TAP_COUNT
} pb_tap;

enum {
    PB_CAPTURE = 32768, PB_SPECTRUM = 8192, PB_RESPONSE = 40, PB_NOTE_POINTS = 1024,
    PB_PORTRAIT = 2400, PB_TILES = 64, PB_CURVE = 129, PB_COMPASS = 6, PB_COMPASS_POINTS = 256,
    PB_COMPONENT_POINTS = 512,
    PB_COMPONENTS = 6,
};

/* One frame's tap values, at the rates above. */
typedef struct {
    float source[8], at_2x[2];
    float raster[2], mozaik[2], grains[2];
    float pressure, ladder, glas, vca;
    ma_frame output;
    float cutoff_hz;
} pb_taps;

/* The scalar tick with taps; returns ma_synth_tick's frame. */
ma_frame pb_probe_tick(ma_synth *s, pb_taps *taps);

typedef struct {
    float x[PB_CAPTURE];
    unsigned count;          /* samples in x, oldest first */
    float rate_hz;
    unsigned cycle;          /* samples in two cycles of the probe note */
    float db[PB_SPECTRUM / 2 + 1];
    pb_harmonics harmonics;
} pb_tap_capture;

typedef struct {
    uint64_t generation;
    ma_patch patch;
    uint8_t note, velocity;
    float f0, rate;
    double pass_ms, note_ms;
    bool note_ready, response_ready, compass_ready, components_ready;

    pb_tap_capture tap[PB_TAP_COUNT];

    /* Effective values (§4.4): per catalog field, NaN where the front end
     * does not resolve it. */
    double effective[64];
    ma_identity identity;
    float cutoff_effective_hz, cutoff_env_low_hz, cutoff_env_high_hz;
    float guard[2];
    float sync_effective, crossmod_effective;
    uint8_t raster_mip;
    float raster_blend;
    float glas_centre_hz[MA_FORMANTS];
    int16_t glas_captured[MA_FORMANTS];
    float glas_ratio[MA_FORMANTS];
    uint64_t glas_captures, glas_releases, glas_stolen;
    uint64_t bcs_resets;
    float bcs_maximum;

    /* Pictures. */
    float response_hz[PB_RESPONSE], response_low_db[PB_RESPONSE], response_nominal_db[PB_RESPONSE];
    float response_low_level, response_nominal_level;
    float portrait[4][PB_PORTRAIT];          /* hopf re, im, duffing x, v */
    float bcs_trace[PB_PORTRAIT];
    unsigned portrait_count;
    float tile_len[PB_TILES], tile_sign[PB_TILES];
    unsigned tile_count;
    float pressure_curve[PB_CURVE];          /* x from -1.5 to 1.5 */
    float pressure_peak;
    float body_in[PB_CURVE], body_out[PB_CURVE];
    unsigned body_count;

    /* Phase B: a whole note, the filter response above, and the waveform
     * components. */
    float note_amp[PB_NOTE_POINTS], note_filter[PB_NOTE_POINTS], note_cutoff[PB_NOTE_POINTS],
        note_peak[PB_NOTE_POINTS];
    unsigned note_count;
    float note_seconds, note_release_at;
    float component[2][PB_COMPONENTS][PB_COMPONENT_POINTS];   /* VCO, saw/pulse/tri/sine/tri sine/tri saw, two cycles */
    /* Phase C, on request: VCO1 at C1..C6. */
    float compass[PB_COMPASS][PB_COMPASS_POINTS];
    float compass_guard[PB_COMPASS];
} pb_probe_result;

typedef struct {
    ma_patch patch;
    uint8_t note, velocity;
    bool compass;
} pb_probe_request;

typedef struct pb_probe {
    pthread_mutex_t lock;
    pthread_cond_t wake;
    pthread_t thread;
    bool started, quit, has_request, fresh;
    pb_probe_request request;
    uint64_t request_ns;
    _Atomic uint64_t latest;
    pb_probe_result *work, *ready, *shown;
    unsigned rate;
    /* Optional: true while the audio thread is above 80 % of its period. */
    bool (*busy)(void *);
    void *busy_arg;
} pb_probe;

bool pb_probe_start(pb_probe *p, unsigned rate);
void pb_probe_stop(pb_probe *p);
void pb_probe_post(pb_probe *p, const pb_probe_request *request);
/* The newest finished result (stable until the next call), or null. */
const pb_probe_result *pb_probe_acquire(pb_probe *p);

/* The core's Mozaik slope map and its five detents, for the slope ruler. */
uint32_t pb_mozaik_slope_q32(float control);
uint32_t pb_mozaik_detent_q32(int detent);

/* One synchronous pass into `out` (tests, bounce manifest, smoke). */
void pb_probe_run(unsigned rate, const pb_probe_request *request, pb_probe_result *out);

#endif
