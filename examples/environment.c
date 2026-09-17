/* libsk environment example — image-based lighting, background and tone mapping.
 *
 * The material spheres (red plastic and gold, roughness 0 to 1 left to right),
 * a normal-mapped sphere and the gumshoe, lit only by an environment map: no
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
#include "sk.h"

#define SPHERE_PATH "models/sphere/sphere.glb"
#define GUMSHOE_PATH "models/gumshoe/gumshoe.glb"
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
    sk_handle_t scene;
    sk_handle_t camera;
    sk_handle_t bg;
    sk_handle_t bar;
    sk_handle_t environments[ENVIRONMENT_COUNT];
    sk_handle_t spheres[2 * COLUMNS + 1];
    sk_handle_t gumshoe;
    sk_handle_t tiles;
    int environment; /* index, ENVIRONMENT_COUNT = none */
    int blur;        /* index into BLURS, 3 = no background */
    sk_tonemap_t tonemap;
    float exposure;
    float rotation;
    float time;
} g;

static void apply_environment(void)
{
    const sk_handle_t env = g.environment < ENVIRONMENT_COUNT ? g.environments[g.environment] : 0;
    sk_scene_set_environment(g.scene, env, 1.0f, g.rotation);
    sk_scene_set_background(g.scene, g.blur < 3 ? env : 0, g.blur < 3 ? BLURS[g.blur] : 0.0f);
    sk_scene_set_tonemap(g.scene, g.tonemap, g.exposure);
}

static void on_environment_loaded(const char *path, void *user)
{
    const int index = (int)(intptr_t)user;
    g.environments[index] = sk_environment_create(path); /* prepares the lighting: a fraction of a second */
    apply_environment();
}

static void on_sphere_loaded(const char *path, void *user)
{
    sk_handle_t mesh = sk_mesh_create(path);
    (void)user;
    for (int i = 0; i < 2 * COLUMNS + 1; i++) {
        sk_model_set_mesh(g.spheres[i], mesh);
    }
    sk_mesh_release(mesh);
}

static void on_gumshoe_loaded(const char *path, void *user)
{
    sk_handle_t mesh = sk_mesh_create(path);
    (void)user;
    sk_model_set_mesh(g.gumshoe, mesh);
    sk_mesh_release(mesh);
}

static void on_normal_map_loaded(const char *path, void *user)
{
    sk_handle_t texture = sk_texture_create(path);
    (void)user;
    sk_material_set_texture(g.tiles, "normal_texture", texture);
    sk_texture_release(texture);
}

static void on_failed(const char *path, void *user)
{
    (void)user;
    sk_logger_error("load failed: %s", path);
}

static sk_handle_t create_sphere(float x, float y, float r, float gr, float b, float metallic, float roughness)
{
    sk_handle_t model = sk_model_create(0);
    sk_handle_t material = sk_material_create(SK_MATERIAL_PBR);
    sk_material_set_vec4(material, "base_color", r, gr, b, 1.0f);
    sk_material_set_float(material, "metallic", metallic);
    sk_material_set_float(material, "roughness", roughness);
    sk_model_set_transform(model, x, y, 0, 0, 0, 0, 1, 1, 1);
    sk_model_set_material(model, 0, material);
    sk_material_release(material);
    sk_scene_add(g.scene, model, 0);
    return model;
}

