/* Gamepads (wgr_gamepad.c), driven through test pads (the platform is ignored): edges
 * in frames and ticks, the stick dead zone, triggers as buttons, disconnecting, and
 * slots out of range. */
#include <math.h>

#include "internal/wgr_gamepad.h"
#include "internal/wgr_internal.h"
#include "wgr_input.h"
#include "test.h"
#include "tests.h"

static bool buttons[WGR_GAMEPAD_BUTTON_COUNT];
static float axes[WGR_GAMEPAD_AXIS_COUNT];

static void frame(void)
{
    wgr_gamepad_frame_done();
    wgr_gamepad_end_tick();
    wgr_gamepad_begin_frame();
}

void test_gamepad_buttons(void)
{
    wgr_gamepad_init();
    wgr_input_set_context(WGR_INPUT_CONTEXT_FRAME);
    CHECK(!wgr_input_is_gamepad_connected(0));
    CHECK(wgr_input_get_gamepad_name(0)[0] == '\0');

    /* plugged in with SOUTH held: it's pressed on the first frame */
    buttons[WGR_GAMEPAD_BUTTON_SOUTH] = true;
    wgr_gamepad_set_test_pad(1, true, "Test Pad", buttons, axes);
    frame();
    CHECK(!wgr_input_is_gamepad_connected(0));
    CHECK(wgr_input_is_gamepad_connected(1));
    CHECK(wgr_input_get_gamepad_button(1, WGR_GAMEPAD_BUTTON_SOUTH) == WGR_BUTTON_PRESSED);
    CHECK(wgr_input_get_gamepad_button(1, WGR_GAMEPAD_BUTTON_EAST) == WGR_BUTTON_UP);
    frame();
    CHECK(wgr_input_get_gamepad_button(1, WGR_GAMEPAD_BUTTON_SOUTH) == WGR_BUTTON_DOWN);

    /* ticks have their own edges: a press seen by the frame is still news to the tick
       until a tick has run */
    buttons[WGR_GAMEPAD_BUTTON_SOUTH] = false;
    buttons[WGR_GAMEPAD_BUTTON_NORTH] = true;
    wgr_gamepad_set_test_pad(1, true, "Test Pad", buttons, axes);
    frame();
    CHECK(wgr_input_get_gamepad_button(1, WGR_GAMEPAD_BUTTON_SOUTH) == WGR_BUTTON_RELEASED);
    wgr_gamepad_frame_done(); /* the frame is done; no tick ran */
    CHECK(wgr_input_get_gamepad_button(1, WGR_GAMEPAD_BUTTON_NORTH) == WGR_BUTTON_DOWN);
    wgr_input_set_context(WGR_INPUT_CONTEXT_TICK);
    CHECK(wgr_input_get_gamepad_button(1, WGR_GAMEPAD_BUTTON_NORTH) == WGR_BUTTON_PRESSED);
    wgr_gamepad_end_tick();
    CHECK(wgr_input_get_gamepad_button(1, WGR_GAMEPAD_BUTTON_NORTH) == WGR_BUTTON_DOWN);
    wgr_input_set_context(WGR_INPUT_CONTEXT_FRAME);

    /* unplugged while holding: released, then gone */
    wgr_gamepad_set_test_pad(1, false, NULL, NULL, NULL);
    frame();
    CHECK(!wgr_input_is_gamepad_connected(1));
    CHECK(wgr_input_get_gamepad_button(1, WGR_GAMEPAD_BUTTON_NORTH) == WGR_BUTTON_RELEASED);
    frame();
    CHECK(wgr_input_get_gamepad_button(1, WGR_GAMEPAD_BUTTON_NORTH) == WGR_BUTTON_UP);

    /* out of range */
    CHECK(wgr_input_get_gamepad_button(7, WGR_GAMEPAD_BUTTON_SOUTH) == WGR_BUTTON_UP);
    CHECK(wgr_input_get_gamepad_button(0, (wgr_gamepad_button_t)99) == WGR_BUTTON_UP);
    CHECK(wgr_input_get_gamepad_axis(-1, WGR_GAMEPAD_AXIS_LEFT_X) == 0.0f);
    CHECK(wgr_input_get_gamepad_name(9)[0] == '\0');
    wgr_gamepad_deinit();
}

