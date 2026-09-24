/* Shadows (docs/PLAN-shadows.md): the state a light and a model carry, and the fit
 * that decides what a directional light's map covers. The depth pass itself needs a
 * GPU to say anything about pixels, but what it draws — the casters queued for a
 * lighting environment — is checked here on the dummy backend. */
#include <math.h>
#include <string.h>

#include "internal/wgr_camera3d_internal.h"
#include "internal/wgr_environment_internal.h"
#include "internal/wgr_internal_internal.h"
#include "internal/wgr_light_internal.h"
#include "internal/wgr_material_internal.h"
#include "internal/wgr_model_internal.h"
#include "internal/wgr_platform_internal.h"
#include "internal/wgr_render_internal.h"
#include "internal/wgr_scene_internal.h"
#include "internal/wgr_shadow_internal.h"
#include "internal/wgr_texture_internal.h"
#include "wgr_camera3d.h"
#include "wgr_color.h"
#include "wgr_light.h"
#include "wgr_logger.h"
#include "wgr_model.h"
#include "wgr_render.h"
#include "wgr_scene.h"
#include "test.h"
#include "tests.h"

#include "sokol_gfx.h"

#define EPS 1e-4f

static void begin(void)
{
    sg_setup(&(sg_desc){.environment = wgri_platform_environment()});
    wgri_render_init();
    wgri_scene_init();
    wgri_camera3d_init();
    wgri_texture_init();
    wgri_light_init();
    wgri_material_init();
    wgri_environment_init();
    wgri_model_init();
    wgri_shadow_init();
}

static void end(void)
{
    wgri_shadow_deinit();
    wgri_model_deinit();
    wgri_environment_deinit();
    wgri_material_deinit();
    wgri_light_deinit();
    wgri_texture_deinit();
    wgri_camera3d_deinit();
    wgri_scene_deinit();
    wgri_render_deinit();
    sg_shutdown();
}

