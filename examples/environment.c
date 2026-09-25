/* libwgrender environment example — image-based lighting, background and tone mapping.
 *
 * The material spheres (red plastic and gold, roughness 0 to 1 left to right),
 * a normal-mapped sphere and the woman, lit only by an environment map: no
 * lights, no ambient. Metals reflect the environment; rough surfaces blur it.
 *
 * Keys:
 *   E            environment: sunset, studio, none
 *   B            background blur: sharp, soft, blurred, off
 *   T            tone mapping: Neutral, ACES, none
 *   UP / DOWN    exposure (+/- half a stop)
 *   LEFT / RIGHT rotate the environment
 *   ESC          quit */
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "example_assets.h"
#include "wgr.h"

#define SPHERE_PATH "models/sphere/sphere.glb"
#define WOMAN_CASUAL_PATH "models/woman_casual/woman_casual.glb"
#define NORMAL_MAP_PATH "textures/tiles_normal.png"

enum { COLUMNS = 5, ENVIRONMENT_COUNT = 2 };

static const char *ENVIRONMENT_PATHS[ENVIRONMENT_COUNT] = {
    "environments/venice_sunset_1k.hdr",
    "environments/studio_small_09_1k.hdr",
};
static const char *ENVIRONMENT_NAMES[ENVIRONMENT_COUNT + 1] = {"sunset", "studio", "none"};
static const char *TONEMAP_NAMES[] = {"none", "Neutral", "ACES"};
static const float BLURS[] = {0.0f, 0.35f, 0.8f};

static struct {
    wgr_handle_t scene;
    wgr_handle_t camera;
    wgr_color_t bg;
    wgr_color_t bar;
    wgr_handle_t environments[ENVIRONMENT_COUNT];
    wgr_handle_t spheres[2 * COLUMNS + 1];
    wgr_handle_t woman_casual;
    wgr_handle_t tiles;
    int environment; /* index, ENVIRONMENT_COUNT = none */
    int blur;        /* index into BLURS, 3 = no background */
    wgr_tonemap_t tonemap;
    float exposure;
    float rotation;
    float time;
} g;

static void apply_environment(void)
{
    const wgr_handle_t env = g.environment < ENVIRONMENT_COUNT ? g.environments[g.environment] : 0;
    wgr_scene_set_environment(g.scene, env, 1.0f, g.rotation);
    wgr_scene_set_background(g.scene, g.blur < 3 ? env : 0, g.blur < 3 ? BLURS[g.blur] : 0.0f);
    wgr_scene_set_tonemap(g.scene, g.tonemap, g.exposure);
}

static void on_environment_loaded(const char *path, void *user)
{
    const int index = (int)(intptr_t)user;
    g.environments[index] = wgr_environment_create(path); /* prepares the lighting: a fraction of a second */
    apply_environment();
}

static void on_sphere_loaded(const char *path, void *user)
{
    wgr_handle_t mesh = wgr_mesh_create(path);
    (void)user;
    for (int i = 0; i < 2 * COLUMNS + 1; i++) {
        wgr_model_set_mesh(g.spheres[i], mesh);
    }
    wgr_mesh_release(mesh);
}

static void on_woman_casual_loaded(const char *path, void *user)
{
    wgr_handle_t mesh = wgr_mesh_create(path);
    (void)user;
    wgr_model_set_mesh(g.woman_casual, mesh);
    wgr_mesh_release(mesh);
}

static void on_normal_map_loaded(const char *path, void *user)
{
    wgr_handle_t texture = wgr_texture_create(path);
    (void)user;
    wgr_material_set_texture(g.tiles, "normal_texture", texture);
    wgr_texture_release(texture);
}

static void on_failed(const char *path, void *user)
{
    (void)user;
    wgr_logger_error("load failed: %s", path);
}

static wgr_handle_t create_sphere(float x, float y, float r, float gr, float b, float metallic, float roughness)
{
    wgr_handle_t model = wgr_model_create(0);
    wgr_handle_t material = wgr_material_create(WGR_MATERIAL_PBR);
    wgr_material_set_vec4(material, "base_color", r, gr, b, 1.0f);
    wgr_material_set_float(material, "metallic", metallic);
    wgr_material_set_float(material, "roughness", roughness);
    wgr_model_set_transform(model, x, y, 0, 0, 0, 0, 1, 1, 1);
    wgr_model_set_material(model, 0, material);
    wgr_material_release(material);
    wgr_scene_add(g.scene, model, 0);
    return model;
}

