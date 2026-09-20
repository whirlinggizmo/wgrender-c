#ifndef SK_INTERNAL_SCENE_H
#define SK_INTERNAL_SCENE_H

#include <stdbool.h>

#include "internal/sk_camera3d.h"
#include "internal/sk_light.h"
#include "sk_handle.h"
#include "sk_math.h"
#include "sk_types.h"

void sk_scene_init(void);
void sk_scene_deinit(void);

/* Pointer interaction (sk_scene_set_interactive), driven by the runtime: update
 * before the frame's ticks, clear edges after each tick and after the frame. */
void sk_scene_update_interaction(void);
void sk_scene_end_tick_interaction(void);
void sk_scene_end_frame_interaction(void);

/* Render passes
 * -------------
 * sk_scene_draw() draws each layer in two passes: an opaque pass (depth writes
 * on), then a transparent pass sorted back to front across every drawable kind
 * (depth writes off). Each drawable module registers how it takes part:
 *
 *   draw_opaque          draw the handle's opaque parts (may be NULL)
 *   collect_transparent  report the handle's transparent parts, one item each,
 *                        with their view depth; returns the number written
 *                        (may be NULL)
 *   draw_transparent     draw one transparent part reported by collect
 *
 * A part is drawable-defined (e.g. a model primitive index; 0 for a sprite). */
typedef struct {
    sk_handle_t handle;
    int part;
    float depth; /* distance along the camera's view direction; larger = farther */
    int order;   /* set by the scene: stable tie-break for equal depths */
} sk_transparent_item_t;

typedef void (*sk_drawable_draw_opaque_fn)(sk_handle_t handle);
typedef int (*sk_drawable_collect_transparent_fn)(sk_handle_t handle, const sk_camera3d_t *cam,
                                                  sk_transparent_item_t *out, int max_items);
typedef void (*sk_drawable_draw_transparent_fn)(sk_handle_t handle, int part);

/* An object is being destroyed: take it out of every scene (members, hover and press
 * state, the active camera). Every object's destroy calls this. */
void sk_scene_forget(sk_handle_t object);

void sk_scene_register_passes(sk_handle_kind_t kind,
                              sk_drawable_draw_opaque_fn draw_opaque,
                              sk_drawable_collect_transparent_fn collect_transparent,
                              sk_drawable_draw_transparent_fn draw_transparent);
/* Additive parts: drawn after a layer's blended parts, unsorted (the order of added
 * light doesn't matter). */
void sk_scene_register_additive(sk_handle_kind_t kind, sk_drawable_draw_opaque_fn draw_additive);

/* 2D drawables (screen space). A scene draws them after all 3D layers, in layer
 * then member order, and picks them before 3D, topmost first. `pick` returns true
 * and fills `out` when (screen_x, screen_y), in logical pixels, hits the drawable. */
typedef void (*sk_drawable_draw_2d_fn)(sk_handle_t handle);
typedef bool (*sk_drawable_pick_2d_fn)(sk_handle_t handle, float screen_x, float screen_y,
                                       sk_pick_result_t *out);
void sk_scene_register_2d(sk_handle_kind_t kind, sk_drawable_draw_2d_fn draw, sk_drawable_pick_2d_fn pick);
/* For kinds with both 2D and 3D objects (shapes): whether this object is 2D. Kinds
 * that register 2D functions without it are always 2D. */

/* Distance of a world-space point along the camera's view direction. */
float sk_scene_view_depth(const sk_camera3d_t *cam, vec3_t world_point);

/* Sort transparent items back to front (farthest first), keeping `order` for
 * equal depths. Pure; exposed for tests. */
void sk_scene_sort_transparent(sk_transparent_item_t *items, int count);

/* Bounds for picking: a drawable reports its local-space AABB plus the model
 * matrix placing it in the world. Returns false if the handle has no bounds
 * (e.g. not yet loaded). */
typedef bool (*sk_drawable_bounds_fn)(sk_handle_t handle,
                                      vec3_t *local_min, vec3_t *local_max,
                                      sk_mat4_t *model);

void sk_scene_register_bounds(sk_handle_kind_t kind, sk_drawable_bounds_fn bounds);

/* Bounds for culling, when a kind has a cheaper answer than its picking bounds: a
 * model's picking bounds re-skin an animated mesh to be exact, which is far too much
 * work to do for every member of every frame. A kind that registers none is culled
 * with its picking bounds. */
void sk_scene_register_cull_bounds(sk_handle_kind_t kind, sk_drawable_bounds_fn bounds);
bool sk_drawable_bounds(sk_handle_t handle, vec3_t *local_min, vec3_t *local_max,
                        sk_mat4_t *model);

/* Narrow-phase pick: test `origin`+`dir` (world-space unit ray) against a drawable.
 * Returns false when the handle is not pickable or has no geometry. When true,
 * `out` holds the nearest hit along the ray in world space (may be .hit=false). */
typedef bool (*sk_drawable_pick_fn)(sk_handle_t handle, vec3_t origin, vec3_t dir,
                                    sk_pick_result_t *out);

void sk_scene_register_pick(sk_handle_kind_t kind, sk_drawable_pick_fn pick);

/* Whether a drawable's hits react in an interactive scene (sk_<kind>_is_enabled). A
 * kind that doesn't register one is always enabled. */
typedef bool (*sk_drawable_enabled_fn)(sk_handle_t handle);
void sk_scene_register_enabled(sk_handle_kind_t kind, sk_drawable_enabled_fn enabled);

/* Whether a drawable of this kind throws shadows, so culling keeps the ones that cast
 * into view from off screen. A kind that registers nothing never casts (sprites, shapes
 * and text don't). */
typedef bool (*sk_drawable_casts_shadow_fn)(sk_handle_t handle);
void sk_scene_register_casts_shadow(sk_handle_kind_t kind, sk_drawable_casts_shadow_fn casts);
bool sk_drawable_pick(sk_handle_t handle, vec3_t origin, vec3_t dir, sk_pick_result_t *out);

/* What scenes reach through optional modules (internal/sk_module.h): set by each
 * module's init, cleared by its deinit; NULL while it isn't linked or running. */
typedef struct {
    void (*environment_retain)(sk_handle_t environment); /* sk_environment */
    void (*environment_release)(sk_handle_t environment);
    void (*environment_background)(sk_handle_t environment, float blur, float intensity, float rotation,
                                   int tonemap, float exposure);
    bool (*scene_light)(sk_handle_t light, sk_scene_light_t *out); /* sk_light */
    int (*light_env_push)(const sk_light_env_t *env);
    void (*light_env_set_current)(int index);
    const sk_light_env_t *(*light_env_get)(int index); /* culling asks it which lights cast */
    void (*sprites_begin_unordered)(void); /* sk_sprite_batch */
    void (*sprites_end_unordered)(void);
    void (*models_begin_unordered)(void); /* sk_model: what it may reorder to batch */
    void (*models_end_unordered)(void);
} sk_scene_hooks_t;
extern sk_scene_hooks_t sk_scene_hooks;

#endif // SK_INTERNAL_SCENE_H
