#include <assert.h>
#include <stdio.h>

#include "../src/adaptive_core.h"

#define S 1000000UL

static struct adapt_state mk(unsigned long base, unsigned long cap)
{
    struct adapt_state s = { .base_us = base, .cap_us = cap, .tol_abs_w = 1.0, .tol_rel = 0.03 };
    return s;
}

int main(void)
{
    int ch;
    unsigned long iv;

    /* steady power: base, then doubling up to the cap and staying there */
    struct adapt_state s = mk(1 * S, 600 * S);
    iv = 1 * S;
    for (int i = 0; i < 12; i++) {
        unsigned long want = 1UL << i;                /* 1, 2, 4, ... */
        if (want > 600) want = 600;
        iv = adaptive_step(&s, 25.0 + (i % 2 ? 0.2 : -0.2), -1, iv, iv, 0, &ch);
        assert(!ch);
        assert(iv == want * S);
    }
    assert(iv == 600 * S);

    /* a change returns to the base interval immediately and restarts the band */
    iv = adaptive_step(&s, 40.0, -1, iv, iv, 0, &ch);
    assert(ch && iv == 1 * S);
    iv = adaptive_step(&s, 40.3, -1, iv, iv, 0, &ch);       /* within the new band */
    assert(!ch && iv == 2 * S);
    iv = adaptive_step(&s, 25.0, -1, iv, iv, 0, &ch);       /* back down: another change */
    assert(ch && iv == 1 * S);

    /* noise inside the band never resets; the relative band grows with power */
    s = mk(1 * S, 64 * S);
    iv = 1 * S;
    iv = adaptive_step(&s, 200.0, -1, iv, iv, 0, &ch);
    iv = adaptive_step(&s, 205.0, -1, iv, iv, 0, &ch);      /* +5 W < 3% of 200 W = 6 W */
    assert(!ch && iv == 2 * S);
    iv = adaptive_step(&s, 207.5, -1, iv, iv, 0, &ch);      /* mean 202.5 -> band 6.1 W, +5 W is fine */
    assert(!ch);

    /* the cap is honoured, also when it equals the base */
    s = mk(5 * S, 5 * S);
    iv = 5 * S;
    for (int i = 0; i < 4; i++) {
        iv = adaptive_step(&s, 30.0, -1, iv, iv, 0, &ch);
        assert(iv == 5 * S);
    }

    /* the run end clamps the interval so the last tick lands on the timeout */
    s = mk(1 * S, 600 * S);
    iv = 128 * S;
    s.have_ref = 1; s.sum_j = 25.0; s.sum_s = 1.0;
    iv = adaptive_step(&s, 25.0, -1, iv, iv, 100 * S, &ch);  /* would be 256 s */
    assert(iv == 100 * S);
    iv = adaptive_step(&s, 25.0, -1, iv, iv, 300, &ch);      /* tiny remainder -> at least 1 ms */
    assert(iv == 1000);

    /* power is averaged over the actual tick length: a long quiet tick weighs more */
    s = mk(1 * S, 600 * S);
    iv = adaptive_step(&s, 20.0, -1, 1 * S, 1 * S, 0, &ch);
    iv = adaptive_step(&s, 20.0, -1, 100 * S, 100 * S, 0, &ch);
    iv = adaptive_step(&s, 21.5, -1, 1 * S, 1 * S, 0, &ch);  /* mean ~20.0, +1.5 W > 1 W band */
    assert(ch && iv == 1 * S);

    /* residency is a second change signal; unknown residency is ignored */
    s = mk(1 * S, 600 * S);
    s.tol_deep_pct = 10;
    iv = 1 * S;
    iv = adaptive_step(&s, 25.0, 88.0, iv, iv, 0, &ch);
    iv = adaptive_step(&s, 25.1, 85.0, iv, iv, 0, &ch);  /* -3 points: inside the band */
    assert(!ch && iv == 2 * S);
    iv = adaptive_step(&s, 25.0, -1, iv, iv, 0, &ch);    /* unknown: only power counts */
    assert(!ch && iv == 4 * S);
    iv = adaptive_step(&s, 25.0, 60.0, iv, iv, 0, &ch);  /* power unchanged, residency fell 27 points */
    assert(ch && iv == 1 * S);
    s.tol_deep_pct = 0;                                  /* disabled */
    iv = adaptive_step(&s, 25.0, 10.0, iv, iv, 0, &ch);
    assert(!ch);

    printf("adaptive_core: all tests passed\n");
    return 0;
}
