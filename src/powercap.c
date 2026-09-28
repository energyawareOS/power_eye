#define _GNU_SOURCE

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <errno.h>

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

double energy_cpu, power_cpu, energy_dram, power_dram;

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

/* RAPL energy_uj reads per tick; each is an MSR read on the domain's package. */
int
powercap_reads_per_tick(void)
{
    return n_cpu + n_dram;
}

void
start_powercap_monitor()
{
    delta_energy(cpu_doms, n_cpu);   /* sets baseline; first call's delta is discarded */
    delta_energy(dram_doms, n_dram);
}

void
monitor_powercap_energy(void)
{
    double sec = interval / 1e6;

    energy_cpu = delta_energy(cpu_doms, n_cpu);
    power_cpu = energy_cpu / sec;
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