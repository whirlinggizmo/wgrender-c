/* libwgrender post-processing example — screen effects over the finished frame.
 *
 * The frame is an ordinary lit scene (an animated woman on a floor, generated shapes,
 * a circling point light). The effects are custom materials whose shaders are screen
 * effects (examples/shaders/vignette.glsl and scanlines.glsl, compiled by
 * tools/shaderpack.py; tools/gen_shaders.py --examples):
 *
 *   - the vignette darkens the corners and warms the middle
 *   - the scanlines darken alternating rows, shift red and blue apart and flicker
 *
 * Both apply in the order they were added, so turning one off and on rebuilds the chain
 * (wgr_render_clear_effects, then wgr_render_add_effect again). Their parameters are
 * ordinary material parameters, so they can change any frame — here the vignette
 * breathes in and out, and the arrow keys change how dark it gets.
 *
 * Keys: 1 vignette, 2 scanlines, UP/DOWN vignette strength, SPACE stop the breathing,
 * O stop the camera, ESC quit. */
#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "example_assets.h"
#include "wgr.h"

#define WOMAN_CASUAL_PATH "models/woman_casual/woman_casual.glb"
#define VIGNETTE_PATH "shaders/vignette.wgrshader"
#define SCANLINES_PATH "shaders/scanlines.wgrshader"

enum { SHAPE_COUNT = 3 };

static struct {
    wgr_handle_t scene, camera, woman_casual, lamp, lamp_marker;
    wgr_handle_t shapes[SHAPE_COUNT];
    wgr_handle_t vignette, scanlines; /* the effect materials (0 until they load) */
    bool vignette_on, scanlines_on, breathing, orbit;
    float strength; /* the vignette's, before breathing */
    float angle, time;
} g = {.vignette_on = true, .scanlines_on = false, .breathing = true, .orbit = true, .strength = 0.85f};

/* The chain, in order: the vignette darkens the corners, then the scanlines go over
 * everything. Rebuilt whenever one is switched on or off. */
static void rebuild_effects(void)
{
    wgr_render_clear_effects();
    if (g.vignette_on && g.vignette != 0) wgr_render_add_effect(g.vignette);
    if (g.scanlines_on && g.scanlines != 0) wgr_render_add_effect(g.scanlines);
}

static void on_model_loaded(const char *path, void *user)
{
    wgr_handle_t mesh = wgr_mesh_create(path);
    (void)user;
    wgr_model_set_mesh(g.woman_casual, mesh);
    wgr_mesh_release(mesh); /* the model holds its own reference */
    wgr_model_set_animation(g.woman_casual, 3);
    wgr_model_set_animation_loop(g.woman_casual, true);
}

static void on_vignette_loaded(const char *path, void *user)
{
    wgr_handle_t shader = wgr_shader_create(path);
    (void)user;
    g.vignette = wgr_material_create_custom(shader);
    wgr_shader_release(shader); /* the material holds its own reference */
    wgr_material_set_float(g.vignette, "strength", g.strength);
    wgr_material_set_float(g.vignette, "radius", 0.25f);
    wgr_material_set_vec4(g.vignette, "tint", 1.04f, 1.0f, 0.94f, 1.0f);
    rebuild_effects();
}

static void on_scanlines_loaded(const char *path, void *user)
{
    wgr_handle_t shader = wgr_shader_create(path);
    (void)user;
    g.scanlines = wgr_material_create_custom(shader);
    wgr_shader_release(shader);
    wgr_material_set_float(g.scanlines, "lines", 220.0f);
    wgr_material_set_float(g.scanlines, "darkness", 0.35f);
    wgr_material_set_float(g.scanlines, "offset", 1.5f);
    wgr_material_set_float(g.scanlines, "flicker", 1.0f);
    rebuild_effects();
}

static void on_failed(const char *path, void *user)
{
    (void)user;
    wgr_logger_error("load failed: %s", path);
}

