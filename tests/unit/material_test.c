#include "internal/sk_internal.h"
#include "internal/sk_material.h"
#include "internal/sk_math.h"
#include "internal/sk_model.h"
#include "sk_color.h"
#include "sk_handle.h"
#include "sk_logger.h"
#include "sk_material.h"
#include "test.h"
#include "tests.h"

#define EPS 1e-4f

void test_material_srgb(void)
{
    CHECK_NEAR(sk_srgb_to_linear(0.0f), 0.0f, EPS);
    CHECK_NEAR(sk_srgb_to_linear(1.0f), 1.0f, EPS);
    CHECK_NEAR(sk_srgb_to_linear(0.5f), 0.21404f, EPS);     /* mid gray is much darker in linear */
    CHECK_NEAR(sk_srgb_to_linear(0.04f), 0.04f / 12.92f, EPS); /* linear segment near black */
    CHECK_NEAR(sk_linear_to_srgb(0.21404f), 0.5f, EPS);
    CHECK_NEAR(sk_linear_to_srgb(-1.0f), 0.0f, EPS);
    for (int i = 0; i <= 10; i++) {
        const float c = (float)i / 10.0f;
        CHECK_NEAR(sk_linear_to_srgb(sk_srgb_to_linear(c)), c, EPS);
    }
}

void test_material_api(void)
{
    const sk_material_t *data;
    sk_color_init();
    sk_material_init();
    sk_logger_set_level(SK_LOGGER_LEVEL_FATAL); /* invalid names/handles below log on purpose */

    CHECK(sk_material_create((sk_material_shading_t)9) == 0);
    sk_handle_t material = sk_material_create(SK_MATERIAL_PBR);
    CHECK(material != 0);
    CHECK(sk_handle_get_kind(material) == SK_HANDLE_KIND_MATERIAL);

    /* glTF defaults */
    data = sk_material_get(material);
    CHECK(data != NULL);
    CHECK(sk_material_get_shading(material) == SK_MATERIAL_PBR);
    CHECK(sk_material_get_alpha_mode(material) == SK_MATERIAL_ALPHA_OPAQUE);
    CHECK(!sk_material_is_double_sided(material));
    CHECK_NEAR(data->base_color[0], 1, EPS);
    CHECK_NEAR(data->base_color[3], 1, EPS);
    CHECK_NEAR(data->metallic, 1, EPS);
    CHECK_NEAR(data->roughness, 1, EPS);
    CHECK_NEAR(data->normal_scale, 1, EPS);
    CHECK_NEAR(data->occlusion_strength, 1, EPS);
    CHECK_NEAR(data->alpha_cutoff, 0.5f, EPS);
    CHECK_NEAR(data->emissive[0], 0, EPS);

    /* parameters by name, checked by kind */
    CHECK(sk_material_set_float(material, "roughness", 0.25f));
    CHECK(sk_material_set_float(material, "metallic", 0.0f));
    CHECK(sk_material_set_vec3(material, "emissive", 2, 1, 0.5f));
    CHECK(sk_material_set_vec4(material, "base_color", 0.1f, 0.2f, 0.3f, 0.4f));
    CHECK(!sk_material_set_float(material, "shininess", 1.0f)); /* unknown */
    CHECK(!sk_material_set_float(material, NULL, 1.0f));
    CHECK(!sk_material_set_float(material, "base_color", 1.0f));        /* wrong kind */
    CHECK(!sk_material_set_vec4(material, "emissive", 1, 1, 1, 1));    /* wrong kind */
    CHECK(!sk_material_set_vec3(material, "normal_texture", 1, 1, 1)); /* wrong kind */
    CHECK_NEAR(data->roughness, 0.25f, EPS);
    CHECK_NEAR(data->metallic, 0.0f, EPS);
    CHECK_NEAR(data->emissive[0], 2, EPS);
    CHECK_NEAR(data->emissive[2], 0.5f, EPS);
    CHECK_NEAR(data->base_color[2], 0.3f, EPS);
    CHECK_NEAR(data->base_color[3], 0.4f, EPS);

    /* colors are sRGB: converted to linear, alpha kept */
    sk_handle_t gray = sk_color_create(128, 128, 128, 51);
    CHECK(sk_material_set_color(material, "base_color", gray));
    CHECK_NEAR(data->base_color[0], sk_srgb_to_linear(128.0f / 255.0f), EPS);
    CHECK_NEAR(data->base_color[3], 0.2f, EPS);
    CHECK(sk_material_set_color(material, "emissive", gray)); /* vec3: alpha ignored */
    CHECK_NEAR(data->emissive[1], sk_srgb_to_linear(128.0f / 255.0f), EPS);
    CHECK(!sk_material_set_color(material, "roughness", gray));

    /* textures: 0 clears, other handle kinds are rejected */
    CHECK(sk_material_set_texture(material, "normal_texture", 0));
    CHECK(!sk_material_set_texture(material, "normal_texture", gray));
    CHECK(!sk_material_set_texture(material, "roughness", 0));

    CHECK(sk_material_set_alpha_mode(material, SK_MATERIAL_ALPHA_MASK, 0.3f));
    CHECK(sk_material_get_alpha_mode(material) == SK_MATERIAL_ALPHA_MASK);
    CHECK_NEAR(data->alpha_cutoff, 0.3f, EPS);
    CHECK(!sk_material_set_alpha_mode(material, (sk_material_alpha_t)5, 0.3f));
    CHECK(sk_material_set_double_sided(material, true));
    CHECK(sk_material_is_double_sided(material));
    CHECK(sk_material_set_shading(material, SK_MATERIAL_UNLIT));
    CHECK(sk_material_get_shading(material) == SK_MATERIAL_UNLIT);

    /* reference counted: freed when the last holder releases it */
    sk_material_retain(material); /* e.g. a model */
    sk_material_destroy(material); /* the creator's reference */
    CHECK(sk_material_get(material) != NULL);
    sk_material_release(material);
    CHECK(sk_material_get(material) == NULL);
    CHECK(!sk_material_set_float(material, "roughness", 0.5f));

    sk_logger_set_level(SK_LOGGER_LEVEL_INFO);
    sk_material_deinit();
    sk_color_deinit();
}

