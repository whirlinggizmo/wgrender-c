/* Frustum culling (docs/PLAN-culling.md): the planes of a view-projection and the box
 * test against them, then what a scene does with them — a member the camera can't see
 * isn't submitted at all, while a caster whose shadow could still fall into view is. */
#include <math.h>

#include "internal/sk_camera3d.h"
#include "internal/sk_environment.h"
#include "internal/sk_internal.h"
#include "internal/sk_light.h"
#include "internal/sk_material.h"
#include "internal/sk_math.h"
#include "internal/sk_model.h"
#include "internal/sk_platform.h"
#include "internal/sk_render.h"
#include "internal/sk_scene.h"
#include "internal/sk_texture.h"
#include "sk_camera3d.h"
#include "sk_color.h"
#include "sk_light.h"
#include "sk_model.h"
#include "sk_render.h"
#include "sk_scene.h"
#include "test.h"
#include "tests.h"

#include "sokol_gfx.h"

/* A camera at (0, 0, 10) looking at the origin, 60 degrees, square. */
static sk_mat4_t test_view_proj(void)
{
    sk_camera3d_t cam = {
        .position = {0.0f, 0.0f, 10.0f},
        .target = {0.0f, 0.0f, 0.0f},
        .up = {0.0f, 1.0f, 0.0f},
        .fov = 60.0f * 3.14159265f / 180.0f,
        .ortho_height = 10.0f,
        .projection = SK_CAMERA3D_PERSPECTIVE,
    };
    return sk_mat4_mul(sk_camera3d_projection(&cam, 1.0f), sk_camera3d_view(&cam));
}

void test_cull_frustum(void)
{
    sk_plane_t planes[6];
    sk_frustum_from_view_proj(test_view_proj(), planes);

    /* the planes are unit length, so a test gives a real distance */
    for (int i = 0; i < 6; i++) {
        const float length = sqrtf(planes[i].a * planes[i].a + planes[i].b * planes[i].b + planes[i].c * planes[i].c);
        CHECK_NEAR(length, 1.0f, 1e-4f);
    }

    /* what the camera is looking at */
    CHECK(sk_frustum_test_aabb(planes, (vec3_t){-1, -1, -1}, (vec3_t){1, 1, 1}));
    /* behind it, to the side, and far past it */
    CHECK(!sk_frustum_test_aabb(planes, (vec3_t){-1, -1, 20}, (vec3_t){1, 1, 22}));
    CHECK(!sk_frustum_test_aabb(planes, (vec3_t){80, -1, -1}, (vec3_t){82, 1, 1}));
    CHECK(!sk_frustum_test_aabb(planes, (vec3_t){-1, -1, -3000}, (vec3_t){1, 1, -2998}));
    /* a box that straddles the edge is kept: the test never culls something visible */
    CHECK(sk_frustum_test_aabb(planes, (vec3_t){-100, -1, -1}, (vec3_t){0, 1, 1}));
    /* and one that swallows the whole frustum is too */
    CHECK(sk_frustum_test_aabb(planes, (vec3_t){-500, -500, -500}, (vec3_t){500, 500, 500}));

    /* a box swept along a direction covers where its shadow could fall */
    vec3_t smin, smax;
    sk_aabb_sweep((vec3_t){-1, 0, -1}, (vec3_t){1, 2, 1}, (vec3_t){0, -1, 0}, 5.0f, &smin, &smax);
    CHECK_NEAR(smin.y, -5.0f, 1e-5f); /* down five */
    CHECK_NEAR(smax.y, 2.0f, 1e-5f);  /* and not up at all */
    CHECK_NEAR(smin.x, -1.0f, 1e-5f);
    CHECK_NEAR(smax.x, 1.0f, 1e-5f);
}

/* Every model draw reads its matrices and tint out of the frame's instance records
 * (docs/PLAN-instancing.md), so what goes into one is what gets drawn. */
