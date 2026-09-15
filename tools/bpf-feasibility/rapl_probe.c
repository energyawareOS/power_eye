// User side of the BPF perf-event-read feasibility probe. Needs root.
// usage: sudo ./rapl_probe [-P pmu] [-e event] [-o object]
//   default: PMU "power", event "energy-pkg"; try -P cstate_pkg -e c6-residency
#define _GNU_SOURCE
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <errno.h>
#include <linux/perf_event.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#define MAXCPU 1024

struct result {
    long long ret_a;
    unsigned long long counter_a;
    long long ret_b;
    unsigned long long counter_b;
    unsigned int cpu, lead;
};

static int read_file(const char *path, char *buf, size_t len)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    size_t n = fread(buf, 1, len - 1, f);
    fclose(f);
    buf[n] = '\0';
    return 0;
}

static int parse_cpulist(const char *s, int *out, int max)
{
    int n = 0;
    while (*s && *s != '\n') {
        int a, b;
        char *end;
        a = b = (int)strtol(s, &end, 10);
        if (end == s)
            break;
        s = end;
        if (*s == '-')
            b = (int)strtol(s + 1, (char **)&s, 10);
        for (; a <= b && n < max; a++)
            out[n++] = a;
        if (*s == ',')
            s++;
    }
    return n;
}

static int package_of(int cpu)
{
    char path[128], buf[32];
    snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/topology/physical_package_id", cpu);
    return read_file(path, buf, sizeof buf) < 0 ? -1 : atoi(buf);
}

