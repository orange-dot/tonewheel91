#ifndef HOST_PARSE_H
#define HOST_PARSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool host_parse_u64(const char *text, uint64_t min, uint64_t max,
                    uint64_t *out);
bool host_parse_double(const char *text, double min, double max, double *out);
bool host_size_add(size_t a, size_t b, size_t *out);
bool host_size_mul(size_t a, size_t b, size_t *out);

#endif
