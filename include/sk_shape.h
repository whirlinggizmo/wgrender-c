#ifndef SK_SHAPE_H
#define SK_SHAPE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "sk_types.h"

/* Immediate-mode 2D primitives (recorded via sokol_gl between
 * sk_render_begin() and sk_render_end()). 3D shapes and retained shape
 * handles from librl are not part of this first milestone. */

void sk_shape_draw_rectangle(int x, int y, int width, int height, sk_handle_t color);
void sk_shape_draw_rectangle_lines(int x, int y, int width, int height, sk_handle_t color);
void sk_shape_draw_line(int start_x, int start_y, int end_x, int end_y, sk_handle_t color);
void sk_shape_draw_circle(int center_x, int center_y, float radius, sk_handle_t color);
void sk_shape_draw_circle_lines(int center_x, int center_y, float radius, sk_handle_t color);
void sk_shape_draw_triangle(float x0, float y0, float x1, float y1, float x2, float y2,
                            sk_handle_t color);

/* Immediate-mode 3D primitives. Call between sk_render_begin_mode_3d() and
 * sk_render_end_mode_3d(). */
void sk_shape_draw_line_3d(float x0, float y0, float z0,
                           float x1, float y1, float z1, sk_handle_t color);
void sk_shape_draw_cube(float cx, float cy, float cz,
                        float width, float height, float length, sk_handle_t color);
void sk_shape_draw_cube_wires(float cx, float cy, float cz,
                              float width, float height, float length, sk_handle_t color);
void sk_shape_draw_sphere(float cx, float cy, float cz, float radius, sk_handle_t color);
void sk_shape_draw_grid(int slices, float spacing, sk_handle_t color);
/* A filled rectangle / circle outline in their local XY plane, centered at
 * (cx, cy, cz) and turned by euler rotation (radians). */
void sk_shape_draw_rectangle_3d(float cx, float cy, float cz, float width, float height,
                                float rx, float ry, float rz, sk_handle_t color);
void sk_shape_draw_circle_3d(float cx, float cy, float cz, float radius,
                             float rx, float ry, float rz, sk_handle_t color);

/* Retained 3D shapes — handle-based drawables that can be added to a scene.
 * A shape has a kind (cube, sphere, rectangle, circle, line, line strip) with
 * local geometry, a transform, a color, and visibility and pickable flags.
 * Rectangles and circles lie in the local XY plane; picks hit rectangles and the
 * inside of circles; lines and strips have no area and aren't hit. Draw directly with sk_shape_draw() inside 3D mode, or add it to a scene
 * via sk_scene_add(). */
sk_handle_t sk_shape_create(void);
void sk_shape_destroy(sk_handle_t shape);
bool sk_shape_set_cube(sk_handle_t shape, float width, float height, float length);
bool sk_shape_set_sphere(sk_handle_t shape, float radius);
bool sk_shape_set_rectangle(sk_handle_t shape, float width, float height); /* filled */
bool sk_shape_set_circle(sk_handle_t shape, float radius);                 /* outline */
bool sk_shape_set_line(sk_handle_t shape, float x0, float y0, float z0, float x1, float y1, float z1);
/* A line strip is built point by point: set_line_strip empties it, add_point
 * appends (in local space). Rebuild it the same way to change it. */
bool sk_shape_set_line_strip(sk_handle_t shape);
bool sk_shape_add_point(sk_handle_t shape, float x, float y, float z);
int  sk_shape_get_point_count(sk_handle_t shape);
/* 2D shapes (screen space, logical pixels): drawn over 3D as scene 2D members, or with
 * sk_shape_draw in 2D mode, and picked by their exact area (pointer interaction).
 * - rectangle: from its origin (top-left) to (width, height), corners rounded by
 *   corner_radius (clamped to half the shorter side);
 * - circle: centered on its origin;
 * - line: from (x0, y0) to (x1, y1), thickness pixels wide (butt ends).
 * Placed with sk_shape_set_transform_2d: position, rotation (radians, around the
 * origin), scale. Rectangles and circles are filled unless an outline is set. */
bool sk_shape_set_rectangle_2d(sk_handle_t shape, float width, float height, float corner_radius);
bool sk_shape_set_circle_2d(sk_handle_t shape, float radius);
bool sk_shape_set_line_2d(sk_handle_t shape, float x0, float y0, float x1, float y1, float thickness);
bool sk_shape_set_transform_2d(sk_handle_t shape, float x, float y, float rotation, float scale_x, float scale_y);
bool sk_shape_set_outline(sk_handle_t shape, float thickness); /* 2D rectangles and circles; 0 = filled (default) */

bool sk_shape_set_transform(sk_handle_t shape,
                            float position_x, float position_y, float position_z,
                            float rotation_x, float rotation_y, float rotation_z, /* radians */
                            float scale_x, float scale_y, float scale_z);
bool sk_shape_set_color(sk_handle_t shape, sk_handle_t color);
bool sk_shape_set_visible(sk_handle_t shape, bool visible);
bool sk_shape_is_visible(sk_handle_t shape);
bool sk_shape_set_pickable(sk_handle_t shape, bool pickable);
bool sk_shape_is_pickable(sk_handle_t shape);
/* Enabled (default): a hit reacts (hover, press, click in an interactive scene).
 * Disabled: still drawn and picked, and still blocks the pointer, but doesn't react. */
bool sk_shape_set_enabled(sk_handle_t shape, bool enabled);
bool sk_shape_is_enabled(sk_handle_t shape);
void sk_shape_draw(sk_handle_t shape);

#ifdef __cplusplus
}
#endif

#endif // SK_SHAPE_H
