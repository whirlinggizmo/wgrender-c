/* Functions added for librl parity (docs/PLAN-parity.md), on sokol's dummy backend. */
#include <math.h>
#include <string.h>

#include "internal/sk_audio.h"
#include "internal/sk_environment.h"
#include "internal/sk_internal.h"
#include "internal/sk_light.h"
#include "internal/sk_material.h"
#include "internal/sk_model.h"
#include "internal/sk_platform.h"
#include "internal/sk_render.h"
#include "internal/sk_scene.h"
#include "sk_asset.h"
#include "sk_audio.h"
#include "sk_camera3d.h"
#include "sk_font.h"
#include "sk_logger.h"
#include "sk_model.h"
#include "sk_pick.h"
#include "sk_scene.h"
#include "sk_sound.h"
#include "sk_sprite3d.h"
#include "sk_text3d.h"
#include "test.h"
#include "tests.h"

#include "sokol_gfx.h"

#define EPS 1e-3f

static void setup(void)
{
    sg_setup(&(sg_desc){.environment = sk_platform_environment()});
    sk_render_init();
    sk_scene_init();
    sk_color_init();
    sk_camera3d_init();
    sk_texture_init();
    sk_sprite3d_init();
    sk_light_init();
    sk_material_init();
    sk_environment_init();
    sk_model_init();
    sk_font_init();
    sk_text3d_init();
    sk_logger_set_level(SK_LOGGER_LEVEL_FATAL);
}

static void teardown(void)
{
    sk_logger_set_level(SK_LOGGER_LEVEL_INFO);
    sk_text3d_deinit();
    sk_font_deinit();
    sk_model_deinit();
    sk_environment_deinit();
    sk_material_deinit();
    sk_light_deinit();
    sk_sprite3d_deinit();
    sk_texture_deinit();
    sk_camera3d_deinit();
    sk_color_deinit();
    sk_scene_deinit();
    sk_render_deinit();
    sg_shutdown();
}

void test_parity_text3d(void)
{
    setup();
    sk_handle_t camera = sk_camera3d_create(SK_CAMERA3D_PERSPECTIVE);
    sk_camera3d_set_view(camera, 0, 0, 10, 0, 0, 0, 0, 1, 0);
    sk_handle_t font = sk_font_create("../examples/assets/fonts/JetBrainsMono/JetBrainsMono-Regular.ttf");
    CHECK(font != 0);

    sk_handle_t text = sk_text3d_create(0);
    CHECK(sk_text3d_set_text(text, "Hello"));
    CHECK(sk_text3d_set_size(text, 1.0f));
    CHECK(!sk_text3d_set_size(text, 0.0f));
    CHECK(sk_text3d_get_size(text).x == 0.0f); /* no font yet: nothing to measure or hit */
    CHECK(!sk_pick_object(text, camera, 0.5f, 0.5f).hit);

    CHECK(sk_text3d_set_font(text, font));
    vec2_t size = sk_text3d_get_size(text);
    CHECK(size.x > 1.5f && size.x < 4.0f); /* five monospace glyphs, each about 0.45 of the font size */
    CHECK(size.y > 0.4f && size.y < 1.0f); /* the ink of "Hello": capitals, no descenders */

    /* centered on its position, facing the camera */
    sk_pick_result_t r = sk_pick_object(text, camera, 0.5f, 0.5f);
    CHECK(r.hit && r.handle == text);
    CHECK_NEAR(r.distance, 9.99f, EPS);
    sk_text3d_set_transform(text, size.x * 0.45f, 0, 0, 0, 0, 0); /* center still inside the text */
    CHECK(sk_pick_object(text, camera, 0.5f, 0.5f).hit);
    sk_text3d_set_transform(text, size.x * 0.55f, 0, 0, 0, 0, 0); /* now outside */
    CHECK(!sk_pick_object(text, camera, 0.5f, 0.5f).hit);

    /* FREE facing follows the rotation: turned 1.2 rad around y, the text's
     * half width shrinks to about 0.36 of it, so a point 0.3 of the width off
     * center is missed; facing the camera again, it's hit */
    sk_text3d_set_transform(text, size.x * 0.3f, 0, 0, 0, 1.2f, 0);
    CHECK(sk_text3d_set_facing(text, SK_SPRITE3D_FACING_FREE));
    CHECK(!sk_text3d_set_facing(text, (sk_sprite3d_facing_t)9));
    CHECK(!sk_pick_object(text, camera, 0.5f, 0.5f).hit);
    sk_text3d_set_facing(text, SK_SPRITE3D_FACING_CAMERA); /* the camera-facing modes ignore rotation */
    CHECK(sk_pick_object(text, camera, 0.5f, 0.5f).hit);
    sk_text3d_set_transform(text, 0, 0, 0, 0, 0, 0);

    /* flags and scenes */
    sk_handle_t scene = sk_scene_create();
    sk_scene_add(scene, text, 0);
    CHECK(sk_scene_pick(scene, camera, 0.5f, 0.5f).handle == text);
    CHECK(sk_text3d_set_pickable(text, false) && !sk_text3d_is_pickable(text));
    CHECK(!sk_scene_pick(scene, camera, 0.5f, 0.5f).hit);
    sk_text3d_set_pickable(text, true);
    CHECK(sk_text3d_set_visible(text, false) && !sk_text3d_is_visible(text));
    CHECK(!sk_scene_pick(scene, camera, 0.5f, 0.5f).hit);

    sk_text3d_destroy(text);
    CHECK(!sk_text3d_is_visible(text));
    teardown();
}

