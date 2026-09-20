#ifndef WGR_SHAPE3D_H
#define WGR_SHAPE3D_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "wgr_types.h"

/* Shapes in the world: right-handed, +y up, angles in radians. Screen-space
 * shapes are wgr_shape2d, the same 2D/3D split as sprite2d/sprite3d and
 * text2d/text3d.
 *
 * Immediate primitives draw between wgr_render_begin_mode_3d() and
 * wgr_render_end_mode_3d(), in call order. */
void wgr_shape3d_draw_line(float x0, float y0, float z0,
                          float x1, float y1, float z1, wgr_color_t color);
void wgr_shape3d_draw_cube(float cx, float cy, float cz,
                          float width, float height, float length, wgr_color_t color);
void wgr_shape3d_draw_cube_wires(float cx, float cy, float cz,
                                float width, float height, float length, wgr_color_t color);
void wgr_shape3d_draw_sphere(float cx, float cy, float cz, float radius, wgr_color_t color);
void wgr_shape3d_draw_grid(int slices, float spacing, wgr_color_t color);
/* A filled rectangle / circle outline in their local XY plane, centered at
 * (cx, cy, cz) and turned by euler rotation (radians). */
void wgr_shape3d_draw_rectangle(float cx, float cy, float cz, float width, float height,
                               float rx, float ry, float rz, wgr_color_t color);
void wgr_shape3d_draw_circle(float cx, float cy, float cz, float radius,
                            float rx, float ry, float rz, wgr_color_t color);

/* Retained 3D shapes — handle-based drawables that can be added to a scene.
 * A shape has a kind (cube, sphere, rectangle, circle, line, line strip) with
 * local geometry, a transform, a color, and visible / pickable / enabled flags.
 * Rectangles and circles lie in the local XY plane; picks hit rectangles and the
 * inside of circles; lines and strips have no area and aren't hit. Draw directly
 * with wgr_shape3d_draw() inside 3D mode, or add it to a scene via wgr_scene_add(). */
wgr_handle_t wgr_shape3d_create(void);
void wgr_shape3d_destroy(wgr_handle_t shape);
bool wgr_shape3d_set_cube(wgr_handle_t shape, float width, float height, float length);
bool wgr_shape3d_set_sphere(wgr_handle_t shape, float radius);
bool wgr_shape3d_set_rectangle(wgr_handle_t shape, float width, float height); /* filled */
bool wgr_shape3d_set_circle(wgr_handle_t shape, float radius);                 /* outline */
bool wgr_shape3d_set_line(wgr_handle_t shape, float x0, float y0, float z0, float x1, float y1, float z1);
/* A line strip is built point by point: set_line_strip empties it, add_point
 * appends (in local space). Rebuild it the same way to change it. */
bool wgr_shape3d_set_line_strip(wgr_handle_t shape);
bool wgr_shape3d_add_point(wgr_handle_t shape, float x, float y, float z);
int  wgr_shape3d_get_point_count(wgr_handle_t shape);

bool wgr_shape3d_set_transform(wgr_handle_t shape,
                              float position_x, float position_y, float position_z,
                              float rotation_x, float rotation_y, float rotation_z, /* radians */
                              float scale_x, float scale_y, float scale_z);
bool wgr_shape3d_set_color(wgr_handle_t shape, wgr_color_t color);
bool wgr_shape3d_set_visible(wgr_handle_t shape, bool visible);
bool wgr_shape3d_is_visible(wgr_handle_t shape);
bool wgr_shape3d_set_pickable(wgr_handle_t shape, bool pickable);
bool wgr_shape3d_is_pickable(wgr_handle_t shape);
/* Enabled (default): a hit reacts (hover, press, click in an interactive scene).
 * Disabled: still drawn and picked, and still blocks the pointer, but doesn't react. */
bool wgr_shape3d_set_enabled(wgr_handle_t shape, bool enabled);
bool wgr_shape3d_is_enabled(wgr_handle_t shape);
void wgr_shape3d_draw(wgr_handle_t shape);

#ifdef __cplusplus
}
#endif

#endif // WGR_SHAPE3D_H
