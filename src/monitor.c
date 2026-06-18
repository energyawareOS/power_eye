#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h>

unsigned long interval = 1000000; // Default interval in usecs
unsigned int timeout = 120;       // Default timeout in seconds
int has_cpu_field = 0;
int has_dram = 0;
int has_mmdd = 0;
int has_energy = 0;
int has_headers = 0;

extern void print_gpu_headers(void);
extern void monitor_gpu_energy(void);
extern int init_ipmi(void);
extern void print_headers(void);
extern void wait_until_aligned_interval(void);
extern void timestamp_start(void);
extern int timestamp_sleep(void);
extern void print_headers(void);
extern void start_powercap_monitor(void);
extern void monitor_powercap_energy(void);
extern void monitor_cpu_usages(void);
extern void monitor_gpu_usages(void);
extern void monitor_ipmi(void);
extern void start_gpu_monitor(void);

extern void report_by_timestep(void);

void
monitor_energy(void)
{
    wait_until_aligned_interval();

    print_headers();

    timestamp_start();

    start_powercap_monitor();

#ifndef NO_NVML
    start_gpu_monitor();
#endif

    while (1) {
        if (timestamp_sleep())
            break;

        monitor_powercap_energy();
        monitor_cpu_usages();
        monitor_gpu_usages();
        monitor_ipmi();
        monitor_gpu_energy();

        report_by_timestep();
    }
}
