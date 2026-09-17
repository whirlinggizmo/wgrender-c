#include "sk_input.h"

#include <string.h>

#include "internal/exports.h"
#include "internal/sk_internal.h"

#include "internal/sk_platform.h"
#include "sokol_app.h" /* event types only */

/* Edge model
 * ----------
 * sokol_app delivers all pending events (event_cb) before each frame_cb. Held
 * state (buttons/keys down, mouse position) is shared. Edges (pressed/released,
 * mouse and wheel deltas, typed keys and chars) are tracked twice, because they
 * are relative to the callback reading them (docs/PLAN-tick.md):
 *
 *   frame edges: since the previous frame callback; cleared after it runs.
 *   tick edges:  since the previous tick; cleared after each tick. A frame that
 *                runs no ticks carries them over, so every press is seen by
 *                exactly one tick.
 *
 * The runtime sets the context before each callback; getters read the matching
 * edge set. */

#define SK_MOUSE_BUTTONS 3

typedef struct {
    int dx, dy;
    int wheel;
    bool pressed[SK_MOUSE_BUTTONS];
    bool released[SK_MOUSE_BUTTONS];
    bool key_pressed[SK_KEYBOARD_MAX_KEYS];
    bool key_released[SK_KEYBOARD_MAX_KEYS];
    int pressed_keys[SK_KEYBOARD_MAX_PRESSED_KEYS];
    int num_pressed_keys;
    int pressed_chars[SK_KEYBOARD_MAX_PRESSED_CHARS];
    int num_pressed_chars;
} sk_input_edges_t;

