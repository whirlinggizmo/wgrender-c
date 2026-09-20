#ifndef WGR_WINDOW_H
#define WGR_WINDOW_H

#ifdef __cplusplus
extern "C" {
#endif

#include "wgr_types.h"

/* Window flags (wgr_init_values). The window style ones apply on the desktop; on the
 * web the page lays out the canvas.
 *   RESIZABLE    the user can resize the window; without it the window keeps its
 *                size (wgr_window_set_size still changes it)
 *   UNDECORATED  no title bar or border
 *   HIDDEN       the window starts hidden: wgr_window_set_visible(true) shows it (after
 *                loading, say). sokol_app makes the window visible, so it can show for
 *                a moment first on some desktops. Web: the canvas is hidden.
 *   TRANSPARENT  the framebuffer's alpha shows what's behind the window: clear with
 *                alpha 0. Web (the page: its canvas mustn't paint a background) and
 *                macOS; not X11 with OpenGL. */
#define WGR_WINDOW_FLAG_FULLSCREEN_MODE 0x00000002u
#define WGR_WINDOW_FLAG_WINDOW_RESIZABLE 0x00000004u
#define WGR_WINDOW_FLAG_WINDOW_UNDECORATED 0x00000008u
#define WGR_WINDOW_FLAG_WINDOW_TRANSPARENT 0x00000010u
#define WGR_WINDOW_FLAG_MSAA_4X_HINT 0x00000020u
#define WGR_WINDOW_FLAG_VSYNC_OFF 0x00000040u /* unlock from vsync (desktop; the web is always vsynced) */
#define WGR_WINDOW_FLAG_WINDOW_HIDDEN 0x00000080u
/* Windows render at the display's full resolution (high-DPI): sizes and coordinates
 * stay in logical pixels, text rasterizes at the real pixel scale. LOW_DPI renders at
 * one framebuffer pixel per logical pixel instead, scaled up by the display: fewer
 * pixels to fill (a 2x screen has 4x as many), softer edges and text. */
#define WGR_WINDOW_FLAG_LOW_DPI 0x00002000u

/*
 * Window lifecycle is owned by the core runtime:
 * - wgr_init_values(...) configures the window
 * - wgr_run() opens it (sokol_app owns the message loop)
 * - wgr_deinit()/cleanup closes it
 */

void wgr_window_set_title(const char *title);
int wgr_window_close_requested(void);
vec2_t wgr_window_get_screen_size(void);

/* Window size, position and monitors (docs/PLAN-window.md).
 *
 * Sizes are logical pixels, like wgr_window_get_screen_size; positions are the
 * desktop's coordinates (top-left origin). Functions return false where the platform
 * can't do what's asked, logging why once:
 *   - web: the canvas is the window. Setting its size works (unless the page's CSS
 *     overrides it); there's no position and one monitor, the screen.
 *   - Linux: under XWayland, compositors usually ignore a program moving its own
 *     window (and may ignore resizing); the call still succeeds.
 *   - fullscreen on web only takes effect during a user gesture (a key or click). */
bool   wgr_window_set_size(int width, int height);
/* Moving the window (and wgr_window_set_monitor) needs a desktop that lets programs
 * place their windows: not the web, and not a Wayland desktop (libwgrender runs there
 * through XWayland, and the compositor places windows). There they return false. */
bool   wgr_window_set_position(int x, int y);
vec2_t wgr_window_get_position(void); /* (0, 0) where there's no position */
bool   wgr_window_set_fullscreen(bool fullscreen);
bool   wgr_window_is_fullscreen(void);
/* Show or hide the window (WGR_WINDOW_FLAG_WINDOW_HIDDEN starts it hidden); it keeps
 * running either way. Web: the canvas. */
bool   wgr_window_set_visible(bool visible);
bool   wgr_window_is_visible(void);
bool   wgr_window_is_focused(void);

/* Monitors, 0 .. count - 1. Size and position are (0, 0) for an invalid index. */
int         wgr_window_get_monitor_count(void);
int         wgr_window_get_monitor(void); /* the one the window is on */
bool        wgr_window_set_monitor(int monitor); /* move the window there (centered) */
vec2_t      wgr_window_get_monitor_size(int monitor);
vec2_t      wgr_window_get_monitor_position(int monitor);
const char *wgr_window_get_monitor_name(int monitor); /* "" if unknown */

#ifdef __cplusplus
}
#endif

#endif // WGR_WINDOW_H
