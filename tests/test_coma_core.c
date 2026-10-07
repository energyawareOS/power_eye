#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "../src/adaptive_core.h"
#include "../src/coma_core.h"

/* Feed one whole window of constant power and deep residency (percent). */
static int win(struct coma_state *s, double w, double deep_pct, struct coma_window *out)
{
    double secs = s->c.window_s;
    return coma_feed(s, w * secs, secs, deep_pct < 0 ? -1 : deep_pct / 100.0 * secs, out);
}

int main(void)
{
    struct coma_cfg c = { .window_s = 600, .delta_w = 1.0, .deep_min_pct = 80, .hours = 1 };
    struct coma_state s;
    struct coma_window w;

    /* a window closes only when the full length has been fed, also over several ticks */
    coma_init(&s, &c);
    assert(!coma_feed(&s, 25.0 * 100, 100, 90, &w));
    assert(fabs(coma_window_remaining_s(&s) - 500) < 1e-9);
    assert(!coma_feed(&s, 25.0 * 300, 300, 270, &w));
    assert(coma_feed(&s, 25.0 * 200, 200, 180, &w));
    assert(fabs(w.power_w - 25.0) < 1e-9 && fabs(w.deep_pct - 90.0) < 1e-9 && w.idle);

    /* learned floor, a busy start cannot define it: first windows are busy and shallow */
    coma_init(&s, &c);
    assert(win(&s, 90.0, 20, &w) && !w.idle && w.floor_w == 0);   /* no deep window yet: no floor */
    assert(win(&s, 25.0, 88, &w) && w.idle && w.floor_w == 25.0);
    assert(win(&s, 25.6, 88, &w) && w.idle);                      /* within delta */
    assert(win(&s, 27.0, 88, &w) && !w.idle);                     /* above floor + delta */
    assert(win(&s, 24.0, 88, &w) && w.idle && w.floor_w == 24.0); /* floor follows downwards */
    assert(win(&s, 25.5, 70, &w) && !w.idle);                     /* residency too low */

    /* coma candidate after `hours` consecutive idle windows, at the window that completes it */
    coma_init(&s, &c);
    for (int i = 1; i <= 6; i++) {
        assert(win(&s, 25.0, 90, &w) && w.idle);
        assert(w.coma_started == (i == 6));
        assert(fabs(w.idle_run_s - 600.0 * i) < 1e-6);
    }
    assert(win(&s, 25.0, 90, &w) && !w.coma_started && s.coma);   /* stays a candidate, reported once */
    assert(win(&s, 60.0, 30, &w) && !w.idle && w.coma_ended && !s.coma && s.idle_run_s == 0);
    assert(win(&s, 25.0, 90, &w) && w.idle && fabs(w.idle_run_start_s - s.win_start_s + 600) < 1e-6);
    assert(s.longest_idle_s == 4200);

    /* one busy window in the middle restarts the count */
    coma_init(&s, &c);
    for (int i = 0; i < 5; i++) win(&s, 25.0, 90, &w);
    win(&s, 40.0, 50, &w);
    for (int i = 1; i <= 6; i++) {
        win(&s, 25.0, 90, &w);
        assert(w.coma_started == (i == 6));
    }

    /* given floor: the first window can already be idle, learning is off */
    c.floor_fixed_w = 20.0;
    coma_init(&s, &c);
    assert(win(&s, 20.5, 90, &w) && w.idle && w.floor_w == 20.0);
    assert(win(&s, 25.0, 90, &w) && !w.idle && w.floor_w == 20.0);
    c.floor_fixed_w = 0;

    /* unknown residency: power decides, with the learned floor */
    coma_init(&s, &c);
    assert(win(&s, 25.0, -1, &w) && w.deep_pct < 0 && w.idle);
    assert(win(&s, 30.0, -1, &w) && !w.idle);

    /* a partly unknown window is unknown as a whole */
    coma_init(&s, &c);
    coma_feed(&s, 25.0 * 300, 300, 270, &w);
    assert(coma_feed(&s, 25.0 * 300, 300, -1, &w) && w.deep_pct < 0);

    /*
     * Adaptive interval cut at window ends, as the glue does: a node that is
     * idle for 3 hours is flagged in the window that completes hour 1, windows
     * are exactly 600 s long, and a long idle needs few samples.
     */
    {
        struct coma_cfg cc = { .window_s = 600, .delta_w = 1.0, .deep_min_pct = 80, .hours = 1 };
        struct adapt_state as = { .base_us = 1000000UL, .cap_us = 600000000UL, .tol_abs_w = 1.0,
                                  .tol_rel = 0.03, .tol_deep_pct = 10 };
        unsigned long iv = 1000000UL, ticks = 0, first_win_ticks = 0;
        double t = 0;
        int ch, started_at = 0, nwin = 0;

        coma_init(&s, &cc);
        while (t < 3 * 3600.0 - 1e-6) {
            double secs = iv / 1e6;
            unsigned long rem;
            ticks++;
            t += secs;
            if (coma_feed(&s, 25.0 * secs, secs, 0.88 * secs, &w)) {
                nwin++;
                assert(w.end_s - w.start_s >= 600.0 - 0.01 && w.end_s - w.start_s <= 601.0);
                if (nwin == 1) first_win_ticks = ticks;
                if (w.coma_started) started_at = nwin;
            }
            /* the glue clamps with the time left in the window after this tick was fed */
            rem = (unsigned long)(coma_window_remaining_s(&s) * 1e6);
            if (rem < as.base_us)       /* as in adaptive.c: no tiny remainder ticks */
                rem = as.base_us;
            iv = adaptive_step(&as, 25.0, 88.0, iv, iv, rem, &ch);
            assert(iv <= rem + 1000);
            assert(iv >= as.base_us);
        }
        assert(nwin == 18);
        assert(started_at == 6);
        assert(first_win_ticks <= 12);             /* ramps up 1,2,4,... within the first window */
        assert(ticks < 18 * 3 + 12);               /* then about one or two ticks per window */
    }

    printf("coma_core: all tests passed\n");
    return 0;
}
