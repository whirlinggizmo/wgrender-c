#ifndef WGR_INTERNAL_H
#define WGR_INTERNAL_H

#include <stdbool.h>

#include "wgr_text.h" /* wgr_text_align_t */
#include "wgr_types.h"

/* Shared lifecycle/state across the C translation units. These are NOT public
 * API. */

extern bool wgr_initialized;

/* smoothed seconds per frame that actually ran (FPS counter) */
double wgr_get_fps_delta(void);

/* framebuffer pixels per logical pixel (1 unless high-DPI); see wgr_window_get_screen_size */
float wgr_window_dpi_scale(void);

/* logger */
void wgr_logger_init(void);
void wgr_logger_deinit(void);

/* event bus */
int wgr_event_init(void);
void wgr_event_deinit(void);

/* color store */
void wgr_color_set(wgr_handle_t handle, int r, int g, int b, int a);

/* camera3d */
void wgr_camera3d_init(void);
void wgr_camera3d_deinit(void);

/* render: see internal/wgr_render.h */

/* scene */
void wgr_scene_init(void);
void wgr_scene_deinit(void);

/* shape */
void wgr_shape2d_init(void);
void wgr_shape2d_deinit(void);
void wgr_shape3d_init(void);
void wgr_shape3d_deinit(void);

/* texture */
void wgr_texture_init(void);
void wgr_texture_deinit(void);

/* sprite3d */
void wgr_sprite3d_init(void);
void wgr_sprite3d_deinit(void);

/* model (cgltf, custom pipeline) */
void wgr_model_init(void);
void wgr_model_deinit(void);
/* model draw queue: see internal/wgr_model.h */

/* fs (local storage; web idbfs later) — asset acquisition sits on top */
void wgr_fs_init(const char *root_dir);
void wgr_fs_deinit(void);

/* asset (ensure/fetch acquisition, async) */
void wgr_asset_init(void);
void wgr_asset_tick(void);
void wgr_asset_deinit(void);

/* audio (sokol_audio mixer) + sound/music stores */
void wgr_audio_init(void);
void wgr_audio_deinit(void);
void wgr_sound_init(void);
void wgr_sound_deinit(void);

/* font (fontstash) */
void wgr_font_init(void);
void wgr_font_deinit(void);
void wgr_font_flush(void); /* upload the font atlas (outside a render pass) */
void wgr_font_end_frame(void); /* grow a glyph atlas that filled up (after the frame is submitted) */

/* text (fontstash; the built-in font is embedded) */
void wgr_text_init(void);
void wgr_text_deinit(void);
/* The font to use for `font`: the default font for 0 (wgr_text_set_default_font, else
 * the built-in font), and the built-in font for a font that isn't loaded. */
wgr_handle_t wgr_text_resolve_font(wgr_handle_t font);
/* A block of text laid out in `font` (0 = default) at `size`: lines break at
 * newlines and, when max_width > 0, between words that don't fit (a word longer
 * than that keeps a line to itself). Lines are one font line height apart. `length`
 * is in bytes; negative means up to the NUL. Sizes and positions are logical
 * pixels; glyphs are rasterized at the drawing target's pixel scale.
 * wgr_text_block_size gives the widest line and the total height; wgr_text_block_draw
 * draws it with (left, top) as the block's top-left corner, aligning each line
 * inside a box `box_width` wide (the block's own width when it's not wider). */
vec2_t wgr_text_block_size(wgr_handle_t font, const char *text, int length, float size, float max_width);
void wgr_text_block_draw(wgr_handle_t font, const char *text, int length, float left, float top, float size,
                        wgr_color_t color, float max_width, float box_width, wgr_text_align_t align_x);
/* The same line splitting, for callers that draw their own glyphs (text3d): the
 * font and size must already be selected in fontstash, and max_width is in those
 * units (0: no wrap). Fills [starts[i], ends[i]) and returns the line count, at
 * most max_lines. */
int wgr_text_split_lines(const char *text, int length, float max_width, const char **starts, const char **ends,
                        int max_lines);

/* text2d (retained text object, built on the text layer) */
void wgr_text2d_init(void);
void wgr_text2d_deinit(void);

/* text3d (TrueType text in the 3D world) */
void wgr_text3d_init(void);
void wgr_text3d_deinit(void);

/* debug overlay */
void wgr_debug_init(void);
void wgr_debug_deinit(void);
void wgr_debug_draw(void);

/* input */
void wgr_input_init(void);
void wgr_input_deinit(void);
struct sapp_event; /* fwd decl from sokol_app */
void wgr_input_handle_event(const struct sapp_event *ev);
/* Input edges are relative to the running callback (docs/PLAN-tick.md). The
 * runtime sets the context before each tick/frame callback and clears that
 * context's edges after it. */
typedef enum {
    WGR_INPUT_CONTEXT_FRAME = 0,
    WGR_INPUT_CONTEXT_TICK = 1,
} wgr_input_context_t;
void wgr_input_set_context(wgr_input_context_t context);
void wgr_input_end_tick(void);  /* clear tick edges (after each tick) */
void wgr_input_end_frame(void); /* clear frame edges (after the frame callback) */
wgr_input_context_t wgr_input_get_context(void);
/* The pointer (mouse, or the primary touch) with this frame's edges, whatever the
 * context: position in logical pixels, primary button held / pressed / released. */
void wgr_input_get_pointer_frame(float *x, float *y, bool *down, bool *pressed, bool *released);
void wgr_input_set_scene_pointer_captured(bool captured); /* wgr_scene.c's interaction capture */

#endif // WGR_INTERNAL_H
