/* Shadows (docs/PLAN-shadows.md): the state a light and a model carry, and the fit
 * that decides what a directional light's map covers. The depth pass itself needs a
 * GPU to say anything about pixels, but what it draws — the casters queued for a
 * lighting environment — is checked here on the dummy backend. */
#include <math.h>
#include <string.h>

#include "internal/sk_camera3d.h"
#include "internal/sk_environment.h"
#include "internal/sk_internal.h"
#include "internal/sk_light.h"
#include "internal/sk_material.h"
#include "internal/sk_model.h"
#include "internal/sk_platform.h"
#include "internal/sk_render.h"
#include "internal/sk_scene.h"
#include "internal/sk_shadow.h"
#include "internal/sk_texture.h"
#include "sk_camera3d.h"
#include "sk_color.h"
#include "sk_light.h"
#include "sk_logger.h"
#include "sk_model.h"
#include "sk_render.h"
#include "sk_scene.h"
#include "test.h"
#include "tests.h"

#include "sokol_gfx.h"

#define EPS 1e-4f

static void begin(void)
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
    sk_shadow_init();
}

static void end(void)
{
    sk_shadow_deinit();
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

void test_shadow_state(void)
{
    begin();
    sk_logger_set_level(SK_LOGGER_LEVEL_FATAL); /* the refusals below log on purpose */

    const sk_handle_t sun = sk_light_create(SK_LIGHT_DIRECTIONAL);
    CHECK(!sk_light_get_casts_shadows(sun)); /* off until asked: a map costs a pass */
    CHECK(sk_light_set_casts_shadows(sun, true));
    CHECK(sk_light_get_casts_shadows(sun));
    CHECK(sk_light_set_casts_shadows(sun, false));
    CHECK(!sk_light_get_casts_shadows(sun));

    /* spot lights cast too; point lights would need six maps, so they still don't */
    const sk_handle_t spot = sk_light_create(SK_LIGHT_SPOT);
    const sk_handle_t point = sk_light_create(SK_LIGHT_POINT);
    CHECK(sk_light_set_casts_shadows(spot, true));
    CHECK(sk_light_get_casts_shadows(spot));
    CHECK(!sk_light_set_casts_shadows(point, true));
    CHECK(!sk_light_get_casts_shadows(point));
    CHECK(sk_light_set_casts_shadows(point, false)); /* turning it off is always fine */

    /* the settings take sensible values and refuse silly ones */
    CHECK(sk_light_set_shadow_distance(sun, 25.0f));
    CHECK(!sk_light_set_shadow_distance(sun, 0.0f));
    CHECK(!sk_light_set_shadow_distance(sun, -5.0f));
    CHECK(sk_light_set_shadow_map_size(sun, 1024));
    CHECK(!sk_light_set_shadow_map_size(sun, 64)); /* below the smallest map */
    CHECK(sk_light_set_shadow_bias(sun, 2.0f, 6.0f)); /* in shadow texels */
    CHECK(!sk_light_set_shadow_bias(sun, -2.0f, 6.0f));
    CHECK(!sk_light_set_shadow_bias(sun, 2.0f, -6.0f));

    /* how much light a shadow takes away, and what it leaves behind */
    CHECK(sk_light_set_shadow_strength(sun, 0.5f));
    CHECK(sk_light_set_shadow_strength(sun, 0.0f) && sk_light_set_shadow_strength(sun, 1.0f));
    CHECK(!sk_light_set_shadow_strength(sun, -0.1f));
    CHECK(!sk_light_set_shadow_strength(sun, 1.5f));
    CHECK(sk_light_set_shadow_color(sun, sk_color_rgba(30, 60, 100, 255)));
    CHECK(sk_light_set_shadow_color(sun, SK_COLOR_BLACK));

    /* and nothing works on a handle that isn't a light */
    CHECK(!sk_light_set_casts_shadows(0, true));
    CHECK(!sk_light_get_casts_shadows(0));
    CHECK(!sk_light_set_shadow_distance(0, 10.0f));
    CHECK(!sk_light_set_shadow_map_size(0, 1024));
    CHECK(!sk_light_set_shadow_bias(0, 1.0f, 1.0f));
    CHECK(!sk_light_set_shadow_strength(0, 0.5f));
    CHECK(!sk_light_set_shadow_color(0, SK_COLOR_BLACK));

    /* a model casts and receives until it's told otherwise */
    const sk_handle_t model = sk_model_create(0);
    CHECK(sk_model_casts_shadow(model) && sk_model_receives_shadow(model));
    CHECK(sk_model_set_casts_shadow(model, false));
    CHECK(!sk_model_casts_shadow(model) && sk_model_receives_shadow(model));
    CHECK(sk_model_set_receives_shadow(model, false));
    CHECK(!sk_model_receives_shadow(model));
    CHECK(sk_model_set_casts_shadow(model, true) && sk_model_set_receives_shadow(model, true));
    CHECK(!sk_model_set_casts_shadow(0, true) && !sk_model_casts_shadow(0));

    sk_model_destroy(model);
    sk_light_destroy(point);
    sk_light_destroy(spot);
    sk_light_destroy(sun);
    sk_logger_set_level(SK_LOGGER_LEVEL_INFO);
    end();
}

/* A point through the light's matrix, in its clip space. */
static vec3_t to_light_clip(const sk_mat4_t *view_proj, vec3_t world)
{
    const sk_mat4_t m = *view_proj;
    const float x = m.m[0] * world.x + m.m[4] * world.y + m.m[8] * world.z + m.m[12];
    const float y = m.m[1] * world.x + m.m[5] * world.y + m.m[9] * world.z + m.m[13];
    const float z = m.m[2] * world.x + m.m[6] * world.y + m.m[10] * world.z + m.m[14];
    const float w = m.m[3] * world.x + m.m[7] * world.y + m.m[11] * world.z + m.m[15];
    return (vec3_t){x / w, y / w, z / w};
}

void test_shadow_fit(void)
{
    sk_camera3d_t cam = {
        .position = {0.0f, 4.0f, 10.0f},
        .target = {0.0f, 0.0f, 0.0f},
        .up = {0.0f, 1.0f, 0.0f},
        .fov = 60.0f * 3.14159265f / 180.0f,
        .ortho_height = 10.0f,
        .projection = SK_CAMERA3D_PERSPECTIVE,
    };
    const vec3_t straight_down = {0.0f, -1.0f, 0.0f};

    /* the depth range follows the backend: GL clips -1..1, WebGPU 0..1 */
    const sk_mat4_t gl = sk_shadow_ortho(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 10.0f, false);
    const sk_mat4_t wgpu = sk_shadow_ortho(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 10.0f, true);
    CHECK_NEAR(to_light_clip(&gl, (vec3_t){0, 0, 0}).z, -1.0f, EPS);   /* the near plane */
    CHECK_NEAR(to_light_clip(&gl, (vec3_t){0, 0, -10}).z, 1.0f, EPS);  /* and the far one */
    CHECK_NEAR(to_light_clip(&wgpu, (vec3_t){0, 0, 0}).z, 0.0f, EPS);
    CHECK_NEAR(to_light_clip(&wgpu, (vec3_t){0, 0, -10}).z, 1.0f, EPS);

    /* what the camera looks at is inside what the light's map covers */
    const sk_shadow_fit_t fit = sk_shadow_fit_directional(&cam, 16.0f / 9.0f, straight_down, 30.0f, 1024, 50.0f, false);
    const vec3_t middle = to_light_clip(&fit.view_proj, (vec3_t){0.0f, 0.0f, 0.0f});
    CHECK(middle.x > -1.0f && middle.x < 1.0f);
    CHECK(middle.y > -1.0f && middle.y < 1.0f);
    CHECK(middle.z > -1.0f && middle.z < 1.0f);
    CHECK(fit.texel_world > 0.0f);
    CHECK(fit.depth_range > 30.0f); /* the slice, plus the pull-back for casters behind it */

    /* a point far outside the camera's view isn't covered */
    const vec3_t far_away = to_light_clip(&fit.view_proj, (vec3_t){500.0f, 0.0f, 0.0f});
    CHECK(far_away.x < -1.0f || far_away.x > 1.0f);

    /* a bigger map over the same ground means smaller texels */
    const sk_shadow_fit_t coarse = sk_shadow_fit_directional(&cam, 16.0f / 9.0f, straight_down, 30.0f, 512, 50.0f, false);
    const sk_shadow_fit_t fine = sk_shadow_fit_directional(&cam, 16.0f / 9.0f, straight_down, 30.0f, 4096, 50.0f, false);
    CHECK(fine.texel_world < coarse.texel_world);
    CHECK_NEAR(coarse.texel_world / fine.texel_world, 8.0f, 0.001f);

    /* less distance covers less ground, so its texels are smaller again */
    const sk_shadow_fit_t near_fit = sk_shadow_fit_directional(&cam, 16.0f / 9.0f, straight_down, 10.0f, 1024, 50.0f,
                                                               false);
    CHECK(near_fit.texel_world < fit.texel_world);

    /* the fit is snapped to whole texels: nudging the camera by less than one doesn't
       move the map, which is what keeps a shadow's edge from crawling */
    sk_camera3d_t nudged = cam;
    nudged.position.x += fit.texel_world * 0.1f;
    nudged.target.x += fit.texel_world * 0.1f;
    const sk_shadow_fit_t shifted = sk_shadow_fit_directional(&nudged, 16.0f / 9.0f, straight_down, 30.0f, 1024, 50.0f,
                                                              false);
    const vec3_t before = to_light_clip(&fit.view_proj, (vec3_t){1.0f, 0.0f, 1.0f});
    const vec3_t after = to_light_clip(&shifted.view_proj, (vec3_t){1.0f, 0.0f, 1.0f});
    CHECK_NEAR(after.x, before.x, 1e-3f);
    CHECK_NEAR(after.y, before.y, 1e-3f);

    /* a light pointing straight down has no obvious "up": the fit still works */
    const sk_shadow_fit_t sideways = sk_shadow_fit_directional(&cam, 1.0f, (vec3_t){1.0f, -0.2f, 0.3f}, 20.0f, 1024,
                                                               50.0f, false);
    const vec3_t seen = to_light_clip(&sideways.view_proj, (vec3_t){0.0f, 0.0f, 0.0f});
    CHECK(seen.x > -1.0f && seen.x < 1.0f && seen.y > -1.0f && seen.y < 1.0f);

    /* nonsense in, something sane out */
    const sk_shadow_fit_t degenerate = sk_shadow_fit_directional(&cam, 1.0f, (vec3_t){0, 0, 0}, -5.0f, 0, 0.0f, false);
    CHECK(degenerate.texel_world > 0.0f);
    CHECK(degenerate.depth_range > 0.0f);
}

/* A spot light's map covers its own cone, from where it stands. */
void test_shadow_fit_spot(void)
{
    const vec3_t at = {0.0f, 6.0f, 0.0f};
    const vec3_t down = {0.0f, -1.0f, 0.0f};
    const float cos_outer = cosf(0.5f); /* a 0.5 rad half-angle cone */

    const sk_shadow_fit_t fit = sk_shadow_fit_spot(at, down, cos_outer, 20.0f, 0.2f, 1024, false);
    /* straight below the lamp is the middle of its map */
    const vec3_t under = to_light_clip(&fit.view_proj, (vec3_t){0.0f, 0.0f, 0.0f});
    CHECK_NEAR(under.x, 0.0f, 1e-3f);
    CHECK_NEAR(under.y, 0.0f, 1e-3f);
    CHECK(under.z > -1.0f && under.z < 1.0f);

    /* a point inside the cone is covered, one well outside it isn't */
    const vec3_t inside = to_light_clip(&fit.view_proj, (vec3_t){1.0f, 0.0f, 0.0f});
    CHECK(inside.x > -1.0f && inside.x < 1.0f && inside.y > -1.0f && inside.y < 1.0f);
    const vec3_t outside = to_light_clip(&fit.view_proj, (vec3_t){20.0f, 0.0f, 0.0f});
    CHECK(outside.x < -1.0f || outside.x > 1.0f);

    /* behind the lamp is behind the projection, which the shader treats as unlit */
    const vec3_t behind = to_light_clip(&fit.view_proj, (vec3_t){0.0f, 12.0f, 0.0f});
    CHECK(behind.z < 0.0f || behind.z > 1.0f);

    /* the depth range is how far it reaches, and its texels grow with the cone */
    CHECK_NEAR(fit.depth_range, 20.0f, 1e-3f);
    const sk_shadow_fit_t wide = sk_shadow_fit_spot(at, down, cosf(0.9f), 20.0f, 0.2f, 1024, false);
    CHECK(wide.texel_world > fit.texel_world);
    const sk_shadow_fit_t sharper = sk_shadow_fit_spot(at, down, cos_outer, 20.0f, 0.2f, 4096, false);
    CHECK(sharper.texel_world < fit.texel_world);

    /* WebGPU's depth range, and nonsense in, something sane out */
    const sk_shadow_fit_t wgpu = sk_shadow_fit_spot(at, down, cos_outer, 20.0f, 0.2f, 1024, true);
    const vec3_t near_wgpu = to_light_clip(&wgpu.view_proj, (vec3_t){0.0f, 5.0f, 0.0f});
    CHECK(near_wgpu.z >= 0.0f && near_wgpu.z <= 1.0f);
    const sk_shadow_fit_t silly = sk_shadow_fit_spot(at, (vec3_t){0, 0, 0}, 2.0f, -1.0f, -1.0f, 0, false);
    CHECK(silly.texel_world > 0.0f && silly.depth_range > 0.0f);
}

/* The scene picks the casting light, and the casters are the models queued for it. */
void test_shadow_casters(void)
{
    begin();

    const sk_handle_t scene = sk_scene_create();
    const sk_handle_t camera = sk_camera3d_create(SK_CAMERA3D_PERSPECTIVE);
    sk_camera3d_set_view(camera, 0, 4, 10, 0, 0, 0, 0, 1, 0);
    sk_scene_set_active_camera(scene, camera);

    const sk_handle_t plain = sk_light_create(SK_LIGHT_DIRECTIONAL);
    const sk_handle_t sun = sk_light_create(SK_LIGHT_DIRECTIONAL);
    sk_light_set_direction(sun, -0.5f, -1.0f, -0.3f);
    sk_scene_add(scene, plain, 0);
    sk_scene_add(scene, sun, 0);

    const sk_handle_t mesh = sk_mesh_create_cube(1.0f, 1.0f, 1.0f);
    const sk_handle_t model = sk_model_create(mesh);
    sk_mesh_release(mesh);
    sk_scene_add(scene, model, 0);

    /* nothing casts yet, so no environment asks for a map */
    sk_render_begin();
    sk_scene_draw(scene);
    const sk_light_env_t *env = sk_light_env_get(0);
    CHECK(env != NULL && env->count == 2);
    CHECK(env->shadow_count == 0);
    sk_render_end();

    /* with the second light casting, that's the one the scene points at */
    CHECK(sk_light_set_casts_shadows(sun, true));
    sk_render_begin();
    sk_scene_draw(scene);
    env = sk_light_env_get(0);
    CHECK(env != NULL && env->shadow_count == 1 && env->shadow_lights[0] == 1);
    CHECK(env->lights[env->shadow_lights[0]].casts_shadows);
    CHECK(sk_model_has_shadow_casters(0)); /* the cube is queued for it */
    sk_render_end();

    /* several casting lights each get a slot, in the order the scene found them */
    CHECK(sk_light_set_casts_shadows(plain, true));
    sk_render_begin();
    sk_scene_draw(scene);
    env = sk_light_env_get(0);
    CHECK(env != NULL && env->shadow_count == 2);
    CHECK(env->shadow_lights[0] == 0 && env->shadow_lights[1] == 1);
    sk_render_end();
    CHECK(sk_light_set_casts_shadows(plain, false));

    /* a map nothing samples is a pass for nothing: models say whether they receive,
       and the shadow module asks before drawing one */
    sk_render_begin();
    sk_scene_draw(scene);
    CHECK(sk_model_has_shadow_receivers(0));
    sk_render_end();
    CHECK(sk_model_set_receives_shadow(model, false));
    sk_render_begin();
    sk_scene_draw(scene);
    CHECK(sk_model_has_shadow_casters(0));    /* it still casts */
    CHECK(!sk_model_has_shadow_receivers(0)); /* but nothing is darkened by the map */
    sk_render_end();
    CHECK(sk_model_set_receives_shadow(model, true));
    CHECK(!sk_model_has_shadow_receivers(1)); /* nor in an environment with nothing in it */

    /* a model that doesn't cast isn't drawn into the map, and with no casters at all
       there's nothing to draw */
    CHECK(sk_model_set_casts_shadow(model, false));
    sk_render_begin();
    sk_scene_draw(scene);
    CHECK(!sk_model_has_shadow_casters(0));
    sk_render_end();
    CHECK(sk_model_set_casts_shadow(model, true));

    /* nor is a hidden one */
    CHECK(sk_model_set_visible(model, false));
    sk_render_begin();
    sk_scene_draw(scene);
    CHECK(!sk_model_has_shadow_casters(0));
    sk_render_end();
    CHECK(sk_model_set_visible(model, true));

    /* at most SK_MAX_SHADOW_LIGHTS cast at once; the rest light without shadows */
    sk_handle_t extra[SK_MAX_SHADOW_LIGHTS + 2];
    for (int i = 0; i < SK_MAX_SHADOW_LIGHTS + 2; i++) {
        extra[i] = sk_light_create(SK_LIGHT_DIRECTIONAL);
        CHECK(sk_light_set_casts_shadows(extra[i], true));
        sk_scene_add(scene, extra[i], 0);
    }
    sk_render_begin();
    sk_scene_draw(scene);
    env = sk_light_env_get(0);
    CHECK(env != NULL && env->shadow_count == SK_MAX_SHADOW_LIGHTS);
    for (int i = 0; i < env->shadow_count; i++) { /* each slot is a distinct light */
        CHECK(env->lights[env->shadow_lights[i]].casts_shadows);
        for (int j = 0; j < i; j++) CHECK(env->shadow_lights[i] != env->shadow_lights[j]);
    }
    sk_render_end();
    for (int i = 0; i < SK_MAX_SHADOW_LIGHTS + 2; i++) {
        sk_scene_remove(scene, extra[i]);
        sk_light_destroy(extra[i]);
    }

    /* a lighting environment nothing was queued for has no casters either */
    sk_render_begin();
    sk_scene_draw(scene);
    CHECK(sk_model_has_shadow_casters(0));
    CHECK(!sk_model_has_shadow_casters(1));
    CHECK(!sk_model_has_shadow_casters(-1));
    sk_render_end();

    sk_model_destroy(model);
    sk_light_destroy(sun);
    sk_light_destroy(plain);
    sk_scene_destroy(scene);
    end();
}
