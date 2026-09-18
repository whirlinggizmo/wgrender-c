#include "sk_color.h"

#include "internal/exports.h"
#include "internal/sk_color.h"

/* Colors are plain values (0xRRGGBBAA) — no pool, no handles, no lifecycle. They
 * were handles in librl, which said in rl_color.c that colors are "tiny value
 * objects" without shared-asset semantics; making them values says the same thing
 * in the type. See docs/PLAN-color.md. */

static unsigned int clamp_component(int v)
{
    return (unsigned int)(v < 0 ? 0 : v > 255 ? 255 : v);
}

SK_KEEP
sk_color_t sk_color_rgba(int r, int g, int b, int a)
{
    return (sk_color_t)((clamp_component(r) << 24) | (clamp_component(g) << 16) | (clamp_component(b) << 8) |
                        clamp_component(a));
}

SK_KEEP
sk_color_t sk_color_with_alpha(sk_color_t color, int a)
{
    return (sk_color_t)((color & 0xFFFFFF00u) | clamp_component(a));
}

SK_KEEP
sk_color_t sk_color_lerp(sk_color_t from, sk_color_t to, float t)
{
    const float k = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    sk_color_t out = 0;
    for (int shift = 24; shift >= 0; shift -= 8) {
        const float a = (float)((from >> shift) & 0xFFu);
        const float b = (float)((to >> shift) & 0xFFu);
        out |= (sk_color_t)((unsigned int)(a + (b - a) * k + 0.5f) & 0xFFu) << shift;
    }
    return out;
}

sk_colorf_t sk_color_unpack(sk_color_t color)
{
    return (sk_colorf_t){
        .r = (float)((color >> 24) & 0xFFu) / 255.0f,
        .g = (float)((color >> 16) & 0xFFu) / 255.0f,
        .b = (float)((color >> 8) & 0xFFu) / 255.0f,
        .a = (float)(color & 0xFFu) / 255.0f,
    };
}
