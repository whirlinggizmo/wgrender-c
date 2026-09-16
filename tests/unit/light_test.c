#include "internal/sk_internal.h"
#include "internal/sk_light.h"
#include "sk_color.h"
#include "sk_light.h"
#include "sk_logger.h"
#include "test.h"
#include "tests.h"

#define EPS 1e-4f

static sk_scene_light_t directional(float brightness)
{
    return (sk_scene_light_t){.type = SK_LIGHT_DIRECTIONAL, .radiance = {brightness, brightness, brightness},
                              .direction = {0, -1, 0}};
}

static sk_scene_light_t point(float x, float y, float z, float brightness, float range)
{
    return (sk_scene_light_t){.type = SK_LIGHT_POINT, .radiance = {brightness, brightness, brightness},
                              .position = {x, y, z}, .range = range};
}

void test_light_falloff(void)
{
    /* inverse square, no range limit */
    CHECK_NEAR(sk_light_attenuation(2.0f, 0.0f), 0.25f, EPS);
    CHECK_NEAR(sk_light_attenuation(0.0f, 0.0f), 100.0f, EPS); /* distance clamped at 0.1 */
    /* smooth window: unchanged near the light, exactly 0 at and past the range */
    CHECK_NEAR(sk_light_attenuation(1.0f, 10.0f), 1.0f * (1.0f - 1e-4f) * (1.0f - 1e-4f), EPS);
    CHECK_NEAR(sk_light_attenuation(10.0f, 10.0f), 0.0f, EPS);
    CHECK_NEAR(sk_light_attenuation(12.0f, 10.0f), 0.0f, EPS);
    CHECK(sk_light_attenuation(9.0f, 10.0f) < sk_light_attenuation(5.0f, 10.0f));

    /* spot cone: full inside inner, zero outside outer, smooth between */
    const float cos_inner = 0.9f, cos_outer = 0.7f;
    CHECK_NEAR(sk_light_spot_factor(0.95f, cos_inner, cos_outer), 1.0f, EPS);
    CHECK_NEAR(sk_light_spot_factor(0.5f, cos_inner, cos_outer), 0.0f, EPS);
    CHECK_NEAR(sk_light_spot_factor(0.8f, cos_inner, cos_outer), 0.5f, EPS);
    /* inner == outer: hard edge */
    CHECK_NEAR(sk_light_spot_factor(0.71f, 0.7f, 0.7f), 1.0f, EPS);
    CHECK_NEAR(sk_light_spot_factor(0.69f, 0.7f, 0.7f), 0.0f, EPS);
}

void test_light_select(void)
{
    static sk_light_env_t env;
    int picked[SK_MAX_DRAW_LIGHTS];
    const vec3_t bmin = {-1, 0, -1}, bmax = {1, 2, 1}; /* a model at the origin */

    /* ranked by contribution; equal scores keep scene order */
    env.count = 4;
    env.lights[0] = directional(1.0f);
    env.lights[1] = directional(3.0f);
    env.lights[2] = directional(2.0f);
    env.lights[3] = directional(3.0f);
    CHECK(sk_light_select(&env, bmin, bmax, picked, SK_MAX_DRAW_LIGHTS) == 4);
    CHECK(picked[0] == 1 && picked[1] == 3 && picked[2] == 2 && picked[3] == 0);

    /* range culling is against the nearest point of the bounds, not the center */
    env.count = 3;
    env.lights[0] = point(4.0f, 1.0f, 0.0f, 1.0f, 2.5f);  /* 3 from the box edge: out of range */
    env.lights[1] = point(3.0f, 1.0f, 0.0f, 1.0f, 2.5f);  /* 2 from the edge (3 from center): in range */
    env.lights[2] = point(0.0f, 1.0f, 0.0f, 0.0f, 10.0f); /* black: contributes nothing */
    CHECK(sk_light_select(&env, bmin, bmax, picked, SK_MAX_DRAW_LIGHTS) == 1);
    CHECK(picked[0] == 1);

    /* a dim lamp right at the model outranks a bright lamp far away */
    env.count = 2;
    env.lights[0] = point(20.0f, 1.0f, 0.0f, 50.0f, 0.0f); /* 50 / 19^2 = 0.14 */
    env.lights[1] = point(1.5f, 1.0f, 0.0f, 1.0f, 0.0f);   /* 1 / 0.5^2 = 4 */
    CHECK(sk_light_select(&env, bmin, bmax, picked, SK_MAX_DRAW_LIGHTS) == 2);
    CHECK(picked[0] == 1 && picked[1] == 0);

    /* a spot aimed away from the model doesn't reach it */
    env.count = 2;
    env.lights[0] = (sk_scene_light_t){.type = SK_LIGHT_SPOT, .radiance = {5, 5, 5}, .position = {0, 5, 0},
                                       .direction = {0, 1, 0}, .cos_inner = 0.95f, .cos_outer = 0.9f};
    env.lights[1] = env.lights[0];
    env.lights[1].direction = (vec3_t){0, -1, 0}; /* aimed at the model */
    CHECK(sk_light_select(&env, bmin, bmax, picked, SK_MAX_DRAW_LIGHTS) == 1);
    CHECK(picked[0] == 1);

    /* capped at the output size, keeping the strongest */
    env.count = 12;
    for (int i = 0; i < 12; i++) {
        env.lights[i] = directional((float)(i + 1));
    }
    CHECK(sk_light_select(&env, bmin, bmax, picked, SK_MAX_DRAW_LIGHTS) == SK_MAX_DRAW_LIGHTS);
    for (int i = 0; i < SK_MAX_DRAW_LIGHTS; i++) {
        CHECK(picked[i] == 11 - i);
    }
}

