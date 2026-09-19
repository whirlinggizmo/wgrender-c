#include "sk.h"

#include <stddef.h>
#include <string.h>
#if defined(_WIN32)
#  include <windows.h>
#elif !defined(__EMSCRIPTEN__)
#  include <time.h>
#endif

#include "internal/exports.h"
#include "internal/sk_frame_pace.h"
#include "internal/sk_internal.h"
#include "internal/sk_emitter.h"
#include "internal/sk_environment.h"
#include "internal/sk_light.h"
#include "internal/sk_material.h"
#include "internal/sk_platform.h"
#include "internal/sk_render.h"
#include "internal/sk_scene.h"
#include "internal/sk_sprite2d.h"
#include "internal/sk_tick_clock.h"
#include "sk_logger.h"
#include "sk_version.h"

#include "sokol_gfx.h"
#include "sokol_log.h"
#include "sokol_time.h"

bool sk_initialized = false;

typedef struct {
    int window_width;
    int window_height;
    char window_title[256];
    unsigned int window_flags;

    sk_frame_fn frame_fn;
    void *frame_user_data;
    sk_tick_fn tick_fn;
    void *tick_user_data;
    sk_tick_clock_t tick_clock;
    unsigned tick_generation; /* bumped by sk_set_tick, to notice changes made inside a tick */
    sk_lifecycle_fn init_fn;
    void *init_user_data;
    sk_lifecycle_fn cleanup_fn;
    void *cleanup_user_data;

    int target_fps;
    sk_frame_pace_t pace;

    uint64_t start_ticks;
    double delta_time;      /* this frame's delta (seconds), passed to the frame callback */
    double last_frame_time; /* sk_get_time() when the previous frame ran; 0 before the first */
    double fps_delta;       /* smoothed delta for the FPS counter */
    bool first_frame_done;
} sk_runtime_t;

static sk_runtime_t sk_rt;
static bool sk_configured = false;

static void apply_defaults(sk_runtime_t *rt)
{
    if (rt->window_width <= 0) {
        rt->window_width = 1024;
    }
    if (rt->window_height <= 0) {
        rt->window_height = 768;
    }
    if (rt->window_title[0] == '\0') {
        strncpy(rt->window_title, "libsk", sizeof(rt->window_title) - 1);
    }
    if (rt->target_fps == 0) {
        rt->target_fps = -1; /* -1: unpaced (vsync, or uncapped with SK_WINDOW_FLAG_VSYNC_OFF) */
    }
    sk_frame_pace_set_fps(&rt->pace, rt->target_fps);
}

SK_KEEP
int sk_init_values(int window_width,
                   int window_height,
                   const char *window_title,
                   unsigned int window_flags)
{
    if (sk_initialized) {
        return SK_INIT_ERR_ALREADY_INITIALIZED;
    }

    memset(&sk_rt, 0, sizeof(sk_rt));
    sk_rt.window_width = window_width;
    sk_rt.window_height = window_height;
    sk_rt.window_flags = window_flags;
    if (window_title != NULL && window_title[0] != '\0') {
        strncpy(sk_rt.window_title, window_title, sizeof(sk_rt.window_title) - 1);
    }
    apply_defaults(&sk_rt);

    sk_logger_init();
    sk_logger_info("libsk %s", sk_version_string());

    sk_configured = true;
    return SK_INIT_OK;
}

SK_KEEP
void sk_set_frame(sk_frame_fn frame_fn, void *user_data)
{
    sk_rt.frame_fn = frame_fn;
    sk_rt.frame_user_data = user_data;
}

SK_KEEP
void sk_set_tick(sk_tick_fn tick_fn, void *user_data, int hz)
{
    sk_rt.tick_fn = hz > 0 ? tick_fn : NULL;
    sk_rt.tick_user_data = user_data;
    sk_tick_clock_set_rate(&sk_rt.tick_clock, tick_fn != NULL ? hz : 0);
    sk_rt.tick_generation++;
}

SK_KEEP
void sk_set_init(sk_lifecycle_fn init_fn, void *user_data)
{
    sk_rt.init_fn = init_fn;
    sk_rt.init_user_data = user_data;
}

