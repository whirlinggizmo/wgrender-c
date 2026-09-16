#ifndef SK_INTERNAL_SPRITE2D_H
#define SK_INTERNAL_SPRITE2D_H

#include <stdbool.h>

void sk_sprite2d_init(void);
void sk_sprite2d_deinit(void);

/* Where a sprite sits on screen, resolved from its settings (sizes in logical
 * pixels, after defaults are applied). Pure data for the helpers below. */
typedef struct {
    float x, y;             /* pivot position */
    float width, height;    /* size before scale */
    float scale_x, scale_y;
    float pivot_x, pivot_y; /* 0..1 */
    float rotation;         /* radians, clockwise on screen */
} sk_sprite2d_placement_t;

/* Screen positions of the corners in order top-left, top-right, bottom-right,
 * bottom-left (as seen in the texture), as x,y pairs into out[8]. */
void sk_sprite2d_corners(const sk_sprite2d_placement_t *placement, float out[8]);

/* Map a screen point into the sprite: u, v in 0..1 across it (u follows the
 * texture's x, v its y, so flipped sprites mirror u/v). Returns false when the
 * point is outside the sprite. Pure; exposed for tests. */
bool sk_sprite2d_screen_to_unit(const sk_sprite2d_placement_t *placement, float screen_x, float screen_y,
                                float *u, float *v);

#endif // SK_INTERNAL_SPRITE2D_H
