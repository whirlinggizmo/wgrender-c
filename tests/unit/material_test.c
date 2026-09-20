#include "internal/wgr_internal.h"
#include "internal/wgr_material.h"
#include "internal/wgr_math.h"
#include "internal/wgr_model.h"
#include "wgr_color.h"
#include "wgr_handle.h"
#include "wgr_logger.h"
#include "wgr_material.h"
#include "test.h"
#include "tests.h"

#define EPS 1e-4f

void test_material_srgb(void)
{
    CHECK_NEAR(wgr_srgb_to_linear(0.0f), 0.0f, EPS);
    CHECK_NEAR(wgr_srgb_to_linear(1.0f), 1.0f, EPS);
    CHECK_NEAR(wgr_srgb_to_linear(0.5f), 0.21404f, EPS);     /* mid gray is much darker in linear */
    CHECK_NEAR(wgr_srgb_to_linear(0.04f), 0.04f / 12.92f, EPS); /* linear segment near black */
    CHECK_NEAR(wgr_linear_to_srgb(0.21404f), 0.5f, EPS);
    CHECK_NEAR(wgr_linear_to_srgb(-1.0f), 0.0f, EPS);
    for (int i = 0; i <= 10; i++) {
        const float c = (float)i / 10.0f;
        CHECK_NEAR(wgr_linear_to_srgb(wgr_srgb_to_linear(c)), c, EPS);
    }
}

