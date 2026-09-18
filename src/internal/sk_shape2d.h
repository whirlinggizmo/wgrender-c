#ifndef SK_INTERNAL_SHAPE2D_H
#define SK_INTERNAL_SHAPE2D_H

/* The outline of the rectangle (x, y, width, height) with per-corner radii
 * (top-left, top-right, bottom-right, bottom-left), each clamped to half the shorter
 * side, clockwise from the top-left arc: SK_SHAPE2D_OUTLINE_POINTS points as x,y
 * pairs into xy. Shared by retained and immediate rounded rectangles and borders.
 * Pure; exposed for tests. */
#define SK_SHAPE2D_OUTLINE_POINTS (4 * (8 + 1))
int sk_shape2d_rounded_outline(float x, float y, float width, float height, const float radii[4], float *xy);

#endif // SK_INTERNAL_SHAPE2D_H
