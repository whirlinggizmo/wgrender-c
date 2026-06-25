/* libsk picking example — ray-pick a scene containing all three drawable kinds.
 *
 * Click to pick. sk_scene_pick() does a world-AABB broadphase then a per-kind
 * narrow phase: shapes use exact ray/cube + ray/sphere tests; models use exact
 * ray/triangle against the bind-pose mesh; sprites test the billboard quad and
 * (when alpha-test picking is enabled) reject transparent texels via a CPU mask
 * built from the texture source on demand. The camera is fixed so aiming is predictable; the readout shows what was
 * hit, where, and how far. */
#include <stddef.h>
#include <stdio.h>

#include "sk.h"
#include "example_assets.h"
#include "sk_sprite3d.h"

#define LOGO_PATH  "sprites/logo/wg-logo-bw-alpha.png"
#define MODEL_PATH "models/gumshoe/gumshoe.glb"

static sk_handle_t g_scene;
static sk_handle_t g_camera;
static sk_handle_t g_bg;

static sk_handle_t g_cube;
static sk_handle_t g_sphere;
static sk_handle_t g_sprite; /* set once the texture finishes loading */
static sk_handle_t g_model;  /* set once the glTF finishes loading */

static sk_handle_t g_selected;
static sk_pick_result_t g_last;

static const char *kind_name(sk_handle_t handle)
{
    switch (sk_handle_get_kind(handle)) {
    case SK_HANDLE_KIND_SHAPE:    return "shape";
    case SK_HANDLE_KIND_SPRITE3D: return "sprite3d";
    case SK_HANDLE_KIND_MODEL:    return "model";
    default:                      return "?";
    }
}

static void on_logo_loaded(const char *path, void *user)
{
    sk_handle_t texture = sk_texture_create(path);
    (void)user;
    g_sprite = sk_sprite3d_create(texture);
    sk_texture_destroy(texture); /* the sprite holds its own reference */
    if (g_sprite == 0) {
        return;
    }
    sk_sprite3d_set_size(g_sprite, 4.0f);
    sk_sprite3d_set_facing(g_sprite, SK_SPRITE3D_FACING_CAMERA);
    sk_sprite3d_set_tint(g_sprite, SK_COLOR_WHITE);
    sk_sprite3d_set_transform(g_sprite, 0.0f, 3.0f, 4.0f, 0, 0, 0, 1, 1, 1);
    sk_sprite3d_set_pick_alpha_test(g_sprite, true, 0.5f);
    sk_scene_add(g_scene, g_sprite, 1);
}

static void on_model_loaded(const char *path, void *user)
{
    sk_handle_t mesh = sk_mesh_create(path);
    (void)user;
    g_model = sk_model_create(mesh);
    sk_mesh_destroy(mesh); /* the model holds its own reference to the mesh */
    if (g_model == 0) {
        return;
    }
    sk_model_set_transform(g_model, 0.0f, 0.0f, -4.0f, 0, 0, 0, 1, 1, 1);
    sk_model_set_tint(g_model, SK_COLOR_RAYWHITE);
    sk_scene_add(g_scene, g_model, 0);
}

static void on_failed(const char *path, void *user)
{
    (void)user;
    sk_logger_error("asset load failed: %s", path);
}

