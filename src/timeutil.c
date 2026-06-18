#include <stdio.h>
#include <time.h>
#include <stdint.h>
#include <unistd.h>

extern int has_mmdd;
extern int interval;
extern int timeout;

static void
setup_current_time_str(char *timebuf)
{
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    if (has_mmdd)
        strftime(timebuf, 32, "%m-%d %H:%M:%S", &tm_now);
    else
        strftime(timebuf, 32, "%H:%M:%S", &tm_now);
}

void
show_current_time(void)
{
    char timebuf[32];

    setup_current_time_str(timebuf);
    printf("%s ", timebuf);
}

void
wait_until_aligned_interval(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    unsigned intv = interval / 1000000; // Convert microseconds to seconds
    unsigned sec = ts.tv_sec % 60;
    unsigned nsec = ts.tv_nsec;
    unsigned wait_sec = (sec % intv == 0) ? 0 : intv - (sec % intv);
    long wait_usec = (1000000000L - nsec) / 1000;

    // total wait time(microsecond)
    long total_wait = wait_sec * 1000000L + wait_usec;
    if (total_wait > 0)
        usleep(total_wait);
}

struct timespec ts_start, ts_next;

static inline uint64_t
get_usec_elapsed(struct timespec *start, struct timespec *end)
{
    time_t sec = end->tv_sec - start->tv_sec;
    long nsec = end->tv_nsec - start->tv_nsec;

    if (nsec < 0) {
        sec -= 1;
        nsec += 1000000000L;
    }
    return (uint64_t)sec * 1000000 + (uint64_t)(nsec / 1000);
}

void
timestamp_start(void)
{
    clock_gettime(CLOCK_MONOTONIC, &ts_start);
    ts_next = ts_start;
}

int
timestamp_sleep(void)
{
    uint64_t elapsed_from_start = get_usec_elapsed(&ts_start, &ts_next);

    if (elapsed_from_start >= timeout * 1000000)
        return 1;

    ts_next.tv_sec += interval / 1000000;
    ts_next.tv_nsec += (interval % 1000000) * 1000;
    if (ts_next.tv_nsec >= 1000000000) {
        ts_next.tv_sec += 1;
        ts_next.tv_nsec -= 1000000000;
    }
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts_next, NULL);
    return 0;
}