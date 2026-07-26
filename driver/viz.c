/* Raster primitives and the PNG writer. See viz.h.
 *
 * A stored deflate block carries its payload verbatim behind a five-byte
 * header, so "compressed" here means framed: signature, IHDR, one IDAT
 * holding a zlib stream of one stored block per image row, IEND. The
 * cost against a real deflate is the filter byte plus five bytes a row;
 * what is bought is that the whole encoder is a CRC and an Adler sum.
 * Run the artifacts through a recompressor if size ever matters. */
#include <stdio.h>
#include "viz.h"

void viz_px(unsigned char *rgb, int w, int h, int x, int y, uint32_t colour) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    unsigned char *p = rgb + 3u * ((size_t)y * (size_t)w + (size_t)x);
    p[0] = (unsigned char)(colour >> 16);
    p[1] = (unsigned char)(colour >> 8);
    p[2] = (unsigned char)colour;
}

void viz_line(unsigned char *rgb, int w, int h,
              int x0, int y0, int x1, int y1, uint32_t colour) {
    int dx = x1 - x0, dy = y1 - y0;
    int sx = (dx < 0) ? -1 : 1, sy = (dy < 0) ? -1 : 1;
    dx = (dx < 0) ? -dx : dx;
    dy = (dy < 0) ? -dy : dy;
    for (int err = dx - dy;;) {
        viz_px(rgb, w, h, x0, y0, colour);
        if (x0 == x1 && y0 == y1) return;
        int e2 = err + err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

typedef struct {
    FILE *f;
    uint32_t crc;  /* chunk CRC-32, pre-inversion                       */
    uint32_t a, b; /* Adler-32 halves over the raw (pre-deflate) stream */
} png_out;

static void be32(unsigned char b[4], uint32_t v) {
    b[0] = (unsigned char)(v >> 24);
    b[1] = (unsigned char)(v >> 16);
    b[2] = (unsigned char)(v >> 8);
    b[3] = (unsigned char)v;
}

/* Chunk bytes: into the file and the running CRC. */
static void wr(png_out *o, const void *p, size_t n) {
    const unsigned char *b = p;
    for (size_t i = 0; i < n; i++) {
        o->crc ^= b[i];
        for (int k = 0; k < 8; k++)
            o->crc = (o->crc >> 1) ^ ((o->crc & 1u) ? 0xedb88320u : 0u);
    }
    fwrite(p, 1, n, o->f);
}

/* Raw stream bytes: the Adler sum covers these and nothing else. */
static void raw(png_out *o, const void *p, size_t n) {
    const unsigned char *b = p;
    for (size_t i = 0; i < n; i++) {
        o->a = (o->a + b[i]) % 65521u;
        o->b = (o->b + o->a) % 65521u;
    }
    wr(o, p, n);
}

static void chunk_head(png_out *o, uint32_t len, const char *type) {
    unsigned char b[4];
    be32(b, len);
    fwrite(b, 1, 4, o->f); /* the length prefix sits outside the CRC */
    o->crc = 0xffffffffu;
    wr(o, type, 4);
}

static void chunk_end(png_out *o) {
    unsigned char b[4];
    be32(b, ~o->crc);
    fwrite(b, 1, 4, o->f);
}

int viz_write_png(const char *path, const unsigned char *rgb, int w, int h) {
    if (w < 1 || h < 1 || w > 21844) return -1; /* a row must fit one block */

    png_out o = { .f = fopen(path, "wb"), .crc = 0, .a = 1, .b = 0 };
    if (!o.f) return -1;

    static const unsigned char sig[8] = { 137, 'P', 'N', 'G', 13, 10, 26, 10 };
    fwrite(sig, 1, 8, o.f);

    unsigned char ihdr[13] = { 0 };
    be32(ihdr, (uint32_t)w);
    be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8; /* bit depth                   */
    ihdr[9] = 2; /* colour type: truecolour RGB */
    chunk_head(&o, 13, "IHDR");
    wr(&o, ihdr, 13);
    chunk_end(&o);

    /* Each row is a filter byte plus its pixels, framed in a stored block
     * of its own; BFINAL rides the last one. */
    const uint32_t row = 1u + 3u * (uint32_t)w;
    chunk_head(&o, 2u + (5u + row) * (uint32_t)h + 4u, "IDAT");
    static const unsigned char zhdr[2] = { 0x78, 0x01 };
    wr(&o, zhdr, 2);
    for (int y = 0; y < h; y++) {
        const unsigned char blk[5] = {
            (unsigned char)(y == h - 1),
            (unsigned char)row, (unsigned char)(row >> 8),
            (unsigned char)~(unsigned char)row,
            (unsigned char)~(unsigned char)(row >> 8),
        };
        static const unsigned char none = 0; /* filter: None */
        wr(&o, blk, 5);
        raw(&o, &none, 1);
        raw(&o, rgb + 3u * (size_t)y * (size_t)w, 3u * (size_t)w);
    }
    unsigned char adler[4];
    be32(adler, (o.b << 16) | o.a);
    wr(&o, adler, 4);
    chunk_end(&o);

    chunk_head(&o, 0, "IEND");
    chunk_end(&o);

    int bad = ferror(o.f) != 0;
    if (fclose(o.f) != 0) bad = 1;
    return bad ? -1 : 0;
}
