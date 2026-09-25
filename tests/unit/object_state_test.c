/* Object state across subsystems -- text3d, sprite3d, model, sound, asset host -- on
 * sokol's dummy backend. */
#include <math.h>
#include <string.h>

#include "internal/wgr_audio_internal.h"
#include "internal/wgr_environment_internal.h"
#include "internal/wgr_internal_internal.h"
#include "internal/wgr_light_internal.h"
#include "internal/wgr_material_internal.h"
#include "internal/wgr_model_internal.h"
#include "internal/wgr_platform_internal.h"
#include "internal/wgr_render_internal.h"
#include "internal/wgr_scene_internal.h"
#include "wgr_asset.h"
#include "wgr_audio.h"
#include "wgr_camera3d.h"
#include "wgr_font.h"
#include "wgr_logger.h"
#include "wgr_model.h"
#include "wgr_pick.h"
#include "wgr_scene.h"
#include "wgr_sound.h"
#include "wgr_sprite3d.h"
#include "wgr_text3d.h"
#include "test.h"
#include "tests.h"

#include "sokol_gfx.h"

#define EPS 1e-3f

static void setup(void)
{
    sg_setup(&(sg_desc){.environment = wgri_platform_environment()});
    wgri_render_init();
    wgri_scene_init();
    wgri_camera3d_init();
    wgri_texture_init();
    wgri_sprite3d_init();
    wgri_light_init();
    wgri_material_init();
    wgri_environment_init();
    wgri_model_init();
    wgri_font_init();
    wgri_text3d_init();
    wgr_logger_set_level(WGR_LOGGER_LEVEL_FATAL);
}

static void teardown(void)
{
    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    wgri_text3d_deinit();
    wgri_font_deinit();
    wgri_model_deinit();
    wgri_environment_deinit();
    wgri_material_deinit();
    wgri_light_deinit();
    wgri_sprite3d_deinit();
    wgri_texture_deinit();
    wgri_camera3d_deinit();
    wgri_scene_deinit();
    wgri_render_deinit();
    sg_shutdown();
}

void test_text3d_state(void)
{
    setup();
    wgr_handle_t camera = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE);
    wgr_camera3d_set_view(camera, 0, 0, 10, 0, 0, 0, 0, 1, 0);
    wgr_handle_t font = wgr_font_create("examples/assets/fonts/JetBrainsMono/JetBrainsMono-Regular.ttf");
    CHECK(font != 0);

    wgr_handle_t text = wgr_text3d_create(0);
    CHECK(wgr_text3d_set_text(text, "Hello"));
    CHECK(wgr_text3d_set_size(text, 1.0f));
    CHECK(!wgr_text3d_set_size(text, 0.0f));
    CHECK(wgr_text3d_get_size(text).x == 0.0f); /* no font yet: nothing to measure or hit */
    CHECK(!wgr_pick_object(text, camera, 0.5f, 0.5f).hit);

    CHECK(wgr_text3d_set_font(text, font));
    vec2_t size = wgr_text3d_get_size(text);
    CHECK(size.x > 1.5f && size.x < 4.0f); /* five monospace glyphs, each about 0.45 of the font size */
    CHECK(size.y > 0.9f && size.y < 1.6f); /* one line at size 1: the font's line height, not the ink */

    /* centered on its position, facing the camera */
    wgr_pick_result_t r = wgr_pick_object(text, camera, 0.5f, 0.5f);
    CHECK(r.hit && r.handle == text);
    CHECK_NEAR(r.distance, 9.99f, EPS);
    wgr_text3d_set_transform(text, size.x * 0.45f, 0, 0, 0, 0, 0); /* center still inside the text */
    CHECK(wgr_pick_object(text, camera, 0.5f, 0.5f).hit);
    wgr_text3d_set_transform(text, size.x * 0.55f, 0, 0, 0, 0, 0); /* now outside */
    CHECK(!wgr_pick_object(text, camera, 0.5f, 0.5f).hit);

    /* FREE facing follows the rotation: turned 1.2 rad around y, the text's
     * half width shrinks to about 0.36 of it, so a point 0.3 of the width off
     * center is missed; facing the camera again, it's hit */
    wgr_text3d_set_transform(text, size.x * 0.3f, 0, 0, 0, 1.2f, 0);
    CHECK(wgr_text3d_set_facing(text, WGR_SPRITE3D_FACING_FREE));
    CHECK(!wgr_text3d_set_facing(text, (wgr_sprite3d_facing_t)9));
    CHECK(!wgr_pick_object(text, camera, 0.5f, 0.5f).hit);
    wgr_text3d_set_facing(text, WGR_SPRITE3D_FACING_CAMERA); /* the camera-facing modes ignore rotation */
    CHECK(wgr_pick_object(text, camera, 0.5f, 0.5f).hit);
    wgr_text3d_set_transform(text, 0, 0, 0, 0, 0, 0);

    /* flags and scenes */
    wgr_handle_t scene = wgr_scene_create();
    wgr_scene_add(scene, text, 0);
    CHECK(wgr_scene_pick(scene, camera, 0.5f, 0.5f).handle == text);
    CHECK(wgr_text3d_set_pickable(text, false) && !wgr_text3d_is_pickable(text));
    CHECK(!wgr_scene_pick(scene, camera, 0.5f, 0.5f).hit);
    wgr_text3d_set_pickable(text, true);
    CHECK(wgr_text3d_set_visible(text, false) && !wgr_text3d_is_visible(text));
    CHECK(!wgr_scene_pick(scene, camera, 0.5f, 0.5f).hit);

    wgr_text3d_destroy(text);
    CHECK(!wgr_text3d_is_visible(text));
    teardown();
}

