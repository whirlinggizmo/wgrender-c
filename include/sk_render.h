#ifndef SK_RENDER_H
#define SK_RENDER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "sk_types.h"

void sk_render_begin(void);
void sk_render_end(void);
void sk_render_clear_background(sk_color_t color);
/* Screen space (logical pixels, top-left origin): what sk_render_begin already
 * sets up, so this only matters after 3D mode. */
void sk_render_begin_mode_2d(void);
void sk_render_end_mode_2d(void);
void sk_render_begin_mode_3d(void);
void sk_render_end_mode_3d(void);

/* Clip drawing to a rectangle (logical pixels, top-left origin; the texture's pixels
 * inside sk_render_begin_texture) until the matching pop. Clips nest: each push
 * intersects with the clip it's pushed inside, so a scroll area inside a panel stays
 * inside the panel, and a scene's layer clips (sk_scene_set_clip) intersect with a clip
 * pushed around sk_scene_draw. A width or height of 0 clips everything away. Each
 * render pass starts unclipped, and every push should be popped within the frame
 * (unmatched ones are dropped at sk_render_end, with a warning). Up to 32 deep. */
void sk_render_push_clip(float x, float y, float width, float height);
void sk_render_pop_clip(void);

/* Draw into a render target texture (sk_texture_create_target) instead of the
 * screen, until sk_render_end_texture. Call between sk_render_begin and
 * sk_render_end; everything works inside (clear, 2D, 3D mode, scenes, models,
 * sprites, text). 2D coordinates are the target's pixels; 3D uses the active
 * camera with the target's aspect ratio. Targets are drawn before the screen, in
 * the order begun; a target drawn more than once in a frame keeps the earlier
 * drawing. Not nestable. A target can't be used as a texture inside its own
 * pass (the default texture is used instead). */
bool sk_render_begin_texture(sk_handle_t texture);
void sk_render_end_texture(void);

#ifdef __cplusplus
}
#endif

#endif // SK_RENDER_H
