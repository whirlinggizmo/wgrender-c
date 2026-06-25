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

/* Retained 3D shapes — handle-based drawables that can be added to a scene.
 * A shape has a kind (cube/sphere), a transform, a color, and a visibility
 * flag. Draw directly with sk_shape_draw() inside 3D mode, or add it to a scene
 * via sk_scene_add(). */
sk_handle_t sk_shape_create(void);
void sk_shape_destroy(sk_handle_t shape);
bool sk_shape_set_cube(sk_handle_t shape, float width, float height, float length);
bool sk_shape_set_sphere(sk_handle_t shape, float radius);
bool sk_shape_set_transform(sk_handle_t shape,
                            float position_x, float position_y, float position_z,
                            float rotation_x, float rotation_y, float rotation_z, /* radians */
                            float scale_x, float scale_y, float scale_z);
bool sk_shape_set_color(sk_handle_t shape, sk_handle_t color);
bool sk_shape_set_visible(sk_handle_t shape, bool visible);
bool sk_shape_is_visible(sk_handle_t shape);
bool sk_shape_set_pickable(sk_handle_t shape, bool pickable);
bool sk_shape_is_pickable(sk_handle_t shape);
void sk_shape_draw(sk_handle_t shape);

#ifdef __cplusplus
}
#endif

#endif // SK_SHAPE_H
