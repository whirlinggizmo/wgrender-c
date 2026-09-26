/* libwgrender particles example — emitters (wgr_emitter3d.h, wgr_emitter2d.h).
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

#include "wgr.h"
#include "example_assets.h"

#define PARTICLE_PATH "textures/particle.png"
#define FLAME_PATH    "textures/flame.png" /* a 4x4 flipbook (tools/gen_particles.py) */

static wgr_handle_t g_scene;
static wgr_handle_t g_camera;
static wgr_handle_t g_fountain;
static wgr_handle_t g_sparks;
static wgr_handle_t g_smoke;
static wgr_handle_t g_flame;
static wgr_handle_t g_confetti;
static bool g_paused;
static float g_time;
static bool g_orbit = true;
static float g_orbit_angle; /* radians around the fountain; holds while the orbit is stopped */

#define ORBIT_RADIUS 16.0f
#define ORBIT_SPEED 0.15f /* radians per second */

static void make_fountain(wgr_handle_t texture)
{
    g_fountain = wgr_emitter3d_create(texture);
    wgr_emitter3d_set_max(g_fountain, 4096);
    wgr_emitter3d_set_rate(g_fountain, 1200.0f);
    wgr_emitter3d_set_life(g_fountain, 1.4f, 1.9f);
    wgr_emitter3d_set_position(g_fountain, 0.0f, 0.2f, 0.0f);
    wgr_emitter3d_set_spawn_box(g_fountain, 0.15f, 0.0f, 0.15f);
    wgr_emitter3d_set_velocity(g_fountain, 0.0f, 9.0f, 0.0f, 0.22f, 0.15f);
    wgr_emitter3d_set_gravity(g_fountain, 0.0f, -9.8f, 0.0f);
    wgr_emitter3d_set_size(g_fountain, 0.22f, 0.12f, 0.4f);
    wgr_emitter3d_set_color(g_fountain, wgr_color_rgba(150, 210, 255, 230), wgr_color_rgba(60, 120, 255, 0));
    wgr_emitter3d_set_alpha_mode(g_fountain, WGR_ALPHA_BLEND, 0.0f);
    wgr_emitter3d_prewarm(g_fountain, 2.0f); /* already running when the page opens */
    wgr_scene_add(g_scene, g_fountain, 0);
}

static void make_sparks(wgr_handle_t texture)
{
    g_sparks = wgr_emitter3d_create(texture);
    wgr_emitter3d_set_max(g_sparks, 2048);
    wgr_emitter3d_set_rate(g_sparks, 600.0f);
    wgr_emitter3d_set_life(g_sparks, 0.5f, 1.2f);
    wgr_emitter3d_set_velocity(g_sparks, 0.0f, 4.0f, 0.0f, 1.2f, 0.6f);
    wgr_emitter3d_set_gravity(g_sparks, 0.0f, -6.0f, 0.0f);
    wgr_emitter3d_set_drag(g_sparks, 1.5f);               /* they slow down */
    wgr_emitter3d_set_inherit_velocity(g_sparks, 0.4f);   /* thrown along by the moving source */
    wgr_emitter3d_set_stretch(g_sparks, 0.04f);           /* streaks along their motion */
    wgr_emitter3d_set_size(g_sparks, 0.08f, 0.02f, 0.5f);
    wgr_emitter3d_set_color(g_sparks, wgr_color_rgba(255, 220, 120, 255), wgr_color_rgba(255, 60, 10, 0));
    /* WGR_ALPHA_ADD is the default */
    wgr_scene_add(g_scene, g_sparks, 0);
}

static void make_smoke(wgr_handle_t texture)
{
    g_smoke = wgr_emitter3d_create(texture);
    wgr_emitter3d_set_max(g_smoke, 512);
    wgr_emitter3d_set_rate(g_smoke, 30.0f);
    wgr_emitter3d_set_life(g_smoke, 3.0f, 4.5f);
    wgr_emitter3d_set_position(g_smoke, -5.0f, 1.5f, -2.0f); /* above the fire */
    wgr_emitter3d_set_spawn_sphere(g_smoke, 0.3f);
    wgr_emitter3d_set_velocity(g_smoke, 0.0f, 1.4f, 0.0f, 0.35f, 0.3f);
    wgr_emitter3d_set_gravity(g_smoke, 0.35f, 0.0f, 0.0f); /* a breeze */
    wgr_emitter3d_set_spin(g_smoke, -0.8f, 0.8f);
    /* curves: a quick puff that keeps spreading; dark, then light, then gone */
    wgr_emitter3d_clear_size_keys(g_smoke);
    wgr_emitter3d_add_size_key(g_smoke, 0.0f, 0.3f);
    wgr_emitter3d_add_size_key(g_smoke, 0.15f, 1.4f);
    wgr_emitter3d_add_size_key(g_smoke, 1.0f, 3.6f);
    wgr_emitter3d_clear_color_keys(g_smoke);
    wgr_emitter3d_add_color_key(g_smoke, 0.0f, wgr_color_rgba(40, 36, 34, 0));
    wgr_emitter3d_add_color_key(g_smoke, 0.1f, wgr_color_rgba(50, 46, 44, 190));
    wgr_emitter3d_add_color_key(g_smoke, 0.5f, wgr_color_rgba(130, 130, 140, 120));
    wgr_emitter3d_add_color_key(g_smoke, 1.0f, wgr_color_rgba(170, 170, 180, 0));
    wgr_emitter3d_set_alpha_mode(g_smoke, WGR_ALPHA_BLEND, 0.0f);
    wgr_emitter3d_prewarm(g_smoke, 5.0f);
    wgr_scene_add(g_scene, g_smoke, 0);
}

