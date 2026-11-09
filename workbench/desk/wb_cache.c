#define _POSIX_C_SOURCE 200809L
#include "wb_cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "wb_format.h"
#include "wb_hash.h"
#include "wb_patch.h"

static void add_object(wb_sha256 *h, const wb_session *s, wb_kind kind, unsigned index) {
    wb_buf b = { 0 };
    if (wb_write_object(s, kind, index, &b)) wb_sha256_update(h, b.data, b.length);
    wb_buf_free(&b);
}

bool wb_cache_key(const wb_session *s, const char *dir, const wb_derived *d, unsigned part, char key[65]) {
    if (part >= s->part_count || part >= d->part_count) return false;
    wb_sha256 h;
    wb_sha256_init(&h);
    static const char VERSION[] = "wb-part-cache-1";
    wb_sha256_update(&h, VERSION, sizeof VERSION);
    add_object(&h, s, WB_KIND_SESSION, 0);
    add_object(&h, s, WB_KIND_FORM, 0);
    add_object(&h, s, WB_KIND_PART, part);
    char path[4400];
    snprintf(path, sizeof path, "%s/%s", dir, s->part[part].patch);
    char *bytes;
    size_t size;
    if (s->part[part].engine == WB_PART_MAMUT) {
        /* The flattened patch, so a change anywhere up a parent chain
         * invalidates the cache. */
        ma_patch_document doc;
        wb_error e;
        if (!wb_patch_load(dir, s->part[part].patch, &doc, &e)) return false;
        FILE *f = open_memstream(&bytes, &size);
        if (!f) return false;
        bool written = ma_patch_write(f, &doc);
        if (fclose(f) || !written) { free(bytes); return false; }
    } else if (!wb_read_file(path, &bytes, &size)) return false;
    wb_sha256_update(&h, bytes, size);
    free(bytes);
    const wb_opart *p = &d->part[part];
    /* Field by field: struct padding is not part of the key. */
    for (size_t i = 0; i < p->count; i++) {
        uint8_t x[18];
        memcpy(x, &p->note[i].start, 8);
        memcpy(x + 8, &p->note[i].end, 8);
        x[16] = p->note[i].note;
        x[17] = p->note[i].velocity;
        wb_sha256_update(&h, x, sizeof x);
    }
    for (size_t i = 0; i < p->ctl_count; i++) {
        uint8_t x[11];
        memcpy(x, &p->ctl[i].tick, 8);
        x[8] = p->ctl[i].kind; x[9] = p->ctl[i].d1; x[10] = p->ctl[i].d2;
        wb_sha256_update(&h, x, sizeof x);
    }
    /* The derived tempo map times the notes. */
    wb_sha256_update(&h, d->tempo, d->tempo_count * sizeof d->tempo[0]);
    for (unsigned l = 0; l < s->lane_count; l++) {
        if (strcmp(s->lane[l].part, s->part[part].id)) continue;
        add_object(&h, s, WB_KIND_LANE, l);
        int c = s->lane[l].curve[0] ? wb_find_curve(s, s->lane[l].curve) : -1;
        if (c >= 0) add_object(&h, s, WB_KIND_CURVE, (unsigned)c);
    }
    uint8_t digest[WB_SHA256_BYTES];
    wb_sha256_final(&h, digest);
    wb_sha256_hex(digest, key);
    return true;
}

void wb_cache_path(const char *dir, const char key[65], char *path, size_t size) {
    snprintf(path, size, "%s/cache/%.64s.f32", dir, key);
}

bool wb_cache_valid(const char *dir, const char key[65]) {
    char path[4400];
    wb_cache_path(dir, key, path, sizeof path);
    struct stat st;
    return stat(path, &st) == 0 && st.st_size > 0 && st.st_size % 8 == 0;
}

bool wb_cache_load(const char *dir, const char key[65], float **samples, uint64_t *frames) {
    char path[4400];
    wb_cache_path(dir, key, path, sizeof path);
    char *bytes;
    size_t size;
    *samples = nullptr;
    *frames = 0;
    if (!wb_read_file(path, &bytes, &size) || size % 8) return false;
    *samples = (float *)(void *)bytes;
    *frames = size / 8;
    return true;
}