void test_material_api(void)
{
    const wgr_material_t *data;
    wgr_material_init();
    wgr_logger_set_level(WGR_LOGGER_LEVEL_FATAL); /* invalid names/handles below log on purpose */

    CHECK(wgr_material_create((wgr_material_shading_t)9) == 0);
    wgr_handle_t material = wgr_material_create(WGR_MATERIAL_PBR);
    CHECK(material != 0);
    CHECK(wgr_handle_get_kind(material) == WGR_HANDLE_KIND_MATERIAL);

    /* glTF defaults */
    data = wgr_material_get(material);
    CHECK(data != NULL);
    CHECK(wgr_material_get_shading(material) == WGR_MATERIAL_PBR);
    CHECK(wgr_material_get_alpha_mode(material) == WGR_ALPHA_OPAQUE);
    CHECK(!wgr_material_is_double_sided(material));
    CHECK_NEAR(data->base_color[0], 1, EPS);
    CHECK_NEAR(data->base_color[3], 1, EPS);
    CHECK_NEAR(data->metallic, 1, EPS);
    CHECK_NEAR(data->roughness, 1, EPS);
    CHECK_NEAR(data->normal_scale, 1, EPS);
    CHECK_NEAR(data->occlusion_strength, 1, EPS);
    CHECK_NEAR(data->alpha_cutoff, 0.5f, EPS);
    CHECK_NEAR(data->emissive[0], 0, EPS);

    /* parameters by name, checked by kind */
    CHECK(wgr_material_set_float(material, "roughness", 0.25f));
    CHECK(wgr_material_set_float(material, "metallic", 0.0f));
    CHECK(wgr_material_set_vec3(material, "emissive", 2, 1, 0.5f));
    CHECK(wgr_material_set_vec4(material, "base_color", 0.1f, 0.2f, 0.3f, 0.4f));
    CHECK(!wgr_material_set_float(material, "shininess", 1.0f)); /* unknown */
    CHECK(!wgr_material_set_float(material, NULL, 1.0f));
    CHECK(!wgr_material_set_float(material, "base_color", 1.0f));        /* wrong kind */
    CHECK(!wgr_material_set_vec4(material, "emissive", 1, 1, 1, 1));    /* wrong kind */
    CHECK(!wgr_material_set_vec3(material, "normal_texture", 1, 1, 1)); /* wrong kind */
    CHECK_NEAR(data->roughness, 0.25f, EPS);
    CHECK_NEAR(data->metallic, 0.0f, EPS);
    CHECK_NEAR(data->emissive[0], 2, EPS);
    CHECK_NEAR(data->emissive[2], 0.5f, EPS);
    CHECK_NEAR(data->base_color[2], 0.3f, EPS);
    CHECK_NEAR(data->base_color[3], 0.4f, EPS);

    /* colors are sRGB: converted to linear, alpha kept */
    wgr_color_t gray = wgr_color_rgba(128, 128, 128, 51);
    CHECK(wgr_material_set_color(material, "base_color", gray));
    CHECK_NEAR(data->base_color[0], wgr_srgb_to_linear(128.0f / 255.0f), EPS);
    CHECK_NEAR(data->base_color[3], 0.2f, EPS);
    CHECK(wgr_material_set_color(material, "emissive", gray)); /* vec3: alpha ignored */
    CHECK_NEAR(data->emissive[1], wgr_srgb_to_linear(128.0f / 255.0f), EPS);
    CHECK(!wgr_material_set_color(material, "roughness", gray));

    /* per-texture coordinate set, transform and sampling */
    const wgr_material_texture_t *base = &data->textures[WGR_MATERIAL_TEXTURE_BASE_COLOR];
    CHECK(base->texcoord == 0 && base->wrap_u == WGR_TEXTURE_WRAP_REPEAT && base->filter == WGR_TEXTURE_FILTER_LINEAR);
    CHECK_NEAR(base->scale[0], 1, EPS);
    CHECK(base->mipmaps);
    CHECK(wgr_material_set_int(material, "base_color_texture_texcoord", 1));
    CHECK(!wgr_material_set_int(material, "base_color_texture_texcoord", 2)); /* only sets 0 and 1 */
    CHECK(!wgr_material_set_int(material, "metallic", 1));                    /* wrong kind */
    CHECK(wgr_material_set_vec2(material, "normal_texture_scale", 4, 2));
    CHECK(wgr_material_set_vec2(material, "normal_texture_offset", 0.5f, 0.25f));
    CHECK(wgr_material_set_float(material, "normal_texture_rotation", 1.0f));
    CHECK(!wgr_material_set_vec2(material, "normal_texture_rotation", 1, 1));
    CHECK(base->texcoord == 1);
    CHECK_NEAR(data->textures[WGR_MATERIAL_TEXTURE_NORMAL].scale[0], 4, EPS);
    CHECK_NEAR(data->textures[WGR_MATERIAL_TEXTURE_NORMAL].offset[1], 0.25f, EPS);
    CHECK(wgr_material_set_texture_sampling(material, "emissive_texture", WGR_TEXTURE_WRAP_CLAMP, WGR_TEXTURE_WRAP_MIRROR,
                                           WGR_TEXTURE_FILTER_NEAREST));
    CHECK(data->textures[WGR_MATERIAL_TEXTURE_EMISSIVE].wrap_u == WGR_TEXTURE_WRAP_CLAMP);
    CHECK(data->textures[WGR_MATERIAL_TEXTURE_EMISSIVE].wrap_v == WGR_TEXTURE_WRAP_MIRROR);
    CHECK(data->textures[WGR_MATERIAL_TEXTURE_EMISSIVE].filter == WGR_TEXTURE_FILTER_NEAREST);
    CHECK(!wgr_material_set_texture_sampling(material, "emissive", WGR_TEXTURE_WRAP_CLAMP, WGR_TEXTURE_WRAP_CLAMP,
                                            WGR_TEXTURE_FILTER_LINEAR)); /* not a texture */
    CHECK(!wgr_material_set_texture_sampling(material, "emissive_texture", (wgr_texture_wrap_t)7, WGR_TEXTURE_WRAP_CLAMP,
                                            WGR_TEXTURE_FILTER_LINEAR));

    /* textures: 0 clears, other handle kinds are rejected */
    CHECK(wgr_material_set_texture(material, "normal_texture", 0));
    CHECK(!wgr_material_set_texture(material, "normal_texture", gray));
    CHECK(!wgr_material_set_texture(material, "roughness", 0));

    CHECK(wgr_material_set_alpha_mode(material, WGR_ALPHA_MASK, 0.3f));
    CHECK(wgr_material_get_alpha_mode(material) == WGR_ALPHA_MASK);
    CHECK_NEAR(data->alpha_cutoff, 0.3f, EPS);
    CHECK(!wgr_material_set_alpha_mode(material, (wgr_alpha_mode_t)5, 0.3f));
    CHECK(wgr_material_set_double_sided(material, true));
    CHECK(wgr_material_is_double_sided(material));
    CHECK(wgr_material_set_shading(material, WGR_MATERIAL_UNLIT));
    CHECK(wgr_material_get_shading(material) == WGR_MATERIAL_UNLIT);

    /* reference counted: freed when the last holder releases it */
    wgr_material_retain(material); /* e.g. a model */
    wgr_material_release(material); /* the creator's reference */
    CHECK(wgr_material_get(material) != NULL);
    wgr_material_release(material);
    CHECK(wgr_material_get(material) == NULL);
    CHECK(!wgr_material_set_float(material, "roughness", 0.5f));

    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    wgr_material_deinit();
}

