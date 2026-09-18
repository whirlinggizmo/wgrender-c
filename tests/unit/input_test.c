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

/* One touch event: `changed` fingers (identifier, x, y) begin, move or end. */
static void send_touches(sapp_event_type type, int count, const float fingers[][3])
{
    sapp_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.num_touches = count;
    for (int i = 0; i < count; i++) {
        ev.touches[i] = (sapp_touchpoint){
            .identifier = (uintptr_t)fingers[i][0], .pos_x = fingers[i][1], .pos_y = fingers[i][2], .changed = true};
    }
    sk_input_handle_event(&ev);
}

static void send_touch(sapp_event_type type, float id, float x, float y)
{
    const float finger[1][3] = {{id, x, y}};
    send_touches(type, 1, finger);
}

/* Fingers: ids, positions, edges and deltas per context, oldest first. */
void test_input_touches(void)
{
    sk_input_init();
    sk_input_set_context(SK_INPUT_CONTEXT_FRAME);
    CHECK(sk_input_get_touch_count() == 0);
    CHECK(sk_input_get_touch(0).id == -1);

    send_touch(SAPP_EVENTTYPE_TOUCHES_BEGAN, 100, 10, 20);
    send_touch(SAPP_EVENTTYPE_TOUCHES_BEGAN, 200, 50, 60);
    CHECK(sk_input_get_touch_count() == 2);
    sk_touch_t first = sk_input_get_touch(0), second = sk_input_get_touch(1);
    CHECK(first.state == SK_BUTTON_PRESSED && second.state == SK_BUTTON_PRESSED);
    CHECK(first.x == 10 && first.y == 20 && second.x == 50 && second.y == 60);
    CHECK(first.id != second.id);
    sk_input_end_frame();

    send_touch(SAPP_EVENTTYPE_TOUCHES_MOVED, 200, 55, 70);
    send_touch(SAPP_EVENTTYPE_TOUCHES_MOVED, 200, 58, 71);
    second = sk_input_get_touch(1);
    CHECK(second.state == SK_BUTTON_DOWN);
    CHECK(second.dx == 8 && second.dy == 11);
    CHECK(sk_input_get_touch(0).dx == 0);

    /* the tick hasn't run since they went down: it sees the presses and all the movement */
    sk_input_set_context(SK_INPUT_CONTEXT_TICK);
    CHECK(sk_input_get_touch(0).state == SK_BUTTON_PRESSED);
    CHECK(sk_input_get_touch(1).dx == 8);
    sk_input_end_tick();
    CHECK(sk_input_get_touch(1).state == SK_BUTTON_DOWN && sk_input_get_touch(1).dx == 0);
    sk_input_set_context(SK_INPUT_CONTEXT_FRAME);
    sk_input_end_frame();

    /* the first lifts: listed as released this frame, then gone; the other keeps its id */
    send_touch(SAPP_EVENTTYPE_TOUCHES_ENDED, 100, 12, 20);
    CHECK(sk_input_get_touch_count() == 2);
    CHECK(sk_input_get_touch(0).state == SK_BUTTON_RELEASED && sk_input_get_touch(0).dx == 2);
    sk_input_end_frame();
    sk_input_end_tick();
    CHECK(sk_input_get_touch_count() == 1);
    CHECK(sk_input_get_touch(0).id == second.id);

    /* a new finger takes a free id and is listed after the older one */
    send_touch(SAPP_EVENTTYPE_TOUCHES_BEGAN, 300, 1, 1);
    CHECK(sk_input_get_touch_count() == 2);
    CHECK(sk_input_get_touch(0).id == second.id);
    CHECK(sk_input_get_touch(1).state == SK_BUTTON_PRESSED);

    /* a tap within one frame: pressed and lifted together, then gone */
    sk_input_end_frame();
    send_touch(SAPP_EVENTTYPE_TOUCHES_BEGAN, 400, 5, 5);
    send_touch(SAPP_EVENTTYPE_TOUCHES_ENDED, 400, 5, 5);
    CHECK(sk_input_get_touch_count() == 3);
    CHECK(sk_input_get_touch(2).state == SK_BUTTON_PRESSED);
    sk_input_end_frame();
    sk_input_end_tick();
    CHECK(sk_input_get_touch_count() == 2);

    /* a cancelled touch sequence lifts everything */
    const float both[2][3] = {{200, 58, 71}, {300, 1, 1}};
    send_touches(SAPP_EVENTTYPE_TOUCHES_CANCELLED, 2, both);
    CHECK(sk_input_get_touch(0).state == SK_BUTTON_RELEASED && sk_input_get_touch(1).state == SK_BUTTON_RELEASED);
    sk_input_end_frame();
    sk_input_end_tick();
    CHECK(sk_input_get_touch_count() == 0);
    sk_input_deinit();
}

