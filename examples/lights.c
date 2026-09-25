/* libwgrender lights example — directional, point and spot lights in a scene.
 *
 * Five animated models on a grid:
 *   - a dim warm sun (directional)
 *   - a cyan point light orbiting through them, falling off with its range (the
 *     small sphere marks it; shapes are unlit, so it shows the light's color)
 *   - a white spotlight sweeping across them from above
 * Behind them stand billboard sprites with a built-in material (wgr_sprite3d_set_material):
 * they take the same lights as the models, facing the camera. Each shows one cell of the
 * tilemap example's sprite sheet (wgr_sprite3d_set_source), and the sheet's normal map
 * gives it relief: the normal map is sampled through the same region.
 * Scenes start unlit (no lights, no ambient); everything here is explicit.
 * Keys: 1 sun, 2 point light, 3 spotlight, ESC quit. */
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>

#include "example_assets.h"
#include "wgr.h"

#define MODEL_PATH "models/woman_casual/woman_casual.glb"
#define SPRITE_PATH "textures/tiles.png"
#define NORMAL_PATH "textures/tiles_sheet_normal.png" /* tools/gen_tiles.py */

enum { MODEL_COUNT = 5, SPRITE_COUNT = 4 };

/* the sprites' cells in the sheet (pixels, from tools/gen_tiles.py) and their world
 * height; all 1.6 wide */
static const float SPRITE_CELLS[SPRITE_COUNT][5] = {
    {62, 2, 16, 16, 1.6f},  /* stone */
    {2, 22, 16, 32, 3.2f},  /* tree */
    {42, 22, 16, 16, 1.6f}, /* coin */
    {62, 22, 16, 16, 1.6f}, /* rock */
};

static struct {
    wgr_handle_t scene;
    wgr_handle_t camera;
    wgr_color_t bg;
    wgr_color_t grid;
    wgr_handle_t models[MODEL_COUNT];
    wgr_handle_t sprites[SPRITE_COUNT]; /* lit billboards: one material, the same lights */
    wgr_handle_t sprite_material;
    wgr_handle_t sun;
    wgr_handle_t lamp;
    wgr_handle_t lamp_marker;
    wgr_handle_t spot;
    float time;
} g;

static void on_mesh_loaded(const char *path, void *user)
{
    wgr_handle_t mesh = wgr_mesh_create(path);
    (void)user;
    for (int i = 0; i < MODEL_COUNT; i++) {
        wgr_model_set_mesh(g.models[i], mesh);
    }
    wgr_mesh_release(mesh); /* the models hold their own references */
}

static void on_sprite_texture(const char *path, void *user)
{
    wgr_handle_t texture = wgr_texture_create(path);
    (void)user;
    wgr_texture_set_sampling(texture, WGR_TEXTURE_WRAP_CLAMP, WGR_TEXTURE_WRAP_CLAMP, WGR_TEXTURE_FILTER_NEAREST);
    for (int i = 0; i < SPRITE_COUNT; i++) wgr_sprite3d_set_texture(g.sprites[i], texture);
    wgr_texture_release(texture); /* the sprites hold their own references */
}

static void on_sprite_normal_map(const char *path, void *user)
{
    wgr_handle_t texture = wgr_texture_create(path);
    (void)user;
    wgr_material_set_texture(g.sprite_material, "normal_texture", texture);
    wgr_texture_release(texture);
}

static void on_failed(const char *path, void *user)
{
    (void)user;
    wgr_logger_error("load failed: %s", path);
}

static void toggle(wgr_handle_t light)
{
    wgr_light_set_enabled(light, !wgr_light_is_enabled(light));
}

