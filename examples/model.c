/* libwgrender model example — static glTF (cgltf) rendered with a lit/textured
 * pipeline, loaded async and placed in the scene. 
 * Note that for this example, we create the model handle first, then load the mesh async and set it on the model once ready.
 * This allows the model to be added to the scene immediately, and the mesh will appear once loaded. 
 */
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "example_assets.h"
#include "wgr.h"

#define MODEL_PATH "models/gumshoe/gumshoe.glb"

static wgr_handle_t g_scene;
static wgr_handle_t g_camera;
static wgr_color_t g_bg;
static wgr_handle_t g_model;
static bool g_orbit_camera = true;
static bool g_spin_model = false;

static void on_mesh_asset_loaded(const char *path, void *user) {
  wgr_handle_t model = (wgr_handle_t)(uintptr_t)user; /* handle passed by value */
  wgr_handle_t mesh = wgr_mesh_create(path);
  wgr_model_set_mesh(model, mesh);
  wgr_mesh_release(mesh); /* the model holds its own reference to the mesh */
}

static void on_failed(const char *p, void *u) {
  (void)u;
  wgr_logger_error("model load failed: %s", p);
}

static wgr_handle_t create_model(const char *mesh_path) {
  wgr_handle_t model = wgr_model_create(0); /* empty: mesh attached when it loads */
  wgr_model_set_transform(model, 0, 0, 0, 0, 0, 0, 1, 1, 1);
  wgr_model_set_tint(model, WGR_COLOR_RAYWHITE);
  /* drive skeletal animation if the glTF has any (no-op until the mesh arrives) */
  wgr_model_set_animation(model, 3);
  wgr_model_set_animation_speed(model, 1.0f);
  wgr_model_set_animation_loop(model, true);

  /* pass the handle by value through user_data — `model` is a local, so &model
   * would dangle by the time the async callback fires */
  wgr_asset_add_task(wgr_asset_ensure_async(mesh_path, NULL, 0),
                    on_mesh_asset_loaded, on_failed, (void *)(uintptr_t)model);
  return model;
}

static void on_init(void *user_data) {
  wgr_asset_set_host(EXAMPLE_ASSET_BASE);
  (void)user_data;
  g_bg = wgr_color_rgba(30, 32, 40, 255);
  g_camera = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE);
  wgr_camera3d_set_view(g_camera, 8, 8, 8, 0, 3, 0, 0, 1, 0);
  g_scene = wgr_scene_create();
  wgr_scene_set_active_camera(g_scene, g_camera);

  /* scenes start unlit: add a sun and some ambient */
  wgr_handle_t sun = wgr_light_create(WGR_LIGHT_DIRECTIONAL);
  wgr_light_set_direction(sun, -0.6f, -1.0f, -0.5f);
  wgr_light_set_intensity(sun, 3.0f); /* ~pi: a white surface facing the sun shows its full color */
  wgr_scene_add(g_scene, sun, 0);
  wgr_scene_set_ambient(g_scene, WGR_COLOR_WHITE, 0.3f);
  wgr_debug_enable_fps(12, 10, 16);

  g_model = create_model(MODEL_PATH);
  wgr_scene_add(g_scene, g_model, 0);
}

static void frame(float dt, float tick_fraction, void *user_data) {
  (void)user_data;
  float t = (float)wgr_get_time();

  // orbit the camera around the model
  if (g_orbit_camera) {
    wgr_camera3d_set_view(g_camera, cosf(t * 0.4f) * 9.0f, 7.0f,
                         sinf(t * 0.4f) * 9.0f, 0, 3, 0, 0, 1, 0);
  }

  // spin the model in place
  if (g_spin_model) {
    wgr_model_set_transform(g_model, 0, 0, 0, 0, t * 0.5f, 0, 1, 1,
                           1); /* slow spin */
  }
  wgr_model_animate(g_model, dt); /* skeletal anim */

  wgr_render_begin_frame();
  wgr_render_clear_background(g_bg);

  wgr_render_begin_mode_3d();
  wgr_shape3d_draw_grid(20, 1.0f, WGR_COLOR_DARKGRAY);
  wgr_render_end_mode_3d();

  wgr_scene_draw(g_scene);

  wgr_text_draw("libwgrender + sokol — model (glTF/cgltf)", 12, 36, 22,
               WGR_COLOR_RAYWHITE);
  wgr_text_draw(g_model ? "gumshoe.glb — skeletal animation (glTF skin)"
                        : "loading model...",
               12, 68, 16, WGR_COLOR_LIGHTGRAY);

  wgr_render_end_frame();

  wgr_keyboard_state_t kb = wgr_input_get_keyboard_state();
  if (kb.keys[WGR_KEY_ESCAPE] == WGR_BUTTON_PRESSED) {
    wgr_request_quit();
  }
}

int main(void) {
  wgr_init_values(900, 700, "libwgrender model", WGR_WINDOW_FLAG_MSAA_4X_HINT | WGR_WINDOW_FLAG_WINDOW_RESIZABLE);
  wgr_set_init(on_init, NULL);
  wgr_set_frame(frame, NULL);

  return wgr_run();
}
