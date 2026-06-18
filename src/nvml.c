#ifndef NO_NVML

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <nvml.h>

extern unsigned long interval;
extern int has_energy;

int use_gpu = 0, per_gpu_output = 0;
static unsigned int gpu_count;
static nvmlDevice_t *gpu_handles;
static unsigned int *gpu_powers;
static double *gpu_energies;

void
print_gpu_headers(void)
{
#ifndef NO_NVML
    if (use_gpu) {
        printf(" GPU(W)");
        if (has_energy)
            printf(" GPU(J)");
        if (per_gpu_output) {
            for (int i = 0; i < gpu_count; i++) {
                printf(" GPU%02d(W)", i);
                if (has_energy)
                    printf(" GPU%02d(J)", i);
            }
        }
    }
#endif
}

static void
read_energy_gpu(double *p_total_power, double *p_total_energy)
{
    *p_total_power = 0;
    *p_total_energy = 0;

    for (unsigned int i = 0; i < gpu_count; i++) {
        unsigned int power_mw;
        if (nvmlDeviceGetPowerUsage(gpu_handles[i], &power_mw) == NVML_SUCCESS) {
            if (gpu_powers[i] != UINT_MAX)
                gpu_energies[i] = (gpu_powers[i] + power_mw) / 1000.0 *
                                  (double)interval / 1000000.0 / 2;
            gpu_powers[i] = power_mw;
            *p_total_power += (power_mw / 1000.0);
            *p_total_energy += gpu_energies[i];
        }
    }
}

void
monitor_gpu_energy()
{
    if (use_gpu) {
        double total_power, total_energy;

        read_energy_gpu(&total_power, &total_energy);

        printf(" %.3f", total_power);
        if (has_energy)
            printf(" %.3f", total_energy);

        if (per_gpu_output) {
            for (unsigned int i = 0; i < gpu_count; i++) {
                printf(" %.3f", gpu_powers[i] / 1000.0);
                if (has_energy)
                    printf(" %.3f", gpu_energies[i]);
            }
        }
    }
}

void
init_nvml(void)
{
    if (!use_gpu)
        return;

    if (nvmlInit_v2() != NVML_SUCCESS) {
        fprintf(stderr, "NVML init failed\n");
        exit(EXIT_FAILURE);
    }
    if (nvmlDeviceGetCount_v2(&gpu_count) != NVML_SUCCESS || gpu_count == 0) {
        fprintf(stderr, "No NVIDIA GPUs found\n");
        exit(EXIT_FAILURE);
    }
    gpu_powers = (unsigned int *)calloc(gpu_count, sizeof(unsigned int));
    gpu_energies = (double *)malloc(sizeof(double) * gpu_count);
    gpu_handles = (nvmlDevice_t *)malloc(sizeof(nvmlDevice_t) * gpu_count);
    if (gpu_powers == NULL || gpu_energies == NULL || gpu_handles == NULL) {
        perror("malloc for gpu monitoring");
        exit(EXIT_FAILURE);
    }
    for (unsigned int i = 0; i < gpu_count; i++) {
        gpu_powers[i] = UINT_MAX;
        gpu_energies[i] = 0.0;
        if (nvmlDeviceGetHandleByIndex_v2(i, &gpu_handles[i]) != NVML_SUCCESS) {
            fprintf(stderr, "Failed to get handle for GPU %d\n", i);
            exit(EXIT_FAILURE);
        }
    }
}

void
fini_nvml(void)
{
    if (!use_gpu)
        return;

    if (gpu_handles) {
        free(gpu_handles);
        gpu_handles = NULL;
    }
    nvmlShutdown();
}

#else

void init_nvml(void) {}
void fini_nvml(void) {}

#endif

void
start_gpu_monitor()
{

}