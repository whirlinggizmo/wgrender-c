/* Particle emitters (sk_emitter.c): spawning by rate and burst, lives, the ring's
 * limit, seeds, the velocity cone, the clock's rebase, and drawing in a scene. The
 * frame update is driven directly (sk_emitter_update), as the runtime would. */
#include <math.h>

#include "internal/sk_camera3d.h"
#include "internal/sk_emitter.h"
#include "internal/sk_internal.h"
#include "internal/sk_platform.h"
#include "internal/sk_render.h"
#include "internal/sk_scene.h"
#include "internal/sk_sprite_batch.h"
#include "internal/sk_texture.h"
#include "sk_camera3d.h"
#include "sk_color.h"
#include "sk_emitter2d.h"
#include "sk_emitter3d.h"
#include "sk_logger.h"
#include "sk_render.h"
#include "sk_scene.h"
#include "sk_texture.h"
#include "test.h"
#include "tests.h"

#include "sokol_gfx.h"

static void start(void)
{
    sg_setup(&(sg_desc){.environment = sk_platform_environment()});
    sk_render_init();
    sk_scene_init();
    sk_camera3d_init();
    sk_texture_init();
    sk_emitter_init();
    sk_logger_set_level(SK_LOGGER_LEVEL_ERROR);
}

static void stop(void)
{
    sk_logger_set_level(SK_LOGGER_LEVEL_INFO);
    sk_emitter_deinit();
    sk_texture_deinit();
    sk_camera3d_deinit();
    sk_scene_deinit();
    sk_render_deinit();
    sg_shutdown();
}

void test_emitter_spawning(void)
{
    start();
    const sk_handle_t e = sk_emitter3d_create(sk_texture_get_default());
    CHECK(e != 0);
    CHECK(sk_emitter3d_is_emitting(e));
    CHECK(sk_emitter3d_get_count(e) == 0);

    /* a steady rate: 100 a second for half a second, each living 1 s */
    CHECK(sk_emitter3d_set_rate(e, 100));
    CHECK(sk_emitter3d_set_life(e, 1, 1));
    for (int i = 0; i < 10; i++) sk_emitter_update(0.05f);
    CHECK(sk_emitter3d_get_count(e) == 50);
    for (int i = 0; i < 20; i++) sk_emitter_update(0.05f); /* 1.5 s in: the first 0.5 s have died */
    CHECK(sk_emitter3d_get_count(e) >= 99 && sk_emitter3d_get_count(e) <= 101);

    /* stop: the living finish their lives */
    CHECK(sk_emitter3d_set_emitting(e, false));
    sk_emitter_update(0.5f);
    CHECK(sk_emitter3d_get_count(e) > 0);
    sk_emitter_update(0.6f);
    CHECK(sk_emitter3d_get_count(e) == 0);

    /* bursts, capped by max (the newest replace the oldest) */
    CHECK(sk_emitter3d_burst(e, 30));
    CHECK(sk_emitter3d_get_count(e) == 30);
    CHECK(sk_emitter3d_set_max(e, 64)); /* a new ring: empty */
    CHECK(sk_emitter3d_get_count(e) == 0);
    CHECK(sk_emitter3d_burst(e, 100));
    CHECK(sk_emitter3d_get_count(e) == 64);
    sk_emitter3d_clear(e);
    CHECK(sk_emitter3d_get_count(e) == 0);

    /* refused */
    CHECK(!sk_emitter3d_set_life(e, 0, 1));
    CHECK(!sk_emitter3d_set_life(e, 2, 1));
    CHECK(!sk_emitter3d_set_max(e, 0));
    CHECK(!sk_emitter3d_set_rate(e, -1));
    CHECK(!sk_emitter3d_set_alpha_mode(e, (sk_alpha_mode_t)9, 0));
    CHECK(!sk_emitter2d_set_rate(e, 10)); /* a 3D emitter isn't a 2D one */
    sk_emitter3d_destroy(e);
    CHECK(!sk_emitter3d_set_rate(e, 10)); /* gone */
    stop();
}

