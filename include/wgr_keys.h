#ifndef WGR_KEYS_H
#define WGR_KEYS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Key codes used to index wgr_keyboard_state_t.keys[]. These are libwgrender's public
 * key identifiers; consumers should use these names rather than raw integers.
 * (The numeric values currently follow the GLFW/sokol layout, but that is an
 * implementation detail — code to the WGR_KEY_* names.) */
typedef enum wgr_keycode_t {
    WGR_KEY_SPACE = 32,
    WGR_KEY_APOSTROPHE = 39,
    WGR_KEY_COMMA = 44,
    WGR_KEY_MINUS = 45,
    WGR_KEY_PERIOD = 46,
    WGR_KEY_SLASH = 47,
    WGR_KEY_0 = 48,
    WGR_KEY_1 = 49,
    WGR_KEY_2 = 50,
    WGR_KEY_3 = 51,
    WGR_KEY_4 = 52,
    WGR_KEY_5 = 53,
    WGR_KEY_6 = 54,
    WGR_KEY_7 = 55,
    WGR_KEY_8 = 56,
    WGR_KEY_9 = 57,
    WGR_KEY_SEMICOLON = 59,
    WGR_KEY_EQUAL = 61,
    WGR_KEY_A = 65,
    WGR_KEY_B = 66,
    WGR_KEY_C = 67,
    WGR_KEY_D = 68,
    WGR_KEY_E = 69,
    WGR_KEY_F = 70,
    WGR_KEY_G = 71,
    WGR_KEY_H = 72,
    WGR_KEY_I = 73,
    WGR_KEY_J = 74,
    WGR_KEY_K = 75,
    WGR_KEY_L = 76,
    WGR_KEY_M = 77,
    WGR_KEY_N = 78,
    WGR_KEY_O = 79,
    WGR_KEY_P = 80,
    WGR_KEY_Q = 81,
    WGR_KEY_R = 82,
    WGR_KEY_S = 83,
    WGR_KEY_T = 84,
    WGR_KEY_U = 85,
    WGR_KEY_V = 86,
    WGR_KEY_W = 87,
    WGR_KEY_X = 88,
    WGR_KEY_Y = 89,
    WGR_KEY_Z = 90,
    WGR_KEY_LEFT_BRACKET = 91,
    WGR_KEY_BACKSLASH = 92,
    WGR_KEY_RIGHT_BRACKET = 93,
    WGR_KEY_GRAVE_ACCENT = 96,

    WGR_KEY_ESCAPE = 256,
    WGR_KEY_ENTER = 257,
    WGR_KEY_TAB = 258,
    WGR_KEY_BACKSPACE = 259,
    WGR_KEY_INSERT = 260,
    WGR_KEY_DELETE = 261,
    WGR_KEY_RIGHT = 262,
    WGR_KEY_LEFT = 263,
    WGR_KEY_DOWN = 264,
    WGR_KEY_UP = 265,
    WGR_KEY_PAGE_UP = 266,
    WGR_KEY_PAGE_DOWN = 267,
    WGR_KEY_HOME = 268,
    WGR_KEY_END = 269,
    WGR_KEY_CAPS_LOCK = 280,
    WGR_KEY_F1 = 290,
    WGR_KEY_F2 = 291,
    WGR_KEY_F3 = 292,
    WGR_KEY_F4 = 293,
    WGR_KEY_F5 = 294,
    WGR_KEY_F6 = 295,
    WGR_KEY_F7 = 296,
    WGR_KEY_F8 = 297,
    WGR_KEY_F9 = 298,
    WGR_KEY_F10 = 299,
    WGR_KEY_F11 = 300,
    WGR_KEY_F12 = 301,
    WGR_KEY_LEFT_SHIFT = 340,
    WGR_KEY_LEFT_CONTROL = 341,
    WGR_KEY_LEFT_ALT = 342,
    WGR_KEY_LEFT_SUPER = 343,
    WGR_KEY_RIGHT_SHIFT = 344,
    WGR_KEY_RIGHT_CONTROL = 345,
    WGR_KEY_RIGHT_ALT = 346,
    WGR_KEY_RIGHT_SUPER = 347,
} wgr_keycode_t;

/* Mouse button indices into wgr_mouse_state_t.buttons[]. */
typedef enum wgr_mouse_button_t {
    WGR_MOUSE_BUTTON_LEFT = 0,
    WGR_MOUSE_BUTTON_RIGHT = 1,
    WGR_MOUSE_BUTTON_MIDDLE = 2,
} wgr_mouse_button_t;

#ifdef __cplusplus
}
#endif

#endif // WGR_KEYS_H
