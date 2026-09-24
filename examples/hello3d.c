/* libwgrender 3D example — camera3d, depth-tested 3D primitives, 2D overlay.
 *
 * The camera orbits the origin (driven through the public camera API) to show
 * the 3D render mode; shapes are immediate-mode sokol_gl. */
#include <math.h>
#include <stddef.h>

#include "wgr.h"

static wgr_handle_t g_camera;
static wgr_color_t g_bg;

static void on_init(void *user_data)
{
    (void)user_data;
    g_camera = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE); /* default fov: pi/4 */
    wgr_camera3d_set_view(g_camera, 14.0f, 8.0f, 14.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    wgr_camera3d_set_active(g_camera);
    g_bg = wgr_color_rgba(28, 28, 38, 255);
    wgr_debug_enable_fps(12, 10, 16);
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    (void)user_data;

    /* orbit the camera around the origin */
    float t = (float)wgr_get_time();
    float r = 16.0f;
    float cam_x = cosf(t * 0.4f) * r;
    float cam_z = sinf(t * 0.4f) * r;
    wgr_camera3d_set_view(g_camera, cam_x, 9.0f, cam_z, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);

    wgr_render_begin_frame();
    wgr_render_clear_background(g_bg);

    /* ---- 3D ---- */
    wgr_render_begin_mode_3d();
    wgr_shape3d_draw_grid(20, 1.0f, WGR_COLOR_DARKGRAY);

    /* axes */
    wgr_shape3d_draw_line(0, 0, 0, 5, 0, 0, WGR_COLOR_RED);
    wgr_shape3d_draw_line(0, 0, 0, 0, 5, 0, WGR_COLOR_GREEN);
    wgr_shape3d_draw_line(0, 0, 0, 0, 0, 5, WGR_COLOR_BLUE);

    wgr_shape3d_draw_cube(0.0f, 1.0f, 0.0f, 2.0f, 2.0f, 2.0f, WGR_COLOR_SKYBLUE);
    wgr_shape3d_draw_cube_wires(0.0f, 1.0f, 0.0f, 2.02f, 2.02f, 2.02f, WGR_COLOR_DARKBLUE);
    wgr_shape3d_draw_sphere(5.0f, 1.5f, 0.0f, 1.5f, WGR_COLOR_GOLD);
    wgr_shape3d_draw_sphere(-5.0f, 1.5f, 0.0f, 1.5f, WGR_COLOR_MAROON);
    wgr_render_end_mode_3d();

    /* ---- 2D overlay ---- */
    wgr_text_draw("libwgrender + sokol — 3D", 12, 36, 24, WGR_COLOR_RAYWHITE);
    wgr_text_draw("orbiting camera3d, depth-tested sokol_gl", 12, 70, 16,
                 WGR_COLOR_LIGHTGRAY);

    wgr_render_end_frame();

    wgr_keyboard_state_t kb = wgr_input_get_keyboard_state();
    if (kb.keys[WGR_KEY_ESCAPE] == WGR_BUTTON_PRESSED) {
        wgr_request_quit();
    }
}

int main(void)
{
    wgr_init_values(900, 700, "libwgrender hello3d", WGR_WINDOW_FLAG_MSAA_4X_HINT | WGR_WINDOW_FLAG_WINDOW_RESIZABLE);
    wgr_set_init(on_init, NULL);
    wgr_set_frame(frame, NULL);
    return wgr_run();
}
