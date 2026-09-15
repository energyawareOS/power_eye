// Feasibility probe: can a BPF program read a package-scoped perf event
// (RAPL energy, cstate residency) with bpf_perf_event_read_value(), and from
// which CPUs? Triggered by the user-space tool calling getppid() while pinned
// to each CPU in turn.
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

struct result {
    __s64 ret_a;      /* index = BPF_F_CURRENT_CPU */
    __u64 counter_a;
    __s64 ret_b;      /* index = the package's lead CPU slot */
    __u64 counter_b;
    __u32 cpu;        /* CPU the program really ran on */
    __u32 lead;
};

struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size, sizeof(__u32));
    __uint(value_size, sizeof(__u32));
} events SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __type(key, __u32);
    __type(value, __u32);
    __uint(max_entries, 1);
} cfg SEC(".maps");              /* [0] = tgid of the test program */

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __type(key, __u32);
    __type(value, __u32);
    __uint(max_entries, 1024);
} lead_of_cpu SEC(".maps");      /* lead CPU of the PMU for each CPU's package */

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __type(key, __u32);
    __type(value, struct result);
    __uint(max_entries, 1024);
} results SEC(".maps");

SEC("tracepoint/syscalls/sys_enter_getppid")
int probe(void *ctx)
{
    __u32 zero = 0, cpu = bpf_get_smp_processor_id();
    __u32 *tgid = bpf_map_lookup_elem(&cfg, &zero);
    __u32 *lead = bpf_map_lookup_elem(&lead_of_cpu, &cpu);
    struct bpf_perf_event_value va = {}, vb = {};
    struct result r = {};

    if (!tgid || (bpf_get_current_pid_tgid() >> 32) != *tgid)
        return 0;

    r.cpu = cpu;
    r.ret_a = bpf_perf_event_read_value(&events, BPF_F_CURRENT_CPU, &va, sizeof(va));
    r.counter_a = va.counter;
    if (lead && *lead < 1024) {
        r.lead = *lead;
        r.ret_b = bpf_perf_event_read_value(&events, *lead, &vb, sizeof(vb));
        r.counter_b = vb.counter;
    } else {
        r.ret_b = -1000; /* no lead CPU known for this package */
    }
    bpf_map_update_elem(&results, &cpu, &r, BPF_ANY);
    return 0;
}

char LICENSE[] SEC("license") = "GPL";