SK_KEEP
void sk_set_cleanup(sk_lifecycle_fn cleanup_fn, void *user_data)
{
    sk_rt.cleanup_fn = cleanup_fn;
    sk_rt.cleanup_user_data = user_data;
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
    sk_platform_mark("sk:init"); /* startup points: tools/webstart.mjs */
    sg_setup(&(sg_desc){
        .environment = sk_platform_environment(),
        .logger.func = slog_func,
        .buffer_pool_size = SK_GFX_BUFFER_POOL_SIZE,
        .image_pool_size = SK_GFX_IMAGE_POOL_SIZE,
        .view_pool_size = SK_GFX_VIEW_POOL_SIZE,
    });
    sk_logger_info("libsk: %s backend", backend_name(sg_query_backend()));
    stm_setup();
    sk_rt.start_ticks = stm_now();

    /* GPU-backed subsystems (need a live sg context) */
    sk_render_init();
    sk_scene_init();  /* registry must exist before drawables register */
    sk_shape2d_init();
    sk_shape3d_init();
    sk_texture_init();
    sk_sprite3d_init();
    sk_sprite2d_init();
    sk_emitter_init();
    sk_light_init();
    sk_material_init();
    sk_environment_init();
    sk_model_init();
    sk_font_init();
    sk_text_init();
    sk_text2d_init(); /* retained text; delegates to the text layer */
    sk_text3d_init();

    /* CPU-side stores */
    sk_camera3d_init();
    sk_fs_init(NULL);   /* local storage; asset acquisition sits on top */
    sk_asset_init();
    sk_audio_init();
    sk_sound_init();
    sk_event_init();
    sk_input_init();
    sk_debug_init();

    sk_initialized = true;
    sk_platform_mark("sk:subsystems");

    if (sk_rt.init_fn != NULL) {
        sk_rt.init_fn(sk_rt.init_user_data);
    }
    sk_platform_mark("sk:user-init");
}

#if !defined(__EMSCRIPTEN__)
/* Wait until `deadline` (seconds on the sk_get_time clock): sleep, then yield for
 * the last moment because OS sleeps overshoot by up to a millisecond or two.
 * (Audio mixes on its own thread, so nothing needs feeding while waiting.) */