static void init(void *user_data)
{
    (void)user_data;
    wgr_asset_set_host(EXAMPLE_ASSET_BASE);
    g.bg = wgr_color_rgba(12, 13, 18, 255);
    g.grid = wgr_color_rgba(40, 42, 50, 255);
    g.camera = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE);
    wgr_camera3d_set_view(g.camera, 0, 4.5f, 10, 0, 1, 0, 0, 1, 0);
    g.scene = wgr_scene_create();
    wgr_scene_set_active_camera(g.scene, g.camera);
    wgr_scene_set_ambient(g.scene, wgr_color_rgba(90, 110, 160, 255), 0.05f);

    for (int i = 0; i < MODEL_COUNT; i++) {
        g.models[i] = wgr_model_create(0);
        wgr_model_set_transform(g.models[i], -4.0f + 2.0f * (float)i, 0, (i % 2) ? -0.8f : 0.8f, 0, 0, 0, 1, 1, 1);
        wgr_model_set_animation(g.models[i], 3);
        wgr_model_set_animation_loop(g.models[i], true);
        wgr_scene_add(g.scene, g.models[i], 0);
    }

    g.sun = wgr_light_create(WGR_LIGHT_DIRECTIONAL);
    wgr_light_set_direction(g.sun, -0.4f, -1.0f, -0.6f);
    wgr_light_set_color(g.sun, wgr_color_rgba(255, 210, 160, 255));
    wgr_light_set_intensity(g.sun, 1.1f);
    wgr_scene_add(g.scene, g.sun, 0);

    g.lamp = wgr_light_create(WGR_LIGHT_POINT);
    wgr_light_set_color(g.lamp, wgr_color_rgba(60, 220, 255, 255));
    wgr_light_set_intensity(g.lamp, 20.0f);
    wgr_light_set_range(g.lamp, 5.0f);
    wgr_scene_add(g.scene, g.lamp, 0);
    g.lamp_marker = wgr_shape3d_create();
    wgr_shape3d_set_sphere(g.lamp_marker, 0.12f);
    wgr_shape3d_set_color(g.lamp_marker, wgr_color_rgba(60, 220, 255, 255));
    wgr_scene_add(g.scene, g.lamp_marker, 0);

    g.spot = wgr_light_create(WGR_LIGHT_SPOT);
    wgr_light_set_position(g.spot, 0, 6, 2);
    wgr_light_set_spot_cone(g.spot, 0.14f, 0.28f); /* radians: about 8 and 16 degrees */
    wgr_light_set_intensity(g.spot, 125.0f);
    wgr_scene_add(g.scene, g.spot, 0);

    /* lit billboards: a built-in material, so the scene's lights reach them */
    g.sprite_material = wgr_material_create(WGR_MATERIAL_PBR);
    wgr_material_set_float(g.sprite_material, "metallic", 0.0f);
    wgr_material_set_float(g.sprite_material, "roughness", 0.55f);
    /* cells sit side by side in the sheet: clamp, so none reaches into the next */
    wgr_material_set_texture_sampling(g.sprite_material, "normal_texture", WGR_TEXTURE_WRAP_CLAMP,
                                      WGR_TEXTURE_WRAP_CLAMP, WGR_TEXTURE_FILTER_LINEAR);
    for (int i = 0; i < SPRITE_COUNT; i++) {
        g.sprites[i] = wgr_sprite3d_create(0);
        const float *cell = SPRITE_CELLS[i];
        wgr_sprite3d_set_transform(g.sprites[i], -3.0f + 2.0f * (float)i, 0.2f, -2.5f, 0, 0, 0, 1, 1, 1);
        wgr_sprite3d_set_source(g.sprites[i], cell[0], cell[1], cell[2], cell[3]);
        wgr_sprite3d_set_extent(g.sprites[i], 1.6f, cell[4]);
        wgr_sprite3d_set_pivot(g.sprites[i], 0.5f, 1.0f); /* standing on their bottom edge */
        wgr_sprite3d_set_alpha_mode(g.sprites[i], WGR_ALPHA_MASK, 0.5f);
        wgr_sprite3d_set_material(g.sprites[i], g.sprite_material);
        wgr_scene_add(g.scene, g.sprites[i], 0);
    }
    wgr_material_release(g.sprite_material); /* the sprites hold it */

    wgr_asset_add_task(wgr_asset_ensure_async(MODEL_PATH, NULL, WGR_ASSET_NONE), on_mesh_loaded, on_failed, NULL);
    wgr_asset_add_task(wgr_asset_ensure_async(SPRITE_PATH, NULL, WGR_ASSET_NONE), on_sprite_texture, on_failed, NULL);
    wgr_asset_add_task(wgr_asset_ensure_async(NORMAL_PATH, NULL, WGR_ASSET_NONE), on_sprite_normal_map, on_failed, NULL);
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    (void)tick_fraction;
    (void)user_data;
    wgr_keyboard_state_t kb = wgr_input_get_keyboard_state();
    char line[128];

    if (kb.keys[WGR_KEY_1] == WGR_BUTTON_PRESSED) toggle(g.sun);
    if (kb.keys[WGR_KEY_2] == WGR_BUTTON_PRESSED) toggle(g.lamp);
    if (kb.keys[WGR_KEY_3] == WGR_BUTTON_PRESSED) toggle(g.spot);
    if (kb.keys[WGR_KEY_ESCAPE] == WGR_BUTTON_PRESSED) wgr_request_quit();

    g.time += dt;
    for (int i = 0; i < MODEL_COUNT; i++) {
        wgr_model_animate(g.models[i], dt);
    }

    /* point light orbits through the row; spot sweeps left and right */
    float lx = sinf(g.time * 0.6f) * 5.0f, lz = cosf(g.time * 0.6f) * 2.0f;
    wgr_light_set_position(g.lamp, lx, 1.2f, lz);
    wgr_shape3d_set_transform(g.lamp_marker, lx, 1.2f, lz, 0, 0, 0, 1, 1, 1);
    wgr_shape3d_set_visible(g.lamp_marker, wgr_light_is_enabled(g.lamp));
    wgr_light_set_direction(g.spot, sinf(g.time * 0.8f) * 0.7f, -1.0f, -0.3f);

    wgr_render_begin_frame();
    wgr_render_clear_background(g.bg);
    wgr_render_begin_mode_3d();
    wgr_shape3d_draw_grid(20, 1.0f, g.grid);
    wgr_render_end_mode_3d();
    wgr_scene_draw(g.scene);

    wgr_text_draw("libwgrender lights: directional, point, spot", 12, 12, 20, WGR_COLOR_RAYWHITE);
    snprintf(line, sizeof(line), "[1] sun %s   [2] point light %s   [3] spotlight %s",
             wgr_light_is_enabled(g.sun) ? "on " : "off", wgr_light_is_enabled(g.lamp) ? "on " : "off",
             wgr_light_is_enabled(g.spot) ? "on " : "off");
    wgr_text_draw(line, 12, 40, 16, WGR_COLOR_LIGHTGRAY);
    wgr_text_draw_fps(12, 64);
    wgr_render_end_frame();
}

int main(void)
{
    wgr_init_values(1000, 600, "libwgrender lights", WGR_WINDOW_FLAG_MSAA_4X_HINT | WGR_WINDOW_FLAG_WINDOW_RESIZABLE);
    wgr_set_init(init, NULL);
    wgr_set_frame(frame, NULL);
    return wgr_run();
}