void test_sprite3d_state(void)
{
    setup();
    wgr_handle_t camera = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE);
    wgr_camera3d_set_view(camera, 0, 0, 10, 0, 0, 0, 0, 1, 0);
    wgr_handle_t sprite = wgr_sprite3d_create(0);

    CHECK(wgr_sprite3d_set_transform(sprite, 1, 2, 3, 0.1f, 0.2f, 0.3f, 4, 5, 6));
    CHECK_VEC3_NEAR(wgr_sprite3d_get_position(sprite), 1, 2, 3, EPS);
    CHECK_VEC3_NEAR(wgr_sprite3d_get_rotation(sprite), 0.1f, 0.2f, 0.3f, EPS);
    CHECK_VEC3_NEAR(wgr_sprite3d_get_scale(sprite), 4, 5, 6, EPS);
    CHECK_VEC3_NEAR(wgr_sprite3d_get_position(0), 0, 0, 0, EPS);

    wgr_sprite3d_set_transform(sprite, 0, 0, 0, 0, 0, 0, 1, 1, 1);
    CHECK(wgr_pick_object(sprite, camera, 0.5f, 0.5f).hit);
    CHECK(wgr_sprite3d_set_pickable(sprite, false) && !wgr_sprite3d_is_pickable(sprite));
    CHECK(!wgr_pick_object(sprite, camera, 0.5f, 0.5f).hit);
    wgr_sprite3d_set_pickable(sprite, true);
    /* FREE facing follows the rotation (see test_text3d_state) */
    wgr_sprite3d_set_transform(sprite, 0.3f, 0, 0, 0, 1.2f, 0, 1, 1, 1);
    CHECK(wgr_pick_object(sprite, camera, 0.5f, 0.5f).hit); /* facing the camera: half width 0.5 */
    CHECK(wgr_sprite3d_set_facing(sprite, WGR_SPRITE3D_FACING_FREE));
    CHECK(!wgr_pick_object(sprite, camera, 0.5f, 0.5f).hit); /* turned: half width 0.18 */
    CHECK(!wgr_sprite3d_set_facing(sprite, (wgr_sprite3d_facing_t)42));
    teardown();
}

