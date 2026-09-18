#include "sk_input.h"

#include <math.h>
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
    float wheel, wheel_x; /* accumulated as floats: truncating each event lost small trackpad steps */
    bool pressed[SK_MOUSE_BUTTONS];
    bool released[SK_MOUSE_BUTTONS];
    bool key_pressed[SK_KEYBOARD_MAX_KEYS];
    bool key_released[SK_KEYBOARD_MAX_KEYS];
    int pressed_keys[SK_KEYBOARD_MAX_PRESSED_KEYS];
    int num_pressed_keys;
    int pressed_chars[SK_KEYBOARD_MAX_PRESSED_CHARS];
    int num_pressed_chars;
    bool touch_pressed[SK_INPUT_MAX_TOUCHES];
    bool touch_released[SK_INPUT_MAX_TOUCHES];
    float touch_dx[SK_INPUT_MAX_TOUCHES], touch_dy[SK_INPUT_MAX_TOUCHES];
    float gesture_dx, gesture_dy;
    float gesture_log_scale; /* summed, so a cleared edge set means a scale of 1 */
    float gesture_rotation;
} sk_input_edges_t;

/* A finger, in the slot its id names. A lifted finger keeps its slot (and position)
 * until both edge sets have seen the release. */
typedef struct {
    bool down;
    uintptr_t system_id; /* sokol's identifier */
    float x, y;          /* logical pixels */
    unsigned order;      /* when it went down: fingers are listed oldest first */
} sk_finger_t;

