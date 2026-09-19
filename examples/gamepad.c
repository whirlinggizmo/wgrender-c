/* libsk gamepad example — every connected pad, live (sk_input.h).
 *
 * Up to four pads side by side: the name, both sticks (the dot is where the stick is,
 * after the dead zone; the ring is its reach), the triggers as bars, and the buttons
 * laid out like an Xbox-style pad, lit while held and flashed on the frame they're
 * pressed. SOUTH on a pad cycles the dead zone. In a browser, press a button on the
 * pad first: pages only see gamepads after that. */
#include <stdio.h>

#include "sk.h"

static const float DEADZONES[] = {0.15f, 0.0f, 0.3f};
static int g_deadzone;

static sk_color_t button_color(int pad, sk_gamepad_button_t button)
{
    switch (sk_input_get_gamepad_button(pad, button)) {
        case SK_BUTTON_PRESSED: return SK_COLOR_WHITE;
        case SK_BUTTON_DOWN: return SK_COLOR_GOLD;
        default: return sk_color_rgba(60, 66, 80, 255);
    }
}

static void draw_button(int pad, sk_gamepad_button_t button, float x, float y, float r)
{
    sk_shape2d_draw_circle(x, y, r, button_color(pad, button));
}

static void draw_stick(int pad, sk_gamepad_axis_t x_axis, sk_gamepad_button_t click, float cx, float cy)
{
    const float reach = 34.0f;
    sk_shape2d_draw_circle_lines(cx, cy, reach, sk_color_rgba(90, 98, 118, 255));
    if (sk_input_get_gamepad_button(pad, click) != SK_BUTTON_UP) {
        sk_shape2d_draw_circle(cx, cy, reach, sk_color_rgba(60, 66, 80, 255));
    }
    sk_shape2d_draw_circle(cx + sk_input_get_gamepad_axis(pad, x_axis) * reach,
                           cy + sk_input_get_gamepad_axis(pad, (sk_gamepad_axis_t)(x_axis + 1)) * reach, 9.0f,
                           SK_COLOR_SKYBLUE);
}

static void draw_trigger(int pad, sk_gamepad_axis_t axis, float x, float y)
{
    const float height = 60.0f, value = sk_input_get_gamepad_axis(pad, axis);
    sk_shape2d_draw_rectangle_lines(x, y, 16.0f, height, sk_color_rgba(90, 98, 118, 255));
    sk_shape2d_draw_rectangle(x, y + height * (1.0f - value), 16.0f, height * value, SK_COLOR_ORANGE);
}

