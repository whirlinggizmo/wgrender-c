#include "internal/wgr_internal_internal.h"
#include "internal/wgr_light_internal.h"
#include "wgr_color.h"
#include "wgr_light.h"
#include "wgr_logger.h"
#include "test.h"
#include "tests.h"

#define EPS 1e-4f

static wgri_scene_light_t directional(float brightness)
{
    return (wgri_scene_light_t){.type = WGR_LIGHT_DIRECTIONAL, .radiance = {brightness, brightness, brightness},
                              .direction = {0, -1, 0}};
}

static wgri_scene_light_t point(float x, float y, float z, float brightness, float range)
{
    return (wgri_scene_light_t){.type = WGR_LIGHT_POINT, .radiance = {brightness, brightness, brightness},
                              .position = {x, y, z}, .range = range};
}

void test_light_falloff(void)
{
    /* inverse square, no range limit */
    CHECK_NEAR(wgri_light_attenuation(2.0f, 0.0f), 0.25f, EPS);
    CHECK_NEAR(wgri_light_attenuation(0.0f, 0.0f), 100.0f, EPS); /* distance clamped at 0.1 */
    /* smooth window: unchanged near the light, exactly 0 at and past the range */
    CHECK_NEAR(wgri_light_attenuation(1.0f, 10.0f), 1.0f * (1.0f - 1e-4f) * (1.0f - 1e-4f), EPS);
    CHECK_NEAR(wgri_light_attenuation(10.0f, 10.0f), 0.0f, EPS);
    CHECK_NEAR(wgri_light_attenuation(12.0f, 10.0f), 0.0f, EPS);
    CHECK(wgri_light_attenuation(9.0f, 10.0f) < wgri_light_attenuation(5.0f, 10.0f));

    /* spot cone: full inside inner, zero outside outer, smooth between */
    const float cos_inner = 0.9f, cos_outer = 0.7f;
    CHECK_NEAR(wgri_light_spot_factor(0.95f, cos_inner, cos_outer), 1.0f, EPS);
    CHECK_NEAR(wgri_light_spot_factor(0.5f, cos_inner, cos_outer), 0.0f, EPS);
    CHECK_NEAR(wgri_light_spot_factor(0.8f, cos_inner, cos_outer), 0.5f, EPS);
    /* inner == outer: hard edge */
    CHECK_NEAR(wgri_light_spot_factor(0.71f, 0.7f, 0.7f), 1.0f, EPS);
    CHECK_NEAR(wgri_light_spot_factor(0.69f, 0.7f, 0.7f), 0.0f, EPS);
}

void test_light_select(void)
{
    static wgri_light_env_t env;
    int picked[WGRI_MAX_DRAW_LIGHTS];
    const vec3_t bmin = {-1, 0, -1}, bmax = {1, 2, 1}; /* a model at the origin */

    /* ranked by contribution; equal scores keep scene order */
    env.count = 4;
    env.lights[0] = directional(1.0f);
    env.lights[1] = directional(3.0f);
    env.lights[2] = directional(2.0f);
    env.lights[3] = directional(3.0f);
    CHECK(wgri_light_select(&env, bmin, bmax, picked, WGRI_MAX_DRAW_LIGHTS) == 4);
    CHECK(picked[0] == 1 && picked[1] == 3 && picked[2] == 2 && picked[3] == 0);

    /* range culling is against the nearest point of the bounds, not the center */
    env.count = 3;
    env.lights[0] = point(4.0f, 1.0f, 0.0f, 1.0f, 2.5f);  /* 3 from the box edge: out of range */
    env.lights[1] = point(3.0f, 1.0f, 0.0f, 1.0f, 2.5f);  /* 2 from the edge (3 from center): in range */
    env.lights[2] = point(0.0f, 1.0f, 0.0f, 0.0f, 10.0f); /* black: contributes nothing */
    CHECK(wgri_light_select(&env, bmin, bmax, picked, WGRI_MAX_DRAW_LIGHTS) == 1);
    CHECK(picked[0] == 1);

    /* a dim lamp right at the model outranks a bright lamp far away */
    env.count = 2;
    env.lights[0] = point(20.0f, 1.0f, 0.0f, 50.0f, 0.0f); /* 50 / 19^2 = 0.14 */
    env.lights[1] = point(1.5f, 1.0f, 0.0f, 1.0f, 0.0f);   /* 1 / 0.5^2 = 4 */
    CHECK(wgri_light_select(&env, bmin, bmax, picked, WGRI_MAX_DRAW_LIGHTS) == 2);
    CHECK(picked[0] == 1 && picked[1] == 0);

    /* a spot aimed away from the model doesn't reach it */
    env.count = 2;
    env.lights[0] = (wgri_scene_light_t){.type = WGR_LIGHT_SPOT, .radiance = {5, 5, 5}, .position = {0, 5, 0},
                                       .direction = {0, 1, 0}, .cos_inner = 0.95f, .cos_outer = 0.9f};
    env.lights[1] = env.lights[0];
    env.lights[1].direction = (vec3_t){0, -1, 0}; /* aimed at the model */
    CHECK(wgri_light_select(&env, bmin, bmax, picked, WGRI_MAX_DRAW_LIGHTS) == 1);
    CHECK(picked[0] == 1);

    /* capped at the output size, keeping the strongest */
    env.count = 12;
    for (int i = 0; i < 12; i++) {
        env.lights[i] = directional((float)(i + 1));
    }
    CHECK(wgri_light_select(&env, bmin, bmax, picked, WGRI_MAX_DRAW_LIGHTS) == WGRI_MAX_DRAW_LIGHTS);
    for (int i = 0; i < WGRI_MAX_DRAW_LIGHTS; i++) {
        CHECK(picked[i] == 11 - i);
    }
}

