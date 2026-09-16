#include "internal/sk_camera3d.h"
#include "internal/sk_pick.h"
#include "sk_camera3d.h"
#include "test.h"
#include "tests.h"

#define EPS 1e-4f

static const sk_camera3d_t ORTHO = {
    .position = {0, 0, 10},
    .target = {0, 0, 0},
    .up = {0, 1, 0},
    .fovy = 10.0f, /* orthographic: visible height in world units */
    .projection = SK_CAMERA3D_ORTHOGRAPHIC,
};

void test_camera_projection(void)
{
    const float aspect = 2.0f;
    sk_mat4_t view_proj;
    vec3_t ndc;

    /* perspective is the existing perspective matrix */
    sk_camera3d_t persp = ORTHO;
    persp.fovy = 45.0f;
    persp.projection = SK_CAMERA3D_PERSPECTIVE;
    sk_mat4_t expected = sk_mat4_perspective(45.0f * SK_DEG2RAD, aspect, SK_CAMERA3D_PERSPECTIVE_NEAR,
                                             SK_CAMERA3D_PERSPECTIVE_FAR);
    sk_mat4_t actual = sk_camera3d_projection(&persp, aspect);
    for (int i = 0; i < 16; i++) {
        CHECK_NEAR(actual.m[i], expected.m[i], EPS);
    }

    /* orthographic: the view spans fovy vertically and fovy * aspect horizontally,
     * with no perspective divide (distance doesn't change size) */
    view_proj = sk_mat4_mul(sk_camera3d_projection(&ORTHO, aspect), sk_camera3d_view(&ORTHO));
    ndc = sk_mat4_mul_point(view_proj, (vec3_t){10, 5, 0}); /* right edge, top edge */
    CHECK_NEAR(ndc.x, 1, EPS);
    CHECK_NEAR(ndc.y, 1, EPS);
    ndc = sk_mat4_mul_point(view_proj, (vec3_t){10, 5, -50}); /* same, much farther away */
    CHECK_NEAR(ndc.x, 1, EPS);
    CHECK_NEAR(ndc.y, 1, EPS);
    CHECK_NEAR(actual.m[15], 0, EPS);                                   /* perspective has w = -z */
    CHECK_NEAR(sk_camera3d_projection(&ORTHO, aspect).m[15], 1, EPS); /* orthographic doesn't */
}

void test_pick_ray_from_screen_ortho(void)
{
    const float w = 800, h = 400; /* aspect 2: visible area 20 x 10 world units */

    /* every orthographic ray points along the view direction */
    sk_ray_t center = sk_pick_ray_from_screen(&ORTHO, w / 2, h / 2, w, h);
    sk_ray_t corner = sk_pick_ray_from_screen(&ORTHO, w, 0, w, h);
    CHECK_VEC3_NEAR(center.dir, 0, 0, -1, EPS);
    CHECK_VEC3_NEAR(corner.dir, 0, 0, -1, EPS);

    /* and starts offset sideways by the screen position, in world units */
    CHECK_NEAR(center.origin.x, 0, 1e-3f);
    CHECK_NEAR(center.origin.y, 0, 1e-3f);
    CHECK_NEAR(corner.origin.x, 10, 1e-3f); /* right edge: half of 20 */
    CHECK_NEAR(corner.origin.y, 5, 1e-3f);  /* top edge: half of 10 */

    /* so a box off to the side is hit straight on, not at a perspective angle */
    sk_ray_hit_t hit = {0};
    sk_ray_t ray = sk_pick_ray_from_screen(&ORTHO, w * 0.75f, h / 2, w, h); /* x = 5 */
    CHECK(sk_pick_ray_aabb(ray, (vec3_t){4.5f, -0.5f, -0.5f}, (vec3_t){5.5f, 0.5f, 0.5f}, &hit));
    CHECK_VEC3_NEAR(hit.point, 5, 0, 0.5f, 1e-3f);
    CHECK_VEC3_NEAR(hit.normal, 0, 0, 1, EPS);
}
