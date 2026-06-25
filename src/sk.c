#include "sk.h"

#include <stddef.h>
#include <string.h>

#include "internal/exports.h"
#include "internal/sk_internal.h"
#include "sk_logger.h"
#include "sk_version.h"

#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
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
    sk_lifecycle_fn init_fn;
    void *init_user_data;
    sk_lifecycle_fn cleanup_fn;
    void *cleanup_user_data;

    int target_fps;

    uint64_t start_ticks;
    double delta_time;
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
        rt->target_fps = -1; /* -1: follow vsync */
    }
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

static void on_init(void)
{
    sg_setup(&(sg_desc){
        .environment = sglue_environment(),
        .logger.func = slog_func,
    });
    stm_setup();
    sk_rt.start_ticks = stm_now();

    /* GPU-backed subsystems (need a live sg context) */
    sk_render_init();
    sk_scene_init();  /* registry must exist before drawables register */
    sk_shape_init();
    sk_texture_init();
    sk_sprite3d_init();
    sk_model_init();
    sk_font_init();
    sk_text_init();

    /* CPU-side stores */
    sk_color_init();
    sk_camera3d_init();
    sk_asset_init();
    sk_audio_init();
    sk_sound_init();
    sk_music_init();
    sk_event_init();
    sk_input_init();
    sk_debug_init();

    sk_initialized = true;

    if (sk_rt.init_fn != NULL) {
        sk_rt.init_fn(sk_rt.init_user_data);
    }
}

static void on_frame(void)
{
    sk_rt.delta_time = sapp_frame_duration();

    /* pump async asset loads; completion callbacks fire here (main thread) */
    sk_asset_tick();
    /* mix + push one audio block */
    sk_audio_tick();

    if (sk_rt.frame_fn != NULL) {
        sk_rt.frame_fn(sk_rt.frame_user_data);
    }

    /* clear per-frame input edges/deltas after the user frame; sokol delivers
     * the next frame's events before the next frame_cb */
    sk_input_new_frame();
}

static void on_event(const sapp_event *ev)
{
    sk_input_handle_event(ev);
}

static void on_cleanup(void)
{
    if (sk_rt.cleanup_fn != NULL) {
        sk_rt.cleanup_fn(sk_rt.cleanup_user_data);
    }

    sk_debug_deinit();
    sk_input_deinit();
    sk_event_deinit();
    sk_music_deinit();
    sk_sound_deinit();
    sk_audio_deinit();
    sk_asset_deinit();
    sk_camera3d_deinit();
    sk_color_deinit();

    sk_text_deinit();
    sk_font_deinit();
    sk_model_deinit();
    sk_sprite3d_deinit();
    sk_texture_deinit();
    sk_shape_deinit();
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
    bool high_dpi = (sk_rt.window_flags & SK_WINDOW_FLAG_WINDOW_HIGHDPI) != 0;
    int sample_count = (sk_rt.window_flags & SK_WINDOW_FLAG_MSAA_4X_HINT) != 0 ? 4 : 1;
    int swap_interval = (sk_rt.target_fps == 0) ? 1 : 1; /* vsync; arbitrary fps TBD */

    sapp_run(&(sapp_desc){
        .init_cb = on_init,
        .frame_cb = on_frame,
        .event_cb = on_event,
        .cleanup_cb = on_cleanup,
        .width = sk_rt.window_width,
        .height = sk_rt.window_height,
        .window_title = sk_rt.window_title,
        .fullscreen = fullscreen,
        .high_dpi = high_dpi,
        .sample_count = sample_count,
        .swap_interval = swap_interval,
        .logger.func = slog_func,
    });
    return 0;
}

SK_KEEP
void sk_request_quit(void)
{
    sapp_request_quit();
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
}

SK_KEEP
float sk_get_delta_time(void)
{
    return (float)sk_rt.delta_time;
}

SK_KEEP
double sk_get_time(void)
{
    return stm_sec(stm_since(sk_rt.start_ticks));
}