/* A campfire's flames: puffs from a flipbook, played once over each one's life, glowing
 * white-yellow, then orange, red and out as they rise and break up. */
static void on_flame_loaded(const char *path, void *user)
{
    wgr_handle_t texture = wgr_texture_create(path);
    (void)user;
    if (texture == 0) {
        return;
    }
    g_flame = wgr_emitter3d_create(texture);
    wgr_texture_release(texture);
    wgr_emitter3d_set_frames(g_flame, 4, 4, 0, 0.0f);
    wgr_emitter3d_set_rate(g_flame, 40.0f);
    wgr_emitter3d_set_life(g_flame, 0.7f, 1.1f);
    wgr_emitter3d_set_position(g_flame, -5.0f, 0.3f, -2.0f);
    wgr_emitter3d_set_spawn_sphere(g_flame, 0.3f);
    wgr_emitter3d_set_velocity(g_flame, 0.0f, 1.8f, 0.0f, 0.2f, 0.3f);
    wgr_emitter3d_set_drag(g_flame, 0.8f);
    wgr_emitter3d_set_spin(g_flame, -1.5f, 1.5f);
    wgr_emitter3d_clear_size_keys(g_flame);
    wgr_emitter3d_add_size_key(g_flame, 0.0f, 0.6f);
    wgr_emitter3d_add_size_key(g_flame, 0.3f, 1.1f);
    wgr_emitter3d_add_size_key(g_flame, 1.0f, 0.4f);
    wgr_emitter3d_clear_color_keys(g_flame);
    wgr_emitter3d_add_color_key(g_flame, 0.0f, wgr_color_rgba(255, 235, 190, 0));
    wgr_emitter3d_add_color_key(g_flame, 0.08f, wgr_color_rgba(255, 235, 190, 110));
    wgr_emitter3d_add_color_key(g_flame, 0.25f, wgr_color_rgba(255, 150, 40, 100));
    wgr_emitter3d_add_color_key(g_flame, 0.65f, wgr_color_rgba(200, 50, 15, 60));
    wgr_emitter3d_add_color_key(g_flame, 1.0f, wgr_color_rgba(80, 15, 5, 0));
    wgr_emitter3d_prewarm(g_flame, 1.0f);
    wgr_scene_add(g_scene, g_flame, 0); /* added: WGR_ALPHA_ADD, the default */
}

static void make_confetti(wgr_handle_t texture)
{
    g_confetti = wgr_emitter2d_create(texture);
    wgr_emitter2d_set_source(g_confetti, 24.0f, 24.0f, 16.0f, 16.0f); /* the dot's solid middle: squares */
    wgr_emitter2d_set_max(g_confetti, 4096);
    wgr_emitter2d_set_life(g_confetti, 1.2f, 2.2f);
    wgr_emitter2d_set_velocity(g_confetti, 0.0f, -420.0f, 1.3f, 0.7f);
    wgr_emitter2d_set_gravity(g_confetti, 0.0f, 700.0f);
    wgr_emitter2d_set_drag(g_confetti, 0.8f); /* flutters down instead of dropping */
    wgr_emitter2d_set_size(g_confetti, 12.0f, 6.0f, 0.5f);
    wgr_emitter2d_set_spin(g_confetti, -8.0f, 8.0f);
    wgr_emitter2d_set_color(g_confetti, WGR_COLOR_WHITE, wgr_color_rgba(255, 255, 255, 0));
    /* each piece picks one of these at birth */
    wgr_emitter2d_add_palette_color(g_confetti, WGR_COLOR_RED);
    wgr_emitter2d_add_palette_color(g_confetti, WGR_COLOR_GOLD);
    wgr_emitter2d_add_palette_color(g_confetti, WGR_COLOR_LIME);
    wgr_emitter2d_add_palette_color(g_confetti, WGR_COLOR_SKYBLUE);
    wgr_emitter2d_add_palette_color(g_confetti, WGR_COLOR_VIOLET);
    wgr_emitter2d_set_alpha_mode(g_confetti, WGR_ALPHA_BLEND, 0.0f);
    wgr_scene_add(g_scene, g_confetti, 1);
}

static void burst_confetti(float x, float y)
{
    wgr_emitter2d_jump(g_confetti, x, y);
    wgr_emitter2d_burst(g_confetti, 300);
}