typedef struct {
    int x, y;
    bool down[SK_MOUSE_BUTTONS];
    bool key_down[SK_KEYBOARD_MAX_KEYS];

    sk_input_edges_t frame_edges;
    sk_input_edges_t tick_edges;
    sk_input_context_t context;

    sk_finger_t fingers[SK_INPUT_MAX_TOUCHES];
    unsigned finger_order;
    /* the first finger drives the pointer, like the left mouse button, until a second
     * one cancels it; then the pointer stays up until every finger has lifted */
    bool touching;
    uintptr_t touch_id;
    bool touch_cancelled;
    bool scene_pointer_captured; /* set by interactive scenes (sk_scene.c) */
    bool ui_pointer_captured;    /* sticky, set by the game's UI */
    bool ui_keyboard_captured;   /* sticky, set by the game's UI */
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
            edges->wheel += ev->scroll_y;
            edges->wheel_x += ev->scroll_x;
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

static int find_finger(uintptr_t system_id)
{
    for (int i = 0; i < SK_INPUT_MAX_TOUCHES; i++) {
        if (sk_input.fingers[i].down && sk_input.fingers[i].system_id == system_id) return i;
    }
    return -1;
}

/* A slot for a new finger: one with no edges left to report, else any that's up. */
static int free_finger(void)
{
    const sk_input_edges_t *f = &sk_input.frame_edges, *t = &sk_input.tick_edges;
    for (int i = 0; i < SK_INPUT_MAX_TOUCHES; i++) {
        if (!sk_input.fingers[i].down && !f->touch_pressed[i] && !f->touch_released[i] && !t->touch_pressed[i] &&
            !t->touch_released[i]) {
            return i;
        }
    }
    for (int i = 0; i < SK_INPUT_MAX_TOUCHES; i++) {
        if (!sk_input.fingers[i].down) return i;
    }
    return -1;
}

static int fingers_down(void)
{
    int count = 0;
    for (int i = 0; i < SK_INPUT_MAX_TOUCHES; i++) count += sk_input.fingers[i].down ? 1 : 0;
    return count;
}

/* The first two fingers down (the gesture's), or false. */
static bool gesture_pair(int *a, int *b)
{
    *a = *b = -1;
    for (int i = 0; i < SK_INPUT_MAX_TOUCHES; i++) {
        const sk_finger_t *finger = &sk_input.fingers[i];
        if (!finger->down) continue;
        if (*a < 0 || finger->order < sk_input.fingers[*a].order) {
            *b = *a;
            *a = i;
        } else if (*b < 0 || finger->order < sk_input.fingers[*b].order) {
            *b = i;
        }
    }
    return *b >= 0;
}

/* Move the pointer to logical (x, y), and press or release its left button. */
static void pointer_event(sapp_event_type type, float x, float y)
{
    sapp_event mouse = {.type = SAPP_EVENTTYPE_MOUSE_MOVE,
                        .mouse_x = x * sk_window_dpi_scale(),
                        .mouse_y = y * sk_window_dpi_scale()};
    sk_input_handle_event(&mouse);
    if (type != SAPP_EVENTTYPE_MOUSE_MOVE) {
        mouse.type = type;
        sk_input_handle_event(&mouse);
    }
}

/* The pointer follows the first finger; a second one cancels its press. */
static void touch_pointer(sapp_event_type type, uintptr_t system_id, const sk_finger_t *finger)
{
    if (type == SAPP_EVENTTYPE_TOUCHES_BEGAN) {
        if (!sk_input.touching && !sk_input.touch_cancelled && fingers_down() == 1) {
            sk_input.touching = true;
            sk_input.touch_id = system_id;
            pointer_event(SAPP_EVENTTYPE_MOUSE_DOWN, finger->x, finger->y);
        } else if (sk_input.touching) {
            sk_input.touching = false; /* released off-screen: nothing under it is clicked */
            sk_input.touch_cancelled = true;
            pointer_event(SAPP_EVENTTYPE_MOUSE_UP, -1.0f, -1.0f);
        }
    } else if (sk_input.touching && system_id == sk_input.touch_id) {
        if (type == SAPP_EVENTTYPE_TOUCHES_MOVED) {
            pointer_event(SAPP_EVENTTYPE_MOUSE_MOVE, finger->x, finger->y);
        } else {
            sk_input.touching = false;
            pointer_event(SAPP_EVENTTYPE_MOUSE_UP, finger->x, finger->y);
        }
    }
}

/* Fingers, their edges, the pointer and the two-finger gesture from one touch event. */
static void handle_touch(const sapp_event *ev)
{
    const float scale = sk_window_dpi_scale();
    const sapp_event_type type = ev->type == SAPP_EVENTTYPE_TOUCHES_CANCELLED ? SAPP_EVENTTYPE_TOUCHES_ENDED : ev->type;
    int a, b;
    float ax = 0, ay = 0, bx = 0, by = 0;
    const bool had_pair = gesture_pair(&a, &b);
    const int pair_a = a, pair_b = b;

    if (had_pair) {
        ax = sk_input.fingers[a].x, ay = sk_input.fingers[a].y;
        bx = sk_input.fingers[b].x, by = sk_input.fingers[b].y;
    }
    for (int i = 0; i < ev->num_touches; i++) {
        const sapp_touchpoint *touch = &ev->touches[i];
        const float x = touch->pos_x / scale, y = touch->pos_y / scale;
        int slot = find_finger(touch->identifier);
        sk_finger_t *finger;

        if (!touch->changed) continue;
        if (type == SAPP_EVENTTYPE_TOUCHES_BEGAN && slot < 0) {
            slot = free_finger();
            if (slot < 0) continue;
            sk_input.frame_edges.touch_released[slot] = sk_input.tick_edges.touch_released[slot] = false;
            sk_input.frame_edges.touch_dx[slot] = sk_input.tick_edges.touch_dx[slot] = 0.0f;
            sk_input.frame_edges.touch_dy[slot] = sk_input.tick_edges.touch_dy[slot] = 0.0f;
            sk_input.fingers[slot] = (sk_finger_t){
                .down = true, .system_id = touch->identifier, .x = x, .y = y, .order = ++sk_input.finger_order};
            sk_input.frame_edges.touch_pressed[slot] = sk_input.tick_edges.touch_pressed[slot] = true;
        } else if (slot < 0) {
            continue;
        }
        finger = &sk_input.fingers[slot];
        sk_input.frame_edges.touch_dx[slot] += x - finger->x;
        sk_input.tick_edges.touch_dx[slot] += x - finger->x;
        sk_input.frame_edges.touch_dy[slot] += y - finger->y;
        sk_input.tick_edges.touch_dy[slot] += y - finger->y;
        finger->x = x;
        finger->y = y;
        if (type == SAPP_EVENTTYPE_TOUCHES_ENDED) {
            finger->down = false;
            sk_input.frame_edges.touch_released[slot] = sk_input.tick_edges.touch_released[slot] = true;
        }
        touch_pointer(type, touch->identifier, finger);
    }
    if (fingers_down() == 0) {
        sk_input.touch_cancelled = false;
    }

    /* the same two fingers before and after: they panned, pinched and twisted */
    if (had_pair && gesture_pair(&a, &b) && a == pair_a && b == pair_b) {
        const float nax = sk_input.fingers[a].x, nay = sk_input.fingers[a].y;
        const float nbx = sk_input.fingers[b].x, nby = sk_input.fingers[b].y;
        const float before = hypotf(bx - ax, by - ay), after = hypotf(nbx - nax, nby - nay);
        float turn = atan2f(nby - nay, nbx - nax) - atan2f(by - ay, bx - ax);
        const float pan_x = (nax + nbx - ax - bx) * 0.5f, pan_y = (nay + nby - ay - by) * 0.5f;
        if (turn > (float)M_PI) turn -= 2.0f * (float)M_PI;
        if (turn < -(float)M_PI) turn += 2.0f * (float)M_PI;
        sk_input_edges_t *sets[2] = {&sk_input.frame_edges, &sk_input.tick_edges};
        for (int s = 0; s < 2; s++) {
            sets[s]->gesture_dx += pan_x;
            sets[s]->gesture_dy += pan_y;
            if (before > 0.0f && after > 0.0f) {
                sets[s]->gesture_log_scale += logf(after / before);
                sets[s]->gesture_rotation += turn;
            }
        }
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

void sk_input_set_scene_pointer_captured(bool captured)
{
    sk_input.scene_pointer_captured = captured;
}

SK_KEEP
void sk_input_set_pointer_captured(bool captured)
{
    sk_input.ui_pointer_captured = captured;
}

SK_KEEP
bool sk_input_is_pointer_captured(void)
{
    return sk_input.scene_pointer_captured || sk_input.ui_pointer_captured;
}

SK_KEEP
void sk_input_set_keyboard_captured(bool captured)
{
    sk_input.ui_keyboard_captured = captured;
}

SK_KEEP
bool sk_input_is_keyboard_captured(void)
{
    return sk_input.ui_keyboard_captured;
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
float sk_input_get_mouse_wheel(void)
{
    return current_edges()->wheel;
}

SK_KEEP
float sk_input_get_mouse_wheel_x(void)
{
    return current_edges()->wheel_x;
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
    state.wheel_x = edges->wheel_x;
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

/* The fingers to report in the current context (down, or with an edge in it), oldest first. */
static int list_touches(int out[SK_INPUT_MAX_TOUCHES])
{
    const sk_input_edges_t *edges = current_edges();
    int count = 0;
    for (int i = 0; i < SK_INPUT_MAX_TOUCHES; i++) {
        if (!sk_input.fingers[i].down && !edges->touch_pressed[i] && !edges->touch_released[i]) continue;
        int j = count++;
        while (j > 0 && sk_input.fingers[out[j - 1]].order > sk_input.fingers[i].order) {
            out[j] = out[j - 1];
            j--;
        }
        out[j] = i;
    }
    return count;
}

SK_KEEP
int sk_input_get_touch_count(void)
{
    int slots[SK_INPUT_MAX_TOUCHES];
    return list_touches(slots);
}

SK_KEEP
sk_touch_t sk_input_get_touch(int index)
{
    int slots[SK_INPUT_MAX_TOUCHES];
    const int count = list_touches(slots);
    const sk_input_edges_t *edges = current_edges();
    int slot;

    if (index < 0 || index >= count) {
        return (sk_touch_t){.id = -1, .state = SK_BUTTON_UP};
    }
    slot = slots[index];
    return (sk_touch_t){
        .id = slot,
        .x = sk_input.fingers[slot].x,
        .y = sk_input.fingers[slot].y,
        .dx = edges->touch_dx[slot],
        .dy = edges->touch_dy[slot],
        .state = button_state(sk_input.fingers[slot].down, edges->touch_pressed[slot], edges->touch_released[slot]),
    };
}

SK_KEEP
sk_touch_gesture_t sk_input_get_touch_gesture(void)
{
    const sk_input_edges_t *edges = current_edges();
    sk_touch_gesture_t gesture = {.scale = expf(edges->gesture_log_scale)};
    int a, b;

    gesture.dx = edges->gesture_dx;
    gesture.dy = edges->gesture_dy;
    gesture.rotation = edges->gesture_rotation;
    if (gesture_pair(&a, &b)) {
        gesture.active = true;
        gesture.x = (sk_input.fingers[a].x + sk_input.fingers[b].x) * 0.5f;
        gesture.y = (sk_input.fingers[a].y + sk_input.fingers[b].y) * 0.5f;
    }
    return gesture;
}
