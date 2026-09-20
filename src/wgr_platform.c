#include "internal/wgr_platform_internal.h"

#include <stdlib.h>

#include "wgr_logger.h"

#if !defined(WGR_HEADLESS)
/* -------------------------------------------------------------- sokol_app ---- */

#include "sokol_app.h"
#include "sokol_app_utils.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

static void (*wgr_platform_event_fn)(const void *);

static void forward_event(const sapp_event *ev)
{
    if (wgr_platform_event_fn != NULL) {
        wgr_platform_event_fn(ev);
    }
}

static bool wgr_platform_transparent;

void wgr_platform_run(const wgr_platform_desc_t *desc)
{
    wgr_platform_event_fn = desc->event;
    wgr_platform_transparent = desc->transparent;
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
        .composite_mode = desc->transparent ? SAPP_COMPOSITEMODE_PREMULTIPLIED : SAPP_COMPOSITEMODE_OPAQUE,
        .logger.func = slog_func,
    });
}

void wgr_platform_request_quit(void) { sapp_request_quit(); }
bool wgr_platform_is_headless(void) { return false; }
int wgr_platform_width(void) { return sapp_width(); }
int wgr_platform_height(void) { return sapp_height(); }
double wgr_platform_frame_duration(void) { return sapp_frame_duration_unfiltered(); }
void wgr_platform_set_title(const char *title) { sapp_set_window_title(title); }
void wgr_platform_lock_mouse(bool locked) { sapp_lock_mouse(locked); }
sg_environment wgr_platform_environment(void) { return sglue_environment(); }
sg_swapchain wgr_platform_swapchain(void) { return sglue_swapchain(); }

float wgr_platform_dpi_scale(void)
{
    const float scale = sapp_dpi_scale();
    return scale > 0.0f ? scale : 1.0f;
}

/* ---------------------------------------------------- window and monitors ---- */

/* The style the program asked for: applied while windowed, and again on leaving
 * fullscreen (Win32's toggle resets it; a fixed size could keep X11 window managers
 * from making the window fullscreen). */
static bool wgr_style_resizable = true;
static bool wgr_style_decorated = true;

static void apply_style(void)
{
    sapp_set_window_resizable(wgr_style_resizable);
    sapp_set_window_decorated(wgr_style_decorated);
}

void wgr_platform_set_window_style(bool resizable, bool decorated)
{
    wgr_style_resizable = resizable;
    wgr_style_decorated = decorated;
    if (!sapp_is_fullscreen()) apply_style();
}

bool wgr_platform_set_window_visible(bool visible)
{
    sapp_set_window_visible(visible);
    return true;
}

bool wgr_platform_is_window_visible(void) { return sapp_window_visible(); }
bool wgr_platform_is_window_transparent(void) { return wgr_platform_transparent; }

bool wgr_platform_set_fullscreen(bool fullscreen)
{
    if (sapp_is_fullscreen() != fullscreen) {
        if (fullscreen) sapp_set_window_resizable(true);
        sapp_toggle_fullscreen(); /* on web, only takes effect during a user gesture */
        if (!fullscreen) apply_style();
    }
    return true;
}

bool wgr_platform_is_fullscreen(void) { return sapp_is_fullscreen(); }

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

EM_JS(void, performance_mark, (const char *name), { performance.mark(UTF8ToString(name)); });

void wgr_platform_mark(const char *name) { performance_mark(name); }
bool wgr_platform_set_window_size(int width, int height) { return canvas_set_size(width, height) != 0; }
bool wgr_platform_set_window_position(int x, int y) { (void)x; (void)y; return false; }
bool wgr_platform_get_window_position(int *x, int *y) { *x = 0; *y = 0; return false; }
bool wgr_platform_is_focused(void) { return document_has_focus() != 0; }
int wgr_platform_monitor_count(void) { return 1; }
int wgr_platform_current_monitor(void) { return 0; }
bool wgr_platform_set_monitor(int monitor) { return monitor == 0; }
const char *wgr_platform_monitor_name(int monitor) { (void)monitor; return ""; }

bool wgr_platform_monitor_rect(int monitor, int *x, int *y, int *width, int *height)
{
    if (monitor != 0) return false;
    *x = 0;
    *y = 0;
    *width = screen_width();
    *height = screen_height();
    return true;
}

#else
void wgr_platform_mark(const char *name) { (void)name; }

/* Desktop, through deps/sokol_utils. Window and monitor sizes are in the OS's
 * pixels, except on macOS where they're already points (logical). */
static float desktop_scale(void)
{
#if defined(__APPLE__)
    return 1.0f;
#else
    return wgr_platform_dpi_scale();
#endif
}

bool wgr_platform_set_window_size(int width, int height)
{
    const float scale = desktop_scale();
    sapp_set_window_size((int)((float)width * scale + 0.5f), (int)((float)height * scale + 0.5f));
    return true;
}

/* Under XWayland the Wayland compositor places windows: moves are ignored, and the
 * position X11 reports isn't where the window is. Say so, once. */
static bool can_move(void)
{
    static bool logged;
    if (sapp_can_move_window()) return true;
    if (!logged) {
        log_info("window: a Wayland desktop (XWayland) places windows: moving them and changing monitor "
                 "aren't possible here");
        logged = true;
    }
    return false;
}

bool wgr_platform_set_window_position(int x, int y)
{
    if (!can_move()) return false;
    sapp_set_window_position(x, y);
    return true;
}

bool wgr_platform_get_window_position(int *x, int *y)
{
    if (!can_move()) {
        *x = 0, *y = 0;
        return false;
    }
    sapp_get_window_position(x, y);
    return true;
}

