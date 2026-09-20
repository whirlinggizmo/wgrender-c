#ifndef WGR_SHAPE2D_H
#define WGR_SHAPE2D_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "wgr_types.h"

/* 2D shapes in screen space: logical pixels, top-left origin, y down; angles in
 * radians, positive turning clockwise on screen. The world's shapes are
 * wgr_shape3d, the same 2D/3D split as sprite2d/sprite3d and text2d/text3d.
 *
 * Immediate primitives draw between wgr_render_begin() and wgr_render_end(), in
 * call order. */
void wgr_shape2d_draw_rectangle(float x, float y, float width, float height, wgr_color_t color);
void wgr_shape2d_draw_rectangle_lines(float x, float y, float width, float height, wgr_color_t color);
void wgr_shape2d_draw_line(float start_x, float start_y, float end_x, float end_y, wgr_color_t color);
void wgr_shape2d_draw_circle(float center_x, float center_y, float radius, wgr_color_t color);
void wgr_shape2d_draw_circle_lines(float center_x, float center_y, float radius, wgr_color_t color);
void wgr_shape2d_draw_triangle(float x0, float y0, float x1, float y1, float x2, float y2,
                              wgr_color_t color);

/* A filled rectangle from (x, y), with each corner rounded by its own radius
 * (clamped to half the shorter side; 0 is square). */
void wgr_shape2d_draw_rounded_rectangle(float x, float y, float width, float height,
                                       float r_top_left, float r_top_right,
                                       float r_bottom_right, float r_bottom_left, wgr_color_t color);

/* A border just inside the rectangle (x, y, width, height), each side its own width,
 * each outer corner its own radius, as in CSS. Inner corners are rounded by the outer
 * radius less the wider of the two sides meeting there; borders wider than the box
 * fill it. */
void wgr_shape2d_draw_border(float x, float y, float width, float height,
                            float left, float top, float right, float bottom,
                            float r_top_left, float r_top_right,
                            float r_bottom_right, float r_bottom_left, wgr_color_t color);

/* Retained 2D shapes — handle-based drawables with a kind, a transform, a color,
 * and visible / pickable / enabled flags. Add one to a scene (wgr_scene_add): 2D
 * members draw over all 3D, in layer then insertion order, and are picked first,
 * topmost first, by their exact area. Or draw it directly with wgr_shape2d_draw.
 *
 * - rectangle: from its origin (top-left) to (width, height), corners rounded by
 *   corner_radius (clamped to half the shorter side);
 * - circle: centered on its origin;
 * - line: from (x0, y0) to (x1, y1), thickness pixels wide (butt ends).
 * Rectangles and circles are filled unless an outline is set. */
wgr_handle_t wgr_shape2d_create(void);
void        wgr_shape2d_destroy(wgr_handle_t shape);
bool wgr_shape2d_set_rectangle(wgr_handle_t shape, float width, float height, float corner_radius);
bool wgr_shape2d_set_circle(wgr_handle_t shape, float radius);
bool wgr_shape2d_set_line(wgr_handle_t shape, float x0, float y0, float x1, float y1, float thickness);

/* Position, rotation (radians, around the pivot) and scale. */
bool wgr_shape2d_set_transform(wgr_handle_t shape, float x, float y, float rotation,
                              float scale_x, float scale_y);

/* The point the position refers to and rotation and scale turn around, as a
 * fraction of the shape's bounds: (0, 0) their top-left, (1, 1) their
 * bottom-right, (0.5, 0.5) the middle. Values outside 0..1 are allowed.
 * Default: the shape's own origin — a rectangle's top-left corner, a circle's
 * center — so nothing moves until you set one. Lines have explicit endpoints, so
 * they ignore the pivot. */
bool wgr_shape2d_set_pivot(wgr_handle_t shape, float x, float y);

bool wgr_shape2d_set_outline(wgr_handle_t shape, float thickness); /* rectangles and circles; 0 = filled (default) */
bool wgr_shape2d_set_color(wgr_handle_t shape, wgr_color_t color);
bool wgr_shape2d_set_visible(wgr_handle_t shape, bool visible);
bool wgr_shape2d_is_visible(wgr_handle_t shape);
bool wgr_shape2d_set_pickable(wgr_handle_t shape, bool pickable);
bool wgr_shape2d_is_pickable(wgr_handle_t shape);
/* Enabled (default): a hit reacts (hover, press, click in an interactive scene).
 * Disabled: still drawn and picked, and still blocks the pointer, but doesn't react. */
bool wgr_shape2d_set_enabled(wgr_handle_t shape, bool enabled);
bool wgr_shape2d_is_enabled(wgr_handle_t shape);
void wgr_shape2d_draw(wgr_handle_t shape);

#ifdef __cplusplus
}
#endif

#endif // WGR_SHAPE2D_H
