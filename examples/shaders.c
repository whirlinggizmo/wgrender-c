/* libwgrender custom shaders example — materials drawn by shaders of your own.
 *
 *   - left: toon shading (lights in flat bands, a rim light) on the animated
 *     character: custom shaders work on skinned models too
 *   - middle: a sphere dissolving and coming back through a noise texture, with a
 *     glowing edge (time, a texture, discard)
 *   - right: a sphere of water rippling in waves (a vertex hook moves the surface)
 *     reflecting the scene's environment
 *   - a logo sprite, in the world and in the screen's corner: one material outlines
 *     it and pulses a flash (its shader reads the sprite's own texture)
 * The shaders are the .glsl files in examples/shaders, compiled for every backend by
 * tools/shaderpack.py into .wgrshader files in examples/assets/shaders (tools/gen_shaders.py --examples).
 * They load through wgr_asset like any other file. A sun, a point light circling in front and
 * an environment (a sunset, not shown as the background) light the scene. Keys: 1 sun, 2 point light, ESC quit. */
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "example_assets.h"
#include "wgr.h"

#define NOISE_PATH "textures/noise.png"
#define LOGO_PATH "sprites/logo/wg-logo-white-alpha.png"
#define ENVIRONMENT_PATH "environments/venice_sunset_1k.hdr"
#define FLOOR_Y -0.3f
#define SPHERE_Y (FLOOR_Y + 0.5f) /* spheres 1 m across, resting on the floor */

enum { SHADER_TOON, SHADER_DISSOLVE, SHADER_WAVE, SHADER_SPRITE_FX, SHADER_COUNT, CHARACTER_BODY_SLOT = 1 };
static const char *SHADER_PATHS[SHADER_COUNT] = {
    "shaders/toon.wgrshader",
    "shaders/dissolve.wgrshader",
    "shaders/wave.wgrshader",
    "shaders/sprite_fx.wgrshader",
};

static struct {
    wgr_handle_t scene;
    wgr_handle_t camera;
    wgr_color_t bg;
    wgr_handle_t character, dissolving, rippling; /* models */
    wgr_handle_t floor;
    wgr_handle_t dissolve;                      /* its material gets the noise texture */
    wgr_handle_t sun, lamp, lamp_marker;
    wgr_handle_t logo3d, logo2d; /* sprites, drawn by the sprite effects shader */
    float time;
} g;

static void on_failed(const char *path, void *user)
{
    (void)user;
    wgr_logger_error("load failed: %s", path);
}

static void on_character_loaded(const char *path, void *user)
{
    wgr_handle_t mesh = wgr_mesh_create(path);
    (void)user;
    wgr_model_set_mesh(g.character, mesh);
    wgr_mesh_release(mesh);
}

static void on_environment_loaded(const char *path, void *user)
{
    wgr_handle_t environment = wgr_environment_create(path);
    (void)user;
    wgr_scene_set_environment(g.scene, environment, 1.0f, 0.0f); /* lighting only: the background stays dark */
    wgr_environment_release(environment); /* the scene holds its own reference */
}

static void on_logo_loaded(const char *path, void *user)
{
    wgr_handle_t texture = wgr_texture_create(path);
    (void)user;
    wgr_sprite3d_set_texture(g.logo3d, texture);
    wgr_sprite2d_set_texture(g.logo2d, texture);
    wgr_texture_release(texture); /* the sprites hold their own references */
}

static void on_noise_loaded(const char *path, void *user)
{
    wgr_handle_t texture = wgr_texture_create(path);
    (void)user;
    if (g.dissolve != 0) wgr_material_set_texture(g.dissolve, "noise_tex", texture);
    wgr_texture_release(texture); /* the material holds its own reference */
}

