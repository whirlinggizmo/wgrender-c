/* libsk shadows example — a casting light and what it does to a scene.
 *
 * The sun casts (sk_light_set_casts_shadows): once a frame it draws everything that
 * casts into a depth map, and the lit shading darkens what's behind something. The
 * scene is a floor, a wall, some generated shapes and an animated gumshoe, so the
 * shadows fall across each other and across themselves.
 *
 *   - 1 turns casting on and off, the difference this whole feature makes
 *   - the ball on the left doesn't cast (sk_model_set_casts_shadow), so it floats
 *     like everything did before shadows; the one on the right doesn't receive
 *     (sk_model_set_receives_shadow), so the wall's shadow passes over it
 *   - UP/DOWN change the shadow distance: less distance covers less of the scene with
 *     the same map, so the shadows get sharper
 *   - [ and ] change the depth bias: too little stripes the lit surfaces ("acne"),
 *     too much lifts a shadow away from what casts it
 *   - M cycles the map size (512, 1024, 2048, 4096)
 *   - S changes how much light a shadow blocks, and T tints what it leaves behind
 *
 * Keys: 1 shadows, UP/DOWN distance, [ ] bias, M map size, S strength, T tint,
 * O camera, ESC quit. */
#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "example_assets.h"
#include "sk.h"

#define GUMSHOE_PATH "models/gumshoe/gumshoe.glb"

enum { SIZE_COUNT = 4 };
static const int MAP_SIZES[SIZE_COUNT] = {512, 1024, 2048, 4096};

static struct {
    sk_handle_t scene, camera, sun, gumshoe;
    sk_handle_t no_cast, no_receive;
    bool shadows, orbit;
    float distance, bias, strength;
    int size_index, tint_index;
    float angle, time;
} g = {.shadows = true, .orbit = true, .distance = 30.0f, .bias = 1.0f, .strength = 1.0f, .size_index = 2};

/* what a shadow keeps of the light: nothing (physical), then two stylised tints */
enum { TINT_COUNT = 3 };
static const sk_color_t TINTS[TINT_COUNT] = {0x000000FFu, 0x1E3C64FFu, 0x64321EFFu};
static const char *TINT_NAMES[TINT_COUNT] = {"none", "cool", "warm"};

static void on_model_loaded(const char *path, void *user)
{
    sk_handle_t mesh = sk_mesh_create(path);
    (void)user;
    sk_model_set_mesh(g.gumshoe, mesh);
    sk_mesh_release(mesh);
    sk_model_set_animation(g.gumshoe, 3);
    sk_model_set_animation_loop(g.gumshoe, true);
}

static void on_failed(const char *path, void *user)
{
    (void)user;
    sk_logger_error("load failed: %s", path);
}

/* A model of `mesh` at (x, y, z) in one color, added to the scene. */
static sk_handle_t place(sk_handle_t mesh, float x, float y, float z, float r, float gr, float b, float roughness)
{
    const sk_handle_t model = sk_model_create(mesh);
    const sk_handle_t material = sk_material_create(SK_MATERIAL_PBR);
    sk_mesh_release(mesh); /* the model holds it */
    sk_model_set_transform(model, x, y, z, 0, 0, 0, 1, 1, 1);
    sk_material_set_vec4(material, "base_color", r, gr, b, 1.0f);
    sk_material_set_float(material, "metallic", 0.0f);
    sk_material_set_float(material, "roughness", roughness);
    sk_model_set_material(model, -1, material);
    sk_material_release(material);
    sk_scene_add(g.scene, model, 0);
    return model;
}

