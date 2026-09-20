/* libwgrender window example — window size, position, fullscreen and monitors.
 *
 *   arrows      move the window 50 pixels
 *   = / -       grow / shrink the window by 10%
 *   F           toggle fullscreen
 *   M           move to the next monitor
 *   H           hide the window for two seconds (wgr_window_set_visible)
 *   ESC         quit
 *
 * The window is resizable (WGR_WINDOW_FLAG_WINDOW_RESIZABLE); without that flag it
 * keeps its size, and =/- still change it.
 *
 * On a Wayland desktop (Linux, through XWayland) the compositor places windows:
 * moving and changing monitor report "not supported here". On web the canvas is the
 * window: resizing works, moving and other monitors don't. */
#include <stdio.h>

#include "wgr.h"

static struct {
    wgr_color_t bg;
    char status[96];
    float hidden_for; /* seconds left hidden */
} g;

static void report(const char *what, bool ok)
{
    snprintf(g.status, sizeof(g.status), "%s: %s", what, ok ? "done" : "not supported here");
}

static void init(void *user_data)
{
    (void)user_data;
    g.bg = wgr_color_rgba(24, 28, 38, 255);
    snprintf(g.status, sizeof(g.status), "press a key");
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    const wgr_keyboard_state_t kb = wgr_input_get_keyboard_state();
    const vec2_t size = wgr_window_get_screen_size();
    const vec2_t position = wgr_window_get_position();
    char line[160];
    int y = 12;

    (void)tick_fraction;
    (void)user_data;
    if (kb.keys[WGR_KEY_ESCAPE] == WGR_BUTTON_PRESSED) wgr_request_quit();
    if (kb.keys[WGR_KEY_LEFT] == WGR_BUTTON_PRESSED) report("move", wgr_window_set_position((int)position.x - 50, (int)position.y));
    if (kb.keys[WGR_KEY_RIGHT] == WGR_BUTTON_PRESSED) report("move", wgr_window_set_position((int)position.x + 50, (int)position.y));
    if (kb.keys[WGR_KEY_UP] == WGR_BUTTON_PRESSED) report("move", wgr_window_set_position((int)position.x, (int)position.y - 50));
    if (kb.keys[WGR_KEY_DOWN] == WGR_BUTTON_PRESSED) report("move", wgr_window_set_position((int)position.x, (int)position.y + 50));
    if (kb.keys[WGR_KEY_EQUAL] == WGR_BUTTON_PRESSED) {
        report("grow", wgr_window_set_size((int)(size.x * 1.1f), (int)(size.y * 1.1f)));
    }
    if (kb.keys[WGR_KEY_MINUS] == WGR_BUTTON_PRESSED) {
        report("shrink", wgr_window_set_size((int)(size.x / 1.1f), (int)(size.y / 1.1f)));
    }
    if (kb.keys[WGR_KEY_F] == WGR_BUTTON_PRESSED) report("fullscreen", wgr_window_set_fullscreen(!wgr_window_is_fullscreen()));
    if (kb.keys[WGR_KEY_H] == WGR_BUTTON_PRESSED && wgr_window_set_visible(false)) {
        g.hidden_for = 2.0f;
        report("hide for 2 s", true);
    }
    if (g.hidden_for > 0.0f && (g.hidden_for -= dt) <= 0.0f) { /* it keeps running while hidden */
        report("show", wgr_window_set_visible(true));
    }
    if (kb.keys[WGR_KEY_M] == WGR_BUTTON_PRESSED) {
        report("monitor", wgr_window_set_monitor((wgr_window_get_monitor() + 1) % wgr_window_get_monitor_count()));
    }

    wgr_render_begin();
    wgr_render_clear_background(g.bg);
    wgr_text_draw("libwgrender window   arrows: move   =/-: size   F: fullscreen   M: next monitor   H: hide", 12, y, 16,
                 WGR_COLOR_RAYWHITE);
    y += 32;
    snprintf(line, sizeof(line), "window: %.0f x %.0f at (%.0f, %.0f)   fullscreen: %s   focused: %s", size.x, size.y,
             position.x, position.y, wgr_window_is_fullscreen() ? "yes" : "no", wgr_window_is_focused() ? "yes" : "no");
    wgr_text_draw(line, 12, y, 16, WGR_COLOR_LIGHTGRAY);
    y += 24;
    wgr_text_draw(g.status, 12, y, 16, WGR_COLOR_GOLD);
    y += 32;
    for (int m = 0; m < wgr_window_get_monitor_count(); m++) {
        const vec2_t monitor_size = wgr_window_get_monitor_size(m);
        const vec2_t monitor_position = wgr_window_get_monitor_position(m);
        snprintf(line, sizeof(line), "%s monitor %d \"%s\": %.0f x %.0f at (%.0f, %.0f)",
                 m == wgr_window_get_monitor() ? ">" : " ", m, wgr_window_get_monitor_name(m), monitor_size.x,
                 monitor_size.y, monitor_position.x, monitor_position.y);
        wgr_text_draw(line, 12, y, 16, WGR_COLOR_LIGHTGRAY);
        y += 22;
    }
    wgr_render_end();
}

int main(void)
{
    wgr_init_values(900, 400, "libwgrender window", WGR_WINDOW_FLAG_WINDOW_RESIZABLE);
    wgr_set_init(init, NULL);
    wgr_set_frame(frame, NULL);
    return wgr_run();
}
