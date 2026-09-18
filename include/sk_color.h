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

#define SK_COLOR_LIGHTGRAY  0xC8C8C8FFu  /* 200, 200, 200, 255 */
#define SK_COLOR_GRAY       0x828282FFu  /* 130, 130, 130, 255 */
#define SK_COLOR_DARKGRAY   0x505050FFu  /*  80,  80,  80, 255 */
#define SK_COLOR_YELLOW     0xFFFF00FFu  /* 255, 255,   0, 255 */
#define SK_COLOR_GOLD       0xFFCB00FFu  /* 255, 203,   0, 255 */
#define SK_COLOR_ORANGE     0xFFA100FFu  /* 255, 161,   0, 255 */
#define SK_COLOR_PINK       0xFF6DC2FFu  /* 255, 109, 194, 255 */
#define SK_COLOR_RED        0xE62937FFu  /* 230,  41,  55, 255 */
#define SK_COLOR_MAROON     0xBE212DFFu  /* 190,  33,  45, 255 */
#define SK_COLOR_GREEN      0x00E430FFu  /*   0, 228,  48, 255 */
#define SK_COLOR_LIME       0x009E2FFFu  /*   0, 158,  47, 255 */
#define SK_COLOR_DARKGREEN  0x00752CFFu  /*   0, 117,  44, 255 */
#define SK_COLOR_SKYBLUE    0x66BFFFFFu  /* 102, 191, 255, 255 */
#define SK_COLOR_BLUE       0x0079F1FFu  /*   0, 121, 241, 255 */
#define SK_COLOR_DARKBLUE   0x0052ACFFu  /*   0,  82, 172, 255 */
#define SK_COLOR_PURPLE     0xC87AFFFFu  /* 200, 122, 255, 255 */
#define SK_COLOR_VIOLET     0x873CBEFFu  /* 135,  60, 190, 255 */
#define SK_COLOR_DARKPURPLE 0x701F7EFFu  /* 112,  31, 126, 255 */
#define SK_COLOR_BEIGE      0xD3B083FFu  /* 211, 176, 131, 255 */
#define SK_COLOR_BROWN      0x7F6A4FFFu  /* 127, 106,  79, 255 */
#define SK_COLOR_DARKBROWN  0x4C3F2FFFu  /*  76,  63,  47, 255 */
#define SK_COLOR_WHITE      0xFFFFFFFFu  /* 255, 255, 255, 255 */
#define SK_COLOR_BLACK      0x000000FFu  /*   0,   0,   0, 255 */
#define SK_COLOR_BLANK      0x00000000u  /*   0,   0,   0,   0  transparent */
#define SK_COLOR_MAGENTA    0xFF00FFFFu  /* 255,   0, 255, 255 */
#define SK_COLOR_RAYWHITE   0xF5F5F5FFu  /* 245, 245, 245, 255 */

/* Build a color from components, clamped to range: 0..255 for sk_color_rgba,
 * 0..1 for sk_color_rgbaf (which rounds to the nearest 8-bit step). Out-of-range
 * components saturate; they never wrap into the neighbouring channel. */
sk_color_t sk_color_rgba(int r, int g, int b, int a);
sk_color_t sk_color_rgbaf(float r, float g, float b, float a);
sk_color_t sk_color_with_alpha(sk_color_t color, int a);

/* Components back out, 0..255. */
int sk_color_get_red(sk_color_t color);
int sk_color_get_green(sk_color_t color);
int sk_color_get_blue(sk_color_t color);
int sk_color_get_alpha(sk_color_t color);

/* Straight-line blend of two colors, component by component; t is clamped to
 * 0..1 (0 gives `from`, 1 gives `to`). */
sk_color_t sk_color_lerp(sk_color_t from, sk_color_t to, float t);

#ifdef __cplusplus
}
#endif

#endif // SK_COLOR_H
