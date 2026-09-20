/* libwgrender picking example — ray-pick a scene containing all three drawable kinds.
 *
 * Click to pick. wgr_scene_pick() does a world-AABB broadphase then a per-kind
 * narrow phase: shapes use exact ray/cube + ray/sphere tests; models use exact
 * ray/triangle against the bind-pose mesh; sprites test the billboard quad and
 * (when alpha-test picking is enabled) reject transparent texels via a CPU mask
 * built from the texture source on demand. The camera is fixed so aiming is predictable; the readout shows what was
 * hit, where, and how far. */
#include <stddef.h>
#include <stdio.h>

#include "wgr.h"
#include "example_assets.h"
#include "wgr_sprite3d.h"

#define LOGO_PATH  "sprites/logo/wg-logo-bw-alpha.png"
#define MODEL_PATH "models/gumshoe/gumshoe.glb"

static wgr_handle_t g_scene;
static wgr_handle_t g_camera;
static wgr_color_t g_bg;

static wgr_handle_t g_cube;
static wgr_handle_t g_sphere;
static wgr_handle_t g_sprite; /* set once the texture finishes loading */
static wgr_handle_t g_model;  /* set once the glTF finishes loading */

static wgr_handle_t g_selected;
static wgr_pick_result_t g_last;

static const char *kind_name(wgr_handle_t handle)
{
    switch (wgr_handle_get_kind(handle)) {
    case WGR_HANDLE_KIND_SHAPE3D:    return "shape";
    case WGR_HANDLE_KIND_SPRITE3D: return "sprite3d";
    case WGR_HANDLE_KIND_MODEL:    return "model";
    default:                      return "?";
    }
}

static void on_logo_loaded(const char *path, void *user)
{
    wgr_handle_t texture = wgr_texture_create(path);
    (void)user;
    g_sprite = wgr_sprite3d_create(texture);
    wgr_texture_release(texture); /* the sprite holds its own reference */
    if (g_sprite == 0) {
        return;
    }
    wgr_sprite3d_set_size(g_sprite, 4.0f);
    wgr_sprite3d_set_facing(g_sprite, WGR_SPRITE3D_FACING_CAMERA);
    wgr_sprite3d_set_tint(g_sprite, WGR_COLOR_WHITE);
    wgr_sprite3d_set_transform(g_sprite, 0.0f, 3.0f, 4.0f, 0, 0, 0, 1, 1, 1);
    wgr_sprite3d_set_pick_alpha_test(g_sprite, true, 0.5f);
    wgr_scene_add(g_scene, g_sprite, 1);
}

static void on_model_loaded(const char *path, void *user)
{
    wgr_handle_t mesh = wgr_mesh_create(path);
    (void)user;
    g_model = wgr_model_create(mesh);
    wgr_mesh_release(mesh); /* the model holds its own reference to the mesh */
    if (g_model == 0) {
        return;
    }
    wgr_model_set_transform(g_model, 0.0f, 0.0f, -4.0f, 0, 0, 0, 1, 1, 1);
    wgr_model_set_tint(g_model, WGR_COLOR_RAYWHITE);
    wgr_scene_add(g_scene, g_model, 0);
}

static void on_failed(const char *path, void *user)
{
    (void)user;
    wgr_logger_error("asset load failed: %s", path);
}

static void on_init(void *user_data)
{
    wgr_asset_set_host(EXAMPLE_ASSET_BASE);
    (void)user_data;

    g_bg = wgr_color_rgba(24, 26, 34, 255);
    g_camera = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE);
    wgr_camera3d_set_view(g_camera, 11.0f, 9.0f, 11.0f, 0.0f, 2.0f, 0.0f, 0.0f, 1.0f, 0.0f);

    g_scene = wgr_scene_create();
    wgr_scene_set_active_camera(g_scene, g_camera);

    /* scenes start unlit: a sun and some ambient so the model is visible */
    wgr_handle_t sun = wgr_light_create(WGR_LIGHT_DIRECTIONAL);
    wgr_light_set_direction(sun, -0.6f, -1.0f, -0.5f);
    wgr_light_set_intensity(sun, 3.0f);
    wgr_scene_add(g_scene, sun, 0);
    wgr_scene_set_ambient(g_scene, WGR_COLOR_WHITE, 0.3f);

    g_cube = wgr_shape3d_create();
    wgr_shape3d_set_cube(g_cube, 2.0f, 2.0f, 2.0f);
    wgr_shape3d_set_transform(g_cube, -3.5f, 1.0f, 0.0f, 0.0f, 0.6f, 0.0f, 1.0f, 1.0f, 1.0f);
    wgr_shape3d_set_color(g_cube, WGR_COLOR_ORANGE);
    wgr_scene_add(g_scene, g_cube, 0);

    g_sphere = wgr_shape3d_create();
    wgr_shape3d_set_sphere(g_sphere, 1.5f);
    wgr_shape3d_set_transform(g_sphere, 3.5f, 1.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    wgr_shape3d_set_color(g_sphere, WGR_COLOR_GOLD);
    wgr_scene_add(g_scene, g_sphere, 0);

    wgr_asset_add_task(wgr_asset_ensure_async(LOGO_PATH, NULL, 0), on_logo_loaded, on_failed, NULL);
    wgr_asset_add_task(wgr_asset_ensure_async(MODEL_PATH, NULL, 0), on_model_loaded, on_failed, NULL);

    wgr_debug_enable_fps(12, 10, 16);
}