void test_emitter_particles(void)
{
    float born_a[4], motion_a[4], born_b[4], motion_b[4], shape[4];
    bool same = true, in_cone = true, in_fan = true;
    start();

    /* the same seed and settings: the same particles */
    const sk_handle_t a = sk_emitter3d_create(sk_texture_get_default());
    const sk_handle_t b = sk_emitter3d_create(sk_texture_get_default());
    for (int k = 0; k < 2; k++) {
        const sk_handle_t e = k == 0 ? a : b;
        CHECK(sk_emitter3d_set_seed(e, 42));
        CHECK(sk_emitter3d_set_position(e, 1, 2, 3));
        CHECK(sk_emitter3d_set_spawn_box(e, 0.5f, 0, 0));
        CHECK(sk_emitter3d_set_velocity(e, 0, 10, 0, 0.3f, 0.2f));
        CHECK(sk_emitter3d_set_life(e, 1, 2));
        CHECK(sk_emitter3d_burst(e, 200));
    }
    for (int i = 0; i < 200; i++) {
        CHECK(sk_emitter_particle(a, i, born_a, motion_a, shape) && sk_emitter_particle(b, i, born_b, motion_b, shape));
        for (int c = 0; c < 4; c++) same = same && born_a[c] == born_b[c] && motion_a[c] == motion_b[c];
        /* within the box, the cone (0.3 rad around +y), the speed and the life */
        const float speed = sqrtf(motion_a[0] * motion_a[0] + motion_a[1] * motion_a[1] + motion_a[2] * motion_a[2]);
        in_cone = in_cone && fabsf(born_a[0] - 1) <= 0.5f && born_a[1] == 2 && born_a[2] == 3 && speed >= 8 - 1e-3f &&
                  speed <= 12 + 1e-3f && motion_a[1] / speed >= cosf(0.3f) - 1e-4f && motion_a[3] >= 1 &&
                  motion_a[3] <= 2;
    }
    CHECK(same);
    CHECK(in_cone);
    CHECK(!sk_emitter_particle(a, 200, born_a, motion_a, shape));

    /* 2D: turned up to `spread` either way, in the screen's plane */
    const sk_handle_t flat = sk_emitter2d_create(sk_texture_get_default());
    CHECK(sk_emitter2d_set_velocity(flat, 100, 0, 0.5f, 0));
    CHECK(sk_emitter2d_burst(flat, 100));
    for (int i = 0; i < 100; i++) {
        sk_emitter_particle(flat, i, born_a, motion_a, shape);
        in_fan = in_fan && motion_a[2] == 0 && fabsf(atan2f(motion_a[1], motion_a[0])) <= 0.5f + 1e-4f;
    }
    CHECK(in_fan);
    CHECK(sk_emitter2d_get_position(flat).x == 0);
    CHECK(!sk_emitter3d_set_rate(flat, 10));

    /* the clock rebases (the shader's time is a float): particles keep their ages */
    CHECK(sk_emitter3d_set_life(b, 8000, 8000));
    sk_emitter3d_clear(b);
    CHECK(sk_emitter3d_burst(b, 3));
    sk_emitter_update(3000.0f);
    sk_emitter_update(3000.0f); /* past 4096: rebased, 6000 s old of 8000 */
    CHECK(sk_emitter3d_get_count(b) == 3);
    sk_emitter_update(3000.0f); /* 9000 > 8000 */
    CHECK(sk_emitter3d_get_count(b) == 0);

    sk_emitter3d_destroy(a);
    sk_emitter3d_destroy(b);
    sk_emitter2d_destroy(flat);
    stop();
}

/* Emitters in a scene draw as render callbacks; destroying one takes it out. */
void test_emitter_scene(void)
{
    start();
    const sk_handle_t scene = sk_scene_create();
    const sk_handle_t camera = sk_camera3d_create(SK_CAMERA3D_PERSPECTIVE);
    sk_camera3d_set_view(camera, 0, 2, 10, 0, 0, 0, 0, 1, 0);
    sk_scene_set_active_camera(scene, camera);
    const sk_handle_t fire = sk_emitter3d_create(sk_texture_get_default());
    const sk_handle_t smoke = sk_emitter3d_create(sk_texture_get_default());
    const sk_handle_t confetti = sk_emitter2d_create(sk_texture_get_default());
    CHECK(sk_emitter3d_set_alpha_mode(smoke, SK_ALPHA_BLEND, 0));
    sk_emitter3d_burst(fire, 50);
    sk_emitter3d_burst(smoke, 50);
    sk_emitter2d_burst(confetti, 50);
    CHECK(sk_scene_add(scene, fire, 0) && sk_scene_add(scene, smoke, 0) && sk_scene_add(scene, confetti, 0));

    sk_render_begin();
    const int before = sk_render_command_count();
    sk_scene_draw(scene);
    CHECK(sk_render_command_count() >= before + 3); /* a draw each (and the layers between) */
    sk_render_end();

    sk_emitter3d_destroy(fire);
    CHECK(!sk_scene_set_layer(scene, fire, 1)); /* not a member any more */
    sk_render_begin();
    sk_scene_draw(scene);
    sk_render_end();

    sk_emitter3d_destroy(smoke);
    sk_emitter2d_destroy(confetti);
    sk_scene_destroy(scene);
    stop();
}

/* Moving emitters: steady spawns spread along the way, velocity inherited from the
 * move, jumps and the first position aren't moves. */
