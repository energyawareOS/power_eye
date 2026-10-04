// Idle-exit piggyback sampler: read the package RAPL energy counters on the
// idle exit of whichever CPU wakes first, at most once per min_interval per
// package. No timer and no IPI: when nothing leaves idle, nothing runs.
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "sampler_shared.h"

struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size, sizeof(__u32));
    __uint(value_size, sizeof(__u32));
    __uint(max_entries, MAXPKG);
} ev_energy SEC(".maps");            /* slot = package id */

struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size, sizeof(__u32));
    __uint(value_size, sizeof(__u32));
    __uint(max_entries, MAXPKG);
} ev_ram SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size, sizeof(__u32));
    __uint(value_size, sizeof(__u32));
    __uint(max_entries, MAXPKG);
} ev_c6 SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __type(key, __u32);
    __type(value, struct sampler_cfg);
    __uint(max_entries, 1);
} cfg SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __type(key, __u32);
    __type(value, __u32);
    __uint(max_entries, MAXCPU);
} pkg_of_cpu SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __type(key, __u32);
    __type(value, struct pkg_state);
    __uint(max_entries, MAXPKG);
} pkg_state SEC(".maps");

/* Take one sample for package pkg. Idle exits are rate limited by
 * min_interval; a forced sample ignores the interval. Either way only one CPU
 * wins the claim, so there is a single writer at a time. */
static __always_inline void take_sample(__u32 pkg, struct sampler_cfg *c, int force)
{
    struct bpf_perf_event_value ve = {}, vr = {}, vc = {};
    struct pkg_state *st;
    __u64 now, last;
    long ret;

    if (pkg >= MAXPKG)
        return;
    st = bpf_map_lookup_elem(&pkg_state, &pkg);
    if (!st)
        return;
    now = bpf_ktime_get_ns();
    last = st->last_claim_ns;
    if (!force && now - last < c->min_interval_ns)
        return;
    if (__sync_val_compare_and_swap(&st->last_claim_ns, last, now) != last)
        return;

    ret = bpf_perf_event_read_value(&ev_energy, pkg, &ve, sizeof(ve));
    if (!ret && c->use_ram)
        ret = bpf_perf_event_read_value(&ev_ram, pkg, &vr, sizeof(vr));
    if (!ret && c->use_c6)
        ret = bpf_perf_event_read_value(&ev_c6, pkg, &vc, sizeof(vc));
    if (ret) {
        st->errors++;
        st->last_err = ret;
        return;
    }
    __sync_fetch_and_add(&st->seq, 1);       /* odd: writing */
    st->ts_ns = now;
    st->energy = ve.counter;
    st->ram = vr.counter;
    st->c6 = vc.counter;
    st->reads++;
    if (force)
        st->forced++;
    __sync_fetch_and_add(&st->seq, 1);       /* even: stable */
}

SEC("raw_tp/cpu_idle")
int on_idle(struct bpf_raw_tracepoint_args *ctx)
{
    __u32 zero = 0, cpu, *pk;
    struct sampler_cfg *c;

    if ((__u32)ctx->args[0] != (__u32)-1)    /* PWR_EVENT_EXIT: leaving idle */
        return 0;

    cpu = bpf_get_smp_processor_id();
    pk = bpf_map_lookup_elem(&pkg_of_cpu, &cpu);
    c = bpf_map_lookup_elem(&cfg, &zero);
    if (pk && c)
        take_sample(*pk, c, 0);
    return 0;
}

/* The loader pins itself to a CPU of a package whose sample is stale and
 * calls getppid(); the sample is then taken on that CPU. */
SEC("tracepoint/syscalls/sys_enter_getppid")
int on_force(void *ctx)
{
    __u32 zero = 0, cpu = bpf_get_smp_processor_id(), *pk;
    struct sampler_cfg *c = bpf_map_lookup_elem(&cfg, &zero);

    if (!c || (bpf_get_current_pid_tgid() >> 32) != c->loader_tgid)
        return 0;
    pk = bpf_map_lookup_elem(&pkg_of_cpu, &cpu);
    if (pk)
        take_sample(*pk, c, 1);
    return 0;
}

char LICENSE[] SEC("license") = "GPL";