void test_model_instance_record(void)
{
    sg_setup(&(sg_desc){.environment = sk_platform_environment()});
    sk_render_init();
    sk_camera3d_init();
    sk_texture_init();
    sk_light_init();
    sk_material_init();
    sk_environment_init();
    sk_model_init();

    const sk_handle_t camera = sk_camera3d_create(SK_CAMERA3D_PERSPECTIVE);
    sk_camera3d_set_view(camera, 0, 0, 10, 0, 0, 0, 0, 1, 0);
    sk_camera3d_set_active(camera);

    const sk_handle_t mesh = sk_mesh_create_cube(1.0f, 1.0f, 1.0f);
    const sk_handle_t model = sk_model_create(mesh);
    sk_mesh_release(mesh);
    sk_model_set_transform(model, 3.0f, 4.0f, 5.0f, 0, 0, 0, 1, 1, 1);
    sk_model_set_tint(model, sk_color_rgba(255, 128, 0, 128));

    int count = 0;
    sk_render_begin();
    sk_model_draw(model);
    sk_model_flush();
    const float *records = sk_model_instance_records(&count);
    CHECK(count == 1 && records != NULL);
    if (records != NULL && count == 1) {
        /* the model matrix goes up as three rows, so the translation is each row's w */
        CHECK_NEAR(records[3], 3.0f, 1e-5f);
        CHECK_NEAR(records[7], 4.0f, 1e-5f);
        CHECK_NEAR(records[11], 5.0f, 1e-5f);
        /* no rotation or scale: the rest is the identity */
        CHECK_NEAR(records[0], 1.0f, 1e-5f);
        CHECK_NEAR(records[5], 1.0f, 1e-5f);
        CHECK_NEAR(records[10], 1.0f, 1e-5f);
        /* the tint is linear by the time it is a record; its alpha already was */
        CHECK_NEAR(records[24], 1.0f, 1e-4f);
        CHECK_NEAR(records[25], sk_srgb_to_linear(128.0f / 255.0f), 1e-4f);
        CHECK_NEAR(records[26], 0.0f, 1e-4f);
        CHECK_NEAR(records[27], 128.0f / 255.0f, 1e-3f);
        CHECK(records[28] == 0.0f); /* not skinned: no joints of its own */
    }
    sk_render_end();

    sk_model_destroy(model);
    sk_camera3d_destroy(camera);
    sk_model_deinit();
    sk_environment_deinit();
    sk_material_deinit();
    sk_light_deinit();
    sk_texture_deinit();
    sk_camera3d_deinit();
    sk_render_deinit();
    sg_shutdown();
}

/* Models that agree on everything but their placement go up as one draw
 * (docs/PLAN-instancing.md, phase 2). */
void test_model_instancing(void)
{
    sg_setup(&(sg_desc){.environment = sk_platform_environment()});
    sk_render_init();
    sk_scene_init();
    sk_camera3d_init();
    sk_texture_init();
    sk_light_init();
    sk_material_init();
    sk_environment_init();
    sk_model_init();

    const sk_handle_t scene = sk_scene_create();
    const sk_handle_t camera = sk_camera3d_create(SK_CAMERA3D_PERSPECTIVE);
    sk_camera3d_set_view(camera, 0, 2, 20, 0, 0, 0, 0, 1, 0);
    sk_scene_set_active_camera(scene, camera);

    const sk_handle_t mesh = sk_mesh_create_cube(1.0f, 1.0f, 1.0f);
    const sk_handle_t material = sk_material_create(SK_MATERIAL_PBR);
    sk_handle_t models[8];
    for (int i = 0; i < 8; i++) {
        models[i] = sk_model_create(mesh);
        sk_model_set_material(models[i], -1, material);
        sk_model_set_transform(models[i], (float)i - 4.0f, 0, 0, 0, 0, 0, 1, 1, 1);
        sk_scene_add(scene, models[i], 0);
    }
    sk_mesh_release(mesh);
    sk_material_release(material);

    /* one mesh, one material, eight placements: one draw */
    sk_render_begin();
    sk_scene_draw(scene);
    sk_render_end();
    CHECK(sk_model_draw_call_count() == 1);

    /* a different material splits it in two, wherever the models sit in the scene */
    const sk_handle_t other = sk_material_create(SK_MATERIAL_PBR);
    sk_model_set_material(models[3], -1, other);
    sk_render_begin();
    sk_scene_draw(scene);
    sk_render_end();
    CHECK(sk_model_draw_call_count() == 2);
    sk_model_set_material(models[3], -1, material);
    sk_material_release(other);

    /* so does a different mesh */
    const sk_handle_t sphere = sk_mesh_create_sphere(0.5f, 8, 8);
    sk_model_set_mesh(models[5], sphere);
    sk_model_set_material(models[5], -1, material);
    sk_mesh_release(sphere);
    sk_render_begin();
    sk_scene_draw(scene);
    sk_render_end();
    CHECK(sk_model_draw_call_count() == 2);

    /* and a tint does not: that is what the instance record is for */
    sk_model_set_mesh(models[5], mesh);
    sk_model_set_material(models[5], -1, material);
    for (int i = 0; i < 8; i++) {
        sk_model_set_tint(models[i], sk_color_rgba(255, i * 30, 0, 255));
    }
    sk_render_begin();
    sk_scene_draw(scene);
    sk_render_end();
    CHECK(sk_model_draw_call_count() == 1);

    for (int i = 0; i < 8; i++) {
        sk_model_destroy(models[i]);
    }
    sk_scene_destroy(scene);
    sk_model_deinit();
    sk_environment_deinit();
    sk_material_deinit();
    sk_light_deinit();
    sk_texture_deinit();
    sk_camera3d_deinit();
    sk_scene_deinit();
    sk_render_deinit();
    sg_shutdown();
}

