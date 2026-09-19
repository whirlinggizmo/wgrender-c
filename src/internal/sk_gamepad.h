#ifndef SK_INTERNAL_GAMEPAD_H
#define SK_INTERNAL_GAMEPAD_H

#include <stdbool.h>

#include "sk_input.h"

/* Gamepads (sk_gamepad.c; public API in sk_input.h): an optional module, polled at the
 * start of each frame, edges cleared after each tick and after the frame callback. */
void sk_gamepad_init(void);
void sk_gamepad_deinit(void);
void sk_gamepad_begin_frame(void);
void sk_gamepad_end_tick(void);
void sk_gamepad_frame_done(void);

/* For tests: report pad `pad` like this from the next frame on, instead of the
 * platform's pads (all of which are then ignored). */
void sk_gamepad_set_test_pad(int pad, bool connected, const char *name, const bool buttons[SK_GAMEPAD_BUTTON_COUNT],
                             const float axes[SK_GAMEPAD_AXIS_COUNT]);

#endif // SK_INTERNAL_GAMEPAD_H
