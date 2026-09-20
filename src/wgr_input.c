#include "wgr_input.h"

#include <math.h>
#include <string.h>

#include "internal/exports_internal.h"
#include "internal/wgr_internal_internal.h"

#include "internal/wgr_platform_internal.h"
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

#define WGR_MOUSE_BUTTONS 3

typedef struct {
    int dx, dy;
    float wheel, wheel_x; /* accumulated as floats: truncating each event lost small trackpad steps */
    bool pressed[WGR_MOUSE_BUTTONS];
    bool released[WGR_MOUSE_BUTTONS];
    bool key_pressed[WGR_KEYBOARD_MAX_KEYS];
    bool key_released[WGR_KEYBOARD_MAX_KEYS];
    int pressed_keys[WGR_KEYBOARD_MAX_PRESSED_KEYS];
    int num_pressed_keys;
    int pressed_chars[WGR_KEYBOARD_MAX_PRESSED_CHARS];
    int num_pressed_chars;
    bool touch_pressed[WGR_INPUT_MAX_TOUCHES];
    bool touch_released[WGR_INPUT_MAX_TOUCHES];
    float touch_dx[WGR_INPUT_MAX_TOUCHES], touch_dy[WGR_INPUT_MAX_TOUCHES];
    float gesture_dx, gesture_dy;
    float gesture_log_scale; /* summed, so a cleared edge set means a scale of 1 */
    float gesture_rotation;
} wgr_input_edges_t;

/* A finger, in the slot its id names. A lifted finger keeps its slot (and position)
 * until both edge sets have seen the release. */
typedef struct {
    bool down;
    uintptr_t system_id; /* sokol's identifier */
    float x, y;          /* logical pixels */
    unsigned order;      /* when it went down: fingers are listed oldest first */
} wgr_finger_t;

typedef struct {
    int x, y;
    bool down[WGR_MOUSE_BUTTONS];
    bool key_down[WGR_KEYBOARD_MAX_KEYS];

    wgr_input_edges_t frame_edges;
    wgr_input_edges_t tick_edges;
    wgri_input_context_t context;

    wgr_finger_t fingers[WGR_INPUT_MAX_TOUCHES];
    unsigned finger_order;
    /* the first finger drives the pointer, like the left mouse button, until a second
     * one cancels it; then the pointer stays up until every finger has lifted */
    bool touching;
    uintptr_t touch_id;
    bool touch_cancelled;
    bool scene_pointer_captured; /* set by interactive scenes (wgr_scene.c) */
    bool ui_pointer_captured;    /* sticky, set by the game's UI */
    bool ui_keyboard_captured;   /* sticky, set by the game's UI */
} wgr_input_state_t;

static wgr_input_state_t wgr_input;

void wgri_input_init(void)
{
    memset(&wgr_input, 0, sizeof(wgr_input));
}

void wgri_input_deinit(void)
{
    memset(&wgr_input, 0, sizeof(wgr_input));
}

void wgri_input_set_context(wgri_input_context_t context)
{
    wgr_input.context = context;
}

void wgri_input_end_tick(void)
{
    memset(&wgr_input.tick_edges, 0, sizeof(wgr_input.tick_edges));
}

void wgri_input_end_frame(void)
{
    memset(&wgr_input.frame_edges, 0, sizeof(wgr_input.frame_edges));
}

static const wgr_input_edges_t *current_edges(void)
{
    return wgr_input.context == WGRI_INPUT_CONTEXT_TICK ? &wgr_input.tick_edges : &wgr_input.frame_edges;
}

static int button_state(bool down, bool pressed, bool released)
{
    if (pressed) return WGR_BUTTON_PRESSED;
    if (released) return WGR_BUTTON_RELEASED;
    if (down) return WGR_BUTTON_DOWN;
    return WGR_BUTTON_UP;
}

