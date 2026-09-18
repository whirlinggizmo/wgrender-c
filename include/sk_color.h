#ifndef SK_COLOR_H
#define SK_COLOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sk_types.h"

/* A color is a value, not a handle: `sk_color_t` packs 8-bit RGBA as 0xRRGGBBAA,
 * so 0xFF0000FF is opaque red and 0x00000000 is fully transparent. There is
 * nothing to create, destroy or run out of — write a literal, use one of the
 * built-ins below, or build one with the helpers.
 *
 * Note that 0 is transparent black (SK_COLOR_BLANK), not "unset": a tint that
 * changes nothing is SK_COLOR_WHITE. */

#define SK_COLOR_LIGHTGRAY  0xC8C8C8FFu
#define SK_COLOR_GRAY       0x828282FFu
#define SK_COLOR_DARKGRAY   0x505050FFu
#define SK_COLOR_YELLOW     0xFFFF00FFu
#define SK_COLOR_GOLD       0xFFCB00FFu
#define SK_COLOR_ORANGE     0xFFA100FFu
#define SK_COLOR_PINK       0xFF6DC2FFu
#define SK_COLOR_RED        0xE62937FFu
#define SK_COLOR_MAROON     0xBE212DFFu
#define SK_COLOR_GREEN      0x00E430FFu
#define SK_COLOR_LIME       0x009E2FFFu
#define SK_COLOR_DARKGREEN  0x00752CFFu
#define SK_COLOR_SKYBLUE    0x66BFFFFFu
#define SK_COLOR_BLUE       0x0079F1FFu
#define SK_COLOR_DARKBLUE   0x0052ACFFu
#define SK_COLOR_PURPLE     0xC87AFFFFu
#define SK_COLOR_VIOLET     0x873CBEFFu
#define SK_COLOR_DARKPURPLE 0x701F7EFFu
#define SK_COLOR_BEIGE      0xD3B083FFu
#define SK_COLOR_BROWN      0x7F6A4FFFu
#define SK_COLOR_DARKBROWN  0x4C3F2FFFu
#define SK_COLOR_WHITE      0xFFFFFFFFu
#define SK_COLOR_BLACK      0x000000FFu
#define SK_COLOR_BLANK      0x00000000u /* transparent */
#define SK_COLOR_MAGENTA    0xFF00FFFFu
#define SK_COLOR_RAYWHITE   0xF5F5F5FFu

/* Components are 0..255 and clamped. */
sk_color_t sk_color_rgba(int r, int g, int b, int a);
sk_color_t sk_color_with_alpha(sk_color_t color, int a);

/* Straight-line blend of two colors, component by component; t is clamped to
 * 0..1 (0 gives `from`, 1 gives `to`). */
sk_color_t sk_color_lerp(sk_color_t from, sk_color_t to, float t);

#ifdef __cplusplus
}
#endif

#endif // SK_COLOR_H
