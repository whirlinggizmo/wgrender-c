/* Custom material shaders (docs/PLAN-materials.md): loading the example .wgrshader files
 * written by tools/shaderpack.py (sokol's dummy backend takes the GL layout), their
 * parameters and textures by name through the material setters, where the values land
 * (std140 offsets, fragment block then vertex block), and reference counting. */
#include <stdio.h>
#include <string.h>

#include "internal/wgr_effect_internal.h"
#include "internal/wgr_material_internal.h"
#include "internal/wgr_internal_internal.h"
#include "internal/wgr_platform_internal.h"
#include "internal/wgr_render_internal.h"
#include "internal/wgr_scene_internal.h"
#include "internal/wgr_camera3d_internal.h"
#include "internal/wgr_sprite3d_internal.h"
#include "internal/wgr_sprite2d_internal.h"
#include "internal/wgr_sprite_batch_internal.h"
#include "wgr_render.h"
#include "wgr_scene.h"
#include "wgr_camera3d.h"
#include "wgr_sprite3d.h"
#include "wgr_sprite2d.h"
#include "internal/wgr_shader_internal.h"
#include "internal/wgr_texture_internal.h"
#include "wgr_color.h"
#include "wgr_handle.h"
#include "wgr_logger.h"
#include "wgr_material.h"
#include "wgr_shader.h"
#include "wgr_texture.h"
#include "test.h"
#include "tests.h"

#include "sokol_gfx.h"
#include "sokol_time.h"

#define SHADERS "../examples/assets/shaders/"
#define EPS 1e-5f

static float value_at(wgr_handle_t material, const char *name, int component)
{
    const wgri_material_t *data = wgri_material_get(material);
    const wgri_shader_t *shader = wgri_shader_get(data->shader);
    const wgri_shader_param_t *param = &shader->params[wgri_shader_find_param(shader, name)];
    const int base = param->block == WGRI_SHADER_BLOCK_VS_PARAMS ? shader->block_size[WGRI_SHADER_BLOCK_FS_PARAMS] : 0;
    float value;
    memcpy(&value, data->custom_params + base + param->offset + 4 * component, sizeof(value));
    return value;
}