static void draw_pad(int pad, float x, float y)
{
    char line[96];
    snprintf(line, sizeof(line), "pad %d", pad);
    sk_text_draw(line, (int)x, (int)y, 20, SK_COLOR_RAYWHITE);
    if (!sk_input_is_gamepad_connected(pad)) {
        sk_text_draw("not connected", (int)x, (int)y + 26, 16, SK_COLOR_GRAY);
        return;
    }
    snprintf(line, sizeof(line), "%.40s", sk_input_get_gamepad_name(pad));
    sk_text_draw(line, (int)x, (int)y + 26, 14, SK_COLOR_LIGHTGRAY);

    /* shoulders and triggers */
    draw_trigger(pad, SK_GAMEPAD_AXIS_LEFT_TRIGGER, x + 10.0f, y + 56.0f);
    draw_trigger(pad, SK_GAMEPAD_AXIS_RIGHT_TRIGGER, x + 274.0f, y + 56.0f);
    sk_shape2d_draw_rectangle(x + 34.0f, y + 60.0f, 60.0f, 14.0f, button_color(pad, SK_GAMEPAD_BUTTON_LEFT_BUMPER));
    sk_shape2d_draw_rectangle(x + 206.0f, y + 60.0f, 60.0f, 14.0f,
                              button_color(pad, SK_GAMEPAD_BUTTON_RIGHT_BUMPER));

    /* sticks, d-pad, face buttons, middle buttons */
    draw_stick(pad, SK_GAMEPAD_AXIS_LEFT_X, SK_GAMEPAD_BUTTON_LEFT_STICK, x + 70.0f, y + 130.0f);
    draw_stick(pad, SK_GAMEPAD_AXIS_RIGHT_X, SK_GAMEPAD_BUTTON_RIGHT_STICK, x + 190.0f, y + 210.0f);
    sk_shape2d_draw_rectangle(x + 102.0f, y + 180.0f, 18.0f, 18.0f, button_color(pad, SK_GAMEPAD_BUTTON_DPAD_UP));
    sk_shape2d_draw_rectangle(x + 102.0f, y + 220.0f, 18.0f, 18.0f, button_color(pad, SK_GAMEPAD_BUTTON_DPAD_DOWN));
    sk_shape2d_draw_rectangle(x + 82.0f, y + 200.0f, 18.0f, 18.0f, button_color(pad, SK_GAMEPAD_BUTTON_DPAD_LEFT));
    sk_shape2d_draw_rectangle(x + 122.0f, y + 200.0f, 18.0f, 18.0f,
                              button_color(pad, SK_GAMEPAD_BUTTON_DPAD_RIGHT));
    draw_button(pad, SK_GAMEPAD_BUTTON_NORTH, x + 240.0f, y + 106.0f, 12.0f);
    draw_button(pad, SK_GAMEPAD_BUTTON_SOUTH, x + 240.0f, y + 154.0f, 12.0f);
    draw_button(pad, SK_GAMEPAD_BUTTON_WEST, x + 216.0f, y + 130.0f, 12.0f);
    draw_button(pad, SK_GAMEPAD_BUTTON_EAST, x + 264.0f, y + 130.0f, 12.0f);
    draw_button(pad, SK_GAMEPAD_BUTTON_BACK, x + 126.0f, y + 130.0f, 8.0f);
    draw_button(pad, SK_GAMEPAD_BUTTON_GUIDE, x + 150.0f, y + 110.0f, 10.0f);
    draw_button(pad, SK_GAMEPAD_BUTTON_START, x + 174.0f, y + 130.0f, 8.0f);

    snprintf(line, sizeof(line), "L %+.2f %+.2f  R %+.2f %+.2f", sk_input_get_gamepad_axis(pad, SK_GAMEPAD_AXIS_LEFT_X),
             sk_input_get_gamepad_axis(pad, SK_GAMEPAD_AXIS_LEFT_Y),
             sk_input_get_gamepad_axis(pad, SK_GAMEPAD_AXIS_RIGHT_X),
             sk_input_get_gamepad_axis(pad, SK_GAMEPAD_AXIS_RIGHT_Y));
    sk_text_draw(line, (int)x, (int)y + 262, 14, SK_COLOR_LIGHTGRAY);
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    char line[96];
    (void)dt;
    (void)tick_fraction;
    (void)user_data;

    for (int pad = 0; pad < SK_INPUT_MAX_GAMEPADS; pad++) {
        if (sk_input_get_gamepad_button(pad, SK_GAMEPAD_BUTTON_SOUTH) == SK_BUTTON_PRESSED) {
            g_deadzone = (g_deadzone + 1) % (int)(sizeof(DEADZONES) / sizeof(DEADZONES[0]));
            sk_input_set_gamepad_deadzone(DEADZONES[g_deadzone]);
        }
    }

    sk_render_begin();
    sk_render_clear_background(sk_color_rgba(20, 22, 30, 255));
    sk_text_draw("libsk + sokol — gamepads", 12, 36, 24, SK_COLOR_RAYWHITE);
    snprintf(line, sizeof(line), "dead zone %.2f (SOUTH changes it)   web: press a pad button first",
             DEADZONES[g_deadzone]);
    sk_text_draw(line, 12, 70, 16, SK_COLOR_LIGHTGRAY);
    for (int pad = 0; pad < SK_INPUT_MAX_GAMEPADS; pad++) {
        draw_pad(pad, 20.0f + (float)(pad % 2) * 320.0f, 110.0f + (float)(pad / 2) * 300.0f);
    }
    sk_render_end();

    if (sk_input_get_keyboard_state().keys[SK_KEY_ESCAPE] == SK_BUTTON_PRESSED) {
        sk_request_quit();
    }
}

int main(void)
{
    sk_init_values(680, 720, "libsk gamepad", SK_WINDOW_FLAG_MSAA_4X_HINT | SK_WINDOW_FLAG_WINDOW_RESIZABLE);
    sk_set_frame(frame, NULL);
    return sk_run();
}