void test_shadow_state(void)
{
    begin();
    wgr_logger_set_level(WGR_LOGGER_LEVEL_FATAL); /* the refusals below log on purpose */

    const wgr_handle_t sun = wgr_light_create(WGR_LIGHT_DIRECTIONAL);
    CHECK(!wgr_light_get_casts_shadows(sun)); /* off until asked: a map costs a pass */
    CHECK(wgr_light_set_casts_shadows(sun, true));
    CHECK(wgr_light_get_casts_shadows(sun));
    CHECK(wgr_light_set_casts_shadows(sun, false));
    CHECK(!wgr_light_get_casts_shadows(sun));

    /* spot lights cast too; point lights would need six maps, so they still don't */
    const wgr_handle_t spot = wgr_light_create(WGR_LIGHT_SPOT);
    const wgr_handle_t point = wgr_light_create(WGR_LIGHT_POINT);
    CHECK(wgr_light_set_casts_shadows(spot, true));
    CHECK(wgr_light_get_casts_shadows(spot));
    CHECK(!wgr_light_set_casts_shadows(point, true));
    CHECK(!wgr_light_get_casts_shadows(point));
    CHECK(wgr_light_set_casts_shadows(point, false)); /* turning it off is always fine */

    /* the settings take sensible values and refuse silly ones */
    CHECK(wgr_light_set_shadow_distance(sun, 25.0f));
    CHECK(!wgr_light_set_shadow_distance(sun, 0.0f));
    CHECK(!wgr_light_set_shadow_distance(sun, -5.0f));
    CHECK(wgr_light_set_shadow_map_size(sun, 1024));
    CHECK(wgr_light_set_shadow_map_size(sun, 64));   /* a fidelity: clamped up to 256, not refused */
    CHECK(wgr_light_set_shadow_map_size(sun, 9000)); /* and down to 4096 */
    CHECK(!wgr_light_set_shadow_map_size(sun, 0));   /* a size of nothing means nothing */
    CHECK(!wgr_light_set_shadow_map_size(sun, -256));
    CHECK(wgr_light_set_shadow_bias(sun, 2.0f, 6.0f)); /* in shadow texels */
    CHECK(!wgr_light_set_shadow_bias(sun, -2.0f, 6.0f));
    CHECK(!wgr_light_set_shadow_bias(sun, 2.0f, -6.0f));

    /* how much light a shadow takes away, and what it leaves behind */
    CHECK(wgr_light_set_shadow_strength(sun, 0.5f));
    CHECK(wgr_light_set_shadow_strength(sun, 0.0f) && wgr_light_set_shadow_strength(sun, 1.0f));
    CHECK(wgr_light_set_shadow_strength(sun, -0.1f)); /* a fraction: clamped, not refused */
    CHECK(wgr_light_set_shadow_strength(sun, 1.5f));
    /* the getters are how a caller learns what a clamp did */
    CHECK_NEAR(wgr_light_get_shadow_strength(sun), 1.0f, 1e-6f);
    CHECK(wgr_light_set_shadow_map_size(sun, 64) && wgr_light_get_shadow_map_size(sun) == 256);
    CHECK(wgr_light_set_shadow_map_size(sun, 9000) && wgr_light_get_shadow_map_size(sun) == 4096);
    CHECK(wgr_light_set_shadow_map_size(sun, 1500) && wgr_light_get_shadow_map_size(sun) == 1024);
    CHECK(wgr_light_set_shadow_distance(sun, 12.0f) && wgr_light_get_shadow_distance(sun) == 12.0f);
    CHECK(wgr_light_set_shadow_bias(sun, 2.0f, 6.0f) && wgr_light_get_shadow_bias_constant(sun) == 2.0f &&
          wgr_light_get_shadow_bias_slope(sun) == 6.0f);
    CHECK(wgr_light_set_shadow_color(sun, WGR_COLOR_BLUE) && wgr_light_get_shadow_color(sun) == WGR_COLOR_BLUE);
    CHECK(wgr_light_get_shadow_map_size(0) == 0 && wgr_light_get_shadow_strength(0) == 0.0f);
    CHECK(wgr_light_set_shadow_color(sun, wgr_color_rgba(30, 60, 100, 255)));
    CHECK(wgr_light_set_shadow_color(sun, WGR_COLOR_BLACK));

    /* and nothing works on a handle that isn't a light */
    CHECK(!wgr_light_set_casts_shadows(0, true));
    CHECK(!wgr_light_get_casts_shadows(0));
    CHECK(!wgr_light_set_shadow_distance(0, 10.0f));
    CHECK(!wgr_light_set_shadow_map_size(0, 1024));
    CHECK(!wgr_light_set_shadow_bias(0, 1.0f, 1.0f));
    CHECK(!wgr_light_set_shadow_strength(0, 0.5f));
    CHECK(!wgr_light_set_shadow_color(0, WGR_COLOR_BLACK));

    /* a model casts and receives until it's told otherwise */
    const wgr_handle_t model = wgr_model_create(0);
    CHECK(wgr_model_casts_shadow(model) && wgr_model_receives_shadow(model));
    CHECK(wgr_model_set_casts_shadow(model, false));
    CHECK(!wgr_model_casts_shadow(model) && wgr_model_receives_shadow(model));
    CHECK(wgr_model_set_receives_shadow(model, false));
    CHECK(!wgr_model_receives_shadow(model));
    CHECK(wgr_model_set_casts_shadow(model, true) && wgr_model_set_receives_shadow(model, true));
    CHECK(!wgr_model_set_casts_shadow(0, true) && !wgr_model_casts_shadow(0));

    wgr_model_destroy(model);
    wgr_light_destroy(point);
    wgr_light_destroy(spot);
    wgr_light_destroy(sun);
    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    end();
}

