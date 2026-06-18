#include "config.h"

#include <stdio.h>

extern int has_headers;
extern void print_powercap_headers(void);
extern void print_gpu_headers(void);
extern void print_ipmi_headers(void);

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

    printf("\n");
}