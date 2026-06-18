#define _GNU_SOURCE

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"

static int fd_cpu, fd_cpu_dram = -1;

extern int has_dram;
static double acc_energy_last_cpu, acc_energy_last_dram;

double energy_cpu, power_cpu, energy_dram, power_dram;

static int
open_powercap(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        char *errmsg;
        if (asprintf(&errmsg, "failed to open %s", path) > 0)
            perror(errmsg);
        exit(EXIT_FAILURE);
    }
    return fd;
}

static int
open_powercap_pkg0_energy(void)
{
    return open_powercap("/sys/class/powercap/intel-rapl:0/energy_uj");
}

static int
open_powercap_pkg0_dram_energy(void)
{
    for (int i = 0;; i++) {
        char *path, name[64];

        if (asprintf(&path, "/sys/class/powercap/intel-rapl:0:%d/name", i) < 0)
            return -1;
        int fd = open_powercap(path);
        int ret = read(fd, name, sizeof name);
        free(path);
        if (ret <= 0)
            return -1;
        if (strncmp(name, "dram", 4) == 0) {
            if (asprintf(&path, "/sys/class/powercap/intel-rapl:0:%d/energy_uj", i) < 0)
                return -1;
            fd = open_powercap(path);
            free(path);
            return fd;
        }
    }
}

int
init_powercap(void)
{
    fd_cpu = open_powercap_pkg0_energy();
    if (has_dram)
        fd_cpu_dram = open_powercap_pkg0_dram_energy();
    return 0;
}

void
fini_powercap(void)
{
    close(fd_cpu);
    if (fd_cpu_dram >= 0)
        close(fd_cpu_dram);
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
    // ReSharper disable once CppJoinDeclarationAndAssignment
    unsigned long energy_uj;
    char buf[128];

    lseek(fd, 0, SEEK_SET);
    if (read(fd, buf, sizeof(buf)) == -1) {
        perror("failed to read");
        exit(EXIT_FAILURE);
    }
    energy_uj = strtoul(buf, NULL, 10);
    return (double)energy_uj / 1e6;
}

static void
read_power_energy(int fd, double *p_power, double *p_energy,
                  double *p_acc_energy_last)
{
    double acc_energy_cur = read_energy(fd);

    *p_energy = acc_energy_cur - *p_acc_energy_last;
    *p_power = *p_energy / (interval / 1000000);
    *p_acc_energy_last = acc_energy_cur;
}

void
start_powercap_monitor()
{
    acc_energy_last_cpu = read_energy(fd_cpu);
    if (fd_cpu_dram >= 0)
        acc_energy_last_dram = read_energy(fd_cpu_dram);
}

void
monitor_powercap_energy(void)
{
    read_power_energy(fd_cpu, &power_cpu, &energy_cpu, &acc_energy_last_cpu);
    if (fd_cpu_dram >= 0)
        read_power_energy(fd_cpu_dram, &power_dram, &energy_dram,
                          &acc_energy_last_dram);
}

void
report_powercap_energy(void)
{
    if (!has_cpu_field)
        return;
    printf("%.3f", power_cpu);
    if (has_energy)
        printf(" %.3f", energy_cpu);
    if (fd_cpu_dram >= 0) {
        printf(" %.3f", power_dram);
        if (has_energy)
            printf(" %.3f", energy_dram);
    }
}