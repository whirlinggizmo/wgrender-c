#include "sk_window.h"

#include "internal/exports.h"
#include "internal/sk_internal.h"

#include "internal/sk_platform.h"

SK_KEEP
void sk_window_set_title(const char *title)
{
    sk_platform_set_title(title);
}

SK_KEEP
int sk_window_close_requested(void)
{
    /* sokol_app drives the loop and tears down on quit, so there is no
     * poll-style "should close" query. Reported via the cleanup callback. */
    return 0;
}

float sk_window_dpi_scale(void)
{
    return sk_platform_dpi_scale();
}

/* Logical pixels: framebuffer pixels / DPI scale, so 2D layout and mouse
 * coordinates don't shrink on high-DPI displays. */
vec2_t sk_window_get_screen_size(void)
{
    const float scale = sk_window_dpi_scale();
    return (vec2_t){(float)sk_platform_width() / scale, (float)sk_platform_height() / scale};
}

vec2_t sk_window_get_position(void)
{
    /* sokol_app does not expose window position; report origin. */
    return (vec2_t){0.0f, 0.0f};
}