/* A point through the light's matrix, in its clip space. */
static vec3_t to_light_clip(const wgri_mat4_t *view_proj, vec3_t world)
{
    const wgri_mat4_t m = *view_proj;
    const float x = m.m[0] * world.x + m.m[4] * world.y + m.m[8] * world.z + m.m[12];
    const float y = m.m[1] * world.x + m.m[5] * world.y + m.m[9] * world.z + m.m[13];
    const float z = m.m[2] * world.x + m.m[6] * world.y + m.m[10] * world.z + m.m[14];
    const float w = m.m[3] * world.x + m.m[7] * world.y + m.m[11] * world.z + m.m[15];
    return (vec3_t){x / w, y / w, z / w};
}

void test_shadow_fit(void)
{
    wgri_camera3d_t cam = {
        .position = {0.0f, 4.0f, 10.0f},
        .target = {0.0f, 0.0f, 0.0f},
        .up = {0.0f, 1.0f, 0.0f},
        .fov = 60.0f * 3.14159265f / 180.0f,
        .ortho_height = 10.0f,
        .projection = WGR_CAMERA3D_PERSPECTIVE,
    };
    const vec3_t straight_down = {0.0f, -1.0f, 0.0f};

    /* the depth range follows the backend: GL clips -1..1, WebGPU 0..1 */
    const wgri_mat4_t gl = wgri_shadow_ortho(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 10.0f, false);
    const wgri_mat4_t wgpu = wgri_shadow_ortho(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 10.0f, true);
    CHECK_NEAR(to_light_clip(&gl, (vec3_t){0, 0, 0}).z, -1.0f, EPS);   /* the near plane */
    CHECK_NEAR(to_light_clip(&gl, (vec3_t){0, 0, -10}).z, 1.0f, EPS);  /* and the far one */
    CHECK_NEAR(to_light_clip(&wgpu, (vec3_t){0, 0, 0}).z, 0.0f, EPS);
    CHECK_NEAR(to_light_clip(&wgpu, (vec3_t){0, 0, -10}).z, 1.0f, EPS);

    /* what the camera looks at is inside what the light's map covers */
    const wgri_shadow_fit_t fit = wgri_shadow_fit_directional(&cam, 16.0f / 9.0f, straight_down, 30.0f, 1024, 50.0f, false);
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
    const wgri_shadow_fit_t coarse = wgri_shadow_fit_directional(&cam, 16.0f / 9.0f, straight_down, 30.0f, 512, 50.0f, false);
    const wgri_shadow_fit_t fine = wgri_shadow_fit_directional(&cam, 16.0f / 9.0f, straight_down, 30.0f, 4096, 50.0f, false);
    CHECK(fine.texel_world < coarse.texel_world);
    CHECK_NEAR(coarse.texel_world / fine.texel_world, 8.0f, 0.001f);

    /* less distance covers less ground, so its texels are smaller again */
    const wgri_shadow_fit_t near_fit = wgri_shadow_fit_directional(&cam, 16.0f / 9.0f, straight_down, 10.0f, 1024, 50.0f,
                                                               false);
    CHECK(near_fit.texel_world < fit.texel_world);

    /* the fit is snapped to whole texels: nudging the camera by less than one doesn't
       move the map, which is what keeps a shadow's edge from crawling */
    wgri_camera3d_t nudged = cam;
    nudged.position.x += fit.texel_world * 0.1f;
    nudged.target.x += fit.texel_world * 0.1f;
    const wgri_shadow_fit_t shifted = wgri_shadow_fit_directional(&nudged, 16.0f / 9.0f, straight_down, 30.0f, 1024, 50.0f,
                                                              false);
    const vec3_t before = to_light_clip(&fit.view_proj, (vec3_t){1.0f, 0.0f, 1.0f});
    const vec3_t after = to_light_clip(&shifted.view_proj, (vec3_t){1.0f, 0.0f, 1.0f});
    CHECK_NEAR(after.x, before.x, 1e-3f);
    CHECK_NEAR(after.y, before.y, 1e-3f);

    /* a light pointing straight down has no obvious "up": the fit still works */
    const wgri_shadow_fit_t sideways = wgri_shadow_fit_directional(&cam, 1.0f, (vec3_t){1.0f, -0.2f, 0.3f}, 20.0f, 1024,
                                                               50.0f, false);
    const vec3_t seen = to_light_clip(&sideways.view_proj, (vec3_t){0.0f, 0.0f, 0.0f});
    CHECK(seen.x > -1.0f && seen.x < 1.0f && seen.y > -1.0f && seen.y < 1.0f);

    /* nonsense in, something sane out */
    const wgri_shadow_fit_t degenerate = wgri_shadow_fit_directional(&cam, 1.0f, (vec3_t){0, 0, 0}, -5.0f, 0, 0.0f, false);
    CHECK(degenerate.texel_world > 0.0f);
    CHECK(degenerate.depth_range > 0.0f);
}