void test_shader_custom_material(void)
{
    sg_setup(&(sg_desc){.environment = wgri_platform_environment()});
    wgri_texture_init();
    wgri_shader_init();
    wgri_material_init();
    wgr_logger_set_level(WGR_LOGGER_LEVEL_FATAL); /* wrong names and files below log on purpose */

    const wgr_handle_t toon = wgr_shader_create(SHADERS "toon.wgrshader");
    CHECK(toon != 0);
    CHECK(wgr_handle_get_kind(toon) == WGR_HANDLE_KIND_SHADER);
    CHECK(wgr_shader_create(SHADERS "toon.wgrshader") == toon); /* deduped by path */
    wgr_shader_release(toon);

    const wgri_shader_t *shader = wgri_shader_get(toon);
    CHECK(shader != NULL);
    if (shader == NULL) return; /* the rest reads it */
    CHECK(shader->param_count == 3 && shader->texture_count == 1);
    CHECK(strcmp(shader->textures[0], "base_tex") == 0);
    CHECK(shader->block_size[WGRI_SHADER_BLOCK_FS_PARAMS] == 32); /* vec4 color, float bands, float rim */
    CHECK(shader->programs[0].has_block[WGRI_SHADER_BLOCK_OBJECT] && shader->programs[1].has_block[WGRI_SHADER_BLOCK_OBJECT]);
    CHECK(shader->programs[0].has_block[WGRI_SHADER_BLOCK_FRAME]);
    CHECK(shader->programs[0].view_slot[0] == 0 && shader->programs[0].sampler_slot[0] == 0);
    CHECK(shader->programs[0].env_view_slot < 0 && shader->programs[0].brdf_view_slot < 0); /* doesn't use them */

    /* a material using it: the shader's names, not the built-in ones */
    const wgr_handle_t material = wgr_material_create_custom(toon);
    CHECK(material != 0);
    CHECK(wgr_material_get_shading(material) == WGR_MATERIAL_CUSTOM);
    CHECK(wgr_material_get_shader(material) == toon);
    CHECK(!wgr_material_set_shading(material, WGR_MATERIAL_PBR));
    CHECK(wgr_material_set_float(material, "bands", 4.0f));
    CHECK_NEAR(value_at(material, "bands", 0), 4.0f, EPS);
    CHECK(wgr_material_set_float(material, "rim", 0.25f));
    CHECK_NEAR(value_at(material, "rim", 0), 0.25f, EPS);
    CHECK(wgr_material_set_color(material, "color", wgr_color_rgba(255, 0, 0, 128))); /* sRGB -> linear */
    CHECK_NEAR(value_at(material, "color", 0), 1.0f, EPS);
    CHECK_NEAR(value_at(material, "color", 1), 0.0f, EPS);
    CHECK_NEAR(value_at(material, "color", 3), 128.0f / 255.0f, 1e-3f);
    CHECK(wgr_material_set_vec4(material, "color", 0.1f, 0.2f, 0.3f, 0.4f));
    CHECK_NEAR(value_at(material, "color", 2), 0.3f, EPS);
    CHECK(!wgr_material_set_vec2(material, "bands", 1, 2)); /* a float, not a vec2 */
    CHECK(!wgr_material_set_float(material, "color", 1)); /* a vec4 */
    CHECK(!wgr_material_set_float(material, "metallic", 0)); /* built-in names don't apply */
    CHECK(!wgr_material_set_float(material, "missing", 0));

    const wgr_handle_t texture = wgr_texture_create("../examples/assets/textures/noise.png");
    CHECK(texture != 0);
    CHECK(wgr_material_set_texture(material, "base_tex", texture));
    CHECK(wgri_material_get(material)->textures[0].texture == texture);
    CHECK(wgr_material_set_texture_sampling(material, "base_tex", WGR_TEXTURE_WRAP_CLAMP, WGR_TEXTURE_WRAP_CLAMP,
                                           WGR_TEXTURE_FILTER_NEAREST));
    CHECK(!wgr_material_set_texture(material, "base_color_texture", texture));
    CHECK(!wgr_material_set_texture(material, "missing", texture));
    wgr_texture_release(texture); /* the material keeps it */

    /* the material holds the shader: released with the material's last reference */
    wgr_shader_release(toon);
    CHECK(wgri_shader_get(toon) != NULL);
    wgr_material_release(material);
    CHECK(wgri_shader_get(toon) == NULL);

    /* a vertex hook's parameters come after the fragment block */
    const wgr_handle_t wave = wgr_shader_create(SHADERS "wave.wgrshader");
    const wgr_handle_t rippling = wgr_material_create_custom(wave);
    wgr_shader_release(wave);
    shader = wgri_shader_get(wave);
    CHECK(shader != NULL && shader->texture_count == 0);
    CHECK(shader->block_size[WGRI_SHADER_BLOCK_FS_PARAMS] == 48 && shader->block_size[WGRI_SHADER_BLOCK_VS_PARAMS] == 16);
    CHECK(shader->programs[0].has_block[WGRI_SHADER_BLOCK_VS_PARAMS]);
    /* it reflects the environment: libwgrender's textures 8 and 9 in both programs, sharing
       one sampler (both are linear and clamped, and slots are scarce) */
    for (int p = 0; p < 2; p++) {
        CHECK(shader->programs[p].env_view_slot == 8 && shader->programs[p].env_sampler_slot == 8);
        CHECK(shader->programs[p].brdf_view_slot == 9 && shader->programs[p].brdf_sampler_slot == 8);
    }
    CHECK(wgr_material_set_float(rippling, "amplitude", 0.5f));
    CHECK(wgr_material_set_float(rippling, "wave_speed", 2.0f));
    CHECK(wgr_material_set_vec4(rippling, "high_color", 1, 1, 1, 1));
    CHECK_NEAR(value_at(rippling, "amplitude", 0), 0.5f, EPS);
    CHECK_NEAR(value_at(rippling, "wave_speed", 0), 2.0f, EPS);
    wgr_material_release(rippling);

    /* made by an older shaderpack: refused (wgr_frame changed), not drawn wrongly */
    FILE *old = fopen("build/old.wgrshader", "wb");
    CHECK(old != NULL);
    if (old != NULL) {
        fputs("wgrshader 1\nend\n", old);
        fclose(old);
    }
    CHECK(wgr_shader_create("build/old.wgrshader") == 0);

    /* not shaders */
    CHECK(wgr_shader_create("../examples/assets/textures/noise.png") == 0);
    CHECK(wgr_shader_create(SHADERS "missing.wgrshader") == 0);
    CHECK(wgr_material_create_custom(0) == 0);

    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    wgri_material_deinit();
    wgri_shader_deinit();
    wgri_texture_deinit();
    sg_shutdown();
}

