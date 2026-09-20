/* Frustum culling (docs/PLAN-culling.md): the planes of a view-projection and the box
 * test against them, then what a scene does with them — a member the camera can't see
 * isn't submitted at all, while a caster whose shadow could still fall into view is. */
#include <math.h>

#include "internal/wgr_camera3d_internal.h"
#include "internal/wgr_environment_internal.h"
#include "internal/wgr_internal_internal.h"
#include "internal/wgr_light_internal.h"
#include "internal/wgr_material_internal.h"
#include "internal/wgr_math_internal.h"
#include "internal/wgr_model_internal.h"
#include "internal/wgr_platform_internal.h"
#include "internal/wgr_render_internal.h"
#include "internal/wgr_scene_internal.h"
#include "internal/wgr_shader_internal.h"
#include "internal/wgr_texture_internal.h"
#include "wgr_camera3d.h"
#include "wgr_color.h"
#include "wgr_light.h"
#include "wgr_material.h"
#include "wgr_model.h"
#include "wgr_shader.h"
#include "wgr_render.h"
#include "wgr_scene.h"
#include "test.h"
#include "tests.h"

#include "sokol_gfx.h"
#include "sokol_time.h"

/* A camera at (0, 0, 10) looking at the origin, 60 degrees, square. */
static wgr_mat4_t test_view_proj(void)
{
    wgr_camera3d_t cam = {
        .position = {0.0f, 0.0f, 10.0f},
        .target = {0.0f, 0.0f, 0.0f},
        .up = {0.0f, 1.0f, 0.0f},
        .fov = 60.0f * 3.14159265f / 180.0f,
        .ortho_height = 10.0f,
        .projection = WGR_CAMERA3D_PERSPECTIVE,
    };
    return wgr_mat4_mul(wgr_camera3d_projection(&cam, 1.0f), wgr_camera3d_view(&cam));
}

void test_cull_frustum(void)
{
    wgr_plane_t planes[6];
    wgr_frustum_from_view_proj(test_view_proj(), planes);

    /* the planes are unit length, so a test gives a real distance */
    for (int i = 0; i < 6; i++) {
        const float length = sqrtf(planes[i].a * planes[i].a + planes[i].b * planes[i].b + planes[i].c * planes[i].c);
        CHECK_NEAR(length, 1.0f, 1e-4f);
    }

    /* what the camera is looking at */
    CHECK(wgr_frustum_test_aabb(planes, (vec3_t){-1, -1, -1}, (vec3_t){1, 1, 1}));
    /* behind it, to the side, and far past it */
    CHECK(!wgr_frustum_test_aabb(planes, (vec3_t){-1, -1, 20}, (vec3_t){1, 1, 22}));
    CHECK(!wgr_frustum_test_aabb(planes, (vec3_t){80, -1, -1}, (vec3_t){82, 1, 1}));
    CHECK(!wgr_frustum_test_aabb(planes, (vec3_t){-1, -1, -3000}, (vec3_t){1, 1, -2998}));
    /* a box that straddles the edge is kept: the test never culls something visible */
    CHECK(wgr_frustum_test_aabb(planes, (vec3_t){-100, -1, -1}, (vec3_t){0, 1, 1}));
    /* and one that swallows the whole frustum is too */
    CHECK(wgr_frustum_test_aabb(planes, (vec3_t){-500, -500, -500}, (vec3_t){500, 500, 500}));

    /* a box swept along a direction covers where its shadow could fall */
    vec3_t smin, smax;
    wgr_aabb_sweep((vec3_t){-1, 0, -1}, (vec3_t){1, 2, 1}, (vec3_t){0, -1, 0}, 5.0f, &smin, &smax);
    CHECK_NEAR(smin.y, -5.0f, 1e-5f); /* down five */
    CHECK_NEAR(smax.y, 2.0f, 1e-5f);  /* and not up at all */
    CHECK_NEAR(smin.x, -1.0f, 1e-5f);
    CHECK_NEAR(smax.x, 1.0f, 1e-5f);
}

/* Every model draw reads its matrices and tint out of the frame's instance records
 * (docs/PLAN-instancing.md), so what goes into one is what gets drawn. */
