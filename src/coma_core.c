#include "coma_core.h"

void
coma_init(struct coma_state *s, const struct coma_cfg *c)
{
    *s = (struct coma_state){ .c = *c, .win_deep_known = 1 };
}

double
coma_window_remaining_s(const struct coma_state *s)
{
    double r = s->c.window_s - s->win_s;
    return r > 0.001 ? r : 0.001;
}

int
coma_feed(struct coma_state *s, double joules, double secs, double deep_s, struct coma_window *w)
{
    double power, deep_pct;
    int deep_ok, idle;

    s->win_j += joules;
    s->win_s += secs;
    s->elapsed_s += secs;
    if (deep_s >= 0)
        s->win_deep_s += deep_s;
    else
        s->win_deep_known = 0;

    if (s->win_s + 0.001 < s->c.window_s)
        return 0;

    power = s->win_s > 0 ? s->win_j / s->win_s : 0;
    deep_pct = s->win_deep_known && s->win_s > 0 ? 100.0 * s->win_deep_s / s->win_s : -1;
    deep_ok = deep_pct < 0 || deep_pct >= s->c.deep_min_pct;

    if (s->c.floor_fixed_w > 0) {
        s->floor_w = s->c.floor_fixed_w;
        s->have_floor = 1;
    } else if (deep_ok && (!s->have_floor || power < s->floor_w)) {
        s->floor_w = power;
        s->have_floor = 1;
    }
    idle = deep_ok && s->have_floor && power <= s->floor_w + s->c.delta_w;

    w->start_s = s->win_start_s;
    w->end_s = s->elapsed_s;
    w->power_w = power;
    w->deep_pct = deep_pct;
    w->floor_w = s->have_floor ? s->floor_w : 0;
    w->idle = idle;
    w->coma_started = w->coma_ended = 0;

    s->n_windows++;
    if (idle) {
        if (s->idle_run_s == 0)
            s->idle_run_start_s = s->win_start_s;
        s->idle_run_s += s->win_s;
        s->n_idle++;
        if (s->idle_run_s > s->longest_idle_s)
            s->longest_idle_s = s->idle_run_s;
        if (!s->coma && s->c.hours > 0 && s->idle_run_s + 0.5 >= s->c.hours * 3600.0) {
            s->coma = 1;
            w->coma_started = 1;
        }
    } else {
        if (s->coma)
            w->coma_ended = 1;
        s->coma = 0;
        s->idle_run_s = 0;
    }
    w->idle_run_s = s->idle_run_s;
    w->idle_run_start_s = s->idle_run_start_s;

    s->win_j = s->win_s = s->win_deep_s = 0;
    s->win_deep_known = 1;
    s->win_start_s = s->elapsed_s;
    return 1;
}