/* Record one event's edges into an edge set. */
static void add_edges(wgr_input_edges_t *edges, const sapp_event *ev, bool key_was_down)
{
    switch (ev->type) {
        case SAPP_EVENTTYPE_MOUSE_MOVE:
            edges->dx += (int)(ev->mouse_dx / wgri_window_dpi_scale()); /* logical pixels */
            edges->dy += (int)(ev->mouse_dy / wgri_window_dpi_scale());
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
                if (edges->num_pressed_keys < WGR_KEYBOARD_MAX_PRESSED_KEYS) {
                    edges->pressed_keys[edges->num_pressed_keys++] = ev->key_code;
                }
            }
            break;
        case SAPP_EVENTTYPE_KEY_UP:
            edges->key_released[ev->key_code] = true;
            break;
        case SAPP_EVENTTYPE_CHAR:
            if (edges->num_pressed_chars < WGR_KEYBOARD_MAX_PRESSED_CHARS) {
                edges->pressed_chars[edges->num_pressed_chars++] = (int)ev->char_code;
            }
            break;
        default:
            break;
    }
}

static int find_finger(uintptr_t system_id)
{
    for (int i = 0; i < WGR_INPUT_MAX_TOUCHES; i++) {
        if (wgr_input.fingers[i].down && wgr_input.fingers[i].system_id == system_id) return i;
    }
    return -1;
}

/* A slot for a new finger: one with no edges left to report, else any that's up. */
static int free_finger(void)
{
    const wgr_input_edges_t *f = &wgr_input.frame_edges, *t = &wgr_input.tick_edges;
    for (int i = 0; i < WGR_INPUT_MAX_TOUCHES; i++) {
        if (!wgr_input.fingers[i].down && !f->touch_pressed[i] && !f->touch_released[i] && !t->touch_pressed[i] &&
            !t->touch_released[i]) {
            return i;
        }
    }
    for (int i = 0; i < WGR_INPUT_MAX_TOUCHES; i++) {
        if (!wgr_input.fingers[i].down) return i;
    }
    return -1;
}

static int fingers_down(void)
{
    int count = 0;
    for (int i = 0; i < WGR_INPUT_MAX_TOUCHES; i++) count += wgr_input.fingers[i].down ? 1 : 0;
    return count;
}

/* The first two fingers down (the gesture's), or false. */
static bool gesture_pair(int *a, int *b)
{
    *a = *b = -1;
    for (int i = 0; i < WGR_INPUT_MAX_TOUCHES; i++) {
        const wgr_finger_t *finger = &wgr_input.fingers[i];
        if (!finger->down) continue;
        if (*a < 0 || finger->order < wgr_input.fingers[*a].order) {
            *b = *a;
            *a = i;
        } else if (*b < 0 || finger->order < wgr_input.fingers[*b].order) {
            *b = i;
        }
    }
    return *b >= 0;
}

/* Move the pointer to logical (x, y), and press or release its left button. */
static void pointer_event(sapp_event_type type, float x, float y)
{
    sapp_event mouse = {.type = SAPP_EVENTTYPE_MOUSE_MOVE,
                        .mouse_x = x * wgri_window_dpi_scale(),
                        .mouse_y = y * wgri_window_dpi_scale()};
    wgri_input_handle_event(&mouse);
    if (type != SAPP_EVENTTYPE_MOUSE_MOVE) {
        mouse.type = type;
        wgri_input_handle_event(&mouse);
    }
}

/* The pointer follows the first finger; a second one cancels its press. */
static void touch_pointer(sapp_event_type type, uintptr_t system_id, const wgr_finger_t *finger)
{
    if (type == SAPP_EVENTTYPE_TOUCHES_BEGAN) {
        if (!wgr_input.touching && !wgr_input.touch_cancelled && fingers_down() == 1) {
            wgr_input.touching = true;
            wgr_input.touch_id = system_id;
            pointer_event(SAPP_EVENTTYPE_MOUSE_DOWN, finger->x, finger->y);
        } else if (wgr_input.touching) {
            wgr_input.touching = false; /* released off-screen: nothing under it is clicked */
            wgr_input.touch_cancelled = true;
            pointer_event(SAPP_EVENTTYPE_MOUSE_UP, -1.0f, -1.0f);
        }
    } else if (wgr_input.touching && system_id == wgr_input.touch_id) {
        if (type == SAPP_EVENTTYPE_TOUCHES_MOVED) {
            pointer_event(SAPP_EVENTTYPE_MOUSE_MOVE, finger->x, finger->y);
        } else {
            wgr_input.touching = false;
            pointer_event(SAPP_EVENTTYPE_MOUSE_UP, finger->x, finger->y);
        }
    }
}