void test_model_instance_record(void)
{
    sg_setup(&(sg_desc){.environment = wgr_platform_environment()});
    wgr_render_init();
    wgr_camera3d_init();
    wgr_texture_init();
    wgr_light_init();
    wgr_material_init();
    wgr_environment_init();
    wgr_model_init();

    const wgr_handle_t camera = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE);
    wgr_camera3d_set_view(camera, 0, 0, 10, 0, 0, 0, 0, 1, 0);
    wgr_camera3d_set_active(camera);

    const wgr_handle_t mesh = wgr_mesh_create_cube(1.0f, 1.0f, 1.0f);
    const wgr_handle_t model = wgr_model_create(mesh);
    wgr_mesh_release(mesh);
    wgr_model_set_transform(model, 3.0f, 4.0f, 5.0f, 0, 0, 0, 1, 1, 1);
    wgr_model_set_tint(model, wgr_color_rgba(255, 128, 0, 128));

    int count = 0;
    wgr_render_begin();
    wgr_model_draw(model);
    wgr_model_flush();
    const float *records = wgr_model_instance_records(&count);
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
        CHECK_NEAR(records[25], wgr_srgb_to_linear(128.0f / 255.0f), 1e-4f);
        CHECK_NEAR(records[26], 0.0f, 1e-4f);
        CHECK_NEAR(records[27], 128.0f / 255.0f, 1e-3f);
        CHECK(records[28] == 0.0f); /* not skinned: no joints of its own */
    }
    wgr_render_end();

    wgr_model_destroy(model);
    wgr_camera3d_destroy(camera);
    wgr_model_deinit();
    wgr_environment_deinit();
    wgr_material_deinit();
    wgr_light_deinit();
    wgr_texture_deinit();
    wgr_camera3d_deinit();
    wgr_render_deinit();
    sg_shutdown();
}

/* Models that agree on everything but their placement go up as one draw
 * (docs/PLAN-instancing.md, phase 2). */