void test_light_api(void)
{
    wgri_scene_light_t data;
    wgri_light_init();

    wgr_logger_set_level(WGR_LOGGER_LEVEL_FATAL); /* invalid handles/types below log on purpose */
    CHECK(wgr_light_create((wgr_light_type_t)7) == 0);
    wgr_handle_t light = wgr_light_create(WGR_LIGHT_SPOT);
    CHECK(light != 0);
    CHECK(wgr_light_is_enabled(light));

    /* defaults: white, intensity 1, pointing down, unlimited range, pi/6 and pi/4 cone */
    CHECK(wgri_light_get_scene_light(light, &data));
    CHECK(data.type == WGR_LIGHT_SPOT);
    CHECK_VEC3_NEAR(data.radiance, 1, 1, 1, EPS);
    CHECK_VEC3_NEAR(data.direction, 0, -1, 0, EPS);
    CHECK_NEAR(data.range, 0, EPS);
    CHECK_NEAR(data.cos_inner, 0.8660254f, EPS);
    CHECK_NEAR(data.cos_outer, 0.7071068f, EPS);

    wgr_color_t red = wgr_color_rgba(255, 0, 0, 255);
    CHECK(wgr_light_set_color(light, red));
    CHECK(wgr_light_set_intensity(light, 2.0f));
    CHECK(wgr_light_set_direction(light, 3, 0, 4));
    CHECK(!wgr_light_set_direction(light, 0, 0, 0)); /* no direction: rejected, unchanged */
    CHECK(wgr_light_set_range(light, -5.0f));        /* negative range: unlimited */
    CHECK(wgr_light_set_spot_cone(light, 1.0f, 0.35f)); /* inner wider than outer: clamped to outer */
    CHECK(wgri_light_get_scene_light(light, &data));
    CHECK_VEC3_NEAR(data.radiance, 2, 0, 0, EPS);
    CHECK_VEC3_NEAR(data.direction, 0.6f, 0, 0.8f, EPS);
    CHECK_NEAR(data.range, 0, EPS);
    CHECK_NEAR(data.cos_inner, data.cos_outer, EPS);
    CHECK_NEAR(data.cos_outer, 0.9393727f, EPS); /* cos(0.35 rad) */
    CHECK(wgr_light_set_spot_cone(light, 0.0f, 3.0f)); /* outer past pi/2: clamped to pi/2 */
    CHECK(wgri_light_get_scene_light(light, &data));
    CHECK_NEAR(data.cos_outer, 0.0f, EPS);

    /* getters return what was stored, clamps included, so a caller can see them */
    CHECK(wgr_light_get_type(light) == WGR_LIGHT_SPOT);
    CHECK_NEAR(wgr_light_get_intensity(light), 2.0f, EPS);
    CHECK_NEAR(wgr_light_get_direction(light).x, 0.6f, EPS); /* (3, 0, 4) normalized */
    CHECK_NEAR(wgr_light_get_direction(light).z, 0.8f, EPS);
    CHECK_NEAR(wgr_light_get_range(light), 0.0f, EPS);       /* -5 became unlimited */
    CHECK_NEAR(wgr_light_get_spot_outer_angle(light), 3.14159265f / 2.0f, EPS); /* 3 clamped */
    CHECK_NEAR(wgr_light_get_spot_inner_angle(light), 0.0f, EPS);
    CHECK(wgr_light_set_position(light, 1, 2, 3) && wgr_light_get_position(light).y == 2.0f);
    CHECK(wgr_light_set_color(light, WGR_COLOR_RED) && wgr_light_get_color(light) == WGR_COLOR_RED);
    CHECK(wgr_light_get_intensity(0) == 0.0f && wgr_light_get_color(0) == 0 && wgr_light_get_position(0).x == 0.0f);

    /* disabled and destroyed lights don't reach scenes */
    CHECK(wgr_light_set_enabled(light, false));
    CHECK(!wgri_light_get_scene_light(light, &data));
    wgr_light_destroy(light);
    CHECK(!wgr_light_is_enabled(light));
    CHECK(!wgri_light_get_scene_light(light, &data));

    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    wgri_light_deinit();
}
