#include <stdio.h>
#include <stdlib.h>

#include "config.h"

int n_pids;
pid_t *pids;

extern double *cpu_usages;
extern double energy_cpu, power_cpu;

extern int use_gpu;
extern double total_gpu_power, total_gpu_energy;
extern double *gpu_usages;

void
print_proc_headers(void)
{
    if (n_pids <= 0)
        return;
    for (int i = 0; i < n_pids; i++) {
        if (use_gpu) {
            printf(" PID%d-CPU(W)", pids[i]);
            if (has_energy)
                printf(" PID%d-CPU(J)", pids[i]);
            printf(" PID%d-GPU(W)", pids[i]);
            if (has_energy)
                printf(" PID%d-GPU(J)", pids[i]);
        } else {
            printf(" PID%d(W)", pids[i]);
            if (has_energy)
                printf(" PID%d(J)", pids[i]);
        }
    }
}

void
add_proc_usage(const pid_t pid)
{
    n_pids++;

    pid_t *pids_temp = (pid_t *)realloc(pids, sizeof(pid_t) * n_pids);
    pids = pids_temp;
    pids[n_pids - 1] = pid;
}

void
report_proc_usage(void)
{
    for (int i = 0; i < n_pids; i++) {
        printf(" %f", power_cpu * cpu_usages[i]);
        if (has_energy)
            printf(" %f", energy_cpu * cpu_usages[i]);
        if (use_gpu) {
            const double frac = (gpu_usages != NULL) ? gpu_usages[i] : 0.0;
            printf(" %f", total_gpu_power * frac);
            if (has_energy)
                printf(" %f", total_gpu_energy * frac);
        }
    }
}
