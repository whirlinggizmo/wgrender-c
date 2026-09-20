#include "wgr_color.h"

#include "internal/exports_internal.h"
#include "internal/wgr_color_internal.h"

/* Colors are plain values (0xRRGGBBAA) — no pool, no handles, no lifecycle. They
 * were handles in librl, which said in rl_color.c that colors are "tiny value
 * objects" without shared-asset semantics; making them values says the same thing
 * in the type. See docs/PLAN-color.md. */

static unsigned int clamp_component(int v)
{
    return (unsigned int)(v < 0 ? 0 : v > 255 ? 255 : v);
}

WGRI_KEEP
wgr_color_t wgr_color_rgba(int r, int g, int b, int a)
{
    return (wgr_color_t)((clamp_component(r) << 24) | (clamp_component(g) << 16) | (clamp_component(b) << 8) |
                        clamp_component(a));
}

/* 0..1 float -> 0..255, clamped and rounded to the nearest step. */
static unsigned int clamp_component_f(float v)
{
    const float scaled = v * 255.0f + 0.5f;
    return (unsigned int)(scaled < 0.0f ? 0.0f : scaled > 255.0f ? 255.0f : scaled);
}

WGRI_KEEP
wgr_color_t wgr_color_rgbaf(float r, float g, float b, float a)
{
    return (wgr_color_t)((clamp_component_f(r) << 24) | (clamp_component_f(g) << 16) |
                        (clamp_component_f(b) << 8) | clamp_component_f(a));
}

WGRI_KEEP
int wgr_color_get_red(wgr_color_t color)
{
    return (int)((color >> 24) & 0xFFu);
}

WGRI_KEEP
int wgr_color_get_green(wgr_color_t color)
{
    return (int)((color >> 16) & 0xFFu);
}

WGRI_KEEP
int wgr_color_get_blue(wgr_color_t color)
{
    return (int)((color >> 8) & 0xFFu);
}

WGRI_KEEP
int wgr_color_get_alpha(wgr_color_t color)
{
    return (int)(color & 0xFFu);
}

WGRI_KEEP
wgr_color_t wgr_color_with_alpha(wgr_color_t color, int a)
{
    return (wgr_color_t)((color & 0xFFFFFF00u) | clamp_component(a));
}

WGRI_KEEP
wgr_color_t wgr_color_lerp(wgr_color_t from, wgr_color_t to, float t)
{
    const float k = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    wgr_color_t out = 0;
    for (int shift = 24; shift >= 0; shift -= 8) {
        const float a = (float)((from >> shift) & 0xFFu);
        const float b = (float)((to >> shift) & 0xFFu);
        out |= (wgr_color_t)((unsigned int)(a + (b - a) * k + 0.5f) & 0xFFu) << shift;
    }
    return out;
}

wgri_colorf_t wgri_color_unpack(wgr_color_t color)
{
    return (wgri_colorf_t){
        .r = (float)((color >> 24) & 0xFFu) / 255.0f,
        .g = (float)((color >> 16) & 0xFFu) / 255.0f,
        .b = (float)((color >> 8) & 0xFFu) / 255.0f,
        .a = (float)(color & 0xFFu) / 255.0f,
    };
}
