#ifndef SK_INTERNAL_PLATFORM_H
#define SK_INTERNAL_PLATFORM_H

#include <stdbool.h>

#include "sokol_gfx.h"

/* The app/window layer libsk runs on. Normal builds use sokol_app (a window, input
 * events, the display's frame loop). Headless builds (SK_HEADLESS, with sokol's
 * dummy GPU backend) have no window, GPU or audio device: the loop runs frames
 * paced in real time until sk_request_quit() or SK_HEADLESS_FRAMES frames. See
 * docs/ROADMAP.md (null / headless renderer). */

typedef struct {
    int width, height;          /* logical window size */
    const char *title;
    bool fullscreen;
    bool high_dpi;
    int sample_count;           /* 1 or 4 */
    bool disable_vsync;
    void (*init)(void);
    void (*frame)(void);
    void (*cleanup)(void);
    void (*event)(const void *sapp_event); /* sokol_app builds only */
} sk_platform_desc_t;

/* Run the app loop. Desktop: blocks until quit. Web: returns immediately. */
void sk_platform_run(const sk_platform_desc_t *desc);
void sk_platform_request_quit(void);

bool sk_platform_is_headless(void);
int sk_platform_width(void);         /* framebuffer pixels */
int sk_platform_height(void);
float sk_platform_dpi_scale(void);   /* framebuffer pixels per logical pixel, > 0 */
double sk_platform_frame_duration(void); /* last frame's raw duration, seconds */

void sk_platform_set_title(const char *title);

/* Window and monitors (docs/PLAN-window.md). Sizes in logical pixels, positions in
 * the desktop's coordinates. False where the platform can't do it. */
bool sk_platform_set_window_size(int width, int height);
bool sk_platform_set_window_position(int x, int y);
bool sk_platform_get_window_position(int *x, int *y);
bool sk_platform_set_fullscreen(bool fullscreen);
bool sk_platform_is_fullscreen(void);
bool sk_platform_is_focused(void);
int sk_platform_monitor_count(void);
int sk_platform_current_monitor(void);
bool sk_platform_set_monitor(int monitor);
/* false for an invalid index */
bool sk_platform_monitor_rect(int monitor, int *x, int *y, int *width, int *height);
const char *sk_platform_monitor_name(int monitor);
void sk_platform_lock_mouse(bool locked);

/* What sokol_gfx renders into. */
sg_environment sk_platform_environment(void);
sg_swapchain sk_platform_swapchain(void);

#endif // SK_INTERNAL_PLATFORM_H
