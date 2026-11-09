/* libFuzzer harness for the .mws reader and writer (make fuzz-mws).
 * Any accepted input must write canonical text that reads back and writes
 * the same bytes again; block reads must leave a session consistent. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../desk/wb_format.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    static wb_session a, b;
    wb_error e;
    if (!wb_read((const char *)data, size, &a, &e)) {
        /* Also feed the bytes as a block into a fresh session. */
        static const char BASE[] = "mws=1\n[session]\nname=f\n[stem]\nid=x\n";
        if (wb_read(BASE, sizeof BASE - 1, &b, &e)) {
            wb_read_object(&b, WB_KIND_STEM, 0, false, (const char *)data, size, &e);
            wb_read_object(&b, WB_KIND_LANE, 0, true, (const char *)data, size, &e);
        }
        return 0;
    }
    (void)wb_session_validate(&a, &e);
    wb_buf one = { 0 }, two = { 0 };
    if (!wb_write(&a, &one)) abort();
    if (!wb_read(one.data, one.length, &b, &e)) abort();
    if (!wb_write(&b, &two)) abort();
    if (one.length != two.length || memcmp(one.data, two.data, one.length)) abort();
    wb_buf_free(&one);
    wb_buf_free(&two);
    return 0;
}
