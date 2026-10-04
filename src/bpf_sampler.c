#define _GNU_SOURCE

#include <stdint.h>
#include <stdio.h>

#include "bpf_sampler.h"

#ifndef HAVE_BPF

int
bpf_sampler_init(int want_ram, int want_c6)
{
    (void)want_ram;
    (void)want_c6;
    fprintf(stderr, "this power_eye was built without BPF support (cmake -DUSE_BPF=ON)\n");
    return -1;
}
void bpf_sampler_fini(void) {}
int bpf_sampler_has_c6(void) { return 0; }
int bpf_sampler_npkg(void) { return 0; }
void bpf_sampler_set_min_interval_ns(uint64_t ns) { (void)ns; }
int bpf_sampler_get(int idx, uint64_t stale_us, struct bpf_sample *out)
{
    (void)idx; (void)stale_us; (void)out;
    return -1;
}
unsigned long bpf_sampler_forced_total(void) { return 0; }

#else /* HAVE_BPF */

#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <errno.h>
#include <linux/perf_event.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include "bpf/sampler_shared.h"

extern const char pe_bpf_obj_start[], pe_bpf_obj_end[];

static struct bpf_object *obj;
static struct bpf_link *link_idle, *link_force;
static int n_pkg;
static int pkg_ids[MAXPKG], first_cpu[MAXPKG];
static int fd_e[MAXPKG], fd_r[MAXPKG], fd_c[MAXPKG];
static int have_c6;
static double tsc_hz;
static int fd_cfg, fd_state;
static double scale_pkg = 2.3283064365386963e-10, scale_ram = 2.3283064365386963e-10;
static struct sampler_cfg cfg;

static int
read_file(const char *path, char *buf, size_t len)
{
    FILE *f = fopen(path, "r");
    size_t n;
    if (!f)
        return -1;
    n = fread(buf, 1, len - 1, f);
    fclose(f);
    buf[n] = '\0';
    return 0;
}

static int
parse_cpulist(const char *s, int *out, int max)
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

static int
package_of(int cpu)
{
    char path[128], buf[32];
    snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/topology/physical_package_id", cpu);
    return read_file(path, buf, sizeof buf) < 0 ? -1 : atoi(buf);
}

/* One counting event on the package's lead CPU; -1 if the event is missing. */
static int
open_event(const char *pmu, const char *event, int cpu, double *scale)
{
    char path[256], buf[256];
    unsigned long long config;
    struct perf_event_attr attr;
    int type;

    snprintf(path, sizeof path, "/sys/bus/event_source/devices/%s/type", pmu);
    if (read_file(path, buf, sizeof buf) < 0)
        return -1;
    type = atoi(buf);
    snprintf(path, sizeof path, "/sys/bus/event_source/devices/%s/events/%s", pmu, event);
    if (read_file(path, buf, sizeof buf) < 0 || sscanf(buf, "event=%llx", &config) != 1)
        return -1;
    snprintf(path, sizeof path, "/sys/bus/event_source/devices/%s/events/%s.scale", pmu, event);
    if (scale && read_file(path, buf, sizeof buf) == 0 && atof(buf) > 0)
        *scale = atof(buf);
    memset(&attr, 0, sizeof attr);
    attr.type = type;
    attr.size = sizeof attr;
    attr.config = config;
    return syscall(SYS_perf_event_open, &attr, -1, cpu, -1, PERF_FLAG_FD_CLOEXEC);
}

static uint64_t
now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

/* Lead CPU of package pkg for the cstate_pkg PMU (falls back to dflt). */
static int
c6_cpu_for(int pkg, int dflt)
{
    char buf[512];
    int cl[MAXPKG], n;
    if (read_file("/sys/bus/event_source/devices/cstate_pkg/cpumask", buf, sizeof buf) < 0)
        return dflt;
    n = parse_cpulist(buf, cl, MAXPKG);
    for (int i = 0; i < n; i++)
        if (package_of(cl[i]) == pkg)
            return cl[i];
    return dflt;
}

