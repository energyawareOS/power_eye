#define _GNU_SOURCE

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <errno.h>
#include <stdint.h>

#include "bpf_sampler.h"
#include "config.h"

#define MAX_PKGS 64

struct rapl_dom {
    int fd;
    double max_range;   /* max_energy_range_uj in J, 0 if unknown */
    double last;        /* last raw reading in J */
    double max_power;   /* highest constraint max_power in W, 0 if unknown */
};

static struct rapl_dom cpu_doms[MAX_PKGS], dram_doms[MAX_PKGS];
static int n_cpu, n_dram;

extern int has_dram;

int use_bpf;                    /* -B: take samples from the BPF sampler instead of sysfs */
static unsigned long sysfs_reads;
static struct bpf_sample bprev[MAX_PKGS];

double energy_cpu, power_cpu, energy_dram, power_dram;

/* The last tick as the coma detector needs it: its real length and the time
 * spent in deep package C-states, averaged over packages (valid with -B and a
 * cstate_pkg PMU only). */
double tick_secs, tick_deep_s;
int tick_deep_valid;

static int
open_quiet(const char *path)
{
    return open(path, O_RDONLY);
}

static int
open_or_die(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "failed to open %s: %s\n", path, strerror(errno));
        exit(EXIT_FAILURE);
    }
    return fd;
}

static double
read_uj_file(const char *path)
{
    char buf[64];
    int fd = open_quiet(path);
    if (fd < 0)
        return 0;
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0)
        return 0;
    buf[n] = '\0';
    return strtod(buf, NULL) / 1e6;
}

static void
add_dom(struct rapl_dom *doms, int *n, const char *dir)
{
    char path[256];

    if (*n >= MAX_PKGS)
        return;
    snprintf(path, sizeof path, "%s/energy_uj", dir);
    doms[*n].fd = open_or_die(path);
    snprintf(path, sizeof path, "%s/max_energy_range_uj", dir);
    doms[*n].max_range = read_uj_file(path);
    doms[*n].max_power = 0;
    for (int c = 0; c < 2; c++) {
        snprintf(path, sizeof path, "%s/constraint_%d_max_power_uw", dir, c);
        double w = read_uj_file(path);   /* uW -> W by the same /1e6 */
        if (w > doms[*n].max_power)
            doms[*n].max_power = w;
    }
    (*n)++;
}

static int
read_name(const char *dir, char *name, size_t len)
{
    char path[256];
    snprintf(path, sizeof path, "%s/name", dir);
    int fd = open_quiet(path);
    if (fd < 0)
        return -1;
    ssize_t n = read(fd, name, len - 1);
    close(fd);
    if (n <= 0)
        return -1;
    name[n] = '\0';
    return 0;
}

int
init_powercap(void)
{
    /* Every CPU package: intel-rapl:<N>; its DRAM subdomain: intel-rapl:<N>:<M> */
    for (int pkg = 0; pkg < MAX_PKGS; pkg++) {
        char dir[128], name[64];

        snprintf(dir, sizeof dir, "/sys/class/powercap/intel-rapl:%d", pkg);
        if (read_name(dir, name, sizeof name) < 0)
            break;
        if (strncmp(name, "package", 7) != 0)
            continue;
        add_dom(cpu_doms, &n_cpu, dir);

        if (!has_dram)
            continue;
        for (int sub = 0; sub < 16; sub++) {
            char sdir[160];
            snprintf(sdir, sizeof sdir, "%s:%d", dir, sub);
            if (read_name(sdir, name, sizeof name) < 0)
                break;
            if (strncmp(name, "dram", 4) == 0) {
                add_dom(dram_doms, &n_dram, sdir);
                break;
            }
        }
    }

    if (n_cpu == 0) {
        fprintf(stderr, "no RAPL package domain found under /sys/class/powercap\n");
        exit(EXIT_FAILURE);
    }
    if (has_dram && n_dram == 0) {
        fprintf(stderr, "warning: no DRAM RAPL domain found; DRAM disabled\n");
        has_dram = 0;
    }
    return 0;
}

void
fini_powercap(void)
{
    for (int i = 0; i < n_cpu; i++)
        close(cpu_doms[i].fd);
    for (int i = 0; i < n_dram; i++)
        close(dram_doms[i].fd);
}

void
print_powercap_headers()
{
    if (has_cpu_field) {
        printf(" CPU(W)");
        if (has_energy)
            printf(" CPU(J)");
        if (has_dram) {
            printf(" DRAM(W)");
            if (has_energy)
                printf(" DRAM(J)");
        }
    }
}

static double
read_energy(int fd)
{
    char buf[128];
    ssize_t n;

    lseek(fd, 0, SEEK_SET);
    n = read(fd, buf, sizeof(buf) - 1);
    if (n <= 0) {
        perror("failed to read");
        exit(EXIT_FAILURE);
    }
    buf[n] = '\0';
    return strtod(buf, NULL) / 1e6;
}

/* Sum of energy deltas (J) over all domains, handling counter wraparound. */
static double
delta_energy(struct rapl_dom *doms, int n)
{
    double total = 0;

    for (int i = 0; i < n; i++) {
        double cur = read_energy(doms[i].fd);
        sysfs_reads++;
        double d = cur - doms[i].last;
        if (d < 0 && doms[i].max_range > 0)
            d += doms[i].max_range;
        else if (d < 0)
            d = 0;
        doms[i].last = cur;
        total += d;
    }
    return total;
}