/* Custom materials on sprites: the sprite programs in a .wgrshader (both ways of
 * getting sprite data), set_material taking custom materials only and holding a
 * reference, batches split by material, and a frame drawn through them (the dummy
 * backend validates every pipeline, uniform block and binding). */
void test_shader_sprites(void)
{
    enum { COUNT = 40 };
    wgr_handle_t sprites[COUNT];

    sg_setup(&(sg_desc){.environment = wgri_platform_environment()});
    stm_setup(); /* custom shaders get the time (wgr_get_time), which wgr_run starts */
    wgri_render_init();
    wgri_scene_init();
    wgri_camera3d_init();
    wgri_texture_init();
    wgri_shader_init();
    wgri_material_init();
    wgri_sprite3d_init();
    wgri_sprite2d_init();
    wgr_logger_set_level(WGR_LOGGER_LEVEL_FATAL); /* refusals below log on purpose */

    const wgr_handle_t shader = wgr_shader_create(SHADERS "sprite_fx.wgrshader");
    const wgri_shader_t *shader_ptr = wgri_shader_get(shader);
    CHECK(shader_ptr != NULL);
    if (shader_ptr == NULL) return;
    CHECK(shader_ptr->programs[WGRI_SHADER_PROGRAM_SPRITE].sprite_view_slot == 10);
    CHECK(shader_ptr->programs[WGRI_SHADER_PROGRAM_SPRITE].data_view_slot < 0);
    CHECK(shader_ptr->programs[WGRI_SHADER_PROGRAM_SPRITE_PULLED].data_view_slot == 11);
    CHECK(shader_ptr->programs[WGRI_SHADER_PROGRAM_SPRITE_PULLED].has_block[WGRI_SHADER_BLOCK_SPRITE_BATCH]);
    CHECK(shader_ptr->programs[WGRI_SHADER_PROGRAM_STATIC].sprite_view_slot == 10); /* white on models */
    const wgr_handle_t custom = wgr_material_create_custom(shader);
    wgr_shader_release(shader); /* the material holds it */
    const wgr_handle_t pbr = wgr_material_create(WGR_MATERIAL_PBR);

    const unsigned char pixel[4] = {255, 255, 255, 255};
    const wgr_handle_t texture = wgri_texture_create_rgba(pixel, 1, 1);
    const wgr_handle_t scene = wgr_scene_create();
    const wgr_handle_t camera = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE);
    wgr_camera3d_set_view(camera, 0, 5, 30, 0, 0, 0, 0, 1, 0);
    wgr_scene_set_active_camera(scene, camera);
    for (int i = 0; i < COUNT; i++) {
        sprites[i] = wgr_sprite3d_create(texture);
        wgr_sprite3d_set_transform(sprites[i], (float)(i % 8) - 4.0f, 0, (float)(i / 8) - 4.0f, 0, 0, 0, 1, 1, 1);
        wgr_sprite3d_set_alpha_mode(sprites[i], WGR_ALPHA_MASK, 0.5f); /* unordered: grouped */
        if (i % 2 == 0) CHECK(wgr_sprite3d_set_material(sprites[i], custom)); /* alternating */
        wgr_scene_add(scene, sprites[i], 0);
    }
    CHECK(wgr_sprite3d_set_material(sprites[1], pbr)); /* built-in: lit, like a model */
    CHECK(wgr_sprite3d_get_material(sprites[0]) == custom && wgr_sprite3d_get_material(sprites[1]) == pbr);
    const wgr_handle_t sprite2d = wgr_sprite2d_create(texture);
    CHECK(wgr_sprite2d_set_material(sprite2d, custom));
    CHECK(!wgr_sprite2d_set_material(sprite2d, pbr)); /* 2D has no lights */

    /* one texture, three materials (custom, built-in, none): three batches, however
       they interleave, drawn through the shader's sprite program and libwgrender's shading */
    wgr_render_begin_frame();
    wgr_scene_draw(scene);
    CHECK(wgri_sprite_batch_count() == 3);
    wgr_sprite2d_draw(sprite2d);
    CHECK(wgri_sprite_batch_count() == 4);
    wgr_render_end_frame();

    /* 0 goes back to libwgrender's shader: one batch again */
    for (int i = 0; i < COUNT; i++) CHECK(wgr_sprite3d_set_material(sprites[i], 0));
    wgr_render_begin_frame();
    wgr_scene_draw(scene);
    CHECK(wgri_sprite_batch_count() == 1);
    wgr_render_end_frame();

    /* the sprites held the material: it goes with the last of them */
    wgr_material_release(custom);
    CHECK(wgri_material_get(custom) != NULL); /* sprite2d still has it */
    wgr_sprite2d_destroy(sprite2d);
    CHECK(wgri_material_get(custom) == NULL);
    CHECK(wgri_shader_get(shader) == NULL); /* and the material held the shader */

    for (int i = 0; i < COUNT; i++) wgr_sprite3d_destroy(sprites[i]);
    wgr_material_release(pbr);
    wgr_texture_release(texture);
    wgr_scene_destroy(scene);
    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    wgri_sprite2d_deinit();
    wgri_sprite3d_deinit();
    wgri_material_deinit();
    wgri_shader_deinit();
    wgri_texture_deinit();
    wgri_camera3d_deinit();
    wgri_scene_deinit();
    wgri_render_deinit();
    sg_shutdown();
}

