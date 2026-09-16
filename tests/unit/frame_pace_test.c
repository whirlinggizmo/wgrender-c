#include "internal/sk_frame_pace.h"
#include "test.h"
#include "tests.h"

#define EPS 1e-9

void test_frame_pace_unpaced(void)
{
    sk_frame_pace_t pace;
    sk_frame_pace_set_fps(&pace, 0);
    CHECK(!sk_frame_pace_enabled(&pace));
    CHECK_NEAR(sk_frame_pace_wait(&pace, 123.0), 0, EPS);
    sk_frame_pace_mark(&pace, 123.0);
    CHECK_NEAR(sk_frame_pace_wait(&pace, 123.0), 0, EPS);

    sk_frame_pace_set_fps(&pace, -1);
    CHECK(!sk_frame_pace_enabled(&pace));
}

void test_frame_pace_schedule(void)
{
    sk_frame_pace_t pace;
    sk_frame_pace_set_fps(&pace, 10); /* period 0.1 s */
    CHECK(sk_frame_pace_enabled(&pace));

    /* the first frame is due immediately */
    CHECK_NEAR(sk_frame_pace_wait(&pace, 5.0), 0, EPS);
    sk_frame_pace_mark(&pace, 5.0);
    CHECK_NEAR(sk_frame_pace_wait(&pace, 5.03), 0.07, EPS);

    /* a slightly late frame keeps the cadence: the next deadline is 5.2, not 5.21 */
    sk_frame_pace_mark(&pace, 5.11);
    CHECK_NEAR(sk_frame_pace_wait(&pace, 5.11), 0.09, EPS);

    /* a slightly early frame (web tolerance) also keeps the cadence */
    sk_frame_pace_mark(&pace, 5.19);
    CHECK_NEAR(sk_frame_pace_wait(&pace, 5.19), 0.11, EPS);

    /* more than a period late (a stall): resync instead of bursting */
    sk_frame_pace_mark(&pace, 7.0);
    CHECK_NEAR(sk_frame_pace_wait(&pace, 7.0), 0.1, EPS);

    /* changing the rate restarts the schedule */
    sk_frame_pace_set_fps(&pace, 50);
    CHECK_NEAR(sk_frame_pace_wait(&pace, 7.01), 0, EPS);
    sk_frame_pace_mark(&pace, 7.01);
    CHECK_NEAR(sk_frame_pace_wait(&pace, 7.01), 0.02, EPS);
}

void test_frame_pace_web_skip(void)
{
    /* 30 fps on a 60 Hz browser: frames run when due within half a display frame,
     * which is every other refresh */
    const double refresh = 1.0 / 60.0, tolerance = 0.5 * refresh;
    sk_frame_pace_t pace;
    int ran = 0;
    sk_frame_pace_set_fps(&pace, 30);
    for (int i = 0; i < 600; i++) { /* 10 seconds of refreshes */
        double now = 1.0 + i * refresh;
        if (sk_frame_pace_wait(&pace, now) <= tolerance) {
            sk_frame_pace_mark(&pace, now);
            ran++;
        }
    }
    CHECK(ran >= 299 && ran <= 301);
}
