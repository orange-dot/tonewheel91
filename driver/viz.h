/* Raster output for the offline exhibits: 8-bit RGB in, PNG out. Hosted
 * code (driver layer), same posture as wav.h — plumbing this repo writes
 * rather than vendors (docs/design.md dependency policy). */
#ifndef VIZ_H
#define VIZ_H

#include <stdint.h>

/* Pixel buffers are caller-owned, row-major, three bytes per pixel; the
 * drivers keep them static, as they do their render buffers. Colours are
 * 0xRRGGBB. Both drawing calls clip. */
void viz_px(unsigned char *rgb, int w, int h, int x, int y, uint32_t colour);
void viz_line(unsigned char *rgb, int w, int h,
              int x0, int y0, int x1, int y1, uint32_t colour);

/* PNG with stored (uncompressed) deflate blocks, one per row: no
 * compressor, no dependency. Returns 0, or -1 on I/O failure or on a
 * width outside 1..21844 (the 65535-byte stored-block limit) or a
 * height below 1. */
int viz_write_png(const char *path, const unsigned char *rgb, int w, int h);

#endif