static void load(const char *path, wgr_asset_callback_fn done)
{
    wgr_asset_add_task(wgr_asset_ensure_async(path, NULL, WGR_ASSET_NONE), done, on_failed, NULL);
}

static void init(void *user_data)
{
    /* the shapes beside the woman, and their colors */
    const struct {
        wgr_handle_t mesh;
        float x, y;
        float r, gr, b;
    } shapes[SHAPE_COUNT] = {
        {wgr_mesh_create_sphere(0.5f, 24, 48), -2.2f, 0.5f, 0.2f, 0.55f, 0.9f},
        {wgr_mesh_create_torus(0.45f, 0.16f, 48, 24), 2.2f, 0.7f, 0.95f, 0.6f, 0.25f},
        {wgr_mesh_create_cube(0.8f, 0.8f, 0.8f), 3.6f, 0.4f, 0.35f, 0.85f, 0.5f},
    };
    (void)user_data;
    wgr_asset_set_host(EXAMPLE_ASSET_BASE);

    g.camera = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE);
    g.scene = wgr_scene_create();
    wgr_scene_set_active_camera(g.scene, g.camera);
    wgr_scene_set_ambient(g.scene, wgr_color_rgba(90, 110, 160, 255), 0.12f);

    wgr_handle_t sun = wgr_light_create(WGR_LIGHT_DIRECTIONAL);
    wgr_light_set_direction(sun, -0.4f, -1.0f, -0.5f);
    wgr_light_set_color(sun, wgr_color_rgba(255, 215, 170, 255));
    wgr_light_set_intensity(sun, 2.2f);
    wgr_scene_add(g.scene, sun, 0);

    g.lamp = wgr_light_create(WGR_LIGHT_POINT);
    wgr_light_set_color(g.lamp, wgr_color_rgba(80, 220, 255, 255));
    wgr_light_set_intensity(g.lamp, 18.0f);
    wgr_light_set_range(g.lamp, 6.0f);
    wgr_scene_add(g.scene, g.lamp, 0);
    g.lamp_marker = wgr_shape3d_create();
    wgr_shape3d_set_sphere(g.lamp_marker, 0.1f);
    wgr_shape3d_set_color(g.lamp_marker, wgr_color_rgba(80, 220, 255, 255));
    wgr_scene_add(g.scene, g.lamp_marker, 0);

    wgr_handle_t plane = wgr_mesh_create_plane(16.0f, 16.0f, 0);
    wgr_handle_t floor = wgr_model_create(plane);
    wgr_mesh_release(plane);
    wgr_handle_t ground = wgr_material_create(WGR_MATERIAL_PBR);
    wgr_material_set_vec4(ground, "base_color", 0.07f, 0.07f, 0.08f, 1.0f);
    wgr_material_set_float(ground, "roughness", 0.85f);
    wgr_model_set_material(floor, -1, ground);
    wgr_material_release(ground);
    wgr_scene_add(g.scene, floor, 0);

    for (int i = 0; i < SHAPE_COUNT; i++) {
        wgr_handle_t material = wgr_material_create(WGR_MATERIAL_PBR);
        g.shapes[i] = wgr_model_create(shapes[i].mesh);
        wgr_mesh_release(shapes[i].mesh);
        wgr_model_set_transform(g.shapes[i], shapes[i].x, shapes[i].y, -0.6f, 0, 0, 0, 1, 1, 1);
        wgr_material_set_vec4(material, "base_color", shapes[i].r, shapes[i].gr, shapes[i].b, 1.0f);
        wgr_material_set_float(material, "roughness", 0.4f);
        wgr_model_set_material(g.shapes[i], 0, material);
        wgr_material_release(material);
        wgr_scene_add(g.scene, g.shapes[i], 0);
    }

    g.woman_casual = wgr_model_create(0);
    wgr_model_set_transform(g.woman_casual, 0, 0, 0, 0, 0, 0, 1, 1, 1);
    wgr_scene_add(g.scene, g.woman_casual, 0);

    load(WOMAN_CASUAL_PATH, on_model_loaded);
    load(VIGNETTE_PATH, on_vignette_loaded);
    load(SCANLINES_PATH, on_scanlines_loaded);
    wgr_debug_enable_fps(12, 10, 16);
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    char line[96];
    (void)tick_fraction;
    (void)user_data;

    if (wgr_input_get_key(WGR_KEY_ESCAPE) == WGR_BUTTON_PRESSED) wgr_request_quit();
    if (wgr_input_get_key(WGR_KEY_1) == WGR_BUTTON_PRESSED) {
        g.vignette_on = !g.vignette_on;
        rebuild_effects();
    }
    if (wgr_input_get_key(WGR_KEY_2) == WGR_BUTTON_PRESSED) {
        g.scanlines_on = !g.scanlines_on;
        rebuild_effects();
    }
    if (wgr_input_get_key(WGR_KEY_SPACE) == WGR_BUTTON_PRESSED) g.breathing = !g.breathing;
    if (wgr_input_get_key(WGR_KEY_O) == WGR_BUTTON_PRESSED) g.orbit = !g.orbit;
    if (wgr_input_get_key(WGR_KEY_UP) != WGR_BUTTON_UP) g.strength = fminf(g.strength + dt, 1.0f);
    if (wgr_input_get_key(WGR_KEY_DOWN) != WGR_BUTTON_UP) g.strength = fmaxf(g.strength - dt, 0.0f);

    g.time += dt;
    wgr_model_animate(g.woman_casual, dt);
    if (g.orbit) g.angle += dt * 0.25f;
    wgr_camera3d_set_view(g.camera, 9.0f * sinf(g.angle), 3.2f, 9.0f * cosf(g.angle), 0, 1.0f, 0, 0, 1, 0);
    const float lamp_x = 3.0f * sinf(g.time * 0.9f), lamp_z = 2.2f + 1.2f * cosf(g.time * 0.9f);
    wgr_light_set_position(g.lamp, lamp_x, 1.4f, lamp_z);
    wgr_shape3d_set_transform(g.lamp_marker, lamp_x, 1.4f, lamp_z, 0, 0, 0, 1, 1, 1);
    wgr_model_set_transform(g.shapes[1], 2.2f, 0.7f, -0.6f, 0, g.time * 40.0f, g.time * 25.0f, 1, 1, 1);

    /* the effect's parameters are the material's: change them any frame */
    const float strength = g.breathing ? g.strength * (0.55f + 0.45f * sinf(g.time * 0.8f)) : g.strength;
    if (g.vignette != 0) wgr_material_set_float(g.vignette, "strength", strength);

    wgr_render_begin_frame();
    wgr_render_clear_background(wgr_color_rgba(16, 18, 24, 255));
    wgr_scene_draw(g.scene);
    wgr_text_draw("libwgrender post-processing: screen effects over the finished frame", 12, 36, 20, WGR_COLOR_RAYWHITE);
    snprintf(line, sizeof(line), "[1] vignette %s   [2] scanlines %s   effects: %d", g.vignette_on ? "on" : "off",
             g.scanlines_on ? "on" : "off", wgr_render_effect_count());
    wgr_text_draw(line, 12, 64, 16, WGR_COLOR_LIGHTGRAY);
    snprintf(line, sizeof(line), "UP/DOWN strength %.2f   SPACE %s   O camera   ESC quit", (double)g.strength,
             g.breathing ? "stop breathing" : "breathe");
    wgr_text_draw(line, 12, 86, 16, WGR_COLOR_GRAY);
    wgr_render_end_frame();
}

int main(void)
{
    wgr_init_values(1000, 600, "libwgrender postprocess", WGR_WINDOW_FLAG_MSAA_4X_HINT | WGR_WINDOW_FLAG_WINDOW_RESIZABLE);
    wgr_set_init(init, NULL);
    wgr_set_frame(frame, NULL);
    return wgr_run();
}
