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
    .ortho_height = 10.0f,
    .projection = SK_CAMERA3D_ORTHOGRAPHIC,
};

void test_camera_projection(void)
{
    const float aspect = 2.0f;
    sk_mat4_t view_proj;
    vec3_t ndc;

    /* perspective is the existing perspective matrix */
    sk_camera3d_t persp = ORTHO;
    persp.fov = 0.785398163f; /* pi / 4 */
    persp.projection = SK_CAMERA3D_PERSPECTIVE;
    sk_mat4_t expected = sk_mat4_perspective(0.785398163f, aspect, SK_CAMERA3D_PERSPECTIVE_NEAR,
                                             SK_CAMERA3D_PERSPECTIVE_FAR);
    sk_mat4_t actual = sk_camera3d_projection(&persp, aspect);
    for (int i = 0; i < 16; i++) {
        CHECK_NEAR(actual.m[i], expected.m[i], EPS);
    }

    /* orthographic: the view spans ortho_height vertically and ortho_height * aspect horizontally,
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

void test_camera_api(void)
{
    sk_camera3d_init();

    sk_handle_t camera = sk_camera3d_create(SK_CAMERA3D_ORTHOGRAPHIC);
    CHECK(camera != 0);
    CHECK(sk_camera3d_get_projection(camera) == SK_CAMERA3D_ORTHOGRAPHIC);
    CHECK_NEAR(sk_camera3d_get_fov(camera), 0.785398163f, EPS); /* default pi/4 */
    CHECK_NEAR(sk_camera3d_get_ortho_height(camera), 10.0f, EPS);

    /* fov and ortho height are separate and both survive projection switches */
    CHECK(sk_camera3d_set_fov(camera, 1.2f));
    CHECK(sk_camera3d_set_ortho_height(camera, 25.0f));
    CHECK(sk_camera3d_set_projection(camera, SK_CAMERA3D_PERSPECTIVE));
    CHECK(sk_camera3d_set_projection(camera, SK_CAMERA3D_ORTHOGRAPHIC));
    CHECK_NEAR(sk_camera3d_get_fov(camera), 1.2f, EPS);
    CHECK_NEAR(sk_camera3d_get_ortho_height(camera), 25.0f, EPS);

    /* invalid values are rejected and leave the camera unchanged */
    CHECK(!sk_camera3d_set_fov(camera, 0.0f));
    CHECK(!sk_camera3d_set_fov(camera, 3.2f)); /* >= pi */
    CHECK(!sk_camera3d_set_ortho_height(camera, -1.0f));
    CHECK(!sk_camera3d_set_projection(camera, (sk_camera3d_projection_t)5));
    CHECK_NEAR(sk_camera3d_get_fov(camera), 1.2f, EPS);
    CHECK_NEAR(sk_camera3d_get_ortho_height(camera), 25.0f, EPS);
    CHECK(sk_camera3d_get_projection(camera) == SK_CAMERA3D_ORTHOGRAPHIC);

    /* the view is what the projection helper uses */
    CHECK(sk_camera3d_set_view(camera, 1, 2, 3, 4, 5, 6, 0, 1, 0));
    CHECK(sk_camera3d_set_active(camera));
    sk_camera3d_t data;
    CHECK(sk_camera3d_get_active_data(&data));
    CHECK_VEC3_NEAR(data.position, 1, 2, 3, EPS);
    CHECK_VEC3_NEAR(data.target, 4, 5, 6, EPS);

    sk_camera3d_destroy(camera);
    sk_camera3d_deinit();
}
