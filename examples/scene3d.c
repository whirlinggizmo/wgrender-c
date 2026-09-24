/* libwgrender scene example — retained shapes in a scene, drawn via wgr_scene_draw().
 *
 * Click shapes to select them (wgr_scene_pick). The selected shape turns white. */
#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "wgr.h"

static wgr_handle_t g_scene;
static wgr_handle_t g_camera;
static wgr_color_t g_bg;
static wgr_handle_t g_spinner;
static wgr_handle_t g_selected;
static wgr_handle_t g_ring[8];
static wgr_handle_t g_sphere;

static void on_init(void *user_data)
{
    (void)user_data;

    g_bg = wgr_color_rgba(24, 26, 34, 255);
    g_camera = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE);
    wgr_camera3d_set_view(g_camera, 16.0f, 11.0f, 16.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);

    g_scene = wgr_scene_create();
    wgr_scene_set_active_camera(g_scene, g_camera);

    for (int i = 0; i < 8; i++) {
        float a = (float)(i * 6.2831853 / 8.0);
        wgr_handle_t cube = wgr_shape3d_create();
        wgr_shape3d_set_cube(cube, 1.5f, 1.5f, 1.5f);
        wgr_shape3d_set_transform(cube, cosf(a) * 6.0f, 0.75f, sinf(a) * 6.0f,
                               0.0f, a, 0.0f, 1.0f, 1.0f, 1.0f);
        wgr_shape3d_set_color(cube, i % 2 ? WGR_COLOR_SKYBLUE : WGR_COLOR_ORANGE);
        wgr_scene_add(g_scene, cube, 0);
        g_ring[i] = cube;
    }

    g_sphere = wgr_shape3d_create();
    wgr_shape3d_set_sphere(g_sphere, 1.5f);
    wgr_shape3d_set_transform(g_sphere, 0.0f, 2.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    wgr_shape3d_set_color(g_sphere, WGR_COLOR_GOLD);
    wgr_scene_add(g_scene, g_sphere, 0);

    g_spinner = wgr_shape3d_create();
    wgr_shape3d_set_cube(g_spinner, 2.0f, 2.0f, 2.0f);
    wgr_shape3d_set_color(g_spinner, WGR_COLOR_LIME);
    wgr_scene_add(g_scene, g_spinner, 1);

    wgr_debug_enable_fps(12, 10, 16);
}

static wgr_handle_t default_color_for(wgr_handle_t shape)
{
    int i;

    if (shape == g_spinner) {
        return WGR_COLOR_LIME;
    }
    if (shape == g_sphere) {
        return WGR_COLOR_GOLD;
    }
    for (i = 0; i < 8; i++) {
        if (g_ring[i] == shape) {
            return (i % 2) ? WGR_COLOR_SKYBLUE : WGR_COLOR_ORANGE;
        }
    }
    return WGR_COLOR_RAYWHITE;
}

static void update_selection(wgr_handle_t hit)
{
    if (hit == g_selected) {
        return;
    }

    if (g_selected != 0) {
        wgr_shape3d_set_color(g_selected, default_color_for(g_selected));
    }

    g_selected = hit;
    if (g_selected != 0) {
        wgr_shape3d_set_color(g_selected, WGR_COLOR_RAYWHITE);
    }
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    (void)user_data;

    float t = (float)wgr_get_time();
    wgr_mouse_state_t mouse = wgr_input_get_mouse_state();
    char status[96];

    wgr_camera3d_set_view(g_camera, cosf(t * 0.35f) * 18.0f, 11.0f, sinf(t * 0.35f) * 18.0f,
                         0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);

    wgr_shape3d_set_transform(g_spinner, 0.0f, 5.0f, 0.0f, t * 1.3f, t * 0.9f, 0.0f,
                           1.0f, 1.0f, 1.0f);

    if (mouse.left == WGR_BUTTON_PRESSED) {
        wgr_pick_result_t pick = wgr_scene_pick(g_scene, 0, mouse.x, mouse.y);
        update_selection(pick.hit ? pick.handle : 0);
    }

    wgr_render_begin_frame();
    wgr_render_clear_background(g_bg);

    wgr_render_begin_mode_3d();
    wgr_shape3d_draw_grid(24, 1.0f, WGR_COLOR_DARKGRAY);
    wgr_render_end_mode_3d();

    wgr_scene_draw(g_scene);

    wgr_text_draw("libwgrender + sokol — scene pick", 12, 36, 24, WGR_COLOR_RAYWHITE);
    wgr_text_draw("click a shape to select it", 12, 70, 16, WGR_COLOR_LIGHTGRAY);

    if (g_selected != 0) {
        snprintf(status, sizeof(status), "selected handle: %u", (unsigned int)g_selected);
    } else {
        snprintf(status, sizeof(status), "selected: none");
    }
    wgr_text_draw(status, 12, 94, 16, WGR_COLOR_LIGHTGRAY);

    wgr_render_end_frame();

    if (wgr_input_get_key(WGR_KEY_ESCAPE) == WGR_BUTTON_PRESSED) {
        wgr_request_quit();
    }
}

int main(void)
{
    wgr_init_values(900, 700, "libwgrender scene3d", WGR_WINDOW_FLAG_MSAA_4X_HINT | WGR_WINDOW_FLAG_WINDOW_RESIZABLE);
    wgr_set_init(on_init, NULL);
    wgr_set_frame(frame, NULL);
    return wgr_run();
}
