/* libwgrender materials example — glTF metallic-roughness materials created in code.
 *
 *   - top row: dielectric (metallic 0) spheres, roughness 0 to 1 left to right
 *   - middle row: metal (metallic 1) spheres, same roughness steps
 *   - bottom row: unlit, emissive, normal mapped (tangents generated at load),
 *     alpha blended, and the animated gumshoe with its body material replaced
 *     by gold on this model only
 * One sphere mesh backs every sphere; each model overrides the mesh's material.
 * The materials are assigned before the mesh finishes loading. A sun, an orbiting
 * point light and a little ambient light the scene.
 * Keys: 1 sun, 2 point light, ESC quit. */
#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "example_assets.h"
#include "wgr.h"

#define SPHERE_PATH "models/sphere/sphere.glb"
#define GUMSHOE_PATH "models/gumshoe/gumshoe.glb"
#define NORMAL_MAP_PATH "textures/tiles_normal.png"

enum { COLUMNS = 5, SPHERE_COUNT = 2 * COLUMNS + 4, GUMSHOE_BODY_SLOT = 1 };

static struct {
    wgr_handle_t scene;
    wgr_handle_t camera;
    wgr_color_t bg;
    wgr_handle_t spheres[SPHERE_COUNT];
    wgr_handle_t gumshoe;
    wgr_handle_t tiles; /* normal-mapped material, gets its texture when it loads */
    wgr_handle_t sun;
    wgr_handle_t lamp;
    wgr_handle_t lamp_marker;
    float time;
} g;

static void on_sphere_loaded(const char *path, void *user)
{
    wgr_handle_t mesh = wgr_mesh_create(path);
    (void)user;
    for (int i = 0; i < SPHERE_COUNT; i++) {
        wgr_model_set_mesh(g.spheres[i], mesh);
    }
    wgr_mesh_release(mesh); /* the models hold their own references */
}

static void on_gumshoe_loaded(const char *path, void *user)
{
    wgr_handle_t mesh = wgr_mesh_create(path);
    (void)user;
    wgr_model_set_mesh(g.gumshoe, mesh);
    wgr_mesh_release(mesh);
}

static void on_normal_map_loaded(const char *path, void *user)
{
    wgr_handle_t texture = wgr_texture_create(path);
    (void)user;
    wgr_material_set_texture(g.tiles, "normal_texture", texture);
    wgr_texture_release(texture); /* the material holds its own reference */
}

static void on_failed(const char *path, void *user)
{
    (void)user;
    wgr_logger_error("load failed: %s", path);
}

/* Place a sphere and give it `material`; the model keeps its own reference. */
static wgr_handle_t create_sphere(float x, float y, wgr_handle_t material)
{
    wgr_handle_t model = wgr_model_create(0); /* mesh attached when it loads */
    wgr_model_set_transform(model, x, y, 0, 0, 0, 0, 1, 1, 1);
    wgr_model_set_material(model, 0, material);
    wgr_material_release(material);
    wgr_scene_add(g.scene, model, 0);
    return model;
}

static wgr_handle_t create_pbr(float r, float gr, float b, float metallic, float roughness)
{
    wgr_handle_t material = wgr_material_create(WGR_MATERIAL_PBR);
    wgr_material_set_vec4(material, "base_color", r, gr, b, 1.0f); /* linear */
    wgr_material_set_float(material, "metallic", metallic);
    wgr_material_set_float(material, "roughness", roughness);
    return material;
}

