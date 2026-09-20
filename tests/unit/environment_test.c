#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "data/wgr_brdf_lut.h"
#include "internal/wgr_environment.h"
#include "internal/wgr_internal.h"
#include "internal/wgr_platform.h"
#include "internal/wgr_texture.h"
#include "wgr_environment.h"
#include "wgr_handle.h"
#include "wgr_logger.h"
#include "wgr_scene.h"
#include "test.h"
#include "tests.h"

#include "sokol_gfx.h"

#define EPS 1e-3f

/* deterministic pseudo-random unit vectors */
static vec3_t random_dir(unsigned *state)
{
    for (;;) {
        vec3_t v;
        float c[3];
        for (int i = 0; i < 3; i++) {
            *state = *state * 1664525u + 1013904223u;
            c[i] = ((float)(*state >> 8) / 16777216.0f) * 2.0f - 1.0f;
        }
        v = (vec3_t){c[0], c[1], c[2]};
        if (wgr_v3_dot(v, v) > 0.01f && wgr_v3_dot(v, v) <= 1.0f) return wgr_v3_norm(v);
    }
}

/* an equirect image filled from a function of direction */
static wgr_env_image_t make_image(int width, int height, vec3_t (*fn)(vec3_t))
{
    wgr_env_image_t image = {.width = width, .height = height};
    image.rgb = (float *)malloc((size_t)width * height * 3 * sizeof(float));
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const vec3_t c = fn(wgr_environment_equirect_dir(((float)x + 0.5f) / width, ((float)y + 0.5f) / height));
            memcpy(&image.rgb[(y * width + x) * 3], &c, 3 * sizeof(float));
        }
    }
    return image;
}

static vec3_t constant_two(vec3_t d) { (void)d; return (vec3_t){2, 2, 2}; }
static vec3_t sky(vec3_t d) { const float c = d.y > 0 ? d.y : 0; return (vec3_t){c, c, c}; }
static vec3_t smooth(vec3_t d) { return (vec3_t){1.0f + 0.5f * d.x, 1.0f + 0.5f * d.y, 1.0f + 0.5f * d.z}; }

void test_environment_mapping(void)
{
    unsigned state = 7;

    /* the equirect center looks down -z, v = 0 straight up */
    CHECK_VEC3_NEAR(wgr_environment_equirect_dir(0.5f, 0.5f), 0, 0, -1, EPS);
    CHECK_VEC3_NEAR(wgr_environment_equirect_dir(0.75f, 0.5f), 1, 0, 0, EPS);
    CHECK_NEAR(wgr_environment_equirect_dir(0.3f, 0.0f).y, 1, EPS);

    for (int i = 0; i < 200; i++) {
        const vec3_t d = random_dir(&state);
        float u, v, s, t;
        int face;
        wgr_environment_dir_equirect(d, &u, &v);
        CHECK_VEC3_NEAR(wgr_environment_equirect_dir(u, v), d.x, d.y, d.z, EPS);
        wgr_environment_dir_cube(d, &face, &s, &t);
        CHECK(face >= 0 && face < 6 && s >= 0 && s <= 1 && t >= 0 && t <= 1);
        CHECK_VEC3_NEAR(wgr_environment_cube_dir(face, s, t), d.x, d.y, d.z, EPS);
    }
    /* face order +X -X +Y -Y +Z -Z (sokol/OpenGL) */
    CHECK_VEC3_NEAR(wgr_environment_cube_dir(0, 0.5f, 0.5f), 1, 0, 0, EPS);
    CHECK_VEC3_NEAR(wgr_environment_cube_dir(3, 0.5f, 0.5f), 0, -1, 0, EPS);
    CHECK_VEC3_NEAR(wgr_environment_cube_dir(5, 0.5f, 0.5f), 0, 0, -1, EPS);
    /* +X face: s runs toward -z, t toward -y (OpenGL cube map orientation) */
    CHECK(wgr_environment_cube_dir(0, 1.0f, 0.5f).z < -0.5f);
    CHECK(wgr_environment_cube_dir(0, 0.5f, 1.0f).y < -0.5f);

    /* a cubemap made from an image samples like the image */
    wgr_env_image_t image = make_image(128, 64, smooth);
    wgr_env_cube_t cube;
    CHECK(wgr_environment_cube_from_equirect(&image, 32, &cube));
    CHECK(cube.mip_count == 6);
    for (int i = 0; i < 100; i++) {
        const vec3_t d = random_dir(&state);
        const vec3_t a = wgr_environment_sample_equirect(&image, d);
        const vec3_t b = wgr_environment_sample_cube(&cube, d, 0.0f);
        CHECK_VEC3_NEAR(b, a.x, a.y, a.z, 0.03f);
    }
    wgr_environment_cube_free(&cube);
    free(image.rgb);
}