typedef struct {
    int x, y;
    bool down[SK_MOUSE_BUTTONS];
    bool key_down[SK_KEYBOARD_MAX_KEYS];

    sk_input_edges_t frame_edges;
    sk_input_edges_t tick_edges;
    sk_input_context_t context;

    /* the first touch drives the pointer, like the left mouse button */
    bool touching;
    uintptr_t touch_id;
    bool pointer_captured; /* set by interactive scenes (sk_scene.c) */
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

void sk_input_set_context(sk_input_context_t context)
{
    sk_input.context = context;
}

void sk_input_end_tick(void)
{
    memset(&sk_input.tick_edges, 0, sizeof(sk_input.tick_edges));
}

void sk_input_end_frame(void)
{
    memset(&sk_input.frame_edges, 0, sizeof(sk_input.frame_edges));
}

static const sk_input_edges_t *current_edges(void)
{
    return sk_input.context == SK_INPUT_CONTEXT_TICK ? &sk_input.tick_edges : &sk_input.frame_edges;
}

static int button_state(bool down, bool pressed, bool released)
{
    if (pressed) return SK_BUTTON_PRESSED;
    if (released) return SK_BUTTON_RELEASED;
    if (down) return SK_BUTTON_DOWN;
    return SK_BUTTON_UP;
}

/* Record one event's edges into an edge set. */
static void add_edges(sk_input_edges_t *edges, const sapp_event *ev, bool key_was_down)
{
    switch (ev->type) {
        case SAPP_EVENTTYPE_MOUSE_MOVE:
            edges->dx += (int)(ev->mouse_dx / sk_window_dpi_scale()); /* logical pixels */
            edges->dy += (int)(ev->mouse_dy / sk_window_dpi_scale());
            break;
        case SAPP_EVENTTYPE_MOUSE_DOWN:
            edges->pressed[ev->mouse_button] = true;
            break;
        case SAPP_EVENTTYPE_MOUSE_UP:
            edges->released[ev->mouse_button] = true;
            break;
        case SAPP_EVENTTYPE_MOUSE_SCROLL:
            edges->wheel += (int)ev->scroll_y;
            break;
        case SAPP_EVENTTYPE_KEY_DOWN:
            if (!ev->key_repeat && !key_was_down) {
                edges->key_pressed[ev->key_code] = true;
                if (edges->num_pressed_keys < SK_KEYBOARD_MAX_PRESSED_KEYS) {
                    edges->pressed_keys[edges->num_pressed_keys++] = ev->key_code;
                }
            }
            break;
        case SAPP_EVENTTYPE_KEY_UP:
            edges->key_released[ev->key_code] = true;
            break;
        case SAPP_EVENTTYPE_CHAR:
            if (edges->num_pressed_chars < SK_KEYBOARD_MAX_PRESSED_CHARS) {
                edges->pressed_chars[edges->num_pressed_chars++] = (int)ev->char_code;
            }
            break;
        default:
            break;
    }
}

/* Feed the primary touch to the pointer as mouse events (move, then left button). */
static void handle_touch(const sapp_event *ev)
{
    for (int i = 0; i < ev->num_touches; i++) {
        const sapp_touchpoint *touch = &ev->touches[i];
        const bool primary = sk_input.touching && touch->identifier == sk_input.touch_id;
        if (!touch->changed || (sk_input.touching && !primary)) {
            continue;
        }
        if (ev->type == SAPP_EVENTTYPE_TOUCHES_BEGAN && sk_input.touching) {
            continue; /* a second finger */
        }
        sapp_event mouse = {.type = SAPP_EVENTTYPE_MOUSE_MOVE, .mouse_x = touch->pos_x, .mouse_y = touch->pos_y};
        sk_input_handle_event(&mouse);
        if (ev->type == SAPP_EVENTTYPE_TOUCHES_BEGAN) {
            sk_input.touching = true;
            sk_input.touch_id = touch->identifier;
            mouse.type = SAPP_EVENTTYPE_MOUSE_DOWN;
            sk_input_handle_event(&mouse);
        } else if (ev->type == SAPP_EVENTTYPE_TOUCHES_ENDED || ev->type == SAPP_EVENTTYPE_TOUCHES_CANCELLED) {
            sk_input.touching = false;
            mouse.type = SAPP_EVENTTYPE_MOUSE_UP;
            sk_input_handle_event(&mouse);
        }
        return;
    }
}

void sk_input_handle_event(const sapp_event *ev)
{
    bool key_was_down = false;

    if (ev == NULL) {
        return;
    }
    if (ev->type == SAPP_EVENTTYPE_TOUCHES_BEGAN || ev->type == SAPP_EVENTTYPE_TOUCHES_MOVED ||
        ev->type == SAPP_EVENTTYPE_TOUCHES_ENDED || ev->type == SAPP_EVENTTYPE_TOUCHES_CANCELLED) {
        handle_touch(ev);
        return;
    }
    switch (ev->type) {
        case SAPP_EVENTTYPE_MOUSE_DOWN:
        case SAPP_EVENTTYPE_MOUSE_UP:
            if (ev->mouse_button < 0 || ev->mouse_button >= SK_MOUSE_BUTTONS) {
                return;
            }
            break;
        case SAPP_EVENTTYPE_KEY_DOWN:
        case SAPP_EVENTTYPE_KEY_UP:
            if (ev->key_code < 0 || ev->key_code >= SK_KEYBOARD_MAX_KEYS) {
                return;
            }
            key_was_down = sk_input.key_down[ev->key_code];
            break;
        default:
            break;
    }

    add_edges(&sk_input.frame_edges, ev, key_was_down);
    add_edges(&sk_input.tick_edges, ev, key_was_down);

    /* held state */
    switch (ev->type) {
        case SAPP_EVENTTYPE_MOUSE_MOVE:
            sk_input.x = (int)(ev->mouse_x / sk_window_dpi_scale()); /* logical pixels */
            sk_input.y = (int)(ev->mouse_y / sk_window_dpi_scale());
            break;
        case SAPP_EVENTTYPE_MOUSE_DOWN:
            sk_input.down[ev->mouse_button] = true;
            break;
        case SAPP_EVENTTYPE_MOUSE_UP:
            sk_input.down[ev->mouse_button] = false;
            break;
        case SAPP_EVENTTYPE_KEY_DOWN:
            sk_input.key_down[ev->key_code] = true;
            break;
        case SAPP_EVENTTYPE_KEY_UP:
            sk_input.key_down[ev->key_code] = false;
            break;
        default:
            break;
    }
}

sk_input_context_t sk_input_get_context(void)
{
    return sk_input.context;
}

void sk_input_get_pointer_frame(float *x, float *y, bool *down, bool *pressed, bool *released)
{
    *x = (float)sk_input.x;
    *y = (float)sk_input.y;
    *down = sk_input.down[0];
    *pressed = sk_input.frame_edges.pressed[0];
    *released = sk_input.frame_edges.released[0];
}

void sk_input_set_pointer_captured(bool captured)
{
    sk_input.pointer_captured = captured;
}

SK_KEEP
bool sk_input_is_pointer_captured(void)
{
    return sk_input.pointer_captured;
}

SK_KEEP
void sk_input_capture_cursor(void)
{
    sk_platform_lock_mouse(true);
}

SK_KEEP
void sk_input_release_cursor(void)
{
    sk_platform_lock_mouse(false);
}

vec2_t sk_input_get_mouse_position(void)
{
    return (vec2_t){(float)sk_input.x, (float)sk_input.y};
}

vec2_t sk_input_get_mouse_delta(void)
{
    return (vec2_t){(float)current_edges()->dx, (float)current_edges()->dy};
}

SK_KEEP
int sk_input_get_mouse_wheel(void)
{
    return current_edges()->wheel;
}

SK_KEEP
int sk_input_get_mouse_button(int button)
{
    if (button < 0 || button >= SK_MOUSE_BUTTONS) {
        return SK_BUTTON_UP;
    }
    return button_state(sk_input.down[button], current_edges()->pressed[button],
                        current_edges()->released[button]);
}

SK_KEEP
sk_mouse_state_t sk_input_get_mouse_state(void)
{
    const sk_input_edges_t *edges = current_edges();
    sk_mouse_state_t state = {0};
    state.x = sk_input.x;
    state.y = sk_input.y;
    state.wheel = edges->wheel;
    state.left = sk_input_get_mouse_button(0);
    state.right = sk_input_get_mouse_button(1);
    state.middle = sk_input_get_mouse_button(2);
    state.buttons[0] = state.left;
    state.buttons[1] = state.right;
    state.buttons[2] = state.middle;
    state.dx = edges->dx;
    state.dy = edges->dy;
    return state;
}

SK_KEEP
sk_keyboard_state_t sk_input_get_keyboard_state(void)
{
    const sk_input_edges_t *edges = current_edges();
    sk_keyboard_state_t state = {0};

    state.max_num_keys = SK_KEYBOARD_MAX_KEYS;
    for (int i = 0; i < SK_KEYBOARD_MAX_KEYS; i++) {
        state.keys[i] = button_state(sk_input.key_down[i], edges->key_pressed[i],
                                     edges->key_released[i]);
    }

    state.num_pressed_keys = edges->num_pressed_keys;
    for (int i = 0; i < edges->num_pressed_keys; i++) {
        state.pressed_keys[i] = edges->pressed_keys[i];
    }
    if (edges->num_pressed_keys > 0) {
        state.pressed_key = edges->pressed_keys[edges->num_pressed_keys - 1];
    }

    state.num_pressed_chars = edges->num_pressed_chars;
    for (int i = 0; i < edges->num_pressed_chars; i++) {
        state.pressed_chars[i] = edges->pressed_chars[i];
    }
    if (edges->num_pressed_chars > 0) {
        state.pressed_char = edges->pressed_chars[edges->num_pressed_chars - 1];
    }

    return state;
}
