/* libsk particles example — emitters (sk_emitter3d.h, sk_emitter2d.h).
 *
 * 3D emitters in a scene: a fountain (blended drops under gravity), sparks
 * (added, from a point circling the fountain: they trail behind it, are thrown along
 * by it, slowed by drag and stretched along their motion), and a campfire: flames
 * (added puffs from a flipbook texture, colored by a curve) under smoke (blended, with
 * size and color curves: a dark puff that spreads, lightens and fades as it rises).
 * The steady ones are prewarmed, so they're already going when they appear. Click or tap anywhere for a 2D confetti burst there: squares cut from
 * the middle of the particle texture (so their spin shows), each tinted by a color
 * picked from the emitter's palette. Space pauses the
 * steady emitters; the particles alive finish their lives. The camera turns slowly
 * around the scene (O stops and restarts it). */
#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "sk.h"
#include "example_assets.h"

#define PARTICLE_PATH "textures/particle.png"
#define FLAME_PATH    "textures/flame.png" /* a 4x4 flipbook (tools/gen_particles.py) */

static sk_handle_t g_scene;
static sk_handle_t g_camera;
static sk_handle_t g_fountain;
static sk_handle_t g_sparks;
static sk_handle_t g_smoke;
static sk_handle_t g_flame;
static sk_handle_t g_confetti;
static bool g_paused;
static float g_time;
static bool g_orbit = true;
static float g_orbit_angle; /* radians around the fountain; holds while the orbit is stopped */

#define ORBIT_RADIUS 16.0f
#define ORBIT_SPEED 0.15f /* radians per second */

static void make_fountain(sk_handle_t texture)
{
    g_fountain = sk_emitter3d_create(texture);
    sk_emitter3d_set_max(g_fountain, 4096);
    sk_emitter3d_set_rate(g_fountain, 1200.0f);
    sk_emitter3d_set_life(g_fountain, 1.4f, 1.9f);
    sk_emitter3d_set_position(g_fountain, 0.0f, 0.2f, 0.0f);
    sk_emitter3d_set_spawn_box(g_fountain, 0.15f, 0.0f, 0.15f);
    sk_emitter3d_set_velocity(g_fountain, 0.0f, 9.0f, 0.0f, 0.22f, 0.15f);
    sk_emitter3d_set_gravity(g_fountain, 0.0f, -9.8f, 0.0f);
    sk_emitter3d_set_size(g_fountain, 0.22f, 0.12f, 0.4f);
    sk_emitter3d_set_color(g_fountain, sk_color_rgba(150, 210, 255, 230), sk_color_rgba(60, 120, 255, 0));
    sk_emitter3d_set_alpha_mode(g_fountain, SK_ALPHA_BLEND, 0.0f);
    sk_emitter3d_prewarm(g_fountain, 2.0f); /* already running when the page opens */
    sk_scene_add(g_scene, g_fountain, 0);
}

static void make_sparks(sk_handle_t texture)
{
    g_sparks = sk_emitter3d_create(texture);
    sk_emitter3d_set_max(g_sparks, 2048);
    sk_emitter3d_set_rate(g_sparks, 600.0f);
    sk_emitter3d_set_life(g_sparks, 0.5f, 1.2f);
    sk_emitter3d_set_velocity(g_sparks, 0.0f, 4.0f, 0.0f, 1.2f, 0.6f);
    sk_emitter3d_set_gravity(g_sparks, 0.0f, -6.0f, 0.0f);
    sk_emitter3d_set_drag(g_sparks, 1.5f);               /* they slow down */
    sk_emitter3d_set_inherit_velocity(g_sparks, 0.4f);   /* thrown along by the moving source */
    sk_emitter3d_set_stretch(g_sparks, 0.04f);           /* streaks along their motion */
    sk_emitter3d_set_size(g_sparks, 0.08f, 0.02f, 0.5f);
    sk_emitter3d_set_color(g_sparks, sk_color_rgba(255, 220, 120, 255), sk_color_rgba(255, 60, 10, 0));
    /* SK_ALPHA_ADD is the default */
    sk_scene_add(g_scene, g_sparks, 0);
}

