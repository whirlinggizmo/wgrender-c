/* Gamepads (sk_gamepad.c), driven through test pads (the platform is ignored): edges
 * in frames and ticks, the stick dead zone, triggers as buttons, disconnecting, and
 * slots out of range. */
#include <math.h>

#include "internal/sk_gamepad.h"
#include "internal/sk_internal.h"
#include "sk_input.h"
#include "test.h"
#include "tests.h"

static bool buttons[SK_GAMEPAD_BUTTON_COUNT];
static float axes[SK_GAMEPAD_AXIS_COUNT];

static void frame(void)
{
    sk_gamepad_frame_done();
    sk_gamepad_end_tick();
    sk_gamepad_begin_frame();
}

void test_gamepad_buttons(void)
{
    sk_gamepad_init();
    sk_input_set_context(SK_INPUT_CONTEXT_FRAME);
    CHECK(!sk_input_is_gamepad_connected(0));
    CHECK(sk_input_get_gamepad_name(0)[0] == '\0');

    /* plugged in with SOUTH held: it's pressed on the first frame */
    buttons[SK_GAMEPAD_BUTTON_SOUTH] = true;
    sk_gamepad_set_test_pad(1, true, "Test Pad", buttons, axes);
    frame();
    CHECK(!sk_input_is_gamepad_connected(0));
    CHECK(sk_input_is_gamepad_connected(1));
    CHECK(sk_input_get_gamepad_button(1, SK_GAMEPAD_BUTTON_SOUTH) == SK_BUTTON_PRESSED);
    CHECK(sk_input_get_gamepad_button(1, SK_GAMEPAD_BUTTON_EAST) == SK_BUTTON_UP);
    frame();
    CHECK(sk_input_get_gamepad_button(1, SK_GAMEPAD_BUTTON_SOUTH) == SK_BUTTON_DOWN);

    /* ticks have their own edges: a press seen by the frame is still news to the tick
       until a tick has run */
    buttons[SK_GAMEPAD_BUTTON_SOUTH] = false;
    buttons[SK_GAMEPAD_BUTTON_NORTH] = true;
    sk_gamepad_set_test_pad(1, true, "Test Pad", buttons, axes);
    frame();
    CHECK(sk_input_get_gamepad_button(1, SK_GAMEPAD_BUTTON_SOUTH) == SK_BUTTON_RELEASED);
    sk_gamepad_frame_done(); /* the frame is done; no tick ran */
    CHECK(sk_input_get_gamepad_button(1, SK_GAMEPAD_BUTTON_NORTH) == SK_BUTTON_DOWN);
    sk_input_set_context(SK_INPUT_CONTEXT_TICK);
    CHECK(sk_input_get_gamepad_button(1, SK_GAMEPAD_BUTTON_NORTH) == SK_BUTTON_PRESSED);
    sk_gamepad_end_tick();
    CHECK(sk_input_get_gamepad_button(1, SK_GAMEPAD_BUTTON_NORTH) == SK_BUTTON_DOWN);
    sk_input_set_context(SK_INPUT_CONTEXT_FRAME);

    /* unplugged while holding: released, then gone */
    sk_gamepad_set_test_pad(1, false, NULL, NULL, NULL);
    frame();
    CHECK(!sk_input_is_gamepad_connected(1));
    CHECK(sk_input_get_gamepad_button(1, SK_GAMEPAD_BUTTON_NORTH) == SK_BUTTON_RELEASED);
    frame();
    CHECK(sk_input_get_gamepad_button(1, SK_GAMEPAD_BUTTON_NORTH) == SK_BUTTON_UP);

    /* out of range */
    CHECK(sk_input_get_gamepad_button(7, SK_GAMEPAD_BUTTON_SOUTH) == SK_BUTTON_UP);
    CHECK(sk_input_get_gamepad_button(0, (sk_gamepad_button_t)99) == SK_BUTTON_UP);
    CHECK(sk_input_get_gamepad_axis(-1, SK_GAMEPAD_AXIS_LEFT_X) == 0.0f);
    CHECK(sk_input_get_gamepad_name(9)[0] == '\0');
    sk_gamepad_deinit();
}

