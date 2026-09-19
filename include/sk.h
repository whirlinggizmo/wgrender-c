#ifndef SK_H
#define SK_H

#include <stdbool.h>

#include "sk_asset.h"    // IWYU pragma: keep
#include "sk_audio.h"    // IWYU pragma: keep
#include "sk_camera3d.h" // IWYU pragma: keep
#include "sk_color.h"   // IWYU pragma: keep
#include "sk_debug.h"   // IWYU pragma: keep
#include "sk_environment.h" // IWYU pragma: keep
#include "sk_event.h"   // IWYU pragma: keep
#include "sk_font.h"    // IWYU pragma: keep
#include "sk_handle.h"  // IWYU pragma: keep
#include "sk_input.h"   // IWYU pragma: keep
#include "sk_keys.h"    // IWYU pragma: keep
#include "sk_light.h"   // IWYU pragma: keep
#include "sk_logger.h"  // IWYU pragma: keep
#include "sk_material.h" // IWYU pragma: keep
#include "sk_model.h"   // IWYU pragma: keep
#include "sk_pick.h"    // IWYU pragma: keep
#include "sk_render.h"  // IWYU pragma: keep
#include "sk_scene.h"   // IWYU pragma: keep
#include "sk_shader.h"  // IWYU pragma: keep
#include "sk_shape2d.h" // IWYU pragma: keep
#include "sk_shape3d.h" // IWYU pragma: keep
#include "sk_sound.h"   // IWYU pragma: keep
#include "sk_emitter2d.h" // IWYU pragma: keep
#include "sk_emitter3d.h" // IWYU pragma: keep
#include "sk_sprite2d.h" // IWYU pragma: keep
#include "sk_sprite3d.h" // IWYU pragma: keep
#include "sk_text.h"    // IWYU pragma: keep
#include "sk_text2d.h"  // IWYU pragma: keep
#include "sk_text3d.h"  // IWYU pragma: keep
#include "sk_texture.h" // IWYU pragma: keep
#include "sk_types.h"   // IWYU pragma: keep
#include "sk_version.h" // IWYU pragma: keep
#include "sk_window.h"  // IWYU pragma: keep

#ifdef __cplusplus
extern "C" {
#endif

typedef enum sk_init_result_t {
    SK_INIT_OK = 0,
    SK_INIT_ERR_UNKNOWN = -1,
    SK_INIT_ERR_ALREADY_INITIALIZED = -2,
    SK_INIT_ERR_WINDOW = -5,
} sk_init_result_t;

/* Loop model: sokol_app owns the frame loop, so libsk is callback-driven.
 * Configure with sk_init_values(), register callbacks, then call sk_run(). On
 * desktop sk_run() blocks until the window closes; on web it returns immediately
 * and the browser drives frames.
 *
 * Two callbacks, for two rates (see docs/PLAN-tick.md):
 *
 *   tick   simulation at a fixed rate (sk_set_tick). Runs 0..N times before each
 *          frame, always with dt = 1/hz. Use it for physics and gameplay rules.
 *          Never draw here.
 *   frame  once per rendered frame (sk_set_frame): variable update and drawing.
 *          dt is the time since the previous frame. tick_fraction (0..1) is how
 *          far this frame is into the next tick, for drawing tick state smoothly:
 *          lerp(previous_tick_value, tick_value, tick_fraction). 0 without a tick.
 *
 * After a stall, at most 5 ticks run per frame and the rest of the backlog is
 * dropped, so simulation time falls behind instead of snowballing.
 *
 * Input edges (pressed/released, mouse and wheel deltas, typed keys) are relative
 * to the callback reading them: inside a tick, since the previous tick; inside a
 * frame, since the previous frame. Every press is seen by exactly one tick. */
typedef void (*sk_tick_fn)(float dt, void *user_data);
typedef void (*sk_frame_fn)(float dt, float tick_fraction, void *user_data);
typedef void (*sk_lifecycle_fn)(void *user_data);

/* Configure the runtime (logger + window settings). Does NOT open the window;
 * sk_run() does that. Returns an sk_init_result_t. */
int sk_init_values(int window_width,
                   int window_height,
                   const char *window_title,
                   unsigned int window_flags);

/* Register the per-frame callback (required) and optional init/cleanup hooks
 * that fire after the GPU is ready / before it is torn down. */
void sk_set_frame(sk_frame_fn frame_fn, void *user_data);
void sk_set_tick(sk_tick_fn tick_fn, void *user_data, int hz); /* hz <= 0 or NULL: no tick */
void sk_set_init(sk_lifecycle_fn init_fn, void *user_data);
void sk_set_cleanup(sk_lifecycle_fn cleanup_fn, void *user_data);

/* Enter the runtime loop. Blocks on desktop, returns immediately on web.
 * Returns 0 on normal exit. */
int sk_run(void);

/* Ask the runtime to close the window / end the loop. */
void sk_request_quit(void);

bool sk_is_initialized(void);
const char *sk_get_platform(void);

/* Frame rate (a power/heat cap; use a tick for simulation rate).
 * Frames are locked to the display's vsync by default. sk_set_target_fps(fps)
 * caps the rate: fps <= 0 means no cap (vsync rate, or as fast as possible with
 * SK_WINDOW_FLAG_VSYNC_OFF). With vsync on, a cap can only lower the rate. On
 * desktop the runtime sleeps until each frame is due; on the web it skips browser
 * frames that come too early. Can be called at any time. */
void sk_set_target_fps(int fps);
double sk_get_time(void);

#ifdef __cplusplus
}
#endif

#endif // SK_H
