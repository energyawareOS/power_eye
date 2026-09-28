#ifndef POWER_EYE_ADAPTIVE_CORE_H
#define POWER_EYE_ADAPTIVE_CORE_H

/*
 * Decision rule of the adaptive sampling interval, free of any I/O so it can
 * be tested with synthetic power sequences.
 *
 * While the measured power stays within a tolerance of the running mean of the
 * current quiet period, the next interval doubles up to a cap; as soon as a
 * sample leaves the tolerance band the interval returns to the base value and
 * the quiet period restarts at the new level.
 */
struct adapt_state {
    unsigned long base_us;   /* interval used while things change (-i) */
    unsigned long cap_us;    /* longest interval allowed (-A, tightened by the RAPL wrap bound) */
    double tol_abs_w;        /* tolerance band: max(tol_abs_w, tol_rel * mean) */
    double tol_rel;
    /* running state */
    double sum_j;            /* energy of the quiet period */
    double sum_s;            /* its length */
    int have_ref;
};

/*
 * Feed one sample: average power over the last tick, and the tick's length.
 * remaining_us: time left until the end of the run (0 = unlimited). Returns
 * the interval to use for the next tick; *changed is set if the sample left
 * the tolerance band.
 */
unsigned long adaptive_step(struct adapt_state *s, double power_w, unsigned long tick_us,
                            unsigned long cur_us, unsigned long remaining_us, int *changed);

#endif
