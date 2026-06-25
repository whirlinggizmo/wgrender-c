#ifndef RL_TYPES_H
#define RL_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int rl_handle_t;

typedef enum
{
    RL_BUTTON_UP = 0,
    RL_BUTTON_PRESSED = 1,
    RL_BUTTON_DOWN = 2,
    RL_BUTTON_RELEASED = 3
} rl_button_state_t;

typedef struct
{
    float x;
    float y;
} vec2_t;

typedef struct
{
    int x;
    int y;
    int wheel;
    int left;
    int right;
    int middle;
    int buttons[3];
    int dx;
    int dy;
} rl_mouse_state_t;

#define RL_KEYBOARD_MAX_KEYS 512
#define RL_KEYBOARD_MAX_PRESSED_KEYS 32
#define RL_KEYBOARD_MAX_PRESSED_CHARS 32

typedef struct
{
    int max_num_keys;
    int keys[RL_KEYBOARD_MAX_KEYS];
    int pressed_key;
    int pressed_char;
    int num_pressed_keys;
    int pressed_keys[RL_KEYBOARD_MAX_PRESSED_KEYS];
    int num_pressed_chars;
    int pressed_chars[RL_KEYBOARD_MAX_PRESSED_CHARS];
} rl_keyboard_state_t;

typedef struct
{
    float x;
    float y;
    float z;
} vec3_t;

typedef struct
{
    float x;
    float y;
    float z;
    float w;
} vec4_t;

typedef struct
{
    float m0;
    float m4;
    float m8;
    float m12;
    float m1;
    float m5;
    float m9;
    float m13;
    float m2;
    float m6;
    float m10;
    float m14;
    float m3;
    float m7;
    float m11;
    float m15;
} matrix_t;

typedef struct
{
    float x;
    float y;
    float z;
    float w;
} quat_t;

typedef struct
{
    float r;
    float g;
    float b;
    float a;
} color_t;

typedef struct
{
    float x;
    float y;
    float width;
    float height;
} rect_t;

#ifdef __cplusplus
}
#endif

#endif // RL_TYPES_H