void test_parity_sprite3d(void)
{
    setup();
    sk_handle_t camera = sk_camera3d_create(SK_CAMERA3D_PERSPECTIVE);
    sk_camera3d_set_view(camera, 0, 0, 10, 0, 0, 0, 0, 1, 0);
    sk_handle_t sprite = sk_sprite3d_create(0);

    CHECK(sk_sprite3d_set_transform(sprite, 1, 2, 3, 0.1f, 0.2f, 0.3f, 4, 5, 6));
    CHECK_VEC3_NEAR(sk_sprite3d_get_position(sprite), 1, 2, 3, EPS);
    CHECK_VEC3_NEAR(sk_sprite3d_get_rotation(sprite), 0.1f, 0.2f, 0.3f, EPS);
    CHECK_VEC3_NEAR(sk_sprite3d_get_scale(sprite), 4, 5, 6, EPS);
    CHECK_VEC3_NEAR(sk_sprite3d_get_position(0), 0, 0, 0, EPS);

    sk_sprite3d_set_transform(sprite, 0, 0, 0, 0, 0, 0, 1, 1, 1);
    CHECK(sk_pick_object(sprite, camera, 0.5f, 0.5f).hit);
    CHECK(sk_sprite3d_set_pickable(sprite, false) && !sk_sprite3d_is_pickable(sprite));
    CHECK(!sk_pick_object(sprite, camera, 0.5f, 0.5f).hit);
    sk_sprite3d_set_pickable(sprite, true);
    /* FREE facing follows the rotation (see test_parity_text3d) */
    sk_sprite3d_set_transform(sprite, 0.3f, 0, 0, 0, 1.2f, 0, 1, 1, 1);
    CHECK(sk_pick_object(sprite, camera, 0.5f, 0.5f).hit); /* facing the camera: half width 0.5 */
    CHECK(sk_sprite3d_set_facing(sprite, SK_SPRITE3D_FACING_FREE));
    CHECK(!sk_pick_object(sprite, camera, 0.5f, 0.5f).hit); /* turned: half width 0.18 */
    CHECK(!sk_sprite3d_set_facing(sprite, (sk_sprite3d_facing_t)42));
    teardown();
}