static void init(void *user_data)
{
    const float spacing = 1.35f;
    wgr_handle_t material;
    int n = 0;

    (void)user_data;
    wgr_asset_set_host(EXAMPLE_ASSET_BASE);
    g.bg = wgr_color_rgba(20, 22, 28, 255);

    g.camera = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE);
    wgr_camera3d_set_view(g.camera, 0, 1.6f, 7.5f, 0, 1.2f, 0, 0, 1, 0);
    g.scene = wgr_scene_create();
    wgr_scene_set_active_camera(g.scene, g.camera);
    wgr_scene_set_ambient(g.scene, WGR_COLOR_WHITE, 0.12f);

    g.sun = wgr_light_create(WGR_LIGHT_DIRECTIONAL);
    wgr_light_set_direction(g.sun, -0.4f, -0.7f, -0.6f);
    wgr_light_set_color(g.sun, wgr_color_rgba(255, 244, 228, 255));
    wgr_light_set_intensity(g.sun, 3.0f);
    wgr_scene_add(g.scene, g.sun, 0);

    g.lamp = wgr_light_create(WGR_LIGHT_POINT);
    wgr_light_set_color(g.lamp, wgr_color_rgba(120, 190, 255, 255));
    wgr_light_set_intensity(g.lamp, 8.0f);
    wgr_light_set_range(g.lamp, 10.0f);
    wgr_scene_add(g.scene, g.lamp, 0);
    g.lamp_marker = wgr_shape3d_create(); /* shapes are unlit, so it shows the light's color */
    wgr_shape3d_set_sphere(g.lamp_marker, 0.06f);
    wgr_shape3d_set_color(g.lamp_marker, WGR_COLOR_SKYBLUE);
    wgr_scene_add(g.scene, g.lamp_marker, 0);

    /* rows of roughness steps: red plastic, then gold */
    for (int c = 0; c < COLUMNS; c++) {
        const float x = ((float)c - (COLUMNS - 1) * 0.5f) * spacing;
        const float roughness = (float)c / (COLUMNS - 1);
        g.spheres[n++] = create_sphere(x, 2.7f, create_pbr(0.8f, 0.05f, 0.04f, 0.0f, roughness));
        g.spheres[n++] = create_sphere(x, 1.35f, create_pbr(1.0f, 0.77f, 0.34f, 1.0f, roughness));
    }

    /* unlit: ignores the lights */
    material = wgr_material_create(WGR_MATERIAL_UNLIT);
    wgr_material_set_color(material, "base_color", WGR_COLOR_SKYBLUE);
    g.spheres[n++] = create_sphere(-2 * spacing, 0.0f, material);

    /* emissive: glows regardless of lighting */
    material = create_pbr(0.05f, 0.05f, 0.05f, 0.0f, 0.6f);
    wgr_material_set_vec3(material, "emissive", 1.0f, 0.35f, 0.05f);
    g.spheres[n++] = create_sphere(-spacing, 0.0f, material);

    /* normal mapped: bevelled tiles */
    g.tiles = create_pbr(0.6f, 0.6f, 0.62f, 0.0f, 0.45f);
    wgr_material_set_float(g.tiles, "normal_scale", 1.0f);
    g.spheres[n++] = create_sphere(0.0f, 0.0f, g.tiles); /* releases our reference; the model keeps one */

    /* alpha blended glass */
    material = create_pbr(0.3f, 0.9f, 0.5f, 0.0f, 0.1f);
    wgr_material_set_vec4(material, "base_color", 0.3f, 0.9f, 0.5f, 0.35f);
    wgr_material_set_alpha_mode(material, WGR_ALPHA_BLEND, 0.5f);
    g.spheres[n++] = create_sphere(spacing, 0.0f, material);

    /* gumshoe: slot 1 is its body ("gumshoe" material); slot 0, the blob shadow, is kept */
    g.gumshoe = wgr_model_create(0);
    wgr_model_set_transform(g.gumshoe, 2 * spacing, -0.55f, 0, 0, -0.6f, 0, 0.3f, 0.3f, 0.3f);
    wgr_model_set_animation(g.gumshoe, 3);
    material = create_pbr(1.0f, 0.77f, 0.34f, 1.0f, 0.3f);
    wgr_model_set_material(g.gumshoe, GUMSHOE_BODY_SLOT, material);
    wgr_material_release(material);
    wgr_scene_add(g.scene, g.gumshoe, 0);

    wgr_asset_add_task(wgr_asset_ensure_async(SPHERE_PATH, NULL, WGR_ASSET_NONE), on_sphere_loaded, on_failed, NULL);
    wgr_asset_add_task(wgr_asset_ensure_async(GUMSHOE_PATH, NULL, WGR_ASSET_NONE), on_gumshoe_loaded, on_failed, NULL);
    wgr_asset_add_task(wgr_asset_ensure_async(NORMAL_MAP_PATH, NULL, WGR_ASSET_NONE), on_normal_map_loaded, on_failed,
                      NULL);
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    const wgr_keyboard_state_t kb = wgr_input_get_keyboard_state();
    char line[128];
    float lx, ly, lz;

    (void)tick_fraction;
    (void)user_data;
    if (kb.keys[WGR_KEY_ESCAPE] == WGR_BUTTON_PRESSED) {
        wgr_request_quit();
    }
    if (kb.keys[WGR_KEY_1] == WGR_BUTTON_PRESSED) {
        wgr_light_set_enabled(g.sun, !wgr_light_is_enabled(g.sun));
    }
    if (kb.keys[WGR_KEY_2] == WGR_BUTTON_PRESSED) {
        wgr_light_set_enabled(g.lamp, !wgr_light_is_enabled(g.lamp));
    }

    g.time += dt;
    lx = cosf(g.time * 0.7f) * 4.0f;
    ly = 1.4f + sinf(g.time * 0.9f) * 1.2f;
    lz = sinf(g.time * 0.7f) * 1.5f + 2.0f;
    wgr_light_set_position(g.lamp, lx, ly, lz);
    wgr_shape3d_set_transform(g.lamp_marker, lx, ly, lz, 0, 0, 0, 1, 1, 1);
    wgr_shape3d_set_visible(g.lamp_marker, wgr_light_is_enabled(g.lamp));
    for (int i = 2 * COLUMNS; i < SPHERE_COUNT; i++) { /* turn the bottom row so the normal map moves */
        const float x = ((float)(i - 2 * COLUMNS) - 2.0f) * 1.35f;
        wgr_model_set_transform(g.spheres[i], x, 0.0f, 0, 0, g.time * 0.5f, 0, 1, 1, 1);
    }
    wgr_model_animate(g.gumshoe, dt);

    wgr_render_begin_frame();
    wgr_render_clear_background(g.bg);
    wgr_scene_draw(g.scene);

    wgr_text_draw("libwgrender materials: metallic-roughness, unlit, emissive, normal map, blend", 12, 12, 16,
                 WGR_COLOR_RAYWHITE);
    snprintf(line, sizeof(line), "roughness 0 -> 1 (left to right)   rows: plastic, gold   [1] sun %s  [2] lamp %s",
             wgr_light_is_enabled(g.sun) ? "on" : "off", wgr_light_is_enabled(g.lamp) ? "on" : "off");
    wgr_text_draw(line, 12, 36, 16, WGR_COLOR_LIGHTGRAY);
    wgr_render_end_frame();
}

int main(void)
{
    wgr_init_values(1000, 700, "libwgrender materials", WGR_WINDOW_FLAG_MSAA_4X_HINT | WGR_WINDOW_FLAG_WINDOW_RESIZABLE);
    wgr_set_init(init, NULL);
    wgr_set_frame(frame, NULL);
    return wgr_run();
}