void test_environment_irradiance(void)
{
    wgr_env_sh_t sh;
    unsigned state = 11;

    /* constant radiance L: irradiance / pi is L in every direction */
    wgr_env_image_t image = make_image(128, 64, constant_two);
    wgr_environment_project_sh(&image, &sh);
    for (int i = 0; i < 20; i++) {
        const vec3_t e = wgr_environment_eval_sh(&sh, random_dir(&state));
        CHECK_VEC3_NEAR(e, 2, 2, 2, 0.01f);
    }
    free(image.rgb);

    /* radiance = cos(elevation) above the horizon: facing up, E = 2pi/3, so E/pi = 2/3;
     * facing down, nothing (9 coefficients approximate it closely) */
    image = make_image(256, 128, sky);
    wgr_environment_project_sh(&image, &sh);
    CHECK_NEAR(wgr_environment_eval_sh(&sh, (vec3_t){0, 1, 0}).x, 2.0f / 3.0f, 0.03f);
    CHECK_NEAR(wgr_environment_eval_sh(&sh, (vec3_t){0, -1, 0}).x, 0.0f, 0.05f);
    /* facing the horizon: E = integral of y * x over the quarter sphere x, y > 0 = 2/3, so E/pi = 2/(3pi) */
    CHECK_NEAR(wgr_environment_eval_sh(&sh, (vec3_t){1, 0, 0}).x, 2.0f / (3.0f * 3.14159265f), 0.02f);
    free(image.rgb);
}

void test_environment_prefilter(void)
{
    wgr_env_cube_t source, filtered;

    /* a constant environment stays constant at every roughness */
    wgr_env_image_t image = make_image(128, 64, constant_two);
    CHECK(wgr_environment_cube_from_equirect(&image, 32, &source));
    CHECK(wgr_environment_prefilter(&source, 16, 4, 32, &filtered));
    CHECK(filtered.mip_count == 4 && filtered.size == 16);
    for (int m = 0; m < filtered.mip_count; m++) {
        const int s = 16 >> m;
        for (int i = 0; i < 6 * s * s * 3; i++) {
            CHECK_NEAR(filtered.mips[m][i], 2.0f, 0.01f);
        }
    }
    wgr_environment_cube_free(&source);
    wgr_environment_cube_free(&filtered);
    free(image.rgb);

    /* blurring keeps the average: each mip's mean matches the source's */
    image = make_image(128, 64, sky);
    CHECK(wgr_environment_cube_from_equirect(&image, 32, &source));
    CHECK(wgr_environment_prefilter(&source, 16, 4, 64, &filtered));
    double source_mean = 0;
    for (int i = 0; i < 6 * 32 * 32 * 3; i++) source_mean += source.mips[0][i];
    source_mean /= 6.0 * 32 * 32 * 3;
    for (int m = 0; m < filtered.mip_count; m++) {
        const int s = 16 >> m;
        double mean = 0;
        for (int i = 0; i < 6 * s * s * 3; i++) mean += filtered.mips[m][i];
        mean /= 6.0 * s * s * 3;
        CHECK_NEAR(mean, source_mean, 0.03);
    }
    /* rougher mips are blurrier: straight up gets darker, straight down brighter */
    const vec3_t up0 = wgr_environment_sample_cube(&filtered, (vec3_t){0, 1, 0}, 0);
    const vec3_t up3 = wgr_environment_sample_cube(&filtered, (vec3_t){0, 1, 0}, 3);
    const vec3_t down0 = wgr_environment_sample_cube(&filtered, (vec3_t){0, -1, 0}, 0);
    const vec3_t down3 = wgr_environment_sample_cube(&filtered, (vec3_t){0, -1, 0}, 3);
    CHECK(up3.x < up0.x && down3.x > down0.x);
    wgr_environment_cube_free(&source);
    wgr_environment_cube_free(&filtered);
    free(image.rgb);
}

void test_environment_brdf_lut(void)
{
    enum { N = 32 };
    float lut[N * N * 2];
    wgr_environment_brdf_lut(N, 128, lut);

    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            const float a = lut[(y * N + x) * 2], b = lut[(y * N + x) * 2 + 1];
            CHECK(a >= 0.0f && b >= 0.0f && a + b <= 1.01f);
            /* rougher surfaces reflect less overall (more shadowing and masking), except at
             * grazing angles, where the integral has its well-known bump */
            if (y > 0 && ((float)x + 0.5f) / N >= 0.25f) {
                CHECK(a + b <= lut[((y - 1) * N + x) * 2] + lut[((y - 1) * N + x) * 2 + 1] + 0.01f);
            }
        }
    }
    /* Unreal's and LearnOpenGL's tables have about (0.72, 0.02) at n.v 0.5, roughness 0.5 */
    CHECK_NEAR(lut[(16 * N + 16) * 2], 0.72f, 0.04f);
    CHECK_NEAR(lut[(16 * N + 16) * 2 + 1], 0.02f, 0.02f);
    /* nearly smooth: the integral reduces to Schlick's Fresnel split,
     * A = 1 - (1 - n.v)^5 and B = (1 - n.v)^5 */
    for (int x = 2; x < N; x++) {
        const float n_dot_v = ((float)x + 0.5f) / N;
        const float fresnel = powf(1.0f - n_dot_v, 5.0f);
        CHECK_NEAR(lut[x * 2], 1.0f - fresnel, 0.03f);
        CHECK_NEAR(lut[x * 2 + 1], fresnel, 0.03f);
    }
    /* smooth surface seen head-on: specular is F0 exactly (scale 1, bias 0) */
    CHECK_NEAR(lut[((0) * N + (N - 1)) * 2], 1.0f, 0.05f);
    CHECK_NEAR(lut[((0) * N + (N - 1)) * 2 + 1], 0.0f, 0.02f);
}

