/* libsk tick example — fixed-rate simulation (tick) vs rendering (frame).
 *
 * A deliberately slow 10 Hz tick moves two squares at the same speed:
 *   - the top one is drawn at its latest tick position, so it visibly steps;
 *   - the bottom one is drawn between its last two tick positions using
 *     tick_fraction, so it moves smoothly at any frame rate.
 * Press SPACE: presses counted inside the tick and inside the frame always match,
 * because each press is seen by exactly one tick. ESC quits. */
#include <stddef.h>
#include <stdio.h>

#include "sk.h"

enum {
    SCREEN_WIDTH = 800,
    SCREEN_HEIGHT = 450,
    TICK_HZ = 10,
    SQUARE = 40,
    LEFT = 40,
    RIGHT = SCREEN_WIDTH - 80,
};

static const float SPEED = 240.0f; /* pixels per second */

static struct {
    float prev_x, x; /* simulation state: the last two tick positions */
    int ticks;
    int tick_presses;
    int frame_presses;
    sk_color_t bg;
} g;

static void tick(float dt, void *user_data)
{
    (void)user_data;
    g.prev_x = g.x;
    g.x += SPEED * dt;
    if (g.x > RIGHT) {
        g.x = LEFT;
        g.prev_x = g.x; /* don't interpolate across the wrap */
    }
    g.ticks++;

    if (sk_input_get_key(SK_KEY_SPACE) == SK_BUTTON_PRESSED) {
        g.tick_presses++;
    }
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    (void)user_data;
    sk_keyboard_state_t kb = sk_input_get_keyboard_state();
    char line[128];

    if (kb.keys[SK_KEY_SPACE] == SK_BUTTON_PRESSED) {
        g.frame_presses++;
    }
    if (kb.keys[SK_KEY_ESCAPE] == SK_BUTTON_PRESSED) {
        sk_request_quit();
    }

    sk_render_begin();
    sk_render_clear_background(g.bg);

    sk_text_draw("libsk tick: 10 Hz simulation, rendered every frame", 20, 20, 20, SK_COLOR_RAYWHITE);
    snprintf(line, sizeof(line), "frame dt %.4f s   tick_fraction %.2f   ticks %d", dt, tick_fraction, g.ticks);
    sk_text_draw(line, 20, 50, 16, SK_COLOR_LIGHTGRAY);
    snprintf(line, sizeof(line), "SPACE presses: tick %d, frame %d", g.tick_presses, g.frame_presses);
    sk_text_draw(line, 20, 74, 16, SK_COLOR_LIGHTGRAY);

    sk_text_draw("raw tick position", 20, 130, 16, SK_COLOR_GRAY);
    sk_shape2d_draw_rectangle((int)g.x, 155, SQUARE, SQUARE, SK_COLOR_ORANGE);

    sk_text_draw("interpolated with tick_fraction", 20, 250, 16, SK_COLOR_GRAY);
    float smooth_x = g.prev_x + (g.x - g.prev_x) * tick_fraction;
    sk_shape2d_draw_rectangle((int)smooth_x, 275, SQUARE, SQUARE, SK_COLOR_SKYBLUE);

    sk_text_draw_fps(20, SCREEN_HEIGHT - 30);
    sk_render_end();
}

static void init(void *user_data)
{
    (void)user_data;
    g.bg = sk_color_rgba(24, 26, 34, 255);
    g.x = g.prev_x = LEFT;
}

int main(void)
{
    sk_init_values(SCREEN_WIDTH, SCREEN_HEIGHT, "libsk tick", SK_WINDOW_FLAG_MSAA_4X_HINT | SK_WINDOW_FLAG_WINDOW_RESIZABLE);
    sk_set_init(init, NULL);
    sk_set_tick(tick, NULL, TICK_HZ);
    sk_set_frame(frame, NULL);
    return sk_run();
}