int main(int argc, char **argv)
{
    const char *pmu = "power", *event = "energy-pkg", *obj_path = "rapl_probe.bpf.o";
    int opt;

    while ((opt = getopt(argc, argv, "P:e:o:")) != -1) {
        if (opt == 'P') pmu = optarg;
        else if (opt == 'e') event = optarg;
        else if (opt == 'o') obj_path = optarg;
        else return 2;
    }

    char path[256], buf[256];
    unsigned long long config = 0;
    snprintf(path, sizeof path, "/sys/bus/event_source/devices/%s/type", pmu);
    if (read_file(path, buf, sizeof buf) < 0) { perror(path); return 1; }
    int type = atoi(buf);
    snprintf(path, sizeof path, "/sys/bus/event_source/devices/%s/events/%s", pmu, event);
    if (read_file(path, buf, sizeof buf) < 0 || sscanf(buf, "event=%llx", &config) != 1) {
        fprintf(stderr, "cannot read event config from %s\n", path);
        return 1;
    }

    int online[MAXCPU], lead[MAXCPU], non = 0, nlead = 0;
    if (read_file("/sys/devices/system/cpu/online", buf, sizeof buf) < 0) { perror("online"); return 1; }
    non = parse_cpulist(buf, online, MAXCPU);
    snprintf(path, sizeof path, "/sys/bus/event_source/devices/%s/cpumask", pmu);
    if (read_file(path, buf, sizeof buf) == 0)
        nlead = parse_cpulist(buf, lead, MAXCPU);
    if (nlead == 0) { fprintf(stderr, "PMU %s has no cpumask; only package-scoped PMUs are supported\n", pmu); return 1; }

    printf("PMU %s (type %d) event %s (config 0x%llx), lead CPUs:", pmu, type, event, config);
    for (int i = 0; i < nlead; i++) printf(" %d", lead[i]);
    if (read_file("/proc/sys/kernel/osrelease", buf, sizeof buf) == 0)
        printf("\nkernel: %s", buf);

    struct bpf_object *obj = bpf_object__open_file(obj_path, NULL);
    if (!obj) { fprintf(stderr, "open %s failed: %s\n", obj_path, strerror(errno)); return 1; }
    struct bpf_map *m_events = bpf_object__find_map_by_name(obj, "events");
    if (!m_events) { fprintf(stderr, "events map missing in %s\n", obj_path); return 1; }
    bpf_map__set_max_entries(m_events, MAXCPU);
    if (bpf_object__load(obj)) { fprintf(stderr, "BPF load failed: %s\n", strerror(errno)); return 1; }

    int fd_events = bpf_map__fd(m_events);
    int fd_cfg = bpf_map__fd(bpf_object__find_map_by_name(obj, "cfg"));
    int fd_lead = bpf_map__fd(bpf_object__find_map_by_name(obj, "lead_of_cpu"));
    int fd_res = bpf_map__fd(bpf_object__find_map_by_name(obj, "results"));
    unsigned int zero = 0, tgid = (unsigned int)getpid();
    bpf_map_update_elem(fd_cfg, &zero, &tgid, BPF_ANY);

    int open_err[MAXCPU] = {0};
    for (int i = 0; i < non; i++) {
        int c = online[i];
        struct perf_event_attr attr;
        memset(&attr, 0, sizeof attr);
        attr.type = type;
        attr.size = sizeof attr;
        attr.config = config;
        int fd = syscall(SYS_perf_event_open, &attr, -1, c, -1, PERF_FLAG_FD_CLOEXEC);
        if (fd < 0) { open_err[c] = errno; continue; }
        if (bpf_map_update_elem(fd_events, &c, &fd, BPF_ANY))
            open_err[c] = -errno;
        int pk = package_of(c), ld = -1;
        for (int j = 0; j < nlead; j++)
            if (package_of(lead[j]) == pk) ld = lead[j];
        if (ld >= 0) { unsigned int l = ld; bpf_map_update_elem(fd_lead, &c, &l, BPF_ANY); }
    }
    /* CPUs whose perf_event_open failed still need a lead entry for variant B */
    for (int i = 0; i < non; i++) {
        int c = online[i], pk = package_of(c);
        for (int j = 0; j < nlead; j++)
            if (package_of(lead[j]) == pk) { unsigned int l = lead[j]; bpf_map_update_elem(fd_lead, &c, &l, BPF_ANY); }
    }

    struct bpf_program *prog = bpf_object__find_program_by_name(obj, "probe");
    struct bpf_link *link = prog ? bpf_program__attach_tracepoint(prog, "syscalls", "sys_enter_getppid") : NULL;
    if (!link) { fprintf(stderr, "attach failed: %s\n", strerror(errno)); return 1; }

    printf("\n%-4s %-4s %-5s %-14s | %-34s | %-34s | %s\n", "cpu", "pkg", "lead", "perf_event_open",
           "A: read slot=CURRENT_CPU", "B: read slot=lead CPU", "ran-on");
    int okA = 0, okB = 0;
    for (int i = 0; i < non; i++) {
        int c = online[i];
        cpu_set_t set;
        CPU_ZERO(&set); CPU_SET(c, &set);
        if (sched_setaffinity(0, sizeof set, &set)) { perror("sched_setaffinity"); continue; }
        struct result r1 = {0}, r2 = {0};
        syscall(SYS_getppid);
        bpf_map_lookup_elem(fd_res, &c, &r1);
        usleep(50000);
        syscall(SYS_getppid);
        bpf_map_lookup_elem(fd_res, &c, &r2);

        char oa[40], ob[40], op[24];
        snprintf(op, sizeof op, open_err[c] ? "FAIL(%s)" : "ok", strerror(open_err[c] > 0 ? open_err[c] : 0));
        if (r2.ret_a == 0) snprintf(oa, sizeof oa, "OK  counter=%llu (+%llu)", r2.counter_a, r2.counter_a - r1.counter_a);
        else snprintf(oa, sizeof oa, "ret=%lld (%s)", r2.ret_a, strerror(-r2.ret_a));
        if (r2.ret_b == 0) snprintf(ob, sizeof ob, "OK  counter=%llu (+%llu)", r2.counter_b, r2.counter_b - r1.counter_b);
        else snprintf(ob, sizeof ob, "ret=%lld (%s)", r2.ret_b, r2.ret_b == -1000 ? "no lead" : strerror(-r2.ret_b));
        printf("%-4d %-4d %-5u %-14s | %-34s | %-34s | cpu%u%s\n", c, package_of(c), r2.lead, op, oa, ob,
               r2.cpu, r2.cpu == (unsigned)c ? "" : " MISMATCH");
        okA += r2.ret_a == 0 && r2.counter_a > r1.counter_a;
        okB += r2.ret_b == 0 && r2.counter_b > r1.counter_b;
    }
    printf("\nSummary over %d online CPUs: A works (and counts) on %d, B works (and counts) on %d\n", non, okA, okB);
    printf("Expected if the read is bound to the event's CPU: both succeed only on the lead CPUs.\n");
    bpf_link__destroy(link);
    bpf_object__close(obj);
    return 0;
}
