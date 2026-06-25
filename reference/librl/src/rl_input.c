#include "rl_input.h"

#include <raylib.h>

#include "internal/exports.h"
#include "rl_scratch.h"

RL_KEEP
void rl_input_poll_events(void) {
    PollInputEvents();
}

RL_KEEP
void rl_input_capture_cursor(void) {
    DisableCursor();
}

RL_KEEP
void rl_input_release_cursor(void) {
    EnableCursor();
}

// RL_KEEP is emscripten export, and we dont export this function for wasm
vec2_t rl_input_get_mouse_position() {
    const Vector2 pos = GetMousePosition();
    return (vec2_t){pos.x, pos.y};
}

// RL_KEEP is emscripten export, and we dont export this function for wasm
vec2_t rl_input_get_mouse_delta() {
    const Vector2 delta = GetMouseDelta();
    return (vec2_t){delta.x, delta.y};
}

RL_KEEP
void rl_input_get_mouse_position_to_scratch() {
    vec2_t pos = rl_input_get_mouse_position();
    rl_scratch_set_vector2(pos.x, pos.y);
}

RL_KEEP
int rl_input_get_mouse_wheel() {
    return (int)GetMouseWheelMove();
}

RL_KEEP
int rl_input_get_mouse_button(int button) {
    if (button < 0 || button >= RL_SCRATCH_MAX_NUM_MOUSE_BUTTONS) {
        return RL_BUTTON_UP;
    }
    if (IsMouseButtonPressed(button)) {
        return RL_BUTTON_PRESSED;
    }
    if (IsMouseButtonDown(button)) {
        return RL_BUTTON_DOWN;
    }
    if (IsMouseButtonReleased(button)) {
        return RL_BUTTON_RELEASED;
    }
    return RL_BUTTON_UP;
}

RL_KEEP
rl_mouse_state_t rl_input_get_mouse_state(void)
{
    vec2_t pos = rl_input_get_mouse_position();
    Vector2 delta = GetMouseDelta();
    rl_mouse_state_t state = {0};
    state.x = (int)pos.x;
    state.y = (int)pos.y;
    state.wheel = rl_input_get_mouse_wheel();
    state.left = rl_input_get_mouse_button(0);
    state.right = rl_input_get_mouse_button(1);
    state.middle = rl_input_get_mouse_button(2);
    state.buttons[0] = state.left;
    state.buttons[1] = state.right;
    state.buttons[2] = state.middle;
    state.dx = (int)delta.x;
    state.dy = (int)delta.y;
    return state;
}

RL_KEEP
rl_keyboard_state_t rl_input_get_keyboard_state(void)
{
    rl_keyboard_state_t state = {0};
    int max_keys = RL_KEYBOARD_MAX_KEYS;

    state.max_num_keys = max_keys;
    for (int i = 0; i < max_keys; i++) {
        if (IsKeyPressed(i)) {
            state.keys[i] = RL_BUTTON_PRESSED;
        } else if (IsKeyDown(i)) {
            state.keys[i] = RL_BUTTON_DOWN;
        } else if (IsKeyReleased(i)) {
            state.keys[i] = RL_BUTTON_RELEASED;
        } else {
            state.keys[i] = RL_BUTTON_UP;
        }
    }

    while (state.num_pressed_keys < RL_KEYBOARD_MAX_PRESSED_KEYS) {
        int key = GetKeyPressed();
        if (key == 0) {
            break;
        }
        state.pressed_key = key;
        state.pressed_keys[state.num_pressed_keys++] = key;
    }

    while (state.num_pressed_chars < RL_KEYBOARD_MAX_PRESSED_CHARS) {
        int ch = GetCharPressed();
        if (ch == 0) {
            break;
        }
        state.pressed_char = ch;
        state.pressed_chars[state.num_pressed_chars++] = ch;
    }

    return state;
}
