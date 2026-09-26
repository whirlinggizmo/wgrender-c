/* libwgrender generated meshes example — shapes made in code (wgr_mesh_create_plane, _cube,
 * _sphere, _cylinder, _cone, _capsule, _torus).
 *
 * The seven shapes in a row on a generated floor, each with its own color and the same
 * normal-mapped tile material, so their texture coordinates and tangents show: the
 * tiles should sit flat on every surface and catch the light the same way. The camera
 * turns around them (O stops it). Keys: O camera, ESC quit. */
#include <math.h>
#include <stddef.h>

#include "example_assets.h"
#include "wgr.h"

#define NORMAL_MAP_PATH "textures/tiles_normal.png"

enum { SHAPE_COUNT = 7 };

static struct {
    wgr_handle_t scene, camera;
    wgr_handle_t shapes[SHAPE_COUNT];
    wgr_handle_t materials[SHAPE_COUNT]; /* the models hold references; these get the normal map */
    bool orbit;
    float angle;
} g = {.orbit = true};

static void on_normal_map_loaded(const char *path, void *user)
{
    wgr_handle_t texture = wgr_texture_create(path);
    (void)user;
    for (int i = 0; i < SHAPE_COUNT; i++) {
        wgr_material_set_texture(g.materials[i], "normal_texture", texture);
    }
    wgr_texture_release(texture); /* the materials hold their own references */
}

static void on_failed(const char *path, void *user)
{
    (void)user;
    wgr_logger_error("load failed: %s", path);
}

static void init(void *user_data)
{
    /* each shape, how high its center sits (so it rests on the floor), and its color */
    const struct {
        wgr_handle_t mesh;
        float y;
        float r, gr, b;
    } shapes[SHAPE_COUNT] = {
        {wgr_mesh_create_plane(1.0f, 1.0f, 4), 0.01f, 0.85f, 0.85f, 0.85f},
        {wgr_mesh_create_cube(0.9f, 0.9f, 0.9f), 0.45f, 0.9f, 0.3f, 0.2f},
        {wgr_mesh_create_sphere(0.5f, 24, 48), 0.5f, 0.2f, 0.55f, 0.9f},
        {wgr_mesh_create_cylinder(0.45f, 1.0f, 40), 0.5f, 0.3f, 0.8f, 0.35f},
        {wgr_mesh_create_cone(0.5f, 1.1f, 40), 0.55f, 0.95f, 0.75f, 0.2f},
        {wgr_mesh_create_capsule(0.35f, 1.2f, 16, 40), 0.6f, 0.7f, 0.35f, 0.85f},
        {wgr_mesh_create_torus(0.4f, 0.15f, 48, 24), 0.15f, 0.9f, 0.5f, 0.6f},
    };
    (void)user_data;
    wgr_asset_set_host(EXAMPLE_ASSET_BASE);
    wgr_asset_set_manifest(EXAMPLE_ASSET_MANIFEST);

    g.camera = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE);
    g.scene = wgr_scene_create();
    wgr_scene_set_active_camera(g.scene, g.camera);
    wgr_scene_set_ambient(g.scene, WGR_COLOR_WHITE, 0.25f);
    wgr_handle_t sun = wgr_light_create(WGR_LIGHT_DIRECTIONAL);
    wgr_light_set_direction(sun, -0.5f, -1.0f, -0.4f);
    wgr_light_set_intensity(sun, 3.0f);
    wgr_scene_add(g.scene, sun, 0);

    wgr_handle_t plane = wgr_mesh_create_plane(12.0f, 12.0f, 0);
    wgr_handle_t floor = wgr_model_create(plane);
    wgr_mesh_release(plane); /* the model holds its own reference */
    wgr_handle_t ground = wgr_material_create(WGR_MATERIAL_PBR);
    wgr_material_set_vec4(ground, "base_color", 0.06f, 0.06f, 0.07f, 1.0f);
    wgr_material_set_float(ground, "metallic", 0.0f);
    wgr_material_set_float(ground, "roughness", 0.9f);
    wgr_model_set_material(floor, -1, ground);
    wgr_material_release(ground);
    wgr_scene_add(g.scene, floor, 0);

    for (int i = 0; i < SHAPE_COUNT; i++) {
        const float x = ((float)i - (SHAPE_COUNT - 1) * 0.5f) * 1.4f;
        g.shapes[i] = wgr_model_create(shapes[i].mesh);
        wgr_mesh_release(shapes[i].mesh);
        wgr_model_set_transform(g.shapes[i], x, shapes[i].y, 0, 0, 0, 0, 1, 1, 1);
        g.materials[i] = wgr_material_create(WGR_MATERIAL_PBR);
        wgr_material_set_vec4(g.materials[i], "base_color", shapes[i].r, shapes[i].gr, shapes[i].b, 1.0f);
        wgr_material_set_float(g.materials[i], "metallic", 0.0f);
        wgr_material_set_float(g.materials[i], "roughness", 0.45f);
        wgr_material_set_vec2(g.materials[i], "normal_texture_scale", 2.0f, 2.0f); /* tiles repeat */
        wgr_model_set_material(g.shapes[i], 0, g.materials[i]);
        wgr_material_release(g.materials[i]); /* the model keeps it alive */
        wgr_scene_add(g.scene, g.shapes[i], 0);
    }
    wgr_asset_add_task(wgr_asset_ensure_async(NORMAL_MAP_PATH, NULL, WGR_ASSET_NONE), on_normal_map_loaded, on_failed,
                      NULL);
    wgr_debug_enable_fps(12, 10, 16);
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    (void)tick_fraction;
    (void)user_data;
    if (wgr_input_get_key(WGR_KEY_ESCAPE) == WGR_BUTTON_PRESSED) wgr_request_quit();
    if (wgr_input_get_key(WGR_KEY_O) == WGR_BUTTON_PRESSED) g.orbit = !g.orbit;
    if (g.orbit) g.angle += dt * 0.2f;
    wgr_camera3d_set_view(g.camera, 9.0f * sinf(g.angle), 3.5f, 9.0f * cosf(g.angle), 0, 0.4f, 0, 0, 1, 0);

    wgr_render_begin_frame();
    wgr_render_clear_background(wgr_color_rgba(20, 22, 28, 255));
    wgr_scene_draw(g.scene);
    wgr_text_draw("libwgrender generated meshes: plane, cube, sphere, cylinder, cone, capsule, torus", 12, 36, 20,
                 WGR_COLOR_RAYWHITE);
    wgr_text_draw(g.orbit ? "O: stop the camera   ESC: quit" : "O: turn the camera   ESC: quit", 12, 64, 16,
                 WGR_COLOR_LIGHTGRAY);
    wgr_render_end_frame();
}

int main(void)
{
    wgr_init_values(1000, 600, "libwgrender meshes", WGR_WINDOW_FLAG_MSAA_4X_HINT | WGR_WINDOW_FLAG_WINDOW_RESIZABLE);
    wgr_set_init(init, NULL);
    wgr_set_frame(frame, NULL);
    return wgr_run();
}