static void make_smoke(sk_handle_t texture)
{
    g_smoke = sk_emitter3d_create(texture);
    sk_emitter3d_set_max(g_smoke, 512);
    sk_emitter3d_set_rate(g_smoke, 30.0f);
    sk_emitter3d_set_life(g_smoke, 3.0f, 4.5f);
    sk_emitter3d_set_position(g_smoke, -5.0f, 1.5f, -2.0f); /* above the fire */
    sk_emitter3d_set_spawn_sphere(g_smoke, 0.3f);
    sk_emitter3d_set_velocity(g_smoke, 0.0f, 1.4f, 0.0f, 0.35f, 0.3f);
    sk_emitter3d_set_gravity(g_smoke, 0.35f, 0.0f, 0.0f); /* a breeze */
    sk_emitter3d_set_spin(g_smoke, -0.8f, 0.8f);
    /* curves: a quick puff that keeps spreading; dark, then light, then gone */
    sk_emitter3d_clear_size_keys(g_smoke);
    sk_emitter3d_add_size_key(g_smoke, 0.0f, 0.3f);
    sk_emitter3d_add_size_key(g_smoke, 0.15f, 1.4f);
    sk_emitter3d_add_size_key(g_smoke, 1.0f, 3.6f);
    sk_emitter3d_clear_color_keys(g_smoke);
    sk_emitter3d_add_color_key(g_smoke, 0.0f, sk_color_rgba(40, 36, 34, 0));
    sk_emitter3d_add_color_key(g_smoke, 0.1f, sk_color_rgba(50, 46, 44, 190));
    sk_emitter3d_add_color_key(g_smoke, 0.5f, sk_color_rgba(130, 130, 140, 120));
    sk_emitter3d_add_color_key(g_smoke, 1.0f, sk_color_rgba(170, 170, 180, 0));
    sk_emitter3d_set_alpha_mode(g_smoke, SK_ALPHA_BLEND, 0.0f);
    sk_emitter3d_prewarm(g_smoke, 5.0f);
    sk_scene_add(g_scene, g_smoke, 0);
}

/* A campfire's flames: puffs from a flipbook, played once over each one's life, glowing
 * white-yellow, then orange, red and out as they rise and break up. */
static void on_flame_loaded(const char *path, void *user)
{
    sk_handle_t texture = sk_texture_create(path);
    (void)user;
    if (texture == 0) {
        return;
    }
    g_flame = sk_emitter3d_create(texture);
    sk_texture_release(texture);
    sk_emitter3d_set_frames(g_flame, 4, 4, 0, 0.0f);
    sk_emitter3d_set_rate(g_flame, 40.0f);
    sk_emitter3d_set_life(g_flame, 0.7f, 1.1f);
    sk_emitter3d_set_position(g_flame, -5.0f, 0.3f, -2.0f);
    sk_emitter3d_set_spawn_sphere(g_flame, 0.3f);
    sk_emitter3d_set_velocity(g_flame, 0.0f, 1.8f, 0.0f, 0.2f, 0.3f);
    sk_emitter3d_set_drag(g_flame, 0.8f);
    sk_emitter3d_set_spin(g_flame, -1.5f, 1.5f);
    sk_emitter3d_clear_size_keys(g_flame);
    sk_emitter3d_add_size_key(g_flame, 0.0f, 0.6f);
    sk_emitter3d_add_size_key(g_flame, 0.3f, 1.1f);
    sk_emitter3d_add_size_key(g_flame, 1.0f, 0.4f);
    sk_emitter3d_clear_color_keys(g_flame);
    sk_emitter3d_add_color_key(g_flame, 0.0f, sk_color_rgba(255, 235, 190, 0));
    sk_emitter3d_add_color_key(g_flame, 0.08f, sk_color_rgba(255, 235, 190, 110));
    sk_emitter3d_add_color_key(g_flame, 0.25f, sk_color_rgba(255, 150, 40, 100));
    sk_emitter3d_add_color_key(g_flame, 0.65f, sk_color_rgba(200, 50, 15, 60));
    sk_emitter3d_add_color_key(g_flame, 1.0f, sk_color_rgba(80, 15, 5, 0));
    sk_emitter3d_prewarm(g_flame, 1.0f);
    sk_scene_add(g_scene, g_flame, 0); /* added: SK_ALPHA_ADD, the default */
}

static void make_confetti(sk_handle_t texture)
{
    g_confetti = sk_emitter2d_create(texture);
    sk_emitter2d_set_source(g_confetti, 24.0f, 24.0f, 16.0f, 16.0f); /* the dot's solid middle: squares */
    sk_emitter2d_set_max(g_confetti, 4096);
    sk_emitter2d_set_life(g_confetti, 1.2f, 2.2f);
    sk_emitter2d_set_velocity(g_confetti, 0.0f, -420.0f, 1.3f, 0.7f);
    sk_emitter2d_set_gravity(g_confetti, 0.0f, 700.0f);
    sk_emitter2d_set_drag(g_confetti, 0.8f); /* flutters down instead of dropping */
    sk_emitter2d_set_size(g_confetti, 12.0f, 6.0f, 0.5f);
    sk_emitter2d_set_spin(g_confetti, -8.0f, 8.0f);
    sk_emitter2d_set_color(g_confetti, SK_COLOR_WHITE, sk_color_rgba(255, 255, 255, 0));
    /* each piece picks one of these at birth */
    sk_emitter2d_add_palette_color(g_confetti, SK_COLOR_RED);
    sk_emitter2d_add_palette_color(g_confetti, SK_COLOR_GOLD);
    sk_emitter2d_add_palette_color(g_confetti, SK_COLOR_LIME);
    sk_emitter2d_add_palette_color(g_confetti, SK_COLOR_SKYBLUE);
    sk_emitter2d_add_palette_color(g_confetti, SK_COLOR_VIOLET);
    sk_emitter2d_set_alpha_mode(g_confetti, SK_ALPHA_BLEND, 0.0f);
    sk_scene_add(g_scene, g_confetti, 1);
}

