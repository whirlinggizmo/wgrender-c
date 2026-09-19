#include "sk_window.h"

#include "internal/exports.h"
#include "internal/sk_internal.h"

#include "internal/sk_platform.h"
#include "sk_logger.h"

/* Log a platform limitation once per function. */
static bool unsupported(bool *logged, const char *what)
{
    if (!*logged) {
        log_warn("%s isn't supported on this platform", what);
        *logged = true;
    }
    return false;
}

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

SK_KEEP
bool sk_window_set_size(int width, int height)
{
    static bool logged;
    if (width <= 0 || height <= 0) {
        log_warn("sk_window_set_size: invalid size %dx%d", width, height);
        return false;
    }
    return sk_platform_set_window_size(width, height) || unsupported(&logged, "sk_window_set_size");
}

SK_KEEP
bool sk_window_set_position(int x, int y)
{
    static bool logged;
    return sk_platform_set_window_position(x, y) || unsupported(&logged, "sk_window_set_position");
}

vec2_t sk_window_get_position(void)
{
    int x = 0, y = 0;
    sk_platform_get_window_position(&x, &y);
    return (vec2_t){(float)x, (float)y};
}

SK_KEEP
bool sk_window_set_fullscreen(bool fullscreen)
{
    static bool logged;
    return sk_platform_set_fullscreen(fullscreen) || unsupported(&logged, "sk_window_set_fullscreen");
}

SK_KEEP bool sk_window_is_fullscreen(void) { return sk_platform_is_fullscreen(); }
SK_KEEP bool sk_window_set_visible(bool visible) { return sk_platform_set_window_visible(visible); }
SK_KEEP bool sk_window_is_visible(void) { return sk_platform_is_window_visible(); }
SK_KEEP bool sk_window_is_focused(void) { return sk_platform_is_focused(); }
SK_KEEP int sk_window_get_monitor_count(void) { return sk_platform_monitor_count(); }
SK_KEEP int sk_window_get_monitor(void) { return sk_platform_current_monitor(); }

SK_KEEP
bool sk_window_set_monitor(int monitor)
{
    if (monitor < 0 || monitor >= sk_platform_monitor_count()) {
        log_warn("sk_window_set_monitor: no monitor %d (%d available)", monitor, sk_platform_monitor_count());
        return false;
    }
    return sk_platform_set_monitor(monitor);
}

vec2_t sk_window_get_monitor_size(int monitor)
{
    int x, y, width, height;
    return sk_platform_monitor_rect(monitor, &x, &y, &width, &height) ? (vec2_t){(float)width, (float)height}
                                                                       : (vec2_t){0.0f, 0.0f};
}

vec2_t sk_window_get_monitor_position(int monitor)
{
    int x, y, width, height;
    return sk_platform_monitor_rect(monitor, &x, &y, &width, &height) ? (vec2_t){(float)x, (float)y}
                                                                       : (vec2_t){0.0f, 0.0f};
}

SK_KEEP
const char *sk_window_get_monitor_name(int monitor)
{
    return sk_platform_monitor_name(monitor);
}
