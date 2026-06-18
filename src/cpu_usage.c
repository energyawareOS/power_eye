#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>

#include "proc_usage.h"

static unsigned long long total_cpu_time_prev = ULLONG_MAX;
static unsigned long long *proc_cpu_times_prev;
static long n_cpus;

double *cpu_usages;

static unsigned long long
get_total_cpu_time(void)
{
    char line[1024];

    FILE *fp = fopen("/proc/stat", "r");
    if (fp == NULL)
        return 0;

    fgets(line, sizeof(line), fp);
    fclose(fp);

    unsigned long long user, nice, system, idle;
    unsigned long long iowait, irq, softirq, steal;
    unsigned long long guest, guest_nice;

    sscanf(line,
           "cpu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
           &user, &nice, &system, &idle,
           &iowait, &irq, &softirq, &steal,
           &guest, &guest_nice);

    return user + nice + system + idle +
           iowait + irq + softirq + steal +
           guest + guest_nice;
}

static unsigned long long
get_process_cpu_time(pid_t pid)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);

    FILE *fp = fopen(path, "r");
    if (!fp)
        return 0;

    char buf[4096];
    fgets(buf, sizeof(buf), fp);
    fclose(fp);

    char *ptr = strrchr(buf, ')');
    if (!ptr)
        return 0;

    ptr += 2; // state field skip

    unsigned long long utime, stime;

    sscanf(ptr,
           "%*c "
           "%*d %*d %*d %*d %*d "
           "%*u %*u %*u %*u %*u "
           "%llu %llu",
           &utime, &stime);

    return utime + stime;
}

void
monitor_cpu_usages(void)
{
    unsigned long long total_cpu_time_cur = get_total_cpu_time();
    unsigned long long total_cpu_time_delta = (total_cpu_time_prev !=
                                               ULLONG_MAX)
                                                  ? (total_cpu_time_cur -
                                                     total_cpu_time_prev)
                                                  : ULLONG_MAX;

    total_cpu_time_prev = total_cpu_time_cur;
    for (int i = 0; i < n_pids; i++) {
        unsigned long long proc_cpu_time = get_process_cpu_time(pids[i]);
        unsigned long long proc_cpu_time_delta = (total_cpu_time_delta !=
                                                  ULLONG_MAX &&
                                                  total_cpu_time_delta > 0)
                                                     ? (proc_cpu_time -
                                                        proc_cpu_times_prev[i])
                                                     : ULLONG_MAX;
        cpu_usages[i] = (proc_cpu_time_delta != ULLONG_MAX)
                            ? ((double)proc_cpu_time_delta / (double)total_cpu_time_delta) * 100.0
                            : 0.0;
        proc_cpu_times_prev[i] = proc_cpu_time;
    }
}

void
init_cpu_usage(void)
{
    n_cpus = sysconf(_SC_NPROCESSORS_ONLN);

    proc_cpu_times_prev = (unsigned long long *)malloc(sizeof(unsigned long long) * n_pids);
    cpu_usages = (double *)malloc(sizeof(double) * n_pids);

    if (proc_cpu_times_prev == NULL || cpu_usages == NULL) {
        fprintf(stderr, "failed to malloc\n");
        exit(EXIT_FAILURE);
    }
}

void
fini_cpu_usage(void)
{
    free(proc_cpu_times_prev);
}
