#ifndef SK_COLOR_H
#define SK_COLOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sk_types.h"

extern const sk_handle_t SK_COLOR_DEFAULT;
extern const sk_handle_t SK_COLOR_LIGHTGRAY;
extern const sk_handle_t SK_COLOR_GRAY;
extern const sk_handle_t SK_COLOR_DARKGRAY;
extern const sk_handle_t SK_COLOR_YELLOW;
extern const sk_handle_t SK_COLOR_GOLD;
extern const sk_handle_t SK_COLOR_ORANGE;
extern const sk_handle_t SK_COLOR_PINK;
extern const sk_handle_t SK_COLOR_RED;
extern const sk_handle_t SK_COLOR_MAROON;
extern const sk_handle_t SK_COLOR_GREEN;
extern const sk_handle_t SK_COLOR_LIME;
extern const sk_handle_t SK_COLOR_DARKGREEN;
extern const sk_handle_t SK_COLOR_SKYBLUE;
extern const sk_handle_t SK_COLOR_BLUE;
extern const sk_handle_t SK_COLOR_DARKBLUE;
extern const sk_handle_t SK_COLOR_PURPLE;
extern const sk_handle_t SK_COLOR_VIOLET;
extern const sk_handle_t SK_COLOR_DARKPURPLE;
extern const sk_handle_t SK_COLOR_BEIGE;
extern const sk_handle_t SK_COLOR_BROWN;
extern const sk_handle_t SK_COLOR_DARKBROWN;
extern const sk_handle_t SK_COLOR_WHITE;
extern const sk_handle_t SK_COLOR_BLACK;
extern const sk_handle_t SK_COLOR_BLANK;
extern const sk_handle_t SK_COLOR_MAGENTA;
extern const sk_handle_t SK_COLOR_RAYWHITE;

sk_handle_t sk_color_create(int r, int g, int b, int a);
void sk_color_destroy(sk_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // SK_COLOR_H