/* A shader is loaded: make its material and give it to its model. */
static void on_shader_loaded(const char *path, void *user)
{
    const int which = (int)(intptr_t)user;
    wgr_handle_t shader = wgr_shader_create(path);
    wgr_handle_t material = wgr_material_create_custom(shader);
    wgr_shader_release(shader); /* the material holds its own reference */
    if (material == 0) return;

    switch (which) {
        case SHADER_TOON:
            wgr_material_set_color(material, "color", wgr_color_rgba(255, 196, 120, 255));
            wgr_material_set_float(material, "bands", 3.0f);
            wgr_material_set_float(material, "rim", 0.35f);
            wgr_model_set_material(g.character, CHARACTER_BODY_SLOT, material);
            break;
        case SHADER_DISSOLVE:
            wgr_material_set_vec4(material, "color", 0.55f, 0.6f, 0.7f, 1.0f); /* linear */
            wgr_material_set_vec3(material, "edge_color", 4.0f, 1.2f, 0.2f);
            wgr_material_set_float(material, "speed", 0.15f);
            wgr_material_set_double_sided(material, true); /* the inside shows through the holes */
            wgr_model_set_material(g.dissolving, 0, material);
            g.dissolve = material; /* the model holds a reference; this one is released below */
            wgr_asset_add_task(wgr_asset_ensure_async(NOISE_PATH, NULL, WGR_ASSET_NONE), on_noise_loaded, on_failed, NULL);
            break;
        case SHADER_WAVE:
            wgr_material_set_float(material, "amplitude", 0.03f);
            wgr_material_set_float(material, "frequency", 2.5f);
            wgr_material_set_float(material, "wave_speed", 3.0f);
            wgr_material_set_vec4(material, "low_color", 0.0f, 0.03f, 0.1f, 1.0f); /* deep water */
            wgr_material_set_vec4(material, "high_color", 0.05f, 0.3f, 0.35f, 1.0f);
            wgr_material_set_float(material, "roughness", 0.05f);
            wgr_material_set_float(material, "reflectivity", 0.35f); /* real water is 0.02: more, so it shows */
            wgr_model_set_material(g.rippling, 0, material);
            break;
        case SHADER_SPRITE_FX: /* one material, a 3D and a 2D sprite */
            wgr_material_set_vec4(material, "outline_color", 1.0f, 0.45f, 0.1f, 1.0f);
            wgr_material_set_float(material, "outline_width", 2.5f);
            wgr_material_set_float(material, "flash", 0.8f);
            wgr_material_set_float(material, "pulse_speed", 5.0f);
            wgr_sprite3d_set_material(g.logo3d, material);
            wgr_sprite2d_set_material(g.logo2d, material);
            break;
    }
    wgr_material_release(material); /* the models hold their own references */
}