static void on_init(void *user_data)
{
    sk_asset_set_host(EXAMPLE_ASSET_BASE);
    (void)user_data;

    g_bg = sk_color_create(24, 26, 34, 255);
    g_camera = sk_camera3d_create(11.0f, 9.0f, 11.0f, 0.0f, 2.0f, 0.0f,
                                  0.0f, 1.0f, 0.0f, 45.0f, SK_CAMERA3D_PERSPECTIVE);

    g_scene = sk_scene_create();
    sk_scene_set_active_camera(g_scene, g_camera);

    g_cube = sk_shape_create();
    sk_shape_set_cube(g_cube, 2.0f, 2.0f, 2.0f);
    sk_shape_set_transform(g_cube, -3.5f, 1.0f, 0.0f, 0.0f, 0.6f, 0.0f, 1.0f, 1.0f, 1.0f);
    sk_shape_set_color(g_cube, SK_COLOR_ORANGE);
    sk_scene_add(g_scene, g_cube, 0);

    g_sphere = sk_shape_create();
    sk_shape_set_sphere(g_sphere, 1.5f);
    sk_shape_set_transform(g_sphere, 3.5f, 1.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    sk_shape_set_color(g_sphere, SK_COLOR_GOLD);
    sk_scene_add(g_scene, g_sphere, 0);

    sk_asset_add_task(sk_asset_ensure_async(LOGO_PATH, NULL), on_logo_loaded, on_failed, NULL);
    sk_asset_add_task(sk_asset_ensure_async(MODEL_PATH, NULL), on_model_loaded, on_failed, NULL);

    sk_debug_enable_fps(12, 10, 16);
}

/* Shapes can be recolored to show selection; other kinds just get reported. */
static sk_handle_t default_color_for(sk_handle_t shape)
{
    if (shape == g_cube) {
        return SK_COLOR_ORANGE;
    }
    if (shape == g_sphere) {
        return SK_COLOR_GOLD;
    }
    return SK_COLOR_RAYWHITE;
}

static void update_selection(sk_handle_t hit)
{
    if (hit == g_selected) {
        return;
    }
    if (sk_handle_get_kind(g_selected) == SK_HANDLE_KIND_SHAPE) {
        sk_shape_set_color(g_selected, default_color_for(g_selected));
    }
    g_selected = hit;
    if (sk_handle_get_kind(g_selected) == SK_HANDLE_KIND_SHAPE) {
        sk_shape_set_color(g_selected, SK_COLOR_RAYWHITE);
    }
}

static void frame(void *user_data)
{
    (void)user_data;

    sk_mouse_state_t mouse = sk_input_get_mouse_state();
    char line[128];

    if (mouse.left == SK_BUTTON_PRESSED) {
        sk_pick_result_t pick = sk_scene_pick(g_scene, 0, mouse.x, mouse.y);
        g_last = pick;
        update_selection(pick.hit ? pick.handle : 0);
    }

    sk_render_begin();
    sk_render_clear_background(g_bg);

    sk_render_begin_mode_3d();
    sk_shape_draw_grid(24, 1.0f, SK_COLOR_DARKGRAY);
    sk_render_end_mode_3d();

    sk_scene_draw(g_scene);

    sk_text_draw("libsk + sokol — picking", 12, 36, 24, SK_COLOR_RAYWHITE);
    sk_text_draw("click cube / sphere / sprite / model", 12, 70, 16, SK_COLOR_LIGHTGRAY);

    if (g_last.hit) {
        snprintf(line, sizeof(line), "hit %s (handle %u)",
                 kind_name(g_last.handle), (unsigned int)g_last.handle);
        sk_text_draw(line, 12, 94, 16, SK_COLOR_LIME);
        snprintf(line, sizeof(line), "world %.2f, %.2f, %.2f   dist %.2f",
                 g_last.point_world.x, g_last.point_world.y, g_last.point_world.z,
                 g_last.distance);
        sk_text_draw(line, 12, 114, 16, SK_COLOR_LIGHTGRAY);
        snprintf(line, sizeof(line), "local %.2f, %.2f, %.2f",
                 g_last.point_local.x, g_last.point_local.y, g_last.point_local.z);
        sk_text_draw(line, 12, 134, 16, SK_COLOR_LIGHTGRAY);
    } else {
        sk_text_draw("no hit", 12, 94, 16, SK_COLOR_LIGHTGRAY);
    }

    sk_render_end();

    if (sk_input_get_keyboard_state().keys[SK_KEY_ESCAPE] == SK_BUTTON_PRESSED) {
        sk_request_quit();
    }
}

int main(void)
{
    sk_init_values(900, 700, "libsk pick", SK_WINDOW_FLAG_MSAA_4X_HINT);
    sk_set_init(on_init, NULL);
    sk_set_frame(frame, NULL);
    return sk_run();
}
