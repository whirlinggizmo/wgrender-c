#include "internal/wgr_camera3d_internal.h"
#include "internal/wgr_pick_internal.h"
#include "wgr_camera3d.h"
#include "test.h"
#include "tests.h"

#define EPS 1e-4f

static const wgri_camera3d_t ORTHO = {
    .position = {0, 0, 10},
    .target = {0, 0, 0},
    .up = {0, 1, 0},
    .ortho_height = 10.0f,
    .projection = WGR_CAMERA3D_ORTHOGRAPHIC,
};

void test_camera_projection(void)
{
    const float aspect = 2.0f;
    wgri_mat4_t view_proj;
    vec3_t ndc;

    /* perspective is the existing perspective matrix */
    wgri_camera3d_t persp = ORTHO;
    persp.fov = 0.785398163f; /* pi / 4 */
    persp.projection = WGR_CAMERA3D_PERSPECTIVE;
    wgri_mat4_t expected = wgri_mat4_perspective(0.785398163f, aspect, WGRI_CAMERA3D_PERSPECTIVE_NEAR,
                                             WGRI_CAMERA3D_PERSPECTIVE_FAR);
    wgri_mat4_t actual = wgri_camera3d_projection(&persp, aspect);
    for (int i = 0; i < 16; i++) {
        CHECK_NEAR(actual.m[i], expected.m[i], EPS);
    }

    /* orthographic: the view spans ortho_height vertically and ortho_height * aspect horizontally,
     * with no perspective divide (distance doesn't change size) */
    view_proj = wgri_mat4_mul(wgri_camera3d_projection(&ORTHO, aspect), wgri_camera3d_view(&ORTHO));
    ndc = wgri_mat4_mul_point(view_proj, (vec3_t){10, 5, 0}); /* right edge, top edge */
    CHECK_NEAR(ndc.x, 1, EPS);
    CHECK_NEAR(ndc.y, 1, EPS);
    ndc = wgri_mat4_mul_point(view_proj, (vec3_t){10, 5, -50}); /* same, much farther away */
    CHECK_NEAR(ndc.x, 1, EPS);
    CHECK_NEAR(ndc.y, 1, EPS);
    CHECK_NEAR(actual.m[15], 0, EPS);                                   /* perspective has w = -z */
    CHECK_NEAR(wgri_camera3d_projection(&ORTHO, aspect).m[15], 1, EPS); /* orthographic doesn't */
}

void test_pick_ray_from_screen_ortho(void)
{
    const float w = 800, h = 400; /* aspect 2: visible area 20 x 10 world units */

    /* every orthographic ray points along the view direction */
    wgri_ray_t center = wgri_pick_ray_from_screen(&ORTHO, w / 2, h / 2, w, h);
    wgri_ray_t corner = wgri_pick_ray_from_screen(&ORTHO, w, 0, w, h);
    CHECK_VEC3_NEAR(center.dir, 0, 0, -1, EPS);
    CHECK_VEC3_NEAR(corner.dir, 0, 0, -1, EPS);

    /* and starts offset sideways by the screen position, in world units */
    CHECK_NEAR(center.origin.x, 0, 1e-3f);
    CHECK_NEAR(center.origin.y, 0, 1e-3f);
    CHECK_NEAR(corner.origin.x, 10, 1e-3f); /* right edge: half of 20 */
    CHECK_NEAR(corner.origin.y, 5, 1e-3f);  /* top edge: half of 10 */

    /* so a box off to the side is hit straight on, not at a perspective angle */
    wgri_ray_hit_t hit = {0};
    wgri_ray_t ray = wgri_pick_ray_from_screen(&ORTHO, w * 0.75f, h / 2, w, h); /* x = 5 */
    CHECK(wgri_pick_ray_aabb(ray, (vec3_t){4.5f, -0.5f, -0.5f}, (vec3_t){5.5f, 0.5f, 0.5f}, &hit));
    CHECK_VEC3_NEAR(hit.point, 5, 0, 0.5f, 1e-3f);
    CHECK_VEC3_NEAR(hit.normal, 0, 0, 1, EPS);
}

void test_camera_api(void)
{
    wgri_camera3d_init();

    wgr_handle_t camera = wgr_camera3d_create(WGR_CAMERA3D_ORTHOGRAPHIC);
    CHECK(camera != 0);
    CHECK(wgr_camera3d_get_projection(camera) == WGR_CAMERA3D_ORTHOGRAPHIC);
    CHECK_NEAR(wgr_camera3d_get_fov(camera), 0.785398163f, EPS); /* default pi/4 */
    CHECK_NEAR(wgr_camera3d_get_ortho_height(camera), 10.0f, EPS);

    /* fov and ortho height are separate and both survive projection switches */
    CHECK(wgr_camera3d_set_fov(camera, 1.2f));
    CHECK(wgr_camera3d_set_ortho_height(camera, 25.0f));
    CHECK(wgr_camera3d_set_projection(camera, WGR_CAMERA3D_PERSPECTIVE));
    CHECK(wgr_camera3d_set_projection(camera, WGR_CAMERA3D_ORTHOGRAPHIC));
    CHECK_NEAR(wgr_camera3d_get_fov(camera), 1.2f, EPS);
    CHECK_NEAR(wgr_camera3d_get_ortho_height(camera), 25.0f, EPS);

    /* invalid values are rejected and leave the camera unchanged */
    CHECK(!wgr_camera3d_set_fov(camera, 0.0f));
    CHECK(!wgr_camera3d_set_fov(camera, 3.2f)); /* >= pi */
    CHECK(!wgr_camera3d_set_ortho_height(camera, -1.0f));
    CHECK(!wgr_camera3d_set_projection(camera, (wgr_camera3d_projection_t)5));
    CHECK_NEAR(wgr_camera3d_get_fov(camera), 1.2f, EPS);
    CHECK_NEAR(wgr_camera3d_get_ortho_height(camera), 25.0f, EPS);
    CHECK(wgr_camera3d_get_projection(camera) == WGR_CAMERA3D_ORTHOGRAPHIC);

    /* the view is what the projection helper uses */
    CHECK(wgr_camera3d_set_view(camera, 1, 2, 3, 4, 5, 6, 0, 1, 0));
    CHECK(wgr_camera3d_set_active(camera));
    wgri_camera3d_t data;
    CHECK(wgri_camera3d_get_active_data(&data));
    CHECK_VEC3_NEAR(data.position, 1, 2, 3, EPS);
    CHECK_VEC3_NEAR(data.target, 4, 5, 6, EPS);

    wgr_camera3d_destroy(camera);
    wgri_camera3d_deinit();
}
