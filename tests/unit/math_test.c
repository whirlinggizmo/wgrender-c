#include "internal/wgr_math.h"
#include "test.h"
#include "tests.h"

#define EPS 1e-4f
#define HALF_PI 1.5707963267948966f

void test_math_inverse(void)
{
    wgr_mat4_t m = wgr_mat4_trs((vec3_t){1, 2, 3}, (vec3_t){0.3f, 0.5f, -0.2f}, (vec3_t){2, 0.5f, 1.5f});
    wgr_mat4_t product = wgr_mat4_mul(m, wgr_mat4_inverse(m));
    wgr_mat4_t identity = wgr_mat4_identity();
    for (int i = 0; i < 16; i++) {
        CHECK_NEAR(product.m[i], identity.m[i], EPS);
    }

    vec3_t p = {-4, 7, 0.5f};
    vec3_t round_trip = wgr_mat4_mul_point(wgr_mat4_inverse(m), wgr_mat4_mul_point(m, p));
    CHECK_VEC3_NEAR(round_trip, p.x, p.y, p.z, EPS);

    /* documented: a singular matrix inverts to identity */
    wgr_mat4_t singular = wgr_mat4_inverse(wgr_mat4_scale(0, 1, 1));
    for (int i = 0; i < 16; i++) {
        CHECK_NEAR(singular.m[i], identity.m[i], EPS);
    }
}

void test_math_trs(void)
{
    const vec3_t origin = {0, 0, 0}, unit = {1, 1, 1};

    /* right-handed rotations */
    vec3_t p = wgr_mat4_mul_point(wgr_mat4_trs(origin, (vec3_t){0, HALF_PI, 0}, unit), (vec3_t){1, 0, 0});
    CHECK_VEC3_NEAR(p, 0, 0, -1, EPS);
    p = wgr_mat4_mul_point(wgr_mat4_trs(origin, (vec3_t){0, 0, HALF_PI}, unit), (vec3_t){1, 0, 0});
    CHECK_VEC3_NEAR(p, 0, 1, 0, EPS);

    /* scale, then rotate, then translate */
    p = wgr_mat4_mul_point(wgr_mat4_trs((vec3_t){10, 0, 0}, (vec3_t){0, 0, HALF_PI}, (vec3_t){2, 1, 1}),
                          (vec3_t){1, 0, 0});
    CHECK_VEC3_NEAR(p, 10, 2, 0, EPS);

    /* T * Rz * Ry * Rx * S: Ry applies before Rz. (0,0,1) -> Ry -> (1,0,0) -> Rz -> (0,1,0);
     * the reverse order would give (1,0,0). */
    p = wgr_mat4_mul_point(wgr_mat4_trs(origin, (vec3_t){0, HALF_PI, HALF_PI}, unit), (vec3_t){0, 0, 1});
    CHECK_VEC3_NEAR(p, 0, 1, 0, EPS);
}

void test_math_angles(void)
{
    CHECK_NEAR(180.0f * WGR_DEG2RAD, 3.14159265f, 1e-6f);
    CHECK_NEAR(0.5f * WGR_RAD2DEG, 28.6478898f, 1e-4f);
    CHECK_NEAR(37.0f * WGR_DEG2RAD * WGR_RAD2DEG, 37.0f, 1e-4f);
}