void test_gamepad_axes(void)
{
    for (int b = 0; b < SK_GAMEPAD_BUTTON_COUNT; b++) buttons[b] = false;
    sk_gamepad_init();
    sk_input_set_context(SK_INPUT_CONTEXT_FRAME);

    /* the dead zone (0.15): inside it the stick is centered; past it, rescaled to reach 1 */
    axes[SK_GAMEPAD_AXIS_LEFT_X] = 0.1f;
    axes[SK_GAMEPAD_AXIS_LEFT_Y] = -0.05f;
    axes[SK_GAMEPAD_AXIS_RIGHT_X] = 0.5f;
    axes[SK_GAMEPAD_AXIS_RIGHT_Y] = 0.0f;
    axes[SK_GAMEPAD_AXIS_LEFT_TRIGGER] = 0.7f;
    axes[SK_GAMEPAD_AXIS_RIGHT_TRIGGER] = 1.5f; /* out of range: clamped */
    sk_gamepad_set_test_pad(0, true, "Test Pad", buttons, axes);
    frame();
    CHECK(sk_input_get_gamepad_axis(0, SK_GAMEPAD_AXIS_LEFT_X) == 0.0f);
    CHECK(sk_input_get_gamepad_axis(0, SK_GAMEPAD_AXIS_LEFT_Y) == 0.0f);
    CHECK_NEAR(sk_input_get_gamepad_axis(0, SK_GAMEPAD_AXIS_RIGHT_X), (0.5f - 0.15f) / 0.85f, 1e-5f);
    CHECK_NEAR(sk_input_get_gamepad_axis(0, SK_GAMEPAD_AXIS_LEFT_TRIGGER), 0.7f, 1e-6f);
    CHECK(sk_input_get_gamepad_axis(0, SK_GAMEPAD_AXIS_RIGHT_TRIGGER) == 1.0f);

    /* triggers are buttons past half-way */
    CHECK(sk_input_get_gamepad_button(0, SK_GAMEPAD_BUTTON_LEFT_TRIGGER) == SK_BUTTON_PRESSED);
    CHECK(sk_input_get_gamepad_button(0, SK_GAMEPAD_BUTTON_RIGHT_TRIGGER) == SK_BUTTON_PRESSED);
    axes[SK_GAMEPAD_AXIS_LEFT_TRIGGER] = 0.2f;
    sk_gamepad_set_test_pad(0, true, "Test Pad", buttons, axes);
    frame();
    CHECK(sk_input_get_gamepad_button(0, SK_GAMEPAD_BUTTON_LEFT_TRIGGER) == SK_BUTTON_RELEASED);

    /* a diagonal past the edge stays within the circle */
    axes[SK_GAMEPAD_AXIS_RIGHT_X] = 1.0f;
    axes[SK_GAMEPAD_AXIS_RIGHT_Y] = 1.0f;
    sk_gamepad_set_test_pad(0, true, "Test Pad", buttons, axes);
    frame();
    const float x = sk_input_get_gamepad_axis(0, SK_GAMEPAD_AXIS_RIGHT_X);
    const float y = sk_input_get_gamepad_axis(0, SK_GAMEPAD_AXIS_RIGHT_Y);
    CHECK_NEAR(sqrtf(x * x + y * y), 1.0f, 1e-5f);

    /* no dead zone: raw values */
    CHECK(sk_input_set_gamepad_deadzone(0.0f));
    CHECK_NEAR(sk_input_get_gamepad_axis(0, SK_GAMEPAD_AXIS_LEFT_X), 0.1f, 1e-6f);
    CHECK(!sk_input_set_gamepad_deadzone(-0.1f));
    CHECK(!sk_input_set_gamepad_deadzone(0.95f));
    sk_gamepad_deinit();
}
