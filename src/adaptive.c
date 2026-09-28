#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "adaptive_core.h"
#include "config.h"

/*
 * Adaptive sampling interval (-A <max_sec>): while power is steady, double the
 * interval up to the cap, return to -i as soon as it changes. The cap is
 * tightened by the RAPL counter wrap bound so no energy is lost between reads.
 * The interval is changed at the end of a tick, so everything computed within
 * a tick (power = energy / interval) keeps using the interval that tick had.
 */

unsigned long adapt_cap_us;        /* 0 = adaptive sampling off */
double adapt_tol_abs_w = 1.0;
static struct adapt_state st;
static unsigned long n_changes;

extern double powercap_wrap_bound_s(void);
extern uint64_t timestamp_remaining_us(void);
extern double power_cpu, power_dram;

void
init_adaptive(void)
{
    double wb;

    if (adapt_cap_us == 0)
        return;
    st.base_us = interval;
    st.cap_us = adapt_cap_us;
    st.tol_abs_w = adapt_tol_abs_w;
    st.tol_rel = 0.03;

    wb = powercap_wrap_bound_s();
    if (wb > 0 && wb * 1e6 < (double)st.cap_us) {
        st.cap_us = (unsigned long)(wb * 1e6);
        fprintf(stderr, "adaptive sampling: cap lowered to %.0f s by the RAPL counter wrap bound\n", wb);
    }
    if (st.cap_us < st.base_us)
        st.cap_us = st.base_us;
    fprintf(stderr, "adaptive sampling: %.3g s .. %.0f s, change tolerance max(%.2f W, %.0f%%)\n",
            st.base_us / 1e6, st.cap_us / 1e6, st.tol_abs_w, st.tol_rel * 100);
}

void
print_adaptive_headers(void)
{
    if (adapt_cap_us)
        printf(" INT(s)");
}

void
report_adaptive(void)
{
    if (adapt_cap_us)
        printf(" %.3f", interval / 1e6);
}

/* Call after the tick has been reported. */
void
update_adaptive(void)
{
    double w;
    int changed;

    if (adapt_cap_us == 0)
        return;
    w = power_cpu + (has_dram ? power_dram : 0);
    interval = adaptive_step(&st, w, interval, interval, (unsigned long)timestamp_remaining_us(), &changed);
    n_changes += changed;
}

void
fini_adaptive(void)
{
    if (adapt_cap_us)
        fprintf(stderr, "adaptive sampling: %lu change(s) detected\n", n_changes);
}
