#ifndef SK_INPUT_H
#define SK_INPUT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sk_types.h"

void sk_input_capture_cursor(void);
void sk_input_release_cursor(void);
vec2_t sk_input_get_mouse_position(void);
vec2_t sk_input_get_mouse_delta(void);
float sk_input_get_mouse_wheel(void);   /* this frame (or tick); fractional on trackpads */
float sk_input_get_mouse_wheel_x(void); /* horizontal */
int sk_input_get_mouse_button(int button);
sk_mouse_state_t sk_input_get_mouse_state(void);
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
