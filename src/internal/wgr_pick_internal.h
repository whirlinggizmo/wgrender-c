#ifndef WGR_INTERNAL_PICK_H
#define WGR_INTERNAL_PICK_H

#include <stdbool.h>

#include "internal/wgr_camera3d_internal.h"
#include "internal/wgr_math_internal.h"
#include "wgr_types.h"

typedef struct {
    vec3_t origin;
    vec3_t dir; /* unit length */
} wgr_ray_t;

/* Raw intersection result in the space the test was run in (the ray's space).
 * `t` is the distance along `ray.dir`; with a unit `dir` it is a length in that
 * space. Use the resolvers below to turn this into a populated pick result. */
typedef struct {
    bool hit;
    float t;
    vec3_t point;  /* in the ray's space */
    vec3_t normal; /* in the ray's space, oriented against the ray */
    float u, v;    /* barycentric weights of v1/v2 (triangle tests only) */
} wgr_ray_hit_t;

wgr_ray_t wgr_pick_ray_from_screen(const wgr_camera3d_t *cam,
                                 float mouse_x, float mouse_y,
                                 float screen_w, float screen_h);

wgr_ray_t wgr_pick_ray_to_local(wgr_mat4_t model, wgr_ray_t world);

/* Geometry tests; results are reported in the ray's space. */
bool wgr_pick_ray_sphere(wgr_ray_t ray, vec3_t center, float radius, wgr_ray_hit_t *out);
bool wgr_pick_ray_aabb(wgr_ray_t ray, vec3_t bmin, vec3_t bmax, wgr_ray_hit_t *out);

/* Ray vs single triangle (Moller-Trumbore, double-sided). The reported normal
 * is oriented to face against the ray. */
bool wgr_pick_ray_triangle(wgr_ray_t ray, vec3_t v0, vec3_t v1, vec3_t v2,
                          wgr_ray_hit_t *out);

/* Populate a pick result from a hit computed in LOCAL space (the ray was
 * transformed into `model` space). Fills both local and world point/normal and
 * the world-space distance. */
void wgr_pick_result_from_local(const wgr_ray_hit_t *local_hit, wgr_ray_t world_ray,
                               wgr_mat4_t model, wgr_pick_result_t *out);

/* Populate a pick result from a hit computed directly in WORLD space. Derives
 * the local point/normal via the inverse of `model`. */
void wgr_pick_result_from_world(const wgr_ray_hit_t *world_hit, wgr_ray_t world_ray,
                               wgr_mat4_t model, wgr_pick_result_t *out);

void wgr_pick_world_aabb(vec3_t lmin, vec3_t lmax, wgr_mat4_t model,
                        vec3_t *wmin, vec3_t *wmax);
bool wgr_pick_ray_world_aabb(wgr_ray_t ray, vec3_t lmin, vec3_t lmax, wgr_mat4_t model,
                            float *t_out);

#endif // WGR_INTERNAL_PICK_H