static void on_texture_loaded(const char *path, void *user)
{
    wgr_handle_t texture = wgr_texture_create(path);
    (void)user;
    if (texture == 0) {
        return;
    }
    make_fountain(texture);
    make_sparks(texture);
    make_smoke(texture);
    make_confetti(texture);
    wgr_texture_release(texture); /* each emitter holds its own reference */

    vec2_t screen = wgr_window_get_screen_size();
    burst_confetti(screen.x * 0.5f, screen.y * 0.4f); /* one to start with */
}

static void on_failed(const char *path, void *user)
{
    (void)user;
    wgr_logger_error("asset load failed: %s", path);
}

static void on_init(void *user_data)
{
    (void)user_data;
    wgr_asset_set_host(EXAMPLE_ASSET_BASE);
    wgr_asset_set_manifest(EXAMPLE_ASSET_MANIFEST);

    g_camera = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE);
    wgr_camera3d_set_view(g_camera, 0.0f, 6.0f, 16.0f, 0.0f, 3.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    g_scene = wgr_scene_create();
    wgr_scene_set_active_camera(g_scene, g_camera);

    wgr_asset_add_task(wgr_asset_ensure_async(PARTICLE_PATH, NULL, 0), on_texture_loaded, on_failed, NULL);
    wgr_asset_add_task(wgr_asset_ensure_async(FLAME_PATH, NULL, 0), on_flame_loaded, on_failed, NULL);
    wgr_debug_enable_fps(12, 10, 16);
}

static void set_paused(bool paused)
{
    g_paused = paused;
    wgr_emitter3d_set_emitting(g_fountain, !paused);
    wgr_emitter3d_set_emitting(g_sparks, !paused);
    wgr_emitter3d_set_emitting(g_smoke, !paused);
    wgr_emitter3d_set_emitting(g_flame, !paused);
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    (void)tick_fraction;
    (void)user_data;
    char line[128];

    g_time += dt;
    if (g_sparks != 0) {
        /* the sparks' source circles the fountain; the sparks stay where they were born */
        wgr_emitter3d_set_position(g_sparks, 3.5f * cosf(g_time * 1.3f), 1.5f + 0.8f * sinf(g_time * 2.1f),
                                  3.5f * sinf(g_time * 1.3f));
    }

    wgr_mouse_state_t mouse = wgr_input_get_mouse_state();
    if (mouse.left == WGR_BUTTON_PRESSED && g_confetti != 0) {
        burst_confetti(mouse.x, mouse.y);
    }
    if (wgr_input_get_key(WGR_KEY_SPACE) == WGR_BUTTON_PRESSED && g_fountain != 0) {
        set_paused(!g_paused);
    }
    if (wgr_input_get_key(WGR_KEY_O) == WGR_BUTTON_PRESSED) {
        g_orbit = !g_orbit;
    }
    if (g_orbit) {
        g_orbit_angle += dt * ORBIT_SPEED;
    }
    wgr_camera3d_set_view(g_camera, ORBIT_RADIUS * sinf(g_orbit_angle), 6.0f, ORBIT_RADIUS * cosf(g_orbit_angle), 0.0f,
                         3.0f, 0.0f, 0.0f, 1.0f, 0.0f);

    wgr_render_begin_frame();
    wgr_render_clear_background(wgr_color_rgba(14, 16, 24, 255));

    wgr_render_begin_mode_3d();
    wgr_shape3d_draw_grid(24, 1.0f, WGR_COLOR_DARKGRAY);
    wgr_render_end_mode_3d();

    wgr_scene_draw(g_scene);

    wgr_text_draw("libwgrender + sokol — particles", 12, 36, 24, WGR_COLOR_RAYWHITE);
    snprintf(line, sizeof(line), "click / tap: confetti   space: %s   O: %s the camera",
             g_paused ? "resume" : "pause", g_orbit ? "stop" : "turn");
    wgr_text_draw(line, 12, 70, 16, WGR_COLOR_LIGHTGRAY);
    snprintf(line, sizeof(line), "fountain %d   sparks %d   flame %d   smoke %d   confetti %d",
             wgr_emitter3d_get_count(g_fountain), wgr_emitter3d_get_count(g_sparks), wgr_emitter3d_get_count(g_flame),
             wgr_emitter3d_get_count(g_smoke), wgr_emitter2d_get_count(g_confetti));
    wgr_text_draw(line, 12, 94, 16, WGR_COLOR_LIGHTGRAY);

    wgr_render_end_frame();

    if (wgr_input_get_key(WGR_KEY_ESCAPE) == WGR_BUTTON_PRESSED) {
        wgr_request_quit();
    }
}

int main(void)
{
    wgr_init_values(1000, 700, "libwgrender particles", WGR_WINDOW_FLAG_MSAA_4X_HINT | WGR_WINDOW_FLAG_WINDOW_RESIZABLE);
    wgr_set_init(on_init, NULL);
    wgr_set_frame(frame, NULL);
    return wgr_run();
}
