// Loader and reporter for the idle-exit piggyback sampler. Needs root.
//
//   sudo ./idle_sampler [-i print_sec] [-m min_interval_ms] [-t total_sec]
//                       [-a max_age_sec] [-o object]
//
// Prints package power and package C6 residency derived only from samples the
// BPF program took on idle exits, plus its own cost. At the end it compares
// the BPF-derived mean power with a direct read() of the same perf events.
#define _GNU_SOURCE
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <errno.h>
#include <sched.h>
#include <linux/perf_event.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include "idle_sampler.h"

static volatile sig_atomic_t stop;
static void on_sig(int s) { (void)s; stop = 1; }

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

static unsigned long long now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

/* Open one counting event per package on its lead CPU; returns fd or -1. */
static int open_pkg_event(const char *pmu, const char *event, int lead, double *scale)
{
    char path[256], buf[256];
    unsigned long long config;
    snprintf(path, sizeof path, "/sys/bus/event_source/devices/%s/type", pmu);
    if (read_file(path, buf, sizeof buf) < 0)
        return -1;
    int type = atoi(buf);
    snprintf(path, sizeof path, "/sys/bus/event_source/devices/%s/events/%s", pmu, event);
    if (read_file(path, buf, sizeof buf) < 0 || sscanf(buf, "event=%llx", &config) != 1)
        return -1;
    if (scale) {
        snprintf(path, sizeof path, "/sys/bus/event_source/devices/%s/events/%s.scale", pmu, event);
        if (read_file(path, buf, sizeof buf) == 0)
            *scale = atof(buf);
    }
    struct perf_event_attr attr;
    memset(&attr, 0, sizeof attr);
    attr.type = type;
    attr.size = sizeof attr;
    attr.config = config;
    return syscall(SYS_perf_event_open, &attr, -1, lead, -1, PERF_FLAG_FD_CLOEXEC);
}

static int lead_cpus(const char *pmu, int *lead, int max)
{
    char path[256], buf[256];
    snprintf(path, sizeof path, "/sys/bus/event_source/devices/%s/cpumask", pmu);
    if (read_file(path, buf, sizeof buf) < 0)
        return 0;
    return parse_cpulist(buf, lead, max);
}

static double measure_tsc_mhz(void)
{
    unsigned long long t0 = now_ns(), c0 = __builtin_ia32_rdtsc();
    usleep(200000);
    unsigned long long t1 = now_ns(), c1 = __builtin_ia32_rdtsc();
    return (double)(c1 - c0) / ((double)(t1 - t0) / 1000.0);
}

static int get_state(int fd, unsigned int pkg, struct pkg_state *out)
{
    struct pkg_state a, b;
    for (int i = 0; i < 100; i++) {
        if (bpf_map_lookup_elem(fd, &pkg, &a))
            return -1;
        if (a.seq & 1)
            continue;
        if (bpf_map_lookup_elem(fd, &pkg, &b))
            return -1;
        if (a.seq == b.seq) {
            *out = a;
            return 0;
        }
    }
    return -1;
}

/* Upper bound (s) on the time between two samples so the 32-bit RAPL counter
 * cannot wrap more than once unseen: range / max power, with a safety factor
 * of 2. Returns 0 if the powercap limits cannot be read. */
static double wrap_bound_s(int pkg)
{
    char path[256], buf[64], name[64];
    for (int i = 0; i < 16; i++) {
        snprintf(path, sizeof path, "/sys/class/powercap/intel-rapl:%d/name", i);
        if (read_file(path, name, sizeof name) < 0)
            continue;
        if (strncmp(name, "package-", 8) || atoi(name + 8) != pkg)
            continue;
        snprintf(path, sizeof path, "/sys/class/powercap/intel-rapl:%d/max_energy_range_uj", i);
        if (read_file(path, buf, sizeof buf) < 0)
            return 0;
        double range_uj = atof(buf);
        double max_uw = 0;
        for (int c = 0; c < 2; c++) {
            snprintf(path, sizeof path, "/sys/class/powercap/intel-rapl:%d/constraint_%d_max_power_uw", i, c);
            if (read_file(path, buf, sizeof buf) == 0 && atof(buf) > max_uw)
                max_uw = atof(buf);
        }
        if (range_uj <= 0 || max_uw <= 0)
            return 0;
        return range_uj / max_uw / 2.0;
    }
    return 0;
}

