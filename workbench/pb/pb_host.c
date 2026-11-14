/* Host services for the live path; see pb_host.h. */
#define _GNU_SOURCE
#include "pb_host.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>

/* ---- sd-bus through dlopen ---------------------------------------------- */

typedef struct { const char *name, *message; int need_free; } bus_error;
typedef struct {
    bool tried, ok;
    int (*open_system)(void **bus);
    int (*call)(void *bus, const char *dest, const char *path, const char *iface, const char *member,
                bus_error *error, void **reply, const char *types, ...);
    int (*get_property)(void *bus, const char *dest, const char *path, const char *iface, const char *member,
                        bus_error *error, void **reply, const char *type);
    int (*read)(void *msg, const char *types, ...);
    void *(*message_unref)(void *msg);
    void *(*bus_unref)(void *bus);
    void (*error_free)(bus_error *error);
} sdbus;

static sdbus *bus_api(void) {
    static sdbus api;
    if (api.tried) return api.ok ? &api : nullptr;
    api.tried = true;
    void *lib = dlopen("libsystemd.so.0", RTLD_NOW | RTLD_LOCAL);
    if (!lib) return nullptr;
    /* POSIX makes dlsym's result convertible to a function pointer; ISO C
     * does not, so copy the bits instead of casting. */
#define LOAD(field, name) do {                                                \
        void *symbol = dlsym(lib, name);                                      \
        static_assert(sizeof api.field == sizeof symbol, "function pointer size"); \
        memcpy(&api.field, &symbol, sizeof symbol);                           \
    } while (0)
    LOAD(open_system, "sd_bus_open_system");
    LOAD(call, "sd_bus_call_method");
    LOAD(get_property, "sd_bus_get_property");
    LOAD(read, "sd_bus_message_read");
    LOAD(message_unref, "sd_bus_message_unref");
    LOAD(bus_unref, "sd_bus_flush_close_unref");
    LOAD(error_free, "sd_bus_error_free");
#undef LOAD
    api.ok = api.open_system && api.call && api.get_property && api.read && api.message_unref && api.bus_unref
          && api.error_free;
    return api.ok ? &api : nullptr;
}

/* ---- RealtimeKit -------------------------------------------------------- */

static const char RTKIT[] = "org.freedesktop.RealtimeKit1", RTKIT_PATH[] = "/org/freedesktop/RealtimeKit1";

static long long rtkit_property(sdbus *api, void *bus, const char *name, const char *type) {
    bus_error e = { 0 };
    void *reply = nullptr;
    long long value = -1;
    if (api->get_property(bus, RTKIT, RTKIT_PATH, RTKIT, name, &e, &reply, type) >= 0) {
        if (type[0] == 'i') {
            int32_t v;
            if (api->read(reply, "i", &v) >= 0) value = v;
        } else {
            int64_t v;
            if (api->read(reply, "x", &v) >= 0) value = v;
        }
    }
    if (reply) api->message_unref(reply);
    api->error_free(&e);
    return value;
}

bool pb_rtkit_realtime(pid_t tid, int *priority) {
    sdbus *api = bus_api();
    void *bus = nullptr;
    if (!api || api->open_system(&bus) < 0) return false;
    long long max_priority = rtkit_property(api, bus, "MaxRealtimePriority", "i");
    long long rttime = rtkit_property(api, bus, "RTTimeUSecMax", "x");
    if (max_priority < 1) max_priority = 1;
    if (rttime <= 0) rttime = 200000;
    /* RealtimeKit needs the hard limit at most RTTimeUSecMax. The soft limit
     * sits 50 ms below it, so an overloaded thread gets SIGXCPU (which the
     * engine answers by leaving real time) before the hard limit's SIGKILL. */
    rlim_t soft = rttime > 100000 ? (rlim_t)rttime - 50000 : (rlim_t)rttime / 2;
    struct rlimit limit = { soft, (rlim_t)rttime };
    bool ok = setrlimit(RLIMIT_RTTIME, &limit) == 0;
    bus_error e = { 0 };
    void *reply = nullptr;
    ok = ok && api->call(bus, RTKIT, RTKIT_PATH, RTKIT, "MakeThreadRealtime", &e, &reply, "tu",
                         (uint64_t)tid, (uint32_t)max_priority) >= 0;
    if (reply) api->message_unref(reply);
    api->error_free(&e);
    api->bus_unref(bus);
    if (ok && priority) *priority = (int)max_priority;
    return ok;
}

/* ---- power profile ------------------------------------------------------ */

static const char PPD[] = "net.hadess.PowerProfiles", PPD_PATH[] = "/net/hadess/PowerProfiles";

