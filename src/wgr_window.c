#include "wgr_window.h"

#include "internal/exports.h"
#include "internal/wgr_internal.h"

#include "internal/wgr_platform.h"
#include "wgr_logger.h"

/* Log a platform limitation once per function. */
static bool unsupported(bool *logged, const char *what)
{
    if (!*logged) {
        log_warn("%s isn't supported on this platform", what);
        *logged = true;
    }
    return false;
}

WGR_KEEP
void wgr_window_set_title(const char *title)
{
    wgr_platform_set_title(title);
}

WGR_KEEP
int wgr_window_close_requested(void)
{
    /* sokol_app drives the loop and tears down on quit, so there is no
     * poll-style "should close" query. Reported via the cleanup callback. */
    return 0;
}

float wgr_window_dpi_scale(void)
{
    return wgr_platform_dpi_scale();
}

/* Logical pixels: framebuffer pixels / DPI scale, so 2D layout and mouse
 * coordinates don't shrink on high-DPI displays. */
vec2_t wgr_window_get_screen_size(void)
{
    const float scale = wgr_window_dpi_scale();
    return (vec2_t){(float)wgr_platform_width() / scale, (float)wgr_platform_height() / scale};
}

WGR_KEEP
bool wgr_window_set_size(int width, int height)
{
    static bool logged;
    if (width <= 0 || height <= 0) {
        log_warn("wgr_window_set_size: invalid size %dx%d", width, height);
        return false;
    }
    return wgr_platform_set_window_size(width, height) || unsupported(&logged, "wgr_window_set_size");
}

WGR_KEEP
bool wgr_window_set_position(int x, int y)
{
    static bool logged;
    return wgr_platform_set_window_position(x, y) || unsupported(&logged, "wgr_window_set_position");
}

vec2_t wgr_window_get_position(void)
{
    int x = 0, y = 0;
    wgr_platform_get_window_position(&x, &y);
    return (vec2_t){(float)x, (float)y};
}

WGR_KEEP
bool wgr_window_set_fullscreen(bool fullscreen)
{
    static bool logged;
    return wgr_platform_set_fullscreen(fullscreen) || unsupported(&logged, "wgr_window_set_fullscreen");
}

WGR_KEEP bool wgr_window_is_fullscreen(void) { return wgr_platform_is_fullscreen(); }
WGR_KEEP bool wgr_window_set_visible(bool visible) { return wgr_platform_set_window_visible(visible); }
WGR_KEEP bool wgr_window_is_visible(void) { return wgr_platform_is_window_visible(); }
WGR_KEEP bool wgr_window_is_focused(void) { return wgr_platform_is_focused(); }
WGR_KEEP int wgr_window_get_monitor_count(void) { return wgr_platform_monitor_count(); }
WGR_KEEP int wgr_window_get_monitor(void) { return wgr_platform_current_monitor(); }

WGR_KEEP
bool wgr_window_set_monitor(int monitor)
{
    if (monitor < 0 || monitor >= wgr_platform_monitor_count()) {
        log_warn("wgr_window_set_monitor: no monitor %d (%d available)", monitor, wgr_platform_monitor_count());
        return false;
    }
    return wgr_platform_set_monitor(monitor);
}

vec2_t wgr_window_get_monitor_size(int monitor)
{
    int x, y, width, height;
    return wgr_platform_monitor_rect(monitor, &x, &y, &width, &height) ? (vec2_t){(float)width, (float)height}
                                                                       : (vec2_t){0.0f, 0.0f};
}

vec2_t wgr_window_get_monitor_position(int monitor)
{
    int x, y, width, height;
    return wgr_platform_monitor_rect(monitor, &x, &y, &width, &height) ? (vec2_t){(float)x, (float)y}
                                                                       : (vec2_t){0.0f, 0.0f};
}

WGR_KEEP
const char *wgr_window_get_monitor_name(int monitor)
{
    return wgr_platform_monitor_name(monitor);
}
