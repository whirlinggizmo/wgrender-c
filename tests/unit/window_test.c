/* Window and monitor API (docs/PLAN-window.md) on the headless platform: one
 * virtual monitor the size of the framebuffer; no position, no fullscreen. */
#include <string.h>

#include "wgr_logger.h"
#include "wgr_window.h"
#include "test.h"
#include "tests.h"

void test_window_headless(void)
{
    const vec2_t original = wgr_window_get_screen_size(); /* other tests pick in it */

    wgr_logger_set_level(WGR_LOGGER_LEVEL_ERROR); /* unsupported calls warn once */

    CHECK(wgr_window_set_size(320, 200));
    CHECK(wgr_window_get_screen_size().x == 320.0f && wgr_window_get_screen_size().y == 200.0f);
    CHECK(!wgr_window_set_size(0, 200));

    CHECK(!wgr_window_set_position(10, 20));
    CHECK(wgr_window_get_position().x == 0.0f && wgr_window_get_position().y == 0.0f);
    CHECK(!wgr_window_set_fullscreen(true));
    CHECK(!wgr_window_is_fullscreen());
    CHECK(wgr_window_is_focused());

    /* shown by default; hiding and showing are remembered */
    CHECK(wgr_window_is_visible());
    CHECK(wgr_window_set_visible(false));
    CHECK(!wgr_window_is_visible());
    CHECK(wgr_window_set_visible(true));
    CHECK(wgr_window_is_visible());

    CHECK(wgr_window_get_monitor_count() == 1);
    CHECK(wgr_window_get_monitor() == 0);
    CHECK(wgr_window_set_monitor(0));
    CHECK(!wgr_window_set_monitor(1));
    CHECK(!wgr_window_set_monitor(-1));
    CHECK(wgr_window_get_monitor_size(0).x == 320.0f && wgr_window_get_monitor_size(0).y == 200.0f);
    CHECK(wgr_window_get_monitor_size(1).x == 0.0f);
    CHECK(wgr_window_get_monitor_position(0).x == 0.0f);
    CHECK(strcmp(wgr_window_get_monitor_name(0), "headless") == 0);

    CHECK(wgr_window_set_size((int)original.x, (int)original.y));
    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
}
