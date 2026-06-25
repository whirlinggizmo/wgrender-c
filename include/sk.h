#ifndef SK_H
#define SK_H

#include <stdbool.h>

#include "sk_asset.h"    // IWYU pragma: keep
#include "sk_camera3d.h" // IWYU pragma: keep
#include "sk_color.h"   // IWYU pragma: keep
#include "sk_debug.h"   // IWYU pragma: keep
#include "sk_event.h"   // IWYU pragma: keep
#include "sk_font.h"    // IWYU pragma: keep
#include "sk_handle.h"  // IWYU pragma: keep
#include "sk_input.h"   // IWYU pragma: keep
#include "sk_keys.h"    // IWYU pragma: keep
#include "sk_logger.h"  // IWYU pragma: keep
#include "sk_model.h"   // IWYU pragma: keep
#include "sk_music.h"   // IWYU pragma: keep
#include "sk_render.h"  // IWYU pragma: keep
#include "sk_scene.h"   // IWYU pragma: keep
#include "sk_shape.h"   // IWYU pragma: keep
#include "sk_sound.h"   // IWYU pragma: keep
#include "sk_sprite3d.h" // IWYU pragma: keep
#include "sk_text.h"    // IWYU pragma: keep
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

/* Per-frame callback invoked by the runtime once the window/GPU is ready.
 *
 * NOTE ON THE LOOP MODEL: sokol_app owns the frame loop, so libsk is
 * callback-driven rather than poll-driven (no sk_tick()). Configure with
 * sk_init_values(), register a frame function with sk_set_frame(), then call
 * sk_run(). On desktop sk_run() blocks until the window closes; on web it
 * returns immediately and the browser drives frames. */
typedef void (*sk_frame_fn)(void *user_data);
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
void sk_set_init(sk_lifecycle_fn init_fn, void *user_data);
void sk_set_cleanup(sk_lifecycle_fn cleanup_fn, void *user_data);

/* Enter the runtime loop. Blocks on desktop, returns immediately on web.
 * Returns 0 on normal exit. */
int sk_run(void);

/* Ask the runtime to close the window / end the loop. */
void sk_request_quit(void);

bool sk_is_initialized(void);
const char *sk_get_platform(void);

void sk_set_target_fps(int fps);
float sk_get_delta_time(void);
double sk_get_time(void);

#ifdef __cplusplus
}
#endif

#endif // SK_H