void test_parity_model(void)
{
    setup();
    sk_handle_t camera = sk_camera3d_create(SK_CAMERA3D_PERSPECTIVE);
    sk_camera3d_set_view(camera, 0, 1, 12, 0, 1, 0, 0, 1, 0); /* the gumshoe is 1.77 tall */
    sk_handle_t model = sk_model_create(0);

    CHECK(!sk_model_is_ready(model));
    CHECK(sk_model_get_animation_duration(model, 0) == 0.0f);
    CHECK(sk_model_set_animation(model, 3));
    CHECK(sk_model_set_animation_time(model, 0.25f)); /* kept until the mesh arrives */

    sk_handle_t mesh = sk_mesh_create("../examples/assets/models/gumshoe/gumshoe.glb");
    CHECK(mesh != 0);
    sk_model_set_mesh(model, mesh);
    sk_mesh_destroy(mesh);
    CHECK(sk_model_is_ready(model));

    const float duration = sk_model_get_animation_duration(model, 3);
    CHECK(duration > 0.0f);
    CHECK(sk_model_get_animation_duration(model, 99) == 0.0f);
    CHECK(sk_model_set_animation_time(model, duration * 2.5f)); /* looping: wraps */
    CHECK_NEAR(sk_model_get_animation_time(model), duration * 0.5f, EPS);
    sk_model_set_animation_loop(model, false);
    CHECK(sk_model_set_animation_time(model, duration * 3.0f)); /* not looping: clamps */
    CHECK_NEAR(sk_model_get_animation_time(model), duration, EPS);
    CHECK(sk_model_animate(model, 0.1f));
    CHECK_NEAR(sk_model_get_animation_time(model), duration, EPS); /* stays at the end */

    CHECK(sk_model_is_pickable(model));
    CHECK(sk_pick_object(model, camera, 0.5f, 0.5f).hit); /* the gumshoe stands at the origin */
    CHECK(sk_model_set_pickable(model, false));
    CHECK(!sk_pick_object(model, camera, 0.5f, 0.5f).hit);
    teardown();
}

void test_parity_sound_pan(void)
{
    float buffer[64 * 2];

    sk_audio_init();
    sk_sound_init();
    sk_handle_t audio = sk_audio_create("../examples/assets/sounds/click_004.ogg");
    sk_handle_t sound = sk_sound_create(audio);
    sk_audio_destroy(audio);
    sk_sound_set_loop(sound, true);

    /* find the loudest frame at center, then check the same frame panned */
    const int frame = 20;
    sk_sound_play(sound);
    sk_audio_mix(buffer, 64, 44100);
    const float center_l = buffer[frame * 2], center_r = buffer[frame * 2 + 1];

    CHECK(sk_sound_set_pan(sound, 1.0f)); /* right only */
    sk_sound_play(sound);
    sk_audio_mix(buffer, 64, 44100);
    CHECK_NEAR(buffer[frame * 2], 0.0f, 1e-6f);
    CHECK_NEAR(buffer[frame * 2 + 1], center_r, 1e-6f);

    CHECK(sk_sound_set_pan(sound, -0.5f)); /* left full, right half */
    sk_sound_play(sound);
    sk_audio_mix(buffer, 64, 44100);
    CHECK_NEAR(buffer[frame * 2], center_l, 1e-6f);
    CHECK_NEAR(buffer[frame * 2 + 1], center_r * 0.5f, 1e-6f);

    CHECK(sk_sound_set_pan(sound, -5.0f)); /* clamped to -1 */
    sk_sound_play(sound);
    sk_audio_mix(buffer, 64, 44100);
    CHECK_NEAR(buffer[frame * 2 + 1], 0.0f, 1e-6f);

    sk_sound_destroy(sound);
    sk_sound_deinit();
    sk_audio_deinit();
}

void test_parity_asset_host(void)
{
    sk_asset_set_host("https://example.com/assets///");
    CHECK(strcmp(sk_asset_get_host(), "https://example.com/assets") == 0);
    sk_asset_set_host(NULL);
    CHECK(strcmp(sk_asset_get_host(), "") == 0);
}
