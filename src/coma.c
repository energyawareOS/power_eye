#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "coma_core.h"
#include "config.h"

/*
 * Long-idle ("coma") detection (-C <hours>). See coma_core.h for the rule.
 * Window summaries and state changes are printed to stdout as '#' lines, so
 * data rows stay parseable. With -A the interval is cut so that ticks end on
 * window boundaries.
 */

double coma_hours;                 /* 0 = off */
double coma_window_s = 600;
double coma_delta_w = 1.0;
double coma_deep_pct = 80;
double coma_floor_w;               /* 0 = learn */

static struct coma_state st;
static time_t t_start;      /* wall-clock time of the start of the first window */
static int warned_no_deep;

extern double energy_cpu, tick_secs, tick_deep_s;
extern int tick_deep_valid;

static void
fmt_time(time_t t, char *buf, size_t len)
{
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, len, "%m-%d %H:%M:%S", &tm);
}

static void
fmt_dur(double s, char *buf, size_t len)
{
    if (s >= 3600)
        snprintf(buf, len, "%.2f h", s / 3600.0);
    else
        snprintf(buf, len, "%.1f min", s / 60.0);
}

void
init_coma(void)
{
    struct coma_cfg c;

    if (coma_hours <= 0)
        return;
    c = (struct coma_cfg){ .window_s = coma_window_s, .delta_w = coma_delta_w,
                           .deep_min_pct = coma_deep_pct, .floor_fixed_w = coma_floor_w,
                           .hours = coma_hours };
    coma_init(&st, &c);
    fprintf(stderr, "coma detection: %.0f s windows, idle = package power <= floor (%s) + %.2f W and "
            "deep package C-state residency >= %.0f%%; candidate after %.2f h of idle windows\n",
            coma_window_s, coma_floor_w > 0 ? "given" : "learned", coma_delta_w, coma_deep_pct, coma_hours);
}

/* Time left in the current window, 0 if detection is off. */
uint64_t
coma_window_remaining_us(void)
{
    return coma_hours > 0 ? (uint64_t)(coma_window_remaining_s(&st) * 1e6) : 0;
}

/* Call after the tick has been reported. */
void
update_coma(void)
{
    struct coma_window w;
    char a[32], b[32], d[32];
    double deep_s;

    if (coma_hours <= 0)
        return;
    if (!t_start)   /* the first tick covers the time since monitoring started */
        t_start = time(NULL) - (time_t)(tick_secs + 0.5);
    deep_s = tick_deep_valid ? tick_deep_s : -1;
    if (!tick_deep_valid && !warned_no_deep) {
        warned_no_deep = 1;
        fprintf(stderr, "coma detection: no package C-state residency (needs -B and the cstate_pkg PMU); "
                "judging by power only, which is unreliable\n");
    }
    if (!coma_feed(&st, energy_cpu, tick_secs, deep_s, &w))
        return;

    fmt_time(t_start + (time_t)w.start_s, a, sizeof a);
    fmt_time(t_start + (time_t)w.end_s, b, sizeof b);
    fmt_dur(w.idle_run_s, d, sizeof d);
    printf("# window %s .. %s %.3f W", a, b, w.power_w);
    if (w.floor_w > 0)
        printf(" (floor %.3f W, %+.3f W)", w.floor_w, w.power_w - w.floor_w);
    else
        printf(" (no floor yet)");
    if (w.deep_pct >= 0)
        printf(" deep %.1f%%", w.deep_pct);
    else
        printf(" deep n/a");
    printf(" -> %s", w.idle ? "idle" : "busy");
    if (w.idle)
        printf(" (idle for %s)", d);
    printf("\n");
    if (w.coma_started) {
        fmt_time(t_start + (time_t)w.idle_run_start_s, a, sizeof a);
        printf("# COMA CANDIDATE: idle since %s (%s)\n", a, d);
    }
    if (w.coma_ended)
        printf("# coma candidacy ended: busy window ending %s\n", b);
    fflush(stdout);
}

void
fini_coma(void)
{
    char d[32];

    if (coma_hours <= 0 || st.n_windows == 0)
        return;
    fmt_dur(st.longest_idle_s, d, sizeof d);
    fprintf(stderr, "coma detection: %lu window(s), %lu idle, longest idle run %s, %s\n",
            st.n_windows, st.n_idle, d, st.coma ? "ended as a COMA CANDIDATE" : "not a coma candidate");
}
