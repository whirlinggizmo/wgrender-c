/* libwgrender render target example — drawing into textures.
 *
 *   - "pixel view": the scene drawn into a 160x100 texture and shown 4x larger
 *     with nearest filtering (a low-resolution pixel-art look)
 *   - "minimap": the same scene from a top-down orthographic camera, in a 256x256
 *     texture
 *   - "label": text in two fonts drawn into a 256x128 texture, used as the
 *     base color texture of the spinning sphere's material (and shown on its own)
 * Each frame draws the label first, so the scene views that use it show this
 * frame's label. Keys: ESC quit. */
#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "example_assets.h"
#include "wgr.h"

#define GUMSHOE_PATH "models/gumshoe/gumshoe.glb"
#define SPHERE_PATH "models/sphere/sphere.glb"
#define FONT_PATH "fonts/Komika/KOMIKAH_.ttf"

enum { PIXEL_W = 160, PIXEL_H = 100, PIXEL_SCALE = 4, MINIMAP = 256, LABEL_W = 256, LABEL_H = 128 };

static struct {
    wgr_handle_t scene;
    wgr_handle_t camera;
    wgr_handle_t top_camera;
    wgr_color_t bg, label_bg, minimap_bg, frame_color;
    wgr_handle_t pixel_view, minimap, label; /* render target textures */
    wgr_handle_t font;
    wgr_handle_t gumshoe, globe, ground;
    float time;
} g;

static void on_gumshoe_loaded(const char *path, void *user)
{
    wgr_handle_t mesh = wgr_mesh_create(path);
    (void)user;
    wgr_model_set_mesh(g.gumshoe, mesh);
    wgr_mesh_release(mesh);
}

static void on_sphere_loaded(const char *path, void *user)
{
    wgr_handle_t mesh = wgr_mesh_create(path);
    (void)user;
    wgr_model_set_mesh(g.globe, mesh);
    wgr_model_set_mesh(g.ground, mesh);
    wgr_mesh_release(mesh);
}

static void on_font_loaded(const char *path, void *user)
{
    (void)user;
    g.font = wgr_font_create(path);
}

static void on_failed(const char *path, void *user)
{
    (void)user;
    wgr_logger_error("load failed: %s", path);
}

static wgr_handle_t create_model(float x, float y, float z, float scale_y, float scale, wgr_handle_t material)
{
    wgr_handle_t model = wgr_model_create(0);
    wgr_model_set_transform(model, x, y, z, 0, 0, 0, scale, scale_y, scale);
    if (material != 0) {
        wgr_model_set_material(model, 0, material);
        wgr_material_release(material); /* the model keeps its own reference */
    }
    wgr_scene_add(g.scene, model, 0);
    return model;
}

static void init(void *user_data)
{
    wgr_handle_t material, sun;

    (void)user_data;
    wgr_asset_set_host(EXAMPLE_ASSET_BASE);
    g.bg = wgr_color_rgba(24, 26, 34, 255);
    g.label_bg = wgr_color_rgba(30, 60, 140, 255);
    g.minimap_bg = wgr_color_rgba(12, 14, 18, 255);
    g.frame_color = wgr_color_rgba(90, 96, 110, 255);

    g.pixel_view = wgr_texture_create_target(PIXEL_W, PIXEL_H);
    wgr_texture_set_sampling(g.pixel_view, WGR_TEXTURE_WRAP_CLAMP, WGR_TEXTURE_WRAP_CLAMP, WGR_TEXTURE_FILTER_NEAREST);
    g.minimap = wgr_texture_create_target(MINIMAP, MINIMAP);
    g.label = wgr_texture_create_target(LABEL_W, LABEL_H);

    g.scene = wgr_scene_create();
    g.camera = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE);
    wgr_camera3d_set_view(g.camera, 0, 6.0f, 10.0f, 0, 0.8f, 0, 0, 1, 0);
    g.top_camera = wgr_camera3d_create(WGR_CAMERA3D_ORTHOGRAPHIC);
    wgr_camera3d_set_view(g.top_camera, 0, 12, 0, 0, 0, 0, 0, 0, -1); /* looking down, -z up the map */
    wgr_camera3d_set_ortho_height(g.top_camera, 9.0f);

    sun = wgr_light_create(WGR_LIGHT_DIRECTIONAL);
    wgr_light_set_direction(sun, -0.5f, -1.0f, -0.4f);
    wgr_light_set_intensity(sun, 3.0f);
    wgr_scene_add(g.scene, sun, 0);
    wgr_scene_set_ambient(g.scene, WGR_COLOR_WHITE, 0.25f);

    /* ground: a flattened sphere */
    material = wgr_material_create(WGR_MATERIAL_PBR);
    wgr_material_set_vec4(material, "base_color", 0.25f, 0.3f, 0.25f, 1.0f);
    wgr_material_set_float(material, "metallic", 0.0f);
    g.ground = create_model(0, -0.05f, 0, 0.1f, 8.0f, material);

    g.gumshoe = wgr_model_create(0);
    wgr_model_set_animation(g.gumshoe, 3);
    wgr_scene_add(g.scene, g.gumshoe, 0);

    /* the globe wears the label texture: drawn into each frame, used like any texture */
    material = wgr_material_create(WGR_MATERIAL_UNLIT);
    wgr_material_set_texture(material, "base_color_texture", g.label);
    wgr_material_set_vec2(material, "base_color_texture_scale", 2.0f, 1.0f); /* twice around */
    g.globe = create_model(2.2f, 1.2f, 0, 1.6f, 1.6f, material);

    wgr_asset_add_task(wgr_asset_ensure_async(GUMSHOE_PATH, NULL, WGR_ASSET_NONE), on_gumshoe_loaded, on_failed, NULL);
    wgr_asset_add_task(wgr_asset_ensure_async(SPHERE_PATH, NULL, WGR_ASSET_NONE), on_sphere_loaded, on_failed, NULL);
    wgr_asset_add_task(wgr_asset_ensure_async(FONT_PATH, NULL, WGR_ASSET_NONE), on_font_loaded, on_failed, NULL);
}

