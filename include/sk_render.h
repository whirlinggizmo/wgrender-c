#ifndef SK_RENDER_H
#define SK_RENDER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "sk_types.h"

void sk_render_begin(void);
void sk_render_end(void);
void sk_render_clear_background(sk_handle_t color);
/* Screen space (logical pixels, top-left origin): what sk_render_begin already
 * sets up, so this only matters after 3D mode. */
void sk_render_begin_mode_2d(void);
void sk_render_end_mode_2d(void);
void sk_render_begin_mode_3d(void);
void sk_render_end_mode_3d(void);

/* Clip drawing to a screen rectangle (logical pixels, top-left origin; the
 * drawing target's pixels inside sk_render_begin_texture) until
 * sk_render_end_clip, which goes back to the whole target. A width or height of
 * 0 clips everything away. Not nestable: the last rectangle set wins, and
 * ending a clip restores the whole target, not an enclosing rectangle. Scenes
 * clip their own layers with sk_scene_set_clip. */
void sk_render_begin_clip(float x, float y, float width, float height);
void sk_render_end_clip(void);

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