/* Fingers, their edges, the pointer and the two-finger gesture from one touch event. */
static void handle_touch(const sapp_event *ev)
{
    const float scale = wgri_window_dpi_scale();
    const sapp_event_type type = ev->type == SAPP_EVENTTYPE_TOUCHES_CANCELLED ? SAPP_EVENTTYPE_TOUCHES_ENDED : ev->type;
    int a, b;
    float ax = 0, ay = 0, bx = 0, by = 0;
    const bool had_pair = gesture_pair(&a, &b);
    const int pair_a = a, pair_b = b;

    if (had_pair) {
        ax = wgr_input.fingers[a].x, ay = wgr_input.fingers[a].y;
        bx = wgr_input.fingers[b].x, by = wgr_input.fingers[b].y;
    }
    for (int i = 0; i < ev->num_touches; i++) {
        const sapp_touchpoint *touch = &ev->touches[i];
        const float x = touch->pos_x / scale, y = touch->pos_y / scale;
        int slot = find_finger(touch->identifier);
        wgr_finger_t *finger;

        if (!touch->changed) continue;
        if (type == SAPP_EVENTTYPE_TOUCHES_BEGAN && slot < 0) {
            slot = free_finger();
            if (slot < 0) continue;
            wgr_input.frame_edges.touch_released[slot] = wgr_input.tick_edges.touch_released[slot] = false;
            wgr_input.frame_edges.touch_dx[slot] = wgr_input.tick_edges.touch_dx[slot] = 0.0f;
            wgr_input.frame_edges.touch_dy[slot] = wgr_input.tick_edges.touch_dy[slot] = 0.0f;
            wgr_input.fingers[slot] = (wgr_finger_t){
                .down = true, .system_id = touch->identifier, .x = x, .y = y, .order = ++wgr_input.finger_order};
            wgr_input.frame_edges.touch_pressed[slot] = wgr_input.tick_edges.touch_pressed[slot] = true;
        } else if (slot < 0) {
            continue;
        }
        finger = &wgr_input.fingers[slot];
        wgr_input.frame_edges.touch_dx[slot] += x - finger->x;
        wgr_input.tick_edges.touch_dx[slot] += x - finger->x;
        wgr_input.frame_edges.touch_dy[slot] += y - finger->y;
        wgr_input.tick_edges.touch_dy[slot] += y - finger->y;
        finger->x = x;
        finger->y = y;
        if (type == SAPP_EVENTTYPE_TOUCHES_ENDED) {
            finger->down = false;
            wgr_input.frame_edges.touch_released[slot] = wgr_input.tick_edges.touch_released[slot] = true;
        }
        touch_pointer(type, touch->identifier, finger);
    }
    if (fingers_down() == 0) {
        wgr_input.touch_cancelled = false;
    }

    /* the same two fingers before and after: they panned, pinched and twisted */
    if (had_pair && gesture_pair(&a, &b) && a == pair_a && b == pair_b) {
        const float nax = wgr_input.fingers[a].x, nay = wgr_input.fingers[a].y;
        const float nbx = wgr_input.fingers[b].x, nby = wgr_input.fingers[b].y;
        const float before = hypotf(bx - ax, by - ay), after = hypotf(nbx - nax, nby - nay);
        float turn = atan2f(nby - nay, nbx - nax) - atan2f(by - ay, bx - ax);
        const float pan_x = (nax + nbx - ax - bx) * 0.5f, pan_y = (nay + nby - ay - by) * 0.5f;
        if (turn > (float)M_PI) turn -= 2.0f * (float)M_PI;
        if (turn < -(float)M_PI) turn += 2.0f * (float)M_PI;
        wgr_input_edges_t *sets[2] = {&wgr_input.frame_edges, &wgr_input.tick_edges};
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

void wgri_input_handle_event(const sapp_event *ev)
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
            if (ev->mouse_button < 0 || ev->mouse_button >= WGR_MOUSE_BUTTONS) {
                return;
            }
            break;
        case SAPP_EVENTTYPE_KEY_DOWN:
        case SAPP_EVENTTYPE_KEY_UP:
            if (ev->key_code < 0 || ev->key_code >= WGR_KEYBOARD_MAX_KEYS) {
                return;
            }
            key_was_down = wgr_input.key_down[ev->key_code];
            break;
        default:
            break;
    }

    add_edges(&wgr_input.frame_edges, ev, key_was_down);
    add_edges(&wgr_input.tick_edges, ev, key_was_down);

    /* held state */
    switch (ev->type) {
        case SAPP_EVENTTYPE_MOUSE_MOVE:
            wgr_input.x = (int)(ev->mouse_x / wgri_window_dpi_scale()); /* logical pixels */
            wgr_input.y = (int)(ev->mouse_y / wgri_window_dpi_scale());
            break;
        case SAPP_EVENTTYPE_MOUSE_DOWN:
            wgr_input.down[ev->mouse_button] = true;
            break;
        case SAPP_EVENTTYPE_MOUSE_UP:
            wgr_input.down[ev->mouse_button] = false;
            break;
        case SAPP_EVENTTYPE_KEY_DOWN:
            wgr_input.key_down[ev->key_code] = true;
            break;
        case SAPP_EVENTTYPE_KEY_UP:
            wgr_input.key_down[ev->key_code] = false;
            break;
        default:
            break;
    }
}

