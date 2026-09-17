/* Window and monitor API (docs/PLAN-window.md) on the headless platform: one
 * virtual monitor the size of the framebuffer; no position, no fullscreen. */
#include <string.h>

#include "sk_logger.h"
#include "sk_window.h"
#include "test.h"
#include "tests.h"

void test_window_headless(void)
{
    const vec2_t original = sk_window_get_screen_size(); /* other tests pick in it */

    sk_logger_set_level(SK_LOGGER_LEVEL_ERROR); /* unsupported calls warn once */

    CHECK(sk_window_set_size(320, 200));
    CHECK(sk_window_get_screen_size().x == 320.0f && sk_window_get_screen_size().y == 200.0f);
    CHECK(!sk_window_set_size(0, 200));

    CHECK(!sk_window_set_position(10, 20));
    CHECK(sk_window_get_position().x == 0.0f && sk_window_get_position().y == 0.0f);
    CHECK(!sk_window_set_fullscreen(true));
    CHECK(!sk_window_is_fullscreen());
    CHECK(sk_window_is_focused());

    CHECK(sk_window_get_monitor_count() == 1);
    CHECK(sk_window_get_monitor() == 0);
    CHECK(sk_window_set_monitor(0));
    CHECK(!sk_window_set_monitor(1));
    CHECK(!sk_window_set_monitor(-1));
    CHECK(sk_window_get_monitor_size(0).x == 320.0f && sk_window_get_monitor_size(0).y == 200.0f);
    CHECK(sk_window_get_monitor_size(1).x == 0.0f);
    CHECK(sk_window_get_monitor_position(0).x == 0.0f);
    CHECK(strcmp(sk_window_get_monitor_name(0), "headless") == 0);

    CHECK(sk_window_set_size((int)original.x, (int)original.y));
    sk_logger_set_level(SK_LOGGER_LEVEL_INFO);
}