void test_model_state(void)
{
    setup();
    wgr_handle_t camera = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE);
    wgr_camera3d_set_view(camera, 0, 1, 12, 0, 1, 0, 0, 1, 0); /* the character is about 2 tall */
    wgr_handle_t model = wgr_model_create(0);

    CHECK(!wgr_model_is_ready(model));
    CHECK(wgr_model_get_animation_duration(model, 0) == 0.0f);
    CHECK(wgr_model_set_animation(model, 3));
    CHECK(wgr_model_set_animation_time(model, 0.25f)); /* kept until the mesh arrives */

    wgr_handle_t mesh = wgr_mesh_create("examples/assets/" CHARACTER_PATH);
    CHECK(mesh != 0);
    wgr_model_set_mesh(model, mesh);
    wgr_mesh_release(mesh);
    CHECK(wgr_model_is_ready(model));

    const float duration = wgr_model_get_animation_duration(model, 3);
    CHECK(duration > 0.0f);
    CHECK(wgr_model_get_animation_duration(model, 99) == 0.0f);
    CHECK(wgr_model_set_animation_time(model, duration * 2.5f)); /* looping: wraps */
    CHECK_NEAR(wgr_model_get_animation_time(model), duration * 0.5f, EPS);
    wgr_model_set_animation_loop(model, false);
    CHECK(wgr_model_set_animation_time(model, duration * 3.0f)); /* not looping: clamps */
    CHECK_NEAR(wgr_model_get_animation_time(model), duration, EPS);
    CHECK(wgr_model_animate(model, 0.1f));
    CHECK_NEAR(wgr_model_get_animation_time(model), duration, EPS); /* stays at the end */

    CHECK(wgr_model_is_pickable(model));
    CHECK(wgr_pick_object(model, camera, 0.5f, 0.5f).hit); /* the character stands at the origin */
    CHECK(wgr_model_set_pickable(model, false));
    CHECK(!wgr_pick_object(model, camera, 0.5f, 0.5f).hit);
    teardown();
}

void test_sound_pan(void)
{
    float buffer[64 * 2];

    wgri_audio_init();
    wgri_sound_init();
    wgr_handle_t audio = wgr_audio_create("examples/assets/sounds/click_004.ogg");
    wgr_handle_t sound = wgr_sound_create(audio);
    wgr_audio_release(audio);
    wgr_sound_set_loop(sound, true);

    /* find the loudest frame at center, then check the same frame panned */
    const int frame = 20;
    wgr_sound_play(sound);
    wgri_audio_mix(buffer, 64, 44100);
    const float center_l = buffer[frame * 2], center_r = buffer[frame * 2 + 1];

    CHECK(wgr_sound_set_pan(sound, 1.0f)); /* right only */
    wgr_sound_play(sound);
    wgri_audio_mix(buffer, 64, 44100);
    CHECK_NEAR(buffer[frame * 2], 0.0f, 1e-6f);
    CHECK_NEAR(buffer[frame * 2 + 1], center_r, 1e-6f);

    CHECK(wgr_sound_set_pan(sound, -0.5f)); /* left full, right half */
    wgr_sound_play(sound);
    wgri_audio_mix(buffer, 64, 44100);
    CHECK_NEAR(buffer[frame * 2], center_l, 1e-6f);
    CHECK_NEAR(buffer[frame * 2 + 1], center_r * 0.5f, 1e-6f);

    CHECK(wgr_sound_set_pan(sound, -5.0f)); /* clamped to -1 */
    wgr_sound_play(sound);
    wgri_audio_mix(buffer, 64, 44100);
    CHECK_NEAR(buffer[frame * 2 + 1], 0.0f, 1e-6f);

    wgr_sound_destroy(sound);
    wgri_sound_deinit();
    wgri_audio_deinit();
}

void test_asset_host(void)
{
    wgr_asset_set_host("https://example.com/assets///");
    CHECK(strcmp(wgr_asset_get_host(), "https://example.com/assets") == 0);
    wgr_asset_set_host(NULL);
    CHECK(strcmp(wgr_asset_get_host(), "") == 0);
}