static void wait_until(double deadline)
{
    const double spin = 0.002;
    for (;;) {
        double remaining = deadline - sk_get_time();
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

/* Pace the frame for sk_set_target_fps(). Returns false if this frame should be
 * skipped (web only: the browser drives frames at display rate, and the canvas
 * keeps showing the last drawn frame). */
static bool pace_frame(void)
{
    static sk_frame_pace_t headless_vsync = {.period = 1.0 / 60.0};
    sk_frame_pace_t *pace = &sk_rt.pace;
    double now = sk_get_time();
    double wait;

    if (!sk_frame_pace_enabled(pace)) {
        if (!sk_platform_is_headless()) {
            return true; /* the display's vsync paces frames */
        }
        pace = &headless_vsync; /* no display: stand in for 60 Hz so async loads get real time */
    }
    wait = sk_frame_pace_wait(pace, now);
#if defined(__EMSCRIPTEN__)
    /* run a frame that's due within half a display frame, so e.g. 30 fps on a
     * 60 Hz display runs every other frame instead of drifting */
    if (wait > 0.5 * sk_platform_frame_duration()) {
        return false;
    }
#else
    if (wait > 0.0) {
        wait_until(now + wait);
        now = sk_get_time();
    }
#endif
    sk_frame_pace_mark(pace, now);
    return true;
}

/* Update this frame's delta and FPS smoothing. Returns the real elapsed time
 * since the previous frame (0 for the first), which drives the tick clock. */
static double update_frame_timing(void)
{
    double now = sk_get_time();
    double elapsed = sk_rt.last_frame_time > 0.0 ? now - sk_rt.last_frame_time : 0.0;
    double dt;

    /* Measured from our own clock between frames that ran, not sokol's smoothed
     * frame duration: when swap timing is irregular (e.g. vsync blocking only every
     * other swap on NVIDIA + XWayland) the smoothed value drifts, and summed dt no
     * longer matches wall time. Clamped like sokol's raw delta. The first frame
     * assumes the target period, or 60 Hz. */
    if (sk_rt.last_frame_time > 0.0) {
        dt = elapsed;
    } else {
        dt = sk_frame_pace_enabled(&sk_rt.pace) ? sk_rt.pace.period : 1.0 / 60.0;
    }
    dt = dt < 0.000001 ? 0.000001 : (dt > 0.1 ? 0.1 : dt);
    sk_rt.last_frame_time = now;
    sk_rt.delta_time = dt;
    sk_rt.fps_delta = sk_rt.fps_delta > 0.0 ? sk_rt.fps_delta + 0.05 * (dt - sk_rt.fps_delta) : dt;
    return elapsed;
}

/* Run the ticks that are due (docs/PLAN-tick.md). Input edges seen by a tick are
 * those since the previous tick. */
static void run_ticks(double elapsed)
{
    const unsigned generation = sk_rt.tick_generation;
    const float step = (float)sk_rt.tick_clock.step;
    int ticks = sk_tick_clock_advance(&sk_rt.tick_clock, elapsed, SK_MAX_TICKS_PER_FRAME);

    sk_input_set_context(SK_INPUT_CONTEXT_TICK);
    for (int i = 0; i < ticks; i++) {
        if (sk_rt.tick_fn == NULL || sk_rt.tick_generation != generation) {
            break; /* the tick was changed or removed from inside a tick */
        }
        sk_rt.tick_fn(step, sk_rt.tick_user_data);
        sk_input_end_tick();
        sk_scene_end_tick_interaction();
    }
    sk_input_set_context(SK_INPUT_CONTEXT_FRAME);
}

static void on_frame(void)
{
    /* pump async asset loads; completion callbacks fire here (main thread) */
    sk_asset_tick();

    if (!pace_frame()) {
        return;
    }
    sk_scene_update_interaction(); /* before the ticks: they read it too */
    run_ticks(update_frame_timing());
    sk_emitter_update((float)sk_rt.delta_time); /* particles: spawn and retire, once a frame */

    if (sk_rt.frame_fn != NULL) {
        sk_rt.frame_fn((float)sk_rt.delta_time, sk_tick_clock_fraction(&sk_rt.tick_clock),
                       sk_rt.frame_user_data);
    }
    if (!sk_rt.first_frame_done) {
        sk_rt.first_frame_done = true;
        sk_platform_mark("sk:first-frame");
    }

    /* clear frame input edges after the frame; sokol delivers the next frame's
     * events before the next frame_cb */
    sk_input_end_frame();
    sk_scene_end_frame_interaction();
    if (sk_rt.tick_fn == NULL) { /* nothing reads tick edges: don't let them pile up */
        sk_input_end_tick();
        sk_scene_end_tick_interaction();
    }
}

static void on_event(const void *ev)
{
    sk_input_handle_event((const struct sapp_event *)ev);
}

static void on_cleanup(void)
{
    if (sk_rt.cleanup_fn != NULL) {
        sk_rt.cleanup_fn(sk_rt.cleanup_user_data);
    }

    sk_debug_deinit();
    sk_input_deinit();
    sk_event_deinit();
    sk_sound_deinit();
    sk_audio_deinit();
    sk_asset_deinit();
    sk_fs_deinit();
    sk_camera3d_deinit();

    sk_text3d_deinit();
    sk_text2d_deinit();
    sk_text_deinit();
    sk_font_deinit();
    sk_model_deinit();
    sk_environment_deinit();
    sk_material_deinit();
    sk_light_deinit();
    sk_emitter_deinit();
    sk_sprite2d_deinit();
    sk_sprite3d_deinit();
    sk_texture_deinit();
    sk_shape2d_deinit();
    sk_shape3d_deinit();
    sk_scene_deinit();
    sk_render_deinit();

    sg_shutdown();
    sk_initialized = false;
    sk_logger_deinit();
}

SK_KEEP
int sk_run(void)
{
    if (!sk_configured) {
        sk_logger_error("sk_run() called before sk_init_values()");
        return SK_INIT_ERR_UNKNOWN;
    }

    bool fullscreen = (sk_rt.window_flags & SK_WINDOW_FLAG_FULLSCREEN_MODE) != 0;
    bool high_dpi = (sk_rt.window_flags & SK_WINDOW_FLAG_LOW_DPI) == 0;
    int sample_count = (sk_rt.window_flags & SK_WINDOW_FLAG_MSAA_4X_HINT) != 0 ? 4 : 1;
    /* vsync is on unless explicitly turned off; sk_set_target_fps() caps below it */
    bool disable_vsync = (sk_rt.window_flags & SK_WINDOW_FLAG_VSYNC_OFF) != 0;

    sk_platform_run(&(sk_platform_desc_t){
        .init = on_init,
        .frame = on_frame,
        .event = on_event,
        .cleanup = on_cleanup,
        .width = sk_rt.window_width,
        .height = sk_rt.window_height,
        .title = sk_rt.window_title,
        .fullscreen = fullscreen,
        .high_dpi = high_dpi,
        .sample_count = sample_count,
        .disable_vsync = disable_vsync,
    });
    return 0;
}

SK_KEEP
void sk_request_quit(void)
{
    sk_platform_request_quit();
}

SK_KEEP
bool sk_is_initialized(void)
{
    return sk_initialized;
}

SK_KEEP
const char *sk_get_platform(void)
{
#if defined(PLATFORM_WEB) || defined(__EMSCRIPTEN__)
    return "web";
#else
    return "desktop";
#endif
}

SK_KEEP
void sk_set_target_fps(int fps)
{
    sk_rt.target_fps = fps;
    sk_frame_pace_set_fps(&sk_rt.pace, fps);
}

double sk_get_fps_delta(void)
{
    return sk_rt.fps_delta;
}

SK_KEEP
double sk_get_time(void)
{
    return stm_sec(stm_since(sk_rt.start_ticks));
}
