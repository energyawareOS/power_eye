#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>

#include "config.h"

extern void init_powercap(void);
extern void fini_powercap(void);
extern void init_nvml(void);
extern void fini_nvml(void);
extern void init_ipmi(void);
extern void fini_ipmi(void);

extern void monitor_energy(void);

#ifndef NO_NVML
extern int use_gpu;
extern int per_gpu_output;
#endif

extern int use_ipmi;

static void
usage(void)
{
    printf(
        "Usage: power_eye [-i interval] [-t timeout] [-h]\n"
        "Options:\n"
        "  -i <interval>  Sampling interval in seconds (default: 1 second)\n"
        "  -t <timeout>   Total measurement duration in seconds (default: 120 seconds)\n"
        "  -d             Enable DRAM power measurement\n"
        "  -D             Show MM-dd field in outputs\n"
        "  -E             Show energy field in outputs\n"
        "  -H             Show field headers in outputs\n"
        "  -h             Show this help message and exit\n"
        "  -I             Enable IPMI power measurement\n"
#ifndef NO_NVML
        "  -g             Enable GPU power measurement\n"
        "  -G             Show all powers of GPU devices\n"
#endif
        );
}

static void
parse_args(int argc, char *argv[])
{
    int c;

    while ((c = getopt(argc, argv, "i:t:dDEHhIgG")) != -1) {
        switch (c) {
        case 'i':
            interval = strtoul(optarg, NULL, 10) * 1000000;
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
        case 'D':
            has_mmdd = 1;
            break;
        case 'E':
            has_energy = 1;
            break;
        case 'H':
            has_headers = 1;
            break;
        case 'h':
            usage();
            exit(0);
        case 'I':
            use_ipmi = 1;
            init_ipmi();
            break;
#ifndef NO_NVML
        case 'g':
            use_gpu = 1;
            break;
        case 'G':
            use_gpu = 1;
            per_gpu_output = 1;
            break;
#endif
        default:
            usage();
            exit(EXIT_FAILURE);
        }
    }
}

int
main(int argc, char *argv[])
{
    parse_args(argc, argv);

    init_powercap();
    init_nvml();
    init_ipmi();

    monitor_energy();

    fini_ipmi();
    fini_nvml();
    fini_powercap();

    return 0;
}