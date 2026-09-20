#ifndef WGR_INTERNAL_SHAPE2D_H
#define WGR_INTERNAL_SHAPE2D_H

/* The outline of the rectangle (x, y, width, height) with per-corner radii
 * (top-left, top-right, bottom-right, bottom-left), each clamped to half the shorter
 * side, clockwise from the top-left arc: WGR_SHAPE2D_OUTLINE_POINTS points as x,y
 * pairs into xy. Shared by retained and immediate rounded rectangles and borders.
 * Pure; exposed for tests. */
#define WGR_SHAPE2D_OUTLINE_POINTS (4 * (8 + 1))
int wgr_shape2d_rounded_outline(float x, float y, float width, float height, const float radii[4], float *xy);

#endif // WGR_INTERNAL_SHAPE2D_H
