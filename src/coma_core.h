#ifndef POWER_EYE_COMA_CORE_H
#define POWER_EYE_COMA_CORE_H

/*
 * Long-idle ("coma") detection, free of any I/O so it can be tested with
 * synthetic sequences.
 *
 * Time is cut into windows of window_s. A window is idle if its mean package
 * power is within delta_w of the idle floor and its deep package C-state
 * residency is at least deep_min_pct. The floor is either given or learned as
 * the lowest mean power of windows that meet the residency condition (so a
 * busy start cannot define it). A node is a coma candidate after hours of
 * consecutive idle windows.
 */
struct coma_cfg {
    double window_s;        /* window length */
    double delta_w;         /* idle if power <= floor + delta_w */
    double deep_min_pct;    /* idle if deep residency >= this (when known) */
    double floor_fixed_w;   /* > 0: use this floor instead of learning it */
    double hours;           /* consecutive idle hours that make a coma candidate */
};

struct coma_state {
    struct coma_cfg c;
    double win_j, win_s, win_deep_s;
    int win_deep_known;
    double elapsed_s;       /* total time fed */
    double win_start_s;
    double floor_w;
    int have_floor;
    double idle_run_s, idle_run_start_s;
    int coma;
    unsigned long n_windows, n_idle;
    double longest_idle_s;
};

struct coma_window {      /* result of a closed window */
    double start_s, end_s;
    double power_w;
    double deep_pct;        /* < 0 if unknown */
    double floor_w;         /* 0 if none yet */
    int idle;
    double idle_run_s;      /* idle time in a row including this window */
    double idle_run_start_s;
    int coma_started;       /* this window made the node a coma candidate */
    int coma_ended;         /* this window ended a coma candidacy */
};

void coma_init(struct coma_state *s, const struct coma_cfg *c);

/*
 * Add one tick: energy (J) and length (s) of the tick, and the seconds it
 * spent in deep package C-states averaged over packages (negative if
 * unknown). Returns 1 and fills *w when the tick closed a window.
 */
int coma_feed(struct coma_state *s, double joules, double secs, double deep_s, struct coma_window *w);

/* Time left in the current window (> 0). */
double coma_window_remaining_s(const struct coma_state *s);

#endif
