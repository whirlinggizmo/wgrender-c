#ifndef SK_SHAPE2D_H
#define SK_SHAPE2D_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "sk_types.h"

/* 2D shapes in screen space: logical pixels, top-left origin, y down; angles in
 * radians, positive turning clockwise on screen. The world's shapes are
 * sk_shape3d, the same 2D/3D split as sprite2d/sprite3d and text2d/text3d.
 *
 * Immediate primitives draw between sk_render_begin() and sk_render_end(), in
 * call order. */
void sk_shape2d_draw_rectangle(int x, int y, int width, int height, sk_handle_t color);
void sk_shape2d_draw_rectangle_lines(int x, int y, int width, int height, sk_handle_t color);
void sk_shape2d_draw_line(int start_x, int start_y, int end_x, int end_y, sk_handle_t color);
void sk_shape2d_draw_circle(int center_x, int center_y, float radius, sk_handle_t color);
void sk_shape2d_draw_circle_lines(int center_x, int center_y, float radius, sk_handle_t color);
void sk_shape2d_draw_triangle(float x0, float y0, float x1, float y1, float x2, float y2,
                              sk_handle_t color);

/* Retained 2D shapes — handle-based drawables with a kind, a transform, a color,
 * and visible / pickable / enabled flags. Add one to a scene (sk_scene_add): 2D
 * members draw over all 3D, in layer then insertion order, and are picked first,
 * topmost first, by their exact area. Or draw it directly with sk_shape2d_draw.
 *
 * - rectangle: from its origin (top-left) to (width, height), corners rounded by
 *   corner_radius (clamped to half the shorter side);
 * - circle: centered on its origin;
 * - line: from (x0, y0) to (x1, y1), thickness pixels wide (butt ends).
 * Rectangles and circles are filled unless an outline is set. */
sk_handle_t sk_shape2d_create(void);
void        sk_shape2d_destroy(sk_handle_t shape);
bool sk_shape2d_set_rectangle(sk_handle_t shape, float width, float height, float corner_radius);
bool sk_shape2d_set_circle(sk_handle_t shape, float radius);
bool sk_shape2d_set_line(sk_handle_t shape, float x0, float y0, float x1, float y1, float thickness);

/* Position, rotation (radians, around the pivot) and scale. */
bool sk_shape2d_set_transform(sk_handle_t shape, float x, float y, float rotation,
                              float scale_x, float scale_y);

/* The point the position refers to and rotation and scale turn around, as a
 * fraction of the shape's bounds: (0, 0) their top-left, (1, 1) their
 * bottom-right, (0.5, 0.5) the middle. Values outside 0..1 are allowed.
 * Default: the shape's own origin — a rectangle's top-left corner, a circle's
 * center — so nothing moves until you set one. Lines have explicit endpoints, so
 * they ignore the pivot. */
bool sk_shape2d_set_pivot(sk_handle_t shape, float x, float y);

bool sk_shape2d_set_outline(sk_handle_t shape, float thickness); /* rectangles and circles; 0 = filled (default) */
bool sk_shape2d_set_color(sk_handle_t shape, sk_handle_t color);
bool sk_shape2d_set_visible(sk_handle_t shape, bool visible);
bool sk_shape2d_is_visible(sk_handle_t shape);
bool sk_shape2d_set_pickable(sk_handle_t shape, bool pickable);
bool sk_shape2d_is_pickable(sk_handle_t shape);
/* Enabled (default): a hit reacts (hover, press, click in an interactive scene).
 * Disabled: still drawn and picked, and still blocks the pointer, but doesn't react. */
bool sk_shape2d_set_enabled(sk_handle_t shape, bool enabled);
bool sk_shape2d_is_enabled(sk_handle_t shape);
void sk_shape2d_draw(sk_handle_t shape);

#ifdef __cplusplus
}
#endif

#endif // SK_SHAPE2D_H