void test_light_api(void)
{
    sk_scene_light_t data;
    sk_color_init();
    sk_light_init();

    sk_logger_set_level(SK_LOGGER_LEVEL_FATAL); /* invalid handles/types below log on purpose */
    CHECK(sk_light_create((sk_light_type_t)7) == 0);
    sk_handle_t light = sk_light_create(SK_LIGHT_SPOT);
    CHECK(light != 0);
    CHECK(sk_light_is_enabled(light));

    /* defaults: white, intensity 1, pointing down, unlimited range, pi/6 and pi/4 cone */
    CHECK(sk_light_get_scene_light(light, &data));
    CHECK(data.type == SK_LIGHT_SPOT);
    CHECK_VEC3_NEAR(data.radiance, 1, 1, 1, EPS);
    CHECK_VEC3_NEAR(data.direction, 0, -1, 0, EPS);
    CHECK_NEAR(data.range, 0, EPS);
    CHECK_NEAR(data.cos_inner, 0.8660254f, EPS);
    CHECK_NEAR(data.cos_outer, 0.7071068f, EPS);

    sk_handle_t red = sk_color_create(255, 0, 0, 255);
    CHECK(sk_light_set_color(light, red));
    CHECK(sk_light_set_intensity(light, 2.0f));
    CHECK(sk_light_set_direction(light, 3, 0, 4));
    CHECK(!sk_light_set_direction(light, 0, 0, 0)); /* no direction: rejected, unchanged */
    CHECK(sk_light_set_range(light, -5.0f));        /* negative range: unlimited */
    CHECK(sk_light_set_spot_cone(light, 1.0f, 0.35f)); /* inner wider than outer: clamped to outer */
    CHECK(sk_light_get_scene_light(light, &data));
    CHECK_VEC3_NEAR(data.radiance, 2, 0, 0, EPS);
    CHECK_VEC3_NEAR(data.direction, 0.6f, 0, 0.8f, EPS);
    CHECK_NEAR(data.range, 0, EPS);
    CHECK_NEAR(data.cos_inner, data.cos_outer, EPS);
    CHECK_NEAR(data.cos_outer, 0.9393727f, EPS); /* cos(0.35 rad) */
    CHECK(sk_light_set_spot_cone(light, 0.0f, 3.0f)); /* outer past pi/2: clamped to pi/2 */
    CHECK(sk_light_get_scene_light(light, &data));
    CHECK_NEAR(data.cos_outer, 0.0f, EPS);

    /* disabled and destroyed lights don't reach scenes */
    CHECK(sk_light_set_enabled(light, false));
    CHECK(!sk_light_get_scene_light(light, &data));
    sk_light_destroy(light);
    CHECK(!sk_light_is_enabled(light));
    CHECK(!sk_light_get_scene_light(light, &data));

    sk_logger_set_level(SK_LOGGER_LEVEL_INFO);
    sk_light_deinit();
    sk_color_deinit();
}
