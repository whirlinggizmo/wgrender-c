/* libsk hello example — window, clear, 2D shapes, text, input.
 *
 * Note the callback loop model (sokol_app owns the loop): configure with
 * sk_init_values(), register a frame function, then sk_run(). */
#include <stddef.h>

#include "sk.h"

static void frame(float dt, float tick_fraction, void *user_data)
{
    (void)user_data;

    sk_mouse_state_t mouse = sk_input_get_mouse_state();

    sk_render_begin();
    sk_render_clear_background(SK_COLOR_RAYWHITE);

    /* filled + outlined rectangles */
    sk_shape2d_draw_rectangle(40, 40, 200, 120, SK_COLOR_SKYBLUE);
    sk_shape2d_draw_rectangle_lines(40, 40, 200, 120, SK_COLOR_DARKBLUE);

    /* line + triangle + circles */
    sk_shape2d_draw_line(40, 200, 240, 320, SK_COLOR_RED);
    sk_shape2d_draw_triangle(320, 60, 280, 180, 360, 180, SK_COLOR_GOLD);
    sk_shape2d_draw_circle(440, 120, 60.0f, SK_COLOR_PURPLE);
    sk_shape2d_draw_circle_lines(440, 120, 60.0f, SK_COLOR_BLACK);

    /* a marker that follows the mouse */
    sk_shape2d_draw_circle(mouse.x, mouse.y, 8.0f, SK_COLOR_MAROON);

    /* text */
    sk_text_draw("libsk + sokol", 40, 360, 32, SK_COLOR_DARKGRAY);
    sk_text_draw("press ESC to quit", 40, 410, 16, SK_COLOR_GRAY);

    sk_text_draw_fps(40, 12);

    sk_render_end();

    sk_keyboard_state_t kb = sk_input_get_keyboard_state();
    if (kb.keys[SK_KEY_ESCAPE] == SK_BUTTON_PRESSED) {
        sk_request_quit();
    }
}

int main(void)
{
    sk_init_values(800, 600, "libsk hello", SK_WINDOW_FLAG_MSAA_4X_HINT);
    sk_set_frame(frame, NULL);
    return sk_run();
}