/* A spot light's map covers its own cone, from where it stands. */
void test_shadow_fit_spot(void)
{
    const vec3_t at = {0.0f, 6.0f, 0.0f};
    const vec3_t down = {0.0f, -1.0f, 0.0f};
    const float cos_outer = cosf(0.5f); /* a 0.5 rad half-angle cone */

    const wgri_shadow_fit_t fit = wgri_shadow_fit_spot(at, down, cos_outer, 20.0f, 0.2f, 1024, false);
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
    const wgri_shadow_fit_t wide = wgri_shadow_fit_spot(at, down, cosf(0.9f), 20.0f, 0.2f, 1024, false);
    CHECK(wide.texel_world > fit.texel_world);
    const wgri_shadow_fit_t sharper = wgri_shadow_fit_spot(at, down, cos_outer, 20.0f, 0.2f, 4096, false);
    CHECK(sharper.texel_world < fit.texel_world);

    /* WebGPU's depth range, and nonsense in, something sane out */
    const wgri_shadow_fit_t wgpu = wgri_shadow_fit_spot(at, down, cos_outer, 20.0f, 0.2f, 1024, true);
    const vec3_t near_wgpu = to_light_clip(&wgpu.view_proj, (vec3_t){0.0f, 5.0f, 0.0f});
    CHECK(near_wgpu.z >= 0.0f && near_wgpu.z <= 1.0f);
    const wgri_shadow_fit_t silly = wgri_shadow_fit_spot(at, (vec3_t){0, 0, 0}, 2.0f, -1.0f, -1.0f, 0, false);
    CHECK(silly.texel_world > 0.0f && silly.depth_range > 0.0f);
}

/* The scene picks the casting light, and the casters are the models queued for it. */
/* A light's map only covers what its fit reaches, so the depth pass tests each caster
 * against that fit and skips the ones outside (docs/PLAN-culling.md, phase 2). */
void test_shadow_caster_cull(void)
{
    wgri_camera3d_t cam = {
        .position = {0.0f, 2.0f, 0.0f},
        .target = {0.0f, 2.0f, -1.0f},
        .up = {0.0f, 1.0f, 0.0f},
        .fov = 60.0f * 3.14159265f / 180.0f,
        .projection = WGR_CAMERA3D_PERSPECTIVE,
    };
    const vec3_t down = {-0.3f, -1.0f, -0.2f};
    const wgri_shadow_fit_t fit = wgri_shadow_fit_directional(&cam, 1.0f, down, 30.0f, 1024, 50.0f, false);
    wgri_plane_t planes[6];
    wgri_frustum_from_view_proj(fit.view_proj, planes);

    /* what the camera is looking at is in the map */
    CHECK(wgri_frustum_test_aabb(planes, (vec3_t){-1, 0, -12}, (vec3_t){1, 2, -10}));
    /* something a world away from it is not, whichever way it lies */
    CHECK(!wgri_frustum_test_aabb(planes, (vec3_t){499, 0, -1}, (vec3_t){501, 2, 1}));
    CHECK(!wgri_frustum_test_aabb(planes, (vec3_t){-1, 0, -501}, (vec3_t){1, 2, -499}));
    /* but something overhead is: it is between the light and the ground it shades,
       which is what the fit's pull-back is for */
    CHECK(wgri_frustum_test_aabb(planes, (vec3_t){-1, 20, -12}, (vec3_t){1, 22, -10}));

    /* a spot reaches only as far as its range */
    const wgri_shadow_fit_t spot = wgri_shadow_fit_spot((vec3_t){0, 10, 0}, (vec3_t){0, -1, 0}, cosf(0.4f), 20.0f,
                                                    0.2f, 1024, false);
    wgri_frustum_from_view_proj(spot.view_proj, planes);
    CHECK(wgri_frustum_test_aabb(planes, (vec3_t){-1, 4, -1}, (vec3_t){1, 6, 1}));     /* under it */
    CHECK(!wgri_frustum_test_aabb(planes, (vec3_t){39, 4, -1}, (vec3_t){41, 6, 1}));   /* outside the cone */
    CHECK(!wgri_frustum_test_aabb(planes, (vec3_t){-1, -40, -1}, (vec3_t){1, -38, 1})); /* past its range */
}

