#ifndef SK_INTERNAL_H
#define SK_INTERNAL_H

#include <stdbool.h>

#include "sk_text.h" /* sk_text_align_t */
#include "sk_types.h"

/* Shared lifecycle/state across the C translation units. These are NOT public
 * API. */

extern bool sk_initialized;

/* smoothed seconds per frame that actually ran (FPS counter) */
double sk_get_fps_delta(void);

/* framebuffer pixels per logical pixel (1 unless high-DPI); see sk_window_get_screen_size */
float sk_window_dpi_scale(void);

/* logger */
void sk_logger_init(void);
void sk_logger_deinit(void);

/* event bus */
int sk_event_init(void);
void sk_event_deinit(void);

/* color store */
void sk_color_set(sk_handle_t handle, int r, int g, int b, int a);

/* camera3d */
void sk_camera3d_init(void);
void sk_camera3d_deinit(void);

/* render: see internal/sk_render.h */

/* scene */
void sk_scene_init(void);
void sk_scene_deinit(void);

/* shape */
void sk_shape2d_init(void);
void sk_shape2d_deinit(void);
void sk_shape3d_init(void);
void sk_shape3d_deinit(void);

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
void sk_audio_deinit(void);
void sk_sound_init(void);
void sk_sound_deinit(void);

/* font (fontstash) */
void sk_font_init(void);
void sk_font_deinit(void);
void sk_font_flush(void); /* upload the font atlas (outside a render pass) */

/* text (fontstash; the built-in font is embedded) */
void sk_text_init(void);
void sk_text_deinit(void);
/* The font to use for `font`: the default font for 0 (sk_text_set_default_font, else
 * the built-in font), and the built-in font for a font that isn't loaded. */
sk_handle_t sk_text_resolve_font(sk_handle_t font);
/* A block of text laid out in `font` (0 = default) at `size`: lines break at
 * newlines and, when max_width > 0, between words that don't fit (a word longer
 * than that keeps a line to itself). Lines are one font line height apart.
 * sk_text_block_size gives the widest line and the total height; sk_text_block_draw
 * draws it with (left, top) as the block's top-left corner, aligning each line
 * inside a box `box_width` wide (the block's own width when it's not wider). */
vec2_t sk_text_block_size(sk_handle_t font, const char *text, float size, float max_width);
/* The same line splitting, for callers that draw their own glyphs (text3d): the
 * font and size must already be selected in fontstash, and max_width is in those
 * units (0: no wrap). Fills [starts[i], ends[i]) and returns the line count, at
 * most max_lines. */
int sk_text_split_lines(const char *text, float max_width, const char **starts, const char **ends, int max_lines);
void sk_text_block_draw(sk_handle_t font, const char *text, float left, float top, float size,
                        sk_color_t color, float max_width, float box_width, sk_text_align_t align_x);

/* text2d (retained text object, built on the text layer) */
void sk_text2d_init(void);
void sk_text2d_deinit(void);

/* text3d (TrueType text in the 3D world) */
void sk_text3d_init(void);
void sk_text3d_deinit(void);

/* debug overlay */
void sk_debug_init(void);
void sk_debug_deinit(void);
void sk_debug_draw(void);

/* input */
void sk_input_init(void);
void sk_input_deinit(void);
struct sapp_event; /* fwd decl from sokol_app */
void sk_input_handle_event(const struct sapp_event *ev);
/* Input edges are relative to the running callback (docs/PLAN-tick.md). The
 * runtime sets the context before each tick/frame callback and clears that
 * context's edges after it. */
typedef enum {
    SK_INPUT_CONTEXT_FRAME = 0,
    SK_INPUT_CONTEXT_TICK = 1,
} sk_input_context_t;
void sk_input_set_context(sk_input_context_t context);
void sk_input_end_tick(void);  /* clear tick edges (after each tick) */
void sk_input_end_frame(void); /* clear frame edges (after the frame callback) */
sk_input_context_t sk_input_get_context(void);
/* The pointer (mouse, or the primary touch) with this frame's edges, whatever the
 * context: position in logical pixels, primary button held / pressed / released. */
void sk_input_get_pointer_frame(float *x, float *y, bool *down, bool *pressed, bool *released);
void sk_input_set_scene_pointer_captured(bool captured); /* sk_scene.c's interaction capture */

#endif // SK_INTERNAL_H
