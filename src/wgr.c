#include "wgr.h"

#include <stddef.h>
#include <string.h>
#if defined(_WIN32)
#  include <windows.h>
#elif !defined(__EMSCRIPTEN__)
#  include <time.h>
#endif

#include "internal/exports.h"
#include "internal/wgr_frame_pace.h"
#include "internal/wgr_internal.h"
#include "internal/wgr_module.h"
#include "internal/wgr_platform.h"
#include "internal/wgr_render.h"
#include "internal/wgr_scene.h"
#include "internal/wgr_tick_clock.h"
#include "wgr_logger.h"
#include "wgr_version.h"

#include "sokol_gfx.h"
#include "sokol_log.h"
#include "sokol_time.h"

bool wgr_initialized = false;

typedef struct {
    int window_width;
    int window_height;
    char window_title[256];
    unsigned int window_flags;

    wgr_frame_fn frame_fn;
    void *frame_user_data;
    wgr_tick_fn tick_fn;
    void *tick_user_data;
    wgr_tick_clock_t tick_clock;
    unsigned tick_generation; /* bumped by wgr_set_tick, to notice changes made inside a tick */
    wgr_lifecycle_fn init_fn;
    void *init_user_data;
    wgr_lifecycle_fn cleanup_fn;
    void *cleanup_user_data;

    int target_fps;
    wgr_frame_pace_t pace;

    uint64_t start_ticks;
    double delta_time;      /* this frame's delta (seconds), passed to the frame callback */
    double last_frame_time; /* wgr_get_time() when the previous frame ran; 0 before the first */
    double fps_delta;       /* smoothed delta for the FPS counter */
    bool first_frame_done;
} wgr_runtime_t;

static wgr_runtime_t wgr_rt;
static bool wgr_configured = false;

static void apply_defaults(wgr_runtime_t *rt)
{
    if (rt->window_width <= 0) {
        rt->window_width = 1024;
    }
    if (rt->window_height <= 0) {
        rt->window_height = 768;
    }
    if (rt->window_title[0] == '\0') {
        strncpy(rt->window_title, "libwgrender", sizeof(rt->window_title) - 1);
    }
    if (rt->target_fps == 0) {
        rt->target_fps = -1; /* -1: unpaced (vsync, or uncapped with WGR_WINDOW_FLAG_VSYNC_OFF) */
    }
    wgr_frame_pace_set_fps(&rt->pace, rt->target_fps);
}

WGR_KEEP
int wgr_init_values(int window_width,
                   int window_height,
                   const char *window_title,
                   unsigned int window_flags)
{
    if (wgr_initialized) {
        return WGR_INIT_ERR_ALREADY_INITIALIZED;
    }

    memset(&wgr_rt, 0, sizeof(wgr_rt));
    wgr_rt.window_width = window_width;
    wgr_rt.window_height = window_height;
    wgr_rt.window_flags = window_flags;
    if (window_title != NULL && window_title[0] != '\0') {
        strncpy(wgr_rt.window_title, window_title, sizeof(wgr_rt.window_title) - 1);
    }
    apply_defaults(&wgr_rt);

    wgr_logger_init();
    wgr_logger_info("libwgrender %s", wgr_version_string());

    wgr_configured = true;
    return WGR_INIT_OK;
}

WGR_KEEP
void wgr_set_frame(wgr_frame_fn frame_fn, void *user_data)
{
    wgr_rt.frame_fn = frame_fn;
    wgr_rt.frame_user_data = user_data;
}

WGR_KEEP
void wgr_set_tick(wgr_tick_fn tick_fn, void *user_data, int hz)
{
    wgr_rt.tick_fn = hz > 0 ? tick_fn : NULL;
    wgr_rt.tick_user_data = user_data;
    wgr_tick_clock_set_rate(&wgr_rt.tick_clock, tick_fn != NULL ? hz : 0);
    wgr_rt.tick_generation++;
}

WGR_KEEP
void wgr_set_init(wgr_lifecycle_fn init_fn, void *user_data)
{
    wgr_rt.init_fn = init_fn;
    wgr_rt.init_user_data = user_data;
}

