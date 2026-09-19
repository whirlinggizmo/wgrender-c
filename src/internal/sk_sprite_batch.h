#ifndef SK_INTERNAL_SPRITE_BATCH_H
#define SK_INTERNAL_SPRITE_BATCH_H

#include <stdbool.h>
#include <stdint.h>

#include "sk_types.h"

/* Instanced sprite drawing (docs/PLAN-sprites.md): sprites are recorded as one small
 * instance each, in call order, and drawn as instanced quads; consecutive sprites
 * with the same texture, camera, clip and depth mode share one draw. Batches are
 * render commands, so they keep their place among sokol_gl layers, model draws and
 * passes. The GPU builds each quad: see src/shaders/sk_sprite.glsl. */

/* One sprite, as the shader reads it (per-instance vertex data). */
typedef struct {
    float position[3];
    float facing;       /* sk_sprite3d_facing_t: 0 camera, 1 upright about Y, 2+ own axes */
    float size[2];      /* world width and height, scale included */
    float pivot[2];     /* 0..1 across the quad, y down */
    float uv[4];        /* u0, v0 (top-left), u1, v1 */
    float right[3];     /* facing 2+: the quad's axes */
    float up[3];
    float alpha;        /* > 0 mask cutoff, < 0 opaque, 0 as is (blended / added) */
    uint8_t color[4];   /* tint, sRGB */
} sk_sprite_quad_t;

void sk_sprite_batch_init(void);
void sk_sprite_batch_deinit(void);

/* Record a 3D sprite for the current pass, seen by the active camera, drawn with its
 * alpha mode (the quad's `alpha` says how the shader treats alpha). blend_depth_write:
 * whether a blended sprite writes depth (direct draws do; a scene's sorted pass
 * doesn't). view and sampler: the texture's sokol ids (sk_texture_get_binding). */
void sk_sprite_batch_add_3d(const sk_sprite_quad_t *instance, uint32_t view, uint32_t sampler, sk_alpha_mode_t mode,
                            bool blend_depth_write);

/* Sprites added between these don't need their order (opaque, masked and additive
 * sprites in a scene): they're grouped by texture and mode, so 4 textures in any
 * order make 4 draws. end_unordered records them. */
void sk_sprite_batch_begin_unordered(void);
void sk_sprite_batch_end_unordered(void);

/* sk_render: upload the frame's instances (before any pass), draw one batch (inside
 * its pass), and start over (after the frame is submitted). */
void sk_sprite_batch_flush(void);
/* follows: the previous command drawn in this pass was also a sprite batch, so the
 * pipeline, uniforms and scissor it applied are still in place */
void sk_sprite_batch_draw(int batch, bool follows);
void sk_sprite_batch_end_frame(void);

/* Batches recorded this frame so far. For tests. */
int sk_sprite_batch_count(void);

#endif // SK_INTERNAL_SPRITE_BATCH_H