static void init(void *user_data)
{
    const float spacing = 1.3f;
    int n = 0;

    (void)user_data;
    sk_asset_set_host(EXAMPLE_ASSET_BASE);
    g.bg = sk_color_create(20, 22, 28, 255);
    g.bar = sk_color_create(0, 0, 0, 150);
    g.tonemap = SK_TONEMAP_NEUTRAL;

    g.camera = sk_camera3d_create(SK_CAMERA3D_PERSPECTIVE);
    g.scene = sk_scene_create();
    sk_scene_set_active_camera(g.scene, g.camera);

    for (int c = 0; c < COLUMNS; c++) {
        const float x = ((float)c - (COLUMNS - 1) * 0.5f) * spacing;
        const float roughness = (float)c / (COLUMNS - 1);
        g.spheres[n++] = create_sphere(x, 1.9f, 0.8f, 0.05f, 0.04f, 0.0f, roughness);
        g.spheres[n++] = create_sphere(x, 0.6f, 1.0f, 0.77f, 0.34f, 1.0f, roughness);
    }
    g.spheres[n] = create_sphere(-1.3f, -0.7f, 0.9f, 0.9f, 0.9f, 0.0f, 0.3f);
    g.tiles = sk_model_get_material(g.spheres[n], 0); /* borrowed: the model's own material */

    g.gumshoe = sk_model_create(0);
    sk_model_set_transform(g.gumshoe, 1.3f, -1.3f, 0, 0, 0.4f, 0, 0.3f, 0.3f, 0.3f);
    sk_model_set_animation(g.gumshoe, 3);
    sk_scene_add(g.scene, g.gumshoe, 0);

    apply_environment();
    for (int i = 0; i < ENVIRONMENT_COUNT; i++) {
        sk_asset_add_task(sk_asset_ensure_async(ENVIRONMENT_PATHS[i], NULL, SK_ASSET_NONE), on_environment_loaded,
                          on_failed, (void *)(intptr_t)i);
    }
    sk_asset_add_task(sk_asset_ensure_async(SPHERE_PATH, NULL, SK_ASSET_NONE), on_sphere_loaded, on_failed, NULL);
    sk_asset_add_task(sk_asset_ensure_async(GUMSHOE_PATH, NULL, SK_ASSET_NONE), on_gumshoe_loaded, on_failed, NULL);
    sk_asset_add_task(sk_asset_ensure_async(NORMAL_MAP_PATH, NULL, SK_ASSET_NONE), on_normal_map_loaded, on_failed,
                      NULL);
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    const sk_keyboard_state_t kb = sk_input_get_keyboard_state();
    char line[160];
    bool changed = false;

    (void)tick_fraction;
    (void)user_data;
    if (kb.keys[SK_KEY_ESCAPE] == SK_BUTTON_PRESSED) sk_request_quit();
    if (kb.keys[SK_KEY_E] == SK_BUTTON_PRESSED) { g.environment = (g.environment + 1) % (ENVIRONMENT_COUNT + 1); changed = true; }
    if (kb.keys[SK_KEY_B] == SK_BUTTON_PRESSED) { g.blur = (g.blur + 1) % 4; changed = true; }
    if (kb.keys[SK_KEY_T] == SK_BUTTON_PRESSED) {
        g.tonemap = g.tonemap == SK_TONEMAP_NEUTRAL ? SK_TONEMAP_ACES
                  : g.tonemap == SK_TONEMAP_ACES ? SK_TONEMAP_NONE : SK_TONEMAP_NEUTRAL;
        changed = true;
    }
    if (kb.keys[SK_KEY_UP] == SK_BUTTON_PRESSED) { g.exposure += 0.5f; changed = true; }
    if (kb.keys[SK_KEY_DOWN] == SK_BUTTON_PRESSED) { g.exposure -= 0.5f; changed = true; }
    if (kb.keys[SK_KEY_LEFT] >= SK_BUTTON_PRESSED) { g.rotation -= dt; changed = true; }
    if (kb.keys[SK_KEY_RIGHT] >= SK_BUTTON_PRESSED) { g.rotation += dt; changed = true; }
    if (changed) apply_environment();

    g.time += dt;
    sk_camera3d_set_view(g.camera, sinf(g.time * 0.15f) * 7.5f, 1.2f, cosf(g.time * 0.15f) * 7.5f, 0, 0.3f, 0,
                         0, 1, 0);
    sk_model_animate(g.gumshoe, dt);

    sk_render_begin();
    sk_render_clear_background(g.bg);
    sk_scene_draw(g.scene);
    sk_shape2d_draw_rectangle(0, 0, (int)sk_window_get_screen_size().x, 60, g.bar);
    sk_text_draw("libsk environment lighting: reflections, background and tone mapping", 12, 12, 16,
                 SK_COLOR_RAYWHITE);
    snprintf(line, sizeof(line), "[E] %s   [B] background %s   [T] tone mapping %s   [UP/DOWN] exposure %+.1f EV",
             ENVIRONMENT_NAMES[g.environment], g.blur < 3 ? (g.blur == 0 ? "sharp" : g.blur == 1 ? "soft" : "blurred") : "off",
             TONEMAP_NAMES[g.tonemap], g.exposure);
    sk_text_draw(line, 12, 36, 16, SK_COLOR_LIGHTGRAY);
    sk_render_end();
}

int main(void)
{
    sk_init_values(1100, 720, "libsk environment", SK_WINDOW_FLAG_MSAA_4X_HINT);
    sk_set_init(init, NULL);
    sk_set_frame(frame, NULL);
    return sk_run();
}
