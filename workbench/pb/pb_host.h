/* Host services for the live path (docs/patchbench-plan.md §5, live-path
 * tuning 2026-11-11): what the audio thread may ask of the operating
 * system, all optional and reversible.
 *
 *   - RealtimeKit: real-time scheduling for one thread without rtprio
 *     rights, through the system bus.
 *   - Power profile hold: "performance" while audio runs, through
 *     power-profiles (tuned-ppd here); the hold ends when released or when
 *     the process exits.
 *   - CPU layout: one physical core for the audio thread, the others for
 *     the UI and the probe.
 *   - Memory locking of the regions the audio thread touches.
 *
 * The bus calls go through libsystemd's sd-bus, loaded with dlopen, so the
 * build has no new dependency; without it these calls report failure. */
#ifndef PB_HOST_H
#define PB_HOST_H

#include <sched.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* Make thread `tid` real time through RealtimeKit at the highest priority
 * it grants. Sets RLIMIT_RTTIME first, as RealtimeKit requires. The granted
 * priority goes to *priority. */
bool pb_rtkit_realtime(pid_t tid, int *priority);

typedef struct {
    void *bus;
    uint32_t cookie;
    bool held;
} pb_power_hold;
/* Hold `profile` ("performance") with a reason; false when the service or
 * libsystemd is not there. Release is idempotent. */
bool pb_power_hold_start(pb_power_hold *h, const char *profile, const char *reason);
void pb_power_hold_end(pb_power_hold *h);
/* The active profile, or "" when unknown. */
void pb_power_profile(char *out, size_t size);

typedef struct {
    int audio_cpu;          /* first logical CPU of the last physical core, -1 none */
    cpu_set_t audio_core;   /* every logical CPU of that core */
    cpu_set_t others;       /* every other logical CPU */
    int cores, cpus;
} pb_cpu_layout;
/* From /sys/devices/system/cpu/cpuN/topology; with one physical core there is
 * nothing to separate and audio_cpu is -1. */
bool pb_cpu_layout_read(pb_cpu_layout *layout);
/* The first CPU of `others` (for the voice-bank worker), and `others`
 * without it when that leaves at least one CPU (for the UI and probe). */
int pb_cpu_worker(const pb_cpu_layout *layout, cpu_set_t *rest);
/* The current frequency of `cpu` in MHz, 0 when unknown. */
unsigned pb_cpu_mhz(int cpu);

/* mlock a region; the locked byte count accumulates into *total. */
bool pb_lock(const void *at, size_t size, size_t *total);
void pb_unlock(const void *at, size_t size);

#endif
