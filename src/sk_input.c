#include "sk_input.h"

#include <string.h>

#include "internal/exports.h"
#include "internal/sk_internal.h"

#include "sokol_app.h"

/* Edge model
 * ----------
 * sokol_app delivers all pending events (event_cb) before each frame_cb, so we
 * accumulate down/pressed/released and per-frame deltas as events arrive, expose
 * them during the user frame, then clear the per-frame edge state at the END of
 * the frame (sk_input_new_frame, called after the user callback). */

#define SK_MOUSE_BUTTONS 3

typedef struct {
    int x, y;
    int dx, dy;
    int wheel;
    bool down[SK_MOUSE_BUTTONS];
    bool pressed[SK_MOUSE_BUTTONS];
    bool released[SK_MOUSE_BUTTONS];

    bool key_down[SK_KEYBOARD_MAX_KEYS];
    bool key_pressed[SK_KEYBOARD_MAX_KEYS];
    bool key_released[SK_KEYBOARD_MAX_KEYS];

    int pressed_keys[SK_KEYBOARD_MAX_PRESSED_KEYS];
    int num_pressed_keys;
    int pressed_chars[SK_KEYBOARD_MAX_PRESSED_CHARS];
    int num_pressed_chars;
} sk_input_state_t;

static sk_input_state_t sk_input;

void sk_input_init(void)
{
    memset(&sk_input, 0, sizeof(sk_input));
}

void sk_input_deinit(void)
{
    memset(&sk_input, 0, sizeof(sk_input));
}

static int button_state(bool down, bool pressed, bool released)
{
    if (pressed) return SK_BUTTON_PRESSED;
    if (released) return SK_BUTTON_RELEASED;
    if (down) return SK_BUTTON_DOWN;
    return SK_BUTTON_UP;
}

void sk_input_handle_event(const sapp_event *ev)
{
    if (ev == NULL) {
        return;
    }

    switch (ev->type) {
        case SAPP_EVENTTYPE_MOUSE_MOVE:
            sk_input.x = (int)ev->mouse_x;
            sk_input.y = (int)ev->mouse_y;
            sk_input.dx += (int)ev->mouse_dx;
            sk_input.dy += (int)ev->mouse_dy;
            break;
        case SAPP_EVENTTYPE_MOUSE_DOWN:
            if (ev->mouse_button >= 0 && ev->mouse_button < SK_MOUSE_BUTTONS) {
                sk_input.down[ev->mouse_button] = true;
                sk_input.pressed[ev->mouse_button] = true;
            }
            break;
        case SAPP_EVENTTYPE_MOUSE_UP:
            if (ev->mouse_button >= 0 && ev->mouse_button < SK_MOUSE_BUTTONS) {
                sk_input.down[ev->mouse_button] = false;
                sk_input.released[ev->mouse_button] = true;
            }
            break;
        case SAPP_EVENTTYPE_MOUSE_SCROLL:
            sk_input.wheel += (int)ev->scroll_y;
            break;
        case SAPP_EVENTTYPE_KEY_DOWN:
            if (ev->key_code >= 0 && ev->key_code < SK_KEYBOARD_MAX_KEYS) {
                if (!ev->key_repeat && !sk_input.key_down[ev->key_code]) {
                    sk_input.key_pressed[ev->key_code] = true;
                    if (sk_input.num_pressed_keys < SK_KEYBOARD_MAX_PRESSED_KEYS) {
                        sk_input.pressed_keys[sk_input.num_pressed_keys++] = ev->key_code;
                    }
                }
                sk_input.key_down[ev->key_code] = true;
            }
            break;
        case SAPP_EVENTTYPE_KEY_UP:
            if (ev->key_code >= 0 && ev->key_code < SK_KEYBOARD_MAX_KEYS) {
                sk_input.key_down[ev->key_code] = false;
                sk_input.key_released[ev->key_code] = true;
            }
            break;
        case SAPP_EVENTTYPE_CHAR:
            if (sk_input.num_pressed_chars < SK_KEYBOARD_MAX_PRESSED_CHARS) {
                sk_input.pressed_chars[sk_input.num_pressed_chars++] = (int)ev->char_code;
            }
            break;
        default:
            break;
    }
}

void sk_input_new_frame(void)
{
    /* clear per-frame edges + deltas (called after the user frame callback) */
    sk_input.dx = 0;
    sk_input.dy = 0;
    sk_input.wheel = 0;
    for (int i = 0; i < SK_MOUSE_BUTTONS; i++) {
        sk_input.pressed[i] = false;
        sk_input.released[i] = false;
    }
    memset(sk_input.key_pressed, 0, sizeof(sk_input.key_pressed));
    memset(sk_input.key_released, 0, sizeof(sk_input.key_released));
    sk_input.num_pressed_keys = 0;
    sk_input.num_pressed_chars = 0;
}

SK_KEEP
void sk_input_capture_cursor(void)
{
    sapp_lock_mouse(true);
}

SK_KEEP
void sk_input_release_cursor(void)
{
    sapp_lock_mouse(false);
}

vec2_t sk_input_get_mouse_position(void)
{
    return (vec2_t){(float)sk_input.x, (float)sk_input.y};
}

vec2_t sk_input_get_mouse_delta(void)
{
    return (vec2_t){(float)sk_input.dx, (float)sk_input.dy};
}

SK_KEEP
int sk_input_get_mouse_wheel(void)
{
    return sk_input.wheel;
}

SK_KEEP
int sk_input_get_mouse_button(int button)
{
    if (button < 0 || button >= SK_MOUSE_BUTTONS) {
        return SK_BUTTON_UP;
    }
    return button_state(sk_input.down[button], sk_input.pressed[button],
                        sk_input.released[button]);
}

SK_KEEP
sk_mouse_state_t sk_input_get_mouse_state(void)
{
    sk_mouse_state_t state = {0};
    state.x = sk_input.x;
    state.y = sk_input.y;
    state.wheel = sk_input.wheel;
    state.left = sk_input_get_mouse_button(0);
    state.right = sk_input_get_mouse_button(1);
    state.middle = sk_input_get_mouse_button(2);
    state.buttons[0] = state.left;
    state.buttons[1] = state.right;
    state.buttons[2] = state.middle;
    state.dx = sk_input.dx;
    state.dy = sk_input.dy;
    return state;
}

SK_KEEP
sk_keyboard_state_t sk_input_get_keyboard_state(void)
{
    sk_keyboard_state_t state = {0};

    state.max_num_keys = SK_KEYBOARD_MAX_KEYS;
    for (int i = 0; i < SK_KEYBOARD_MAX_KEYS; i++) {
        state.keys[i] = button_state(sk_input.key_down[i], sk_input.key_pressed[i],
                                     sk_input.key_released[i]);
    }

    state.num_pressed_keys = sk_input.num_pressed_keys;
    for (int i = 0; i < sk_input.num_pressed_keys; i++) {
        state.pressed_keys[i] = sk_input.pressed_keys[i];
    }
    if (sk_input.num_pressed_keys > 0) {
        state.pressed_key = sk_input.pressed_keys[sk_input.num_pressed_keys - 1];
    }

    state.num_pressed_chars = sk_input.num_pressed_chars;
    for (int i = 0; i < sk_input.num_pressed_chars; i++) {
        state.pressed_chars[i] = sk_input.pressed_chars[i];
    }
    if (sk_input.num_pressed_chars > 0) {
        state.pressed_char = sk_input.pressed_chars[sk_input.num_pressed_chars - 1];
    }

    return state;
}
