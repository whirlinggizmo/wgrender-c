/* libsk render target example — drawing into textures.
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
#include "sk.h"

#define GUMSHOE_PATH "models/gumshoe/gumshoe.glb"
#define SPHERE_PATH "models/sphere/sphere.glb"
#define FONT_PATH "fonts/Komika/KOMIKAH_.ttf"

enum { PIXEL_W = 160, PIXEL_H = 100, PIXEL_SCALE = 4, MINIMAP = 256, LABEL_W = 256, LABEL_H = 128 };

static struct {
    sk_handle_t scene;
    sk_handle_t camera;
    sk_handle_t top_camera;
    sk_color_t bg, label_bg, minimap_bg, frame_color;
    sk_handle_t pixel_view, minimap, label; /* render target textures */
    sk_handle_t font;
    sk_handle_t gumshoe, globe, ground;
    float time;
} g;

static void on_gumshoe_loaded(const char *path, void *user)
{
    sk_handle_t mesh = sk_mesh_create(path);
    (void)user;
    sk_model_set_mesh(g.gumshoe, mesh);
    sk_mesh_release(mesh);
}

static void on_sphere_loaded(const char *path, void *user)
{
    sk_handle_t mesh = sk_mesh_create(path);
    (void)user;
    sk_model_set_mesh(g.globe, mesh);
    sk_model_set_mesh(g.ground, mesh);
    sk_mesh_release(mesh);
}

static void on_font_loaded(const char *path, void *user)
{
    (void)user;
    g.font = sk_font_create(path);
}

static void on_failed(const char *path, void *user)
{
    (void)user;
    sk_logger_error("load failed: %s", path);
}

static sk_handle_t create_model(float x, float y, float z, float scale_y, float scale, sk_handle_t material)
{
    sk_handle_t model = sk_model_create(0);
    sk_model_set_transform(model, x, y, z, 0, 0, 0, scale, scale_y, scale);
    if (material != 0) {
        sk_model_set_material(model, 0, material);
        sk_material_release(material); /* the model keeps its own reference */
    }
    sk_scene_add(g.scene, model, 0);
    return model;
}

static void init(void *user_data)
{
    sk_handle_t material, sun;

    (void)user_data;
    sk_asset_set_host(EXAMPLE_ASSET_BASE);
    g.bg = sk_color_rgba(24, 26, 34, 255);
    g.label_bg = sk_color_rgba(30, 60, 140, 255);
    g.minimap_bg = sk_color_rgba(12, 14, 18, 255);
    g.frame_color = sk_color_rgba(90, 96, 110, 255);

    g.pixel_view = sk_texture_create_target(PIXEL_W, PIXEL_H);
    sk_texture_set_sampling(g.pixel_view, SK_TEXTURE_WRAP_CLAMP, SK_TEXTURE_WRAP_CLAMP, SK_TEXTURE_FILTER_NEAREST);
    g.minimap = sk_texture_create_target(MINIMAP, MINIMAP);
    g.label = sk_texture_create_target(LABEL_W, LABEL_H);

    g.scene = sk_scene_create();
    g.camera = sk_camera3d_create(SK_CAMERA3D_PERSPECTIVE);
    sk_camera3d_set_view(g.camera, 0, 6.0f, 10.0f, 0, 0.8f, 0, 0, 1, 0);
    g.top_camera = sk_camera3d_create(SK_CAMERA3D_ORTHOGRAPHIC);
    sk_camera3d_set_view(g.top_camera, 0, 12, 0, 0, 0, 0, 0, 0, -1); /* looking down, -z up the map */
    sk_camera3d_set_ortho_height(g.top_camera, 9.0f);

    sun = sk_light_create(SK_LIGHT_DIRECTIONAL);
    sk_light_set_direction(sun, -0.5f, -1.0f, -0.4f);
    sk_light_set_intensity(sun, 3.0f);
    sk_scene_add(g.scene, sun, 0);
    sk_scene_set_ambient(g.scene, SK_COLOR_WHITE, 0.25f);

    /* ground: a flattened sphere */
    material = sk_material_create(SK_MATERIAL_PBR);
    sk_material_set_vec4(material, "base_color", 0.25f, 0.3f, 0.25f, 1.0f);
    sk_material_set_float(material, "metallic", 0.0f);
    g.ground = create_model(0, -0.05f, 0, 0.1f, 8.0f, material);

    g.gumshoe = sk_model_create(0);
    sk_model_set_animation(g.gumshoe, 3);
    sk_scene_add(g.scene, g.gumshoe, 0);

    /* the globe wears the label texture: drawn into each frame, used like any texture */
    material = sk_material_create(SK_MATERIAL_UNLIT);
    sk_material_set_texture(material, "base_color_texture", g.label);
    sk_material_set_vec2(material, "base_color_texture_scale", 2.0f, 1.0f); /* twice around */
    g.globe = create_model(2.2f, 1.2f, 0, 1.6f, 1.6f, material);

    sk_asset_add_task(sk_asset_ensure_async(GUMSHOE_PATH, NULL, SK_ASSET_NONE), on_gumshoe_loaded, on_failed, NULL);
    sk_asset_add_task(sk_asset_ensure_async(SPHERE_PATH, NULL, SK_ASSET_NONE), on_sphere_loaded, on_failed, NULL);
    sk_asset_add_task(sk_asset_ensure_async(FONT_PATH, NULL, SK_ASSET_NONE), on_font_loaded, on_failed, NULL);
}

