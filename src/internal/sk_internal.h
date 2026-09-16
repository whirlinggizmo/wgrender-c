#ifndef SK_INTERNAL_H
#define SK_INTERNAL_H

#include <stdbool.h>

#include "sk_types.h"

/* Shared lifecycle/state across the C translation units. These are NOT public
 * API. */

extern bool sk_initialized;

/* smoothed seconds per frame that actually ran (FPS counter) */
double sk_get_fps_delta(void);

/* logger */
void sk_logger_init(void);
void sk_logger_deinit(void);

/* event bus */
int sk_event_init(void);
void sk_event_deinit(void);

/* color store */
void sk_color_init(void);
void sk_color_deinit(void);
color_t sk_color_get(sk_handle_t handle); /* normalized 0..1 rgba */
void sk_color_set(sk_handle_t handle, int r, int g, int b, int a);

/* camera3d */
void sk_camera3d_init(void);
void sk_camera3d_deinit(void);

/* render: see internal/sk_render.h */

/* scene */
void sk_scene_init(void);
void sk_scene_deinit(void);

/* shape */
void sk_shape_init(void);
void sk_shape_deinit(void);

/* texture */
void sk_texture_init(void);
void sk_texture_deinit(void);

/* sprite3d */
void sk_sprite3d_init(void);
void sk_sprite3d_deinit(void);

/* model (cgltf, custom pipeline) */
void sk_model_init(void);
void sk_model_deinit(void);
/* model draw queue: see internal/sk_model.h */

/* fs (local storage; web idbfs later) — asset acquisition sits on top */
void sk_fs_init(const char *root_dir);
void sk_fs_deinit(void);

/* asset (ensure/fetch acquisition, async) */
void sk_asset_init(void);
void sk_asset_tick(void);
void sk_asset_deinit(void);

/* audio (sokol_audio mixer) + sound/music stores */
void sk_audio_init(void);
void sk_audio_tick(void);
void sk_audio_deinit(void);
void sk_sound_init(void);
void sk_sound_deinit(void);

/* font (fontstash) */
void sk_font_init(void);
void sk_font_deinit(void);
void sk_font_flush(void); /* upload the font atlas (outside a render pass) */

/* text (sokol_debugtext) */
void sk_text_init(void);
void sk_text_deinit(void);
void sk_text_flush(void); /* emit recorded debugtext into the current pass */

/* text2d (retained text object, built on the text layer) */
void sk_text2d_init(void);
void sk_text2d_deinit(void);

/* debug overlay */
void sk_debug_init(void);
void sk_debug_deinit(void);
void sk_debug_draw(void);

/* input */
void sk_input_init(void);
void sk_input_deinit(void);
struct sapp_event; /* fwd decl from sokol_app */
void sk_input_handle_event(const struct sapp_event *ev);
void sk_input_new_frame(void); /* clear per-frame edge state at frame start */

#endif // SK_INTERNAL_H
