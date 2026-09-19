#ifndef SK_INPUT_H
#define SK_INPUT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sk_keys.h"
#include "sk_types.h"

void sk_input_capture_cursor(void);
void sk_input_release_cursor(void);
vec2_t sk_input_get_mouse_position(void);
vec2_t sk_input_get_mouse_delta(void);
float sk_input_get_mouse_wheel(void);   /* this frame (or tick); fractional on trackpads */
float sk_input_get_mouse_wheel_x(void); /* horizontal */
int sk_input_get_mouse_button(int button);
sk_mouse_state_t sk_input_get_mouse_state(void);
int sk_input_get_key(sk_keycode_t key); /* SK_BUTTON_*; SK_BUTTON_UP for an unknown key */
sk_keyboard_state_t sk_input_get_keyboard_state(void);

/* Touch
 * -----
 * The first finger also drives the pointer (position and left button), so UI and scene
 * interaction work by touch. A second finger cancels that press: the pointer is
 * released off-screen, at (-1, -1), so nothing under it is clicked, and it stays up
 * until every finger has lifted.
 *
 * Fingers, and the two-finger gesture, have edges and deltas like the mouse: since the
 * previous frame in the frame callback, since the previous tick in a tick. */
#define SK_INPUT_MAX_TOUCHES 8

typedef struct {
    int id;       /* stable while the finger is down (0 .. SK_INPUT_MAX_TOUCHES - 1);
                     reused once it has lifted */
    float x, y;   /* logical pixels, like the mouse */
    float dx, dy; /* moved this frame (or tick) */
    int state;    /* SK_BUTTON_PRESSED, _DOWN or _RELEASED */
} sk_touch_t;

/* Two fingers: the first two down, while both are. */
typedef struct {
    bool active;    /* two or more fingers down */
    float x, y;     /* the point between them */
    float dx, dy;   /* two-finger pan this frame (or tick) */
    float scale;    /* pinch this frame (or tick): the ratio of their distances, 1 = none */
    float rotation; /* twist this frame (or tick), radians, clockwise on screen */
} sk_touch_gesture_t;

/* Fingers down, plus those lifted this frame (or tick); sk_input_get_touch(0 .. count - 1)
 * reads them, oldest first. */
int sk_input_get_touch_count(void);
sk_touch_t sk_input_get_touch(int index);
sk_touch_gesture_t sk_input_get_touch_gesture(void);

/* Gamepads
 * --------
 * Up to SK_INPUT_MAX_GAMEPADS at once. A pad keeps its slot (0 .. 3) while it's
 * connected; a new one takes the lowest free slot, so pad 0 stays "player 1" until
 * it's unplugged. Buttons are named by position on an Xbox-style layout (SOUTH is A
 * on Xbox, cross on PlayStation, B on Switch) and have edges like keys: since the
 * previous frame in the frame callback, since the previous tick in a tick. Sticks run
 * -1 .. 1 with y down, like the screen, with a dead zone (sk_input_set_gamepad_deadzone);
 * triggers 0 .. 1, and also count as buttons past half-way.
 *
 * Web: the browser lists a gamepad only after one of its buttons is pressed on the
 * page. Linux: evdev (the kernel's input devices), checked for new ones every couple
 * of seconds. Windows: XInput. macOS: none yet. */
#define SK_INPUT_MAX_GAMEPADS 4

typedef enum {
    SK_GAMEPAD_BUTTON_SOUTH,
    SK_GAMEPAD_BUTTON_EAST,
    SK_GAMEPAD_BUTTON_WEST,
    SK_GAMEPAD_BUTTON_NORTH,
    SK_GAMEPAD_BUTTON_LEFT_BUMPER,
    SK_GAMEPAD_BUTTON_RIGHT_BUMPER,
    SK_GAMEPAD_BUTTON_LEFT_TRIGGER,
    SK_GAMEPAD_BUTTON_RIGHT_TRIGGER,
    SK_GAMEPAD_BUTTON_BACK,  /* view / select / share / minus */
    SK_GAMEPAD_BUTTON_START, /* menu / options / plus */
    SK_GAMEPAD_BUTTON_GUIDE, /* the logo button */
    SK_GAMEPAD_BUTTON_LEFT_STICK,
    SK_GAMEPAD_BUTTON_RIGHT_STICK,
    SK_GAMEPAD_BUTTON_DPAD_UP,
    SK_GAMEPAD_BUTTON_DPAD_DOWN,
    SK_GAMEPAD_BUTTON_DPAD_LEFT,
    SK_GAMEPAD_BUTTON_DPAD_RIGHT,
    SK_GAMEPAD_BUTTON_COUNT
} sk_gamepad_button_t;

typedef enum {
    SK_GAMEPAD_AXIS_LEFT_X,
    SK_GAMEPAD_AXIS_LEFT_Y,
    SK_GAMEPAD_AXIS_RIGHT_X,
    SK_GAMEPAD_AXIS_RIGHT_Y,
    SK_GAMEPAD_AXIS_LEFT_TRIGGER,
    SK_GAMEPAD_AXIS_RIGHT_TRIGGER,
    SK_GAMEPAD_AXIS_COUNT
} sk_gamepad_axis_t;

bool sk_input_is_gamepad_connected(int pad);
const char *sk_input_get_gamepad_name(int pad); /* "" when none */
int sk_input_get_gamepad_button(int pad, sk_gamepad_button_t button); /* SK_BUTTON_* */
float sk_input_get_gamepad_axis(int pad, sk_gamepad_axis_t axis);
/* Sticks: how far from the middle counts as the middle (0 .. 0.9; default 0.15).
 * Past it, values rescale to reach 1 at the edge. */
bool sk_input_set_gamepad_deadzone(float radius);

/* Whether game controls (camera drags, 3D selection, hotkeys) should leave the pointer
 * or the keyboard alone because a UI has it. Advisory: libsk keeps reporting input;
 * game code checks these first.
 *
 * The pointer is captured while either:
 * - the current press (left button, or the primary touch) started on a 2D member of an
 *   interactive scene (sk_scene_set_interactive), from the frame it was pressed through
 *   the frame it's released; or
 * - the game's UI says so with sk_input_set_pointer_captured.
 *
 * The UI's captures are sticky: they stay until the UI changes them. Set them every
 * frame from the UI's own hit-testing (pointer over UI, or a press that started on UI
 * still held; a text field with focus for the keyboard). A UI lays out in the frame
 * callback, after that frame's ticks, so ticks see the previous frame's value. */
bool sk_input_is_pointer_captured(void);
void sk_input_set_pointer_captured(bool captured);
bool sk_input_is_keyboard_captured(void);
void sk_input_set_keyboard_captured(bool captured);

#ifdef __cplusplus
}
#endif

#endif // SK_INPUT_H