/* The baked table (src/data/wgr_brdf_lut.h) is the function's: regenerate it with
 * `make brdf-lut` when the function or its size changes. */
static float float_from_half(uint16_t h)
{
    const int exponent = (h >> 10) & 0x1F, mantissa = h & 0x3FF;
    const float value = exponent == 0 ? ldexpf((float)mantissa, -24) : ldexpf((float)(mantissa | 0x400), exponent - 25);
    return (h & 0x8000) ? -value : value;
}

void test_environment_brdf_lut_baked(void)
{
    enum { N = WGR_ENVIRONMENT_LUT_SIZE };
    static float lut[N * N * 2];
    float worst = 0.0f;
    wgr_environment_brdf_lut(N, WGR_ENVIRONMENT_LUT_SAMPLES, lut);
    for (int i = 0; i < N * N * 2; i++) {
        const float diff = fabsf(float_from_half(wgr_brdf_lut[i]) - lut[i]);
        worst = diff > worst ? diff : worst;
    }
    CHECK(worst < 1e-3f); /* half precision */
}

void test_environment_half_float(void)
{
    CHECK(wgr_environment_half_from_float(0.0f) == 0x0000);
    CHECK(wgr_environment_half_from_float(1.0f) == 0x3C00);
    CHECK(wgr_environment_half_from_float(0.5f) == 0x3800);
    CHECK(wgr_environment_half_from_float(-2.0f) == 0xC000);
    CHECK(wgr_environment_half_from_float(65504.0f) == 0x7BFF);
    CHECK(wgr_environment_half_from_float(1e9f) == 0x7BFF); /* clamped to the largest half */
    CHECK(wgr_environment_half_from_float(1e-9f) == 0x0000);
    /* expected values from Python's struct 'e' (IEEE 754 half, round to nearest) */
    CHECK(wgr_environment_half_from_float(0.1f) == 0x2E66);
    CHECK(wgr_environment_half_from_float(1e-4f) == 0x068E);
    CHECK(wgr_environment_half_from_float(3e-5f) == 0x01F7); /* subnormal */
    CHECK(wgr_environment_half_from_float(1e-5f) == 0x00A8); /* subnormal */
    CHECK(wgr_environment_half_from_float(65519.0f) == 0x7BFF); /* rounds up past the largest finite: clamped */
    CHECK(wgr_environment_half_from_float(1.99999f) == 0x4000); /* rounding carries into the exponent */
}

/* Resource and scene bookkeeping on sokol's dummy backend. */
void test_environment_api(void)
{
    sg_setup(&(sg_desc){.environment = wgr_platform_environment()});
    wgr_scene_init();
    wgr_environment_init();
    wgr_logger_set_level(WGR_LOGGER_LEVEL_FATAL);

    if (!sg_query_pixelformat(SG_PIXELFORMAT_RGBA16F).filter) {
        CHECK(wgr_environment_create("../examples/assets/environments/studio_small_09_1k.hdr") == 0);
    } else {
        wgr_handle_t env = wgr_environment_create("../examples/assets/environments/studio_small_09_1k.hdr");
        CHECK(env != 0);
        CHECK(wgr_handle_get_kind(env) == WGR_HANDLE_KIND_ENVIRONMENT);
        CHECK(wgr_environment_create("../examples/assets/environments/studio_small_09_1k.hdr") == env); /* deduped */
        wgr_environment_release(env);
        CHECK(wgr_environment_create("missing.hdr") == 0);

        wgr_environment_binding_t binding;
        wgr_environment_get_binding(env, &binding);
        CHECK(binding.valid);
        CHECK(binding.sh.c[0][0] > 0.0f); /* a studio has light */
        wgr_environment_get_binding(0, &binding);
        CHECK(!binding.valid && binding.cube.id != 0); /* a black cubemap stands in */

        wgr_handle_t scene = wgr_scene_create();
        CHECK(wgr_scene_set_environment(scene, env, 1.0f, 0.5f));
        CHECK(wgr_scene_set_background(scene, env, 0.2f));
        CHECK(!wgr_scene_set_environment(scene, scene, 1.0f, 0.0f)); /* not an environment */
        CHECK(wgr_scene_set_tonemap(scene, WGR_TONEMAP_ACES, 1.0f));
        CHECK(!wgr_scene_set_tonemap(scene, (wgr_tonemap_t)9, 0.0f));
        wgr_environment_release(env); /* the scene still holds two references */
        wgr_environment_get_binding(env, &binding);
        CHECK(binding.valid);
        wgr_scene_destroy(scene); /* last references */
        wgr_environment_get_binding(env, &binding);
        CHECK(!binding.valid);
    }

    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    wgr_environment_deinit();
    wgr_scene_deinit();
    sg_shutdown();
}
