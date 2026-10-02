/* libwgrender sprite3d example — a textured billboard in a scene.
 *
 * A texture loads on create: wgr_texture_create(path) -> wgr_sprite3d_create(texture)
 * -> wgr_scene_add, all in init. The texture comes back PENDING and the sprite draws
 * nothing until it is READY, a frame or so later (longer on the web, where it may
 * download). Nothing waits for it: the status only says what to print. The sprite
 * bobs and faces the camera. */
#include <math.h>
#include <stddef.h>

#include "wgr.h"
#include "shared/example_assets.h"

#define LOGO_PATH "sprites/logo/wg-logo-bw-alpha.png"

static wgr_handle_t g_scene;
static wgr_handle_t g_camera;
static wgr_color_t g_bg;
static wgr_handle_t g_logo; /* the texture, kept to read its status */
static wgr_handle_t g_sprite;

static void on_init(void *user_data)
{
    wgr_asset_set_host(EXAMPLE_ASSET_BASE);
    wgr_asset_set_manifest(EXAMPLE_ASSET_MANIFEST);
    (void)user_data;
    g_bg = wgr_color_rgba(20, 22, 30, 255);
    g_camera = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE);
    wgr_camera3d_set_view(g_camera, 12.0f, 7.0f, 12.0f, 0.0f, 2.5f, 0.0f, 0.0f, 1.0f, 0.0f);
    g_scene = wgr_scene_create();
    wgr_scene_set_active_camera(g_scene, g_camera);

    /* a couple of ground shapes for depth reference */
    wgr_handle_t pedestal = wgr_shape3d_create();
    wgr_shape3d_set_cube(pedestal, 3.0f, 0.5f, 3.0f);
    wgr_shape3d_set_transform(pedestal, 0.0f, 0.25f, 0.0f, 0, 0, 0, 1, 1, 1);
    wgr_shape3d_set_color(pedestal, WGR_COLOR_DARKGRAY);
    wgr_scene_add(g_scene, pedestal, 0);

    g_logo = wgr_texture_create(LOGO_PATH); /* PENDING: loads over the next frames */
    g_sprite = wgr_sprite3d_create(g_logo);  /* fine at once: drawn when the texture is READY */
    wgr_sprite3d_set_size(g_sprite, 6.0f);
    wgr_sprite3d_set_facing(g_sprite, WGR_SPRITE3D_FACING_CAMERA);
    wgr_scene_add(g_scene, g_sprite, 1);
    wgr_debug_enable_fps(12, 10, 16);
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    (void)user_data;
    float t = (float)wgr_get_time();

    wgr_camera3d_set_view(g_camera, cosf(t * 0.3f) * 13.0f, 7.0f, sinf(t * 0.3f) * 13.0f,
                         0.0f, 2.5f, 0.0f, 0.0f, 1.0f, 0.0f);

    const float y = 3.5f + sinf(t * 1.5f) * 0.8f; /* bob */
    wgr_sprite3d_set_transform(g_sprite, 0.0f, y, 0.0f, 0, 0, 0, 1, 1, 1);

    wgr_render_begin_frame();
    wgr_render_clear_background(g_bg);

    wgr_render_begin_mode_3d();
    wgr_shape3d_draw_grid(20, 1.0f, WGR_COLOR_DARKGRAY);
    wgr_render_end_mode_3d();

    wgr_scene_draw(g_scene);

    wgr_text_draw("libwgrender + sokol — sprite3d", 12, 36, 24, WGR_COLOR_RAYWHITE);
    switch (wgr_resource_get_status(g_logo)) {
        case WGR_RESOURCE_PENDING: wgr_text_draw("loading logo...", 12, 70, 16, WGR_COLOR_LIGHTGRAY); break;
        case WGR_RESOURCE_FAILED: wgr_text_draw("logo failed to load (see the log)", 12, 70, 16, WGR_COLOR_RED); break;
        default: wgr_text_draw("logo: texture_create -> sprite3d_create", 12, 70, 16, WGR_COLOR_LIGHTGRAY); break;
    }

    wgr_render_end_frame();

#ifndef __EMSCRIPTEN__ /* on the web Escape is the browser's (it leaves fullscreen), and a page has nothing to quit to */
    wgr_keyboard_state_t kb = wgr_input_get_keyboard_state();
    if (kb.keys[WGR_KEY_ESCAPE] == WGR_BUTTON_PRESSED) {
        wgr_request_quit();
    }
#endif
}

int main(void)
{
    wgr_init_values(900, 700, "libwgrender sprite3d", WGR_WINDOW_FLAG_MSAA_4X_HINT | WGR_WINDOW_FLAG_WINDOW_RESIZABLE);
    wgr_set_init(on_init, NULL);
    wgr_set_frame(frame, NULL);
    return wgr_run();
}