void test_model_instancing(void)
{
    sg_setup(&(sg_desc){.environment = wgr_platform_environment()});
    wgr_render_init();
    wgr_scene_init();
    wgr_camera3d_init();
    wgr_texture_init();
    wgr_light_init();
    wgr_material_init();
    wgr_environment_init();
    wgr_shader_init();
    wgr_model_init();
    stm_setup(); /* custom shaders read the time (wgr_get_time), which wgr_run starts */

    const wgr_handle_t scene = wgr_scene_create();
    const wgr_handle_t camera = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE);
    wgr_camera3d_set_view(camera, 0, 2, 20, 0, 0, 0, 0, 1, 0);
    wgr_scene_set_active_camera(scene, camera);

    const wgr_handle_t mesh = wgr_mesh_create_cube(1.0f, 1.0f, 1.0f);
    const wgr_handle_t material = wgr_material_create(WGR_MATERIAL_PBR);
    wgr_handle_t models[8];
    for (int i = 0; i < 8; i++) {
        models[i] = wgr_model_create(mesh);
        wgr_model_set_material(models[i], -1, material);
        wgr_model_set_transform(models[i], (float)i - 4.0f, 0, 0, 0, 0, 0, 1, 1, 1);
        wgr_scene_add(scene, models[i], 0);
    }
    wgr_mesh_release(mesh);
    wgr_material_release(material);

    /* one mesh, one material, eight placements: one draw */
    wgr_render_begin();
    wgr_scene_draw(scene);
    wgr_render_end();
    CHECK(wgr_model_draw_call_count() == 1);

    /* a different material splits it in two, wherever the models sit in the scene */
    const wgr_handle_t other = wgr_material_create(WGR_MATERIAL_PBR);
    wgr_model_set_material(models[3], -1, other);
    wgr_render_begin();
    wgr_scene_draw(scene);
    wgr_render_end();
    CHECK(wgr_model_draw_call_count() == 2);
    wgr_model_set_material(models[3], -1, material);
    wgr_material_release(other);

    /* so does a different mesh */
    const wgr_handle_t sphere = wgr_mesh_create_sphere(0.5f, 8, 8);
    wgr_model_set_mesh(models[5], sphere);
    wgr_model_set_material(models[5], -1, material);
    wgr_mesh_release(sphere);
    wgr_render_begin();
    wgr_scene_draw(scene);
    wgr_render_end();
    CHECK(wgr_model_draw_call_count() == 2);

    /* and a tint does not: that is what the instance record is for */
    wgr_model_set_mesh(models[5], mesh);
    wgr_model_set_material(models[5], -1, material);
    for (int i = 0; i < 8; i++) {
        wgr_model_set_tint(models[i], wgr_color_rgba(255, i * 30, 0, 255));
    }
    wgr_render_begin();
    wgr_scene_draw(scene);
    wgr_render_end();
    CHECK(wgr_model_draw_call_count() == 1);

    /* a custom material shader batches like the built-in one: it reads each placement
       from the same records (docs/PLAN-instancing.md, phase 4) */
    const wgr_handle_t custom = wgr_material_create_custom(wgr_shader_create("../examples/assets/shaders/toon.wgrshader"));
    CHECK(custom != 0);
    for (int i = 0; i < 8; i++) {
        wgr_model_set_material(models[i], -1, custom);
    }
    wgr_render_begin();
    wgr_scene_draw(scene);
    wgr_render_end();
    CHECK(wgr_model_draw_call_count() == 1);
    for (int i = 0; i < 8; i++) { /* back to the built-in one */
        wgr_model_set_material(models[i], -1, material);
    }
    wgr_material_release(custom);

    /* skinned models sharing a mesh group as well: each instance's record says where
       its own joint matrices are, so two walkers out of step are still one draw per
       primitive of the mesh */
    const wgr_handle_t gumshoe = wgr_mesh_create("../examples/assets/models/gumshoe/gumshoe.glb");
    const wgr_handle_t walker_a = wgr_model_create(gumshoe), walker_b = wgr_model_create(gumshoe);
    CHECK(gumshoe != 0 && walker_a != 0 && walker_b != 0);
    wgr_mesh_release(gumshoe);
    wgr_model_set_transform(walker_a, -1.0f, 0, 2.0f, 0, 0, 0, 1, 1, 1);
    wgr_model_set_transform(walker_b, 1.0f, 0, 2.0f, 0, 0, 0, 1, 1, 1);
    wgr_model_set_animation(walker_a, 0);
    wgr_model_set_animation(walker_b, 0);
    wgr_model_set_animation_time(walker_b, 0.4f);
    wgr_scene_add(scene, walker_a, 0);
    wgr_scene_add(scene, walker_b, 0);
    {
        int placements = 0, primitives = 0, records = 0, distinct_bases = 0;
        float bases[8] = {0};
        wgr_render_begin();
        wgr_scene_draw(scene);
        wgr_model_queue_counts(&placements, &primitives, NULL);
        wgr_model_flush();
        const float *record = wgr_model_instance_records(&records);
        /* the walkers' records carry their own joint bases: walker_a's are at 0 and
           walker_b's after them, so at least two different values show up */
        for (int i = 0; record != NULL && i < records; i++) {
            const float base = record[i * 32 + 28];
            int seen = 0;
            for (int k = 0; k < distinct_bases; k++) seen |= bases[k] == base;
            if (!seen && distinct_bases < 8) bases[distinct_bases++] = base;
        }
        wgr_render_end();
        /* the gumshoe is an opaque part and a see-through one, so each walker is two
           primitives (and two placements: one per pass it appears in) */
        const int walker_prims = (primitives - 8) / 2;
        CHECK(placements >= 10 && walker_prims >= 1 && (primitives - 8) % 2 == 0);
        CHECK(distinct_bases >= 2);
        /* one draw for the cubes, then one per kind of walker primitive: the two
           walkers share each of them */
        CHECK(wgr_model_draw_call_count() == 1 + walker_prims);
    }
    wgr_model_destroy(walker_a);
    wgr_model_destroy(walker_b);

    for (int i = 0; i < 8; i++) {
        wgr_model_destroy(models[i]);
    }
    wgr_scene_destroy(scene);
    wgr_model_deinit();
    wgr_shader_deinit();
    wgr_environment_deinit();
    wgr_material_deinit();
    wgr_light_deinit();
    wgr_texture_deinit();
    wgr_camera3d_deinit();
    wgr_scene_deinit();
    wgr_render_deinit();
    sg_shutdown();
}