/* A texture with a 2px frame and a caption above it. */
static void draw_panel(sk_handle_t texture, float x, float y, float w, float h, const char *caption)
{
    sk_shape2d_draw_rectangle((int)x - 2, (int)y - 2, (int)w + 4, (int)h + 4, g.frame_color);
    sk_texture_draw(texture, x, y, w, h, SK_COLOR_WHITE);
    sk_text_draw(caption, (int)x, (int)y - 20, 16, SK_COLOR_LIGHTGRAY);
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    const float gx = cosf(g.time * 0.6f) * 2.5f, gz = sinf(g.time * 0.6f) * 2.5f;
    char line[64];

    (void)tick_fraction;
    (void)user_data;
    if (sk_input_get_keyboard_state().keys[SK_KEY_ESCAPE] == SK_BUTTON_PRESSED) {
        sk_request_quit();
    }
    g.time += dt;
    sk_model_set_transform(g.gumshoe, gx, 0, gz, 0, -g.time * 0.6f, 0, 0.6f, 0.6f, 0.6f); /* walks in a circle */
    sk_model_animate(g.gumshoe, dt);
    sk_model_set_transform(g.globe, -2.2f, 1.2f, 0, 0, g.time * 0.8f, 0, 1.6f, 1.6f, 1.6f);

    sk_render_begin();

    /* 1. the label, first, so the views below use this frame's text */
    if (sk_render_begin_texture(g.label)) {
        sk_render_clear_background(g.label_bg);
        sk_text_draw_ex(g.font, "libsk", 20, 14, 64, SK_COLOR_RAYWHITE);
        snprintf(line, sizeof(line), "t = %.1f", g.time);
        sk_text_draw(line, 24, 92, 16, SK_COLOR_GOLD);
        sk_render_end_texture();
    }

    /* 2. low-resolution view of the scene */
    if (sk_render_begin_texture(g.pixel_view)) {
        sk_render_clear_background(g.bg);
        sk_scene_set_active_camera(g.scene, g.camera);
        sk_scene_draw(g.scene);
        sk_render_end_texture();
    }

    /* 3. minimap from above */
    if (sk_render_begin_texture(g.minimap)) {
        sk_render_clear_background(g.minimap_bg);
        sk_scene_set_active_camera(g.scene, g.top_camera);
        sk_scene_draw(g.scene);
        sk_render_end_texture();
    }

    /* the screen */
    sk_render_clear_background(g.bg);
    draw_panel(g.pixel_view, 24, 64, PIXEL_W * PIXEL_SCALE, PIXEL_H * PIXEL_SCALE, "pixel view (160x100, nearest)");
    draw_panel(g.minimap, 700, 64, MINIMAP, MINIMAP, "minimap (orthographic, from above)");
    draw_panel(g.label, 700, 380, LABEL_W, LABEL_H, "label (text drawn into a texture)");
    sk_text_draw("libsk render targets: sk_texture_create_target + sk_render_begin_texture", 12, 12, 16,
                 SK_COLOR_RAYWHITE);
    sk_render_end();
}

int main(void)
{
    sk_init_values(1000, 560, "libsk render targets", SK_WINDOW_FLAG_MSAA_4X_HINT | SK_WINDOW_FLAG_WINDOW_RESIZABLE);
    sk_set_init(init, NULL);
    sk_set_frame(frame, NULL);
    return sk_run();
}