void test_shadow_casters(void)
{
    begin();

    const wgr_handle_t scene = wgr_scene_create();
    const wgr_handle_t camera = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE);
    wgr_camera3d_set_view(camera, 0, 4, 10, 0, 0, 0, 0, 1, 0);
    wgr_scene_set_active_camera(scene, camera);

    const wgr_handle_t plain = wgr_light_create(WGR_LIGHT_DIRECTIONAL);
    const wgr_handle_t sun = wgr_light_create(WGR_LIGHT_DIRECTIONAL);
    wgr_light_set_direction(sun, -0.5f, -1.0f, -0.3f);
    wgr_scene_add(scene, plain, 0);
    wgr_scene_add(scene, sun, 0);

    const wgr_handle_t mesh = wgr_mesh_create_cube(1.0f, 1.0f, 1.0f);
    const wgr_handle_t model = wgr_model_create(mesh);
    wgr_mesh_release(mesh);
    wgr_scene_add(scene, model, 0);

    /* nothing casts yet, so no environment asks for a map */
    wgr_render_begin_frame();
    wgr_scene_draw(scene);
    const wgri_light_env_t *env = wgri_light_env_get(0);
    CHECK(env != NULL && env->count == 2);
    CHECK(env->shadow_count == 0);
    wgr_render_end_frame();

    /* with the second light casting, that's the one the scene points at */
    CHECK(wgr_light_set_casts_shadows(sun, true));
    wgr_render_begin_frame();
    wgr_scene_draw(scene);
    env = wgri_light_env_get(0);
    CHECK(env != NULL && env->shadow_count == 1 && env->shadow_lights[0] == 1);
    CHECK(env->lights[env->shadow_lights[0]].casts_shadows);
    CHECK(wgri_model_has_shadow_casters(0)); /* the cube is queued for it */
    wgr_render_end_frame();

    /* several casting lights each get a slot, in the order the scene found them */
    CHECK(wgr_light_set_casts_shadows(plain, true));
    wgr_render_begin_frame();
    wgr_scene_draw(scene);
    env = wgri_light_env_get(0);
    CHECK(env != NULL && env->shadow_count == 2);
    CHECK(env->shadow_lights[0] == 0 && env->shadow_lights[1] == 1);
    wgr_render_end_frame();
    CHECK(wgr_light_set_casts_shadows(plain, false));

    /* a map nothing samples is a pass for nothing: models say whether they receive,
       and the shadow module asks before drawing one */
    wgr_render_begin_frame();
    wgr_scene_draw(scene);
    CHECK(wgri_model_has_shadow_receivers(0));
    wgr_render_end_frame();
    CHECK(wgr_model_set_receives_shadow(model, false));
    wgr_render_begin_frame();
    wgr_scene_draw(scene);
    CHECK(wgri_model_has_shadow_casters(0));    /* it still casts */
    CHECK(!wgri_model_has_shadow_receivers(0)); /* but nothing is darkened by the map */
    wgr_render_end_frame();
    CHECK(wgr_model_set_receives_shadow(model, true));
    CHECK(!wgri_model_has_shadow_receivers(1)); /* nor in an environment with nothing in it */

    /* a model that doesn't cast isn't drawn into the map, and with no casters at all
       there's nothing to draw */
    CHECK(wgr_model_set_casts_shadow(model, false));
    wgr_render_begin_frame();
    wgr_scene_draw(scene);
    CHECK(!wgri_model_has_shadow_casters(0));
    wgr_render_end_frame();
    CHECK(wgr_model_set_casts_shadow(model, true));

    /* nor is a hidden one */
    CHECK(wgr_model_set_visible(model, false));
    wgr_render_begin_frame();
    wgr_scene_draw(scene);
    CHECK(!wgri_model_has_shadow_casters(0));
    wgr_render_end_frame();
    CHECK(wgr_model_set_visible(model, true));

    /* at most WGRI_MAX_SHADOW_LIGHTS cast at once; the rest light without shadows */
    wgr_handle_t extra[WGRI_MAX_SHADOW_LIGHTS + 2];
    for (int i = 0; i < WGRI_MAX_SHADOW_LIGHTS + 2; i++) {
        extra[i] = wgr_light_create(WGR_LIGHT_DIRECTIONAL);
        CHECK(wgr_light_set_casts_shadows(extra[i], true));
        wgr_scene_add(scene, extra[i], 0);
    }
    wgr_render_begin_frame();
    wgr_scene_draw(scene);
    env = wgri_light_env_get(0);
    CHECK(env != NULL && env->shadow_count == WGRI_MAX_SHADOW_LIGHTS);
    for (int i = 0; i < env->shadow_count; i++) { /* each slot is a distinct light */
        CHECK(env->lights[env->shadow_lights[i]].casts_shadows);
        for (int j = 0; j < i; j++) CHECK(env->shadow_lights[i] != env->shadow_lights[j]);
    }
    wgr_render_end_frame();
    for (int i = 0; i < WGRI_MAX_SHADOW_LIGHTS + 2; i++) {
        wgr_scene_remove(scene, extra[i]);
        wgr_light_destroy(extra[i]);
    }

    /* a lighting environment nothing was queued for has no casters either */
    wgr_render_begin_frame();
    wgr_scene_draw(scene);
    CHECK(wgri_model_has_shadow_casters(0));
    CHECK(!wgri_model_has_shadow_casters(1));
    CHECK(!wgri_model_has_shadow_casters(-1));
    wgr_render_end_frame();

    wgr_model_destroy(model);
    wgr_light_destroy(sun);
    wgr_light_destroy(plain);
    wgr_scene_destroy(scene);
    end();
}

