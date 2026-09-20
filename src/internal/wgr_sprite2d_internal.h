#ifndef WGR_INTERNAL_SPRITE2D_H
#define WGR_INTERNAL_SPRITE2D_H

#include <stdbool.h>

void wgr_sprite2d_init(void);
void wgr_sprite2d_deinit(void);

/* Where a sprite sits on screen, resolved from its settings (sizes in logical
 * pixels, after defaults are applied). Pure data for the helpers below. */
typedef struct {
    float x, y;             /* pivot position */
    float width, height;    /* size before scale */
    float scale_x, scale_y;
    float pivot_x, pivot_y; /* 0..1 */
    float rotation;         /* radians, clockwise on screen */
} wgr_sprite2d_placement_t;

/* Screen positions of the corners in order top-left, top-right, bottom-right,
 * bottom-left (as seen in the texture), as x,y pairs into out[8]. */
void wgr_sprite2d_corners(const wgr_sprite2d_placement_t *placement, float out[8]);

/* One axis of a nine-slice: where the two inner edges sit, as fractions (0..1) of
 * the destination (out_dest) and of the source region (out_source). Borders wider
 * than the region share it; borders that don't fit the destination shrink to fill
 * it (so a stretched patch never flips). False when the axis isn't sliced. Pure;
 * exposed for tests. */
bool wgr_sprite2d_nine_slice_axis(float border_low, float border_high, float dest_size, float source_size,
                                 float out_dest[2], float out_source[2]);

/* Map a screen point into the sprite: u, v in 0..1 across it (u follows the
 * texture's x, v its y, so flipped sprites mirror u/v). Returns false when the
 * point is outside the sprite. Pure; exposed for tests. */
bool wgr_sprite2d_screen_to_unit(const wgr_sprite2d_placement_t *placement, float screen_x, float screen_y,
                                float *u, float *v);

#endif // WGR_INTERNAL_SPRITE2D_H
