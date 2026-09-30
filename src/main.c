#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>

#include "config.h"

extern void init_cpu_usage(void);
extern void fini_cpu_usage(void);
extern void init_gpu_usage(void);
extern void fini_gpu_usage(void);
extern void init_powercap(void);
extern void fini_powercap(void);
extern void init_nvml(void);
extern void fini_nvml(void);
extern void init_ipmi(void);
extern void fini_ipmi(void);
extern void fini_self_overhead(void);
extern void init_powercap_bpf(void);
extern void fini_powercap_bpf(void);
extern void init_adaptive(void);
extern void fini_adaptive(void);

extern void monitor_energy(void);

extern void add_proc_usage(pid_t pid);

extern int use_gpu;
extern int per_gpu_output;
extern int use_ipmi;

static void
usage(void)
{
    printf(
        "Usage: power_eye <options>\n"
        "Options:\n"
        "  -i <interval>  Sampling interval in seconds, fractions allowed (default: 1)\n"
        "  -t <timeout>   Total measurement duration in seconds (default: 120 seconds)\n"
        "  -A <max>       Adaptive interval: while power is steady, double the interval up to\n"
        "                 <max> seconds; return to -i when it changes\n"
        "  -T <watts>     Change tolerance for -A (default: 1.0, or 3%% of the mean if larger)\n"
        "  -d             Enable DRAM power measurement\n"
        "  -B             Read RAPL from an eBPF sampler on CPU idle exits instead of sysfs\n"
        "                 (no IPIs; needs a build with -DUSE_BPF=ON, root)\n"
        "  -h             Show this help message and exit\n"
        "  -I             Enable IPMI power measurement\n"
        "  -p <pid>       Monitor a specific process by PID\n"
        "  -f <field specs>\n"
        "     d: Prepend MM-dd to the timestamp\n"
        "     c: CPU power\n"
        "     E: Show energy field in outputs\n"
        "     H: Show field headers in outputs\n"
        "     o: Self-overhead of the observation (CPU ms/s, wakeups/s, RAPL reads/s)\n"
#ifndef NO_NVML
        "     g: GPU total power\n"
        "     G: Per-GPU power (implies g)\n"
#endif
        );
}

static void
parse_field_options(const char *optarg)
{
    for (const char *p = optarg; *p != '\0'; p++) {
        switch (*p) {
        case 'd':
            has_mmdd = 1;
            break;
        case 'c':
            has_cpu_field = 1;
            break;
        case 'g':
            use_gpu = 1;
            break;
        case 'G':
            use_gpu = 1;
            per_gpu_output = 1;
            break;
        case 'E':
            has_energy = 1;
            break;
        case 'H':
            has_headers = 1;
            break;
        case 'o':
            has_self = 1;
            break;
        default:
            fprintf(stderr, "Unknown field option: %c\n", *p);
            exit(EXIT_FAILURE);
        }
    }
}

static void
parse_args(int argc, char *argv[])
{
    int c;

    while ((c = getopt(argc, argv, "i:t:f:dhIBp:A:T:")) != -1) {
        switch (c) {
        case 'i':
            {
                char *end;
                double sec = strtod(optarg, &end);
                if (*end != '\0' || sec < 0.001) {
                    fprintf(stderr, "Invalid interval value: %s\n", optarg);
                    exit(EXIT_FAILURE);
                }
                interval = (unsigned long)(sec * 1000000.0 + 0.5);
            }
            break;
        case 't':
            timeout = strtoul(optarg, NULL, 10);
            if (timeout <= 0) {
                fprintf(stderr, "Invalid timeout value: %s\n", optarg);
                exit(EXIT_FAILURE);
            }
            break;
        case 'd':
            has_dram = 1;
            break;
        case 'f':
            parse_field_options(optarg);
            break;
        case 'A':
            {
                char *end;
                double sec = strtod(optarg, &end);
                if (*end != '\0' || sec < 0.001) {
                    fprintf(stderr, "Invalid -A value: %s\n", optarg);
                    exit(EXIT_FAILURE);
                }
                adapt_cap_us = (unsigned long)(sec * 1000000.0 + 0.5);
            }
            break;
        case 'T':
            {
                char *end;
                adapt_tol_abs_w = strtod(optarg, &end);
                if (*end != '\0' || adapt_tol_abs_w <= 0) {
                    fprintf(stderr, "Invalid -T value: %s\n", optarg);
                    exit(EXIT_FAILURE);
                }
            }
            break;
        case 'h':
            usage();
            exit(0);
        case 'I':
            use_ipmi = 1;
            break;
        case 'B':
            use_bpf = 1;
            break;
        case 'p':
            add_proc_usage((pid_t)strtoul(optarg, NULL, 10));
            break;
        default:
            usage();
            exit(EXIT_FAILURE);
        }
    }
}

int
main(const int argc, char *argv[])
{
    parse_args(argc, argv);

    init_cpu_usage();
    init_powercap();
    init_powercap_bpf();
    init_nvml();
    init_gpu_usage();
    init_ipmi();
    init_adaptive();

    monitor_energy();

    fini_adaptive();
    fini_self_overhead();
    fini_ipmi();
    fini_gpu_usage();
    fini_nvml();
    fini_powercap_bpf();
    fini_powercap();
    fini_cpu_usage();

    return 0;
}