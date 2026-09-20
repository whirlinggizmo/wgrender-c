/* libwgrender gamepad example — every connected pad, live (wgr_input.h).
 *
 * Up to four pads side by side: the name, both sticks (the dot is where the stick is,
 * after the dead zone; the ring is its reach), the triggers as bars, and the buttons
 * laid out like an Xbox-style pad, lit while held and flashed on the frame they're
 * pressed. SOUTH on a pad cycles the dead zone. In a browser, press a button on the
 * pad first: pages only see gamepads after that. */
#include <stdio.h>

#include "wgr.h"

static const float DEADZONES[] = {0.15f, 0.0f, 0.3f};
static int g_deadzone;

static wgr_color_t button_color(int pad, wgr_gamepad_button_t button)
{
    switch (wgr_input_get_gamepad_button(pad, button)) {
        case WGR_BUTTON_PRESSED: return WGR_COLOR_WHITE;
        case WGR_BUTTON_DOWN: return WGR_COLOR_GOLD;
        default: return wgr_color_rgba(60, 66, 80, 255);
    }
}

static void draw_button(int pad, wgr_gamepad_button_t button, float x, float y, float r)
{
    wgr_shape2d_draw_circle(x, y, r, button_color(pad, button));
}

static void draw_stick(int pad, wgr_gamepad_axis_t x_axis, wgr_gamepad_button_t click, float cx, float cy)
{
    const float reach = 34.0f;
    wgr_shape2d_draw_circle_lines(cx, cy, reach, wgr_color_rgba(90, 98, 118, 255));
    if (wgr_input_get_gamepad_button(pad, click) != WGR_BUTTON_UP) {
        wgr_shape2d_draw_circle(cx, cy, reach, wgr_color_rgba(60, 66, 80, 255));
    }
    wgr_shape2d_draw_circle(cx + wgr_input_get_gamepad_axis(pad, x_axis) * reach,
                           cy + wgr_input_get_gamepad_axis(pad, (wgr_gamepad_axis_t)(x_axis + 1)) * reach, 9.0f,
                           WGR_COLOR_SKYBLUE);
}

static void draw_trigger(int pad, wgr_gamepad_axis_t axis, float x, float y)
{
    const float height = 60.0f, value = wgr_input_get_gamepad_axis(pad, axis);
    wgr_shape2d_draw_rectangle_lines(x, y, 16.0f, height, wgr_color_rgba(90, 98, 118, 255));
    wgr_shape2d_draw_rectangle(x, y + height * (1.0f - value), 16.0f, height * value, WGR_COLOR_ORANGE);
}

