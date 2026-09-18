#ifndef SK_INTERNAL_COLOR_H
#define SK_INTERNAL_COLOR_H

#include <sk_color.h> /* the public header ("" would find this file): sk_color_t */
#include "sk_types.h"

/* The renderer's color: normalized 0..1 components, because that's what sokol_gl
 * takes and what the sRGB -> linear conversion needs. The public `sk_color_t` is
 * the packed 8-bit form; this is what it unpacks to. */
typedef struct {
    float r;
    float g;
    float b;
    float a;
} sk_colorf_t;

sk_colorf_t sk_color_unpack(sk_color_t color);

#endif // SK_INTERNAL_COLOR_H
