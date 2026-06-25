#ifndef SK_INTERNAL_PICK_H
#define SK_INTERNAL_PICK_H

#include <stdbool.h>

#include "internal/sk_camera3d.h"
#include "internal/sk_math.h"
#include "sk_types.h"

typedef struct {
    vec3_t origin;
    vec3_t dir; /* unit length */
} sk_ray_t;

/* Raw intersection result in the space the test was run in (the ray's space).
 * `t` is the distance along `ray.dir`; with a unit `dir` it is a length in that
 * space. Use the resolvers below to turn this into a populated pick result. */
typedef struct {
    bool hit;
    float t;
    vec3_t point;  /* in the ray's space */
    vec3_t normal; /* in the ray's space, oriented against the ray */
    float u, v;    /* barycentric weights of v1/v2 (triangle tests only) */
} sk_ray_hit_t;

sk_ray_t sk_pick_ray_from_screen(const sk_camera3d_t *cam,
                                 float mouse_x, float mouse_y,
                                 float screen_w, float screen_h);

sk_ray_t sk_pick_ray_to_local(sk_mat4_t model, sk_ray_t world);

/* Geometry tests; results are reported in the ray's space. */
bool sk_pick_ray_sphere(sk_ray_t ray, vec3_t center, float radius, sk_ray_hit_t *out);
bool sk_pick_ray_aabb(sk_ray_t ray, vec3_t bmin, vec3_t bmax, sk_ray_hit_t *out);

/* Ray vs single triangle (Moller-Trumbore, double-sided). The reported normal
 * is oriented to face against the ray. */
bool sk_pick_ray_triangle(sk_ray_t ray, vec3_t v0, vec3_t v1, vec3_t v2,
                          sk_ray_hit_t *out);

/* Populate a pick result from a hit computed in LOCAL space (the ray was
 * transformed into `model` space). Fills both local and world point/normal and
 * the world-space distance. */
void sk_pick_result_from_local(const sk_ray_hit_t *local_hit, sk_ray_t world_ray,
                               sk_mat4_t model, sk_pick_result_t *out);

/* Populate a pick result from a hit computed directly in WORLD space. Derives
 * the local point/normal via the inverse of `model`. */
void sk_pick_result_from_world(const sk_ray_hit_t *world_hit, sk_ray_t world_ray,
                               sk_mat4_t model, sk_pick_result_t *out);

void sk_pick_world_aabb(vec3_t lmin, vec3_t lmax, sk_mat4_t model,
                        vec3_t *wmin, vec3_t *wmax);
bool sk_pick_ray_world_aabb(sk_ray_t ray, vec3_t lmin, vec3_t lmax, sk_mat4_t model,
                            float *t_out);

#endif // SK_INTERNAL_PICK_H
