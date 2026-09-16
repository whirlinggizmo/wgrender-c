#include "internal/sk_platform.h"

#include <stdlib.h>

#include "sk_logger.h"

#if !defined(SK_HEADLESS)
/* -------------------------------------------------------------- sokol_app ---- */

#include "sokol_app.h"
#include "sokol_glue.h"
#include "sokol_log.h"

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