void test_emitter_motion(void)
{
    float born[4], motion[4], shape[4];
    bool along = true, inherited = true, jumped = true;
    start();
    const sk_handle_t e = sk_emitter3d_create(sk_texture_get_default());
    CHECK(sk_emitter3d_set_life(e, 10, 10));
    CHECK(sk_emitter3d_set_position(e, 5, 0, 0)); /* the first position: not a move */
    CHECK(sk_emitter3d_set_rate(e, 100));
    sk_emitter_update(0.1f); /* 10 at x = 5 */
    for (int i = 0; i < 10; i++) {
        sk_emitter_particle(e, i, born, motion, shape);
        jumped = jumped && born[0] == 5 && motion[0] == 0;
    }
    CHECK(jumped);

    /* moved 10 in 0.1 s: the next 10 spread from 5 to 15, a tenth of the way each */
    CHECK(sk_emitter3d_set_inherit_velocity(e, 0.5f));
    CHECK(sk_emitter3d_set_position(e, 15, 0, 0));
    sk_emitter_update(0.1f);
    for (int i = 0; i < 10; i++) {
        sk_emitter_particle(e, 10 + i, born, motion, shape);
        along = along && fabsf(born[0] - (5.0f + (float)(i + 1))) < 1e-4f;
        inherited = inherited && fabsf(motion[0] - 50.0f) < 1e-2f; /* half of 100 a second */
    }
    CHECK(along);
    CHECK(inherited);

    /* a jump: no trail, nothing to inherit */
    jumped = true;
    CHECK(sk_emitter3d_jump(e, 100, 0, 0));
    sk_emitter_update(0.1f);
    for (int i = 0; i < 10; i++) {
        sk_emitter_particle(e, 20 + i, born, motion, shape);
        jumped = jumped && born[0] == 100 && motion[0] == 0;
    }
    CHECK(jumped);

    /* standing still: nothing inherited */
    sk_emitter_update(0.1f);
    sk_emitter_particle(e, 30, born, motion, shape);
    CHECK(born[0] == 100 && motion[0] == 0);

    /* uneven frames: the move was made over the last frame (0.1 s), not the next (0.02 s) */
    CHECK(sk_emitter3d_set_position(e, 110, 0, 0));
    sk_emitter_update(0.02f);
    CHECK(sk_emitter3d_get_count(e) == 42);
    sk_emitter_particle(e, 41, born, motion, shape);
    CHECK(fabsf(motion[0] - 50.0f) < 1e-2f); /* half of 10 / 0.1 s */

    CHECK(sk_emitter3d_set_drag(e, 2) && sk_emitter3d_set_stretch(e, 0.05f));
    CHECK(!sk_emitter3d_set_drag(e, -1) && !sk_emitter3d_set_stretch(e, -1));
    CHECK(!sk_emitter2d_jump(e, 0, 0));
    sk_emitter3d_destroy(e);
    stop();
}

/* Curves over life: keys kept in order, bounded; the palette's pick is spread. */
void test_emitter_curves(void)
{
    float times[8], values[8], born[4], motion[4], shape[4];
    int picks[4] = {0};
    start();
    const sk_handle_t e = sk_emitter3d_create(sk_texture_get_default());
    CHECK(sk_emitter_size_keys(e, times, values) == 2); /* 1 -> 1 */
    CHECK(sk_emitter3d_set_size(e, 2, 5, 0));
    CHECK(sk_emitter_size_keys(e, times, values) == 2 && times[0] == 0 && values[0] == 2 && times[1] == 1 &&
          values[1] == 5);

    /* added out of order: kept in order; the same time twice is a step, in added order */
    CHECK(sk_emitter3d_clear_size_keys(e));
    CHECK(sk_emitter_size_keys(e, times, values) == 0);
    CHECK(sk_emitter3d_add_size_key(e, 1, 0));
    CHECK(sk_emitter3d_add_size_key(e, 0, 1));
    CHECK(sk_emitter3d_add_size_key(e, 0.5f, 3));
    CHECK(sk_emitter3d_add_size_key(e, 0.5f, 4));
    CHECK(sk_emitter_size_keys(e, times, values) == 4);
    CHECK(times[0] == 0 && values[0] == 1 && times[1] == 0.5f && values[1] == 3 && times[2] == 0.5f &&
          values[2] == 4 && times[3] == 1 && values[3] == 0);
    for (int i = 0; i < 4; i++) CHECK(sk_emitter3d_add_size_key(e, 0.9f, 1));
    CHECK(!sk_emitter3d_add_size_key(e, 0.9f, 1)); /* 8 at most */
    CHECK(!sk_emitter3d_add_color_key(e, 1.5f, SK_COLOR_RED)); /* outside the life */
    CHECK(!sk_emitter3d_add_size_key(e, 0.5f, -1));
    CHECK(sk_emitter3d_clear_color_keys(e) && sk_emitter3d_add_color_key(e, 0.3f, SK_COLOR_RED));

    /* the palette: 8 at most; particles carry a random pick, spread over it */
    for (int i = 0; i < 8; i++) CHECK(sk_emitter3d_add_palette_color(e, SK_COLOR_RED));
    CHECK(!sk_emitter3d_add_palette_color(e, SK_COLOR_RED));
    CHECK(sk_emitter3d_clear_palette(e));
    CHECK(sk_emitter3d_set_max(e, 4000) && sk_emitter3d_burst(e, 4000));
    for (int i = 0; i < 4000; i++) {
        sk_emitter_particle(e, i, born, motion, shape);
        CHECK(shape[3] >= 0 && shape[3] < 1);
        picks[(int)(shape[3] * 4)]++;
    }
    for (int k = 0; k < 4; k++) CHECK(picks[k] > 850 && picks[k] < 1150);
    sk_emitter3d_destroy(e);
    stop();
}