WGR_KEEP
void wgr_set_cleanup(wgr_lifecycle_fn cleanup_fn, void *user_data)
{
    wgr_rt.cleanup_fn = cleanup_fn;
    wgr_rt.cleanup_user_data = user_data;
}

static const char *backend_name(sg_backend b)
{
    switch (b) {
        case SG_BACKEND_GLCORE:           return "GL core";
        case SG_BACKEND_GLES3:            return "GLES3/WebGL2";
        case SG_BACKEND_D3D11:            return "D3D11";
        case SG_BACKEND_METAL_IOS:        return "Metal (iOS)";
        case SG_BACKEND_METAL_MACOS:      return "Metal (macOS)";
        case SG_BACKEND_METAL_SIMULATOR:  return "Metal (sim)";
        case SG_BACKEND_WGPU:             return "WebGPU";
        case SG_BACKEND_DUMMY:            return "dummy";
        default:                          return "unknown";
    }
}

static void on_init(void)
{
    wgr_platform_mark("wgr:init"); /* startup points: tools/webstart.mjs */
    /* the window's style, as soon as it exists (sokol_app made it, visible, before
       this: a hidden window can show for a moment first) */
    wgr_platform_set_window_style((wgr_rt.window_flags & WGR_WINDOW_FLAG_WINDOW_RESIZABLE) != 0,
                                 (wgr_rt.window_flags & WGR_WINDOW_FLAG_WINDOW_UNDECORATED) == 0);
    if ((wgr_rt.window_flags & WGR_WINDOW_FLAG_WINDOW_HIDDEN) != 0) {
        wgr_platform_set_window_visible(false);
    }
    sg_setup(&(sg_desc){
        .environment = wgr_platform_environment(),
        .logger.func = slog_func,
        .buffer_pool_size = WGR_GFX_BUFFER_POOL_SIZE,
        .image_pool_size = WGR_GFX_IMAGE_POOL_SIZE,
        .view_pool_size = WGR_GFX_VIEW_POOL_SIZE,
    });
    wgr_logger_info("libwgrender: %s backend", backend_name(sg_query_backend()));
    stm_setup();
    wgr_rt.start_ticks = stm_now();

    /* GPU-backed subsystems (need a live sg context) */
    wgr_render_init();
    wgr_scene_init();  /* registry must exist before drawables register */
    wgr_shape2d_init();
    wgr_shape3d_init();
    wgr_font_init();
    wgr_text_init();

    /* CPU-side stores */
    wgr_camera3d_init();
    wgr_fs_init(NULL);   /* local storage; asset acquisition sits on top */
    wgr_asset_init();
    wgr_event_init();
    wgr_input_init();
    wgr_debug_init();

    /* the optional subsystems the program uses (textures, models, sprites, particles,
       audio, ...): those linked, in their order (internal/wgr_module.h) */
    wgr_module_init_all();

    wgr_initialized = true;
    wgr_platform_mark("wgr:subsystems");

    if (wgr_rt.init_fn != NULL) {
        wgr_rt.init_fn(wgr_rt.init_user_data);
    }
    wgr_platform_mark("wgr:user-init");
}

#if !defined(__EMSCRIPTEN__)
/* Wait until `deadline` (seconds on the wgr_get_time clock): sleep, then yield for
 * the last moment because OS sleeps overshoot by up to a millisecond or two.
 * (Audio mixes on its own thread, so nothing needs feeding while waiting.) */
static void wait_until(double deadline)
{
    const double spin = 0.002;
    for (;;) {
        double remaining = deadline - wgr_get_time();
        if (remaining <= 0.0) {
            return;
        }
        if (remaining > spin) {
            const double sleep_for = remaining - spin;
#  if defined(_WIN32)
            Sleep((DWORD)(sleep_for * 1000.0));
#  else
            struct timespec ts = {(time_t)sleep_for, (long)((sleep_for - (double)(time_t)sleep_for) * 1e9)};
            nanosleep(&ts, NULL);
#  endif
        } else {
#  if defined(_WIN32)
            Sleep(0);
#  else
            struct timespec ts = {0, 0};
            nanosleep(&ts, NULL);
#  endif
        }
    }
}
#endif