/* Screen effects (wgr_render_add_effect): a screen shader has one program and reads the
 * frame as a texture; its material is refused on surfaces, and a surface material is
 * refused as an effect. The chain runs at wgr_render_end_frame, ping-ponging between two
 * render targets when effects follow each other. */
void test_shader_effects(void)
{
    sg_setup(&(sg_desc){.environment = wgri_platform_environment()});
    stm_setup();
    wgri_render_init();
    wgri_texture_init();
    wgri_shader_init();
    wgri_material_init();
    wgri_sprite3d_init();
    wgri_sprite2d_init();
    wgri_effect_init();
    wgr_logger_set_level(WGR_LOGGER_LEVEL_FATAL); /* refusals below log on purpose */

    const wgr_handle_t vignette = wgr_shader_create(SHADERS "vignette.wgrshader");
    const wgr_handle_t scanlines = wgr_shader_create(SHADERS "scanlines.wgrshader");
    const wgr_handle_t toon = wgr_shader_create(SHADERS "toon.wgrshader");
    const wgri_shader_t *screen = wgri_shader_get(vignette);
    CHECK(screen != NULL);
    if (screen == NULL) return;

    /* a screen shader: its one program, reading the frame at libwgrender's texture slot 8 */
    CHECK(screen->screen);
    CHECK(!wgri_shader_get(toon)->screen);
    CHECK(screen->programs[WGRI_SHADER_PROGRAM_SCREEN].screen_view_slot == 8);
    CHECK(screen->programs[WGRI_SHADER_PROGRAM_SCREEN].screen_sampler_slot == 8);
    CHECK(screen->programs[WGRI_SHADER_PROGRAM_SCREEN].has_block[WGRI_SHADER_BLOCK_FRAME]);
    CHECK(screen->programs[WGRI_SHADER_PROGRAM_SCREEN].has_block[WGRI_SHADER_BLOCK_FS_PARAMS]);
    CHECK(screen->programs[WGRI_SHADER_PROGRAM_SCREEN].shader.id != SG_INVALID_ID);
    CHECK(screen->programs[WGRI_SHADER_PROGRAM_STATIC].shader.id == SG_INVALID_ID); /* no surface programs */
    CHECK(screen->param_count == 3 && screen->block_size[WGRI_SHADER_BLOCK_FS_PARAMS] == 32);

    const wgr_handle_t dark = wgr_material_create_custom(vignette);
    wgr_shader_release(vignette); /* the material holds it from here */
    const wgr_handle_t crt = wgr_material_create_custom(scanlines);
    const wgr_handle_t surface = wgr_material_create_custom(toon);
    const wgr_handle_t pbr = wgr_material_create(WGR_MATERIAL_PBR);
    CHECK(wgri_material_is_screen(dark) && wgri_material_is_screen(crt));
    CHECK(!wgri_material_is_screen(surface) && !wgri_material_is_screen(pbr));
    CHECK(wgr_material_set_float(dark, "strength", 0.75f)); /* parameters like any material */
    CHECK_NEAR(value_at(dark, "strength", 0), 0.75f, EPS);

    /* the chain: added in order, counted, cleared */
    CHECK(wgr_render_effect_count() == 0);
    CHECK(!wgr_render_add_effect(surface)); /* a surface shader has no screen program */
    CHECK(!wgr_render_add_effect(pbr));     /* nor does a built-in material */
    CHECK(!wgr_render_add_effect(0));
    CHECK(wgr_render_effect_count() == 0);
    CHECK(wgr_render_add_effect(dark));
    CHECK(wgr_render_effect_count() == 1);

    /* a screen material is refused where a surface is drawn */
    const unsigned char pixel[4] = {255, 255, 255, 255};
    const wgr_handle_t texture = wgri_texture_create_rgba(pixel, 1, 1);
    const wgr_handle_t sprite = wgr_sprite3d_create(texture);
    const wgr_handle_t sprite2d = wgr_sprite2d_create(texture);
    CHECK(!wgr_sprite3d_set_material(sprite, dark));
    CHECK(!wgr_sprite2d_set_material(sprite2d, dark));
    CHECK(wgr_sprite3d_get_material(sprite) == 0 && wgr_sprite2d_get_material(sprite2d) == 0);

    /* one effect: the frame draws into a target and the effect puts it on the screen */
    wgr_render_begin_frame();
    wgr_render_clear_background(WGR_COLOR_BLACK);
    wgr_render_end_frame();
    const wgri_shader_t *after = wgri_shader_get(vignette);
    CHECK(after->screen_pipeline.id != SG_INVALID_ID); /* made on first use */

    /* two effects: the first draws into the second's source */
    CHECK(wgr_render_add_effect(crt));
    CHECK(wgr_render_effect_count() == 2);
    wgr_render_begin_frame();
    wgr_render_end_frame();

    /* the same material twice is a chain of two, not one */
    wgr_render_clear_effects();
    CHECK(wgr_render_effect_count() == 0);
    CHECK(wgr_render_add_effect(dark) && wgr_render_add_effect(dark));
    CHECK(wgr_render_effect_count() == 2);
    wgr_render_begin_frame();
    wgr_render_end_frame();

    /* the chain holds a reference to each material until it's cleared */
    wgr_material_release(dark);
    CHECK(wgri_material_get(dark) != NULL);
    wgr_render_clear_effects();
    CHECK(wgri_material_get(dark) == NULL);
    CHECK(wgri_shader_get(vignette) == NULL); /* and the material held the shader */

    wgr_sprite2d_destroy(sprite2d);
    wgr_sprite3d_destroy(sprite);
    wgr_texture_release(texture);
    wgr_material_release(crt);
    wgr_material_release(surface);
    wgr_material_release(pbr);
    wgr_shader_release(scanlines);
    wgr_shader_release(toon);
    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    wgri_effect_deinit();
    wgri_sprite2d_deinit();
    wgri_sprite3d_deinit();
    wgri_material_deinit();
    wgri_shader_deinit();
    wgri_texture_deinit();
    wgri_render_deinit();
    sg_shutdown();
}