/* The frame's draw queue grows with the scene instead of dropping work at a fixed
 * size: a thousand models used to be the ceiling, and everything past it silently
 * didn't draw. It still stops somewhere, far past anything playable. */
/* The depth pass batches like the shading pass: casters that draw the same thing go
 * into the map together (docs/PLAN-instancing.md, phase 3). */
void test_shadow_instancing(void)
{
    begin();

    const wgr_handle_t scene = wgr_scene_create();
    const wgr_handle_t camera = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE);
    wgr_camera3d_set_view(camera, 0, 4, 14, 0, 0, 0, 0, 1, 0);
    wgr_scene_set_active_camera(scene, camera);

    const wgr_handle_t sun = wgr_light_create(WGR_LIGHT_DIRECTIONAL);
    wgr_light_set_direction(sun, -0.4f, -1.0f, -0.3f);
    wgr_light_set_shadow_distance(sun, 60.0f);
    CHECK(wgr_light_set_casts_shadows(sun, true));
    wgr_scene_add(scene, sun, 0);

    const wgr_handle_t mesh = wgr_mesh_create_cube(1.0f, 1.0f, 1.0f);
    const wgr_handle_t material = wgr_material_create(WGR_MATERIAL_PBR);
    wgr_handle_t models[6];
    for (int i = 0; i < 6; i++) {
        models[i] = wgr_model_create(mesh);
        wgr_model_set_material(models[i], -1, material);
        wgr_model_set_transform(models[i], (float)i - 3.0f, 0.5f, 0, 0, 0, 0, 1, 1, 1);
        wgr_scene_add(scene, models[i], 0);
    }
    wgr_mesh_release(mesh);
    wgr_material_release(material);

    /* six casters, one mesh, one material: one draw into the map */
    wgr_render_begin_frame();
    wgr_scene_draw(scene);
    wgr_render_end_frame();
    CHECK(wgri_model_shadow_draw_call_count() == 1);
    CHECK(wgri_model_draw_call_count() == 1); /* and one into the screen */

    /* one that doesn't cast leaves the others batched, and the map draws five */
    CHECK(wgr_model_set_casts_shadow(models[2], false));
    wgr_render_begin_frame();
    wgr_scene_draw(scene);
    wgr_render_end_frame();
    CHECK(wgri_model_shadow_draw_call_count() == 2); /* the run is cut where it sat */
    CHECK(wgri_model_draw_call_count() == 1);        /* it is still drawn on screen */
    CHECK(wgr_model_set_casts_shadow(models[2], true));

    /* a different mesh splits the map's draws too */
    const wgr_handle_t sphere = wgr_mesh_create_sphere(0.5f, 8, 8);
    wgr_model_set_mesh(models[4], sphere);
    wgr_model_set_material(models[4], -1, material);
    wgr_mesh_release(sphere);
    wgr_render_begin_frame();
    wgr_scene_draw(scene);
    wgr_render_end_frame();
    CHECK(wgri_model_shadow_draw_call_count() == 2);

    for (int i = 0; i < 6; i++) {
        wgr_model_destroy(models[i]);
    }
    wgr_light_destroy(sun);
    wgr_scene_destroy(scene);
    end();
}