static void init(void *user_data)
{
    (void)user_data;
    sk_asset_set_host(EXAMPLE_ASSET_BASE);

    g.camera = sk_camera3d_create(SK_CAMERA3D_PERSPECTIVE);
    g.scene = sk_scene_create();
    sk_scene_set_active_camera(g.scene, g.camera);
    sk_scene_set_ambient(g.scene, sk_color_rgba(140, 170, 225, 255), 0.25f);

    g.sun = sk_light_create(SK_LIGHT_DIRECTIONAL);
    sk_light_set_direction(g.sun, -0.75f, -0.85f, -0.35f);
    sk_light_set_color(g.sun, sk_color_rgba(255, 244, 224, 255));
    sk_light_set_intensity(g.sun, 3.2f);
    sk_light_set_casts_shadows(g.sun, true);
    sk_light_set_shadow_distance(g.sun, g.distance);
    sk_light_set_shadow_map_size(g.sun, MAP_SIZES[g.size_index]);
    sk_light_set_shadow_strength(g.sun, g.strength);
    sk_light_set_shadow_color(g.sun, TINTS[g.tint_index]);
    sk_scene_add(g.scene, g.sun, 0);

    place(sk_mesh_create_plane(40.0f, 40.0f, 0), 0, 0, 0, 0.42f, 0.44f, 0.46f, 0.9f);
    /* a wall to throw a long shadow across the floor */
    place(sk_mesh_create_cube(0.5f, 3.0f, 7.0f), -4.5f, 1.5f, 0, 0.55f, 0.5f, 0.45f, 0.85f);
    place(sk_mesh_create_torus(0.7f, 0.22f, 48, 24), 2.6f, 1.1f, -1.6f, 0.9f, 0.55f, 0.2f, 0.4f);
    place(sk_mesh_create_capsule(0.4f, 1.6f, 16, 32), 1.2f, 0.8f, 1.8f, 0.35f, 0.75f, 0.45f, 0.5f);
    place(sk_mesh_create_cube(1.0f, 1.0f, 1.0f), 3.8f, 0.5f, 1.4f, 0.3f, 0.5f, 0.85f, 0.6f);

    /* one that casts nothing, and one that nothing shadows */
    g.no_cast = place(sk_mesh_create_sphere(0.6f, 24, 48), -2.0f, 0.6f, 2.4f, 0.95f, 0.85f, 0.3f, 0.35f);
    sk_model_set_casts_shadow(g.no_cast, false);
    g.no_receive = place(sk_mesh_create_sphere(0.6f, 24, 48), -2.6f, 0.6f, -1.2f, 0.9f, 0.3f, 0.5f, 0.35f);
    sk_model_set_receives_shadow(g.no_receive, false);

    g.gumshoe = sk_model_create(0);
    sk_model_set_transform(g.gumshoe, 0, 0, 0, 0, 0, 0, 1, 1, 1);
    sk_scene_add(g.scene, g.gumshoe, 0);
    sk_asset_add_task(sk_asset_ensure_async(GUMSHOE_PATH, NULL, SK_ASSET_NONE), on_model_loaded, on_failed, NULL);
    sk_debug_enable_fps(12, 10, 16);
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    char line[128];
    (void)tick_fraction;
    (void)user_data;

    if (sk_input_get_key(SK_KEY_ESCAPE) == SK_BUTTON_PRESSED) sk_request_quit();
    if (sk_input_get_key(SK_KEY_1) == SK_BUTTON_PRESSED) {
        g.shadows = !g.shadows;
        sk_light_set_casts_shadows(g.sun, g.shadows);
    }
    if (sk_input_get_key(SK_KEY_O) == SK_BUTTON_PRESSED) g.orbit = !g.orbit;
    if (sk_input_get_key(SK_KEY_S) == SK_BUTTON_PRESSED) {
        g.strength = g.strength > 0.9f ? 0.65f : g.strength > 0.5f ? 0.35f : 1.0f;
        sk_light_set_shadow_strength(g.sun, g.strength);
    }
    if (sk_input_get_key(SK_KEY_T) == SK_BUTTON_PRESSED) {
        g.tint_index = (g.tint_index + 1) % TINT_COUNT;
        sk_light_set_shadow_color(g.sun, TINTS[g.tint_index]);
    }
    if (sk_input_get_key(SK_KEY_M) == SK_BUTTON_PRESSED) {
        g.size_index = (g.size_index + 1) % SIZE_COUNT;
        sk_light_set_shadow_map_size(g.sun, MAP_SIZES[g.size_index]);
    }
    if (sk_input_get_key(SK_KEY_UP) != SK_BUTTON_UP || sk_input_get_key(SK_KEY_DOWN) != SK_BUTTON_UP) {
        const float step = sk_input_get_key(SK_KEY_UP) != SK_BUTTON_UP ? dt * 20.0f : -dt * 20.0f;
        g.distance = fmaxf(2.0f, fminf(g.distance + step, 200.0f));
        sk_light_set_shadow_distance(g.sun, g.distance);
    }
    if (sk_input_get_key(SK_KEY_LEFT_BRACKET) != SK_BUTTON_UP ||
        sk_input_get_key(SK_KEY_RIGHT_BRACKET) != SK_BUTTON_UP) {
        const float step = sk_input_get_key(SK_KEY_RIGHT_BRACKET) != SK_BUTTON_UP ? dt * 4.0f : -dt * 4.0f;
        g.bias = fmaxf(0.0f, fminf(g.bias + step, 16.0f));
        sk_light_set_shadow_bias(g.sun, g.bias, g.bias * 4.0f);
    }

    g.time += dt;
    sk_model_animate(g.gumshoe, dt);
    if (g.orbit) g.angle += dt * 0.18f;
    sk_camera3d_set_view(g.camera, 11.0f * sinf(g.angle), 5.0f, 11.0f * cosf(g.angle), 0, 1.2f, 0, 0, 1, 0);

    sk_render_begin();
    sk_render_clear_background(sk_color_rgba(120, 150, 200, 255));
    sk_scene_draw(g.scene);
    sk_text_draw("libsk shadows: a directional light casting into a depth map", 12, 36, 20, SK_COLOR_RAYWHITE);
    snprintf(line, sizeof(line), "[1] shadows %s   map %d   distance %.0f   bias %.1f texels", g.shadows ? "on" : "off",
             MAP_SIZES[g.size_index], (double)g.distance, (double)g.bias);
    sk_text_draw(line, 12, 64, 16, SK_COLOR_LIGHTGRAY);
    snprintf(line, sizeof(line), "[S] strength %.2f   [T] tint %s", (double)g.strength, TINT_NAMES[g.tint_index]);
    sk_text_draw(line, 12, 86, 16, SK_COLOR_LIGHTGRAY);
    sk_text_draw("UP/DOWN distance   [ ] bias   M map size   O camera   ESC quit", 12, 108, 16, SK_COLOR_GRAY);
    sk_text_draw("left ball casts nothing; right ball receives nothing", 12, 130, 16, SK_COLOR_GRAY);
    sk_render_end();
}

int main(void)
{
    sk_init_values(1000, 600, "libsk shadows", SK_WINDOW_FLAG_MSAA_4X_HINT | SK_WINDOW_FLAG_WINDOW_RESIZABLE);
    sk_set_init(init, NULL);
    sk_set_frame(frame, NULL);
    return sk_run();
}