/* Pace the frame for wgr_set_target_fps(). Returns false if this frame should be
 * skipped (web only: the browser drives frames at display rate, and the canvas
 * keeps showing the last drawn frame). */
static bool pace_frame(void)
{
    static wgr_frame_pace_t headless_vsync = {.period = 1.0 / 60.0};
    wgr_frame_pace_t *pace = &wgr_rt.pace;
    double now = wgr_get_time();
    double wait;

    if (!wgr_frame_pace_enabled(pace)) {
        if (!wgr_platform_is_headless()) {
            return true; /* the display's vsync paces frames */
        }
        pace = &headless_vsync; /* no display: stand in for 60 Hz so async loads get real time */
    }
    wait = wgr_frame_pace_wait(pace, now);
#if defined(__EMSCRIPTEN__)
    /* run a frame that's due within half a display frame, so e.g. 30 fps on a
     * 60 Hz display runs every other frame instead of drifting */
    if (wait > 0.5 * wgr_platform_frame_duration()) {
        return false;
    }
#else
    if (wait > 0.0) {
        wait_until(now + wait);
        now = wgr_get_time();
    }
#endif
    wgr_frame_pace_mark(pace, now);
    return true;
}

/* Update this frame's delta and FPS smoothing. Returns the real elapsed time
 * since the previous frame (0 for the first), which drives the tick clock. */
static double update_frame_timing(void)
{
    double now = wgr_get_time();
    double elapsed = wgr_rt.last_frame_time > 0.0 ? now - wgr_rt.last_frame_time : 0.0;
    double dt;

    /* Measured from our own clock between frames that ran, not sokol's smoothed
     * frame duration: when swap timing is irregular (e.g. vsync blocking only every
     * other swap on NVIDIA + XWayland) the smoothed value drifts, and summed dt no
     * longer matches wall time. Clamped like sokol's raw delta. The first frame
     * assumes the target period, or 60 Hz. */
    if (wgr_rt.last_frame_time > 0.0) {
        dt = elapsed;
    } else {
        dt = wgr_frame_pace_enabled(&wgr_rt.pace) ? wgr_rt.pace.period : 1.0 / 60.0;
    }
    dt = dt < 0.000001 ? 0.000001 : (dt > 0.1 ? 0.1 : dt);
    wgr_rt.last_frame_time = now;
    wgr_rt.delta_time = dt;
    wgr_rt.fps_delta = wgr_rt.fps_delta > 0.0 ? wgr_rt.fps_delta + 0.05 * (dt - wgr_rt.fps_delta) : dt;
    return elapsed;
}

/* Run the ticks that are due (docs/PLAN-tick.md). Input edges seen by a tick are
 * those since the previous tick. */
static void run_ticks(double elapsed)
{
    const unsigned generation = wgr_rt.tick_generation;
    const float step = (float)wgr_rt.tick_clock.step;
    int ticks = wgr_tick_clock_advance(&wgr_rt.tick_clock, elapsed, WGR_MAX_TICKS_PER_FRAME);

    wgr_input_set_context(WGR_INPUT_CONTEXT_TICK);
    for (int i = 0; i < ticks; i++) {
        if (wgr_rt.tick_fn == NULL || wgr_rt.tick_generation != generation) {
            break; /* the tick was changed or removed from inside a tick */
        }
        wgr_rt.tick_fn(step, wgr_rt.tick_user_data);
        wgr_input_end_tick();
        wgr_module_end_tick_all();
        wgr_scene_end_tick_interaction();
    }
    wgr_input_set_context(WGR_INPUT_CONTEXT_FRAME);
}

