#ifndef WGR_INPUT_H
#define WGR_INPUT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "wgr_keys.h"
#include "wgr_types.h"

void wgr_input_capture_cursor(void);
void wgr_input_release_cursor(void);
vec2_t wgr_input_get_mouse_position(void);
vec2_t wgr_input_get_mouse_delta(void);
float wgr_input_get_mouse_wheel(void);   /* this frame (or tick); fractional on trackpads */
float wgr_input_get_mouse_wheel_x(void); /* horizontal */
int wgr_input_get_mouse_button(int button);
wgr_mouse_state_t wgr_input_get_mouse_state(void);
int wgr_input_get_key(wgr_keycode_t key); /* WGR_BUTTON_*; WGR_BUTTON_UP for an unknown key */
wgr_keyboard_state_t wgr_input_get_keyboard_state(void);

/* Touch
 * -----
 * The first finger also drives the pointer (position and left button), so UI and scene
 * interaction work by touch. A second finger cancels that press: the pointer is
 * released off-screen, at (-1, -1), so nothing under it is clicked, and it stays up
 * until every finger has lifted.
 *
 * Fingers, and the two-finger gesture, have edges and deltas like the mouse: since the
 * previous frame in the frame callback, since the previous tick in a tick. */
#define WGR_INPUT_MAX_TOUCHES 8

typedef struct {
    int id;       /* stable while the finger is down (0 .. WGR_INPUT_MAX_TOUCHES - 1);
                     reused once it has lifted */
    float x, y;   /* logical pixels, like the mouse */
    float dx, dy; /* moved this frame (or tick) */
    int state;    /* WGR_BUTTON_PRESSED, _DOWN or _RELEASED */
} wgr_touch_t;

/* Two fingers: the first two down, while both are. */
typedef struct {
    bool active;    /* two or more fingers down */
    float x, y;     /* the point between them */
    float dx, dy;   /* two-finger pan this frame (or tick) */
    float scale;    /* pinch this frame (or tick): the ratio of their distances, 1 = none */
    float rotation; /* twist this frame (or tick), radians, clockwise on screen */
} wgr_touch_gesture_t;

/* Fingers down, plus those lifted this frame (or tick); wgr_input_get_touch(0 .. count - 1)
 * reads them, oldest first. */
int wgr_input_get_touch_count(void);
wgr_touch_t wgr_input_get_touch(int index);
wgr_touch_gesture_t wgr_input_get_touch_gesture(void);

/* Gamepads
 * --------
 * Up to WGR_INPUT_MAX_GAMEPADS at once. A pad keeps its slot (0 .. 3) while it's
 * connected; a new one takes the lowest free slot, so pad 0 stays "player 1" until
 * it's unplugged. Buttons are named by position on an Xbox-style layout (SOUTH is A
 * on Xbox, cross on PlayStation, B on Switch) and have edges like keys: since the
 * previous frame in the frame callback, since the previous tick in a tick. Sticks run
 * -1 .. 1 with y down, like the screen, with a dead zone (wgr_input_set_gamepad_deadzone);
 * triggers 0 .. 1, and also count as buttons past half-way.
 *
 * Web: the browser lists a gamepad only after one of its buttons is pressed on the
 * page. Linux: evdev (the kernel's input devices), checked for new ones every couple
 * of seconds. Windows: XInput. macOS: none yet. */
#define WGR_INPUT_MAX_GAMEPADS 4

typedef enum {
    WGR_GAMEPAD_BUTTON_SOUTH,
    WGR_GAMEPAD_BUTTON_EAST,
    WGR_GAMEPAD_BUTTON_WEST,
    WGR_GAMEPAD_BUTTON_NORTH,
    WGR_GAMEPAD_BUTTON_LEFT_BUMPER,
    WGR_GAMEPAD_BUTTON_RIGHT_BUMPER,
    WGR_GAMEPAD_BUTTON_LEFT_TRIGGER,
    WGR_GAMEPAD_BUTTON_RIGHT_TRIGGER,
    WGR_GAMEPAD_BUTTON_BACK,  /* view / select / share / minus */
    WGR_GAMEPAD_BUTTON_START, /* menu / options / plus */
    WGR_GAMEPAD_BUTTON_GUIDE, /* the logo button */
    WGR_GAMEPAD_BUTTON_LEFT_STICK,
    WGR_GAMEPAD_BUTTON_RIGHT_STICK,
    WGR_GAMEPAD_BUTTON_DPAD_UP,
    WGR_GAMEPAD_BUTTON_DPAD_DOWN,
    WGR_GAMEPAD_BUTTON_DPAD_LEFT,
    WGR_GAMEPAD_BUTTON_DPAD_RIGHT,
    WGR_GAMEPAD_BUTTON_COUNT
} wgr_gamepad_button_t;

typedef enum {
    WGR_GAMEPAD_AXIS_LEFT_X,
    WGR_GAMEPAD_AXIS_LEFT_Y,
    WGR_GAMEPAD_AXIS_RIGHT_X,
    WGR_GAMEPAD_AXIS_RIGHT_Y,
    WGR_GAMEPAD_AXIS_LEFT_TRIGGER,
    WGR_GAMEPAD_AXIS_RIGHT_TRIGGER,
    WGR_GAMEPAD_AXIS_COUNT
} wgr_gamepad_axis_t;

bool wgr_input_is_gamepad_connected(int pad);
const char *wgr_input_get_gamepad_name(int pad); /* "" when none */
int wgr_input_get_gamepad_button(int pad, wgr_gamepad_button_t button); /* WGR_BUTTON_* */
float wgr_input_get_gamepad_axis(int pad, wgr_gamepad_axis_t axis);
/* Sticks: how far from the middle counts as the middle (0 .. 0.9; default 0.15).
 * Past it, values rescale to reach 1 at the edge. False outside that range. */
bool wgr_input_set_gamepad_deadzone(float radius);

/* Whether game controls (camera drags, 3D selection, hotkeys) should leave the pointer
 * or the keyboard alone because a UI has it. Advisory: libwgrender keeps reporting input;
 * game code checks these first.
 *
 * The pointer is captured while either:
 * - the current press (left button, or the primary touch) started on a 2D member of an
 *   interactive scene (wgr_scene_set_interactive), from the frame it was pressed through
 *   the frame it's released; or
 * - the game's UI says so with wgr_input_set_pointer_captured.
 *
 * The UI's captures are sticky: they stay until the UI changes them. Set them every
 * frame from the UI's own hit-testing (pointer over UI, or a press that started on UI
 * still held; a text field with focus for the keyboard). A UI lays out in the frame
 * callback, after that frame's ticks, so ticks see the previous frame's value. */
bool wgr_input_is_pointer_captured(void);
void wgr_input_set_pointer_captured(bool captured);
bool wgr_input_is_keyboard_captured(void);
void wgr_input_set_keyboard_captured(bool captured);

#ifdef __cplusplus
}
#endif

#endif // WGR_INPUT_H