static double
measure_tsc_hz(void)
{
    struct timespec a, b;
    uint64_t c0, c1;
    clock_gettime(CLOCK_MONOTONIC_RAW, &a);
    c0 = __builtin_ia32_rdtsc();
    usleep(200000);
    clock_gettime(CLOCK_MONOTONIC_RAW, &b);
    c1 = __builtin_ia32_rdtsc();
    return (double)(c1 - c0) / ((b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9);
}

static void
cleanup(void)
{
    if (link_force) bpf_link__destroy(link_force);
    if (link_idle) bpf_link__destroy(link_idle);
    if (obj) bpf_object__close(obj);
    link_force = link_idle = NULL;
    obj = NULL;
    for (int i = 0; i < n_pkg; i++) {
        if (fd_e[i] >= 0) close(fd_e[i]);
        if (fd_r[i] >= 0) close(fd_r[i]);
        if (fd_c[i] >= 0) close(fd_c[i]);
    }
    n_pkg = 0;
    have_c6 = 0;
}

int
bpf_sampler_init(int want_ram, int want_c6)
{
    char buf[512];
    int lead[MAXPKG], nlead, online[MAXCPU], non = 0;
    struct bpf_program *p_idle, *p_force;
    int c6_failed = 0;

    if (read_file("/sys/bus/event_source/devices/power/cpumask", buf, sizeof buf) < 0) {
        fprintf(stderr, "BPF sampling: no RAPL perf PMU (power/) on this system\n");
        return -1;
    }
    nlead = parse_cpulist(buf, lead, MAXPKG);
    if (nlead == 0) {
        fprintf(stderr, "BPF sampling: RAPL perf PMU has no CPU mask\n");
        return -1;
    }
    for (int i = 0; i < MAXPKG; i++)
        fd_e[i] = fd_r[i] = fd_c[i] = -1;
    if (read_file("/sys/devices/system/cpu/online", buf, sizeof buf) == 0)
        non = parse_cpulist(buf, online, MAXCPU);

    for (int i = 0; i < nlead; i++) {
        int pk = package_of(lead[i]);
        if (pk < 0 || pk >= MAXPKG) {
            fprintf(stderr, "BPF sampling: bad package id for cpu %d\n", lead[i]);
            cleanup();
            return -1;
        }
        fd_e[i] = open_event("power", "energy-pkg", lead[i], &scale_pkg);
        if (fd_e[i] < 0) {
            fprintf(stderr, "BPF sampling: cannot open power/energy-pkg: %s\n", strerror(errno));
            n_pkg = i + 1;
            cleanup();
            return -1;
        }
        if (want_ram) {
            fd_r[i] = open_event("power", "energy-ram", lead[i], &scale_ram);
            if (fd_r[i] < 0) {
                fprintf(stderr, "BPF sampling: cannot open power/energy-ram: %s\n", strerror(errno));
                n_pkg = i + 1;
                cleanup();
                return -1;
            }
        }
        pkg_ids[i] = pk;
        first_cpu[i] = lead[i];
        if (want_c6 && !c6_failed) {
            int cl = c6_cpu_for(pk, lead[i]);
            fd_c[i] = open_event("cstate_pkg", "c6-residency", cl, NULL);
            if (fd_c[i] < 0) {
                fprintf(stderr, "BPF sampling: no package C6 residency (cstate_pkg/c6-residency: %s); "
                        "continuing without it\n", strerror(errno));
                c6_failed = 1;
            }
        }
        for (int k = 0; k < non; k++)
            if (package_of(online[k]) == pk) { first_cpu[i] = online[k]; break; }
        n_pkg = i + 1;
    }

    have_c6 = want_c6 && !c6_failed;
    if (!have_c6)
        for (int i = 0; i < n_pkg; i++)
            if (fd_c[i] >= 0) { close(fd_c[i]); fd_c[i] = -1; }
    if (have_c6)
        tsc_hz = measure_tsc_hz();

    obj = bpf_object__open_mem(pe_bpf_obj_start, (size_t)(pe_bpf_obj_end - pe_bpf_obj_start), NULL);
    if (!obj) {
        fprintf(stderr, "BPF sampling: cannot open the embedded BPF object: %s\n", strerror(errno));
        cleanup();
        return -1;
    }
    if (bpf_object__load(obj)) {
        fprintf(stderr, "BPF sampling: loading the BPF program failed: %s\n", strerror(errno));
        cleanup();
        return -1;
    }
#define MAPFD(n) bpf_map__fd(bpf_object__find_map_by_name(obj, n))
    {
        int fd_ee = MAPFD("ev_energy"), fd_er = MAPFD("ev_ram"), fd_ec = MAPFD("ev_c6");
        int fd_pc = MAPFD("pkg_of_cpu");
        fd_cfg = MAPFD("cfg");
        fd_state = MAPFD("pkg_state");
        for (int i = 0; i < n_pkg; i++) {
            unsigned int slot = pkg_ids[i];
            if (bpf_map_update_elem(fd_ee, &slot, &fd_e[i], BPF_ANY) ||
                (want_ram && bpf_map_update_elem(fd_er, &slot, &fd_r[i], BPF_ANY)) ||
                (have_c6 && bpf_map_update_elem(fd_ec, &slot, &fd_c[i], BPF_ANY))) {
                fprintf(stderr, "BPF sampling: cannot register perf events: %s\n", strerror(errno));
                cleanup();
                return -1;
            }
        }
        for (int i = 0; i < non; i++) {
            unsigned int c = online[i], pk = package_of(c);
            bpf_map_update_elem(fd_pc, &c, &pk, BPF_ANY);
        }
    }
    cfg.min_interval_ns = 100000000ULL;
    cfg.use_ram = want_ram ? 1 : 0;
    cfg.use_c6 = have_c6 ? 1 : 0;
    cfg.loader_tgid = (unsigned int)getpid();
    {
        unsigned int zero = 0;
        bpf_map_update_elem(fd_cfg, &zero, &cfg, BPF_ANY);
    }

    p_idle = bpf_object__find_program_by_name(obj, "on_idle");
    p_force = bpf_object__find_program_by_name(obj, "on_force");
    link_idle = p_idle ? bpf_program__attach(p_idle) : NULL;
    link_force = p_force ? bpf_program__attach(p_force) : NULL;
    if (!link_idle || !link_force) {
        fprintf(stderr, "BPF sampling: attaching failed: %s\n", strerror(errno));
        cleanup();
        return -1;
    }
    return 0;
}

int
bpf_sampler_has_c6(void)
{
    return have_c6;
}

void
bpf_sampler_fini(void)
{
    cleanup();
}

int
bpf_sampler_npkg(void)
{
    return n_pkg;
}

void
bpf_sampler_set_min_interval_ns(uint64_t ns)
{
    unsigned int zero = 0;
    if (!obj || ns == cfg.min_interval_ns)
        return;
    cfg.min_interval_ns = ns;
    bpf_map_update_elem(fd_cfg, &zero, &cfg, BPF_ANY);
}

static int
read_state(unsigned int pkg, struct pkg_state *out)
{
    struct pkg_state a, b;
    for (int i = 0; i < 100; i++) {
        if (bpf_map_lookup_elem(fd_state, &pkg, &a))
            return -1;
        if (a.seq & 1)
            continue;
        if (bpf_map_lookup_elem(fd_state, &pkg, &b))
            return -1;
        if (a.seq == b.seq) {
            *out = a;
            return 0;
        }
    }
    return -1;
}

/* Run on a CPU of the package and let the BPF hook on getppid() sample there. */
static void
force_sample(int cpu)
{
    cpu_set_t set, old;
    if (sched_getaffinity(0, sizeof old, &old))
        return;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    if (sched_setaffinity(0, sizeof set, &set))
        return;
    syscall(SYS_getppid);
    sched_setaffinity(0, sizeof old, &old);
}

int
bpf_sampler_get(int idx, uint64_t stale_us, struct bpf_sample *out)
{
    struct pkg_state st;
    uint64_t now;

    if (idx < 0 || idx >= n_pkg || read_state(pkg_ids[idx], &st))
        return -1;
    now = now_ns();
    if (st.ts_ns == 0 || now < st.ts_ns || (now - st.ts_ns) / 1000 >= stale_us) {
        force_sample(first_cpu[idx]);
        if (read_state(pkg_ids[idx], &st) || st.ts_ns == 0) {
            fprintf(stderr, "BPF sampling: no sample for package %d (errors %llu, last %lld)\n",
                    pkg_ids[idx], (unsigned long long)st.errors, (long long)st.last_err);
            return -1;
        }
    }
    out->ts_ns = st.ts_ns;
    out->energy_pkg_j = st.energy * scale_pkg;
    out->energy_ram_j = st.ram * scale_ram;
    out->c6_s = have_c6 && tsc_hz > 0 ? st.c6 / tsc_hz : 0;
    return 0;
}

unsigned long
bpf_sampler_forced_total(void)
{
    unsigned long total = 0;
    for (int i = 0; i < n_pkg; i++) {
        struct pkg_state st;
        if (!read_state(pkg_ids[i], &st))
            total += st.forced;
    }
    return total;
}

#endif /* HAVE_BPF */
