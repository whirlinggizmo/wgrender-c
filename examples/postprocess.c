/* libsk post-processing example — screen effects over the finished frame.
 *
 * The frame is an ordinary lit scene (an animated gumshoe on a floor, generated shapes,
 * a circling point light). The effects are custom materials whose shaders are screen
 * effects (examples/shaders/vignette.glsl and scanlines.glsl, compiled by
 * tools/shaderpack.py; make example-shaders):
 *
 *   - the vignette darkens the corners and warms the middle
 *   - the scanlines darken alternating rows, shift red and blue apart and flicker
 *
 * Both apply in the order they were added, so turning one off and on rebuilds the chain
 * (sk_render_clear_effects, then sk_render_add_effect again). Their parameters are
 * ordinary material parameters, so they can change any frame — here the vignette
 * breathes in and out, and the arrow keys change how dark it gets.
 *
 * Keys: 1 vignette, 2 scanlines, UP/DOWN vignette strength, SPACE stop the breathing,
 * O stop the camera, ESC quit. */
#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "example_assets.h"
#include "sk.h"

#define GUMSHOE_PATH "models/gumshoe/gumshoe.glb"
#define VIGNETTE_PATH "shaders/vignette.skshader"
#define SCANLINES_PATH "shaders/scanlines.skshader"

enum { SHAPE_COUNT = 3 };

static struct {
    sk_handle_t scene, camera, gumshoe, lamp, lamp_marker;
    sk_handle_t shapes[SHAPE_COUNT];
    sk_handle_t vignette, scanlines; /* the effect materials (0 until they load) */
    bool vignette_on, scanlines_on, breathing, orbit;
    float strength; /* the vignette's, before breathing */
    float angle, time;
} g = {.vignette_on = true, .scanlines_on = false, .breathing = true, .orbit = true, .strength = 0.85f};

/* The chain, in order: the vignette darkens the corners, then the scanlines go over
 * everything. Rebuilt whenever one is switched on or off. */
static void rebuild_effects(void)
{
    sk_render_clear_effects();
    if (g.vignette_on && g.vignette != 0) sk_render_add_effect(g.vignette);
    if (g.scanlines_on && g.scanlines != 0) sk_render_add_effect(g.scanlines);
}

static void on_model_loaded(const char *path, void *user)
{
    sk_handle_t mesh = sk_mesh_create(path);
    (void)user;
    sk_model_set_mesh(g.gumshoe, mesh);
    sk_mesh_release(mesh); /* the model holds its own reference */
    sk_model_set_animation(g.gumshoe, 3);
    sk_model_set_animation_loop(g.gumshoe, true);
}

static void on_vignette_loaded(const char *path, void *user)
{
    sk_handle_t shader = sk_shader_create(path);
    (void)user;
    g.vignette = sk_material_create_custom(shader);
    sk_shader_release(shader); /* the material holds its own reference */
    sk_material_set_float(g.vignette, "strength", g.strength);
    sk_material_set_float(g.vignette, "radius", 0.25f);
    sk_material_set_vec4(g.vignette, "tint", 1.04f, 1.0f, 0.94f, 1.0f);
    rebuild_effects();
}

static void on_scanlines_loaded(const char *path, void *user)
{
    sk_handle_t shader = sk_shader_create(path);
    (void)user;
    g.scanlines = sk_material_create_custom(shader);
    sk_shader_release(shader);
    sk_material_set_float(g.scanlines, "lines", 220.0f);
    sk_material_set_float(g.scanlines, "darkness", 0.35f);
    sk_material_set_float(g.scanlines, "offset", 1.5f);
    sk_material_set_float(g.scanlines, "flicker", 1.0f);
    rebuild_effects();
}

static void on_failed(const char *path, void *user)
{
    (void)user;
    sk_logger_error("load failed: %s", path);
}

static void load(const char *path, sk_asset_callback_fn done)
{
    sk_asset_add_task(sk_asset_ensure_async(path, NULL, SK_ASSET_NONE), done, on_failed, NULL);
}

