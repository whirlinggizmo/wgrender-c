/* libsk lights example — directional, point and spot lights in a scene.
 *
 * Five animated models on a grid:
 *   - a dim warm sun (directional)
 *   - a cyan point light orbiting through them, falling off with its range (the
 *     small sphere marks it; shapes are unlit, so it shows the light's color)
 *   - a white spotlight sweeping across them from above
 * Scenes start unlit (no lights, no ambient); everything here is explicit.
 * Keys: 1 sun, 2 point light, 3 spotlight, ESC quit. */
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>

#include "example_assets.h"
#include "sk.h"

#define MODEL_PATH "models/gumshoe/gumshoe.glb"

enum { MODEL_COUNT = 5 };

static struct {
    sk_handle_t scene;
    sk_handle_t camera;
    sk_handle_t bg;
    sk_handle_t grid;
    sk_handle_t models[MODEL_COUNT];
    sk_handle_t sun;
    sk_handle_t lamp;
    sk_handle_t lamp_marker;
    sk_handle_t spot;
    float time;
} g;

static void on_mesh_loaded(const char *path, void *user)
{
    sk_handle_t mesh = sk_mesh_create(path);
    (void)user;
    for (int i = 0; i < MODEL_COUNT; i++) {
        sk_model_set_mesh(g.models[i], mesh);
    }
    sk_mesh_destroy(mesh); /* the models hold their own references */
}

static void on_failed(const char *path, void *user)
{
    (void)user;
    sk_logger_error("load failed: %s", path);
}

static void toggle(sk_handle_t light)
{
    sk_light_set_enabled(light, !sk_light_is_enabled(light));
}

static void init(void *user_data)
{
    (void)user_data;
    sk_asset_set_host(EXAMPLE_ASSET_BASE);
    g.bg = sk_color_create(12, 13, 18, 255);
    g.grid = sk_color_create(40, 42, 50, 255);
    g.camera = sk_camera3d_create(SK_CAMERA3D_PERSPECTIVE);
    sk_camera3d_set_view(g.camera, 0, 4.5f, 10, 0, 1, 0, 0, 1, 0);
    g.scene = sk_scene_create();
    sk_scene_set_active_camera(g.scene, g.camera);
    sk_scene_set_ambient(g.scene, sk_color_create(90, 110, 160, 255), 0.05f);

    for (int i = 0; i < MODEL_COUNT; i++) {
        g.models[i] = sk_model_create(0);
        sk_model_set_transform(g.models[i], -4.0f + 2.0f * (float)i, 0, (i % 2) ? -0.8f : 0.8f, 0, 0, 0, 1, 1, 1);
        sk_model_set_animation(g.models[i], 3);
        sk_model_set_animation_loop(g.models[i], true);
        sk_scene_add(g.scene, g.models[i], 0);
    }

    g.sun = sk_light_create(SK_LIGHT_DIRECTIONAL);
    sk_light_set_direction(g.sun, -0.4f, -1.0f, -0.6f);
    sk_light_set_color(g.sun, sk_color_create(255, 210, 160, 255));
    sk_light_set_intensity(g.sun, 1.1f);
    sk_scene_add(g.scene, g.sun, 0);

    g.lamp = sk_light_create(SK_LIGHT_POINT);
    sk_light_set_color(g.lamp, sk_color_create(60, 220, 255, 255));
    sk_light_set_intensity(g.lamp, 20.0f);
    sk_light_set_range(g.lamp, 5.0f);
    sk_scene_add(g.scene, g.lamp, 0);
    g.lamp_marker = sk_shape_create();
    sk_shape_set_sphere(g.lamp_marker, 0.12f);
    sk_shape_set_color(g.lamp_marker, sk_color_create(60, 220, 255, 255));
    sk_scene_add(g.scene, g.lamp_marker, 0);

    g.spot = sk_light_create(SK_LIGHT_SPOT);
    sk_light_set_position(g.spot, 0, 6, 2);
    sk_light_set_spot_cone(g.spot, 0.14f, 0.28f); /* radians: about 8 and 16 degrees */
    sk_light_set_intensity(g.spot, 125.0f);
    sk_scene_add(g.scene, g.spot, 0);

    sk_asset_add_task(sk_asset_ensure_async(MODEL_PATH, NULL, SK_ASSET_NONE), on_mesh_loaded, on_failed, NULL);
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    (void)tick_fraction;
    (void)user_data;
    sk_keyboard_state_t kb = sk_input_get_keyboard_state();
    char line[128];

    if (kb.keys[SK_KEY_1] == SK_BUTTON_PRESSED) toggle(g.sun);
    if (kb.keys[SK_KEY_2] == SK_BUTTON_PRESSED) toggle(g.lamp);
    if (kb.keys[SK_KEY_3] == SK_BUTTON_PRESSED) toggle(g.spot);
    if (kb.keys[SK_KEY_ESCAPE] == SK_BUTTON_PRESSED) sk_request_quit();

    g.time += dt;
    for (int i = 0; i < MODEL_COUNT; i++) {
        sk_model_animate(g.models[i], dt);
    }

    /* point light orbits through the row; spot sweeps left and right */
    float lx = sinf(g.time * 0.6f) * 5.0f, lz = cosf(g.time * 0.6f) * 2.0f;
    sk_light_set_position(g.lamp, lx, 1.2f, lz);
    sk_shape_set_transform(g.lamp_marker, lx, 1.2f, lz, 0, 0, 0, 1, 1, 1);
    sk_shape_set_visible(g.lamp_marker, sk_light_is_enabled(g.lamp));
    sk_light_set_direction(g.spot, sinf(g.time * 0.8f) * 0.7f, -1.0f, -0.3f);

    sk_render_begin();
    sk_render_clear_background(g.bg);
    sk_render_begin_mode_3d();
    sk_shape_draw_grid(20, 1.0f, g.grid);
    sk_render_end_mode_3d();
    sk_scene_draw(g.scene);

    sk_text_draw("libsk lights: directional, point, spot", 12, 12, 20, SK_COLOR_RAYWHITE);
    snprintf(line, sizeof(line), "[1] sun %s   [2] point light %s   [3] spotlight %s",
             sk_light_is_enabled(g.sun) ? "on " : "off", sk_light_is_enabled(g.lamp) ? "on " : "off",
             sk_light_is_enabled(g.spot) ? "on " : "off");
    sk_text_draw(line, 12, 40, 16, SK_COLOR_LIGHTGRAY);
    sk_text_draw_fps(12, 64);
    sk_render_end();
}

int main(void)
{
    sk_init_values(1000, 600, "libsk lights", SK_WINDOW_FLAG_MSAA_4X_HINT);
    sk_set_init(init, NULL);
    sk_set_frame(frame, NULL);
    return sk_run();
}