/*
 * Longest safe time (s) between two reads of every domain: the counter range
 * divided by the highest power it can reach, halved for margin. Domains that
 * do not report a maximum power are assumed to reach ASSUMED_*_MAX_W.
 * Returns 0 if no range is known.
 */
#define ASSUMED_PKG_MAX_W 300.0
#define ASSUMED_DRAM_MAX_W 100.0

static double
dom_bound(const struct rapl_dom *d, double assumed_w)
{
    double w = d->max_power > 0 ? d->max_power : assumed_w;
    return d->max_range > 0 ? d->max_range / w / 2.0 : 0;
}

double
powercap_wrap_bound_s(void)
{
    double best = 0;

    for (int i = 0; i < n_cpu + n_dram; i++) {
        double b = i < n_cpu ? dom_bound(&cpu_doms[i], ASSUMED_PKG_MAX_W)
                             : dom_bound(&dram_doms[i - n_cpu], ASSUMED_DRAM_MAX_W);
        if (b > 0 && (best == 0 || b < best))
            best = b;
    }
    return best;
}

/*
 * Reads that may wake another CPU, cumulative: sysfs energy_uj reads (each is
 * an MSR read on a CPU of the domain's package) or, with the BPF sampler,
 * samples forced by this process.
 */
unsigned long
powercap_remote_reads_total(void)
{
    return use_bpf ? bpf_sampler_forced_total() : sysfs_reads;
}

/* Switch to the BPF sampler; on failure keep reading sysfs. */
void
init_powercap_bpf(void)
{
    if (!use_bpf)
        return;
    if (bpf_sampler_init(has_dram, coma_hours > 0) != 0) {
        fprintf(stderr, "falling back to reading RAPL through sysfs\n");
        use_bpf = 0;
        return;
    }
    if (bpf_sampler_npkg() != n_cpu)
        fprintf(stderr, "warning: %d package(s) in the BPF sampler but %d RAPL package domain(s) in sysfs\n",
                bpf_sampler_npkg(), n_cpu);
}

void
fini_powercap_bpf(void)
{
    if (use_bpf)
        bpf_sampler_fini();
}

/*
 * One tick from the BPF samples. Per package, power is the energy between the
 * previous and the latest sample divided by the time between them (sample
 * times, not tick times), so rows stay exact however old the samples are.
 */
static void
bpf_tick(int baseline)
{
    uint64_t min_ns = (uint64_t)interval * 1000 / 10;   /* a tenth of the tick */
    double e_cpu = 0, e_dram = 0, p_cpu = 0, p_dram = 0, dt_sum = 0, deep_sum = 0;
    int n_used = 0;

    if (min_ns < 10000000ULL) min_ns = 10000000ULL;     /* 10 ms .. 10 s */
    if (min_ns > 10000000000ULL) min_ns = 10000000000ULL;
    bpf_sampler_set_min_interval_ns(min_ns);

    for (int i = 0; i < bpf_sampler_npkg() && i < MAX_PKGS; i++) {
        struct bpf_sample s;

        /* a sample older than half a tick is refreshed by a forced one */
        if (bpf_sampler_get(i, baseline ? 0 : interval / 2, &s) != 0)
            exit(EXIT_FAILURE);
        if (!baseline && s.ts_ns > bprev[i].ts_ns) {
            double dt = (s.ts_ns - bprev[i].ts_ns) / 1e9;
            double de = s.energy_pkg_j - bprev[i].energy_pkg_j;
            double dr = s.energy_ram_j - bprev[i].energy_ram_j;
            e_cpu += de;
            p_cpu += de / dt;
            e_dram += dr;
            p_dram += dr / dt;
            dt_sum += dt;
            deep_sum += s.c6_s - bprev[i].c6_s;
            n_used++;
        }
        bprev[i] = s;
    }
    if (baseline)
        return;
    energy_cpu = e_cpu;
    power_cpu = p_cpu;
    if (n_dram > 0) {
        energy_dram = e_dram;
        power_dram = p_dram;
    }
    if (n_used > 0) {
        tick_secs = dt_sum / n_used;
        tick_deep_s = deep_sum / n_used;
        tick_deep_valid = bpf_sampler_has_c6();
    }
}

void
start_powercap_monitor()
{
    if (use_bpf) {
        bpf_tick(1);
        return;
    }
    delta_energy(cpu_doms, n_cpu);   /* sets baseline; first call's delta is discarded */
    delta_energy(dram_doms, n_dram);
}

void
monitor_powercap_energy(void)
{
    double sec = interval / 1e6;

    if (use_bpf) {
        bpf_tick(0);
        return;
    }
    energy_cpu = delta_energy(cpu_doms, n_cpu);
    power_cpu = energy_cpu / sec;
    tick_secs = sec;
    tick_deep_valid = 0;
    if (n_dram > 0) {
        energy_dram = delta_energy(dram_doms, n_dram);
        power_dram = energy_dram / sec;
    }
}

void
report_powercap_energy(void)
{
    if (!has_cpu_field)
        return;
    printf("%.3f", power_cpu);
    if (has_energy)
        printf(" %.3f", energy_cpu);
    if (n_dram > 0) {
        printf(" %.3f", power_dram);
        if (has_energy)
            printf(" %.3f", energy_dram);
    }
}