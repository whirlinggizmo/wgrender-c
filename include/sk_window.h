#ifndef SK_WINDOW_H
#define SK_WINDOW_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sk_types.h"

/* Window flags. Only a subset is honored by the sokol_app backend; the rest are
 * accepted for source compatibility and ignored where the backend has no
 * equivalent. */
#define SK_WINDOW_FLAG_FULLSCREEN_MODE 0x00000002u
#define SK_WINDOW_FLAG_WINDOW_RESIZABLE 0x00000004u
#define SK_WINDOW_FLAG_WINDOW_UNDECORATED 0x00000008u
#define SK_WINDOW_FLAG_WINDOW_TRANSPARENT 0x00000010u
#define SK_WINDOW_FLAG_MSAA_4X_HINT 0x00000020u
#define SK_WINDOW_FLAG_VSYNC_OFF 0x00000040u /* unlock from vsync (desktop; the web is always vsynced) */
#define SK_WINDOW_FLAG_WINDOW_HIDDEN 0x00000080u
#define SK_WINDOW_FLAG_WINDOW_ALWAYS_RUN 0x00000100u
/* Windows render at the display's full resolution (high-DPI): sizes and coordinates
 * stay in logical pixels, text rasterizes at the real pixel scale. LOW_DPI renders at
 * one framebuffer pixel per logical pixel instead, scaled up by the display: fewer
 * pixels to fill (a 2x screen has 4x as many), softer edges and text. */
#define SK_WINDOW_FLAG_LOW_DPI 0x00002000u

/*
 * Window lifecycle is owned by the core runtime:
 * - sk_init_values(...) configures the window
 * - sk_run() opens it (sokol_app owns the message loop)
 * - sk_deinit()/cleanup closes it
 */

void sk_window_set_title(const char *title);
int sk_window_close_requested(void);
vec2_t sk_window_get_screen_size(void);

/* Window size, position and monitors (docs/PLAN-window.md).
 *
 * Sizes are logical pixels, like sk_window_get_screen_size; positions are the
 * desktop's coordinates (top-left origin). Functions return false where the platform
 * can't do what's asked, logging why once:
 *   - web: the canvas is the window. Setting its size works (unless the page's CSS
 *     overrides it); there's no position and one monitor, the screen.
 *   - Linux: under XWayland, compositors usually ignore a program moving its own
 *     window (and may ignore resizing); the call still succeeds.
 *   - fullscreen on web only takes effect during a user gesture (a key or click). */
bool   sk_window_set_size(int width, int height);
bool   sk_window_set_position(int x, int y);
vec2_t sk_window_get_position(void); /* (0, 0) where there's no position */
bool   sk_window_set_fullscreen(bool fullscreen);
bool   sk_window_is_fullscreen(void);
bool   sk_window_is_focused(void);

/* Monitors, 0 .. count - 1. Size and position are (0, 0) for an invalid index. */
int         sk_window_get_monitor_count(void);
int         sk_window_get_monitor(void); /* the one the window is on */
bool        sk_window_set_monitor(int monitor); /* move the window there (centered) */
vec2_t      sk_window_get_monitor_size(int monitor);
vec2_t      sk_window_get_monitor_position(int monitor);
const char *sk_window_get_monitor_name(int monitor); /* "" if unknown */

#ifdef __cplusplus
}
#endif

#endif // SK_WINDOW_H
