#ifndef WGRI_INTERNAL_COLOR_H
#define WGRI_INTERNAL_COLOR_H

#include "wgr_color.h"
#include "wgr_types.h"

/* The renderer's color: normalized 0..1 components, because that's what sokol_gl
 * takes and what the sRGB -> linear conversion needs. The public `wgr_color_t` is
 * the packed 8-bit form; this is what it unpacks to. */
typedef struct {
    float r;
    float g;
    float b;
    float a;
} wgri_colorf_t;

wgri_colorf_t wgri_color_unpack(wgr_color_t color);

#endif // WGRI_INTERNAL_COLOR_H