/* A unit quad in the xy plane facing +z, two triangles, glTF texture coordinates
 * (v runs down the image, so v = 1 - y). */
static const float QUAD_POSITIONS[] = {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0};
static const float QUAD_NORMALS[] = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
static const uint32_t QUAD_INDICES[] = {0, 1, 2, 0, 2, 3};

static void check_tangents(const float *uvs, float tx, float ty, float tz, float w)
{
    float tangents[16];
    wgr_model_generate_tangents(QUAD_POSITIONS, QUAD_NORMALS, uvs, 4, QUAD_INDICES, 6, tangents);
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
    wgr_model_generate_tangents(QUAD_POSITIONS, QUAD_NORMALS, (const float[8]){0}, 4, QUAD_INDICES, 6, tangents);
    for (int i = 0; i < 4; i++) {
        vec3_t t = {tangents[i * 4], tangents[i * 4 + 1], tangents[i * 4 + 2]};
        CHECK_NEAR(wgr_v3_dot(t, t), 1.0f, EPS);
        CHECK_NEAR(t.z, 0.0f, EPS);
    }
}

void test_material_uv_matrix(void)
{
    wgr_material_texture_t texture = {.scale = {1, 1}};
    float m[6];

    wgr_material_uv_matrix(&texture, m); /* identity */
    CHECK_NEAR(m[0], 1, EPS);
    CHECK_NEAR(m[1], 0, EPS);
    CHECK_NEAR(m[2], 0, EPS);
    CHECK_NEAR(m[3], 0, EPS);
    CHECK_NEAR(m[4], 1, EPS);
    CHECK_NEAR(m[5], 0, EPS);

    /* glTF KHR_texture_transform: translation * rotation * scale */
    texture = (wgr_material_texture_t){.offset = {0.5f, 0.25f}, .rotation = 1.5707963f, .scale = {2, 3}};
    wgr_material_uv_matrix(&texture, m);
    /* uv (1, 0): scaled (2, 0), rotated by the glTF matrix [c s; -s c] to (0, -2), offset (0.5, -1.75) */
    CHECK_NEAR(m[0] * 1 + m[1] * 0 + m[2], 0.5f, EPS);
    CHECK_NEAR(m[3] * 1 + m[4] * 0 + m[5], -1.75f, EPS);
    /* uv (0, 1): scaled (0, 3), rotated to (3, 0), offset (3.5, 0.25) */
    CHECK_NEAR(m[0] * 0 + m[1] * 1 + m[2], 3.5f, EPS);
    CHECK_NEAR(m[3] * 0 + m[4] * 1 + m[5], 0.25f, EPS);
}
