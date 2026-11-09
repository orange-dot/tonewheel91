/* Export the Hurt orchestra exhibit's part patches as .mapatch files and
 * print its part configuration, so the session fixture can be checked
 * against the C source it restates.
 *
 *   export_hurt_orch EDITION OUT_DIR
 *
 * The exhibit is compiled into this unit with its main renamed; its static
 * patch helpers stay the only definition of the sounds. */
#define main hurt_orch_exhibit_main
#include "../../driver/exhibit_ma3_hurt_orch.c"
#undef main

#include <sys/stat.h>

#include "../../driver/ma_patch_file.h"

int main(int argc, char **argv) {
    uint64_t n = 0;
    if (argc != 3 || !host_parse_u64(argv[1], 1, 2, &n)) {
        fprintf(stderr, "usage: %s 1|2 OUT_DIR\n", argv[0]);
        return 2;
    }
    edition = (unsigned)n;
    if (mkdir(argv[2], 0755) && errno != EEXIST) {
        perror(argv[2]);
        return 1;
    }
    static ma3_show s;
    s = (ma3_show){ .part_count = O_PARTS, .stem_count = ST_COUNT,
                    .stem_name = { "tonal", "bass", "pads", "vocal", "lead", "rhythm" } };
    parts_init(&s);
    for (unsigned p = 0; p < O_PARTS; p++) {
        const ma3_part *part = &s.part[p];
        ma_patch_document doc = { .value = part->patch };
        snprintf(doc.name, sizeof doc.name, "%s", part->name);
        char path[4096];
        snprintf(path, sizeof path, "%s/%s.mapatch", argv[2], part->name);
        ma_patch_error error = { 0 };
        if (!ma_patch_save(path, &doc, &error)) {
            fprintf(stderr, "%s: %s\n", path, error.message);
            return 1;
        }
        /* Round trip: the saved text must give back the same struct bits. */
        ma_patch_document back;
        if (!ma_patch_load(path, &back, &error) || memcmp(&back.value, &part->patch, sizeof back.value)) {
            fprintf(stderr, "%s: does not round-trip\n", path);
            return 1;
        }
        printf("%s limit=%u unison=%u stem=%s gain=%.9g chorus=%.9g reverb=%.9g "
               "detune=%.9g character=%.9g seed=%016llx\n",
               part->name, part->config.limit, part->config.unison_count,
               s.stem_name[part->stem], (double)part->gain, (double)part->chorus.mix,
               (double)part->reverb.mix, (double)part->config.detune_cents,
               (double)part->character, (unsigned long long)part->config.session_seed);
    }
    return 0;
}
