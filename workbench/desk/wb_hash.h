/* SHA-256 for session provenance: source copies, session files, patches,
 * derived MIDI and finished WAVs.  FNV-64 for PCM stays tw_fnv1a64. */
#ifndef WB_HASH_H
#define WB_HASH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { WB_SHA256_BYTES = 32, WB_SHA256_HEX = 64 };

typedef struct {
    uint32_t state[8];
    uint64_t length;
    uint8_t block[64];
    size_t used;
} wb_sha256;

void wb_sha256_init(wb_sha256 *hash);
void wb_sha256_update(wb_sha256 *hash, const void *data, size_t size);
void wb_sha256_final(wb_sha256 *hash, uint8_t digest[WB_SHA256_BYTES]);

/* Lowercase hex, NUL-terminated (65 bytes). */
void wb_sha256_hex(const uint8_t digest[WB_SHA256_BYTES],
                   char hex[WB_SHA256_HEX + 1]);
void wb_sha256_bytes_hex(const void *data, size_t size,
                         char hex[WB_SHA256_HEX + 1]);
/* Hash a whole file; false when it cannot be read. */
bool wb_sha256_file_hex(const char *path, char hex[WB_SHA256_HEX + 1]);
/* True for exactly 64 lowercase hex digits. */
bool wb_sha256_hex_valid(const char *text);

#endif
