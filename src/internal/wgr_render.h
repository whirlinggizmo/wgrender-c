#ifndef WGR_INTERNAL_RENDER_H
#define WGR_INTERNAL_RENDER_H

#include <stdbool.h>

#include "wgr_types.h"
#include "sokol_gfx.h"

/* sokol_gfx resource pool sizes (sg_setup). sokol's defaults (128 buffers and
 * images) run out at ~60 textured materials or glTF primitives. */
#define WGR_GFX_BUFFER_POOL_SIZE 4096 /* two per glTF primitive */
#define WGR_GFX_IMAGE_POOL_SIZE 2048  /* textures, render targets, environments */
#define WGR_GFX_VIEW_POOL_SIZE 4096

/* Frame command list
 * ------------------
 * sokol_gl content (shapes, sprites, 2D, fontstash text) is recorded into
 * sokol_gl layers; models are queued by wgr_model. Nothing reaches the GPU until
 * wgr_render_end(), which replays both in the order they were submitted:
 *
 *   SGL layer 0, MODELS [0..3), SGL layer 1, MODELS [3..5), SGL layer 2, ...
 *
 * so draw order follows call order across sokol_gl and model draws. Submitting
 * models closes the current sokol_gl layer and opens a new one; empty layers
 * and adjacent model ranges are merged, so runs of the same kind stay batched. */

void wgr_render_init(void);
void wgr_render_deinit(void);

/* What wgr_render reaches through optional modules (internal/wgr_module.h): each module
 * sets its hooks in its init and clears them in its deinit; NULL while it isn't linked
 * or running. */
typedef struct {
    void (*draw_models)(int first, int count);     /* wgr_model: model items */
    void (*draw_sprites)(int batch, bool follows); /* wgr_sprite_batch: a sprite batch */
    bool (*texture_target)(wgr_handle_t texture, sg_attachments *attachments, int *width, int *height);
    void (*texture_drawing_into)(wgr_handle_t texture); /* wgr_texture: the target being drawn */
    /* wgr_effect (screen effects): where the screen's own pass draws when there are any
     * (false: straight to the swapchain), and the chain, which opens its own passes and
     * ends with the swapchain. */
    bool (*effects_begin)(sg_attachments *attachments);
    void (*effects_draw)(void);
    /* wgr_shadow: the casting light's depth map, drawn before anything is shaded */
    void (*shadows_draw)(void);
    /* wgr_sprite_batch: whether a lit sprite was recorded in this lighting environment
     * (they receive shadows, though they never cast), so a map nothing reads isn't
     * drawn. NULL when no sprite is in the program at all */
    bool (*sprites_lit_in)(int light_env);
} wgr_render_hooks_t;
extern wgr_render_hooks_t wgr_render_hooks;

/* Append model items [first, first + count) (indices into wgr_model's item
 * queue) to the command list, after everything recorded so far. */
void wgr_render_submit_models(int first, int count);

/* Append a custom draw to the command list: `draw(arg)` runs inside the render
 * pass, in call order (e.g. a scene background). */
typedef void (*wgr_render_callback_fn)(int arg);
void wgr_render_submit_callback(wgr_render_callback_fn draw, int arg);

/* Append a batch of instanced sprites (wgr_sprite_batch.c) to the command list; false
 * when the list is full. wgr_render_sprites_open: whether that batch's command is
 * still the last thing recorded, so a sprite can join it. */
bool wgr_render_submit_sprites(int batch);
bool wgr_render_sprites_open(int batch);

/* Switch the sokol_gl 3D pipeline between opaque (depth writes on) and
 * transparent (blended, depth writes off). Only valid inside 3D mode.
 * wgr_render_is_3d_transparent: which one is on (sprites follow it). */
void wgr_render_set_3d_transparent(bool transparent);
bool wgr_render_is_3d_transparent(void);

/* Changes whenever the pass, 3D mode or clip changes (and each frame): a cheap check
 * for whether state derived from them (sprite batches) is still current. */
unsigned wgr_render_state_revision(void);

/* Commands in the frame's list so far. For tests. */
int wgr_render_command_count(void);

/* The pass being recorded: 0 = the screen, >0 = a render target pass. */
int wgr_render_current_pass(void);

/* Size of what's being drawn into, in framebuffer pixels: the current render
 * target, else the screen. For aspect ratios. */
vec2_t wgr_render_target_size(void);

/* The current clip in logical pixels (the whole target when nothing is pushed);
 * false when nothing is pushed in this pass. For tests. */
bool wgr_render_get_clip(float *x, float *y, float *width, float *height);

/* Framebuffer pixels per logical pixel where drawing goes now: the screen's DPI
 * scale, or 1 inside a render target (its pixels are its own). */
float wgr_render_pixel_scale(void);

#endif // WGR_INTERNAL_RENDER_H
