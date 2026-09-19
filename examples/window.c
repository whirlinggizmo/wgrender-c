/* libsk window example — window size, position, fullscreen and monitors.
 *
 *   arrows      move the window 50 pixels
 *   = / -       grow / shrink the window by 10%
 *   F           toggle fullscreen
 *   M           move to the next monitor
 *   H           hide the window for two seconds (sk_window_set_visible)
 *   ESC         quit
 *
 * The window is resizable (SK_WINDOW_FLAG_WINDOW_RESIZABLE); without that flag it
 * keeps its size, and =/- still change it.
 *
 * Under XWayland (Linux on a Wayland desktop) the compositor usually ignores moves.
 * On web the canvas is the window: resizing works, moving and other monitors don't. */
#include <stdio.h>

#include "sk.h"

static struct {
    sk_color_t bg;
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
    g.bg = sk_color_rgba(24, 28, 38, 255);
    snprintf(g.status, sizeof(g.status), "press a key");
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    const sk_keyboard_state_t kb = sk_input_get_keyboard_state();
    const vec2_t size = sk_window_get_screen_size();
    const vec2_t position = sk_window_get_position();
    char line[160];
    int y = 12;

    (void)tick_fraction;
    (void)user_data;
    if (kb.keys[SK_KEY_ESCAPE] == SK_BUTTON_PRESSED) sk_request_quit();
    if (kb.keys[SK_KEY_LEFT] == SK_BUTTON_PRESSED) report("move", sk_window_set_position((int)position.x - 50, (int)position.y));
    if (kb.keys[SK_KEY_RIGHT] == SK_BUTTON_PRESSED) report("move", sk_window_set_position((int)position.x + 50, (int)position.y));
    if (kb.keys[SK_KEY_UP] == SK_BUTTON_PRESSED) report("move", sk_window_set_position((int)position.x, (int)position.y - 50));
    if (kb.keys[SK_KEY_DOWN] == SK_BUTTON_PRESSED) report("move", sk_window_set_position((int)position.x, (int)position.y + 50));
    if (kb.keys[SK_KEY_EQUAL] == SK_BUTTON_PRESSED) {
        report("grow", sk_window_set_size((int)(size.x * 1.1f), (int)(size.y * 1.1f)));
    }
    if (kb.keys[SK_KEY_MINUS] == SK_BUTTON_PRESSED) {
        report("shrink", sk_window_set_size((int)(size.x / 1.1f), (int)(size.y / 1.1f)));
    }
    if (kb.keys[SK_KEY_F] == SK_BUTTON_PRESSED) report("fullscreen", sk_window_set_fullscreen(!sk_window_is_fullscreen()));
    if (kb.keys[SK_KEY_H] == SK_BUTTON_PRESSED && sk_window_set_visible(false)) {
        g.hidden_for = 2.0f;
        report("hide for 2 s", true);
    }
    if (g.hidden_for > 0.0f && (g.hidden_for -= dt) <= 0.0f) { /* it keeps running while hidden */
        report("show", sk_window_set_visible(true));
    }
    if (kb.keys[SK_KEY_M] == SK_BUTTON_PRESSED) {
        report("monitor", sk_window_set_monitor((sk_window_get_monitor() + 1) % sk_window_get_monitor_count()));
    }

    sk_render_begin();
    sk_render_clear_background(g.bg);
    sk_text_draw("libsk window   arrows: move   =/-: size   F: fullscreen   M: next monitor   H: hide", 12, y, 16,
                 SK_COLOR_RAYWHITE);
    y += 32;
    snprintf(line, sizeof(line), "window: %.0f x %.0f at (%.0f, %.0f)   fullscreen: %s   focused: %s", size.x, size.y,
             position.x, position.y, sk_window_is_fullscreen() ? "yes" : "no", sk_window_is_focused() ? "yes" : "no");
    sk_text_draw(line, 12, y, 16, SK_COLOR_LIGHTGRAY);
    y += 24;
    sk_text_draw(g.status, 12, y, 16, SK_COLOR_GOLD);
    y += 32;
    for (int m = 0; m < sk_window_get_monitor_count(); m++) {
        const vec2_t monitor_size = sk_window_get_monitor_size(m);
        const vec2_t monitor_position = sk_window_get_monitor_position(m);
        snprintf(line, sizeof(line), "%s monitor %d \"%s\": %.0f x %.0f at (%.0f, %.0f)",
                 m == sk_window_get_monitor() ? ">" : " ", m, sk_window_get_monitor_name(m), monitor_size.x,
                 monitor_size.y, monitor_position.x, monitor_position.y);
        sk_text_draw(line, 12, y, 16, SK_COLOR_LIGHTGRAY);
        y += 22;
    }
    sk_render_end();
}

int main(void)
{
    sk_init_values(900, 400, "libsk window", SK_WINDOW_FLAG_WINDOW_RESIZABLE);
    sk_set_init(init, NULL);
    sk_set_frame(frame, NULL);
    return sk_run();
}