/* Two fingers: pan, pinch and twist, per frame and per tick. */
void test_input_touch_gesture(void)
{
    const float eps = 1e-4f;
    sk_input_init();
    sk_input_set_context(SK_INPUT_CONTEXT_FRAME);

    send_touch(SAPP_EVENTTYPE_TOUCHES_BEGAN, 1, 100, 100);
    sk_touch_gesture_t gesture = sk_input_get_touch_gesture();
    CHECK(!gesture.active && gesture.scale == 1.0f);
    send_touch(SAPP_EVENTTYPE_TOUCHES_BEGAN, 2, 200, 100); /* 100 apart, level */
    gesture = sk_input_get_touch_gesture();
    CHECK(gesture.active);
    CHECK(gesture.x == 150 && gesture.y == 100);
    CHECK(gesture.scale == 1.0f && gesture.dx == 0 && gesture.rotation == 0); /* no movement yet */
    sk_input_end_frame();
    sk_input_end_tick();

    /* spread to 200 apart, around the same centre: a pinch of 2 */
    send_touch(SAPP_EVENTTYPE_TOUCHES_MOVED, 1, 50, 100);
    send_touch(SAPP_EVENTTYPE_TOUCHES_MOVED, 2, 250, 100);
    gesture = sk_input_get_touch_gesture();
    CHECK_NEAR(gesture.scale, 2.0f, eps);
    CHECK_NEAR(gesture.dx, 0.0f, eps);
    CHECK_NEAR(gesture.rotation, 0.0f, eps);
    sk_input_end_frame();

    /* a quarter turn clockwise (screen y is down), then a pan of (10, 20) */
    send_touches(SAPP_EVENTTYPE_TOUCHES_MOVED, 2, (const float[][3]){{1, 150, 0}, {2, 150, 200}});
    send_touches(SAPP_EVENTTYPE_TOUCHES_MOVED, 2, (const float[][3]){{1, 160, 20}, {2, 160, 220}});
    gesture = sk_input_get_touch_gesture();
    CHECK_NEAR(gesture.rotation, (float)M_PI / 2.0f, eps);
    CHECK_NEAR(gesture.scale, 1.0f, eps);
    CHECK_NEAR(gesture.dx, 10.0f, eps);
    CHECK_NEAR(gesture.dy, 20.0f, eps);
    CHECK(gesture.x == 160 && gesture.y == 120);

    /* the tick hasn't run since the pinch: it sees both steps, the pinches multiplied */
    sk_input_set_context(SK_INPUT_CONTEXT_TICK);
    gesture = sk_input_get_touch_gesture();
    CHECK_NEAR(gesture.scale, 2.0f, eps);
    CHECK_NEAR(gesture.rotation, (float)M_PI / 2.0f, eps);
    sk_input_set_context(SK_INPUT_CONTEXT_FRAME);
    sk_input_end_frame();
    sk_input_end_tick();

    /* a third finger doesn't change the pair; when the first lifts, the pair is new and
       the jump isn't a gesture */
    send_touch(SAPP_EVENTTYPE_TOUCHES_BEGAN, 3, 400, 400);
    send_touch(SAPP_EVENTTYPE_TOUCHES_ENDED, 1, 160, 20);
    gesture = sk_input_get_touch_gesture();
    CHECK(gesture.active);
    CHECK(gesture.x == 280 && gesture.y == 310); /* between fingers 2 and 3 */
    CHECK(gesture.scale == 1.0f && gesture.dx == 0);
    send_touch(SAPP_EVENTTYPE_TOUCHES_ENDED, 2, 160, 220);
    CHECK(!sk_input_get_touch_gesture().active);
    sk_input_deinit();
}
