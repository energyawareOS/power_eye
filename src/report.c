#include "config.h"

#include <stdio.h>

extern int has_headers;
extern void print_powercap_headers(void);
extern void print_gpu_headers(void);
extern void print_ipmi_headers(void);
extern void print_proc_headers(void);

extern const char *get_current_time_str(void);
extern void report_powercap_energy(void);
extern void report_gpu_energy(void);
extern void report_proc_usage(void);

void
print_headers(void)
{
    if (!has_headers)
        return;
    printf("#");
    if (has_mmdd)
        printf(" mm-dd");
    printf(" HH:MM:ss ");

    print_powercap_headers();
    print_gpu_headers();
    print_ipmi_headers();
    print_proc_headers();

    printf("\n");
}

void
report_by_timestep(void)
{
    printf("%s ", get_current_time_str());

    report_powercap_energy();
    report_gpu_energy();
    report_proc_usage();

    printf("\n");
    fflush(stdout);
}