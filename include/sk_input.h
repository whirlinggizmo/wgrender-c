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
int sk_input_get_mouse_wheel(void);
int sk_input_get_mouse_button(int button);
sk_mouse_state_t sk_input_get_mouse_state(void);
sk_keyboard_state_t sk_input_get_keyboard_state(void);

#ifdef __cplusplus
}
#endif

#endif // SK_INPUT_H
