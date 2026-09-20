/* libwgrender tick example — fixed-rate simulation (tick) vs rendering (frame).
 *
 * A deliberately slow 10 Hz tick moves two squares at the same speed:
 *   - the top one is drawn at its latest tick position, so it visibly steps;
 *   - the bottom one is drawn between its last two tick positions using
 *     tick_fraction, so it moves smoothly at any frame rate.
 * Press SPACE: presses counted inside the tick and inside the frame always match,
 * because each press is seen by exactly one tick. ESC quits. */
#include <stddef.h>
#include <stdio.h>

#include "wgr.h"

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
    wgr_color_t bg;
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

    if (wgr_input_get_key(WGR_KEY_SPACE) == WGR_BUTTON_PRESSED) {
        g.tick_presses++;
    }
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    (void)user_data;
    wgr_keyboard_state_t kb = wgr_input_get_keyboard_state();
    char line[128];

    if (kb.keys[WGR_KEY_SPACE] == WGR_BUTTON_PRESSED) {
        g.frame_presses++;
    }
    if (kb.keys[WGR_KEY_ESCAPE] == WGR_BUTTON_PRESSED) {
        wgr_request_quit();
    }

    wgr_render_begin();
    wgr_render_clear_background(g.bg);

    wgr_text_draw("libwgrender tick: 10 Hz simulation, rendered every frame", 20, 20, 20, WGR_COLOR_RAYWHITE);
    snprintf(line, sizeof(line), "frame dt %.4f s   tick_fraction %.2f   ticks %d", dt, tick_fraction, g.ticks);
    wgr_text_draw(line, 20, 50, 16, WGR_COLOR_LIGHTGRAY);
    snprintf(line, sizeof(line), "SPACE presses: tick %d, frame %d", g.tick_presses, g.frame_presses);
    wgr_text_draw(line, 20, 74, 16, WGR_COLOR_LIGHTGRAY);

    wgr_text_draw("raw tick position", 20, 130, 16, WGR_COLOR_GRAY);
    wgr_shape2d_draw_rectangle((int)g.x, 155, SQUARE, SQUARE, WGR_COLOR_ORANGE);

    wgr_text_draw("interpolated with tick_fraction", 20, 250, 16, WGR_COLOR_GRAY);
    float smooth_x = g.prev_x + (g.x - g.prev_x) * tick_fraction;
    wgr_shape2d_draw_rectangle((int)smooth_x, 275, SQUARE, SQUARE, WGR_COLOR_SKYBLUE);

    wgr_text_draw_fps(20, SCREEN_HEIGHT - 30);
    wgr_render_end();
}

static void init(void *user_data)
{
    (void)user_data;
    g.bg = wgr_color_rgba(24, 26, 34, 255);
    g.x = g.prev_x = LEFT;
}

int main(void)
{
    wgr_init_values(SCREEN_WIDTH, SCREEN_HEIGHT, "libwgrender tick", WGR_WINDOW_FLAG_MSAA_4X_HINT | WGR_WINDOW_FLAG_WINDOW_RESIZABLE);
    wgr_set_init(init, NULL);
    wgr_set_tick(tick, NULL, TICK_HZ);
    wgr_set_frame(frame, NULL);
    return wgr_run();
}
