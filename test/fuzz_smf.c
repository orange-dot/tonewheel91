#include <stddef.h>
#include <stdint.h>

#include "../driver/smf.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    smf_file file = { 0 };
    smf_error error = { 0 };
    (void)smf_parse(data, size, UINT16_MAX, &file, &error);
    smf_dispose(&file);
    return 0;
}