void test_gamepad_axes(void)
{
    for (int b = 0; b < WGR_GAMEPAD_BUTTON_COUNT; b++) buttons[b] = false;
    wgr_gamepad_init();
    wgr_input_set_context(WGR_INPUT_CONTEXT_FRAME);

    /* the dead zone (0.15): inside it the stick is centered; past it, rescaled to reach 1 */
    axes[WGR_GAMEPAD_AXIS_LEFT_X] = 0.1f;
    axes[WGR_GAMEPAD_AXIS_LEFT_Y] = -0.05f;
    axes[WGR_GAMEPAD_AXIS_RIGHT_X] = 0.5f;
    axes[WGR_GAMEPAD_AXIS_RIGHT_Y] = 0.0f;
    axes[WGR_GAMEPAD_AXIS_LEFT_TRIGGER] = 0.7f;
    axes[WGR_GAMEPAD_AXIS_RIGHT_TRIGGER] = 1.5f; /* out of range: clamped */
    wgr_gamepad_set_test_pad(0, true, "Test Pad", buttons, axes);
    frame();
    CHECK(wgr_input_get_gamepad_axis(0, WGR_GAMEPAD_AXIS_LEFT_X) == 0.0f);
    CHECK(wgr_input_get_gamepad_axis(0, WGR_GAMEPAD_AXIS_LEFT_Y) == 0.0f);
    CHECK_NEAR(wgr_input_get_gamepad_axis(0, WGR_GAMEPAD_AXIS_RIGHT_X), (0.5f - 0.15f) / 0.85f, 1e-5f);
    CHECK_NEAR(wgr_input_get_gamepad_axis(0, WGR_GAMEPAD_AXIS_LEFT_TRIGGER), 0.7f, 1e-6f);
    CHECK(wgr_input_get_gamepad_axis(0, WGR_GAMEPAD_AXIS_RIGHT_TRIGGER) == 1.0f);

    /* triggers are buttons past half-way */
    CHECK(wgr_input_get_gamepad_button(0, WGR_GAMEPAD_BUTTON_LEFT_TRIGGER) == WGR_BUTTON_PRESSED);
    CHECK(wgr_input_get_gamepad_button(0, WGR_GAMEPAD_BUTTON_RIGHT_TRIGGER) == WGR_BUTTON_PRESSED);
    axes[WGR_GAMEPAD_AXIS_LEFT_TRIGGER] = 0.2f;
    wgr_gamepad_set_test_pad(0, true, "Test Pad", buttons, axes);
    frame();
    CHECK(wgr_input_get_gamepad_button(0, WGR_GAMEPAD_BUTTON_LEFT_TRIGGER) == WGR_BUTTON_RELEASED);

    /* a diagonal past the edge stays within the circle */
    axes[WGR_GAMEPAD_AXIS_RIGHT_X] = 1.0f;
    axes[WGR_GAMEPAD_AXIS_RIGHT_Y] = 1.0f;
    wgr_gamepad_set_test_pad(0, true, "Test Pad", buttons, axes);
    frame();
    const float x = wgr_input_get_gamepad_axis(0, WGR_GAMEPAD_AXIS_RIGHT_X);
    const float y = wgr_input_get_gamepad_axis(0, WGR_GAMEPAD_AXIS_RIGHT_Y);
    CHECK_NEAR(sqrtf(x * x + y * y), 1.0f, 1e-5f);

    /* no dead zone: raw values */
    CHECK(wgr_input_set_gamepad_deadzone(0.0f));
    CHECK_NEAR(wgr_input_get_gamepad_axis(0, WGR_GAMEPAD_AXIS_LEFT_X), 0.1f, 1e-6f);
    CHECK(!wgr_input_set_gamepad_deadzone(-0.1f));
    CHECK(!wgr_input_set_gamepad_deadzone(0.95f));
    wgr_gamepad_deinit();
}
