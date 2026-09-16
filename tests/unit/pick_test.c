#include "internal/sk_pick.h"
#include "sk_camera3d.h"
#include "test.h"
#include "tests.h"

#define EPS 1e-4f
#define HALF_PI 1.5707963267948966f

static sk_ray_t ray(float ox, float oy, float oz, float dx, float dy, float dz)
{
    return (sk_ray_t){.origin = {ox, oy, oz}, .dir = sk_v3_norm((vec3_t){dx, dy, dz})};
}

static float dot(vec3_t a, vec3_t b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

void test_pick_ray_sphere(void)
{
    const vec3_t center = {0, 0, 0};
    sk_ray_hit_t hit = {0};

    CHECK(sk_pick_ray_sphere(ray(0, 0, -10, 0, 0, 1), center, 2, &hit));
    CHECK_NEAR(hit.t, 8, EPS);
    CHECK_VEC3_NEAR(hit.point, 0, 0, -2, EPS);
    CHECK_VEC3_NEAR(hit.normal, 0, 0, -1, EPS);

    CHECK(!sk_pick_ray_sphere(ray(0, 5, -10, 0, 0, 1), center, 2, &hit)); /* passes beside */
    CHECK(!sk_pick_ray_sphere(ray(0, 0, 10, 0, 0, 1), center, 2, &hit));  /* sphere behind */
    CHECK(!sk_pick_ray_sphere(ray(0, 0, -10, 0, 0, 1), center, 0, &hit)); /* no radius */

    /* from inside: the exit point, normal against the ray (sk_ray_hit_t contract) */
    CHECK(sk_pick_ray_sphere(ray(0, 0, 0, 0, 0, 1), center, 2, &hit));
    CHECK_NEAR(hit.t, 2, EPS);
    CHECK_VEC3_NEAR(hit.point, 0, 0, 2, EPS);
    CHECK(dot(hit.normal, (vec3_t){0, 0, 1}) < 0);
}

void test_pick_ray_aabb(void)
{
    const vec3_t lo = {-1, -1, -1}, hi = {1, 1, 1};
    sk_ray_hit_t hit = {0};

    CHECK(sk_pick_ray_aabb(ray(-5, 0, 0, 1, 0, 0), lo, hi, &hit));
    CHECK_NEAR(hit.t, 4, EPS);
    CHECK_VEC3_NEAR(hit.point, -1, 0, 0, EPS);
    CHECK_VEC3_NEAR(hit.normal, -1, 0, 0, EPS);

    CHECK(sk_pick_ray_aabb(ray(0, 5, 0, 0, -1, 0), lo, hi, &hit));
    CHECK_NEAR(hit.t, 4, EPS);
    CHECK_VEC3_NEAR(hit.normal, 0, 1, 0, EPS);

    /* entering through an edge */
    CHECK(sk_pick_ray_aabb(ray(-5, -5, 0, 1, 1, 0), lo, hi, &hit));
    CHECK_VEC3_NEAR(hit.point, -1, -1, 0, EPS);

    CHECK(!sk_pick_ray_aabb(ray(-5, 3, 0, 1, 0, 0), lo, hi, &hit));  /* parallel, outside the slab */
    CHECK(!sk_pick_ray_aabb(ray(-5, 0, 0, -1, 0, 0), lo, hi, &hit)); /* pointing away */

    /* from inside: the exit point, normal against the ray */
    CHECK(sk_pick_ray_aabb(ray(0, 0, 0, 1, 0, 0), lo, hi, &hit));
    CHECK_NEAR(hit.t, 1, EPS);
    CHECK_VEC3_NEAR(hit.point, 1, 0, 0, EPS);
    CHECK(dot(hit.normal, (vec3_t){1, 0, 0}) < 0);
}

void test_pick_ray_triangle(void)
{
    const vec3_t v0 = {-1, -1, 0}, v1 = {1, -1, 0}, v2 = {0, 1, 0};
    sk_ray_hit_t hit = {0};

    CHECK(sk_pick_ray_triangle(ray(0, 0, -5, 0, 0, 1), v0, v1, v2, &hit));
    CHECK_NEAR(hit.t, 5, EPS);
    CHECK_VEC3_NEAR(hit.point, 0, 0, 0, EPS);
    CHECK_VEC3_NEAR(hit.normal, 0, 0, -1, EPS); /* faces the ray */
    /* point = v0 + u*(v1 - v0) + v*(v2 - v0) */
    CHECK_NEAR(hit.u, 0.25f, EPS);
    CHECK_NEAR(hit.v, 0.5f, EPS);

    /* double-sided: hit from behind, normal flips to face that ray */
    CHECK(sk_pick_ray_triangle(ray(0, 0, 5, 0, 0, -1), v0, v1, v2, &hit));
    CHECK_VEC3_NEAR(hit.normal, 0, 0, 1, EPS);

    CHECK(!sk_pick_ray_triangle(ray(2, 0, -5, 0, 0, 1), v0, v1, v2, &hit)); /* outside the edges */
    CHECK(!sk_pick_ray_triangle(ray(0, 0, -5, 1, 0, 0), v0, v1, v2, &hit)); /* parallel */
    CHECK(!sk_pick_ray_triangle(ray(0, 0, 5, 0, 0, 1), v0, v1, v2, &hit));  /* triangle behind */
}

void test_pick_ray_to_local(void)
{
    /* model: scale 2, rotate +90 degrees about y, then translate to (10,0,0) */
    sk_mat4_t model = sk_mat4_trs((vec3_t){10, 0, 0}, (vec3_t){0, HALF_PI, 0}, (vec3_t){2, 2, 2});
    sk_ray_t world = ray(10, 0, -10, 0, 0, 1);
    sk_ray_t local = sk_pick_ray_to_local(model, world);
    CHECK_VEC3_NEAR(local.origin, 5, 0, 0, EPS);
    CHECK_VEC3_NEAR(local.dir, -1, 0, 0, EPS);

    /* a local-space hit resolves back to world space */
    sk_ray_hit_t local_hit = {0};
    sk_pick_result_t result = {0};
    CHECK(sk_pick_ray_sphere(local, (vec3_t){0, 0, 0}, 1, &local_hit));
    sk_pick_result_from_local(&local_hit, world, model, &result);
    CHECK(result.hit);
    CHECK_VEC3_NEAR(result.point_local, 1, 0, 0, EPS);
    CHECK_VEC3_NEAR(result.point_world, 10, 0, -2, EPS);
    CHECK_VEC3_NEAR(result.normal_world, 0, 0, -1, EPS);
    CHECK_NEAR(result.distance, 8, EPS);
}

void test_pick_world_aabb(void)
{
    /* rotating +90 degrees about z swaps the x and y extents */
    sk_mat4_t model = sk_mat4_trs((vec3_t){5, 0, 0}, (vec3_t){0, 0, HALF_PI}, (vec3_t){1, 1, 1});
    vec3_t wmin, wmax;
    sk_pick_world_aabb((vec3_t){-1, -2, -3}, (vec3_t){1, 2, 3}, model, &wmin, &wmax);
    CHECK_VEC3_NEAR(wmin, 3, -1, -3, EPS);
    CHECK_VEC3_NEAR(wmax, 7, 1, 3, EPS);

    float t = 0;
    CHECK(sk_pick_ray_world_aabb(ray(0, 0, 0, 1, 0, 0), (vec3_t){-1, -2, -3}, (vec3_t){1, 2, 3}, model, &t));
    CHECK_NEAR(t, 3, EPS);
}

void test_pick_ray_from_screen(void)
{
    const sk_camera3d_t cam = {
        .position = {0, 0, 10},
        .target = {0, 0, 0},
        .up = {0, 1, 0},
        .fovy = 45.0f,
        .projection = SK_CAMERA3D_PERSPECTIVE,
    };
    const float w = 800, h = 600;
    const float tan_half_fovy = 0.41421356f; /* tan(22.5 degrees) */

    /* screen center looks straight at the target, starting on the near plane */
    sk_ray_t r = sk_pick_ray_from_screen(&cam, w / 2, h / 2, w, h);
    CHECK_VEC3_NEAR(r.dir, 0, 0, -1, EPS);
    CHECK_VEC3_NEAR(r.origin, 0, 0, 9.99f, 1e-3f);

    /* right edge: horizontal slope is tan(fovy/2) * aspect */
    r = sk_pick_ray_from_screen(&cam, w, h / 2, w, h);
    CHECK_NEAR(r.dir.x / -r.dir.z, tan_half_fovy * (w / h), 1e-3f);
    CHECK_NEAR(r.dir.y, 0, EPS);

    /* top edge (screen y grows downward): vertical slope is tan(fovy/2) */
    r = sk_pick_ray_from_screen(&cam, w / 2, 0, w, h);
    CHECK_NEAR(r.dir.y / -r.dir.z, tan_half_fovy, 1e-3f);

    /* degenerate screen size: zero ray */
    r = sk_pick_ray_from_screen(&cam, 0, 0, 0, h);
    CHECK_VEC3_NEAR(r.dir, 0, 0, 0, EPS);
}
