#ifndef SMF_H
#define SMF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t tick;
    uint32_t seq;
    uint8_t status, d1, d2;
} smf_event;

typedef struct {
    uint64_t tick;
    uint32_t seq;
    uint32_t us_per_quarter;
} smf_tempo;

typedef struct {
    unsigned format, tracks, division;
    smf_event *events;
    size_t event_count;
    smf_tempo *tempos;
    size_t tempo_count;
} smf_file;

typedef struct {
    size_t offset;
    const char *message;
} smf_error;

/* Parse bounded SMF format 0/1 with PPQ division. On success, out owns the
 * event and tempo arrays and must be passed to smf_dispose. On failure, out
 * is empty and error, when supplied, points to a static diagnostic string. */
bool smf_parse(const uint8_t *data, size_t size, uint16_t channel_mask,
               smf_file *out, smf_error *error);
void smf_dispose(smf_file *file);

#endif
