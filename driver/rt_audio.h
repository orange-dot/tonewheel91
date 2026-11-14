#ifndef RT_AUDIO_H
#define RT_AUDIO_H

/* Engage real-time conditions for the calling (audio) thread: lock the process memory so a
 * page fault never stalls the render, and raise to SCHED_FIFO at `prio`. Both need privilege
 * (CAP_SYS_NICE, or a non-zero RLIMIT_RTPRIO); on failure this warns and returns false, leaving
 * the thread at normal priority so the program still runs.
 *
 * Measured on the i7-4600U, patchlab at period 128x3 under four CPU hogs, a live drum take:
 * normal priority 5712 xruns, SCHED_FIFO 80 zero xruns. mlockall is belt-and-suspenders for
 * page-fault stalls that `chrt` alone does not cover. */

#include <errno.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

static inline bool rt_audio_engage(int prio) {
    bool ok = true;
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        fprintf(stderr, "rt: mlockall: %s (page faults may still stall the render)\n",
                strerror(errno));
        ok = false;
    }
    struct sched_param sp = { .sched_priority = prio };
    if (sched_setscheduler(0, SCHED_FIFO, &sp) != 0) {
        fprintf(stderr, "rt: SCHED_FIFO prio %d: %s "
                "(needs privilege: sudo, setcap cap_sys_nice, or RLIMIT_RTPRIO)\n",
                prio, strerror(errno));
        ok = false;
    }
    return ok;
}

#endif
