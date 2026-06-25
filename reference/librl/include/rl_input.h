#ifndef RL_INPUT_H
#define RL_INPUT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "rl_types.h"

void rl_input_poll_events(void);
void rl_input_capture_cursor(void);
void rl_input_release_cursor(void);
vec2_t rl_input_get_mouse_position(void);
vec2_t rl_input_get_mouse_delta(void);
int rl_input_get_mouse_wheel(void);
int rl_input_get_mouse_button(int button);
rl_mouse_state_t rl_input_get_mouse_state(void);
rl_keyboard_state_t rl_input_get_keyboard_state(void);

#ifdef __cplusplus
}
#endif

#endif // RL_INPUT_H
