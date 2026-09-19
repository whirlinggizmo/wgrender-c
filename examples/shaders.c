/* libsk custom shaders example — materials drawn by shaders of your own.
 *
 *   - left: toon shading (lights in flat bands, a rim light) on the animated
 *     gumshoe: custom shaders work on skinned models too
 *   - middle: a sphere dissolving and coming back through a noise texture, with a
 *     glowing edge (time, a texture, discard)
 *   - right: a sphere rippling in waves: a vertex hook moves the surface
 * The shaders are the .glsl files in examples/shaders, compiled for every backend by
 * tools/shaderpack.py into .skshader files in examples/assets/shaders (make example-shaders).
 * They load through sk_asset like any other file. A sun and an orbiting point light
 * light the scene. Keys: 1 sun, 2 point light, ESC quit. */
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "example_assets.h"
#include "sk.h"

#define SPHERE_PATH "models/sphere/sphere.glb"
#define GUMSHOE_PATH "models/gumshoe/gumshoe.glb"
#define NOISE_PATH "textures/noise.png"

enum { SHADER_TOON, SHADER_DISSOLVE, SHADER_WAVE, SHADER_COUNT, GUMSHOE_BODY_SLOT = 1 };
static const char *SHADER_PATHS[SHADER_COUNT] = {
    "shaders/toon.skshader",
    "shaders/dissolve.skshader",
    "shaders/wave.skshader",
};

static struct {
    sk_handle_t scene;
    sk_handle_t camera;
    sk_color_t bg;
    sk_handle_t gumshoe, dissolving, rippling; /* models */
    sk_handle_t dissolve;                      /* its material gets the noise texture */
    sk_handle_t sun, lamp, lamp_marker;
    float time;
} g;

static void on_failed(const char *path, void *user)
{
    (void)user;
    sk_logger_error("load failed: %s", path);
}

static void on_sphere_loaded(const char *path, void *user)
{
    sk_handle_t mesh = sk_mesh_create(path);
    (void)user;
    sk_model_set_mesh(g.dissolving, mesh);
    sk_model_set_mesh(g.rippling, mesh);
    sk_mesh_release(mesh); /* the models hold their own references */
}

static void on_gumshoe_loaded(const char *path, void *user)
{
    sk_handle_t mesh = sk_mesh_create(path);
    (void)user;
    sk_model_set_mesh(g.gumshoe, mesh);
    sk_mesh_release(mesh);
}

static void on_noise_loaded(const char *path, void *user)
{
    sk_handle_t texture = sk_texture_create(path);
    (void)user;
    if (g.dissolve != 0) sk_material_set_texture(g.dissolve, "noise_tex", texture);
    sk_texture_release(texture); /* the material holds its own reference */
}

/* A shader is loaded: make its material and give it to its model. */
static void on_shader_loaded(const char *path, void *user)
{
    const int which = (int)(intptr_t)user;
    sk_handle_t shader = sk_shader_create(path);
    sk_handle_t material = sk_material_create_custom(shader);
    sk_shader_release(shader); /* the material holds its own reference */
    if (material == 0) return;

    switch (which) {
        case SHADER_TOON:
            sk_material_set_color(material, "color", sk_color_rgba(255, 196, 120, 255));
            sk_material_set_float(material, "bands", 3.0f);
            sk_material_set_float(material, "rim", 0.35f);
            sk_model_set_material(g.gumshoe, GUMSHOE_BODY_SLOT, material);
            break;
        case SHADER_DISSOLVE:
            sk_material_set_vec4(material, "color", 0.55f, 0.6f, 0.7f, 1.0f); /* linear */
            sk_material_set_vec3(material, "edge_color", 4.0f, 1.2f, 0.2f);
            sk_material_set_float(material, "speed", 0.15f);
            sk_material_set_double_sided(material, true); /* the inside shows through the holes */
            sk_model_set_material(g.dissolving, 0, material);
            g.dissolve = material; /* the model holds a reference; this one is released below */
            sk_asset_add_task(sk_asset_ensure_async(NOISE_PATH, NULL, SK_ASSET_NONE), on_noise_loaded, on_failed, NULL);
            break;
        case SHADER_WAVE:
            sk_material_set_float(material, "amplitude", 0.03f);
            sk_material_set_float(material, "frequency", 2.5f);
            sk_material_set_float(material, "wave_speed", 3.0f);
            sk_material_set_vec4(material, "low_color", 0.02f, 0.1f, 0.35f, 1.0f);
            sk_material_set_vec4(material, "high_color", 0.3f, 0.85f, 0.9f, 1.0f);
            sk_model_set_material(g.rippling, 0, material);
            break;
    }
    sk_material_release(material); /* the models hold their own references */
}