/* How many model placements a scene draw queued. */
static int queued(wgr_handle_t scene)
{
    int placements = 0;
    wgr_render_begin();
    wgr_scene_draw(scene);
    wgr_model_queue_counts(&placements, NULL, NULL);
    wgr_render_end();
    return placements;
}

void test_cull_scene(void)
{
    sg_setup(&(sg_desc){.environment = wgr_platform_environment()});
    wgr_render_init();
    wgr_scene_init();
    wgr_camera3d_init();
    wgr_texture_init();
    wgr_light_init();
    wgr_material_init();
    wgr_environment_init();
    wgr_model_init();

    const wgr_handle_t scene = wgr_scene_create();
    const wgr_handle_t camera = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE);
    wgr_camera3d_set_view(camera, 0, 0, 10, 0, 0, 0, 0, 1, 0);
    wgr_scene_set_active_camera(scene, camera);
    CHECK(wgr_scene_is_culling(scene)); /* on unless a scene says otherwise */

    const wgr_handle_t mesh = wgr_mesh_create_cube(1.0f, 1.0f, 1.0f);
    const wgr_handle_t seen = wgr_model_create(mesh);
    const wgr_handle_t away = wgr_model_create(mesh);
    wgr_mesh_release(mesh);
    wgr_scene_add(scene, seen, 0);
    wgr_scene_add(scene, away, 0);
    wgr_model_set_transform(seen, 0, 0, 0, 0, 0, 0, 1, 1, 1);
    wgr_model_set_transform(away, 0, 0, 400, 0, 0, 0, 1, 1, 1); /* behind the camera */

    /* only what the camera can see is submitted */
    CHECK(queued(scene) == 1);

    /* moved into view, it is */
    wgr_model_set_transform(away, 2, 0, 0, 0, 0, 0, 1, 1, 1);
    CHECK(queued(scene) == 2);
    wgr_model_set_transform(away, 0, 0, 400, 0, 0, 0, 1, 1, 1);
    CHECK(queued(scene) == 1);

    /* with the switch off, everything is submitted as it used to be */
    CHECK(wgr_scene_set_culling(scene, false));
    CHECK(!wgr_scene_is_culling(scene));
    CHECK(queued(scene) == 2);
    CHECK(wgr_scene_set_culling(scene, true));
    CHECK(queued(scene) == 1);

    /* a caster the camera can't see is kept when its shadow could reach the view: the
       sun points from the far model toward the origin, so it shadows what is seen */
    const wgr_handle_t sun = wgr_light_create(WGR_LIGHT_DIRECTIONAL);
    wgr_light_set_direction(sun, 0, 0, -1);
    wgr_light_set_shadow_distance(sun, 500.0f);
    wgr_scene_add(scene, sun, 0);
    CHECK(queued(scene) == 1); /* nothing casts yet */
    CHECK(wgr_light_set_casts_shadows(sun, true));
    CHECK(queued(scene) == 2); /* now the far one is drawn, for its shadow */

    /* unless it is told not to cast, when it is culled again */
    CHECK(wgr_model_set_casts_shadow(away, false));
    CHECK(queued(scene) == 1);
    CHECK(wgr_model_set_casts_shadow(away, true));

    /* or unless the light's shadows don't reach that far */
    CHECK(wgr_light_set_shadow_distance(sun, 10.0f));
    CHECK(queued(scene) == 1);

    wgr_light_destroy(sun);
    wgr_model_destroy(away);
    wgr_model_destroy(seen);
    wgr_scene_destroy(scene);
    wgr_model_deinit();
    wgr_environment_deinit();
    wgr_material_deinit();
    wgr_light_deinit();
    wgr_texture_deinit();
    wgr_camera3d_deinit();
    wgr_scene_deinit();
    wgr_render_deinit();
    sg_shutdown();
}