/* Prewarming, the spawn sphere and circle, flipbook settings. */
void test_emitter_start(void)
{
    float born[4], motion[4], shape[4];
    bool inside = true, filled = false;
    start();
    const sk_handle_t e = sk_emitter3d_create(sk_texture_get_default());

    /* as if running for 5 s at 100 a second, each living 1 s: about 100 alive at once */
    CHECK(sk_emitter3d_set_rate(e, 100) && sk_emitter3d_set_life(e, 1, 1));
    CHECK(sk_emitter3d_burst(e, 10));
    CHECK(sk_emitter3d_prewarm(e, 5));
    CHECK(sk_emitter3d_get_count(e) >= 99 && sk_emitter3d_get_count(e) <= 100); /* the burst went */
    sk_emitter_update(0.5f); /* the steady state goes on */
    CHECK(sk_emitter3d_get_count(e) >= 99 && sk_emitter3d_get_count(e) <= 101);
    /* only as many as fit; less time, fewer */
    CHECK(sk_emitter3d_set_max(e, 50) && sk_emitter3d_prewarm(e, 5));
    CHECK(sk_emitter3d_get_count(e) == 50);
    CHECK(sk_emitter3d_set_max(e, 1000) && sk_emitter3d_prewarm(e, 0.25f));
    CHECK(sk_emitter3d_get_count(e) >= 24 && sk_emitter3d_get_count(e) <= 25);
    CHECK(!sk_emitter3d_prewarm(e, -1));

    /* the sphere: within its radius, and not only near the middle */
    CHECK(sk_emitter3d_set_position(e, 1, 2, 3) && sk_emitter3d_set_spawn_sphere(e, 2));
    CHECK(sk_emitter3d_burst(e, 500));
    for (int i = 0; i < 500; i++) {
        sk_emitter_particle(e, sk_emitter3d_get_count(e) - 500 + i, born, motion, shape);
        const float r = sqrtf((born[0] - 1) * (born[0] - 1) + (born[1] - 2) * (born[1] - 2) + (born[2] - 3) * (born[2] - 3));
        inside = inside && r <= 2 + 1e-4f;
        filled = filled || r > 1.8f;
    }
    CHECK(inside && filled);
    CHECK(!sk_emitter3d_set_spawn_sphere(e, -1));

    /* 2D: the circle stays in the screen's plane */
    const sk_handle_t flat = sk_emitter2d_create(sk_texture_get_default());
    CHECK(sk_emitter2d_set_spawn_circle(flat, 10) && sk_emitter2d_burst(flat, 100));
    inside = true;
    for (int i = 0; i < 100; i++) {
        sk_emitter_particle(flat, i, born, motion, shape);
        inside = inside && born[2] == 0 && born[0] * born[0] + born[1] * born[1] <= 100 + 1e-3f;
    }
    CHECK(inside);

    /* flipbooks */
    CHECK(sk_emitter3d_set_frames(e, 4, 4, 0, 0));
    CHECK(sk_emitter3d_set_frames(e, 4, 4, 13, 24));
    CHECK(!sk_emitter3d_set_frames(e, 4, 4, 17, 0)); /* more frames than cells */
    CHECK(!sk_emitter3d_set_frames(e, 0, 4, 0, 0));
    CHECK(!sk_emitter3d_set_frames(e, 4, 4, 0, -1));
    CHECK(!sk_emitter2d_set_frames(e, 4, 4, 0, 0));

    sk_emitter3d_destroy(e);
    sk_emitter2d_destroy(flat);
    stop();
}
