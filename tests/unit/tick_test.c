#include "internal/wgr_tick_clock_internal.h"
#include "test.h"
#include "tests.h"

#define EPS 1e-6

void test_tick_clock_rate(void)
{
    wgri_tick_clock_t clock;

    wgri_tick_clock_set_rate(&clock, 0);
    CHECK(!wgri_tick_clock_enabled(&clock));
    CHECK(wgri_tick_clock_advance(&clock, 1.0, WGRI_MAX_TICKS_PER_FRAME) == 0);
    CHECK_NEAR(wgri_tick_clock_fraction(&clock), 0, EPS);

    /* frames exactly one step apart tick exactly once each, despite rounding */
    wgri_tick_clock_set_rate(&clock, 60);
    int total = 0, per_frame_ok = 1;
    for (int i = 0; i < 600; i++) {
        int n = wgri_tick_clock_advance(&clock, 1.0 / 60.0, WGRI_MAX_TICKS_PER_FRAME);
        per_frame_ok &= (n == 1);
        total += n;
    }
    CHECK(per_frame_ok);
    CHECK(total == 600);

    /* 30 Hz ticks under 144 Hz frames: 30 ticks per second, at most one per frame,
     * fraction always in [0, 1) */
    wgri_tick_clock_set_rate(&clock, 30);
    total = 0;
    int max_per_frame = 0, fraction_ok = 1;
    for (int i = 0; i < 144; i++) {
        int n = wgri_tick_clock_advance(&clock, 1.0 / 144.0, WGRI_MAX_TICKS_PER_FRAME);
        float f = wgri_tick_clock_fraction(&clock);
        total += n;
        max_per_frame = n > max_per_frame ? n : max_per_frame;
        fraction_ok &= (f >= 0.0f && f < 1.0f);
    }
    CHECK(total >= 29 && total <= 30);
    CHECK(max_per_frame == 1);
    CHECK(fraction_ok);

    /* negative elapsed is ignored */
    wgri_tick_clock_set_rate(&clock, 10);
    CHECK(wgri_tick_clock_advance(&clock, -5.0, WGRI_MAX_TICKS_PER_FRAME) == 0);
    CHECK_NEAR(wgri_tick_clock_fraction(&clock), 0, EPS);
}

void test_tick_clock_stall(void)
{
    wgri_tick_clock_t clock;
    wgri_tick_clock_set_rate(&clock, 10); /* step 0.1 s */

    /* partial progress shows up as the fraction */
    CHECK(wgri_tick_clock_advance(&clock, 0.025, WGRI_MAX_TICKS_PER_FRAME) == 0);
    CHECK_NEAR(wgri_tick_clock_fraction(&clock), 0.25, 1e-4);

    /* a 2.35 s stall (23 steps due, 0.025 already accumulated): run the maximum of
     * 5, drop the backlog, keep the phase (2.375 s total -> 0.075 into a step) */
    CHECK(wgri_tick_clock_advance(&clock, 2.35, WGRI_MAX_TICKS_PER_FRAME) == 5);
    CHECK_NEAR(wgri_tick_clock_fraction(&clock), 0.75, 1e-4);

    /* and normal ticking resumes */
    CHECK(wgri_tick_clock_advance(&clock, 0.1, WGRI_MAX_TICKS_PER_FRAME) == 1);

    /* changing the rate restarts the accumulator */
    wgri_tick_clock_set_rate(&clock, 20);
    CHECK_NEAR(wgri_tick_clock_fraction(&clock), 0, EPS);
}