/* A unit quad in the xy plane facing +z, two triangles, glTF texture coordinates
 * (v runs down the image, so v = 1 - y). */
static const float QUAD_POSITIONS[] = {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0};
static const float QUAD_NORMALS[] = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
static const uint32_t QUAD_INDICES[] = {0, 1, 2, 0, 2, 3};

static void check_tangents(const float *uvs, float tx, float ty, float tz, float w)
{
    float tangents[16];
    sk_model_generate_tangents(QUAD_POSITIONS, QUAD_NORMALS, uvs, 4, QUAD_INDICES, 6, tangents);
    for (int i = 0; i < 4; i++) {
        CHECK_NEAR(tangents[i * 4], tx, EPS);
        CHECK_NEAR(tangents[i * 4 + 1], ty, EPS);
        CHECK_NEAR(tangents[i * 4 + 2], tz, EPS);
        CHECK_NEAR(tangents[i * 4 + 3], w, EPS);
    }
}

void test_model_generate_tangents(void)
{
    /* tangent along +u; bitangent cross(n, t) * w points up the texture (toward -v) */
    check_tangents((const float[]){0, 1, 1, 1, 1, 0, 0, 0}, 1, 0, 0, 1);
    /* u mirrored: tangent flips, bitangent still up, so w flips */
    check_tangents((const float[]){1, 1, 0, 1, 0, 0, 1, 0}, -1, 0, 0, -1);
    /* v mirrored (v runs up): tangent unchanged, bitangent flips */
    check_tangents((const float[]){0, 0, 1, 0, 1, 1, 0, 1}, 1, 0, 0, -1);
    /* texture rotated 90 degrees: u runs up the quad */
    check_tangents((const float[]){0, 0, 0, 1, 1, 1, 1, 0}, 0, 1, 0, 1);

    /* no texture mapping: any unit tangent perpendicular to the normal */
    float tangents[16];
    sk_model_generate_tangents(QUAD_POSITIONS, QUAD_NORMALS, (const float[8]){0}, 4, QUAD_INDICES, 6, tangents);
    for (int i = 0; i < 4; i++) {
        vec3_t t = {tangents[i * 4], tangents[i * 4 + 1], tangents[i * 4 + 2]};
        CHECK_NEAR(sk_v3_dot(t, t), 1.0f, EPS);
        CHECK_NEAR(t.z, 0.0f, EPS);
    }
}
