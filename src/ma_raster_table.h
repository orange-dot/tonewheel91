#ifndef MA_RASTER_TABLE_H
#define MA_RASTER_TABLE_H

/* Offline-generated, peak-normalized additive families. One copy, in
 * ma_raster_table.c, serves the scalar raster (ma_voice.c) and the vector
 * lanes (ma_lanes.c). */

#include <stdint.h>

enum {
    MA_RASTER_FAMILIES = 8,
    MA_RASTER_MIPS = 7,
    MA_RASTER_SAMPLES = 256,
};

extern const int16_t MA_RASTER_TABLE[MA_RASTER_FAMILIES][MA_RASTER_MIPS][MA_RASTER_SAMPLES];

#endif
