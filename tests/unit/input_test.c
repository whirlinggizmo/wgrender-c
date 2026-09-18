#include <string.h>

#include "internal/sk_internal.h"
#include "sk_input.h"
#include "sokol_app.h"
#include "test.h"
#include "tests.h"

enum { KEY = SAPP_KEYCODE_SPACE };

static void send_key(sapp_event_type type, bool repeat)
{
    sapp_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.key_code = (sapp_keycode)KEY;
    ev.key_repeat = repeat;
    sk_input_handle_event(&ev);
}

static int key_in(sk_input_context_t context)
{
    sk_input_set_context(context);
    return sk_input_get_keyboard_state().keys[KEY];
}

/* A press must reach exactly one tick, however many ticks a frame runs. */
void test_input_tick_edges(void)
{
    sk_input_init();

    /* frame 1 runs no ticks: the frame sees the press, and so does the next tick */
    send_key(SAPP_EVENTTYPE_KEY_DOWN, false);
    CHECK(key_in(SK_INPUT_CONTEXT_FRAME) == SK_BUTTON_PRESSED);
    sk_input_end_frame();

    /* frame 2 runs three ticks: only the first sees the press */
    CHECK(key_in(SK_INPUT_CONTEXT_TICK) == SK_BUTTON_PRESSED);
    sk_input_end_tick();
    CHECK(key_in(SK_INPUT_CONTEXT_TICK) == SK_BUTTON_DOWN);
    sk_input_end_tick();
    CHECK(key_in(SK_INPUT_CONTEXT_TICK) == SK_BUTTON_DOWN);
    sk_input_end_tick();
    CHECK(key_in(SK_INPUT_CONTEXT_FRAME) == SK_BUTTON_DOWN); /* the frame already saw it */
    sk_input_end_frame();

    /* key repeat is not a new press in either context */
    send_key(SAPP_EVENTTYPE_KEY_DOWN, true);
    CHECK(key_in(SK_INPUT_CONTEXT_TICK) == SK_BUTTON_DOWN);
    CHECK(key_in(SK_INPUT_CONTEXT_FRAME) == SK_BUTTON_DOWN);

    /* release: one tick and one frame see it, then the key is up */
    send_key(SAPP_EVENTTYPE_KEY_UP, false);
    CHECK(key_in(SK_INPUT_CONTEXT_TICK) == SK_BUTTON_RELEASED);
    sk_input_end_tick();
    CHECK(key_in(SK_INPUT_CONTEXT_TICK) == SK_BUTTON_UP);
    CHECK(key_in(SK_INPUT_CONTEXT_FRAME) == SK_BUTTON_RELEASED);
    sk_input_end_frame();
    CHECK(key_in(SK_INPUT_CONTEXT_FRAME) == SK_BUTTON_UP);

    sk_input_set_context(SK_INPUT_CONTEXT_FRAME);
}

void test_input_tick_deltas(void)
{
    sapp_event ev;
    sk_input_init();

    memset(&ev, 0, sizeof(ev));
    ev.type = SAPP_EVENTTYPE_MOUSE_MOVE;
    ev.mouse_x = 100;
    ev.mouse_y = 50;
    ev.mouse_dx = 5;
    ev.mouse_dy = -2;
    sk_input_handle_event(&ev);
    sk_input_handle_event(&ev); /* two moves accumulate */

    sk_input_set_context(SK_INPUT_CONTEXT_TICK);
    sk_mouse_state_t tick = sk_input_get_mouse_state();
    CHECK(tick.dx == 10 && tick.dy == -4);
    CHECK(tick.x == 100 && tick.y == 50);
    sk_input_end_tick();
    tick = sk_input_get_mouse_state();
    CHECK(tick.dx == 0 && tick.dy == 0);
    CHECK(tick.x == 100 && tick.y == 50); /* position is shared, not an edge */

    sk_input_set_context(SK_INPUT_CONTEXT_FRAME);
    sk_mouse_state_t frame = sk_input_get_mouse_state();
    CHECK(frame.dx == 10 && frame.dy == -4); /* ticks don't consume frame deltas */
    sk_input_end_frame();
    frame = sk_input_get_mouse_state();
    CHECK(frame.dx == 0 && frame.dy == 0);
}

/* Scrolling accumulates as floats on both axes: small trackpad steps used to be
 * truncated to 0 one event at a time and lost. */
void test_input_wheel(void)
{
    sapp_event ev;
    sk_input_init();
    sk_input_set_context(SK_INPUT_CONTEXT_FRAME);

    memset(&ev, 0, sizeof(ev));
    ev.type = SAPP_EVENTTYPE_MOUSE_SCROLL;
    ev.scroll_y = 0.25f;
    ev.scroll_x = -0.5f;
    for (int i = 0; i < 4; i++) {
        sk_input_handle_event(&ev);
    }
    CHECK_NEAR(sk_input_get_mouse_state().wheel, 1.0, 1e-6);
    CHECK_NEAR(sk_input_get_mouse_state().wheel_x, -2.0, 1e-6);
    CHECK_NEAR(sk_input_get_mouse_wheel(), 1.0, 1e-6);
    CHECK_NEAR(sk_input_get_mouse_wheel_x(), -2.0, 1e-6);

    ev.scroll_y = 0.03f; /* one trackpad step: kept, not rounded away */
    ev.scroll_x = 0.0f;
    sk_input_handle_event(&ev);
    CHECK_NEAR(sk_input_get_mouse_wheel(), 1.03, 1e-5);

    sk_input_end_frame();
    CHECK(sk_input_get_mouse_state().wheel == 0.0f && sk_input_get_mouse_state().wheel_x == 0.0f);
}

/* The UI's captures are sticky, and the pointer is captured by either the UI or a
 * scene press. */
void test_input_capture(void)
{
    sk_input_init();
    CHECK(!sk_input_is_pointer_captured());
    CHECK(!sk_input_is_keyboard_captured());

    sk_input_set_pointer_captured(true);
    CHECK(sk_input_is_pointer_captured());
    sk_input_end_frame(); /* sticky: survives the frame */
    sk_input_end_tick();
    CHECK(sk_input_is_pointer_captured());

    sk_input_set_scene_pointer_captured(true); /* a scene press as well */
    sk_input_set_pointer_captured(false);
    CHECK(sk_input_is_pointer_captured());     /* still held by the scene */
    sk_input_set_scene_pointer_captured(false);
    CHECK(!sk_input_is_pointer_captured());

    sk_input_set_keyboard_captured(true);
    sk_input_end_frame();
    CHECK(sk_input_is_keyboard_captured());
    CHECK(!sk_input_is_pointer_captured());    /* independent of the pointer */
    sk_input_set_keyboard_captured(false);
    CHECK(!sk_input_is_keyboard_captured());
}
