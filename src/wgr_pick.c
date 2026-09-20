#include "internal/wgr_pick.h"

#include <math.h>
#include <stddef.h>

static inline vec3_t mul_direction(wgr_mat4_t m, vec3_t d)
{
    return (vec3_t){
        m.m[0] * d.x + m.m[4] * d.y + m.m[8] * d.z,
        m.m[1] * d.x + m.m[5] * d.y + m.m[9] * d.z,
        m.m[2] * d.x + m.m[6] * d.y + m.m[10] * d.z,
    };
}

static inline float v3_dot(vec3_t a, vec3_t b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static inline vec3_t v3_add(vec3_t a, vec3_t b)
{
    return (vec3_t){a.x + b.x, a.y + b.y, a.z + b.z};
}

static inline vec3_t v3_scale(vec3_t a, float s)
{
    return (vec3_t){a.x * s, a.y * s, a.z * s};
}

wgr_ray_t wgr_pick_ray_from_screen(const wgr_camera3d_t *cam,
                                 float mouse_x, float mouse_y,
                                 float screen_w, float screen_h)
{
    wgr_ray_t ray = {0};
    wgr_mat4_t view, proj, inv_vp;
    vec3_t near_p, far_p;
    float aspect, ndc_x, ndc_y;

    if (cam == NULL || screen_w <= 0.0f || screen_h <= 0.0f) {
        return ray;
    }

    aspect = screen_h > 0.0f ? screen_w / screen_h : 1.0f;
    view = wgr_camera3d_view(cam);
    proj = wgr_camera3d_projection(cam, aspect);
    inv_vp = wgr_mat4_inverse(wgr_mat4_mul(proj, view));

    ndc_x = 2.0f * mouse_x / screen_w - 1.0f;
    ndc_y = 1.0f - 2.0f * mouse_y / screen_h;
    near_p = wgr_mat4_mul_point(inv_vp, (vec3_t){ndc_x, ndc_y, -1.0f});
    far_p = wgr_mat4_mul_point(inv_vp, (vec3_t){ndc_x, ndc_y, 1.0f});
    ray.origin = near_p;
    ray.dir = wgr_v3_norm(wgr_v3_sub(far_p, near_p));
    return ray;
}

wgr_ray_t wgr_pick_ray_to_local(wgr_mat4_t model, wgr_ray_t world)
{
    wgr_mat4_t inv = wgr_mat4_inverse(model);
    wgr_ray_t local;
    float len;

    local.origin = wgr_mat4_mul_point(inv, world.origin);
    local.dir = mul_direction(inv, world.dir);
    len = sqrtf(local.dir.x * local.dir.x + local.dir.y * local.dir.y + local.dir.z * local.dir.z);
    if (len > 1e-6f) {
        local.dir.x /= len;
        local.dir.y /= len;
        local.dir.z /= len;
    } else {
        local.dir = (vec3_t){0, 0, 0};
    }
    return local;
}

static inline float v3_dist(vec3_t a, vec3_t b)
{
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

bool wgr_pick_ray_aabb(wgr_ray_t ray, vec3_t bmin, vec3_t bmax, wgr_ray_hit_t *out)
{
    float tmin = -1e30f, tmax = 1e30f;
    float t_entry = 0.0f;
    int entry_axis = 0;
    int entry_sign = 0;
    const float od[3] = {ray.origin.x, ray.origin.y, ray.origin.z};
    const float dd[3] = {ray.dir.x, ray.dir.y, ray.dir.z};
    const float lo[3] = {bmin.x, bmin.y, bmin.z};
    const float hi[3] = {bmax.x, bmax.y, bmax.z};

    if (out == NULL) {
        return false;
    }

    for (int i = 0; i < 3; i++) {
        if (dd[i] > -1e-8f && dd[i] < 1e-8f) {
            if (od[i] < lo[i] || od[i] > hi[i]) {
                return false;
            }
        } else {
            float inv = 1.0f / dd[i];
            float t1 = (lo[i] - od[i]) * inv;
            float t2 = (hi[i] - od[i]) * inv;
            /* a ray moving in +axis enters through the low face (normal -1), in
             * -axis through the high face (+1). The swap below happens exactly
             * when the ray moves in -axis, so it must not flip the sign again. */
            int sign = dd[i] > 0.0f ? -1 : 1;
            if (t1 > t2) {
                float tmp = t1;
                t1 = t2;
                t2 = tmp;
            }
            if (t1 > tmin) {
                tmin = t1;
                entry_axis = i;
                entry_sign = sign;
            }
            if (t2 < tmax) {
                tmax = t2;
            }
            if (tmin > tmax) {
                return false;
            }
        }
    }

    if (tmax < 0.0f) {
        return false;
    }

    t_entry = tmin >= 0.0f ? tmin : tmax;
    out->hit = true;
    out->t = t_entry;
    out->point = v3_add(ray.origin, v3_scale(ray.dir, t_entry));
    out->normal = (vec3_t){0, 0, 0};
    if (entry_axis == 0) {
        out->normal.x = (float)entry_sign;
    } else if (entry_axis == 1) {
        out->normal.y = (float)entry_sign;
    } else {
        out->normal.z = (float)entry_sign;
    }
    return true;
}

bool wgr_pick_ray_sphere(wgr_ray_t ray, vec3_t center, float radius, wgr_ray_hit_t *out)
{
    vec3_t oc = wgr_v3_sub(ray.origin, center);
    float b = v3_dot(oc, ray.dir);
    float c = v3_dot(oc, oc) - radius * radius;
    float disc = b * b - c;
    float t;

    if (out == NULL || radius <= 0.0f) {
        return false;
    }

    if (disc < 0.0f) {
        return false;
    }

    disc = sqrtf(disc);
    t = -b - disc;
    if (t < 0.0f) {
        t = -b + disc;
    }
    if (t < 0.0f) {
        return false;
    }

    out->hit = true;
    out->t = t;
    out->point = v3_add(ray.origin, v3_scale(ray.dir, t));
    out->normal = wgr_v3_norm(wgr_v3_sub(out->point, center));
    if (v3_dot(out->normal, ray.dir) > 0.0f) {
        /* ray started inside: the exit point's outward normal faces along the
         * ray; flip it to face against the ray like every other hit */
        out->normal = v3_scale(out->normal, -1.0f);
    }
    return true;
}

bool wgr_pick_ray_triangle(wgr_ray_t ray, vec3_t v0, vec3_t v1, vec3_t v2, wgr_ray_hit_t *out)
{
    const float eps = 1e-7f;
    vec3_t e1 = wgr_v3_sub(v1, v0);
    vec3_t e2 = wgr_v3_sub(v2, v0);
    vec3_t p = wgr_v3_cross(ray.dir, e2);
    float det = v3_dot(e1, p);
    float inv, u, v, t;
    vec3_t tvec, q, n;

    if (out == NULL) {
        return false;
    }
    if (det > -eps && det < eps) {
        return false; /* ray parallel to triangle */
    }

    inv = 1.0f / det;
    tvec = wgr_v3_sub(ray.origin, v0);
    u = v3_dot(tvec, p) * inv;
    if (u < 0.0f || u > 1.0f) {
        return false;
    }
    q = wgr_v3_cross(tvec, e1);
    v = v3_dot(ray.dir, q) * inv;
    if (v < 0.0f || u + v > 1.0f) {
        return false;
    }
    t = v3_dot(e2, q) * inv;
    if (t < 0.0f) {
        return false;
    }

    n = wgr_v3_norm(wgr_v3_cross(e1, e2));
    if (v3_dot(n, ray.dir) > 0.0f) {
        n = (vec3_t){-n.x, -n.y, -n.z};
    }
    out->hit = true;
    out->t = t;
    out->point = v3_add(ray.origin, v3_scale(ray.dir, t));
    out->normal = n;
    out->u = u;
    out->v = v;
    return true;
}

void wgr_pick_result_from_local(const wgr_ray_hit_t *local_hit, wgr_ray_t world_ray,
                               wgr_mat4_t model, wgr_pick_result_t *out)
{
    if (out == NULL) {
        return;
    }
    if (local_hit == NULL || !local_hit->hit) {
        *out = (wgr_pick_result_t){0};
        return;
    }

    out->hit = true;
    out->point_local = local_hit->point;
    out->normal_local = wgr_v3_norm(local_hit->normal);
    out->point_world = wgr_mat4_mul_point(model, local_hit->point);
    out->normal_world = wgr_v3_norm(mul_direction(model, local_hit->normal));
    out->distance = v3_dist(out->point_world, world_ray.origin);
}

void wgr_pick_result_from_world(const wgr_ray_hit_t *world_hit, wgr_ray_t world_ray,
                               wgr_mat4_t model, wgr_pick_result_t *out)
{
    wgr_mat4_t inv;

    if (out == NULL) {
        return;
    }
    if (world_hit == NULL || !world_hit->hit) {
        *out = (wgr_pick_result_t){0};
        return;
    }

    inv = wgr_mat4_inverse(model);
    out->hit = true;
    out->point_world = world_hit->point;
    out->normal_world = wgr_v3_norm(world_hit->normal);
    out->point_local = wgr_mat4_mul_point(inv, world_hit->point);
    out->normal_local = wgr_v3_norm(mul_direction(inv, world_hit->normal));
    out->distance = v3_dist(world_hit->point, world_ray.origin);
}

void wgr_pick_world_aabb(vec3_t lmin, vec3_t lmax, wgr_mat4_t model, vec3_t *wmin, vec3_t *wmax)
{
    vec3_t mn = {1e30f, 1e30f, 1e30f}, mx = {-1e30f, -1e30f, -1e30f};

    for (int i = 0; i < 8; i++) {
        vec3_t c = {(i & 1) ? lmax.x : lmin.x,
                    (i & 2) ? lmax.y : lmin.y,
                    (i & 4) ? lmax.z : lmin.z};
        vec3_t w = wgr_mat4_mul_point(model, c);
        if (w.x < mn.x) mn.x = w.x;
        if (w.y < mn.y) mn.y = w.y;
        if (w.z < mn.z) mn.z = w.z;
        if (w.x > mx.x) mx.x = w.x;
        if (w.y > mx.y) mx.y = w.y;
        if (w.z > mx.z) mx.z = w.z;
    }
    *wmin = mn;
    *wmax = mx;
}

bool wgr_pick_ray_world_aabb(wgr_ray_t ray, vec3_t lmin, vec3_t lmax, wgr_mat4_t model, float *t_out)
{
    vec3_t wmin, wmax;
    wgr_ray_hit_t hit = {0};

    wgr_pick_world_aabb(lmin, lmax, model, &wmin, &wmax);
    if (!wgr_pick_ray_aabb(ray, wmin, wmax, &hit)) {
        return false;
    }
    if (t_out != NULL) {
        *t_out = hit.t;
    }
    return true;
}
