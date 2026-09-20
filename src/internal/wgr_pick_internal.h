#ifndef WGRI_INTERNAL_PICK_H
#define WGRI_INTERNAL_PICK_H

#include <stdbool.h>

#include "internal/wgr_camera3d_internal.h"
#include "internal/wgr_math_internal.h"
#include "wgr_types.h"

typedef struct {
    vec3_t origin;
    vec3_t dir; /* unit length */
} wgri_ray_t;

/* Raw intersection result in the space the test was run in (the ray's space).
 * `t` is the distance along `ray.dir`; with a unit `dir` it is a length in that
 * space. Use the resolvers below to turn this into a populated pick result. */
typedef struct {
    bool hit;
    float t;
    vec3_t point;  /* in the ray's space */
    vec3_t normal; /* in the ray's space, oriented against the ray */
    float u, v;    /* barycentric weights of v1/v2 (triangle tests only) */
} wgri_ray_hit_t;

wgri_ray_t wgri_pick_ray_from_screen(const wgri_camera3d_t *cam,
                                 float mouse_x, float mouse_y,
                                 float screen_w, float screen_h);

wgri_ray_t wgri_pick_ray_to_local(wgri_mat4_t model, wgri_ray_t world);

/* Geometry tests; results are reported in the ray's space. */
bool wgri_pick_ray_sphere(wgri_ray_t ray, vec3_t center, float radius, wgri_ray_hit_t *out);
bool wgri_pick_ray_aabb(wgri_ray_t ray, vec3_t bmin, vec3_t bmax, wgri_ray_hit_t *out);

/* Ray vs single triangle (Moller-Trumbore, double-sided). The reported normal
 * is oriented to face against the ray. */
bool wgri_pick_ray_triangle(wgri_ray_t ray, vec3_t v0, vec3_t v1, vec3_t v2,
                          wgri_ray_hit_t *out);

/* Populate a pick result from a hit computed in LOCAL space (the ray was
 * transformed into `model` space). Fills both local and world point/normal and
 * the world-space distance. */
void wgri_pick_result_from_local(const wgri_ray_hit_t *local_hit, wgri_ray_t world_ray,
                               wgri_mat4_t model, wgr_pick_result_t *out);

/* Populate a pick result from a hit computed directly in WORLD space. Derives
 * the local point/normal via the inverse of `model`. */
void wgri_pick_result_from_world(const wgri_ray_hit_t *world_hit, wgri_ray_t world_ray,
                               wgri_mat4_t model, wgr_pick_result_t *out);

void wgri_pick_world_aabb(vec3_t lmin, vec3_t lmax, wgri_mat4_t model,
                        vec3_t *wmin, vec3_t *wmax);
bool wgri_pick_ray_world_aabb(wgri_ray_t ray, vec3_t lmin, vec3_t lmax, wgri_mat4_t model,
                            float *t_out);

#endif // WGRI_INTERNAL_PICK_H