wgri_input_context_t wgri_input_get_context(void)
{
    return wgr_input.context;
}

void wgri_input_get_pointer_frame(float *x, float *y, bool *down, bool *pressed, bool *released)
{
    *x = (float)wgr_input.x;
    *y = (float)wgr_input.y;
    *down = wgr_input.down[0];
    *pressed = wgr_input.frame_edges.pressed[0];
    *released = wgr_input.frame_edges.released[0];
}

void wgri_input_set_scene_pointer_captured(bool captured)
{
    wgr_input.scene_pointer_captured = captured;
}

WGRI_KEEP
void wgr_input_set_pointer_captured(bool captured)
{
    wgr_input.ui_pointer_captured = captured;
}

WGRI_KEEP
bool wgr_input_is_pointer_captured(void)
{
    return wgr_input.scene_pointer_captured || wgr_input.ui_pointer_captured;
}

WGRI_KEEP
void wgr_input_set_keyboard_captured(bool captured)
{
    wgr_input.ui_keyboard_captured = captured;
}

WGRI_KEEP
bool wgr_input_is_keyboard_captured(void)
{
    return wgr_input.ui_keyboard_captured;
}

WGRI_KEEP
void wgr_input_capture_cursor(void)
{
    wgri_platform_lock_mouse(true);
}

WGRI_KEEP
void wgr_input_release_cursor(void)
{
    wgri_platform_lock_mouse(false);
}

vec2_t wgr_input_get_mouse_position(void)
{
    return (vec2_t){(float)wgr_input.x, (float)wgr_input.y};
}

vec2_t wgr_input_get_mouse_delta(void)
{
    return (vec2_t){(float)current_edges()->dx, (float)current_edges()->dy};
}

WGRI_KEEP
float wgr_input_get_mouse_wheel(void)
{
    return current_edges()->wheel;
}

WGRI_KEEP
float wgr_input_get_mouse_wheel_x(void)
{
    return current_edges()->wheel_x;
}

WGRI_KEEP
int wgr_input_get_mouse_button(int button)
{
    if (button < 0 || button >= WGR_MOUSE_BUTTONS) {
        return WGR_BUTTON_UP;
    }
    return button_state(wgr_input.down[button], current_edges()->pressed[button],
                        current_edges()->released[button]);
}

