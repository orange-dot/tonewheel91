/* Authored → audible (docs/patchbench-plan.md §3.3): solo and mute make a
 * derived patch for the audio thread and the probe; the authored patch is
 * never touched.  Morph interpolates two patches. */
#ifndef PB_DERIVE_H
#define PB_DERIVE_H

#include <stdbool.h>

#include "../../src/mamutanalog.h"

typedef enum : unsigned char {
    PB_SRC_VCO1, PB_SRC_VCO2, PB_SRC_RASTER, PB_SRC_MOZAIK, PB_SRC_NOISE, PB_SRC_COUNT
} pb_source;

/* Shaping stages with an exact bypass (a mix or amount at literal zero),
 * plus the filter, whose "mute" opens it: cutoff at the top, no resonance,
 * drive, envelope or key tracking. */
typedef enum : unsigned char {
    PB_STAGE_PRESSURE, PB_STAGE_BCS, PB_STAGE_FILTER, PB_STAGE_GLAS, PB_STAGE_BODY, PB_STAGE_COUNT
} pb_stage;

typedef struct {
    bool solo[PB_SRC_COUNT];
    bool mute[PB_STAGE_COUNT];
} pb_monitor;

/* No solo set means every source sounds; a source outside a non-empty solo
 * set is removed.  VCO1 is removed by zeroing its four waveform levels (sync
 * and cross-mod stay as they were); VCO2 by its level, so its waveform still
 * drives cross-mod and its phase still drives sync. */
ma_patch pb_derive(const ma_patch *authored, const pb_monitor *monitor);
bool pb_monitor_active(const pb_monitor *monitor);

/* Floats interpolate; the interval switches at the midpoint.  t = 0 and
 * t = 1 return a and b bit for bit. */
ma_patch pb_morph(const ma_patch *a, const ma_patch *b, double t);

#endif
