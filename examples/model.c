/* libsk model example — static glTF (cgltf) rendered with a lit/textured
 * pipeline, loaded async and placed in the scene. (No skeletal animation yet.) */
#include <math.h>
#include <stddef.h>

#include "sk.h"
#include "example_assets.h"

#define MODEL_PATH "models/gumshoe/gumshoe.glb"

static sk_handle_t g_scene;
static sk_handle_t g_camera;
static sk_handle_t g_bg;
static sk_handle_t g_model;
static bool g_loaded;

static void on_model_loaded(const char *path, void *user)
{
    sk_handle_t mesh = sk_mesh_create(path);
    (void)user;
    g_model = sk_model_create(mesh);
    sk_mesh_destroy(mesh); /* the model holds its own reference to the mesh */
    if (g_model == 0) {
        return;
    }
    sk_model_set_transform(g_model, 0, 0, 0, 0, 0, 0, 1, 1, 1);
    sk_model_set_tint(g_model, SK_COLOR_RAYWHITE);
    /* drive skeletal animation if the glTF has any */
    if (sk_model_get_animation_count(g_model) > 0) {
        sk_model_set_animation(g_model, 0);
        sk_model_set_animation_speed(g_model, 1.0f);
        sk_model_set_animation_loop(g_model, true);
    }
    sk_scene_add(g_scene, g_model, 0);
    g_loaded = true;
}

static void on_failed(const char *p, void *u) { (void)u; sk_logger_error("model load failed: %s", p); }

static void on_init(void *user_data)
{
    sk_asset_set_host(EXAMPLE_ASSET_BASE);
    (void)user_data;
    g_bg = sk_color_create(30, 32, 40, 255);
    g_camera = sk_camera3d_create(8, 8, 8, 0, 3, 0, 0, 1, 0, 45.0f, SK_CAMERA3D_PERSPECTIVE);
    g_scene = sk_scene_create();
    sk_scene_set_active_camera(g_scene, g_camera);
    sk_asset_add_task(sk_asset_ensure_async(MODEL_PATH, NULL, 0), on_model_loaded, on_failed, NULL);
    sk_debug_enable_fps(12, 10, 16);
}

static void frame(void *user_data)
{
    (void)user_data;
    float t = (float)sk_get_time();

    sk_camera3d_set(g_camera, cosf(t * 0.4f) * 9.0f, 7.0f, sinf(t * 0.4f) * 9.0f,
                    0, 3, 0, 0, 1, 0, 45.0f, SK_CAMERA3D_PERSPECTIVE);

    if (g_loaded) {
        sk_model_set_transform(g_model, 0, 0, 0, 0, t * 0.5f, 0, 1, 1, 1); /* slow spin */
        sk_model_animate(g_model, sk_get_delta_time());                    /* skeletal anim */
    }

    sk_render_begin();
    sk_render_clear_background(g_bg);

    sk_render_begin_mode_3d();
    sk_shape_draw_grid(20, 1.0f, SK_COLOR_DARKGRAY);
    sk_render_end_mode_3d();

    sk_scene_draw(g_scene);

    sk_text_draw("libsk + sokol — model (glTF/cgltf)", 12, 36, 22, SK_COLOR_RAYWHITE);
    sk_text_draw(g_loaded ? "gumshoe.glb — skeletal animation (glTF skin)" : "loading model...",
                 12, 68, 16, SK_COLOR_LIGHTGRAY);

    sk_render_end();

    sk_keyboard_state_t kb = sk_input_get_keyboard_state();
    if (kb.keys[SK_KEY_ESCAPE] == SK_BUTTON_PRESSED) {
        sk_request_quit();
    }
}

int main(void)
{
    sk_init_values(900, 700, "libsk model", SK_WINDOW_FLAG_MSAA_4X_HINT);
    sk_set_init(on_init, NULL);
    sk_set_frame(frame, NULL);
    return sk_run();
}
