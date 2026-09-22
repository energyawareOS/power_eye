/* Shared between the BPF program and the user-space loader. */
#ifndef IDLE_SAMPLER_H
#define IDLE_SAMPLER_H
#include <linux/types.h>

#define MAXPKG 16
#define MAXCPU 1024

/* Latest package counter sample. Written by whichever CPU of the package
 * leaves idle first after min_interval; seq is odd while being written. */
struct pkg_state {
    __u64 seq;
    __u64 ts_ns;          /* CLOCK_MONOTONIC when the sample was taken */
    __u64 energy;         /* perf counter value, power/energy-pkg */
    __u64 c6;             /* perf counter value, cstate_pkg/c6-residency */
    __u64 last_claim_ns;  /* rate limiter */
    __u64 reads;          /* samples taken */
    __u64 errors;         /* failed bpf_perf_event_read_value() calls */
    __u64 forced;         /* samples taken on request of the safety timer */
    __s64 last_err;
};

struct sampler_cfg {
    __u64 min_interval_ns;
    __u32 use_c6;
    __u32 loader_tgid;    /* only this process may force a sample */
};
#endif