static void init(void *user_data)
{
    (void)user_data;
    wgr_asset_set_host(EXAMPLE_ASSET_BASE);
    wgr_asset_set_manifest(EXAMPLE_ASSET_MANIFEST);
    g.bg = wgr_color_rgba(20, 22, 28, 255);

    g.camera = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE);
    wgr_camera3d_set_view(g.camera, 0, 1.2f, 5.0f, 0, 0.3f, 0, 0, 1, 0);
    g.scene = wgr_scene_create();
    wgr_scene_set_active_camera(g.scene, g.camera);
    wgr_scene_set_ambient(g.scene, WGR_COLOR_WHITE, 0.15f);

    g.sun = wgr_light_create(WGR_LIGHT_DIRECTIONAL);
    wgr_light_set_direction(g.sun, -0.4f, -0.7f, -0.6f);
    wgr_light_set_color(g.sun, wgr_color_rgba(255, 244, 228, 255));
    wgr_light_set_intensity(g.sun, 1.5f); /* soft: the point light and the environment show too */
    wgr_scene_add(g.scene, g.sun, 0);

    g.lamp = wgr_light_create(WGR_LIGHT_POINT);
    wgr_light_set_color(g.lamp, wgr_color_rgba(120, 190, 255, 255));
    wgr_light_set_intensity(g.lamp, 9.0f); /* falls off with distance squared: ~2.3 at 2 m */
    wgr_light_set_range(g.lamp, 8.0f);
    wgr_scene_add(g.scene, g.lamp, 0);
    g.lamp_marker = wgr_shape3d_create();
    wgr_shape3d_set_sphere(g.lamp_marker, 0.05f);
    wgr_shape3d_set_color(g.lamp_marker, WGR_COLOR_SKYBLUE);
    wgr_scene_add(g.scene, g.lamp_marker, 0);

    /* generated meshes (wgr_mesh_create_*): a floor, so the models stand somewhere, and
       the spheres; the water's finely divided, so its waves are smooth */
    wgr_handle_t plane = wgr_mesh_create_plane(6.0f, 6.0f, 0);
    wgr_handle_t sphere = wgr_mesh_create_sphere(0.5f, 32, 64);
    wgr_handle_t fine_sphere = wgr_mesh_create_sphere(0.5f, 96, 192);
    g.floor = wgr_model_create(plane);
    wgr_model_set_transform(g.floor, 0.0f, FLOOR_Y, 0.0f, 0, 0, 0, 1, 1, 1);
    wgr_handle_t ground = wgr_material_create(WGR_MATERIAL_PBR);
    wgr_material_set_vec4(ground, "base_color", 0.04f, 0.04f, 0.045f, 1.0f); /* dark: the lights show on it */
    wgr_material_set_float(ground, "metallic", 0.0f);
    wgr_material_set_float(ground, "roughness", 0.8f);
    wgr_model_set_material(g.floor, -1, ground);
    wgr_material_release(ground); /* the model holds its own reference */
    wgr_scene_add(g.scene, g.floor, 0);

    g.character = wgr_model_create(0); /* meshes attach when they load */
    wgr_model_set_transform(g.character, -1.9f, FLOOR_Y, 0, 0, 0.4f, 0, 0.5f, 0.5f, 0.5f); /* feet at its origin */
    wgr_model_set_animation(g.character, 3);
    wgr_scene_add(g.scene, g.character, 0);
    g.dissolving = wgr_model_create(sphere);
    wgr_model_set_transform(g.dissolving, 0.0f, SPHERE_Y, 0, 0, 0, 0, 1, 1, 1);
    wgr_scene_add(g.scene, g.dissolving, 0);
    g.rippling = wgr_model_create(fine_sphere);
    wgr_model_set_transform(g.rippling, 1.9f, SPHERE_Y, 0, 0, 0, 0, 1, 1, 1);
    wgr_scene_add(g.scene, g.rippling, 0);
    wgr_mesh_release(plane); /* the models hold their own references */
    wgr_mesh_release(sphere);
    wgr_mesh_release(fine_sphere);

    /* the logo, in the world above the middle and in the screen's corner */
    g.logo3d = wgr_sprite3d_create(0);
    wgr_sprite3d_set_transform(g.logo3d, 0.0f, 1.55f, -0.8f, 0, 0, 0, 1, 1, 1);
    wgr_sprite3d_set_size(g.logo3d, 0.9f);
    wgr_sprite3d_set_tint(g.logo3d, wgr_color_rgba(90, 190, 255, 255)); /* so the white flash shows */
    wgr_scene_add(g.scene, g.logo3d, 0);
    g.logo2d = wgr_sprite2d_create(0);
    wgr_sprite2d_set_size(g.logo2d, 96.0f, 96.0f);
    wgr_sprite2d_set_pivot(g.logo2d, 1.0f, 1.0f);
    wgr_sprite2d_set_tint(g.logo2d, wgr_color_rgba(90, 190, 255, 255));
    wgr_asset_add_task(wgr_asset_ensure_async(LOGO_PATH, NULL, WGR_ASSET_NONE), on_logo_loaded, on_failed, NULL);

    for (int i = 0; i < SHADER_COUNT; i++) {
        wgr_asset_add_task(wgr_asset_ensure_async(SHADER_PATHS[i], NULL, WGR_ASSET_NONE), on_shader_loaded, on_failed,
                          (void *)(intptr_t)i);
    }
    wgr_asset_add_task(wgr_asset_ensure_async(ENVIRONMENT_PATH, NULL, WGR_ASSET_NONE), on_environment_loaded, on_failed,
                      NULL);
    wgr_asset_add_task(wgr_asset_ensure_async(CHARACTER_PATH, NULL, WGR_ASSET_NONE), on_character_loaded, on_failed, NULL);
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    float lx, ly, lz;

    (void)tick_fraction;
    (void)user_data;
    if (wgr_input_get_key(WGR_KEY_ESCAPE) == WGR_BUTTON_PRESSED) {
        wgr_request_quit();
    }
    if (wgr_input_get_key(WGR_KEY_1) == WGR_BUTTON_PRESSED) {
        wgr_light_set_enabled(g.sun, !wgr_light_is_enabled(g.sun));
    }
    if (wgr_input_get_key(WGR_KEY_2) == WGR_BUTTON_PRESSED) {
        wgr_light_set_enabled(g.lamp, !wgr_light_is_enabled(g.lamp));
    }

    g.time += dt;
    /* circling in front of the models, facing the camera: always in view, and 1.8 m or
       more from them (closer, it would wash them out) */
    lx = cosf(g.time * 0.7f) * 2.2f;
    ly = 0.8f + sinf(g.time * 0.7f) * 1.0f;
    lz = 1.8f;
    wgr_light_set_position(g.lamp, lx, ly, lz);
    wgr_shape3d_set_transform(g.lamp_marker, lx, ly, lz, 0, 0, 0, 1, 1, 1);
    wgr_shape3d_set_visible(g.lamp_marker, wgr_light_is_enabled(g.lamp));
    wgr_model_set_transform(g.dissolving, 0.0f, SPHERE_Y, 0, 0, g.time * 0.4f, 0, 1, 1, 1);
    wgr_model_animate(g.character, dt);

    wgr_render_begin_frame();
    wgr_render_clear_background(g.bg);
    wgr_scene_draw(g.scene);
    const vec2_t screen = wgr_window_get_screen_size();
    wgr_sprite2d_set_position(g.logo2d, screen.x - 16.0f, screen.y - 16.0f); /* bottom right */
    wgr_sprite2d_draw(g.logo2d);
    wgr_text_draw("libwgrender custom shaders: toon, dissolve, water, sprite effects", 12, 12, 20, WGR_COLOR_RAYWHITE);
    wgr_text_draw("1 sun, 2 point light, ESC quit", 12, 40, 16, WGR_COLOR_LIGHTGRAY);
    wgr_render_end_frame();
}

int main(void)
{
    wgr_init_values(960, 540, "libwgrender shaders", WGR_WINDOW_FLAG_MSAA_4X_HINT | WGR_WINDOW_FLAG_WINDOW_RESIZABLE);
    wgr_set_init(init, NULL);
    wgr_set_frame(frame, NULL);
    return wgr_run();
}
