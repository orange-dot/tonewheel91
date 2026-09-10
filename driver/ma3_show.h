/* Hosted score/render desk for the two MA3 musical exhibits. */
#ifndef MA3_SHOW_H
#define MA3_SHOW_H
#include "../src/mamutanalog.h"

enum { MA3_RATE=48000, MA3_PARTS=10, MA3_STEMS=5, MA3_EVENTS=16000 };
typedef struct {
    const char *name;
    ma_patch patch;
    ma_voice_pool_config config;
    ma_chorus_controls chorus;
    ma_reverb_controls reverb;
    float gain, character;
    unsigned stem;
} ma3_part;
typedef struct {
    size_t frame;
    unsigned order;
    uint8_t part, channel, note, velocity;
    float pressure;
    bool on;
} ma3_event;
typedef struct ma3_show ma3_show;
struct ma3_show {
    const char *name;
    unsigned part_count, stem_count, seconds;
    const char *stem_name[MA3_STEMS];
    ma3_part part[MA3_PARTS];
    ma3_event event[MA3_EVENTS];
    size_t event_count;
    void (*controls)(ma_voice_pool *pools, const ma3_show *show, size_t frame);
};
void ma3_part_init(ma3_part *part, const char *name, ma_patch patch,
    unsigned limit, unsigned unison, unsigned stem, float gain, float chorus_mix, float reverb_mix);
bool ma3_add_event(ma3_show *show, ma3_event event);
bool ma3_check(ma3_show *show);
/* Two complete passes; write only the second, then compare per-stem hashes.
 * seconds limits the captured prefix for short checks; zero selects the full show. */
bool ma3_render(const ma3_show *show, const char *prefix, unsigned seconds);
#endif