static void init(void *user_data)
{
    /* the shapes beside the gumshoe, and their colors */
    const struct {
        sk_handle_t mesh;
        float x, y;
        float r, gr, b;
    } shapes[SHAPE_COUNT] = {
        {sk_mesh_create_sphere(0.5f, 24, 48), -2.2f, 0.5f, 0.2f, 0.55f, 0.9f},
        {sk_mesh_create_torus(0.45f, 0.16f, 48, 24), 2.2f, 0.7f, 0.95f, 0.6f, 0.25f},
        {sk_mesh_create_cube(0.8f, 0.8f, 0.8f), 3.6f, 0.4f, 0.35f, 0.85f, 0.5f},
    };
    (void)user_data;
    sk_asset_set_host(EXAMPLE_ASSET_BASE);

    g.camera = sk_camera3d_create(SK_CAMERA3D_PERSPECTIVE);
    g.scene = sk_scene_create();
    sk_scene_set_active_camera(g.scene, g.camera);
    sk_scene_set_ambient(g.scene, sk_color_rgba(90, 110, 160, 255), 0.12f);

    sk_handle_t sun = sk_light_create(SK_LIGHT_DIRECTIONAL);
    sk_light_set_direction(sun, -0.4f, -1.0f, -0.5f);
    sk_light_set_color(sun, sk_color_rgba(255, 215, 170, 255));
    sk_light_set_intensity(sun, 2.2f);
    sk_scene_add(g.scene, sun, 0);

    g.lamp = sk_light_create(SK_LIGHT_POINT);
    sk_light_set_color(g.lamp, sk_color_rgba(80, 220, 255, 255));
    sk_light_set_intensity(g.lamp, 18.0f);
    sk_light_set_range(g.lamp, 6.0f);
    sk_scene_add(g.scene, g.lamp, 0);
    g.lamp_marker = sk_shape3d_create();
    sk_shape3d_set_sphere(g.lamp_marker, 0.1f);
    sk_shape3d_set_color(g.lamp_marker, sk_color_rgba(80, 220, 255, 255));
    sk_scene_add(g.scene, g.lamp_marker, 0);

    sk_handle_t plane = sk_mesh_create_plane(16.0f, 16.0f, 0);
    sk_handle_t floor = sk_model_create(plane);
    sk_mesh_release(plane);
    sk_handle_t ground = sk_material_create(SK_MATERIAL_PBR);
    sk_material_set_vec4(ground, "base_color", 0.07f, 0.07f, 0.08f, 1.0f);
    sk_material_set_float(ground, "roughness", 0.85f);
    sk_model_set_material(floor, -1, ground);
    sk_material_release(ground);
    sk_scene_add(g.scene, floor, 0);

    for (int i = 0; i < SHAPE_COUNT; i++) {
        sk_handle_t material = sk_material_create(SK_MATERIAL_PBR);
        g.shapes[i] = sk_model_create(shapes[i].mesh);
        sk_mesh_release(shapes[i].mesh);
        sk_model_set_transform(g.shapes[i], shapes[i].x, shapes[i].y, -0.6f, 0, 0, 0, 1, 1, 1);
        sk_material_set_vec4(material, "base_color", shapes[i].r, shapes[i].gr, shapes[i].b, 1.0f);
        sk_material_set_float(material, "roughness", 0.4f);
        sk_model_set_material(g.shapes[i], 0, material);
        sk_material_release(material);
        sk_scene_add(g.scene, g.shapes[i], 0);
    }

    g.gumshoe = sk_model_create(0);
    sk_model_set_transform(g.gumshoe, 0, 0, 0, 0, 0, 0, 1, 1, 1);
    sk_scene_add(g.scene, g.gumshoe, 0);

    load(GUMSHOE_PATH, on_model_loaded);
    load(VIGNETTE_PATH, on_vignette_loaded);
    load(SCANLINES_PATH, on_scanlines_loaded);
    sk_debug_enable_fps(12, 10, 16);
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    char line[96];
    (void)tick_fraction;
    (void)user_data;

    if (sk_input_get_key(SK_KEY_ESCAPE) == SK_BUTTON_PRESSED) sk_request_quit();
    if (sk_input_get_key(SK_KEY_1) == SK_BUTTON_PRESSED) {
        g.vignette_on = !g.vignette_on;
        rebuild_effects();
    }
    if (sk_input_get_key(SK_KEY_2) == SK_BUTTON_PRESSED) {
        g.scanlines_on = !g.scanlines_on;
        rebuild_effects();
    }
    if (sk_input_get_key(SK_KEY_SPACE) == SK_BUTTON_PRESSED) g.breathing = !g.breathing;
    if (sk_input_get_key(SK_KEY_O) == SK_BUTTON_PRESSED) g.orbit = !g.orbit;
    if (sk_input_get_key(SK_KEY_UP) != SK_BUTTON_UP) g.strength = fminf(g.strength + dt, 1.0f);
    if (sk_input_get_key(SK_KEY_DOWN) != SK_BUTTON_UP) g.strength = fmaxf(g.strength - dt, 0.0f);

    g.time += dt;
    sk_model_animate(g.gumshoe, dt);
    if (g.orbit) g.angle += dt * 0.25f;
    sk_camera3d_set_view(g.camera, 9.0f * sinf(g.angle), 3.2f, 9.0f * cosf(g.angle), 0, 1.0f, 0, 0, 1, 0);
    const float lamp_x = 3.0f * sinf(g.time * 0.9f), lamp_z = 2.2f + 1.2f * cosf(g.time * 0.9f);
    sk_light_set_position(g.lamp, lamp_x, 1.4f, lamp_z);
    sk_shape3d_set_transform(g.lamp_marker, lamp_x, 1.4f, lamp_z, 0, 0, 0, 1, 1, 1);
    sk_model_set_transform(g.shapes[1], 2.2f, 0.7f, -0.6f, 0, g.time * 40.0f, g.time * 25.0f, 1, 1, 1);

    /* the effect's parameters are the material's: change them any frame */
    const float strength = g.breathing ? g.strength * (0.55f + 0.45f * sinf(g.time * 0.8f)) : g.strength;
    if (g.vignette != 0) sk_material_set_float(g.vignette, "strength", strength);

    sk_render_begin();
    sk_render_clear_background(sk_color_rgba(16, 18, 24, 255));
    sk_scene_draw(g.scene);
    sk_text_draw("libsk post-processing: screen effects over the finished frame", 12, 36, 20, SK_COLOR_RAYWHITE);
    snprintf(line, sizeof(line), "[1] vignette %s   [2] scanlines %s   effects: %d", g.vignette_on ? "on" : "off",
             g.scanlines_on ? "on" : "off", sk_render_effect_count());
    sk_text_draw(line, 12, 64, 16, SK_COLOR_LIGHTGRAY);
    snprintf(line, sizeof(line), "UP/DOWN strength %.2f   SPACE %s   O camera   ESC quit", (double)g.strength,
             g.breathing ? "stop breathing" : "breathe");
    sk_text_draw(line, 12, 86, 16, SK_COLOR_GRAY);
    sk_render_end();
}

int main(void)
{
    sk_init_values(1000, 600, "libsk postprocess", SK_WINDOW_FLAG_MSAA_4X_HINT | SK_WINDOW_FLAG_WINDOW_RESIZABLE);
    sk_set_init(init, NULL);
    sk_set_frame(frame, NULL);
    return sk_run();
}
