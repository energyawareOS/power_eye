#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/time.h>

#include "proc_usage.h"

double *gpu_usages;

#ifndef NO_NVML

#include <nvml.h>

extern unsigned int gpu_count;
extern nvmlDevice_t *gpu_handles;
extern unsigned int *gpu_powers;

static unsigned long long *last_seen_timestamps;
static nvmlProcessUtilizationSample_t *samples_buffer;
static unsigned int samples_buffer_capacity;

static unsigned long long
now_usec(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (unsigned long long)tv.tv_sec * 1000000ULL +
           (unsigned long long)tv.tv_usec;
}

static int
ensure_samples_buffer(const unsigned int count)
{
    if (count <= samples_buffer_capacity)
        return 0;
    nvmlProcessUtilizationSample_t *tmp = (nvmlProcessUtilizationSample_t *)
        realloc(samples_buffer, sizeof(*samples_buffer) * count);
    if (tmp == NULL)
        return -1;
    samples_buffer = tmp;
    samples_buffer_capacity = count;
    return 0;
}

void
init_gpu_usage(void)
{
    if (n_pids <= 0 || gpu_count == 0)
        return;

    gpu_usages = (double *)calloc(n_pids, sizeof(double));
    last_seen_timestamps = (unsigned long long *)
        calloc(gpu_count, sizeof(unsigned long long));
    if (gpu_usages == NULL || last_seen_timestamps == NULL) {
        fprintf(stderr, "failed to malloc for gpu usage\n");
        exit(EXIT_FAILURE);
    }

    const unsigned long long t0 = now_usec();
    for (unsigned int d = 0; d < gpu_count; d++)
        last_seen_timestamps[d] = t0;
}

void
fini_gpu_usage(void)
{
    free(gpu_usages);
    gpu_usages = NULL;
    free(last_seen_timestamps);
    last_seen_timestamps = NULL;
    free(samples_buffer);
    samples_buffer = NULL;
    samples_buffer_capacity = 0;
}

void
monitor_gpu_usages(void)
{
    if (n_pids <= 0 || gpu_count == 0 || gpu_usages == NULL)
        return;

    for (int i = 0; i < n_pids; i++)
        gpu_usages[i] = 0.0;

    unsigned long long total_power_mw = 0;
    for (unsigned int d = 0; d < gpu_count; d++) {
        if (gpu_powers[d] != UINT_MAX)
            total_power_mw += gpu_powers[d];
    }

    const int has_power_weight = (total_power_mw > 0);

    for (unsigned int d = 0; d < gpu_count; d++) {
        unsigned int count = 0;
        nvmlReturn_t ret = nvmlDeviceGetProcessUtilization(
            gpu_handles[d], NULL, &count, last_seen_timestamps[d]);
        if (ret == NVML_ERROR_NOT_FOUND || count == 0)
            continue;
        if (ret != NVML_ERROR_INSUFFICIENT_SIZE && ret != NVML_SUCCESS)
            continue;

        if (ensure_samples_buffer(count) < 0)
            continue;

        ret = nvmlDeviceGetProcessUtilization(
            gpu_handles[d], samples_buffer, &count,
            last_seen_timestamps[d]);
        if (ret != NVML_SUCCESS)
            continue;

        unsigned long long latest_ts = last_seen_timestamps[d];
        for (unsigned int s = 0; s < count; s++) {
            if (samples_buffer[s].timeStamp > latest_ts)
                latest_ts = samples_buffer[s].timeStamp;
        }

        const double weight = has_power_weight
            ? ((double)gpu_powers[d] / (double)total_power_mw)
            : (1.0 / (double)gpu_count);

        for (int i = 0; i < n_pids; i++) {
            unsigned long long sum_smutil = 0;
            int num_samples = 0;
            for (unsigned int s = 0; s < count; s++) {
                if (samples_buffer[s].pid == (unsigned int)pids[i]) {
                    sum_smutil += samples_buffer[s].smUtil;
                    num_samples++;
                }
            }
            if (num_samples > 0) {
                const double avg_smutil =
                    (double)sum_smutil / (double)num_samples;
                gpu_usages[i] += (avg_smutil / 100.0) * weight;
            }
        }

        last_seen_timestamps[d] = latest_ts;
    }
}

#else

void init_gpu_usage(void) {}
void fini_gpu_usage(void) {}
void monitor_gpu_usages(void) {}

#endif
