/* libsk sprite3d example — async asset load + textured billboard in a scene.
 *
 * Demonstrates the Phase C flow:
 *   sk_asset_load_async(png) -> on_loaded(bytes) -> sk_texture_create_from_memory
 *   -> sk_sprite3d_create -> sk_scene_add. The sprite bobs and faces the camera. */
#include <math.h>
#include <stddef.h>

#include "sk.h"

#define LOGO_PATH "examples/assets/sprites/logo/wg-logo-bw-alpha.png"

static sk_handle_t g_scene;
static sk_handle_t g_camera;
static sk_handle_t g_bg;
static sk_handle_t g_sprite; /* set once the texture finishes loading */
static bool g_loaded;

static void on_logo_loaded(const char *path, const unsigned char *data, int size, void *user)
{
    (void)path;
    (void)user;
    sk_handle_t tex = sk_texture_create_from_memory(data, size);
    if (tex == 0) {
        return;
    }
    g_sprite = sk_sprite3d_create(tex);
    sk_sprite3d_set_size(g_sprite, 6.0f);
    sk_sprite3d_set_facing(g_sprite, SK_SPRITE3D_FACING_CAMERA);
    sk_sprite3d_set_tint(g_sprite, SK_COLOR_WHITE);
    sk_scene_add(g_scene, g_sprite, 1);
    g_loaded = true;
}

static void on_logo_failed(const char *path, void *user)
{
    (void)user;
    sk_logger_error("could not load %s", path);
}

static void on_init(void *user_data)
{
    (void)user_data;
    g_bg = sk_color_create(20, 22, 30, 255);
    g_camera = sk_camera3d_create(12.0f, 7.0f, 12.0f, 0.0f, 2.5f, 0.0f,
                                  0.0f, 1.0f, 0.0f, 45.0f, SK_CAMERA3D_PERSPECTIVE);
    g_scene = sk_scene_create();
    sk_scene_set_active_camera(g_scene, g_camera);

    /* a couple of ground shapes for depth reference */
    sk_handle_t pedestal = sk_shape_create();
    sk_shape_set_cube(pedestal, 3.0f, 0.5f, 3.0f);
    sk_shape_set_transform(pedestal, 0.0f, 0.25f, 0.0f, 0, 0, 0, 1, 1, 1);
    sk_shape_set_color(pedestal, SK_COLOR_DARKGRAY);
    sk_scene_add(g_scene, pedestal, 0);

    sk_asset_load_async(LOGO_PATH, on_logo_loaded, on_logo_failed, NULL);
    sk_debug_enable_fps(12, 10, 16);
}

static void frame(void *user_data)
{
    (void)user_data;
    float t = (float)sk_get_time();

    sk_camera3d_set(g_camera, cosf(t * 0.3f) * 13.0f, 7.0f, sinf(t * 0.3f) * 13.0f,
                    0.0f, 2.5f, 0.0f, 0.0f, 1.0f, 0.0f, 45.0f, SK_CAMERA3D_PERSPECTIVE);

    if (g_loaded) {
        float y = 3.5f + sinf(t * 1.5f) * 0.8f; /* bob */
        sk_sprite3d_set_transform(g_sprite, 0.0f, y, 0.0f, 0, 0, 0, 1, 1, 1);
    }

    sk_render_begin();
    sk_render_clear_background(g_bg);

    sk_render_begin_mode_3d();
    sk_shape_draw_grid(20, 1.0f, SK_COLOR_DARKGRAY);
    sk_render_end_mode_3d();

    sk_scene_draw(g_scene);

    sk_text_draw("libsk + sokol — sprite3d", 12, 36, 24, SK_COLOR_RAYWHITE);
    sk_text_draw(g_loaded ? "logo loaded async via sokol_fetch" : "loading logo...",
                 12, 70, 16, SK_COLOR_LIGHTGRAY);

    sk_render_end();

    sk_keyboard_state_t kb = sk_input_get_keyboard_state();
    if (kb.keys[SK_KEY_ESCAPE] == SK_BUTTON_PRESSED) {
        sk_request_quit();
    }
}

int main(void)
{
    sk_init_values(900, 700, "libsk sprite3d", SK_WINDOW_FLAG_MSAA_4X_HINT);
    sk_set_init(on_init, NULL);
    sk_set_frame(frame, NULL);
    return sk_run();
}