WGRI_KEEP
wgr_mouse_state_t wgr_input_get_mouse_state(void)
{
    const wgr_input_edges_t *edges = current_edges();
    wgr_mouse_state_t state = {0};
    state.x = wgr_input.x;
    state.y = wgr_input.y;
    state.wheel = edges->wheel;
    state.wheel_x = edges->wheel_x;
    state.left = wgr_input_get_mouse_button(0);
    state.right = wgr_input_get_mouse_button(1);
    state.middle = wgr_input_get_mouse_button(2);
    state.buttons[0] = state.left;
    state.buttons[1] = state.right;
    state.buttons[2] = state.middle;
    state.dx = edges->dx;
    state.dy = edges->dy;
    return state;
}

WGRI_KEEP
int wgr_input_get_key(wgr_keycode_t key)
{
    const wgr_input_edges_t *edges = current_edges();
    if ((int)key < 0 || (int)key >= WGR_KEYBOARD_MAX_KEYS) {
        return WGR_BUTTON_UP;
    }
    return button_state(wgr_input.key_down[key], edges->key_pressed[key], edges->key_released[key]);
}

WGRI_KEEP
wgr_keyboard_state_t wgr_input_get_keyboard_state(void)
{
    const wgr_input_edges_t *edges = current_edges();
    wgr_keyboard_state_t state = {0};

    state.max_num_keys = WGR_KEYBOARD_MAX_KEYS;
    for (int i = 0; i < WGR_KEYBOARD_MAX_KEYS; i++) {
        state.keys[i] = wgr_input_get_key((wgr_keycode_t)i);
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
static int list_touches(int out[WGR_INPUT_MAX_TOUCHES])
{
    const wgr_input_edges_t *edges = current_edges();
    int count = 0;
    for (int i = 0; i < WGR_INPUT_MAX_TOUCHES; i++) {
        if (!wgr_input.fingers[i].down && !edges->touch_pressed[i] && !edges->touch_released[i]) continue;
        int j = count++;
        while (j > 0 && wgr_input.fingers[out[j - 1]].order > wgr_input.fingers[i].order) {
            out[j] = out[j - 1];
            j--;
        }
        out[j] = i;
    }
    return count;
}

WGRI_KEEP
int wgr_input_get_touch_count(void)
{
    int slots[WGR_INPUT_MAX_TOUCHES];
    return list_touches(slots);
}

WGRI_KEEP
wgr_touch_t wgr_input_get_touch(int index)
{
    int slots[WGR_INPUT_MAX_TOUCHES];
    const int count = list_touches(slots);
    const wgr_input_edges_t *edges = current_edges();
    int slot;

    if (index < 0 || index >= count) {
        return (wgr_touch_t){.id = -1, .state = WGR_BUTTON_UP};
    }
    slot = slots[index];
    return (wgr_touch_t){
        .id = slot,
        .x = wgr_input.fingers[slot].x,
        .y = wgr_input.fingers[slot].y,
        .dx = edges->touch_dx[slot],
        .dy = edges->touch_dy[slot],
        .state = button_state(wgr_input.fingers[slot].down, edges->touch_pressed[slot], edges->touch_released[slot]),
    };
}

WGRI_KEEP
wgr_touch_gesture_t wgr_input_get_touch_gesture(void)
{
    const wgr_input_edges_t *edges = current_edges();
    wgr_touch_gesture_t gesture = {.scale = expf(edges->gesture_log_scale)};
    int a, b;

    gesture.dx = edges->gesture_dx;
    gesture.dy = edges->gesture_dy;
    gesture.rotation = edges->gesture_rotation;
    if (gesture_pair(&a, &b)) {
        gesture.active = true;
        gesture.x = (wgr_input.fingers[a].x + wgr_input.fingers[b].x) * 0.5f;
        gesture.y = (wgr_input.fingers[a].y + wgr_input.fingers[b].y) * 0.5f;
    }
    return gesture;
}
