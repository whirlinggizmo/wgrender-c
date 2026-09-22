/* Window and monitor API (docs/PLAN-window.md) on the headless platform: one
 * virtual monitor the size of the framebuffer; no position, no fullscreen. */
#include <string.h>

#include "wgr.h"
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
    CHECK(!wgr_window_has_fullscreen()); /* headless: nothing to ask for */
    CHECK(!wgr_window_request_fullscreen(true));
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

/* wgr_get_renderer / wgr_has_threads: what a program asks before claiming something
 * about the host (examples/loading.c says which way it is decoding). */
void test_runtime_capabilities(void)
{
    const char *renderer = wgr_get_renderer();
    CHECK(renderer != NULL && *renderer != '\0');
    /* the test build is headless, and says so rather than naming sokol's dummy backend */
    CHECK(strcmp(renderer, "headless") == 0);

    /* a bool, and on desktop threads are always there; the web build decides at
       compile time (WEB_THREADS) and the host decides whether they can start */
    const bool threads = wgr_has_threads();
    CHECK(threads == true || threads == false);
#ifndef __EMSCRIPTEN__
    CHECK(threads); /* desktop has pthreads */
#endif
}
