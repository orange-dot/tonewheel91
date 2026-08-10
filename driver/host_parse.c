#include "host_parse.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>

bool host_parse_u64(const char *text, uint64_t min, uint64_t max,
                    uint64_t *out) {
    if (!text || !*text || *text == '-') return false;
    errno = 0;
    char *end = 0;
    unsigned long long value = strtoull(text, &end, 10);
    if (errno == ERANGE || end == text || *end || value < min || value > max)
        return false;
    *out = (uint64_t)value;
    return true;
}

bool host_parse_double(const char *text, double min, double max, double *out) {
    if (!text || !*text) return false;
    errno = 0;
    char *end = 0;
    double value = strtod(text, &end);
    if (errno == ERANGE || end == text || *end || !isfinite(value)
        || value < min || value > max)
        return false;
    *out = value;
    return true;
}

bool host_size_add(size_t a, size_t b, size_t *out) {
    if (a > SIZE_MAX - b) return false;
    *out = a + b;
    return true;
}

bool host_size_mul(size_t a, size_t b, size_t *out) {
    if (a && b > SIZE_MAX / a) return false;
    *out = a * b;
    return true;
}
