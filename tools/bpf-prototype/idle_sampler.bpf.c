// Piggyback sampler: read package RAPL / C6 counters on the idle-exit of any
// CPU, rate limited per package. No timer, no IPI: if nothing leaves idle,
// nothing runs.
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "idle_sampler.h"

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

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __type(key, __u32);
    __type(value, __u64);
    __uint(max_entries, MAXCPU);
} idle_exits SEC(".maps");           /* per CPU: only its own CPU writes */

SEC("raw_tp/cpu_idle")
int on_idle(struct bpf_raw_tracepoint_args *ctx)
{
    __u32 zero = 0, cpu, *pk;
    struct sampler_cfg *c;
    struct pkg_state *st;
    struct bpf_perf_event_value ve = {}, vc = {};
    __u64 now, last, *ex;
    long ret;

    if ((__u32)ctx->args[0] != (__u32)-1)    /* PWR_EVENT_EXIT: leaving idle */
        return 0;

    cpu = bpf_get_smp_processor_id();
    ex = bpf_map_lookup_elem(&idle_exits, &cpu);
    if (ex)
        (*ex)++;

    pk = bpf_map_lookup_elem(&pkg_of_cpu, &cpu);
    c = bpf_map_lookup_elem(&cfg, &zero);
    if (!pk || !c || *pk >= MAXPKG)
        return 0;
    st = bpf_map_lookup_elem(&pkg_state, pk);
    if (!st)
        return 0;

    now = bpf_ktime_get_ns();
    last = st->last_claim_ns;
    if (now - last < c->min_interval_ns)
        return 0;
    /* only one CPU of the package wins this interval */
    if (__sync_val_compare_and_swap(&st->last_claim_ns, last, now) != last)
        return 0;

    ret = bpf_perf_event_read_value(&ev_energy, *pk, &ve, sizeof(ve));
    if (ret) {
        st->errors++;
        st->last_err = ret;
        return 0;
    }
    if (c->use_c6) {
        ret = bpf_perf_event_read_value(&ev_c6, *pk, &vc, sizeof(vc));
        if (ret) {
            st->errors++;
            st->last_err = ret;
            return 0;
        }
    }
    __sync_fetch_and_add(&st->seq, 1);       /* odd: writing */
    st->ts_ns = now;
    st->energy = ve.counter;
    st->c6 = vc.counter;
    st->reads++;
    __sync_fetch_and_add(&st->seq, 1);       /* even: stable */
    return 0;
}

char LICENSE[] SEC("license") = "GPL";
