#include "wgr_window.h"

#include "internal/exports_internal.h"
#include "internal/wgr_internal_internal.h"

#include "internal/wgr_platform_internal.h"
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

WGRI_KEEP
void wgr_window_set_title(const char *title)
{
    wgri_platform_set_title(title);
}

WGRI_KEEP
int wgr_window_close_requested(void)
{
    /* sokol_app drives the loop and tears down on quit, so there is no
     * poll-style "should close" query. Reported via the cleanup callback. */
    return 0;
}

float wgri_window_dpi_scale(void)
{
    return wgri_platform_dpi_scale();
}

/* Logical pixels: framebuffer pixels / DPI scale, so 2D layout and mouse
 * coordinates don't shrink on high-DPI displays. */
vec2_t wgr_window_get_screen_size(void)
{
    const float scale = wgri_window_dpi_scale();
    return (vec2_t){(float)wgri_platform_width() / scale, (float)wgri_platform_height() / scale};
}

WGRI_KEEP
bool wgr_window_set_size(int width, int height)
{
    static bool logged;
    if (width <= 0 || height <= 0) {
        log_warn("wgr_window_set_size: invalid size %dx%d", width, height);
        return false;
    }
    return wgri_platform_set_window_size(width, height) || unsupported(&logged, "wgr_window_set_size");
}

WGRI_KEEP
bool wgr_window_set_position(int x, int y)
{
    static bool logged;
    return wgri_platform_set_window_position(x, y) || unsupported(&logged, "wgr_window_set_position");
}

vec2_t wgr_window_get_position(void)
{
    int x = 0, y = 0;
    wgri_platform_get_window_position(&x, &y);
    return (vec2_t){(float)x, (float)y};
}

WGRI_KEEP
bool wgr_window_request_fullscreen(bool fullscreen)
{
    static bool logged;
    return wgri_platform_set_fullscreen(fullscreen) || unsupported(&logged, "wgr_window_request_fullscreen");
}

WGRI_KEEP bool wgr_window_has_fullscreen(void) { return wgri_platform_has_fullscreen(); }
WGRI_KEEP bool wgr_window_is_fullscreen(void) { return wgri_platform_is_fullscreen(); }
WGRI_KEEP bool wgr_window_set_visible(bool visible) { return wgri_platform_set_window_visible(visible); }
WGRI_KEEP bool wgr_window_is_visible(void) { return wgri_platform_is_window_visible(); }
WGRI_KEEP bool wgr_window_is_focused(void) { return wgri_platform_is_focused(); }
WGRI_KEEP int wgr_window_get_monitor_count(void) { return wgri_platform_monitor_count(); }
WGRI_KEEP int wgr_window_get_monitor(void) { return wgri_platform_current_monitor(); }

WGRI_KEEP
bool wgr_window_set_monitor(int monitor)
{
    if (monitor < 0 || monitor >= wgri_platform_monitor_count()) {
        log_warn("wgr_window_set_monitor: no monitor %d (%d available)", monitor, wgri_platform_monitor_count());
        return false;
    }
    return wgri_platform_set_monitor(monitor);
}

vec2_t wgr_window_get_monitor_size(int monitor)
{
    int x, y, width, height;
    return wgri_platform_monitor_rect(monitor, &x, &y, &width, &height) ? (vec2_t){(float)width, (float)height}
                                                                       : (vec2_t){0.0f, 0.0f};
}

vec2_t wgr_window_get_monitor_position(int monitor)
{
    int x, y, width, height;
    return wgri_platform_monitor_rect(monitor, &x, &y, &width, &height) ? (vec2_t){(float)x, (float)y}
                                                                       : (vec2_t){0.0f, 0.0f};
}

WGRI_KEEP
const char *wgr_window_get_monitor_name(int monitor)
{
    return wgri_platform_monitor_name(monitor);
}
