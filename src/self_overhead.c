#define _GNU_SOURCE

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "config.h"

/*
 * Cost of the observation itself ("-f o"), per tick and for the whole run:
 *   OBS-CPU(ms/s)    CPU time this process (all threads) used per second
 *   OBS-WAKE(/s)     times it was put on a CPU per second (from schedstat)
 *   OBS-RAPLRD(/s)   Reads per second that may wake another CPU. With sysfs
 *                    each energy_uj read executes an MSR read on a CPU of the
 *                    domain's package and, unless we happen to run there,
 *                    wakes that CPU by IPI (an upper bound). With the BPF
 *                    sampler (-B) it counts the samples this process forced.
 * Needs CONFIG_SCHED_INFO (/proc/<pid>/task/<tid>/schedstat); the first two
 * columns show "-" without it.
 */

extern unsigned long powercap_remote_reads_total(void);

static int have_schedstat;
static uint64_t prev_run_ns, prev_slices;
static uint64_t first_run_ns, first_slices;
static struct timespec t_prev, t_first;
static unsigned long n_ticks;
static double cpu_ms_s, wake_s, rd_s;
static unsigned long prev_reads, first_reads;

/* Sum over all threads; returns -1 if schedstat is unavailable. */
static int
read_self_sched(uint64_t *run_ns, uint64_t *slices)
{
    DIR *d = opendir("/proc/self/task");
    struct dirent *de;
    uint64_t run = 0, sl = 0;
    int ok = 0;

    if (!d)
        return -1;
    while ((de = readdir(d)) != NULL) {
        char path[320];
        unsigned long long r, w, s;
        FILE *f;

        if (de->d_name[0] < '0' || de->d_name[0] > '9')
            continue;
        snprintf(path, sizeof path, "/proc/self/task/%s/schedstat", de->d_name);
        f = fopen(path, "r");
        if (!f)
            continue;
        if (fscanf(f, "%llu %llu %llu", &r, &w, &s) == 3) {
            run += r;
            sl += s;
            ok = 1;
        }
        fclose(f);
    }
    closedir(d);
    if (!ok)
        return -1;
    *run_ns = run;
    *slices = sl;
    return 0;
}

static double
secs_between(const struct timespec *a, const struct timespec *b)
{
    return (b->tv_sec - a->tv_sec) + (b->tv_nsec - a->tv_nsec) / 1e9;
}

void
print_self_headers(void)
{
    if (has_self)
        printf(" OBS-CPU(ms/s) OBS-WAKE(/s) OBS-RAPLRD(/s)");
}

void
start_self_overhead(void)
{
    if (!has_self)
        return;
    have_schedstat = read_self_sched(&prev_run_ns, &prev_slices) == 0;
    if (!have_schedstat)
        fprintf(stderr, "warning: no schedstat (CONFIG_SCHED_INFO); OBS-CPU and OBS-WAKE unavailable\n");
    prev_reads = first_reads = powercap_remote_reads_total();
    first_run_ns = prev_run_ns;
    first_slices = prev_slices;
    clock_gettime(CLOCK_MONOTONIC, &t_prev);
    t_first = t_prev;
}

void
monitor_self_overhead(void)
{
    struct timespec now;
    uint64_t run, sl;
    double dt;

    if (!has_self)
        return;
    clock_gettime(CLOCK_MONOTONIC, &now);
    dt = secs_between(&t_prev, &now);
    if (dt <= 0)
        return;
    n_ticks++;
    {
        unsigned long r = powercap_remote_reads_total();
        rd_s = (r - prev_reads) / dt;
        prev_reads = r;
    }
    if (have_schedstat && read_self_sched(&run, &sl) == 0) {
        cpu_ms_s = (run >= prev_run_ns ? run - prev_run_ns : 0) / 1e6 / dt;
        wake_s = (sl >= prev_slices ? sl - prev_slices : 0) / dt;
        prev_run_ns = run;
        prev_slices = sl;
    }
    t_prev = now;
}

void
report_self_overhead(void)
{
    if (!has_self)
        return;
    if (have_schedstat)
        printf(" %.3f %.2f", cpu_ms_s, wake_s);
    else
        printf(" - -");
    printf(" %.2f", rd_s);
}

void
fini_self_overhead(void)
{
    double total;
    uint64_t run, sl;

    if (!has_self || n_ticks == 0)
        return;
    total = secs_between(&t_first, &t_prev);
    fprintf(stderr, "power_eye self-overhead over %.1f s (%lu samples):", total, n_ticks);
    if (have_schedstat && read_self_sched(&run, &sl) == 0 && total > 0) {
        fprintf(stderr, " %.3f ms CPU/s (%.4f%% of one CPU), %.2f wakeups/s,",
                (run - first_run_ns) / 1e6 / total, (run - first_run_ns) / 1e9 / total * 100.0,
                (sl - first_slices) / total);
    }
    fprintf(stderr, " %.2f reads/s that may wake a package's CPU (%lu in total)\n",
            (powercap_remote_reads_total() - first_reads) / total,
            powercap_remote_reads_total() - first_reads);
}