/* Shapes can be recolored to show selection; other kinds just get reported. */
static wgr_handle_t default_color_for(wgr_handle_t shape)
{
    if (shape == g_cube) {
        return WGR_COLOR_ORANGE;
    }
    if (shape == g_sphere) {
        return WGR_COLOR_GOLD;
    }
    return WGR_COLOR_RAYWHITE;
}

static void update_selection(wgr_handle_t hit)
{
    if (hit == g_selected) {
        return;
    }
    if (wgr_handle_get_kind(g_selected) == WGR_HANDLE_KIND_SHAPE3D) {
        wgr_shape3d_set_color(g_selected, default_color_for(g_selected));
    }
    g_selected = hit;
    if (wgr_handle_get_kind(g_selected) == WGR_HANDLE_KIND_SHAPE3D) {
        wgr_shape3d_set_color(g_selected, WGR_COLOR_RAYWHITE);
    }
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    (void)user_data;

    wgr_mouse_state_t mouse = wgr_input_get_mouse_state();
    char line[128];

    if (mouse.left == WGR_BUTTON_PRESSED) {
        wgr_pick_result_t pick = wgr_scene_pick(g_scene, 0, mouse.x, mouse.y);
        g_last = pick;
        update_selection(pick.hit ? pick.handle : 0);
    }

    wgr_render_begin();
    wgr_render_clear_background(g_bg);

    wgr_render_begin_mode_3d();
    wgr_shape3d_draw_grid(24, 1.0f, WGR_COLOR_DARKGRAY);
    wgr_render_end_mode_3d();

    wgr_scene_draw(g_scene);

    wgr_text_draw("libwgrender + sokol — picking", 12, 36, 24, WGR_COLOR_RAYWHITE);
    wgr_text_draw("click cube / sphere / sprite / model", 12, 70, 16, WGR_COLOR_LIGHTGRAY);

    if (g_last.hit) {
        snprintf(line, sizeof(line), "hit %s (handle %u)",
                 kind_name(g_last.handle), (unsigned int)g_last.handle);
        wgr_text_draw(line, 12, 94, 16, WGR_COLOR_LIME);
        snprintf(line, sizeof(line), "world %.2f, %.2f, %.2f   dist %.2f",
                 g_last.point_world.x, g_last.point_world.y, g_last.point_world.z,
                 g_last.distance);
        wgr_text_draw(line, 12, 114, 16, WGR_COLOR_LIGHTGRAY);
        snprintf(line, sizeof(line), "local %.2f, %.2f, %.2f",
                 g_last.point_local.x, g_last.point_local.y, g_last.point_local.z);
        wgr_text_draw(line, 12, 134, 16, WGR_COLOR_LIGHTGRAY);
    } else {
        wgr_text_draw("no hit", 12, 94, 16, WGR_COLOR_LIGHTGRAY);
    }

    wgr_render_end();

    if (wgr_input_get_key(WGR_KEY_ESCAPE) == WGR_BUTTON_PRESSED) {
        wgr_request_quit();
    }
}

int main(void)
{
    wgr_init_values(900, 700, "libwgrender pick", WGR_WINDOW_FLAG_MSAA_4X_HINT | WGR_WINDOW_FLAG_WINDOW_RESIZABLE);
    wgr_set_init(on_init, NULL);
    wgr_set_frame(frame, NULL);
    return wgr_run();
}
