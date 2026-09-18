#ifndef SK_SHAPE3D_H
#define SK_SHAPE3D_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "sk_types.h"

/* Shapes in the world: right-handed, +y up, angles in radians. Screen-space
 * shapes are sk_shape2d, the same 2D/3D split as sprite2d/sprite3d and
 * text2d/text3d.
 *
 * Immediate primitives draw between sk_render_begin_mode_3d() and
 * sk_render_end_mode_3d(), in call order. */
void sk_shape3d_draw_line(float x0, float y0, float z0,
                          float x1, float y1, float z1, sk_color_t color);
void sk_shape3d_draw_cube(float cx, float cy, float cz,
                          float width, float height, float length, sk_color_t color);
void sk_shape3d_draw_cube_wires(float cx, float cy, float cz,
                                float width, float height, float length, sk_color_t color);
void sk_shape3d_draw_sphere(float cx, float cy, float cz, float radius, sk_color_t color);
void sk_shape3d_draw_grid(int slices, float spacing, sk_color_t color);
/* A filled rectangle / circle outline in their local XY plane, centered at
 * (cx, cy, cz) and turned by euler rotation (radians). */
void sk_shape3d_draw_rectangle(float cx, float cy, float cz, float width, float height,
                               float rx, float ry, float rz, sk_color_t color);
void sk_shape3d_draw_circle(float cx, float cy, float cz, float radius,
                            float rx, float ry, float rz, sk_color_t color);

/* Retained 3D shapes — handle-based drawables that can be added to a scene.
 * A shape has a kind (cube, sphere, rectangle, circle, line, line strip) with
 * local geometry, a transform, a color, and visible / pickable / enabled flags.
 * Rectangles and circles lie in the local XY plane; picks hit rectangles and the
 * inside of circles; lines and strips have no area and aren't hit. Draw directly
 * with sk_shape3d_draw() inside 3D mode, or add it to a scene via sk_scene_add(). */
sk_handle_t sk_shape3d_create(void);
void sk_shape3d_destroy(sk_handle_t shape);
bool sk_shape3d_set_cube(sk_handle_t shape, float width, float height, float length);
bool sk_shape3d_set_sphere(sk_handle_t shape, float radius);
bool sk_shape3d_set_rectangle(sk_handle_t shape, float width, float height); /* filled */
bool sk_shape3d_set_circle(sk_handle_t shape, float radius);                 /* outline */
bool sk_shape3d_set_line(sk_handle_t shape, float x0, float y0, float z0, float x1, float y1, float z1);
/* A line strip is built point by point: set_line_strip empties it, add_point
 * appends (in local space). Rebuild it the same way to change it. */
bool sk_shape3d_set_line_strip(sk_handle_t shape);
bool sk_shape3d_add_point(sk_handle_t shape, float x, float y, float z);
int  sk_shape3d_get_point_count(sk_handle_t shape);

bool sk_shape3d_set_transform(sk_handle_t shape,
                              float position_x, float position_y, float position_z,
                              float rotation_x, float rotation_y, float rotation_z, /* radians */
                              float scale_x, float scale_y, float scale_z);
bool sk_shape3d_set_color(sk_handle_t shape, sk_color_t color);
bool sk_shape3d_set_visible(sk_handle_t shape, bool visible);
bool sk_shape3d_is_visible(sk_handle_t shape);
bool sk_shape3d_set_pickable(sk_handle_t shape, bool pickable);
bool sk_shape3d_is_pickable(sk_handle_t shape);
/* Enabled (default): a hit reacts (hover, press, click in an interactive scene).
 * Disabled: still drawn and picked, and still blocks the pointer, but doesn't react. */
bool sk_shape3d_set_enabled(sk_handle_t shape, bool enabled);
bool sk_shape3d_is_enabled(sk_handle_t shape);
void sk_shape3d_draw(sk_handle_t shape);

#ifdef __cplusplus
}
#endif

#endif // SK_SHAPE3D_H
