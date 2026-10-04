#include <math.h>

#include "adaptive_core.h"

static void
restart(struct adapt_state *s, double power_w, double deep_pct, double sec)
{
    s->sum_j = power_w * sec;
    s->sum_s = sec;
    s->sum_deep = deep_pct >= 0 ? deep_pct * sec : 0;
    s->sum_deep_s = deep_pct >= 0 ? sec : 0;
}

unsigned long
adaptive_step(struct adapt_state *s, double power_w, double deep_pct, unsigned long tick_us,
              unsigned long cur_us, unsigned long remaining_us, int *changed)
{
    double sec = tick_us / 1e6;
    unsigned long next;
    int chg = 0;

    if (!s->have_ref) {
        restart(s, power_w, deep_pct, sec);
        s->have_ref = 1;
        next = s->base_us;
    } else {
        double mean = s->sum_s > 0 ? s->sum_j / s->sum_s : power_w;
        double tol = s->tol_abs_w > s->tol_rel * mean ? s->tol_abs_w : s->tol_rel * mean;
        int deep_moved = 0;

        if (s->tol_deep_pct > 0 && deep_pct >= 0 && s->sum_deep_s > 0)
            deep_moved = fabs(deep_pct - s->sum_deep / s->sum_deep_s) > s->tol_deep_pct;

        if (fabs(power_w - mean) > tol || deep_moved) {
            chg = 1;
            restart(s, power_w, deep_pct, sec);
            next = s->base_us;
        } else {
            s->sum_j += power_w * sec;
            s->sum_s += sec;
            if (deep_pct >= 0) {
                s->sum_deep += deep_pct * sec;
                s->sum_deep_s += sec;
            }
            next = cur_us * 2;
            if (next > s->cap_us)
                next = s->cap_us;
        }
    }
    if (next < s->base_us)
        next = s->base_us;
    if (remaining_us > 0 && next > remaining_us)
        next = remaining_us < 1000 ? 1000 : remaining_us;
    if (changed)
        *changed = chg;
    return next;
}