void test_model_draw_queue(void)
{
    begin();

    const wgr_handle_t mesh = wgr_mesh_create_cube(1.0f, 1.0f, 1.0f);
    /* two models drawn alternately: each call is its own placement, where the same
       model twice in a row would share one */
    const wgr_handle_t a = wgr_model_create(mesh);
    const wgr_handle_t bb = wgr_model_create(mesh);
    wgr_mesh_release(mesh);
    int placements = 0, primitives = 0, ceiling = 0;

    /* past where the queue used to stop, everything is still queued */
    wgr_render_begin_frame();
    for (int i = 0; i < 2000; i++) {
        wgr_model_draw(i % 2 == 0 ? a : bb);
    }
    wgri_model_queue_counts(&placements, &primitives, &ceiling);
    CHECK(ceiling > 2000);
    CHECK(placements == 2000);
    CHECK(primitives == 2000); /* a cube is one primitive */
    wgr_render_end_frame();

    /* it starts over each frame */
    wgr_render_begin_frame();
    wgri_model_queue_counts(&placements, &primitives, NULL);
    CHECK(placements == 0 && primitives == 0);
    wgr_render_end_frame();

    /* and it does stop: past the ceiling the rest of the frame isn't drawn, with a
       warning rather than a crash or a silently wrong queue */
    wgr_logger_set_level(WGR_LOGGER_LEVEL_FATAL); /* the warning is the point */
    wgr_render_begin_frame();
    for (int i = 0; i < ceiling + 500; i++) {
        wgr_model_draw(i % 2 == 0 ? a : bb);
    }
    wgri_model_queue_counts(&placements, &primitives, NULL);
    CHECK(placements == ceiling);
    wgr_render_end_frame();
    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);

    wgr_model_destroy(bb);
    wgr_model_destroy(a);
    end();
}