bool wgr_platform_is_focused(void) { return sapp_window_focused(); }
int wgr_platform_monitor_count(void) { return sapp_num_displays(); }
int wgr_platform_current_monitor(void) { return sapp_current_display(); }

bool wgr_platform_set_monitor(int monitor)
{
    if (monitor < 0 || monitor >= sapp_num_displays() || !can_move()) return false;
    sapp_set_display(monitor);
    return true;
}

bool wgr_platform_monitor_rect(int monitor, int *x, int *y, int *width, int *height)
{
    const float scale = desktop_scale();
    if (monitor < 0 || monitor >= sapp_num_displays()) return false;
    sapp_display_position(monitor, x, y);
    *width = (int)((float)sapp_display_width(monitor) / scale + 0.5f);
    *height = (int)((float)sapp_display_height(monitor) / scale + 0.5f);
    return true;
}

const char *wgr_platform_monitor_name(int monitor)
{
    return monitor >= 0 && monitor < sapp_num_displays() ? sapp_display_name(monitor) : "";
}
#endif

#else
/* --------------------------------------------------------------- headless ---- */

#include "sokol_time.h"

static struct {
    wgr_platform_desc_t desc;
    bool quit;
    bool hidden;
    double frame_duration;
} wgr_headless;

void wgr_platform_run(const wgr_platform_desc_t *desc)
{
    const char *frames_env = getenv("WGR_HEADLESS_FRAMES");
    const long max_frames = frames_env != NULL ? strtol(frames_env, NULL, 10) : 0;
    uint64_t last;

    wgr_headless.desc = *desc;
    wgr_headless.quit = false;
    wgr_headless.hidden = false;
    wgr_headless.frame_duration = 1.0 / 60.0;

    stm_setup();
    if (desc->init != NULL) {
        desc->init();
    }
    last = stm_now();
    for (long frame = 0; !wgr_headless.quit && (max_frames <= 0 || frame < max_frames); frame++) {
        if (desc->frame != NULL) {
            desc->frame();
        }
        wgr_headless.frame_duration = stm_sec(stm_laptime(&last));
    }
    if (desc->cleanup != NULL) {
        desc->cleanup();
    }
}

void wgr_platform_request_quit(void) { wgr_headless.quit = true; }
bool wgr_platform_is_headless(void) { return true; }

/* A headless "window" is its framebuffer size on one virtual monitor of the same
 * size; resizing works, the rest has nothing to act on. */
bool wgr_platform_set_window_size(int width, int height)
{
    if (width <= 0 || height <= 0) return false;
    wgr_headless.desc.width = width;
    wgr_headless.desc.height = height;
    return true;
}
bool wgr_platform_set_window_position(int x, int y) { (void)x; (void)y; return false; }
bool wgr_platform_get_window_position(int *x, int *y) { *x = 0; *y = 0; return false; }
bool wgr_platform_set_fullscreen(bool fullscreen) { (void)fullscreen; return false; }
bool wgr_platform_is_fullscreen(void) { return false; }
void wgr_platform_set_window_style(bool resizable, bool decorated) { (void)resizable, (void)decorated; }
bool wgr_platform_set_window_visible(bool visible)
{
    wgr_headless.hidden = !visible;
    return true;
}
bool wgr_platform_is_window_visible(void) { return !wgr_headless.hidden; }
bool wgr_platform_is_window_transparent(void) { return wgr_headless.desc.transparent; }
bool wgr_platform_is_focused(void) { return true; }
int wgr_platform_monitor_count(void) { return 1; }
int wgr_platform_current_monitor(void) { return 0; }
bool wgr_platform_set_monitor(int monitor) { return monitor == 0; }
const char *wgr_platform_monitor_name(int monitor) { (void)monitor; return "headless"; }

bool wgr_platform_monitor_rect(int monitor, int *x, int *y, int *width, int *height)
{
    if (monitor != 0) return false;
    *x = 0;
    *y = 0;
    *width = wgr_platform_width();
    *height = wgr_platform_height();
    return true;
}
/* No display, so no DPI of its own: tests pick the scale it reports. */
static float wgr_headless_dpi_scale = 1.0f;
float wgr_platform_dpi_scale(void) { return wgr_headless_dpi_scale; }
void wgr_platform_set_headless_dpi_scale(float scale) { wgr_headless_dpi_scale = scale > 0.0f ? scale : 1.0f; }
double wgr_platform_frame_duration(void) { return wgr_headless.frame_duration; }
void wgr_platform_set_title(const char *title) { (void)title; }
void wgr_platform_mark(const char *name) { (void)name; }
void wgr_platform_lock_mouse(bool locked) { (void)locked; }

int wgr_platform_width(void)
{
    return wgr_headless.desc.width > 0 ? wgr_headless.desc.width : 1;
}

int wgr_platform_height(void)
{
    return wgr_headless.desc.height > 0 ? wgr_headless.desc.height : 1;
}

sg_environment wgr_platform_environment(void)
{
    return (sg_environment){
        .defaults = {
            .color_format = SG_PIXELFORMAT_RGBA8,
            .depth_format = SG_PIXELFORMAT_DEPTH_STENCIL,
            .sample_count = wgr_headless.desc.sample_count > 0 ? wgr_headless.desc.sample_count : 1,
        },
    };
}

sg_swapchain wgr_platform_swapchain(void)
{
    return (sg_swapchain){
        .width = wgr_platform_width(),
        .height = wgr_platform_height(),
        .sample_count = wgr_headless.desc.sample_count > 0 ? wgr_headless.desc.sample_count : 1,
        .color_format = SG_PIXELFORMAT_RGBA8,
        .depth_format = SG_PIXELFORMAT_DEPTH_STENCIL,
    };
}

#endif