static void burst_confetti(float x, float y)
{
    sk_emitter2d_jump(g_confetti, x, y);
    sk_emitter2d_burst(g_confetti, 300);
}

static void on_texture_loaded(const char *path, void *user)
{
    sk_handle_t texture = sk_texture_create(path);
    (void)user;
    if (texture == 0) {
        return;
    }
    make_fountain(texture);
    make_sparks(texture);
    make_smoke(texture);
    make_confetti(texture);
    sk_texture_release(texture); /* each emitter holds its own reference */

    vec2_t screen = sk_window_get_screen_size();
    burst_confetti(screen.x * 0.5f, screen.y * 0.4f); /* one to start with */
}

static void on_failed(const char *path, void *user)
{
    (void)user;
    sk_logger_error("asset load failed: %s", path);
}

static void on_init(void *user_data)
{
    (void)user_data;
    sk_asset_set_host(EXAMPLE_ASSET_BASE);

    g_camera = sk_camera3d_create(SK_CAMERA3D_PERSPECTIVE);
    sk_camera3d_set_view(g_camera, 0.0f, 6.0f, 16.0f, 0.0f, 3.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    g_scene = sk_scene_create();
    sk_scene_set_active_camera(g_scene, g_camera);

    sk_asset_add_task(sk_asset_ensure_async(PARTICLE_PATH, NULL, 0), on_texture_loaded, on_failed, NULL);
    sk_asset_add_task(sk_asset_ensure_async(FLAME_PATH, NULL, 0), on_flame_loaded, on_failed, NULL);
    sk_debug_enable_fps(12, 10, 16);
}

static void set_paused(bool paused)
{
    g_paused = paused;
    sk_emitter3d_set_emitting(g_fountain, !paused);
    sk_emitter3d_set_emitting(g_sparks, !paused);
    sk_emitter3d_set_emitting(g_smoke, !paused);
    sk_emitter3d_set_emitting(g_flame, !paused);
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    (void)tick_fraction;
    (void)user_data;
    char line[128];

    g_time += dt;
    if (g_sparks != 0) {
        /* the sparks' source circles the fountain; the sparks stay where they were born */
        sk_emitter3d_set_position(g_sparks, 3.5f * cosf(g_time * 1.3f), 1.5f + 0.8f * sinf(g_time * 2.1f),
                                  3.5f * sinf(g_time * 1.3f));
    }

    sk_mouse_state_t mouse = sk_input_get_mouse_state();
    if (mouse.left == SK_BUTTON_PRESSED && g_confetti != 0) {
        burst_confetti(mouse.x, mouse.y);
    }
    if (sk_input_get_key(SK_KEY_SPACE) == SK_BUTTON_PRESSED && g_fountain != 0) {
        set_paused(!g_paused);
    }
    if (sk_input_get_key(SK_KEY_O) == SK_BUTTON_PRESSED) {
        g_orbit = !g_orbit;
    }
    if (g_orbit) {
        g_orbit_angle += dt * ORBIT_SPEED;
    }
    sk_camera3d_set_view(g_camera, ORBIT_RADIUS * sinf(g_orbit_angle), 6.0f, ORBIT_RADIUS * cosf(g_orbit_angle), 0.0f,
                         3.0f, 0.0f, 0.0f, 1.0f, 0.0f);

    sk_render_begin();
    sk_render_clear_background(sk_color_rgba(14, 16, 24, 255));

    sk_render_begin_mode_3d();
    sk_shape3d_draw_grid(24, 1.0f, SK_COLOR_DARKGRAY);
    sk_render_end_mode_3d();

    sk_scene_draw(g_scene);

    sk_text_draw("libsk + sokol — particles", 12, 36, 24, SK_COLOR_RAYWHITE);
    snprintf(line, sizeof(line), "click / tap: confetti   space: %s   O: %s the camera",
             g_paused ? "resume" : "pause", g_orbit ? "stop" : "turn");
    sk_text_draw(line, 12, 70, 16, SK_COLOR_LIGHTGRAY);
    snprintf(line, sizeof(line), "fountain %d   sparks %d   flame %d   smoke %d   confetti %d",
             sk_emitter3d_get_count(g_fountain), sk_emitter3d_get_count(g_sparks), sk_emitter3d_get_count(g_flame),
             sk_emitter3d_get_count(g_smoke), sk_emitter2d_get_count(g_confetti));
    sk_text_draw(line, 12, 94, 16, SK_COLOR_LIGHTGRAY);

    sk_render_end();

    if (sk_input_get_key(SK_KEY_ESCAPE) == SK_BUTTON_PRESSED) {
        sk_request_quit();
    }
}

int main(void)
{
    sk_init_values(1000, 700, "libsk particles", SK_WINDOW_FLAG_MSAA_4X_HINT | SK_WINDOW_FLAG_WINDOW_RESIZABLE);
    sk_set_init(on_init, NULL);
    sk_set_frame(frame, NULL);
    return sk_run();
}