/* Safety timer action: run on a CPU of the package and let the BPF hook on
 * getppid() take a sample there. */
static void force_sample(int cpu)
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

static unsigned long long direct_read(int fd)
{
    unsigned long long v = 0;
    if (read(fd, &v, sizeof v) != sizeof v)
        return 0;
    return v;
}

int main(int argc, char **argv)
{
    const char *obj_path = NULL;
    char obj_default[4096];
    double print_sec = 1.0;
    unsigned long long min_ms = 100, total_sec = 0;
    double max_age_s = 600;
    int opt;

    while ((opt = getopt(argc, argv, "i:m:t:a:o:")) != -1) {
        if (opt == 'a') { max_age_s = atof(optarg); continue; }
        if (opt == 'i') print_sec = atof(optarg);
        else if (opt == 'm') min_ms = strtoull(optarg, NULL, 10);
        else if (opt == 't') total_sec = strtoull(optarg, NULL, 10);
        else if (opt == 'o') obj_path = optarg;
        else return 2;
    }
    if (print_sec < 0.1) { fprintf(stderr, "-i must be >= 0.1\n"); return 2; }
    if (max_age_s < 1) { fprintf(stderr, "-a must be >= 1\n"); return 2; }
    if (!obj_path) {
        /* default: next to the executable, so it works from any directory */
        ssize_t n = readlink("/proc/self/exe", obj_default, sizeof obj_default - 32);
        char *slash = n > 0 ? memrchr(obj_default, '/', n) : NULL;
        if (slash) {
            strcpy(slash + 1, "idle_sampler.bpf.o");
            obj_path = obj_default;
        } else {
            obj_path = "idle_sampler.bpf.o";
        }
    }

    /* packages and their lead CPUs */
    int lead[MAXPKG], nlead = lead_cpus("power", lead, MAXPKG);
    if (nlead == 0) { fprintf(stderr, "no power PMU (RAPL perf events) found\n"); return 1; }
    int clead[MAXPKG], nclead = lead_cpus("cstate_pkg", clead, MAXPKG);
    int use_c6 = nclead > 0;

    int ev_e[MAXPKG], ev_c[MAXPKG], pkg_ids[MAXPKG];
    double scale = 2.3283064365386963e-10;
    for (int i = 0; i < nlead; i++) {
        int p = package_of(lead[i]);
        if (p < 0 || p >= MAXPKG) { fprintf(stderr, "bad package id for cpu %d\n", lead[i]); return 1; }
        pkg_ids[i] = p;
        ev_e[i] = open_pkg_event("power", "energy-pkg", lead[i], &scale);
        if (ev_e[i] < 0) { perror("perf_event_open energy-pkg"); return 1; }
        ev_c[i] = -1;
        if (use_c6) {
            int cl = lead[i];
            for (int j = 0; j < nclead; j++)
                if (package_of(clead[j]) == p) cl = clead[j];
            ev_c[i] = open_pkg_event("cstate_pkg", "c6-residency", cl, NULL);
            if (ev_c[i] < 0) { perror("perf_event_open c6-residency (continuing without)"); use_c6 = 0; }
        }
    }
    double tsc_mhz = use_c6 ? measure_tsc_mhz() : 0;

    struct bpf_object *obj = bpf_object__open_file(obj_path, NULL);
    if (!obj) { fprintf(stderr, "open %s failed: %s\n", obj_path, strerror(errno)); return 1; }
    if (bpf_object__load(obj)) { fprintf(stderr, "BPF load failed: %s\n", strerror(errno)); return 1; }
#define MAPFD(n) bpf_map__fd(bpf_object__find_map_by_name(obj, n))
    int fd_ee = MAPFD("ev_energy"), fd_ec = MAPFD("ev_c6"), fd_cfg = MAPFD("cfg");
    int fd_pc = MAPFD("pkg_of_cpu"), fd_st = MAPFD("pkg_state"), fd_ex = MAPFD("idle_exits");

    for (int i = 0; i < nlead; i++) {
        unsigned int slot = pkg_ids[i];
        if (bpf_map_update_elem(fd_ee, &slot, &ev_e[i], BPF_ANY)) { perror("ev_energy"); return 1; }
        if (use_c6 && bpf_map_update_elem(fd_ec, &slot, &ev_c[i], BPF_ANY)) { perror("ev_c6"); return 1; }
    }
    char buf[512];
    int online[MAXCPU], non = 0;
    if (read_file("/sys/devices/system/cpu/online", buf, sizeof buf) == 0)
        non = parse_cpulist(buf, online, MAXCPU);
    for (int i = 0; i < non; i++) {
        unsigned int c = online[i], p = package_of(c);
        bpf_map_update_elem(fd_pc, &c, &p, BPF_ANY);
    }
    struct sampler_cfg cfg = { .min_interval_ns = min_ms * 1000000ULL, .use_c6 = use_c6,
                               .loader_tgid = (unsigned int)getpid() };
    unsigned int zero = 0;
    bpf_map_update_elem(fd_cfg, &zero, &cfg, BPF_ANY);

    int stats_fd = bpf_enable_stats(BPF_STATS_RUN_TIME);
    struct bpf_program *prog = bpf_object__find_program_by_name(obj, "on_idle");
    struct bpf_link *link = prog ? bpf_program__attach(prog) : NULL;
    if (!link) { fprintf(stderr, "attach failed: %s\n", strerror(errno)); return 1; }
    int prog_fd = bpf_program__fd(prog);
    struct bpf_program *fprog = bpf_object__find_program_by_name(obj, "on_force");
    struct bpf_link *flink = fprog ? bpf_program__attach(fprog) : NULL;
    if (!flink) { fprintf(stderr, "attach (safety hook) failed: %s\n", strerror(errno)); return 1; }

    /* effective maximum sample age: user cap, tightened by the RAPL wrap bound */
    double eff_age = max_age_s;
    int first_cpu[MAXPKG];
    for (int i = 0; i < nlead; i++) {
        first_cpu[i] = lead[i];
        for (int k = 0; k < non; k++)
            if (package_of(online[k]) == (int)pkg_ids[i]) { first_cpu[i] = online[k]; break; }
        double wb = wrap_bound_s(pkg_ids[i]);
        if (wb > 0 && wb < eff_age) eff_age = wb;
    }

    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);
    printf("%d package(s), min interval %llu ms, print every %.1f s, energy unit %.4g J%s\n",
           nlead, min_ms, print_sec, scale, use_c6 ? "" : " (no package C6 counter)");
    printf("safety timer: forced sample when a package's last sample is older than %.0f s "
           "(cap %.0f s%s)\n", eff_age / 2, max_age_s, eff_age < max_age_s ? ", tightened by RAPL wrap bound" : "");

    unsigned long long d_e0[MAXPKG], t_d0 = now_ns();
    for (int i = 0; i < nlead; i++) d_e0[i] = direct_read(ev_e[i]);

    struct pkg_state first[MAXPKG] = {{0}}, prev[MAXPKG] = {{0}}, cur[MAXPKG];
    unsigned long long prev_exits = 0, prev_runs = 0, prev_rt = 0, t_prev = now_ns(), t_start = t_prev;
    int have_first[MAXPKG] = {0};

    double chunk = print_sec < eff_age / 2 ? print_sec : eff_age / 2;
    while (!stop) {
        usleep((useconds_t)(chunk * 1e6));
        unsigned long long t = now_ns();
        /* safety timer: refresh packages whose samples are getting old */
        for (int i = 0; i < nlead; i++) {
            struct pkg_state sst;
            double age = 1e18;
            if (!get_state(fd_st, pkg_ids[i], &sst))
                age = sst.ts_ns ? (t - sst.ts_ns) / 1e9 : (t - t_start) / 1e9;
            if (age >= eff_age / 2)
                force_sample(first_cpu[i]);
        }
        if ((t - t_prev) / 1e9 < print_sec - 0.05) {
            if (total_sec && (t - t_start) / 1000000000ULL >= total_sec)
                break;
            continue;
        }
        double dt = (t - t_prev) / 1e9;
        printf("t=%6.1fs", (t - t_start) / 1e9);

        for (int i = 0; i < nlead; i++) {
            unsigned int p = pkg_ids[i];
            if (get_state(fd_st, p, &cur[i]) || cur[i].ts_ns == 0) {
                printf(" | pkg%u: no sample yet", p);
                continue;
            }
            if (!have_first[i]) { first[i] = cur[i]; have_first[i] = 1; }
            double age_ms = (t - cur[i].ts_ns) / 1e6;
            if (prev[i].ts_ns && cur[i].ts_ns > prev[i].ts_ns) {
                double span = (cur[i].ts_ns - prev[i].ts_ns) / 1e9;
                double w = (cur[i].energy - prev[i].energy) * scale / span;
                printf(" | pkg%u: %6.2f W", p, w);
                if (use_c6)
                    printf(" c6 %5.1f%%", 100.0 * (cur[i].c6 - prev[i].c6) / (tsc_mhz * 1e6 * span));
                printf(" reads %4.1f/s age %4.0f ms", (cur[i].reads - prev[i].reads) / dt, age_ms);
            } else {
                printf(" | pkg%u: %s age %4.0f ms", p, prev[i].ts_ns ? "(no new sample)" : "(baseline)", age_ms);
            }
            if (cur[i].forced)
                printf(" forced %llu", (unsigned long long)cur[i].forced);
            if (cur[i].errors)
                printf(" ERR %llu last %lld", cur[i].errors, (long long)cur[i].last_err);
            prev[i] = cur[i];
        }

        unsigned long long exits = 0;
        for (int i = 0; i < non; i++) {
            unsigned long long v = 0;
            unsigned int c = online[i];
            if (!bpf_map_lookup_elem(fd_ex, &c, &v)) exits += v;
        }
        struct bpf_prog_info info = {0};
        __u32 len = sizeof info;
        double avg_ns = 0, runs_s = 0;
        if (!bpf_prog_get_info_by_fd(prog_fd, &info, &len) && stats_fd >= 0) {
            runs_s = (info.run_cnt - prev_runs) / dt;
            if (info.run_cnt > prev_runs)
                avg_ns = (double)(info.run_time_ns - prev_rt) / (info.run_cnt - prev_runs);
            prev_runs = info.run_cnt;
            prev_rt = info.run_time_ns;
        }
        printf(" | idle exits %6.0f/s | prog %6.0f runs/s avg %4.0f ns\n", (exits - prev_exits) / dt, runs_s, avg_ns);
        prev_exits = exits;
        t_prev = t;
        if (total_sec && (t - t_start) / 1000000000ULL >= total_sec)
            break;
    }

    unsigned long long t_d1 = now_ns();
    printf("\nBPF-derived mean power vs direct read() of the same perf event:\n");
    for (int i = 0; i < nlead; i++) {
        unsigned long long d1 = direct_read(ev_e[i]);
        double direct_w = (d1 - d_e0[i]) * scale / ((t_d1 - t_d0) / 1e9);
        if (!have_first[i] || prev[i].ts_ns <= first[i].ts_ns) {
            printf("  pkg%d: not enough BPF samples (direct %.2f W)\n", pkg_ids[i], direct_w);
            continue;
        }
        double bpf_w = (prev[i].energy - first[i].energy) * scale / ((prev[i].ts_ns - first[i].ts_ns) / 1e9);
        printf("  pkg%d: bpf %.2f W over %.1f s (%llu samples), direct %.2f W over %.1f s, diff %+.2f%%\n",
               pkg_ids[i], bpf_w, (prev[i].ts_ns - first[i].ts_ns) / 1e9, prev[i].reads - first[i].reads,
               direct_w, (t_d1 - t_d0) / 1e9, 100.0 * (bpf_w - direct_w) / direct_w);
    }
    bpf_link__destroy(flink);
    bpf_link__destroy(link);
    if (stats_fd >= 0) close(stats_fd);
    bpf_object__close(obj);
    return 0;
}
