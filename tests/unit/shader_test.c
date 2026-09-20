/* Custom material shaders (docs/PLAN-materials.md): loading the example .skshader files
 * written by tools/shaderpack.py (sokol's dummy backend takes the GL layout), their
 * parameters and textures by name through the material setters, where the values land
 * (std140 offsets, fragment block then vertex block), and reference counting. */
#include <stdio.h>
#include <string.h>

#include "internal/sk_material.h"
#include "internal/sk_internal.h"
#include "internal/sk_platform.h"
#include "internal/sk_render.h"
#include "internal/sk_scene.h"
#include "internal/sk_camera3d.h"
#include "internal/sk_sprite3d.h"
#include "internal/sk_sprite2d.h"
#include "internal/sk_sprite_batch.h"
#include "sk_render.h"
#include "sk_scene.h"
#include "sk_camera3d.h"
#include "sk_sprite3d.h"
#include "sk_sprite2d.h"
#include "internal/sk_shader.h"
#include "internal/sk_texture.h"
#include "sk_color.h"
#include "sk_handle.h"
#include "sk_logger.h"
#include "sk_material.h"
#include "sk_shader.h"
#include "sk_texture.h"
#include "test.h"
#include "tests.h"

#include "sokol_gfx.h"
#include "sokol_time.h"

#define SHADERS "../examples/assets/shaders/"
#define EPS 1e-5f

static float value_at(sk_handle_t material, const char *name, int component)
{
    const sk_material_t *data = sk_material_get(material);
    const sk_shader_t *shader = sk_shader_get(data->shader);
    const sk_shader_param_t *param = &shader->params[sk_shader_find_param(shader, name)];
    const int base = param->block == SK_SHADER_BLOCK_VS_PARAMS ? shader->block_size[SK_SHADER_BLOCK_FS_PARAMS] : 0;
    float value;
    memcpy(&value, data->custom_params + base + param->offset + 4 * component, sizeof(value));
    return value;
}

