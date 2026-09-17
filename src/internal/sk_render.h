#ifndef SK_INTERNAL_RENDER_H
#define SK_INTERNAL_RENDER_H

#include <stdbool.h>

#include "sk_types.h"

/* sokol_gfx resource pool sizes (sg_setup). sokol's defaults (128 buffers and
 * images) run out at ~60 textured materials or glTF primitives. */
#define SK_GFX_BUFFER_POOL_SIZE 4096 /* two per glTF primitive */
#define SK_GFX_IMAGE_POOL_SIZE 2048  /* textures, render targets, environments */
#define SK_GFX_VIEW_POOL_SIZE 4096

/* Frame command list
 * ------------------
 * sokol_gl content (shapes, sprites, 2D, fontstash text) is recorded into
 * sokol_gl layers; models are queued by sk_model. Nothing reaches the GPU until
 * sk_render_end(), which replays both in the order they were submitted:
 *
 *   SGL layer 0, MODELS [0..3), SGL layer 1, MODELS [3..5), SGL layer 2, ...
 *
 * so draw order follows call order across sokol_gl and model draws. Submitting
 * models closes the current sokol_gl layer and opens a new one; empty layers
 * and adjacent model ranges are merged, so runs of the same kind stay batched. */

void sk_render_init(void);
void sk_render_deinit(void);

/* Append model items [first, first + count) (indices into sk_model's item
 * queue) to the command list, after everything recorded so far. */
void sk_render_submit_models(int first, int count);

/* Append a custom draw to the command list: `draw(arg)` runs inside the render
 * pass, in call order (e.g. a scene background). */
typedef void (*sk_render_callback_fn)(int arg);
void sk_render_submit_callback(sk_render_callback_fn draw, int arg);

/* Switch the sokol_gl 3D pipeline between opaque (depth writes on) and
 * transparent (blended, depth writes off). Only valid inside 3D mode. */
void sk_render_set_3d_transparent(bool transparent);

/* The pass being recorded: 0 = the screen, >0 = a render target pass. */
int sk_render_current_pass(void);

/* Size of what's being drawn into, in framebuffer pixels: the current render
 * target, else the screen. For aspect ratios. */
vec2_t sk_render_target_size(void);

#endif // SK_INTERNAL_RENDER_H
