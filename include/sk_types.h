#ifndef SK_TYPES_H
#define SK_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

typedef unsigned int sk_handle_t;

/* Packed 8-bit RGBA, 0xRRGGBBAA — a value, not a handle (see sk_color.h). */
typedef uint32_t sk_color_t;

typedef enum
{
    SK_BUTTON_UP = 0,
    SK_BUTTON_PRESSED = 1,
    SK_BUTTON_DOWN = 2,
    SK_BUTTON_RELEASED = 3
} sk_button_state_t;

typedef struct
{
    float x;
    float y;
} vec2_t;

typedef struct
{
    int x;
    int y;
    float wheel;   /* vertical scroll this frame (or tick): about one unit per wheel notch,
                      fractional on trackpads and precision wheels */
    float wheel_x; /* horizontal scroll, same units */
    int left;
    int right;
    int middle;
    int buttons[3];
    int dx;
    int dy;
} sk_mouse_state_t;

#define SK_KEYBOARD_MAX_KEYS 512
#define SK_KEYBOARD_MAX_PRESSED_KEYS 32
#define SK_KEYBOARD_MAX_PRESSED_CHARS 32

typedef struct
{
    int max_num_keys;
    int keys[SK_KEYBOARD_MAX_KEYS];
    int pressed_key;
    int pressed_char;
    int num_pressed_keys;
    int pressed_keys[SK_KEYBOARD_MAX_PRESSED_KEYS];
    int num_pressed_chars;
    int pressed_chars[SK_KEYBOARD_MAX_PRESSED_CHARS];
} sk_keyboard_state_t;

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
    float m0, m4, m8, m12;
    float m1, m5, m9, m13;
    float m2, m6, m10, m14;
    float m3, m7, m11, m15;
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
    float x;
    float y;
    float width;
    float height;
} rect_t;

typedef struct
{
    bool hit;
    sk_handle_t handle;
    float distance; /* world-space distance from ray origin to hit */
    vec3_t point_local;
    vec3_t point_world;
    vec3_t normal_local;
    vec3_t normal_world;
} sk_pick_result_t;

#ifdef __cplusplus
}
#endif

#endif // SK_TYPES_H
