#include "internal/sk_platform.h"

#include <stdlib.h>

#include "sk_logger.h"

#if !defined(SK_HEADLESS)
/* -------------------------------------------------------------- sokol_app ---- */

#include "sokol_app.h"
#include "sokol_app_utils.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

static void (*sk_platform_event_fn)(const void *);

static void forward_event(const sapp_event *ev)
{
    if (sk_platform_event_fn != NULL) {
        sk_platform_event_fn(ev);
    }
}

void sk_platform_run(const sk_platform_desc_t *desc)
{
    sk_platform_event_fn = desc->event;
    sapp_run(&(sapp_desc){
        .init_cb = desc->init,
        .frame_cb = desc->frame,
        .event_cb = forward_event,
        .cleanup_cb = desc->cleanup,
        .width = desc->width,
        .height = desc->height,
        .window_title = desc->title,
        .fullscreen = desc->fullscreen,
        .high_dpi = desc->high_dpi,
        .sample_count = desc->sample_count,
        .swap_interval = 1,
        .disable_vsync = desc->disable_vsync,
        .logger.func = slog_func,
    });
}

void sk_platform_request_quit(void) { sapp_request_quit(); }
bool sk_platform_is_headless(void) { return false; }
int sk_platform_width(void) { return sapp_width(); }
int sk_platform_height(void) { return sapp_height(); }
double sk_platform_frame_duration(void) { return sapp_frame_duration_unfiltered(); }
void sk_platform_set_title(const char *title) { sapp_set_window_title(title); }
void sk_platform_lock_mouse(bool locked) { sapp_lock_mouse(locked); }
sg_environment sk_platform_environment(void) { return sglue_environment(); }
sg_swapchain sk_platform_swapchain(void) { return sglue_swapchain(); }

float sk_platform_dpi_scale(void)
{
    const float scale = sapp_dpi_scale();
    return scale > 0.0f ? scale : 1.0f;
}

/* ---------------------------------------------------- window and monitors ---- */

bool sk_platform_set_fullscreen(bool fullscreen)
{
    if (sapp_is_fullscreen() != fullscreen) {
        sapp_toggle_fullscreen(); /* on web, only takes effect during a user gesture */
    }
    return true;
}

bool sk_platform_is_fullscreen(void) { return sapp_is_fullscreen(); }

#if defined(__EMSCRIPTEN__)
/* The canvas is the window: its CSS size is the logical size. sokol_app reads the
 * canvas size on window resize events, so one is sent after changing it. */
EM_JS(int, canvas_set_size, (int width, int height), {
    const canvas = Module.canvas;
    if (!canvas) return 0;
    canvas.style.width = width + "px";
    canvas.style.height = height + "px";
    window.dispatchEvent(new Event("resize"));
    const rect = canvas.getBoundingClientRect();
    return Math.round(rect.width) === width && Math.round(rect.height) === height ? 1 : 0;
});
EM_JS(int, screen_width, (void), { return window.screen.width | 0; });
EM_JS(int, screen_height, (void), { return window.screen.height | 0; });
EM_JS(int, document_has_focus, (void), { return document.hasFocus() ? 1 : 0; });

bool sk_platform_set_window_size(int width, int height) { return canvas_set_size(width, height) != 0; }
bool sk_platform_set_window_position(int x, int y) { (void)x; (void)y; return false; }
bool sk_platform_get_window_position(int *x, int *y) { *x = 0; *y = 0; return false; }
bool sk_platform_is_focused(void) { return document_has_focus() != 0; }
int sk_platform_monitor_count(void) { return 1; }
int sk_platform_current_monitor(void) { return 0; }
bool sk_platform_set_monitor(int monitor) { return monitor == 0; }
const char *sk_platform_monitor_name(int monitor) { (void)monitor; return ""; }

bool sk_platform_monitor_rect(int monitor, int *x, int *y, int *width, int *height)
{
    if (monitor != 0) return false;
    *x = 0;
    *y = 0;
    *width = screen_width();
    *height = screen_height();
    return true;
}

#else
/* Desktop, through deps/sokol_utils. Window and monitor sizes are in the OS's
 * pixels, except on macOS where they're already points (logical). */
static float desktop_scale(void)
{
#if defined(__APPLE__)
    return 1.0f;
#else
    return sk_platform_dpi_scale();
#endif
}

bool sk_platform_set_window_size(int width, int height)
{
    const float scale = desktop_scale();
    sapp_set_window_size((int)((float)width * scale + 0.5f), (int)((float)height * scale + 0.5f));
    return true;
}

bool sk_platform_set_window_position(int x, int y)
{
    sapp_set_window_position(x, y);
    return true;
}

bool sk_platform_get_window_position(int *x, int *y)
{
    sapp_get_window_position(x, y);
    return true;
}

