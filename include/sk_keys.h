#ifndef SK_KEYS_H
#define SK_KEYS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Key codes used to index sk_keyboard_state_t.keys[]. These are libsk's public
 * key identifiers; consumers should use these names rather than raw integers.
 * (The numeric values currently follow the GLFW/sokol layout, but that is an
 * implementation detail — code to the SK_KEY_* names.) */
typedef enum sk_keycode_t {
    SK_KEY_SPACE = 32,
    SK_KEY_APOSTROPHE = 39,
    SK_KEY_COMMA = 44,
    SK_KEY_MINUS = 45,
    SK_KEY_PERIOD = 46,
    SK_KEY_SLASH = 47,
    SK_KEY_0 = 48,
    SK_KEY_1 = 49,
    SK_KEY_2 = 50,
    SK_KEY_3 = 51,
    SK_KEY_4 = 52,
    SK_KEY_5 = 53,
    SK_KEY_6 = 54,
    SK_KEY_7 = 55,
    SK_KEY_8 = 56,
    SK_KEY_9 = 57,
    SK_KEY_SEMICOLON = 59,
    SK_KEY_EQUAL = 61,
    SK_KEY_A = 65,
    SK_KEY_B = 66,
    SK_KEY_C = 67,
    SK_KEY_D = 68,
    SK_KEY_E = 69,
    SK_KEY_F = 70,
    SK_KEY_G = 71,
    SK_KEY_H = 72,
    SK_KEY_I = 73,
    SK_KEY_J = 74,
    SK_KEY_K = 75,
    SK_KEY_L = 76,
    SK_KEY_M = 77,
    SK_KEY_N = 78,
    SK_KEY_O = 79,
    SK_KEY_P = 80,
    SK_KEY_Q = 81,
    SK_KEY_R = 82,
    SK_KEY_S = 83,
    SK_KEY_T = 84,
    SK_KEY_U = 85,
    SK_KEY_V = 86,
    SK_KEY_W = 87,
    SK_KEY_X = 88,
    SK_KEY_Y = 89,
    SK_KEY_Z = 90,
    SK_KEY_LEFT_BRACKET = 91,
    SK_KEY_BACKSLASH = 92,
    SK_KEY_RIGHT_BRACKET = 93,
    SK_KEY_GRAVE_ACCENT = 96,

    SK_KEY_ESCAPE = 256,
    SK_KEY_ENTER = 257,
    SK_KEY_TAB = 258,
    SK_KEY_BACKSPACE = 259,
    SK_KEY_INSERT = 260,
    SK_KEY_DELETE = 261,
    SK_KEY_RIGHT = 262,
    SK_KEY_LEFT = 263,
    SK_KEY_DOWN = 264,
    SK_KEY_UP = 265,
    SK_KEY_PAGE_UP = 266,
    SK_KEY_PAGE_DOWN = 267,
    SK_KEY_HOME = 268,
    SK_KEY_END = 269,
    SK_KEY_CAPS_LOCK = 280,
    SK_KEY_F1 = 290,
    SK_KEY_F2 = 291,
    SK_KEY_F3 = 292,
    SK_KEY_F4 = 293,
    SK_KEY_F5 = 294,
    SK_KEY_F6 = 295,
    SK_KEY_F7 = 296,
    SK_KEY_F8 = 297,
    SK_KEY_F9 = 298,
    SK_KEY_F10 = 299,
    SK_KEY_F11 = 300,
    SK_KEY_F12 = 301,
    SK_KEY_LEFT_SHIFT = 340,
    SK_KEY_LEFT_CONTROL = 341,
    SK_KEY_LEFT_ALT = 342,
    SK_KEY_LEFT_SUPER = 343,
    SK_KEY_RIGHT_SHIFT = 344,
    SK_KEY_RIGHT_CONTROL = 345,
    SK_KEY_RIGHT_ALT = 346,
    SK_KEY_RIGHT_SUPER = 347,
} sk_keycode_t;

/* Mouse button indices into sk_mouse_state_t.buttons[]. */
typedef enum sk_mouse_button_t {
    SK_MOUSE_BUTTON_LEFT = 0,
    SK_MOUSE_BUTTON_RIGHT = 1,
    SK_MOUSE_BUTTON_MIDDLE = 2,
} sk_mouse_button_t;

#ifdef __cplusplus
}
#endif

#endif // SK_KEYS_H
