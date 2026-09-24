/* libwgrender hello example — window, clear, 2D shapes, text, input.
 *
 * Note the callback loop model (sokol_app owns the loop): configure with
 * wgr_init_values(), register a frame function, then wgr_run(). */
#include <stddef.h>

#include "wgr.h"

static void frame(float dt, float tick_fraction, void *user_data)
{
    (void)user_data;

    wgr_mouse_state_t mouse = wgr_input_get_mouse_state();

    wgr_render_begin_frame();
    wgr_render_clear_background(WGR_COLOR_RAYWHITE);

    /* filled + outlined rectangles */
    wgr_shape2d_draw_rectangle(40, 40, 200, 120, WGR_COLOR_SKYBLUE);
    wgr_shape2d_draw_rectangle_lines(40, 40, 200, 120, WGR_COLOR_DARKBLUE);

    /* line + triangle + circles */
    wgr_shape2d_draw_line(40, 200, 240, 320, WGR_COLOR_RED);
    wgr_shape2d_draw_triangle(320, 60, 280, 180, 360, 180, WGR_COLOR_GOLD);
    wgr_shape2d_draw_circle(440, 120, 60.0f, WGR_COLOR_PURPLE);
    wgr_shape2d_draw_circle_lines(440, 120, 60.0f, WGR_COLOR_BLACK);

    /* a marker that follows the mouse */
    wgr_shape2d_draw_circle(mouse.x, mouse.y, 8.0f, WGR_COLOR_MAROON);

    /* text */
    wgr_text_draw("libwgrender + sokol", 40, 360, 32, WGR_COLOR_DARKGRAY);
    wgr_text_draw("press ESC to quit", 40, 410, 16, WGR_COLOR_GRAY);

    wgr_text_draw_fps(40, 12);

    wgr_render_end_frame();

    wgr_keyboard_state_t kb = wgr_input_get_keyboard_state();
    if (kb.keys[WGR_KEY_ESCAPE] == WGR_BUTTON_PRESSED) {
        wgr_request_quit();
    }
}

int main(void)
{
    wgr_init_values(800, 600, "libwgrender hello", WGR_WINDOW_FLAG_MSAA_4X_HINT | WGR_WINDOW_FLAG_WINDOW_RESIZABLE);
    wgr_set_frame(frame, NULL);
    return wgr_run();
}
