#ifndef WGRI_INTERNAL_GAMEPAD_H
#define WGRI_INTERNAL_GAMEPAD_H

#include <stdbool.h>

#include "wgr_input.h"

/* Gamepads (wgr_gamepad.c; public API in wgr_input.h): an optional module, polled at the
 * start of each frame, edges cleared after each tick and after the frame callback. */
void wgri_gamepad_init(void);
void wgri_gamepad_deinit(void);
void wgri_gamepad_begin_frame(void);
void wgri_gamepad_end_tick(void);
void wgri_gamepad_frame_done(void);

/* For tests: report pad `pad` like this from the next frame on, instead of the
 * platform's pads (all of which are then ignored). */
void wgri_gamepad_set_test_pad(int pad, bool connected, const char *name, const bool buttons[WGR_GAMEPAD_BUTTON_COUNT],
                             const float axes[WGR_GAMEPAD_AXIS_COUNT]);

#endif // WGRI_INTERNAL_GAMEPAD_H
