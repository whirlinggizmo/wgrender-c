#ifndef SK_INTERNAL_SCENE_H
#define SK_INTERNAL_SCENE_H

#include <stdbool.h>

#include "sk_handle.h"
#include "sk_math.h"
#include "sk_types.h"

void sk_scene_init(void);
void sk_scene_deinit(void);

/* Drawable dispatch: each drawable module registers a draw function for its
 * handle kind at init time. The scene (and direct draws) dispatch through this
 * registry, so the scene need not know about concrete drawable types. */
typedef void (*sk_drawable_draw_fn)(sk_handle_t handle);

void sk_scene_register_drawable(sk_handle_kind_t kind, sk_drawable_draw_fn draw);
void sk_drawable_draw(sk_handle_t handle);

/* Bounds for picking: a drawable reports its local-space AABB plus the model
 * matrix placing it in the world. Returns false if the handle has no bounds
 * (e.g. not yet loaded). */
typedef bool (*sk_drawable_bounds_fn)(sk_handle_t handle,
                                      vec3_t *local_min, vec3_t *local_max,
                                      sk_mat4_t *model);

void sk_scene_register_bounds(sk_handle_kind_t kind, sk_drawable_bounds_fn bounds);
bool sk_drawable_bounds(sk_handle_t handle, vec3_t *local_min, vec3_t *local_max,
                        sk_mat4_t *model);

/* Narrow-phase pick: test `origin`+`dir` (world-space unit ray) against a drawable.
 * Returns false when the handle is not pickable or has no geometry. When true,
 * `out` holds the nearest hit along the ray in world space (may be .hit=false). */
typedef bool (*sk_drawable_pick_fn)(sk_handle_t handle, vec3_t origin, vec3_t dir,
                                    sk_pick_result_t *out);

void sk_scene_register_pick(sk_handle_kind_t kind, sk_drawable_pick_fn pick);
bool sk_drawable_pick(sk_handle_t handle, vec3_t origin, vec3_t dir, sk_pick_result_t *out);

#endif // SK_INTERNAL_SCENE_H