bool sk_platform_is_focused(void) { return sapp_window_focused(); }
int sk_platform_monitor_count(void) { return sapp_num_displays(); }
int sk_platform_current_monitor(void) { return sapp_current_display(); }

bool sk_platform_set_monitor(int monitor)
{
    if (monitor < 0 || monitor >= sapp_num_displays()) return false;
    sapp_set_display(monitor);
    return true;
}

bool sk_platform_monitor_rect(int monitor, int *x, int *y, int *width, int *height)
{
    const float scale = desktop_scale();
    if (monitor < 0 || monitor >= sapp_num_displays()) return false;
    sapp_display_position(monitor, x, y);
    *width = (int)((float)sapp_display_width(monitor) / scale + 0.5f);
    *height = (int)((float)sapp_display_height(monitor) / scale + 0.5f);
    return true;
}

const char *sk_platform_monitor_name(int monitor)
{
    return monitor >= 0 && monitor < sapp_num_displays() ? sapp_display_name(monitor) : "";
}
#endif

#else
/* --------------------------------------------------------------- headless ---- */

#include "sokol_time.h"

static struct {
    sk_platform_desc_t desc;
    bool quit;
    double frame_duration;
} sk_headless;

void sk_platform_run(const sk_platform_desc_t *desc)
{
    const char *frames_env = getenv("SK_HEADLESS_FRAMES");
    const long max_frames = frames_env != NULL ? strtol(frames_env, NULL, 10) : 0;
    uint64_t last;

    sk_headless.desc = *desc;
    sk_headless.quit = false;
    sk_headless.frame_duration = 1.0 / 60.0;

    stm_setup();
    if (desc->init != NULL) {
        desc->init();
    }
    last = stm_now();
    for (long frame = 0; !sk_headless.quit && (max_frames <= 0 || frame < max_frames); frame++) {
        if (desc->frame != NULL) {
            desc->frame();
        }
        sk_headless.frame_duration = stm_sec(stm_laptime(&last));
    }
    if (desc->cleanup != NULL) {
        desc->cleanup();
    }
}

void sk_platform_request_quit(void) { sk_headless.quit = true; }
bool sk_platform_is_headless(void) { return true; }

/* A headless "window" is its framebuffer size on one virtual monitor of the same
 * size; resizing works, the rest has nothing to act on. */
bool sk_platform_set_window_size(int width, int height)
{
    if (width <= 0 || height <= 0) return false;
    sk_headless.desc.width = width;
    sk_headless.desc.height = height;
    return true;
}
bool sk_platform_set_window_position(int x, int y) { (void)x; (void)y; return false; }
bool sk_platform_get_window_position(int *x, int *y) { *x = 0; *y = 0; return false; }
bool sk_platform_set_fullscreen(bool fullscreen) { (void)fullscreen; return false; }
bool sk_platform_is_fullscreen(void) { return false; }
bool sk_platform_is_focused(void) { return true; }
int sk_platform_monitor_count(void) { return 1; }
int sk_platform_current_monitor(void) { return 0; }
bool sk_platform_set_monitor(int monitor) { return monitor == 0; }
const char *sk_platform_monitor_name(int monitor) { (void)monitor; return "headless"; }

bool sk_platform_monitor_rect(int monitor, int *x, int *y, int *width, int *height)
{
    if (monitor != 0) return false;
    *x = 0;
    *y = 0;
    *width = sk_platform_width();
    *height = sk_platform_height();
    return true;
}
float sk_platform_dpi_scale(void) { return 1.0f; }
double sk_platform_frame_duration(void) { return sk_headless.frame_duration; }
void sk_platform_set_title(const char *title) { (void)title; }
void sk_platform_lock_mouse(bool locked) { (void)locked; }

int sk_platform_width(void)
{
    return sk_headless.desc.width > 0 ? sk_headless.desc.width : 1;
}

int sk_platform_height(void)
{
    return sk_headless.desc.height > 0 ? sk_headless.desc.height : 1;
}

sg_environment sk_platform_environment(void)
{
    return (sg_environment){
        .defaults = {
            .color_format = SG_PIXELFORMAT_RGBA8,
            .depth_format = SG_PIXELFORMAT_DEPTH_STENCIL,
            .sample_count = sk_headless.desc.sample_count > 0 ? sk_headless.desc.sample_count : 1,
        },
    };
}

sg_swapchain sk_platform_swapchain(void)
{
    return (sg_swapchain){
        .width = sk_platform_width(),
        .height = sk_platform_height(),
        .sample_count = sk_headless.desc.sample_count > 0 ? sk_headless.desc.sample_count : 1,
        .color_format = SG_PIXELFORMAT_RGBA8,
        .depth_format = SG_PIXELFORMAT_DEPTH_STENCIL,
    };
}

#endif