/* How many model placements a scene draw queued. */
static int queued(sk_handle_t scene)
{
    int placements = 0;
    sk_render_begin();
    sk_scene_draw(scene);
    sk_model_queue_counts(&placements, NULL, NULL);
    sk_render_end();
    return placements;
}

void test_cull_scene(void)
{
    sg_setup(&(sg_desc){.environment = sk_platform_environment()});
    sk_render_init();
    sk_scene_init();
    sk_camera3d_init();
    sk_texture_init();
    sk_light_init();
    sk_material_init();
    sk_environment_init();
    sk_model_init();

    const sk_handle_t scene = sk_scene_create();
    const sk_handle_t camera = sk_camera3d_create(SK_CAMERA3D_PERSPECTIVE);
    sk_camera3d_set_view(camera, 0, 0, 10, 0, 0, 0, 0, 1, 0);
    sk_scene_set_active_camera(scene, camera);
    CHECK(sk_scene_is_culling(scene)); /* on unless a scene says otherwise */

    const sk_handle_t mesh = sk_mesh_create_cube(1.0f, 1.0f, 1.0f);
    const sk_handle_t seen = sk_model_create(mesh);
    const sk_handle_t away = sk_model_create(mesh);
    sk_mesh_release(mesh);
    sk_scene_add(scene, seen, 0);
    sk_scene_add(scene, away, 0);
    sk_model_set_transform(seen, 0, 0, 0, 0, 0, 0, 1, 1, 1);
    sk_model_set_transform(away, 0, 0, 400, 0, 0, 0, 1, 1, 1); /* behind the camera */

    /* only what the camera can see is submitted */
    CHECK(queued(scene) == 1);

    /* moved into view, it is */
    sk_model_set_transform(away, 2, 0, 0, 0, 0, 0, 1, 1, 1);
    CHECK(queued(scene) == 2);
    sk_model_set_transform(away, 0, 0, 400, 0, 0, 0, 1, 1, 1);
    CHECK(queued(scene) == 1);

    /* with the switch off, everything is submitted as it used to be */
    CHECK(sk_scene_set_culling(scene, false));
    CHECK(!sk_scene_is_culling(scene));
    CHECK(queued(scene) == 2);
    CHECK(sk_scene_set_culling(scene, true));
    CHECK(queued(scene) == 1);

    /* a caster the camera can't see is kept when its shadow could reach the view: the
       sun points from the far model toward the origin, so it shadows what is seen */
    const sk_handle_t sun = sk_light_create(SK_LIGHT_DIRECTIONAL);
    sk_light_set_direction(sun, 0, 0, -1);
    sk_light_set_shadow_distance(sun, 500.0f);
    sk_scene_add(scene, sun, 0);
    CHECK(queued(scene) == 1); /* nothing casts yet */
    CHECK(sk_light_set_casts_shadows(sun, true));
    CHECK(queued(scene) == 2); /* now the far one is drawn, for its shadow */

    /* unless it is told not to cast, when it is culled again */
    CHECK(sk_model_set_casts_shadow(away, false));
    CHECK(queued(scene) == 1);
    CHECK(sk_model_set_casts_shadow(away, true));

    /* or unless the light's shadows don't reach that far */
    CHECK(sk_light_set_shadow_distance(sun, 10.0f));
    CHECK(queued(scene) == 1);

    sk_light_destroy(sun);
    sk_model_destroy(away);
    sk_model_destroy(seen);
    sk_scene_destroy(scene);
    sk_model_deinit();
    sk_environment_deinit();
    sk_material_deinit();
    sk_light_deinit();
    sk_texture_deinit();
    sk_camera3d_deinit();
    sk_scene_deinit();
    sk_render_deinit();
    sg_shutdown();
}