/* A texture with a 2px frame and a caption above it. */
static void draw_panel(wgr_handle_t texture, float x, float y, float w, float h, const char *caption)
{
    wgr_shape2d_draw_rectangle((int)x - 2, (int)y - 2, (int)w + 4, (int)h + 4, g.frame_color);
    wgr_texture_draw(texture, x, y, w, h, WGR_COLOR_WHITE);
    wgr_text_draw(caption, (int)x, (int)y - 20, 16, WGR_COLOR_LIGHTGRAY);
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    const float gx = cosf(g.time * 0.6f) * 2.5f, gz = sinf(g.time * 0.6f) * 2.5f;
    char line[64];

    (void)tick_fraction;
    (void)user_data;
    if (wgr_input_get_key(WGR_KEY_ESCAPE) == WGR_BUTTON_PRESSED) {
        wgr_request_quit();
    }
    g.time += dt;
    wgr_model_set_transform(g.gumshoe, gx, 0, gz, 0, -g.time * 0.6f, 0, 0.6f, 0.6f, 0.6f); /* walks in a circle */
    wgr_model_animate(g.gumshoe, dt);
    wgr_model_set_transform(g.globe, -2.2f, 1.2f, 0, 0, g.time * 0.8f, 0, 1.6f, 1.6f, 1.6f);

    wgr_render_begin_frame();

    /* 1. the label, first, so the views below use this frame's text */
    if (wgr_render_begin_texture(g.label)) {
        wgr_render_clear_background(g.label_bg);
        wgr_text_draw_ex(g.font, "libwgrender", 20, 14, 64, WGR_COLOR_RAYWHITE);
        snprintf(line, sizeof(line), "t = %.1f", g.time);
        wgr_text_draw(line, 24, 92, 16, WGR_COLOR_GOLD);
        wgr_render_end_texture();
    }

    /* 2. low-resolution view of the scene */
    if (wgr_render_begin_texture(g.pixel_view)) {
        wgr_render_clear_background(g.bg);
        wgr_scene_set_active_camera(g.scene, g.camera);
        wgr_scene_draw(g.scene);
        wgr_render_end_texture();
    }

    /* 3. minimap from above */
    if (wgr_render_begin_texture(g.minimap)) {
        wgr_render_clear_background(g.minimap_bg);
        wgr_scene_set_active_camera(g.scene, g.top_camera);
        wgr_scene_draw(g.scene);
        wgr_render_end_texture();
    }

    /* the screen */
    wgr_render_clear_background(g.bg);
    draw_panel(g.pixel_view, 24, 64, PIXEL_W * PIXEL_SCALE, PIXEL_H * PIXEL_SCALE, "pixel view (160x100, nearest)");
    draw_panel(g.minimap, 700, 64, MINIMAP, MINIMAP, "minimap (orthographic, from above)");
    draw_panel(g.label, 700, 380, LABEL_W, LABEL_H, "label (text drawn into a texture)");
    wgr_text_draw("libwgrender render targets: wgr_texture_create_target + wgr_render_begin_texture", 12, 12, 16,
                 WGR_COLOR_RAYWHITE);
    wgr_render_end_frame();
}

int main(void)
{
    wgr_init_values(1000, 560, "libwgrender render targets", WGR_WINDOW_FLAG_MSAA_4X_HINT | WGR_WINDOW_FLAG_WINDOW_RESIZABLE);
    wgr_set_init(init, NULL);
    wgr_set_frame(frame, NULL);
    return wgr_run();
}
