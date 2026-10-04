#ifndef POWER_EYE_BPF_SAMPLER_H
#define POWER_EYE_BPF_SAMPLER_H

#include <stdint.h>

struct bpf_sample {
    uint64_t ts_ns;         /* CLOCK_MONOTONIC time of the sample */
    double energy_pkg_j;    /* cumulative package energy since the event was opened */
    double energy_ram_j;    /* same for DRAM (0 if not requested) */
    double c6_s;            /* cumulative time in package C6 (s), 0 if not requested */
};

/* 0 on success; on failure a message was printed and the caller should fall
 * back to reading sysfs. */
int bpf_sampler_init(int want_ram, int want_c6);
/* 1 if package C6 residency is sampled (it is dropped with a message if the
 * cstate_pkg PMU or its c6-residency event is missing). */
int bpf_sampler_has_c6(void);
void bpf_sampler_fini(void);
int bpf_sampler_npkg(void);
/* Rate limit of idle-exit samples. */
void bpf_sampler_set_min_interval_ns(uint64_t ns);
/* Latest sample of package idx. A sample older than stale_us (0: always)
 * is first refreshed by a forced sample on a CPU of that package. */
int bpf_sampler_get(int idx, uint64_t stale_us, struct bpf_sample *out);
/* Samples forced so far (each wakes one CPU of a package). */
unsigned long bpf_sampler_forced_total(void);

#endif