static void on_frame(void)
{
    /* pump async asset loads; completion callbacks fire here (main thread) */
    wgr_asset_tick();

    if (!pace_frame()) {
        return;
    }
    wgr_module_begin_frame_all(); /* e.g. gamepads: polled, so ticks and the frame see them */
    wgr_scene_update_interaction(); /* before the ticks: they read it too */
    run_ticks(update_frame_timing());
    wgr_module_update_all((float)wgr_rt.delta_time); /* e.g. particles: spawn and retire, once a frame */

    if (wgr_rt.frame_fn != NULL) {
        wgr_rt.frame_fn((float)wgr_rt.delta_time, wgr_tick_clock_fraction(&wgr_rt.tick_clock),
                       wgr_rt.frame_user_data);
    }
    if (!wgr_rt.first_frame_done) {
        wgr_rt.first_frame_done = true;
        wgr_platform_mark("wgr:first-frame");
    }

    /* clear frame input edges after the frame; sokol delivers the next frame's
     * events before the next frame_cb */
    wgr_input_end_frame();
    wgr_module_frame_done_all();
    wgr_scene_end_frame_interaction();
    if (wgr_rt.tick_fn == NULL) { /* nothing reads tick edges: don't let them pile up */
        wgr_input_end_tick();
        wgr_module_end_tick_all();
        wgr_scene_end_tick_interaction();
    }
}

static void on_event(const void *ev)
{
    wgr_input_handle_event((const struct sapp_event *)ev);
}

static void on_cleanup(void)
{
    if (wgr_rt.cleanup_fn != NULL) {
        wgr_rt.cleanup_fn(wgr_rt.cleanup_user_data);
    }

    wgr_module_deinit_all(); /* before the core they use */
    wgr_debug_deinit();
    wgr_input_deinit();
    wgr_event_deinit();
    wgr_asset_deinit();
    wgr_fs_deinit();
    wgr_camera3d_deinit();

    wgr_text_deinit();
    wgr_font_deinit();
    wgr_shape2d_deinit();
    wgr_shape3d_deinit();
    wgr_scene_deinit();
    wgr_render_deinit();

    sg_shutdown();
    wgr_initialized = false;
    wgr_logger_deinit();
}

WGR_KEEP
int wgr_run(void)
{
    if (!wgr_configured) {
        wgr_logger_error("wgr_run() called before wgr_init_values()");
        return WGR_INIT_ERR_UNKNOWN;
    }

    bool fullscreen = (wgr_rt.window_flags & WGR_WINDOW_FLAG_FULLSCREEN_MODE) != 0;
    bool high_dpi = (wgr_rt.window_flags & WGR_WINDOW_FLAG_LOW_DPI) == 0;
    int sample_count = (wgr_rt.window_flags & WGR_WINDOW_FLAG_MSAA_4X_HINT) != 0 ? 4 : 1;
    /* vsync is on unless explicitly turned off; wgr_set_target_fps() caps below it */
    bool disable_vsync = (wgr_rt.window_flags & WGR_WINDOW_FLAG_VSYNC_OFF) != 0;

    wgr_platform_run(&(wgr_platform_desc_t){
        .init = on_init,
        .frame = on_frame,
        .event = on_event,
        .cleanup = on_cleanup,
        .width = wgr_rt.window_width,
        .height = wgr_rt.window_height,
        .title = wgr_rt.window_title,
        .fullscreen = fullscreen,
        .high_dpi = high_dpi,
        .sample_count = sample_count,
        .disable_vsync = disable_vsync,
        .transparent = (wgr_rt.window_flags & WGR_WINDOW_FLAG_WINDOW_TRANSPARENT) != 0,
    });
    return 0;
}

WGR_KEEP
void wgr_request_quit(void)
{
    wgr_platform_request_quit();
}

WGR_KEEP
bool wgr_is_initialized(void)
{
    return wgr_initialized;
}

WGR_KEEP
const char *wgr_get_platform(void)
{
#if defined(PLATFORM_WEB) || defined(__EMSCRIPTEN__)
    return "web";
#else
    return "desktop";
#endif
}

WGR_KEEP
void wgr_set_target_fps(int fps)
{
    wgr_rt.target_fps = fps;
    wgr_frame_pace_set_fps(&wgr_rt.pace, fps);
}

double wgr_get_fps_delta(void)
{
    return wgr_rt.fps_delta;
}

WGR_KEEP
double wgr_get_time(void)
{
    return stm_sec(stm_since(wgr_rt.start_ticks));
}
