#include <math.h>

#include "adaptive_core.h"

unsigned long
adaptive_step(struct adapt_state *s, double power_w, unsigned long tick_us,
              unsigned long cur_us, unsigned long remaining_us, int *changed)
{
    double sec = tick_us / 1e6;
    unsigned long next;
    int chg = 0;

    if (!s->have_ref) {
        s->sum_j = power_w * sec;
        s->sum_s = sec;
        s->have_ref = 1;
        next = s->base_us;
    } else {
        double mean = s->sum_s > 0 ? s->sum_j / s->sum_s : power_w;
        double tol = s->tol_abs_w > s->tol_rel * mean ? s->tol_abs_w : s->tol_rel * mean;

        if (fabs(power_w - mean) > tol) {
            chg = 1;
            s->sum_j = power_w * sec;
            s->sum_s = sec;
            next = s->base_us;
        } else {
            s->sum_j += power_w * sec;
            s->sum_s += sec;
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