void test_shader_custom_material(void)
{
    sg_setup(&(sg_desc){.environment = sk_platform_environment()});
    sk_texture_init();
    sk_shader_init();
    sk_material_init();
    sk_logger_set_level(SK_LOGGER_LEVEL_FATAL); /* wrong names and files below log on purpose */

    const sk_handle_t toon = sk_shader_create(SHADERS "toon.skshader");
    CHECK(toon != 0);
    CHECK(sk_handle_get_kind(toon) == SK_HANDLE_KIND_SHADER);
    CHECK(sk_shader_create(SHADERS "toon.skshader") == toon); /* deduped by path */
    sk_shader_release(toon);

    const sk_shader_t *shader = sk_shader_get(toon);
    CHECK(shader != NULL && shader->param_count == 3 && shader->texture_count == 1);
    CHECK(strcmp(shader->textures[0], "base_tex") == 0);
    CHECK(shader->block_size[SK_SHADER_BLOCK_FS_PARAMS] == 32); /* vec4 color, float bands, float rim */
    CHECK(shader->programs[0].has_block[SK_SHADER_BLOCK_OBJECT] && shader->programs[1].has_block[SK_SHADER_BLOCK_OBJECT]);
    CHECK(shader->programs[0].has_block[SK_SHADER_BLOCK_FRAME]);
    CHECK(shader->programs[0].view_slot[0] == 0 && shader->programs[0].sampler_slot[0] == 0);
    CHECK(shader->programs[0].env_view_slot < 0 && shader->programs[0].brdf_view_slot < 0); /* doesn't use them */

    /* a material using it: the shader's names, not the built-in ones */
    const sk_handle_t material = sk_material_create_custom(toon);
    CHECK(material != 0);
    CHECK(sk_material_get_shading(material) == SK_MATERIAL_CUSTOM);
    CHECK(sk_material_get_shader(material) == toon);
    CHECK(!sk_material_set_shading(material, SK_MATERIAL_PBR));
    CHECK(sk_material_set_float(material, "bands", 4.0f));
    CHECK_NEAR(value_at(material, "bands", 0), 4.0f, EPS);
    CHECK(sk_material_set_float(material, "rim", 0.25f));
    CHECK_NEAR(value_at(material, "rim", 0), 0.25f, EPS);
    CHECK(sk_material_set_color(material, "color", sk_color_rgba(255, 0, 0, 128))); /* sRGB -> linear */
    CHECK_NEAR(value_at(material, "color", 0), 1.0f, EPS);
    CHECK_NEAR(value_at(material, "color", 1), 0.0f, EPS);
    CHECK_NEAR(value_at(material, "color", 3), 128.0f / 255.0f, 1e-3f);
    CHECK(sk_material_set_vec4(material, "color", 0.1f, 0.2f, 0.3f, 0.4f));
    CHECK_NEAR(value_at(material, "color", 2), 0.3f, EPS);
    CHECK(!sk_material_set_vec2(material, "bands", 1, 2)); /* a float, not a vec2 */
    CHECK(!sk_material_set_float(material, "color", 1)); /* a vec4 */
    CHECK(!sk_material_set_float(material, "metallic", 0)); /* built-in names don't apply */
    CHECK(!sk_material_set_float(material, "missing", 0));

    const sk_handle_t texture = sk_texture_create("../examples/assets/textures/noise.png");
    CHECK(texture != 0);
    CHECK(sk_material_set_texture(material, "base_tex", texture));
    CHECK(sk_material_get(material)->textures[0].texture == texture);
    CHECK(sk_material_set_texture_sampling(material, "base_tex", SK_TEXTURE_WRAP_CLAMP, SK_TEXTURE_WRAP_CLAMP,
                                           SK_TEXTURE_FILTER_NEAREST));
    CHECK(!sk_material_set_texture(material, "base_color_texture", texture));
    CHECK(!sk_material_set_texture(material, "missing", texture));
    sk_texture_release(texture); /* the material keeps it */

    /* the material holds the shader: released with the material's last reference */
    sk_shader_release(toon);
    CHECK(sk_shader_get(toon) != NULL);
    sk_material_release(material);
    CHECK(sk_shader_get(toon) == NULL);

    /* a vertex hook's parameters come after the fragment block */
    const sk_handle_t wave = sk_shader_create(SHADERS "wave.skshader");
    const sk_handle_t rippling = sk_material_create_custom(wave);
    sk_shader_release(wave);
    shader = sk_shader_get(wave);
    CHECK(shader != NULL && shader->texture_count == 0);
    CHECK(shader->block_size[SK_SHADER_BLOCK_FS_PARAMS] == 48 && shader->block_size[SK_SHADER_BLOCK_VS_PARAMS] == 16);
    CHECK(shader->programs[0].has_block[SK_SHADER_BLOCK_VS_PARAMS]);
    /* it reflects the environment: libsk's slots 8 and 9, in both programs */
    for (int p = 0; p < 2; p++) {
        CHECK(shader->programs[p].env_view_slot == 8 && shader->programs[p].env_sampler_slot == 8);
        CHECK(shader->programs[p].brdf_view_slot == 9 && shader->programs[p].brdf_sampler_slot == 9);
    }
    CHECK(sk_material_set_float(rippling, "amplitude", 0.5f));
    CHECK(sk_material_set_float(rippling, "wave_speed", 2.0f));
    CHECK(sk_material_set_vec4(rippling, "high_color", 1, 1, 1, 1));
    CHECK_NEAR(value_at(rippling, "amplitude", 0), 0.5f, EPS);
    CHECK_NEAR(value_at(rippling, "wave_speed", 0), 2.0f, EPS);
    sk_material_release(rippling);

    /* made by an older shaderpack: refused (sk_frame changed), not drawn wrongly */
    FILE *old = fopen("build/old.skshader", "wb");
    CHECK(old != NULL);
    if (old != NULL) {
        fputs("skshader 1\nend\n", old);
        fclose(old);
    }
    CHECK(sk_shader_create("build/old.skshader") == 0);

    /* not shaders */
    CHECK(sk_shader_create("../examples/assets/textures/noise.png") == 0);
    CHECK(sk_shader_create(SHADERS "missing.skshader") == 0);
    CHECK(sk_material_create_custom(0) == 0);

    sk_logger_set_level(SK_LOGGER_LEVEL_INFO);
    sk_material_deinit();
    sk_shader_deinit();
    sk_texture_deinit();
    sg_shutdown();
}

/* Custom materials on sprites: the sprite programs in a .skshader (both ways of
 * getting sprite data), set_material taking custom materials only and holding a
 * reference, batches split by material, and a frame drawn through them (the dummy
 * backend validates every pipeline, uniform block and binding). */
