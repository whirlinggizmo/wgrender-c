/* libsk 3D example — camera3d, depth-tested 3D primitives, 2D overlay.
 *
 * The camera orbits the origin (driven through the public camera API) to show
 * the 3D render mode; shapes are immediate-mode sokol_gl. */
#include <math.h>
#include <stddef.h>

#include "sk.h"

static sk_handle_t g_camera;
static sk_handle_t g_bg;

static void on_init(void *user_data)
{
    (void)user_data;
    g_camera = sk_camera3d_create(SK_CAMERA3D_PERSPECTIVE); /* default fov: pi/4 */
    sk_camera3d_set_view(g_camera, 14.0f, 8.0f, 14.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    sk_camera3d_set_active(g_camera);
    g_bg = sk_color_create(28, 28, 38, 255);
    sk_debug_enable_fps(12, 10, 16);
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    (void)user_data;

    /* orbit the camera around the origin */
    float t = (float)sk_get_time();
    float r = 16.0f;
    float cam_x = cosf(t * 0.4f) * r;
    float cam_z = sinf(t * 0.4f) * r;
    sk_camera3d_set_view(g_camera, cam_x, 9.0f, cam_z, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);

    sk_render_begin();
    sk_render_clear_background(g_bg);

    /* ---- 3D ---- */
    sk_render_begin_mode_3d();
    sk_shape_draw_grid(20, 1.0f, SK_COLOR_DARKGRAY);

    /* axes */
    sk_shape_draw_line_3d(0, 0, 0, 5, 0, 0, SK_COLOR_RED);
    sk_shape_draw_line_3d(0, 0, 0, 0, 5, 0, SK_COLOR_GREEN);
    sk_shape_draw_line_3d(0, 0, 0, 0, 0, 5, SK_COLOR_BLUE);

    sk_shape_draw_cube(0.0f, 1.0f, 0.0f, 2.0f, 2.0f, 2.0f, SK_COLOR_SKYBLUE);
    sk_shape_draw_cube_wires(0.0f, 1.0f, 0.0f, 2.02f, 2.02f, 2.02f, SK_COLOR_DARKBLUE);
    sk_shape_draw_sphere(5.0f, 1.5f, 0.0f, 1.5f, SK_COLOR_GOLD);
    sk_shape_draw_sphere(-5.0f, 1.5f, 0.0f, 1.5f, SK_COLOR_MAROON);
    sk_render_end_mode_3d();

    /* ---- 2D overlay ---- */
    sk_text_draw("libsk + sokol — 3D", 12, 36, 24, SK_COLOR_RAYWHITE);
    sk_text_draw("orbiting camera3d, depth-tested sokol_gl", 12, 70, 16,
                 SK_COLOR_LIGHTGRAY);

    sk_render_end();

    sk_keyboard_state_t kb = sk_input_get_keyboard_state();
    if (kb.keys[SK_KEY_ESCAPE] == SK_BUTTON_PRESSED) {
        sk_request_quit();
    }
}

int main(void)
{
    sk_init_values(900, 700, "libsk hello3d", SK_WINDOW_FLAG_MSAA_4X_HINT);
    sk_set_init(on_init, NULL);
    sk_set_frame(frame, NULL);
    return sk_run();
}
