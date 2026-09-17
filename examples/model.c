/* libsk model example — static glTF (cgltf) rendered with a lit/textured
 * pipeline, loaded async and placed in the scene. 
 * Note that for this example, we create the model handle first, then load the mesh async and set it on the model once ready.
 * This allows the model to be added to the scene immediately, and the mesh will appear once loaded. 
 */
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "example_assets.h"
#include "sk.h"

#define MODEL_PATH "models/gumshoe/gumshoe.glb"

static sk_handle_t g_scene;
static sk_handle_t g_camera;
static sk_handle_t g_bg;
static sk_handle_t g_model;
static bool g_orbit_camera = true;
static bool g_spin_model = false;

static void on_mesh_asset_loaded(const char *path, void *user) {
  sk_handle_t model = (sk_handle_t)(uintptr_t)user; /* handle passed by value */
  sk_handle_t mesh = sk_mesh_create(path);
  sk_model_set_mesh(model, mesh);
  sk_mesh_release(mesh); /* the model holds its own reference to the mesh */
}

static void on_failed(const char *p, void *u) {
  (void)u;
  sk_logger_error("model load failed: %s", p);
}

static sk_handle_t create_model(const char *mesh_path) {
  sk_handle_t model = sk_model_create(0); /* empty: mesh attached when it loads */
  sk_model_set_transform(model, 0, 0, 0, 0, 0, 0, 1, 1, 1);
  sk_model_set_tint(model, SK_COLOR_RAYWHITE);
  /* drive skeletal animation if the glTF has any (no-op until the mesh arrives) */
  sk_model_set_animation(model, 3);
  sk_model_set_animation_speed(model, 1.0f);
  sk_model_set_animation_loop(model, true);

  /* pass the handle by value through user_data — `model` is a local, so &model
   * would dangle by the time the async callback fires */
  sk_asset_add_task(sk_asset_ensure_async(mesh_path, NULL, 0),
                    on_mesh_asset_loaded, on_failed, (void *)(uintptr_t)model);
  return model;
}

static void on_init(void *user_data) {
  sk_asset_set_host(EXAMPLE_ASSET_BASE);
  (void)user_data;
  g_bg = sk_color_create(30, 32, 40, 255);
  g_camera = sk_camera3d_create(SK_CAMERA3D_PERSPECTIVE);
  sk_camera3d_set_view(g_camera, 8, 8, 8, 0, 3, 0, 0, 1, 0);
  g_scene = sk_scene_create();
  sk_scene_set_active_camera(g_scene, g_camera);

  /* scenes start unlit: add a sun and some ambient */
  sk_handle_t sun = sk_light_create(SK_LIGHT_DIRECTIONAL);
  sk_light_set_direction(sun, -0.6f, -1.0f, -0.5f);
  sk_light_set_intensity(sun, 3.0f); /* ~pi: a white surface facing the sun shows its full color */
  sk_scene_add(g_scene, sun, 0);
  sk_scene_set_ambient(g_scene, 0, 0.3f);
  sk_debug_enable_fps(12, 10, 16);

  g_model = create_model(MODEL_PATH);
  sk_scene_add(g_scene, g_model, 0);
}

static void frame(float dt, float tick_fraction, void *user_data) {
  (void)user_data;
  float t = (float)sk_get_time();

  // orbit the camera around the model
  if (g_orbit_camera) {
    sk_camera3d_set_view(g_camera, cosf(t * 0.4f) * 9.0f, 7.0f,
                         sinf(t * 0.4f) * 9.0f, 0, 3, 0, 0, 1, 0);
  }

  // spin the model in place
  if (g_spin_model) {
    sk_model_set_transform(g_model, 0, 0, 0, 0, t * 0.5f, 0, 1, 1,
                           1); /* slow spin */
  }
  sk_model_animate(g_model, dt); /* skeletal anim */

  sk_render_begin();
  sk_render_clear_background(g_bg);

  sk_render_begin_mode_3d();
  sk_shape3d_draw_grid(20, 1.0f, SK_COLOR_DARKGRAY);
  sk_render_end_mode_3d();

  sk_scene_draw(g_scene);

  sk_text_draw("libsk + sokol — model (glTF/cgltf)", 12, 36, 22,
               SK_COLOR_RAYWHITE);
  sk_text_draw(g_model ? "gumshoe.glb — skeletal animation (glTF skin)"
                        : "loading model...",
               12, 68, 16, SK_COLOR_LIGHTGRAY);

  sk_render_end();

  sk_keyboard_state_t kb = sk_input_get_keyboard_state();
  if (kb.keys[SK_KEY_ESCAPE] == SK_BUTTON_PRESSED) {
    sk_request_quit();
  }
}

int main(void) {
  sk_init_values(900, 700, "libsk model", SK_WINDOW_FLAG_MSAA_4X_HINT);
  sk_set_init(on_init, NULL);
  sk_set_frame(frame, NULL);

  return sk_run();
}
