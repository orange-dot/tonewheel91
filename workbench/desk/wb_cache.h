/* Per-part preview cache (docs/workbench-plan.md §6.2, W8).
 *
 * A part's cache file holds its post-gain stereo output over the whole form,
 * rendered alone from frame 0 so its state is exact.  The key hashes
 * everything that affects that part's audio: its block, its patch file
 * bytes, its derived notes and controllers, its lanes and the curves they
 * use, the form, the session head (rate, engine, compat) and a cache format
 * version.  Edit one patch and only that part's key changes.
 *
 * Cached playback is a preview: parts are summed without stems or fades,
 * so its bits differ from a final render, which always runs as one pass. */
#ifndef WB_CACHE_H
#define WB_CACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wb_orch.h"
#include "wb_session.h"

bool wb_cache_key(const wb_session *session, const char *session_dir, const wb_derived *derived,
                  unsigned part, char key[65]);
/* <dir>/cache/<key>.f32 */
void wb_cache_path(const char *session_dir, const char key[65], char *path, size_t size);
bool wb_cache_valid(const char *session_dir, const char key[65]);
/* Interleaved stereo f32; the caller frees *samples. */
bool wb_cache_load(const char *session_dir, const char key[65], float **samples, uint64_t *frames);

#endif