bool pb_power_hold_start(pb_power_hold *h, const char *profile, const char *reason) {
    if (h->held) return true;
    sdbus *api = bus_api();
    if (!api || api->open_system(&h->bus) < 0) {
        h->bus = nullptr;
        return false;
    }
    bus_error e = { 0 };
    void *reply = nullptr;
    uint32_t cookie = 0;
    bool ok = api->call(h->bus, PPD, PPD_PATH, PPD, "HoldProfile", &e, &reply, "sss", profile, reason,
                        "tonewheel91-patchbench") >= 0
           && api->read(reply, "u", &cookie) >= 0;
    if (reply) api->message_unref(reply);
    api->error_free(&e);
    if (!ok) {
        api->bus_unref(h->bus);
        h->bus = nullptr;
        return false;
    }
    h->cookie = cookie;
    h->held = true;
    return true;
}

void pb_power_hold_end(pb_power_hold *h) {
    if (!h->held) return;
    sdbus *api = bus_api();
    bus_error e = { 0 };
    void *reply = nullptr;
    if (api) {
        if (api->call(h->bus, PPD, PPD_PATH, PPD, "ReleaseProfile", &e, &reply, "u", h->cookie) >= 0 && reply)
            api->message_unref(reply);
        api->error_free(&e);
        api->bus_unref(h->bus);   /* closing the connection releases it too */
    }
    *h = (pb_power_hold){ 0 };
}

void pb_power_profile(char *out, size_t size) {
    out[0] = 0;
    sdbus *api = bus_api();
    void *bus = nullptr;
    if (!api || api->open_system(&bus) < 0) return;
    bus_error e = { 0 };
    void *reply = nullptr;
    const char *profile = nullptr;
    if (api->get_property(bus, PPD, PPD_PATH, PPD, "ActiveProfile", &e, &reply, "s") >= 0
        && api->read(reply, "s", &profile) >= 0 && profile)
        snprintf(out, size, "%s", profile);
    if (reply) api->message_unref(reply);
    api->error_free(&e);
    api->bus_unref(bus);
}

/* ---- CPU layout --------------------------------------------------------- */

static int read_int(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int v = -1;
    if (fscanf(f, "%d", &v) != 1) v = -1;
    fclose(f);
    return v;
}

bool pb_cpu_layout_read(pb_cpu_layout *l) {
    *l = (pb_cpu_layout){ .audio_cpu = -1 };
    CPU_ZERO(&l->audio_core);
    CPU_ZERO(&l->others);
    int core_of[CPU_SETSIZE];
    int top_core = -1, cpus = 0, distinct[CPU_SETSIZE], ncores = 0;
    long online = sysconf(_SC_NPROCESSORS_ONLN);
    for (int c = 0; c < online && c < CPU_SETSIZE; c++) {
        char path[128];
        snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/topology/core_id", c);
        int core = read_int(path);
        snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/topology/physical_package_id", c);
        int package = read_int(path);
        core_of[c] = core < 0 ? c : core + 4096 * (package < 0 ? 0 : package);
        bool seen = false;
        for (int k = 0; k < ncores; k++) seen |= distinct[k] == core_of[c];
        if (!seen) distinct[ncores++] = core_of[c];
        if (core_of[c] > top_core) top_core = core_of[c];
        cpus++;
    }
    l->cpus = cpus;
    l->cores = ncores;
    if (ncores < 2) return false;
    for (int c = 0; c < cpus; c++) {
        if (core_of[c] == top_core) {
            CPU_SET(c, &l->audio_core);
            if (l->audio_cpu < 0) l->audio_cpu = c;
        } else {
            CPU_SET(c, &l->others);
        }
    }
    return l->audio_cpu >= 0;
}

int pb_cpu_worker(const pb_cpu_layout *l, cpu_set_t *rest) {
    *rest = l->others;
    int first = -1;
    for (int c = 0; c < CPU_SETSIZE && first < 0; c++)
        if (CPU_ISSET(c, &l->others)) first = c;
    if (first >= 0 && CPU_COUNT(&l->others) > 1) CPU_CLR(first, rest);
    return first;
}

unsigned pb_cpu_mhz(int cpu) {
    char path[128];
    snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", cpu);
    int khz = read_int(path);
    return khz > 0 ? (unsigned)(khz / 1000) : 0;
}

/* ---- memory locking ----------------------------------------------------- */

bool pb_lock(const void *at, size_t size, size_t *total) {
    if (!at || !size) return true;
    if (mlock(at, size)) return false;
    if (total) *total += size;
    return true;
}

void pb_unlock(const void *at, size_t size) {
    if (at && size) (void)munlock(at, size);
}