static void init(void *user_data)
{
    (void)user_data;
    sk_asset_set_host(EXAMPLE_ASSET_BASE);
    g.bg = sk_color_rgba(20, 22, 28, 255);

    g.camera = sk_camera3d_create(SK_CAMERA3D_PERSPECTIVE);
    sk_camera3d_set_view(g.camera, 0, 1.2f, 5.0f, 0, 0.6f, 0, 0, 1, 0);
    g.scene = sk_scene_create();
    sk_scene_set_active_camera(g.scene, g.camera);
    sk_scene_set_ambient(g.scene, SK_COLOR_WHITE, 0.15f);

    g.sun = sk_light_create(SK_LIGHT_DIRECTIONAL);
    sk_light_set_direction(g.sun, -0.4f, -0.7f, -0.6f);
    sk_light_set_color(g.sun, sk_color_rgba(255, 244, 228, 255));
    sk_light_set_intensity(g.sun, 3.0f);
    sk_scene_add(g.scene, g.sun, 0);

    g.lamp = sk_light_create(SK_LIGHT_POINT);
    sk_light_set_color(g.lamp, sk_color_rgba(120, 190, 255, 255));
    sk_light_set_intensity(g.lamp, 6.0f);
    sk_light_set_range(g.lamp, 8.0f);
    sk_scene_add(g.scene, g.lamp, 0);
    g.lamp_marker = sk_shape3d_create();
    sk_shape3d_set_sphere(g.lamp_marker, 0.05f);
    sk_shape3d_set_color(g.lamp_marker, SK_COLOR_SKYBLUE);
    sk_scene_add(g.scene, g.lamp_marker, 0);

    g.gumshoe = sk_model_create(0); /* meshes attach when they load */
    sk_model_set_transform(g.gumshoe, -1.9f, -0.3f, 0, 0, 0.4f, 0, 0.5f, 0.5f, 0.5f);
    sk_model_set_animation(g.gumshoe, 3);
    sk_scene_add(g.scene, g.gumshoe, 0);
    g.dissolving = sk_model_create(0);
    sk_model_set_transform(g.dissolving, 0.0f, 0.6f, 0, 0, 0, 0, 0.75f, 0.75f, 0.75f);
    sk_scene_add(g.scene, g.dissolving, 0);
    g.rippling = sk_model_create(0);
    sk_model_set_transform(g.rippling, 1.9f, 0.6f, 0, 0, 0, 0, 0.75f, 0.75f, 0.75f);
    sk_scene_add(g.scene, g.rippling, 0);

    for (int i = 0; i < SHADER_COUNT; i++) {
        sk_asset_add_task(sk_asset_ensure_async(SHADER_PATHS[i], NULL, SK_ASSET_NONE), on_shader_loaded, on_failed,
                          (void *)(intptr_t)i);
    }
    sk_asset_add_task(sk_asset_ensure_async(SPHERE_PATH, NULL, SK_ASSET_NONE), on_sphere_loaded, on_failed, NULL);
    sk_asset_add_task(sk_asset_ensure_async(GUMSHOE_PATH, NULL, SK_ASSET_NONE), on_gumshoe_loaded, on_failed, NULL);
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    float lx, ly, lz;

    (void)tick_fraction;
    (void)user_data;
    if (sk_input_get_key(SK_KEY_ESCAPE) == SK_BUTTON_PRESSED) {
        sk_request_quit();
    }
    if (sk_input_get_key(SK_KEY_1) == SK_BUTTON_PRESSED) {
        sk_light_set_enabled(g.sun, !sk_light_is_enabled(g.sun));
    }
    if (sk_input_get_key(SK_KEY_2) == SK_BUTTON_PRESSED) {
        sk_light_set_enabled(g.lamp, !sk_light_is_enabled(g.lamp));
    }

    g.time += dt;
    lx = cosf(g.time * 0.7f) * 3.0f;
    ly = 1.2f + sinf(g.time * 0.9f) * 0.8f;
    lz = sinf(g.time * 0.7f) * 1.0f + 1.8f;
    sk_light_set_position(g.lamp, lx, ly, lz);
    sk_shape3d_set_transform(g.lamp_marker, lx, ly, lz, 0, 0, 0, 1, 1, 1);
    sk_shape3d_set_visible(g.lamp_marker, sk_light_is_enabled(g.lamp));
    sk_model_set_transform(g.dissolving, 0.0f, 0.6f, 0, 0, g.time * 0.4f, 0, 0.75f, 0.75f, 0.75f);
    sk_model_animate(g.gumshoe, dt);

    sk_render_begin();
    sk_render_clear_background(g.bg);
    sk_scene_draw(g.scene);
    sk_text_draw("libsk custom shaders: toon, dissolve, waves", 12, 12, 20, SK_COLOR_RAYWHITE);
    sk_text_draw("1 sun, 2 point light, ESC quit", 12, 40, 16, SK_COLOR_LIGHTGRAY);
    sk_render_end();
}

int main(void)
{
    sk_init_values(960, 540, "libsk shaders", SK_WINDOW_FLAG_MSAA_4X_HINT | SK_WINDOW_FLAG_WINDOW_RESIZABLE);
    sk_set_init(init, NULL);
    sk_set_frame(frame, NULL);
    return sk_run();
}