static void draw_pad(int pad, float x, float y)
{
    char line[96];
    snprintf(line, sizeof(line), "pad %d", pad);
    wgr_text_draw(line, (int)x, (int)y, 20, WGR_COLOR_RAYWHITE);
    if (!wgr_input_is_gamepad_connected(pad)) {
        wgr_text_draw("not connected", (int)x, (int)y + 26, 16, WGR_COLOR_GRAY);
        return;
    }
    snprintf(line, sizeof(line), "%.40s", wgr_input_get_gamepad_name(pad));
    wgr_text_draw(line, (int)x, (int)y + 26, 14, WGR_COLOR_LIGHTGRAY);

    /* shoulders and triggers */
    draw_trigger(pad, WGR_GAMEPAD_AXIS_LEFT_TRIGGER, x + 10.0f, y + 56.0f);
    draw_trigger(pad, WGR_GAMEPAD_AXIS_RIGHT_TRIGGER, x + 274.0f, y + 56.0f);
    wgr_shape2d_draw_rectangle(x + 34.0f, y + 60.0f, 60.0f, 14.0f, button_color(pad, WGR_GAMEPAD_BUTTON_LEFT_BUMPER));
    wgr_shape2d_draw_rectangle(x + 206.0f, y + 60.0f, 60.0f, 14.0f,
                              button_color(pad, WGR_GAMEPAD_BUTTON_RIGHT_BUMPER));

    /* sticks, d-pad, face buttons, middle buttons */
    draw_stick(pad, WGR_GAMEPAD_AXIS_LEFT_X, WGR_GAMEPAD_BUTTON_LEFT_STICK, x + 70.0f, y + 130.0f);
    draw_stick(pad, WGR_GAMEPAD_AXIS_RIGHT_X, WGR_GAMEPAD_BUTTON_RIGHT_STICK, x + 190.0f, y + 210.0f);
    wgr_shape2d_draw_rectangle(x + 102.0f, y + 180.0f, 18.0f, 18.0f, button_color(pad, WGR_GAMEPAD_BUTTON_DPAD_UP));
    wgr_shape2d_draw_rectangle(x + 102.0f, y + 220.0f, 18.0f, 18.0f, button_color(pad, WGR_GAMEPAD_BUTTON_DPAD_DOWN));
    wgr_shape2d_draw_rectangle(x + 82.0f, y + 200.0f, 18.0f, 18.0f, button_color(pad, WGR_GAMEPAD_BUTTON_DPAD_LEFT));
    wgr_shape2d_draw_rectangle(x + 122.0f, y + 200.0f, 18.0f, 18.0f,
                              button_color(pad, WGR_GAMEPAD_BUTTON_DPAD_RIGHT));
    draw_button(pad, WGR_GAMEPAD_BUTTON_NORTH, x + 240.0f, y + 106.0f, 12.0f);
    draw_button(pad, WGR_GAMEPAD_BUTTON_SOUTH, x + 240.0f, y + 154.0f, 12.0f);
    draw_button(pad, WGR_GAMEPAD_BUTTON_WEST, x + 216.0f, y + 130.0f, 12.0f);
    draw_button(pad, WGR_GAMEPAD_BUTTON_EAST, x + 264.0f, y + 130.0f, 12.0f);
    draw_button(pad, WGR_GAMEPAD_BUTTON_BACK, x + 126.0f, y + 130.0f, 8.0f);
    draw_button(pad, WGR_GAMEPAD_BUTTON_GUIDE, x + 150.0f, y + 110.0f, 10.0f);
    draw_button(pad, WGR_GAMEPAD_BUTTON_START, x + 174.0f, y + 130.0f, 8.0f);

    snprintf(line, sizeof(line), "L %+.2f %+.2f  R %+.2f %+.2f", wgr_input_get_gamepad_axis(pad, WGR_GAMEPAD_AXIS_LEFT_X),
             wgr_input_get_gamepad_axis(pad, WGR_GAMEPAD_AXIS_LEFT_Y),
             wgr_input_get_gamepad_axis(pad, WGR_GAMEPAD_AXIS_RIGHT_X),
             wgr_input_get_gamepad_axis(pad, WGR_GAMEPAD_AXIS_RIGHT_Y));
    wgr_text_draw(line, (int)x, (int)y + 262, 14, WGR_COLOR_LIGHTGRAY);
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    char line[96];
    (void)dt;
    (void)tick_fraction;
    (void)user_data;

    for (int pad = 0; pad < WGR_INPUT_MAX_GAMEPADS; pad++) {
        if (wgr_input_get_gamepad_button(pad, WGR_GAMEPAD_BUTTON_SOUTH) == WGR_BUTTON_PRESSED) {
            g_deadzone = (g_deadzone + 1) % (int)(sizeof(DEADZONES) / sizeof(DEADZONES[0]));
            wgr_input_set_gamepad_deadzone(DEADZONES[g_deadzone]);
        }
    }

    wgr_render_begin();
    wgr_render_clear_background(wgr_color_rgba(20, 22, 30, 255));
    wgr_text_draw("libwgrender + sokol — gamepads", 12, 36, 24, WGR_COLOR_RAYWHITE);
    snprintf(line, sizeof(line), "dead zone %.2f (SOUTH changes it)   web: press a pad button first",
             DEADZONES[g_deadzone]);
    wgr_text_draw(line, 12, 70, 16, WGR_COLOR_LIGHTGRAY);
    for (int pad = 0; pad < WGR_INPUT_MAX_GAMEPADS; pad++) {
        draw_pad(pad, 20.0f + (float)(pad % 2) * 320.0f, 110.0f + (float)(pad / 2) * 300.0f);
    }
    wgr_render_end();

    if (wgr_input_get_key(WGR_KEY_ESCAPE) == WGR_BUTTON_PRESSED) {
        wgr_request_quit();
    }
}

int main(void)
{
    wgr_init_values(680, 720, "libwgrender gamepad", WGR_WINDOW_FLAG_MSAA_4X_HINT | WGR_WINDOW_FLAG_WINDOW_RESIZABLE);
    wgr_set_frame(frame, NULL);
    return wgr_run();
}