void test_shader_sprites(void)
{
    enum { COUNT = 40 };
    sk_handle_t sprites[COUNT];

    sg_setup(&(sg_desc){.environment = sk_platform_environment()});
    stm_setup(); /* custom shaders get the time (sk_get_time), which sk_run starts */
    sk_render_init();
    sk_scene_init();
    sk_camera3d_init();
    sk_texture_init();
    sk_shader_init();
    sk_material_init();
    sk_sprite3d_init();
    sk_sprite2d_init();
    sk_logger_set_level(SK_LOGGER_LEVEL_FATAL); /* refusals below log on purpose */

    const sk_handle_t shader = sk_shader_create(SHADERS "sprite_fx.skshader");
    const sk_shader_t *shader_ptr = sk_shader_get(shader);
    CHECK(shader_ptr != NULL);
    if (shader_ptr == NULL) return;
    CHECK(shader_ptr->programs[SK_SHADER_PROGRAM_SPRITE].sprite_view_slot == 10);
    CHECK(shader_ptr->programs[SK_SHADER_PROGRAM_SPRITE].data_view_slot < 0);
    CHECK(shader_ptr->programs[SK_SHADER_PROGRAM_SPRITE_PULLED].data_view_slot == 11);
    CHECK(shader_ptr->programs[SK_SHADER_PROGRAM_SPRITE_PULLED].has_block[SK_SHADER_BLOCK_SPRITE_BATCH]);
    CHECK(shader_ptr->programs[SK_SHADER_PROGRAM_STATIC].sprite_view_slot == 10); /* white on models */
    const sk_handle_t custom = sk_material_create_custom(shader);
    sk_shader_release(shader); /* the material holds it */
    const sk_handle_t pbr = sk_material_create(SK_MATERIAL_PBR);

    const unsigned char pixel[4] = {255, 255, 255, 255};
    const sk_handle_t texture = sk_texture_create_rgba(pixel, 1, 1);
    const sk_handle_t scene = sk_scene_create();
    const sk_handle_t camera = sk_camera3d_create(SK_CAMERA3D_PERSPECTIVE);
    sk_camera3d_set_view(camera, 0, 5, 30, 0, 0, 0, 0, 1, 0);
    sk_scene_set_active_camera(scene, camera);
    for (int i = 0; i < COUNT; i++) {
        sprites[i] = sk_sprite3d_create(texture);
        sk_sprite3d_set_transform(sprites[i], (float)(i % 8) - 4.0f, 0, (float)(i / 8) - 4.0f, 0, 0, 0, 1, 1, 1);
        sk_sprite3d_set_alpha_mode(sprites[i], SK_ALPHA_MASK, 0.5f); /* unordered: grouped */
        if (i % 2 == 0) CHECK(sk_sprite3d_set_material(sprites[i], custom)); /* alternating */
        sk_scene_add(scene, sprites[i], 0);
    }
    CHECK(sk_sprite3d_set_material(sprites[1], pbr)); /* built-in: lit, like a model */
    CHECK(sk_sprite3d_get_material(sprites[0]) == custom && sk_sprite3d_get_material(sprites[1]) == pbr);
    const sk_handle_t sprite2d = sk_sprite2d_create(texture);
    CHECK(sk_sprite2d_set_material(sprite2d, custom));
    CHECK(!sk_sprite2d_set_material(sprite2d, pbr)); /* 2D has no lights */

    /* one texture, three materials (custom, built-in, none): three batches, however
       they interleave, drawn through the shader's sprite program and libsk's shading */
    sk_render_begin();
    sk_scene_draw(scene);
    CHECK(sk_sprite_batch_count() == 3);
    sk_sprite2d_draw(sprite2d);
    CHECK(sk_sprite_batch_count() == 4);
    sk_render_end();

    /* 0 goes back to libsk's shader: one batch again */
    for (int i = 0; i < COUNT; i++) CHECK(sk_sprite3d_set_material(sprites[i], 0));
    sk_render_begin();
    sk_scene_draw(scene);
    CHECK(sk_sprite_batch_count() == 1);
    sk_render_end();

    /* the sprites held the material: it goes with the last of them */
    sk_material_release(custom);
    CHECK(sk_material_get(custom) != NULL); /* sprite2d still has it */
    sk_sprite2d_destroy(sprite2d);
    CHECK(sk_material_get(custom) == NULL);
    CHECK(sk_shader_get(shader) == NULL); /* and the material held the shader */

    for (int i = 0; i < COUNT; i++) sk_sprite3d_destroy(sprites[i]);
    sk_material_release(pbr);
    sk_texture_release(texture);
    sk_scene_destroy(scene);
    sk_logger_set_level(SK_LOGGER_LEVEL_INFO);
    sk_sprite2d_deinit();
    sk_sprite3d_deinit();
    sk_material_deinit();
    sk_shader_deinit();
    sk_texture_deinit();
    sk_camera3d_deinit();
    sk_scene_deinit();
    sk_render_deinit();
    sg_shutdown();
}