static void init(void *user_data)
{
    const float spacing = 1.3f;
    int n = 0;

    (void)user_data;
    wgr_asset_set_host(EXAMPLE_ASSET_BASE);
    g.bg = wgr_color_rgba(20, 22, 28, 255);
    g.bar = wgr_color_rgba(0, 0, 0, 150);
    g.tonemap = WGR_TONEMAP_NEUTRAL;

    g.camera = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE);
    g.scene = wgr_scene_create();
    wgr_scene_set_active_camera(g.scene, g.camera);

    for (int c = 0; c < COLUMNS; c++) {
        const float x = ((float)c - (COLUMNS - 1) * 0.5f) * spacing;
        const float roughness = (float)c / (COLUMNS - 1);
        g.spheres[n++] = create_sphere(x, 1.9f, 0.8f, 0.05f, 0.04f, 0.0f, roughness);
        g.spheres[n++] = create_sphere(x, 0.6f, 1.0f, 0.77f, 0.34f, 1.0f, roughness);
    }
    g.spheres[n] = create_sphere(-1.3f, -0.7f, 0.9f, 0.9f, 0.9f, 0.0f, 0.3f);
    g.tiles = wgr_model_get_material(g.spheres[n], 0); /* borrowed: the model's own material */

    g.woman_casual = wgr_model_create(0);
    wgr_model_set_transform(g.woman_casual, 1.3f, -1.3f, 0, 0, 0.4f, 0, 0.3f, 0.3f, 0.3f);
    wgr_model_set_animation(g.woman_casual, 3);
    wgr_scene_add(g.scene, g.woman_casual, 0);

    apply_environment();
    for (int i = 0; i < ENVIRONMENT_COUNT; i++) {
        wgr_asset_add_task(wgr_asset_ensure_async(ENVIRONMENT_PATHS[i], NULL, WGR_ASSET_NONE), on_environment_loaded,
                          on_failed, (void *)(intptr_t)i);
    }
    wgr_asset_add_task(wgr_asset_ensure_async(SPHERE_PATH, NULL, WGR_ASSET_NONE), on_sphere_loaded, on_failed, NULL);
    wgr_asset_add_task(wgr_asset_ensure_async(WOMAN_CASUAL_PATH, NULL, WGR_ASSET_NONE), on_woman_casual_loaded, on_failed, NULL);
    wgr_asset_add_task(wgr_asset_ensure_async(NORMAL_MAP_PATH, NULL, WGR_ASSET_NONE), on_normal_map_loaded, on_failed,
                      NULL);
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    const wgr_keyboard_state_t kb = wgr_input_get_keyboard_state();
    char line[160];
    bool changed = false;

    (void)tick_fraction;
    (void)user_data;
    if (kb.keys[WGR_KEY_ESCAPE] == WGR_BUTTON_PRESSED) wgr_request_quit();
    if (kb.keys[WGR_KEY_E] == WGR_BUTTON_PRESSED) { g.environment = (g.environment + 1) % (ENVIRONMENT_COUNT + 1); changed = true; }
    if (kb.keys[WGR_KEY_B] == WGR_BUTTON_PRESSED) { g.blur = (g.blur + 1) % 4; changed = true; }
    if (kb.keys[WGR_KEY_T] == WGR_BUTTON_PRESSED) {
        g.tonemap = g.tonemap == WGR_TONEMAP_NEUTRAL ? WGR_TONEMAP_ACES
                  : g.tonemap == WGR_TONEMAP_ACES ? WGR_TONEMAP_NONE : WGR_TONEMAP_NEUTRAL;
        changed = true;
    }
    if (kb.keys[WGR_KEY_UP] == WGR_BUTTON_PRESSED) { g.exposure += 0.5f; changed = true; }
    if (kb.keys[WGR_KEY_DOWN] == WGR_BUTTON_PRESSED) { g.exposure -= 0.5f; changed = true; }
    if (kb.keys[WGR_KEY_LEFT] >= WGR_BUTTON_PRESSED) { g.rotation -= dt; changed = true; }
    if (kb.keys[WGR_KEY_RIGHT] >= WGR_BUTTON_PRESSED) { g.rotation += dt; changed = true; }
    if (changed) apply_environment();

    g.time += dt;
    wgr_camera3d_set_view(g.camera, sinf(g.time * 0.15f) * 7.5f, 1.2f, cosf(g.time * 0.15f) * 7.5f, 0, 0.3f, 0,
                         0, 1, 0);
    wgr_model_animate(g.woman_casual, dt);

    wgr_render_begin_frame();
    wgr_render_clear_background(g.bg);
    wgr_scene_draw(g.scene);
    wgr_shape2d_draw_rectangle(0, 0, (int)wgr_window_get_screen_size().x, 60, g.bar);
    wgr_text_draw("libwgrender environment lighting: reflections, background and tone mapping", 12, 12, 16,
                 WGR_COLOR_RAYWHITE);
    snprintf(line, sizeof(line), "[E] %s   [B] background %s   [T] tone mapping %s   [UP/DOWN] exposure %+.1f EV",
             ENVIRONMENT_NAMES[g.environment], g.blur < 3 ? (g.blur == 0 ? "sharp" : g.blur == 1 ? "soft" : "blurred") : "off",
             TONEMAP_NAMES[g.tonemap], g.exposure);
    wgr_text_draw(line, 12, 36, 16, WGR_COLOR_LIGHTGRAY);
    wgr_render_end_frame();
}

int main(void)
{
    wgr_init_values(1100, 720, "libwgrender environment", WGR_WINDOW_FLAG_MSAA_4X_HINT | WGR_WINDOW_FLAG_WINDOW_RESIZABLE);
    wgr_set_init(init, NULL);
    wgr_set_frame(frame, NULL);
    return wgr_run();
}
