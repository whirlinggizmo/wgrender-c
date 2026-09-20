#ifndef WGR_INTERNAL_COLOR_H
#define WGR_INTERNAL_COLOR_H

#include <wgr_color.h> /* the public header ("" would find this file): wgr_color_t */
#include "wgr_types.h"

/* The renderer's color: normalized 0..1 components, because that's what sokol_gl
 * takes and what the sRGB -> linear conversion needs. The public `wgr_color_t` is
 * the packed 8-bit form; this is what it unpacks to. */
typedef struct {
    float r;
    float g;
    float b;
    float a;
} wgr_colorf_t;

wgr_colorf_t wgr_color_unpack(wgr_color_t color);

#endif // WGR_INTERNAL_COLOR_H
