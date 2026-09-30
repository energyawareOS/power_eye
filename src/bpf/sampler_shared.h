/* Shared between the BPF program and the user-space side of the sampler. */
#ifndef SAMPLER_SHARED_H
#define SAMPLER_SHARED_H
#include <linux/types.h>

#define MAXPKG 16
#define MAXCPU 1024

/* Latest package counter sample. Written by whichever CPU of the package
 * leaves idle first after min_interval (or by a forced sample); seq is odd
 * while it is being written. */
struct pkg_state {
    __u64 seq;
    __u64 ts_ns;          /* CLOCK_MONOTONIC when the sample was taken */
    __u64 energy;         /* perf counter value, power/energy-pkg */
    __u64 ram;            /* perf counter value, power/energy-ram (if use_ram) */
    __u64 last_claim_ns;  /* rate limiter */
    __u64 reads;          /* samples taken */
    __u64 errors;         /* failed bpf_perf_event_read_value() calls */
    __u64 forced;         /* samples taken on request of the loader */
    __s64 last_err;
};

struct sampler_cfg {
    __u64 min_interval_ns;
    __u32 use_ram;
    __u32 loader_tgid;    /* only this process may force a sample */
};
#endif
