#ifndef WGRI_INTERNAL_PLATFORM_H
#define WGRI_INTERNAL_PLATFORM_H

#include <stdbool.h>

#include "sokol_gfx.h"

/* The app/window layer libwgrender runs on. Normal builds use sokol_app (a window, input
 * events, the display's frame loop). Headless builds (WGR_HEADLESS, with sokol's
 * dummy GPU backend) have no window, GPU or audio device: the loop runs frames
 * paced in real time until wgr_request_quit() or WGR_HEADLESS_FRAMES frames. See
 * docs/ROADMAP.md (null / headless renderer). */

typedef struct {
    int width, height;          /* logical window size */
    const char *title;
    bool fullscreen;
    bool high_dpi;
    int sample_count;           /* 1 or 4 */
    bool disable_vsync;
    bool transparent;           /* the framebuffer's alpha shows what's behind the window */
    void (*init)(void);
    void (*frame)(void);
    void (*cleanup)(void);
    void (*event)(const void *sapp_event); /* sokol_app builds only */
} wgri_platform_desc_t;

/* Run the app loop. Desktop: blocks until quit. Web: returns immediately. */
void wgri_platform_run(const wgri_platform_desc_t *desc);
void wgri_platform_request_quit(void);

bool wgri_platform_is_headless(void);
int wgri_platform_width(void);         /* framebuffer pixels */
int wgri_platform_height(void);
float wgri_platform_dpi_scale(void);   /* framebuffer pixels per logical pixel, > 0 */
#if defined(WGR_HEADLESS)
void wgri_platform_set_headless_dpi_scale(float scale); /* for tests: what wgri_platform_dpi_scale reports */
#endif
double wgri_platform_frame_duration(void); /* last frame's raw duration, seconds */

void wgri_platform_set_title(const char *title);

/* A named point in startup, for measuring it (web: performance.mark, which DevTools and
 * tools/webstart.mjs read; elsewhere nothing). */
void wgri_platform_mark(const char *name);

/* Window and monitors (docs/PLAN-window.md). Sizes in logical pixels, positions in
 * the desktop's coordinates. False where the platform can't do it. */
bool wgri_platform_set_window_size(int width, int height);
bool wgri_platform_set_window_position(int x, int y);
bool wgri_platform_get_window_position(int *x, int *y);
bool wgri_platform_set_fullscreen(bool fullscreen);
bool wgri_platform_is_fullscreen(void);
/* The window's style (desktop): resizable by the user, decorated (title bar and
 * border). Kept through fullscreen, which sets its own. Elsewhere nothing. */
void wgri_platform_set_window_style(bool resizable, bool decorated);
bool wgri_platform_set_window_visible(bool visible);
bool wgri_platform_is_window_visible(void);
/* WGR_WINDOW_FLAG_WINDOW_TRANSPARENT: the screen composites premultiplied */
bool wgri_platform_is_window_transparent(void);
bool wgri_platform_is_focused(void);
int wgri_platform_monitor_count(void);
int wgri_platform_current_monitor(void);
bool wgri_platform_set_monitor(int monitor);
/* false for an invalid index */
bool wgri_platform_monitor_rect(int monitor, int *x, int *y, int *width, int *height);
const char *wgri_platform_monitor_name(int monitor);
void wgri_platform_lock_mouse(bool locked);

/* What sokol_gfx renders into. */
sg_environment wgri_platform_environment(void);
sg_swapchain wgri_platform_swapchain(void);

#endif // WGRI_INTERNAL_PLATFORM_H
