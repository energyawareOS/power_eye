#include <stdio.h>
#include <time.h>
#include <stdint.h>
#include <unistd.h>

struct timespec ts_real, ts_start, ts_next;

extern int has_mmdd;
extern unsigned long interval;
extern unsigned int timeout;

static inline void
timespec_add_us(struct timespec *ts, const long usec)
{
    ts->tv_sec += usec / 1000000;
    ts->tv_nsec += (usec % 1000000) * 1000;

    // 나노초가 1초를 넘어가면 초(sec)로 올림 처리
    if (ts->tv_nsec >= 1000000000) {
        ts->tv_sec += 1;
        ts->tv_nsec -= 1000000000;
    }
}

const char *
get_current_time_str(void)
{
    static char timebuf[32];
    struct tm tm_now;

    localtime_r(&ts_real.tv_sec, &tm_now);

    if (has_mmdd)
        strftime(timebuf, 32, "%m-%d %H:%M:%S", &tm_now);
    else
        strftime(timebuf, 32, "%H:%M:%S", &tm_now);
    return timebuf;
}

void
wait_until_aligned_interval(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    // Whole-second intervals align to the minute; sub-second/fractional ones to the next second
    unsigned intv = interval / 1000000;
    unsigned sec = ts.tv_sec % 60;
    unsigned nsec = ts.tv_nsec;
    unsigned wait_sec = (intv == 0 || sec % intv == 0) ? 0 : intv - (sec % intv);
    long wait_usec = (1000000000L - nsec) / 1000;

    // total wait time(microsecond)
    long total_wait = wait_sec * 1000000L + wait_usec;
    if (total_wait > 0)
        usleep(total_wait);
}

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
    clock_gettime(CLOCK_REALTIME, &ts_real);
    clock_gettime(CLOCK_MONOTONIC, &ts_start);
    ts_next = ts_start;
}

int
timestamp_sleep(void)
{
    uint64_t elapsed_from_start = get_usec_elapsed(&ts_start, &ts_next);

    if (elapsed_from_start >= (uint64_t)timeout * 1000000)
        return 1;

    timespec_add_us(&ts_next, interval);

    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts_next, NULL);

    timespec_add_us(&ts_real, interval);

    return 0;
}
